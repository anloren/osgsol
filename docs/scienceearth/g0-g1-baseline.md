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
The reference also records deterministic generation-boundary toolchain provenance:
AppleClang `21.0.0` and macOS SDK `26.5` build `25F70`. These values are schema-validated,
path-free identities captured by the guarded generator; cross-machine release validation checks
the committed canonical bytes through the independent executable hashes below rather than
re-probing the validator host's current toolchain.

| Manifest | Finding count | Canonical SHA-256 | File SHA-256 |
|---|---:|---|---|
| `v0.2.0-macos-arm64-reference.json` | 1,086 | `145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada` | `afd80d8ad9419882793926d781affb970246358fbf74c514b950bf0eb924a5a6` |
| `current-macos-arm64-ratchet.json` | 1,086 | `fd2d67424356637dd71beb4120647726836fb9a9b3cec03223bb83378fe960cb` | `a732bacfec5b16b6e42ef4b4573827df6edfa73a5d8bf5b2c7b94d0d684c06cc` |

The ratchet references canonical reference hash
`145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada`; its initial
`v0.2.0` parent-finding-set hash is
`5eeb1fc96226557050bffecfe5ab7cb11f32713fddb83c3147b1684ad675bd55`.
`g0_manifest.py` independently pins both canonical manifest hashes and the release validator and
bundle-audit CLI accept only the two canonical committed paths. The ratchet hash is therefore an
anti-rollback ceiling, not a value supplied by the manifest pair itself.

The policy-bearing audit visited 128 Mach-O nodes and kept all 1,086 historical non-science
identities visible: 572 external dependencies, 103 forbidden rpaths, 34 forbidden runtime
references, 77 forbidden strings, 299 static science symbols, and one static science string.
Tier A has zero science findings. Tier B has zero new identities and zero removed identities.

| Gate | Acceptance limit | Measured result | Outcome |
|---|---|---|---|
| Existing behavior | Science-off targeted suite passes unchanged | Not rerun after the formal optimized latency STOP; last protected selection passed 15/15 in 9.81 s | BLOCKED |
| Dependency build | Pinned private static prefix verifies | Not rerun after STOP; last GDAL 3.13.1 / PROJ 9.8.1 / ZSTD 1.5.7 verification passed | BLOCKED |
| Tier A absolute science isolation | No external, source/build, unresolved, or main-reachable science finding | Not rerun after STOP; last audit had 0 absolute science findings and 0 unresolved dependencies | BLOCKED |
| Tier B historical delta | Candidate non-science identities are a subset of the ratchet | Not rerun after STOP; last audit had 1,086 historical identities, 0 new, 0 removed | BLOCKED |
| Added bundle size | `<= 40 MiB` target | Not rerun after STOP; last candidate added 21,283,735 bytes (20.30 MiB) | BLOCKED |
| AlphaEarth correctness and HTTP | Correct fixtures and bounded byte ranges | Fresh offline suite 3/3 passed; both A/B profiles preserved exact bounded ranges, byte budgets, and zero retries | PASS |
| Uncached first RGB latency | Median `<= 3 s`, P95 `<= 8 s` | Optimized NVIDIA 3.058162792 s / 3.269105875 s; Hong Kong 2.660813916 s / 2.841004208 s | **FAIL** |

G0_DECISION=STOP
REASON=The formal separate-process optimized profile preserves correctness and bounded transfers, but the NVIDIA HQ uncached first-RGB median is 3058.162792 ms, exceeding the 3000 ms hard limit by 58.162792 ms.
RECORDED_BY=Codex automated Task 5 formal separate-process A/B latency run
AUTOMATED_REVIEW=Optimized hard gate failed; downstream full G0 gates were not rerun and readiness was not asserted
HUMAN_PRODUCT_SIGN_OFF=PENDING
RECORDED_DATE=2026-07-12

## Concurrent metadata prefetch Task 4 corrected preflight

The historical
`0926eff5871c9e8313715c08349293e74db4aef9b9d9cde224743e831b605a6e` Desktop tree digest is
invalid as a preflight baseline because no reproducible helper invocation was preserved for it.
The first Task 4 STOP based on that value is withdrawn. Zero public processes had started and no
diagnostic or formal output root existed, so this correction does not constitute a rerun.

