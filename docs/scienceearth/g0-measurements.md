# ScienceEarth G0 Task 5 measurements

Date: 2026-07-12 (CST)

Status: Task 5 offline correctness and bounded-range tests pass. The optional live test passes
with bounded retries, but both live medians exceed the G0 `<= 3 s` target. This document does not
make the Task 6 go/no-go decision; `G0_DECISION` remains unset.

## Reference setup

- Machine: Mac16,11, Apple M4 Pro, arm64, 48 GiB RAM
- OS: macOS 26.5.2 (25F84)
- Compiler: AppleClang 21.0.0.21000101, Release
- GDAL / PROJ / ZSTD: private static prefix, GDAL 3.13.1 / PROJ 9.8.1 / ZSTD 1.5.7
- Network: active Wi-Fi interface `en1`; SSID was unavailable from `networksetup`
- A post-run `networkQuality -c` sample reported 489,964,832 bit/s download throughput and
  277.34 ms base RTT. It ran after the live COG measurements and is context, not a normalization
  applied to their timings.

## TDD evidence

The first offline spike build compiled and then failed at link time before GDAL linkage was added.
The unresolved symbols included `GDALRegister_GTiff`, `GDALRegister_VRT`, `GDALRegister_MEM`,
`GDALOpenEx`, and `GDALRasterBand::RasterIO`. This is the required linkage-specific RED.

The first local range execution compiled and linked while the server was intentionally absent. It
failed with Python `Errno 2` for `tests/science_http_range_server.py`, followed by
`instrumented range server did not publish a port`. This is the separate missing-server RED.

After the minimum private-prefix linkage and server implementation:

- `osgVerse_Test_ScienceGdalSpike`: pass; 64-band signed Int8, tiled ZSTD GTiff, 2x/4x
  overviews, manual GTiff/VRT/MEM registration, A01/A16/A09 order, exact signed
  dequantization, NoData mask versus valid zero, trusted vertical flip, and a bounded 4x-overview
  window are verified.
- `osgVerse_Test_ScienceHttpRanges`: pass; the local fixture was 4,876,194 bytes, the explicit
  budget was 1,048,576 bytes, and five single-range HTTP 206 GETs transferred 76,161 bytes total.
  The metadata HEAD returned HTTP 200 with zero body bytes. No GET lacked `Range`, used a comma
  multi-range, returned a full HTTP 200 body, or exceeded the budget.

Fresh offline CTest result: 2/2 passed in 1.45 seconds.

## Optional live AlphaEarth measurements

The live test is registered only with `OSGSOL_SCIENCE_NETWORK_TESTS=ON`. It uses the two pinned
case URLs in `tests/data/science/alphaearth_rgb_cases.json`; it does not download the global index
or a complete COG. Each iteration clears the VSICurl cache, opens the known COG, verifies
8192x8192x64 signed Int8 and A01/A16/A09, and reads a 256x256 source window into 64x64 RGB.

GDAL settings are:

```text
GDAL_HTTP_MULTIRANGE=YES
GDAL_HTTP_MERGE_CONSECUTIVE_RANGES=YES
CPL_VSIL_CURL_ALLOWED_EXTENSIONS=.tif,.tiff,.vrt
GDAL_HTTP_MAX_RETRY=3
GDAL_HTTP_RETRY_DELAY=0.1
GDAL_HTTP_RETRY_CODES=429,500,502,503,504
```

`GDAL_HTTP_MULTIRANGE=SINGLE_GET` is never used.

| Case | Selected tile | Iterations (ms) | Median | P95 | Requested bytes / iteration | Source size | Codes | Full object |
| --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- |
| NVIDIA HQ | fid 9790, 2025 | 3710.74, 3710.48, 3678.85, 3988.34, 3560.26 | 3710.48 ms | 3988.34 ms | 9,422,044 (8.99 MiB) | 3,700,007,174 | 200 HEAD, 206 ranges | no |
| Hong Kong | fid 181593, 2025 | 3853.60, 3456.00, 3369.64, 3063.99, 3289.26 | 3369.64 ms | 3853.60 ms | 5,716,979 (5.45 MiB) | 2,352,783,157 | 200 HEAD, 206 ranges | no |

The NVIDIA total requested bytes across five iterations were 47,110,220; Hong Kong used
28,584,895. Both P95 values meet the `<= 8 s` target. Both medians miss the `<= 3 s` target and
must remain visible to Task 6.

Before bounded retries were enabled, two live attempts exposed an intermittent source failure for
NVIDIA range `117442209-120770886`: one failed immediately with HTTP 500; the next completed two
iterations and then received the same HTTP 500. The final five-iteration evidence completed with
200/206 after the bounded retry policy. The transient 500 is a source-reliability concern, not
erased by the passing retry run.

The two current COGs are 3.70 GB and 2.35 GB, materially larger than the historical 270 MB sample
mentioned in the G0 baseline. The bounded-window invariant held, but Task 6 should use these
measured current fixture sizes rather than assuming 270 MB.

## Test-only probe closure

`osgdb_science_g0_probe.so` is a test-only, non-installed Mach-O arm64 bundle. It links the private
`libgdal.a`, `libproj.a`, and `libzstd.a` plus SDK curl and SQLite with hidden visibility and
dead-strip. It is 21,283,608 bytes (20.30 MiB), SHA-256
`835bc36a6f395d285e9a7f5d494c9d15dd56509f760728b26da24090d15011ad`.

`nm -gU` exposes exactly one non-product marker,
`_osgsol_science_g0_probe_anchor`. `otool -L` contains only `/usr/lib/libcurl.4.dylib`,
`/usr/lib/libSystem.B.dylib`, `/usr/lib/libsqlite3.dylib`, and `/usr/lib/libc++.1.dylib`.
Target-local RPATH suppression leaves no `LC_RPATH` commands in the probe.

Concern for Task 6: the statically linked GDAL objects still contain 74 build/source/prefix string
occurrences. The linked probe contains no `/opt/homebrew` or `/usr/local` string, but source-tree
strings require the dedicated Task 6 bundle audit before any G0 decision. The generated link
command also inherits an unused `-L/opt/homebrew/lib` search flag from the wider project even
though no Homebrew library or RPATH appears in the resulting bundle.
