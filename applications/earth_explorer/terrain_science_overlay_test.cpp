#include "terrain_science_overlay.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <osg/Image>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Uniform>

namespace
{
    void checkOrThrow(bool condition, const char* expression,
                      const char* file, int line)
    {
        if (condition) return;
        throw std::runtime_error(
            std::string("CHECK failed at ") + file + ":" +
            std::to_string(line) + ": " + expression);
    }

#define CHECK(x) checkOrThrow((x), #x, __FILE__, __LINE__)

    terrainoverlay::GeoRasterFrameView frameView(
        std::uint64_t generation, std::vector<unsigned char>& pixels)
    {
        terrainoverlay::GeoRasterFrameView frame;
        frame.generation = generation;
        frame.width = 2;
        frame.height = 2;
        frame.rowBytes = 8;
        frame.rgba = pixels.data();
        frame.bounds.west = 114.0;
        frame.bounds.south = 22.0;
        frame.bounds.east = 115.0;
        frame.bounds.north = 23.0;
        return frame;
    }

    osg::Texture2D* scienceTexture(osg::StateSet& state)
    {
        return dynamic_cast<osg::Texture2D*>(state.getTextureAttribute(
            8, osg::StateAttribute::TEXTURE));
    }

    int runTests()
    {
        terrainoverlay::TerrainScienceOverlay overlay;
        osg::ref_ptr<osg::StateSet> state = new osg::StateSet;
        CHECK(!overlay.rendererSupported());
        overlay.setRendererFragmentTextureUnits(8);
        CHECK(!overlay.rendererSupported());
        CHECK(overlay.rendererStatus() ==
              "terrain-integrated display unavailable: renderer exposes "
              "fewer than 9 fragment texture units");
        overlay.drainTo(*state);
        osg::Texture2D* texture = scienceTexture(*state);
        CHECK(texture && texture->getImage());
        CHECK(texture->getImage()->s() == 1 && texture->getImage()->t() == 1);
        CHECK(texture->getImage()->data()[3] == 0);

        std::vector<unsigned char> transparent(16, 0);
        terrainoverlay::GeoRasterFrameView invalid = frameView(1, transparent);
        std::string error;
        CHECK(!overlay.enqueueCopy(invalid, error));
        CHECK(error == "science raster contains no visible pixels");

        std::vector<unsigned char> pixels = {
            10, 20, 30, 255, 40, 50, 60, 128,
            70, 80, 90, 64, 100, 110, 120, 32};
        invalid = frameView(1, pixels);
        invalid.rowBytes = 7;
        CHECK(!overlay.enqueueCopy(invalid, error));
        CHECK(error == "science raster row stride must equal width * 4");
        invalid = frameView(1, pixels);
        invalid.bounds.east = invalid.bounds.west;
        CHECK(!overlay.enqueueCopy(invalid, error));
        CHECK(error == "display raster bounds must have positive area");

        terrainoverlay::GeoRasterFrameView valid = frameView(2, pixels);
        CHECK(overlay.enqueueCopy(valid, error));
        pixels[0] = 250;
        overlay.setVisible(true);
        overlay.setOpacity(0.65f);
        overlay.setRendererFragmentTextureUnits(16);
        overlay.drainTo(*state);
        CHECK(overlay.rendererSupported());
        CHECK(overlay.hasFrame());
        CHECK(overlay.appliedGeneration() == 2);
        texture = scienceTexture(*state);
        CHECK(texture && texture->getImage());
        CHECK(texture->getImage()->s() == 2 && texture->getImage()->t() == 2);
        CHECK(texture->getImage()->data()[0] == 10);

        bool visible = false;
        float opacity = 0.0f;
        osg::Vec4 bounds;
        CHECK(state->getUniform("ScienceOverlayVisible")->get(visible));
        CHECK(state->getUniform("ScienceOverlayOpacity")->get(opacity));
        CHECK(state->getUniform("ScienceOverlayBounds")->get(bounds));
        CHECK(visible);
        CHECK(std::fabs(opacity - 0.65f) < 1.0e-6f);
        CHECK(std::fabs(bounds.x() - 114.0f) < 1.0e-6f);
        CHECK(std::fabs(bounds.w() - 23.0f) < 1.0e-6f);

        terrainoverlay::GeoRasterFrameView stale = frameView(1, pixels);
        CHECK(!overlay.enqueueCopy(stale, error));
        CHECK(error == "science raster generation is stale");
        overlay.enqueueClear(3);
        stale = frameView(2, pixels);
        CHECK(!overlay.enqueueCopy(stale, error));
        overlay.drainTo(*state);
        CHECK(!overlay.hasFrame());
        CHECK(overlay.appliedGeneration() == 3);
        texture = scienceTexture(*state);
        CHECK(texture && texture->getImage());
        CHECK(texture->getImage()->s() == 1 && texture->getImage()->t() == 1);
        CHECK(texture->getImage()->data()[3] == 0);

        overlay.setVisible(false);
        overlay.setOpacity(3.0f);
        overlay.drainTo(*state);
        CHECK(!overlay.visible());
        CHECK(std::fabs(overlay.opacity() - 1.0f) < 1.0e-6f);
        CHECK(state->getUniform("ScienceOverlayVisible")->get(visible));
        CHECK(state->getUniform("ScienceOverlayOpacity")->get(opacity));
        CHECK(!visible);
        CHECK(std::fabs(opacity - 1.0f) < 1.0e-6f);
        return 0;
    }
}

int main(int, char**)
{
    try
    {
        return runTests();
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
