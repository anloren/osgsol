# ScienceEarth 64D Analysis Engine Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver a scientifically bounded AlphaEarth 64D analysis engine, honest research UI and
Agent workflows, and a macOS desktop build that exits normally without regressing the accepted
Earth, camera, photo, Hong Kong 3D Tiles, clean-reset, or preview behavior.

**Architecture:** Preserve the existing A01/A16/A09 preview path and add a separate on-demand
64-band runtime behind `ScienceQueryService`. Keep decoding, numerical analysis, artifacts,
progress, and bounded in-memory evidence in GDAL-free core units; keep COG access inside the
private trimmed-GDAL target. UI, Agent tools, and the existing curved artifact renderer consume
the same immutable artifacts. Prove normal exit through the same `viewer.run()` path used by panel
Quit, window close, and `Cmd+Q`.

**Tech Stack:** C++17, CMake/CTest, OpenSceneGraph 3.6.5, Eigen 3, ImGui, picojson, SQLite RTree,
private static GDAL 3.13.1 v5 prefix, macOS CGL testing, codesign, and shell package contracts.

## Global Constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on
  `codex/scienceearth-g2-query-service`.
- Start implementation from design commit `d331f0a` and preserve the approved design in
  `docs/superpowers/specs/2026-07-16-scienceearth-64d-analysis-engine-design.md`.
- Treat `v0.2.0` / `ScienceEarth` at `9b1988b9...` as immutable.
- Use only `build/science-deps-g2-v5/prefix` for private GDAL.
- Do not add a local server, proxy, port, security gate, credential, or remote endpoint.
- Keep preview reads limited to A01/A16/A09. Preview must never load all 64 components.
- Raw `-128` is NoData. Dequantize every other value with
  `sign(q) * pow(abs(q) / 127.5, 2)` before analysis.
- Never average, interpolate, compare, cluster, or pass quantized Int8 values to PCA.
- Never assign physical meanings to a latent dimension, PCA axis, or cluster without independent
  validated evidence.
- Science core/provider/service code has no camera, ImGui, `LayerManager`, or scene authority.
- Preview continues to materialize automatically. Heavy analysis changes the map only after an
  explicit show action.
- Default regional work is two years on a 128 by 128 nearest-source-pixel sample grid. A 256 by
  256 grid or retained multi-year regional grids require explicit cost confirmation.
- Permit one active heavy 64D job. Observe cancellation between years and eight-band read batches.
- Preserve independent last-good preview and analysis artifacts on failure or cancel. Deliberate
  Remove clears every retained science artifact and visible science layer.
- Do not use `_Exit`, forced kill, crash-report suppression, or disabled signal handling as an exit
  repair.
- Use TDD for every task: focused RED, minimum implementation, focused GREEN, protected
  regressions, then commit.
- Update only `/Users/USER/Desktop/osgSol Earth.app` after staging gates pass.
- Do not tag, push, merge, or publish until manual acceptance and an explicit release request.

## Current State Ledger

```text
ACTIVE_BRANCH=codex/scienceearth-g2-query-service
DESIGN_COMMIT=d331f0a
IMPLEMENTATION_STATUS=NOT_STARTED
CURRENT_DESKTOP_VERSION=0.3.0
CURRENT_DESKTOP_SOURCE_COMMIT=338bd9d5389d0dac095f33a89202fc0851322d8e
CURRENT_DESKTOP_APP=/Users/USER/Desktop/osgSol Earth.app
PROTECTED_BOUNDARY=0e91c7c4b121d80b929d595ea711d3dd0833ee67
SCIENCE_DEPENDENCY_PREFIX=build/science-deps-g2-v5/prefix
CURRENT_PREVIEW_BANDS=A01,A16,A09
CURRENT_PROGRESS=0.05,0.20,1.00_FIXED_STEPS
CURRENT_EXIT=EXC_BAD_ACCESS_IN_OSG_APPLICATIONUSAGE_FINALIZATION
NEXT_TAG=FORBIDDEN_BEFORE_MANUAL_ACCEPTANCE
```

## File Structure

- `science/ScienceQueryTypes.*`: typed stages, analysis options, embedding/analysis payloads.
- `science/ScienceEmbedding.*`: dequantization, metrics, norm validation, vector aggregation.
- `science/ScienceAnalysisEngine.*`: time series, regional change, PCA, spherical clustering.
- `science/ScienceAnalysisExport.*`: deterministic CSV/JSON evidence.
- `science/ScienceArtifactStore.*`: bounded in-memory artifact lookup and eviction.
- `science/AlphaEarthEmbeddingRuntime.*`: indexed 64-band COG reading and heavy worker.
- `science/AlphaEarthProvider.*`: preserve preview adapter and route explicit heavy queries.
- `science/ScienceQueryService.*`: validation, budgets, retained state, display selection.
- `applications/earth_explorer/science_earth_panel.*`: progressive operations/results UI.
- `applications/earth_explorer/science_ai_tools.*`: six compatible/composable science tools.
- `applications/earth_explorer/science_preview_layer.cpp`: render selected display artifact.
- `tests/macos_normal_exit_tests.sh`: same-path exit code and `.ips` regression.
- `docs/scienceearth/normal-exit-root-cause.md`: reproducible root-cause evidence.
- `docs/scienceearth/64d-analysis-verification.md`: final automated/manual evidence.

---

## Task 1: Establish and repair the real macOS normal-exit path

**Files:**

- Create: `tests/osg_application_usage_exit_test.cpp`
- Create: `tests/macos_normal_exit_tests.sh`
- Create: `docs/scienceearth/normal-exit-root-cause.md`
- Modify: `tests/CMakeLists.txt`
- Modify: `applications/earth_explorer/earth_main.cpp:409-420,1544-1581`
- Modify only when selected by evidence: `pipeline/Allocator.h`, `packaging/package_macos.sh`, or
  the concrete owner file named by AddressSanitizer
- Modify: `tests/package_macos_tests.sh`

**Interfaces:**

- Produces `EARTH_AUTOQUIT_FRAMES=N`, an inert hook that calls `setDone(true)` from a `FRAME`
  event and still returns through `viewer.run()`.
- Produces `tests/macos_normal_exit_tests.sh APP_PATH`, which fails for a signal, timeout, or newly
  created matching `osgSol_Earth*.ips` report.
- Keeps panel Quit, window close, and `Cmd+Q` on the graceful viewer-done path.

- [ ] **Step 1: Write the minimal OSG finalization control**

Create this complete test:

```cpp
#include <osg/ApplicationUsage>
#include <osg/ArgumentParser>
#include <osgViewer/Viewer>

int main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc, argv);
    osg::ApplicationUsage::instance()->setApplicationName(arguments.getApplicationName());
    osgViewer::Viewer viewer;
    viewer.setDone(true);
    return viewer.run();
}
```

