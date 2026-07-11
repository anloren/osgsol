# ScienceEarth G0/G1 Baseline Contract

## Immutable release boundary

The protected baseline is osgSol Earth `v0.2.0`, commit
`0e91c7c4b121d80b929d595ea711d3dd0833ee67`. The annotated `ScienceEarth` boundary tag points to
that exact commit and is immutable. The plain `ScienceEarth` tag is never moved to a later commit.

Only a user-approved formal release receives the immutable tag pair `vX.Y.Z` and
`ScienceEarth-vX.Y.Z`, and both tags must dereference to the same verified commit. Development and
intermediate acceptance commits receive neither release tag.

## Protected behavior

The approved ScienceEarth design protects all of the following contracts:

- the clean preset removes every selectable overlay, satellite track, selected path, and range;
- the control panel scrolls down and back up;
- satellite trajectories and station range graphics do not survive a clean reset;
- Hong Kong terrain keeps the continuous elevation correction, nested grid, skirts, and camera
  floor behavior;
- Hong Kong 3D Tiles continue refining after altitude changes without holes, distorted ground, or
  height-scale disagreement;
- middle-mouse camera tilt does not shift the selected ground point;
- higher precision loading does not automatically oscillate camera altitude;
- photo generation uses the user-confirmed visible view and never switches to a top view;
- target photography remains a two-turn flow: fly, allow view confirmation, then capture;
- every generated image is independent and must not inherit a previous Hong Kong or other image;
- existing basemap, labels, precipitation, clouds, NDVI, nightlights, GEBCO, feeds, satellites,
  flights, ships, 3D Tiles, and AI tools retain their established behavior when science is off.

## Science-off validation snapshot

Task 2 established a fresh `build/science_test` baseline on 2026-07-12 from commit `5476f31`
plus the Task 2 science-off build-contract patch. The build used CMake 4.3.3, Release mode,
AppleClang 21.0.0.21000101, and arm64, with `BUILD_TESTING=ON`,
`VERSE_BUILD_EXAMPLES=ON`, and `OSGSOL_BUILD_SCIENCE=OFF`.

The generated contract recorded phase `G0-G1`, science value `0`, and no
`osgSolScienceCore` or `osgdb_science` target. All 15 requested targets built successfully. The
matching CTest selection passed 15 of 15 tests in 20.52 seconds:

| Test | Result | Duration |
|---|---|---:|
| `osgVerse_Test_Ai_Chat` | Passed | 3.15 s |
| `osgVerse_Test_Feeds` | Passed | 0.56 s |
| `osgVerse_Test_TileOverlay` | Passed | 1.01 s |
| `osgVerse_Test_TerrainGrid` | Passed | 0.48 s |
| `osgVerse_Test_Tiles3dPaging` | Passed | 0.89 s |
| `osgVerse_Test_Satellite` | Passed | 0.58 s |
| `osgVerse_Test_Geospatial` | Passed | 0.56 s |
| `osgVerse_Test_EarthManipulator` | Passed | 0.50 s |
| `osgVerse_Test_Ais` | Passed | 0.50 s |
| `osgVerse_Test_WorldTools` | Passed | 2.22 s |
| `osgVerse_Test_ImGuiSettings` | Passed | 0.59 s |
| `osgVerse_Test_McpSafety` | Passed | 8.46 s |
| `osgVerse_Test_ImGuiThreading` | Passed | 0.48 s |
| `osgVerse_Test_MediaThreading` | Passed | 0.52 s |
| `osgVerse_Test_ScienceBuildContract` | Passed | 0.00 s |

## G0 boundary and limits

G0 covers only a trimmed GDAL/PROJ/ZSTD dependency build, isolation proof, bounded COG byte-range
proof, and bundle-cost proof. It uses local fixtures and has no UI integration. It must not replace
the existing basemap, terrain, TIFF, 3D Tiles, camera, or photo paths with GDAL, and it must not
install or ship the full Homebrew GDAL dependency tree.

G0 is a hard go/no-go gate. Tasks 7-16 of the approved implementation plan must not begin unless
Task 6 records a passing signed-off gate. A failed gate requires a product decision; it does not
permit relaxing these limits.

| Gate | Pass condition |
|---|---|
| Existing behavior | Science-off targeted regression suite passes unchanged |
| Link isolation | Main executable and non-science libraries have no GDAL/PROJ/ZSTD references |
| System isolation | Bundle has no `/opt/homebrew`, `/usr/local`, build-tree, or source-tree runtime references |
| Added bundle size | Target `<= 40 MiB`; `40-60 MiB` requires explicit review; `> 60 MiB` is STOP |
| AlphaEarth correctness | NVIDIA HQ and Hong Kong fixture queries select expected tile/year, orientation, RGB bands, and dequantization |
| HTTP behavior | Requests are bounded byte ranges; no response equals the complete 270 MB source COG |
| Network latency | Uncached first RGB: median `<= 3 s`, P95 `<= 8 s`; rendered-cache hit `<= 100 ms` on the reference setup |
| Active memory | AlphaEarth RGB activity adds `<= 300 MB` on the reference test |
| Camera authority | Search/start/poll/show do not invoke `fly_to` or mutate the manipulator |
| Cache | Separate bounded science cache with deterministic eviction and no use of the existing terrain cache |
| Missing plugin/offline | App starts normally; science reports unavailable/partial without affecting existing layers |

## Task 6 recorded G0 decision

| Gate | Acceptance limit | Measured result | Outcome |
|---|---|---|---|
| Existing behavior | Science-off targeted suite passes unchanged | 15/15 passed | PASS |
| Dependency build | Pinned private static prefix verifies | GDAL 3.13.1 / PROJ 9.8.1 / ZSTD 1.5.7 hashes and prefix passed | PASS |
| Link isolation | No dynamic or static GDAL/PROJ/ZSTD marker in any non-science Mach-O | 0 main-reachable, but 49 pre-existing static ZSTD markers in one non-science library | **FAIL** |
| System isolation | No external, Homebrew, `/usr/local`, source, build, or unresolved references | 572 external resolutions, 36 forbidden-prefix, 252 source/build, 0 unresolved | **FAIL** |
| Added bundle size | `<= 40 MiB` target | 21,283,767 bytes (20.30 MiB) | PASS |
| AlphaEarth correctness and HTTP | Correct fixtures and bounded byte ranges | Task 5 offline suite 3/3 passed; both formal live transfers stayed bounded | PASS |
| Uncached first RGB latency | Median `<= 3 s`, P95 `<= 8 s` | NVIDIA 4.23269 s / 4.69879 s; Hong Kong 3.68769 s / 4.17196 s | **FAIL** |

G0_DECISION=STOP
REASON=Corrected uncached AlphaEarth RGB medians exceed the hard 3 s limit; all-Mach-O auditing also finds external forbidden paths, source/build references, and non-science static ZSTD markers.
RECORDED_BY=Codex automated Task 6 audit
AUTOMATED_REVIEW=Root review findings implemented and verified
HUMAN_PRODUCT_SIGN_OFF=PENDING
RECORDED_DATE=2026-07-12
