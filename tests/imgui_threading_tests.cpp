#include <ui/ImGuiInputQueue.h>
#include <ui/ImGuiScroll.h>
#include "../applications/earth_explorer/earth_exit.h"

#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

int main()
{
    earthexit::QuitRequest quitRequest;
    CHECK(!quitRequest.consume());
    quitRequest.request();
    CHECK(quitRequest.consume());
    CHECK(!quitRequest.consume());

    osgVerse::ImGuiInputQueue queue;
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer)
    {
        producers.push_back(std::thread([producer, &queue]
        {
            for (int i = 0; i < 1000; ++i)
                queue.push(osgVerse::ImGuiInputEvent::keyEvent(
                    producer * 1000 + i, true, 0));
        }));
    }
    for (size_t i = 0; i < producers.size(); ++i) producers[i].join();
    CHECK(queue.takeAll().size() == 4000);
    CHECK(queue.takeAll().empty());
    queue.publishCapture(true, false);
    CHECK(queue.wantsMouse());
    CHECK(!queue.wantsKeyboard());

    unsigned int frames = 99;
    CHECK(earthexit::parsePositiveFrameCount("5", frames));
    CHECK(frames == 5);
    CHECK(!earthexit::parsePositiveFrameCount("not-a-count", frames));
    CHECK(!earthexit::parsePositiveFrameCount("0", frames));
    CHECK(!earthexit::parsePositiveFrameCount("-1", frames));
    CHECK(!earthexit::parsePositiveFrameCount("5frames", frames));
    const std::string maxFrames = std::to_string(
        static_cast<unsigned long long>(std::numeric_limits<unsigned int>::max()));
    CHECK(earthexit::parsePositiveFrameCount(maxFrames.c_str(), frames));
    CHECK(frames == std::numeric_limits<unsigned int>::max());
    const std::string overflowingFrames = maxFrames + "0";
    CHECK(!earthexit::parsePositiveFrameCount(overflowingFrames.c_str(), frames));

    std::ifstream sourceFile(std::string(OSGVERSE_SOURCE_DIR) + "/ui/ImGui3D.cpp");
    std::ostringstream sourceBuffer; sourceBuffer << sourceFile.rdbuf();
    const std::string source = sourceBuffer.str();
    const size_t release = source.find("void releaseOnDrawThread()");
    const size_t nextMethod = source.find("virtual bool handle(", release);
    const size_t shutdown = source.find("shutdownImGuiRendererBackend();", release);
    CHECK(release != std::string::npos);
    CHECK(nextMethod != std::string::npos);
    CHECK(shutdown != std::string::npos && shutdown < nextMethod);

    // macOS trackpad / Magic Mouse reports both directions as SCROLL_2D.  Each
    // ImGui event bridge must preserve getScrollingDeltaY() instead of mapping
    // every non-SCROLL_UP event to -1 (which makes a panel scroll down forever).
    std::ifstream imgui2DFile(std::string(OSGVERSE_SOURCE_DIR) + "/ui/ImGui.cpp");
    std::ostringstream imgui2DBuffer; imgui2DBuffer << imgui2DFile.rdbuf();
    const std::string imgui2DSource = imgui2DBuffer.str();
    CHECK(imgui2DSource.find("resolveImGuiWheelAmount") != std::string::npos);
    CHECK(source.find("resolveImGuiWheelAmount") != std::string::npos);

    // The camera draw callbacks retain raw application pointers.  Earth must stop viewer threads
    // after viewer.run() returns and before function-local callback owners are destroyed.
    std::ifstream earthFile(std::string(OSGVERSE_SOURCE_DIR) +
                            "/applications/earth_explorer/earth_main.cpp");
    std::ostringstream earthBuffer; earthBuffer << earthFile.rdbuf();
    const std::string earthSource = earthBuffer.str();
    const size_t guardClass = earthSource.find("class ViewerThreadStopGuard");
    const size_t guardDestructor = earthSource.find("~ViewerThreadStopGuard()", guardClass);
    const size_t stopOperation = earthSource.find("_viewer.stopThreading();", guardDestructor);
    const size_t guardClassEnd = earthSource.find("\n};", guardDestructor);
    const size_t stopGuard = earthSource.find(
        "ViewerThreadStopGuard stopViewerThreads(viewer);");
    const size_t viewerRun = earthSource.rfind("viewerResult = viewer.run();");
    const size_t prefetchJoin = earthSource.rfind("prefetchWorker.stopAndJoin();");
    const size_t viewerReturn = earthSource.rfind("return viewerResult;");
    CHECK(guardClass != std::string::npos);
    CHECK(guardDestructor != std::string::npos);
    CHECK(stopOperation != std::string::npos && stopOperation < guardClassEnd);
    CHECK(stopGuard != std::string::npos);
    CHECK(viewerRun != std::string::npos);
    CHECK(stopGuard < viewerRun);
    CHECK(prefetchJoin != std::string::npos && viewerRun < prefetchJoin);
    CHECK(viewerReturn != std::string::npos && prefetchJoin < viewerReturn);
    CHECK(earthSource.find("#include \"earth_exit.h\"") != std::string::npos);
    CHECK(earthSource.find(
        "earthexit::parsePositiveFrameCount(autoQuitFramesEnv, autoQuitFrames)") !=
        std::string::npos);
    CHECK(earthSource.find("class UiQuitDrainHandler") != std::string::npos);
    CHECK(earthSource.find("_quitRequest->consume()") != std::string::npos);
    CHECK(earthSource.find("std::thread(prefetchLowLODGlobe, prefetchZ).detach()") ==
        std::string::npos);
    CHECK(earthSource.find("class LowLodPrefetchWorker") != std::string::npos);

    std::ifstream controlFile(std::string(OSGVERSE_SOURCE_DIR) +
                              "/applications/earth_explorer/EarthControlUI.h");
    std::ostringstream controlBuffer; controlBuffer << controlFile.rdbuf();
    const std::string controlSource = controlBuffer.str();
    CHECK(controlSource.find("_quitRequest->request();") != std::string::npos);
    CHECK(controlSource.find("_viewer->setDone(true)") == std::string::npos);

    std::ifstream tilesFile(std::string(OSGVERSE_SOURCE_DIR) +
                            "/applications/earth_explorer/tiles3d_data.cpp");
    std::ostringstream tilesBuffer; tilesBuffer << tilesFile.rdbuf();
    const std::string tilesSource = tilesBuffer.str();
    CHECK(tilesSource.find(".detach()") == std::string::npos);
    CHECK(tilesSource.find("_worker.join()") != std::string::npos);

    std::ifstream normalExitFile(std::string(OSGVERSE_SOURCE_DIR) +
                                 "/tests/macos_normal_exit_tests.sh");
    std::ostringstream normalExitBuffer; normalExitBuffer << normalExitFile.rdbuf();
    const std::string normalExitSource = normalExitBuffer.str();
    CHECK(normalExitSource.find(
        "/usr/bin/env -u EARTH_AUTOCAP EARTH_OFFSCREEN=1") != std::string::npos);

    osg::ref_ptr<osgGA::GUIEventAdapter> scrollEvent = new osgGA::GUIEventAdapter;
    scrollEvent->setScrollingMotion(osgGA::GUIEventAdapter::SCROLL_UP);
    CHECK(osgVerse::resolveImGuiWheelAmount(*scrollEvent) == 1.0f);
    scrollEvent->setScrollingMotion(osgGA::GUIEventAdapter::SCROLL_DOWN);
    CHECK(osgVerse::resolveImGuiWheelAmount(*scrollEvent) == -1.0f);
    scrollEvent->setScrollingMotionDelta(0.0f, 2.5f);
    CHECK(osgVerse::resolveImGuiWheelAmount(*scrollEvent) == 2.5f);
    scrollEvent->setScrollingMotionDelta(0.0f, -1.25f);
    CHECK(osgVerse::resolveImGuiWheelAmount(*scrollEvent) == -1.25f);
    std::cout << "[OK] ImGui immutable input queue\n";
    return 0;
}
