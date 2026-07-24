#include "map_context_capture.h"

#include <osg/Camera>
#include <osg/GL>
#include <osg/Image>
#include <osg/Viewport>
#include <osgDB/FileUtils>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace
{
constexpr int MAXIMUM_WIDTH = 512;
constexpr int MAXIMUM_HEIGHT = 288;
constexpr std::size_t MAXIMUM_SNAPSHOTS = 3;

#pragma pack(push, 1)
struct TgaHeader
{
    unsigned char idLength = 0;
    unsigned char colorMapType = 0;
    unsigned char dataType = 2;
    unsigned short colorMapOrigin = 0;
    unsigned short colorMapLength = 0;
    unsigned char colorMapDepth = 0;
    unsigned short xOrigin = 0;
    unsigned short yOrigin = 0;
    unsigned short width = 0;
    unsigned short height = 0;
    unsigned char bitsPerPixel = 32;
    unsigned char imageDescriptor = 8;
};
#pragma pack(pop)

std::vector<unsigned char> downsample(
    const std::vector<unsigned char>& source,
    int sourceWidth, int sourceHeight,
    int outputWidth, int outputHeight)
{
    std::vector<unsigned char> output(
        static_cast<std::size_t>(outputWidth) * outputHeight * 4, 0);
    for (int y = 0; y < outputHeight; ++y)
    {
        const int sourceY = std::min(sourceHeight - 1,
            static_cast<int>((static_cast<long long>(y) * sourceHeight) /
                             outputHeight));
        for (int x = 0; x < outputWidth; ++x)
        {
            const int sourceX = std::min(sourceWidth - 1,
                static_cast<int>((static_cast<long long>(x) * sourceWidth) /
                                 outputWidth));
            const std::size_t from =
                (static_cast<std::size_t>(sourceY) * sourceWidth + sourceX) * 4;
            const std::size_t to =
                (static_cast<std::size_t>(y) * outputWidth + x) * 4;
            std::memcpy(output.data() + to, source.data() + from, 4);
        }
    }
    return output;
}

bool writeTga(const std::string& path, int width, int height,
              const std::vector<unsigned char>& bottomUpRgba)
{
    if (path.empty()) return true;
    TgaHeader header;
    header.width = static_cast<unsigned short>(width);
    header.height = static_cast<unsigned short>(height);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const std::size_t index =
                (static_cast<std::size_t>(y) * width + x) * 4;
            const unsigned char bgra[4] = {
                bottomUpRgba[index + 2], bottomUpRgba[index + 1],
                bottomUpRgba[index], bottomUpRgba[index + 3]};
            output.write(reinterpret_cast<const char*>(bgra), 4);
        }
    return output.good();
}

std::string safeName(const std::string& value)
{
    std::string output;
    output.reserve(std::min<std::size_t>(value.size(), 80));
    for (char character : value)
    {
        if (output.size() >= 80) break;
        const bool safe = (character >= 'a' && character <= 'z') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= '0' && character <= '9') || character == '-' ||
            character == '_';
        output.push_back(safe ? character : '_');
    }
    return output.empty() ? "science-result" : output;
}
}

ScienceCaptureSize scienceContextCaptureSize(
    int sourceWidth, int sourceHeight)
{
    ScienceCaptureSize output;
    if (sourceWidth <= 0 || sourceHeight <= 0) return output;
    const double scale = std::min(
        1.0, std::min(static_cast<double>(MAXIMUM_WIDTH) / sourceWidth,
                      static_cast<double>(MAXIMUM_HEIGHT) / sourceHeight));
    output.width = std::max(1, static_cast<int>(std::floor(sourceWidth * scale)));
    output.height = std::max(1, static_cast<int>(std::floor(sourceHeight * scale)));
    return output;
}

bool scienceCaptureHasVisualContent(
    const std::vector<unsigned char>& bottomUpRgba)
{
    if (bottomUpRgba.size() < 8 || bottomUpRgba.size() % 4 != 0)
        return false;
    unsigned char minimum[3] = {255, 255, 255};
    unsigned char maximum[3] = {0, 0, 0};
    const std::size_t pixels = bottomUpRgba.size() / 4;
    const std::size_t sampleStride = std::max<std::size_t>(
        1, pixels / 4096);
    for (std::size_t pixel = 0; pixel < pixels; pixel += sampleStride)
    {
        const std::size_t offset = pixel * 4;
        for (std::size_t channel = 0; channel < 3; ++channel)
        {
            minimum[channel] = std::min(
                minimum[channel], bottomUpRgba[offset + channel]);
            maximum[channel] = std::max(
                maximum[channel], bottomUpRgba[offset + channel]);
        }
    }
    // A real map frame may be dark space, bright snow, or mostly ocean, but it
    // still contains texture/edges. Uniform white/black frames are capture
    // timing failures and must never be published as an analysis overview.
    return maximum[0] - minimum[0] >= 6 ||
        maximum[1] - minimum[1] >= 6 ||
        maximum[2] - minimum[2] >= 6;
}

