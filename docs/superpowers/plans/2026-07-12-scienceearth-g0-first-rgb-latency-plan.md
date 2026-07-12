# ScienceEarth G0 First-RGB Latency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce uncached AlphaEarth RGB median latency below 3 seconds for both NVIDIA HQ and Hong Kong without relaxing correctness, byte-range, memory, camera, or isolation gates.

**Architecture:** Preserve HEAD-based size discovery and strict single-range HTTP semantics, but batch A01/A16/A09 through dataset-level RasterIO so GDAL can schedule block ranges in parallel over HTTP/2. Increase the bounded `/vsicurl/` minimum chunk to 128 KiB so the two observed metadata reads collapse into one, add phase/request evidence, and turn latency limits into executable test failures.

**Tech Stack:** C++17, GDAL 3.13.1 GTiff/VRT/MEM, `/vsicurl/`, HTTP/2 multiplexing, local Python range fixture, CTest, JSON evidence.

## Global Constraints

- Formal fixtures remain NVIDIA HQ `fid=9790` and Hong Kong `fid=181593`, year 2025.
- RGB remains A01/A16/A09 with exact `sign(v) * pow(abs(v) / 127.5, 2)` dequantization.
- Source orientation remains bottom-up and output normalization remains top-down.
- The exact 4x source overview and 256x256 source window to 64x64 output remain mandatory.
- Every GET carries one Range header; comma multi-range and any GET 200 are rejected.
- A response must never equal the complete 2.35-3.70 GB source COG.
- Per-iteration conservative transfer remains `<= 16,777,216` bytes.
- Uncached median is `<= 3000 ms`, P95 is `<= 8000 ms`; five iterations per fixture are required.
- `CPL_VSIL_CURL_USE_HEAD=YES` remains enabled; saving one RTT is not allowed by issuing an unbounded GET.
- The camera, app UI, science cache, existing terrain cache, and product runtime are not modified in G0.
- This plan begins only after the delta-isolation plan reports Tier A and Tier B `PASS`.

---

## File Responsibility Map

- `tests/science_gdal_network_test.cpp`: live/offline RGB reader, phase timings, profiles, hard latency gate, evidence summaries.
- `tests/science_http_range_server.py`: unchanged strict single-range server; only existing logs are consumed.
- `tests/data/science/alphaearth_rgb_cases.json`: immutable fixture identity and georeference inputs; no coordinate changes.
- `tests/CMakeLists.txt`: formal optimized live-test command and timeout.
- `docs/scienceearth/g0-measurements.md`: before/after phases, request ranges, bytes, median, P95.
- `docs/scienceearth/g0-g1-baseline.md`: final gate table and sign-off readiness.

### Task 1: Make latency phases and thresholds deterministic

**Files:**
- Modify: `tests/science_gdal_network_test.cpp`

**Interfaces:**
- Produces: `PhaseTimings { openMs, georeferenceMs, readMs, closeMs, totalMs }`.
- Produces: `LatencySummary summarizeLatency(std::vector<double>)`.
- Produces: `bool passesLatencyGate(const LatencySummary&)`.
- Consumed by: Tasks 2-5.

- [ ] **Step 1: Add a failing in-binary latency-gate regression**

Add this call near the start of `runMain()`:

```cpp
verifyLatencyGateRegression();
```

Add the regression before `runLive()`:

```cpp
void verifyLatencyGateRegression()
{
    const LatencySummary exact = summarizeLatency({1000, 2000, 3000, 7000, 8000});
    require(exact.medianMs == 3000.0 && exact.p95Ms == 8000.0,
            "latency percentile boundary changed");
    require(passesLatencyGate(exact), "exact latency limits must pass");
    require(!passesLatencyGate(summarizeLatency({1000, 2000, 3000.01, 7000, 8000})),
            "median above 3 seconds was accepted");
    require(!passesLatencyGate(summarizeLatency({1000, 2000, 2500, 7000, 8000.01})),
            "P95 above 8 seconds was accepted");
}
```

