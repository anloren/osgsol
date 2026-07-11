#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <string>

#include <osg/Geometry>
#include <osg/Image>
#include <osg/Math>
#include <osg/Texture2D>
#include <osgDB/Options>

#include <readerwriter/TileCallback.h>

#include "../applications/earth_explorer/hk_elevation_filter.h"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; std::abort(); } } while (0)

struct TmsTile
{
    int x;
    int y;
};

static TmsTile tmsTileForLonLat(double lonDegrees, double latDegrees, int z)
{
    const double n = static_cast<double>(1 << z);
    const double latRadians = osg::DegreesToRadians(latDegrees);
    const int x = static_cast<int>(std::floor((lonDegrees + 180.0) / 360.0 * n));
    const int yXyz = static_cast<int>(std::floor(
        (1.0 - std::log(std::tan(latRadians) + 1.0 / std::cos(latRadians)) /
         osg::PI) * 0.5 * n));
    return TmsTile{x, static_cast<int>(n) - 1 - yXyz};
}

static std::string readWholeFile(const std::string& path)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    CHECK(input.good());
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

static osg::Texture2D* constantElevation(float meters)
{
    osg::Image* image = new osg::Image;
    image->allocateImage(2, 2, 1, GL_RED, GL_FLOAT);
    float* values = reinterpret_cast<float*>(image->data());
    for (int i = 0; i < 4; ++i) values[i] = meters;
    return new osg::Texture2D(image);
}

static osg::Texture2D* gradientElevation()
{
    osg::Image* image = new osg::Image;
    image->allocateImage(65, 65, 1, GL_RED, GL_FLOAT);
    float* values = reinterpret_cast<float*>(image->data());
    for (int y = 0; y < 65; ++y)
        for (int x = 0; x < 65; ++x)
            values[x + y * 65] = static_cast<float>(x + 2 * y);
    return new osg::Texture2D(image);
}

static osg::Geometry* makeTile(int z)
{
    osg::ref_ptr<osgVerse::TileCallback> callback = new osgVerse::TileCallback(true);
    callback->setTileNumber(0, 0, z);
    callback->setFlatten(false);
    callback->setSkirtRatio(0.05f);
    callback->setElevationScale(2.0f);
    osg::Matrix matrix;
    osg::ref_ptr<osg::Texture2D> elevation = constantElevation(10.0f);
    return callback->createTileGeometry(
        matrix, elevation.get(),
        osg::Vec3d(0.30, 1.99, 0.0), osg::Vec3d(0.31, 2.00, 0.0),
        0.01, 0.01);
}

static osg::Geometry* makeAncestorSubtile(osgVerse::TileCallback* callback, int z,
                                          osg::Texture2D* elevation,
                                          const osg::Vec3d& tileMin,
                                          const osg::Vec3d& tileMax,
                                          const osg::Vec4& elevScaleBias)
{
    callback->setTileNumber(0, 0, z);
    callback->setFlatten(false);
    callback->setSkirtRatio(0.05f);
    osg::Matrix matrix;
    return callback->createTileGeometry(
        matrix, elevation, tileMin, tileMax,
        tileMax.x() - tileMin.x(), tileMax.y() - tileMin.y(), elevScaleBias);
}

