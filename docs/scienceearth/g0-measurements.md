# ScienceEarth G0 Task 5 measurements

Date: 2026-07-12 (CST)

Status: **STOP at the Task 5 measurement gate.** Offline correctness, bounded transfer, retry
accounting, and the `<= 8 s` P95 target pass. Both live medians exceed the `<= 3 s` target. This
report does not set the repository-wide `G0_DECISION`; it records evidence that Task 6 must not
interpret as GO.

## Reference setup

- Machine: Apple M4 Pro, arm64, 48 GiB RAM
- OS: macOS 26.5.2 (25F84)
- Compiler: AppleClang 21.0.0.21000101, Release
- GDAL / PROJ / ZSTD: private static prefix, GDAL 3.13.1 / PROJ 9.8.1 / ZSTD 1.5.7
- Formal live run: 2026-07-12, immediately before 04:18 CST
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
  planned/actual/committed bytes, partial writes, and violations.
- The curl/CPL parser accepts only retry codes 429/500/502/503/504, binds retries to the exact CPL
  range record, requires the same range to be emitted again and end in matching HTTP 206, and
  permits at most three retry events per range. Regression fixtures cover `500 -> same Range ->
  206`, an unlisted status, more than three retries, and out-of-order HTTP/2 multiplex responses.
- `successful_range_bytes` must equal GDAL `VSINetworkStats`. `actual_http_body_bytes` additionally
  includes transient response bodies and is the value used for transfer budgets.

Fresh offline Task 5 CTest result: 2/2 passed. The science-off AEF index, dependency-script, and
build-contract regressions pass 3/3. The two pinned raw
index rows and SHA-256 fingerprints validate without network access.

## Formal live AlphaEarth evidence

The optional live test is registered only with `OSGSOL_SCIENCE_NETWORK_TESTS=ON`. Each iteration
clears the VSICurl cache, opens one pinned COG, validates the 8192x8192x64 signed Int8 source and
positive-y raw geotransform, selects the exact 4x source overview for A01/A16/A09, reads a
mirrored 256x256 raw bbox as an exact 64x64 overview window, and normalizes pixels and masks to
top-down order in memory. The offline spike owns the VRT flip proof; the live test validates the
pinned `vrt_strategy` but does not make a remote warped-VRT read.

The per-iteration actual HTTP body budget is 16,777,216 bytes. Every final formal response was one
metadata HEAD 200, proxy CONNECT 200 where applicable, or a matching single-range 206. No GET 200,
comma range, complete-object response, transient retry, partial response, or budget violation
occurred.

| Case | Iterations (ms) | Median | P95 | Successful / actual bytes each | GET / HEAD each | Retries | Source size |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| NVIDIA HQ, fid 9790, 2025 | 4489.09, 4157.50, 3927.02, 4181.44, 4839.01 | 4181.44 ms | 4839.01 ms | 4,912,507 / 4,912,507 | 8 / 1 | 0 | 3,700,007,174 |
| Hong Kong, fid 181593, 2025 | 4091.80, 3993.56, 4293.56, 3483.24, 3798.52 | 3993.56 ms | 4293.56 ms | 1,212,416 / 1,212,416 | 5 / 1 | 0 | 2,352,783,157 |

NVIDIA transferred 24,562,535 actual body bytes across five iterations; Hong Kong transferred
6,062,080. The exact 4x raw windows were `(x=884, y=3972, size=256)` and
`(x=2124, y=1220, size=256)`, respectively. Fresh raw curl/CPL logs, GDAL network-stat JSON, and
parsed proofs for all ten iterations are under
`build/science_g0/science-network-evidence/`.

Development runs also exposed intermittent source HTTP 500 responses. The parser neither erases
nor counts their bodies as successful range bytes; it records retry code/count/body bytes and
requires bounded recovery. The final formal run happened to require zero retries.

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
