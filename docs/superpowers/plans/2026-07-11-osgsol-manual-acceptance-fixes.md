# osgSol Manual Acceptance Fixes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close four independently reproduced manual-test defects: stale satellite selection overlays, slow science rasters, middle-button press jumps, and LOD-driven camera-height reversals.

**Architecture:** Each defect remains an independent task, test cycle, commit, and review gate. Satellite cleanup couples selection lifetime to selection-owned geometry; science overlays use the current source with bounded zoom and a configurable image pool; middle-button setup acquires a pivot without rebasing the camera center; terrain-floor sampling keeps a stationary-position high-water floor so LOD refinement may protect the camera but cannot pull it back down automatically.

**Tech Stack:** C++17, OpenSceneGraph 3.6.5, CMake/CTest, existing EarthExplorer offline seams, macOS offscreen packaging smoke.

## Global Constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on `codex/v0.2-runtime-safety`.
- Do not modify `/Users/USER/osgverse`, `/Users/USER/osgsol` stable checkout, `master`, or the `v0.1.0` tag.
- Every production change must first have a failing behavior or bounded production-wiring test.
- Run only one CMake build at a time.
- Do not create `v0.2.0`; the manual-test app remains an untagged branch build.
- Final Desktop delivery updates `/Users/USER/Desktop/osgSol Earth.app` in place; never create a timestamped Desktop app.
- Preserve the v0.1 wheel-up/wheel-down and independent-photo-request regressions.

---

### Task 1: Remove every satellite selection overlay when selection clears

**Files:**

- Modify: `applications/earth_explorer/sat_data.cpp`
- Modify: `tests/satellite_tests.cpp`

**Interfaces:**

- `SatelliteLayerImpl::clearSelected()` invalidates the selected satellite and publishes one atomic orbit rebuild request.
- `SatelliteLayerImpl::pickAt()` treats a miss, an empty visible set, or an unusable camera viewport as a clear-selection result.
- Disabling the category that owns the current selection clears that selection.
- `rebuildOrbitLines()` creates trajectory and footprint geometry only while `_selected.valid`; ISS and Tiangong no longer have persistent unselected trajectories.

- [ ] **Step 1: Add failing cleanup and threading assertions**

Extend the bounded source-body tests in `tests/satellite_tests.cpp`. Assert non-empty bodies and require:

```cpp
// Product semantics confirmed by the user on 2026-07-11.
// No valid selection means no trajectory and no footprint, including ISS/Tiangong.
CHECK(rebuildOrbitLinesBody.find("if (!sel.valid) return") != std::string::npos);
CHECK(rebuildOrbitLinesBody.find("if (_catStation) for") == std::string::npos);

// A click miss and hiding the owning category both clear the selection.
CHECK(pickAtBody.find("clearSelected()") != std::string::npos);
CHECK(setCategoryBody.find("clearSelected()") != std::string::npos);

// Draw/update invalidation is atomic; consuming it is one-shot.
CHECK(source.find("std::atomic<bool> _selectedOrbitDirty") != std::string::npos);
CHECK(syncBody.find("_selectedOrbitDirty.exchange(false)") != std::string::npos);
```

Also assert that selecting B still replaces A and creates only B-owned geometry.

- [ ] **Step 2: Verify RED**

Run:

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Satellite -j2
build/osgsol_core/bin/osgVerse_Test_Satellite
```

Expected: the cleanup contract fails because misses are no-ops, station trajectories are persistent, category disable does not clear selection, and the dirty flag is a plain `bool`.

- [ ] **Step 3: Implement the minimal selection-lifetime fix**

Make `_selectedOrbitDirty` atomic and consume it with `exchange(false)` in `syncIfDirty()`. Clear selection on a pick miss and when disabling the selected satellite's category. Remove the unconditional ISS/Tiangong station-orbit loop from `rebuildOrbitLines()`; when selection is invalid, `_orbitRoot` remains empty.

Do not change propagation, category fetching, marker visibility, or selected footprint math.

- [ ] **Step 4: Verify GREEN and commit**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Satellite osgVerse_EarthExplorer -j2
build/osgsol_core/bin/osgVerse_Test_Satellite
ctest --test-dir build/osgsol_core -L offline --output-on-failure
git diff --check
git add applications/earth_explorer/sat_data.cpp tests/satellite_tests.cpp
git commit -m "fix: clear satellite selection overlays"
```