static double checkSiblingEdge(int z, osg::Texture2D* elevation,
                               const osg::Vec3d& leftMin, const osg::Vec3d& leftMax,
                               const osg::Vec3d& rightMin, const osg::Vec3d& rightMax,
                               const osg::Vec4& leftBias, const osg::Vec4& rightBias)
{
    CHECK(leftMax.x() == rightMin.x());
    CHECK(leftMin.y() == rightMin.y());
    CHECK(leftMax.y() == rightMax.y());

    osg::ref_ptr<osgVerse::TileCallback> leftCallback = new osgVerse::TileCallback(true);
    osg::ref_ptr<osgVerse::TileCallback> rightCallback = new osgVerse::TileCallback(true);
    osg::ref_ptr<osg::Geometry> leftGeometry = makeAncestorSubtile(
        leftCallback.get(), z, elevation, leftMin, leftMax, leftBias);
    osg::ref_ptr<osg::Geometry> rightGeometry = makeAncestorSubtile(
        rightCallback.get(), z, elevation, rightMin, rightMax, rightBias);

    unsigned int leftRows = 0, leftColumns = 0, rightRows = 0, rightColumns = 0;
    CHECK(osgVerse::tileGeometryGridSize(leftGeometry.get(), leftRows, leftColumns));
    CHECK(osgVerse::tileGeometryGridSize(rightGeometry.get(), rightRows, rightColumns));
    CHECK(leftRows == 33u && leftColumns == 33u);
    CHECK(rightRows == 33u && rightColumns == 33u);

    const osg::Vec3Array* leftVertices =
        static_cast<const osg::Vec3Array*>(leftGeometry->getVertexArray());
    const osg::Vec3Array* rightVertices =
        static_cast<const osg::Vec3Array*>(rightGeometry->getVertexArray());
    CHECK(leftVertices && leftVertices->size() >= leftRows * leftColumns);
    CHECK(rightVertices && rightVertices->size() >= rightRows * rightColumns);

    const osg::Matrixd leftToWorld =
        osg::Matrixd::inverse(leftCallback->getTileWorldToLocalMatrix());
    const osg::Matrixd rightToWorld =
        osg::Matrixd::inverse(rightCallback->getTileWorldToLocalMatrix());
    double maximumDifference = 0.0;
    for (unsigned int row = 0; row < 33u; ++row)
    {
        const osg::Vec3d leftWorld =
            osg::Vec3d((*leftVertices)[32u + row * 33u]) * leftToWorld;
        const osg::Vec3d rightWorld =
            osg::Vec3d((*rightVertices)[row * 33u]) * rightToWorld;
        const double difference = (leftWorld - rightWorld).length();
        maximumDifference = std::max(maximumDifference, difference);
        CHECK(difference < 1e-3);
    }
    return maximumDifference;
}

