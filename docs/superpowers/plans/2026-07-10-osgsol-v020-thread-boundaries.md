# osgSol v0.2.0 Thread Boundaries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make LayerManager and MediaManager safe under OSG DrawThreadPerContext, and make transient Veo polling failures retryable without changing user-visible workflows.

**Architecture:** ImGui callbacks publish commands and consume immutable snapshots; the FRAME/update thread exclusively mutates live layer and video state. Layer callbacks run outside manager locks, UI state is copied under locks, and Veo HTTP response classification is extracted into a deterministic pure helper.

**Tech Stack:** C++17, OpenSceneGraph 3.6.5, ImGui, libhv, CMake/CTest, macOS.

## Global Constraints

- Work only in `/Users/USER/osgsol` from `v0.1.0` on `codex/v0.2-runtime-safety`; do not modify the original `osgverse` repository or `master`.
- Preserve bidirectional panel wheel scrolling and independent per-photo target coordinates.
- ImGui draw traversal may enqueue commands and read snapshots only; it must not mutate live layer/video state.
- Every production behavior change requires a failing test observed before implementation.
- Run at most one `cmake --build` process at a time.
- Run EarthExplorer only with `EARTH_OFFSCREEN=1`.
- Do not package or embed `EARTH_AI_KEY`.
- This plan covers only the first Wave 1 batch; geospatial, feed, tile/input, AI Web, and release batches have separate plans.

---

## File Map

- `applications/earth_explorer/LayerManager.h`: synchronized command queue, value snapshots, subtitle updates, and FRAME drain.
- `applications/earth_explorer/EarthControlUI.h`: render layer controls from snapshots and publish commands.
- `applications/earth_explorer/earth_main.cpp`: FRAME drain handler and synchronized subtitle writes.
- `applications/earth_explorer/ai_setup.cpp`: replace runtime vector references with snapshots/commands.
- `applications/earth_explorer/event_ticker.h`: consume the preset name by value.
- `tests/feed_layer_tests.cpp`: LayerManager queue, ordering, snapshot, and concurrent producer regression.
- `applications/earth_explorer/ai_media.h`: Video UI request/snapshot types, queue lock, and atomic HUD count.
- `applications/earth_explorer/ai_media.cpp`: request drain, snapshot publication, and pure Veo poll classification.
- `applications/earth_explorer/ai_ui.cpp`: enqueue video actions and render published state.
- `tests/media_threading_tests.cpp`: deterministic video request queue and poll-classification tests.
- `tests/CMakeLists.txt`: register the new offline test.

### Task 1: LayerManager Command Queue and Snapshots

**Files:**
- Modify: `applications/earth_explorer/LayerManager.h`
- Modify: `tests/feed_layer_tests.cpp`

**Interfaces:**
- Produces: `std::vector<OverlayLayer> layersSnapshot() const`
- Produces: `bool setSubtitle(const std::string&, const std::string&)`
- Produces: `size_t drainPending()`
- Changes: `setEnabled`, `setOpacity`, and `applyPreset` publish ordered commands; callbacks execute only from `drainPending()`.
- Changes: `lastAppliedPreset()` returns `std::string` by value.

- [ ] **Step 1: Add the failing deferred-apply and snapshot test**

Append this block beside the existing LayerManager tests in `tests/feed_layer_tests.cpp`:

