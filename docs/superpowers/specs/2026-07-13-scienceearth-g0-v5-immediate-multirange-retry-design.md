# ScienceEarth G0 v5 Immediate Multi-Range Retry Design

## Status and authority

This design continues the active ScienceEarth G0 goal after the authoritative v4 `STOP`. The user
selected option 1 and instructed the work to continue until a human-verifiable Desktop app exists.
That authorizes the bounded implementation and qualification workflow below. It does not authorize
a tag, push, release, G1 work before G0 `GO`, or replacement of the fixed Desktop app before every
automated gate passes.

The v4 control, candidate, and formal artifacts remain immutable. No v4 command is rerun and no v4
artifact is edited. Every v5 artifact uses a new absent path.

## Problem statement

v4 closed the metadata-open bottleneck but the one permitted formal run still failed the NVIDIA
median gate:

- NVIDIA median/P95: `3207.465916 / 3376.432625 ms`; median misses by `207.465916 ms`.
- Hong Kong median/P95: `2474.804959 / 2829.896208 ms`; both pass.
- All ten formal correctness proofs completed with zero terminal metadata fallback.

The failure is in the ordinary RGB read phase, not in metadata open. The candidate NVIDIA median
open time was about `1501 ms`; formal was about `1479 ms`. Candidate read median was about
`1556 ms`; formal was about `1738 ms`.

The frozen formal NVIDIA iteration 5 log gives a deterministic scheduling explanation:

1. Six independent RGB ranges were emitted over one multiplexed HTTP/2 connection at about
   `539494238 ms`.
2. Three received HTTP 500 near `539494554 ms`.
3. A different successful sibling did not finish until about `539495203 ms`.
4. The current GDAL `ReadMultiRange()` did not classify the three 500 responses until about
   `539495289 ms`, after every sibling had finished.
5. It then slept 100 ms and emitted all three retries near `539495400 ms`.

The first failed response was therefore known roughly `846 ms` before its retry request was sent.
An independent 100 ms retry timer could have emitted that request around `539494654 ms`, while the
slow sibling remained in flight, recovering roughly `746 ms` of avoidable wait in this sample.

Source inspection confirms this is the upstream GDAL 3.13.1 algorithm: `VSICURLMultiPerform()` waits
for all attached handles, result processing computes one maximum delay, and the next loop sleeps
that delay before recreating the failed handles.

## Required production corrections

The v4 final review also found two Important coordinator-retry gaps and one Minor audit gap. The
audit gap is already closed by commit `377dff7`: the committed post-hoc v4 auditor now permits
exactly `429/500/502/503/504`, with a regenerated source and deterministic transcript binding.

Before any new public qualification, the private GDAL coordinator must also satisfy both production
corrections:

1. A completed immutable HEAD result must be validated before `CPLHTTPRetryContext::CanRetry()` is
   called for a transient first Range.
2. Each failed transient Range attempt must be redirect-free HTTP/2 on the validated HEAD
   connection before it may be retried.

The immutable HEAD retry prerequisite is:

- HEAD completed and returned `CURLE_OK`;
- status is exactly 200;
- the content length reported by libcurl is positive;
- redirect count is zero;
- HTTP version is exactly HTTP/2;
- connection ID is available.

The failed Range retry prerequisite is:

- Range completed and returned `CURLE_OK`;
- status is exactly one of `429/500/502/503/504`;
- redirect count is zero;
- HTTP version is exactly HTTP/2;
- connection ID is available and equals the validated HEAD connection ID.

These values are captured immediately after the initial parallel multi completes. The retry loop
must not call `CanRetry()`, publish the first interval, or emit a retry event when any prerequisite
fails. It emits one exact blocked event and enters the existing safe fallback/rejection path:

```text
ParallelHeadRange: transient-retry-blocked range=bytes=0-131071 status=<CODE> reason=<REASON> range-connection=<ID> range-http=<MAJOR>
```

The same Range transport prerequisites are rechecked after every failed retry attempt. A later
valid 206 cannot retroactively legitimize an invalid earlier attempt.

