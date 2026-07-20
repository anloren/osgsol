#ifndef EARTH_SCIENCE_PREVIEW_LAYER_H
#define EARTH_SCIENCE_PREVIEW_LAYER_H

#include "science_plugin_api.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <osg/Group>
#include <string>

namespace earthscience { class ScienceQueryService; }
namespace earthscience { struct ScienceArtifact; }

enum class SciencePreviewPublishState
{
    Idle,
    Waiting,
    Published,
    RendererUnavailable,
};

struct SciencePreviewPublishStatus
{
    SciencePreviewPublishState state = SciencePreviewPublishState::Idle;
    std::uint64_t artifactGeneration = 0;
    std::string message;
};

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
    SciencePreviewPublishStatus displayStatus() const;
    void removeArtifact();

protected:
    virtual ~SciencePreviewLayer();

private:
    class SyncCallback;
    friend class SyncCallback;
    void syncFromService();
    bool publishArtifact(const earthscience::ScienceArtifact& artifact);
    void clearHostOverlay();
    void setDisplayStatus(SciencePreviewPublishState state,
                          std::uint64_t generation,
                          const std::string& message);

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
    std::atomic<SciencePreviewPublishState> _publishState;
    std::uint64_t _statusGeneration;
    std::string _statusMessage;
    std::uint64_t _publishedArtifactGeneration;
    std::uint64_t _transportGeneration;
};

#endif