Two fresh, separate invocations of the exact committed
`ScienceProbeBuilderTests.tree_digest()` helper both returned
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214` for 414 recursive entries
and 355 regular non-symlink files. The protected canonical fingerprint was exactly
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18` before and after those
calculations. This reproducible fingerprint/tree pair is the corrected Task 4 preflight baseline.

The corrected preflight then passed on clean commit
`905044238fda6b93f26b54ebf6a74da0dfefbeea`. The optimized control and prefetch candidate each
ran exactly once, control first, in separate processes and output paths. The control summary
passed: NVIDIA median/P95 was `2894.516583/3768.248083 ms` and Hong Kong was
`2711.594750/3553.175208 ms`, with zero retries and bounded transfers. Its SHA-256 is
`f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096`.

The candidate exited `1` during NVIDIA iteration 1 with
`VSINetworkStats GET operations disagree with CPL read operations`. Its atomic summary contains
zero completed cases, has status `ERROR`, and hashes to
`1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`. Partial raw evidence
recorded overlapping HTTP/2 HEAD and exact first `bytes=0-131071` Range emission, 5,010,811
downloaded bytes, and no retry, but it did not produce the required complete parsed proof,
accepted correctness result, latency result, or Hong Kong sample. The diagnostic is therefore
rejected without resampling. The formal profile remains `optimized`, and no formal or downstream
automated gate ran.

G0_DECISION=STOP
REASON=The one-shot prefetch candidate exited during NVIDIA iteration 1 because VSINetworkStats GET operations disagreed with CPL read operations; its summary is ERROR with zero completed cases.
RECORDED_BY=Codex automated Task 4 isolated public diagnostic
G0_AUTOMATED_GATES=NOT_RUN
PUBLIC_PREFETCH_DIAGNOSTIC=FAIL
HUMAN_PRODUCT_SIGN_OFF=PENDING
DESKTOP_PACKAGE=NOT_READY
RECORDED_DATE=2026-07-13

## Remediation Task 3 public requalification v2 decision

Evidence classification: named summary/raw/stats/proof hashes and contents, plus current
filesystem file counts, mtimes, and absence, are independently inspectable. Exact preflight
result/timestamp, process exit statuses, process counts/order/no-rerun, candidate console
timing/phase lines, and final-verifier result are contemporaneous operator console observations
only. Their stdout, preflight, and final-verifier transcripts were not preserved, so they are not
independently hash-verifiable.

Contemporaneous operator console observation (preflight transcript not preserved): the binding
snapshot reported PASS at `2026-07-13T10:30:33+0800` on clean commit
`ea96c86ee2668ab701f6801293edb1d1d2bae5e7`, with all six immutable hashes, protected Desktop
fingerprint/helper digest/counts, credential-free loopback HTTP proxy class, and absent v2 roots
matching the authorized baseline. This before-public-use claim is not independently
hash-verifiable.

Contemporaneous operator console observation (process stdout not preserved): exactly one
optimized control process and one enforced prefetch candidate process ran in that order, with no
reruns; the control completed 10/10 and exited `0`. The process count/order/no-rerun and exit
status are not independently hash-verifiable. Independently, the hashed control summary status is
`FAIL`: NVIDIA median/P95 is `3475.268042/3771.631000 ms`, so the median exceeds `3000 ms`; Hong
Kong is `2747.161708/2924.877542 ms`. Its SHA-256 is
`0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0`.

Current filesystem counts show five NVIDIA and four Hong Kong candidate proof files. The hashed
Hong Kong iteration 5 raw artifact records an overlapping initial HTTP/2 Range returning `500`
with a 17-byte body, and no proof file exists for that iteration. Contemporaneous operator console
observation (process stdout not preserved): the candidate exited `1` and printed
`transient HTTP responses do not reconcile with CPL retry events`. That exit status and exact
diagnostic are not independently hash-verifiable. The hashed atomic summary is `ERROR` with zero
cases and SHA-256
`1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`. The complete candidate
proof is therefore absent. Under the operator-recorded no-rerun decision above, this sample set is
frozen; the no-rerun history is not independently hash-verifiable.