---

### Task 2: Remove avoidable science-overlay latency

**Files:**

- Create: `applications/earth_explorer/science_overlay.h`
- Create: `applications/earth_explorer/science_image_pager.h`
- Modify: `applications/earth_explorer/earth_main.cpp`
- Modify: `readerwriter/TileCallback.h`
- Modify: `readerwriter/TileCallback.cpp`
- Modify: `plugins/osgdb_tms/ReaderWriterTMS.cpp`
- Modify: `tests/tile_overlay_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

```cpp
namespace earthscience
{
    inline const std::string& ndviTemplate();
    inline const std::string& nightlightsTemplate();
    inline int nativeMaxZoom(const std::string& overlayPath);

    class ScienceImagePager : public osgDB::ImagePager
    {
    public:
        explicit ScienceImagePager(unsigned int totalThreads = 8);
    };
}
```

```cpp
bool TileManager::tryGetLayerPath(TileCallback::LayerType id,
                                  std::string& path) const;
```

`TileManager` serializes `setLayerPath`, `tryGetLayerPath`, `getLayerPath`, and `check` with one mutex. A new TMS child uses the current global OVERLAY path when one has been published; only the pre-publication startup path falls back to cloned Options.

- [ ] **Step 1: Add failing native-zoom, current-source, and pool tests**

Extend `osgVerse_Test_TileOverlay` to assert:

```cpp
CHECK(earthscience::nativeMaxZoom(earthscience::ndviTemplate()) == 9);
CHECK(earthscience::nativeMaxZoom(earthscience::nightlightsTemplate()) == 8);
CHECK(earthscience::nativeMaxZoom("gebco") == 8);

earthscience::ScienceImagePager pager(8);
CHECK(pager.getNumImageThreads() == 8);
```

Add a fake-reader/new-child seam that publishes NDVI in `TileManager`, supplies stale `Overlay=gibs` Options, and asserts the child requests the current NDVI source exactly once and never requests GIBS. Add bounded wiring checks that `requestImageFile` receives `_z` instead of constant priority `0.0`.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_TileOverlay -j2
build/osgsol_core/bin/osgVerse_Test_TileOverlay
```

Expected: native zoom, configurable pool, current-source precedence, and nonconstant priority assertions fail.

- [ ] **Step 3: Implement only the measured bottleneck fixes**

- Move the two NASA templates into `science_overlay.h`, then make NDVI stop above z9, nightlights above z8, and GEBCO above z8 by routing `createCustomPath()` through `earthscience::nativeMaxZoom()`.
- Install `ScienceImagePager(8)` on the viewer before scene traversal begins. Its base three workers remain and it appends five `HANDLE_ALL_REQUESTS` workers.
- Pass `_z` as `timeToMergeBy` so coarse tiles sort before fine tiles.
- Protect `TileManager::_layerPaths` with a mutex and add `tryGetLayerPath()` so explicit empty paths remain distinguishable from an unpublished path.
- In `ReaderWriterTMS::createTile()`, prefer the published current OVERLAY path over the stale cloned `Overlay=gibs` option. This removes the serial old-GIBS download before the correct science request.

Do not change science imagery, opacity, cache location, source URLs, or the single-OVERLAY mutual-exclusion model. Source-generation cancellation is outside this bounded fix.

- [ ] **Step 4: Verify GREEN and commit**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_TileOverlay osgVerse_EarthExplorer -j2
build/osgsol_core/bin/osgVerse_Test_TileOverlay
ctest --test-dir build/osgsol_core -L offline --output-on-failure
git diff --check
git add applications/earth_explorer/science_overlay.h \
  applications/earth_explorer/science_image_pager.h \
  applications/earth_explorer/earth_main.cpp readerwriter/TileCallback.h \
  readerwriter/TileCallback.cpp plugins/osgdb_tms/ReaderWriterTMS.cpp \
  tests/tile_overlay_tests.cpp tests/CMakeLists.txt