```cpp
    {
        LayerManager lm;
        std::vector<std::string> applied;
        OverlayLayer a; a.id = "a"; a.group = "live";
        a.apply = [&](const OverlayLayer& layer) {
            applied.push_back(std::string("a:") + (layer.enabled ? "1" : "0"));
        };
        OverlayLayer b; b.id = "b"; b.group = "live";
        b.apply = [&](const OverlayLayer& layer) {
            applied.push_back(std::string("b:") + (layer.enabled ? "1" : "0"));
        };
        lm.add(a); lm.add(b);

        lm.setEnabled("a", true);
        lm.setEnabled("b", true);
        lm.setEnabled("a", false);
        CHECK(applied.empty());

        std::vector<OverlayLayer> beforeDrain = lm.layersSnapshot();
        CHECK(beforeDrain.size() == 2);
        CHECK(beforeDrain[0].enabled == false);
        CHECK(beforeDrain[1].enabled == true);

        CHECK(lm.drainPending() == 3);
        CHECK(applied.size() == 3);
        CHECK(applied[0] == "a:1");
        CHECK(applied[1] == "b:1");
        CHECK(applied[2] == "a:0");

        CHECK(lm.setSubtitle("a", "fresh") == true);
        CHECK(lm.setSubtitle("missing", "ignored") == false);
        std::vector<OverlayLayer> snap = lm.layersSnapshot();
        CHECK(snap[0].subtitle == "fresh");
    }
```

- [ ] **Step 2: Run the target and verify RED**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core --target osgVerse_Test_Feeds -j2
```

Expected: compilation fails because `layersSnapshot`, `drainPending`, and `setSubtitle` do not exist.

- [ ] **Step 3: Add synchronized storage and command types**

Add `<deque>` and `<mutex>`, then add these private members to `LayerManager`:

```cpp
    struct PendingCommand
    {
        enum Kind { Enable, Opacity, Preset } kind = Enable;
        std::string id;
        bool enabled = false;
        float opacity = 1.0f;
    };

    mutable std::mutex _mutex;
    std::deque<PendingCommand> _pending;
```

Implement `layersSnapshot`, `setSubtitle`, and the value-returning preset getter exactly as:

```cpp
    std::vector<OverlayLayer> layersSnapshot() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _layers;
    }

    bool setSubtitle(const std::string& id, const std::string& value)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        OverlayLayer* layer = findUnlocked(id);
        if (!layer) return false;
        layer->subtitle = value;
        return true;
    }

    std::string lastAppliedPreset() const
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _lastAppliedPreset;
    }
```

Replace internal searches with a private `findUnlocked()` used only while the caller already holds `_mutex`:

```cpp
    OverlayLayer* findUnlocked(const std::string& id)
    {
        for (size_t i = 0; i < _layers.size(); ++i)
            if (_layers[i].id == id) return &_layers[i];
        return nullptr;
    }
```

- [ ] **Step 4: Convert mutations into queued commands**

Implement `setEnabled` and `setOpacity` so the desired value is visible immediately in snapshots but callbacks are deferred:

```cpp
    void setEnabled(const std::string& id, bool on)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        OverlayLayer* layer = findUnlocked(id);
        if (!layer) return;
        layer->enabled = on;
        PendingCommand command;
        command.kind = PendingCommand::Enable;
        command.id = id;
        command.enabled = on;
        _pending.push_back(command);
    }

    void setOpacity(const std::string& id, float value)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        OverlayLayer* layer = findUnlocked(id);
        if (!layer) return;
        layer->opacity = value;
        PendingCommand command;
        command.kind = PendingCommand::Opacity;
        command.id = id;
        command.opacity = value;
        _pending.push_back(command);
    }
```

Implement `applyPreset()` as validation plus queue publication. It must update desired layer states under the lock, append one `Preset` command, and leave `_lastAppliedPreset` unchanged until drain:

```cpp
    bool applyPreset(const std::string& name)
    {
        std::lock_guard<std::mutex> lock(_mutex);
        const Preset* preset = findPresetUnlocked(name);
        if (!preset) return false;
        for (size_t i = 0; i < _layers.size(); ++i)
            if (!exemptFromPreset(_layers[i])) _layers[i].enabled = false;
        for (size_t i = 0; i < preset->enabledIds.size(); ++i)
        {
            OverlayLayer* layer = findUnlocked(preset->enabledIds[i]);
            if (layer && !exemptFromPreset(*layer)) layer->enabled = true;
        }
        PendingCommand command;
        command.kind = PendingCommand::Preset;
        command.id = name;
        _pending.push_back(command);
        return true;
    }
