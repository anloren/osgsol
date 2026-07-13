# ScienceEarth G0 measurements

Date: 2026-07-13 (CST)

Status: **STOP at the concurrent metadata-prefetch Task 4 diagnostic.** The corrected preflight
passed and the authorized control and candidate each ran exactly once in separate processes and
paths. The control passed. The candidate stopped during its first NVIDIA iteration because its
VSINetworkStats GET-operation count disagreed with the CPL read-operation count. The candidate
summary is `ERROR` with no completed case, so no formal profile was promoted and no formal or
downstream automated gate ran. This is not G0 readiness and must not be interpreted as permission
to package the Desktop app or start G1.

## Reference setup

- Machine: Apple M4 Pro, arm64, 48 GiB RAM
- OS: macOS 26.5.2 (25F84)
- Compiler: AppleClang 21.0.0.21000101, Release
- GDAL / PROJ / ZSTD: private static prefix, GDAL 3.13.1 / PROJ 9.8.1 / ZSTD 1.5.7
- Formal separate-process A/B run: 2026-07-12, 19:15-19:16 CST
- Network: public Source Cooperative fixtures through the active system route; timings were not
  normalized against a separate throughput sample.

## Offline and TDD evidence

The initial spike compiled and failed at link time on the intended missing GDAL symbols. The
initial local range client failed because the instrumented server was deliberately absent. Further
RED checks rejected the old fixture schema, missing request lifecycle events, missing
`VSINetworkStats`, and a transient HTTP 500 before the bounded retry parser was implemented.

The final tests verify:

- `osgVerse_Test_ScienceGdalSpike`: manual GTiff/VRT/MEM registration; 64-band signed Int8,
  tiled ZSTD GTiff; 2x/4x overviews; A01/A16/A09 order; exact signed dequantization; NoData mask
  versus valid zero; a trusted `/vsimem` vertical-flip VRT; mirrored pixels; and an explicitly
  selected bounded overview window.
- `osgVerse_Test_ScienceHttpRanges`: the 4,876,194-byte local fixture uses five single-range
  HTTP 206 GETs and transfers 76,161 actual body bytes against a 1,048,576-byte budget. The
  metadata HEAD is HTTP 200 with no response body. The server logs paired start/completion events,
  planned/actual/committed/reserved bytes, partial writes, and violations. A forced second-chunk
  failure proves that only the first completed 65,536-byte chunk is committed and the reservation
  returns to zero.
- The curl/CPL parser accepts only retry codes 429/500/502/503/504, binds retries to the exact CPL
  range record, requires the same range to be emitted again and end in matching HTTP 206, and
  permits at most three retry events per range. Regression fixtures cover `500 -> same Range ->
  206`, an unlisted status, more than three retries, out-of-order HTTP/2 multiplex responses, and
  an explicit ranged GET receiving HTTP 200.
- `successful_range_bytes` must equal GDAL `VSINetworkStats` and is the observed
  `actual_http_body_bytes`. A transient `Content-Length` is only
  `declared_transient_bytes`; it is not treated as observed. The transfer budget uses
  `conservative_body_upper_bound_bytes = successful + declared transient`.
- Offline georeference regressions verify unclipped EPSG:32610 and area-of-use-clipped EPSG:32650
  footprints, plus rejection of mismatched CRS, affine/UTM bounds, and WGS84 bbox.

Fresh offline Task 5 CTest result: 3/3 passed. The science-off AEF index, dependency-script, and
build-contract regressions pass 3/3. The two pinned raw
index rows and SHA-256 fingerprints validate without network access.

## Formal separate-process A/B AlphaEarth evidence

The optional live test is registered only with `OSGSOL_SCIENCE_NETWORK_TESTS=ON`. The committed
fixture file SHA-256 is
`6a67af9a1380250704b9032f8b4933965196ed2a119f07be4cb99dafe032806d`; its pinned source-index
SHA-256 is `f738e7d274ad582e56e20a3a8b444c6f2a3ece5781f8f9855bb7ca3d9ed2942f` and the
NVIDIA/Hong Kong raw-record fingerprints are respectively
`03956d76d3bc0d67c4f61608a1771ffb036a7e3ece6df326a2b52e66d412a0f1` and
`deded3c50b4a91db0fe3fe97691734c7fa5a802647c1571d28f75515587a59ff`. Fresh fixture validation
accepted exactly those two raw rows before the A/B run.

Each iteration clears the VSICurl cache, opens one pinned COG, validates the 8192x8192x64 signed
Int8 source and pinned raw CRS, derives the affine transform and source window, verifies the
densified WGS84 footprint, selects the exact 4x overview for A01/A16/A09, and validates orientation,
mask, and signed dequantization. Baseline and optimized were separate executable processes because
GDAL latches chunk/cache behavior. Both used `GDAL_HTTP_MULTIPLEX=YES`, consecutive-range merging,
HEAD metadata, a 16,777,216-byte VSICurl cache and per-iteration transfer budget, and the bounded
retry policy `429,500,502,503,504`, at most three retries. The only profile differences were:

| Profile | `GDAL_HTTP_MULTIRANGE` | `CPL_VSIL_CURL_CHUNK_SIZE` | RGB implementation |
|---|---|---:|---|
| Baseline | `YES` | 16,384 | three-band oracle reads |
| Optimized | `PARALLEL` | 131,072 | one batched A01/A16/A09 read |

The summaries were atomically published with no temporary sibling left behind. Profile-prefixed
curl/CPL logs, GDAL network statistics, and parsed proof JSON preserve every iteration.

| Summary | SHA-256 | JSON status |
|---|---|---|
| `build/science_g0/science-network-evidence/baseline-summary.json` | `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed` | `FAIL` (not enforced) |
| `build/science_g0/science-network-evidence/live-summary.json` | `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe` | `FAIL` (enforced) |

### Latency and phase evidence

| Profile / case | Iterations (ms) | Median | P95 | Median open / georef / read / close (ms) | Gate |
|---|---|---:|---:|---:|---|
| Baseline NVIDIA HQ | 5064.970583, 4411.434250, 3724.519916, 4127.744666, 4114.368875 | 4127.744666 | 5064.970583 | 1528.219458 / 1.219291 / 2601.443458 / 0.353291 | FAIL median |
| Baseline Hong Kong | 4037.869125, 3746.480708, 3815.077292, 4474.028458, 3781.300459 | 3815.077292 | 4474.028458 | 1469.435417 / 1.258416 / 2349.340417 / 0.380708 | FAIL median |
| Optimized NVIDIA HQ | 3213.307791, 3269.105875, 3058.162792, 2993.688625, 2889.297541 | **3058.162792** | 3269.105875 | 1738.541666 / 1.039500 / 1169.100834 / 0.172167 | **FAIL median by 58.162792 ms** |
| Optimized Hong Kong | 2841.004208, 2660.813916, 2658.137375, 2693.983666, 2530.803292 | 2660.813916 | 2841.004208 | 1840.688458 / 1.286125 / 818.607916 / 0.324833 | PASS |

The optimization reduced NVIDIA median/P95 by `1069.581874/1795.864708 ms` and Hong Kong by
`1154.263376/1633.024250 ms`. In the remaining optimized latency, `open` is the dominant median
phase for both cases: 1738.541666 ms for NVIDIA and 1840.688458 ms for Hong Kong.

### Request and byte evidence

All counts below are exact summary totals across five iterations; the parenthesized values are per
iteration. Successful range bytes equal actual HTTP body bytes in every iteration. Declared
transient bytes and retries are zero, so the conservative upper bounds equal the successful totals.

| Profile / case | GET / HEAD / stats GET ops | Successful / actual / declared / conservative total bytes | Per-iteration bytes | Source size |
|---|---:|---:|---:|---:|
| Baseline NVIDIA HQ | 40 / 5 / 25 (8 / 1 / 5) | 24,562,535 / 24,562,535 / 0 / 24,562,535 | 4,912,507 | 3,700,007,174 |
| Baseline Hong Kong | 25 / 5 / 25 (5 / 1 / 5) | 6,062,080 / 6,062,080 / 0 / 6,062,080 | 1,212,416 | 2,352,783,157 |
| Optimized NVIDIA HQ | 35 / 5 / 10 (7 / 1 / 2) | 25,054,055 / 25,054,055 / 0 / 25,054,055 | 5,010,811 | 3,700,007,174 |
| Optimized Hong Kong | 20 / 5 / 10 (4 / 1 / 2) | 6,320,460 / 6,320,460 / 0 / 6,320,460 | 1,264,092 | 2,352,783,157 |

The exact successful inclusive byte-range sets were stable across all five iterations; only
parallel completion order varied:

- Baseline NVIDIA HQ: `0-16383`, `65536-81919`, `71982535-72822510`,
  `73542206-74331519`, `96968877-97740801`, `98430589-99197436`,
  `117442209-118296807`, `119129892-119986968`.
- Optimized NVIDIA HQ: `0-131071`, `71982535-72822510`, `73542206-74331519`,
  `96968877-97740801`, `98430589-99197436`, `117442209-118296807`,
  `119129892-119986968`.
- Baseline Hong Kong: `0-16383`, `65536-81919`, `46022656-46415871`,
  `62193664-62570495`, `75218944-75628543`.
- Optimized Hong Kong: `0-131071`, `46029923-46405871`, `62196464-62556994`,
  `75229143-75625682`.

Every actual GET was a successful matching single-range 206. The 200 codes were the metadata HEAD
and proxy CONNECT where applicable; there was no GET 200, comma range, full-object response,
partial response, transient retry, georeference/orientation/band/dequantization error, or byte-budget
violation. NVIDIA used EPSG:32610, affine `[581920,10,0,4096000,0,10]`, raw pixel
`(989.080249916486,4050.089574773959)`, and raw window `(x=860,y=3924,size=256)`. Hong Kong used
EPSG:32650, affine `[172320,10,0,2457600,0,10]`, raw pixel
`(3607.668022183540,1330.670897266608)`, and raw window `(x=3476,y=1204,size=256)`.

The enforced optimized summary is `FAIL` solely because NVIDIA's median exceeds 3000 ms. Both P95
values pass 8000 ms and Hong Kong passes both thresholds. Per the plan, no cache warming, fixture or
iteration change, threshold relaxation, rerun, or additional optimization was attempted. The full
G0 regression, exact two-root policy audit, dependency verifier, signature verification, and
protected science-off selection were not rerun after this hard stop. `G0_DECISION=STOP`.

## Test-only probe closure

`osgdb_science_g0_probe.so` is a test-only, non-installed Mach-O arm64 bundle. The fresh Task 5
build is 21,283,576 bytes with SHA-256
`664b284db20530a28586a573d01f78e4b8bd244075617936a77596f6298131ba`.
It links the private `libgdal.a`, `libproj.a`, and `libzstd.a` plus SDK curl and SQLite.

`nm -gU` exposes exactly `_osgsol_science_g0_probe_anchor`. `otool -L` lists only system curl,
System, SQLite, and libc++; there is no `LC_RPATH` and no install rule. The link command inherits
an unused `-L/opt/homebrew/lib` from the wider project, but no Homebrew library or RPATH enters the
bundle. The private dependency verifier and linked-probe scan contain no private dependency
workspace root.

## Protected reference generation and normalization provenance

The first guarded generation exposed 37 historical subjects beginning with a doubled
`//Users/USER` slash. The residual-home guard had checked only a single leading slash. No such
manifest was accepted or committed. Focused RED tests proved both normalization and publication
could be bypassed; the fixed tests and full audit/manifest suite passed before commit `06887b9`.

A corrected one-root pre-acceptance pair then passed chain and home-path validation but the first
formal policy audit correctly stopped: Tier A passed, while Tier B reported exactly 74 new and 74
removed `forbidden_string` identities. The bytes were historical; the reference had normalized the
osgSol worktree as `${HOME}/osgsol/...`, while the formal two-root audit normalized the same paths
as `${SOURCE_ROOT}/...`. The rejected file hashes were:

| Rejected one-root file | SHA-256 |
|---|---|
| `v0.2.0-macos-arm64-reference.json` | `f196c693a70f3a775c76eea7a966a671ef45315b58fc5c51d9bcec72d3a38f52` |
| `current-macos-arm64-ratchet.json` | `25b12045ee44dbeba09cfde440955f6a3d49b59635ed928ed9b0787f313abba4` |

Those two uncommitted files alone were deleted with reason: `initial reference failed first policy
audit due normalization-profile mismatch`. No audit fallback, ratchet relaxation, or hand edit was
used. Commit `94c95c2` added a required normalization profile with exact schema, version, and
source-root count; the policy CLI now stops before identity comparison for a missing, malformed,
wrong-version, wrong-schema, or count-mismatched profile.

The explicitly approved pre-acceptance generation was then run once with both formal audit roots:

```bash
python3 packaging/scienceearth/generate_g0_reference.py \
  --app '/Users/USER/Desktop/osgSol Earth.app' \
  --reference packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json \
  --ratchet packaging/scienceearth/baselines/current-macos-arm64-ratchet.json \
  --boundary-tag ScienceEarth \
  --release-tag v0.2.0 \
  --source-root /Users/USER/osgverse \
  --source-root /Users/USER/osgsol/.worktrees/v0.2-runtime-safety
```

The initial committed profile was `scienceearth-g0-source-root-normalization` version 1 with
`source_root_count=2`. Both normalized generation-command roots were placeholders. Reference and
initial ratchet each contained 1,086 identities; neither manifest contained `/Users/USER` or
`//Users/USER`.

| Committed profile-v1 manifest | Canonical SHA-256 | File SHA-256 |
|---|---|---|
| Reference | `da541f190e0679b3e3b67234b1d100d7d20caa7aa3eccbaa0177040930b83f4d` | `d08bb3e27b240a6d371e8941b01e9d41eea09b91ee9ea8bb6a6607b744b41a81` |
| Ratchet | `439dadb3ba459785c573ef3ca09401e9e1a6e429a8b6a96e869998318af08c41` | `5ab42ff0ceeb361732c98f15d07354d0e8cb7ae04e0d2b83f35e7c821c869a22` |

Independent review found that count alone did not bind which two roots defined `${SOURCE_ROOT}`
identities, and that direct `audit_bundle()` callers bypassed the CLI-only profile check. The
explicit pre-release migration retained history, recorded the v1 hashes above, removed/recreated
only the two tracked manifests through the guarded generator, and committed profile v2 in
`f976c1c`.

Profile v2 canonicalizes `remote.origin.url` across HTTPS, SSH URL, and scp forms by stripping
credentials, scheme, leading slash, and trailing `.git`, while lowercasing the host. Every root
must be inside a Git worktree and have a canonical remote; duplicate descriptors stop generation.
The current sorted descriptors are:

```json
[
  {"repository": "github.com/anloren/osgsol", "subpath": "."},
  {"repository": "github.com/anloren/osgverse", "subpath": "."}
]
```

Their canonical SHA-256 is
`646b5eb80be60524ca6aa8dad55921f2966cc40562b29489483f1184a04c1936`. The profile stores
both inspectable descriptors and this hash. `validate_reference()` requires the exact v2 shape and
self-consistent hash; `audit_bundle()` recomputes the descriptor set from its actual source roots.
The CLI uses that same boundary, while the release/chain contract rejects missing, malformed, or
descriptor-tampered profiles.

| Current profile-v2 manifest | Canonical SHA-256 | File SHA-256 |
|---|---|---|
| Reference | `145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada` | `afd80d8ad9419882793926d781affb970246358fbf74c514b950bf0eb924a5a6` |
| Ratchet | `fd2d67424356637dd71beb4120647726836fb9a9b3cec03223bb83378fe960cb` | `a732bacfec5b16b6e42ef4b4573827df6edfa73a5d8bf5b2c7b94d0d684c06cc` |

The guarded generator records path-free, deterministic generation-boundary provenance for
AppleClang `21.0.0` and macOS SDK `26.5` build `25F70`. The reference schema requires this exact
field shape. Release/audit validation remains cross-machine deterministic because it checks the
committed canonical bytes against independent executable reference and ratchet anchors; it does
not substitute the validator host's current compiler or SDK identity.

The immutable reference source commit is
`0e91c7c4b121d80b929d595ea711d3dd0833ee67`; its protected bundle fingerprint is
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`. The ratchet's
reference hash matches the canonical reference hash, and its initial parent-finding-set hash is
`5eeb1fc96226557050bffecfe5ab7cb11f32713fddb83c3147b1684ad675bd55`.

## Protected baseline and disposable candidate

The disposable `build/science_g0/osgSol Science G0 Probe.app` was rebuilt from the protected
Desktop baseline, replacing only the main executable and adding the test-only probe as
`Contents/lib/osgPlugins-3.6.5/osgdb_science.so`. It was ad-hoc signed and passed
`codesign --verify --deep --strict`. The builder also recomputed the canonical protected bundle
fingerprint before and after. The earlier record named
`0926eff5871c9e8313715c08349293e74db4aef9b9d9cde224743e831b605a6e` as an independent
file-tree digest without preserving a reproducible helper calculation. Task 4 review therefore
invalidated that value as unproven; it is not a protected baseline.

## Task 5 policy-bearing bundle audit

The formal audit used the same two normalization roots as the accepted reference:

```bash
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

The recursive audit visited all 128 Mach-O executables, dylibs, and plugins. Its JSON and readable
graph are `build/science_g0/bundle-audit.json` and `build/science_g0/bundle-audit.txt`.

| Measurement | Result |
|---|---:|
| Protected baseline size | 542,594,200 bytes (517.46 MiB) |
| Disposable probe size | 563,877,935 bytes (537.76 MiB) |
| Added size | 21,283,735 bytes (20.30 MiB) |
| Science-only closure | 21,283,576 bytes (20.30 MiB), probe plugin only |
| Tier A absolute science findings | 0 |
| Tier B new non-science identities | 0 |
| Tier B removed non-science identities | 0 |
| Historical non-science identities | 1,086 |
| Unresolved dependencies | 0 |
| Historical external dependencies | 572 |
| Historical forbidden rpaths | 103 |
| Historical forbidden runtime references | 34 |
| Historical forbidden strings | 77 |
| Historical static science symbols / strings | 299 / 1 |

Both immutable size tiers pass. Tier A passes absolutely: the science-only closure adds no
external dependency, forbidden runtime/source/build lookup, unresolved install name, or
main-reachable science edge. Tier B passes by exact identity: the candidate non-science set equals
the ratchet ceiling, with no new or removed identity. The 1,086 historical findings remain fully
visible and are not reclassified as clean.

The last profile-v2 verification before this A/B task passed the combined audit/manifest unit
suites 57/57 in 2.73 seconds, the private dependency builder contract, the release
boundary/manifest contract, the pinned private prefix verifier, and deep strict app signature
verification. The last protected science-off selection passed 15/15 in 9.81 seconds. The formal
optimized latency failure stopped this task before those gates could be rerun, and the historical
passes do not override the independent current median-latency hard gate.

G0 remains `STOP`. Delta isolation is no longer a blocker; only uncached first-RGB median latency
remains failed, and human product sign-off is still pending. G1 has not started.

## Concurrent metadata prefetch prototype isolation proof

On 2026-07-13, the opt-in parallel HEAD/Range prototype was rebuilt and audited locally without a
public-network run. The guarded clean build removed only the repository-owned
`build/science-deps-prefetch` root, seeded the three checksum-verified source archives from the
existing local cache, and rebuilt with four jobs in 136.87 seconds. The standalone verifier then
passed. The prefix measured 52,356 KiB (51 MiB), contained 404 regular files and 52 symlinks, and
produced manifest SHA-256
`a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116`.
The resolved prefetch patch SHA-256 was the pinned
`d545c492eda3a7c9c7faa4ed06334dfd0723c50aa99ca5f62cb6c7ae664255e8`.

The rebuilt runtime probe reported raster drivers `GTiff`, `MEM`, and `VRT`, OGR driver `MEM`, and
only `/vsicurl/` as an active remote VFS. Its ZSTD GeoTIFF, VRT, MEM, and PROJ warp checks passed;
COG and GNM remained inactive. The complete non-public regression set also passed:

| Regression | Result |
|---|---:|
| Manifest and bundle-audit Python suites | 66/66 passed |
| Private dependency builder contract | passed |
| `build/osgsol_core` CTest | 16/16 passed |
| Selected `build/science_g0_prefetch` release/index/manifest/dependency/GDAL/local HTTP CTest | 7/7 passed |

The disposable probe was relinked against the clean prefetch prefix, copied into
`build/science_g0_prefetch/osgSol Science G0 Probe.app`, ad-hoc signed, and strictly verified. Its
plugin SHA-256 was
`276f66d1814a032b9748d7ce96038b428771af60744e1840e1e820d1997d9c7c`.
The canonical two-root policy audit visited 128 Mach-O nodes and returned `PASS`:

| Isolation measurement | Result |
|---|---:|
| Protected baseline size | 542,594,200 bytes |
| Disposable probe size | 563,878,831 bytes |
| Added size | 21,284,631 bytes (20.30 MiB) |
| Science-only closure | 21,284,472 bytes, probe plugin only |
| Tier A absolute science findings | 0 |
| Tier B new / removed identities | 0 / 0 |
| Unresolved dependencies | 0 |
| Historical non-science identities still visible | 1,086 |

`nm -gU` exposed only `_osgsol_science_g0_probe_anchor`. The linked plugin contained no workspace
or `science-deps-prefetch` path in its load commands, dependencies, or strings. The protected
Desktop canonical fingerprint remained
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`. The historical
`0926eff5871c9e8313715c08349293e74db4aef9b9d9cde224743e831b605a6e` tree-digest statement
was not backed by a reproducible helper invocation and is invalid as a baseline. The Task 4
correction below establishes the reproducible value.

The previous formal evidence was not rewritten. Its hashes still exactly match the protected
values:

| Protected summary | SHA-256 |
|---|---|
| Baseline | `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed` |
| Optimized | `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe` |

`git diff` contained no path below `build/science_g0/science-network-evidence`. No public
diagnostic, formal rerun, Desktop replacement, packaging/update, tag, push, or G1 work occurred.
The isolated prototype is ready only for the separately authorized public diagnostic.

G0_DECISION=STOP
PUBLIC_PREFETCH_DIAGNOSTIC=PENDING_AT_TASK_3
DESKTOP_PACKAGE=NOT_READY

## Concurrent metadata prefetch Task 4 corrected preflight baseline

Independent review found that the first Task 4 STOP had treated the historical `0926eff...`
string as immutable even though no reproducible helper invocation supported it. Zero public
control, candidate, or formal processes had started, and neither the diagnostic nor formal output
root existed, so correcting the preflight does not resample or discard any public observation.

At `2026-07-13T02:30:07+0800`, two fresh, separate Python processes imported and invoked the exact
committed `ScienceProbeBuilderTests.tree_digest()` helper against the protected Desktop. Both
returned
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214`. The helper contract
hashes sorted recursive relative paths plus every regular non-symlink file's bytes. Both scans
covered 414 entries and 355 regular non-symlink files. The canonical fingerprint was
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18` immediately before and
after the two helper processes. No Desktop mutation was observed.

The reproducible Task 4 protected Desktop baseline is therefore:

| Protected Desktop check | Established value | Reproduction |
|---|---|---|
| Canonical bundle fingerprint | `91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18` | exact before/after match |
| Independent file-tree digest | `14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214` | two separate exact-helper matches |
| Recursive entries / regular files | `414 / 355` | same protected tree |

The immutable old summary hashes remain
`17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed` and
`e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe`. The active proxy is
recorded only as a credential-free loopback HTTP endpoint. The binding pre-call snapshot at
`2026-07-13T02:32:13+0800` passed on clean commit
`905044238fda6b93f26b54ebf6a74da0dfefbeea`: both old summary hashes, the canonical Desktop
fingerprint, the corrected tree digest and counts, executable path, and absent diagnostic/formal
roots matched exactly.

### One-shot diagnostic result

The optimized control and prefetch candidate ran in that order as separate executable processes
with separate evidence directories and summaries. Neither process was repeated.

| Diagnostic artifact | SHA-256 | Status |
|---|---|---|
| `diagnostic-evidence/control-summary.json` | `f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096` | PASS |
| `diagnostic-evidence/prefetch-summary.json` | `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff` | ERROR, zero completed cases |
| Control raw/proof tree, 30 files | `d3eb2a1adb60aa4a911b14e47854a2fc7b7fe7b81424de17779ae7862b888133` | complete |
| Candidate partial raw/stats tree, 2 files | `054d7d76266febab932255db9658845717625d1ea348bef4021fd3330adffca0` | incomplete first iteration |

The complete control measurements were:

| Control case | Iterations (ms) | Median / P95 (ms) | Median open / georef / read / close (ms) | GET / HEAD / retries | Successful and conservative bytes | Outcome |
|---|---|---|---|---:|---:|---|
| NVIDIA HQ | 3768.248083, 3164.802084, 2860.372458, 2777.421083, 2894.516583 | 2894.516583 / 3768.248083 | 1749.556333 / 1.549542 / 1145.232375 / 0.085041 | 35 / 5 / 0 | 25,054,055 | PASS |
| Hong Kong | 2643.870166, 3553.175208, 2774.859000, 2711.594750, 2651.743250 | 2711.594750 / 3553.175208 | 1810.881375 / 1.508083 / 918.411333 / 0.179500 | 20 / 5 / 0 | 6,320,460 | PASS |

Every control iteration remained below the 16,777,216-byte budget, used one HEAD, had no retry,
and preserved bounded single-range HTTP 206 reads. NVIDIA transferred 5,010,811 bytes per
iteration; Hong Kong transferred 1,264,092 bytes per iteration. Both control medians and P95s
passed the immutable limits.

The candidate started NVIDIA iteration 1, wrote its raw curl/CPL log and network statistics, and
then exited `1` with the exact error:

```text
ScienceHttpRanges failure: VSINetworkStats GET operations disagree with CPL read operations
```

The raw chronology recorded coordinator start, HTTP/2 HEAD header emission at
`474400880674875 ns`, exact `Range: bytes=0-131071` GET emission at `474400880782000 ns`, HEAD
200 at `474401287265500 ns`, Range 206 at `474401352775208 ns`, and cache publication at
`474401786475791 ns`. Thus the outbound Range began before the HEAD response, but the executable
did not produce the required complete parsed proof. The raw trace contained seven actual GETs
(one initial exact Range plus six RGB ranges) and no retry. VSINetworkStats recorded two logical
GET operations—one 131,072-byte Stat GET and one 4,879,739-byte ReadMultiRange operation—for
5,010,811 downloaded bytes plus one HEAD. That disagreement is the rejected observation; it is
not waived or reinterpreted as a passing candidate.

No candidate phase timing, latency percentile, correctness result, Hong Kong iteration, or
complete metadata-prefetch proof was accepted. The candidate summary stayed atomically published
as `{"cases":[],"profile":"prefetch","status":"ERROR"}` with the unchanged 3000/8000 ms
limits. Per the plan, an incomplete or failed candidate set is an immutable diagnostic rejection:
there was no resampling, threshold change, formal CTest promotion, formal evidence directory,
science-off rerun, dependency verification rerun, canonical audit rerun, signature/size/memory/
correctness/range/camera/cache rerun, clean-machine launch, Desktop replacement, package, tag,
push, or G1 work.

After both processes, the old formal summary hashes remained exact, the protected Desktop
fingerprint/tree pair remained
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18` /
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214`, and the formal output
root remained absent.

G0_DECISION=STOP
REASON=The one-shot prefetch candidate exited during NVIDIA iteration 1 because VSINetworkStats GET operations disagreed with CPL read operations; its summary is ERROR with zero completed cases.
G0_AUTOMATED_GATES=NOT_RUN
PUBLIC_PREFETCH_DIAGNOSTIC=FAIL
HUMAN_PRODUCT_SIGN_OFF=PENDING
DESKTOP_PACKAGE=NOT_READY

## Remediation Task 2 offline requalification readiness

On 2026-07-13, source base
`fc56a9b23b827bd61fdf84b754f7c5e6970fb786` was requalified locally after the transport-proof
remediation. This task performed no public request, did not create either requalification sample
root or the new formal root, did not reconfigure the CMake profile, and did not package or replace
the protected Desktop app.

The complete exact non-public regression set passed freshly:

| Regression | Fresh result |
|---|---:|
| Manifest and bundle-audit Python suites | 66/66 passed in 5.000 s |
| Private dependency builder contract | passed in 1.12 s |
| `build/osgsol_core` CTest | 16/16 passed in 10.57 s |
| Selected `build/science_g0_prefetch` release/index/manifest/dependency/GDAL/local HTTP CTest | 7/7 passed in 9.22 s |
| Standalone final private-prefix verifier | passed in 1.39 s |

The verified private prefix remained 52,356 KiB (51 MiB), with 404 regular files and 52
symlinks. Its manifest SHA-256 was
`a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116`, and the final pinned
prefetch patch SHA-256 was
`42db517609c534c6178ce23052449ad747bf4d641b8eae07744839ae92f34c9c`.

The disposable plugin was rebuilt against that verified prefix. Its SHA-256 was
`660c8278d5e9105089afabe2acaee717dfd1fcd260354786a599de5c6feba1a1`. The probe app was
rebuilt at `build/science_g0_prefetch/osgSol Science G0 Probe.app`, ad-hoc signed, and passed
`codesign --verify --deep --strict`. The canonical policy-bearing audit returned `PASS`:

| Requalification isolation measurement | Fresh result |
|---|---:|
| Mach-O graph nodes | 128 |
| Protected baseline size | 542,594,200 bytes |
| Disposable probe size | 563,878,831 bytes |
| Added size | 21,284,631 bytes (20.30 MiB; below 40 MiB) |
| Science-only closure | 21,284,472 bytes (20.30 MiB; below 60 MiB), probe plugin only |
| Tier A absolute science findings | 0 |
| Tier B new / removed identities | 0 / 0 |
| Unresolved dependencies | 0 |
| Historical non-science identities still visible | 1,086 |

`nm -gU` exposed exactly `_osgsol_science_g0_probe_anchor`. A combined `otool -L`, `otool -l`,
and `strings` scan found no worktree or `science-deps-prefetch` path in the plugin. The audit JSON
and text SHA-256 values were respectively
`794b94472590dbaa88a8df99b0beb30cb1bb8846996bc1bf20b0a8a50ee2ec42` and
`b26711027fb7ff1f20790031f888b2155a9819ee56179312f5b6281f59c434e6`.

Every immutable artifact was recomputed without rewriting it:

| Immutable artifact | Fresh SHA-256 |
|---|---|
| Rejected control summary | `f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096` |
| Rejected candidate summary | `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff` |
| Rejected candidate raw log | `3312a78a073140ea1422ece0ffbc927ce853a4d7b8cec02f3033734a7e217a3a` |
| Rejected candidate network statistics | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` |
| Older baseline formal summary | `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed` |
| Older optimized/live formal summary | `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe` |

The protected Desktop canonical fingerprint and independent tree digest remained respectively
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18` and
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214`. The exact helper scan
again covered 414 recursive entries and 355 regular non-symlink files.

The following required v2 roots were all absent after the complete local run:

```text
build/science_g0_prefetch/requalification-evidence-v2/control
build/science_g0_prefetch/requalification-evidence-v2/prefetch
build/science_g0_prefetch/formal-evidence-v2
```

This evidence authorizes exactly one later public v2 requalification set; it does not execute or
prejudge that set, promote the formal CTest profile, authorize Desktop packaging, or start G1.
The immutable rejected result remains part of the record.

G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V2=AUTHORIZED_NOT_RUN
DESKTOP_PACKAGE=NOT_READY

## Remediation Task 3 one-shot public requalification v2

Evidence classification for this section: SHA-256 values and content extracted from the named
summary, raw, stats, and proof artifacts, plus current filesystem file counts, mtimes, and
absence, are independently inspectable. Exact preflight result and timestamp, process exit
statuses, process counts/order/no-rerun statements, candidate console timing/phase lines, and the
final-verifier result are contemporaneous operator console observations only. Their stdout,
preflight, and final-verifier transcripts were not preserved, so those observations are not
independently hash-verifiable.

Contemporaneous operator console observation (preflight transcript not preserved): the binding
pre-call snapshot reported PASS at `2026-07-13T10:30:33+0800` on clean commit
`ea96c86ee2668ab701f6801293edb1d1d2bae5e7`. It recorded the active proxy only as a
credential-free loopback HTTP endpoint. It reported the protected Desktop canonical fingerprint
as
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`; the exact committed
`ScienceProbeBuilderTests.tree_digest()` helper returned
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214` over 414 recursive
entries and 355 regular non-symlink files. The formal CTest profile was still `optimized` with
five iterations. Both v2 diagnostic paths, both v2 summary paths, and the v2 formal root were
reported absent. These preflight observations are not independently hash-verifiable because the
preflight stdout was not preserved.

The preflight console reported that all six protected hashes matched immediately before public
use; that timing claim is an operator observation only. The hash values themselves remain
independently recomputable from the preserved artifacts:

| Protected artifact | Binding SHA-256 |
|---|---|
| Rejected control summary | `f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096` |
| Rejected candidate summary | `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff` |
| Rejected candidate raw log | `3312a78a073140ea1422ece0ffbc927ce853a4d7b8cec02f3033734a7e217a3a` |
| Rejected candidate network statistics | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` |
| Older baseline formal summary | `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed` |
| Older optimized/live formal summary | `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe` |

Contemporaneous operator console observation (process stdout not preserved): the optimized
control and prefetch candidate each ran exactly once, control first, as two separate executable
processes with separate v2 output paths; neither was rerun. The same unpreserved console record
reported that the control exited `0` after 10/10 iterations. These process-count, order, no-rerun,
and exit-status statements are not independently hash-verifiable.

Independently, the hashed control summary contains two five-iteration cases and status `FAIL`
because NVIDIA's median exceeded the immutable limit; Hong Kong passed. Its SHA-256 is
`0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0`.

| Control case | Iteration | Total / open / georeference / read / close ms | GET successful/actual; HEAD; stats GET | Successful / actual / transient / conservative bytes | Retries |
|---|---:|---|---|---|---:|
| NVIDIA | 1 | 3726.996750 / 2475.881958 / 13.598000 / 1237.333250 / 0.183542 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| NVIDIA | 2 | 3031.761959 / 1847.655500 / 1.396459 / 1182.609041 / 0.100959 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| NVIDIA | 3 | 3099.048208 / 1928.344000 / 0.348708 / 1170.255334 / 0.100166 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| NVIDIA | 4 | 3771.631000 / 2495.449583 / 1.078750 / 1274.922584 / 0.180083 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| NVIDIA | 5 | 3475.268042 / 2306.899834 / 0.756833 / 1167.433667 / 0.177708 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| Hong Kong | 1 | 2924.877542 / 1893.256792 / 0.994542 / 1030.452708 / 0.173500 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 2 | 2652.629125 / 1807.329291 / 1.299250 / 843.655084 / 0.345500 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 3 | 2796.605875 / 1839.025750 / 0.953541 / 956.434792 / 0.191792 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 4 | 2640.291500 / 1692.710250 / 1.171916 / 945.885917 / 0.523417 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 5 | 2747.161708 / 1903.087916 / 1.166667 / 842.598083 / 0.309042 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |

| Control aggregate | NVIDIA | Hong Kong |
|---|---:|---:|
| Median / P95 ms | 3475.268042 / 3771.631000 | 2747.161708 / 2924.877542 |
| Summed open / georeference / read / close / total ms | 11054.230875 / 17.178750 / 6032.553876 / 0.742458 / 17104.705959 | 9135.409999 / 5.585916 / 4619.026584 / 1.543251 / 13761.565750 |
| GET successful/actual; HEAD; stats GET | 35/35; 5; 10 | 20/20; 5; 10 |
| Successful / actual / transient / conservative bytes | 25054055 / 25054055 / 0 / 25054055 | 6320460 / 6320460 / 0 / 6320460 |
| Source size | 3700007174 | 2352783157 |
| Retries | 0 | 0 |
| Status | FAIL, median above 3000 ms | PASS |

Contemporaneous operator console observation (process stdout not preserved): the enforced
prefetch process exited `1` during Hong Kong iteration 5 with
`transient HTTP responses do not reconcile with CPL retry events`. The exact exit status and
diagnostic line are not independently hash-verifiable. Independently, the filesystem contains
five NVIDIA proofs and four Hong Kong proofs, and the hashed atomic summary has status `ERROR`,
contains zero cases, and hashes to
`1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`. This byte-identical
empty-error summary is in the new v2 path. The old/rejected artifacts currently retain the six
hashes above; that verifies their current content, not the unpreserved terminal history.

The exact timing and phase values in the next table are contemporaneous operator console
observations whose stdout was not preserved; they are not independently hash-verifiable. For the
nine completed rows, the GET/HEAD/stats/byte/retry columns are independently recorded by the
hashed raw/stats/proof triplets. The Hong Kong iteration 5 row is limited to its hashed raw and
stats artifacts. None of these partial rows form a complete accepted candidate sample because the
atomic summary rejected the run:

| Candidate case | Iteration | Total / open / georeference / read / close ms | GET successful/actual; HEAD; stats GET | Successful / actual / transient / conservative bytes | Retries |
|---|---:|---|---|---|---:|
| NVIDIA | 1 | 2790.07 / 1572.97 / 10.993 / 1205.94 / 0.168791 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| NVIDIA | 2 | 2750.87 / 1578.86 / 1.13163 / 1170.70 / 0.177917 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| NVIDIA | 3 | 2832.01 / 1573.41 / 1.24283 / 1257.07 / 0.285250 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| NVIDIA | 4 | 2641.63 / 1470.16 / 0.812541 / 1170.47 / 0.185917 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| NVIDIA | 5 | 2628.96 / 1458.67 / 1.14604 / 1168.96 / 0.177334 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| Hong Kong | 1 | 2433.78 / 1564.08 / 1.23737 / 867.747 / 0.710959 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 2 | 2209.19 / 1375.02 / 1.03846 / 832.812 / 0.321167 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 3 | 2403.25 / 1484.32 / 1.20471 / 917.408 / 0.316125 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 4 | 2332.39 / 1489.94 / 1.15842 / 841.075 / 0.217875 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 5 | no accepted timing or proof | observed raw 4 successful / 5 actual GET; 1 HEAD; stats GET 3 | stats downloaded 1264109, including the 17-byte transient body | 500 not reconciled |

The unpreserved operator console emitted NVIDIA median/P95 `2750.87/2832.01 ms` and summed phase
values `7654.07/15.3261/5973.15/0.995209 ms` for open/georeference/read/close. Those timing and
phase values are not independently hash-verifiable. The five hashed NVIDIA proofs independently
record 35/35 GETs, five HEADs, ten stats GET operations, 25,054,055 successful and actual body
bytes, zero declared transient bytes, and zero retries. Those partial values cannot qualify the
candidate. Hong Kong iteration 5's hashed raw artifact records an overlapping initial HTTP/2
Range returning `500` with a 17-byte body while HEAD returned `200`; it records
`logical-get-complete bytes=17`, fallback, the later exact `bytes=0-131071` read, and three bounded
multi-ranges returning `206`. The hashed stats artifact records 1,264,109 downloaded bytes. No
proof JSON exists for that iteration. The statement that the process rejected this chronology for
lacking a matching CPL retry event is the unpreserved operator diagnostic quoted above.

Every accepted candidate proof recorded exact first Range `0-131071`, overlapping HEAD/Range,
HTTP/2 for both handles, equal explicit connection IDs, a published cache, correct fixture output,
bounded bytes, and zero retry. The complete required 10-iteration proof is nevertheless absent.

### V2 control artifact hashes

| Iteration | Raw SHA-256 | Stats SHA-256 | Proof SHA-256 |
|---|---|---|---|
| `optimized-hong_kong-1` | `d924d50008b72437fdc3e55c58c9752d7f0b65b61284b501bbb3ee1969c00e12` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `b4e1c2f3079869021eb8efa873e6dbc504751fdd34298349c069210dabb6c2c8` |
| `optimized-hong_kong-2` | `8bf93e338293aa65366e85bcac250bf4e2e5ff0fa9b4b2a04471aa6508ce4c2f` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `8bac34615ff79ab6daea7a9e12ff69074caa010683b6060487fc07d6fa2d46a1` |
| `optimized-hong_kong-3` | `cef27ffd3ec32db26456376c162fc92ce78418ae4f7430a649a3213fd13e714d` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `b4e1c2f3079869021eb8efa873e6dbc504751fdd34298349c069210dabb6c2c8` |
| `optimized-hong_kong-4` | `74bd5ae05e094a206477dfb062bbda09c157df1746370dbd3f2447766307fb1d` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `2d7eed81bb68e48c9a7aba80b861a4288ec362cf9bceab9f0c11e2d0539451a6` |
| `optimized-hong_kong-5` | `3d1b8db16a4e78a8e0c27be669363448983065942f3389c4cce14dd0b2aecd71` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `8bac34615ff79ab6daea7a9e12ff69074caa010683b6060487fc07d6fa2d46a1` |
| `optimized-nvidia_hq-1` | `e5670ed476381d7a88d713417c000bdefb8bb93ef851494efebfc67f9c59b216` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `78851862e3c3a90d18fd012c182019907234c19e7efb698332637cc3dcd087ff` |
| `optimized-nvidia_hq-2` | `9c6bc931f2f1d0e20d5fa5fac289e4338300f1617f6e7e1c7772c39092a35831` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `5db4834db1b1b62b0c45421b0438c9870dba5683832dc977e6b4e7de1974e55e` |
| `optimized-nvidia_hq-3` | `09bb302001fe28c31e54dabd1e557b3e0c8e52b39ef5211c2622661ea82cd93e` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `cb94ed252db861434583d1fc1721c80dc13dc97322caef4fe4646b4499601c1c` |
| `optimized-nvidia_hq-4` | `13a4b772e63e0b8f817b4828adcb41d9a659bec322d509eddff6ea741c6b4523` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `3db6f1f0c60decb06ea3a2ff0ae2174d5513529e4e8b68c1c032ca0bd93009b4` |
| `optimized-nvidia_hq-5` | `d73a8a53264f076b63a33b69f481ed5699ff9284aaa99a32183896f0e74b49ea` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `d492859747c1385a4b139d3915280a45a93c9d69610be5ff9113fc1a231fc1a3` |

### V2 candidate artifact hashes

| Iteration | Raw SHA-256 | Stats SHA-256 | Proof SHA-256 |
|---|---|---|---|
| `prefetch-hong_kong-1` | `b90a64c3f5bb07fa34e22aaef0af6c1f2b6f8d53bd14921eea713a1731fa8d80` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `713442423979d9780d5316754cb197ef6976fbfa6e0233184a3f9a00ad61a6ca` |
| `prefetch-hong_kong-2` | `6c8f0229e7ea5f5e1f09924c2ac1d70734b0a1d0149cfb68de87b1929655c4a9` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `4e5da004266ce20810f24bb260ce99eb8499eb0371c0e404013db6f9d8c732d2` |
| `prefetch-hong_kong-3` | `ac5d7b0c6c6f8538b4caccef5da663dbb83f08568d1a72a9c9d52bc7e4685465` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `4e5da004266ce20810f24bb260ce99eb8499eb0371c0e404013db6f9d8c732d2` |
| `prefetch-hong_kong-4` | `bd98f1a06ed0d594625e200b35076b2c2959c10c88a6c2a1baf0b5cf4fb2aad7` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `a831394cb00c0136b0e31743a2c58b075ed8f1457d473b8673522caabcd53731` |
| `prefetch-hong_kong-5` | `f32cfdfd05f50a3de8f34b4f69e1e2cefe61d667e20936ddcf86f1fa8b1a429e` | `f54110c20ab81230c5d3af708f2df4d9d8754e5957366875c162b0395d2c1974` | absent; parser rejected the iteration |
| `prefetch-nvidia_hq-1` | `3123151aacce8660a4bf2957793d5cc32973d2acfaee16a77037ef53e0ce6bd6` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `8fb3837b2faf776118ed747b03896f8c643c3d7abf4522c9c3b779794bc2c7cf` |
| `prefetch-nvidia_hq-2` | `3078be57af6922e2a5ac4daa9204c60c2b735946a84fa65dece457017b81c318` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `6c050b99ee7a696c228c82d62f9ea68de0752b6e6066df8eb16feb9464825e8b` |
| `prefetch-nvidia_hq-3` | `5fe6c1e7069c74daf0ba2197698b963ccc3a9b78bf6e15fc2e31ba7faa79c4a0` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `4a157cb77250c16f9b320182a8cb4da868ae9f0fb46e987440f0db0a45c54643` |
| `prefetch-nvidia_hq-4` | `308e896f4173d21f58f4fd49668ef2d224dffcede0a6477d096b4c46b5dd1387` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `4837de28b1a5ed9626a2538175670636cc388111f4784bea6cc66221bdef0e77` |
| `prefetch-nvidia_hq-5` | `669199de9898bbfffe16027f681f69370dd6619e46a52fd4ed0bce19781ae686` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `2a90942362f552b42fb3cc5ca05e70406871f9cf58b0482c45488afed8951ea0` |

The failed complete candidate proof forbids formal promotion. Current tracked
`tests/CMakeLists.txt` remains `--profile optimized`, and the current filesystem has no
`build/science_g0_prefetch/formal-evidence-v2` root. Contemporaneous operator console observation
(stdout/final-verifier transcript not preserved): formal CTest and every formal/downstream audit,
signature, size, export, path, memory, correctness, range, camera/cache, and clean-machine process
count were zero, and no Desktop package was built or replaced. Those process-count and no-package
statements are not independently hash-verifiable. The Task 2 section above records the last local
canonical audit; it was not rerun as a Task 3 post-promotion gate and does not change this
decision.

G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V2=FAIL
DESKTOP_PACKAGE=NOT_READY

## Prefetch v3 Task 2 offline requalification authorization

On 2026-07-13, source base
`d4505b077e067f7f16276fe5191c8042092c961d` was requalified locally after the
transient-fallback accounting change. This task performed no public request, did not create a v3
diagnostic or formal root, did not reconfigure or promote the formal CMake profile, and did not
package or replace the protected Desktop app.

The complete non-public regression commands were:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
bash tests/science_deps_script_tests.sh
ctest --test-dir build/osgsol_core --output-on-failure
ctest --test-dir build/science_g0_prefetch --output-on-failure \
  -R 'ScienceEarthRelease|AefIndexTool|ScienceG0Manifest|ScienceDepsScript|ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer'
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --verify
```

Each command ran once and passed freshly:

| Regression | Fresh result |
|---|---:|
| Manifest and bundle-audit Python suites | 66/66 passed in 4.75 s |
| Private dependency builder contract | passed in 1.15 s |
| `build/osgsol_core` CTest | 16/16 passed in 10.53 s |
| Selected `build/science_g0_prefetch` CTest | 7/7 passed in 8.79 s |
| Standalone final private-prefix verifier | passed in 1.29 s |

Only generated `tests/__pycache__` and `packaging/scienceearth/__pycache__` directories were
removed after their producing Python gates had been recorded.

The disposable plugin and app were rebuilt once with:

```bash
cmake --build build/science_g0_prefetch \
  --target osgdb_science_g0_probe --parallel 8
SCIENCE_G0_PROBE_PLUGIN="$PWD/build/science_g0_prefetch/lib/osgdb_science_g0_probe.so" \
SCIENCE_G0_OUTPUT_APP="$PWD/build/science_g0_prefetch/osgSol Science G0 Probe.app" \
  bash packaging/build_science_g0_probe.sh
```

The target build passed in 0.51 seconds and the copy/sign/strict-verify builder passed in 1.00
second. The policy-bearing audit then ran once in 14.39 seconds:

```bash
python3 packaging/audit_macos_bundle.py \
  --app 'build/science_g0_prefetch/osgSol Science G0 Probe.app' \
  --baseline '/Users/USER/Desktop/osgSol Earth.app' \
  --reference-manifest packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json \
  --ratchet-manifest packaging/scienceearth/baselines/current-macos-arm64-ratchet.json \
  --source-root /Users/USER/osgverse \
  --source-root /Users/USER/osgsol/.worktrees/v0.2-runtime-safety \
  --json build/science_g0_prefetch/bundle-audit.json \
  --text build/science_g0_prefetch/bundle-audit.txt
```

| V3 offline isolation measurement | Fresh result |
|---|---:|
| Audit status | PASS, exit 0 |
| Mach-O graph nodes | 128 |
| Protected baseline size | 542,594,200 bytes |
| Disposable probe size | 563,878,831 bytes |
| Added size | 21,284,631 bytes (20.30 MiB; below 40 MiB) |
| Science-only closure | 21,284,472 bytes (20.30 MiB; below 60 MiB), probe plugin only |
| Tier A absolute science findings | 0 |
| Tier B new / removed identities | 0 / 0 |
| Unresolved dependencies | 0 |
| Historical non-science identities still visible | 1,086 |

`codesign --verify --deep --strict` passed. `nm -gU` exposed exactly
`_osgsol_science_g0_probe_anchor`. A combined `otool -L`, `otool -l`, and `strings` scan found
zero worktree or `science-deps-prefetch` path hits in the plugin. Fresh build/audit hashes were:

| V3 private artifact | SHA-256 |
|---|---|
| Final pinned prefetch patch | `70307965af8caf8a9644335e99b0a34c9c15b85185ec96ed333a85407cf68569` |
| Sanitized transient replay trace | `18df92055ef74d460743b01b8f9853b1b3d3e4baf267017f43b08928c7734c04` |
| Byte-identical replay stats fixture | `f54110c20ab81230c5d3af708f2df4d9d8754e5957366875c162b0395d2c1974` |
| Private-prefix manifest | `a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116` |
| Rebuilt probe plugin | `0aa31d3d5da931fbb7458cf3228c7e43b9b65ba54083e31cdc1f2f9f2fc4656b` |
| Canonical audit JSON | `794b94472590dbaa88a8df99b0beb30cb1bb8846996bc1bf20b0a8a50ee2ec42` |
| Canonical audit text | `b26711027fb7ff1f20790031f888b2155a9819ee56179312f5b6281f59c434e6` |

Every old diagnostic/formal binding and every v2 summary/raw/stats/proof table entry was
recomputed without rewriting evidence. The old diagnostic tree helper hashed sorted relative
paths followed by each regular non-symlink file's bytes. The v2 verifier parsed all 20 committed
iteration rows in this document, derived the raw/stats/proof path for each row, and checked 60
artifact slots: 59 file hashes matched and the rejected Hong Kong iteration-5 proof remained the
one expected absence. The filesystem contained exactly those 59 per-iteration files plus two v2
summaries. The two v2 summaries also matched independently.

| Critical immutable artifact | Fresh SHA-256 |
|---|---|
| Rejected diagnostic control summary | `f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096` |
| Rejected diagnostic candidate summary | `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff` |
| Rejected diagnostic candidate raw log | `3312a78a073140ea1422ece0ffbc927ce853a4d7b8cec02f3033734a7e217a3a` |
| Rejected diagnostic candidate network statistics | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` |
| Older baseline formal summary | `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed` |
| Older optimized/live formal summary | `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe` |
| V2 control summary | `0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0` |
| V2 candidate summary | `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff` |
| V2 Hong Kong iteration-5 raw log | `f32cfdfd05f50a3de8f34b4f69e1e2cefe61d667e20936ddcf86f1fa8b1a429e` |
| V2 Hong Kong iteration-5 network statistics | `f54110c20ab81230c5d3af708f2df4d9d8754e5957366875c162b0395d2c1974` |

The duplicate candidate-summary binding means the Global Constraints' ten listed bindings are
nine unique SHA-256 values; all bindings matched. The old diagnostic 30-file control tree and
two-file candidate tree hashes also remained respectively
`d3eb2a1adb60aa4a911b14e47854a2fc7b7fe7b81424de17779ae7862b888133` and
`054d7d76266febab932255db9658845717625d1ea348bef4021fd3330adffca0`.

The protected Desktop canonical fingerprint and exact committed
`ScienceProbeBuilderTests.tree_digest()` helper remained respectively
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18` and
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214`.
The helper again covered 414 recursive entries and 355 regular non-symlink files.

`ctest --test-dir build/science_g0_prefetch -N -V -R
'^osgVerse_Test_ScienceGdalLive$'` returned the configured formal command with `--iterations 5`,
`--profile optimized`, the existing `science-network-evidence` paths, and `--enforce-latency`.
It listed the test without running it. The following required v3 roots were absent before
documentation and at the final local gate:

```text
build/science_g0_prefetch/requalification-evidence-v3/control
build/science_g0_prefetch/requalification-evidence-v3/prefetch
build/science_g0_prefetch/formal-evidence-v3
```

This evidence authorizes exactly one later public v3 requalification set; it does not execute or
prejudge that set, promote the formal CTest profile, authorize Desktop packaging, or start G1.
The old rejected diagnostic and failed v2 result remain part of the immutable record.

G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V3=AUTHORIZED_NOT_RUN
DESKTOP_PACKAGE=NOT_READY

## Prefetch v3 Task 3 one-shot public requalification decision

At `2026-07-13T13:09:11+08:00`, immediately before public network use, the binding
preflight passed on clean commit `e2d1f17e2fc958f6625971909dbcac9bfccabe4d` and branch
`codex/science-earth-g0-g1`. The active proxy was recorded only as a credential-free
`loopback_http` endpoint class. The exact executable was 28,968,240 bytes with SHA-256
`56a6d68f8ab6e709dee51b0394f49666a5a9a82163232981456a5963c9628218`.

The preflight recomputed all rejected-diagnostic, older-formal, and v2 bindings. All 59 present
v2 iteration artifacts and both v2 summaries matched, the rejected Hong Kong iteration-5 proof
was the one expected absence, and the v2 tree still contained 61 regular files. The ten critical
bindings retained the values in the Task 2 authorization section above. The rejected control and
candidate tree digests remained
`d3eb2a1adb60aa4a911b14e47854a2fc7b7fe7b81424de17779ae7862b888133` and
`054d7d76266febab932255db9658845717625d1ea348bef4021fd3330adffca0`.
The protected Desktop fingerprint/helper/counts were exactly
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`,
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214`,
and `414 / 355`. CTest listed one formal test with `--iterations 5`, `--profile optimized`,
the existing `science-network-evidence` paths, and `--enforce-latency`, without executing it.
The v3 parent, control, prefetch, and formal roots were absent.

The independently inspectable preflight transcript is
`.superpowers/sdd/task-3-v3-preflight.log`, SHA-256
`d48994e70732786983b9c5d2f9c67bb2ca0c936d0b25e69c7acefd694fbe6aff`. It records
`PREFLIGHT_EXIT_STATUS=0`.

### Exact one-shot processes

The two authorized processes ran once each, in control-then-candidate order, with no smoke or
preflight invocation of the executable:

```bash
build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile optimized \
  --evidence-dir build/science_g0_prefetch/requalification-evidence-v3/control \
  --summary-json build/science_g0_prefetch/requalification-evidence-v3/control-summary.json

build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile prefetch \
  --evidence-dir build/science_g0_prefetch/requalification-evidence-v3/prefetch \
  --summary-json build/science_g0_prefetch/requalification-evidence-v3/prefetch-summary.json \
  --enforce-latency
```

| Process | Start / finish | Exit | Transcript SHA-256 |
|---|---|---:|---|
| Optimized control | `13:10:08 / 13:10:41 +0800` | 0 | `a1d8e36c152f71771e1ad80ae548e43e397456ce6ea47fe03e50e2cad9b2d142` |
| Prefetch candidate | `13:11:34 / 13:11:41 +0800` | 1 | `c7f98c3e8428cb72092e6e49a1fff359232c4b4463607e18d52d55aac1b907d2` |

Each transcript contains exactly one process label, command, start, finish, and exit record. The
control and candidate artifact manifests have SHA-256 values
`c81a325b4aecd6b1a722c01789270c2568fc55cb7a1b201b7a6dd8c73e4b3849` and
`5845a9aac3a9b7e4b88e9323b16f20ff45f1e6f99ecc7a3997175b0f791458ff`.
All 37 produced diagnostic artifacts were made non-writable after their process exited.

### Optimized control

The control completed all ten iterations and produced all ten raw/stats/proof triplets. Its
summary SHA-256 is
`47abb0bc91e4afba412d1150822b90110650f8e225b45b7fa52f4627889c405c`.
The process exits zero because latency is diagnostic without `--enforce-latency`; the summary is
`FAIL` because NVIDIA's median exceeds 3000 ms. Every iteration retained correct fixture output,
bounded successful ranges, reconciled GET/HEAD/stats accounting, and a complete proof. Ordinary
CPL retry events were accepted only after exact status/range/byte reconciliation; this control
profile has no metadata-prefetch coordinator fallback.

| Control case | Iteration | Total / open / georeference / read / close ms | GET successful/actual; HEAD; stats GET | Successful / actual / transient / conservative bytes | Ordinary retries |
|---|---:|---|---|---|---:|
| NVIDIA | 1 | 3852.527208 / 2011.467667 / 12.410708 / 1828.464542 / 0.184291 | 7/9; 1; 2 | 5010811 / 5010811 / 34 / 5010845 | 2 x 500 |
| NVIDIA | 2 | 3699.333500 / 1969.281667 / 1.447666 / 1728.420334 / 0.183833 | 7/9; 1; 2 | 5010811 / 5010811 / 34 / 5010845 | 2 x 500 |
| NVIDIA | 3 | 3131.709959 / 1968.486459 / 1.518000 / 1161.571208 / 0.134292 | 7/7; 1; 2 | 5010811 / 5010811 / 0 / 5010811 | 0 |
| NVIDIA | 4 | 3691.365250 / 1910.753000 / 1.778458 / 1778.656750 / 0.177042 | 7/10; 1; 2 | 5010811 / 5010811 / 51 / 5010862 | 3 x 500 |
| NVIDIA | 5 | 3576.102667 / 1815.754208 / 1.895834 / 1758.276791 / 0.175834 | 7/9; 1; 2 | 5010811 / 5010811 / 34 / 5010845 | 2 x 500 |
| Hong Kong | 1 | 2929.535709 / 1967.382750 / 1.914750 / 959.927167 / 0.311042 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 2 | 3358.329875 / 2001.104417 / 1.581291 / 1355.334750 / 0.309417 | 4/5; 1; 2 | 1264092 / 1264092 / 17 / 1264109 | 1 x 500 |
| Hong Kong | 3 | 2811.656375 / 1885.494000 / 3.090708 / 922.756625 / 0.315042 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 4 | 2912.941459 / 1898.625000 / 1.112667 / 1012.805667 / 0.398125 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |
| Hong Kong | 5 | 2777.528250 / 1931.166459 / 2.080916 / 843.978542 / 0.302333 | 4/4; 1; 2 | 1264092 / 1264092 / 0 / 1264092 | 0 |

| Control aggregate | NVIDIA | Hong Kong |
|---|---:|---:|
| Median / P95 ms | 3691.365250 / 3852.527208 | 2912.941459 / 3358.329875 |
| GET successful/actual; HEAD; stats GET | 35/44; 5; 10 | 20/21; 5; 10 |
| Successful / actual / transient / conservative bytes | 25054055 / 25054055 / 153 / 25054208 | 6320460 / 6320460 / 17 / 6320477 |
| Ordinary retries | 9 x 500 | 1 x 500 |
| Status | FAIL, median above 3000 ms | PASS |

### Prefetch candidate and hard stop

NVIDIA iteration 1 completed in `2813.06 ms` and produced a valid proof. It recorded exact first
Range `bytes=0-131071`, one HEAD and one prefetch Range on HTTP/2, equal explicit connection IDs,
request overlap, cache publication, zero ordinary retry, zero coordinator fallback, correct
fixture output, 7/7 GETs, one HEAD, two stats GET operations, and
`5010811 / 5010811 / 0 / 5010811` successful/actual/transient/conservative bytes.

NVIDIA iteration 2 then received HTTP/2 `500` with a 17-byte body on the parallel first Range.
The hashed raw trace contains exactly one authoritative event
`ParallelHeadRange: transient-fallback range=bytes=0-131071 status=500 bytes=17`, one
`fallback=status-500`, shared connection IDs `0/0`, HTTP/2 for both handles, and three separate
ordinary CPL `500` retry events. Physical GETs reconcile as 7 successful + 3 ordinary retries +
1 coordinator fallback = 11. VSINetworkStats records 3 GET operations, one HEAD, and 5,010,828
downloaded bytes, exactly 5,010,811 successful bytes plus the 17 coordinator bytes. The HTTP
layer therefore reconciled the coordinator event separately and then the formal proof rejected
the iteration with exactly:

```text
ScienceHttpRanges failure: metadata prefetch formal proof contains a fallback
```

No iteration-2 proof was published. The candidate summary is the atomic `ERROR` object with zero
cases and SHA-256
`1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`.
The candidate produced two NVIDIA raw files, two NVIDIA stats files, one NVIDIA proof, and no
Hong Kong file: only 1/10 required proofs and 0/5 Hong Kong iterations. Candidate medians and
P95s do not exist. The complete proof, zero-fallback, latency, and two-fixture gates therefore
fail closed.

### Frozen v3 artifact hashes

| Control iteration | Raw SHA-256 | Stats SHA-256 | Proof SHA-256 |
|---|---|---|---|
| `optimized-hong_kong-1` | `e992b0723f763dbeb14853f3e0c5b400d22ebe8de999b8c21e44985f8db041b6` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `dafe7f8dda16027077624aa31f90a63cd97260986772c69c03c6784a902112aa` |
| `optimized-hong_kong-2` | `a256cd80c76aea3eeb8e9dbdcd49649364743f4de504e50a37732edc6e0136aa` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `a3229f4cb831b5ed7d21a964118d5342eacd44f9269d2275ab6546965efdbf04` |
| `optimized-hong_kong-3` | `a97ccb41648f67d4220e7d806f49f1691a722641e7f2d9b8561b2782d1b30042` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `6abeff76fc5027f50ee792295eda95388c65d982852b930d2cfa82100e2621a1` |
| `optimized-hong_kong-4` | `5f3c255450e329888f9d5356b6eb60d0db6ef93ce49dd4d71ac6227d2fa93802` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `6abeff76fc5027f50ee792295eda95388c65d982852b930d2cfa82100e2621a1` |
| `optimized-hong_kong-5` | `32a060dfb41e72424b57e5261d410d2038164fef932f72bad776f1028c1c824e` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `dafe7f8dda16027077624aa31f90a63cd97260986772c69c03c6784a902112aa` |
| `optimized-nvidia_hq-1` | `4d7df3e3af6ae51021319461bb194e58138b184bcdacb454b2fbc13233aefb7f` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `6f31188548645cec15770310cae26b8adacdbaa9a31ee1f05b7c066e3da7c989` |
| `optimized-nvidia_hq-2` | `656993a304546cb01fbaed2312038ae9fcc4e2873ba0401169596b9f90186fa9` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `7eaf175f6ec176410b30c2f33391760e34bd202dc1098dd62b4b7b373e6cbe84` |
| `optimized-nvidia_hq-3` | `c308295d9c59a791c01bd26817169a4026cafee19e1f7b77fdbb01155544b8a0` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `4513289ba2f2affe33d4f982e354584640faa53eadfc088eec899de10aeb70d4` |
| `optimized-nvidia_hq-4` | `f2086a3cfc1314d85d3d7581e410ca7f2693c0213e2ac325bec3dd7adde04c71` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `8a8ee4a8cf6b9473702ed540ad9a15828e8b7096187125c7a80278a2c21ea703` |
| `optimized-nvidia_hq-5` | `9613fbcaf1c2f206f0aa502bd813da411d2217db2d17ba1d5e45468952ebf0bb` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `15236a898fe3c4c99c504c0b5edb323c921aa93550f2a29fa73100b7727855e1` |

| Candidate iteration | Raw SHA-256 | Stats SHA-256 | Proof SHA-256 |
|---|---|---|---|
| `prefetch-nvidia_hq-1` | `114d20ad165945e144d48c6ea18c4db1e190311a218394f5d99c831e8950343b` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `da4ebeddbbff9cc6d13854c941172bf24c3dbe455d0e1fe6fbc7abf677b72ca4` |
| `prefetch-nvidia_hq-2` | `1ccb4361f6c0c5bd5c19271cf74f2f72135bdebd87515f55110c18353ecadc07` | `67168772dbae8576f3e2810b65931e6737e854aba9c4183b2d40726e1fb97fd8` | absent; formal no-fallback rejection |

The hashed decision-gate transcript is
`.superpowers/sdd/task-3-v3-decision-gate.log`, SHA-256
`0fd10896a891e20eb8e63a755fb68f4772e2cefc36b5b30f148165101001bafa`.
The final protected-state transcript is `.superpowers/sdd/task-3-v3-final-protected.log`,
SHA-256 `cb5bc37da993e862b4cfbfae4e8777df586fd03e05a753693b972c518dc072a9`.
It freshly reverified every old and v2 artifact, the Desktop hashes/counts, the unchanged
`tests/CMakeLists.txt` SHA-256
`b9eea24fabb9fa2fb1d79bf384320e5082bd0155aedfcc18fa2d2ab33411141a`,
and the absent v3 formal root.

Because the candidate did not produce 10/10 complete zero-fallback proofs, `tests/CMakeLists.txt`
was not modified or committed. Formal CTest process count is zero; every downstream test, audit,
signature, size, export, path, memory, correctness, range, camera/cache, and clean-machine process
count is zero. The formal profile remains `optimized`, and no Desktop package was built or
replaced. No tag, push, G1 work, threshold/retry change, or old/v2 evidence mutation occurred.

G0_DECISION=STOP
REASON=The one-shot v3 prefetch candidate encountered one explicitly accounted coordinator HTTP 500 fallback on NVIDIA iteration 2 and was correctly rejected by the formal no-fallback gate after producing only 1/10 proofs.
G0_AUTOMATED_GATES=NOT_RUN
PUBLIC_REQUALIFICATION_V3=FAIL
HUMAN_PRODUCT_SIGN_OFF=PENDING
DESKTOP_PACKAGE=NOT_READY
RECORDED_DATE=2026-07-13

## Bounded-retry v4 Task 2 offline requalification authorization

On 2026-07-13, source base
`4a1c1172e02ce13b5fd6452a97834dbd9f2001b3` on
`codex/science-earth-g0-g1` was requalified locally. This task made no public AlphaEarth request,
did not start a control, candidate, or formal process, did not create a v4 evidence root or
summary, did not change `tests/CMakeLists.txt`, and did not package or modify
`/Users/USER/Desktop/osgSol Earth.app`.

### Complete non-public regression

The required commands ran once each exactly as follows:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
bash tests/science_deps_script_tests.sh
ctest --test-dir build/osgsol_core --output-on-failure
ctest --test-dir build/science_g0_prefetch --output-on-failure \
  -R 'ScienceEarthRelease|AefIndexTool|ScienceG0Manifest|ScienceDepsScript|ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer'
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --verify
```