std::vector<osg::Vec2f> normalizeScienceOverlay(
    const std::vector<ScienceOverlayPath>& paths,
    float viewportWidth, float viewportHeight)
{
    std::vector<osg::Vec2f> output;
    if (!(viewportWidth > 0.0f) || !(viewportHeight > 0.0f)) return output;
    for (const ScienceOverlayPath& path : paths)
        for (const ScienceOverlayVertex& vertex : path.vertices)
            output.push_back({
                std::clamp(vertex.x / viewportWidth, 0.0f, 1.0f),
                std::clamp(vertex.y / viewportHeight, 0.0f, 1.0f)});
    return output;
}

MapContextCapture::MapContextCapture(std::string outputDirectory)
    : _outputDirectory(std::move(outputDirectory))
{
    if (!_outputDirectory.empty()) osgDB::makeDirectory(_outputDirectory);
}

void MapContextCapture::request(
    const std::string& artifactId,
    const ScienceTargetOverlayInput& target)
{
    if (artifactId.empty()) return;
    std::lock_guard<std::mutex> guard(_mutex);
    if (_snapshots.count(artifactId)) return;
    for (PendingRequest& pending : _pending)
        if (pending.artifactId == artifactId)
        {
            pending.target = target;
            return;
        }
    _pending.push_back({artifactId, target});
    while (_pending.size() > MAXIMUM_SNAPSHOTS) _pending.pop_front();
}

void MapContextCapture::setLiveTarget(
    const ScienceTargetOverlayInput& target)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _liveTarget = target;
    if (!target.requestedVisible && !target.actualVisible)
        _liveProjection = ScienceLiveTargetProjection();
}

void MapContextCapture::projectLiveTarget(const osg::Camera& sceneCamera)
{
    ScienceTargetOverlayInput target;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        target = _liveTarget;
    }
    if (!target.requestedVisible && !target.actualVisible) return;
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;
    osg::ref_ptr<osg::Viewport> projectionViewport = new osg::Viewport(
        0.0, 0.0, viewport[2], viewport[3]);
    const ScienceTargetOverlayFrame projected = projectScienceTarget(
        target, sceneCamera.getViewMatrix(), sceneCamera.getProjectionMatrix(),
        *projectionViewport);
    ScienceLiveTargetProjection output;
    output.requestedOverlay = normalizeScienceOverlay(
        projected.requested, static_cast<float>(viewport[2]),
        static_cast<float>(viewport[3]));
    output.actualOverlay = normalizeScienceOverlay(
        projected.actual, static_cast<float>(viewport[2]),
        static_cast<float>(viewport[3]));
    output.labelAnchor.set(
        std::clamp(projected.labelAnchor.x() / viewport[2], 0.0f, 1.0f),
        std::clamp(projected.labelAnchor.y() / viewport[3], 0.0f, 1.0f));
    output.label = projected.label;
    output.visible = projected.visible;
    std::lock_guard<std::mutex> guard(_mutex);
    _liveProjection = std::move(output);
}

bool MapContextCapture::copyLiveProjection(
    ScienceLiveTargetProjection& output) const
{
    std::lock_guard<std::mutex> guard(_mutex);
    output = _liveProjection;
    return output.visible;
}

bool MapContextCapture::captureBeforeUi(
    const osg::Camera& sceneCamera, std::uint64_t frame)
{
    PendingRequest request;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_pending.empty()) return false;
        request = _pending.front();
        _pending.pop_front();
    }
    GLint viewport[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return false;
    std::vector<unsigned char> pixels(
        static_cast<std::size_t>(viewport[2]) * viewport[3] * 4);
    GLint previousAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3],
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glPixelStorei(GL_PACK_ALIGNMENT, previousAlignment);
    if (!scienceCaptureHasVisualContent(pixels))
    {
        std::lock_guard<std::mutex> guard(_mutex);
        bool alreadyQueued = false;
        for (const PendingRequest& pending : _pending)
            if (pending.artifactId == request.artifactId)
            {
                alreadyQueued = true;
                break;
            }
        if (!alreadyQueued) _pending.push_front(request);
        return false;
    }
    osg::ref_ptr<osg::Viewport> projectionViewport = new osg::Viewport(
        0.0, 0.0, viewport[2], viewport[3]);
    const ScienceTargetOverlayFrame projected = projectScienceTarget(
        request.target, sceneCamera.getViewMatrix(),
        sceneCamera.getProjectionMatrix(), *projectionViewport);
    return store(request, viewport[2], viewport[3], pixels, frame, projected);
}

