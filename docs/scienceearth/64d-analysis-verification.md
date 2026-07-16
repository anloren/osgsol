# ScienceEarth 64D Analysis Verification

**Date:** 2026-07-16

**Scope:** Task 11 automated regression and bounded production-data verification. This document
does not claim that Task 12 packaging or Desktop replacement has occurred.

## 1. Status vocabulary

- **VERIFIED** — the stated command or invariant passed in the frozen evidence.
- **FAILED -> FIXED** — a real failure was observed, repaired with a focused test-first cycle,
  and covered by the final passing run. The original failure remains recorded below.
- **NOT MEASURED** — the product or verifier did not expose the quantity. No value is inferred.

## 2. Frozen evidence identity

| Item | Value |
|---|---|
| Source commit under verification | `a49ede2b289d7ed220818f2eb2b6fa65f1f02a72` |
| Science dependency prefix | `build/science-deps-g2-v5/prefix` |
| Production index | `build/science-index-full/alphaearth.sqlite` |
| Production index SHA-256 | `15875963d1bf4dd3f35a1f6c3ec6329378fef0fab6549fac677552f1d348f736` |
| Index source | `https://data.source.coop/tge-labs/aef/v1/annual/aef_index.parquet` |
| Index source SHA-256 | `f738e7d274ad582e56e20a3a8b444c6f2a3ece5781f8f9855bb7ca3d9ed2942f` |
| Index contents | schema 2; 302,466 declared and actual rows; 2017-2025; 9 distinct years |
| Verifier source SHA-256 | `be05028f35847b09c81db872b402dc9b058538b8f4c6772ea6212dc3528edea0` |
| Verifier binary SHA-256 | `3de2a41cc1951a87e891508aee4d8ae3f2a18faf9fe1629d66335dc15cf3ee2b` |
| Final JSONL SHA-256 | `7da6d879b9cece487515b5d05d8b75b701b4b5893a8195d8cc6a4ce2b0f5fdf0` |
| Verifier watchdog | 3,600 seconds; TERM grace 2 seconds |

The final JSONL contains 40 records and ends with `status=PASS`,
`raw_embedding_values_emitted=false`,
`network_path="production GDAL /vsicurl/ direct to data.source.coop"`, and
`local_server_started=false`. The run used the production Source Cooperative asset path directly;
no proxy, local server, port, or alternate data service was introduced.

## 3. Automated gates

| Gate | Status | Evidence |
|---|---|---|
| v5 private science dependencies | **VERIFIED** | `SCIENCE_DEPS_ROOT=build/science-deps-g2-v5 packaging/science_deps/build_science_deps.sh --verify` passed. |
| Science-enabled build | **VERIFIED** | `cmake --build build/science_g2 -j4` passed. |
| Science-enabled offline suite | **VERIFIED** | 41/41 tests passed. |
| Fresh science-off build | **VERIFIED** | Fresh configure/build with `OSGSOL_BUILD_SCIENCE=OFF` passed; 22/22 offline tests passed. |
| Science-off binary/link isolation | **VERIFIED** | `otool -L` lists no science library; the EarthExplorer `link.txt` scan found no `osgSolScience`, AlphaEarth, query-service, or analysis-engine reference. |
| macOS normal-exit regression | **VERIFIED** | `macos-exit` 1/1 passed; this is automated same-path evidence, not the Task 12 three-path manual result. |
| Protected regression selection | **VERIFIED** | 26/26 tests passed. |
| Production camera-authority scan | **VERIFIED** | No production match after excluding the intentional `_test.cpp` assertion. |
| Implementation diff check | **VERIFIED** | `git diff --check d331f0a..HEAD` passed with no output. |
| Verifier self-test | **VERIFIED** | 11 valid fixtures accepted; 34 bad fixtures rejected; 0 network requests. Both counts are produced from successful validator calls, not constants. |

The final gate commands were:

```sh
SCIENCE_DEPS_ROOT=build/science-deps-g2-v5 \
  packaging/science_deps/build_science_deps.sh --verify
cmake --build build/science_g2 -j4
ctest --test-dir build/science_g2 --output-on-failure
cmake --fresh -S . -B build/science_64d_off -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DVERSE_BUILD_EXAMPLES=ON -DOSGSOL_BUILD_SCIENCE=OFF
cmake --build build/science_64d_off -j4
ctest --test-dir build/science_64d_off -L offline --output-on-failure
/bin/zsh -ec '
test -x build/science_64d_off/bin/osgVerse_EarthExplorer
otool -L build/science_64d_off/bin/osgVerse_EarthExplorer > /tmp/science_64d_off_otool.txt
if rg -n "osgSolScience|AlphaEarth|ScienceQueryService|ScienceAnalysisEngine" \
  /tmp/science_64d_off_otool.txt; then exit 1; else rc=$?; test "$rc" -eq 1; fi
test -f build/science_64d_off/applications/earth_explorer/CMakeFiles/osgVerse_EarthExplorer.dir/link.txt
if rg -n "osgSolScience|AlphaEarth|ScienceQueryService|ScienceAnalysisEngine" \
  build/science_64d_off/applications/earth_explorer/CMakeFiles/osgVerse_EarthExplorer.dir/link.txt; \
  then exit 1; else rc=$?; test "$rc" -eq 1; fi
test -d applications/earth_explorer
if rg -n "setByEye|setByMatrix|setCenter|setDistance|fly" applications/earth_explorer \
  -g "science_*.cpp" -g "!science_*_test.cpp"; \
  then exit 1; else rc=$?; test "$rc" -eq 1; fi
'
/tmp/scienceearth_64d_real_verification --self-test
/bin/zsh -o pipefail -c '/tmp/scienceearth_64d_real_verification build/science-index-full/alphaearth.sqlite | tee /tmp/scienceearth_64d_real_verification.jsonl'
ctest --test-dir build/science_g2 --output-on-failure \
  -R 'EarthManipulator|TerrainGrid|Tiles3dPaging|Satellite|EarthControlLayout|Science'
ctest --test-dir build/science_g2 --output-on-failure \
  -R '^osgVerse_Test_OsgApplicationUsageExit$'
git diff --check d331f0a..HEAD
```

Task 12 still must build, audit, sign, exercise, and replace the fixed Desktop application before
human verification. The results above do not authorize tagging, pushing, or release publication.

## 4. Production-data verification

All component values used by the engine are the complete 64-dimensional latent embedding
(`A00` through `A63`). Reported distances, PCA results, and clusters are mathematical relations
inside that embedding space. They are not temperature, vegetation, elevation, land cover,
natural color, or evidence of a physical cause.

### 4.1 Point series, 2017-2025

| Case | NVIDIA point | Hong Kong point |
|---|---:|---:|
| Requested coordinate | 37.3707, -121.9631 | 22.3193, 114.1694 |
| Status / job / artifact | Ready / 1 / `alphaearth-embedding-1` | Ready / 2 / `alphaearth-embedding-2` |
| Valid years | 2017-2025 (9/9) | 2017-2025 (9/9) |
| Explicit gaps | none | none |
| Components per valid year | 64 | 64 |
| Verified footprint W,S,E,N | -121.963109173090, 37.370691016509, -121.962995010574, 37.370782130474 | 114.169334616251, 22.319238332179, 114.169433416729, 22.319330257859 |
| Source references | 9 | 9 |
| Estimated source bytes | 576 | 576 |
| Artifact bytes | 69,715 | 69,715 |
| Provider elapsed | 54.123016916 s | 52.071519791 s |
| Observed wall time | 54.124171750 s | 52.071686167 s |
| Norm count | 9 | 9 |
| Norm min / mean / max | 0.997595787 / 1.000095632 / 1.002830863 | 0.997039676 / 1.000682506 / 1.002815485 |

Dataset ids, keyed by acquisition year:

| Year | NVIDIA | Hong Kong |
|---:|---:|---:|
| 2017 | 7574 | 176633 |
| 2018 | 7851 | 177253 |
| 2019 | 8128 | 177873 |
| 2020 | 8405 | 178493 |
| 2021 | 8682 | 179113 |
| 2022 | 8959 | 179733 |
| 2023 | 9236 | 180353 |
| 2024 | 9513 | 180973 |
| 2025 | 9790 | 181593 |

The exact source COG URL for every id is retained in the frozen JSONL `source_reference` records.
The byte values above are provider estimates, not measured HTTP transfer totals.

### 4.2 NVIDIA regional change, 2018 versus 2025

**VERIFIED** — job 3 returned artifact `alphaearth-embedding-3`, processing version
`ScienceEarth-64D-v1`, with the following bounded result:

- grid: 128 by 128; retained grids: `[2018, 2025]`;
- source dataset ids: 7851 (2018) and 9790 (2025);
- actual footprint W,S,E,N: `-121.977948000863, 37.358846329988,
  -121.948265781008, 37.382535946223`;
- actual resolution: 19.981395548 metres;
- valid overlap: 16,384; NoData: 0; coverage fraction: 1.0;
- cosine-distance summary: mean 0.0530061023, median 0.0372295375,
  standard deviation 0.0533768260, minimum 0.0075953920, maximum 0.5790513956;
- relative hotspot quantile: 0.90; threshold: 0.0916155930;
- estimated source bytes: 8,520,192; artifact bytes: 9,798,504;
- provider elapsed: 25.376147917 seconds; wall time: 25.376454708 seconds.

The hotspot threshold is relative in embedding space and is not a threshold for a named physical
variable.

### 4.3 NVIDIA annual regional overview

**VERIFIED** — job 4 returned artifact `alphaearth-embedding-4`. It processed all nine years in
order while retaining only the comparison grids `[2018, 2025]`. The 128 by 128 footprint,
resolution, overlap, coverage, distribution, and hotspot result match the bounded two-year case.
All annual validity flags are 1.

| Year | Annual embedding-space value |
|---:|---:|
| 2017 | 0 |
| 2018 | 0.0235014924171958 |
| 2019 | 0.0114112629918451 |
| 2020 | 0.0364376364498761 |
| 2021 | 0.0507246679732389 |
| 2022 | 0.0536175172540508 |
| 2023 | 0.0238778107624626 |
| 2024 | 0.0276516240472743 |
| 2025 | 0.0291672375907470 |

The annual overview used NVIDIA dataset ids 7574, 7851, 8128, 8405, 8682, 8959, 9236, 9513,
and 9790 respectively. Estimated source bytes were 38,340,864; artifact bytes were 9,847,469;
provider elapsed was 111.623283542 seconds and wall time was 111.623577125 seconds.

### 4.4 Cancellation and last-good retention

**VERIFIED** — Hong Kong annual job 5 was cancelled at the `32/64 dimensions` checkpoint after
3.488704291 seconds. The result was `Cancelled`; no partial `Ready` state was observed. The prior
last-good artifact `alphaearth-embedding-4` remained retained. The cancelled request's estimated
source size was 38,489,472 bytes; no cancelled artifact was published.

### 4.5 Deterministic PCA and clustering replay

**VERIFIED** — both analyses used 16,384 valid samples and produced identical bounded-result
hashes on replay (`FNV-1a-64 over bounded result fields`).

| Analysis | Artifacts | Replay hash | Wall times | Diagnostics |
|---|---|---|---|---|
| Local PCA | `alphaearth-embedding-6`, `alphaearth-embedding-7` | `ba725382b307fb71` both times | 5.477051833 s, 4.424083458 s | Limitation text states that loadings have no physical labels and components were centered but not standardized. |
| Spherical clusters | `alphaearth-embedding-8`, `alphaearth-embedding-9` | `6cb01c1c1771a54e` both times | 4.202332125 s, 3.737881125 s | populations `[4961, 4270, 2757, 4396]`; converged; 60 iterations; limitation states ids are mathematical groups, not physical labels. |