- [ ] **Step 2: Build and verify RED**

```bash
cmake --build build/science_g0 --target osgVerse_Test_ScienceHttpRanges --parallel 8
```

Expected: compile fails because `LatencySummary`, `summarizeLatency`, and `passesLatencyGate` do not exist.

- [ ] **Step 3: Implement exact summary and phase structures**

Add:

```cpp
constexpr double MAX_MEDIAN_MS = 3000.0;
constexpr double MAX_P95_MS = 8000.0;

struct PhaseTimings
{
    double openMs = 0.0;
    double georeferenceMs = 0.0;
    double readMs = 0.0;
    double closeMs = 0.0;
    double totalMs = 0.0;
};

struct LatencySummary
{
    double medianMs = 0.0;
    double p95Ms = 0.0;
};

LatencySummary summarizeLatency(const std::vector<double>& values)
{
    return {percentile(values, 0.5), percentile(values, 0.95)};
}

bool passesLatencyGate(const LatencySummary& summary)
{
    return summary.medianMs <= MAX_MEDIAN_MS && summary.p95Ms <= MAX_P95_MS;
}
```

Extend `LiveMeasurement` with `PhaseTimings phases`. In `runLiveIteration`, take steady-clock marks immediately after `GDALOpenEx`, `deriveGeoreferencedWindow`, RGB read, and `raw.reset()`. Convert adjacent marks to milliseconds and require:

```cpp
require(std::abs(measurement.phases.totalMs -
        (measurement.phases.openMs + measurement.phases.georeferenceMs +
         measurement.phases.readMs + measurement.phases.closeMs)) < 0.5,
        "latency phases do not sum to total");
```

Transport-log parsing and evidence-file writes remain outside `totalMs` exactly as before.

- [ ] **Step 4: Emit phase values without changing the current gate result**

Add `open_ms`, `georeference_ms`, `read_ms`, `close_ms`, and `total_ms` to each iteration line. Add summed/median phase fields to each case summary. Do not enforce the 3-second gate yet; this task is instrumentation only.

- [ ] **Step 5: Run the local offline suite**

```bash
cmake --build build/science_g0 --target osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0 \
  -R 'ScienceHttpRanges|ScienceHttpRangeServer' --output-on-failure
```

Expected: both offline tests pass and the local run reports all phase keys.

- [ ] **Step 6: Commit phase instrumentation**

```bash
git add tests/science_gdal_network_test.cpp
git commit -m "test(scienceearth): instrument first RGB latency phases"
```

### Task 2: Batch the three RGB bands while preserving the exact overview oracle

**Files:**
- Modify: `tests/science_gdal_network_test.cpp`

**Interfaces:**
- Produces: `NormalizedRgbWindow { rgb, mask }`, both pixel-interleaved and top-down.
- Renames current helper to: `readNormalizedOverviewRgbOracle(...) -> NormalizedRgbWindow`.
- Produces: `readNormalizedOverviewRgbBatched(...) -> NormalizedRgbWindow` used by optimized live reads.

- [ ] **Step 1: Write a failing batched-vs-oracle regression**

After creating the local ZSTD fixture but before starting the HTTP server, open it by file path, derive an aligned window, and add:

```cpp
const int bandMap[] = {2, 17, 10};
const auto oracle = readNormalizedOverviewRgbOracle(dataset.get(), window, bandMap);
const auto batched = readNormalizedOverviewRgbBatched(dataset.get(), window, bandMap);
require(batched.rgb == oracle.rgb && batched.mask == oracle.mask,
        "batched RGB differs from the exact per-band overview oracle");
```

The fixture must include a valid zero and a NoData pixel; assert both masks remain distinguishable after batching.

- [ ] **Step 2: Build and verify RED**

Expected: compile fails because `readNormalizedOverviewRgbBatched` is undefined.

- [ ] **Step 3: Preserve the old reader as the test oracle**

Add this value type, then rename the current `readNormalizedOverviewRgb` implementation to
`readNormalizedOverviewRgbOracle` and return both of its existing normalized arrays:

```cpp
struct NormalizedRgbWindow
{
    std::vector<std::int8_t> rgb;
    std::vector<unsigned char> mask;
};
```

The oracle remains test-only and is not called by optimized live measurements.

- [ ] **Step 4: Implement the dataset-level batched reader**

Use one dataset `RasterIO` call for A01/A16/A09:

```cpp
NormalizedRgbWindow readNormalizedOverviewRgbBatched(
    GDALDataset* raw, const PixelWindow& window, const int* bandMap)
{
    require(raw != nullptr, "batched RGB dataset is null");
    const int rawWindowY = raw->GetRasterYSize() - window.topDownY - window.size;
    const int overviewSize = window.size / LIVE_OVERVIEW_FACTOR;
    for (int channel = 0; channel < 3; ++channel)
        exactOverview(raw->GetRasterBand(bandMap[channel]), LIVE_OVERVIEW_FACTOR);

    std::vector<std::int8_t> rawRgb(overviewSize * overviewSize * 3);
    GDALRasterIOExtraArg extra;
    INIT_RASTERIO_EXTRA_ARG(extra);
    extra.eResampleAlg = GRIORA_NearestNeighbour;
    require(raw->RasterIO(
                GF_Read, window.x, rawWindowY, window.size, window.size,
                rawRgb.data(), overviewSize, overviewSize, GDT_Int8,
                3, const_cast<int*>(bandMap), 3, overviewSize * 3, 1,
                &extra) == CE_None,
            "batched exact-overview RGB read failed");

    std::vector<unsigned char> masks(overviewSize * overviewSize * 3, 255);
    for (int channel = 0; channel < 3; ++channel)
    {
        GDALRasterBand* overview = exactOverview(
            raw->GetRasterBand(bandMap[channel]), LIVE_OVERVIEW_FACTOR);
        const int flags = overview->GetMaskFlags();
        if ((flags & GMF_ALL_VALID) != 0) continue;
        std::vector<unsigned char> channelMask(overviewSize * overviewSize);
        require(overview->GetMaskBand()->RasterIO(
                    GF_Read, window.x / LIVE_OVERVIEW_FACTOR,
                    rawWindowY / LIVE_OVERVIEW_FACTOR,
                    overviewSize, overviewSize, channelMask.data(),
                    overviewSize, overviewSize, GDT_Byte, 0, 0, nullptr) == CE_None,
                "batched RGB mask read failed");
        for (std::size_t pixel = 0; pixel < channelMask.size(); ++pixel)
            masks[pixel * 3 + channel] = channelMask[pixel];
    }

    std::vector<std::int8_t> topDown(rawRgb.size());
    std::vector<unsigned char> topDownMasks(masks.size());
    for (int topY = 0; topY < overviewSize; ++topY)
    {
        const int sourceY = overviewSize - 1 - topY;
        for (int x = 0; x < overviewSize; ++x)
            for (int channel = 0; channel < 3; ++channel)
            {
                const std::size_t source =
                    (sourceY * overviewSize + x) * 3 + channel;
                const std::size_t destination =
                    (topY * overviewSize + x) * 3 + channel;
                topDown[destination] = rawRgb[source];
                topDownMasks[destination] = masks[source];
                require(masks[source] == 0 ||
                        std::isfinite(checkedDequantize(rawRgb[source])),
                        "batched valid sample dequantized non-finite");
            }
    }
    require(std::any_of(topDown.begin(), topDown.end(),
                        [](std::int8_t value) { return value != 0; }),
            "batched RGB is empty");
    return {std::move(topDown), std::move(topDownMasks)};
}
```

If the exact function name in GDAL 3.13.1 is `GRIORA_NearestNeighbour`, use it verbatim; do not fall back to an unspecified resampler.

- [ ] **Step 5: Route optimized live iterations through the batched reader**

Replace only the live call with `readNormalizedOverviewRgbBatched`. Assert that at least one
`rgb` value is nonzero, preserve `mask` for the NoData proof, and keep the oracle call in the
deterministic local regression.