Register `osgVerse_Test_OsgApplicationUsageExit` with labels `offline;macos-exit` and timeout 10.

- [ ] **Step 2: Write the failing packaged-app exit regression**

The script must snapshot crash reports, launch the app with
`EARTH_OFFSCREEN=1 EARTH_PREFETCH=0 EARTH_IME=0 EARTH_AUTOQUIT_FRAMES=5`, enforce a 30-second
watchdog, require status zero, then require `comm -13 before after` to be empty.

```bash
app="$1"
binary="$app/Contents/MacOS/osgSol_Earth"
before="$(mktemp -t osgsol-exit-before.XXXXXX)"
after="$(mktemp -t osgsol-exit-after.XXXXXX)"
find "$HOME/Library/Logs/DiagnosticReports" -name 'osgSol_Earth*.ips' -print | sort > "$before"
set +e
/usr/bin/perl -e '$seconds=shift; alarm $seconds; exec @ARGV' 30 \
    /usr/bin/env EARTH_OFFSCREEN=1 EARTH_PREFETCH=0 EARTH_IME=0 \
    EARTH_AUTOQUIT_FRAMES=5 "$binary"
status=$?
set -e
find "$HOME/Library/Logs/DiagnosticReports" -name 'osgSol_Earth*.ips' -print | sort > "$after"
test "$status" -ne 142
test "$status" -eq 0
test -z "$(comm -13 "$before" "$after")"
```

- [ ] **Step 3: Run controls and observe RED**

```bash
cmake --build build/science_g2 --target osgVerse_Test_OsgApplicationUsageExit osgVerse_EarthExplorer -j4
ctest --test-dir build/science_g2 -R OsgApplicationUsageExit --output-on-failure
bash tests/macos_normal_exit_tests.sh '/Users/USER/Desktop/osgSol Earth.app'
```

Expected: minimal control passes; the current app ignores the new hook or reproduces the observed
`ApplicationUsage::~ApplicationUsage()` finalization failure.

- [ ] **Step 4: Add the inert same-path handler**

Add next to `CloseWindowQuitHandler`:

```cpp
class AutoQuitAfterFramesHandler : public osgGA::GUIEventHandler
{
public:
    explicit AutoQuitAfterFramesHandler(unsigned int frames) : _remaining(frames) {}
    bool handle(const osgGA::GUIEventAdapter& ea,
                osgGA::GUIActionAdapter& action) override
    {
        if (ea.getEventType() != osgGA::GUIEventAdapter::FRAME || _remaining == 0)
            return false;
        if (--_remaining == 0)
        {
            osgViewer::View* view = action.asView();
            if (view && view->getViewerBase()) view->getViewerBase()->setDone(true);
        }
        return false;
    }
private:
    unsigned int _remaining;
};
```

Register it only for a positive environment integer. Do not add an early return; the test must
reach `return viewer.run();`.

- [ ] **Step 5: Isolate the first invalid owner**

```bash
cmake -S . -B build/science_64d_asan -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DVERSE_BUILD_EXAMPLES=ON -DOSGSOL_BUILD_SCIENCE=ON -DOSGSOL_SCIENCE_DEPS_ROOT="$PWD/build/science-deps-g2-v5/prefix" -DCMAKE_C_FLAGS='-fsanitize=address -fno-omit-frame-pointer' -DCMAKE_CXX_FLAGS='-fsanitize=address -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address' -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address'
cmake --build build/science_64d_asan --target osgVerse_EarthExplorer -j4
EARTH_OFFSCREEN=1 EARTH_PREFETCH=0 EARTH_IME=0 EARTH_AUTOQUIT_FRAMES=5 build/science_64d_asan/bin/osgVerse_EarthExplorer
```

Audit staging OSG install ids and UUIDs. The known stack
`mfm_free -> ApplicationUsage::~ApplicationUsage -> __cxa_finalize_ranges` is a symptom; the
accepted root is the first invalid owner or duplicated runtime identity.

- [ ] **Step 6: Apply exactly the evidence-matched repair**

```text
ASan names an application owner       -> repair that owner and add its focused lifetime test.
Minimal OSG control also fails        -> select one ABI-compatible OSG runtime; do not patch exit.
Bundle audit finds duplicate UUIDs    -> canonicalize install ids and reject duplicates in packaging.
Only prepended static finalizer fails -> replace header-static ownership with one .cpp-owned object.
```

Record the selected branch, first invalid access, changed owner, before/after status, and report
comparison in `docs/scienceearth/normal-exit-root-cause.md`. Include one machine-readable line
`CHANGED_OWNER_FILE=<repository-relative path>` so the evidence-selected repair is staged
explicitly. `_Exit`, signal suppression, and a forced kill fail this task.

- [ ] **Step 7: Prove GREEN and commit**

```bash
ctest --test-dir build/science_g2 -R OsgApplicationUsageExit --output-on-failure
cmake --build build/science_g2 --target install -j4
env -u EARTH_AI_KEY OSGVERSE_SDK="$PWD/build/science_g2/sdk" OSGSOL_PACKAGE_OUTPUT="$PWD/dist/osgSol Earth.app" OSGSOL_PACKAGE_VERSION=0.3.0 OSGSOL_BUILD_CHANNEL=exit-test OSGSOL_SOURCE_COMMIT="$(git rev-parse HEAD)" OSGSOL_PACKAGE_EXECUTABLE=osgSol_Earth OSGSOL_ALPHAEARTH_INDEX="$PWD/build/science-index-full/alphaearth.sqlite" bash packaging/package_macos.sh
bash tests/macos_normal_exit_tests.sh "$PWD/dist/osgSol Earth.app"
git add tests/osg_application_usage_exit_test.cpp tests/macos_normal_exit_tests.sh tests/CMakeLists.txt applications/earth_explorer/earth_main.cpp tests/package_macos_tests.sh docs/scienceearth/normal-exit-root-cause.md
owner_path="$(sed -n 's/^CHANGED_OWNER_FILE=//p' docs/scienceearth/normal-exit-root-cause.md | head -1)"
test -n "$owner_path" && test -f "$owner_path"
git add -- "$owner_path"
git commit -m "fix(earth): repair normal macOS exit"
```

Expected: control and full app exit zero with no new report.

## Task 2: Extend GDAL-free contracts and honest progress

**Files:**

- Modify: `science/ScienceQueryTypes.h`
- Modify: `science/ScienceQueryTypes.cpp`
- Modify: `science/ScienceQueryTypesTest.cpp`
- Modify: `science/ScienceProvider.h`

