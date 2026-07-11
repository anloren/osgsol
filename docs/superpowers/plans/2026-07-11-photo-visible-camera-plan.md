# Photo Visible-Camera Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `generate_photo` capture the camera view visible at shutter time without moving or leveling the camera, while preserving independent coordinates, paths, and state for every photo job.

**Architecture:** `fly_to` remains the only AI tool that changes location. `generate_photo` becomes a gated, camera-read-only operation: it rejects an active flight, schedules the existing one-frame render wait, and snapshots the latest main-camera view matrix when the grab is armed. Pure helpers in `ai_photo_request.h` provide deterministic RED/GREEN seams; the real tool block is also protected by a source guard against reintroducing `setByEye()` or `stopAnimation()`.

**Tech Stack:** C++17, OpenSceneGraph 3.6.5, picojson, existing EarthExplorer `MediaManager`, CMake/CTest.

## Global Constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on `codex/v0.2-runtime-safety`.
- Do not modify `/Users/USER/osgverse`, `/Users/USER/osgsol`, `master`, tags, or pull requests.
- `fly_to` keeps its current path, speed, and landing pose; this batch changes only photo capture behavior.
- `generate_photo` must not call `setByEye()`, `moveTo()`, `stopAnimation()`, or write center, tilt, distance, or a camera matrix.
- `targetLla` is prompt metadata only; it never drives or validates camera pose.
- Every job retains unique `snap_<epoch>_<job>.png` and `gen_<epoch>_<job>.png` paths.
- `show_camera_platform` remains false unless the user explicitly asks to see a spacecraft/platform; ISS viewpoint alone is false.
- Build one target at a time; do not run concurrent CMake builds.

## File Structure

- `applications/earth_explorer/ai_photo_request.h`: pure tool gate and value-type photo/capture requests.
- `applications/earth_explorer/ai_setup.cpp`: camera-read-only `generate_photo` tool and corrected model instructions.
- `applications/earth_explorer/ai_media.h`: pending input and locked shutter request fields.
- `applications/earth_explorer/ai_media.cpp`: create the immutable capture request when the snapshot is armed.
- `tests/ai_chat_tests.cpp`: pure behavior, request isolation, matrix locking, and production source guards.
- `tests/CMakeLists.txt`: expose the repository source directory to the existing AI chat test.

---

### Task 1: Add the failing camera-read-only tool regression

**Files:**
- Modify: `tests/ai_chat_tests.cpp:1-60,629-657`
- Modify: `tests/CMakeLists.txt:139-140`

**Interfaces:**
- Consumes: existing `earthai::parsePhotoRequest(...)` and `EarthManipulator::isAnimationRunning() const`.
- Produces: a RED source/behavior gate that fails while `generate_photo` resets the camera.

- [ ] **Step 1: Expose the source root to the AI test**

Immediately after `NEW_CTEST(osgVerse_Test_Ai_Chat ...)`, add:

```cmake
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_Ai_Chat PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
```

- [ ] **Step 2: Write the failing test**

Add `<fstream>` and this helper near the other test helpers:

```cpp
static std::string readWholeFile(const std::string& path)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

static std::string generatePhotoToolBlock()
{
    const std::string source = readWholeFile(
        std::string(OSGVERSE_SOURCE_DIR) + "/applications/earth_explorer/ai_setup.cpp");
    const size_t begin = source.find("earthai::Tool photo; photo.name = \"generate_photo\"");
    const size_t end = source.find("aiRegistry->add(photo);", begin);
    CHECK(begin != std::string::npos);
    CHECK(end != std::string::npos);
    return source.substr(begin, end - begin);
}
```

Append to the existing `generate_photo` test block:

```cpp
const std::string toolBlock = generatePhotoToolBlock();
CHECK(toolBlock.find("isAnimationRunning()") != std::string::npos);
CHECK(toolBlock.find("setByEye(") == std::string::npos);
CHECK(toolBlock.find("stopAnimation(") == std::string::npos);
CHECK(toolBlock.find("moveTo(") == std::string::npos);
```

- [ ] **Step 3: Build and run the test to verify RED**

