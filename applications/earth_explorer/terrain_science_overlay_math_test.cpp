#include "terrain_science_overlay_math.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    constexpr double PI = 3.14159265358979323846;

    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    bool near(double left, double right, double tolerance = 1.0e-10)
    {
        return std::fabs(left - right) <= tolerance;
    }

    double latitudeToMercatorExtentY(double latitudeDegrees)
    {
        const double latitudeRadians = latitudeDegrees * PI / 180.0;
        return std::asinh(std::tan(latitudeRadians)) * 90.0 / PI;
    }

    void testBoundsValidation()
    {
        std::string error;
        require(terrainoverlay::validateGeoRasterBounds(
                    {114.0, 22.0, 115.0, 23.0}, error) && error.empty(),
                "valid WGS84 bounds were rejected");

        error.clear();
        require(!terrainoverlay::validateGeoRasterBounds(
                    {170.0, -10.0, -170.0, 10.0}, error) &&
                    error == "dateline-crossing display raster requires split frames",
                "dateline-crossing bounds were not rejected explicitly");

        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        const terrainoverlay::GeoRasterBounds invalid[] = {
            {nan, 0.0, 1.0, 1.0}, {0.0, -inf, 1.0, 1.0},
            {-181.0, 0.0, 1.0, 1.0}, {0.0, -91.0, 1.0, 1.0},
            {0.0, 0.0, 181.0, 1.0}, {0.0, 0.0, 1.0, 91.0},
            {1.0, 0.0, 1.0, 1.0}, {0.0, 1.0, 1.0, 1.0},
        };
        for (const auto& bounds : invalid)
        {
            error.clear();
            require(!terrainoverlay::validateGeoRasterBounds(bounds, error) &&
                        !error.empty(),
                    "invalid WGS84 bounds were accepted");
        }
    }

    void testMercatorConversionMatchesTerrain()
    {
        require(near(terrainoverlay::mercatorExtentYToLatitudeDegrees(0.0),
                     0.0),
                "equator conversion changed");
        require(near(terrainoverlay::mercatorExtentYToLatitudeDegrees(90.0),
                     85.0511287798066, 1.0e-9),
                "north Web Mercator limit changed");
        require(near(terrainoverlay::mercatorExtentYToLatitudeDegrees(-90.0),
                     -85.0511287798066, 1.0e-9),
                "south Web Mercator limit changed");

        const double locations[] = {22.3193, 35.6762, -33.8688, 35.3606};
        for (double latitude : locations)
        {
            const double y = latitudeToMercatorExtentY(latitude);
            require(near(
                        terrainoverlay::mercatorExtentYToLatitudeDegrees(y),
                        latitude, 1.0e-9),
                    "known-location Mercator round trip changed");
        }
    }

    void testTileCornersAndCenterMapToRasterUv()
    {
        const terrainoverlay::GeoRasterBounds raster =
            {114.0, 22.0, 115.0, 23.0};
        const terrainoverlay::TerrainTileMapBounds tile = {
            114.0, latitudeToMercatorExtentY(22.0),
            115.0, latitudeToMercatorExtentY(23.0), true};
        double u = -1.0, v = -1.0;
        require(terrainoverlay::terrainTileUvToRasterUv(
                    tile, raster, 0.0, 0.0, u, v) &&
                    near(u, 0.0) && near(v, 0.0),
                "southwest corner is not bottom-left texture origin");
        require(terrainoverlay::terrainTileUvToRasterUv(
                    tile, raster, 1.0, 0.0, u, v) &&
                    near(u, 1.0) && near(v, 0.0),
                "southeast corner is flipped");
        require(terrainoverlay::terrainTileUvToRasterUv(
                    tile, raster, 0.0, 1.0, u, v) &&
                    near(u, 0.0) && near(v, 1.0),
                "northwest corner is flipped");
        require(terrainoverlay::terrainTileUvToRasterUv(
                    tile, raster, 1.0, 1.0, u, v) &&
                    near(u, 1.0) && near(v, 1.0),
                "northeast corner is flipped");
        require(terrainoverlay::terrainTileUvToRasterUv(
                    tile, raster, 0.5, 0.5, u, v) &&
                    u > 0.49 && u < 0.51 && v > 0.49 && v < 0.51,
                "tile center did not remain inside raster center");
    }

    void testOutsideAndDirectLatitudeTiles()
    {
        const terrainoverlay::GeoRasterBounds raster =
            {114.2, 22.2, 114.8, 22.8};
        const terrainoverlay::TerrainTileMapBounds mercatorTile = {
            114.0, latitudeToMercatorExtentY(22.0),
            115.0, latitudeToMercatorExtentY(23.0), true};
        double u = 0.0, v = 0.0;
        require(!terrainoverlay::terrainTileUvToRasterUv(
                    mercatorTile, raster, 0.0, 0.0, u, v),
                "outside terrain fragment was accepted");
        require(terrainoverlay::terrainTileUvToRasterUv(
                    mercatorTile, raster, 0.5, 0.5, u, v) &&
                    u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0,
                "inside terrain fragment was rejected");

        const terrainoverlay::TerrainTileMapBounds directTile = {
            114.0, 22.0, 115.0, 23.0, false};
        const terrainoverlay::GeoRasterBounds full =
            {114.0, 22.0, 115.0, 23.0};
        require(terrainoverlay::terrainTileUvToRasterUv(
                    directTile, full, 0.25, 0.75, u, v) &&
                    near(u, 0.25) && near(v, 0.75),
                "non-Mercator latitude was transformed twice");
        require(!terrainoverlay::terrainTileUvToRasterUv(
                    directTile, full, -0.01, 0.5, u, v) &&
                    !terrainoverlay::terrainTileUvToRasterUv(
                        directTile, full, 0.5, 1.01, u, v),
                "invalid tile UV was accepted");
    }
}

int main()
{
    testBoundsValidation();
    testMercatorConversionMatchesTerrain();
    testTileCornersAndCenterMapToRasterUv();
    testOutsideAndDirectLatitudeTiles();
    std::cout << "[OK] terrain science geospatial mapping\n";
    return 0;
}