**Interfaces:**

- Produces `ScienceProgress`, `ScienceAnalysisOptions`, `ScienceQueryCost`,
  `ScienceEmbeddingPayload`, and `ScienceAnalysisPayload`.
- Preserves existing `ScienceRasterPayload` and `lastSuccessfulArtifact` compatibility.

- [ ] **Step 1: Write failing defaults and immutable-payload tests**

```cpp
ScienceAnalysisOptions options;
require(options.gridSize == 128 && options.hotspotQuantile == 0.90,
        "analysis defaults changed");
require(options.pcaComponents == 3 && options.clusterCount == 4,
        "advanced defaults changed");
ScienceProgress progress;
require(progress.stage == ScienceProgressStage::Idle && !progress.determinate &&
        progress.completedUnits == 0 && progress.totalUnits == 0,
        "progress fabricated a percentage");
```

Also require immutable embedding/analysis backing vectors to retain pointer identity after artifact
copy.

- [ ] **Step 2: Build to observe RED**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceQueryTypes -j4
```

- [ ] **Step 3: Add typed stages, outputs, and options**

```cpp
enum class ScienceProgressStage
{
    Idle, Queued, Locating, Reading, Decoding, Validating,
    Aligning, Analyzing, Materializing, Cancelling, Ready, Failed, Cancelled
};
enum class ScienceOutputKind
{
    RasterLayer, Table, VectorFeatures, Embedding, TimeSeries, Analysis, Export
};
enum class ScienceAnalysisKind
{
    None, PointSeries, RegionalChange, PrincipalComponents, SphericalClusters
};
enum class ScienceMetric
{
    DotProduct, CosineSimilarity, CosineDistance, EuclideanDistance, AngularDistance
};
struct ScienceProgress
{
    ScienceProgressStage stage = ScienceProgressStage::Idle;
    std::uint64_t completedUnits = 0, totalUnits = 0;
    bool determinate = false;
    std::string unit;
    double elapsedSeconds = 0.0;
};
struct ScienceAnalysisOptions
{
    ScienceAnalysisKind kind = ScienceAnalysisKind::None;
    std::vector<ScienceMetric> metrics = {ScienceMetric::CosineSimilarity,
                                          ScienceMetric::CosineDistance};
    int baselineYear = 0, comparisonYear = 0, gridSize = 128;
    double hotspotQuantile = 0.90;
    bool enablePca = false, enableClustering = false, confirmedLargeRequest = false;
    int pcaComponents = 3, clusterCount = 4;
};
struct ScienceQueryCost
{
    std::uint64_t sourceBytesUpperBound = 0;
    std::uint64_t residentBytesUpperBound = 0;
    std::uint64_t resultCells = 0;
    double estimatedDurationSeconds = 0.0;
    bool durationDeterminate = false;
    bool requiresConfirmation = false;
};
```

Define embedding payload fields for years, width, height, exactly 64 components, immutable floats,
mask, bounds, ground grid, actual resolution, processing, valid/NoData counts, coverage, norms, and
warnings. Define analysis records for metrics, annual series, scalar change raster, PCA, clusters,
interpretation, and limitations. Add stable enum-name functions and overflow-checked
`estimatedArtifactBytes()`.

- [ ] **Step 4: Migrate provider/service snapshots without breaking preview**

Add `ScienceProgress progress` to generic snapshots. Temporarily derive legacy `float progress`
only for measurable totals. Private `AlphaEarthPreviewSnapshot::progress` remains isolated until
the UI migration.

- [ ] **Step 5: Run focused compatibility tests**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceQueryTypes osgSol_Test_ScienceQueryService osgSol_Test_AlphaEarthProvider -j4
ctest --test-dir build/science_g2 --output-on-failure -R 'ScienceQueryTypes|ScienceQueryService|AlphaEarthProvider'
```

- [ ] **Step 6: Commit**

```bash
git add science/ScienceQueryTypes.h science/ScienceQueryTypes.cpp science/ScienceQueryTypesTest.cpp science/ScienceProvider.h
git commit -m "feat(scienceearth): define 64d analysis contracts"
```

## Task 3: Implement dequantization, metrics, and vector aggregation

**Files:**

- Create: `science/ScienceEmbedding.h`
- Create: `science/ScienceEmbedding.cpp`
- Create: `science/ScienceEmbeddingTest.cpp`
- Modify: `science/CMakeLists.txt`

**Interfaces:**

- Produces `dequantizeAlphaEarth()`, `compareEmbeddingVectors()`,
  `aggregateEmbeddingVectors()`, and norm summaries.

- [ ] **Step 1: Write the deterministic failing fixtures**

```cpp
bool valid = false;
require(nearlyEqual(dequantizeAlphaEarth(0, valid), 0.0) && valid,
        "zero is valid");
require(nearlyEqual(dequantizeAlphaEarth(127, valid),
                    std::pow(127.0 / 127.5, 2.0)) && valid,
        "positive dequantization changed");
require(nearlyEqual(dequantizeAlphaEarth(-127, valid),
                    -std::pow(127.0 / 127.5, 2.0)) && valid,
        "negative dequantization changed");
dequantizeAlphaEarth(-128, valid);
require(!valid, "-128 must remain NoData");
```

Test identical, orthogonal, opposite, and scaled 64D vectors. Require cosine `1,0,-1,1`, angular
`0,pi/2,pi,0`, finite dot/Euclidean results, invalid zero norms, valid-only aggregation, and failure
when no valid vector remains. Feed an unexpected but finite norm distribution and require a
diagnostic warning without silent normalization or rejection.

- [ ] **Step 2: Build to observe RED**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceEmbedding -j4
```

- [ ] **Step 3: Implement the exact public seam**

```cpp
constexpr std::size_t SCIENCE_EMBEDDING_COMPONENTS = 64;
struct ScienceVectorMetrics
{
    double dotProduct = 0.0, cosineSimilarity = 0.0, cosineDistance = 0.0;
    double euclideanDistance = 0.0, angularDistanceRadians = 0.0;
};
double dequantizeAlphaEarth(std::int8_t raw, bool& valid);
bool compareEmbeddingVectors(const float* a, const float* b,
                             ScienceVectorMetrics& output, std::string& error);
bool aggregateEmbeddingVectors(const float* values, const unsigned char* validMask,
                               std::size_t vectorCount,
                               std::array<float, 64>& meanDirection,
                               double& concentration, std::string& error);
