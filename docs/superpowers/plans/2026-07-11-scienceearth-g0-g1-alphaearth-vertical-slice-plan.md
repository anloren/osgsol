# ScienceEarth G0/G1 AlphaEarth Vertical Slice Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this
> plan task-by-task. Also use `superpowers:test-driven-development`,
> `superpowers:systematic-debugging` for any failure, and
> `superpowers:verification-before-completion` before reporting a gate or release candidate.

**Goal:** Prove a trimmed, isolated GDAL runtime and ship one end-to-end AlphaEarth RGB
scientific source that is discoverable and queryable by the Agent without changing any protected
`v0.2.0` terrain, camera, 3D Tiles, photo, overlay, or reset behavior.

**Architecture:** Add a small `osgSolScienceCore` shared library before the plugin build. The main
application and an optional `osgdb_science` plugin share only stable science contracts, registry,
jobs, evidence, and cache APIs. GDAL/PROJ/ZSTD are statically linked with hidden symbols into the
plugin; the application never links GDAL. AlphaEarth uses a release-built compact SQLite/RTree
index and byte-range COG access. All network/decode/analysis work runs as cancellable jobs; Agent
tools only enqueue, poll, and explicitly materialize artifacts. The science-off build remains the
baseline-equivalent default until G0 is accepted.

**Tech Stack:** C++17, CMake/CTest, OpenSceneGraph 3.6.5, GDAL 3.13.1, PROJ 9.8.1,
ZSTD 1.5.7, SQLite/RTree, libcurl, Python 3 stdlib for release tooling, pinned dev-only DuckDB CLI
for Parquet extraction, macOS `otool`/`codesign` for bundle verification.

---

## Scope and hard stop

This plan covers only:

- G0: trimmed GDAL/PROJ/ZSTD dependency build, isolation proof, COG range proof, bundle-cost proof;
- G1: AlphaEarth 2017–2025 global index, RGB visualization, point/bbox query, evidence, cache;
- the minimum Agent vertical slice: discover, start, poll, and show an AlphaEarth artifact.

It deliberately defers Sentinel, DEM ingestion, GRIB2/NetCDF/HDF5 provider packs, weather
re-driving, change detection, cross-source comparison, global embedding similarity, and the full
research-brief workflow.

**G0 is a hard go/no-go.** Do not implement Tasks 7–16 unless Task 6 records `GO`. A failed gate
is not permission to relax the limits. Report the failure and stop for a product decision.

## Fixed acceptance limits

| Gate | Pass condition |
|---|---|
| Existing behavior | Science-off targeted regression suite passes unchanged |
| Link isolation | Main executable and non-science libraries have no GDAL/PROJ/ZSTD references |
| System isolation | Bundle has no `/opt/homebrew`, `/usr/local`, build-tree, or source-tree runtime references |
| Added bundle size | Target `<= 40 MiB`; `40–60 MiB` requires explicit review; `> 60 MiB` is STOP |
| AlphaEarth correctness | NVIDIA HQ and Hong Kong fixture queries select expected tile/year, orientation, RGB bands, and dequantization |
| HTTP behavior | Requests are bounded byte ranges; no response equals the complete 270 MB source COG |
| Network latency | Uncached first RGB: median `<= 3 s`, P95 `<= 8 s`; rendered-cache hit `<= 100 ms` on the reference setup |
| Active memory | AlphaEarth RGB activity adds `<= 300 MB` on the reference test |
| Camera authority | Search/start/poll/show do not invoke `fly_to` or mutate the manipulator |
| Cache | Separate bounded science cache with deterministic eviction and no use of the existing terrain cache |
| Missing plugin/offline | App starts normally; science reports unavailable/partial without affecting existing layers |

## Task 1: Create the execution branch and freeze the baseline contract

**Files:**

- Create: `docs/scienceearth/g0-g1-baseline.md`
- Create: `tests/scienceearth_release_tests.sh`
- Modify: `tests/CMakeLists.txt`

**Step 1: Create the execution branch from the approved design branch**

Run:

```bash
git status --short
git rev-parse HEAD
test "$(git branch --show-current)" = "codex/science-earth-design"
git switch -c codex/science-earth-g0-g1
```

Expected: clean tree before the switch; new branch contains the approved design commit
`c2161f3d6b43318e37504f1cc22dfefc96adcd36` and the committed execution plan. Work in the existing
linked worktree; do not create a nested worktree and do not touch the original repo.

**Step 2: Write the failing release/tag guard**

`tests/scienceearth_release_tests.sh` must assert:

```bash
test "$(git rev-list -n 1 v0.2.0)" = "$(git rev-list -n 1 ScienceEarth)"
test "$(git rev-parse 'ScienceEarth^{}')" = \
     "0e91c7c4b121d80b929d595ea711d3dd0833ee67"
```

It must also reject a supplied release pair when `vX.Y.Z` and `ScienceEarth-vX.Y.Z` dereference to
different commits. Do not create either release tag in this task.

**Step 3: Run the guard and confirm the initial failure**

Run:

```bash
bash tests/scienceearth_release_tests.sh
```

Expected: failure because the baseline evidence document and release-pair validation entry point
do not exist yet.

**Step 4: Add the smallest implementation**

Document in `docs/scienceearth/g0-g1-baseline.md`:

- baseline commit and immutable `ScienceEarth` boundary tag;
- protected behavior list from the approved spec;
- G0 limits from this plan;
- rule that only a user-approved release gets `vX.Y.Z` plus `ScienceEarth-vX.Y.Z`;
- rule that plain `ScienceEarth` is never moved.

Register the shell guard as CTest `osgSol_Test_ScienceEarthRelease` when `BUILD_TESTING=ON`.

**Step 5: Verify and commit**

Run:

```bash
bash tests/scienceearth_release_tests.sh
git diff --check
git add docs/scienceearth/g0-g1-baseline.md tests/scienceearth_release_tests.sh tests/CMakeLists.txt
git commit -m "test(scienceearth): freeze release boundary contract"
```

