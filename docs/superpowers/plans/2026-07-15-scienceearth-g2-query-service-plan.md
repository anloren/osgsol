# ScienceEarth G2-1 Unified Query Service Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put the accepted AlphaEarth preview behind a generic, GDAL-free science query service so the UI, Agent, and renderer share one source catalog and one job state without changing the verified Earth, camera, photo, terrain, 3D Tiles, or AlphaEarth rendering behavior.

**Architecture:** Add an isolated `osgSolScienceCore` target for contracts, provider ownership, registry, and the single-active-job state machine. Keep the current v5 `SciencePreviewRuntime` intact behind `AlphaEarthProvider`; migrate all three consumers to `ScienceQueryService`. A replacement query retains the last successful artifact until a new matching provider generation succeeds, and stale generations are ignored.

**Tech Stack:** C++17, CMake/CTest, OpenSceneGraph 3.6.5, existing ImGui/Agent tool registry, existing private static GDAL 3.13.1 v5 prefix, macOS packaging/codesign/offscreen smoke.

## Global Constraints

- Work only on `codex/scienceearth-g2-query-service` from this linked worktree.
- Treat `v0.3.0` / `ScienceEarth-v0.3.0` at `929fab38...` as the product behavior oracle; never move those tags.
- Treat `v0.2.0` / `ScienceEarth` at `9b1988b9...` as the protected pre-science rollback; never move or overwrite it.
- Use only `build/science-deps-g2-v5/prefix`. Never build or link the quarantined v6 prefix or cherry-pick only part of `codex/scienceearth-g0-v6-wip`.
- Do not rewrite `SciencePreviewRuntime` networking, GDAL reads, row orientation, raster-window selection, projected ground grid, opacity, or depth behavior.
- Do not modify camera, terrain, basemap, 3D Tiles, photography, satellites, or other non-science layer code in this slice.
- Do not add Sentinel-2, Copernicus DEM, STAC discovery, new credentials, or new remote endpoints.
- The service and provider have no OSG, ImGui, `LayerManager`, camera, or AI dependencies.
- The UI and Agent may toggle only the existing science layer. They must never write a camera matrix or initiate navigation.
- Follow TDD: write the focused failing test, observe the intended failure, implement the minimum behavior, then rerun the focused and protected regressions.
- Commit each completed task separately. Do not tag, merge, or push a release before manual acceptance.
- The first user package is an untagged manual candidate and updates only `/Users/USER/Desktop/osgSol Earth.app`; do not create a second Desktop app.

## Current State Ledger

This plan starts from the authoritative record in
`docs/scienceearth/g0-product-closure-2026-07-15.md` and the approved design in
`docs/superpowers/specs/2026-07-14-scienceearth-g2-query-service-design.md`.

```text
ACTIVE_BRANCH=codex/scienceearth-g2-query-service
ACTIVE_HEAD=3fa9f6083bf216bdcad1ee3174bc38391e98e7f0
G0_PRODUCT_BASELINE=ACCEPTED
G0_FORMAL_GATE=NOT_GO
G2_PROCEED=YES
G0_V6_WIP_BRANCH=codex/scienceearth-g0-v6-wip
G0_V6_WIP_COMMIT=f37e5e9560d4d610efc31ae49359e035ab0897d2
G0_V6_WIP=QUARANTINED_NOT_RELEASE_READY
G2_V5_SCIENCE_OFF=20_OF_20
DESKTOP_VERSION=0.3.0
DESKTOP_UPDATE_IN_THIS_PLAN=ONLY_AFTER_ALL_AUTOMATED_GATES
NEXT_RELEASE_TAG=FORBIDDEN_UNTIL_MANUAL_ACCEPTANCE
G2_TASKS_1_TO_7=COMPLETE
G2_TASK_8_FULL_REGRESSION=NOT_YET_RUN
G2_TASK_9_MANUAL_CANDIDATE=NOT_YET_BUILT
DESKTOP_APP=UNCHANGED_ACCEPTED_V0_3_0_BASELINE
```

The committed implementation through Task 7 is clean at `3fa9f60`. A fresh v5 private dependency
prefix exists at
`build/science-deps-g2-v5/prefix` and passes its archive, capability, static-prefix, and manifest
verification. The fixed Desktop app is the accepted product baseline, but its current Finder
extended attributes make a fresh strict signature check fail, and it still has a direct Homebrew
Python 3.14 dependency. Those are recorded distribution debts; the candidate packaging task must
remove extended attributes before final signing, while Python relocation remains mandatory before
claiming clean-machine distribution.

## Required Follow-up Register

### Required in this G2-1 slice before the user receives a candidate

- [x] Generic contracts compile in `osgSolScienceCore` with no GDAL, OSG, UI, or AI link edge.
- [x] Registry/provider tests prove deterministic catalog and provider ownership.
- [x] Service tests prove validation, cancellation, job-id monotonicity, stale-result rejection,
      last-good retention, explicit clear, and shutdown cancellation.
- [x] AlphaEarth adapter preserves descriptor, request, progress, failure, and artifact data.
- [x] Preview renderer, UI, and all four Agent tools consume `ScienceQueryService`, not
      `SciencePreviewRuntime`.
- [x] Existing georeference, orientation, view-target, display-resolution, opacity, terrain-depth,
      and back-face regression tests stay green.
- [ ] Science-off remains 20/20 and selects no science targets.
- [ ] Science-enabled offline suite, local-index smoke, app build, package audit, offscreen render,
      and strict/deep codesign all pass.
- [ ] The fixed Desktop app is updated in place only from the verified untagged candidate.
- [ ] Manual matrix covers catalog, load, year change, retained result on failure, cancel,
      hide/show/remove, bidirectional scrolling, low-altitude placement, camera invariance, Agent
      query, photo regression, 3D Tiles regression, and Quit.

### Required before any later remote science provider

- [ ] Define provider-specific latency, transfer, memory, cache, cancellation, and failure-retention
      budgets from its real data shape.
