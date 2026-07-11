#include <cmath>
#include <cstdlib>
#include <iostream>

#include <osg/Geometry>
#include <osg/Image>
#include <osg/Texture2D>
#include <osgDB/Options>

#include <readerwriter/TileCallback.h>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; std::abort(); } } while (0)

static osg::Texture2D* constantElevation(float meters)
{
    osg::Image* image = new osg::Image;
    image->allocateImage(2, 2, 1, GL_RED, GL_FLOAT);
    float* values = reinterpret_cast<float*>(image->data());
    for (int i = 0; i < 4; ++i) values[i] = meters;
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

int main(int, char**)
{
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

    std::cout << "[terrain_grid_tests] grid dimensions OK\n";
    return 0;
}