## Chosen latency mechanism

### Exact-path activation

The optimization is a second explicit path-specific option:

```text
OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY=YES
```

It is installed with RAII for only the exact AlphaEarth `/vsicurl/` object during the prefetch
profile's dataset lifetime. The option is removed at dataset close. A global config value with the
same name does not activate the feature. Baseline, optimized, ordinary osgVerse/osgSol GDAL,
terrain, 3D Tiles, photo, media, and unrelated `/vsicurl/` objects retain upstream behavior.

The existing metadata option remains separate:
`OSGSOL_VSICURL_PREFETCH_HEAD_RANGE=YES`. Neither option silently enables the other.

### Per-handle scheduling

Only the exact-path branch of `VSICurlHandle::ReadMultiRange()` changes. Its behavior is:

1. Create the same merged-request list, ranges, easy handles, headers, write buffers, and one shared
   multi handle as the current path.
2. Add every initial Range and drive the multi with `curl_multi_perform()`.
3. Drain `curl_multi_info_read()` whenever a handle completes; do not wait for unrelated siblings.
4. A complete 206/225 response with the exact expected byte count is distributed to its caller
   buffers and finalized immediately.
5. An exact transient status is passed to that request's existing `CPLHTTPRetryContext`. If its
   budget is available, detach that easy handle, record its independent due time, reset only its
   per-attempt body/header/error buffers, and leave all sibling handles attached.
6. `curl_multi_poll()` waits no longer than the earliest pending retry due time. When due, the same
   easy handle is re-added to the same multi handle with the identical Range and headers.
7. If no sibling is active, a bounded sleep until the earliest due time is allowed; it cannot delay
   useful in-flight work.
8. Permanent errors, exhausted retries, interruption, add/remove failures, and malformed/short
   success responses fail closed and clean up every owned handle exactly once.

The path-specific immediate branch accepts retries only for
`429/500/502/503/504`, remains bounded by `GDAL_HTTP_MAX_RETRY=3`, and uses the existing
`GDAL_HTTP_RETRY_DELAY=0.1` backoff/jitter implementation. A transient attempt must be
redirect-free HTTP/2 to enter the immediate scheduler; otherwise the request fails closed instead
of switching to a broader or less provable transport path.

Each scheduled attempt emits one exact event:

```text
ReadMultiRange: immediate-retry range=bytes=<START>-<END> status=<CODE> bytes=<N> attempt=<1..3> delay-ms=<N> connection=<ID> http=2
```

The event is emitted when the failed handle is detached and its due time is fixed. Curl's outgoing
request timestamp must be no earlier than the event timestamp plus the declared delay, allowing
only the existing 999-microsecond log-rounding tolerance.

### What does not change

- No successful Range is widened, merged across a gap, or added.
- No request threshold, latency limit, retry count, retry status set, byte ceiling, overview,
  georeferencing, RGB band, NoData rule, or science-correctness gate is relaxed.
- The six NVIDIA RGB intervals remain six intervals; transient retries repeat only the failed
  interval, as today.
- HEAD remains enabled and the first metadata interval remains exactly `bytes=0-131071`.
- The successful body-byte budget remains below `16 MiB`; transient response bodies remain fully
  reconciled with raw curl and `VSINetworkStats` evidence.
- AI/agent architecture, cross-source research, application UI, terrain, 3D Tiles, camera, and
  image-generation behavior are outside this patch and must remain bit-for-bit or test-equivalent
  at their protected boundaries.

## Rejected alternatives

### Merge six RGB intervals into three gap-spanning intervals

This could reduce request count but would fetch about 2.24 MiB of unrelated gap data, alter cache
geometry, and make performance depend on extra transfer time. It treats request count rather than
the observed retry barrier. It is retained only as a later option if the chosen scheduler is proven
insufficient.

### Change latency thresholds, warm the connection, skip HEAD, or broaden retries

These would weaken or evade the gate. They are forbidden.

### Modify GDAL globally