Expected: guard passes; commit contains no tags and no build artifacts.

## Task 2: Establish a fresh science test build and science-off baseline

**Files:**

- Modify: `CMakeLists.txt`
- Create: `cmake/ScienceEarthOptions.cmake`
- Create: `tests/science_build_contract_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `docs/scienceearth/g0-g1-baseline.md`

**Step 1: Write a failing build-contract test**

The test reads the generated build-contract header and checks:

```cpp
CHECK(OSGSOL_BUILD_SCIENCE_VALUE == 0);
CHECK(std::string(OSGSOL_SCIENCE_PHASE_VALUE) == "G0-G1");
```

It also checks that the target list does not contain `osgSolScienceCore` or `osgdb_science` when
science is off.

**Step 2: Configure a fresh build and observe failure**

Run:

```bash
cmake -S . -B build/science_test \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DVERSE_BUILD_EXAMPLES=ON \
  -DOSGSOL_BUILD_SCIENCE=OFF
cmake --build build/science_test --target osgVerse_Test_ScienceBuildContract -j4
```

Expected: configure or target build fails because the option/header/test target is absent.

**Step 3: Add the option without changing the science-off graph**

`cmake/ScienceEarthOptions.cmake` defines:

```cmake
option(OSGSOL_BUILD_SCIENCE "Build optional ScienceEarth providers" OFF)
set(OSGSOL_SCIENCE_DEPS_ROOT "" CACHE PATH "Trimmed ScienceEarth dependency prefix")
option(OSGSOL_SCIENCE_NETWORK_TESTS "Enable live scientific-source tests" OFF)
```

Include it near other root options. Generate a small contract header for tests. Do not add the
`science/` or plugin directories yet.

**Step 4: Capture the protected baseline**

Build and run at minimum:

```bash
cmake --build build/science_test --target \
  osgVerse_Test_ScienceBuildContract \
  osgVerse_Test_Ai_Chat osgVerse_Test_Feeds osgVerse_Test_TileOverlay \
  osgVerse_Test_TerrainGrid osgVerse_Test_Tiles3dPaging \
  osgVerse_Test_Satellite osgVerse_Test_Geospatial \
  osgVerse_Test_EarthManipulator osgVerse_Test_Ais \
  osgVerse_Test_WorldTools osgVerse_Test_ImGuiSettings \
  osgVerse_Test_McpSafety osgVerse_Test_ImGuiThreading \
  osgVerse_Test_MediaThreading -j4
ctest --test-dir build/science_test --output-on-failure \
  -R 'ScienceBuildContract|Ai_Chat|Feeds|TileOverlay|TerrainGrid|Tiles3dPaging|Satellite|Geospatial|EarthManipulator|Ais|WorldTools|ImGuiSettings|McpSafety|ImGuiThreading|MediaThreading'
```

Expected: all selected tests pass. Record test names, count, duration, compiler, architecture, and
commit in the baseline document. If a pre-existing baseline test fails, diagnose it before making
science changes.

**Step 5: Verify and commit**

```bash
git diff --check
git add CMakeLists.txt cmake/ScienceEarthOptions.cmake tests/CMakeLists.txt \
  tests/science_build_contract_tests.cpp docs/scienceearth/g0-g1-baseline.md
git commit -m "build(scienceearth): add disabled science build contract"
```

## Task 3: Pin and build the private GDAL dependency prefix

**Files:**

- Create: `packaging/science_deps/versions.env`
- Create: `packaging/science_deps/checksums.txt`
- Create: `packaging/science_deps/build_science_deps.sh`
- Create: `tests/science_deps_script_tests.sh`
- Modify: `tests/CMakeLists.txt`
- Create: `docs/scienceearth/gdal-build.md`

**Step 1: Write failing script tests**

Test that the builder:

- pins GDAL `3.13.1`, PROJ `9.8.1`, and ZSTD `1.5.7` with SHA-256 values;
- defaults to `build/science-deps/{downloads,src,build,prefix}`;
- refuses an install prefix under `/opt/homebrew` or `/usr/local`;
- never calls `brew install` or `sudo`;
- enables only GTiff, VRT, MEM, `/vsicurl/`, curl, SQLite, PROJ, and ZSTD capabilities;
- disables apps, Python/SWIG bindings, tests, Arrow/Parquet runtime support, and unrelated drivers;
- emits `science-deps-manifest.json` from the resolved CMake caches.

Run and confirm failure:

```bash
bash tests/science_deps_script_tests.sh
```

**Step 2: Implement download/checksum and local-prefix behavior**

The script must support:

```bash
packaging/science_deps/build_science_deps.sh --download-only
packaging/science_deps/build_science_deps.sh --build --jobs 4
packaging/science_deps/build_science_deps.sh --verify
```

Use source archives only. The verified source-download budget is approximately 24.12 MB decimal:
GDAL 15,709,136 bytes, PROJ 5,981,846 bytes, ZSTD 2,434,947 bytes.

Configure static dependencies with position-independent code, hidden visibility, release mode,
embedded GDAL/PROJ data where supported, no remote PROJ grids, and a deployment target compatible
with the app. Use system macOS curl and SQLite only as explicitly documented platform dependencies.
Derive the exact accepted CMake keys from the pinned source trees and record them from
`CMakeCache.txt`; do not silently ignore unknown `-D` options.

**Step 3: Build and inspect**

Run:

```bash
bash packaging/science_deps/build_science_deps.sh --download-only
bash packaging/science_deps/build_science_deps.sh --build --jobs 4
bash packaging/science_deps/build_science_deps.sh --verify
du -sh build/science-deps/prefix
find build/science-deps/prefix -type f | sort
```

Expected: checksums pass, private prefix is self-contained, and the manifest lists the compiled
drivers/features. No Homebrew GDAL tree is installed or copied.

**Step 4: Document reproducibility and commit**

`docs/scienceearth/gdal-build.md` records URLs, checksums, exact configure commands, resulting
features, prefix size, build time, and rebuild instructions.

```bash
bash tests/science_deps_script_tests.sh
git diff --check
git add packaging/science_deps tests/science_deps_script_tests.sh tests/CMakeLists.txt \
  docs/scienceearth/gdal-build.md