int main(int, char**)
{
    const TmsTile coreTile = tmsTileForLonLat(114.17, 22.30, 15);
    float core = 40.0f;
    earthterrain::applyHongKongElevationFilter(
        &core, 1, 1, coreTile.x, coreTile.y, 15);
    CHECK(std::fabs(core) < 1e-6f);

    const TmsTile outsideTile = tmsTileForLonLat(0.0, 0.0, 15);
    float outside = 40.0f;
    earthterrain::applyHongKongElevationFilter(
        &outside, 1, 1, outsideTile.x, outsideTile.y, 15);
    CHECK(std::fabs(outside - 40.0f) < 1e-6f);

    const TmsTile metroTile = tmsTileForLonLat(114.00, 22.38, 15);
    float metro = 40.0f;
    earthterrain::applyHongKongElevationFilter(
        &metro, 1, 1, metroTile.x, metroTile.y, 15);
    CHECK(std::fabs(metro - 18.0f) < 1e-3f);

    const double featherLongitudes[] = {114.34, 114.35, 114.36, 114.37, 114.38};
    float featherSamples[5];
    bool foundIntermediateFeather = false;
    for (int i = 0; i < 5; ++i)
    {
        const TmsTile tile = tmsTileForLonLat(featherLongitudes[i], 22.38, 15);
        featherSamples[i] = 40.0f;
        earthterrain::applyHongKongElevationFilter(
            &featherSamples[i], 1, 1, tile.x, tile.y, 15);
        if (featherSamples[i] > 18.0f && featherSamples[i] < 40.0f)
            foundIntermediateFeather = true;
    }
    CHECK(std::fabs(featherSamples[0] - 18.0f) < 1e-3f);
    CHECK(std::fabs(featherSamples[4] - 40.0f) < 1e-6f);
    CHECK(foundIntermediateFeather);
    for (int i = 1; i < 5; ++i)
    {
        CHECK(featherSamples[i] >= featherSamples[i - 1]);
        CHECK(featherSamples[i] - featherSamples[i - 1] < 15.0f);
    }

    const TmsTile featherAncestorTile = tmsTileForLonLat(114.36, 22.38, 15);
    float featherAncestor = 40.0f;
    earthterrain::applyHongKongElevationFilter(
        &featherAncestor, 1, 1, featherAncestorTile.x, featherAncestorTile.y, 15);
    CHECK(featherAncestor > 18.0f && featherAncestor < 40.0f);
    const TmsTile featherDescendantTiles[] = {
        TmsTile{(featherAncestorTile.x << 2) + 3,
                (featherAncestorTile.y << 2) + 1},
        TmsTile{(featherAncestorTile.x << 4) + 15,
                (featherAncestorTile.y << 4) + 7}
    };
    const int descendantLevels[] = {17, 19};
    for (int i = 0; i < 2; ++i)
    {
        float featherDescendant = 40.0f;
        earthterrain::applyHongKongElevationFilter(
            &featherDescendant, 1, 1, featherDescendantTiles[i].x,
            featherDescendantTiles[i].y, descendantLevels[i]);
        CHECK(std::fabs(featherDescendant - featherAncestor) < 1e-6f);
    }

    const std::string mainSource = readWholeFile(
        std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/earth_main.cpp");
    const std::string filterSource = readWholeFile(
        std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/hk_elevation_filter.h");
    CHECK(mainSource.find("TileElevationScale=2.0") != std::string::npos);
    CHECK(mainSource.find("TileSkirtRatio=") != std::string::npos);
    CHECK(mainSource.find(
        "setPluginData(\"ElevationFilterFunction\", (void*)hkElevationFilter)") !=
        std::string::npos);
    CHECK(mainSource.find("x >> dz") != std::string::npos);
    CHECK(mainSource.find("y >> dz") != std::string::npos);
    CHECK(filterSource.find("if (z > 15)") != std::string::npos);
    CHECK(filterSource.find("x >> dz") != std::string::npos);
    CHECK(filterSource.find("y >> dz") != std::string::npos);

    CHECK(osgVerse::terrainGridSizeForLevel(0) == 17u);
    CHECK(osgVerse::terrainGridSizeForLevel(11) == 17u);
    CHECK(osgVerse::terrainGridSizeForLevel(12) == 33u);
    CHECK(osgVerse::terrainGridSizeForLevel(19) == 33u);
    CHECK(((osgVerse::terrainGridSizeForLevel(12) - 1u) &
           (osgVerse::terrainGridSizeForLevel(12) - 2u)) == 0u);

    osg::ref_ptr<osg::Geometry> z11 = makeTile(11);
    osg::ref_ptr<osg::Geometry> z12 = makeTile(12);
    unsigned int rows = 0, columns = 0;
    CHECK(osgVerse::tileGeometryGridSize(z11.get(), rows, columns));
    CHECK(rows == 17u && columns == 17u);
    CHECK(z11->getVertexArray()->getNumElements() == 17u * 17u + 4u * 17u);
    CHECK(osgVerse::tileGeometryGridSize(z12.get(), rows, columns));
    CHECK(rows == 33u && columns == 33u);
    CHECK(z12->getVertexArray()->getNumElements() == 33u * 33u + 4u * 33u);

    osg::ref_ptr<osg::Texture2D> gradient = gradientElevation();
    const double ancestorMinLon = 0.30;
    const double ancestorMaxLon = 0.31;
    const double ancestorMinLat = 1.99;
    const double ancestorMaxLat = 2.00;
    const double ancestorWidth = ancestorMaxLon - ancestorMinLon;
    const double ancestorHeight = ancestorMaxLat - ancestorMinLat;
    const double halfLon = ancestorMinLon + ancestorWidth * 0.5;
    const double halfLat = ancestorMinLat + ancestorHeight * 0.5;
    const double z16MaximumDifference = checkSiblingEdge(
        16, gradient.get(),
        osg::Vec3d(ancestorMinLon, ancestorMinLat, 0.0),
        osg::Vec3d(halfLon, halfLat, 0.0),
        osg::Vec3d(halfLon, ancestorMinLat, 0.0),
        osg::Vec3d(ancestorMaxLon, halfLat, 0.0),
        osg::Vec4(0.0f, 0.0f, 0.5f, 0.5f),
        osg::Vec4(0.5f, 0.0f, 0.5f, 0.5f));

    const double quarterLon = ancestorMinLon + ancestorWidth * 0.25;
    const double quarterLat = ancestorMinLat + ancestorHeight * 0.25;
    const double z17MaximumDifference = checkSiblingEdge(
        17, gradient.get(),
        osg::Vec3d(ancestorMinLon, ancestorMinLat, 0.0),
        osg::Vec3d(quarterLon, quarterLat, 0.0),
        osg::Vec3d(quarterLon, ancestorMinLat, 0.0),
        osg::Vec3d(halfLon, quarterLat, 0.0),
        osg::Vec4(0.0f, 0.0f, 0.25f, 0.25f),
        osg::Vec4(0.25f, 0.0f, 0.25f, 0.25f));

    std::cout << std::setprecision(9)
              << "[terrain_grid_tests] grid dimensions OK; z16 seam max "
              << z16MaximumDifference << " m; z17 seam max "
              << z17MaximumDifference << " m\n";
    return 0;
}