```

Accumulate in double precision, clamp cosine only before `acos`, reject non-finite/zero norm, and
never mutate input.

- [ ] **Step 4: Run GREEN and commit**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceEmbedding -j4
ctest --test-dir build/science_g2 -R ScienceEmbedding --output-on-failure
git add science/ScienceEmbedding.h science/ScienceEmbedding.cpp science/ScienceEmbeddingTest.cpp science/CMakeLists.txt
git commit -m "feat(scienceearth): add embedding math primitives"
```

## Task 4: Implement point series and regional change analysis

**Files:**

- Create: `science/ScienceAnalysisEngine.h`
- Create: `science/ScienceAnalysisEngine.cpp`
- Create: `science/ScienceAnalysisEngineTest.cpp`
- Modify: `science/CMakeLists.txt`

**Interfaces:**

- Produces `analyzePointSeries()`, `analyzeRegionalChange()`,
  `analyzeRegionalAnnualSummaries()`, and `materializeChangeRaster()`.

- [ ] **Step 1: Write failing temporal and regional tests**

Create a nine-year point fixture with one missing year and a known largest consecutive angular
change. Require the gap to remain missing. Create two 4 by 4 regional slices with 12 valid overlap
cells and known change scores. Require exact count, statistics, quantiles, default 90th-percentile
hotspots, bounds, mask, and ground grid. Feed nine regional slices sequentially and require one
annual summary per valid year while the engine retains at most one non-comparison grid.

- [ ] **Step 2: Build to observe RED**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceAnalysisEngine -j4
```

- [ ] **Step 3: Implement bounded deterministic analysis**

```cpp
class ScienceAnalysisEngine
{
public:
    static std::shared_ptr<const ScienceAnalysisPayload> analyzePointSeries(
        const ScienceEmbeddingPayload&, const ScienceAnalysisOptions&,
        const std::function<bool()>& cancelled, std::string& error);
    static std::shared_ptr<const ScienceAnalysisPayload> analyzeRegionalChange(
        const ScienceEmbeddingPayload&, const ScienceAnalysisOptions&,
        const std::function<bool()>& cancelled, std::string& error);
    using YearSliceLoader = std::function<
        std::shared_ptr<const ScienceEmbeddingPayload>(int year, std::string& error)>;
    static bool analyzeRegionalAnnualSummaries(
        const std::vector<int>& years, const YearSliceLoader& loadYear,
        ScienceAnalysisPayload& output,
        const std::function<bool()>& cancelled, std::string& error);
    static ScienceRasterPayload materializeChangeRaster(
        const ScienceAnalysisPayload&, const ScienceEmbeddingPayload&,
        std::string& error);
};
```

Use cosine distance as default change. Sort a copy for quantiles and define interpolation once.
Colorize valid scalar values with a perceptually ordered map and alpha 166; NoData alpha is zero.
Keep the yellow border in the renderer. `analyzeRegionalAnnualSummaries()` must invoke
`loadYear()` once per year in ascending order, extract the bounded summary, release that slice
before requesting the next year, and stop before the next load when cancellation is observed.

- [ ] **Step 4: Run GREEN and commit**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceAnalysisEngine -j4
ctest --test-dir build/science_g2 -R ScienceAnalysisEngine --output-on-failure
git add science/ScienceAnalysisEngine.h science/ScienceAnalysisEngine.cpp science/ScienceAnalysisEngineTest.cpp science/CMakeLists.txt
git commit -m "feat(scienceearth): analyze temporal and regional change"
```

## Task 5: Add reproducible PCA, spherical clustering, and evidence export

**Files:**

- Modify: `science/ScienceAnalysisEngine.h`
- Modify: `science/ScienceAnalysisEngine.cpp`
- Modify: `science/ScienceAnalysisEngineTest.cpp`
- Create: `science/ScienceAnalysisExport.h`
- Create: `science/ScienceAnalysisExport.cpp`
- Create: `science/ScienceAnalysisExportTest.cpp`
- Modify: `science/CMakeLists.txt`

**Interfaces:**

- Produces `computeLocalPca()`, `computeSphericalClusters()`, `exportAnalysisCsv()`, and
  `exportAnalysisJson()`.
- Guarantees deterministic PCA signs, cluster ordering, parameters, provenance, and limitations.

```cpp
class ScienceAnalysisEngine
{
public:
    static bool computeLocalPca(
        const ScienceEmbeddingPayload&, int componentCount,
        ScienceAnalysisPayload&, const std::function<bool()>& cancelled,
        std::string& error);
    static bool computeSphericalClusters(
        const ScienceEmbeddingPayload&, int clusterCount,
        ScienceAnalysisPayload&, const std::function<bool()>& cancelled,
        std::string& error);
};
```

- [ ] **Step 1: Write failing low-rank PCA and clustering tests**

Use samples varying only in components 0 and 1. Require the first two eigenvalues to contain all
variance within `1e-5`, explained ratios to sum to one, and the largest-absolute loading of each
axis to be positive. Replay the same data and require identical serialization.

Use two normalized directional groups. Require deterministic farthest-first initialization, two
non-empty clusters, convergence, lexicographically sorted centroids, and stable ids on replay.

- [ ] **Step 2: Write failing export tests**

Require JSON to parse and contain geometry, years, dataset/provider/processing versions,
algorithms, parameters, warnings, limitations, and upstream ids. Default CSV must omit A01-A64;
raw export must contain all 64 only when `includeRawComponents=true`.

- [ ] **Step 3: Build to observe RED**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceAnalysisEngine osgSol_Test_ScienceAnalysisExport -j4
```

- [ ] **Step 4: Implement PCA and clustering**

Use centered, non-standardized `Eigen::MatrixXf` input and
`Eigen::SelfAdjointEigenSolver<Eigen::Matrix<float,64,64>>`. Sort eigenpairs descending.
Canonicalize sign by making the largest-absolute loading positive, breaking ties by component
index.

Spherical k-means accepts `k=2..8`, uses farthest-first deterministic initialization,
double-precision cosine assignment, normalized centroid updates, tolerance `1e-6`, and at most 100
iterations. Sort centroids lexicographically and remap ids.

- [ ] **Step 5: Implement deterministic export**

```cpp
struct ScienceExportOptions { bool includeRawComponents = false; };
std::string exportAnalysisCsv(const ScienceArtifact& artifact,
                              const ScienceExportOptions& options);
std::string exportAnalysisJson(const ScienceArtifact& artifact,
                               const ScienceExportOptions& options);