| V4 offline gate | Exit | Fresh result | Timing |
|---|---:|---:|---:|
| Manifest and bundle-audit Python suites | 0 | 66/66, zero skip/failure | 5.361 s internal; 5.389 s wall |
| Private dependency builder contract | 0 | PASS | 1.151 s wall |
| `build/osgsol_core` CTest | 0 | 16/16 | 10.27 s CTest real |
| Selected `build/science_g0_prefetch` CTest | 0 | 7/7 | 11.88 s CTest real |
| Standalone private-prefix verifier | 0 | PASS | 1.264 s wall |

The selected CTest covered `ScienceEarthRelease`, `AefIndexTool`, `ScienceG0Manifest`,
`ScienceDepsScript`, `ScienceGdalSpike`, `ScienceHttpRanges`, and `ScienceHttpRangeServer`; its
network-labelled cases were loopback-only. The verifier reported all three source archives OK,
independent GDAL `#embed` capability ON, and a verified private static prefix and manifest.
Generated `tests/__pycache__` and `packaging/scienceearth/__pycache__` were listed only after their
producing results were recorded and then removed. The audit later regenerated only the packaging
cache; it too was recorded and removed. Final `find . -type d -name __pycache__ -prune -print`
output was empty.

### Disposable probe and canonical isolation audit

The disposable target and application were rebuilt once with:

```bash
cmake --build build/science_g0_prefetch \
  --target osgdb_science_g0_probe --parallel 8
SCIENCE_G0_PROBE_PLUGIN="$PWD/build/science_g0_prefetch/lib/osgdb_science_g0_probe.so" \
SCIENCE_G0_OUTPUT_APP="$PWD/build/science_g0_prefetch/osgSol Science G0 Probe.app" \
  bash packaging/build_science_g0_probe.sh
```

The target build exited 0 in 0.422 seconds. The copy, ad-hoc sign, strict verify, and protected
Desktop fingerprint check exited 0 in 1.094 seconds. The committed policy-bearing audit then ran
once:

```bash
python3 packaging/audit_macos_bundle.py \
  --app 'build/science_g0_prefetch/osgSol Science G0 Probe.app' \
  --baseline '/Users/USER/Desktop/osgSol Earth.app' \
  --reference-manifest packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json \
  --ratchet-manifest packaging/scienceearth/baselines/current-macos-arm64-ratchet.json \
  --source-root /Users/USER/osgverse \
  --source-root /Users/USER/osgsol/.worktrees/v0.2-runtime-safety \
  --json build/science_g0_prefetch/bundle-audit.json \
  --text build/science_g0_prefetch/bundle-audit.txt
```

It exited 0 with `PASS` in 15.547 seconds.

| V4 isolation measurement | Fresh result |
|---|---:|
| Mach-O graph nodes | 128 |
| Protected baseline size | 542,594,200 bytes |
| Disposable probe size | 563,895,343 bytes |
| Added size | 21,301,143 bytes (20.31 MiB; below 40 MiB) |
| Science-only closure | 21,300,984 bytes (20.31 MiB; below 60 MiB) |
| Science closure membership | only `Contents/lib/osgPlugins-3.6.5/osgdb_science.so` |
| Tier A absolute science findings | 0 |
| Tier B new / removed identities | 0 / 0 |
| Unresolved dependencies | 0 |
| Historical non-science identities | 1,086 |
| Violations | 0 |

Independent signature, export, dependency, and path commands were:

```bash
/usr/bin/codesign --verify --deep --strict \
  'build/science_g0_prefetch/osgSol Science G0 Probe.app'
nm -gU build/science_g0_prefetch/lib/osgdb_science_g0_probe.so
otool -L build/science_g0_prefetch/lib/osgdb_science_g0_probe.so
(otool -L build/science_g0_prefetch/lib/osgdb_science_g0_probe.so; \
 otool -l build/science_g0_prefetch/lib/osgdb_science_g0_probe.so; \
 strings -a build/science_g0_prefetch/lib/osgdb_science_g0_probe.so) | \
  rg -n '/Users/USER/osgsol/\.worktrees|science-deps-prefetch'
```

The signature command exited 0. `nm -gU` produced exactly one export,
`_osgsol_science_g0_probe_anchor`. Direct dependencies were only system curl, System, SQLite,
and C++. The final `rg` had the required zero matches and therefore exited 1.

Fresh private and audit bindings were:

| V4 private artifact | SHA-256 |
|---|---|
| Final bounded-retry patch and configured pin | `5ce830f7853db1c6f53b833741261f55f382c0b8972161999a2d34b99c3ab15f` |
| Successful-retry replay trace | `124564316b6a1cfc0dd89ada2c0d4d43ca503dfe5e5b122b3a50eff605f5dd46` |
| Successful-retry replay statistics | `ea88aae77d69537b90dbd5e6c7188d37ad31837b3500e614cb0f3500d48d42b6` |
| Private-prefix manifest | `a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116` |
| Rebuilt probe plugin | `1184bb101c5f8a3fb0bbff26b0efaa1987f002306a949fc51c587e20fdb1a218` |
| Canonical audit JSON | `eee5b4b4dd5c946f802a2dd92296efec47e1fb9ed357a750295b8d1214fb217d` |
| Canonical audit text | `975065237553f763bb71e189330dbf206a56723145cbfab95c45c4ec09c4950e` |

The verified prefix was 52,356 KiB (`du -sh`: 51 MiB), with 404 regular files and 52 symlinks.

### Read-only historical, Desktop, profile, and v4-absence audit

The authorization verifier is preserved below as executable source. It uses only read operations
for protected evidence (`read_bytes`, JSON reads, directory enumeration, and `stat`), lists the
formal CTest without executing it, and fails closed on every recorded Global Constraint, v2/v3
table/hash/set/permission invariant, documented v3 scratch/CMake binding, Desktop invariant, formal
profile invariant, and v4 absence requirement. The extracted source SHA-256 is
`807c3a1c5e0c4000dcb2ba2e3da06c97900b1fec099aed372455ec8a5294ad30`.

<!-- V4_AUTHORIZATION_VERIFIER_BEGIN -->
```python
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import stat
import subprocess
import sys

ROOT = Path.cwd().resolve()
EXPECTED_ROOT = Path(
    "/Users/USER/osgsol/.worktrees/v0.2-runtime-safety").resolve()
if ROOT != EXPECTED_ROOT:
    raise SystemExit(f"wrong worktree: {ROOT} != {EXPECTED_ROOT}")
AUTHORIZATION_BASE = "4a1c1172e02ce13b5fd6452a97834dbd9f2001b3"
DOC_RELATIVE = "docs/scienceearth/g0-measurements.md"
DOC_PATH = ROOT / DOC_RELATIVE
DOC_BYTES = DOC_PATH.read_bytes()


def require(condition, message):
    if not condition:
        raise SystemExit(message)


base_type = subprocess.run(
    ["git", "cat-file", "-t", AUTHORIZATION_BASE], cwd=ROOT,
    capture_output=True, check=False)
require(base_type.returncode == 0 and base_type.stdout == b"commit\n",
        f"authorization base is not a commit: {AUTHORIZATION_BASE}")
base_doc_result = subprocess.run(
    ["git", "show", f"{AUTHORIZATION_BASE}:{DOC_RELATIVE}"], cwd=ROOT,
    capture_output=True, check=False)
require(base_doc_result.returncode == 0,
        f"cannot read authorization base document: {base_doc_result.stderr!r}")
BASE_DOC_BYTES = base_doc_result.stdout


def file_sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_hash(relative, expected):
    path = ROOT / relative
    require(path.is_file() and not path.is_symlink(), f"missing file: {relative}")
    actual = file_sha256(path)
    require(actual == expected, f"hash mismatch: {relative}: {actual} != {expected}")


def tree_digest(relative):
    root = ROOT / relative
    require(root.is_dir() and not root.is_symlink(), f"missing tree: {relative}")
    digest = hashlib.sha256()
    entries = sorted(root.rglob("*"), key=lambda path: path.relative_to(root).as_posix())
    regular = 0
    for path in entries:
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        if path.is_file() and not path.is_symlink():
            digest.update(path.read_bytes())
            regular += 1
    return digest.hexdigest(), len(entries), regular


global_constraints = [
    ("build/science_g0_prefetch/diagnostic-evidence/control-summary.json",
     "f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096"),
    ("build/science_g0_prefetch/diagnostic-evidence/prefetch-summary.json",
     "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff"),
    ("build/science_g0_prefetch/diagnostic-evidence/prefetch/"
     "prefetch-nvidia_hq-1-curl-cpl.log",
     "3312a78a073140ea1422ece0ffbc927ce853a4d7b8cec02f3033734a7e217a3a"),
    ("build/science_g0_prefetch/diagnostic-evidence/prefetch/"
     "prefetch-nvidia_hq-1-network-stats.json",
     "0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe"),
    ("build/science_g0/science-network-evidence/baseline-summary.json",
     "17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed"),
    ("build/science_g0/science-network-evidence/live-summary.json",
     "e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe"),
    ("build/science_g0_prefetch/requalification-evidence-v2/control-summary.json",
     "0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0"),
    ("build/science_g0_prefetch/requalification-evidence-v2/prefetch-summary.json",
     "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff"),
    ("build/science_g0_prefetch/requalification-evidence-v2/prefetch/"
     "prefetch-hong_kong-5-curl-cpl.log",
     "f32cfdfd05f50a3de8f34b4f69e1e2cefe61d667e20936ddcf86f1fa8b1a429e"),
    ("build/science_g0_prefetch/requalification-evidence-v2/prefetch/"
     "prefetch-hong_kong-5-network-stats.json",
     "f54110c20ab81230c5d3af708f2df4d9d8754e5957366875c162b0395d2c1974"),
]
for relative, expected in global_constraints:
    require_hash(relative, expected)
require(len(global_constraints) == 10, "Global Constraint count mismatch")
require(len({expected for _, expected in global_constraints}) == 9,
        "Global Constraint unique-hash count mismatch")

control_tree = tree_digest(
    "build/science_g0_prefetch/diagnostic-evidence/control")
candidate_tree = tree_digest(
    "build/science_g0_prefetch/diagnostic-evidence/prefetch")
require(control_tree == (
    "d3eb2a1adb60aa4a911b14e47854a2fc7b7fe7b81424de17779ae7862b888133",
    30, 30), f"rejected control tree mismatch: {control_tree}")
require(candidate_tree == (
    "054d7d76266febab932255db9658845717625d1ea348bef4021fd3330adffca0",
    2, 2), f"rejected candidate tree mismatch: {candidate_tree}")


def exact_table_block(content, start_marker, end_marker, label):
    require(content.count(start_marker) == 1,
            f"{label} start-marker count mismatch: {content.count(start_marker)}")
    require(content.count(end_marker) == 1,
            f"{label} end-marker count mismatch: {content.count(end_marker)}")
    start = content.index(start_marker)
    end = content.index(end_marker)
    require(start < end, f"{label} markers out of order")
    return content[start:end]


V2_START = ("### V2 " + "control artifact hashes\n").encode("utf-8")
V2_END = ("\n### Prefetch " + "candidate and hard stop\n").encode("utf-8")
V3_START = ("### Frozen v3 " + "artifact hashes\n").encode("utf-8")
V3_END = ("\nThe hashed decision-gate " + "transcript is").encode("utf-8")
V2_TABLE_BLOCK_SHA256 = \
    "5a15d5a87a87d1976321aff7f2afde63d4a2eb19b1c6be824c6fdd4f7d8c0f8b"
V3_TABLE_BLOCK_SHA256 = \
    "a9f3c6c2f929c01429809d684841213fcd72718f75f2274b666395c733a9e76a"

current_v2_block = exact_table_block(DOC_BYTES, V2_START, V2_END, "current v2")
base_v2_block = exact_table_block(BASE_DOC_BYTES, V2_START, V2_END, "base v2")
current_v3_block = exact_table_block(DOC_BYTES, V3_START, V3_END, "current v3")
base_v3_block = exact_table_block(BASE_DOC_BYTES, V3_START, V3_END, "base v3")
require(current_v2_block == base_v2_block, "v2 table block differs from base Git object")
require(current_v3_block == base_v3_block, "v3 table block differs from base Git object")
require(hashlib.sha256(base_v2_block).hexdigest() == V2_TABLE_BLOCK_SHA256,
        "base v2 table-block hash mismatch")
require(hashlib.sha256(current_v2_block).hexdigest() == V2_TABLE_BLOCK_SHA256,
        "current v2 table-block hash mismatch")
require(hashlib.sha256(base_v3_block).hexdigest() == V3_TABLE_BLOCK_SHA256,
        "base v3 table-block hash mismatch")
require(hashlib.sha256(current_v3_block).hexdigest() == V3_TABLE_BLOCK_SHA256,
        "current v3 table-block hash mismatch")


def table_rows(block):
    pattern = re.compile(
        r"^\| `(?P<stem>(?:optimized|prefetch)-[^`]+)` "
        r"\| `(?P<raw>[0-9a-f]{64})` "
        r"\| `(?P<stats>[0-9a-f]{64})` "
        r"\| (?P<proof>`[0-9a-f]{64}`|absent;[^|]+) \|$", re.MULTILINE)
    return list(pattern.finditer(block.decode("utf-8")))


def verify_evidence(version, table_block, expected_rows,
                    expected_iteration_files, summaries, require_mode_0400):
    root = ROOT / f"build/science_g0_prefetch/requalification-evidence-{version}"
    rows = table_rows(table_block)
    require(len(rows) == expected_rows,
            f"{version} row count mismatch: {len(rows)} != {expected_rows}")
    expected_paths = set()
    slots = iteration_files = absent = 0
    for match in rows:
        stem = match["stem"]
        subdir = "control" if stem.startswith("optimized-") else "prefetch"
        for suffix, field in (("-curl-cpl.log", "raw"),
                              ("-network-stats.json", "stats")):
            path = root / subdir / f"{stem}{suffix}"
            require(path.is_file() and not path.is_symlink(), f"missing file: {path}")
            actual = file_sha256(path)
            require(actual == match[field],
                    f"{version} hash mismatch: {path}: {actual} != {match[field]}")
            expected_paths.add(path)
            slots += 1
            iteration_files += 1
        proof_path = root / subdir / f"{stem}-proof.json"
        slots += 1
        if match["proof"].startswith("`"):
            expected = match["proof"].strip("`")
            require(proof_path.is_file() and not proof_path.is_symlink(),
                    f"missing proof: {proof_path}")
            actual = file_sha256(proof_path)
            require(actual == expected,
                    f"{version} proof mismatch: {proof_path}: {actual} != {expected}")
            expected_paths.add(proof_path)
            iteration_files += 1
        else:
            require(not proof_path.exists(), f"expected absent proof exists: {proof_path}")
            absent += 1
    for name, expected in summaries.items():
        path = root / name
        require(path.is_file() and not path.is_symlink(), f"missing summary: {path}")
        actual = file_sha256(path)
        require(actual == expected,
                f"{version} summary mismatch: {path}: {actual} != {expected}")
        expected_paths.add(path)
    actual_paths = {
        path for path in root.rglob("*") if path.is_file() and not path.is_symlink()
    }
    require(actual_paths == expected_paths,
            f"{version} complete-set mismatch: missing={expected_paths - actual_paths}; "
            f"extra={actual_paths - expected_paths}")
    require(iteration_files == expected_iteration_files,
            f"{version} iteration-file count mismatch: {iteration_files}")
    non_0400 = [
        path for path in actual_paths if stat.S_IMODE(path.stat().st_mode) != 0o400
    ]
    if require_mode_0400:
        require(not non_0400, f"{version} frozen mode mismatch: {non_0400}")
    return {
        "rows": len(rows), "slots": slots, "iteration_files": iteration_files,
        "absent": absent, "summaries": len(summaries), "regular": len(actual_paths),
        "non_0400": len(non_0400),
    }


v2 = verify_evidence(
    "v2", current_v2_block, 20, 59,
    {"control-summary.json":
     "0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0",
     "prefetch-summary.json":
     "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff"},
    False)
v3 = verify_evidence(
    "v3", current_v3_block, 12, 35,
    {"control-summary.json":
     "47abb0bc91e4afba412d1150822b90110650f8e225b45b7fa52f4627889c405c",
     "prefetch-summary.json":
     "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff"},
    True)
require(v2 == {"rows": 20, "slots": 60, "iteration_files": 59, "absent": 1,
               "summaries": 2, "regular": 61, "non_0400": 61},
        f"v2 totals mismatch: {v2}")
require(v3 == {"rows": 12, "slots": 36, "iteration_files": 35, "absent": 1,
               "summaries": 2, "regular": 37, "non_0400": 0},
        f"v3 totals mismatch: {v3}")

scratch = [
    (".superpowers/sdd/task-3-v3-preflight.log",
     "d48994e70732786983b9c5d2f9c67bb2ca0c936d0b25e69c7acefd694fbe6aff"),
    (".superpowers/sdd/task-3-v3-control.log",
     "a1d8e36c152f71771e1ad80ae548e43e397456ce6ea47fe03e50e2cad9b2d142"),
    (".superpowers/sdd/task-3-v3-candidate.log",
     "c7f98c3e8428cb72092e6e49a1fff359232c4b4463607e18d52d55aac1b907d2"),
    (".superpowers/sdd/task-3-v3-control-artifacts.sha256",
     "c81a325b4aecd6b1a722c01789270c2568fc55cb7a1b201b7a6dd8c73e4b3849"),
    (".superpowers/sdd/task-3-v3-candidate-artifacts.sha256",
     "5845a9aac3a9b7e4b88e9323b16f20ff45f1e6f99ecc7a3997175b0f791458ff"),
    (".superpowers/sdd/task-3-v3-decision-gate.log",
     "0fd10896a891e20eb8e63a755fb68f4772e2cefc36b5b30f148165101001bafa"),
    (".superpowers/sdd/task-3-v3-final-protected.log",
     "cb5bc37da993e862b4cfbfae4e8777df586fd03e05a753693b972c518dc072a9"),
    ("tests/CMakeLists.txt",
     "b9eea24fabb9fa2fb1d79bf384320e5082bd0155aedfcc18fa2d2ab33411141a"),
]
for relative, expected in scratch:
    require_hash(relative, expected)

manifest_path = ROOT / "packaging/scienceearth/g0_manifest.py"
spec = importlib.util.spec_from_file_location("v4_authorization_manifest", manifest_path)
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)
reference_path = ROOT / (
    "packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json")
ratchet_path = ROOT / (
    "packaging/scienceearth/baselines/current-macos-arm64-ratchet.json")
reference = json.loads(reference_path.read_text())
ratchet = json.loads(ratchet_path.read_text())
require(manifest.manifest_sha256(reference) ==
        "145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada",
        "reference canonical hash mismatch")
require(file_sha256(reference_path) ==
        "afd80d8ad9419882793926d781affb970246358fbf74c514b950bf0eb924a5a6",
        "reference file hash mismatch")
require(manifest.manifest_sha256(ratchet) ==
        "fd2d67424356637dd71beb4120647726836fb9a9b3cec03223bb83378fe960cb",
        "ratchet canonical hash mismatch")
require(file_sha256(ratchet_path) ==
        "a732bacfec5b16b6e42ef4b4573827df6edfa73a5d8bf5b2c7b94d0d684c06cc",
        "ratchet file hash mismatch")
require(reference["metadata"]["normalization_profile"]["source_roots_sha256"] ==
        "646b5eb80be60524ca6aa8dad55921f2966cc40562b29489483f1184a04c1936",
        "normalization descriptor hash mismatch")
require(ratchet["reference_sha256"] ==
        "145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada",
        "ratchet reference hash mismatch")
require(ratchet["releases"][0]["parent_finding_ids_sha256"] ==
        "5eeb1fc96226557050bffecfe5ab7cb11f32713fddb83c3147b1684ad675bd55",
        "ratchet parent-finding-set hash mismatch")

fixture_path = ROOT / "tests/data/science/alphaearth_rgb_cases.json"
fixture = json.loads(fixture_path.read_text())
require(file_sha256(fixture_path) ==
        "6a67af9a1380250704b9032f8b4933965196ed2a119f07be4cb99dafe032806d",
        "fixture file hash mismatch")
require(fixture["source_index_sha256"] ==
        "f738e7d274ad582e56e20a3a8b444c6f2a3ece5781f8f9855bb7ca3d9ed2942f",
        "fixture source-index hash mismatch")
require({case["name"]: case["record_fingerprint"] for case in fixture["cases"]} == {
    "nvidia_hq":
    "03956d76d3bc0d67c4f61608a1771ffb036a7e3ece6df326a2b52e66d412a0f1",
    "hong_kong":
    "deded3c50b4a91db0fe3fe97691734c7fa5a802647c1571d28f75515587a59ff",
}, "fixture record fingerprints mismatch")

sys.path.insert(0, str(ROOT))
from tests.science_bundle_audit_tests import ScienceProbeBuilderTests, MANIFEST

desktop = Path("/Users/USER/Desktop/osgSol Earth.app")
desktop_entries = list(desktop.rglob("*"))
desktop_regular = [
    path for path in desktop_entries if path.is_file() and not path.is_symlink()
]
desktop_fingerprint = MANIFEST.bundle_fingerprint(desktop)
desktop_digest = ScienceProbeBuilderTests().tree_digest(desktop)
require(desktop_fingerprint ==
        "91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18",
        "Desktop fingerprint mismatch")
require(desktop_digest ==
        "14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214",
        "Desktop helper digest mismatch")
require(len(desktop_entries) == 414, "Desktop entry count mismatch")
require(len(desktop_regular) == 355, "Desktop regular-file count mismatch")

formal = subprocess.run([
    "ctest", "--test-dir", "build/science_g0_prefetch", "-N", "-V",
    "-R", "^osgVerse_Test_ScienceGdalLive$",
], cwd=ROOT, text=True, capture_output=True, check=False)
formal_output = formal.stdout + formal.stderr
require(formal.returncode == 0, f"formal listing exit mismatch: {formal.returncode}")
require(len(re.findall(
    r"Test #[0-9]+: osgVerse_Test_ScienceGdalLive", formal_output)) == 1,
    "formal listing count mismatch")
for token in ('"--iterations" "5"', '"--profile" "optimized"',
              "science-network-evidence", '"--enforce-latency"'):
    require(token in formal_output, f"formal listing missing token: {token}")
require("formal-evidence-v4" not in formal_output,
        "formal listing contains future v4 path")

v4_paths = [
    ROOT / "build/science_g0_prefetch/requalification-evidence-v4/control",
    ROOT / "build/science_g0_prefetch/requalification-evidence-v4/prefetch",
    ROOT / "build/science_g0_prefetch/formal-evidence-v4",
    ROOT / "build/science_g0_prefetch/requalification-evidence-v4/control-summary.json",
    ROOT / "build/science_g0_prefetch/requalification-evidence-v4/prefetch-summary.json",
]
for path in v4_paths:
    require(not path.exists(), f"v4 path exists: {path}")

print(f"AUTHORIZATION_BASE={AUTHORIZATION_BASE}")
print(f"V2_TABLE_BLOCK_SHA256={V2_TABLE_BLOCK_SHA256};BASE_MATCH=YES")
print(f"V3_TABLE_BLOCK_SHA256={V3_TABLE_BLOCK_SHA256};BASE_MATCH=YES")
print("GLOBAL_CONSTRAINTS=10/10;UNIQUE_SHA256=9")
print("REJECTED_TREES=2/2;CONTROL=30/30;CANDIDATE=2/2")
print("V2=20_ROWS/60_SLOTS/59_HASHES/1_ABSENT/2_SUMMARIES/61_FILES")
print("V3=12_ROWS/36_SLOTS/35_HASHES/1_ABSENT/2_SUMMARIES/37_FILES/0_WRITABLE")
print("V3_SCRATCH_AND_CMAKE=8/8")
print("REFERENCE_RATCHET_FIXTURE=PASS")
print(f"DESKTOP={desktop_fingerprint}/{desktop_digest}/414/355")
print("FORMAL=1_TEST/5_ITERATIONS/OPTIMIZED/NO_FORMAL_V4")
print("V4_ABSENT=5/5")
print("AUTHORIZATION_VERIFIER=PASS")
```
<!-- V4_AUTHORIZATION_VERIFIER_END -->

The exact extraction and invocation are:

```bash
verifier=$(mktemp "${TMPDIR:-/tmp}/osgsol-v4-authorization-verifier.XXXXXX")
trap 'rm -f "$verifier"' EXIT
awk '
  $0 == "<!-- V4_AUTHORIZATION_VERIFIER_BEGIN -->" { capture = 1; next }
  $0 == "<!-- V4_AUTHORIZATION_VERIFIER_END -->" { exit }
  capture && $0 == "```python" { next }
  capture && $0 == "```" { next }
  capture { print }
' docs/scienceearth/g0-measurements.md > "$verifier"
test "$(shasum -a 256 "$verifier" | awk '{print $1}')" = \
  '807c3a1c5e0c4000dcb2ba2e3da06c97900b1fec099aed372455ec8a5294ad30'
PYTHONDONTWRITEBYTECODE=1 python3 "$verifier"
```

A read-only Python verifier first loaded the immutable authorization-base document from Git,
required unique fail-closed boundaries and exact base/current byte equality for both v2/v3 table
blocks, and checked their hard SHA-256 bindings before parsing any iteration hash. It then derived
each raw/stats/proof path, compared the complete regular-file sets, and used the committed
sorted-relative-path-plus-file-bytes tree helper for the rejected diagnostic roots. The documented
invocation exited 0 in 0.57 seconds wall time with:

```text
AUTHORIZATION_BASE=4a1c1172e02ce13b5fd6452a97834dbd9f2001b3
V2_TABLE_BLOCK_SHA256=5a15d5a87a87d1976321aff7f2afde63d4a2eb19b1c6be824c6fdd4f7d8c0f8b;BASE_MATCH=YES
V3_TABLE_BLOCK_SHA256=a9f3c6c2f929c01429809d684841213fcd72718f75f2274b666395c733a9e76a;BASE_MATCH=YES
GLOBAL_CONSTRAINTS=10/10;UNIQUE_SHA256=9
REJECTED_TREES=2/2;CONTROL=30/30;CANDIDATE=2/2
V2=20_ROWS/60_SLOTS/59_HASHES/1_ABSENT/2_SUMMARIES/61_FILES
V3=12_ROWS/36_SLOTS/35_HASHES/1_ABSENT/2_SUMMARIES/37_FILES/0_WRITABLE
V3_SCRATCH_AND_CMAKE=8/8
REFERENCE_RATCHET_FIXTURE=PASS
DESKTOP=91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18/14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214/414/355
FORMAL=1_TEST/5_ITERATIONS/OPTIMIZED/NO_FORMAL_V4
V4_ABSENT=5/5
AUTHORIZATION_VERIFIER=PASS
```

The verifier opens protected evidence read-only. Its final hashes and complete file sets matched,
and all 37 frozen v3 files retained mode `0400` with zero write bits. This final-state audit cannot
prove the absence of a transient mutation before verification. The critical bindings remained:

| Historical binding | Fresh SHA-256 |
|---|---|
| Rejected diagnostic control summary | `f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096` |
| Rejected diagnostic candidate summary | `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff` |
| Rejected diagnostic candidate raw | `3312a78a073140ea1422ece0ffbc927ce853a4d7b8cec02f3033734a7e217a3a` |
| Rejected diagnostic candidate statistics | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` |
| Older baseline formal summary | `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed` |
| Older optimized/live formal summary | `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe` |
| V2 control summary | `0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0` |
| V2 candidate summary | `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff` |
| V2 Hong Kong iteration-5 raw | `f32cfdfd05f50a3de8f34b4f69e1e2cefe61d667e20936ddcf86f1fa8b1a429e` |
| V2 Hong Kong iteration-5 statistics | `f54110c20ab81230c5d3af708f2df4d9d8754e5957366875c162b0395d2c1974` |
| V3 control summary | `47abb0bc91e4afba412d1150822b90110650f8e225b45b7fa52f4627889c405c` |
| V3 candidate summary | `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff` |

The current reference/ratchet canonical hashes remained
`145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada` and
`fd2d67424356637dd71beb4120647726836fb9a9b3cec03223bb83378fe960cb`; their file hashes
remained `afd80d8ad9419882793926d781affb970246358fbf74c514b950bf0eb924a5a6` and
`a732bacfec5b16b6e42ef4b4573827df6edfa73a5d8bf5b2c7b94d0d684c06cc`.
The fixture file, source-index binding, and NVIDIA/Hong Kong record fingerprints also remained
exactly the values documented above.

The exact committed Desktop helper was invoked read-only and returned:

```text
desktop_fingerprint=91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18
desktop_helper_digest=14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214
desktop_entries=414
desktop_regular_non_symlink=355
```

The formal test was listed without execution using:

```bash
ctest --test-dir build/science_g0_prefetch -N -V \
  -R '^osgVerse_Test_ScienceGdalLive$'
```

It exited 0 and listed exactly one test with `--iterations 5`, `--profile optimized`, the existing
`science-network-evidence` paths, and `--enforce-latency`. It contained no future
`formal-evidence-v4` path. The final five-path absence loop exited 0 for:

```text
build/science_g0_prefetch/requalification-evidence-v4/control
build/science_g0_prefetch/requalification-evidence-v4/prefetch
build/science_g0_prefetch/formal-evidence-v4
build/science_g0_prefetch/requalification-evidence-v4/control-summary.json
build/science_g0_prefetch/requalification-evidence-v4/prefetch-summary.json
```

The exact final combined gate is preserved below. It extracts and hash-binds the verifier, requires
its exact exit/output, rebinds every private/audit artifact and audit status, verifies the signature,
export, direct dependencies, and forbidden-path absence, requires all five recorded regression
status rows, verifies the immutable base object and exact base-to-HEAD two-document range, requires
a clean tracked worktree/index, checks terminal authorization, and fails on any missing or mismatched
prerequisite.

<!-- V4_FINAL_COMBINED_GATE_BEGIN -->
```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT=/Users/USER/osgsol/.worktrees/v0.2-runtime-safety
DOC="$ROOT/docs/scienceearth/g0-measurements.md"
AUTHORIZATION_BASE=4a1c1172e02ce13b5fd6452a97834dbd9f2001b3
EXPECTED_VERIFIER_SHA=807c3a1c5e0c4000dcb2ba2e3da06c97900b1fec099aed372455ec8a5294ad30
cd "$ROOT"

tracked_status=$(git status --porcelain=v1 --untracked-files=no)
test -z "$tracked_status"
git cat-file -e "${AUTHORIZATION_BASE}^{commit}"
git merge-base --is-ancestor "$AUTHORIZATION_BASE" HEAD
authorization_head=$(git rev-parse HEAD)
authorization_range="$AUTHORIZATION_BASE..$authorization_head"
expected_range_files=$(printf '%s\n' \
  docs/scienceearth/g0-measurements.md docs/scienceearth/gdal-build.md | sort)
actual_range_files=$(git diff --name-only "$authorization_range" | sort)
test "$actual_range_files" = "$expected_range_files"
git diff --check "$authorization_range"

verifier=$(mktemp "${TMPDIR:-/tmp}/osgsol-v4-authorization-verifier.XXXXXX")
trap 'rm -f "$verifier"' EXIT
awk '
  $0 == "<!-- V4_AUTHORIZATION_VERIFIER_BEGIN -->" { capture = 1; next }
  $0 == "<!-- V4_AUTHORIZATION_VERIFIER_END -->" { exit }
  capture && $0 == "```python" { next }
  capture && $0 == "```" { next }
  capture { print }
' "$DOC" > "$verifier"
actual_verifier_sha=$(shasum -a 256 "$verifier" | awk '{print $1}')
test "$actual_verifier_sha" = "$EXPECTED_VERIFIER_SHA"