Run:

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Ai_Chat -j2
build/osgsol_core/bin/osgVerse_Test_Ai_Chat
```

Expected: build succeeds, then the test aborts because the current tool block contains `stopAnimation()` and `setByEye()` and does not contain the flight gate.

- [ ] **Step 4: Commit the RED test**

```bash
git add tests/ai_chat_tests.cpp tests/CMakeLists.txt
git commit -m "test: guard photo capture camera pose"
```

---

### Task 2: Make `generate_photo` camera-read-only

**Files:**
- Modify: `applications/earth_explorer/ai_photo_request.h:11-80`
- Modify: `applications/earth_explorer/ai_setup.cpp:334-380,476-481`
- Test: `tests/ai_chat_tests.cpp:629-657`

**Interfaces:**
- Consumes: `bool EarthManipulator::isAnimationRunning() const`.
- Produces: `const char* earthai::photoCaptureGateError(bool animationRunning)` and a tool that never writes camera state.

- [ ] **Step 1: Add the failing pure gate assertions**

Before the source guard assertions, add:

```cpp
CHECK(std::string(earthai::photoCaptureGateError(true)) ==
      "camera_flight_in_progress");
CHECK(earthai::photoCaptureGateError(false) == NULL);
```

Run:

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Ai_Chat -j2
```

Expected: compile failure because `photoCaptureGateError` is not defined.

- [ ] **Step 2: Implement the minimal gate**

Add to `ai_photo_request.h` above `PhotoRequest`:

```cpp
inline const char* photoCaptureGateError(bool animationRunning)
{
    return animationRunning ? "camera_flight_in_progress" : NULL;
}
```

Replace the camera-reset section in the `generate_photo` lambda with:

```cpp
const char* gateError = earthai::photoCaptureGateError(maniPhoto->isAnimationRunning());
if (gateError)
{
    picojson::object err;
    err["error"] = picojson::value(std::string(gateError));
    return picojson::value(err);
}

picojson::value r = mediaPtr->startPhotoJob(
    request.style, request.lla, request.showCameraPlatform);
```

Do not leave either of these old lines in the tool block:

```cpp
maniPhoto->stopAnimation();
maniPhoto->setByEye(request.lla[0], request.lla[1], request.lla[2]);
```

- [ ] **Step 3: Correct tool and model instructions**

Use this semantic text in the tool description/system prompt:

```cpp
u8"generate_photo 只拍摄屏幕当前可见视角，绝不移动或重置相机。若目标不在当前视角，"
u8"必须先调用 fly_to，等待目标画面出现后再调用 generate_photo。每次请求仍必须传本次"
u8"目标的 lat/lon，坐标只描述照片地点，不控制快门相机。"
```

Keep the existing ISS rule that `show_camera_platform=false` for an ISS viewpoint unless the user explicitly requests visible hardware.

- [ ] **Step 4: Run GREEN**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Ai_Chat -j2
build/osgsol_core/bin/osgVerse_Test_Ai_Chat
```

Expected: exit `0`, ending with the existing AI chat success output; the production source guard passes.

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/ai_photo_request.h \
        applications/earth_explorer/ai_setup.cpp tests/ai_chat_tests.cpp
git commit -m "fix: preserve visible camera during photo capture"
```

---

### Task 3: Lock an independent shutter request at the real capture boundary

**Files:**
- Modify: `applications/earth_explorer/ai_photo_request.h:16-80`
- Modify: `applications/earth_explorer/ai_media.h:334-342`
- Modify: `applications/earth_explorer/ai_media.cpp:774-825,879-895`
- Test: `tests/ai_chat_tests.cpp:629-657`

**Interfaces:**
- Consumes: parsed `earthai::PhotoRequest`, the main camera `osg::Matrixd`, and the monotonically increasing job id.
- Produces: `earthai::PhotoCaptureRequest makePhotoCaptureRequest(...)` and per-job capture state.

- [ ] **Step 1: Write the failing value-isolation test**

Add `<osg/Matrixd>` to `ai_photo_request.h` includes only after the test is written. In the test, add:

```cpp
osg::Matrixd hongKongMatrix = osg::Matrixd::rotate(0.4, osg::X_AXIS);
osg::Matrixd nvidiaMatrix = osg::Matrixd::rotate(0.8, osg::Y_AXIS);

earthai::PhotoRequest hongKongInput;
hongKongInput.lla.set(22.298 * 0.017453292519943295,
                      114.172 * 0.017453292519943295, 500.0);
hongKongInput.style = "harbour";

earthai::PhotoRequest nvidiaInput;
nvidiaInput.lla.set(37.3707 * 0.017453292519943295,
                    -121.9631 * 0.017453292519943295, 800.0);
nvidiaInput.style = "campus";

const earthai::PhotoCaptureRequest first =
    earthai::makePhotoCaptureRequest(hongKongInput, hongKongMatrix, 41);
const earthai::PhotoCaptureRequest second =
    earthai::makePhotoCaptureRequest(nvidiaInput, nvidiaMatrix, 42);

CHECK(first.requestId == 41);
CHECK(second.requestId == 42);
CHECK(first.targetLla != second.targetLla);
CHECK(first.visibleCameraMatrix != second.visibleCameraMatrix);
CHECK(first.style == "harbour");
CHECK(second.style == "campus");
```