```

Use picojson for JSON escaping and a dedicated CSV field escaper. Never serialize RGB display
bytes as embeddings.

In `science/CMakeLists.txt`, keep Eigen and picojson header-only and private to core:

```cmake
TARGET_INCLUDE_DIRECTORIES(osgSolScienceCore PRIVATE "${CMAKE_SOURCE_DIR}/3rdparty")
```

- [ ] **Step 6: Run GREEN and commit**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceAnalysisEngine osgSol_Test_ScienceAnalysisExport -j4
ctest --test-dir build/science_g2 --output-on-failure -R 'ScienceAnalysisEngine|ScienceAnalysisExport'
git add science/ScienceAnalysisEngine.h science/ScienceAnalysisEngine.cpp science/ScienceAnalysisEngineTest.cpp science/ScienceAnalysisExport.h science/ScienceAnalysisExport.cpp science/ScienceAnalysisExportTest.cpp science/CMakeLists.txt
git commit -m "feat(scienceearth): add latent structure analysis"
```

## Task 6: Read complete AlphaEarth embeddings on demand

**Files:**

- Create: `science/AlphaEarthEmbeddingRuntime.h`
- Create: `science/AlphaEarthEmbeddingRuntime.cpp`
- Create: `science/AlphaEarthEmbeddingRuntimeTest.cpp`
- Modify: `science/CMakeLists.txt`

**Interfaces:**

- Produces `AlphaEarthEmbeddingRuntime::submit/cancel/snapshot` with generic snapshots.
- Consumes production SQLite resolution or an injected local fixture resolver.
- Guarantees 64 named Int8 bands, shared sample positions, eight-band cancellation points, actual
  footprint/resolution, and processing provenance.

- [ ] **Step 1: Write the local 64-band fixture test**

Create a temporary 8 by 8 GeoTIFF with 64 `GDT_Int8` bands named A01-A64, EPSG:4326 transform,
one `-128` sample, and deterministic values. Inject a resolver returning it for 2017 and 2018.

Require point mode to return `2*64` floats and two validity entries. Require region mode to return
`2*4*4*64` floats, 4 by 4 masks, exact bounds, and one ground grid. Assert decoded values through
`dequantizeAlphaEarth()`.

- [ ] **Step 2: Build to observe RED**

```bash
cmake --build build/science_g2 --target osgSol_Test_AlphaEarthEmbeddingRuntime -j4
```

- [ ] **Step 3: Define resolver and runtime**

```cpp
struct AlphaEarthAsset
{
    std::string datasetId, pathOrUrl, sourceVersion;
    ScienceWgs84Bounds indexedBounds;
};
using AlphaEarthAssetResolver = std::function<bool(
    double latitude, double longitude, int year,
    AlphaEarthAsset& asset, std::string& error)>;
class AlphaEarthEmbeddingRuntime
{
public:
    explicit AlphaEarthEmbeddingRuntime(const std::string& indexPath);
    explicit AlphaEarthEmbeddingRuntime(AlphaEarthAssetResolver resolver);
    std::uint64_t submit(const GeoTemporalQuery& query);
    void cancel(std::uint64_t generation);
    ScienceProviderSnapshot snapshot() const;
};
```

Production keeps the immutable SQLite/RTree query and accepts its indexed
`https://data.source.coop/` base. Injection is for local deterministic tests only.

- [ ] **Step 4: Implement exact reads and progress**

Validate 64 band descriptions and `GDT_Int8`. In nearest mode, use one grid and
`GRIORA_NearestNeighbour` for all components. Read ordered groups of eight and report:

```cpp
progress.stage = ScienceProgressStage::Reading;
progress.completedUnits = completedBands;
progress.totalUnits = 64;
progress.determinate = true;
progress.unit = "dimensions";
```

Check cancellation after every batch/year. Dequantize after raw reads, create one complete-vector
mask, compute norms, then call the analysis engine. Record nearest sampling. For requested mean
aggregation, read a budgeted source block, dequantize it, then call vector aggregation; reject an
oversized input before reading.

For regional annual summaries, load one year, calculate its summary against the baseline, release
that non-comparison grid, then continue. Keep the selected comparison pair resident for the map;
never retain all nine regional 64D grids by default.

- [ ] **Step 5: Add cancel and no-coverage tests**

Block the fixture resolver after the first year, cancel, and require `Cancelled` with no partial
Ready. Return `No AlphaEarth tile covers this point and year` and require it to remain distinct
from GDAL/network failure.

- [ ] **Step 6: Run GREEN, preview regression, and commit**

```bash
cmake --build build/science_g2 --target osgSol_Test_AlphaEarthEmbeddingRuntime osgSol_Test_SciencePreviewRegression -j4
ctest --test-dir build/science_g2 --output-on-failure -R 'AlphaEarthEmbeddingRuntime|SciencePreviewRegression'
git add science/AlphaEarthEmbeddingRuntime.h science/AlphaEarthEmbeddingRuntime.cpp science/AlphaEarthEmbeddingRuntimeTest.cpp science/CMakeLists.txt
git commit -m "feat(scienceearth): load complete AlphaEarth embeddings"
```

## Task 7: Route heavy jobs through provider, service, and bounded artifacts

**Files:**

- Create: `science/ScienceArtifactStore.h`
- Create: `science/ScienceArtifactStore.cpp`
- Create: `science/ScienceArtifactStoreTest.cpp`
- Modify: `science/AlphaEarthProvider.h`
- Modify: `science/AlphaEarthProvider.cpp`
- Modify: `science/AlphaEarthProviderTest.cpp`
- Modify: `science/ScienceQueryService.h`
- Modify: `science/ScienceQueryService.cpp`
- Modify: `science/ScienceQueryServiceTest.cpp`
- Modify: `science/CMakeLists.txt`

**Interfaces:**

- Produces `estimate(query)`, `findArtifact(id)`, `showArtifact(id)`, `clearArtifacts()`,
  independent last-good preview/analysis, and `displayArtifact`.
- Preserves `clearArtifact()` as an all-gone compatibility alias until consumers migrate.

- [ ] **Step 1: Write failing bounded-store tests**

Construct ten artifacts and a store with `maxArtifacts=3,maxBytes=4096`. Require lookup,
oldest-unpinned eviction, byte accounting, oversized rejection, and clear. Pin display and two
last-good ids; require a rejected insert instead of pinned eviction.

- [ ] **Step 2: Write failing routing and budget tests**

Require exact A01/A16/A09 raster requests to use preview; `embedding64` series/change uses the new
runtime. Replacement failure preserves both last-good kinds. Analysis success does not change
display; `showArtifact(id)` changes it without camera access.

Require `estimate(query)` to return exact cell and memory upper bounds. Duration is determinate
only after the provider has observed successful throughput; before that the field remains explicitly
unknown rather than using a fabricated number.

```cpp
query.analysis.gridSize = 256;
query.analysis.confirmedLargeRequest = false;
requireFailedWith(query, "256x256 analysis requires explicit confirmation");
query.analysis.confirmedLargeRequest = true;
query.limits.maximumMemoryBytes = 1024;
requireFailedWith(query, "analysis exceeds maximum memory");
```