verifier_output=$(PYTHONDONTWRITEBYTECODE=1 python3 "$verifier")
expected_verifier_output=$(cat <<'EOF'
AUTHORIZATION_BASE=4a1c1172e02ce13b5fd6452a97834dbd9f2001b3
V2_TABLE_BLOCK_SHA256=5a15d5a87a87d1976321aff7f2afde63d4a2eb19b1c6be824c6fdd4f7d8c0f8b;BASE_MATCH=YES
V3_TABLE_BLOCK_SHA256=a9f3c6c2f929c01429809d684841213fcd72718f75f2274b666395c733a9e76a;BASE_MATCH=YES
GLOBAL_CONSTRAINTS=10/10;UNIQUE_SHA256=9
REJECTED_TREES=2/2;CONTROL=30/30;CANDIDATE=2/2
V2=20_ROWS/60_SLOTS/59_HASHES/1_ABSENT/2_SUMMARIES/61_FILES
V3=12_ROWS/36_SLOTS/35_HASHES/1_ABSENT/2_SUMMARIES/37_FILES/0_WRITABLE
V3_SCRATCH_AND_CMAKE=8/8
REFERENCE_RATCHET_FIXTURE=PASS
DESKTOP=91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18/14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214/414/355
FORMAL=1_TEST/5_ITERATIONS/OPTIMIZED/NO_FORMAL_V4
V4_ABSENT=5/5
AUTHORIZATION_VERIFIER=PASS
EOF
)
test "$verifier_output" = "$expected_verifier_output"

check_hash()
{
    local relative=$1 expected=$2 actual
    actual=$(shasum -a 256 "$relative" | awk '{print $1}')
    test "$actual" = "$expected"
}

check_hash packaging/science_deps/gdal-3.13.1-parallel-head-range.patch \
  5ce830f7853db1c6f53b833741261f55f382c0b8972161999a2d34b99c3ab15f
test "$(sed -n 's/^GDAL_PREFETCH_PATCH_SHA256=//p' \
  packaging/science_deps/versions.env)" = \
  5ce830f7853db1c6f53b833741261f55f382c0b8972161999a2d34b99c3ab15f
check_hash tests/data/science/prefetch_nvidia_transient_retry_trace.log \
  124564316b6a1cfc0dd89ada2c0d4d43ca503dfe5e5b122b3a50eff605f5dd46
check_hash tests/data/science/prefetch_nvidia_transient_retry_stats.json \
  ea88aae77d69537b90dbd5e6c7188d37ad31837b3500e614cb0f3500d48d42b6
check_hash build/science-deps-prefetch/prefix/science-deps-manifest.json \
  a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116
check_hash build/science_g0_prefetch/lib/osgdb_science_g0_probe.so \
  1184bb101c5f8a3fb0bbff26b0efaa1987f002306a949fc51c587e20fdb1a218
check_hash build/science_g0_prefetch/bundle-audit.json \
  eee5b4b4dd5c946f802a2dd92296efec47e1fb9ed357a750295b8d1214fb217d
check_hash build/science_g0_prefetch/bundle-audit.txt \
  975065237553f763bb71e189330dbf206a56723145cbfab95c45c4ec09c4950e

jq -e '
  .status == "PASS" and .ok == true and .exit_code == 0 and
  .sizes.baseline_bytes == 542594200 and .sizes.total_bytes == 563895343 and
  .sizes.delta_bytes == 21301143 and .sizes.science_closure_bytes == 21300984 and
  (.graph | length) == 128 and (.unresolved | length) == 0 and
  (.absolute.science | length) == 0 and (.absolute.non_science | length) == 1086 and
  (.delta.new | length) == 0 and (.delta.removed | length) == 0 and
  (.violations | length) == 0 and (.findings | length) == 1086
' build/science_g0_prefetch/bundle-audit.json >/dev/null

/usr/bin/codesign --verify --deep --strict \
  'build/science_g0_prefetch/osgSol Science G0 Probe.app'
test "$(nm -gU build/science_g0_prefetch/lib/osgdb_science_g0_probe.so | \
  awk '{print $3}')" = _osgsol_science_g0_probe_anchor
actual_dependencies=$(otool -L \
  build/science_g0_prefetch/lib/osgdb_science_g0_probe.so | \
  tail -n +2 | awk '{print $1}' | sort)
expected_dependencies=$(printf '%s\n' \
  /usr/lib/libSystem.B.dylib /usr/lib/libc++.1.dylib \
  /usr/lib/libcurl.4.dylib /usr/lib/libsqlite3.dylib | sort)
test "$actual_dependencies" = "$expected_dependencies"
path_hits=$( { otool -L build/science_g0_prefetch/lib/osgdb_science_g0_probe.so; \
  otool -l build/science_g0_prefetch/lib/osgdb_science_g0_probe.so; \
  strings -a build/science_g0_prefetch/lib/osgdb_science_g0_probe.so; } | \
  rg -n '/Users/USER/osgsol/\.worktrees|science-deps-prefetch' || true )
test -z "$path_hits"

for required_record in \
  '| Manifest and bundle-audit Python suites | 0 | 66/66, zero skip/failure | 5.361 s internal; 5.389 s wall |' \
  '| Private dependency builder contract | 0 | PASS | 1.151 s wall |' \
  '| `build/osgsol_core` CTest | 0 | 16/16 | 10.27 s CTest real |' \
  '| Selected `build/science_g0_prefetch` CTest | 0 | 7/7 | 11.88 s CTest real |' \
  '| Standalone private-prefix verifier | 0 | PASS | 1.264 s wall |'
do
    rg -Fqx "$required_record" "$DOC"
done

expected_decision=$(printf '%s\n' G0_DECISION=STOP \
  PUBLIC_REQUALIFICATION_V4=AUTHORIZED_NOT_RUN DESKTOP_PACKAGE=NOT_READY)
test "$(tail -n 3 docs/scienceearth/gdal-build.md)" = "$expected_decision"
test "$(tail -n 3 docs/scienceearth/g0-measurements.md)" = "$expected_decision"
test -z "$(find . -type d -name __pycache__ -prune -print)"

echo "VERIFIER_SOURCE_SHA256=$actual_verifier_sha"
echo "AUTHORIZATION_RANGE=$authorization_range"
echo 'TRACKED_WORKTREE_INDEX=CLEAN'
printf '%s\n' "$verifier_output"
echo 'PRIVATE_BINDINGS=7/7'
echo 'AUDIT_SIGNATURE_EXPORT_DEPENDENCY_PATH=PASS'
echo 'RECORDED_REGRESSION_RESULTS=5/5'
echo 'DOC_RANGE_SCOPE_AND_CONTENT=PASS'
echo 'FINAL_COMBINED_GATE=PASS'
```
<!-- V4_FINAL_COMBINED_GATE_END -->

The exact extraction and execution of that preserved gate are:

```bash
gate=$(mktemp "${TMPDIR:-/tmp}/osgsol-v4-final-combined-gate.XXXXXX")
trap 'rm -f "$gate"' EXIT
awk '
  $0 == "<!-- V4_FINAL_COMBINED_GATE_BEGIN -->" { capture = 1; next }
  $0 == "<!-- V4_FINAL_COMBINED_GATE_END -->" { exit }
  capture && $0 == "```bash" { next }
  capture && $0 == "```" { next }
  capture { print }
' docs/scienceearth/g0-measurements.md > "$gate"
bash "$gate"
```

This evidence authorizes exactly one later v4 public control/candidate set. It does not execute or
prejudge that set, promote the formal profile, authorize Desktop packaging, or start G1. The failed
v3 result and all earlier STOP evidence remain binding.

G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V4=AUTHORIZED_NOT_RUN
DESKTOP_PACKAGE=NOT_READY

## Bounded-retry v4 recorded public and formal decision

The immutable pre-network snapshot ran at `2026-07-13T20:28:20+0800` on clean commit `7a0abbc5212f88e191a0b2279c78d8cc84eb4422`. Both `HTTP_PROXY` and `HTTPS_PROXY` were credential-free loopback HTTP endpoint classes. The preserved authorization verifier and combined gate passed, and all five v4 output paths were absent at that snapshot. That absence is a pre-public state observation, not a proof of later process counts. Its transcript SHA-256 is `2bae6ec0237839cc14cdde0f90c8084b89b46336ecee20ebaf4598b7d6d2f22b`.

The snapshot re-bound the final patch and private-prefix manifest to `5ce830f7853db1c6f53b833741261f55f382c0b8972161999a2d34b99c3ab15f` and `a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116`. It rechecked all 10 Global Constraints, both rejected trees, all 61 v2 files, all 37 non-writable v3 files, the reference/ratchet/fixture identities, and the protected Desktop fingerprint/helper/count tuple `91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18 / 14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214 / 414 / 355`. The formal CTest still listed one optimized, five-iteration, latency-enforced command. `tests/CMakeLists.txt` then hashed to `b9eea24fabb9fa2fb1d79bf384320e5082bd0155aedfcc18fa2d2ab33411141a`.

### Recorded execution ledger

| Recorded stage | Start / finish | Preserved transcript SHA-256 | Exit marker | Frozen artifacts | Result |
|---|---|---|---:|---:|---|
| Optimized control | `2026-07-13T20:28:47+0800` / `2026-07-13T20:29:20+0800` | `b2ec9bf61a949a80b6a0fa6c87491ccff79837ab3a201aff445a07423f2f323b` | 0 | 10 raw / 10 stats / 10 proof | Diagnostic `FAIL` on both medians; non-enforced |
| Prefetch candidate | `2026-07-13T20:30:05+0800` / `2026-07-13T20:30:33+0800` | `81b40fbb098716e59ceada54b7bf33079fa7fcdf8a866aeb1d1828aafe2f434c` | 0 | 10 raw / 10 stats / 10 proof | Complete qualification `PASS` |
| Promoted formal CTest | `2026-07-13T20:37:45+0800` / `2026-07-13T20:38:14+0800` | `29b280119a919e1ae27a4adb18a24efa20c6254c91bf0d239f0b821edbbf0a00` | 8 | 10 raw / 10 stats / 10 proof | Formal `FAIL`: NVIDIA median `3207.465916 ms` |

This is a recorded execution ledger, not an independently derived process-count fact. The final scoped filesystem preserves one expected transcript and one 31-entry artifact set per stage, with unique expected timestamps and hashes, and contains no evidence of a rerun. The absolute absence of an omitted or overwritten rerun cannot be proven from final filesystem state. Every one of the 93 summary/raw/stats/proof artifacts is mode `0400`; the control, candidate, requalification-parent, and formal evidence directories are mode `0500`.

### Control per-iteration measurements

Times are `total / open / georeference / read / close` in milliseconds. Retry counts are `coordinator / ordinary`; bytes are `successful / actual / declared transient / conservative`.

| Case | Iteration | Phase times | GET actual/success | HEAD / stats GET | Retries | Bytes | Response codes |
|---|---:|---:|---:|---:|---:|---:|---|
| `nvidia_hq` | 1 | 4056.281667 / 2360.760917 / 9.633167 / 1685.806791 / 0.080792 | 8/7 | 1 / 2 | 0 / 1 | 5010811 / 5010811 / 17 / 5010828 | 200,200,206,500,206,206,206,206,206,206 |
| `nvidia_hq` | 2 | 3272.290542 / 1750.258709 / 1.515000 / 1520.442666 / 0.074167 | 10/7 | 1 / 2 | 0 / 3 | 5010811 / 5010811 / 51 / 5010862 | 200,200,206,206,206,500,500,500,206,206,206,206 |
| `nvidia_hq` | 3 | 3537.941083 / 1855.944625 / 1.486583 / 1680.431750 / 0.078125 | 10/7 | 1 / 2 | 0 / 3 | 5010811 / 5010811 / 51 / 5010862 | 200,200,206,206,500,500,500,206,206,206,206,206 |
| `nvidia_hq` | 4 | 3391.137834 / 1881.673417 / 1.259083 / 1508.123042 / 0.082292 | 8/7 | 1 / 2 | 0 / 1 | 5010811 / 5010811 / 17 / 5010828 | 200,200,206,206,206,206,500,206,206,206 |
| `nvidia_hq` | 5 | 3362.548834 / 2221.456334 / 1.179583 / 1139.825000 / 0.087917 | 7/7 | 1 / 2 | 0 / 0 | 5010811 / 5010811 / 0 / 5010811 | 200,200,206,206,206,206,206,206,206 |
| `hong_kong` | 1 | 3231.595583 / 1705.478083 / 0.644458 / 1525.311750 / 0.161292 | 5/4 | 1 / 2 | 0 / 1 | 1264092 / 1264092 / 17 / 1264109 | 200,200,206,500,206,206,206 |
| `hong_kong` | 2 | 3319.029083 / 1810.487875 / 0.647375 / 1507.727958 / 0.165875 | 5/4 | 1 / 2 | 0 / 1 | 1264092 / 1264092 / 17 / 1264109 | 200,200,206,500,206,206,206 |
| `hong_kong` | 3 | 3237.080583 / 1726.223833 / 0.754292 / 1509.904750 / 0.197708 | 5/4 | 1 / 2 | 0 / 1 | 1264092 / 1264092 / 17 / 1264109 | 200,200,206,500,206,206,206 |
| `hong_kong` | 4 | 2711.758292 / 1882.907459 / 1.485166 / 827.285042 / 0.080625 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,200,206,206,206,206 |
| `hong_kong` | 5 | 3072.389250 / 1854.121334 / 1.468708 / 1216.627875 / 0.171333 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,200,206,206,206,206 |

| Case | Median / P95 ms | GET actual/success | HEAD / stats GET | Retries | Bytes | Response codes | Status |
|---|---:|---:|---:|---:|---:|---|---|
| `nvidia_hq` | 3391.137834 / 4056.281667 | 43/35 | 5 / 10 | 0 / 8 | 25054055 / 25054055 / 136 / 25054191 | 200,206,500 | FAIL |
| `hong_kong` | 3231.595583 / 3319.029083 | 23/20 | 5 / 10 | 0 / 3 | 6320460 / 6320460 / 51 / 6320511 | 200,206,500 | FAIL |

### Candidate per-iteration measurements

Times are `total / open / georeference / read / close` in milliseconds. Retry counts are `coordinator / ordinary`; bytes are `successful / actual / declared transient / conservative`.

| Case | Iteration | Phase times | GET actual/success | HEAD / stats GET | Retries | Bytes | Response codes |
|---|---:|---:|---:|---:|---:|---:|---|
| `nvidia_hq` | 1 | 2745.894875 / 1539.766791 / 9.774959 / 1196.236625 / 0.116500 | 7/7 | 1 / 2 | 0 / 0 | 5010811 / 5010811 / 0 / 5010811 | 200,206,200,206,206,206,206,206,206 |
| `nvidia_hq` | 2 | 3132.981917 / 1501.819208 / 1.482250 / 1629.599917 / 0.080542 | 9/7 | 1 / 2 | 0 / 2 | 5010811 / 5010811 / 34 / 5010845 | 200,200,206,500,500,206,206,206,206,206,206 |
| `nvidia_hq` | 3 | 3360.568375 / 1525.799458 / 1.494500 / 1833.192750 / 0.081667 | 9/7 | 1 / 2 | 0 / 2 | 5010811 / 5010811 / 34 / 5010845 | 200,200,206,500,500,206,206,206,206,206,206 |
| `nvidia_hq` | 4 | 2688.067250 / 1444.372459 / 1.222750 / 1242.393750 / 0.078291 | 7/7 | 1 / 2 | 0 / 0 | 5010811 / 5010811 / 0 / 5010811 | 200,200,206,206,206,206,206,206,206 |
| `nvidia_hq` | 5 | 2896.292875 / 1338.875542 / 1.496500 / 1555.836791 / 0.084042 | 9/7 | 1 / 2 | 0 / 2 | 5010811 / 5010811 / 34 / 5010845 | 200,200,206,206,206,500,500,206,206,206,206 |
| `hong_kong` | 1 | 3459.563000 / 2588.177166 / 1.483042 / 869.720958 / 0.181834 | 5/4 | 1 / 2 | 1 / 0 | 1264092 / 1264109 / 17 / 1264109 | 200,500,200,206,206,206,206 |
| `hong_kong` | 2 | 2313.934292 / 1480.717667 / 0.926583 / 832.108542 / 0.181500 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,200,206,206,206,206 |
| `hong_kong` | 3 | 2611.634167 / 1491.738459 / 0.650583 / 1119.073958 / 0.171167 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,206,200,206,206,206 |
| `hong_kong` | 4 | 2305.279750 / 1474.685041 / 0.646375 / 829.775459 / 0.172875 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,200,206,206,206,206 |
| `hong_kong` | 5 | 2258.564000 / 1436.755416 / 0.645750 / 820.988459 / 0.174375 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,206,200,206,206,206 |

| Case | Median / P95 ms | GET actual/success | HEAD / stats GET | Retries | Bytes | Response codes | Status |
|---|---:|---:|---:|---:|---:|---|---|
| `nvidia_hq` | 2896.292875 / 3360.568375 | 41/35 | 5 / 10 | 0 / 6 | 25054055 / 25054055 / 102 / 25054157 | 200,206,500 | PASS |
| `hong_kong` | 2313.934292 / 3459.563000 | 21/20 | 5 / 10 | 1 / 0 | 6320460 / 6320477 / 17 / 6320477 | 200,206,500 | PASS |

The candidate has ten complete correctness proofs. All ten retain the exact first `bytes=0-131071` interval, HTTP/2 for HEAD and Range, a shared connection, initial overlap, publication, correct CRS/georeference/bounding box, exact request/retry/byte reconciliation, and a conservative transfer below `16 MiB`. NVIDIA used six ordinary HTTP 500 retries and no coordinator retry. Hong Kong iteration 1 used one bounded coordinator retry: HTTP/2 `500`, 17 bytes, attempt 1, 100 ms, followed by the exact Range on the same connection and successful publication. The set has zero terminal fallback. The committed post-hoc audit source below recomputes these gates from the frozen raw/stats/proof triplets and hard-coded bindings.

Candidate PASS authorized the CMake-only promotion. Commit `bede4b09c93d2383e7467000538bae1c7b03dbcd` changes only the formal command to profile `prefetch`, keeps `--iterations 5 --enforce-latency`, and uses `build/science_g0_prefetch/formal-evidence-v4`. The promoted `tests/CMakeLists.txt` SHA-256 is `029bb3044c5dfcf2701cdbedd4cfe4ebe3488b8e23ef6caf9683fb23c10d4a0c`. The non-executing refreshed CTest listing transcript hashes to `5e8873e10bbe939d39451dc93da7e40ba25d39005c7284f979d867e8f78c14b9`.

### Formal per-iteration measurements

Times are `total / open / georeference / read / close` in milliseconds. Retry counts are `coordinator / ordinary`; bytes are `successful / actual / declared transient / conservative`.

| Case | Iteration | Phase times | GET actual/success | HEAD / stats GET | Retries | Bytes | Response codes |
|---|---:|---:|---:|---:|---:|---:|---|
| `nvidia_hq` | 1 | 3376.432625 / 1531.704708 / 10.075167 / 1834.566500 / 0.086250 | 8/7 | 1 / 2 | 0 / 1 | 5010811 / 5010811 / 17 / 5010828 | 200,200,206,500,206,206,206,206,206,206 |
| `nvidia_hq` | 2 | 3259.630084 / 1483.191250 / 0.918417 / 1775.444833 / 0.075584 | 9/7 | 1 / 2 | 0 / 2 | 5010811 / 5010811 / 34 / 5010845 | 200,200,206,500,500,206,206,206,206,206,206 |
| `nvidia_hq` | 3 | 2831.934542 / 1334.444792 / 1.497541 / 1495.909042 / 0.083167 | 9/7 | 1 / 2 | 0 / 2 | 5010811 / 5010811 / 34 / 5010845 | 200,200,206,206,206,500,500,206,206,206,206 |
| `nvidia_hq` | 4 | 3207.465916 / 1467.750333 / 1.357917 / 1738.278083 / 0.079583 | 10/7 | 1 / 2 | 0 / 3 | 5010811 / 5010811 / 51 / 5010862 | 200,200,206,206,500,500,500,206,206,206,206,206 |
| `nvidia_hq` | 5 | 3181.286834 / 1479.039917 / 0.372667 / 1701.798375 / 0.075875 | 10/7 | 1 / 2 | 0 / 3 | 5010811 / 5010811 / 51 / 5010862 | 200,200,206,500,500,500,206,206,206,206,206,206 |
| `hong_kong` | 1 | 2645.901875 / 1671.559750 / 1.491333 / 972.673042 / 0.177750 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,200,206,206,206,206 |
| `hong_kong` | 2 | 2829.896208 / 1978.975375 / 1.479125 / 849.258625 / 0.183083 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,200,206,206,206,206 |
| `hong_kong` | 3 | 2257.364625 / 1442.725208 / 1.490375 / 812.965417 / 0.183625 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,200,206,206,206,206 |
| `hong_kong` | 4 | 2474.804959 / 1546.867834 / 1.503291 / 926.252959 / 0.180875 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,200,206,206,206,206 |
| `hong_kong` | 5 | 2369.495208 / 1446.476250 / 1.292333 / 921.546708 / 0.179917 | 4/4 | 1 / 2 | 0 / 0 | 1264092 / 1264092 / 0 / 1264092 | 200,200,206,206,206,206 |

| Case | Median / P95 ms | GET actual/success | HEAD / stats GET | Retries | Bytes | Response codes | Status |
|---|---:|---:|---:|---:|---:|---|---|
| `nvidia_hq` | 3207.465916 / 3376.432625 | 46/35 | 5 / 10 | 0 / 11 | 25054055 / 25054055 / 187 / 25054242 | 200,206,500 | FAIL |
| `hong_kong` | 2474.804959 / 2829.896208 | 20/20 | 5 / 10 | 0 / 0 | 6320460 / 6320460 / 0 / 6320460 | 200,206 | PASS |

The formal artifact set contains all ten correctness/range/byte proofs with zero coordinator retries and zero terminal fallback. It nevertheless fails the immutable latency gate: NVIDIA median `3207.465916 ms` exceeds `3000 ms` by `207.465916 ms`; NVIDIA P95 `3376.432625 ms` passes `8000 ms`. Hong Kong median/P95 `2474.804959 / 2829.896208 ms` both pass. The preserved formal transcript carries exit marker 8. The recorded ledger reports no formal rerun and no downstream execution; that negative process history is not independently provable. The committed post-hoc audit below recomputes the formal result from frozen evidence.

### Control immutable artifact hashes

```text
e19d0dceaf396c039497b52c79f5aff5877740c3bef3dca3fd17adf3fbdc4357  build/science_g0_prefetch/requalification-evidence-v4/control-summary.json
b2ec9bf61a949a80b6a0fa6c87491ccff79837ab3a201aff445a07423f2f323b  .superpowers/sdd/task-3-v4-control.log
ce35d41589010eba54b3ccee7b486fb5bbb7dd988141752eba41375ebed393a3  .superpowers/sdd/task-3-v4-control-artifacts.sha256
```

| Stem | Raw SHA-256 | Stats SHA-256 | Proof SHA-256 |
|---|---|---|---|
| `optimized-hong_kong-1` | `683ee2d4ba1418b077acf518b62721ab780642b55d58398e3b276e1912c81fa1` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `0b6bb0051a457bd235202c6b3ebb69ce1a36fc667a51ea621cdc78670b21c19e` |
| `optimized-hong_kong-2` | `797dd001ea94f5d4e969316edc459b592ee71c3b4223ea575e2e1e84e087b74c` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `e5ce172da6dc7cd013eb20da712a871a25587be2c5815ed278c4d2b494926655` |
| `optimized-hong_kong-3` | `3886124744a09096e667440cabf84738355eabc6f86cb0df6a0c8d97f9edf647` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `0b6bb0051a457bd235202c6b3ebb69ce1a36fc667a51ea621cdc78670b21c19e` |
| `optimized-hong_kong-4` | `c54dcef56535a1cda25be83299cc1d336aa5b94ebf5e8511851486dab0d69062` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `2a0a931475ff99c7a4545e41dc9172bea0841b294a735c7a02f737c9367b0a63` |
| `optimized-hong_kong-5` | `7cdcdc0349c07d9ff5951ba5ec8cdcc94a791edcb5b157a74196fae881565a35` | `479d4278a12e282efbabdb4d0aa3c9e88ba35df983e89d53da848fda3a8e0d9f` | `3c745f9c2bb574d7b2c1d5b46bd1d2d2a7a21ad1300607f07374f304aa117a25` |
| `optimized-nvidia_hq-1` | `cee7cbf5b2f8983b446c09cafbfe6ef00e0f2033d25c353132f65db81c3d758b` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `476dcc4a20c5e8978f6a2a9dfdaeaeb2c9d799c0836e24255b46aca7e3067788` |
| `optimized-nvidia_hq-2` | `679749925d79f3061f1ac99697a3c9efdaa07f50e14bd7108d1aa37a498adce7` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `3d606a22267950cb39692ac4af6be5b441ff538d5540fd897cd71cec13eaa76a` |
| `optimized-nvidia_hq-3` | `98b946ccf2299a7ef5353591acc532cda94ba40e0fd7175c23702a2aedecc6b9` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `5c1e35782ab38345d37ab8fe2f413ac52f53eaf4e408d41cf2e5d0329dcfb877` |
| `optimized-nvidia_hq-4` | `68ff8c84930fab4d88453e8c6f353c740dceb027b274e8a0a7999261f4ac214b` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `20c5edf44269509223276ab8f368e3f4e5b7b64321b673b6cc7b15f2b5b27565` |
| `optimized-nvidia_hq-5` | `664193aad06b3c9455f53cd90c845d825b8e34b1cbcb77cc9388f00a5d5672d1` | `9901ae9eae63b68f1a6a5ccf2bf40578ccf479ec57640f18aac1e0cc14cef288` | `a3ebab021769a0f1628b18436c9a5da2fa6f8ae800b122becdda0a413d976f76` |

### Candidate immutable artifact hashes

```text
ac37e643a7e34840a30cc7dd6f3c17c2464a632936f7366396f5f09cd5f521d4  build/science_g0_prefetch/requalification-evidence-v4/prefetch-summary.json
81b40fbb098716e59ceada54b7bf33079fa7fcdf8a866aeb1d1828aafe2f434c  .superpowers/sdd/task-3-v4-candidate.log
81451c29000dab2bcaa262615eb8e9d72ad382adae3269892581423b2587b339  .superpowers/sdd/task-3-v4-candidate-artifacts.sha256
```

| Stem | Raw SHA-256 | Stats SHA-256 | Proof SHA-256 |
|---|---|---|---|
| `prefetch-hong_kong-1` | `5ad7ab1db642d2b49e310911724a33da2107486924d57902ad945ea2479f6dcd` | `b2fa53f61c51e04f947b494cd6ab87c121118b0bec25dae40b52da8808a37ca9` | `d6339159cae624c7655eafb3dcb988d203c06625d5fd1bd006025550356e6c5d` |
| `prefetch-hong_kong-2` | `095d1d8f835a5405f90f7e5b57b8ed0b4802da7f173f0d6689ad524ca9a58ef3` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `712565d303aa52c450f1d7d4a251d93c04454df4b3fadabdb3ab3114d3f364a6` |
| `prefetch-hong_kong-3` | `e3e93cc8c242beeab0efd4269ff0a5f0a1e454b1889109eb25114385ff979ac6` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `922ffda94d802c8da9f98e879d999b635e0aae13021bd3ee05802cb3f7eed237` |
| `prefetch-hong_kong-4` | `71fdb8d994811ee1eef1364d80c624642ebe46bdf6b2566db16fdc3725b1cd2c` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `7e3ee677fa50dfc13dad094898ec23a892e1e9d12cf14675036256e3bd91be42` |
| `prefetch-hong_kong-5` | `dff632dc492f1464a7924eeb846e23cc700c10dcb18c52978b0939dba0c45d15` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `007cf906142b4e5c03939c422f5e7593a646d497144651dcfbc85dfcfa4f5281` |
| `prefetch-nvidia_hq-1` | `c5489375f3e1c0c18103deb60f2154721df7551e7776e961c322f68c213b73c4` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `bbacc88ee825d26d5f8af118468bf14905598e9efec227d08fd4ba1b71bf4487` |
| `prefetch-nvidia_hq-2` | `684f0f6702f28d3696b109f626fe0c05600018f695f3c6602d36240e1da1e57a` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `92b758a47c243fd98b35df96b2a797352b597c224dc47d4270ba33150db20561` |
| `prefetch-nvidia_hq-3` | `26af8fc183682d89852351f01196d7f733744769b4c90ef10e5fc175770e4a9b` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `4a49972e32dc8ef5adf3e544dd607b4f104720bae86307975b55ac854728ad83` |
| `prefetch-nvidia_hq-4` | `936d1ad936630be31ff75ed94cded3b0bd3ef7b80ef4132b35bcde44e3923da1` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `f70d43f2491c89e578d25ad3b3e526a32a78d3ab9879a1e5af755db60e1f2708` |
| `prefetch-nvidia_hq-5` | `bbd19f52f5ada4155ad0e8d58cc8ccb340de7489c1fb2d48eff061bf1b329587` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `645ee3b95758cc9d40248984280d63b76b194bd8c90f01d1b7130bb20dc8ff89` |

### Formal immutable artifact hashes

```text
e7664bb1ebe09dd8f53baf30c81810ff06a62be5b26cc2e267ade561534f4580  build/science_g0_prefetch/formal-evidence-v4/live-summary.json
29b280119a919e1ae27a4adb18a24efa20c6254c91bf0d239f0b821edbbf0a00  .superpowers/sdd/task-3-v4-formal-ctest.log
fe285502c8f3a0c8c9a8f21d543112d8435c7f980698062f7c709119bf98cf3c  .superpowers/sdd/task-3-v4-formal-artifacts.sha256
```

| Stem | Raw SHA-256 | Stats SHA-256 | Proof SHA-256 |
|---|---|---|---|
| `prefetch-hong_kong-1` | `f3b121ffe7e746c39d58b5f943efe5aac1eff62c1c46e3bc17b1c74f0bb973ec` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `7f9544d2095c428b515a20853d1983a1a2e1b0db84380d7408c873cff46f8a17` |
| `prefetch-hong_kong-2` | `d409bdb09ac110014181fca3dd7da4989480bf07c0fb97061d636acd0096a4e5` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `f6cd65414c72ecf0a95544e0a49dc31c2487021dbba735328ccdade11ab505ff` |
| `prefetch-hong_kong-3` | `175348daae708cf7cb0c85f56b322c3d01e1c2ee94cc1a9fd3bb426801be5ab3` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `7e3ee677fa50dfc13dad094898ec23a892e1e9d12cf14675036256e3bd91be42` |
| `prefetch-hong_kong-4` | `128429d672e67821f77cc59e393f9ce2bfdd84ab05812369e4d8d012209be296` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `7f9544d2095c428b515a20853d1983a1a2e1b0db84380d7408c873cff46f8a17` |
| `prefetch-hong_kong-5` | `1b46442aa5a14e13166938916cd17f6f6476883429e7ab2144d4fabcfcaead71` | `34acfe0cf3ce643b23b52f16baa145ac7df8691db7088ace5fc2c6836e8db5ff` | `712565d303aa52c450f1d7d4a251d93c04454df4b3fadabdb3ab3114d3f364a6` |
| `prefetch-nvidia_hq-1` | `808cd5c4ae30c274e13be20ac2400d1d96d5b07bb33f7224b849e8f06fbd7e09` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `6a26840cac0e2dab77e30b1a6b6ee5180c52399f67ba30d6e2f8ab5901d59def` |
| `prefetch-nvidia_hq-2` | `3cca6e0a20d5d51c19589210e07ef5e446627bc986d6c81c094ea916e2fa7a18` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `b1d8be99874bf47bdd6d76df446186acbab78f56fc35a07078eb82d45e8e602e` |
| `prefetch-nvidia_hq-3` | `48e2356cf7bb0129a37a76268f42486a61cda3b4a1b7a24501063a4cfdad9f04` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `bdb01ae502e4b9b29fbb7471e660c6c38b11d9e392bc1a93c5a691b7fca6c58b` |
| `prefetch-nvidia_hq-4` | `a57d139fa2da136c7f91d62f9f61874dcd640837f1ebbf059f2ca4fee788c1b5` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `20df536c0387b6c2982839efa7fdbd0071341b1a28ea363646832c12abfefe93` |
| `prefetch-nvidia_hq-5` | `578e4f1ae96d078b6884bf7c6e1c91154807fa164e27a54b74c7608844bd7978` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` | `36a1e5a24c9ead416903075f092a03f2b36914a15c9b8b39f113b18c1889b5b6` |

### Conditional downstream and final protection