Formal promotion was forbidden. Current tracked CTest profile remains `optimized`, and the
current filesystem has no v2 formal root. Contemporaneous operator console observation
(stdout/final-verifier transcript not preserved): formal/downstream gate process counts were zero,
the protected Desktop was not packaged or replaced, and no tag, push, G1 work, or threshold change
occurred. Those process/no-action statements are not independently hash-verifiable.

G0_DECISION=STOP
REASON=Hashed raw/stats and the absent proof establish an incomplete Hong Kong iteration 5 after HTTP 500; an unpreserved contemporaneous operator console observation recorded exit 1 and a retry-reconciliation diagnostic; the hashed atomic summary is ERROR with zero cases.
RECORDED_BY=Codex automated remediation Task 3 public requalification v2
G0_AUTOMATED_GATES=NOT_RUN
AUTOMATED_GATE_PROCESS_COUNT_EVIDENCE=CONTEMPORANEOUS_OPERATOR_CONSOLE_OBSERVATION_ONLY; TRANSCRIPT_NOT_PRESERVED; NOT_INDEPENDENTLY_HASH_VERIFIABLE
PUBLIC_REQUALIFICATION_V2=FAIL
HUMAN_PRODUCT_SIGN_OFF=PENDING
DESKTOP_PACKAGE=NOT_READY
RECORDED_DATE=2026-07-13

## Prefetch v3 Task 3 one-shot public requalification decision

The v3 binding preflight passed on clean commit
`e2d1f17e2fc958f6625971909dbcac9bfccabe4d`: all old/v2 bindings matched, the protected Desktop
remained at fingerprint/helper/counts
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18` /
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214` /
`414 / 355`, the formal test remained `optimized`, and every v3 root was absent. The durable
preflight transcript hashes to
`d48994e70732786983b9c5d2f9c67bb2ca0c936d0b25e69c7acefd694fbe6aff`.

The optimized control and prefetch candidate then ran once each, in that order, with no rerun.
The control exited 0 and completed 10/10 proofs, but its diagnostic summary was `FAIL`: NVIDIA
median/P95 were `3691.365250 / 3852.527208 ms`, while Hong Kong median/P95 were
`2912.941459 / 3358.329875 ms`. Its transcript and summary hashes are respectively
`a1d8e36c152f71771e1ad80ae548e43e397456ce6ea47fe03e50e2cad9b2d142` and
`47abb0bc91e4afba412d1150822b90110650f8e225b45b7fa52f4627889c405c`.

The candidate exited 1 during NVIDIA iteration 2. Iteration 1 was a valid zero-fallback proof in
`2813.06 ms`. Iteration 2's overlapping shared-HTTP/2 first Range received `500` with 17 bytes;
the authoritative coordinator-fallback event and three separate ordinary CPL retries reconciled,
then the formal proof rejected the sample with `metadata prefetch formal proof contains a
fallback`. The atomic candidate summary is `ERROR` with zero cases. Only 1/10 proof files and no
Hong Kong iteration exist. Candidate transcript and summary hashes are respectively
`c7f98c3e8428cb72092e6e49a1fff359232c4b4463607e18d52d55aac1b907d2` and
`1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`.

The candidate therefore failed the complete-proof and zero-fallback gates. Formal promotion was
forbidden: `tests/CMakeLists.txt` remains unchanged at `--profile optimized`, the v3 formal root
is absent, and formal/downstream process counts are zero. The protected Desktop was not packaged
or replaced; no tag, push, G1 work, threshold/retry change, or old/v2 evidence mutation occurred.

G0_DECISION=STOP
REASON=The one-shot v3 prefetch candidate encountered one explicitly accounted coordinator HTTP 500 fallback on NVIDIA iteration 2 and was correctly rejected by the formal no-fallback gate after producing only 1/10 proofs.
G0_AUTOMATED_GATES=NOT_RUN
PUBLIC_REQUALIFICATION_V3=FAIL
HUMAN_PRODUCT_SIGN_OFF=PENDING
DESKTOP_PACKAGE=NOT_READY
RECORDED_DATE=2026-07-13

## Bounded-retry v4 recorded decision

