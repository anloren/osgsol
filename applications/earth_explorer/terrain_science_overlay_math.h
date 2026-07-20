#ifndef OSGSOL_TERRAIN_SCIENCE_OVERLAY_MATH_H
#define OSGSOL_TERRAIN_SCIENCE_OVERLAY_MATH_H

#include <string>

namespace terrainoverlay
{
    struct GeoRasterBounds
    {
        double west = 0.0;
        double south = 0.0;
        double east = 0.0;
        double north = 0.0;
    };

    struct TerrainTileMapBounds
    {
        double westDegrees = 0.0;
        double southMapDegrees = 0.0;
        double eastDegrees = 0.0;
        double northMapDegrees = 0.0;
        bool usesWebMercator = true;
    };

    bool validateGeoRasterBounds(const GeoRasterBounds& value,
                                 std::string& error);

    double mercatorExtentYToLatitudeDegrees(double yDegrees);

    bool terrainTileUvToRasterUv(const TerrainTileMapBounds& tile,
                                 const GeoRasterBounds& raster,
                                 double tileU, double tileV,
                                 double& rasterU, double& rasterV);
}

#endif