- [ ] Preserve full provenance: source, provider version, dataset, requested/actual coverage, time,
      variables, units, visualization, processing, and attribution.
- [ ] Demonstrate visible progress/cancellation and last-good retention under real failure cases.

### Required before claiming clean-machine macOS distribution

- [ ] Remove, bundle, or make relocatable the direct `/opt/homebrew/opt/python@3.14` dependency.
- [ ] Test the signed app on a Mac without the development Homebrew tree or private build prefixes.
- [ ] Rerun forbidden-path, unresolved-dependency, signature, launch, and fixed-Desktop smoke checks.

### Conditional only if v6 is revived

- [ ] Fix both documented v6 ownership failures without weakening the tests.
- [ ] Rebuild a new private prefix from the final committed patch and matching pin.
- [ ] Pass the full offline HTTP/2, ownership, abandonment, capacity, dependency, science-off,
      packaging, and signature matrices before merge or packaging.

---

## Task 1: Add the GDAL-free contracts and isolated core target

**Files:**

- Create: `science/ScienceQueryTypes.h`
- Create: `science/ScienceQueryTypes.cpp`
- Create: `science/ScienceQueryTypesTest.cpp`
- Modify: `science/CMakeLists.txt`
- Modify: `CMakeLists.txt:806-820`
- Modify: `tests/science_build_contract_tests.cpp`

- [x] **Step 1: Write the failing type and build-isolation tests**

Create `ScienceQueryTypesTest.cpp` with compile-time/default-value and value-semantics checks for:

```cpp
earthscience::ScienceSourceDescriptor source;
earthscience::GeoTemporalQuery query;
earthscience::ScienceArtifact artifact;
earthscience::ScienceJobSnapshot snapshot;
```

Require that a raster artifact can carry an immutable RGBA buffer and the exact existing
`ScienceGroundGrid` without GDAL or OSG types. Extend `science_build_contract_tests.cpp` so a
science-enabled configure requires `osgSolScienceCore` in `OSGSOL_BUILD_TARGETS_VALUE`, and the
generated phase is `G2-1`; a science-off configure requires an empty science target list.

Run:

```bash
cmake -S . -B build/science_g2 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVERSE_BUILD_EXAMPLES=ON \
  -DOSGSOL_BUILD_SCIENCE=ON \
  -DOSGSOL_SCIENCE_DEPS_ROOT="$PWD/build/science-deps-g2-v5/prefix"
cmake --build build/science_g2 --target osgSol_Test_ScienceQueryTypes \
  osgVerse_Test_ScienceBuildContract -j4
```

Expected: configure or build fails because the new target/types and `G2-1` contract do not yet
exist.

- [x] **Step 2: Define the bounded generic types**

Define explicit enums rather than stringly typed state:

```cpp
enum class ScienceSourceHealth { Unavailable, Ready, Busy, Degraded };
enum class ScienceJobState { Unavailable, Idle, Queued, Fetching, Ready, Failed, Cancelled };
enum class ScienceGeometryKind { Point, BoundingBox, CurrentView };
enum class ScienceTimeMode { Instant, Interval, ExplicitYears };
enum class ScienceAggregation { None, Mean, Minimum, Maximum };
enum class ScienceOutputKind { RasterLayer, Table, VectorFeatures };
enum class SciencePriority { Visible, InteractiveResearch, Background };
```

Implement the design's descriptors, geometry/time/limits, query, source reference, raster payload,
artifact, and job snapshot. For the verified AlphaEarth path, `ScienceGeometry` must carry a finite
WGS84 point and `requestedSpanMeters`; `ScienceTimeSelection` must support exactly one year in its
`explicitYears` vector. `ScienceRasterPayload` owns immutable RGBA and ground-grid pointers.

`ScienceQueryTypes.cpp` implements stable `scienceSourceHealthName()` and
`scienceJobStateName()` conversions used by UI and Agent JSON. Unknown enum values return
`"unknown"`; consumers must not duplicate their own state-name switches.

Use neutral generic field names such as `providerVersion`, `nativeResolutionMeters`,
`visualizations`, `sourceReferences`, and `lastSuccessfulArtifact`. Do not put AlphaEarth URLs,
band defaults, or UI labels in this header.

- [x] **Step 3: Split `osgSolScienceCore` from the preview target**

In `science/CMakeLists.txt`, add:

```cmake
ADD_LIBRARY(osgSolScienceCore STATIC
    ScienceQueryTypes.cpp
    ScienceQueryTypes.h)
TARGET_COMPILE_FEATURES(osgSolScienceCore PUBLIC cxx_std_17)
TARGET_INCLUDE_DIRECTORIES(osgSolScienceCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
SET_TARGET_PROPERTIES(osgSolScienceCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden)
```

Register `osgSol_Test_ScienceQueryTypes` linked only to `osgSolScienceCore`. Link
`osgSolSciencePreview` publicly to `osgSolScienceCore`; keep all GDAL/PROJ/ZSTD/curl/SQLite paths
private to the preview target. Update the generated build contract phase to `G2-1`.

- [x] **Step 4: Prove the core has no forbidden link edge**

Run:

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceQueryTypes \
  osgVerse_Test_ScienceBuildContract -j4
ctest --test-dir build/science_g2 --output-on-failure \
  -R 'ScienceQueryTypes|ScienceBuildContract'
sed -n '1,120p' \
  build/science_g2/science/CMakeFiles/osgSolScienceCore.dir/link.txt
! rg -n '#include[[:space:]]*[<\"](gdal|cpl|ogr|osg|imgui)|osg::|ImGui|LayerManager|earthai' \
  science/ScienceQueryTypes.h science/ScienceQueryTypes.cpp
```

Expected: focused tests pass and the isolated static core build has no forbidden dependency token.

- [x] **Step 5: Commit**

```bash
git add science/ScienceQueryTypes.h science/ScienceQueryTypes.cpp \
  science/ScienceQueryTypesTest.cpp \
  science/CMakeLists.txt CMakeLists.txt tests/science_build_contract_tests.cpp