The observed public behavior is specific to the AlphaEarth vertical slice. A global scheduler
change would expand regression risk into unrelated application features and is forbidden.

## TDD and deterministic local evidence

Implementation starts with failing tests against the current private dependency.

### Coordinator safety RED/GREEN cases

The existing local TLS HTTP/2 server and test-only curl interposer cover:

- invalid HEAD plus a Range 500 that would return 206 if retried: zero retry, zero publication;
- transient HTTP/1 Range: zero retry, zero publication;
- transient Range with a test-interposed connection ID different from HEAD: zero retry, zero
  publication;
- transient Range with a test-interposed redirect count: zero retry, zero publication;
- valid HEAD plus same-connection, redirect-free HTTP/2 transient for each of the exact five codes:
  bounded retry and successful publication;
- status 404 and any unlisted status: zero retry.

Faults live only in the preloaded test library. No production `PREFETCH_TEST_*` or equivalent
runtime switch is added.

### Immediate multi-range RED/GREEN case

The local TLS HTTP/2 server accepts three explicit, non-overlapping ranges:

- one slow valid 206 sibling;
- one immediate 500 followed by a valid 206 for the same interval;
- one ordinary valid 206 sibling.

`VSIFReadMultiRangeL()` must return exact fixture bytes for all three. Server monotonic timestamps
and the exact runtime event must prove:

- all initial requests share one HTTP/2 session and overlap;
- only the failed interval is requested twice;
- the second request respects the native delay and begins before the slow sibling ends;
- the old batch barrier would fail this chronology assertion;
- actual body bytes, declared transient bytes, request counts, and network statistics reconcile;
- option absent and global-only activation do not enter the immediate branch;
- path-specific state is absent before activation and after RAII cleanup.

## Build and offline authorization

The implementation updates the pinned private-GDAL patch SHA, rebuilds only the owned
`build/science-deps-prefetch` prefix from checksum-verified local archives, and verifies the final
manifest. No public request is permitted during this stage.

Required offline gates include:

- dependency script contract;
- focused private GDAL local transport tests;
- full Python suite;
- protected core CTest suite;
- selected ScienceEarth CTests;
- private-prefix verifier;
- canonical bundle delta/closure audit;
- exact Desktop fingerprint/helper/entry-count protection;
- exact frozen old/v2/v3/v4 evidence hashes and modes;
- absent v5 evidence roots before authorization;
- task-level specification and code-quality review, followed by a whole-range review.

## v5 public and formal evidence

v5 uses only these new roots:

```text
build/science_g0_prefetch/requalification-evidence-v5/candidate
build/science_g0_prefetch/formal-evidence-v5
```

The frozen v4 formal set is the pre-v5 control; no new control request is necessary. After offline
authorization, exactly one five-iteration/two-case candidate process may run. It must produce ten
complete correctness proofs, no terminal metadata fallback, exact retry/request/byte accounting,
and both latency gates passing.

Only a candidate `PASS` authorizes exactly one formal CTest process using the already formal
prefetch profile and `formal-evidence-v5`. The formal result is authoritative:

- any missing proof, malformed event, fallback, byte mismatch, scientific mismatch, median above
  3000 ms, P95 above 8000 ms, nonzero process exit, or incomplete artifact set is `STOP`;
- no resampling or overwrite is allowed;
- a failure continues the active goal in a new versioned remediation namespace; it never mutates
  v5 evidence.

## G1, Desktop package, and human verification

Only formal G0 `GO` authorizes the previously planned G1 work and its protected regression gates.
Only G0 plus G1 `GO` authorizes packaging. Packaging updates the existing fixed application at:

```text
/Users/USER/Desktop/osgSol Earth.app
```

It must not create another differently named app and must not overwrite the source repository or
any older repository. The delivery report must identify the build commit, package fingerprint,
automated gate results, and a concise manual checklist for AlphaEarth loading/correctness/latency
and protected camera, image-generation, terrain, 3D Tiles, satellite-overlay, and panel-scroll
regressions. Tagging and synchronization remain separately authorized actions and do not occur in
this goal.