Run the AI target build. Expected: compile failure because the capture type/factory does not exist.

- [ ] **Step 2: Add the value type and factory**

Add to `ai_photo_request.h` after `PhotoRequest`:

```cpp
struct PhotoCaptureRequest
{
    osg::Vec3d targetLla;
    osg::Matrixd visibleCameraMatrix;
    std::string style;
    bool showCameraPlatform = false;
    long long requestId = 0;
};

inline PhotoCaptureRequest makePhotoCaptureRequest(
    const PhotoRequest& input, const osg::Matrixd& visibleMatrix, long long requestId)
{
    PhotoCaptureRequest request;
    request.targetLla = input.lla;
    request.visibleCameraMatrix = visibleMatrix;
    request.style = input.style;
    request.showCameraPlatform = input.showCameraPlatform;
    request.requestId = requestId;
    return request;
}
```

- [ ] **Step 3: Wire the shutter boundary**

Add this include to `ai_media.h` so its value fields have complete types:

```cpp
#include "ai_photo_request.h"
```

Replace the loose pending photo fields with these additional value fields while retaining `_snapPath`, `_genPath`, `_prompt`, and state fields:

```cpp
PhotoRequest _pendingPhotoInput;
PhotoCaptureRequest _captureRequest;
long long _photoRequestId;
```

In `startPhotoJob`, copy the input values and job-derived id without reading or changing the camera:

```cpp
_pendingPhotoInput.lla = lla;
_pendingPhotoInput.style = stylePrompt;
_pendingPhotoInput.showCameraPlatform = showCameraPlatform;
_photoRequestId = _jobId;
```

In `WAITING_VIEW_RENDER`, immediately before `hudHide()`/`grab()`, lock the latest main-camera view:

```cpp
if (!_viewer || !_viewer->getCamera())
{
    _jobs.update(_jobId, AIJob::FAILED, 1.0f, "", "camera unavailable");
    _state = IDLE;
    return;
}
_captureRequest = makePhotoCaptureRequest(
    _pendingPhotoInput, _viewer->getCamera()->getViewMatrix(), _photoRequestId);
_prompt = buildPhotoPrompt(_captureRequest.targetLla, _captureRequest.style,
                           _captureRequest.showCameraPlatform);
hudHide();
_grabber.grab(_snapPath);
```

Remove the earlier prompt construction from `startPhotoJob`; prompt creation now occurs exactly once from the shutter request.

- [ ] **Step 4: Run focused and offline tests**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Ai_Chat -j2
build/osgsol_core/bin/osgVerse_Test_Ai_Chat
ctest --test-dir build/osgsol_core -L offline --output-on-failure
git diff --check
```

Expected: focused test exits `0`; all offline tests pass; diff check is clean.

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/ai_photo_request.h \
        applications/earth_explorer/ai_media.h \
        applications/earth_explorer/ai_media.cpp tests/ai_chat_tests.cpp
git commit -m "test: isolate photo shutter requests"
```

---

### Task 4: Build and manually verify the visible-camera flow

**Files:**
- Modify: `docs/superpowers/plans/2026-07-11-photo-visible-camera-plan.md` (append evidence only)

**Interfaces:**
- Consumes: completed Tasks 1-3.
- Produces: an evidence record for NVIDIA, ISS, and Hong Kong→NVIDIA isolation.

- [ ] **Step 1: Build the application sequentially**

```bash
cmake --build build/osgsol_core --target osgVerse_EarthExplorer -j2
```

Expected: exit `0` with only pre-existing macOS OpenGL/duplicate-library warnings.

- [ ] **Step 2: Run the deterministic fake-photo path**

```bash
env -u EARTH_AI_KEY \
  EARTH_AI_FAKE="/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/applications/earth_explorer/test/ai_fake_photo2.json" \
  EARTH_AI_FAKE_IMG="/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/assets/misc/earth_rendering.jpg" \
  EARTH_OFFSCREEN=1 EARTH_AUTOCAP=300 \
  build/osgsol_core/bin/osgVerse_EarthExplorer \
  --goto 37.3707 -121.9631 0.8
```

Expected: no crash; two completed jobs use distinct job ids and distinct `snap_*/gen_*` paths; logs do not show a `generate_photo`-initiated `fly_to`/`setByEye`.