git commit -m "build(scienceearth): pin trimmed GDAL dependency stack"
```

Do not commit `build/science-deps`, archives, source trees, or compiled libraries.

## Task 4: Build the release-time AlphaEarth compact index tool

**Files:**

- Create: `tools/science/duckdb-manifest.json`
- Create: `tools/science/fetch_duckdb.sh`
- Create: `tools/science/extract_aef_index.sh`
- Create: `tools/science/build_aef_index.py`
- Create: `tools/science/aef_index_schema.sql`
- Create: `tests/data/science/aef_index_fixture.csv`
- Create: `tests/data/science/aef_index_expected.json`
- Create: `tests/aef_index_tool_tests.py`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing fixture tests**

The fixture includes at least:

- NVIDIA headquarters (`37.3707, -121.9631`);
- Hong Kong (`22.3193, 114.1694`);
- overlapping years and a boundary-crossing bbox;
- one malformed row and one duplicate dataset id.

Tests require a compact database with:

```text
tiles(id, dataset_id, year, cog_url, vrt_url, min_lon, min_lat, max_lon, max_lat,
      source_version, source_checksum)
tile_rtree(id, min_lon, max_lon, min_lat, max_lat)
metadata(schema_version, source_index_url, source_index_sha256, generated_at, row_count)
```

Queries must return deterministic year/id order and reject malformed/duplicate records.

Run:

```bash
python3 -m unittest tests/aef_index_tool_tests.py -v
```

Expected: failure because the tooling does not exist.

**Step 2: Implement the fixture path with Python stdlib only**

`build_aef_index.py` accepts CSV on stdin/file and writes SQLite/RTree atomically. It validates
longitude/latitude, years 2017–2025, URL scheme, uniqueness, schema version, source checksum, row
count, and `PRAGMA integrity_check`. It must never require DuckDB at runtime.

**Step 3: Add the pinned Parquet extraction path**

The dev-only DuckDB CLI is checksum-pinned and fetched under `build/science-tools`; it is not
packaged. `extract_aef_index.sh` downloads the 77,829,744-byte Parquet index once, verifies its
checksum, selects only required columns, streams CSV to the Python builder, and writes a manifest.
The app must never download the Parquet, 539 MB GPKG, or 798 MB CSV index at runtime.

**Step 4: Verify fixture and optional full index**

```bash
python3 -m unittest tests/aef_index_tool_tests.py -v
bash tools/science/extract_aef_index.sh --fixture tests/data/science/aef_index_fixture.csv \
  --output build/science-index/alphaearth.sqlite
sqlite3 build/science-index/alphaearth.sqlite 'PRAGMA integrity_check; SELECT COUNT(*) FROM tiles;'
```

Expected: `ok` plus the fixture row count. Run the full Parquet path only when updating the
release resource, then record source checksum, output checksum, row count, schema version, and size.

**Step 5: Commit**

```bash
git diff --check
git add tools/science tests/data/science tests/aef_index_tool_tests.py tests/CMakeLists.txt
git commit -m "tools(scienceearth): build compact AlphaEarth spatial index"
```

## Task 5: Prove GDAL COG reads, orientation, RGB, and bounded HTTP ranges

**Files:**

- Create: `tests/science_gdal_spike.cpp`
- Create: `tests/science_gdal_probe_plugin.cpp`
- Create: `tests/science_http_range_server.py`
- Create: `tests/science_gdal_network_test.cpp`
- Create: `tests/data/science/alphaearth_rgb_cases.json`
- Modify: `tests/CMakeLists.txt`
- Create: `docs/scienceearth/g0-measurements.md`

**Step 1: Write a failing offline spike test**

Generate a temporary 8192-style miniature fixture at test time: 64 signed int8 bands, tiled
GTiff, ZSTD compression, overviews, plus a VRT that vertically flips the source. Do not commit a
large TIFF. Assert:

- GTiff/VRT/MEM drivers are available and unrelated drivers are absent;
- bands `A01`, `A16`, `A09` map to RGB in that order;
- dequantization is `sign(v) * pow(abs(v) / 127.5, 2)`;
- NoData remains a mask, not zero;
- VRT orientation matches the known top-down coordinates;
- bbox reads use an overview/window rather than `RasterIO` over the full raster.

Run the new target and confirm failure before adding its GDAL linkage.

**Step 2: Add an instrumented local range server**

The Python server logs request method, URI, `Range`, response code, bytes sent, and total source
size. The test fails if:

- any request has no Range header for a remote COG window;
- a response is HTTP 200 with the entire file;
- comma-separated multi-range syntax is emitted;
- total transferred bytes exceed the explicit test budget.

Configure GDAL with:

```text
GDAL_HTTP_MULTIRANGE=YES
GDAL_HTTP_MERGE_CONSECUTIVE_RANGES=YES
CPL_VSIL_CURL_ALLOWED_EXTENSIONS=.tif,.tiff,.vrt
```

Never use `GDAL_HTTP_MULTIRANGE=SINGLE_GET`.

Also build a test-only `osgdb_science_g0_probe.so` from
`tests/science_gdal_probe_plugin.cpp`. It statically links exactly the candidate GDAL/PROJ/ZSTD
archives with the same hidden-visibility and dead-strip settings planned for the real plugin, but
exposes no product provider and is never installed. Task 6 uses this probe solely to measure the
true dependency closure before G1 code exists.

**Step 3: Add the optional live source test**

Register it only when `OSGSOL_SCIENCE_NETWORK_TESTS=ON`. It queries the NVIDIA and Hong Kong
cases, records per-request byte counts and timing, refuses a complete-COG response, and checks the
expected tile/year. The test downloads no global index because it uses fixture URLs.

**Step 4: Run and record measurements**

```bash
cmake -S . -B build/science_g0 \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DVERSE_BUILD_EXAMPLES=ON \
  -DOSGSOL_BUILD_SCIENCE=ON \
  -DOSGSOL_SCIENCE_DEPS_ROOT="$PWD/build/science-deps/prefix"