```

- [ ] **Step 5: Drain callbacks on the FRAME owner**

Implement `drainPending()` so each command is removed FIFO, the relevant layer copy is created under lock, and its callback runs after unlocking. Presets must call every non-exempt layer in two phases (all off, then enabled IDs) and set `_lastAppliedPreset` while locked:

```cpp
    size_t drainPending()
    {
        size_t count = 0;
        for (;;)
        {
            PendingCommand command;
            {
                std::lock_guard<std::mutex> lock(_mutex);
                if (_pending.empty()) break;
                command = _pending.front();
                _pending.pop_front();
            }
            if (command.kind == PendingCommand::Preset)
                applyPresetNow(command.id);
            else
                applyLayerCommandNow(command);
            ++count;
        }
        return count;
    }
```

`applyLayerCommandNow()` and `applyPresetNow()` must copy both the `OverlayLayer` value and its `std::function` while locked, then invoke the copied function after releasing `_mutex`. They must apply the value stored in each command rather than reading a later desired value.

- [ ] **Step 6: Update old tests for deferred callbacks**

After every existing test call that expects `applyCount` or `lastAppliedPreset` to change, insert `lm.drainPending()`. State-only assertions may continue before drain because queued commands publish desired state immediately.

- [ ] **Step 7: Verify GREEN and the full offline gate**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core --target osgVerse_Test_Feeds -j2
/Users/USER/osgsol/build/osgsol_core/bin/osgVerse_Test_Feeds
ctest --test-dir /Users/USER/osgsol/build/osgsol_core -L offline --output-on-failure
```

Expected: `osgVerse_Test_Feeds` exits 0 and all offline tests pass.

- [ ] **Step 8: Commit Task 1**

```bash
git add applications/earth_explorer/LayerManager.h tests/feed_layer_tests.cpp
git commit -m "fix: marshal layer changes onto frame thread"
```

### Task 2: LayerManager Runtime Integration

**Files:**
- Modify: `applications/earth_explorer/EarthControlUI.h`
- Modify: `applications/earth_explorer/earth_main.cpp`
- Modify: `applications/earth_explorer/ai_setup.cpp`
- Modify: `applications/earth_explorer/event_ticker.h`
- Modify: `tests/feed_layer_tests.cpp`

**Interfaces:**
- Consumes: `layersSnapshot`, `setSubtitle`, `drainPending`, and value-returning `lastAppliedPreset` from Task 1.
- Produces: `LayerManagerDrainHandler`, which is the sole runtime callback executor.

- [ ] **Step 1: Add a source-wiring regression**

Add a source-root compile definition for `osgVerse_Test_Feeds` in `tests/CMakeLists.txt`:

```cmake
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_Feeds PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
```

Add a helper and assertions to `tests/feed_layer_tests.cpp` that read the four runtime source files and require snapshot/queue wiring:

```cpp
static std::string readSourceFile(const std::string& relative)
{
    std::ifstream input(std::string(OSGVERSE_SOURCE_DIR) + "/" + relative);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}
```

```cpp
    {
        const std::string ui = readSourceFile("applications/earth_explorer/EarthControlUI.h");
        const std::string main = readSourceFile("applications/earth_explorer/earth_main.cpp");
        CHECK(ui.find("layersSnapshot()") != std::string::npos);
        CHECK(ui.find("->layers()") == std::string::npos);
        CHECK(main.find("drainPending()") != std::string::npos);
        CHECK(main.find("setSubtitle(") != std::string::npos);
    }
```

- [ ] **Step 2: Run and verify RED**

Run:

```bash
cmake -S /Users/USER/osgsol -B /Users/USER/osgsol/build/osgsol_core
cmake --build /Users/USER/osgsol/build/osgsol_core --target osgVerse_Test_Feeds -j2
/Users/USER/osgsol/build/osgsol_core/bin/osgVerse_Test_Feeds
```

Expected: the source-wiring assertion fails because UI still calls `layers()` and no FRAME drain exists.

- [ ] **Step 3: Render LayerManager values from snapshots**

In `EarthControlUI.h`, replace both mutable vector reads with local value snapshots:

```cpp
std::vector<OverlayLayer> layers = _layers->layersSnapshot();
```