```text
recorded_optimized_control_transcripts=1
recorded_prefetch_candidate_transcripts=1
recorded_formal_promotion_commits=1
recorded_formal_ctest_transcripts=1
reported_downstream_test_processes=0
reported_science_off_core_processes=0
reported_private_dependency_verifier_processes=0
reported_full_python_suite_processes=0
reported_selected_scienceearth_processes=0
reported_disposable_probe_processes=0
reported_canonical_bundle_audit_processes=0
reported_signature_size_export_path_processes=0
reported_memory_correctness_range_camera_cache_processes=0
reported_clean_launch_processes=0
reported_desktop_package_build_or_replace_processes=0
```

The zero downstream values are the reported execution ledger. The final scoped filesystem has no matching v4 downstream artifacts in the recorded namespace and the allowed tracked diff contains no downstream, Desktop, packaging, or G1 change. Those observations do not prove the absolute absence of omitted, overwritten, or externally recorded executions.

The original candidate/formal/final helper scripts were ignored mutable files: their exact promotion-time bytes were not anchored in Git, so their logs are not treated as immutable oracles and cannot retroactively establish helper identity. The committed post-hoc source below instead rechecks the patch/prefix, every old/v2/v3 hard-coded or immutable-Git binding, all 93 v4 manifest entries and modes, candidate/formal substantive proofs, exact aggregates and retry fields, promotion CMake hash/order/input, formal transcript/manifest hashes, the protected Desktop tuple, allowed tracked scope, and final states. This post-hoc audit establishes the substantive STOP decision from immutable artifacts; it does not prove negative process history.

### Committed post-hoc v4 decision audit

The source is delimited by unique extraction markers. Its recorded source SHA-256 and deterministic offline transcript SHA-256 are populated after extraction from the exact staged document and rechecked from clean committed `HEAD`.

```text
SOURCE_SHA256=7c2008834b24e6af519390a05dc8a9317c21f29e40ecc535e992d3843211f661
TRANSCRIPT_SHA256=7619084e74eb89e8cf2d79b792f4e21d8b151e6461028b266e52cd651906531d
```

The final v4 review found that the first committed auditor admitted HTTP 408 even though the
runtime classifier and approved policy contain exactly `429/500/502/503/504`. The source above
now enforces that exact five-code set; the revised source and deterministic transcript hashes are
the bindings recorded here. No frozen v4 network artifact or decision value changed.

The exact clean-`HEAD` wrapper below is fail-closed. It rejects a tracked-dirty worktree, requires
exactly one source begin marker and one source end marker, extracts only from committed `HEAD`,
and checks the extracted source digest before invoking Python. It tees the proxy-free offline
auditor output to the deterministic transcript, checks the transcript digest, and exits nonzero
on either mismatch.

<!-- V4_POSTHOC_DECISION_WRAPPER_BEGIN -->
```bash
#!/usr/bin/env bash
set -euo pipefail
umask 077

doc=docs/scienceearth/g0-measurements.md
transcript=.superpowers/sdd/task-3-v4-posthoc-audit.log
expected_source_sha=7c2008834b24e6af519390a05dc8a9317c21f29e40ecc535e992d3843211f661
expected_transcript_sha=7619084e74eb89e8cf2d79b792f4e21d8b151e6461028b266e52cd651906531d

if ! git diff --quiet || ! git diff --cached --quiet; then
    printf '%s\n' 'tracked worktree/index must be clean' >&2
    exit 1
fi

marker_counts=$(
    git show HEAD:"$doc" | awk '
        /^<!-- V4_POSTHOC_DECISION_AUDIT_BEGIN -->$/ { opens += 1 }
        /^<!-- V4_POSTHOC_DECISION_AUDIT_END -->$/ { closes += 1 }
        END { printf "%d %d", opens, closes }
    '
)
if [[ "$marker_counts" != "1 1" ]]; then
    printf 'source marker count mismatch: %s\n' "$marker_counts" >&2
    exit 1
fi

audit=$(mktemp "${TMPDIR:-/tmp}/osgsol-v4-posthoc-audit.XXXXXX")
trap 'rm -f "$audit"' EXIT
git show HEAD:"$doc" |
    awk '/^<!-- V4_POSTHOC_DECISION_AUDIT_BEGIN -->$/{inside=1; next} /^<!-- V4_POSTHOC_DECISION_AUDIT_END -->$/{inside=0} inside && !/^```python$/ && !/^```$/' > "$audit"

source_sha=$(shasum -a 256 "$audit" | awk '{print $1}')
if [[ "$source_sha" != "$expected_source_sha" ]]; then
    printf 'source SHA-256 mismatch: %s != %s\n' \
        "$source_sha" "$expected_source_sha" >&2
    exit 1
fi

rm -f "$transcript"
{
    printf 'SOURCE_SHA256=%s\n' "$source_sha"
    env -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY \
        -u http_proxy -u https_proxy -u all_proxy \
        PYTHONDONTWRITEBYTECODE=1 python3 "$audit"
} | tee "$transcript"

transcript_sha=$(shasum -a 256 "$transcript" | awk '{print $1}')
chmod 0400 "$transcript"
if [[ "$transcript_sha" != "$expected_transcript_sha" ]]; then
    printf 'transcript SHA-256 mismatch: %s != %s\n' \
        "$transcript_sha" "$expected_transcript_sha" >&2
    exit 1
fi

printf 'WRAPPER_SOURCE_SHA256_ASSERTION=PASS;%s\n' "$source_sha"
printf 'WRAPPER_TRANSCRIPT_SHA256_ASSERTION=PASS;%s\n' "$transcript_sha"
printf '%s\n' 'WRAPPER_AUDIT=PASS'
```
<!-- V4_POSTHOC_DECISION_WRAPPER_END -->

The exact deterministic output is:

```text
SOURCE_SHA256=7c2008834b24e6af519390a05dc8a9317c21f29e40ecc535e992d3843211f661
IMMUTABLE_OLD_V2_V3_PRIVATE_BINDINGS=PASS
DESKTOP_PROTECTED_TUPLE=91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18/14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214/414/355
V4_MANIFESTS=3/3;ENTRIES=31/31/31;RAW_STATS_PROOFS=10/10/10_EACH;FILES=0400;ROOTS=0500
RECORDED_EXECUTION_LEDGER=CONTROL_1_TRANSCRIPT/CANDIDATE_1_TRANSCRIPT/FORMAL_1_TRANSCRIPT;UNIQUE_TIMESTAMPS_AND_HASHES=PASS
NEGATIVE_PROCESS_HISTORY=NOT_PROVEN
CANDIDATE=10/10;NVIDIA=2896.292875/3360.568375/PASS;HONG_KONG=2313.934292/3459.563000/PASS
CANDIDATE_HONG_KONG_RETRY=ITERATION_1/HTTP2_500/17_BYTES/ATTEMPT_1/100_MS;TERMINAL_FALLBACKS=0
FORMAL=10/10;NVIDIA=3207.465916/3376.432625/FAIL;HONG_KONG=2474.804959/2829.896208/PASS;TERMINAL_FALLBACKS=0
PROMOTION_CMAKE=029bb3044c5dfcf2701cdbedd4cfe4ebe3488b8e23ef6caf9683fb23c10d4a0c;ORDER_AND_INPUT=PASS
FORMAL_TRANSCRIPT=29b280119a919e1ae27a4adb18a24efa20c6254c91bf0d239f0b821edbbf0a00;FORMAL_MANIFEST=fe285502c8f3a0c8c9a8f21d543112d8435c7f980698062f7c709119bf98cf3c
ALLOWED_TRACKED_SCOPE_AND_FINAL_STATES=PASS
DOWNSTREAM_LEDGER_ZERO=REPORTED;SCOPED_ARTIFACTS_OBSERVED=0;NEGATIVE_EXECUTION_HISTORY=NOT_PROVEN
DECISION=STOP/FAIL/NOT_READY
POSTHOC_V4_DECISION_AUDIT=PASS
```

The output is preserved at `.superpowers/sdd/task-3-v4-posthoc-audit.log`; its SHA-256 is the recorded transcript hash above. The original staged extraction and every clean-`HEAD` extraction are byte-identical to the committed source hash. The wrapper's explicit `WRAPPER_SOURCE_SHA256_ASSERTION=PASS`, `WRAPPER_TRANSCRIPT_SHA256_ASSERTION=PASS`, and `WRAPPER_AUDIT=PASS` lines are outside the transcript file, so the transcript remains deterministic.

<!-- V4_POSTHOC_DECISION_AUDIT_BEGIN -->
```python
#!/usr/bin/env python3

from collections import Counter
from pathlib import Path
import hashlib
import importlib.util
import json
import math
import re
import stat
import subprocess
import sys


ROOT = Path.cwd().resolve()
AUTHORIZATION_BASE = "4a1c1172e02ce13b5fd6452a97834dbd9f2001b3"
PRE_PUBLIC = "7a0abbc5212f88e191a0b2279c78d8cc84eb4422"
PROMOTION = "bede4b09c93d2383e7467000538bae1c7b03dbcd"
DECISION = "6dd7b8afe291ce4132f7d1887fac1c063ae3a229"
DOC_RELATIVE = "docs/scienceearth/g0-measurements.md"
BASELINE_RELATIVE = "docs/scienceearth/g0-g1-baseline.md"
ALLOWED_TRACKED = {DOC_RELATIVE, BASELINE_RELATIVE}


def require(condition, message):
    if not condition:
        raise SystemExit(message)


def run_git(*args):
    result = subprocess.run(
        ["git", *args], cwd=ROOT, capture_output=True, check=False)
    require(result.returncode == 0,
            f"git {' '.join(args)} failed: {result.stderr.decode(errors='replace')}")
    return result.stdout


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def file_sha256(path):
    return sha256_bytes(path.read_bytes())


def require_hash(relative, expected, mode=None):
    path = ROOT / relative
    require(path.is_file() and not path.is_symlink(), f"missing file: {relative}")
    actual = file_sha256(path)
    require(actual == expected,
            f"hash mismatch: {relative}: {actual} != {expected}")
    if mode is not None:
        actual_mode = stat.S_IMODE(path.stat().st_mode)
        require(actual_mode == mode,
                f"mode mismatch: {relative}: {actual_mode:o} != {mode:o}")


def tree_digest(relative):
    root = ROOT / relative
    require(root.is_dir() and not root.is_symlink(), f"missing tree: {relative}")
    digest = hashlib.sha256()
    entries = sorted(root.rglob("*"),
                     key=lambda path: path.relative_to(root).as_posix())
    regular = 0
    for path in entries:
        digest.update(path.relative_to(root).as_posix().encode("utf-8"))
        if path.is_file() and not path.is_symlink():
            digest.update(path.read_bytes())
            regular += 1
    return digest.hexdigest(), len(entries), regular


def exact_table_block(content, start_marker, end_marker, label):
    require(content.count(start_marker) == 1,
            f"{label} start-marker count mismatch")
    require(content.count(end_marker) == 1,
            f"{label} end-marker count mismatch")
    start = content.index(start_marker)
    end = content.index(end_marker)
    require(start < end, f"{label} markers out of order")
    return content[start:end]


def table_rows(block):
    pattern = re.compile(
        r"^\| `(?P<stem>(?:optimized|prefetch)-[^`]+)` "
        r"\| `(?P<raw>[0-9a-f]{64})` "
        r"\| `(?P<stats>[0-9a-f]{64})` "
        r"\| (?P<proof>`[0-9a-f]{64}`|absent;[^|]+) \|$", re.MULTILINE)
    return list(pattern.finditer(block.decode("utf-8")))


def verify_historical_evidence(version, table_block, expected_rows,
                               expected_iteration_files, summaries,
                               require_mode_0400):
    root = ROOT / f"build/science_g0_prefetch/requalification-evidence-{version}"
    rows = table_rows(table_block)
    require(len(rows) == expected_rows,
            f"{version} row count mismatch: {len(rows)}")
    expected_paths = set()
    slots = iteration_files = absent = 0
    for match in rows:
        stem = match["stem"]
        subdir = "control" if stem.startswith("optimized-") else "prefetch"
        for suffix, field in (("-curl-cpl.log", "raw"),
                              ("-network-stats.json", "stats")):
            path = root / subdir / f"{stem}{suffix}"
            require(path.is_file() and not path.is_symlink(),
                    f"missing file: {path}")
            require(file_sha256(path) == match[field],
                    f"{version} hash mismatch: {path}")
            expected_paths.add(path)
            slots += 1
            iteration_files += 1
        proof_path = root / subdir / f"{stem}-proof.json"
        slots += 1
        if match["proof"].startswith("`"):
            require(proof_path.is_file() and not proof_path.is_symlink(),
                    f"missing proof: {proof_path}")
            require(file_sha256(proof_path) == match["proof"].strip("`"),
                    f"{version} proof mismatch: {proof_path}")
            expected_paths.add(proof_path)
            iteration_files += 1
        else:
            require(not proof_path.exists(),
                    f"expected absent proof exists: {proof_path}")
            absent += 1
    for name, expected in summaries.items():
        path = root / name
        require(path.is_file() and not path.is_symlink(),
                f"missing summary: {path}")
        require(file_sha256(path) == expected,
                f"{version} summary mismatch: {path}")
        expected_paths.add(path)
    actual_paths = {
        path for path in root.rglob("*")
        if path.is_file() and not path.is_symlink()
    }
    require(actual_paths == expected_paths,
            f"{version} complete-set mismatch")
    require(iteration_files == expected_iteration_files,
            f"{version} iteration-file count mismatch")
    non_0400 = [
        path for path in actual_paths
        if stat.S_IMODE(path.stat().st_mode) != 0o400
    ]
    if require_mode_0400:
        require(not non_0400, f"{version} frozen mode mismatch")
    return (len(rows), slots, iteration_files, absent, len(summaries),
            len(actual_paths), len(non_0400))


def verify_manifest_set(label, evidence_relative, summary_relative,
                        manifest_relative, transcript_relative,
                        expected_summary_hash, expected_manifest_hash,
                        expected_transcript_hash):
    evidence = ROOT / evidence_relative
    summary = ROOT / summary_relative
    manifest = ROOT / manifest_relative
    transcript = ROOT / transcript_relative
    require(evidence.is_dir() and not evidence.is_symlink(),
            f"{label} evidence root missing")
    require(stat.S_IMODE(evidence.stat().st_mode) == 0o500,
            f"{label} evidence root mode mismatch")
    require_hash(summary_relative, expected_summary_hash, 0o400)
    require_hash(manifest_relative, expected_manifest_hash, 0o400)
    require_hash(transcript_relative, expected_transcript_hash, 0o400)
    lines = manifest.read_text().splitlines()
    require(len(lines) == 31, f"{label} manifest line count mismatch")
    expected_paths = set()
    for line in lines:
        parts = line.split("  ", 1)
        require(len(parts) == 2 and re.fullmatch(r"[0-9a-f]{64}", parts[0]),
                f"{label} malformed manifest line")
        digest, relative = parts
        path = ROOT / relative
        require(path.is_file() and not path.is_symlink(),
                f"{label} missing artifact: {relative}")
        require(file_sha256(path) == digest,
                f"{label} artifact hash mismatch: {relative}")
        require(stat.S_IMODE(path.stat().st_mode) == 0o400,
                f"{label} artifact mode mismatch: {relative}")
        expected_paths.add(path.resolve())
    actual_paths = {
        path.resolve() for path in evidence.iterdir()
        if path.is_file() and not path.is_symlink()
    }
    if summary.parent.resolve() == evidence.resolve():
        require(actual_paths == expected_paths,
                f"{label} complete file set mismatch")
    else:
        require(actual_paths | {summary.resolve()} == expected_paths,
                f"{label} complete file set mismatch")
    suffix_counts = Counter()
    for path in actual_paths:
        if path.name.endswith("-curl-cpl.log"):
            suffix_counts["raw"] += 1
        elif path.name.endswith("-network-stats.json"):
            suffix_counts["stats"] += 1
        elif path.name.endswith("-proof.json"):
            suffix_counts["proof"] += 1
    require(suffix_counts == {"raw": 10, "stats": 10, "proof": 10},
            f"{label} artifact kind counts mismatch: {suffix_counts}")
    return transcript.read_text()


def close(actual, expected, tolerance=1.0e-9):
    return math.isclose(float(actual), float(expected), rel_tol=0.0,
                        abs_tol=tolerance)


def six(value):
    return f"{float(value):.6f}"


EXPECTED_AGGREGATES = {
    "candidate": {
        "status": "PASS",
        "nvidia_hq": {
            "latency": ("2896.292875", "3360.568375"),
            "http": (41, 35, 5, 10, 0, 6),
            "bytes": (25054055, 25054055, 102, 25054157),
            "retry_codes": {"500": 6},
            "coordinator_codes": {},
            "response_codes": [200, 206, 500],
            "status": "PASS",
        },
        "hong_kong": {
            "latency": ("2313.934292", "3459.563000"),
            "http": (21, 20, 5, 10, 1, 0),
            "bytes": (6320460, 6320477, 17, 6320477),
            "retry_codes": {},
            "coordinator_codes": {"500": 1},
            "response_codes": [200, 206, 500],
            "status": "PASS",
        },
    },
    "formal": {
        "status": "FAIL",
        "nvidia_hq": {
            "latency": ("3207.465916", "3376.432625"),
            "http": (46, 35, 5, 10, 0, 11),
            "bytes": (25054055, 25054055, 187, 25054242),
            "retry_codes": {"500": 11},
            "coordinator_codes": {},
            "response_codes": [200, 206, 500],
            "status": "FAIL",
        },
        "hong_kong": {
            "latency": ("2474.804959", "2829.896208"),
            "http": (20, 20, 5, 10, 0, 0),
            "bytes": (6320460, 6320460, 0, 6320460),
            "retry_codes": {},
            "coordinator_codes": {},
            "response_codes": [200, 206],
            "status": "PASS",
        },
    },
}


def verify_prefetch_proofs(label, evidence_relative, summary_relative, fixtures):
    evidence = ROOT / evidence_relative
    summary = json.loads((ROOT / summary_relative).read_text())
    expected_set = EXPECTED_AGGREGATES[label]
    require(summary["status"] == expected_set["status"],
            f"{label} summary status mismatch")
    require(summary["profile"] == "prefetch", f"{label} profile mismatch")
    require(summary["limits"] == {"median_ms": 3000, "p95_ms": 8000},
            f"{label} limits mismatch")
    case_summaries = {case["name"]: case for case in summary["cases"]}
    require(set(case_summaries) == set(fixtures) == {"nvidia_hq", "hong_kong"},
            f"{label} case names mismatch")
    proof_count = 0
    total_fallbacks = 0
    all_retries = {}
    for name, fixture in fixtures.items():
        case = case_summaries[name]
        require(case["fid"] == fixture["fid"] and
                case["year"] == fixture["year"],
                f"{label}/{name}: source identity mismatch")
        require(case["iteration_count"] == 5 and len(case["iterations"]) == 5,
                f"{label}/{name}: iteration count mismatch")
        totals = Counter()
        ordinary_codes = Counter()
        coordinator_codes = Counter()
        response_codes = set()
        timings = []
        for iteration in range(1, 6):
            stem = f"prefetch-{name}-{iteration}"
            raw = (evidence / f"{stem}-curl-cpl.log").read_text()
            stats_data = json.loads(
                (evidence / f"{stem}-network-stats.json").read_text())
            proof = json.loads((evidence / f"{stem}-proof.json").read_text())
            meta = proof["metadata_prefetch"]
            retries = meta["coordinator_retries"]
            all_retries[(name, iteration)] = retries

            require(meta["enabled"] is True, f"{label}/{stem}: disabled")
            require(meta["head_request_count"] == 1,
                    f"{label}/{stem}: HEAD request mismatch")
            require(meta["range_request_count"] == 1 + len(retries),
                    f"{label}/{stem}: Range request mismatch")
            require((meta["range_start"], meta["range_end"]) == (0, 131071),
                    f"{label}/{stem}: first Range mismatch")
            require(meta["head_http_version"] == 2 and
                    meta["range_http_version"] == 2,
                    f"{label}/{stem}: HTTP/2 mismatch")
            require(meta["shared_connection"] is True,
                    f"{label}/{stem}: shared connection mismatch")
            require(meta["requests_overlapped"] is True,
                    f"{label}/{stem}: overlap mismatch")
            require(meta["cache_published"] is True,
                    f"{label}/{stem}: publication mismatch")
            require(meta["fallback_reason"] == "",
                    f"{label}/{stem}: terminal fallback reason")
            require(len(retries) <= 3,
                    f"{label}/{stem}: coordinator retry budget exceeded")

            require(proof["coordinator_transient_retry_count"] == len(retries),
                    f"{label}/{stem}: coordinator retry count mismatch")
            require(proof["coordinator_transient_retry_bytes"] ==
                    sum(item["bytes"] for item in retries),
                    f"{label}/{stem}: coordinator retry byte mismatch")
            retry_codes = Counter(str(item["code"]) for item in retries)
            require(proof["coordinator_transient_retry_codes"] ==
                    dict(retry_codes),
                    f"{label}/{stem}: coordinator retry code mismatch")
            for index, item in enumerate(retries, 1):
                delay_min, delay_max = ((100, 125), (200, 250),
                                        (400, 625))[index - 1]
                require(item["range"] == "bytes=0-131071" and
                        item["attempt"] == index and
                        item["code"] in (429, 500, 502, 503, 504) and
                        delay_min <= item["delay_ms"] <= delay_max and
                        item["http_major"] == 2,
                        f"{label}/{stem}: coordinator retry fields mismatch")

            require(proof["coordinator_transient_fallback_count"] == 0 and
                    proof["coordinator_transient_fallback_bytes"] == 0 and
                    proof["coordinator_transient_fallback_codes"] == {},
                    f"{label}/{stem}: terminal fallback present")
            require(proof["actual_http_get_count"] ==
                    proof["successful_http_get_count"] +
                    proof["transient_retry_count"] +
                    proof["coordinator_transient_retry_count"],
                    f"{label}/{stem}: GET reconciliation mismatch")
            require(sum(proof["transient_retry_codes"].values()) ==
                    proof["transient_retry_count"],
                    f"{label}/{stem}: ordinary retry mismatch")
            require(proof["actual_http_head_count"] ==
                    proof["stats_head_count"] == 1,
                    f"{label}/{stem}: HEAD reconciliation mismatch")
            require(proof["stats_get_operation_count"] ==
                    stats_data["methods"]["GET"]["count"] and
                    proof["stats_head_count"] ==
                    stats_data["methods"]["HEAD"]["count"],
                    f"{label}/{stem}: stats count mismatch")
            require(proof["actual_http_body_bytes"] ==
                    stats_data["methods"]["GET"]["downloaded_bytes"] ==
                    proof["successful_range_bytes"] +
                    proof["coordinator_transient_retry_bytes"],
                    f"{label}/{stem}: actual byte reconciliation mismatch")
            require(proof["conservative_body_upper_bound_bytes"] ==
                    proof["successful_range_bytes"] +
                    proof["declared_transient_bytes"],
                    f"{label}/{stem}: conservative byte mismatch")
            require(proof["declared_transient_bytes"] >=
                    proof["coordinator_transient_retry_bytes"],
                    f"{label}/{stem}: declared transient byte mismatch")
            require(proof["conservative_body_upper_bound_bytes"] <=
                    proof["transfer_budget_bytes"] == 16777216,
                    f"{label}/{stem}: transfer ceiling mismatch")

            intervals = proof["successful_byte_intervals"]
            require(len(intervals) == proof["successful_http_get_count"] and
                    intervals[0] == [0, 131071],
                    f"{label}/{stem}: interval shape mismatch")
            require(sum(end - start + 1 for start, end in intervals) ==
                    proof["successful_range_bytes"],
                    f"{label}/{stem}: interval byte mismatch")
            require(all(0 <= start <= end < proof["source_size"]
                        for start, end in intervals),
                    f"{label}/{stem}: interval bound mismatch")

            require(proof["source_crs"] == fixture["crs"] and
                    proof["selected_overview_factor"] == 4 and
                    proof["raw_window"]["size"] == 256,
                    f"{label}/{stem}: correctness identity mismatch")
            require(close(proof["geotransform"][0], fixture["utm_bbox"][0]) and
                    close(proof["geotransform"][3], fixture["utm_bbox"][1]),
                    f"{label}/{stem}: geotransform mismatch")
            require(all(close(actual, wanted, 1.0e-10)
                        for actual, wanted in
                        zip(proof["verified_wgs84_bbox"], fixture["bbox"])),
                    f"{label}/{stem}: WGS84 bbox mismatch")

            require(raw.count("ParallelHeadRange: transient-retry ") ==
                    len(retries),
                    f"{label}/{stem}: raw coordinator retry mismatch")
            require(raw.count("ParallelHeadRange: transient-fallback ") == 0,
                    f"{label}/{stem}: raw terminal fallback present")
            require(raw.count("HTTP error code for ") ==
                    proof["transient_retry_count"],
                    f"{label}/{stem}: raw ordinary retry mismatch")
            require(raw.count("[range: bytes=0-131071]") ==
                    meta["range_request_count"],
                    f"{label}/{stem}: raw exact Range count mismatch")
            require(raw.count("ParallelHeadRange: published") == 1 and
                    raw.count("ParallelHeadRange: file-property-published count=1") == 1,
                    f"{label}/{stem}: raw publication mismatch")
            transport = re.findall(
                r"ParallelHeadRange: transport head-connection=(\d+) "
                r"range-connection=(\d+) head-http=(\d+) range-http=(\d+)",
                raw)
            require(len(transport) == 1 and
                    transport[0][0] == transport[0][1] and
                    transport[0][2:] == ("2", "2"),
                    f"{label}/{stem}: raw transport mismatch")
            started = raw.index("ParallelHeadRange: started")
            head_method = raw.index("[:method: HEAD]", started)
            range_method = raw.index("[:method: GET]", started)
            first_target_response = raw.index(
                "CURL_INFO_HEADER_IN: HTTP/2 ", max(head_method, range_method))
            require(head_method < first_target_response and
                    range_method < first_target_response,
                    f"{label}/{stem}: overlap chronology mismatch")

            require(all(code in (200, 206, 429, 500, 502, 503, 504)
                        for code in proof["response_codes"]),
                    f"{label}/{stem}: response code mismatch")
            response_codes.update(proof["response_codes"])
            summary_iteration = case["iterations"][iteration - 1]
            require(summary_iteration["iteration"] == iteration,
                    f"{label}/{stem}: summary order mismatch")
            count_fields = {
                "actual_get": "actual_http_get_count",
                "successful_get": "successful_http_get_count",
                "transient_retries": "transient_retry_count",
                "coordinator_transient_retries":
                    "coordinator_transient_retry_count",
                "actual_head": "actual_http_head_count",
                "stats_get_operations": "stats_get_operation_count",
            }
            for field, proof_field in count_fields.items():
                require(summary_iteration["http_counts"][field] ==
                        proof[proof_field],
                        f"{label}/{stem}: summary count mismatch: {field}")
            byte_fields = {
                "successful_range": "successful_range_bytes",
                "actual_http_body": "actual_http_body_bytes",
                "declared_transient": "declared_transient_bytes",
                "coordinator_transient_retry":
                    "coordinator_transient_retry_bytes",
                "conservative_body_upper_bound":
                    "conservative_body_upper_bound_bytes",
                "source_size": "source_size",
            }
            for field, proof_field in byte_fields.items():
                require(summary_iteration["bytes"][field] == proof[proof_field],
                        f"{label}/{stem}: summary byte mismatch: {field}")
            require(summary_iteration["transient_retry_codes"] ==
                    proof["transient_retry_codes"] and
                    summary_iteration["coordinator_transient_retry_codes"] ==
                    proof["coordinator_transient_retry_codes"] and
                    summary_iteration["response_codes"] == proof["response_codes"],
                    f"{label}/{stem}: summary code mismatch")

            timings.append(summary_iteration["timing_ms"])
            totals.update({
                "actual_get": proof["actual_http_get_count"],
                "successful_get": proof["successful_http_get_count"],
                "actual_head": proof["actual_http_head_count"],
                "stats_get_operations": proof["stats_get_operation_count"],
                "coordinator_retries":
                    proof["coordinator_transient_retry_count"],
                "ordinary_retries": proof["transient_retry_count"],
                "successful_range": proof["successful_range_bytes"],
                "actual_http_body": proof["actual_http_body_bytes"],
                "declared_transient": proof["declared_transient_bytes"],
                "conservative": proof["conservative_body_upper_bound_bytes"],
            })
            ordinary_codes.update(proof["transient_retry_codes"])
            coordinator_codes.update(proof["coordinator_transient_retry_codes"])
            total_fallbacks += proof["coordinator_transient_fallback_count"]
            proof_count += 1

        expected = expected_set[name]
        require((six(case["latency_ms"]["median"]),
                 six(case["latency_ms"]["p95"])) == expected["latency"],
                f"{label}/{name}: exact latency aggregate mismatch")
        require(case["latency_ms"]["median"] == sorted(timings)[2] and
                case["latency_ms"]["p95"] == sorted(timings)[4],
                f"{label}/{name}: latency recomputation mismatch")
        require((totals["actual_get"], totals["successful_get"],
                 totals["actual_head"], totals["stats_get_operations"],
                 totals["coordinator_retries"], totals["ordinary_retries"]) ==
                expected["http"],
                f"{label}/{name}: exact HTTP aggregate mismatch")
        require((totals["successful_range"], totals["actual_http_body"],
                 totals["declared_transient"], totals["conservative"]) ==
                expected["bytes"],
                f"{label}/{name}: exact byte aggregate mismatch")
        require(case["http_counts"] == {
                    "actual_get": expected["http"][0],
                    "successful_get": expected["http"][1],
                    "actual_head": expected["http"][2],
                    "stats_get_operations": expected["http"][3],
                    "coordinator_transient_retries": expected["http"][4],
                    "transient_retries": expected["http"][5],
                }, f"{label}/{name}: summary HTTP aggregate mismatch")
        require(case["bytes"]["successful_range"] == expected["bytes"][0] and
                case["bytes"]["actual_http_body"] == expected["bytes"][1] and
                case["bytes"]["declared_transient"] == expected["bytes"][2] and
                case["bytes"]["conservative_body_upper_bound"] ==
                expected["bytes"][3],
                f"{label}/{name}: summary byte aggregate mismatch")
        require(case["transient_retry_codes"] ==
                dict(ordinary_codes) == expected["retry_codes"] and
                case["coordinator_transient_retry_codes"] ==
                dict(coordinator_codes) == expected["coordinator_codes"],
                f"{label}/{name}: exact retry-code aggregate mismatch")
        require(case["response_codes"] == sorted(response_codes) ==
                expected["response_codes"],
                f"{label}/{name}: exact response aggregate mismatch")
        latency_pass = (case["latency_ms"]["median"] <= 3000 and
                        case["latency_ms"]["p95"] <= 8000)
        require(case["status"] == expected["status"] ==
                ("PASS" if latency_pass else "FAIL"),
                f"{label}/{name}: latency status mismatch")

    require(proof_count == 10 and total_fallbacks == 0,
            f"{label}: proof/fallback total mismatch")
    return all_retries


# Validate the immutable Git anchors and the allowed additive tracked scope.
require(run_git("cat-file", "-t", AUTHORIZATION_BASE) == b"commit\n",
        "authorization base object mismatch")
require(run_git("cat-file", "-t", PRE_PUBLIC) == b"commit\n",
        "pre-public object mismatch")
require(run_git("cat-file", "-t", PROMOTION) == b"commit\n",
        "promotion object mismatch")
require(run_git("cat-file", "-t", DECISION) == b"commit\n",
        "decision object mismatch")
subprocess.run(["git", "merge-base", "--is-ancestor", DECISION, "HEAD"],
               cwd=ROOT, check=True)
range_names = set(run_git("diff", "--name-only", f"{DECISION}..HEAD")
                  .decode().splitlines())
index_names = set(run_git("diff", "--cached", "--name-only")
                  .decode().splitlines())
worktree_names = set(run_git("diff", "--name-only").decode().splitlines())
require(range_names <= ALLOWED_TRACKED and
        index_names <= ALLOWED_TRACKED and
        worktree_names <= ALLOWED_TRACKED,
        "tracked changes exceed additive documentation scope")

doc_bytes = (ROOT / DOC_RELATIVE).read_bytes()
baseline_bytes = (ROOT / BASELINE_RELATIVE).read_bytes()
base_doc_bytes = run_git("show", f"{AUTHORIZATION_BASE}:{DOC_RELATIVE}")
V2_START = b"### V2 control artifact hashes\n"
V2_END = b"\n### Prefetch candidate and hard stop\n"
V3_START = b"### Frozen v3 artifact hashes\n"
V3_END = b"\nThe hashed decision-gate transcript is"
v2_block = exact_table_block(doc_bytes, V2_START, V2_END, "current v2")
base_v2_block = exact_table_block(base_doc_bytes, V2_START, V2_END, "base v2")
v3_block = exact_table_block(doc_bytes, V3_START, V3_END, "current v3")
base_v3_block = exact_table_block(base_doc_bytes, V3_START, V3_END, "base v3")
require(v2_block == base_v2_block and
        sha256_bytes(v2_block) ==
        "5a15d5a87a87d1976321aff7f2afde63d4a2eb19b1c6be824c6fdd4f7d8c0f8b",
        "v2 immutable Git block mismatch")
require(v3_block == base_v3_block and
        sha256_bytes(v3_block) ==
        "a9f3c6c2f929c01429809d684841213fcd72718f75f2274b666395c733a9e76a",
        "v3 immutable Git block mismatch")

global_constraints = [
    ("build/science_g0_prefetch/diagnostic-evidence/control-summary.json",
     "f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096"),
    ("build/science_g0_prefetch/diagnostic-evidence/prefetch-summary.json",
     "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff"),
    ("build/science_g0_prefetch/diagnostic-evidence/prefetch/"
     "prefetch-nvidia_hq-1-curl-cpl.log",
     "3312a78a073140ea1422ece0ffbc927ce853a4d7b8cec02f3033734a7e217a3a"),
    ("build/science_g0_prefetch/diagnostic-evidence/prefetch/"
     "prefetch-nvidia_hq-1-network-stats.json",
     "0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe"),
    ("build/science_g0/science-network-evidence/baseline-summary.json",
     "17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed"),
    ("build/science_g0/science-network-evidence/live-summary.json",
     "e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe"),
    ("build/science_g0_prefetch/requalification-evidence-v2/control-summary.json",
     "0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0"),
    ("build/science_g0_prefetch/requalification-evidence-v2/prefetch-summary.json",
     "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff"),
    ("build/science_g0_prefetch/requalification-evidence-v2/prefetch/"
     "prefetch-hong_kong-5-curl-cpl.log",
     "f32cfdfd05f50a3de8f34b4f69e1e2cefe61d667e20936ddcf86f1fa8b1a429e"),
    ("build/science_g0_prefetch/requalification-evidence-v2/prefetch/"
     "prefetch-hong_kong-5-network-stats.json",
     "f54110c20ab81230c5d3af708f2df4d9d8754e5957366875c162b0395d2c1974"),
]
for relative, expected in global_constraints:
    require_hash(relative, expected)
require(tree_digest("build/science_g0_prefetch/diagnostic-evidence/control") ==
        ("d3eb2a1adb60aa4a911b14e47854a2fc7b7fe7b81424de17779ae7862b888133",
         30, 30), "rejected control tree mismatch")
require(tree_digest("build/science_g0_prefetch/diagnostic-evidence/prefetch") ==
        ("054d7d76266febab932255db9658845717625d1ea348bef4021fd3330adffca0",
         2, 2), "rejected candidate tree mismatch")

v2 = verify_historical_evidence(
    "v2", v2_block, 20, 59,
    {"control-summary.json":
     "0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0",
     "prefetch-summary.json":
     "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff"},
    False)
v3 = verify_historical_evidence(
    "v3", v3_block, 12, 35,
    {"control-summary.json":
     "47abb0bc91e4afba412d1150822b90110650f8e225b45b7fa52f4627889c405c",
     "prefetch-summary.json":
     "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff"},
    True)
require(v2 == (20, 60, 59, 1, 2, 61, 61), "v2 totals mismatch")
require(v3 == (12, 36, 35, 1, 2, 37, 0), "v3 totals mismatch")

v3_scratch = [
    (".superpowers/sdd/task-3-v3-preflight.log",
     "d48994e70732786983b9c5d2f9c67bb2ca0c936d0b25e69c7acefd694fbe6aff"),
    (".superpowers/sdd/task-3-v3-control.log",
     "a1d8e36c152f71771e1ad80ae548e43e397456ce6ea47fe03e50e2cad9b2d142"),
    (".superpowers/sdd/task-3-v3-candidate.log",
     "c7f98c3e8428cb72092e6e49a1fff359232c4b4463607e18d52d55aac1b907d2"),
    (".superpowers/sdd/task-3-v3-control-artifacts.sha256",
     "c81a325b4aecd6b1a722c01789270c2568fc55cb7a1b201b7a6dd8c73e4b3849"),
    (".superpowers/sdd/task-3-v3-candidate-artifacts.sha256",
     "5845a9aac3a9b7e4b88e9323b16f20ff45f1e6f99ecc7a3997175b0f791458ff"),
    (".superpowers/sdd/task-3-v3-decision-gate.log",
     "0fd10896a891e20eb8e63a755fb68f4772e2cefc36b5b30f148165101001bafa"),
    (".superpowers/sdd/task-3-v3-final-protected.log",
     "cb5bc37da993e862b4cfbfae4e8777df586fd03e05a753693b972c518dc072a9"),
]
for relative, expected in v3_scratch:
    require_hash(relative, expected)

require_hash("packaging/science_deps/gdal-3.13.1-parallel-head-range.patch",
             "5ce830f7853db1c6f53b833741261f55f382c0b8972161999a2d34b99c3ab15f")
require_hash("build/science-deps-prefetch/prefix/science-deps-manifest.json",
             "a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116")
require_hash("packaging/scienceearth/g0_manifest.py",
             "38dd891896fa9ce9768163ded36069c329cae98e516a836ec98fc004f50a62f2")
require_hash("tests/science_bundle_audit_tests.py",
             "76fe4c1970d4d22e1c3bdd7d77852af69563993b33bc702d5e1446d1d5bf81a0")

manifest_path = ROOT / "packaging/scienceearth/g0_manifest.py"
spec = importlib.util.spec_from_file_location("posthoc_v4_manifest", manifest_path)
manifest_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest_module)
reference_path = ROOT / (
    "packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json")