- [ ] **Step 6: Run correctness, range, and georeference regressions**

```bash
cmake --build build/science_g0 --target \
  osgVerse_Test_ScienceGdalSpike osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0 \
  -R 'ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer' \
  --output-on-failure
```

Expected: 3/3 pass; exact oracle equals batched output; range evidence remains bounded and no GET 200 appears.

- [ ] **Step 7: Commit the batched reader**

```bash
git add tests/science_gdal_network_test.cpp
git commit -m "perf(scienceearth): batch AlphaEarth RGB range reads"
```

### Task 3: Collapse the two metadata reads into a bounded 128 KiB chunk

**Files:**
- Modify: `tests/science_gdal_network_test.cpp`

**Interfaces:**
- Produces: `RangeProfile::{Baseline, Optimized}`.
- Produces: `rangeAccessConfig(RangeProfile) -> vector<pair<string,string>>`.
- Preserves: HEAD request, retry policy, allowed extensions, and transfer budgets.

- [ ] **Step 1: Add failing profile-config regression**

Add:

```cpp
void verifyRangeProfiles()
{
    ScopedGdalConfig baseline(rangeAccessConfig(RangeProfile::Baseline));
    require(std::string(CPLGetConfigOption("CPL_VSIL_CURL_CHUNK_SIZE", "")) ==
                "16384", "baseline chunk changed");
    {
        ScopedGdalConfig optimized(rangeAccessConfig(RangeProfile::Optimized));
        require(std::string(CPLGetConfigOption("CPL_VSIL_CURL_CHUNK_SIZE", "")) ==
                    "131072", "optimized chunk must be 128 KiB");
        require(std::string(CPLGetConfigOption("GDAL_HTTP_MULTIRANGE", "")) ==
                    "PARALLEL", "optimized multirange must be parallel");
        require(std::string(CPLGetConfigOption("GDAL_HTTP_MULTIPLEX", "")) ==
                    "YES", "HTTP/2 multiplexing must remain enabled");
        require(std::string(CPLGetConfigOption("CPL_VSIL_CURL_USE_HEAD", "")) ==
                    "YES", "optimized profile must retain HEAD");
    }
}
```

- [ ] **Step 2: Build and verify RED**

Expected: compile fails because `RangeProfile` and the profile overload do not exist.

- [ ] **Step 3: Implement explicit baseline and optimized profiles**

```cpp
enum class RangeProfile { Baseline, Optimized };

std::vector<std::pair<std::string, std::string>> rangeAccessConfig(
    RangeProfile profile)
{
    return {
        {"GDAL_HTTP_MULTIRANGE", profile == RangeProfile::Optimized ?
            "PARALLEL" : "YES"},
        {"GDAL_HTTP_MULTIPLEX", "YES"},
        {"GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES"},
        {"CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".tif,.tiff,.vrt"},
        {"GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR"},
        {"CPL_VSIL_CURL_USE_HEAD", "YES"},
        {"CPL_VSIL_CURL_CHUNK_SIZE",
            profile == RangeProfile::Optimized ? "131072" : "16384"},
        {"CPL_VSIL_CURL_CACHE_SIZE", "16777216"},
        {"GDAL_HTTP_MAX_RETRY", "3"},
        {"GDAL_HTTP_RETRY_DELAY", "0.1"},
        {"GDAL_HTTP_RETRY_CODES", "429,500,502,503,504"},
        {"CPL_VSIL_NETWORK_STATS_ENABLED", "YES"},
        {"CPL_CURL_VERBOSE", "YES"},
        {"CPL_CURL_VERBOSE_DATA_IN", "NO"},
        {"CPL_DEBUG", "ON"},
    };
}
```

Do not add `CPL_VSIL_CURL_USE_HEAD=NO`, `GDAL_INGESTED_BYTES_AT_OPEN`, an unbounded prefetch, or a comma-separated Range request.

- [ ] **Step 4: Add metadata-range evidence assertions**

Extend `HttpProof` with sorted successful byte intervals. For the optimized local run assert:

- the first data interval starts at byte `0` and spans exactly `131072` bytes;
- no later interval is wholly contained in `[0, 131071]`;
- every interval is backed by one HTTP 206;
- total conservative bytes remain within the existing budget.

Keep the baseline profile available only for A/B evidence; optimized becomes the default local/live path.

- [ ] **Step 5: Run the local transport tests**

```bash
cmake --build build/science_g0 --target osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0 \
  -R 'ScienceHttpRanges|ScienceHttpRangeServer' --output-on-failure
```

Expected: both pass; local optimized evidence begins with `bytes=0-131071`; no complete object or GET 200 is accepted.

- [ ] **Step 6: Commit the bounded chunk profile**

```bash
git add tests/science_gdal_network_test.cpp
git commit -m "perf(scienceearth): coalesce COG metadata ranges"
```

### Task 4: Turn live latency into an executable formal gate

**Files:**
- Modify: `tests/science_gdal_network_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- CLI: `--live-cases FILE --iterations 5 --profile baseline|optimized --summary-json FILE [--enforce-latency]`.
- Produces: JSON case summaries with timings, phases, HTTP counts, bytes, and gate outcome.

- [ ] **Step 1: Add failing CLI/gate parsing regressions**

Extend `runMain()`'s internal argument regression so an unknown profile, `--enforce-latency` with fewer than five iterations, or missing summary path is rejected. Add a pure JSON serialization regression requiring:

```json
{
  "profile": "optimized",
  "limits": {"median_ms": 3000.0, "p95_ms": 8000.0},
  "cases": [],
  "status": "PASS"
}
```

- [ ] **Step 2: Build and verify RED**

Expected: argument/serialization regression fails because the new CLI is not implemented.

- [ ] **Step 3: Implement profile-aware live execution and JSON summaries**

Thread `RangeProfile` through `runLive`, `runLiveIteration`, and the reader choice:

```cpp
const NormalizedRgbWindow rgb = profile == RangeProfile::Optimized
    ? readNormalizedOverviewRgbBatched(raw.get(), window, bandMap)
    : readNormalizedOverviewRgbOracle(raw.get(), window, bandMap);
```

For every case, compute `LatencySummary`, set `caseStatus` from `passesLatencyGate`, and write one atomic JSON summary after all cases. The command returns nonzero when `--enforce-latency` is set and any case fails. It still writes raw curl/CPL, GDAL network-stat, parsed proof, and summary evidence before returning.

- [ ] **Step 4: Register only the optimized enforced profile in CTest**

Change `osgVerse_Test_ScienceGdalLive` to:

```cmake
ADD_TEST(NAME osgVerse_Test_ScienceGdalLive
    COMMAND $<TARGET_FILE:osgVerse_Test_ScienceHttpRanges>
            --live-cases
            "${CMAKE_CURRENT_SOURCE_DIR}/data/science/alphaearth_rgb_cases.json"
            --iterations 5
            --profile optimized
            --summary-json
            "${CMAKE_BINARY_DIR}/science-network-evidence/live-summary.json"
            --enforce-latency)
```

Keep label `network;scienceearth` and timeout `300`.

- [ ] **Step 5: Run offline CLI regressions**

```bash
cmake --build build/science_g0 --target osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0 \
  -R 'ScienceHttpRanges|ScienceHttpRangeServer' --output-on-failure
```

Expected: offline tests pass; malformed CLI cases fail only inside the internal regression as expected.

- [ ] **Step 6: Commit the hard gate**

```bash
git add tests/science_gdal_network_test.cpp tests/CMakeLists.txt
git commit -m "test(scienceearth): enforce first RGB latency gate"
```

### Task 5: Run formal A/B evidence and close G0

**Files:**
- Modify: `docs/scienceearth/g0-measurements.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`

**Interfaces:**
- Consumes: optimized binary plus completed delta-isolation evidence.
- Produces: final G0 evidence and human-sign-off checkpoint.

- [ ] **Step 1: Run one baseline profile for request/phase comparison**

```bash
build/science_g0/bin/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile baseline \
  --summary-json build/science_g0/science-network-evidence/baseline-summary.json
