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

## Task 5 protected delta-isolation decision

The immutable reference and current ratchet are committed release inputs, not audit outputs. The
reference is bound to commit `0e91c7c4b121d80b929d595ea711d3dd0833ee67`, bundle fingerprint
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`, and normalization
profile `scienceearth-g0-source-root-normalization` version 2 with exactly two source roots. The
profile binds the sorted location-independent descriptors `github.com/anloren/osgsol` and
`github.com/anloren/osgverse`, both at subpath `.`, with root-set SHA-256
`646b5eb80be60524ca6aa8dad55921f2966cc40562b29489483f1184a04c1936`. Its generation
command records both roots as placeholders and contains no machine-home path.

| Manifest | Finding count | Canonical SHA-256 | File SHA-256 |
|---|---:|---|---|
| `v0.2.0-macos-arm64-reference.json` | 1,086 | `0c6bb7949ac789f3c24e86b0862570ebd2997e1530d2499d9caf29a1461064d4` | `6ae2c1a946bd7bcb4eecd386109dbbcab856efd2803d7815c882c6b91f200f9f` |
| `current-macos-arm64-ratchet.json` | 1,086 | `6a0298e26c9b32f5224db68770448dd213855b88df613c0891de4e3f77429967` | `dac1bbf6a0e4a2ae3831904a60174a9bbcb980614c528eeeae295de48cc97dfd` |

The ratchet references canonical reference hash
`0c6bb7949ac789f3c24e86b0862570ebd2997e1530d2499d9caf29a1461064d4`; its initial
`v0.2.0` parent-finding-set hash is
`5eeb1fc96226557050bffecfe5ab7cb11f32713fddb83c3147b1684ad675bd55`.

The policy-bearing audit visited 128 Mach-O nodes and kept all 1,086 historical non-science
identities visible: 572 external dependencies, 103 forbidden rpaths, 34 forbidden runtime
references, 77 forbidden strings, 299 static science symbols, and one static science string.
Tier A has zero science findings. Tier B has zero new identities and zero removed identities.

| Gate | Acceptance limit | Measured result | Outcome |
|---|---|---|---|
| Existing behavior | Science-off targeted suite passes unchanged | Fresh protected selection passed 15/15 in 9.81 s | PASS |
| Dependency build | Pinned private static prefix verifies | GDAL 3.13.1 / PROJ 9.8.1 / ZSTD 1.5.7 hashes, package metadata, and prefix passed | PASS |
| Tier A absolute science isolation | No external, source/build, unresolved, or main-reachable science finding | 0 absolute science findings; 0 unresolved dependencies | PASS |
| Tier B historical delta | Candidate non-science identities are a subset of the ratchet | 1,086 absolute historical identities; 0 new; 0 removed | PASS |
| Added bundle size | `<= 40 MiB` target | 21,283,735 bytes (20.30 MiB); science closure 21,283,576 bytes | PASS |
| AlphaEarth correctness and HTTP | Correct fixtures and bounded byte ranges | Prior G0 offline suite 3/3 passed; both formal live transfers stayed bounded | PASS |
| Uncached first RGB latency | Median `<= 3 s`, P95 `<= 8 s` | NVIDIA 4.23269 s / 4.69879 s; Hong Kong 3.68769 s / 4.17196 s | **FAIL** |

G0_DECISION=STOP
REASON=Delta isolation now passes Tier A and Tier B; only the independent corrected uncached AlphaEarth RGB median latency gate remains failed.
RECORDED_BY=Codex automated Task 5 protected delta-isolation audit
AUTOMATED_REVIEW=Independent normalization review findings implemented and verified with profile v2
HUMAN_PRODUCT_SIGN_OFF=PENDING
RECORDED_DATE=2026-07-12