git commit -m "perf: accelerate science overlay loading"
```

---

### Task 3: Keep middle-button press from rebasing the camera

**Files:**

- Modify: `readerwriter/EarthManipulator.h`
- Modify: `readerwriter/EarthManipulator.cpp`
- Create: `tests/earth_manipulator_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**

```cpp
bool EarthManipulator::calcTiltCenter(bool useCameraMatrix,
                                      bool updateViewingRadius = true);
```

The middle-button setup path acquires `_tiltCenter` and `_rotateAxis` with `updateViewingRadius=false`. Existing navigation paths such as `setByEye()` retain `updateViewingRadius=true`.

- [ ] **Step 1: Add a failing middle-PUSH behavior test**

Create an offline EarthManipulator fixture with a fixed viewer projection and synthetic radial surface. Seed `_center` to a radius different from the screen-center hit, snapshot `getCenter()` and `getMatrix()`, then send a middle-button `PUSH` without a `DRAG`:

```cpp
const osg::Vec3d centerBefore = manipulator->getCenter();
const osg::Matrixd matrixBefore = manipulator->getMatrix();
CHECK(manipulator->handle(*middlePush, view));
CHECK_VEC_NEAR(manipulator->getCenter(), centerBefore, 1e-9);
CHECK_MATRIX_NEAR(manipulator->getMatrix(), matrixBefore, 1e-9);
```

Keep a left-button control and assert two different middle press coordinates no longer rebase `_center`.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_EarthManipulator -j2
build/osgsol_core/bin/osgVerse_Test_EarthManipulator
```

Expected: middle PUSH changes `_center` and the matrix before any drag.

- [ ] **Step 3: Separate pivot acquisition from camera-radius mutation**

Add the `updateViewingRadius` parameter. `calcTiltCenter()` always computes `_tiltCenter`; only update `_center = _initEyeDir * realRadius` when the flag is true. `performRotateAxis()` passes false, so the press can establish a rotation axis without moving the camera. Preserve the existing default for `setByEye()` and other callers.

Do not enable the disabled `#if 0` drag compensation or change pan/scroll semantics in this task.

- [ ] **Step 4: Verify GREEN and commit**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_EarthManipulator osgVerse_EarthExplorer -j2
build/osgsol_core/bin/osgVerse_Test_EarthManipulator
ctest --test-dir build/osgsol_core -L offline --output-on-failure
git diff --check
git add readerwriter/EarthManipulator.h readerwriter/EarthManipulator.cpp \
  tests/earth_manipulator_tests.cpp tests/CMakeLists.txt
git commit -m "fix: preserve camera center on middle press"
```

---

### Task 4: Stop stationary LOD refinement from pulling camera height back down

**Files:**

- Create: `readerwriter/TerrainFloorState.h`
- Modify: `readerwriter/EarthManipulator.h`
- Modify: `readerwriter/EarthManipulator.cpp`
- Modify: `tests/earth_manipulator_tests.cpp`

**Interfaces:**

```cpp
namespace osgVerse
{
    struct TerrainFloorState
    {
        bool hasSample = false;
        double altitude = 0.0;
    };

    inline void updateTerrainFloorSample(TerrainFloorState& state,
                                         bool movedToNewCell,
                                         bool hit,
                                         double altitude);
}
```

For a stationary horizontal cell, valid higher samples raise the high-water altitude and lower samples do not reduce it. A real horizontal move beyond 0.003 degrees starts a new cell and accepts its sample. A temporary miss in the same cell preserves the last valid floor instead of switching to a fallback and back.

- [ ] **Step 1: Add the failing recorded-sequence regression**

Feed the exact manual-diagnosis sequence into the pure state helper:

```cpp
const double samples[] = {0.0, -4602.8, 5324.9, 5099.87, 5087.47,
                          5081.13, 5078.19};
for (double sample : samples)
    updateTerrainFloorSample(state, false, true, sample);
CHECK_NEAR(state.altitude, 5324.9, 1e-6);