```

Expected: evidence is written even if the baseline profile exceeds 3 seconds. It must still pass correctness, response-code, retry, and transfer-budget assertions.

- [ ] **Step 2: Run the optimized enforced formal profile**

```bash
build/science_g0/bin/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile optimized \
  --summary-json build/science_g0/science-network-evidence/live-summary.json \
  --enforce-latency
```

Expected: exit `0`; both case medians `<= 3000 ms`; both P95 values `<= 8000 ms`; no GET 200, comma range, complete object, transient-policy, georeference, orientation, band, dequantization, or byte-budget violation.

If either optimized median still exceeds 3000 ms, stop this plan with `G0_DECISION=STOP`. Preserve the phase/range evidence and report which phase dominates; do not add unbounded prefetch, reuse warm cache, drop iterations, change fixtures, or relax the threshold in the same plan.

- [ ] **Step 3: Run the full G0 automated regression set**

```bash
ctest --test-dir build/science_g0 \
  -R 'ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer|ScienceGdalLive' \
  --output-on-failure
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_bundle_audit_tests tests.science_g0_manifest_tests -v
bash tests/science_deps_script_tests.sh
bash tests/scienceearth_release_tests.sh
packaging/science_deps/build_science_deps.sh --verify
python3 packaging/audit_macos_bundle.py \
  --app 'build/science_g0/osgSol Science G0 Probe.app' \
  --baseline '/Users/USER/Desktop/osgSol Earth.app' \
  --reference-manifest packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json \
  --ratchet-manifest packaging/scienceearth/baselines/current-macos-arm64-ratchet.json \
  --source-root /Users/USER/osgverse \
  --source-root /Users/USER/osgsol/.worktrees/v0.2-runtime-safety \
  --json build/science_g0/bundle-audit.json \
  --text build/science_g0/bundle-audit.txt
```

Expected: every command passes; isolation and latency reports remain separate.

- [ ] **Step 4: Re-run the protected science-off regression selection**

Run the exact protected target/CTest selection recorded in `docs/scienceearth/g0-g1-baseline.md`. Expected: all protected tests pass with unchanged expectations and no science target in the science-off build.

- [ ] **Step 5: Record measured before/after evidence**

Update `g0-measurements.md` with per-case iteration timings, phase medians, exact successful ranges, GET/HEAD counts, successful/actual/conservative bytes, source size, median, P95, profile configuration, and the two summary JSON hashes. Never copy timings from console without matching evidence files.

Update `g0-g1-baseline.md` so every automated row reflects the final run. Set:

```text
G0_DECISION=READY_FOR_HUMAN_SIGN_OFF
REASON=All automated isolation, correctness, bounded-range, size, latency, and protected science-off gates pass.
HUMAN_PRODUCT_SIGN_OFF=PENDING
```

- [ ] **Step 6: Commit evidence readiness**

```bash
git add tests/science_gdal_network_test.cpp tests/CMakeLists.txt \
  docs/scienceearth/g0-measurements.md docs/scienceearth/g0-g1-baseline.md
git commit -m "perf(scienceearth): meet G0 first RGB latency gate"
```

- [ ] **Step 7: Human sign-off and final G0 decision**

Present the isolation report, live summary, science-off total, candidate size, and exact commit to the user. Only after explicit approval, update the baseline record to:

```text
G0_DECISION=GO
HUMAN_PRODUCT_SIGN_OFF=APPROVED
```

Commit only that evidence change:

```bash
git add docs/scienceearth/g0-g1-baseline.md
git commit -m "docs(scienceearth): approve G0 dependency gate"
```

## G0 Completion Gate

Tasks 7-16 of the original G0/G1 vertical-slice plan remain blocked until both implementation
plans pass and the user explicitly approves the final evidence. A failed optimized live run is a
measured `STOP`, not permission to change the 3-second limit or disguise the result with a warm
cache.