Iterate the local vector and continue publishing changes through `_layers->setEnabled()` and `_layers->setOpacity()`. Never retain a pointer or reference into the snapshot across frames.

In `ai_setup.cpp`, replace runtime `layers()` scans with `layersSnapshot()` and use `setEnabled()`/`setOpacity()` to publish changes. In `event_ticker.h`, store the result of `lastAppliedPreset()` in a local `std::string`.

- [ ] **Step 4: Synchronize dynamic subtitles**

Replace direct writes in `SatFetchStatusHandler` and `ShipViewStateHandler` with:

```cpp
_lm->setSubtitle(id, want);
```

The handlers must not call `find()` and must not compare or mutate `OverlayLayer::subtitle` through a raw pointer.

- [ ] **Step 5: Install the FRAME drain handler**

Add this event handler near the existing FRAME handlers in `earth_main.cpp`:

```cpp
class LayerManagerDrainHandler : public osgGA::GUIEventHandler
{
public:
    explicit LayerManagerDrainHandler(LayerManager* layers) : _layers(layers) {}
    bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override
    {
        if (ea.getEventType() == osgGA::GUIEventAdapter::FRAME && _layers)
            _layers->drainPending();
        return false;
    }
private:
    LayerManager* _layers;
};
```

Register one instance after `LayerManager layerMgr;` is constructed and before runtime UI interaction begins:

```cpp
viewer.addEventHandler(new LayerManagerDrainHandler(&layerMgr));
```

- [ ] **Step 6: Verify target, build EarthExplorer, and run offscreen smoke**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core --target osgVerse_Test_Feeds -j2
/Users/USER/osgsol/build/osgsol_core/bin/osgVerse_Test_Feeds
cmake --build /Users/USER/osgsol/build/osgsol_core --target osgVerse_EarthExplorer -j2
EARTH_OFFSCREEN=1 EARTH_IME=0 EARTH_AUTOCAP=60 \
  /Users/USER/osgsol/build/osgsol_core/bin/osgVerse_EarthExplorer
```

Expected: tests exit 0; EarthExplorer logs the offscreen context and exits after capture without a crash.

- [ ] **Step 7: Commit Task 2**

```bash
git add applications/earth_explorer/EarthControlUI.h \
  applications/earth_explorer/earth_main.cpp \
  applications/earth_explorer/ai_setup.cpp \
  applications/earth_explorer/event_ticker.h \
  tests/CMakeLists.txt tests/feed_layer_tests.cpp
git commit -m "fix: drain layer UI commands on frame thread"
```

### Task 3: Media UI Request Queue and Published Snapshot

**Files:**
- Modify: `applications/earth_explorer/ai_media.h`
- Modify: `applications/earth_explorer/ai_media.cpp`
- Modify: `applications/earth_explorer/ai_ui.cpp`
- Create: `tests/media_threading_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `VideoUiRequest`, `VideoUiSnapshot`, `enqueueVideoRequest`, and `videoUiSnapshot`.
- Consumes: `MediaManager::update()` as the sole live video-state owner.
- Preserves: AI tool calls that already execute during main-thread drain.

- [ ] **Step 1: Add the failing pure queue test**

Create `tests/media_threading_tests.cpp` with a header-only queue test seam declared in `ai_media.h`:

```cpp
#include <applications/earth_explorer/ai_media.h>
#include <cstdlib>
#include <iostream>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << "\n"; \
    return 1; } } while (0)

int main()
{
    using namespace earthai;
    VideoUiRequestQueue queue;
    VideoUiRequest first; first.kind = VideoUiRequest::Begin;
    first.lla = osg::Vec3d(1.0, 2.0, 3.0);
    VideoUiRequest second; second.kind = VideoUiRequest::Cancel;
    queue.push(first);
    queue.push(second);

    std::vector<VideoUiRequest> drained = queue.drain();
    CHECK(drained.size() == 2);
    CHECK(drained[0].kind == VideoUiRequest::Begin);
    CHECK(drained[0].lla == osg::Vec3d(1.0, 2.0, 3.0));
    CHECK(drained[1].kind == VideoUiRequest::Cancel);
    CHECK(queue.drain().empty());
    std::cout << "[OK] media UI request queue\n";
    return 0;
}
```