git commit -m "feat(scienceearth): add isolated query contracts"
```

### Task 1 Evidence

- RED: `osgSol_Test_ScienceQueryTypes` failed because `ScienceQueryTypes.h` did not exist.
- RED: `osgVerse_Test_ScienceBuildContract` failed because the generated phase was still
  `G0-G1`.
- GREEN: science-enabled query-type/build-contract tests passed `2/2`; the science-off build
  contract passed `1/1` and selected no science target.
- Isolation: `libosgSolScienceCore.a` contains only `ScienceQueryTypes.cpp.o`; the generic source
  contains no GDAL, OSG, ImGui, LayerManager, or Agent dependency.
- Protected preview regression: passed `1/1`. The first build compiled existing third-party
  dependencies and emitted their pre-existing warnings; the regression itself passed cleanly.

## Task 2: Add provider ownership and deterministic source registry

**Files:**

- Create: `science/ScienceProvider.h`
- Create: `science/ScienceSourceRegistry.h`
- Create: `science/ScienceSourceRegistry.cpp`
- Create: `science/ScienceSourceRegistryTest.cpp`
- Modify: `science/CMakeLists.txt`

- [x] **Step 1: Write a fake provider and failing registry cases**

The test fake records submit/cancel/clear calls and exposes a mutable provider snapshot. Test:

- empty provider ids are rejected with `science provider id is empty`;
- duplicate ids are rejected without replacing the first provider;
- lookup returns the owned provider for an exact stable id;
- missing lookup returns null;
- `listSources()` is deterministic id order, including unavailable/degraded sources;
- destroying the registry destroys each owned provider exactly once.

Run:

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceSourceRegistry -j4
```

Expected: target is absent or fails before implementation.

- [x] **Step 2: Define the provider boundary**

In `ScienceProvider.h`, define:

```cpp
struct ScienceProviderSnapshot
{
    std::uint64_t generation = 0;
    ScienceJobState state = ScienceJobState::Unavailable;
    float progress = 0.0f;
    std::string message;
    std::shared_ptr<const ScienceArtifact> artifact;
};

class IScienceProvider
{
public:
    virtual ~IScienceProvider() = default;
    virtual ScienceSourceDescriptor descriptor() const = 0;
    virtual std::uint64_t submit(const GeoTemporalQuery& query) = 0;
    virtual ScienceProviderSnapshot snapshot() const = 0;
    virtual void cancel(std::uint64_t generation) = 0;
    virtual void clear() = 0;
};
```

No provider method receives a camera, layer, scene node, or UI object.

- [x] **Step 3: Implement the owning registry**

`ScienceSourceRegistry` owns `std::unique_ptr<IScienceProvider>` keyed by descriptor id. Expose:

```cpp
bool add(std::unique_ptr<IScienceProvider> provider, std::string& error);
IScienceProvider* find(const std::string& id);
const IScienceProvider* find(const std::string& id) const;
std::vector<ScienceSourceDescriptor> listSources() const;
```

Use ordered storage or explicit sorting so catalog order is deterministic. `listSources()` calls
`descriptor()` at read time so provider health can change without starting data access.

- [x] **Step 4: Run the focused tests**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceSourceRegistry -j4
ctest --test-dir build/science_g2 --output-on-failure \
  -R 'ScienceSourceRegistry|ScienceQueryTypes'
```

Expected: all registry and type tests pass without GDAL/OSG linkage.

- [x] **Step 5: Commit**

```bash
git add science/ScienceProvider.h science/ScienceSourceRegistry.h \
  science/ScienceSourceRegistry.cpp science/ScienceSourceRegistryTest.cpp \
  science/CMakeLists.txt
git commit -m "feat(scienceearth): add provider registry"
```

### Task 2 Evidence

- RED: `osgSol_Test_ScienceSourceRegistry` failed because `ScienceProvider.h` did not exist.
- GREEN: registry and generic-type tests passed `2/2`.
- Ownership: null/empty/duplicate providers are rejected precisely, rejected ownership is released,
  and accepted providers are destroyed exactly once with the registry.
- Catalog: source descriptors are returned in stable id order and unavailable health remains
  visible.
- Isolation: the core archive contains only query-types and registry objects and still contains no
  GDAL, OSG, UI, or Agent dependency.

## Task 3: Implement the single-active-job query service

**Files:**

- Create: `science/ScienceQueryService.h`
- Create: `science/ScienceQueryService.cpp`
- Create: `science/ScienceQueryServiceTest.cpp`
- Modify: `science/CMakeLists.txt`

- [x] **Step 1: Write failing service-state tests**

Use a controlled fake provider. Cover all of these cases before implementation:

1. unknown source rejects synchronously and never dispatches;
2. unavailable source remains listed and rejects with its health message;
3. non-finite/out-of-range point, year, or span rejects before dispatch;
4. unsupported bounding box/current-view, interval, aggregation, output, variables, or
   visualization rejects with the exact unsupported capability;
5. accepted jobs receive monotonically increasing service ids;
6. replacement cancels the prior provider generation before dispatching the next;
7. `cancel(oldJobId)` cannot cancel the newer active job;
8. provider progress maps only when source id and generation match;
9. stale ready completion is ignored;
10. ready completion atomically replaces the last-good artifact;
11. fetching, failure, and cancellation retain the previous artifact;
12. `clearArtifact()` removes only the retained artifact;
13. service destruction cancels an active provider before registry destruction.

Run:

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceQueryService -j4
```

Expected: target is absent or tests fail before implementation.

- [x] **Step 2: Implement service construction and catalog**

Use an owning registry:

```cpp
explicit ScienceQueryService(std::unique_ptr<ScienceSourceRegistry> registry);
~ScienceQueryService();
std::vector<ScienceSourceDescriptor> listSources() const;
std::uint64_t submit(const GeoTemporalQuery& query);
void cancel(std::uint64_t jobId);
void clearArtifact();
ScienceJobSnapshot snapshot();
```