ratchet_path = ROOT / (
    "packaging/scienceearth/baselines/current-macos-arm64-ratchet.json")
reference = json.loads(reference_path.read_text())
ratchet = json.loads(ratchet_path.read_text())
require_hash(
    "packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json",
    "afd80d8ad9419882793926d781affb970246358fbf74c514b950bf0eb924a5a6")
require_hash(
    "packaging/scienceearth/baselines/current-macos-arm64-ratchet.json",
    "a732bacfec5b16b6e42ef4b4573827df6edfa73a5d8bf5b2c7b94d0d684c06cc")
require(manifest_module.manifest_sha256(reference) ==
        "145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada",
        "reference canonical hash mismatch")
require(manifest_module.manifest_sha256(ratchet) ==
        "fd2d67424356637dd71beb4120647726836fb9a9b3cec03223bb83378fe960cb",
        "ratchet canonical hash mismatch")
require(reference["metadata"]["normalization_profile"]["source_roots_sha256"] ==
        "646b5eb80be60524ca6aa8dad55921f2966cc40562b29489483f1184a04c1936",
        "normalization descriptor mismatch")
require(ratchet["reference_sha256"] ==
        "145231333a233cd356d0aa3e908db922ad1fbbc244868903a60801b334ba5ada" and
        ratchet["releases"][0]["parent_finding_ids_sha256"] ==
        "5eeb1fc96226557050bffecfe5ab7cb11f32713fddb83c3147b1684ad675bd55",
        "ratchet binding mismatch")

fixture_relative = "tests/data/science/alphaearth_rgb_cases.json"
require_hash(fixture_relative,
             "6a67af9a1380250704b9032f8b4933965196ed2a119f07be4cb99dafe032806d")
fixture_data = json.loads((ROOT / fixture_relative).read_text())
require(fixture_data["source_index_sha256"] ==
        "f738e7d274ad582e56e20a3a8b444c6f2a3ece5781f8f9855bb7ca3d9ed2942f",
        "fixture source index mismatch")
require({case["name"]: case["record_fingerprint"]
         for case in fixture_data["cases"]} == {
             "nvidia_hq":
             "03956d76d3bc0d67c4f61608a1771ffb036a7e3ece6df326a2b52e66d412a0f1",
             "hong_kong":
             "deded3c50b4a91db0fe3fe97691734c7fa5a802647c1571d28f75515587a59ff",
         }, "fixture record fingerprint mismatch")
fixtures = {case["name"]: case for case in fixture_data["cases"]}

sys.path.insert(0, str(ROOT))
from tests.science_bundle_audit_tests import ScienceProbeBuilderTests, MANIFEST

desktop = Path("/Users/USER/Desktop/osgSol Earth.app")
require(desktop.is_dir() and not desktop.is_symlink(), "Desktop app missing")
desktop_entries = list(desktop.rglob("*"))
desktop_regular = [
    path for path in desktop_entries if path.is_file() and not path.is_symlink()
]
desktop_fingerprint = MANIFEST.bundle_fingerprint(desktop)
desktop_digest = ScienceProbeBuilderTests().tree_digest(desktop)
require((desktop_fingerprint, desktop_digest, len(desktop_entries),
         len(desktop_regular)) == (
             "91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18",
             "14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214",
             414, 355), "Desktop protected tuple mismatch")

# Verify the promotion from immutable Git objects and exact ordered CMake inputs.
require(run_git("rev-parse", f"{PROMOTION}^").decode().strip() == PRE_PUBLIC,
        "promotion parent mismatch")
require(run_git("diff-tree", "--no-commit-id", "--name-only", "-r", PROMOTION)
        .decode().splitlines() == ["tests/CMakeLists.txt"],
        "promotion scope mismatch")
base_cmake = run_git("show", f"{PRE_PUBLIC}:tests/CMakeLists.txt")
promoted_cmake = run_git("show", f"{PROMOTION}:tests/CMakeLists.txt")
require(sha256_bytes(base_cmake) ==
        "b9eea24fabb9fa2fb1d79bf384320e5082bd0155aedfcc18fa2d2ab33411141a",
        "pre-public CMake hash mismatch")
require(sha256_bytes(promoted_cmake) ==
        "029bb3044c5dfcf2701cdbedd4cfe4ebe3488b8e23ef6caf9683fb23c10d4a0c",
        "promoted Git CMake hash mismatch")
require_hash("tests/CMakeLists.txt",
             "029bb3044c5dfcf2701cdbedd4cfe4ebe3488b8e23ef6caf9683fb23c10d4a0c")
promotion_diff = run_git(
    "diff", "--no-ext-diff", "--binary", PRE_PUBLIC, PROMOTION, "--",
    "tests/CMakeLists.txt")
require(sha256_bytes(promotion_diff) ==
        "20802ca515fe1bd1addecd00cecc285fb612e0cc047604db59d34b1d8b56730b",
        "promotion diff hash mismatch")

def cmake_live_block(content):
    start_marker = b"            ADD_TEST(NAME osgVerse_Test_ScienceGdalLive\n"
    end_marker = b"            SET_TESTS_PROPERTIES(osgVerse_Test_ScienceGdalLive PROPERTIES\n"
    require(content.count(start_marker) == 1 and content.count(end_marker) == 1,
            "formal CMake block marker mismatch")
    start = content.index(start_marker)
    end = content.index(end_marker, start)
    return content[start:end]


base_block = (
    b"            ADD_TEST(NAME osgVerse_Test_ScienceGdalLive\n"
    b"                COMMAND $<TARGET_FILE:osgVerse_Test_ScienceHttpRanges>\n"
    b"                        --live-cases\n"
    b"                        \"${CMAKE_CURRENT_SOURCE_DIR}/data/science/alphaearth_rgb_cases.json\"\n"
    b"                        --iterations 5\n"
    b"                        --profile optimized\n"
    b"                        --evidence-dir\n"
    b"                        \"${CMAKE_BINARY_DIR}/science-network-evidence\"\n"
    b"                        --summary-json\n"
    b"                        \"${CMAKE_BINARY_DIR}/science-network-evidence/live-summary.json\"\n"
    b"                        --enforce-latency)\n")
promoted_block = (
    b"            ADD_TEST(NAME osgVerse_Test_ScienceGdalLive\n"
    b"                COMMAND $<TARGET_FILE:osgVerse_Test_ScienceHttpRanges>\n"
    b"                        --live-cases\n"
    b"                        \"${CMAKE_CURRENT_SOURCE_DIR}/data/science/alphaearth_rgb_cases.json\"\n"
    b"                        --iterations 5\n"
    b"                        --profile prefetch\n"
    b"                        --evidence-dir\n"
    b"                        \"${CMAKE_BINARY_DIR}/formal-evidence-v4\"\n"
    b"                        --summary-json\n"
    b"                        \"${CMAKE_BINARY_DIR}/formal-evidence-v4/live-summary.json\"\n"
    b"                        --enforce-latency)\n")
require(cmake_live_block(base_cmake) == base_block,
        "pre-public formal CMake order/input mismatch")
require(cmake_live_block(promoted_cmake) == promoted_block and
        cmake_live_block((ROOT / "tests/CMakeLists.txt").read_bytes()) ==
        promoted_block,
        "promoted formal CMake order/input mismatch")

# Verify all three frozen v4 sets, their transcript ledger, and exact timestamps.
require(stat.S_IMODE((ROOT / "build/science_g0_prefetch/"
                      "requalification-evidence-v4").stat().st_mode) == 0o500,
        "v4 requalification parent mode mismatch")
control_transcript = verify_manifest_set(
    "control-v4",
    "build/science_g0_prefetch/requalification-evidence-v4/control",
    "build/science_g0_prefetch/requalification-evidence-v4/control-summary.json",
    ".superpowers/sdd/task-3-v4-control-artifacts.sha256",
    ".superpowers/sdd/task-3-v4-control.log",
    "e19d0dceaf396c039497b52c79f5aff5877740c3bef3dca3fd17adf3fbdc4357",
    "ce35d41589010eba54b3ccee7b486fb5bbb7dd988141752eba41375ebed393a3",
    "b2ec9bf61a949a80b6a0fa6c87491ccff79837ab3a201aff445a07423f2f323b")
candidate_transcript = verify_manifest_set(
    "candidate-v4",
    "build/science_g0_prefetch/requalification-evidence-v4/prefetch",
    "build/science_g0_prefetch/requalification-evidence-v4/prefetch-summary.json",
    ".superpowers/sdd/task-3-v4-candidate-artifacts.sha256",
    ".superpowers/sdd/task-3-v4-candidate.log",
    "ac37e643a7e34840a30cc7dd6f3c17c2464a632936f7366396f5f09cd5f521d4",
    "81451c29000dab2bcaa262615eb8e9d72ad382adae3269892581423b2587b339",
    "81b40fbb098716e59ceada54b7bf33079fa7fcdf8a866aeb1d1828aafe2f434c")
formal_transcript = verify_manifest_set(
    "formal-v4",
    "build/science_g0_prefetch/formal-evidence-v4",
    "build/science_g0_prefetch/formal-evidence-v4/live-summary.json",
    ".superpowers/sdd/task-3-v4-formal-artifacts.sha256",
    ".superpowers/sdd/task-3-v4-formal-ctest.log",
    "e7664bb1ebe09dd8f53baf30c81810ff06a62be5b26cc2e267ade561534f4580",
    "fe285502c8f3a0c8c9a8f21d543112d8435c7f980698062f7c709119bf98cf3c",
    "29b280119a919e1ae27a4adb18a24efa20c6254c91bf0d239f0b821edbbf0a00")
require_hash(".superpowers/sdd/task-3-v4-preflight.log",
             "2bae6ec0237839cc14cdde0f90c8084b89b46336ecee20ebaf4598b7d6d2f22b",
             0o400)
require_hash(".superpowers/sdd/task-3-v4-formal-refresh.log",
             "5e8873e10bbe939d39451dc93da7e40ba25d39005c7284f979d867e8f78c14b9",
             0o400)
preflight = (ROOT / ".superpowers/sdd/task-3-v4-preflight.log").read_text()
require("HEAD=" + PRE_PUBLIC in preflight and
        "TRACKED_WORKTREE_INDEX=CLEAN" in preflight and
        "V4_ABSENT=5/5" in preflight and
        "IMMEDIATE_PREFLIGHT=PASS" in preflight,
        "preflight binding mismatch")

ledger = [
    (control_transcript, "v4_optimized_control",
     "2026-07-13T20:28:47+0800", "2026-07-13T20:29:20+0800", "0",
     "--iterations 5 --profile optimized"),
    (candidate_transcript, "v4_prefetch_candidate",
     "2026-07-13T20:30:05+0800", "2026-07-13T20:30:33+0800", "0",
     "--iterations 5 --profile prefetch"),
    (formal_transcript, "v4_formal_ctest",
     "2026-07-13T20:37:45+0800", "2026-07-13T20:38:14+0800", "8",
     "Test command:"),
]
for transcript, label, started, finished, exit_status, command_token in ledger:
    require(transcript.count(f"PROCESS_LABEL={label}") == 1 and
            transcript.count(f"PROCESS_STARTED={started}") == 1 and
            transcript.count(f"PROCESS_FINISHED={finished}") == 1 and
            transcript.count(f"PROCESS_EXIT_STATUS={exit_status}") == 1 and
            transcript.count(command_token) == 1,
            f"recorded transcript ledger mismatch: {label}")
for token in ('"--iterations" "5"', '"--profile" "prefetch"',
              "formal-evidence-v4", '"--enforce-latency"'):
    require(token in formal_transcript,
            f"formal transcript command missing token: {token}")

candidate_retries = verify_prefetch_proofs(
    "candidate",
    "build/science_g0_prefetch/requalification-evidence-v4/prefetch",
    "build/science_g0_prefetch/requalification-evidence-v4/prefetch-summary.json",
    fixtures)
formal_retries = verify_prefetch_proofs(
    "formal", "build/science_g0_prefetch/formal-evidence-v4",
    "build/science_g0_prefetch/formal-evidence-v4/live-summary.json",
    fixtures)
expected_hk_retry = [{
    "range": "bytes=0-131071", "code": 500, "bytes": 17,
    "attempt": 1, "delay_ms": 100, "connection_id": 0,
    "http_major": 2,
}]
require(candidate_retries[("hong_kong", 1)] == expected_hk_retry,
        "candidate Hong Kong retry record mismatch")
require(all(retries == [] for key, retries in candidate_retries.items()
            if key != ("hong_kong", 1)),
        "unexpected candidate coordinator retry")
require(all(retries == [] for retries in formal_retries.values()),
        "unexpected formal coordinator retry")

final_state = (
    b"G0_DECISION=STOP\n"
    b"PUBLIC_REQUALIFICATION_V4=FAIL\n"
    b"DESKTOP_PACKAGE=NOT_READY")
require(doc_bytes.rstrip().endswith(final_state),
        "measurement final state mismatch")
require(baseline_bytes.rstrip().endswith(final_state),
        "baseline final state mismatch")

# The following is an observation about the final scoped filesystem, not proof
# of negative process history. It is deliberately not used to assert exact
# process counts or the absolute absence of overwritten/omitted executions.
downstream_globs = (
    "task-3-v4-downstream*", "task-3-v4-science-off*",
    "task-3-v4-private-dependency*", "task-3-v4-full-python*",
    "task-3-v4-selected-scienceearth*", "task-3-v4-disposable-probe*",
    "task-3-v4-canonical-bundle*", "task-3-v4-clean-launch*",
)
observed_downstream = []
for pattern in downstream_globs:
    observed_downstream.extend((ROOT / ".superpowers/sdd").glob(pattern))
require(not observed_downstream,
        f"scoped downstream artifact observed: {observed_downstream}")

print("IMMUTABLE_OLD_V2_V3_PRIVATE_BINDINGS=PASS")
print("DESKTOP_PROTECTED_TUPLE=91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18/14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214/414/355")
print("V4_MANIFESTS=3/3;ENTRIES=31/31/31;RAW_STATS_PROOFS=10/10/10_EACH;FILES=0400;ROOTS=0500")
print("RECORDED_EXECUTION_LEDGER=CONTROL_1_TRANSCRIPT/CANDIDATE_1_TRANSCRIPT/FORMAL_1_TRANSCRIPT;UNIQUE_TIMESTAMPS_AND_HASHES=PASS")
print("NEGATIVE_PROCESS_HISTORY=NOT_PROVEN")
print("CANDIDATE=10/10;NVIDIA=2896.292875/3360.568375/PASS;HONG_KONG=2313.934292/3459.563000/PASS")
print("CANDIDATE_HONG_KONG_RETRY=ITERATION_1/HTTP2_500/17_BYTES/ATTEMPT_1/100_MS;TERMINAL_FALLBACKS=0")
print("FORMAL=10/10;NVIDIA=3207.465916/3376.432625/FAIL;HONG_KONG=2474.804959/2829.896208/PASS;TERMINAL_FALLBACKS=0")
print("PROMOTION_CMAKE=029bb3044c5dfcf2701cdbedd4cfe4ebe3488b8e23ef6caf9683fb23c10d4a0c;ORDER_AND_INPUT=PASS")
print("FORMAL_TRANSCRIPT=29b280119a919e1ae27a4adb18a24efa20c6254c91bf0d239f0b821edbbf0a00;FORMAL_MANIFEST=fe285502c8f3a0c8c9a8f21d543112d8435c7f980698062f7c709119bf98cf3c")
print("ALLOWED_TRACKED_SCOPE_AND_FINAL_STATES=PASS")
print("DOWNSTREAM_LEDGER_ZERO=REPORTED;SCOPED_ARTIFACTS_OBSERVED=0;NEGATIVE_EXECUTION_HISTORY=NOT_PROVEN")
print("DECISION=STOP/FAIL/NOT_READY")
print("POSTHOC_V4_DECISION_AUDIT=PASS")
```
<!-- V4_POSTHOC_DECISION_AUDIT_END -->

The candidate PASS is not a G0 GO because the separately committed formal gate failed. This immutable v4 evidence set is exhausted and must not be rerun.

G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V4=FAIL
DESKTOP_PACKAGE=NOT_READY

## Immediate multi-range retry v5 renewed offline authorization

On 2026-07-14, exact implementation base
`38203e62ae02e8992cb7214cfdab8634b3c3f5ba` completed the entire renewed non-public gate. The
earlier authorization at `b3bdb4ea76530e9cc65180b4df97967da87c16a5` is superseded. No public
AlphaEarth candidate/formal request was made; no v5 evidence path was created; and frozen v4
artifacts were not edited, chmodded, deleted, or rerun. The fixed Desktop app was not packaged or
replaced, and G1, tag, push, and release were not started.

### Dynamic offline totals and disposable probe

| V5 offline gate | Exit | Fresh result | Timing |
|---|---:|---:|---:|
| Manifest and bundle-audit Python suites | 0 | 66/66, zero skip/failure | 5.432 s internal; 5.52 s wall |
| Private dependency builder contract | 0 | PASS | 1.55 s wall |
| Refreshed `build/osgsol_core` full CTest | 0 | 20/20, including science-off `ScienceBuildContract` | 15.44 s CTest real |
| Selected `build/science_g0_prefetch` CTest | 0 | 9/9, including coordinator abandonment and blocked-operation capacity | 24.90 s CTest real |
| Standalone private-prefix verifier | 0 | three archives and private manifest PASS | 1.41 s wall |

The science-off build tree was reconfigured in place with `OSGSOL_BUILD_SCIENCE=OFF` before its
contract target and full CTest ran. Every explicit Python invocation and every CTest/script that
could call Python used `PYTHONDONTWRITEBYTECODE=1`; the final tree contained zero `__pycache__`
directories.

The disposable plugin/app build and canonical audit exited 0. The plugin SHA-256 is
`90c4816ee4cdc3d503b2f5f0a72738870e553714c2d1128433f2984cb9d06fc7`; audit JSON/text hashes
are `4cac090e6a68e7373fe7a9afcb93cb7c4a9815617aab23b99992682b5500fd70` and
`c46b9bf6df7c1ed04a702f9f5e675d35520454be9b1d3bc37ffa2bb2f480ff02`.

| V5 isolation measurement | Fresh result |
|---|---:|
| Mach-O graph nodes | 128 |
| Protected baseline / disposable probe | 542,594,200 / 563,899,055 bytes |
| Added size | 21,304,855 bytes, below 40 MiB |
| Science-only closure | 21,304,696 bytes, below 60 MiB |
| Tier A absolute science findings | 0 |
| Tier B new / removed identities | 0 / 0 |
| Unresolved dependencies / violations | 0 / 0 |
| Approved science export | `_osgsol_science_g0_probe_anchor`, exactly one |

Deep/strict codesign passed. Direct plugin dependencies are only system curl, SQLite, C++, and
System. Relative-path `otool` load/dependency output and `strings` contain neither the worktree nor
`science-deps-prefetch`. The canonical audit also proves no forbidden source/build graph edge from
the main app.

### Provenance, protected evidence, and Desktop

The private patch and configured pin both equal
`0e67079267a4adfc316f20c88ba22cf078e5d653d2f35c2e3b54fff7efd0de1e`; the exact downloads
match GDAL `e04e9813...c675`, PROJ `af5b731c...1960`, and ZSTD `eb33e51f...6fa3`. The private
prefix manifest remains
`a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116`.

The verifier recomputed a path/hash/mode manifest over every evidence file. Exact totals are old
formal 95, rejected diagnostic 34, v2 61, v3 37, v4 candidate/control 62, and v4 formal 31. The
last 130 files retain mode `0400`; v4 roots retain `0500`; the recorded v2/diagnostic and older
modes also match exactly. All three v4 31-entry SHA manifests reconcile one-to-one with their
complete evidence sets. The corrected v4 post-hoc auditor source, clean-HEAD wrapper, and
transcript hashes are respectively `7c200883...f661`, `0a959470...985`, and `7619084e...531d`;
the current and exact-five base `377dff7d23b1bb1028bceebf2701adcba1911f92` sources match,
and the transcript terminates in `POSTHOC_V4_DECISION_AUDIT=PASS`.

The protected Desktop fingerprint/helper/count tuple remains
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18 /`
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214 / 414 / 355`.
The v5 range contains no application, camera/photo, terrain, 3D Tiles, panel, or satellite path.
The formal CTest lists exactly one five-iteration latency-enforced `prefetch` command and still
targets frozen `formal-evidence-v4`. The candidate directory, candidate summary, and formal v5
path are all absent and non-symlinks.

### Base-bound authorization verifier and mutation tests

The executable verifier below binds the exact implementation base, exact union of the committed
range/index/worktree to these three authorization documents, zero unexpected untracked files,
patch/prefix/archive hashes, dynamic offline results, protected evidence, Desktop tuple, formal
profile/path, protected diff, exact retry codes, and future-v5 absence. Its self-test uses a
temporary synthetic tree and proves rejection of hash drift, a regular future artifact, a
dangling future symlink, a dirty protected path, broader HTTP 408 retry policy, and capacity logic
that would evict a live blocked-operation record. It also checks the exact 256-entry fail-closed
capacity contract. Its extracted source SHA-256 is recorded after the source block below.

<!-- V5_AUTHORIZATION_VERIFIER_BEGIN -->
```python
#!/usr/bin/env python3

from pathlib import Path
import hashlib
import importlib.util
import json
import os
import re
import stat
import subprocess
import sys
import tempfile


sys.dont_write_bytecode = True


EXACT_RETRY_CODES = (429, 500, 502, 503, 504)
ALLOWED_AUTHORIZATION_FILES = {
    "docs/scienceearth/g0-g1-baseline.md",
    "docs/scienceearth/g0-measurements.md",
    "docs/scienceearth/gdal-build.md",
}
ROOT = Path("/Users/USER/osgsol/.worktrees/v0.2-runtime-safety")
IMPLEMENTATION_BASE = "38203e62ae02e8992cb7214cfdab8634b3c3f5ba"
V5_DESIGN_BASE = "f0cfbc3b0d7feeda9cca649de5a53e4e3ac008dc"
PATCH_RELATIVE = "packaging/science_deps/gdal-3.13.1-parallel-head-range.patch"
VERSIONS_RELATIVE = "packaging/science_deps/versions.env"
PATCH_SHA256 = "0e67079267a4adfc316f20c88ba22cf078e5d653d2f35c2e3b54fff7efd0de1e"
VERSIONS_SHA256 = "2df93f51dcc78b9a5fbb5b3545e4ba6c1878547c1af8dac01fd384c21c280af9"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha256_bytes(data):
    return hashlib.sha256(data).hexdigest()


def require_hash_bytes(label, data, expected):
    actual = sha256_bytes(data)
    require(actual == expected, f"hash mismatch: {label}: {actual} != {expected}")


def require_file_hash(relative, expected):
    path = ROOT / relative
    require(path.is_file() and not path.is_symlink(), f"missing file: {relative}")
    require_hash_bytes(relative, path.read_bytes(), expected)


def run_git(*arguments):
    result = subprocess.run(
        ["git", *arguments], cwd=ROOT, capture_output=True, check=False)
    require(result.returncode == 0,
            f"git {' '.join(arguments)} failed: {result.stderr.decode(errors='replace')}")
    return result.stdout


def require_absent_non_symlink(path):
    require(not path.exists() and not path.is_symlink(),
            f"future evidence path exists or is a symlink: {path}")


def tree_manifest(root):
    require(root.is_dir() and not root.is_symlink(), f"missing evidence root: {root}")
    digest = hashlib.sha256()
    file_modes = {}
    directory_modes = {}
    files = set()
    for path in sorted(root.rglob("*"),
                       key=lambda item: item.relative_to(root).as_posix()):
        relative = path.relative_to(root).as_posix()
        require(not path.is_symlink(), f"evidence symlink: {path}")
        mode = stat.S_IMODE(path.stat().st_mode)
        if path.is_dir():
            directory_modes[relative] = mode
            continue
        require(path.is_file(), f"non-regular evidence entry: {path}")
        file_hash = sha256_bytes(path.read_bytes())
        digest.update(f"{relative}\0{mode:04o}\0{file_hash}\n".encode())
        file_modes[mode] = file_modes.get(mode, 0) + 1
        files.add(path)
    return (digest.hexdigest(), files, file_modes, directory_modes,
            stat.S_IMODE(root.stat().st_mode))


def extract_fenced_block(content, begin, end, fence):
    begin_bytes = begin.encode() + b"\n"
    end_bytes = end.encode() + b"\n"
    require(content.count(begin_bytes) == 1 and content.count(end_bytes) == 1,
            f"marker count mismatch: {begin}/{end}")
    start = content.index(begin_bytes) + len(begin_bytes)
    stop = content.index(end_bytes, start)
    lines = content[start:stop].splitlines(keepends=True)
    return b"".join(line for line in lines
                     if line.rstrip(b"\r\n") not in {fence.encode(), b"```"})


def verify_sha_manifest(manifest_relative, expected_manifest_hash,
                        expected_paths, expected_mode):
    require_file_hash(manifest_relative, expected_manifest_hash)
    manifest_path = ROOT / manifest_relative
    seen = set()
    for line in manifest_path.read_text().splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
        require(match is not None, f"malformed SHA manifest line: {line}")
        path = ROOT / match.group(2)
        require(path not in seen, f"duplicate SHA manifest path: {path}")
        require(path in expected_paths, f"unexpected SHA manifest path: {path}")
        require(path.is_file() and not path.is_symlink(),
                f"missing SHA manifest target: {path}")
        require_hash_bytes(str(path), path.read_bytes(), match.group(1))
        require(stat.S_IMODE(path.stat().st_mode) == expected_mode,
                f"SHA manifest target mode mismatch: {path}")
        seen.add(path)
    require(seen == expected_paths, f"incomplete SHA manifest: {manifest_relative}")


def unexpected_dirty_paths(paths):
    return sorted(set(paths) - ALLOWED_AUTHORIZATION_FILES)


def retry_code_sets(patch_text):
    result = {}
    for name in ("IsParallelHeadRangeTransientStatus",
                 "IsImmediateMultiRangeTransientStatus"):
        match = re.search(
            rf"static bool {name}\(long nStatus\).*?\n\+\{{(?P<body>.*?)\n\+\}}",
            patch_text, re.DOTALL)
        require(match is not None, f"missing retry classifier: {name}")
        result[name] = tuple(int(value) for value in re.findall(
            r"\+\s*case (\d+):", match.group("body")))
    return result


def require_exact_retry_codes(patch_text):
    for name, codes in retry_code_sets(patch_text).items():
        require(codes == EXACT_RETRY_CODES,
                f"broader or reordered retry codes in {name}: {codes}")


def require_capacity_contract(patch_text):
    capacity = re.search(
        r"knMAX_BLOCKED_OPERATIONS\s*=\s*(\d+);", patch_text)
    require(capacity is not None and int(capacity.group(1)) == 256,
            "blocked-operation capacity is not exactly 256")
    full = re.search(
        r"\+\s*if \(m_oParallelHeadRangeBlockedOperations\.size\(\) >=\s*\n"
        r"\+\s*knMAX_BLOCKED_OPERATIONS\)\s*\n\+\s*\{"
        r"(?P<body>.*?)\n\+\s*\}", patch_text, re.DOTALL)
    require(full is not None, "missing full-capacity branch")
    body = full.group("body")
    require("m_oParallelHeadRangeBlockedOverflowExpiry" in body and
            "return;" in body and ".erase(" not in body,
            "full-capacity branch does not fail closed without live eviction")
    require("scope=overflow" in patch_text and
            "m_oParallelHeadRangeBlockedOverflowExpiry !=" in patch_text and
            "std::chrono::steady_clock::time_point{}" in patch_text,
            "overflow fail-closed check is missing")


