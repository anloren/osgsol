#include "AlphaEarthMosaic.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>

namespace earthscience
{
namespace alphaearthdetail
{
namespace
{
    struct DatasetCloser
    {
        void operator()(GDALDataset* dataset) const
        {
            if (dataset) GDALClose(dataset);
        }
    };

    struct QuietGdalErrors
    {
        QuietGdalErrors() { CPLPushErrorHandler(CPLQuietErrorHandler); }
        ~QuietGdalErrors() { CPLPopErrorHandler(); }
    };

    using DatasetPtr = std::unique_ptr<GDALDataset, DatasetCloser>;

    struct WarpAppOptionsCloser
    {
        void operator()(GDALWarpAppOptions* options) const
        {
            if (options) GDALWarpAppOptionsFree(options);
        }
    };

    using WarpAppOptionsPtr =
        std::unique_ptr<GDALWarpAppOptions, WarpAppOptionsCloser>;

    int cancelProgress(double, const char*, void* userData)
    {
        const auto* cancelled = static_cast<
            const std::function<bool()>*>(userData);
        return cancelled && *cancelled && (*cancelled)() ? FALSE : TRUE;
    }

    bool validBounds(const ScienceWgs84Bounds& bounds)
    {
        return std::isfinite(bounds.west) && std::isfinite(bounds.south) &&
               std::isfinite(bounds.east) && std::isfinite(bounds.north) &&
               bounds.west < bounds.east && bounds.south < bounds.north;
    }

    bool overlaps(const ScienceWgs84Bounds& left,
                  const ScienceWgs84Bounds& right)
    {
        return left.west < right.east && left.east > right.west &&
               left.south < right.north && left.north > right.south;
    }

    std::string openPath(const std::string& pathOrUrl)
    {
        return pathOrUrl.rfind("https://", 0) == 0
            ? "/vsicurl/" + pathOrUrl : pathOrUrl;
    }

