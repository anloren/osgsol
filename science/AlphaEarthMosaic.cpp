#include "AlphaEarthMosaic.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_string.h>
#include <gdal_priv.h>
#include <gdalwarper.h>
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

    struct WarpOptionsCloser
    {
        void operator()(GDALWarpOptions* options) const
        {
            if (options) GDALDestroyWarpOptions(options);
        }
    };

    using DatasetPtr = std::unique_ptr<GDALDataset, DatasetCloser>;
    using WarpOptionsPtr = std::unique_ptr<GDALWarpOptions, WarpOptionsCloser>;

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
    const std::string destinationProjection(destinationWkt);
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

        WarpOptionsPtr options(GDALCreateWarpOptions());
        options->nBandCount = static_cast<int>(sourceBands.size());
        options->panSrcBands = static_cast<int*>(
            CPLMalloc(sizeof(int) * sourceBands.size()));
        options->panDstBands = static_cast<int*>(
            CPLMalloc(sizeof(int) * sourceBands.size()));
        options->padfSrcNoDataReal = static_cast<double*>(
            CPLMalloc(sizeof(double) * sourceBands.size()));
        options->padfDstNoDataReal = static_cast<double*>(
            CPLMalloc(sizeof(double) * sourceBands.size()));
        for (std::size_t index = 0; index < sourceBands.size(); ++index)
        {
            options->panSrcBands[index] = sourceBands[index];
            options->panDstBands[index] = static_cast<int>(index + 1);
            options->padfSrcNoDataReal[index] = -128.0;
            options->padfDstNoDataReal[index] = -128.0;
        }
        options->papszWarpOptions = CSLSetNameValue(
            options->papszWarpOptions, "UNIFIED_SRC_NODATA", "YES");
        if (GDALReprojectImage(
                source.get(), sourceWkt, destination.get(),
                destinationProjection.c_str(), GRA_NearestNeighbour,
                64.0 * 1024.0 * 1024.0, 0.0, GDALDummyProgress,
                nullptr, options.get()) != CE_None)
        {
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
