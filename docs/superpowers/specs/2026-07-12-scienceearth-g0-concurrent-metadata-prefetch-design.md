# ScienceEarth G0 Concurrent Metadata Prefetch Design

**Date:** 2026-07-12

**Status:** Approved for an isolated prototype; G0 remains `STOP`

**Protected boundary:** `ScienceEarth` / osgSol Earth `v0.2.0`

## 1. Decision

ScienceEarth will prototype one path-specific change in its private GDAL 3.13.1 runtime: the
mandatory HTTP `HEAD` request and the mandatory first `Range: bytes=0-131071` request may be
scheduled together on the same libcurl multi handle. The Range transfer uses
`CURLOPT_PIPEWAIT=1L`, so libcurl waits for the cold connection to confirm HTTP/2 multiplexing
instead of opening a second speculative connection.

The prototype does not remove, replace, or warm either request. It only overlaps their wait time.
It is not a G1 feature and is not enabled for the existing app, terrain, 3D Tiles, photo, media,
or non-AlphaEarth `/vsicurl/` paths.

## 2. Evidence and root cause

The formal optimized NVIDIA run failed by `58.162792 ms`. The median-defining iteration was:

- open: `1899.930042 ms`;
- georeference: `0.926000 ms`;
- read: `1157.117750 ms`;
- total: `3058.162792 ms`.

Every optimized open has exactly one cold proxy/TLS/ALPN connection, one `HEAD`, and one exact
128 KiB Range GET. There is no extra directory, sidecar, or metadata request to delete. The first
Range reuses the connection created by HEAD, but it starts only after HEAD completes. The source
server's HEAD and metadata processing accounts for only a minority of open time; cold proxy and
transport setup dominate.

Configuration-only alternatives do not solve the gap:

- `allowedDrivers={"GTiff"}` cannot remove a request because GTiff is already the first successful
  driver among the only three registered drivers;
- `GEOREF_SOURCES=INTERNAL` affects the approximately 1 ms georeference phase, while
  `GDAL_DISABLE_READDIR_ON_OPEN=EMPTY_DIR` already prevents sidecar network probes;
- `OVERVIEW_LEVEL=1` wraps a fully opened GTiff dataset, changes the public dimensions from
  8192x8192 to 2048x2048, and moves overview work without removing HEAD or the first Range;
- `CPL_VSIL_CURL_USE_HEAD=NO`, warm connection reuse, `GDAL_INGESTED_BYTES_AT_OPEN`, and an
  unbounded GET remain forbidden.

## 3. Isolation boundary

The change is delivered as a pinned patch:

`packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`

`packaging/science_deps/build_science_deps.sh` applies the patch only while building the private
ScienceEarth GDAL archive. The protected Desktop bundle and the normal osgVerse/osgSol GDAL,
TIFF, terrain, and reader/writer paths do not consume the patched archive.

The feature is off by default. A caller must set the path-specific option
`OSGSOL_VSICURL_PREFETCH_HEAD_RANGE=YES` for the exact `/vsicurl/` AlphaEarth object path before
opening it. A global environment variable alone is not sufficient to enable the behavior. The
option is cleared through RAII after the dataset closes.

The build patch and its SHA-256 become part of private dependency verification. A missing,
changed, already-applied, or non-cleanly-applicable patch stops the build. The bundle audit must
continue reporting Tier A `PASS`, Tier B new/removed `0/0`, and only the approved science anchor
as an exported symbol.

## 4. Request coordinator

### 4.1 Eligibility

Parallel prefetch is attempted only when all conditions are true:

1. the filename uses `/vsicurl/http://` or `/vsicurl/https://`;
2. `CPL_VSIL_CURL_USE_HEAD=YES` remains effective;
3. `OSGSOL_VSICURL_PREFETCH_HEAD_RANGE=YES` is set through
   `VSISetPathSpecificOption()` for that filename;
4. the file property and byte-region caches are cold for that URL;
5. the configured `/vsicurl/` chunk size is exactly `131072`;
6. the operation is ordinary read-only file-size discovery, not header-only metadata retrieval,
   signed-URL method fallback, streaming I/O, or a retry of a previous request.

Any ineligible request follows the unchanged upstream GDAL path.

### 4.2 Scheduling

The coordinator creates two easy handles and adds both to the existing filesystem multi handle:

- HEAD handle: the existing HEAD configuration, headers, redirects, retry policy, and response
  parsing remain authoritative;
- Range handle: `GET` with exactly `Range: bytes=0-131071`, the same URL/auth/header policy, a
  body cap of 131072 bytes, and `CURLOPT_PIPEWAIT=1L`.

`CURLMOPT_PIPELINING=CURLPIPE_MULTIPLEX` remains enabled. Both easy handles therefore use the
multi handle's shared DNS, connection, TLS-session, and connection caches. No second private
multi handle or warm-up request is created.

The coordinator drives both handles to completion and identifies results by easy-handle pointer.
It removes both handles before cleanup on every exit path. Network statistics record exactly one
HEAD and one GET; cleanup does not discard the multi handle's connection because later RGB ranges
must use the same normal connection pool.

### 4.3 Validation and cache publication

The first Range is published to the `/vsicurl/` region cache only when all checks pass:

- HEAD returns HTTP 200 and supplies a positive object size;
- GET returns HTTP 206;
- the returned interval is exactly `[0, 131071]`;
- the received body is exactly 131072 bytes;
- `Content-Range` reports the same total object size as HEAD;
- neither request exceeded the existing retry limit or produced a truncated body;
- no redirect changed method/header safety.