Register it:

```cmake
NEW_CTEST(osgVerse_Test_MediaThreading media_threading_tests.cpp offline 30)
```

- [ ] **Step 2: Configure/build and verify RED**

Run:

```bash
cmake -S /Users/USER/osgsol -B /Users/USER/osgsol/build/osgsol_core
cmake --build /Users/USER/osgsol/build/osgsol_core --target osgVerse_Test_MediaThreading -j2
```

Expected: compilation fails because `VideoUiRequestQueue` and `VideoUiRequest` do not exist.

- [ ] **Step 3: Implement the request queue and immutable snapshot types**

Add these public value types to `ai_media.h`:

```cpp
    struct VideoUiRequest
    {
        enum Kind { Begin, CaptureEnd, Confirm, Cancel } kind = Begin;
        osg::Vec3d lla;
        std::string style;
    };

    class VideoUiRequestQueue
    {
    public:
        void push(const VideoUiRequest& request)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _requests.push_back(request);
        }
        std::vector<VideoUiRequest> drain()
        {
            std::lock_guard<std::mutex> lock(_mutex);
            std::vector<VideoUiRequest> result(_requests.begin(), _requests.end());
            _requests.clear();
            return result;
        }
    private:
        std::mutex _mutex;
        std::deque<VideoUiRequest> _requests;
    };
```

Add `<atomic>`, `<deque>`, `<mutex>`, and `<vector>` includes. Add this snapshot after `PendingVideoInfo`:

```cpp
        struct VideoUiSnapshot
        {
            VideoPhaseKindPublic phase = VIDEO_IDLE;
            PendingVideoInfo pending;
            std::string commandError;
        };
```

- [ ] **Step 4: Add MediaManager publication APIs**

Add:

```cpp
        void enqueueVideoRequest(const VideoUiRequest& request) { _videoRequests.push(request); }
        VideoUiSnapshot videoUiSnapshot() const;
```

Add private storage:

```cpp
        VideoUiRequestQueue _videoRequests;
        mutable std::mutex _videoSnapshotMutex;
        VideoUiSnapshot _videoSnapshot;
        std::atomic<int> _hudHideCount;
```

Remove the old plain `int _hudHideCount`. Implement `isHudHidden()` with `_hudHideCount.load()` and update `hudHide`/`hudRestore` with atomic fetch operations without allowing the count to go below zero.

- [ ] **Step 5: Drain requests at the start of update and publish at the end**

At the beginning of `MediaManager::update()`, drain FIFO requests and dispatch them on the FRAME owner:

```cpp
        std::vector<VideoUiRequest> requests = _videoRequests.drain();
        std::string commandError;
        for (size_t i = 0; i < requests.size(); ++i)
        {
            const VideoUiRequest& request = requests[i];
            if (request.kind == VideoUiRequest::Begin)
            {
                if (!beginVideoCapture(request.lla, request.style))
                    commandError = "video capture is not idle";
            }
            else if (request.kind == VideoUiRequest::CaptureEnd)
            {
                if (!captureVideoEnd(request.lla)) commandError = "video is not waiting for B";
            }
            else if (request.kind == VideoUiRequest::Confirm)
            {
                picojson::value result = confirmVideo();
                if (result.is<picojson::object>() && result.contains("error") &&
                    result.get("error").is<std::string>())
                    commandError = result.get("error").get<std::string>();
            }
            else if (request.kind == VideoUiRequest::Cancel)
                cancelVideo();
        }
```

Publish the current phase and pending information under `_videoSnapshotMutex` after `updateVideoInternal()` and after all photo-state early-return paths have been rewritten to reach one publication epilogue:

```cpp
        VideoUiSnapshot snapshot;
        snapshot.phase = videoPhase();
        snapshot.pending = pendingVideoInfo();
        snapshot.commandError = commandError;
        {
            std::lock_guard<std::mutex> lock(_videoSnapshotMutex);
            _videoSnapshot = snapshot;
        }
```

Implement the getter as a locked value copy.

- [ ] **Step 6: Convert ai_ui.cpp to enqueue-only behavior**

Read one snapshot at the start of the video-control section:

```cpp
earthai::MediaManager::VideoUiSnapshot video = media
    ? media->videoUiSnapshot() : earthai::MediaManager::VideoUiSnapshot();
```

Replace every direct video mutation with a request:

```cpp
earthai::VideoUiRequest request;
request.kind = earthai::VideoUiRequest::Begin;
request.lla = llaA;
media->enqueueVideoRequest(request);
```

Use `CaptureEnd`, `Confirm`, and `Cancel` for the other buttons. Render `video.commandError` in the Modal and use `video.pending` for coordinates/prompt. `ai_ui.cpp` must contain no direct calls to `beginVideoCapture`, `captureVideoEnd`, `confirmVideo`, or `cancelVideo` after this step.

- [ ] **Step 7: Verify the new test, EarthExplorer build, and offline gate**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core --target osgVerse_Test_MediaThreading -j2
/Users/USER/osgsol/build/osgsol_core/bin/osgVerse_Test_MediaThreading
cmake --build /Users/USER/osgsol/build/osgsol_core --target osgVerse_EarthExplorer -j2
ctest --test-dir /Users/USER/osgsol/build/osgsol_core -L offline --output-on-failure
```

Expected: media test exits 0, EarthExplorer builds, and the offline gate is green.

- [ ] **Step 8: Commit Task 3**

```bash
git add applications/earth_explorer/ai_media.h \
  applications/earth_explorer/ai_media.cpp \
  applications/earth_explorer/ai_ui.cpp \
  tests/media_threading_tests.cpp tests/CMakeLists.txt
git commit -m "fix: marshal video UI actions onto frame thread"
```

### Task 4: Retry Transient Veo Poll Failures

**Files:**
- Modify: `applications/earth_explorer/ai_media.h`
- Modify: `applications/earth_explorer/ai_media.cpp`
- Modify: `tests/media_threading_tests.cpp`

**Interfaces:**
- Produces: `VideoPollDisposition classifyVideoPollHttp(bool, int)`.
- Changes: network absence, 429, and 5xx return `done=false`; other 4xx remain terminal.

- [ ] **Step 1: Add failing classification tests**

Append:

```cpp
    CHECK(classifyVideoPollHttp(false, 0) == VIDEO_POLL_RETRY);
    CHECK(classifyVideoPollHttp(true, 429) == VIDEO_POLL_RETRY);
    CHECK(classifyVideoPollHttp(true, 500) == VIDEO_POLL_RETRY);
    CHECK(classifyVideoPollHttp(true, 503) == VIDEO_POLL_RETRY);
    CHECK(classifyVideoPollHttp(true, 400) == VIDEO_POLL_TERMINAL_ERROR);
    CHECK(classifyVideoPollHttp(true, 401) == VIDEO_POLL_TERMINAL_ERROR);
    CHECK(classifyVideoPollHttp(true, 200) == VIDEO_POLL_PARSE_BODY);
```

- [ ] **Step 2: Run and verify RED**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core --target osgVerse_Test_MediaThreading -j2
```

Expected: compilation fails because the disposition API does not exist.

- [ ] **Step 3: Implement the pure classifier**

Add to `ai_media.h`:

```cpp
    enum VideoPollDisposition
    {
        VIDEO_POLL_RETRY,
        VIDEO_POLL_TERMINAL_ERROR,
        VIDEO_POLL_PARSE_BODY
    };

    inline VideoPollDisposition classifyVideoPollHttp(bool hasResponse, int status)
    {
        if (!hasResponse || status == 429 || status >= 500) return VIDEO_POLL_RETRY;
        if (status != 200) return VIDEO_POLL_TERMINAL_ERROR;
        return VIDEO_POLL_PARSE_BODY;
    }
```