Reject a null registry during construction with a valid unavailable service state. Do not use a
background service thread; `snapshot()` reconciles the provider's immutable snapshot with the
active service job.

- [x] **Step 3: Implement validation and synchronous failures**

Assign a service job id, validate, and publish a `Failed` snapshot synchronously without calling
the provider when invalid. G2-1 accepts only:

- source `alphaearth-foundations` or another fake advertising the same capabilities;
- `ScienceGeometryKind::Point` with finite WGS84 coordinates;
- one explicit year inside the source range;
- exact variables `A01`, `A16`, `A09`;
- visualization `false-color-a01-a16-a09`;
- no aggregation;
- raster-layer output;
- requested span in `[2560, 81920]` meters;
- limits that do not exceed the descriptor-advertised capability.

Errors must name the rejected field, not collapse into `invalid query`.

- [x] **Step 4: Implement replacement and last-good rules**

On accepted replacement: cancel the old provider generation, retain
`lastSuccessfulArtifact`, dispatch the new query, and record the source id/provider generation.
On reconcile: ignore mismatched generations; copy progress/message for matching states; replace the
artifact only for matching `Ready` with a non-null artifact. `Failed` and `Cancelled` never clear
the retained artifact. `clearArtifact()` never silently cancels the current job.

- [x] **Step 5: Run the focused state matrix and isolation check**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceQueryService -j4
ctest --test-dir build/science_g2 --output-on-failure \
  -R 'ScienceQueryService|ScienceSourceRegistry|ScienceQueryTypes'
otool -L build/science_g2/science/libosgSolScienceCore.a 2>/dev/null || true
```

Expected: all service cases pass; core remains a static, GDAL/OSG-free target.

- [x] **Step 6: Commit**

```bash
git add science/ScienceQueryService.h science/ScienceQueryService.cpp \
  science/ScienceQueryServiceTest.cpp science/CMakeLists.txt
git commit -m "feat(scienceearth): add unified query service"
```

### Task 3 Evidence

- RED: `osgSol_Test_ScienceQueryService` failed because `ScienceQueryService.h` did not exist.
- GREEN: query service, registry, generic type, and build-contract tests passed `4/4`.
- Validation: unknown/unavailable source, geometry, coordinate, span, time, year, visualization,
  variables, aggregation, output, resolution, and limit checks complete before provider dispatch.
- State: accepted job ids increase monotonically; replacement cancels the previous provider
  generation; old job ids and stale completions cannot alter the current job.
- Artifact: fetching, failure, and cancellation retain the last successful artifact; only explicit
  clear removes it.
- Lifetime: an active provider generation is cancelled before registry/provider destruction.
- Isolation: the core archive contains only query service, query type, and registry objects and
  still has no GDAL, OSG, UI, or Agent dependency.

## Task 4: Wrap the accepted AlphaEarth runtime with an adapter

**Files:**

- Create: `science/AlphaEarthProvider.h`
- Create: `science/AlphaEarthProvider.cpp`
- Create: `science/AlphaEarthProviderTest.cpp`
- Modify: `science/SciencePreviewRuntime.h`
- Modify: `science/SciencePreviewRuntime.cpp`
- Modify: `science/SciencePreviewRegressionTest.cpp`
- Modify: `science/SciencePreviewSmokeTest.cpp`
- Modify: `science/CMakeLists.txt`
- Modify mechanically for renamed legacy types only:
  `applications/earth_explorer/EarthControlUI.h`
- Modify mechanically for renamed legacy types only:
  `applications/earth_explorer/science_ai_tools.cpp`
- Modify mechanically for renamed legacy types only:
  `applications/earth_explorer/science_preview_layer.h`
- Modify mechanically for renamed legacy types only:
  `applications/earth_explorer/science_preview_layer.cpp`

- [x] **Step 1: Add failing pure translation tests**

Test descriptor translation for id/name/version/attribution, 2017-2025, 10 m, 64 components,
`A01/A16/A09`, false-color display range, availability, and health message. Test state translation
for unavailable/idle/queued/fetching/ready/failed/cancelled. Test ready artifact translation for
dataset id, URL, version, year, footprint, source/display resolution, RGBA pointer, and exact ground
grid pointer.

These tests construct legacy snapshots in memory and perform no network access.

- [x] **Step 2: Rename only the legacy AlphaEarth-facing types**

Avoid collision with the new generic contracts by mechanically renaming:

```text
PreviewState                  -> AlphaEarthPreviewState
ScienceSourceDescriptor      -> AlphaEarthSourceDescriptor
SciencePreviewArtifact       -> AlphaEarthPreviewArtifact
SciencePreviewSnapshot       -> AlphaEarthPreviewSnapshot
```

Keep `SciencePreviewRuntime` itself and all request/worker/GDAL behavior unchanged. Update the
existing preview regression, smoke tests, and direct app consumers for the type names only. Use
`git diff --word-diff` to verify no algorithmic change entered `SciencePreviewRuntime.cpp` beyond
identifiers. The consumers remain directly wired until Tasks 5-7; this mechanical rename keeps the
intermediate EarthExplorer target buildable.

- [x] **Step 3: Implement the adapter**

`AlphaEarthProvider` owns `SciencePreviewRuntime`. Its constructor receives the compact index path.
Its `descriptor()` translates the current runtime descriptor and availability. `submit()` validates
that the already service-validated generic query contains one point/year/span and delegates to
`queryPoint()`. `snapshot()` publishes only the runtime generation matching the provider's active
generation and converts a ready artifact without copying pixel or grid buffers. `cancel(generation)`
ignores old generations; `clear()` delegates to the runtime.

Expose small pure translation helpers to the adapter test rather than adding a fake network seam
inside the verified runtime.

- [x] **Step 4: Run adapter plus protected renderer/runtime regressions**

```bash
cmake --build build/science_g2 --target \
  osgSol_Test_AlphaEarthProvider osgSol_Test_SciencePreviewRegression \
  osgSol_Test_SciencePreviewSmoke -j4