- [ ] **Step 3: Build to observe RED**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceArtifactStore osgSol_Test_AlphaEarthProvider osgSol_Test_ScienceQueryService -j4
```

- [ ] **Step 4: Implement bounded store and retained service state**

```cpp
class ScienceArtifactStore
{
public:
    explicit ScienceArtifactStore(std::size_t maxArtifacts = 8,
                                  std::uint64_t maxBytes = 256ull * 1024ull * 1024ull);
    bool put(std::shared_ptr<const ScienceArtifact> artifact,
             const std::vector<std::string>& pinnedIds, std::string& error);
    std::shared_ptr<const ScienceArtifact> find(const std::string& id) const;
    void clear();
};
```

Pin display, last preview, and last analysis. Preview success remains auto-displayed. Analysis
success updates only last analysis/store. Stale generations never enter the store.

Implement `ScienceQueryCost ScienceQueryService::estimate(const GeoTemporalQuery&) const` with
overflow-checked dimensions. The service validates this cost before provider submission and
requires `confirmedLargeRequest` for 256 by 256 or retained multi-year regional work.

- [ ] **Step 5: Implement provider routing and capabilities**

Advertise A01-A64 plus `embedding64`, time series, analysis, and export. Route only the exact legacy
raster signature to `SciencePreviewRuntime`. Cancel the inactive runtime before starting the
selected one and translate both to generic progress/artifacts.

- [ ] **Step 6: Run GREEN and commit**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceArtifactStore osgSol_Test_AlphaEarthProvider osgSol_Test_ScienceQueryService osgSol_Test_SciencePreviewRegression -j4
ctest --test-dir build/science_g2 --output-on-failure -R 'ScienceArtifactStore|AlphaEarthProvider|ScienceQueryService|SciencePreviewRegression'
git add science/ScienceArtifactStore.h science/ScienceArtifactStore.cpp science/ScienceArtifactStoreTest.cpp science/AlphaEarthProvider.h science/AlphaEarthProvider.cpp science/AlphaEarthProviderTest.cpp science/ScienceQueryService.h science/ScienceQueryService.cpp science/ScienceQueryServiceTest.cpp science/CMakeLists.txt
git commit -m "feat(scienceearth): orchestrate bounded 64d research"
```

## Task 8: Build exact desktop queries and materialize selected artifacts

**Files:**

- Modify: `applications/earth_explorer/science_query_builder.h`
- Modify: `applications/earth_explorer/science_preview_layer.cpp`
- Modify: `science/SciencePreviewRegressionTest.cpp`
- Modify: `applications/earth_explorer/science_ai_tools_test.cpp`

**Interfaces:**

- Produces `makeSciencePointSeriesQuery()` and `makeScienceRegionalAnalysisQuery()`.
- Renderer changes input to `displayArtifact` only; mesh, altitude, alpha, border, depth, and culling
  stay unchanged.

- [ ] **Step 1: Write failing builders and display tests**

Require point series to select 2017-2025, output `TimeSeries`, and variable `embedding64`. Require
regional change to select two ordered years, output `Analysis`, default 128 grid, and
`RegionalChange`. Require preview builder fields unchanged.

Provide older preview, newer hidden analysis, and explicit display artifact. Require the layer to
render only display generation while existing geolocation/orientation/opacity tests remain.

- [ ] **Step 2: Build to observe RED**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceAiTools osgSol_Test_SciencePreviewRegression -j4
```

- [ ] **Step 3: Implement builders and display selection**

```cpp
GeoTemporalQuery makeSciencePointSeriesQuery(
    const ScienceSourceDescriptor& source, double latitude, double longitude,
    int firstYear, int lastYear);
GeoTemporalQuery makeScienceRegionalAnalysisQuery(
    const ScienceSourceDescriptor& source, double latitude, double longitude,
    double spanMeters, int baselineYear, int comparisonYear,
    const ScienceAnalysisOptions& options);
```

Clamp only UI years/span. Service still rejects malformed Agent queries. Synchronize the layer from
`snapshot.displayArtifact`; do not alter `createSciencePreviewArtifactNode()`.

- [ ] **Step 4: Run GREEN and commit**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceAiTools osgSol_Test_SciencePreviewRegression -j4
ctest --test-dir build/science_g2 --output-on-failure -R 'ScienceAiTools|SciencePreviewRegression'
git add applications/earth_explorer/science_query_builder.h applications/earth_explorer/science_preview_layer.cpp applications/earth_explorer/science_ai_tools_test.cpp science/SciencePreviewRegressionTest.cpp
git commit -m "feat(scienceearth): build and display analysis queries"
```

## Task 9: Extend Agent research without camera authority

**Files:**

- Modify: `applications/earth_explorer/science_ai_tools.cpp`
- Modify: `applications/earth_explorer/science_ai_tools_test.cpp`

**Interfaces:**

- Preserves the four existing tool names and legacy preview arguments.
- Produces `compare_science_artifacts` and `run_change_analysis`.
- Returns compact summaries, artifact ids, provenance, and limitations without raw grids.

- [ ] **Step 1: Write failing compatibility and new-tool tests**

Require legacy `{lat,lon,year}` to submit the same preview. Add `point_series` and
`regional_embedding` modes. Require exactly six science tools after registration.

For analysis JSON require artifact id, kind, years, primary metrics, coverage, source/version,
processing, warnings, and limitations. Require output to omit `embedding_values`, `change_values`,
`rgba`, and raw A01-A64. Record manipulator matrix before/after and require equality.

- [ ] **Step 2: Run to observe RED**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceAiTools -j4
ctest --test-dir build/science_g2 -R ScienceAiTools --output-on-failure
```

- [ ] **Step 3: Extend `start_science_research` compatibly**

Add `mode`, `first_year`, `last_year`, `baseline_year`, `comparison_year`, `grid_size`,
`enable_pca`, and `cluster_count`. Omitted mode remains `preview`. Only preview auto-shows; heavy
modes return a job and leave the map unchanged.

- [ ] **Step 4: Add compare/change tools**

```json
{"name":"compare_science_artifacts","required":["left_artifact_id","right_artifact_id"]}
{"name":"run_change_analysis","required":["lat","lon","baseline_year","comparison_year"]}
```

Compare looks up compatible ready artifacts and returns bounded metrics. Change submits a region
query and does not show/navigate. Preserve `show_science_artifact` legacy `job_id`; add optional
`artifact_id` for explicit materialization.

- [ ] **Step 5: Return honest structured progress**

```json
{"stage":"reading","completed":32,"total":64,"unit":"dimensions","determinate":true}
```

Omit percent when indeterminate. Do not include full embedding arrays.

- [ ] **Step 6: Run GREEN, camera scan, and commit**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceAiTools -j4
ctest --test-dir build/science_g2 -R ScienceAiTools --output-on-failure
rg -n 'setByEye|setByMatrix|setCenter|setDistance|fly' applications/earth_explorer/science_ai_tools.cpp
git add applications/earth_explorer/science_ai_tools.cpp applications/earth_explorer/science_ai_tools_test.cpp
git commit -m "feat(scienceearth): expose 64d Agent research"
```

