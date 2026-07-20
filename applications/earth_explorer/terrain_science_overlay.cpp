#include "terrain_science_overlay.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#include <osg/Image>
#include <osg/StateSet>
#include <osg/Texture2D>
#include <osg/Uniform>

namespace terrainoverlay
{
    namespace
    {
        const int SCIENCE_TEXTURE_UNIT = 8;
        const std::size_t MAX_DISPLAY_BYTES = 256u * 1024u * 1024u;

        osg::Texture2D* createTransparentTexture()
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            std::memset(image->data(), 0, 4);
            image->setInternalTextureFormat(GL_RGBA8);
            image->setOrigin(osg::Image::BOTTOM_LEFT);

            osg::Texture2D* texture = new osg::Texture2D(image.get());
            texture->setResizeNonPowerOfTwoHint(false);
            texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
            texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
            texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            return texture;
        }

        osg::Texture2D* createFrameTexture(int width, int height,
                                           const unsigned char* rgba,
                                           std::size_t byteCount)
        {
            osg::ref_ptr<osg::Image> image = new osg::Image;
            image->allocateImage(width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            std::memcpy(image->data(), rgba, byteCount);
            image->setInternalTextureFormat(GL_RGBA8);
            image->setOrigin(osg::Image::BOTTOM_LEFT);

            osg::Texture2D* texture = new osg::Texture2D(image.get());
            texture->setResizeNonPowerOfTwoHint(false);
            texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
            texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
            texture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            texture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            return texture;
        }

        void setBoundsUniforms(osg::StateSet& state,
                               const GeoRasterBounds& bounds)
        {
            const double highWest = std::floor(bounds.west);
            const double highSouth = std::floor(bounds.south);
            state.getOrCreateUniform(
                "ScienceOverlayBounds", osg::Uniform::FLOAT_VEC4)->set(
                    osg::Vec4(static_cast<float>(bounds.west),
                              static_cast<float>(bounds.south),
                              static_cast<float>(bounds.east),
                              static_cast<float>(bounds.north)));
            state.getOrCreateUniform(
                "ScienceOverlayOriginHigh", osg::Uniform::FLOAT_VEC2)->set(
                    osg::Vec2(static_cast<float>(highWest),
                              static_cast<float>(highSouth)));
            state.getOrCreateUniform(
                "ScienceOverlayOriginLow", osg::Uniform::FLOAT_VEC2)->set(
                    osg::Vec2(static_cast<float>(bounds.west - highWest),
                              static_cast<float>(bounds.south - highSouth)));
            state.getOrCreateUniform(
                "ScienceOverlaySpan", osg::Uniform::FLOAT_VEC2)->set(
                    osg::Vec2(static_cast<float>(bounds.east - bounds.west),
                              static_cast<float>(bounds.north - bounds.south)));
        }
    }

    class TerrainScienceOverlay::Impl
    {
    public:
        enum PendingKind
        {
            PENDING_NONE,
            PENDING_FRAME,
            PENDING_CLEAR
        };

        struct Pending
        {
            PendingKind kind = PENDING_NONE;
            std::uint64_t generation = 0;
            int width = 0;
            int height = 0;
            GeoRasterBounds bounds;
            std::vector<unsigned char> rgba;
        };

        mutable std::mutex mutex;
        Pending pending;
        std::uint64_t latestEnqueuedGeneration = 0;
        bool requestedVisible = false;
        float requestedOpacity = 1.0f;
        int fragmentTextureUnits = -1;

        osg::ref_ptr<osg::Texture2D> transparentTexture;
        osg::ref_ptr<osg::Texture2D> frameTexture;
        std::atomic<std::uint64_t> currentGeneration{0};
        std::atomic<bool> currentHasFrame{false};
        GeoRasterBounds currentBounds;
    };

    TerrainScienceOverlay::TerrainScienceOverlay() : _impl(new Impl)
    {
    }

    TerrainScienceOverlay::~TerrainScienceOverlay() = default;

    bool TerrainScienceOverlay::enqueueCopy(const GeoRasterFrameView& frame,
                                            std::string& error)
    {
        error.clear();
        if (frame.generation == 0)
        {
            error = "science raster generation must be positive";
            return false;
        }
        if (frame.width <= 0 || frame.height <= 0)
        {
            error = "science raster dimensions must be positive";
            return false;
        }
        const std::size_t width = static_cast<std::size_t>(frame.width);
        const std::size_t height = static_cast<std::size_t>(frame.height);
        if (width > std::numeric_limits<std::size_t>::max() / 4u)
        {
            error = "science raster byte size overflows";
            return false;
        }
        const std::size_t expectedRowBytes = width * 4u;
        if (frame.rowBytes != expectedRowBytes)
        {
            error = "science raster row stride must equal width * 4";
            return false;
        }
        if (height > std::numeric_limits<std::size_t>::max() /
                         expectedRowBytes)
        {
            error = "science raster byte size overflows";
            return false;
        }
        const std::size_t byteCount = expectedRowBytes * height;
        if (byteCount > MAX_DISPLAY_BYTES)
        {
            error = "science raster exceeds the 256 MiB display limit";
            return false;
        }
        if (!frame.rgba)
        {
            error = "science raster pixels are missing";
            return false;
        }
        if (!validateGeoRasterBounds(frame.bounds, error)) return false;

        bool hasVisiblePixel = false;
        for (std::size_t index = 3; index < byteCount; index += 4)
        {
            if (frame.rgba[index] != 0)
            {
                hasVisiblePixel = true;
                break;
            }
        }
        if (!hasVisiblePixel)
        {
            error = "science raster contains no visible pixels";
            return false;
        }

        Impl::Pending pending;
        pending.kind = Impl::PENDING_FRAME;
        pending.generation = frame.generation;
        pending.width = frame.width;
        pending.height = frame.height;
        pending.bounds = frame.bounds;
        pending.rgba.assign(frame.rgba, frame.rgba + byteCount);

        std::lock_guard<std::mutex> lock(_impl->mutex);
        if (frame.generation <= _impl->latestEnqueuedGeneration)
        {
            error = "science raster generation is stale";
            return false;
        }
        _impl->latestEnqueuedGeneration = frame.generation;
        _impl->pending = std::move(pending);
        return true;
    }