ctest --test-dir build/science_g2 --output-on-failure \
  -R 'AlphaEarthProvider|SciencePreviewRegression'
git diff --word-diff=porcelain 1183f85 -- science/SciencePreviewRuntime.cpp | \
  rg -v 'AlphaEarthPreview|SciencePreview|^[~+-]$' || true
```

Expected: translation and protected preview regressions pass; runtime diff contains no networking,
windowing, orientation, grid, or rendering change.

- [x] **Step 5: Commit**

```bash
git add science/AlphaEarthProvider.h science/AlphaEarthProvider.cpp \
  science/AlphaEarthProviderTest.cpp science/SciencePreviewRuntime.h \
  science/SciencePreviewRuntime.cpp science/SciencePreviewRegressionTest.cpp \
  science/SciencePreviewSmokeTest.cpp science/CMakeLists.txt
git commit -m "feat(scienceearth): adapt AlphaEarth to query service"
```

### Task 4 Evidence

- RED: `osgSol_Test_AlphaEarthProvider` failed because `AlphaEarthProvider.h` did not exist.
- GREEN: adapter translation and protected preview regression tests passed `2/2`; the preview
  smoke executable and complete EarthExplorer target also compiled.
- Translation: source health/capabilities, all seven runtime states, query, dataset, URL, version,
  year, footprint, resolutions, processing, and attribution are preserved.
- Payload: generic artifacts share the verified RGBA and exact ground-grid pointers; no pixel or
  grid copy/reprojection was introduced.
- Runtime audit: the word diff of `SciencePreviewRuntime.cpp` contains only the mechanical
  `AlphaEarth*` type names and line wrapping; GDAL/network/window/grid algorithms are unchanged.
- Network scope: the adapter test uses only an in-memory snapshot and a deliberately missing local
  index; it starts no data request.

## Task 5: Migrate the preview layer without changing rendering

**Files:**

- Modify: `applications/earth_explorer/science_preview_layer.h`
- Modify: `applications/earth_explorer/science_preview_layer.cpp`
- Modify: `science/SciencePreviewRegressionTest.cpp`

- [x] **Step 1: Add failing service-artifact renderer cases**

Change the regression fixture to create a generic `ScienceArtifact` with the same 2x2 RGBA and
ground grid. Add a controlled service/provider case proving:

- the layer materializes the service's `lastSuccessfulArtifact` even while a replacement is
  fetching or failed;
- a newer artifact id/generation replaces the previous node exactly once;
- `removeArtifact()` clears scene children without touching the service job;
- visibility toggling does not mutate the service or camera.

- [x] **Step 2: Replace the direct runtime dependency**

Change the layer constructor/member to `ScienceQueryService*`. Preserve
`createSciencePreviewArtifactNode()` and the complete shader/mesh implementation. Update
`syncFromRuntime()` to `syncFromService()` and read `snapshot.lastSuccessfulArtifact` independently
of job state. Use artifact generation/id for duplicate suppression.

Do not change `PREVIEW_ALTITUDE_METERS`, shader code, RGBA origin, exact grid-to-ECEF conversion,
depth mode, blend mode, culling, or yellow border.

- [x] **Step 3: Run the renderer oracle**

```bash
cmake --build build/science_g2 --target osgSol_Test_SciencePreviewRegression -j4
ctest --test-dir build/science_g2 --output-on-failure \
  -R '^osgSol_Test_SciencePreviewRegression$'
git diff 1183f85 -- applications/earth_explorer/science_preview_layer.cpp
```

Expected: regression passes and the visual implementation differs only at input type/state sync.

- [x] **Step 4: Commit**

```bash
git add applications/earth_explorer/science_preview_layer.h \
  applications/earth_explorer/science_preview_layer.cpp \
  science/SciencePreviewRegressionTest.cpp
git commit -m "refactor(scienceearth): render query artifacts"
```

### Task 5 Evidence

- The renderer RED failed on the intended missing `ScienceArtifact` overload and
  `ScienceQueryService*` constructor.
- `osgSol_Test_SciencePreviewRegression` passed after migration. It now proves last-good retention
  during replacement fetch/failure, later replacement, visibility purity, and explicit removal.
- Shader strings, exact ECEF ground grid, 650 m preview altitude, blend/depth/cull behavior, and
  yellow coverage border were preserved.
- Task 5 was committed together with Tasks 6-7 as `3fa9f60` because changing the layer constructor
  alone would intentionally leave `earth_main.cpp` uncompilable. The combined commit is the first
  buildable consumer boundary.

## Task 6: Migrate app construction and build the visible source catalog

**Files:**

- Modify: `applications/earth_explorer/earth_main.cpp`
- Modify: `applications/earth_explorer/EarthControlUI.h`
- Modify: `applications/earth_explorer/CMakeLists.txt`
- Create: `tests/science_query_consumer_contract_tests.py`
- Modify: `tests/CMakeLists.txt`

- [x] **Step 1: Write failing source-consumer contract tests**

The Python source contract must assert:

- `EarthControlUI.h`, `science_preview_layer.*`, and `science_ai_tools.*` contain no
  `SciencePreviewRuntime` reference after migration;
- `earth_main.cpp` constructs one `AlphaEarthProvider`, one registry, and one
  `ScienceQueryService`;
- UI stores `ScienceQueryService*` and does not call a camera setter in the ScienceEarth block;
- the catalog contains health, provider version, temporal range, native resolution, variables,
  visualization, channel map, attribution, and unavailable/degraded reason;
- load uses a point derived from `computeViewPointLatLonHeight()` and a span derived from eye
  height, without moving the camera.

Run:

```bash
python3 -m unittest tests.science_query_consumer_contract_tests -v
```

Expected: fails while consumers still reference the runtime.

- [x] **Step 2: Construct the registry/service once in `earth_main.cpp`**

Create the index path exactly as today, then:

```cpp
auto registry = std::make_unique<earthscience::ScienceSourceRegistry>();
std::string providerError;
registry->add(std::make_unique<earthscience::AlphaEarthProvider>(scienceIndexPath),
              providerError);
