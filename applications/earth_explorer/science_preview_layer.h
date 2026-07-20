#ifndef EARTH_SCIENCE_PREVIEW_LAYER_H
#define EARTH_SCIENCE_PREVIEW_LAYER_H

#include "science_plugin_api.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <osg/Group>

namespace earthscience { class ScienceQueryService; }
namespace earthscience { struct ScienceArtifact; }

// Plugin-side display publisher. This node exists only to receive update
// traversal; it owns no drawable, texture, depth state, or render bin.
class SciencePreviewLayer : public osg::Group
{
public:
    explicit SciencePreviewLayer(earthscience::ScienceQueryService* service);

    void bindGeoRaster(const OsgSolGeoRasterBridgeV1* bridge);
    void setVisible(bool visible);
    bool isVisible() const;
    bool hasArtifact() const;
    std::uint64_t artifactGeneration() const;
    void removeArtifact();

protected:
    virtual ~SciencePreviewLayer();

private:
    class SyncCallback;
    friend class SyncCallback;
    void syncFromService();
    bool publishArtifact(const earthscience::ScienceArtifact& artifact);
    void clearHostOverlay();

    earthscience::ScienceQueryService* _service;
    mutable std::mutex _bridgeMutex;
    OsgSolGeoRasterBridgeV1 _bridge;
    std::atomic<bool> _visible;
    std::atomic<bool> _hasArtifact;
    std::atomic<bool> _removeRequested;
    std::atomic<bool> _clearRequested;
    std::atomic<bool> _republishRequested;
    std::atomic<std::uint64_t> _artifactGeneration;
    std::atomic<std::uint64_t> _suppressedGeneration;
    std::uint64_t _publishedArtifactGeneration;
    std::uint64_t _transportGeneration;
};

#endif
