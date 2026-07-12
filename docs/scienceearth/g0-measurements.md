# ScienceEarth G0 Task 5 measurements

Date: 2026-07-12 (CST)

Status: **STOP at the Task 5 formal optimized latency gate.** The baseline and optimized profiles
ran as separate fresh processes. Offline correctness, bounded transfer, retry accounting, both
`<= 8 s` P95 targets, and the Hong Kong `<= 3 s` median pass. The optimized NVIDIA HQ median is
`3058.162792 ms`, which exceeds the hard limit by `58.162792 ms`. The plan therefore stopped before
the full regression, policy-audit, signature, and protected science-off reruns. This is not G0
readiness and must not be interpreted as permission to start G1.

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
| Reference | `0c6bb7949ac789f3c24e86b0862570ebd2997e1530d2499d9caf29a1461064d4` | `6ae2c1a946bd7bcb4eecd386109dbbcab856efd2803d7815c882c6b91f200f9f` |
| Ratchet | `6a0298e26c9b32f5224db68770448dd213855b88df613c0891de4e3f77429967` | `dac1bbf6a0e4a2ae3831904a60174a9bbcb980614c528eeeae295de48cc97dfd` |

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
fingerprint before and after. The independent file-tree digest was identical before and after:
`0926eff5871c9e8313715c08349293e74db4aef9b9d9cde224743e831b605a6e`.

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