auto scienceService = std::make_unique<earthscience::ScienceQueryService>(
    std::move(registry));
```

Pass the service to `SciencePreviewLayer`, UI, and Agent registration. Provider registration
failure must leave a visible unavailable catalog state rather than silently removing the panel.

- [x] **Step 3: Replace the panel's direct runtime logic**

Store `_scienceService`. Read the selected descriptor from `listSources()` and the job from
`snapshot()`. Build one bounded AlphaEarth `GeoTemporalQuery` helper from the viewed target, eye
height span, selected year, `A01/A16/A09`, and false-color visualization.

Render the catalog before controls:

- source name, health, and health message;
- category/provider version;
- `2017-2025`, 10 m, 64 components;
- false-color meaning and channel mapping;
- attribution.

Keep the existing normal slider behavior and year-change reload. During replacement fetching or
failure, show both the current status and the retained artifact details. Remove performs, in order:

```cpp
scienceService->cancel(snapshot.jobId);
scienceService->clearArtifact();
scienceLayer->removeArtifact();
scienceLayer->setVisible(false);
```

Quit remains the existing viewer quit request; do not alter its control path.

- [x] **Step 4: Run contract, service, renderer, and app compile gates**

```bash
python3 -m unittest tests.science_query_consumer_contract_tests -v
cmake --build build/science_g2 --target \
  osgSol_Test_ScienceQueryService osgSol_Test_SciencePreviewRegression \
  osgVerse_EarthExplorer -j4
ctest --test-dir build/science_g2 --output-on-failure \
  -R 'ScienceQueryConsumer|ScienceQueryService|SciencePreviewRegression'
```

Expected: contracts and focused regressions pass; EarthExplorer links the preview/core targets.

- [x] **Step 5: Commit**

```bash
git add applications/earth_explorer/earth_main.cpp \
  applications/earth_explorer/EarthControlUI.h \
  applications/earth_explorer/CMakeLists.txt \
  tests/science_query_consumer_contract_tests.py tests/CMakeLists.txt
git commit -m "feat(scienceearth): expose unified source catalog"
```

### Task 6 Evidence

- The consumer contract first failed in three intended places: UI/Agent runtime references,
  direct runtime construction in `earth_main.cpp`, and the missing generic catalog fields.
- All five Python consumer contracts now pass.
- `earth_main.cpp` owns one registry, one `AlphaEarthProvider`, and one `ScienceQueryService`, then
  injects the service into renderer, UI, and Agent tools.
- UI exposes health/reason, category/provider version, time range, resolution/components,
  visualization channel meaning and attribution. Current failure/progress is displayed separately
  from the retained last-successful artifact.
- `osgVerse_EarthExplorer` compiled and linked successfully; Quit and all non-science control paths
  were not modified.

## Task 7: Migrate the four Agent tools and preserve camera authority

**Files:**

- Modify: `applications/earth_explorer/science_ai_tools.h`
- Modify: `applications/earth_explorer/science_ai_tools.cpp`
- Create: `applications/earth_explorer/science_ai_tools_test.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`
- Modify: `tests/science_query_consumer_contract_tests.py`

- [x] **Step 1: Write failing tool schema/result tests**

Use a fake service/provider and a manipulator with a stable matrix. Verify:

- tool names remain exactly `search_science_sources`, `start_science_research`,
  `get_research_job`, and `show_science_artifact`;
- search returns every catalog descriptor including health and attribution;
- start accepts optional `source_id` and `visualization_id`, with AlphaEarth defaults;
- omitted coordinates resolve to viewed ground target;
- explicit coordinates do not initiate navigation;
- generic job JSON includes job/source/state/progress/message;
- artifact JSON includes dataset, year, footprint, source/display resolution, visualization,
  processing/source reference, and attribution;
- show toggles only layer visibility and reports `camera_changed=false`;
- the camera matrix is byte-stable across every tool execution.

- [x] **Step 2: Change registration to `ScienceQueryService*`**

Build query JSON through the same bounded query helper semantics used by UI. `get_research_job`
must reject a non-current job id. `show_science_artifact` may show the retained last-good artifact
even when the replacement job is failed/cancelled, but it must report both the artifact generation
and current job state honestly.

Do not add a fly-to or camera argument to these tools. Research/navigation composition remains an
Agent-level sequence using existing separate tools.

- [x] **Step 3: Run focused tool and consumer contracts**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceAiTools -j4
ctest --test-dir build/science_g2 --output-on-failure \
  -R 'ScienceAiTools|ScienceQueryConsumer|ScienceQueryService'
python3 -m unittest tests.science_query_consumer_contract_tests -v
```

Expected: schemas/results pass and the camera matrix remains unchanged.

- [x] **Step 4: Commit**

```bash
git add applications/earth_explorer/science_ai_tools.h \
  applications/earth_explorer/science_ai_tools.cpp \
  applications/earth_explorer/science_ai_tools_test.cpp \
  applications/earth_explorer/CMakeLists.txt \
  tests/science_query_consumer_contract_tests.py
git commit -m "feat(scienceearth): route Agent research through service"
```

### Task 7 Evidence

- The Agent source contract first failed because registration still named
  `SciencePreviewRuntime`; it passed after the service migration.
- `osgSol_Test_ScienceAiTools` uses an in-memory provider and verifies the exact four stable tool
  names, catalog health/attribution, optional source/visualization selection, viewed-target
  defaults, explicit coordinates, generic artifact provenance, retained artifact after a failed
  replacement, and layer-only show behavior.
- The test compares the complete 4x4 camera matrix byte-for-byte after every tool execution.
- Focused CTest gate passed `5/5`: query service, AlphaEarth provider, preview regression, consumer
  contract, and Agent tools.