The immediate authorization snapshot passed at `2026-07-13T20:28:20+0800` on `7a0abbc5212f88e191a0b2279c78d8cc84eb4422`: both proxy classes were credential-free loopback HTTP, all historical/private/Desktop bindings matched, the formal profile was still optimized, and all five v4 output paths were absent. The preserved transcript hashes to `2bae6ec0237839cc14cdde0f90c8084b89b46336ecee20ebaf4598b7d6d2f22b`.

The recorded ledger contains one preserved optimized-control transcript/artifact set and one preserved enforced-prefetch-candidate transcript/artifact set, with unique expected timestamps and hashes. The control transcript carries exit marker 0 and its ten complete proofs fail both median gates: NVIDIA `3391.137834 ms`, Hong Kong `3231.595583 ms`. The candidate transcript carries exit marker 0 and its ten frozen proofs pass every correctness/range/HTTP2/shared-connection/overlap/publication/byte gate with zero terminal fallback. Its NVIDIA median/P95 is `2896.292875 / 3360.568375 ms`; Hong Kong is `2313.934292 / 3459.563000 ms`. Hong Kong iteration 1 recovered one bounded coordinator HTTP/2 500 retry; all retry/request/byte accounting reconciles.

Candidate PASS authorized CMake-only promotion commit `bede4b09c93d2383e7467000538bae1c7b03dbcd`. The recorded formal ledger contains one preserved transcript/artifact set with exit marker 8 and ten complete proofs. Hong Kong passes at `2474.804959 / 2829.896208 ms`, and NVIDIA P95 passes at `3376.432625 ms`, but NVIDIA median `3207.465916 ms` exceeds the fixed `3000 ms` gate by `207.465916 ms`. The ledger reports no formal rerun and zero downstream executions; the final scoped filesystem contains no matching downstream artifacts or tracked downstream changes. Absolute absence of omitted or overwritten reruns/downstream executions cannot be proven from final filesystem state.

All 93 v4 summary/raw/stats/proof files are mode `0400` and their evidence roots mode `0500`. The original ignored helpers are not immutable promotion-time oracles because their exact bytes were not Git-anchored. The executable source committed in the measurement record performs a post-hoc, fail-closed recomputation from the frozen artifacts, hard-coded hashes, immutable Git objects, and the current protected Desktop fingerprint/helper/count tuple. It establishes the substantive STOP decision but not negative process history. The v4 evidence set remains immutable and exhausted.

G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V4=FAIL
DESKTOP_PACKAGE=NOT_READY

## Immediate multi-range retry v5 renewed offline authorization

The frozen v4 STOP remains the control. Exact v5 implementation base
`38203e62ae02e8992cb7214cfdab8634b3c3f5ba` passed the complete non-public gate on
2026-07-14: Python 66/66, refreshed science-off core 20/20 including `ScienceBuildContract`,
selected science 9/9 including `ScienceHttpRangesCoordinatorAbandonment` and
`ScienceHttpRangesBlockedOperationCapacity`, private builder contract, and private-prefix
verification, with zero skips/failures. The renewed patch/pin is
`0e67079267a4adfc316f20c88ba22cf078e5d653d2f35c2e3b54fff7efd0de1e`; its 256-entry capacity
fails closed on overflow without evicting a live blocked-operation record.

The disposable probe passed the canonical audit with Tier A `0`, Tier B new/removed `0/0`, zero
unresolved dependencies, delta `21,304,855` bytes, and science closure `21,304,696` bytes. Its
single export, strict/deep signature, direct system dependencies, and forbidden-path scan passed.
The fixed Desktop app was not rebuilt or replaced and retained fingerprint/helper/counts
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18 /`
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214 / 414 / 355`.

The committed v5 verifier binds the implementation parent, exact three-document authorization
scope, patch/prefix/archive provenance, offline result logs and dynamic totals, every historical
evidence tree and mode, the corrected v4 post-hoc source/base-wrapper/transcript, exact-five retry
codes, the 256-entry/no-live-eviction capacity contract, protected source diff, formal
`prefetch`/v4 path, and the absent/non-symlink v5 candidate, summary, and formal paths. The earlier
base-bound authorization is superseded by this renewed authorization. No public candidate/formal
process, Desktop update, G1, tag, push, or release occurred in this task.

G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V5=AUTHORIZED_NOT_RUN
DESKTOP_PACKAGE=NOT_READY