    void registerGdal()
    {
        static std::once_flag registration;
        std::call_once(registration, []()
        {
            GDALRegister_GTiff();
            GDALRegister_VRT();
            GDALRegister_MEM();
            CPLSetConfigOption("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR");
            CPLSetConfigOption(
                "CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".tiff,.tif");
            CPLSetConfigOption("GDAL_HTTP_VERSION", "2TLS");
            CPLSetConfigOption("GDAL_HTTP_MULTIPLEX", "YES");
            CPLSetConfigOption("GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES");
            CPLSetConfigOption("GDAL_HTTP_CONNECTTIMEOUT", "8");
            CPLSetConfigOption("GDAL_HTTP_TIMEOUT", "25");
        });
    }
}

bool alphaEarthAssetsCoverBounds(
    const std::vector<AlphaEarthAsset>& assets,
    const ScienceWgs84Bounds& bounds)
{
    if (!validBounds(bounds) || assets.empty()) return false;
    std::vector<double> xBreaks = {bounds.west, bounds.east};
    for (const AlphaEarthAsset& asset : assets)
    {
        if (!validBounds(asset.indexedBounds) ||
            !overlaps(asset.indexedBounds, bounds))
            continue;
        xBreaks.push_back(std::max(bounds.west, asset.indexedBounds.west));
        xBreaks.push_back(std::min(bounds.east, asset.indexedBounds.east));
    }
    std::sort(xBreaks.begin(), xBreaks.end());
    xBreaks.erase(std::unique(xBreaks.begin(), xBreaks.end()), xBreaks.end());
    constexpr double EPSILON = 1.0e-8;
    for (std::size_t slab = 1; slab < xBreaks.size(); ++slab)
    {
        if (xBreaks[slab] - xBreaks[slab - 1] <= EPSILON) continue;
        const double x = (xBreaks[slab] + xBreaks[slab - 1]) * 0.5;
        std::vector<std::pair<double, double>> intervals;
        for (const AlphaEarthAsset& asset : assets)
            if (x >= asset.indexedBounds.west - EPSILON &&
                x <= asset.indexedBounds.east + EPSILON)
                intervals.emplace_back(
                    std::max(bounds.south, asset.indexedBounds.south),
                    std::min(bounds.north, asset.indexedBounds.north));
        std::sort(intervals.begin(), intervals.end());
        double coveredThrough = bounds.south;
        for (const auto& interval : intervals)
        {
            if (interval.second <= coveredThrough + EPSILON) continue;
            if (interval.first > coveredThrough + EPSILON) return false;
            coveredThrough = interval.second;
            if (coveredThrough >= bounds.north - EPSILON) break;
        }
        if (coveredThrough < bounds.north - EPSILON) return false;
    }
    return true;
}

bool readAlphaEarthMosaic(
    const std::vector<AlphaEarthAsset>& assets,
    const ScienceWgs84Bounds& bounds,
    int width, int height,
    const std::vector<int>& sourceBands,
    const std::vector<std::string>& expectedDescriptions,
    const std::function<bool()>& cancelled,
    AlphaEarthMosaicRaster& output,
    std::string& error)
{
    output = AlphaEarthMosaicRaster();
    error.clear();
    if (!validBounds(bounds) || width <= 0 || height <= 0 ||
        sourceBands.empty() ||
        sourceBands.size() != expectedDescriptions.size() ||
        !alphaEarthAssetsCoverBounds(assets, bounds))
    {
        error = "AlphaEarth indexed tiles do not fully cover the requested geometry";
        return false;
    }
    registerGdal();
    GDALDriver* memoryDriver =
        GetGDALDriverManager()->GetDriverByName("MEM");
    if (!memoryDriver)
    {
        error = "GDAL memory driver is unavailable";
        return false;
    }
    DatasetPtr destination(memoryDriver->Create(
        "", width, height, static_cast<int>(sourceBands.size()),
        GDT_Int8, nullptr));
    if (!destination)
    {
        error = "AlphaEarth mosaic target could not be created";
        return false;
    }
    double transform[6] = {
        bounds.west, (bounds.east - bounds.west) / width, 0.0,
        bounds.north, 0.0, -(bounds.north - bounds.south) / height};
    if (destination->SetGeoTransform(transform) != CE_None)
    {
        error = "AlphaEarth mosaic geotransform could not be set";
        return false;
    }
    OGRSpatialReference wgs84;
    wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    char* destinationWkt = nullptr;
    if (wgs84.importFromEPSG(4326) != OGRERR_NONE ||
        wgs84.exportToWkt(&destinationWkt) != OGRERR_NONE ||
        destination->SetProjection(destinationWkt) != CE_None)
    {
        CPLFree(destinationWkt);
        error = "AlphaEarth mosaic WGS84 target could not be configured";
        return false;
    }
    CPLFree(destinationWkt);
    for (int band = 1; band <= destination->GetRasterCount(); ++band)
    {
        destination->GetRasterBand(band)->SetNoDataValue(-128.0);
        if (destination->GetRasterBand(band)->Fill(-128.0) != CE_None)
        {
            error = "AlphaEarth mosaic target could not be initialized";
            return false;
        }
    }

    for (const AlphaEarthAsset& asset : assets)
    {
        if (cancelled && cancelled())
        {
            error = "Cancelled";
            return false;
        }
        if (!overlaps(asset.indexedBounds, bounds)) continue;
        CPLErrorReset();
        DatasetPtr source(static_cast<GDALDataset*>(GDALOpenEx(
            openPath(asset.pathOrUrl).c_str(),
            GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr)));
        if (!source)
        {
            error = CPLGetLastErrorMsg();
            if (error.empty()) error = "AlphaEarth mosaic source could not be opened";
            else error = "AlphaEarth mosaic source open failed: " + error;
            return false;
        }
        const char* sourceWkt = source->GetProjectionRef();
        double sourceTransform[6] = {};
        if (!sourceWkt || !*sourceWkt ||
            source->GetGeoTransform(sourceTransform) != CE_None)
        {
            error = "AlphaEarth mosaic source georeference is missing";
            return false;
        }
        for (std::size_t index = 0; index < sourceBands.size(); ++index)
        {
            GDALRasterBand* band = source->GetRasterBand(sourceBands[index]);
            if (!band || band->GetRasterDataType() != GDT_Int8 ||
                std::string(band->GetDescription()) !=
                    expectedDescriptions[index])
            {
                error = "AlphaEarth mosaic band metadata mismatch at " +
                    expectedDescriptions[index];
                return false;
            }
        }

        char** arguments = nullptr;
        arguments = CSLAddString(arguments, "-r");
        arguments = CSLAddString(arguments, "near");
        arguments = CSLAddString(arguments, "-ovr");
        arguments = CSLAddString(arguments, "AUTO");
        arguments = CSLAddString(arguments, "-srcnodata");
        arguments = CSLAddString(arguments, "-128");
        arguments = CSLAddString(arguments, "-dstnodata");
        arguments = CSLAddString(arguments, "-128");
        arguments = CSLAddString(arguments, "-wo");
        arguments = CSLAddString(arguments, "UNIFIED_SRC_NODATA=YES");
        for (std::size_t index = 0; index < sourceBands.size(); ++index)
        {
            arguments = CSLAddString(arguments, "-srcband");
            arguments = CSLAddString(
                arguments, std::to_string(sourceBands[index]).c_str());
            arguments = CSLAddString(arguments, "-dstband");
            arguments = CSLAddString(
                arguments, std::to_string(index + 1).c_str());
        }
        WarpAppOptionsPtr options(GDALWarpAppOptionsNew(arguments, nullptr));
        CSLDestroy(arguments);
        if (!options)
        {
            error = "AlphaEarth mosaic warp options could not be created";
            return false;
        }
        GDALWarpAppOptionsSetProgress(
            options.get(), cancelProgress,
            const_cast<std::function<bool()>*>(&cancelled));
        GDALDatasetH sourceHandle = source.get();
        int usageError = FALSE;
        GDALDatasetH warpResult = nullptr;
        {
            QuietGdalErrors quietErrors;
            warpResult = GDALWarp(nullptr, destination.get(), 1,
                                  &sourceHandle, options.get(), &usageError);
        }
        if (!warpResult)
        {
            if (cancelled && cancelled())
            {
                error = "Cancelled";
                return false;
            }
            error = CPLGetLastErrorMsg();
            if (error.empty()) error = "AlphaEarth mosaic reprojection failed";
            else error = "AlphaEarth mosaic reprojection failed: " + error;
            return false;
        }
    }
    if (cancelled && cancelled())
    {
        error = "Cancelled";
        return false;
    }

    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    output.width = width;
    output.height = height;
    output.values.resize(pixelCount * sourceBands.size());
    std::vector<int> bandMap(sourceBands.size());
    for (std::size_t index = 0; index < bandMap.size(); ++index)
        bandMap[index] = static_cast<int>(index + 1);
    if (destination->RasterIO(
            GF_Read, 0, 0, width, height, output.values.data(),
            width, height, GDT_Int8, static_cast<int>(bandMap.size()),
            bandMap.data(), static_cast<GSpacing>(bandMap.size()),
            static_cast<GSpacing>(width * bandMap.size()), 1,
            nullptr) != CE_None)
    {
        error = "AlphaEarth mosaic target read failed";
        return false;
    }
    output.masks.resize(output.values.size());
    for (std::size_t index = 0; index < output.values.size(); ++index)
        output.masks[index] = output.values[index] == -128 ? 0 : 255;
    return true;
}
}
}
