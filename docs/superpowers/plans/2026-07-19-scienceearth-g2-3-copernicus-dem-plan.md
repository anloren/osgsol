# ScienceEarth G2-3 Copernicus DEM Implementation Plan

> Execute in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on
> `codex/scienceearth-g2-3-dem`. Keep v0.5.0 frozen and do not launch the GUI application.

**Goal:** Deliver a scientifically honest, bounded Copernicus DEM GLO-30 provider through the
existing Science Query Service and progressive UI without changing existing world behavior.

**Architecture:** Add an injected asynchronous provider/runtime parallel to Sentinel-2, reuse the
trimmed GDAL COG path and existing ground-grid overlay, extend artifacts only with bounded scalar
statistics, and integrate through registry/query/UI/Agent boundaries.

**Tech stack:** C++17, GDAL GTiff/VRT/MEM and `/vsicurl/`, ImGui, CMake/CTest, AWS Open Data COG.

## Task 1: Freeze contracts and scalar evidence

**Files:**

- Modify `science/ScienceQueryTypes.h`
- Modify `science/ScienceQueryTypes.cpp`
- Modify `science/ScienceQueryTypesTest.cpp`

1. Add failing tests for an immutable `ScienceScalarSummary` vector and byte accounting of all
   owned strings; assert old artifacts retain identical defaults and estimates.
2. Build/run `osgSol_Test_ScienceQueryTypes` and observe RED.
3. Add the summary contract and overflow-safe byte accounting.
4. Run GREEN and the existing artifact/store/query-service tests.
5. Commit only these files: `feat(scienceearth): add scalar evidence summaries`.

## Task 2: Implement deterministic DEM source addressing

**Files:**

- Create `science/CopernicusDemSource.h`
- Create `science/CopernicusDemSource.cpp`
- Create `science/CopernicusDemSourceTest.cpp`
- Modify `science/CMakeLists.txt`

1. Write failing table tests for Tokyo, Hong Kong, NVIDIA headquarters, southern/western
   hemispheres, exact integer boundaries, polar longitude spacing, and antimeridian normalization.
2. Require only `https://copernicus-dem-30m.s3.amazonaws.com/` objects, uppercase zero-padded
   geocells, a hard tile-count limit, and rejection of non-finite/out-of-range coordinates.
3. Implement point-centered WGS84 bounds and intersecting geocell URL derivation. Treat longitude
   wrapping explicitly; never concatenate user strings into a URL.
4. Run GREEN and commit: `feat(scienceearth): address Copernicus DEM COG cells`.

## Task 3: Implement bounded numeric DEM reading

**Files:**

- Create `science/CopernicusDemRuntime.h`
- Create `science/CopernicusDemRuntime.cpp`
- Create `science/CopernicusDemRuntimeTest.cpp`
- Modify `science/CMakeLists.txt`

1. Write failing injected-I/O tests for queued/reading/ready progress, generation cancellation,
   stale-result rejection, transport failure, partial coverage, and destruction join.
2. Write failing local GDAL tests using two adjacent floating-point GeoTIFF fixtures. Assert
   WGS84 ground grid, north-up pixels, NoData alpha, fixed hypsometric colors, center/min/max/mean,
   valid/NoData counts, actual resolution, and multi-cell bounds.
3. Implement `ICopernicusDemIo`, single-worker runtime, `GDALBuildVRT` trusted composition, bounded
   256 by 256 Float32 read, mask/statistics, and fixed color ramp.
4. Production I/O accepts only derived allowlisted URLs and emits exact access evidence. It may not
   issue a complete-object read or unbounded listing.
5. Run runtime and type/store tests GREEN. Commit:
   `feat(scienceearth): read bounded Copernicus DEM evidence`.

## Task 4: Add the provider and query-service integration

**Files:**

- Create `science/CopernicusDemProvider.h`
- Create `science/CopernicusDemProvider.cpp`
- Create `science/CopernicusDemProviderTest.cpp`
- Modify `science/ScienceQueryService.cpp`
- Modify `science/ScienceQueryServiceTest.cpp`
- Modify `science/CMakeLists.txt`

1. Write failing descriptor/query tests for static time, DSM meaning, EGM2008 metres, fixed legend,
   supported geometry/output, and rejection of years, aggregation, AlphaEarth analyses, wrong
   variables, upsampling, and excessive spans.
2. Add a query-service cost test using one Float32 source component plus RGBA output, rather than
   the RGB byte assumptions used by optical previews.
3. Implement provider health translation and precise validation. A normal no-coverage/ocean result
   must not globally degrade the source.
4. Update generic raster cost estimation by variable data kind/bytes without changing existing
   AlphaEarth/Sentinel expectations.
5. Run provider, registry, service, AlphaEarth, and Sentinel tests GREEN. Commit:
   `feat(scienceearth): register Copernicus DEM provider contract`.

## Task 5: Add clear manual UI and Agent access

**Files:**

- Modify `applications/earth_explorer/science_query_builder.h`
- Modify `applications/earth_explorer/science_earth_panel.h`
- Modify `applications/earth_explorer/science_earth_panel.cpp`
- Modify `applications/earth_explorer/science_earth_panel_test.cpp`
- Modify `applications/earth_explorer/science_ai_tools.cpp`
- Modify `applications/earth_explorer/science_ai_tools_test.cpp`
- Modify `applications/earth_explorer/earth_main.cpp`
- Modify `tests/earth_control_layout_tests.cpp`

1. Write failing query-builder/panel tests requiring static instant time, no year slider/control,
   concise DSM help, fixed color legend, EGM2008/unit/statistics, wrapped text, scrollbars, and the
   existing compact panel widths.
2. Extend Agent tests so source discovery returns DEM semantics and preview submission emits a DEM
   query without camera/layer changes. Reject point-series/regional modes precisely.
3. Implement source-aware static query construction and progressive disclosure. Do not place
   permanent explanatory paragraphs in the operation card.
4. Register `CopernicusDemProvider` only inside `OSGSOL_BUILD_SCIENCE`; do not touch science-off.
5. Run panel/layout/AI/preview regression tests and a camera-authority source scan. Commit:
   `feat(scienceearth): expose Copernicus DEM research`.

## Task 6: Real-source and preservation verification

**Files:**

- Create `science/CopernicusDemProductionSmoke.cpp`
- Modify `science/CMakeLists.txt`
- Create `docs/scienceearth/copernicus-dem-verification.md`

1. Add a command-line-only Tokyo smoke target. It must finish within a bounded timeout, produce a
   ready artifact, valid numeric statistics, EGM2008/source URL/DSM evidence, and no full-object
   fallback marker.
2. Build all modified targets, then run `ctest --test-dir build/science_g2 -L offline
   --output-on-failure` from a fully rebuilt tree.
3. Run the real smoke once without launching the app. Record exact command, source object, timings,
   source window, statistics, and limitations in the verification document.
4. Run science build/bundle/consumer contracts. Confirm no modifications under terrain,
   `EarthManipulator`, 3D Tiles, photo, satellite, or exit code.
5. Commit verification and smoke target: `test(scienceearth): verify Copernicus DEM source`.

## G2-3 completion gate

G2-3 is complete only when local/provider/UI/Agent tests pass, the real AWS bounded smoke succeeds,
the full offline suite passes, source evidence is scientifically correct, and the app has never
been launched by automation. Packaging waits until G3 is complete so the Desktop app is updated
once, not repeatedly.

