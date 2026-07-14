#ifndef OSGSOL_SCIENCE_PREVIEW_SUPPORT_H
#define OSGSOL_SCIENCE_PREVIEW_SUPPORT_H

#include <cstdint>
#include <string>
#include <vector>

namespace earthscience
{
    struct ScienceGroundPoint
    {
        double longitude = 0.0;
        double latitude = 0.0;
    };

    struct ScienceGroundGrid
    {
        int columns = 0;
        int rows = 0;
        std::vector<ScienceGroundPoint> points;
    };

    struct ScienceRasterWindow
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        double sourceResolutionMeters = 0.0;
        double displayResolutionMeters = 0.0;
    };

    unsigned char scienceDisplayValue(std::int8_t raw);

    std::vector<unsigned char> composePreviewRgba(
        const std::vector<std::int8_t>& southUpRgb,
        const std::vector<unsigned char>& masks,
        int width, int height);

    ScienceRasterWindow derivePreviewWindow(
        int rasterWidth, int rasterHeight, const double geotransform[6],
        const char* sourceWkt, double latitude, double longitude,
        double requestedSpanMeters, int outputSize, std::string& error);

    ScienceGroundGrid buildPreviewGroundGrid(
        int rasterWidth, int rasterHeight, const double geotransform[6],
        const char* sourceWkt, int columns, int rows, std::string& error);
}

#endif