cmake --build build/science_g0 --target \
  osgVerse_Test_ScienceGdalSpike osgVerse_Test_ScienceHttpRanges \
  osgdb_science_g0_probe -j4
ctest --test-dir build/science_g0 --output-on-failure -R 'ScienceGdalSpike|ScienceHttpRanges'
```

Then, only with network testing explicitly enabled, run 5 iterations and record median/P95,
downloaded bytes, response codes, source size, machine, network, and date in
`docs/scienceearth/g0-measurements.md`.

**Step 5: Commit**

```bash
git diff --check
git add tests/science_gdal_spike.cpp tests/science_gdal_probe_plugin.cpp \
  tests/science_http_range_server.py \
  tests/science_gdal_network_test.cpp tests/data/science/alphaearth_rgb_cases.json \
  tests/CMakeLists.txt docs/scienceearth/g0-measurements.md
git commit -m "test(scienceearth): prove bounded AlphaEarth COG access"
```

## Task 6: Enforce and decide the G0 go/no-go gate

**Files:**

- Create: `packaging/audit_macos_bundle.py`
- Create: `packaging/build_science_g0_probe.sh`
- Create: `tests/science_bundle_audit_tests.py`
- Modify: `docs/scienceearth/g0-measurements.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`

**Step 1: Write failing bundle-audit tests**

Use synthetic `otool -L` fixtures to prove recursive traversal of the executable, every dylib,
and every plugin. Reject:

- missing `@rpath`/`@loader_path` dependencies;
- `/opt/homebrew`, `/usr/local`, source-tree, and build-tree references;
- a GDAL/PROJ/ZSTD dependency reachable from the main executable except through
  `osgdb_science.so`;
- science additions above the size thresholds.

**Step 2: Implement a reusable recursive audit**

The audit emits JSON plus a readable report containing dependency graph, unresolved references,
science-only closure, total bundle size, baseline bundle size, and delta. It must return non-zero
for every STOP condition.

`build_science_g0_probe.sh` creates a disposable
`build/science_g0/osgSol Science G0 Probe.app` containing the current executable, the science
G0 test-only probe plugin, its private closure, and the same non-science runtime closure as the
verified v0.2.0 app.
This exists only to measure G0 before the canonical packaging script is changed. It must not touch
`/Users/USER/Desktop/osgSol Earth.app` and is never a user-test package.

**Step 3: Build the minimal spike bundle and run all G0 evidence**

```bash
python3 -m unittest tests/science_bundle_audit_tests.py -v
packaging/build_science_g0_probe.sh
python3 packaging/audit_macos_bundle.py \
  --app 'build/science_g0/osgSol Science G0 Probe.app' \
  --baseline /Users/USER/Desktop/osgSol\ Earth.app \
  --json build/science_g0/bundle-audit.json
bash packaging/science_deps/build_science_deps.sh --verify
ctest --test-dir build/science_g0 --output-on-failure \
  -R 'ScienceGdalSpike|ScienceHttpRanges'
```

**Step 4: Record exactly one decision**

Append a signed-off table with each acceptance limit and measured result:

```text
G0_DECISION=GO
```

or:

```text
G0_DECISION=STOP
REASON=<failed hard gate>
```

If the decision is `STOP`, commit the evidence, report the blocker, and end execution. Do not
continue by weakening the threshold or broadening the shipped dependency set.

**Step 5: Commit the gate evidence**

```bash
git diff --check
git add packaging/audit_macos_bundle.py packaging/build_science_g0_probe.sh \
  tests/science_bundle_audit_tests.py \
  docs/scienceearth/g0-measurements.md docs/scienceearth/g0-g1-baseline.md
git commit -m "docs(scienceearth): record G0 dependency gate"
```

## Task 7: Add the GDAL-free ScienceCore contracts and source registry

**Files:**

- Create: `science/CMakeLists.txt`
- Create: `science/Export.h`
- Create: `science/ScienceTypes.h`
- Create: `science/ScienceTypes.cpp`
- Create: `science/ScienceSource.h`
- Create: `science/ScienceSourceRegistry.h`
- Create: `science/ScienceSourceRegistry.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/science_core_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing contract tests**

Cover validated construction and stable serialization for:

- `GeoTemporalQuery` with WGS84 geometry, time, variables, resolution, aggregation, output,
  limits, purpose, priority, and generation id;
- `ScienceSourceDescriptor` with capabilities, coverage, variables/units, version, license,
  attribution, health, latency, and cache lifetime;
- `ScienceArtifact` and `EvidenceItem`, including observation/derived/correlation/inference class;
- registry register/unregister/search and duplicate-source rejection.

Include explicit tests that low-resolution data cannot claim higher native resolution, NoData
needs a mask, and inference/correlation require labels.

**Step 2: Add the shared library before plugins**

In the root graph:

```cmake
if(OSGSOL_BUILD_SCIENCE)
    add_subdirectory(science)
endif()
add_subdirectory(plugins)
```

Build `osgSolScienceCore` as a small shared library with hidden visibility and exported public
types. It must not include or link GDAL, PROJ, ZSTD, SQLite, curl, `LayerManager`, camera, or OSG
scene classes.

**Step 3: Run isolation and unit tests**

```bash
cmake --build build/science_g0 --target osgVerse_Test_ScienceCore -j4
ctest --test-dir build/science_g0 --output-on-failure -R ScienceCore
otool -L build/science_g0/lib/libosgSolScienceCore.dylib
```