Expected: tests pass and the camera-authority scan is empty.

## Task 10: Implement the approved progressive ScienceEarth UI

**Files:**

- Create: `applications/earth_explorer/science_earth_panel.h`
- Create: `applications/earth_explorer/science_earth_panel.cpp`
- Create: `applications/earth_explorer/science_earth_panel_test.cpp`
- Modify: `applications/earth_explorer/EarthControlUI.h`
- Modify: `applications/earth_explorer/earth_control_layout.h`
- Modify: `tests/earth_control_layout_tests.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`
- Modify: `applications/earth_explorer/earth_main.cpp`

**Interfaces:**

- Produces a compact left operation card and collapsible right result card.
- Consumes service snapshots/query builders and performs no analysis math.
- Removes the fixed full-width year slider and fabricated generic `ProgressBar(float)`.

- [ ] **Step 1: Write failing panel-state and layout tests**

Test defaults: preview, current location, latest year, advanced collapsed, result expanded, 128
grid, PCA/clustering off. Feed queued, reading 32/64, no coverage, failure, cancelled, ready, stale,
and retained-last-good snapshots. Require distinct title, severity, stage text, and retention reason.

At 1024 by 576 require left panel at most 34%, expanded right at most 32%, center map at least 30%,
and collapsed result at most 44 points. Keep text wrapping and bidirectional scrolling assertions.

- [ ] **Step 2: Build to observe RED**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceEarthPanel osgVerse_Test_EarthControlLayout -j4
```

- [ ] **Step 3: Implement panel state and honest formatting**

```cpp
enum class SciencePanelMode { Preview, PointSeries, RegionalChange };
struct SciencePanelState
{
    SciencePanelMode mode = SciencePanelMode::Preview;
    int firstYear = 2017, lastYear = 2025;
    int baselineYear = 2017, comparisonYear = 2025, gridSize = 128;
    bool advancedOpen = false, resultExpanded = true;
    bool enablePca = false, enableClustering = false;
    int clusterCount = 4;
};

