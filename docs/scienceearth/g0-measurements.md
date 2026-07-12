# ScienceEarth G0 Task 5 measurements

Date: 2026-07-12 (CST)

Status: **STOP at the Task 5 measurement gate.** Offline correctness, bounded transfer, retry
accounting, and the `<= 8 s` P95 target pass. Both live medians exceed the `<= 3 s` target. This
report does not set the repository-wide decision; it records evidence that Task 6 must not
interpret as a passing gate.

## Reference setup

- Machine: Apple M4 Pro, arm64, 48 GiB RAM
- OS: macOS 26.5.2 (25F84)
- Compiler: AppleClang 21.0.0.21000101, Release
- GDAL / PROJ / ZSTD: private static prefix, GDAL 3.13.1 / PROJ 9.8.1 / ZSTD 1.5.7
- Replacement formal live run: 2026-07-12, immediately before 04:47 CST
- Network: default route `en1`; macOS did not disclose the SSID. A post-run `networkQuality -c`
  sample reported a proxied/loopback path, 507,227,520 bit/s download throughput, and 279.24 ms
  base RTT. It is context only and was not used to normalize the live timings.

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

## Formal live AlphaEarth evidence

The optional live test is registered only with `OSGSOL_SCIENCE_NETWORK_TESTS=ON`. Each iteration
clears the VSICurl cache, opens one pinned COG, validates the 8192x8192x64 signed Int8 source and
the pinned raw CRS authority/code. It derives all six affine coefficients from the pinned UTM
bounds, transforms the pinned longitude/latitude from EPSG:4326 with traditional GIS axis order,
inverts the affine transform to obtain the source pixel, and derives the window from that pixel.
The densified raster footprint is transformed back to WGS84, clipped to the CRS area of use, and
compared with the pinned index bbox to `1e-9` degrees. The test then selects the exact 4x source
overview for A01/A16/A09 and normalizes pixels and masks to top-down order in memory. The offline
spike owns the VRT flip proof; live timing makes no remote warped-VRT read.

The per-iteration conservative body upper-bound budget is 16,777,216 bytes. Every final formal
response was one metadata HEAD 200, proxy CONNECT 200 where applicable, or a matching single-range
206. No GET 200, comma range, complete-object response, transient retry, partial response, or
budget violation occurred.

| Case | Iterations (ms) | Median | P95 | Successful / actual / declared / upper bytes each | GET / HEAD | Retries | Source size |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| NVIDIA HQ, fid 9790, 2025 | 4564.72, 4232.69, 4698.79, 3976.85, 3673.84 | 4232.69 ms | 4698.79 ms | 4,912,507 / 4,912,507 / 0 / 4,912,507 | 8 / 1 | 0 | 3,700,007,174 |
| Hong Kong, fid 181593, 2025 | 4171.96, 3529.54, 3563.42, 3769.26, 3687.69 | 3687.69 ms | 4171.96 ms | 1,212,416 / 1,212,416 / 0 / 1,212,416 | 5 / 1 | 0 | 2,352,783,157 |

NVIDIA transferred 24,562,535 actual body bytes across five iterations; Hong Kong transferred
6,062,080. Both declared transient totals were zero, so the conservative totals are identical.
NVIDIA used EPSG:32610, affine `[581920,10,0,4096000,0,10]`, raw pixel
`(989.08025,4050.08957)`, and top-down window `(x=860,y=4012,size=256)` (raw y=3924). Hong Kong
used EPSG:32650, affine `[172320,10,0,2457600,0,10]`, raw pixel
`(3607.66802,1330.67090)`, and top-down window `(x=3476,y=6732,size=256)` (raw y=1204). Fresh raw
curl/CPL logs, GDAL network-stat JSON, and parsed proofs for all ten iterations are under
`build/science_g0/science-network-evidence/`.

The 5x2 measurements recorded in commit `58f4932` are superseded: those windows were normalized
from the WGS84 bbox instead of derived through the actual COG CRS and affine transform. Their
timings are not used here.

Development runs also exposed intermittent source HTTP 500 responses. The parser neither erases
nor counts their bodies as successful range bytes; it records retry code/count/body bytes and
requires bounded recovery. Header-only transient lengths are reported as declared bytes and only
enter the conservative upper bound. The replacement formal run required zero retries.

Both P95 values pass `<= 8 s`. Both medians fail `<= 3 s`; therefore the Task 5 gate outcome is
**STOP**. The current COGs are also 2.35-3.70 GB, materially larger than the historical 270 MB
sample, so later planning must use these measured fixture sizes.

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

The accepted profile is `scienceearth-g0-source-root-normalization` version 1 with
`source_root_count=2`. Both normalized generation-command roots are placeholders. Reference and
initial ratchet each contain 1,086 identities; neither manifest contains `/Users/USER` or
`//Users/USER`.

| Accepted manifest | Canonical SHA-256 | File SHA-256 |
|---|---|---|
| Reference | `da541f190e0679b3e3b67234b1d100d7d20caa7aa3eccbaa0177040930b83f4d` | `d08bb3e27b240a6d371e8941b01e9d41eea09b91ee9ea8bb6a6607b744b41a81` |
| Ratchet | `439dadb3ba459785c573ef3ca09401e9e1a6e429a8b6a96e869998318af08c41` | `5ab42ff0ceeb361732c98f15d07354d0e8cb7ae04e0d2b83f35e7c821c869a22` |

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

Fresh final verification passed the combined audit/manifest unit suites 51/51 in 1.83 seconds, the
private dependency builder contract, the release boundary/manifest contract, the pinned private
prefix verifier, and deep strict app signature verification. The protected science-off selection
passed 15/15 in 10.16 seconds. These isolation passes do not override the independent corrected
median-latency hard gate.

G0 remains `STOP`. Delta isolation is no longer a blocker; only uncached first-RGB median latency
remains failed, and human product sign-off is still pending. G1 has not started.