- Tasks 5-7 share buildable commit `3fa9f6083bf216bdcad1ee3174bc38391e98e7f0`.

## Task 8: Run full automated regressions and local-index smoke

**Files:**

- Modify only if a real defect is exposed: files from Tasks 1-7 and their focused tests
- Append evidence: this plan under `Task 8 Evidence`

- [x] **Step 1: Verify the clean v5 prefix and source contract**

Run the existing committed v5 verification command recorded by
`packaging/science_deps/build_science_deps.sh --help`/script interface, targeting only:

```text
build/science-deps-g2-v5/prefix
```

Expected: archive checksums, embed capability, static prefix, patch/pin, and manifest pass. Any v6
marker or v6 prefix use is a hard stop.

- [x] **Step 2: Run the complete science-enabled offline suite**

```bash
cmake --build build/science_g2 -j4
ctest --test-dir build/science_g2 -L offline --output-on-failure
```

Expected: zero failures. Do not weaken or exclude a failing test; fix the scoped defect and rerun
its focused test before rerunning the suite.

- [x] **Step 3: Reconfigure and run the complete science-off suite**

```bash
cmake -S . -B build/science_g2_off \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVERSE_BUILD_EXAMPLES=ON \
  -DOSGSOL_BUILD_SCIENCE=OFF
cmake --build build/science_g2_off -j4
ctest --test-dir build/science_g2_off -L offline --output-on-failure
```

Expected: the established 20/20 science-off baseline passes, generated target list is empty, and
the Earth app has no science symbols/targets.

- [x] **Step 4: Run the local-index AlphaEarth smoke for two years**

Use the production packaged index and existing smoke executable. Query one verified point for 2018
and 2025 without changing the URL/provider architecture. Record dataset ids, elapsed time, output
size, and exit status. Require both ready artifacts to be 256x256 and dataset ids to differ.

Network access here is only the existing normal AlphaEarth COG data path. Do not start local proxy,
port-forwarding, interception, fault injection, or v6 security experiments.

- [x] **Step 5: Run static scope checks**

```bash
rg -n 'SciencePreviewRuntime' applications/earth_explorer
rg -n 'setByEye|setByMatrix|setCenter|setDistance|fly' \
  applications/earth_explorer/science_ai_tools.cpp \
  applications/earth_explorer/science_preview_layer.cpp
git diff --check 1183f85..HEAD
git status --short
```

Expected: no direct runtime consumer, no science camera authority, no whitespace errors, and only
the planned tracked changes.

- [x] **Step 6: Record Task 8 evidence and commit**

Append exact build directories, test counts, smoke dataset ids/timings, and any known warnings under
`Task 8 Evidence` below.

```bash
git add docs/superpowers/plans/2026-07-15-scienceearth-g2-query-service-plan.md
git commit -m "test(scienceearth): verify G2 query service"
```

### Task 8 Evidence

Verified on 2026-07-15 from committed G2 consumer state `5db1117`:

- v5-only dependency verification used
  `SCIENCE_DEPS_ROOT=build/science-deps-g2-v5 packaging/science_deps/build_science_deps.sh --verify`.
  The GDAL 3.13.1, PROJ 9.8.1, and zstd 1.5.7 archive checksums, independent GDAL embed
  capability, private static prefix, and manifest all passed. No v6 prefix or marker was used.
- Science-enabled Release build `build/science_g2` completed and the complete offline suite passed
  **33/33** in 46.08 seconds real time. The four existing local network simulations passed; no
  proxy, port-forward, interception, or external fault-injection service was started.
- Fresh Science-OFF Release build `build/science_g2_off` configured and built EarthExplorer from
  zero, then passed **20/20** offline tests in 24.42 seconds. Its generated
  `OSGSOL_BUILD_TARGETS_VALUE` is empty, target-help exposes no Science runtime/test target, and
  the EarthExplorer executable contains no `ScienceQuery`, `AlphaEarth`, or `SciencePreview`
  symbol.
- The production local index `build/science-index-full/alphaearth.sqlite` has SHA-256
  `15875963d1bf4dd3f35a1f6c3ec6329378fef0fab6549fac677552f1d348f736`, identical to the
  accepted Desktop v0.3.0 bundle's packaged index. At NVIDIA headquarters vicinity
  `(37.3700, -121.9600)`, the normal GDAL COG path returned:
  - 2018: dataset `7851`, `256x256`, exit 0, 3.57 seconds real;
  - 2025: dataset `9790`, `256x256`, exit 0, 3.23 seconds real.
  The differing dataset ids prove year selection reached distinct indexed source records.
- Static scope scans found no `SciencePreviewRuntime` consumer in `applications/earth_explorer`,
  no camera setter/fly authority in the science Agent or preview layer, and no whitespace error in
  the G2 diff. Build output contains only the established macOS OpenGL deprecation and duplicate
  library-link warnings; there was no compile or test failure.

## Task 9: Build, audit, and install one untagged manual candidate

**Files:**

- Modify: `packaging/package_macos.sh`
- Modify: `tests/package_macos_tests.sh`
- Append evidence: this plan under `Task 9 Evidence`

- [ ] **Step 1: Make the package contract fail against the current generic bundle script**

Extend `tests/package_macos_tests.sh` to require the fixed product contract:

```text
bundle path source = a staging path, never the current Desktop app
CFBundleName = osgSol Earth
CFBundleDisplayName = osgSol Earth
CFBundleIdentifier = com.anloren.osgsol.earth
CFBundleExecutable = osgSol_Earth
CFBundleShortVersionString = explicit candidate version
ScienceEarthBuildChannel = manual-test
ScienceEarthSourceCommit = exact clean HEAD
```

It must also prove the script rejects an unset/nonexistent install tree, refuses `EARTH_AI_KEY`,
does not delete the existing Desktop app before staging passes, and produces no timestamped or
version-suffixed Desktop app. Run the test and observe failure because the current generic script
still writes `EarthExplorer.app`, `com.osgverse.earthexplorer`, and `1.0.0`.