Expected: tests pass; `otool` contains no GDAL/PROJ/ZSTD.

**Step 4: Commit**

```bash
git diff --check
git add CMakeLists.txt science tests/science_core_tests.cpp tests/CMakeLists.txt
git commit -m "feat(scienceearth): add scientific source contracts"
```

## Task 8: Add cancellable research jobs and a bounded evidence/cache store

**Files:**

- Create: `science/ResearchJobManager.h`
- Create: `science/ResearchJobManager.cpp`
- Create: `science/ScienceEvidenceStore.h`
- Create: `science/ScienceEvidenceStore.cpp`
- Create: `science/ScienceCache.h`
- Create: `science/ScienceCache.cpp`
- Modify: `science/CMakeLists.txt`
- Create: `tests/science_jobs_tests.cpp`
- Create: `tests/science_cache_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing state-machine tests**

Assert legal transitions:

```text
Created -> Planning -> Queued -> Fetching -> Decoding -> Aligning -> Analyzing -> Ready
```

and `Partial`, `Failed`, `Cancelled`, late-result rejection by generation id, cancellation during
fetch/decode, timeout, byte/memory/cell limits, deterministic status snapshots, and shutdown join.
An enqueue call must return promptly without executing network work on the caller thread.

**Step 2: Implement the minimal scheduler**

Use a bounded worker pool and immutable job input. Provider work receives a cancellation token and
budget. Completion is published as data only; no worker touches OSG, OpenGL, ImGui, camera, or
`LayerManager`.

**Step 3: Write failing cache/evidence tests**

Use a temporary directory and fake clock. Assert:

- cache key includes source/version/query/processing version;
- atomic write plus checksum validation;
- 3 GB default LRU byte limit, configurable only within 1–10 GB, plus item limits;
- expiry, corrupt-entry removal, and deterministic eviction;
- evidence metadata survives cache hits;
- paths remain under a dedicated science cache root;
- clearing the regenerable cache does not remove explicitly saved research packages.

Default macOS root:

```text
~/Library/Caches/osgSol Earth/science/
```

Do not reuse `~/Library/Caches/EarthExplorer` terrain/satellite storage. Add configurable limits
and show current usage in status APIs. Explicitly saved research metadata/artifacts live under
`~/Library/Application Support/osgSol Earth/research/`; session artifacts expire unless saved.

**Step 4: Verify race-sensitive behavior**

```bash
cmake --build build/science_g0 --target \
  osgVerse_Test_ScienceJobs osgVerse_Test_ScienceCache -j4
ctest --test-dir build/science_g0 --output-on-failure -R 'ScienceJobs|ScienceCache'
```

Repeat the cancellation tests under the available sanitizer configuration before commit.
Also assert the approved initial scheduler limits: at most 3 visible raster jobs, 4 API/STAC
network jobs, and 1–2 heavy-analysis jobs; one oversized request cannot bypass limits by splitting
itself into many child jobs.

**Step 5: Commit**

```bash
git diff --check
git add science tests/science_jobs_tests.cpp tests/science_cache_tests.cpp tests/CMakeLists.txt
git commit -m "feat(scienceearth): add cancellable research jobs and evidence cache"
```

## Task 9: Add the isolated science plugin and provider host

**Files:**

- Create: `science/ScienceProviderHost.h`
- Create: `science/ScienceProviderHost.cpp`
- Modify: `science/CMakeLists.txt`
- Create: `plugins/osgdb_science/CMakeLists.txt`
- Create: `plugins/osgdb_science/ReaderWriterScience.cpp`
- Create: `plugins/osgdb_science/GdalRuntime.h`
- Create: `plugins/osgdb_science/GdalRuntime.cpp`
- Modify: `plugins/CMakeLists.txt`
- Create: `tests/science_plugin_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing host/plugin tests**

Tests load the plugin dynamically, check ABI/version handshake, register a fake provider, and
unload safely after jobs stop. Also test plugin missing, wrong ABI, duplicate registration, and
initialization failure. Missing science must produce a structured unavailable status, not app
startup failure. Security tests must prove external GDAL plugin discovery is disabled, only the
compiled driver allowlist is registered, arbitrary user VRTs are rejected, VRT Python and
RawRasterBand paths are unavailable, and network reads reject non-HTTPS or non-allowlisted hosts.

**Step 2: Implement the process-wide host**

`ScienceProviderHost` lives in `osgSolScienceCore`, so the app and plugin share one registry.
`osgdb_science` statically links the trimmed GDAL/PROJ/ZSTD archives and exposes only its plugin
entry symbols. Initialize GDAL configuration once, register only compiled drivers, and cleanly
reject unsafe runtime settings such as `SINGLE_GET`.

**Step 3: Verify the link boundary**

```bash
cmake --build build/science_g0 --target osgVerse_Test_SciencePlugin osgdb_science -j4
ctest --test-dir build/science_g0 --output-on-failure -R SciencePlugin
otool -L build/science_g0/bin/osgVerse_EarthExplorer
otool -L build/science_g0/lib/osgPlugins-3.6.5/osgdb_science.so
nm -gU build/science_g0/lib/osgPlugins-3.6.5/osgdb_science.so | \
  rg 'GDAL|OGR|proj_' || true
```

Expected: main executable has no GDAL/PROJ/ZSTD dependency; plugin has no unexpected exported
GDAL/PROJ symbols and no Homebrew paths.

**Step 4: Commit**

```bash
git diff --check
git add science plugins/osgdb_science plugins/CMakeLists.txt \
  tests/science_plugin_tests.cpp tests/CMakeLists.txt
git commit -m "feat(scienceearth): isolate GDAL in optional provider plugin"
```

## Task 10: Implement the AlphaEarth index and provider query path

**Files:**