- [ ] **Step 3: Manual visual acceptance**

In the fixed app build before packaging:

1. Fly to NVIDIA headquarters, middle-drag to a clear oblique view, then ask for a photo. The globe must not jump to top-down at the shutter.
2. Fly to the current ISS subpoint, choose an oblique orbital composition, then ask for an ISS-view photo. The snapshot must follow the visible composition and must not add solar panels unless explicitly requested.
3. Generate Hong Kong, then fly to NVIDIA and generate again. The second job must contain no Hong Kong image, coordinates, prompt suffix, or output path.

- [ ] **Step 4: Record evidence and commit**

Append the focused test result, offline count, fake job ids/paths, and the three manual outcomes under an `Implementation evidence` heading, then:

```bash
git add docs/superpowers/plans/2026-07-11-photo-visible-camera-plan.md
git commit -m "docs: record visible-camera photo verification"
```

## Implementation evidence

Recorded on 2026-07-11 in
`/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` at Task 3 commit `76c8abb`.

- Sequential application build:
  `cmake --build build/osgsol_core --target osgVerse_EarthExplorer -j2` exited `0` and ended with
  `[100%] Built target osgVerse_EarthExplorer`. This incremental build emitted no compiler or linker
  warnings.
- Focused regression:
  `cmake --build build/osgsol_core --target osgVerse_Test_Ai_Chat -j2` exited `0`, and
  `build/osgsol_core/bin/osgVerse_Test_Ai_Chat` exited `0`, including
  `generate_photo independent target tests OK` and final `AIChatCore::addErrorNote appends ERR entry OK`.
- Offline regression:
  `ctest --test-dir build/osgsol_core -L offline --output-on-failure` passed 13/13 tests with 0
  failures in 11.07 seconds.
- The exact fake-photo command printed in Task 4 exited `0` but submitted no chat input and therefore
  produced 0 photo jobs. `EARTH_AI_FAKE` supplies scripted provider responses; it does not initiate a
  request. To exercise both scripted requests, the same command was rerun with the existing headless
  test hooks `EARTH_AI_AUTOSUBMIT="first photo"` and
  `EARTH_AI_AUTOSUBMIT2="second photo"`.
- The corrected deterministic run exited `0` and completed two independent jobs:
  - job `1`: nominal snapshot
    `/Users/USER/Pictures/EarthExplorer/snap_1783741228_1.png` (actual OSG capture
    `/Users/USER/Pictures/EarthExplorer/snap_1783741228_1_0.png`) and generated output
    `/Users/USER/Pictures/EarthExplorer/gen_1783741228_1.png`;
  - job `2`: nominal snapshot
    `/Users/USER/Pictures/EarthExplorer/snap_1783741228_2.png` (actual OSG capture
    `/Users/USER/Pictures/EarthExplorer/snap_1783741228_2_0.png`) and generated output
    `/Users/USER/Pictures/EarthExplorer/gen_1783741228_2.png`.
- Both snapshot artifacts exist as 1920x1080 PNG files (82,420 bytes each), and both generated
  artifacts exist (449,281 bytes each). Equal content hashes are expected here: the camera remains at
  the same visible `--goto` composition for both metadata-only targets, while `EARTH_AI_FAKE_IMG`
  deliberately copies the same fixture for both generated outputs. The job ids and all four paths are
  distinct.
- The corrected run logged only `generate_photo` tool calls for the two requests and no `fly_to`.
  The passing focused regression independently guards the production `generate_photo` block against
  `setByEye`, `moveTo`, and `stopAnimation`, and verifies that the animation gate remains present.
- Runtime warnings were pre-existing/environmental: missing optional Gaussian-splatting shaders and
  strategic-feed fixtures from the executable working directory, macOS OpenGL debug/invalid-operation
  warnings, and NASA GIBS 404 tile responses. They did not crash the run or prevent both jobs from
  reaching `photo job done`.

### Manual visual outcomes

These three acceptance cases remain **pending final manual testing**. The offscreen/headless command
cannot perform or observe a middle-drag composition, inspect the visible frame at shutter time, verify
ISS imagery for unwanted solar panels, or visually inspect the second real generated image for leaked
Hong Kong content. No headless result is recorded as a visual pass.

1. NVIDIA headquarters oblique composition and no shutter-time top-down jump: **pending**.
2. Current ISS-subpoint oblique composition, visible-view match, and no unsolicited platform/solar
   panels: **pending**.
3. Hong Kong generation followed by NVIDIA generation with no image, coordinate, prompt-suffix, or
   output-path leakage: **pending**.