PCA axes and cluster ids remain local, deterministic mathematical constructs. This evidence does
not attribute either result to a physical phenomenon.

## 5. Failures exposed and repaired

1. **FAILED -> FIXED — complete-component naming.** The first production-data attempt exposed
   that the complete runtime expected `A01-A64`, while the real Source Cooperative COG has 64
   signed-Int8 bands `A00-A63`. The failing production case was preserved as a focused RED;
   a metadata-only production COG probe independently reported 8,192 by 8,192 pixels, 64 Int8
   bands, `NoData=-128`, band 1=`A00`, band 2=`A01`, band 10=`A09`, band 17=`A16`, and
   band 64=`A63`;
   runtime/provider/export/UI contracts were corrected to map GDAL band `n` to `A(n-1)`, and the
   focused and complete suites passed before the final real rerun. The fast false-color preview
   remains intentionally unchanged at `A01/A16/A09`, using GDAL bands `2/17/10`.
2. **FAILED -> FIXED — verifier provenance vocabulary.** After the product correction, the
   verifier still carried the obsolete complete-component nomenclature in its provenance checks.
   That was a verifier defect, not evidence that `A64` exists. The verifier assertion and
   self-test fixtures were corrected and the verifier was rebuilt; the hashes in section 2 name
   the final source and binary.
3. **FAILED -> FIXED — cross-workload duration contamination.** The production query service
   originally shared one observed-throughput bucket across every non-preview 64D workload. A
   nine-year point series therefore contaminated the later 128 by 128 regional estimate and
   falsely tripped the unchanged 240-second duration gate. A focused RED reproduced that exact
   cross-workload rejection. `ScienceQueryService` now scopes observed throughput by source,
   geometry, output, analysis kind, aggregation, time mode, and explicit year count; the duration
   budget gate itself was not relaxed. Same-class duration estimation and pre-dispatch rejection
   remain covered.

The initial failed real run was not counted as success merely because a shell pipeline could
return the status of `tee`; the final evidence is the rebuilt verifier's PASS summary and
successful process completion after the scoped repairs.

## 6. Warnings and measurements not available

- **VERIFIED WITH WARNING:** the final run completed with process exit status 0 and PASS evidence,
  but its console showed two transient remote HTTP 500 warnings for byte ranges
  `306361740-307054094` and `327788567-328546580`. GDAL `/vsicurl` recovered and every required
  case completed. Stderr was
  not part of the hashed JSONL, and the internal retry count is **NOT MEASURED**; this remains an
  external-path reliability item to observe during Task 12/manual testing.
- GDAL request/read counters are **NOT MEASURED** because the product interface reports
  `not_available_in_product_interface`.
- Actual HTTP bytes transferred are **NOT MEASURED**. Every `estimated_source_bytes` value is a
  budget estimate, not a transport counter.
- Regional and replay norm distributions are **NOT MEASURED** in this evidence; point-series norm
  diagnostics are reported in section 4.1.
- PCA and clustering artifact byte sizes and provider-stage durations are **NOT MEASURED** in the
  final JSONL; their artifact ids, sample counts, replay hashes, wall times, limitations, and
  clustering convergence diagnostics are verified.
- Raw 64D vectors were deliberately not written to the verifier output
  (`raw_embedding_values_emitted=false`). Their absence is a bounded-output invariant, not
  missing scientific evidence.

## 7. Task 11 conclusion and Task 12 boundary

Task 11 has verified the v5 dependency state, enabled and science-off regressions, protected
behaviors, bounded real Source Cooperative reads, cancellation, deterministic PCA/clustering,
provenance, and honest limitations. It does not claim physical interpretation of latent
dimensions.

No Desktop application was replaced by this task. Packaging, bundle/link audit, codesign,
same-path packaged exit, and the full manual matrix remain Task 12 release gates. Tagging,
synchronization, and publication remain forbidden until manual acceptance and an explicit user
request.