- Create: `plugins/osgdb_science/AlphaEarthIndex.h`
- Create: `plugins/osgdb_science/AlphaEarthIndex.cpp`
- Create: `plugins/osgdb_science/AlphaEarthProvider.h`
- Create: `plugins/osgdb_science/AlphaEarthProvider.cpp`
- Create: `plugins/osgdb_science/AlphaEarthRgb.h`
- Create: `plugins/osgdb_science/AlphaEarthRgb.cpp`
- Modify: `plugins/osgdb_science/CMakeLists.txt`
- Modify: `cmake/ScienceEarthOptions.cmake`
- Modify: `science/CMakeLists.txt`
- Create: `tests/alphaearth_provider_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing index/provider tests**

Using only the compact fixture index and local synthetic COG, test:

- source descriptor: years 2017–2025, 10 m embedding source, 64 int8 bands, experimental status,
  license/attribution/source version;
- point and bbox lookup at NVIDIA HQ and Hong Kong;
- exact-year selection, unavailable-year warning, boundary overlap, deterministic ordering;
- UTM-zone edges and antimeridian-wrapping queries without false global bboxes;
- RGB uses `A01/A16/A09`, VRT/top-down orientation, signed dequantization, NoData mask;
- point sample and bbox artifact include original/normalized values, actual coverage, processing,
  source URL/version/checksum, and limitations;
- budget/cancellation/partial result paths;
- cache hit does no network read.

**Step 2: Implement index reads**

Open the SQLite resource read-only and immutable where possible. Validate schema version and
manifest checksum before queries. Use RTree intersection first, then deterministic year/id
selection. Never download or rebuild the global index at runtime.

Add `OSGSOL_ALPHAEARTH_INDEX` as a build-time file option. Unit tests point it at the fixture
database. A formal science package must point it at the verified full database produced by Task 4;
the install step copies that database and its manifest into `misc/science/alphaearth/`. Configure
must fail for a formal/package build if the file, checksum manifest, schema version, or row count
is missing. The database itself remains a generated release resource, not a Git-tracked binary.

Keep the global database compact: store the source base URL once in metadata and reconstruct
validated COG/VRT URLs from compact dataset/path fields instead of duplicating full URLs in every
row. Record both index size and total projected bundle delta; crossing the G0 size decision returns
to the gate rather than silently enlarging the app.

**Step 3: Implement provider reads**

Build `/vsicurl/` paths from validated HTTPS URLs, select a window/overview, and read only the
three RGB bands for visualization. Keep the 64-band scientific descriptor even though G1 exposes
RGB plus bounded sampling. Capture GDAL version/config and every transform in evidence.

**Step 4: Run tests including instrumented ranges**

```bash
cmake --build build/science_g0 --target osgVerse_Test_AlphaEarthProvider -j4
ctest --test-dir build/science_g0 --output-on-failure \
  -R 'AlphaEarthProvider|ScienceHttpRanges'
```

Expected: offline tests pass; logged HTTP bytes stay within budget; no full-file response.

**Step 5: Commit**

```bash
git diff --check
git add plugins/osgdb_science tests/alphaearth_provider_tests.cpp tests/CMakeLists.txt
git commit -m "feat(scienceearth): query AlphaEarth through compact global index"
```

## Task 11: Add artifact materialization without touching terrain or camera paths

**Files:**

- Create: `applications/earth_explorer/science_artifact_layer.h`
- Create: `applications/earth_explorer/science_artifact_layer.cpp`
- Create: `applications/earth_explorer/science_runtime.h`
- Create: `applications/earth_explorer/science_runtime.cpp`
- Modify: `applications/earth_explorer/science_image_pager.h`
- Modify: `applications/earth_explorer/CMakeLists.txt`
- Modify: `applications/earth_explorer/earth_main.cpp`
- Create: `tests/science_artifact_layer_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing scene-authority tests**

With fake camera/manipulator/layer adapters, assert:

- query completion alone creates no scene node;
- `showArtifact(id)` adds or updates only a dedicated science artifact layer;
- materialization uses the existing `ScienceImagePager`/`science://` request boundary and never
  decodes or uploads the scientific raster synchronously in the frame callback;
- show/hide/remove do not call `fly_to`, change view matrices, altitude, selected ground point,
  `TileManager`, `TileCallback::OVERLAY`, terrain elevation, or 3D Tiles settings;
- clean reset removes every science artifact and cancels its jobs;
- stale generation completion cannot resurrect a removed artifact.

**Step 2: Implement a dedicated scene bridge**

The bridge accepts immutable raster artifact payloads on the main thread and creates a bounded
georeferenced overlay node separate from the current single overlay slot. The existing
`ScienceImagePager` receives virtual artifact image requests backed by the verified cache; remote
GDAL/network work remains in research jobs. Visible science work stays below camera, terrain,
3D Tiles, and basemap priorities. It must not modify:

```text
readerwriter/TileCallback.*
plugins/osgdb_tms/*
applications/earth_explorer/hk_elevation_filter.h
applications/earth_explorer/tiles3d_data.*
camera/photo request code
```

If implementation appears to require one of these paths, stop and revise the architecture before
editing.

**Step 3: Wire optional startup**

When science is compiled, `earth_main.cpp` starts `ScienceRuntime`, asks the plugin host for
providers, and registers the dedicated layer. When missing/offline, it records status and proceeds
with all existing startup paths.

**Step 4: Verify**

```bash
cmake --build build/science_g0 --target osgVerse_Test_ScienceArtifactLayer -j4
ctest --test-dir build/science_g0 --output-on-failure -R ScienceArtifactLayer
git diff -- readerwriter/TileCallback.cpp readerwriter/TileCallback.h \
  plugins/osgdb_tms applications/earth_explorer/hk_elevation_filter.h \
  applications/earth_explorer/tiles3d_data.cpp applications/earth_explorer/tiles3d_data.h
```

Expected: test passes; protected-path diff is empty.

**Step 5: Commit**

