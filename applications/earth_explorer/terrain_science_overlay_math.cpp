#include "terrain_science_overlay_math.h"

#include <algorithm>
#include <cmath>

namespace
{
    constexpr double PI = 3.14159265358979323846;
    constexpr double EDGE_EPSILON = 1.0e-12;

    bool finite(double value)
    {
        return std::isfinite(value);
    }

    bool insideUnit(double value)
    {
        return value >= -EDGE_EPSILON && value <= 1.0 + EDGE_EPSILON;
    }

    double unitClamp(double value)
    {
        return std::max(0.0, std::min(1.0, value));
    }
}

namespace terrainoverlay
{
    bool validateGeoRasterBounds(const GeoRasterBounds& value,
                                 std::string& error)
    {
        if (!finite(value.west) || !finite(value.south) ||
            !finite(value.east) || !finite(value.north))
        {
            error = "display raster bounds must be finite";
            return false;
        }
        if (value.west < -180.0 || value.west > 180.0 ||
            value.east < -180.0 || value.east > 180.0 ||
            value.south < -90.0 || value.south > 90.0 ||
            value.north < -90.0 || value.north > 90.0)
        {
            error = "display raster bounds are outside WGS84 limits";
            return false;
        }
        if (value.west > value.east)
        {
            error = "dateline-crossing display raster requires split frames";
            return false;
        }
        if (value.west == value.east || value.south >= value.north)
        {
            error = "display raster bounds must have positive area";
            return false;
        }
        error.clear();
        return true;
    }

    double mercatorExtentYToLatitudeDegrees(double yDegrees)
    {
        const double mercatorRadians = 2.0 * yDegrees * PI / 180.0;
        return std::atan(std::sinh(mercatorRadians)) * 180.0 / PI;
    }

    bool terrainTileUvToRasterUv(const TerrainTileMapBounds& tile,
                                 const GeoRasterBounds& raster,
                                 double tileU, double tileV,
                                 double& rasterU, double& rasterV)
    {
        std::string error;
        if (!validateGeoRasterBounds(raster, error) ||
            !finite(tile.westDegrees) || !finite(tile.southMapDegrees) ||
            !finite(tile.eastDegrees) || !finite(tile.northMapDegrees) ||
            tile.westDegrees >= tile.eastDegrees ||
            tile.southMapDegrees >= tile.northMapDegrees ||
            !finite(tileU) || !finite(tileV) ||
            !insideUnit(tileU) || !insideUnit(tileV))
            return false;

        tileU = unitClamp(tileU);
        tileV = unitClamp(tileV);
        const double longitude = tile.westDegrees +
            (tile.eastDegrees - tile.westDegrees) * tileU;
        const double mapY = tile.southMapDegrees +
            (tile.northMapDegrees - tile.southMapDegrees) * tileV;
        const double latitude = tile.usesWebMercator
            ? mercatorExtentYToLatitudeDegrees(mapY) : mapY;
        rasterU = (longitude - raster.west) / (raster.east - raster.west);
        rasterV = (latitude - raster.south) / (raster.north - raster.south);
        if (!finite(rasterU) || !finite(rasterV) ||
            !insideUnit(rasterU) || !insideUnit(rasterV))
            return false;
        rasterU = unitClamp(rasterU);
        rasterV = unitClamp(rasterV);
        return true;
    }
}
