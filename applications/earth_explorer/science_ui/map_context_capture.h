#ifndef EARTH_SCIENCE_UI_MAP_CONTEXT_CAPTURE_H
#define EARTH_SCIENCE_UI_MAP_CONTEXT_CAPTURE_H

#include "science_target_overlay.h"

#include <osg/Texture2D>

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace osg { class Camera; }

struct ScienceCaptureSize
{
    int width = 0;
    int height = 0;
};

struct ScienceContextSnapshot
{
    osg::ref_ptr<osg::Texture2D> texture;
    std::vector<osg::Vec2f> requestedOverlay;
    std::vector<osg::Vec2f> actualOverlay;
    std::string imagePath;
    std::uint64_t capturedFrame = 0;
    int width = 0;
    int height = 0;
};

struct ScienceLiveTargetProjection
{
    std::vector<osg::Vec2f> requestedOverlay;
    std::vector<osg::Vec2f> actualOverlay;
    osg::Vec2f labelAnchor;
    std::string label;
    bool visible = false;
};

ScienceCaptureSize scienceContextCaptureSize(int sourceWidth,
                                             int sourceHeight);
bool scienceCaptureHasVisualContent(
    const std::vector<unsigned char>& bottomUpRgba);
std::vector<osg::Vec2f> normalizeScienceOverlay(
    const std::vector<ScienceOverlayPath>& paths,
    float viewportWidth, float viewportHeight);

class MapContextCapture
{
public:
    explicit MapContextCapture(std::string outputDirectory = std::string());

    void request(const std::string& artifactId,
                 const ScienceTargetOverlayInput& target);
    void setLiveTarget(const ScienceTargetOverlayInput& target);
    void projectLiveTarget(const osg::Camera& sceneCamera);
    bool copyLiveProjection(ScienceLiveTargetProjection& output) const;
    bool captureBeforeUi(const osg::Camera& sceneCamera,
                         std::uint64_t frame);
    bool storeCapturedRgbaForTesting(
        const std::string& artifactId,
        const ScienceTargetOverlayInput& target,
        int sourceWidth, int sourceHeight,
        const std::vector<unsigned char>& bottomUpRgba,
        std::uint64_t frame,
        const ScienceTargetOverlayFrame* projected = nullptr);
    bool copy(const std::string& artifactId,
              ScienceContextSnapshot& output) const;
    void erase(const std::string& artifactId);
    std::size_t size() const;
    bool pending() const;

private:
    struct PendingRequest
    {
        std::string artifactId;
        ScienceTargetOverlayInput target;
    };

    bool store(const PendingRequest& request,
               int sourceWidth, int sourceHeight,
               const std::vector<unsigned char>& bottomUpRgba,
               std::uint64_t frame,
               const ScienceTargetOverlayFrame& projected);
    std::string pathFor(const std::string& artifactId) const;

    std::string _outputDirectory;
    mutable std::mutex _mutex;
    std::deque<PendingRequest> _pending;
    std::unordered_map<std::string, ScienceContextSnapshot> _snapshots;
    std::deque<std::string> _order;
    ScienceTargetOverlayInput _liveTarget;
    ScienceLiveTargetProjection _liveProjection;
};

#endif