updateTerrainFloorSample(state, false, false, 0.0);
CHECK_NEAR(state.altitude, 5324.9, 1e-6);
updateTerrainFloorSample(state, true, true, 5078.19);
CHECK_NEAR(state.altitude, 5078.19, 1e-6);
```

Add a threshold test proving `0.003°` is converted to radians and movement just beyond approximately 333m starts a new cell.

- [ ] **Step 2: Verify RED**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_EarthManipulator -j2
build/osgsol_core/bin/osgVerse_Test_EarthManipulator
```

Expected: the state helper is absent and runtime currently replaces the probe altitude on every periodic LOD hit.

- [ ] **Step 3: Use a stationary-cell high-water terrain floor**

Route `updateTerrainFloor()` samples through `TerrainFloorState`. Preserve a valid sample across same-cell misses; accept only higher same-cell LOD samples; reset to the new sample after real horizontal movement. Replace the incorrect `0.003`-radian threshold with `osg::DegreesToRadians(0.003)`.

Keep immediate upward collision protection, `_terrainMargin=150m`, the hard sea-level fallback, the 15-frame probe cadence, and user-driven zoom/pan behavior unchanged. The change removes only automatic downward correction caused by parent/child LOD replacement while the camera is stationary.

- [ ] **Step 4: Verify GREEN and commit**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_EarthManipulator osgVerse_EarthExplorer -j2
build/osgsol_core/bin/osgVerse_Test_EarthManipulator
ctest --test-dir build/osgsol_core -L offline --output-on-failure
git diff --check
git add readerwriter/TerrainFloorState.h readerwriter/EarthManipulator.h \
  readerwriter/EarthManipulator.cpp tests/earth_manipulator_tests.cpp
git commit -m "fix: stabilize terrain floor across lod changes"
```

---

### Task 5: Integrated verification and fixed Desktop manual-test package

**Files:**

- Modify: `docs/superpowers/plans/2026-07-11-osgsol-manual-acceptance-fixes.md`

- [ ] **Step 1: Rebuild affected targets sequentially**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Satellite -j2
cmake --build build/osgsol_core --target osgVerse_Test_TileOverlay -j2
cmake --build build/osgsol_core --target osgVerse_Test_EarthManipulator -j2
cmake --build build/osgsol_core --target osgVerse_EarthExplorer -j2
```

- [ ] **Step 2: Run complete offline and source-preservation gates**

```bash
ctest --test-dir build/osgsol_core -L offline --output-on-failure
rg -n 'resolveImGuiWheelAmount|OSGSOL_HAS_PHOTO_REQUEST|_videoRequests.drain' \
  applications/earth_explorer tests
git diff --check
```

- [ ] **Step 3: Run independent offscreen diagnostics**

- Satellite fixture: select, clear, and confirm `_orbitRoot` has zero children.
- Science cold-cache smoke: compare first-visible time and confirm no GIBS request after NDVI/GEBCO publication.
- Middle PUSH fixture: confirm camera matrix is byte-stable before first drag.
- Terrain sequence: confirm stationary lower LOD samples do not reduce the latched floor.
- General Earth offscreen capture: exit `0`, `1920x1080`, non-empty PNG.

- [ ] **Step 4: Update the fixed Desktop app and verify it after runtime**

Build/install/package from this worktree with `EARTH_AI_KEY` unset. Update only `/Users/USER/Desktop/osgSol Earth.app`, retain bundle id `com.anloren.osgsol.earth`, set build channel `manual-test`, and record the final source commit. After the Desktop-path offscreen smoke, require:

```bash
codesign --verify --deep --strict "/Users/USER/Desktop/osgSol Earth.app"
test -z "$(find "/Users/USER/Desktop/osgSol Earth.app" -name imgui.ini -print -quit)"
test -z "$(find "/Users/USER/Desktop/osgSol Earth.app/Contents/MacOS" \
  -maxdepth 1 -name 'libhv.*.log' -print -quit)"
```

- [ ] **Step 5: Record evidence and commit**

Append exact target results, offline count, science timing comparison, screenshot dimensions/hash, package version/commit, and signature result to this tracked plan. Then:

```bash
git add docs/superpowers/plans/2026-07-11-osgsol-manual-acceptance-fixes.md
git commit -m "docs: record manual acceptance fixes"
git push origin codex/v0.2-runtime-safety
```

Do not merge, tag, or create a pull request after this checkpoint.
