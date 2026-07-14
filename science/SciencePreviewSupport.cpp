#include "SciencePreviewSupport.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include <cpl_conv.h>
#include <gdal.h>
#include <ogr_spatialref.h>

namespace earthscience
{
namespace
{
    using TransformPtr = std::unique_ptr<OGRCoordinateTransformation,
                                         decltype(&OCTDestroyCoordinateTransformation)>;

    bool makeSpatialReferences(const char* sourceWkt,
                               OGRSpatialReference& source,
                               OGRSpatialReference& wgs84,
                               std::string& error)
    {
        if (!sourceWkt || !*sourceWkt ||
            source.SetFromUserInput(sourceWkt) != OGRERR_NONE)
        {
            error = "AlphaEarth source CRS is missing or invalid";
            return false;
        }
        source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (wgs84.importFromEPSG(4326) != OGRERR_NONE)
        {
            error = "WGS84 CRS could not be constructed";
            return false;
        }
        return true;
    }

    double pixelResolutionMeters(const OGRSpatialReference& source,
                                 const double geotransform[6])
    {
        if (!source.IsProjected()) return 0.0;
        const double unitMeters = source.GetLinearUnits();
        const double xResolution = std::hypot(geotransform[1], geotransform[4]) *
                                   unitMeters;
        const double yResolution = std::hypot(geotransform[2], geotransform[5]) *
                                   unitMeters;
        if (!std::isfinite(xResolution) || !std::isfinite(yResolution) ||
            xResolution <= 0.0 || yResolution <= 0.0)
            return 0.0;
        return std::max(xResolution, yResolution);
    }
}

unsigned char scienceDisplayValue(std::int8_t raw)
{
    const double normalized = std::abs(static_cast<double>(raw)) / 127.5;
    const double scientific = std::copysign(
        normalized * normalized, static_cast<double>(raw));
    const double display = std::clamp((scientific + 0.3) / 0.6, 0.0, 1.0);
    return static_cast<unsigned char>(std::lround(display * 255.0));
}

std::vector<unsigned char> composePreviewRgba(
    const std::vector<std::int8_t>& southUpRgb,
    const std::vector<unsigned char>& masks,
    int width, int height)
{
    if (width <= 0 || height <= 0 ||
        southUpRgb.size() != static_cast<std::size_t>(width * height * 3) ||
        masks.size() != southUpRgb.size())
        return {};

    std::vector<unsigned char> rgba(
        static_cast<std::size_t>(width * height * 4));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t source =
                static_cast<std::size_t>(y * width + x) * 3;
            const std::size_t destination =
                static_cast<std::size_t>(y * width + x) * 4;
            bool valid = true;
            for (int channel = 0; channel < 3; ++channel)
            {
                rgba[destination + channel] =
                    scienceDisplayValue(southUpRgb[source + channel]);
                valid = valid && masks[source + channel] != 0;
            }
            rgba[destination + 3] = valid ? 166 : 0;
        }
    }
    return rgba;
}