def self_test():
    payload = b"ScienceEarth-v5"
    require_hash_bytes("synthetic", payload, sha256_bytes(payload))
    try:
        require_hash_bytes("synthetic", payload, "0" * 64)
    except AssertionError as error:
        require("hash mismatch" in str(error), "hash drift rejection reason mismatch")
    else:
        raise AssertionError("hash drift was accepted")

    with tempfile.TemporaryDirectory(prefix="osgsol-v5-auth-selftest-") as temporary:
        root = Path(temporary)
        absent = root / "absent"
        require_absent_non_symlink(absent)
        regular = root / "regular"
        regular.write_bytes(b"unexpected")
        dangling = root / "dangling"
        dangling.symlink_to(root / "missing-target")
        for path in (regular, dangling):
            try:
                require_absent_non_symlink(path)
            except AssertionError as error:
                require("exists or is a symlink" in str(error),
                        "v5 path rejection reason mismatch")
            else:
                raise AssertionError(f"unexpected v5 path was accepted: {path}")
        tree = root / "tree"
        tree.mkdir(mode=0o700)
        item = tree / "item"
        item.write_bytes(b"item")
        item.chmod(0o600)
        digest, files, modes, directories, root_mode = tree_manifest(tree)
        expected_digest = sha256_bytes(
            f"item\0{0o600:04o}\0{sha256_bytes(b'item')}\n".encode())
        require((digest, files, modes, directories, root_mode) ==
                (expected_digest, {item}, {0o600: 1}, {}, 0o700),
                "tree manifest synthetic result mismatch")

    require(unexpected_dirty_paths(ALLOWED_AUTHORIZATION_FILES) == [],
            "allowed documentation was rejected")
    protected = "applications/earth_explorer/earth_main.cpp"
    require(unexpected_dirty_paths([protected]) == [protected],
            "dirty protected path was accepted")

    def classifier(name, codes):
        cases = "".join(f"+        case {code}:\n" for code in codes)
        return (f"static bool {name}(long nStatus)\n+{{\n" + cases +
                "+            return true;\n+}\n")

    exact = classifier("IsParallelHeadRangeTransientStatus", EXACT_RETRY_CODES)
    exact += classifier("IsImmediateMultiRangeTransientStatus", EXACT_RETRY_CODES)
    require_exact_retry_codes(exact)
    broader = exact.replace("+        case 429:\n", "+        case 408:\n+        case 429:\n", 1)
    try:
        require_exact_retry_codes(broader)
    except AssertionError as error:
        require("broader or reordered" in str(error),
                "retry-code rejection reason mismatch")
    else:
        raise AssertionError("broader retry codes were accepted")

    capacity = """+    static constexpr std::size_t knMAX_BLOCKED_OPERATIONS = 256;
+    if (m_oParallelHeadRangeBlockedOperations.size() >=
+        knMAX_BLOCKED_OPERATIONS)
+    {
+        m_oParallelHeadRangeBlockedOverflowExpiry = oNow + expiry;
+        return;
+    }
+    if (m_oParallelHeadRangeBlockedOverflowExpiry !=
+        std::chrono::steady_clock::time_point{})
+        CPLDebug(GetDebugKey(), "scope=overflow");
"""
    require_capacity_contract(capacity)
    eviction = capacity.replace(
        "+        m_oParallelHeadRangeBlockedOverflowExpiry = oNow + expiry;",
        "+        m_oParallelHeadRangeBlockedOperations.erase(\n"
        "+            m_oParallelHeadRangeBlockedOperations.begin());")
    try:
        require_capacity_contract(eviction)
    except AssertionError as error:
        require("without live eviction" in str(error),
                "capacity-eviction rejection reason mismatch")
    else:
        raise AssertionError("live blocked-operation eviction was accepted")
    print("V5_AUTHORIZATION_PURE_HELPERS=PASS;MUTATIONS=5/5")


def main():
    require(Path.cwd().resolve() == ROOT.resolve(), "wrong authorization worktree")
    require(run_git("cat-file", "-t", IMPLEMENTATION_BASE) == b"commit\n",
            "implementation base is not a commit")
    require(not run_git("tag", "--points-at", IMPLEMENTATION_BASE).strip(),
            "implementation base unexpectedly tagged")
    require(not run_git("tag", "--points-at", "HEAD").strip(),
            "authorization HEAD unexpectedly tagged")
    subprocess.run(
        ["git", "merge-base", "--is-ancestor", IMPLEMENTATION_BASE, "HEAD"],
        cwd=ROOT, check=True)
    require_hash_bytes(
        f"{IMPLEMENTATION_BASE}:{PATCH_RELATIVE}",
        run_git("show", f"{IMPLEMENTATION_BASE}:{PATCH_RELATIVE}"), PATCH_SHA256)
    require_hash_bytes(
        f"{IMPLEMENTATION_BASE}:{VERSIONS_RELATIVE}",
        run_git("show", f"{IMPLEMENTATION_BASE}:{VERSIONS_RELATIVE}"), VERSIONS_SHA256)
    require_file_hash(PATCH_RELATIVE, PATCH_SHA256)
    require_file_hash(VERSIONS_RELATIVE, VERSIONS_SHA256)
    patch_text = (ROOT / PATCH_RELATIVE).read_text()
    require_exact_retry_codes(patch_text)
    require_capacity_contract(patch_text)
    versions = (ROOT / VERSIONS_RELATIVE).read_text()
    require(f"GDAL_PREFETCH_PATCH_SHA256={PATCH_SHA256}\n" in versions,
            "private patch pin mismatch")

    range_files = set(run_git(
        "diff", "--name-only", f"{IMPLEMENTATION_BASE}..HEAD").decode().splitlines())
    dirty = set(run_git("diff", "--name-only").decode().splitlines())
    dirty.update(run_git("diff", "--cached", "--name-only").decode().splitlines())
    unexpected_dirty = unexpected_dirty_paths(dirty)
    require(not unexpected_dirty,
            f"dirty protected or unexpected tracked files: {unexpected_dirty}")
    require(range_files | dirty == ALLOWED_AUTHORIZATION_FILES,
            "authorization range/index/worktree scope is not the exact three documents")
    untracked = run_git(
        "ls-files", "--others", "--exclude-standard").decode().splitlines()
    require(not untracked, f"unexpected untracked files: {untracked}")
    diff_check = subprocess.run(
        ["git", "diff", "--check"], cwd=ROOT,
        capture_output=True, check=False)
    cached_diff_check = subprocess.run(
        ["git", "diff", "--cached", "--check"], cwd=ROOT,
        capture_output=True, check=False)
    range_diff_check = subprocess.run(
        ["git", "diff", "--check", f"{IMPLEMENTATION_BASE}..HEAD"],
        cwd=ROOT, capture_output=True, check=False)
    require(diff_check.returncode == 0 and cached_diff_check.returncode == 0 and
            range_diff_check.returncode == 0,
            "authorization documentation diff-check failed")
    protected_prefixes = (
        "applications/earth_explorer/", "readerwriter/", "plugins/osgdb_tms/",
    )
    v5_range = run_git(
        "diff", "--name-only", f"{V5_DESIGN_BASE}..HEAD").decode().splitlines()
    protected_changes = sorted(
        name for name in v5_range if name.startswith(protected_prefixes))
    require(not protected_changes,
            f"protected camera/photo/terrain/3D Tiles/panel/satellite diff: {protected_changes}")

    archives = {
        "gdal-3.13.1.tar.gz":
        "e04e9813bd215b56753d5554330c53be25f3df2d7ed7e6413a19e6b66751c675",
        "proj-9.8.1.tar.gz":
        "af5b731c145c1d13c4e3b4eeb7d167e94e845e440f71e3496b4ed8dae0291960",
        "zstd-1.5.7.tar.gz":
        "eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3",
    }
    for name, expected in archives.items():
        require_file_hash(f"build/science-deps-prefetch/downloads/{name}", expected)
    require_file_hash(
        "build/science-deps-prefetch/prefix/science-deps-manifest.json",
        "a42f77f80f252755bf78226e292a9857c71ea1033dc4d27ae016d47ba0fea116")

    future_paths = (
        "build/science_g0_prefetch/requalification-evidence-v5/candidate",
        "build/science_g0_prefetch/requalification-evidence-v5/candidate-summary.json",
        "build/science_g0_prefetch/formal-evidence-v5",
    )
    for relative in future_paths:
        require_absent_non_symlink(ROOT / relative)

    formal = subprocess.run([
        "ctest", "--test-dir", "build/science_g0_prefetch", "-N", "-V",
        "-R", "^osgVerse_Test_ScienceGdalLive$",
    ], cwd=ROOT, text=True, capture_output=True, check=False,
       env={**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})
    formal_output = formal.stdout + formal.stderr
    require(formal.returncode == 0, f"formal listing exit mismatch: {formal.returncode}")
    require(len(re.findall(
        r"Test #[0-9]+: osgVerse_Test_ScienceGdalLive", formal_output)) == 1,
        "formal listing count mismatch")
    for token in ('"--iterations" "5"', '"--profile" "prefetch"',
                  "formal-evidence-v4", '"--enforce-latency"'):
        require(token in formal_output, f"formal listing missing token: {token}")
    require("formal-evidence-v5" not in formal_output,
            "formal listing unexpectedly promoted to v5")

    evidence_expectations = {
        "build/science_g0/science-network-evidence": (
            "eceaea4ae00ab0d23d142955e24827f702991a576fad7f14a98d4d0bdf5f9d32",
            95, {0o644: 95}, {}, 0o755),
        "build/science_g0_prefetch/diagnostic-evidence": (
            "c694d244925eed3763856899a16a3b3947b00e6123e990d45d934a83b18145c8",
            34, {0o600: 34}, {"control": 0o700, "prefetch": 0o700}, 0o700),
        "build/science_g0_prefetch/requalification-evidence-v2": (
            "a08b2ee95d8114e976593d73f07f18ac64dff3a54abc79fc33b4cb6e9bb47f6a",
            61, {0o600: 61}, {"control": 0o700, "prefetch": 0o700}, 0o700),
        "build/science_g0_prefetch/requalification-evidence-v3": (
            "d7b3ac3a9976babc843bebf2bf7abff34b1045aefb58e8bbef3265270feeae0e",
            37, {0o400: 37}, {"control": 0o700, "prefetch": 0o700}, 0o700),
        "build/science_g0_prefetch/requalification-evidence-v4": (
            "25b9d090f12ab0a53121c3f859c4dda8e95856d44f0ae8eedbe6fa6a0f9cc577",
            62, {0o400: 62}, {"control": 0o500, "prefetch": 0o500}, 0o500),
        "build/science_g0_prefetch/formal-evidence-v4": (
            "99cba4c8d20173fb1ee05321eafd9b2efa70e9d294bd1cbf178abec4ae32e8aa",
            31, {0o400: 31}, {}, 0o500),
    }
    evidence_files = {}
    for relative, expected in evidence_expectations.items():
        digest, files, modes, directories, root_mode = tree_manifest(ROOT / relative)
        require((digest, len(files), modes, directories, root_mode) == expected,
                f"immutable evidence manifest/mode mismatch: {relative}")
        evidence_files[relative] = files

    critical_hashes = {
        "build/science_g0_prefetch/diagnostic-evidence/control-summary.json":
        "f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096",
        "build/science_g0_prefetch/diagnostic-evidence/prefetch-summary.json":
        "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff",
        "build/science_g0/science-network-evidence/baseline-summary.json":
        "17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed",
        "build/science_g0/science-network-evidence/live-summary.json":
        "e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe",
        "build/science_g0_prefetch/requalification-evidence-v2/control-summary.json":
        "0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0",
        "build/science_g0_prefetch/requalification-evidence-v2/prefetch-summary.json":
        "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff",
        "build/science_g0_prefetch/requalification-evidence-v3/control-summary.json":
        "47abb0bc91e4afba412d1150822b90110650f8e225b45b7fa52f4627889c405c",
        "build/science_g0_prefetch/requalification-evidence-v3/prefetch-summary.json":
        "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff",
    }
    for relative, expected in critical_hashes.items():
        require_file_hash(relative, expected)

    requalification_root = ROOT / (
        "build/science_g0_prefetch/requalification-evidence-v4")
    requalification_files = evidence_files[
        "build/science_g0_prefetch/requalification-evidence-v4"]
    control_paths = {
        path for path in requalification_files
        if path == requalification_root / "control-summary.json" or
        (requalification_root / "control") in path.parents
    }
    candidate_paths = {
        path for path in requalification_files
        if path == requalification_root / "prefetch-summary.json" or
        (requalification_root / "prefetch") in path.parents
    }
    formal_paths = evidence_files["build/science_g0_prefetch/formal-evidence-v4"]
    verify_sha_manifest(
        ".superpowers/sdd/task-3-v4-control-artifacts.sha256",
        "ce35d41589010eba54b3ccee7b486fb5bbb7dd988141752eba41375ebed393a3",
        control_paths, 0o400)
    verify_sha_manifest(
        ".superpowers/sdd/task-3-v4-candidate-artifacts.sha256",
        "81451c29000dab2bcaa262615eb8e9d72ad382adae3269892581423b2587b339",
        candidate_paths, 0o400)
    verify_sha_manifest(
        ".superpowers/sdd/task-3-v4-formal-artifacts.sha256",
        "fe285502c8f3a0c8c9a8f21d543112d8435c7f980698062f7c709119bf98cf3c",
        formal_paths, 0o400)

    v4_ledger = {
        ".superpowers/sdd/task-3-v4-preflight.log":
        "2bae6ec0237839cc14cdde0f90c8084b89b46336ecee20ebaf4598b7d6d2f22b",
        ".superpowers/sdd/task-3-v4-control.log":
        "b2ec9bf61a949a80b6a0fa6c87491ccff79837ab3a201aff445a07423f2f323b",
        ".superpowers/sdd/task-3-v4-candidate.log":
        "81b40fbb098716e59ceada54b7bf33079fa7fcdf8a866aeb1d1828aafe2f434c",
        ".superpowers/sdd/task-3-v4-formal-refresh.log":
        "5e8873e10bbe939d39451dc93da7e40ba25d39005c7284f979d867e8f78c14b9",
        ".superpowers/sdd/task-3-v4-formal-ctest.log":
        "29b280119a919e1ae27a4adb18a24efa20c6254c91bf0d239f0b821edbbf0a00",
        ".superpowers/sdd/task-3-v4-posthoc-audit.log":
        "7619084e74eb89e8cf2d79b792f4e21d8b151e6461028b266e52cd651906531d",
    }
    for relative, expected in v4_ledger.items():
        require_file_hash(relative, expected)
        require(stat.S_IMODE((ROOT / relative).stat().st_mode) == 0o400,
                f"v4 ledger mode mismatch: {relative}")
    require((ROOT / ".superpowers/sdd/task-3-v4-posthoc-audit.log")
            .read_text().splitlines()[-1] == "POSTHOC_V4_DECISION_AUDIT=PASS",
            "v4 posthoc transcript did not finish PASS")

    document_relative = "docs/scienceearth/g0-measurements.md"
    document = (ROOT / document_relative).read_bytes()
    audit_begin = "<!-- V4_POSTHOC_DECISION_AUDIT_BEGIN -->"
    audit_end = "<!-- V4_POSTHOC_DECISION_AUDIT_END -->"
    wrapper_begin = "<!-- V4_POSTHOC_DECISION_WRAPPER_BEGIN -->"
    wrapper_end = "<!-- V4_POSTHOC_DECISION_WRAPPER_END -->"
    audit_source = extract_fenced_block(
        document, audit_begin, audit_end, "```python")
    wrapper_source = extract_fenced_block(
        document, wrapper_begin, wrapper_end, "```bash")
    require_hash_bytes(
        "v4 posthoc source", audit_source,
        "7c2008834b24e6af519390a05dc8a9317c21f29e40ecc535e992d3843211f661")
    require_hash_bytes(
        "v4 posthoc wrapper", wrapper_source,
        "0a9594708a084db4669c3199315eb7e1a1878cb636d87190644ff3bd5a82b985")
    v4_audit_base = "377dff7d23b1bb1028bceebf2701adcba1911f92"
    base_document = run_git("show", f"{v4_audit_base}:{document_relative}")
    require_hash_bytes(
        "v4 base posthoc source",
        extract_fenced_block(base_document, audit_begin, audit_end, "```python"),
        "7c2008834b24e6af519390a05dc8a9317c21f29e40ecc535e992d3843211f661")
    require_hash_bytes(
        "v4 base posthoc wrapper",
        extract_fenced_block(base_document, wrapper_begin, wrapper_end, "```bash"),
        "0a9594708a084db4669c3199315eb7e1a1878cb636d87190644ff3bd5a82b985")

    offline_logs = {
        ".superpowers/sdd/task-3-v5-renewed-offline-python.log": (
            "f8d15a434f6a0e5951ce1c9c5d1e15bb4133fdb6425770eb157405564f6b91a9",
            ("Ran 66 tests", "\nOK\n")),
        ".superpowers/sdd/task-3-v5-renewed-offline-deps-contract.log": (
            "93913923aaaa04b56115f0ec75f655c7054348eda553aa12364f30e9db25f8aa",
            ("[OK] ScienceEarth private dependency builder contract",)),
        ".superpowers/sdd/task-3-v5-renewed-science-off-reconfigure.log": (
            "3758bd29d7e3cff3e0684e8b6aef29356e1a033ff320b4906ed81a27e7122a26",
            ("Configuring done", "Generating done")),
        ".superpowers/sdd/task-3-v5-renewed-science-off-contract-build.log": (
            "f519272382694a9cd14dd45a772ac7e6d6d403a928fd96ab04eafc0717009692",
            ("Built target osgVerse_Test_ScienceBuildContract",)),
        ".superpowers/sdd/task-3-v5-renewed-offline-core-ctest.log": (
            "fca889edbe2d968d00807d6f5f34427fcaf057b0b42eddea94261e6aec455367",
            ("0 tests failed out of 20", "osgVerse_Test_ScienceBuildContract")),
        ".superpowers/sdd/task-3-v5-renewed-offline-science-ctest.log": (
            "9109d5a3f3d5764c8a55c7e048dc349d6894001c3725cd07279a2dc92f72acc1",
            ("0 tests failed out of 9",
             "osgVerse_Test_ScienceHttpRangesCoordinatorAbandonment",
             "osgVerse_Test_ScienceHttpRangesBlockedOperationCapacity")),
        ".superpowers/sdd/task-3-v5-renewed-offline-prefix-verify.log": (
            "21e4c1406731894e6194d852b4b31b9cd6b8bc048b054c704a45fb3b8b4fb867",
            ("gdal-3.13.1.tar.gz: OK", "proj-9.8.1.tar.gz: OK",
             "zstd-1.5.7.tar.gz: OK",
             "verified private static prefix and manifest")),
        ".superpowers/sdd/task-3-v5-renewed-probe-target-build.log": (
            "c01078f0d48c18543339d32102563ceb7c2fee369fd979425cc012ff04154d6c",
            ("Built target osgdb_science_g0_probe",)),
        ".superpowers/sdd/task-3-v5-renewed-probe-app-build.log": (
            "271747896144b65623c3689989c8056356cacd3f9d935a278363976dd45172d5",
            ("protected baseline fingerprint unchanged: "
             "91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18",
             "built disposable probe:")),
        ".superpowers/sdd/task-3-v5-renewed-probe-audit.log": (
            "da0fb4e28fbbb420ffa6af4501e252754b670212053be08e6db3b9cd3d25a1a5",
            ("ScienceEarth macOS bundle audit: PASS", "Violations\n  none")),
    }
    for relative, (expected, required_tokens) in offline_logs.items():
        require_file_hash(relative, expected)
        content = (ROOT / relative).read_text(errors="replace")
        for token in required_tokens:
            require(token in content, f"offline log missing token: {relative}: {token}")
    require("skipped" not in (ROOT / ".superpowers/sdd/task-3-v5-renewed-offline-python.log")
            .read_text().lower(), "Python suite contains a skip")

    audit_json_relative = "build/science_g0_prefetch/bundle-audit.json"
    audit_text_relative = "build/science_g0_prefetch/bundle-audit.txt"
    plugin_relative = "build/science_g0_prefetch/lib/osgdb_science_g0_probe.so"
    require_file_hash(
        audit_json_relative,
        "4cac090e6a68e7373fe7a9afcb93cb7c4a9815617aab23b99992682b5500fd70")
    require_file_hash(
        audit_text_relative,
        "c46b9bf6df7c1ed04a702f9f5e675d35520454be9b1d3bc37ffa2bb2f480ff02")
    require_file_hash(
        plugin_relative,
        "90c4816ee4cdc3d503b2f5f0a72738870e553714c2d1128433f2984cb9d06fc7")
    audit = json.loads((ROOT / audit_json_relative).read_text())
    require(audit["status"] == "PASS" and audit["ok"] is True and
            audit["exit_code"] == 0, "canonical audit status mismatch")
    require(audit["sizes"] == {
        "baseline_bytes": 542594200,
        "delta_bytes": 21304855,
        "science_closure_bytes": 21304696,
        "total_bytes": 563899055,
    }, "canonical audit sizes mismatch")
    require(audit["sizes"]["delta_bytes"] < 40 * 1024 * 1024 and
            audit["sizes"]["science_closure_bytes"] < 60 * 1024 * 1024,
            "canonical audit immutable size gate mismatch")
    require(len(audit["graph"]) == 128 and not audit["unresolved"] and
            not audit["absolute"]["science"] and
            len(audit["absolute"]["non_science"]) == 1086 and
            audit["delta"] == {"new": [], "ok": True, "removed": []} and
            not audit["violations"] and len(audit["findings"]) == 1086,
            "canonical audit isolation totals mismatch")
    require(audit["science_only_closure"] == [
        "Contents/lib/osgPlugins-3.6.5/osgdb_science.so"],
        "canonical science closure membership mismatch")

    probe_app = ROOT / "build/science_g0_prefetch/osgSol Science G0 Probe.app"
    signature = subprocess.run(
        ["/usr/bin/codesign", "--verify", "--deep", "--strict", str(probe_app)],
        cwd=ROOT, capture_output=True, check=False)
    require(signature.returncode == 0,
            f"disposable probe signature mismatch: {signature.stderr!r}")
    plugin = ROOT / plugin_relative
    exports = subprocess.run(
        ["nm", "-gU", plugin_relative], cwd=ROOT, text=True,
        capture_output=True, check=False)
    require(exports.returncode == 0 and
            [line.split()[-1] for line in exports.stdout.splitlines()] ==
            ["_osgsol_science_g0_probe_anchor"],
            "disposable probe export mismatch")
    loads = subprocess.run(
        ["otool", "-L", plugin_relative], cwd=ROOT, text=True,
        capture_output=True, check=False)
    dependencies = sorted(
        line.strip().split()[0] for line in loads.stdout.splitlines()[1:])
    require(loads.returncode == 0 and dependencies == sorted([
        "/usr/lib/libSystem.B.dylib", "/usr/lib/libc++.1.dylib",
        "/usr/lib/libcurl.4.dylib", "/usr/lib/libsqlite3.dylib",
    ]), f"disposable probe dependency mismatch: {dependencies}")
    load_commands = subprocess.run(
        ["otool", "-l", plugin_relative], cwd=ROOT,
        capture_output=True, check=False)
    strings = subprocess.run(
        ["strings", "-a", plugin_relative], cwd=ROOT,
        capture_output=True, check=False)
    require(load_commands.returncode == 0 and strings.returncode == 0,
            "disposable probe path scan command failed")
    path_scan = loads.stdout.encode() + load_commands.stdout + strings.stdout
    require(b"/Users/USER/osgsol/.worktrees" not in path_scan and
            b"science-deps-prefetch" not in path_scan,
            "disposable probe contains a worktree/private-prefix path")

    require_file_hash(
        "packaging/scienceearth/g0_manifest.py",
        "38dd891896fa9ce9768163ded36069c329cae98e516a836ec98fc004f50a62f2")
    require_file_hash(
        "tests/science_bundle_audit_tests.py",
        "76fe4c1970d4d22e1c3bdd7d77852af69563993b33bc702d5e1446d1d5bf81a0")
    sys.path.insert(0, str(ROOT))
    from tests.science_bundle_audit_tests import ScienceProbeBuilderTests, MANIFEST
    desktop = Path("/Users/USER/Desktop/osgSol Earth.app")
    require(desktop.is_dir() and not desktop.is_symlink(), "Desktop app missing")
    desktop_entries = list(desktop.rglob("*"))
    desktop_regular = [
        path for path in desktop_entries if path.is_file() and not path.is_symlink()
    ]
    desktop_tuple = (
        MANIFEST.bundle_fingerprint(desktop),
        ScienceProbeBuilderTests().tree_digest(desktop),
        len(desktop_entries), len(desktop_regular),
    )
    require(desktop_tuple == (
        "91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18",
        "14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214",
        414, 355), "Desktop protected tuple mismatch")

    print(f"IMPLEMENTATION_BASE={IMPLEMENTATION_BASE};ALLOWED_SCOPE=PASS")
    print(f"PRIVATE_PATCH={PATCH_SHA256};ARCHIVES=3/3;PREFIX=PASS")
    print("RETRY_CODES=429,500,502,503,504;CLASSIFIERS=2/2")
    print("BLOCKED_OPERATION_CAPACITY=256/FAIL_CLOSED/NO_LIVE_EVICTION")
    print("PROTECTED_REGRESSION_DIFF=0;V5_ABSENT_NON_SYMLINK=3/3")
    print("FORMAL=1_TEST/5_ITERATIONS/PREFETCH/FORMAL_EVIDENCE_V4")
    print("IMMUTABLE_EVIDENCE=OLD_95/DIAGNOSTIC_34/V2_61/V3_37/V4_62+31;HASHES_SETS_MODES=PASS")
    print("V4_POSTHOC=SOURCE_BASE_WRAPPER_TRANSCRIPT_PASS")
    print("OFFLINE=PYTHON_66/CORE_20/SCIENCE_9/CONTRACT_VERIFY_PASS;SKIP_FAIL=0")
    print("PROBE=TIER_A_0/TIER_B_0_0/DELTA_21304855/CLOSURE_21304696/EXPORT_1/CODESIGN_PASS")
    print("DESKTOP=91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18/14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214/414/355")
    print("V5_AUTHORIZATION_VERIFIER=PASS")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif sys.argv[1:]:
        raise SystemExit("usage: verifier [--self-test]")
    else:
        main()
```
<!-- V5_AUTHORIZATION_VERIFIER_END -->

The commit-before extraction test used the same marker rules as the final gate:

```bash
verifier=$(mktemp "${TMPDIR:-/tmp}/osgsol-v5-authorization.XXXXXX.py")
trap 'rm -f "$verifier"' EXIT
awk '
  $0 == "<!-- V5_AUTHORIZATION_VERIFIER_BEGIN -->" { capture = 1; next }
  $0 == "<!-- V5_AUTHORIZATION_VERIFIER_END -->" { exit }
  capture && $0 == "```python" { next }
  capture && $0 == "```" { next }
  capture { print }
' docs/scienceearth/g0-measurements.md > "$verifier"
test "$(shasum -a 256 "$verifier" | awk '{print $1}')" = \
  2a7e4d999d1e2ae76e1be668fd00bf555ae55b84ac510af0a7aeec7ada963878
PYTHONDONTWRITEBYTECODE=1 python3 "$verifier" --self-test
PYTHONDONTWRITEBYTECODE=1 python3 "$verifier"
```

It exited 0. The combined self-test/main transcript at
`.superpowers/sdd/task-3-v5-renewed-verifier-green-precommit.log` hashes to
`941ff771dc866ad19a9297dc935ff615c543aac6cc25c6826d9a3a899763d203` and contains the exact
output asserted by the final gate. The ignored transcript is supporting execution evidence; the
committed verifier source and clean-HEAD gate are the durable authorization controls.

The exact final clean-HEAD extraction gate is preserved below. It requires the authorization
commit parent to be the implementation base and the committed range to contain exactly the three
documents, then runs the mutation self-test and the full read-only verifier with proxies removed.

<!-- V5_AUTHORIZATION_FINAL_GATE_BEGIN -->
```bash
#!/usr/bin/env bash
set -euo pipefail
umask 077

root=/Users/USER/osgsol/.worktrees/v0.2-runtime-safety
doc=docs/scienceearth/g0-measurements.md
base=38203e62ae02e8992cb7214cfdab8634b3c3f5ba
expected_source=2a7e4d999d1e2ae76e1be668fd00bf555ae55b84ac510af0a7aeec7ada963878
cd "$root"

test -z "$(git status --porcelain=v1 --untracked-files=no)"
test -z "$(git ls-files --others --exclude-standard)"
test -z "$(git tag --points-at HEAD)"
test "$(git rev-parse HEAD^)" = "$base"
expected_scope=$(printf '%s\n' docs/scienceearth/g0-g1-baseline.md \
    docs/scienceearth/g0-measurements.md docs/scienceearth/gdal-build.md | sort)
actual_scope=$(git diff --name-only "$base..HEAD" | sort)
test "$actual_scope" = "$expected_scope"
git diff --check "$base..HEAD"