    void TerrainScienceOverlay::enqueueClear(std::uint64_t generation)
    {
        if (generation == 0) return;
        std::lock_guard<std::mutex> lock(_impl->mutex);
        if (generation <= _impl->latestEnqueuedGeneration) return;
        _impl->latestEnqueuedGeneration = generation;
        _impl->pending = Impl::Pending();
        _impl->pending.kind = Impl::PENDING_CLEAR;
        _impl->pending.generation = generation;
    }

    void TerrainScienceOverlay::setVisible(bool visible)
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        _impl->requestedVisible = visible;
    }

    bool TerrainScienceOverlay::visible() const
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        return _impl->requestedVisible;
    }

    void TerrainScienceOverlay::setOpacity(float opacity)
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        _impl->requestedOpacity = std::max(0.0f, std::min(1.0f, opacity));
    }

    float TerrainScienceOverlay::opacity() const
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        return _impl->requestedOpacity;
    }

    void TerrainScienceOverlay::setRendererFragmentTextureUnits(int unitCount)
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        _impl->fragmentTextureUnits = unitCount;
    }

    bool TerrainScienceOverlay::rendererSupported() const
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        return _impl->fragmentTextureUnits >= 9;
    }

    std::string TerrainScienceOverlay::rendererStatus() const
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        if (_impl->fragmentTextureUnits < 0)
            return "terrain-integrated display capability has not been checked";
        if (_impl->fragmentTextureUnits < 9)
            return "terrain-integrated display unavailable: renderer exposes "
                   "fewer than 9 fragment texture units";
        return "terrain-integrated display ready";
    }

    void TerrainScienceOverlay::drainTo(osg::StateSet& globeStateSet)
    {
        Impl::Pending pending;
        bool supported = false;
        bool requestedVisible = false;
        float requestedOpacity = 1.0f;
        {
            std::lock_guard<std::mutex> lock(_impl->mutex);
            supported = _impl->fragmentTextureUnits >= 9;
            requestedVisible = _impl->requestedVisible;
            requestedOpacity = _impl->requestedOpacity;
            if (_impl->pending.kind == Impl::PENDING_CLEAR ||
                (supported && _impl->pending.kind == Impl::PENDING_FRAME))
            {
                pending = std::move(_impl->pending);
                _impl->pending = Impl::Pending();
            }
        }

        if (!_impl->transparentTexture)
            _impl->transparentTexture = createTransparentTexture();
        if (pending.kind == Impl::PENDING_CLEAR)
        {
            _impl->frameTexture = nullptr;
            _impl->currentHasFrame.store(false, std::memory_order_release);
            _impl->currentGeneration.store(
                pending.generation, std::memory_order_release);
        }
        else if (pending.kind == Impl::PENDING_FRAME)
        {
            _impl->frameTexture = createFrameTexture(
                pending.width, pending.height, pending.rgba.data(),
                pending.rgba.size());
            _impl->currentHasFrame.store(true, std::memory_order_release);
            _impl->currentGeneration.store(
                pending.generation, std::memory_order_release);
            _impl->currentBounds = pending.bounds;
        }

        const bool currentHasFrame = _impl->currentHasFrame.load(
            std::memory_order_acquire);
        osg::Texture2D* selected = supported && currentHasFrame
            ? _impl->frameTexture.get() : _impl->transparentTexture.get();
        globeStateSet.setTextureAttributeAndModes(
            SCIENCE_TEXTURE_UNIT, selected, osg::StateAttribute::ON);
        globeStateSet.getOrCreateUniform(
            "ScienceOverlaySampler", osg::Uniform::INT)->set(
                SCIENCE_TEXTURE_UNIT);
        globeStateSet.getOrCreateUniform(
            "ScienceOverlayVisible", osg::Uniform::BOOL)->set(
                supported && currentHasFrame && requestedVisible);
        globeStateSet.getOrCreateUniform(
            "ScienceOverlayOpacity", osg::Uniform::FLOAT)->set(
                requestedOpacity);
        globeStateSet.getOrCreateUniform(
            "ScienceOverlayOutlineVisible", osg::Uniform::BOOL)->set(
                supported && currentHasFrame && requestedVisible);
        if (currentHasFrame)
            setBoundsUniforms(globeStateSet, _impl->currentBounds);
        else
            setBoundsUniforms(globeStateSet, GeoRasterBounds{0.0, 0.0, 1.0, 1.0});
    }

    std::uint64_t TerrainScienceOverlay::appliedGeneration() const
    {
        return _impl->currentGeneration.load(std::memory_order_acquire);
    }

    bool TerrainScienceOverlay::hasFrame() const
    {
        return _impl->currentHasFrame.load(std::memory_order_acquire);
    }
}