- [ ] **Step 2: Make packaging parameterized and staging-first**

Update `package_macos.sh` to accept explicit environment inputs for SDK, output staging path,
candidate version, build channel, source commit, and product executable name. Defaults may support
developer packaging, but the formal G2 command supplies every product field. Package and verify the
staging bundle; the script itself must not overwrite the fixed Desktop app.

- [ ] **Step 3: Build/install from the clean committed G2 head**

Require a clean working tree. Configure/install Release into a fresh G2 SDK using the verified v5
prefix. Unset `EARTH_AI_KEY`. Record the exact source commit in the app metadata. Do not use a v6
build directory or private prefix.

- [ ] **Step 4: Package to staging, not directly over the Desktop baseline**

Package from the fresh SDK. Preserve bundle id `com.anloren.osgsol.earth`, formal app name
`osgSol Earth`, version `0.3.0` until user acceptance authorizes a new release version, channel
`manual-test`, and the exact G2 source commit. Do not create a version-suffixed Desktop sibling.

Remove Finder/resource-fork extended attributes from the complete staging bundle before signing:

```bash
xattr -cr '/path/to/osgSol Earth.app.staging'
codesign --force --deep --sign - '/path/to/osgSol Earth.app.staging'
codesign --verify --deep --strict '/path/to/osgSol Earth.app.staging'
```

- [ ] **Step 5: Run package and dependency audits**

Require:

- strict/deep signature pass;
- no `imgui.ini`, local log, API key, worktree path, private prefix path, unresolved `@rpath`, or v6
  runtime marker;
- all science and renderer libraries present;
- offscreen log contains 1920x1080 context and capture-saved markers;
- capture exists, is non-empty, and contains no OpenGL/shader error;
- Quit path remains available;
- direct Homebrew Python dependency is reported as a known clean-machine distribution debt, not
  silently declared solved unless this task actually removes it.

- [ ] **Step 6: Atomically update the fixed Desktop app**

Only after staging passes, replace `/Users/USER/Desktop/osgSol Earth.app` in place while keeping
a rollback copy outside the final Desktop name. Run the Desktop-path offscreen smoke, then remove
any smoke-created mutable files/xattrs, re-sign in final location, and require:

```bash
codesign --verify --deep --strict '/Users/USER/Desktop/osgSol Earth.app'
test -z "$(find '/Users/USER/Desktop/osgSol Earth.app' -name imgui.ini -print -quit)"
```

Do not run the finalized signed bundle again after the last strict verification; manual launch may
create mutable UI state and must be followed by cleanup/re-sign before a release decision.

- [ ] **Step 7: Rerun the package contract**

```bash
bash tests/package_macos_tests.sh
```

Expected: staging/product identity, credential rejection, offscreen smoke, mutable-file exclusion,
dependency closure, and strict signature checks all pass.

- [ ] **Step 8: Record Task 9 evidence and commit**

Append commit, package fingerprint, bundle metadata, signature/dependency/offscreen results,
rollback location, and fixed Desktop path under `Task 9 Evidence`.

```bash
git add packaging/package_macos.sh tests/package_macos_tests.sh \
  docs/superpowers/plans/2026-07-15-scienceearth-g2-query-service-plan.md
git commit -m "build(scienceearth): prepare G2 manual candidate"
```

### Task 9 Evidence

Pending implementation.

## Task 10: Human verification and release-decision handoff

**Files:**

- Append evidence: this plan under `Task 10 Evidence`
- Modify later only after explicit user acceptance: release notes/version metadata/tag commands

- [ ] **Step 1: Give the user the fixed app and exact test matrix**

Ask the user to verify in `/Users/USER/Desktop/osgSol Earth.app`:

1. catalog appears before loading and shows one AlphaEarth source with health and complete meaning;
2. current-view load appears at the real viewed location and scale;
3. year change reloads and changes dataset id;
4. loading a replacement keeps the old result visible;
5. controlled failure/cancel keeps the old result visible and reports the new job honestly;
6. hide/show/remove work, and remove makes every science artifact disappear;
7. panel wheel scrolls down and back up;
8. low-altitude Fuji/NVIDIA/known point placement is stable under middle-button tilt;
9. Agent discovers `alphaearth-foundations`, starts a job, reports evidence, and shows it without
   moving the camera;
10. photo uses exactly the visible view and never reuses an earlier image;
11. Hong Kong 3D Tiles and terrain behavior remain unchanged from the accepted baseline;
12. Quit closes the application normally.

- [ ] **Step 2: Record acceptance or defect evidence**

For a defect, record exact location/year/height/action, current job state, visible old/new artifact,
and whether camera or another layer changed. Fix only the scoped defect, rerun Tasks 8-9, and issue
the same fixed Desktop app path.

- [ ] **Step 3: Stop at the release-decision boundary**

Manual acceptance permits a separate explicit version/tag/sync decision. It does not automatically
authorize moving tags, merging, or publishing. Until the user explicitly requests release:

```text
G2_1_MANUAL_ACCEPTANCE=PENDING
RELEASE_TAG=NOT_CREATED
REMOTE_SYNC=NOT_PERFORMED
```

### Task 10 Evidence

Pending user verification.

## Plan Self-Review

- [x] **Spec coverage:** Design sections 2 and 4-9 map to Tasks 1-10 or an explicit exclusion in
      Global Constraints.
- [x] **Placeholder scan:** The scan found no unresolved implementation placeholder. The three
      `Pending implementation/user verification` evidence values are intentional live state fields.
- [x] **Type consistency:** Generic `Science*` names are separated from the mechanically renamed
      `AlphaEarth*` legacy types, and Tasks 5-7 move every UI/Agent/renderer consumer to the generic
      service.
- [x] **Boundary consistency:** No task adds a new provider, camera authority, release tag, v6 code,
      or clean-machine distribution claim.