class ScienceEarthPanel
{
public:
    void drawOperations(earthscience::ScienceQueryService*, SciencePreviewLayer*,
                        LayerManager*, osgVerse::EarthManipulator*);
    void drawResults(earthscience::ScienceQueryService*, SciencePreviewLayer*, LayerManager*);
    const SciencePanelState& state() const;
    void setStateForTest(const SciencePanelState&);
private:
    SciencePanelState _state;
};
```

Indeterminate locating shows a busy marker and no percent. Failed replacement names the failure
and separately states that the prior artifact remains.

- [ ] **Step 4: Implement progressive operation/result windows**

Default left: location/footprint, mode, year/pair, one primary action. Advanced: grid, metrics, PCA,
clusters, export, cost confirmation. Use discrete year controls with visible endpoints and `-`/`+`
alternatives. Progress is read-only.

Before enabling a 256 by 256 or retained multi-year request, call `service->estimate(query)` and
show its source-byte upper bound, resident-memory upper bound, result cells, and observed duration
estimate. If duration is not determinate, display `尚无可测时长 / duration not yet measurable`.
The confirmation checkbox applies only to that displayed cost.

Default right: one summary, two metrics, one chart/legend, one visible limitation. PCA, clusters,
methods, provenance, raw dimensions, and export are collapsed. Entire results collapse to a narrow
handle. All descriptions use `TextWrapped`; do not use `AlwaysAutoResize` or `NoScrollbar`.

Use `ImGui::PlotLines` for the bounded 2017-2025 metric series and a compact legend for change
raster/cluster colors. A color legend must repeat that values are latent embedding relationships,
not physical measurements or natural color.

- [ ] **Step 5: Delegate from `EarthControlUI`**

Replace the inline block at `EarthControlUI.h:400-580` with:

```cpp
_sciencePanel.drawOperations(_scienceService, _scienceLayer, _layers, _mani);
// after the left window ends:
_sciencePanel.drawResults(_scienceService, _scienceLayer, _layers);
```

Keep service/layer injection. Remove `_scienceYear` and old `ProgressBar(snapshot.progress)`.

- [ ] **Step 6: Run focused regressions**

```bash
cmake --build build/science_g2 --target osgSol_Test_ScienceEarthPanel osgVerse_Test_EarthControlLayout osgSol_Test_ScienceQueryService osgSol_Test_SciencePreviewRegression -j4
ctest --test-dir build/science_g2 --output-on-failure -R 'ScienceEarthPanel|EarthControlLayout|ScienceQueryService|SciencePreviewRegression'
```

- [ ] **Step 7: Remove generic fabricated float progress**

```bash
rg -n '\.progress|ProgressBar' science applications/earth_explorer
```

Migrate every generic consumer to `ScienceProgress`, then remove the generic float. The private
legacy preview float may remain only inside its adapter.

- [ ] **Step 8: Commit**

```bash
git add applications/earth_explorer/science_earth_panel.h applications/earth_explorer/science_earth_panel.cpp applications/earth_explorer/science_earth_panel_test.cpp applications/earth_explorer/EarthControlUI.h applications/earth_explorer/earth_control_layout.h applications/earth_explorer/CMakeLists.txt applications/earth_explorer/earth_main.cpp tests/earth_control_layout_tests.cpp science/ScienceQueryTypes.h science/ScienceProvider.h science/ScienceQueryService.cpp applications/earth_explorer/science_ai_tools.cpp
git commit -m "feat(scienceearth): deliver progressive analysis UI"
```

## Task 11: Run complete regressions and real AlphaEarth verification

**Files:**

- Modify only when a scoped defect is exposed: files/tests from Tasks 1-10
- Create: `docs/scienceearth/64d-analysis-verification.md`

**Interfaces:**

- Produces enabled/offline, science-off, real bounded COG, exit, and protected regression evidence.
- Does not replace the Desktop app.

- [ ] **Step 1: Verify dependency and source state**

```bash
SCIENCE_DEPS_ROOT=build/science-deps-g2-v5 packaging/science_deps/build_science_deps.sh --verify
git diff --check
git status --short
```

Expected: v5 verification passes; no v6 path or uncommitted production change.

- [ ] **Step 2: Run full enabled offline suite**

```bash
cmake --build build/science_g2 -j4
ctest --test-dir build/science_g2 -L offline --output-on-failure
```

Expected: zero failures. A real failure returns to its focused RED/GREEN test before rerun.

- [ ] **Step 3: Run clean science-off suite**

```bash
cmake -S . -B build/science_64d_off -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVERSE_BUILD_EXAMPLES=ON -DOSGSOL_BUILD_SCIENCE=OFF
cmake --build build/science_64d_off -j4
ctest --test-dir build/science_64d_off -L offline --output-on-failure
```

Expected: established science-off suite passes and app links no new science target.

- [ ] **Step 4: Run real bounded research**

Using the production index and normal Source Cooperative path, verify NVIDIA headquarters and one
Hong Kong point:

```text
point series 2017-2025 -> 64 values for every valid year; gaps explicit
region 2018 vs 2025    -> 128x128; actual footprint/coverage/statistics
annual region overview -> sequential summaries; only comparison grids retained
cancel during 32/64    -> Cancelled; no partial Ready; last-good retained
PCA/clusters replay    -> identical hashes; limitation text present
```

Record dataset ids, read counters when available, time, sizes, norm diagnostics, and artifact ids.
Do not start a proxy or local network service.

- [ ] **Step 5: Run protected checks**

```bash
ctest --test-dir build/science_g2 --output-on-failure -R 'EarthManipulator|TerrainGrid|Tiles3dPaging|Satellite|EarthControlLayout|Science'
rg -n 'setByEye|setByMatrix|setCenter|setDistance|fly' applications/earth_explorer/science_*.cpp
git diff --check d331f0a..HEAD
```

Expected: tests pass, camera scan empty, diff clean.

- [ ] **Step 6: Record and commit verification**

Write commands, counts, dataset ids/timings, cancellation, hashes, warnings, and protected results
to `docs/scienceearth/64d-analysis-verification.md`.

```bash
git add docs/scienceearth/64d-analysis-verification.md
git commit -m "test(scienceearth): verify 64d analysis engine"
```

## Task 12: Package one fixed app and hand it to the user

**Files:**

- Modify: `tests/package_macos_tests.sh`
- Modify only for a proven packaging defect: `packaging/package_macos.sh`
- Append: `docs/scienceearth/64d-analysis-verification.md`

**Interfaces:**

- Produces verified staging, then atomically updates only
  `/Users/USER/Desktop/osgSol Earth.app`.
- Requires normal exit/no `.ips`, product identity, signatures, render smoke, and science index.

- [ ] **Step 1: Extend package tests before packaging**

Require exact source commit, app name/id, production index hash, no key, no `imgui.ini`, no private
path, one OSG runtime UUID per family, and same-path exit test against staging.

- [ ] **Step 2: Run package RED/GREEN**

```bash
bash tests/package_macos_tests.sh
```

Expected RED: same-path normal-exit assertion absent. Expected GREEN after wiring: all package,
signature, render, identity, dependency, and exit checks pass.

- [ ] **Step 3: Build/install fresh Release candidate**

```bash
cmake -S . -B build/science_64d_candidate -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVERSE_BUILD_EXAMPLES=ON -DOSGSOL_BUILD_SCIENCE=ON -DOSGSOL_SCIENCE_DEPS_ROOT="$PWD/build/science-deps-g2-v5/prefix" -DCMAKE_INSTALL_PREFIX="$PWD/build/science_64d_candidate/sdk"
cmake --build build/science_64d_candidate -j4
ctest --test-dir build/science_64d_candidate -L offline --output-on-failure
cmake --build build/science_64d_candidate --target install -j4
```

- [ ] **Step 4: Package/audit staging**

```bash
env -u EARTH_AI_KEY OSGVERSE_SDK="$PWD/build/science_64d_candidate/sdk" OSGSOL_PACKAGE_OUTPUT="$PWD/dist/osgSol Earth.app" OSGSOL_PACKAGE_VERSION=0.3.0 OSGSOL_BUILD_CHANNEL=manual-test OSGSOL_SOURCE_COMMIT="$(git rev-parse HEAD)" OSGSOL_PACKAGE_EXECUTABLE=osgSol_Earth OSGSOL_ALPHAEARTH_INDEX="$PWD/build/science-index-full/alphaearth.sqlite" bash packaging/package_macos.sh
python3 packaging/audit_macos_bundle.py "$PWD/dist/osgSol Earth.app"
bash tests/macos_normal_exit_tests.sh "$PWD/dist/osgSol Earth.app"
codesign --verify --deep --strict "$PWD/dist/osgSol Earth.app"
```

Expected: all pass, exit zero, no new report.

- [ ] **Step 5: Atomically update the fixed Desktop app**

```bash
desktop='/Users/USER/Desktop/osgSol Earth.app'
commit="$(git rev-parse --short HEAD)"
backup="$PWD/build/desktop-backups/pre-64d-$commit/osgSol Earth.app"
mkdir -p "$(dirname "$backup")"
cp -a "$desktop" "$backup"
rm -rf "$desktop.new"
cp -a "$PWD/dist/osgSol Earth.app" "$desktop.new"
xattr -cr "$desktop.new"
codesign --force --deep --sign - "$desktop.new"
mv "$desktop" "$desktop.previous"
mv "$desktop.new" "$desktop"
rm -rf "$desktop.previous"
codesign --verify --deep --strict "$desktop"
```

Never create a version-suffixed Desktop sibling.

- [ ] **Step 6: Give the user the manual matrix**

Ask the user to verify:

1. panel scrolls both directions, text is complete, and results collapse;
2. preview stays fast and reports real stages/counts;
3. point series shows 2017-2025 with gaps explicit;
4. 2018/2025 region shows footprint, scale, legend, and limitation;
5. PCA/clusters remain mathematical and unlabeled;
6. CSV/JSON export preserves provenance and raw A01-A64 appears only after explicit raw export;
7. failure/no coverage/cancel retains the prior result with explanation;
8. explicit Show changes map; analysis alone does not move camera;
9. Agent runs series/change, cites evidence, and does not navigate;
10. Hong Kong 3D Tiles/terrain, middle-mouse selection, altitude stability, clean reset, satellite
   tracks/range, current-view photography, and independent images remain correct;
11. panel Quit, window close, and `Cmd+Q` close normally without a macOS warning.

- [ ] **Step 7: Stop at release boundary**

Record manual results in the verification document. A defect returns to its focused TDD task and
repeats Tasks 11-12. Manual acceptance permits a separate version/tag/sync decision only after an
explicit user request.