marker_counts=$(git show HEAD:"$doc" | awk '
  /^<!-- V5_AUTHORIZATION_VERIFIER_BEGIN -->$/ { opens += 1 }
  /^<!-- V5_AUTHORIZATION_VERIFIER_END -->$/ { closes += 1 }
  END { printf "%d %d", opens, closes }
')
test "$marker_counts" = "1 1"
verifier=$(mktemp "${TMPDIR:-/tmp}/osgsol-v5-authorization.XXXXXX.py")
trap 'rm -f "$verifier"' EXIT
git show HEAD:"$doc" | awk '
  $0 == "<!-- V5_AUTHORIZATION_VERIFIER_BEGIN -->" { capture = 1; next }
  $0 == "<!-- V5_AUTHORIZATION_VERIFIER_END -->" { exit }
  capture && $0 == "```python" { next }
  capture && $0 == "```" { next }
  capture { print }
' > "$verifier"
test "$(shasum -a 256 "$verifier" | awk '{print $1}')" = "$expected_source"

self_test=$(env -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY \
    -u http_proxy -u https_proxy -u all_proxy \
    PYTHONDONTWRITEBYTECODE=1 python3 "$verifier" --self-test)
test "$self_test" = 'V5_AUTHORIZATION_PURE_HELPERS=PASS;MUTATIONS=5/5'
output=$(env -u HTTP_PROXY -u HTTPS_PROXY -u ALL_PROXY \
    -u http_proxy -u https_proxy -u all_proxy \
    PYTHONDONTWRITEBYTECODE=1 python3 "$verifier")
expected_output=$(cat <<'EOF'
IMPLEMENTATION_BASE=38203e62ae02e8992cb7214cfdab8634b3c3f5ba;ALLOWED_SCOPE=PASS
PRIVATE_PATCH=0e67079267a4adfc316f20c88ba22cf078e5d653d2f35c2e3b54fff7efd0de1e;ARCHIVES=3/3;PREFIX=PASS
RETRY_CODES=429,500,502,503,504;CLASSIFIERS=2/2
BLOCKED_OPERATION_CAPACITY=256/FAIL_CLOSED/NO_LIVE_EVICTION
PROTECTED_REGRESSION_DIFF=0;V5_ABSENT_NON_SYMLINK=3/3
FORMAL=1_TEST/5_ITERATIONS/PREFETCH/FORMAL_EVIDENCE_V4
IMMUTABLE_EVIDENCE=OLD_95/DIAGNOSTIC_34/V2_61/V3_37/V4_62+31;HASHES_SETS_MODES=PASS
V4_POSTHOC=SOURCE_BASE_WRAPPER_TRANSCRIPT_PASS
OFFLINE=PYTHON_66/CORE_20/SCIENCE_9/CONTRACT_VERIFY_PASS;SKIP_FAIL=0
PROBE=TIER_A_0/TIER_B_0_0/DELTA_21304855/CLOSURE_21304696/EXPORT_1/CODESIGN_PASS
DESKTOP=91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18/14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214/414/355
V5_AUTHORIZATION_VERIFIER=PASS
EOF
)
test "$output" = "$expected_output"

expected_state=$(printf '%s\n' G0_DECISION=STOP \
    PUBLIC_REQUALIFICATION_V5=AUTHORIZED_NOT_RUN DESKTOP_PACKAGE=NOT_READY)
for state_doc in docs/scienceearth/gdal-build.md \
    docs/scienceearth/g0-measurements.md docs/scienceearth/g0-g1-baseline.md
do
    test "$(tail -n 3 "$state_doc")" = "$expected_state"
done
test -z "$(find . -type d -name __pycache__ -prune -print)"

printf 'AUTHORIZATION_HEAD=%s;PARENT=%s;SCOPE=3_DOCS\n' "$(git rev-parse HEAD)" "$base"
printf 'VERIFIER_SOURCE_SHA256=%s\n' "$expected_source"
printf '%s\n' "$self_test" "$output" 'V5_AUTHORIZATION_FINAL_GATE=PASS'
```
<!-- V5_AUTHORIZATION_FINAL_GATE_END -->

G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V5=FAIL
DESKTOP_PACKAGE=NOT_READY

## Immediate multi-range retry v5 one-shot public decision

The network-disabled preflight ran under a macOS sandbox that denied all network operations. It
finished at `2026-07-14T02:47:46+0800` on clean, untagged authorization commit
`a0527b3b72121f423c46206f397bb145f73e72f8`. The committed final authorization gate, all five
mutation tests, private patch/prefix/archive bindings, protected Desktop tuple, and frozen v4
control passed. The exact live binary SHA-256 was
`4e6c4cf9cc4e89523691022ecfc6822df64cdae979f210f85ad838b839d4aa37`; formal CTest still
listed one five-iteration, latency-enforced `prefetch` command targeting v4. The preflight
wrapper/transcript hashes are `23926534ec1ab3aadc43f4e2ea647878a3c60b06a8c4674dec9097cc27334142`
and `12b3447fdb3e498cab0cd4a6a923fae3b6ad2d5b3d64be20a15d14a9e3c199b9`.

Exactly one v5 candidate executable process is recorded by the preserved transcript. It started
at `2026-07-14T02:48:35+0800`, ended at `2026-07-14T02:48:43+0800`, and exited 1 after
`ScienceHttpRanges failure: HEAD request/response count mismatch (possible GET 200)`. The
wrapper/transcript hashes are `b472a4f23e9b35140d8ad88abc747a74adb005a6c70b51623a3e4a2c3a8d858a`
and `0680b84a424b96d7f5c571746e9a3c198d2b2df031f118a7029c48df8b236014`. These hashes and
the scoped filesystem establish the recorded one-shot ledger; they cannot prove the absolute
absence of an omitted, externally recorded, overwritten, or otherwise unobserved process.

The failed candidate was frozen without deletion or resampling. Its directory is mode `0500`;
five directory artifacts and the sibling summary are mode `0400`. The complete six-entry
manifest hashes to `d9be21f1bb3af331e0ed20e4ec6300311dd82b5575a92d985e40faaaee635eb4`.
The exact artifact set is:

| Artifact | SHA-256 |
|---|---|
| `prefetch-nvidia_hq-1-curl-cpl.log` | `af9be410203296e1ce3c1d07dbf6ab925d01383153cb51795f53c5436cba254b` |
| `prefetch-nvidia_hq-1-network-stats.json` | `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe` |
| `prefetch-nvidia_hq-1-proof.json` | `47d6da3ca7cc3cb8caf5050d1e3d98d166214fe1098209b7a1d5f6729f35daf5` |
| `prefetch-nvidia_hq-2-curl-cpl.log` | `ff843ae5dafde039698e929ebd0cccf5f8eb7d54296d0bd5e491c2730351babd` |
| `prefetch-nvidia_hq-2-network-stats.json` | `21d10ea1b4eb5ebab1aa98fbc7f84763702dad87d152e0c483b8983565c46c8a` |
| `candidate-summary.json` | `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff` |

The independent audit constructs all ten expected case/iteration stems before examining what is
present. It found two raw logs, two network-stat files, one proof, and only one complete triplet:
`prefetch-nvidia_hq-1`. That completed triplet reconciles exact request counts, body bytes,
network statistics, immediate retry fields, successful intervals, CRS/geotransform/WGS84 bounds,
overlap, HTTP/2 shared connection, publication, and zero coordinator terminal fallback. Its
recorded wall latency was `3763.36 ms`. The second iteration has one
`fallback=head-invalid`, two HEAD operations, two immediate retries, and no proof. Eight raw
logs, eight stat files, and nine proofs are absent. The summary is fail-closed
`status=ERROR`, `cases=[]`, `profile=prefetch`, with unchanged 3000/8000 ms limits.
Neither case has five timings, so no candidate median or P95 can be computed. Complete ten-proof,
zero-fallback, both-case latency, and aggregate reconciliation gates therefore fail independently
of the process exit.

Candidate PASS was not established, so the formal v5 path remains absent and non-symlink. No
formal process, CMake promotion, Desktop packaging, G1, tag, push, or release is authorized by
this decision. Frozen v4 remains 93 files with exact tree SHA-256
`0a4ca8dc1dd0e1509a912c194e97c14194327890545d1e58e78ec913bc46cda0`, file mode `0400`,
and root mode `0500`. The protected Desktop tuple remains
`91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18 /`
`14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214 / 414 / 355`.

The durable decision auditor below recomputes these substantive conclusions from the frozen
raw/stats/proof artifacts, complete manifest, transcript and wrapper digests, source/fixture/CMake
bindings, v4 control, formal absence/listing, and Desktop tuple. Its extracted source SHA-256 is
`e5a6d34adf1d518377c5e2323bfeb8114c65fb47de05d86cffd9b1f2546f0b07`.

The quality-review correction makes the evidence scanners structural as well as content-aware.
The v5 parent accepts exactly the candidate directory and regular summary; the candidate accepts
exactly the five named flat regular files. The frozen v4 requalification parent accepts exactly
its two roots and two summaries; prefetch/control/formal roots accept exact flat 30/30/31 regular
file sets. Every required directory is non-symlink with its pinned mode, and every artifact is a
non-symlink regular file with mode `0400`. Authentic disposable-copy mutations prove rejection of
an unexpected regular file, dangling symlink, nested directory, and FIFO without changing any
frozen evidence.

<!-- V5_DECISION_AUDITOR_BEGIN -->
```python
#!/usr/bin/env python3

from collections import Counter
from pathlib import Path
import hashlib
import importlib.util
import json
import math
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile


sys.dont_write_bytecode = True


ROOT = Path("/Users/USER/osgsol/.worktrees/v0.2-runtime-safety")
AUTHORIZATION_HEAD = "a0527b3b72121f423c46206f397bb145f73e72f8"
REVIEW_FIX_PARENT = "73b6964bae06014f46fabe86220af551ba252f65"
DECISION_DOCS = {
    "docs/scienceearth/g0-measurements.md",
    "docs/scienceearth/g0-g1-baseline.md",
}
CANDIDATE = ROOT / "build/science_g0_prefetch/requalification-evidence-v5/candidate"
SUMMARY = ROOT / "build/science_g0_prefetch/requalification-evidence-v5/candidate-summary.json"
FORMAL = ROOT / "build/science_g0_prefetch/formal-evidence-v5"
V5_PARENT = CANDIDATE.parent
BINARY = ROOT / "build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges"
EXPECTED_BINARY_SHA256 = "4e6c4cf9cc4e89523691022ecfc6822df64cdae979f210f85ad838b839d4aa37"
EXPECTED_FIXTURE_SHA256 = "6a67af9a1380250704b9032f8b4933965196ed2a119f07be4cb99dafe032806d"
EXPECTED_CMAKE_SHA256 = "f7c52749e4572076f80e0cc01bbeb2ac08a06f10a3654a659816a016470d5af8"

SUPPORTING_HASHES = {
    ".superpowers/sdd/task-4-v5-one-shot-preflight.sh":
        "23926534ec1ab3aadc43f4e2ea647878a3c60b06a8c4674dec9097cc27334142",
    ".superpowers/sdd/task-4-v5-one-shot-preflight.log":
        "12b3447fdb3e498cab0cd4a6a923fae3b6ad2d5b3d64be20a15d14a9e3c199b9",
    ".superpowers/sdd/task-4-v5-one-shot-candidate.sh":
        "b472a4f23e9b35140d8ad88abc747a74adb005a6c70b51623a3e4a2c3a8d858a",
    ".superpowers/sdd/task-4-v5-one-shot-candidate.log":
        "0680b84a424b96d7f5c571746e9a3c198d2b2df031f118a7029c48df8b236014",
    ".superpowers/sdd/task-4-v5-candidate-manifest.sha256":
        "d9be21f1bb3af331e0ed20e4ec6300311dd82b5575a92d985e40faaaee635eb4",
}

EVIDENCE_BINDINGS = {
    "prefetch-nvidia_hq-1-curl-cpl.log":
        ("af9be410203296e1ce3c1d07dbf6ab925d01383153cb51795f53c5436cba254b", 37933),
    "prefetch-nvidia_hq-1-network-stats.json":
        ("0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe", 1571),
    "prefetch-nvidia_hq-1-proof.json":
        ("47d6da3ca7cc3cb8caf5050d1e3d98d166214fe1098209b7a1d5f6729f35daf5", 1963),
    "prefetch-nvidia_hq-2-curl-cpl.log":
        ("ff843ae5dafde039698e929ebd0cccf5f8eb7d54296d0bd5e491c2730351babd", 47144),
    "prefetch-nvidia_hq-2-network-stats.json":
        ("21d10ea1b4eb5ebab1aa98fbc7f84763702dad87d152e0c483b8983565c46c8a", 1769),
}
SUMMARY_BINDING = (
    "1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff", 126)
EXPECTED_V4_TREE_SHA256 = "0a4ca8dc1dd0e1509a912c194e97c14194327890545d1e58e78ec913bc46cda0"


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def require_file(path, expected_hash, expected_size=None, expected_mode=None):
    require(path.is_file() and not path.is_symlink(), f"missing regular file: {path}")
    require(sha256(path) == expected_hash, f"hash mismatch: {path}")
    if expected_size is not None:
        require(path.stat().st_size == expected_size, f"size mismatch: {path}")
    if expected_mode is not None:
        require(stat.S_IMODE(path.stat().st_mode) == expected_mode,
                f"mode mismatch: {path}")


def run_git(*arguments):
    result = subprocess.run(["git", *arguments], cwd=ROOT, capture_output=True,
                            check=False)
    require(result.returncode == 0,
            f"git {' '.join(arguments)} failed: {result.stderr.decode(errors='replace')}")
    return result.stdout.decode().splitlines()


def require_decision_scope(precommit):
    require(not run_git("tag", "--points-at", "HEAD"), "decision HEAD has a tag")
    require(not run_git("ls-files", "--others", "--exclude-standard"),
            "unexpected untracked file")
    if precommit:
        require(run_git("rev-parse", "HEAD") == [REVIEW_FIX_PARENT],
                "precommit review-fix parent mismatch")
        worktree = set(run_git("diff", "--name-only"))
        index = set(run_git("diff", "--cached", "--name-only"))
        require(worktree == DECISION_DOCS and not index,
                f"precommit decision scope mismatch: {worktree}/{index}")
    else:
        require(not run_git("status", "--porcelain=v1", "--untracked-files=no"),
                "tracked worktree/index is dirty")
        require(run_git("rev-parse", "HEAD^") == [REVIEW_FIX_PARENT],
                "review-fix parent mismatch")
        committed = set(run_git("diff", "--name-only", f"{AUTHORIZATION_HEAD}..HEAD"))
        require(committed == DECISION_DOCS,
                f"committed decision scope mismatch: {committed}")
        require(not run_git("diff", "--check", f"{AUTHORIZATION_HEAD}..HEAD"),
                "decision diff-check failed")

    for document in DECISION_DOCS:
        tail = (ROOT / document).read_text().splitlines()[-3:]
        require(tail == [
            "G0_DECISION=STOP",
            "PUBLIC_REQUALIFICATION_V5=FAIL",
            "DESKTOP_PACKAGE=NOT_READY",
        ], f"terminal state mismatch: {document}")


def require_supporting_bindings():
    for relative, digest in SUPPORTING_HASHES.items():
        require_file(ROOT / relative, digest, expected_mode=0o400)
    require_file(BINARY, EXPECTED_BINARY_SHA256)
    require_file(ROOT / "tests/data/science/alphaearth_rgb_cases.json",
                 EXPECTED_FIXTURE_SHA256)
    require_file(ROOT / "tests/CMakeLists.txt", EXPECTED_CMAKE_SHA256)

    preflight = (ROOT / ".superpowers/sdd/task-4-v5-one-shot-preflight.log").read_text(
        errors="replace")
    required_preflight = [
        "NETWORK_POLICY=SANDBOX_DENY_NETWORK_AND_LIVE_EXECUTABLE_NOT_INVOKED",
        f"HEAD={AUTHORIZATION_HEAD};CLEAN=YES;HEAD_TAGS=0",
        "V5_FUTURE_PATHS=ABSENT_NON_SYMLINK_3/3",
        "V5_AUTHORIZATION_FINAL_GATE=PASS",
        f"LIVE_BINARY_SHA256={EXPECTED_BINARY_SHA256};SIZE=29169152",
        "FORMAL_LISTING=1_TEST/5_ITERATIONS/PREFETCH/LATENCY_ENFORCED/FORMAL_V4",
        "V4_FROZEN=FILES_93/FILE_MODE_0400/ROOT_MODE_0500/"
        f"TREE_SHA256_{EXPECTED_V4_TREE_SHA256}",
        "V5_ONE_SHOT_PREFLIGHT=PASS",
    ]
    require(all(item in preflight for item in required_preflight),
            "preflight transcript semantic binding mismatch")

    wrapper = (ROOT / ".superpowers/sdd/task-4-v5-one-shot-candidate.sh").read_text()
    required_wrapper = [
        '"$binary" --live-cases tests/data/science/alphaearth_rgb_cases.json',
        '--iterations 5 --profile prefetch',
        '--evidence-dir "$candidate" --summary-json "$summary" --enforce-latency',
    ]
    require(all(item in wrapper for item in required_wrapper),
            "candidate wrapper command mismatch")

    transcript = (ROOT / ".superpowers/sdd/task-4-v5-one-shot-candidate.log").read_text(
        errors="replace")
    require(transcript.count("CANDIDATE_PROCESS_STARTED=") == 1 and
            transcript.count("CANDIDATE_PROCESS_FINISHED=") == 1 and
            transcript.count("CANDIDATE_PROCESS_EXIT=1") == 1,
            "candidate transcript process ledger mismatch")
    require("CANDIDATE_PROCESS_STARTED=2026-07-14T02:48:35+0800" in transcript and
            "CANDIDATE_PROCESS_FINISHED=2026-07-14T02:48:43+0800" in transcript,
            "candidate timestamps mismatch")
    require(transcript.count(
        "ScienceHttpRanges failure: HEAD request/response count mismatch (possible GET 200)") == 1,
        "candidate failure reason mismatch")
    timing = re.findall(
        r"ScienceGdalLive case=nvidia_hq fid=9790 year=2025 iteration=1 "
        r"milliseconds=([0-9.]+)", transcript)
    require(timing == ["3763.36"], f"completed timing mismatch: {timing}")
    return transcript


def require_directory(path, mode, label):
    require(path.is_dir() and not path.is_symlink(),
            f"{label} missing, not a directory, or symlinked")
    require(stat.S_IMODE(path.stat().st_mode) == mode,
            f"{label} mode mismatch")


def require_v5_structure(parent):
    require_directory(parent, 0o700, "v5 requalification parent")
    parent_entries = list(parent.iterdir())
    require({path.name for path in parent_entries} ==
            {"candidate", "candidate-summary.json"} and len(parent_entries) == 2,
            f"v5 parent exact entry set mismatch: {parent_entries}")

    candidate = parent / "candidate"
    summary = parent / "candidate-summary.json"
    require_directory(candidate, 0o500, "v5 candidate root")
    require(summary.is_file() and not summary.is_symlink() and
            stat.S_IMODE(summary.stat().st_mode) == 0o400,
            "v5 candidate summary type/mode mismatch")

    candidate_entries = list(candidate.iterdir())
    require({path.name for path in candidate_entries} == set(EVIDENCE_BINDINGS) and
            len(candidate_entries) == len(EVIDENCE_BINDINGS),
            f"v5 candidate exact entry set mismatch: {candidate_entries}")
    for path in candidate_entries:
        require(path.is_file() and not path.is_symlink() and
                stat.S_IMODE(path.stat().st_mode) == 0o400,
                f"v5 candidate entry type/mode mismatch: {path}")


def require_frozen_complete_manifest():
    require_v5_structure(V5_PARENT)
    require_file(SUMMARY, *SUMMARY_BINDING, expected_mode=0o400)
    for name, (digest, size) in EVIDENCE_BINDINGS.items():
        require_file(CANDIDATE / name, digest, size, 0o400)

    manifest_path = ROOT / ".superpowers/sdd/task-4-v5-candidate-manifest.sha256"
    manifest_lines = [line for line in manifest_path.read_text().splitlines()
                      if line and not line.startswith("#")]
    require(len(manifest_lines) == 6, "candidate manifest entry count mismatch")
    parsed = {}
    for line in manifest_lines:
        match = re.fullmatch(r"([0-9a-f]{64}) (0400) ([0-9]+) (.+)", line)
        require(match is not None, f"malformed candidate manifest line: {line}")
        digest, mode, size, relative = match.groups()
        parsed[relative] = (digest, int(size), int(mode, 8))
    expected = {
        str((CANDIDATE / name).relative_to(ROOT)): (digest, size, 0o400)
        for name, (digest, size) in EVIDENCE_BINDINGS.items()
    }
    expected[str(SUMMARY.relative_to(ROOT))] = (*SUMMARY_BINDING, 0o400)
    require(parsed == expected, "candidate manifest does not bind complete set")


def require_structure_mutation_self_tests():
    def expect_rejected(name, mutate):
        with tempfile.TemporaryDirectory(prefix="osgsol-v5-structure-") as temporary:
            copied = Path(temporary) / "requalification-evidence-v5"
            shutil.copytree(V5_PARENT, copied, symlinks=True)
            copied_candidate = copied / "candidate"
            os.chmod(copied_candidate, 0o700)
            mutate(copied_candidate)
            os.chmod(copied_candidate, 0o500)
            try:
                require_v5_structure(copied)
            except AssertionError:
                os.chmod(copied_candidate, 0o700)
                return
            os.chmod(copied_candidate, 0o700)
            raise AssertionError(f"structure mutation was accepted: {name}")

    expect_rejected(
        "dangling-symlink",
        lambda candidate: os.symlink("missing-target", candidate / "unexpected-link"))
    expect_rejected(
        "nested-directory",
        lambda candidate: (candidate / "unexpected-directory").mkdir())
    require(hasattr(os, "mkfifo"), "portable FIFO mutation is unavailable")
    expect_rejected(
        "fifo",
        lambda candidate: os.mkfifo(candidate / "unexpected-fifo", 0o600))
    expect_rejected(
        "unexpected-regular",
        lambda candidate: (candidate / "unexpected-regular").write_bytes(b"mutation"))


def close(actual, expected, tolerance=1.0e-9):
    return math.isclose(float(actual), float(expected), rel_tol=0.0,
                        abs_tol=tolerance)


def verify_only_complete_triplet():
    fixture_document = json.loads(
        (ROOT / "tests/data/science/alphaearth_rgb_cases.json").read_text())
    fixtures = {case["name"]: case for case in fixture_document["cases"]}
    require(set(fixtures) == {"nvidia_hq", "hong_kong"}, "fixture cases mismatch")
    expected_stems = [f"prefetch-{name}-{iteration}"
                      for name in ("nvidia_hq", "hong_kong")
                      for iteration in range(1, 6)]
    suffixes = {
        "raw": "-curl-cpl.log",
        "stats": "-network-stats.json",
        "proof": "-proof.json",
    }
    present = {
        kind: {stem for stem in expected_stems
               if (CANDIDATE / f"{stem}{suffix}").is_file()}
        for kind, suffix in suffixes.items()
    }
    require({kind: len(stems) for kind, stems in present.items()} ==
            {"raw": 2, "stats": 2, "proof": 1},
            f"partial artifact counts changed: {present}")
    complete = set.intersection(*present.values())
    require(complete == {"prefetch-nvidia_hq-1"},
            f"complete triplet set mismatch: {complete}")
    missing = {kind: 10 - len(stems) for kind, stems in present.items()}
    require(missing == {"raw": 8, "stats": 8, "proof": 9},
            f"expected-ten absence mismatch: {missing}")

    stem = "prefetch-nvidia_hq-1"
    raw = (CANDIDATE / f"{stem}-curl-cpl.log").read_text(errors="replace")
    stats_data = json.loads((CANDIDATE / f"{stem}-network-stats.json").read_text())
    proof = json.loads((CANDIDATE / f"{stem}-proof.json").read_text())
    fixture = fixtures["nvidia_hq"]
    meta = proof["metadata_prefetch"]

    require(meta == {
        "enabled": True,
        "head_request_count": 1,
        "range_request_count": 1,
        "range_start": 0,
        "range_end": 131071,
        "head_http_version": 2,
        "range_http_version": 2,
        "shared_connection": True,
        "requests_overlapped": True,
        "cache_published": True,
        "coordinator_retries": [],
        "fallback_reason": "",
    }, "completed triplet metadata-prefetch proof mismatch")
    require(proof["coordinator_transient_retry_count"] == 0 and
            proof["coordinator_transient_retry_bytes"] == 0 and
            proof["coordinator_transient_retry_codes"] == {} and
            proof["coordinator_transient_fallback_count"] == 0 and
            proof["coordinator_transient_fallback_bytes"] == 0 and
            proof["coordinator_transient_fallback_codes"] == {},
            "completed triplet coordinator/fallback mismatch")
    require(proof["actual_http_get_count"] ==
            proof["successful_http_get_count"] + proof["transient_retry_count"] == 8,
            "completed triplet GET reconciliation mismatch")
    require(proof["transient_retry_count"] == 1 and
            proof["transient_retry_codes"] == {"500": 1} and
            proof["immediate_transient_retry_count"] == 1 and
            proof["immediate_transient_retry_bytes"] == 17 and
            proof["immediate_transient_retry_codes"] == {"500": 1},
            "completed triplet immediate retry mismatch")
    immediate = proof["immediate_retries"]
    require(len(immediate) == 1 and immediate[0]["code"] == 500 and
            immediate[0]["attempt"] == 1 and immediate[0]["delay_ms"] == 100 and
            immediate[0]["bytes"] == 17 and immediate[0]["http_major"] == 2,
            "completed triplet immediate retry fields mismatch")
    require(proof["actual_http_head_count"] == proof["stats_head_count"] == 1 and
            proof["stats_get_operation_count"] == stats_data["methods"]["GET"]["count"] == 2 and
            stats_data["methods"]["HEAD"]["count"] == 1,
            "completed triplet request/stats mismatch")
    require(proof["actual_http_body_bytes"] ==
            stats_data["methods"]["GET"]["downloaded_bytes"] ==
            proof["successful_range_bytes"] == 5010811,
            "completed triplet body-byte mismatch")
    require(proof["declared_transient_bytes"] == 17 and
            proof["conservative_body_upper_bound_bytes"] == 5010828 and
            proof["transfer_budget_bytes"] == 16777216,
            "completed triplet transfer bound mismatch")
    intervals = proof["successful_byte_intervals"]
    require(len(intervals) == proof["successful_http_get_count"] == 7 and
            intervals[0] == [0, 131071] and
            sum(end - start + 1 for start, end in intervals) == 5010811 and
            all(0 <= start <= end < proof["source_size"] for start, end in intervals),
            "completed triplet interval mismatch")
    require(proof["source_crs"] == fixture["crs"] and
            proof["selected_overview_factor"] == 4 and
            proof["raw_window"]["size"] == 256 and
            close(proof["geotransform"][0], fixture["utm_bbox"][0]) and
            close(proof["geotransform"][3], fixture["utm_bbox"][1]) and
            all(close(actual, expected, 1.0e-10)
                for actual, expected in zip(proof["verified_wgs84_bbox"], fixture["bbox"])),
            "completed triplet science correctness mismatch")
    require(raw.count("ParallelHeadRange: started") == 1 and
            raw.count("ParallelHeadRange: published") == 1 and
            raw.count("ParallelHeadRange: file-property-published count=1") == 1 and
            raw.count("VSICURL: ReadMultiRange: immediate-retry ") == 1 and
            raw.count("ParallelHeadRange: transient-fallback ") == 0 and
            raw.count("ParallelHeadRange: fallback=") == 0,
            "completed triplet raw event mismatch")
    transport = re.findall(
        r"ParallelHeadRange: transport head-connection=(\d+) range-connection=(\d+) "
        r"head-http=(\d+) range-http=(\d+)", raw)
    require(len(transport) == 1 and transport[0][0] == transport[0][1] and
            transport[0][2:] == ("2", "2"),
            "completed triplet transport mismatch")

    incomplete_raw = (CANDIDATE / "prefetch-nvidia_hq-2-curl-cpl.log").read_text(
        errors="replace")
    incomplete_stats = json.loads(
        (CANDIDATE / "prefetch-nvidia_hq-2-network-stats.json").read_text())
    require(incomplete_raw.count("ParallelHeadRange: started") == 1 and
            incomplete_raw.count("ParallelHeadRange: fallback=head-invalid") == 1 and
            incomplete_raw.count("VSICURL: ReadMultiRange: immediate-retry ") == 2,
            "incomplete iteration failure events mismatch")
    require(incomplete_stats["methods"]["HEAD"]["count"] == 2 and
            incomplete_stats["methods"]["GET"]["count"] == 3 and
            incomplete_stats["methods"]["GET"]["downloaded_bytes"] == 5141883,
            "incomplete iteration stats mismatch")
    require(not (CANDIDATE / "prefetch-nvidia_hq-2-proof.json").exists(),
            "failed iteration unexpectedly has a proof")

    summary = json.loads(SUMMARY.read_text())
    require(summary == {
        "cases": [],
        "limits": {"median_ms": 3000, "p95_ms": 8000},
        "profile": "prefetch",
        "status": "ERROR",
    }, "candidate fail-closed summary mismatch")
    return missing


def require_frozen_v4_and_downstream_absence():
    build_parent = ROOT / "build/science_g0_prefetch"
    requalification = build_parent / "requalification-evidence-v4"
    prefetch = requalification / "prefetch"
    control = requalification / "control"
    prefetch_summary = requalification / "prefetch-summary.json"
    control_summary = requalification / "control-summary.json"
    formal = build_parent / "formal-evidence-v4"
    require_directory(build_parent, 0o755, "v4 build ancestry")
    require_directory(requalification, 0o500, "v4 requalification parent")
    require_directory(prefetch, 0o500, "v4 prefetch root")
    require_directory(control, 0o500, "v4 control root")
    require_directory(formal, 0o500, "v4 formal root")
    require(prefetch_summary.is_file() and not prefetch_summary.is_symlink() and
            stat.S_IMODE(prefetch_summary.stat().st_mode) == 0o400,
            "v4 prefetch summary type/mode mismatch")
    require(control_summary.is_file() and not control_summary.is_symlink() and
            stat.S_IMODE(control_summary.stat().st_mode) == 0o400,
            "v4 control summary type/mode mismatch")
    requalification_entries = list(requalification.iterdir())
    require({path.name for path in requalification_entries} == {
        "prefetch", "prefetch-summary.json", "control", "control-summary.json"
    } and len(requalification_entries) == 4,
            f"v4 requalification exact entry set mismatch: {requalification_entries}")

    def triplet_names(profile):
        return {
            f"{profile}-{case}-{iteration}-{suffix}"
            for case in ("nvidia_hq", "hong_kong")
            for iteration in range(1, 6)
            for suffix in ("curl-cpl.log", "network-stats.json", "proof.json")
        }

    expected_by_root = {
        prefetch: triplet_names("prefetch"),
        control: triplet_names("optimized"),
        formal: triplet_names("prefetch") | {"live-summary.json"},
    }
    files = [prefetch_summary, control_summary]
    for evidence_root, expected_names in expected_by_root.items():
        entries = list(evidence_root.iterdir())
        require({path.name for path in entries} == expected_names and
                len(entries) == len(expected_names),
                f"v4 root exact entry set mismatch: {evidence_root}: {entries}")
        for path in entries:
            require(path.is_file() and not path.is_symlink() and
                    stat.S_IMODE(path.stat().st_mode) == 0o400,
                    f"v4 flat entry type/mode mismatch: {path}")
        files.extend(entries)
    require(len(files) == 93 and len({path.resolve() for path in files}) == 93,
            "v4 complete flat file set mismatch")
    tree = hashlib.sha256()
    for path in sorted(files, key=lambda value: value.relative_to(ROOT).as_posix()):
        require(not path.is_symlink() and stat.S_IMODE(path.stat().st_mode) == 0o400,
                f"v4 file drift: {path}")
        relative = path.relative_to(ROOT).as_posix().encode()
        digest = hashlib.sha256(path.read_bytes()).hexdigest().encode()
        tree.update(relative + b"\0" + b"0400" + b"\0" + digest + b"\n")
    require(tree.hexdigest() == EXPECTED_V4_TREE_SHA256, "v4 tree hash mismatch")

    require(not FORMAL.exists() and not FORMAL.is_symlink(),
            "formal v5 exists despite candidate failure")
    listing = subprocess.run(
        ["ctest", "--test-dir", "build/science_g0_prefetch", "-N", "-V",
         "-R", "^osgVerse_Test_ScienceGdalLive$"],
        cwd=ROOT, text=True, capture_output=True, check=False,
        env={key: value for key, value in os.environ.items()
             if key not in {"HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY",
                            "http_proxy", "https_proxy", "all_proxy"}})
    require(listing.returncode == 0 and listing.stdout.count("Test command:") == 1 and
            "Total Tests: 1" in listing.stdout and
            '"--iterations" "5"' in listing.stdout and
            '"--profile" "prefetch"' in listing.stdout and
            '"--enforce-latency"' in listing.stdout and
            "/formal-evidence-v4" in listing.stdout and
            "/formal-evidence-v5" not in listing.stdout,
            "formal listing changed despite candidate failure")


def require_desktop_tuple():
    sys.path.insert(0, str(ROOT))
    from tests.science_bundle_audit_tests import ScienceProbeBuilderTests, MANIFEST
    desktop = Path("/Users/USER/Desktop/osgSol Earth.app")
    require(desktop.is_dir() and not desktop.is_symlink(), "Desktop app missing")
    entries = list(desktop.rglob("*"))
    regular = [path for path in entries if path.is_file() and not path.is_symlink()]
    actual = (
        MANIFEST.bundle_fingerprint(desktop),
        ScienceProbeBuilderTests().tree_digest(desktop),
        len(entries), len(regular),
    )
    require(actual == (
        "91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18",
        "14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214",
        414, 355,
    ), f"Desktop protected tuple mismatch: {actual}")


def main():
    arguments = sys.argv[1:]
    require(arguments in ([], ["--precommit"]), "usage: auditor [--precommit]")
    require_decision_scope(arguments == ["--precommit"])
    require_supporting_bindings()
    require_frozen_complete_manifest()
    require_structure_mutation_self_tests()
    missing = verify_only_complete_triplet()
    require_frozen_v4_and_downstream_absence()
    require_desktop_tuple()
    require(not list(ROOT.rglob("__pycache__")), "__pycache__ directory found")

    print(f"AUTHORIZATION_HEAD={AUTHORIZATION_HEAD};REVIEW_FIX_PARENT={REVIEW_FIX_PARENT};DECISION_SCOPE=2_DOCS")
    print("ONE_SHOT_TRANSCRIPT=START_1/FINISH_1/EXIT_1;ABSOLUTE_NEGATIVE_HISTORY=NOT_PROVABLE")
    print("CANDIDATE_FROZEN=FILES_6/0400/ROOT_0500/MANIFEST_PASS")
    print("STRUCTURE_MUTATIONS=DANGLING_SYMLINK,NESTED_DIR,FIFO,UNEXPECTED_REGULAR=4/4_REJECTED")
    print("EXPECTED_10=RAW_2_MISSING_8/STATS_2_MISSING_8/PROOF_1_MISSING_9/COMPLETE_1")
    print("COMPLETE_TRIPLET=REQUEST_BODY_STATS_SCIENCE_RETRY_PASS;ITERATION1_MS=3763.36")
    print("INCOMPLETE_ITERATION=HEAD_INVALID_FALLBACK/HEAD_2/IMMEDIATE_RETRY_2/NO_PROOF")
    print("CANDIDATE_SUMMARY=ERROR/CASES_0;LATENCY_AGGREGATES=UNAVAILABLE")
    print("FORMAL_V5=ABSENT_NON_SYMLINK;FORMAL_PROCESS=NOT_AUTHORIZED")
    print(f"V4_FROZEN=FILES_93/TREE_{EXPECTED_V4_TREE_SHA256}")
    print("DESKTOP=91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18/14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214/414/355")
    print("G0_DECISION=STOP;PUBLIC_REQUALIFICATION_V5=FAIL;DESKTOP_PACKAGE=NOT_READY")
    print("V5_DECISION_AUDIT=PASS")


if __name__ == "__main__":
    main()
```
<!-- V5_DECISION_AUDITOR_END -->

G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V5=FAIL
DESKTOP_PACKAGE=NOT_READY
