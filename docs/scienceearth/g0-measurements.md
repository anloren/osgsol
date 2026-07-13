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
