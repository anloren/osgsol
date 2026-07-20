#ifndef EARTH_TERRAIN_SCIENCE_OVERLAY_H
#define EARTH_TERRAIN_SCIENCE_OVERLAY_H

#include "terrain_science_overlay_math.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace osg { class StateSet; }

namespace terrainoverlay
{
    struct GeoRasterFrameView
    {
        std::uint64_t generation = 0;
        int width = 0;
        int height = 0;
        std::size_t rowBytes = 0;
        const unsigned char* rgba = nullptr;
        GeoRasterBounds bounds;
    };

    class TerrainScienceOverlay
    {
    public:
        TerrainScienceOverlay();
        ~TerrainScienceOverlay();

        TerrainScienceOverlay(const TerrainScienceOverlay&) = delete;
        TerrainScienceOverlay& operator=(const TerrainScienceOverlay&) = delete;

        bool enqueueCopy(const GeoRasterFrameView& frame, std::string& error);
        void enqueueClear(std::uint64_t generation);
        void setVisible(bool visible);
        bool visible() const;
        void setOpacity(float opacity);
        float opacity() const;
        void setRendererFragmentTextureUnits(int unitCount);
        void drainTo(osg::StateSet& globeStateSet);

        bool rendererSupported() const;
        std::string rendererStatus() const;
        std::uint64_t appliedGeneration() const;
        bool hasFrame() const;

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };
}

#endif
