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

`osgdb_science_g0_probe.so` is a test-only, non-installed Mach-O arm64 bundle. It is 21,283,608
bytes with SHA-256 `835bc36a6f395d285e9a7f5d494c9d15dd56509f760728b26da24090d15011ad`.
It links the private `libgdal.a`, `libproj.a`, and `libzstd.a` plus SDK curl and SQLite.

`nm -gU` exposes exactly `_osgsol_science_g0_probe_anchor`. `otool -L` lists only system curl,
System, SQLite, and libc++; there is no `LC_RPATH` and no install rule. The link command inherits
an unused `-L/opt/homebrew/lib` from the wider project, but no Homebrew library or RPATH enters the
bundle. Static GDAL source/build strings remain a Task 6 bundle-audit concern.

## Task 6 recursive bundle audit

The disposable `build/science_g0/osgSol Science G0 Probe.app` was rebuilt from the protected
Desktop baseline, replacing only the main executable and adding the test-only probe as
`Contents/lib/osgPlugins-3.6.5/osgdb_science.so`. It was ad-hoc signed and passed
`codesign --verify --deep --strict`. The protected app was not changed: its directory mtime stayed
`1783759487`, its main executable SHA-256 stayed
`e79ce979af31227c39137e1caa75aced48ed297bcc1927716284d9f273090fde`, and its
`CodeResources` SHA-256 stayed
`14858b06c343ef2d98ae2c27c118b695c8357af893324b8bb233dd6108a9940f`.

The recursive audit visited all 128 Mach-O executables, dylibs, and plugins. Its JSON and readable
graph are `build/science_g0/bundle-audit.json` and `build/science_g0/bundle-audit.txt`.

| Measurement | Result |
|---|---:|
| Protected baseline size | 542,594,200 bytes (517.46 MiB) |
| Disposable probe size | 563,877,967 bytes (537.76 MiB) |
| Added size | 21,283,767 bytes (20.30 MiB) |
| Science-only closure | 21,283,608 bytes (20.30 MiB), probe plugin only |
| Science dependencies reachable from main | 0 |
| Unresolved dependencies | 34 |
| Forbidden-prefix references | 36 |
| Source/build references | 252 |

The size and main-link isolation limits pass: both added size and science closure are below the
40 MiB target, and GDAL/PROJ/ZSTD remain reachable only below the probe plugin. System isolation
fails. A separate recursive audit of the unchanged baseline attributes all 34 unresolved
Homebrew dependencies, all 36 forbidden-prefix references, and 178 source/build references to the
pre-existing app closure. The science probe adds 74 source/build strings from its static GDAL
closure, including compiled-in data-prefix and source filenames. The candidate therefore has 322
grouped violations in total; unresolved entries are reported separately from the corresponding
forbidden runtime references.

Fresh gate verification passed the bundle-audit unit suite 10/10, the private dependency hashes and
static-prefix verifier, Task 5 offline tests 3/3, and the science-off targeted regressions 15/15.
These passes do not override the failed system-isolation or corrected median-latency hard gates.