ScienceRasterWindow derivePreviewWindow(
    int rasterWidth, int rasterHeight, const double geotransform[6],
    const char* sourceWkt, double latitude, double longitude,
    double requestedSpanMeters, int outputSize, std::string& error)
{
    ScienceRasterWindow window;
    error.clear();
    if (rasterWidth <= 0 || rasterHeight <= 0 || !geotransform ||
        outputSize <= 0 || !std::isfinite(latitude) ||
        !std::isfinite(longitude))
    {
        error = "AlphaEarth preview window inputs are invalid";
        return window;
    }

    OGRSpatialReference source, wgs84;
    if (!makeSpatialReferences(sourceWkt, source, wgs84, error)) return window;
    TransformPtr toSource(
        OGRCreateCoordinateTransformation(&wgs84, &source),
        OCTDestroyCoordinateTransformation);
    if (!toSource)
    {
        error = "AlphaEarth WGS84-to-source transform could not be created";
        return window;
    }

    double projectedX = longitude;
    double projectedY = latitude;
    if (!toSource->Transform(1, &projectedX, &projectedY))
    {
        error = "AlphaEarth view center could not be projected";
        return window;
    }
    double inverse[6] = {};
    if (!GDALInvGeoTransform(geotransform, inverse))
    {
        error = "AlphaEarth source affine transform is not invertible";
        return window;
    }
    double pixelX = 0.0, pixelY = 0.0;
    GDALApplyGeoTransform(inverse, projectedX, projectedY, &pixelX, &pixelY);
    if (pixelX < 0.0 || pixelY < 0.0 || pixelX > rasterWidth ||
        pixelY > rasterHeight)
    {
        error = "AlphaEarth view center is outside the selected source tile";
        return window;
    }

    const double sourceResolution = pixelResolutionMeters(source, geotransform);
    if (sourceResolution <= 0.0)
    {
        error = "AlphaEarth source pixel resolution is unavailable";
        return window;
    }
    const int maximumWindow = std::min(rasterWidth, rasterHeight);
    int windowSize = maximumWindow;
    if (std::isfinite(requestedSpanMeters) && requestedSpanMeters > 0.0)
    {
        windowSize = static_cast<int>(std::lround(
            requestedSpanMeters / sourceResolution));
        windowSize = std::clamp(windowSize, outputSize, maximumWindow);
    }

    window.width = windowSize;
    window.height = windowSize;
    window.x = std::clamp(
        static_cast<int>(std::lround(pixelX - windowSize * 0.5)),
        0, rasterWidth - windowSize);
    window.y = std::clamp(
        static_cast<int>(std::lround(pixelY - windowSize * 0.5)),
        0, rasterHeight - windowSize);
    window.sourceResolutionMeters = sourceResolution;
    window.displayResolutionMeters =
        sourceResolution * windowSize / static_cast<double>(outputSize);
    return window;
}

ScienceGroundGrid buildPreviewGroundGrid(
    int rasterWidth, int rasterHeight, const double geotransform[6],
    const char* sourceWkt, int columns, int rows, std::string& error)
{
    ScienceGroundGrid grid;
    error.clear();
    if (rasterWidth <= 0 || rasterHeight <= 0 || !geotransform ||
        columns < 2 || rows < 2)
    {
        error = "AlphaEarth ground grid inputs are invalid";
        return grid;
    }

    OGRSpatialReference source, wgs84;
    if (!makeSpatialReferences(sourceWkt, source, wgs84, error)) return grid;
    TransformPtr toWgs84(
        OGRCreateCoordinateTransformation(&source, &wgs84),
        OCTDestroyCoordinateTransformation);
    if (!toWgs84)
    {
        error = "AlphaEarth source-to-WGS84 transform could not be created";
        return grid;
    }

    std::vector<double> longitudes(static_cast<std::size_t>(columns * rows));
    std::vector<double> latitudes(longitudes.size());
    for (int row = 0; row < rows; ++row)
    {
        const double pixelY = rasterHeight * row / static_cast<double>(rows - 1);
        for (int column = 0; column < columns; ++column)
        {
            const double pixelX =
                rasterWidth * column / static_cast<double>(columns - 1);
            const std::size_t index =
                static_cast<std::size_t>(row * columns + column);
            GDALApplyGeoTransform(geotransform, pixelX, pixelY,
                                  &longitudes[index], &latitudes[index]);
        }
    }
    if (!toWgs84->Transform(static_cast<int>(longitudes.size()),
                            longitudes.data(), latitudes.data()))
    {
        error = "AlphaEarth ground grid could not be transformed to WGS84";
        return grid;
    }

    grid.columns = columns;
    grid.rows = rows;
    grid.points.resize(longitudes.size());
    for (std::size_t index = 0; index < longitudes.size(); ++index)
    {
        if (!std::isfinite(longitudes[index]) || !std::isfinite(latitudes[index]) ||
            longitudes[index] < -180.0 || longitudes[index] > 180.0 ||
            latitudes[index] < -90.0 || latitudes[index] > 90.0)
        {
            error = "AlphaEarth ground grid contains an invalid WGS84 coordinate";
            return ScienceGroundGrid();
        }
        grid.points[index] = {longitudes[index], latitudes[index]};
    }
    return grid;
}
}
