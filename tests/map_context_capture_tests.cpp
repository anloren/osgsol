#include "science_ui/map_context_capture.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

ScienceTargetOverlayInput target()
{
    ScienceTargetOverlayInput value;
    value.requested.kind = ScienceOverlayGeometryKind::Point;
    value.requested.point = {24.3658, 104.1796};
    value.requestedVisible = true;
    return value;
}

void testSizingAndNormalization()
{
    ScienceCaptureSize wide = scienceContextCaptureSize(2000, 1000);
    expect(wide.width == 512 && wide.height == 256,
           "wide capture must preserve aspect within 512x288");
    ScienceCaptureSize tall = scienceContextCaptureSize(300, 900);
    expect(tall.width == 96 && tall.height == 288,
           "tall capture must preserve aspect within 512x288");

    ScienceOverlayPath path;
    path.vertices = {{0.0f, 0.0f}, {1000.0f, 500.0f},
                     {2000.0f, 1000.0f}};
    const std::vector<osg::Vec2f> normalized =
        normalizeScienceOverlay({path}, 2000.0f, 1000.0f);
    expect(normalized.size() == 3 && normalized[1].x() == 0.5f &&
           normalized[1].y() == 0.5f,
           "target overlay must use normalized capture coordinates");

    std::vector<unsigned char> blank(32 * 18 * 4, 255);
    expect(!scienceCaptureHasVisualContent(blank),
           "uniform white capture must be rejected as a timing failure");
    blank[17] = 120;
    expect(scienceCaptureHasVisualContent(blank),
           "a frame with visible map contrast must be accepted");
}

void testReplacementAndMemoryCap()
{
    MapContextCapture capture;
    ScienceTargetOverlayFrame projected;
    ScienceOverlayPath requested;
    requested.vertices = {{100.0f, 50.0f}, {200.0f, 100.0f}};
    projected.requested.push_back(requested);
    std::vector<unsigned char> pixels(400 * 200 * 4, 127);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 400; ++x)
            pixels[(static_cast<std::size_t>(y) * 400 + x) * 4] =
                static_cast<unsigned char>((x + y) % 256);
    expect(capture.storeCapturedRgbaForTesting(
        "a", target(), 400, 200, pixels, 1, &projected),
        "first context snapshot must store");
    ScienceContextSnapshot snapshot;
    expect(capture.copy("a", snapshot) && snapshot.width == 400 &&
           snapshot.height == 200 && snapshot.capturedFrame == 1,
           "stored snapshot must preserve dimensions and frame");
    expect(snapshot.texture.valid() &&
           snapshot.requestedOverlay.size() == 2,
           "snapshot must retain texture and target overlay");
    expect(capture.storeCapturedRgbaForTesting(
        "a", target(), 400, 200, pixels, 2, &projected),
        "same artifact must replace its snapshot");
    expect(capture.size() == 1 && capture.copy("a", snapshot) &&
           snapshot.capturedFrame == 2,
           "replacement must not consume another memory slot");

    capture.storeCapturedRgbaForTesting("b", target(), 400, 200, pixels, 3);
    capture.storeCapturedRgbaForTesting("c", target(), 400, 200, pixels, 4);
    capture.storeCapturedRgbaForTesting("d", target(), 400, 200, pixels, 5);
    expect(capture.size() == 3,
           "context capture must retain at most three textures");
    expect(!capture.copy("a", snapshot) && capture.copy("d", snapshot),
           "fourth texture must evict the oldest snapshot");
}

void testRequestIsIndependentOfHudAndCameraState()
{
    MapContextCapture capture;
    ScienceTargetOverlayInput value = target();
    capture.request("pending", value);
    expect(capture.pending(), "request must queue without rendering or moving camera");
    capture.request("pending", value);
    expect(capture.pending(), "same pending request must update rather than duplicate");
}
}

int main()
{
    testSizingAndNormalization();
    testReplacementAndMemoryCap();
    testRequestIsIndependentOfHudAndCameraState();
    std::cout << "MapContextCapture tests passed" << std::endl;
    return 0;
}
