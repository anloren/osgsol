#ifndef EARTH_SCIENCE_PREVIEW_LAYER_H
#define EARTH_SCIENCE_PREVIEW_LAYER_H

#include <atomic>
#include <cstdint>
#include <osg/Group>

namespace earthscience { class SciencePreviewRuntime; }

// Dedicated AlphaEarth preview overlay. It is independent from the globe TMS,
// elevation, 3D Tiles, camera, and photo paths: a completed science artifact is
// materialized as its own curved ECEF mesh during scene update traversal.
class SciencePreviewLayer : public osg::Group
{
public:
    explicit SciencePreviewLayer(earthscience::SciencePreviewRuntime* runtime);

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
    void syncFromRuntime();

    earthscience::SciencePreviewRuntime* _runtime;
    osg::ref_ptr<osg::Group> _artifactRoot;
    std::atomic<bool> _visible;
    std::atomic<bool> _hasArtifact;
    std::atomic<bool> _removeRequested;
    std::atomic<std::uint64_t> _artifactGeneration;
};

#endif