- [ ] **Step 4: Apply classification in VeoVideoProvider::poll**

Replace the current `!resp || status != 200` terminal block with:

```cpp
        VideoPollDisposition disposition = classifyVideoPollHttp((bool)resp,
            resp ? (int)resp->status_code : 0);
        if (disposition == VIDEO_POLL_RETRY)
        {
            done = false;
            err.clear();
            return;
        }
        if (disposition == VIDEO_POLL_TERMINAL_ERROR)
        {
            done = true;
            err = "HTTP " + std::to_string((int)resp->status_code) + ": " +
                  truncate200(resp->body);
            return;
        }
```

Keep parse errors in successful 200 bodies terminal, because repeated parsing of the same malformed response cannot self-heal.

- [ ] **Step 5: Verify GREEN and run the full gate**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core --target \
  osgVerse_Test_MediaThreading osgVerse_EarthExplorer -j2
/Users/USER/osgsol/build/osgsol_core/bin/osgVerse_Test_MediaThreading
ctest --test-dir /Users/USER/osgsol/build/osgsol_core -L offline --output-on-failure
```

Expected: all commands exit 0.

- [ ] **Step 6: Commit Task 4**

```bash
git add applications/earth_explorer/ai_media.h \
  applications/earth_explorer/ai_media.cpp tests/media_threading_tests.cpp
git commit -m "fix: retry transient veo polling failures"
```

### Task 5: Thread-Boundary Batch Verification

**Files:**
- Modify: `docs/superpowers/plans/2026-07-10-osgsol-v020-thread-boundaries.md`

**Interfaces:**
- Consumes: Tasks 1-4.
- Produces: verified first-batch checkpoint ready for the geospatial correctness plan.

- [ ] **Step 1: Rebuild all first-batch targets**

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core --target \
  osgVerse_Test_Feeds osgVerse_Test_MediaThreading osgVerse_Test_Ai_Chat \
  osgVerse_EarthExplorer -j2
```

Expected: build exits 0.

- [ ] **Step 2: Run all offline tests**

```bash
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -L offline --output-on-failure
```

Expected: 100% pass, including `osgVerse_Test_MediaThreading`.

- [ ] **Step 3: Run an offscreen smoke**

```bash
rm -f /tmp/osgsol-v020-thread-smoke.png
EARTH_OFFSCREEN=1 EARTH_IME=0 EARTH_AUTOCAP=120 \
EARTH_AUTOCAP_PATH=/tmp/osgsol-v020-thread-smoke.png \
  /Users/USER/osgsol/build/osgsol_core/bin/osgVerse_EarthExplorer
test -s /tmp/osgsol-v020-thread-smoke.png
```

Expected: process exits 0 and the capture is non-empty.

- [ ] **Step 4: Verify the v0.1 regressions remain wired**

```bash
rg -n 'scroll panel down then back up|independent photo target|OSGSOL_HAS_PHOTO_REQUEST' \
  tests applications/earth_explorer
```

Expected: both wheel and independent-photo regression coverage remain present.

- [ ] **Step 5: Mark completed checkboxes and commit the verification record**

Update completed plan steps from `- [ ]` to `- [x]`, then run:

```bash
git add docs/superpowers/plans/2026-07-10-osgsol-v020-thread-boundaries.md
git commit -m "docs: record v0.2 thread-boundary verification"
```

## Self-Review Record

- Spec coverage: LayerManager queue/snapshot, MediaManager request/snapshot, atomic HUD visibility, and Veo transient retry are each assigned to a task.
- Scope split: geospatial correctness, feed preservation, tile/input, AI Web, and release work are intentionally excluded and receive separate plans.
- Type consistency: `VideoUiRequest`, `VideoUiRequestQueue`, `VideoUiSnapshot`, `VideoPollDisposition`, `layersSnapshot`, `setSubtitle`, and `drainPending` use the same names in producer, consumer, and tests.
- Placeholder scan: the plan contains no deferred implementation markers; every code-changing step supplies the required interface or exact replacement behavior.