```bash
git diff --check
git add applications/earth_explorer/science_artifact_layer.* \
  applications/earth_explorer/science_runtime.* applications/earth_explorer/science_image_pager.h \
  applications/earth_explorer/CMakeLists.txt \
  applications/earth_explorer/earth_main.cpp tests/science_artifact_layer_tests.cpp \
  tests/CMakeLists.txt
git commit -m "feat(scienceearth): materialize isolated science artifacts"
```

## Task 12: Connect the minimal Agent vertical slice

**Files:**

- Create: `applications/earth_explorer/ai_science_tools.h`
- Create: `applications/earth_explorer/ai_science_tools.cpp`
- Modify: `applications/earth_explorer/ai_setup.h`
- Modify: `applications/earth_explorer/ai_setup.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`
- Create: `tests/ai_science_tools_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing tool tests**

Register exactly these G1 tools:

```text
search_science_sources
start_science_research
get_research_job
show_science_artifact
```

Assert:

- search returns the AlphaEarth descriptor/capabilities/coverage/license without enabling it;
- start validates geometry/year/budget, enqueues, and returns `job_id` promptly;
- get returns state/progress/warnings/evidence/artifact ids and supports Partial/Failed/Cancelled;
- show requires a Ready/Partial artifact and never moves the camera;
- no tool blocks on network/decode or calls provider work on the ToolRegistry dispatch thread;
- a disabled/missing plugin gives a structured unavailable response;
- generated-image/photo history is not read or reused by science tools;
- the existing photo two-turn gate and `fly_to` semantics remain unchanged.

**Step 2: Implement thin adapters only**

The tools call `ScienceRuntime`/`ResearchJobManager`; they do not call GDAL, HTTP, OSG, or cache
internals. Update the system prompt to explain that research is asynchronous and scene display is
explicit. Preserve the existing 30-tool-round limit and all photo instructions.

**Step 3: Add an end-to-end fake-model scenario**

Script:

1. model searches sources;
2. starts NVIDIA HQ 2024 AlphaEarth RGB research;
3. polls pending;
4. receives Ready artifact with evidence;
5. explicitly shows artifact;
6. responds with source/version/limitations.

Assert zero camera calls and zero prior-image references across the full script.

**Step 4: Verify**

```bash
cmake --build build/science_g0 --target \
  osgVerse_Test_AiScienceTools osgVerse_Test_Ai_Chat -j4
ctest --test-dir build/science_g0 --output-on-failure -R 'AiScienceTools|Ai_Chat'
```

**Step 5: Commit**

```bash
git diff --check
git add applications/earth_explorer/ai_science_tools.* \
  applications/earth_explorer/ai_setup.* applications/earth_explorer/CMakeLists.txt \
  tests/ai_science_tools_tests.cpp tests/CMakeLists.txt
git commit -m "feat(scienceearth): connect AlphaEarth to Agent research tools"
```

## Task 13: Add the minimal experimental science UI and cache controls

**Files:**

- Create: `applications/earth_explorer/science_ui.h`
- Create: `applications/earth_explorer/science_ui.cpp`
- Modify: `applications/earth_explorer/EarthControlUI.h`
- Modify: `applications/earth_explorer/ui.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`
- Create: `tests/science_ui_tests.cpp`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write failing view-model tests**

Test a UI-independent view model for:

- experimental label and source/version/license;
- year selector 2017–2025;
- bbox/current-view request summary and estimated byte/cell limits;
- job state, progress, cancel, retry, warnings, partial result, and evidence;
- show/hide/remove artifact actions;
- cache bytes/items/limit and clear-science-cache only;
- plugin missing/offline status;
- no direct provider, network, camera, or terrain calls.

**Step 2: Implement a small panel section**

Use the existing scroll container and avoid nested wheel capture. UI actions call the same
`ScienceRuntime` APIs as the Agent. Do not create a second query path or a UI-only layer.

**Step 3: Preserve the panel-scroll regression**

Extend existing ImGui settings/input tests to scroll the whole control panel down and back up with
the science section present. Confirm clean reset removes the science layer along with all other
selectable layers/tracks/ranges.

**Step 4: Verify and commit**

```bash
cmake --build build/science_g0 --target \
  osgVerse_Test_ScienceUI osgVerse_Test_ImGuiSettings osgVerse_Test_Feeds -j4
ctest --test-dir build/science_g0 --output-on-failure \
  -R 'ScienceUI|ImGuiSettings|Feeds'
git diff --check
git add applications/earth_explorer/science_ui.* applications/earth_explorer/EarthControlUI.h \
  applications/earth_explorer/ui.cpp applications/earth_explorer/CMakeLists.txt \
  tests/science_ui_tests.cpp tests/CMakeLists.txt
git commit -m "feat(scienceearth): add experimental AlphaEarth controls"
```

## Task 14: Run the complete automated regression and performance gate

**Files:**

- Create: `tests/run_scienceearth_g1_gate.sh`
- Create: `docs/scienceearth/g1-verification.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`
- Modify: `tests/CMakeLists.txt`

**Step 1: Write the gate script as fail-closed**

The script must start from a clean fresh build directory, build science off and on, run all
required targets, perform link audits, and produce a machine-readable report. Any skipped required
test is failure. Network tests remain a separate explicit stage with recorded date/environment.

**Step 2: Run science-off regression**

The required set is the Task 2 protected suite plus package tests and input safety when available.
Compare against the recorded baseline. There must be no protected-path behavior or build-graph
regression.

**Step 3: Run science-on regression**

Include every new test and the existing protected suite. Repeat cancellation/range tests, run the
recursive link audit, check the bounded cache, and verify startup with:

- plugin present and local fixture available;
- plugin absent;
- network offline;
- corrupt index;
- cancelled request;
- stale generation completion.

**Step 4: Run live AlphaEarth measurements**

With explicit network opt-in, perform five NVIDIA HQ and five Hong Kong RGB queries. Record median,
P95, bytes, request count, range response codes, selected tile/year, output checksum, and whether
the full source was avoided. Also record rendered-cache hit latency and peak additional resident
memory. The gate fails if uncached median exceeds 3 s, P95 exceeds 8 s, rendered-cache hit exceeds
100 ms, or AlphaEarth RGB adds more than 300 MB on the documented reference setup. Do not state a
latency promise beyond the measured environment.

**Step 5: Record pass/fail and commit**

```bash
bash tests/run_scienceearth_g1_gate.sh
git diff --check
git add tests/run_scienceearth_g1_gate.sh tests/CMakeLists.txt \
  docs/scienceearth/g1-verification.md docs/scienceearth/g0-g1-baseline.md
