# ScienceEarth G2-2 Sentinel-2 Vertical Slice Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development
> (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded, scientifically honest Sentinel-2 L2A true-color provider to the existing
ScienceEarth query, UI, Agent, evidence, and preview paths and deliver it in the same Desktop app.

**Architecture:** Keep deterministic STAC request/parsing separate from the asynchronous provider
runtime. Discover one scene through Earth Search, read only its `visual` COG through GDAL
`/vsicurl/`, and emit the existing raster artifact. Generalize only the AlphaEarth-specific seams
in `ScienceQueryService`, the panel, and Agent tools.

**Tech Stack:** C++17, picojson, GDAL 3.13.1 GTiff/VRT/MEM and `/vsicurl/`, PROJ 9.8.1, ImGui,
OpenSceneGraph, CMake/CTest, macOS arm64 packaging.

## Global Constraints

- Baseline is `v0.4.0` / `ScienceEarth-v0.4.0` at `76fb4e0`.
- Preserve every protected AlphaEarth, camera, photo, Hong Kong, satellite, reset, and exit behavior.
- Never start or foreground the Desktop app or a GUI/crash-producing test from Codex.
- No local server, port, credential, security gate, full COG download, or new background prefetch.
- Remote access is limited to the two hosts and budgets frozen in the design.
- Keep one active science job and last-good retention.
- Update only `/Users/USER/Desktop/osgSol Earth.app`, transactionally, after package gates.
- Do not tag, push, merge, or publish before later manual acceptance and explicit user request.

---

## File Structure

- `science/Sentinel2Stac.h/.cpp`: pure URL construction, JSON parsing, allowlist, and selection.
- `science/Sentinel2Runtime.h/.cpp`: one worker, STAC fetch, `/vsicurl/` read, cancellation, artifact.
- `science/Sentinel2Provider.h/.cpp`: descriptor and `IScienceProvider` adapter.
- `science/Sentinel2StacTest.cpp`: fixed official-shaped JSON and rejection fixtures.
- `science/Sentinel2RuntimeTest.cpp`: local GDAL fixture and fake STAC transport.
- `science/Sentinel2ProviderTest.cpp`: descriptor, dispatch, progress, cancel, and artifact translation.
- `science/ScienceQueryTypes.*`: generic scene filters and evidence fields.
- `science/ScienceQueryService.*`: descriptor-driven raster/time validation and estimation.
- `applications/earth_explorer/science_query_builder.h`: source-aware query builders.
- `applications/earth_explorer/science_earth_panel.*`: source-specific, progressively disclosed UI.
- `applications/earth_explorer/science_ai_tools.*`: interval/cloud arguments and cited output.
- `applications/earth_explorer/earth_main.cpp`: provider registration and generic science layer copy.
- `docs/scienceearth/sentinel2-verification.md`: frozen real-data, regression, package, and manual record.

---

### Task 1: Freeze generic scene-query and evidence contracts

**Files:**
- Modify: `science/ScienceQueryTypes.h`
- Modify: `science/ScienceQueryTypes.cpp`
- Modify: `science/ScienceQueryTypesTest.cpp`

**Interfaces:**
- Produces: `ScienceSceneFilters`, `ScienceEvidenceField`, and byte accounting used by all later tasks.

- [x] **Step 1: Write failing contract tests**

Add assertions that defaults are `maximumCloudCoverPercent=100.0`, `maximumScenes=10`; copied
artifacts keep evidence fields; and `estimatedArtifactBytes()` counts every evidence-field string.

- [x] **Step 2: Run the focused executable and observe RED**

Run:

```bash
cmake --build build/science_64d_final_verify --target osgSol_Test_ScienceQueryTypes -j6
```

Expected: compile failure because the two structs and fields do not exist.

- [x] **Step 3: Add the minimal types**

Add:

```cpp
struct ScienceSceneFilters
{
    double maximumCloudCoverPercent = 100.0;
    std::uint32_t maximumScenes = 10;
};

struct ScienceEvidenceField
{
    std::string id;
    std::string displayName;
    std::string value;
    std::string unit;
};
```

Add `ScienceSceneFilters sceneFilters` to `GeoTemporalQuery` and
`std::vector<ScienceEvidenceField> fields` to `ScienceSourceReference`. Count their strings in
`estimatedArtifactBytes()`.

- [x] **Step 4: Build and run GREEN**

Run the target and then:

```bash
build/science_64d_final_verify/science/osgSol_Test_ScienceQueryTypes
```

Expected: exit 0 with no GUI or crash report.

- [x] **Step 5: Commit**

```bash
git add science/ScienceQueryTypes.h science/ScienceQueryTypes.cpp \
  science/ScienceQueryTypesTest.cpp
git commit -m "feat(scienceearth): define scene evidence contracts"
```

### Task 2: Implement deterministic STAC request and selection

**Files:**
- Create: `science/Sentinel2Stac.h`
- Create: `science/Sentinel2Stac.cpp`
- Create: `science/Sentinel2StacTest.cpp`
- Modify: `science/CMakeLists.txt`

**Interfaces:**
- Consumes: WGS84 bounds, interval, and `ScienceSceneFilters`.
- Produces: `buildSentinel2SearchUrl(...)`, `parseSentinel2Items(...)`, and
  `selectSentinel2Item(...)` returning `Sentinel2Scene`.

- [x] **Step 1: Write fixed official-shaped fixtures**

Cover two valid scenes, cloud/date/id tie-breaking, no matching cloud threshold, malformed JSON,
oversize body, invalid bbox/time/cloud, missing visual asset, HTTP asset, wrong host/path/suffix,
wrong media type, and more than ten retained items.

- [x] **Step 2: Register and build RED**

Add `osgSol_Test_Sentinel2Stac` labeled `offline;scienceearth`; build it and expect missing symbols.

- [x] **Step 3: Implement the pure seam**

Define:

```cpp
struct Sentinel2Scene
{
    std::string itemId, acquisitionTime, visualUrl, mediaType;
    ScienceWgs84Bounds bounds;
    double cloudCoverPercent = 0.0;
    double resolutionMeters = 10.0;
};

bool parseSentinel2Items(const std::string& json,
                         std::vector<Sentinel2Scene>& scenes,
                         std::string& error);
bool selectSentinel2Item(const std::vector<Sentinel2Scene>& scenes,
                         double maximumCloudCoverPercent,
                         Sentinel2Scene& selected, std::string& error);
```

Use picojson and strict allowlists. Percent-encode every query value; never concatenate unchecked
input into a URL.

- [x] **Step 4: Run GREEN and mutation cases**

Run the executable directly. Expected: all valid and rejection cases exit 0.

- [x] **Step 5: Commit**

```bash
git add science/Sentinel2Stac.h science/Sentinel2Stac.cpp \
  science/Sentinel2StacTest.cpp science/CMakeLists.txt
git commit -m "feat(scienceearth): parse Sentinel-2 STAC scenes"
```

### Task 3: Generalize service validation without weakening AlphaEarth

**Files:**
- Modify: `science/ScienceProvider.h`
- Modify: `science/AlphaEarthProvider.h`
- Modify: `science/AlphaEarthProvider.cpp`
- Modify: `science/ScienceQueryService.cpp`
- Modify: `science/ScienceQueryServiceTest.cpp`
- Modify: `science/AlphaEarthProviderTest.cpp`

**Interfaces:**
- Produces: `IScienceProvider::validateQuery(const GeoTemporalQuery&, std::string&) const`.

- [x] **Step 1: Write multi-provider RED cases**

Use a controlled Sentinel descriptor supporting point, interval, and raster output. Require an
interval raster query to dispatch, malformed/reversed intervals and cloud values outside 0-100 to
fail before dispatch, and every existing AlphaEarth rejection to remain unchanged.

- [x] **Step 2: Build RED**

Build `osgSol_Test_ScienceQueryService` and `osgSol_Test_AlphaEarthProvider`; expect the interval
query to be rejected by the hard-coded explicit-year path.

- [x] **Step 3: Implement descriptor-driven validation**

Validate geometry, source time capability, visualization variables, no raster aggregation, no
raster analysis, finite span, interval ordering, and filters generically. Call provider validation
last. Keep AlphaEarth's exact preview/64D signatures in `AlphaEarthProvider::validateQuery()`.

- [x] **Step 4: Make raster cost source-aware**

Estimate raster source cells times visualization channel count, output 256 by 256 RGBA, and retain
the learned throughput key by source/visualization/time mode.

- [x] **Step 5: Run GREEN and commit**

```bash
git add science/ScienceProvider.h science/AlphaEarthProvider.h \
  science/AlphaEarthProvider.cpp science/ScienceQueryService.cpp \
  science/ScienceQueryServiceTest.cpp science/AlphaEarthProviderTest.cpp
git commit -m "refactor(scienceearth): support provider time contracts"
```

### Task 4: Read one Sentinel-2 visual COG into a bounded artifact

**Files:**
- Create: `science/Sentinel2Runtime.h`
- Create: `science/Sentinel2Runtime.cpp`
- Create: `science/Sentinel2RuntimeTest.cpp`
- Modify: `science/CMakeLists.txt`

**Interfaces:**
- Consumes: exact `GeoTemporalQuery` and a fetched STAC body.
- Produces: `Sentinel2Runtime::submit/snapshot/cancel/clear` with `ScienceProviderSnapshot`.

- [x] **Step 1: Write a local three-band GeoTIFF fixture test**

Create a small projected UInt8 three-band GTiff in a temporary directory with known RGB corners.
Inject a fake STAC fetch returning its scene metadata and a test-only local asset resolver. Require
correct 256 by 256 RGBA, orientation, 33 by 33 ground grid, bounds, resolution, evidence, and no
full-file network path.

- [x] **Step 2: Add cancellation, stale, and failure tests**

Block the injected fetch/read seams, cancel the active generation, submit a newer generation, and
prove no old artifact can become Ready. Cover timeout, no scene, malformed COG, wrong bands, and
GDAL error without throwing across the worker.

- [x] **Step 3: Build RED**

Build `osgSol_Test_Sentinel2Runtime`; expect missing runtime symbols.

- [x] **Step 4: Implement the worker and real transports**

Fetch STAC with GDAL CPL HTTP under the 2 MiB/8 s/15 s limits. Open the selected production asset
only as `/vsicurl/<https-url>`, register only GTiff/VRT/MEM, disable directory reads, allow `.tif`,
and use 8 s/25 s HTTP limits. Use `GDALRasterIOExtraArg` cancellation progress. Never retry with a
plain URL or `/vsicurl_streaming/`.

- [x] **Step 5: Emit honest evidence**

Populate item id, acquisition time, cloud cover, collection, STAC endpoint, COG URL, requested and
actual coverage, natural-color display processing, source/display resolution, attribution, and a
warning that scene cloud cover is scene-wide and not a per-pixel cloud mask.

- [x] **Step 6: Run GREEN and commit**

```bash
git add science/Sentinel2Runtime.h science/Sentinel2Runtime.cpp \
  science/Sentinel2RuntimeTest.cpp science/CMakeLists.txt
git commit -m "feat(scienceearth): read bounded Sentinel-2 scenes"
```

### Task 5: Register the Sentinel-2 provider

**Files:**
- Create: `science/Sentinel2Provider.h`
- Create: `science/Sentinel2Provider.cpp`
- Create: `science/Sentinel2ProviderTest.cpp`
- Modify: `science/CMakeLists.txt`
- Modify: `applications/earth_explorer/earth_main.cpp`

**Interfaces:**
- Produces: `Sentinel2Provider` with id `sentinel-2-l2a`.

- [x] **Step 1: Write descriptor and adapter RED cases**

Require natural-color visualization, 10 m resolution, interval/raster capabilities, no 64D
capabilities, exact query validation, generation translation, cancel, clear, and clean worker join.

- [x] **Step 2: Implement the provider**

Describe `visual` as an RGB display product, mark the source experimental, and explain that raw
surface reflectance and per-pixel cloud masking are not part of this slice.

- [x] **Step 3: Register after AlphaEarth**

Construct `Sentinel2Provider` without probing the network at startup. Registration failure disables
only Sentinel-2 and writes one warning.

- [x] **Step 4: Run provider, registry, service, and science-off GREEN**

Run focused executables and the existing science-off target selection check.

- [x] **Step 5: Commit**

```bash
git add science/Sentinel2Provider.h science/Sentinel2Provider.cpp \
  science/Sentinel2ProviderTest.cpp science/CMakeLists.txt \
  applications/earth_explorer/earth_main.cpp
git commit -m "feat(scienceearth): register Sentinel-2 provider"
```

### Task 6: Build source-aware queries and progressive UI

**Files:**
- Modify: `applications/earth_explorer/science_query_builder.h`
- Modify: `applications/earth_explorer/science_earth_panel.h`
- Modify: `applications/earth_explorer/science_earth_panel.cpp`
- Modify: `applications/earth_explorer/science_earth_panel_test.cpp`
- Modify: `applications/earth_explorer/earth_main.cpp`
- Modify: `tests/earth_control_layout_tests.cpp`
- Modify: `tests/science_query_consumer_contract_tests.py`

**Interfaces:**
- Produces: deterministic `makeSentinel2PreviewQuery(...)` and selected source state.

- [x] **Step 1: Write RED source-selection and copy tests**

Require the default source to remain AlphaEarth; selecting Sentinel exposes only preview; its
primary action reads `加载 Sentinel-2 真彩场景`; 7/30/90 day and 10/20/40/100 cloud choices produce
exact intervals/filters; switching back restores AlphaEarth modes without stale Sentinel settings.

- [x] **Step 2: Implement a full-width source selector**

Persist source id, not vector index. Resolve it against the current catalog each frame. Do not show
AlphaEarth PCA/year controls for Sentinel. Put stable definitions behind source-specific help.

- [x] **Step 3: Generalize the overlay copy**

Rename the visible layer label to `ScienceEarth 科学影像` while retaining the existing internal
layer id and render path. The result border and evidence identify the actual source.

- [x] **Step 4: Run panel/layout/preview GREEN and commit**

```bash
git add applications/earth_explorer/science_query_builder.h \
  applications/earth_explorer/science_earth_panel.h \
  applications/earth_explorer/science_earth_panel.cpp \
  applications/earth_explorer/science_earth_panel_test.cpp \
  applications/earth_explorer/earth_main.cpp \
  tests/earth_control_layout_tests.cpp \
  tests/science_query_consumer_contract_tests.py \
  docs/superpowers/plans/2026-07-18-scienceearth-g2-2-sentinel2-plan.md
git commit -m "feat(scienceearth): add Sentinel-2 source workflow"
```

### Task 7: Extend Agent research and evidence

**Files:**
- Modify: `applications/earth_explorer/science_query_builder.h`
- Modify: `applications/earth_explorer/science_ai_tools.cpp`
- Modify: `applications/earth_explorer/science_ai_tools_test.cpp`
- Modify: `tests/science_query_consumer_contract_tests.py`

**Interfaces:**
- Produces: backward-compatible `start_science_research` source-aware time/cloud arguments.

- [x] **Step 1: Write RED schema/result cases**

Require AlphaEarth year calls to remain valid; Sentinel requires interval start/end, accepts a
0-100 cloud threshold, rejects year-only ambiguity, returns actual acquisition/cloud/scene/COG
evidence, and never calls a camera or layer-enable tool implicitly.

- [x] **Step 2: Implement source-aware argument routing**

Build queries from the selected descriptor instead of assuming the first source. Return compact
structured evidence and warnings; keep large pixels out of model results.

- [x] **Step 3: Run AI/consumer/camera GREEN and commit**

```bash
git add applications/earth_explorer/science_query_builder.h \
  applications/earth_explorer/science_ai_tools.cpp \
  applications/earth_explorer/science_ai_tools_test.cpp \
  tests/science_query_consumer_contract_tests.py \
  docs/superpowers/plans/2026-07-18-scienceearth-g2-2-sentinel2-plan.md
git commit -m "feat(scienceearth): expose Sentinel-2 to Agent research"
```

### Task 8: Verify one bounded production request

**Files:**
- Create: `science/Sentinel2ProductionSmoke.cpp`
- Modify: `science/CMakeLists.txt`
- Create: `docs/scienceearth/sentinel2-verification.md`

**Interfaces:**
- Produces: a JSONL evidence record; no map/UI side effect.

- [x] **Step 1: Add a manually invoked smoke executable**

Accept explicit latitude, longitude, interval, cloud threshold, and span. Print state transitions,
selected scene metadata, byte/memory bounds, artifact dimensions, and evidence. Never create a
listener or local server.

- [x] **Step 2: Run one bounded real query**

Use a 2.56 km Tokyo-area request and at most a 30-day interval. Require Ready, 256 by 256 RGBA,
valid ground grid, one STAC request, one COG asset, and no full-object fallback marker.

- [x] **Step 3: Record exact facts and commit**

Record timestamp, request, endpoint, scene, acquisition, cloud, source/display resolution, elapsed
time, warnings, and limitations. Do not claim a universal latency threshold from one request.

### Task 9: Run safe regressions and build the manual candidate

**Files:**
- Modify: `tests/package_macos_tests.sh` only if the new runtime files reveal a real package gap.
- Modify: `docs/scienceearth/sentinel2-verification.md`

**Interfaces:**
- Produces: verified staging and one same-name Desktop manual candidate.

- [x] **Step 1: Inspect every selected test failure path**

Exclude GUI launch, intentional signal, `abort`, crash-report, and foreground tests. Convert any
new Sentinel test failure to an ordinary nonzero exit before running it.

- [x] **Step 2: Run focused and complete safe suites**

Run all Sentinel tests, query/service/registry/AlphaEarth/UI/Agent/preview tests, the safe
science-enabled offline set, and a fresh science-off build. Record exact counts.

- [x] **Step 3: Build and audit staging**

Build Release from a clean committed source, package version `0.5.0` as an untagged `manual-test`
candidate, verify exact source metadata, dependencies, UUIDs, credentials, mutable files, and
codesign without launching it.

- [x] **Step 4: Atomically update the fixed app**

Back up the previous app outside Desktop, then replace only
`/Users/USER/Desktop/osgSol Earth.app`. Do not run, open, focus, or foreground it.

- [ ] **Step 5: Give the user the manual matrix**

Ask the user to verify source switching, time/cloud choices, Tokyo/NVIDIA/Hong Kong scene loading,
actual scene evidence, failure retention, cancel, hide/show/remove, camera invariance, AlphaEarth
64D workflows, protected terrain/3D Tiles/photo/satellite/reset behavior, and all normal Quit paths.

- [ ] **Step 6: Stop at the release boundary**

Record results. Do not tag or synchronize until the user explicitly approves the exact package.

## Self-Review

- [x] **Spec coverage:** Tasks 1-9 cover every included design requirement and preserve all explicit
  exclusions.
- [x] **Placeholder scan:** The plan contains no deferred implementation placeholder; excluded
  features are named as exclusions rather than future code steps.
- [x] **Type consistency:** `ScienceSceneFilters`, `ScienceEvidenceField`, `Sentinel2Scene`, provider
  validation, runtime snapshots, query builder, UI, and Agent names are consistent across tasks.