bool MapContextCapture::storeCapturedRgbaForTesting(
    const std::string& artifactId,
    const ScienceTargetOverlayInput& target,
    int sourceWidth, int sourceHeight,
    const std::vector<unsigned char>& bottomUpRgba,
    std::uint64_t frame,
    const ScienceTargetOverlayFrame* projected)
{
    PendingRequest request{artifactId, target};
    ScienceTargetOverlayFrame empty;
    return store(request, sourceWidth, sourceHeight, bottomUpRgba, frame,
                 projected ? *projected : empty);
}

bool MapContextCapture::store(
    const PendingRequest& request,
    int sourceWidth, int sourceHeight,
    const std::vector<unsigned char>& bottomUpRgba,
    std::uint64_t frame,
    const ScienceTargetOverlayFrame& projected)
{
    if (request.artifactId.empty() || sourceWidth <= 0 || sourceHeight <= 0 ||
        bottomUpRgba.size() !=
            static_cast<std::size_t>(sourceWidth) * sourceHeight * 4)
        return false;
    if (!scienceCaptureHasVisualContent(bottomUpRgba)) return false;
    const ScienceCaptureSize size = scienceContextCaptureSize(
        sourceWidth, sourceHeight);
    if (size.width <= 0 || size.height <= 0) return false;
    std::vector<unsigned char> pixels = downsample(
        bottomUpRgba, sourceWidth, sourceHeight, size.width, size.height);
    const std::string imagePath = pathFor(request.artifactId);
    if (!writeTga(imagePath, size.width, size.height, pixels)) return false;

    osg::ref_ptr<osg::Image> image = new osg::Image;
    image->allocateImage(size.width, size.height, 1, GL_RGBA,
                         GL_UNSIGNED_BYTE, 1);
    std::memcpy(image->data(), pixels.data(), pixels.size());
    osg::ref_ptr<osg::Texture2D> texture = new osg::Texture2D(image.get());
    texture->setResizeNonPowerOfTwoHint(false);
    texture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
    texture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);

    ScienceContextSnapshot snapshot;
    snapshot.texture = texture;
    snapshot.requestedOverlay = normalizeScienceOverlay(
        projected.requested, static_cast<float>(sourceWidth),
        static_cast<float>(sourceHeight));
    snapshot.actualOverlay = normalizeScienceOverlay(
        projected.actual, static_cast<float>(sourceWidth),
        static_cast<float>(sourceHeight));
    snapshot.imagePath = imagePath;
    snapshot.capturedFrame = frame;
    snapshot.width = size.width;
    snapshot.height = size.height;

    std::lock_guard<std::mutex> guard(_mutex);
    _snapshots[request.artifactId] = std::move(snapshot);
    _order.erase(std::remove(_order.begin(), _order.end(), request.artifactId),
                 _order.end());
    _order.push_back(request.artifactId);
    while (_order.size() > MAXIMUM_SNAPSHOTS)
    {
        _snapshots.erase(_order.front());
        _order.pop_front();
    }
    return true;
}

bool MapContextCapture::copy(
    const std::string& artifactId, ScienceContextSnapshot& output) const
{
    std::lock_guard<std::mutex> guard(_mutex);
    const auto found = _snapshots.find(artifactId);
    if (found == _snapshots.end()) return false;
    output = found->second;
    return true;
}

void MapContextCapture::erase(const std::string& artifactId)
{
    std::lock_guard<std::mutex> guard(_mutex);
    _snapshots.erase(artifactId);
    _order.erase(std::remove(_order.begin(), _order.end(), artifactId),
                 _order.end());
}

std::size_t MapContextCapture::size() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return _snapshots.size();
}

bool MapContextCapture::pending() const
{
    std::lock_guard<std::mutex> guard(_mutex);
    return !_pending.empty();
}

std::string MapContextCapture::pathFor(const std::string& artifactId) const
{
    if (_outputDirectory.empty()) return std::string();
    return _outputDirectory + "/" + safeName(artifactId) + ".tga";
}