git commit -m "test(scienceearth): verify G1 vertical slice"
```

Expected: the document links every result to commit/build/config. A failing protected regression
blocks packaging.

## Task 15: Package the canonical formal app and conduct manual acceptance

**Files:**

- Modify: `packaging/package_macos.sh`
- Modify: `tests/package_macos_tests.sh`
- Create: `docs/scienceearth/g1-manual-test.md`
- Modify: `docs/scienceearth/g1-verification.md`

**Step 1: Make package tests fail on the current generic bundle**

Require the canonical bundle:

```text
/Users/USER/Desktop/osgSol Earth.app
CFBundleName=osgSol Earth
CFBundleDisplayName=osgSol Earth
CFBundleIdentifier=com.anloren.osgsol.earth
CFBundleShortVersionString=<release-candidate version>
CFBundleExecutable=osgSol_Earth
```

Tests also require recursive dependency audit, strict ad-hoc codesign verification, no embedded
keys/config/history, no `imgui.ini`, and one stable app path that is updated rather than a newly
named app each build.

**Step 2: Update packaging without deleting the known-good app first**

Build and verify into a sibling staging path, for example:

```text
/Users/USER/Desktop/osgSol Earth.app.staging
```

Only after all automated bundle checks and launch smoke pass may packaging atomically replace the
canonical app, retaining a rollback copy until manual acceptance. Never write into or overwrite
the old project repository.

**Step 3: Run recursive package verification**

```bash
bash packaging/package_macos.sh
bash tests/package_macos_tests.sh
python3 packaging/audit_macos_bundle.py \
  --app '/Users/USER/Desktop/osgSol Earth.app' \
  --baseline '<verified pre-G1 rollback app>' \
  --json build/science_g1/final-bundle-audit.json
codesign --verify --deep --strict '/Users/USER/Desktop/osgSol Earth.app'
```

Expected: canonical app launches, plugin is found, recursive audit passes, size is inside the G0
decision, and rollback app remains available during manual testing.

**Step 4: Execute the real-device manual matrix**

`docs/scienceearth/g1-manual-test.md` must cover:

1. panel scroll down and back up;
2. clean reset removes all selectable layers, tracks, selected paths, station range circles, and
   science artifacts;
3. Hong Kong 3D Tiles refine across altitude changes without holes, tofu blocks, scale mismatch,
   or distorted terrain;
4. middle-button tilt retains selected ground point and high-precision loading does not oscillate
   altitude;
5. fly to NVIDIA HQ, confirm the visible view, then photo; photo does not change to top view and
   does not inherit a Hong Kong image;
6. AlphaEarth NVIDIA/Hong Kong query shows correct tile/year/orientation/RGB and evidence;
7. Agent search/start/poll/show completes without camera movement;
8. offline and plugin-missing launch leave existing app functions usable;
9. cancel/clear-cache/clean-reset do not resurrect late artifacts;
10. repeat launch and app update at the same desktop path.

Each row records build commit, app hash, OS/hardware, result, screenshot/log path, and issue id.

**Step 5: Commit the release-candidate evidence**

```bash
git diff --check
git add packaging/package_macos.sh tests/package_macos_tests.sh \
  docs/scienceearth/g1-manual-test.md docs/scienceearth/g1-verification.md
git commit -m "build(scienceearth): package G1 manual-test candidate"
```

Do not tag or push here.

## Task 16: Release only after explicit user approval

**Files:**

- Modify: `CHANGELOG.md` or the repository's existing release-notes file
- Modify: `docs/scienceearth/g1-verification.md`

**Step 1: Verify the exact release commit**

Run the full G1 gate and package audit again on a clean tree. Record final token-independent facts:
commit, tag candidates, app hash, test counts, bundle delta, link audit, manual matrix, and rollback
commit.

**Step 2: Stop for user approval**

Present the verification report. Ask whether to release the proposed semantic version. Do not
infer approval from prior design confirmations.

**Step 3: Create the immutable tag pair only after approval**

For an approved version such as `v0.3.0`:

```bash
git tag -a v0.3.0 -m "osgSol Earth v0.3.0 - ScienceEarth G1"
git tag -a ScienceEarth-v0.3.0 -m "ScienceEarth G1 / osgSol Earth v0.3.0"
test "$(git rev-parse 'v0.3.0^{}')" = \
     "$(git rev-parse 'ScienceEarth-v0.3.0^{}')"
test "$(git rev-parse 'ScienceEarth^{}')" = \
     "0e91c7c4b121d80b929d595ea711d3dd0833ee67"
```

Replace the example version with the user-approved version. Never force or move a tag.

**Step 4: Sync only after explicit approval**

Push the branch and both new tags to the verified new-project remote. Confirm remote URLs first;
do not push to or overwrite the original project repository.

## Completion definition

G0/G1 is complete only when all of the following are true:

- G0 says `GO` with measured dependency size/link/range evidence;
- science-off behavior matches the protected `v0.2.0` baseline;
- AlphaEarth works globally through the compact index, with NVIDIA HQ and Hong Kong acceptance;
- the Agent can discover, enqueue, poll, cite, and explicitly show the source;
- no science action implicitly changes camera/view/photo behavior;
- the canonical desktop app is updated in place and passes real-device manual testing;
- no release tag or sync occurs before the user's explicit approval.