Publication uses the existing `AddRegion()` path in 128 KiB chunk units. File properties are
published only from the validated HEAD result. The GTiff opener then reads byte zero from the
region cache, so it must not emit a duplicate metadata GET.

## 5. Failure behavior

Runtime availability and formal evidence use different outcomes without weakening correctness:

- HEAD failure: preserve the existing HEAD retry/failure behavior; prefetched bytes are discarded;
- Range timeout, retryable status, or missing body with a valid HEAD: discard the prefetched
  buffer and continue through the unchanged normal ranged-read path;
- GET 200, comma Range, oversized body, malformed `Content-Range`, or HEAD/Range size mismatch:
  abort the prefetched transfer, discard all bytes, invalidate that URL's cached file properties
  and regions, and fail the open;
- HTTP/1.1 or lack of multiplexing: correctness may fall back, but the candidate cannot be
  promoted because it has not proved the intended single-connection overlap;
- any fallback, retry, second metadata GET, or protocol mismatch in a formal G0 iteration is a
  hard evidence failure even if the runtime open eventually succeeds.

The body callback must stop at 131072 bytes. A server that ignores Range can therefore never cause
the complete 2.35-3.70 GB COG to download.

## 6. Evidence schema

Each parsed proof gains a `metadata_prefetch` object with:

- `enabled`;
- `head_request_count`;
- `range_request_count`;
- `range_start` and `range_end`;
- `head_http_version` and `range_http_version`;
- `shared_connection`;
- `requests_overlapped`;
- `cache_published`;
- `fallback_reason`, empty on success.

`shared_connection` and `requests_overlapped` are derived from timestamped curl debug events, not
from wall-clock improvement. The Range request headers must be emitted before the HEAD transfer
completion, and both transfers must report HTTP/2 on the same connection identity. The existing
proof fields, phase timings, raw curl/CPL log, VSINetworkStats JSON, byte reconciliation, retry
map, and response codes remain mandatory.

## 7. Test strategy

### 7.1 Patch and state-machine tests

The dependency script test proves the patch is pinned, checksummed, applied once, and rejected on
drift. A small deterministic coordinator seam drives success and failure completion orderings
without network mocks leaking into production behavior. Tests cover:

- HEAD-first and Range-first completion;
- Range failure followed by ordinary read fallback;
- HEAD failure;
- HTTP 200 for Range;
- short and oversized bodies;
- malformed and mismatched `Content-Range`;
- retry exhaustion;
- cleanup of both easy handles on every path;
- exactly-once cache and file-property publication.

Every behavior is introduced with a failing test before production patch code.

### 7.2 Local HTTP contract

The existing strict local server remains the byte-safety oracle. It must still prove:

- every GET has one Range header and receives 206;
- the first interval is exactly `0-131071`;
- no GET 200 or complete-object response is accepted;
- total conservative bytes stay at or below 16 MiB;
- the local RGB output, mask, orientation, dequantization, georeference, and overview selection
  remain bit-for-bit equal to the existing oracle.

The HTTP/1.1 local server does not claim HTTP/2 multiplex performance. That property is verified
separately by real curl evidence.

### 7.3 Isolated public diagnostic

Before promotion, a separate diagnostic build directory and evidence directory run one current
optimized control process and one prefetch candidate process. Each process performs five cold
data plus cold connection iterations for NVIDIA HQ and Hong Kong. It never writes into the
existing formal evidence directory.

The candidate is rejected unless all ten iterations prove:

- one HEAD and one exact first Range, both on the intended HTTP/2 connection;
- header emission overlap and no duplicate metadata GET;
- no retries, fallback, GET 200, comma Range, or full COG;
- exact image/georeference correctness and per-iteration bytes at or below 16 MiB;
- NVIDIA and Hong Kong median at or below 3000 ms and P95 at or below 8000 ms.

Passing diagnostic evidence allows a fresh formal optimized run; it does not rewrite or delete
the previous STOP evidence.

## 8. Promotion and rollback

If the diagnostic fails any correctness, transport, isolation, or latency gate, the path-specific
option remains disabled, the prototype is recorded as rejected, and G0 stays `STOP`. No threshold,
fixture, iteration count, coordinate, cache rule, or timing boundary changes.

If the diagnostic passes, the optimized formal profile enables the path-specific option and runs
the same independent-process five-iteration gate. Only a new formal `GO` result plus all existing
G0 gates and explicit human product sign-off can unblock G1.

Rollback consists of disabling the path-specific option and rebuilding the private archive
without promoting the patch. Because the normal app never links the private archive, rollback
does not replace or migrate existing product caches or user data.

## 9. Alternatives rejected

### 9.1 Rerun the unchanged profile until it passes

Two of five NVIDIA samples already fall below 3 seconds. Repeating until a favorable median
appears would measure network luck, not an implementation improvement, and is rejected.

### 9.2 Reuse a warm connection between iterations

`VSICurlPartialClearCache()` could retain the connection pool while clearing object data. That
changes the approved cold-connection gate and is rejected.

### 9.3 Remove HEAD or replace it with a limited GET

The preceding approved plan explicitly requires HEAD. Removing it changes the safety contract and
is rejected even if it saves one round trip.

### 9.4 Expand into RGB range merging or multithreaded decode

Those may reduce the variable read phase, but they are a separate product decision. This design
remains limited to initial dataset-open metadata transport.

