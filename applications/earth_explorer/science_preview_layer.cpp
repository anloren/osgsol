#include "science_preview_layer.h"

#include <array>
#include <cstring>
#include <limits>

#include <ScienceQueryService.h>
#include <osg/NodeCallback>
#include <osg/Notify>
#include <osg/observer_ptr>

class SciencePreviewLayer::SyncCallback : public osg::NodeCallback
{
public:
    explicit SyncCallback(SciencePreviewLayer* layer) : _layer(layer) {}

    void operator()(osg::Node* node, osg::NodeVisitor* visitor) override
    {
        SciencePreviewLayer* layer = _layer.get();
        if (layer) layer->syncFromService();
        traverse(node, visitor);
    }

private:
    osg::observer_ptr<SciencePreviewLayer> _layer;
};

SciencePreviewLayer::SciencePreviewLayer(
    earthscience::ScienceQueryService* service)
    : _service(service), _bridge(), _visible(false), _hasArtifact(false),
      _removeRequested(false), _clearRequested(false),
      _republishRequested(false), _artifactGeneration(0),
      _suppressedGeneration(0), _publishedArtifactGeneration(0),
      _transportGeneration(0)
{
    std::memset(&_bridge, 0, sizeof(_bridge));
    setName("ScienceEarthRasterPublisher");
    setUpdateCallback(new SyncCallback(this));
    setDataVariance(osg::Object::DYNAMIC);
}

SciencePreviewLayer::~SciencePreviewLayer()
{
    clearHostOverlay();
}

void SciencePreviewLayer::bindGeoRaster(
    const OsgSolGeoRasterBridgeV1* bridge)
{
    std::lock_guard<std::mutex> lock(_bridgeMutex);
    std::memset(&_bridge, 0, sizeof(_bridge));
    if (!bridge || bridge->structSize < sizeof(OsgSolGeoRasterBridgeV1) ||
        !bridge->publishCopy || !bridge->clear)
        return;
    _bridge = *bridge;
    _republishRequested.store(true, std::memory_order_release);
}

void SciencePreviewLayer::setVisible(bool visible)
{
    const bool previous = _visible.exchange(visible, std::memory_order_acq_rel);
    if (previous == visible) return;
    if (visible)
        _republishRequested.store(true, std::memory_order_release);
    else
        _clearRequested.store(true, std::memory_order_release);
}

bool SciencePreviewLayer::isVisible() const
{
    return _visible.load(std::memory_order_acquire);
}

bool SciencePreviewLayer::hasArtifact() const
{
    return _hasArtifact.load(std::memory_order_acquire);
}

std::uint64_t SciencePreviewLayer::artifactGeneration() const
{
    return _artifactGeneration.load(std::memory_order_acquire);
}

void SciencePreviewLayer::removeArtifact()
{
    _removeRequested.store(true, std::memory_order_release);
}

void SciencePreviewLayer::clearHostOverlay()
{
    OsgSolGeoRasterBridgeV1 bridge = {};
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        bridge = _bridge;
    }
    if (bridge.clear)
        bridge.clear(++_transportGeneration, bridge.userData);
}

bool SciencePreviewLayer::publishArtifact(
    const earthscience::ScienceArtifact& artifact)
{
    const earthscience::ScienceRasterPayload& raster = artifact.raster;
    if (!raster.rgba || raster.width <= 0 || raster.height <= 0)
        return false;
    const std::size_t width = static_cast<std::size_t>(raster.width);
    const std::size_t height = static_cast<std::size_t>(raster.height);
    if (width > std::numeric_limits<std::size_t>::max() / 4u)
        return false;
    const std::size_t rowBytes = width * 4u;
    if (height > std::numeric_limits<std::size_t>::max() / rowBytes)
        return false;
    const std::size_t expectedBytes = rowBytes * height;
    if (raster.rgba->size() != expectedBytes) return false;

    OsgSolGeoRasterBridgeV1 bridge = {};
    {
        std::lock_guard<std::mutex> lock(_bridgeMutex);
        bridge = _bridge;
    }
    if (!bridge.publishCopy) return false;

    const OsgSolGeoRasterFrameV1 frame = {
        sizeof(OsgSolGeoRasterFrameV1),
        ++_transportGeneration,
        raster.width,
        raster.height,
        static_cast<std::uint64_t>(rowBytes),
        raster.rgba->data(),
        raster.bounds.west,
        raster.bounds.south,
        raster.bounds.east,
        raster.bounds.north,
    };
    std::array<char, 512> error = {};
    if (!bridge.publishCopy(
            &frame, bridge.userData, error.data(), error.size()))
    {
        OSG_WARN << "ScienceEarth terrain publication rejected: "
                 << (error[0] ? error.data() : "unknown host error")
                 << std::endl;
        return false;
    }
    return true;
}

void SciencePreviewLayer::syncFromService()
{
    if (_removeRequested.exchange(false, std::memory_order_acq_rel))
    {
        _suppressedGeneration.store(
            artifactGeneration(), std::memory_order_release);
        clearHostOverlay();
        _artifactGeneration.store(0, std::memory_order_release);
        _publishedArtifactGeneration = 0;
        _hasArtifact.store(false, std::memory_order_release);
    }
    if (_clearRequested.exchange(false, std::memory_order_acq_rel))
        clearHostOverlay();
    if (!isVisible() || !_service) return;

    const earthscience::ScienceJobSnapshot snapshot = _service->snapshot();
    if (!snapshot.displayArtifact || snapshot.displayArtifact->generation == 0 ||
        snapshot.displayArtifact->generation ==
            _suppressedGeneration.load(std::memory_order_acquire))
        return;

    const bool republish = _republishRequested.exchange(
        false, std::memory_order_acq_rel);
    if (!republish && snapshot.displayArtifact->generation ==
                          _publishedArtifactGeneration)
        return;
    if (!publishArtifact(*snapshot.displayArtifact)) return;

    _publishedArtifactGeneration = snapshot.displayArtifact->generation;
    _artifactGeneration.store(snapshot.displayArtifact->generation,
                              std::memory_order_release);
    _suppressedGeneration.store(0, std::memory_order_release);
    _hasArtifact.store(true, std::memory_order_release);
}
