# ScienceEarth G0 v4 Bounded Retry and Human Verification Design

**Date:** 2026-07-13

**Status:** Approved for implementation under the active Goal

**Protected boundary:** `ScienceEarth` / osgSol Earth `v0.2.0`

## 1. Decision

ScienceEarth will keep the isolated concurrent HEAD plus exact 128 KiB Range coordinator and add
one bounded retry loop only to the coordinator's initial Range request. The retry loop uses GDAL's
existing `CPLHTTPRetryParameters` and `CPLHTTPRetryContext`; it does not add a second retry policy.

The public G0 gate remains unchanged:

- exact `Range: bytes=0-131071` and HTTP 206 only;
- no successful full-object or comma-Range response;
- no coordinator fallback in a passing proof;
- exact scientific RGB, mask, orientation, dequantization, georeference, and overview results;
- NVIDIA and Hong Kong median at or below 3000 ms and P95 at or below 8000 ms;
- private dependency, protected Desktop, and earlier evidence isolation remain intact.

This design supersedes only the earlier statement that any transient coordinator Range response is
an automatic formal failure. A fully proved bounded retry followed by a valid exact 206 is now an
accepted prefetch outcome. Terminal fallback remains a formal failure.

When G0 v4 reaches `GO`, work continues through the minimum G1 vertical slice and updates the one
fixed Desktop application at `/Users/USER/Desktop/osgSol Earth.app`. The Goal stops only when
all automated gates have passed and a person has a concrete manual acceptance matrix to run. Tag,
push, and release remain outside this Goal and require separate explicit approval.

## 2. Evidence-based root cause

The v3 candidate did not expose a parsing, cache, or scientific-correctness defect. Its NVIDIA
iteration 2 received one 17-byte Cloudflare/source.coop HTTP 500 for the coordinator Range. HEAD
had returned 200 and the unchanged ordinary GDAL Range path subsequently returned the required
206. The current coordinator nevertheless falls back immediately on its first transient Range
response, so the strict no-fallback proof correctly rejected the run.

The same endpoint also produced isolated 17-byte HTTP 500 responses in the optimized control,
where GDAL's existing retry policy recovered. The approved profile already configures:

- `GDAL_HTTP_MAX_RETRY=3`;
- `GDAL_HTTP_RETRY_DELAY=0.1`;
- `GDAL_HTTP_RETRY_CODES=429,500,502,503,504`.

The missing behavior is therefore narrow: the new coordinator must use the same native retry
mechanism as the ordinary GDAL request path before it declares fallback.

## 3. Runtime boundary

### 3.1 Scope

The retry is eligible only after the existing path-specific concurrent prefetch coordinator has
already passed its eligibility checks. It applies only to the coordinator's exact first Range
request for the AlphaEarth `/vsicurl/` path. HEAD is never repeated.

The following remain unchanged:

- feature off by default and enabled only with the path-specific
  `OSGSOL_VSICURL_PREFETCH_HEAD_RANGE=YES` option;
- private GDAL 3.13.1 archive and pinned ScienceEarth patch;
- normal osgVerse/osgSol GDAL, terrain, 3D Tiles, photo, media, and other `/vsicurl/` paths;
- body cap of 131072 bytes and existing cleanup/fallback paths;
- one logical metadata Range GET in VSINetworkStats.

There is no global environment activation and no migration of existing app caches or user data.

### 3.2 Retry state machine

HEAD and the initial exact Range are added to the existing libcurl multi handle as before. After
both initial transfers complete:

1. validate HEAD through the existing path;
2. inspect the Range HTTP status;
3. if the status is not one of `429,500,502,503,504`, use the existing success or failure path;
4. if it is transient and `CPLHTTPRetryContext` grants a retry, retain the same Range easy handle,
   wait the native delay, reset response header/body state, and run only that Range handle again on
   the same multi handle;
5. repeat until exact success or until the configured retry budget is exhausted;
6. publish the 128 KiB region exactly once only after final validation succeeds.

The initial request is attempt zero. Event field `attempt=1` means the first retry that will be
sent after the event. The maximum configured value of three therefore permits at most four Range
requests: the initial request plus three retries.

Before each retry the coordinator clears response code, headers, parsed `Content-Range`, writer
offset, body size, and transient-body storage. It does not clear the multi handle's DNS,
connection, TLS-session, or HTTP/2 state. Bytes from a failed attempt can be accounted but can
never enter the region cache.

### 3.3 Retry event contract

Every transient response that causes another request emits exactly one complete line:

```text
ParallelHeadRange: transient-retry range=bytes=0-131071 status=<CODE> bytes=<N> attempt=<1..3> delay-ms=<N> range-connection=<ID> range-http=<MAJOR>
```

Fields and field order are part of the evidence contract. `status` must be one of the five
configured codes, `bytes` is the discarded transient response body size, `delay-ms` is the native
retry delay used before the next request, and the connection/version values describe that failed
attempt.

If a transient response has no retry remaining, the existing terminal event remains exact:

```text
ParallelHeadRange: transient-fallback range=bytes=0-131071 status=<CODE> bytes=<N>
```

It is followed by the existing `fallback=status-<CODE>` behavior. No successful v4 proof may
contain either terminal fallback event.

### 3.4 Success conditions

A retried prefetch is accepted only when all existing validation passes and all of these are true:

- HEAD was sent exactly once and returned its required valid result;
- retry attempts are contiguous, monotonic, and no greater than three;
- every Range request used the same URI and exact byte interval;
- every retry occurred only after the preceding configured transient response;
- the final Range returned exact HTTP 206, exact interval `[0, 131071]`, exact 131072-byte body,
  and the same positive object size as HEAD;
- HEAD, every transient Range attempt, and the final Range are HTTP/2 on the same connection;
- request chronology still proves the initial HEAD/Range overlap;
- region cache and file properties are published once and no ordinary metadata fallback occurs.

A changed connection identity, HTTP/1.x attempt, redirect, short/oversized body, malformed
`Content-Range`, nontransient status, GET 200, comma Range, detach/transport failure, or duplicate
publication takes the existing fail/fallback path. These conditions are not made retryable by this
design.

## 4. Accounting and proof schema

### 4.1 Runtime accounting

The coordinator records every physical response but logs one logical prefetch GET to
`NetworkStatisticsLogger`. Its downloaded bytes are:

```text
coordinator logical GET bytes
    = discarded coordinator transient-retry bytes
    + final successful exact-Range bytes
      or terminal coordinator fallback bytes
```

The existing `ParallelHeadRange: logical-get-complete bytes=<N>` event keeps its v3 meaning:
`<N>` is only the final successful or terminal-fallback response body. It is not changed to the
aggregate. The separate coordinator retry fields reconcile the difference between that terminal
body and the aggregate bytes reported by VSINetworkStats.

The physical request reconciliation is:

```text
actual GET count
    = successful GET count
    + ordinary CPL retry count
    + coordinator transient-retry count
    + coordinator terminal-fallback count
```

The equivalent equation is also enforced per Range interval. Downloaded-byte reconciliation is:

```text
VSINetworkStats downloaded bytes
    = successful Range bytes
    + coordinator transient-retry bytes
    + coordinator terminal-fallback bytes
```

Ordinary CPL transient bodies remain declared and conservatively reconciled through the existing
ordinary-retry evidence path; this change does not double-log them.

### 4.2 Proof additions

`HttpProof` gains explicit fields for:

- `coordinator_transient_retry_count`;
- `coordinator_transient_retry_bytes`;
- `coordinator_transient_retry_codes`.

The metadata-prefetch proof records the retry attempt, status, body bytes, delay, connection
identity, HTTP major, request time, response time, and exact Range for each retry. Existing
coordinator fallback and ordinary CPL retry fields remain distinct.

A successful v4 proof requires:

- `head_request_count == 1`;
- `range_request_count == 1 + coordinator_transient_retry_count`;
- retry count in `[0, 3]`;
- every retry event and transfer reconciled one-to-one;
- final success, shared HTTP/2 connection, initial overlap, exact byte/range accounting, and
  exactly-once cache publication;
- zero coordinator terminal fallback.

Missing, duplicate, malformed, out-of-order, inconsistent, or unexpected retry evidence fails
closed.

## 5. Test-driven implementation

Production patch changes begin only after tests demonstrate the current immediate-fallback
behavior. The first RED cases must prove that the current implementation lacks the retry event,
does not recover from a first transient response, and cannot satisfy the new accounting.

### 5.1 Static contract tests

The dependency/script test locks the exact single-line retry event, field order, and all five
positive statuses: 429, 500, 502, 503, and 504. The production patch exposes one shared
status-classification helper so implementation and tests cannot silently drift to different
allowlists.

### 5.2 Deterministic local transport tests

The local HTTP/2 fixture covers each configured status with an initial transient exact Range
followed by exact 206. Every case must prove:

- one HEAD, two Range requests, one retry, and no fallback;
- same HTTP/2 connection and exact interval for all requests;
- native delay and event fields;
- discarded transient body and correct total byte accounting;
- exactly-once publication and successful dataset open.

Additional cases cover:

- configured retry exhaustion: initial plus the allowed retries, then terminal fallback and no
  publication;
- nontransient 404: no coordinator retry;
- new connection, HTTP/1.x, redirect, wrong Range, short/oversized data, and malformed metadata;
- wrong or missing attempt, delay, byte, code, connection, or event evidence;
- all cleanup/completion orders and no HEAD replay.

The frozen v3 NVIDIA iteration 2 chronology is converted into a sanitized replay fixture. The v3
artifacts themselves remain non-writable and unchanged.

### 5.3 Regression suite

After the minimal implementation turns GREEN, run the full offline Python suite, core CTest
suite, selected ScienceEarth CTests, private-prefix verifier, disposable link probe, canonical
bundle/dependency audit, and all protected evidence/Desktop fingerprint checks. No new public
network qualification begins until these pass from a clean owned build root.

## 6. Immutable isolation boundary

The v4 implementation must preserve all rejected/accepted historical evidence, including v2,
v3, older formal evidence, the protected Desktop bundle, and their documented SHA-256 values.
Historical evidence remains frozen and non-writable. New work uses only:

- `build/science_g0_prefetch/requalification-evidence-v4/control`;
- `build/science_g0_prefetch/requalification-evidence-v4/prefetch`;
- `build/science_g0_prefetch/formal-evidence-v4`.

All three roots must be absent before qualification. Existing v2/v3 and formal directories are
inputs to the audit, never output locations.

## 7. One-shot public qualification

After all offline gates pass, execute exactly one control process and exactly one candidate
process. Each process runs five cold-object plus cold-connection iterations for NVIDIA HQ and five
for Hong Kong. Neither profile is rerun to seek more favorable network samples.

The candidate advances only when all ten proofs are complete and independently satisfy:

- exact scientific output and georeference;
- safe exact Range behavior and the existing byte ceiling;
- one HEAD, initial overlap, shared HTTP/2 connection, and exactly-once publication;
- zero coordinator fallback and at most three fully reconciled coordinator retries;
- complete ordinary retry, response-code, physical-request, and byte reconciliation;
- NVIDIA and Hong Kong median at or below 3000 ms and P95 at or below 8000 ms.

If the candidate fails, v4 is frozen as `STOP`; no formal run or Desktop update occurs. The next
action must address the measured cause rather than repeat the same runtime/evidence.

If the candidate passes, first commit the CMake/profile promotion to the v4 formal evidence root,
then run the formal profile exactly once. Formal `GO` also requires all downstream offline,
correctness, isolation, and audit gates.

## 8. Conditional G1 continuation

Formal G0 `GO` automatically resumes the already approved ScienceEarth G1 direction as a separate
implementation plan. The minimum human-verifiable vertical slice includes:

1. ScienceCore contracts and a provider registry;
2. cancellable research jobs, bounded cache, and provenance/evidence records;
3. an isolated science provider host with explicit missing/offline behavior;
4. an AlphaEarth provider/index for the approved NVIDIA and Hong Kong fixtures;
5. a science artifact layer isolated from camera, terrain, photo, and existing overlay state;
6. AI tools to search sources, start/poll research, and show a science artifact;
7. a ScienceEarth panel whose scrolling does not regress the existing panel behavior;
8. automated correctness, cancellation, cache, isolation, agent, and performance gates.

AI remains an end-to-end participant: the agent can research across registered sources, start and
monitor jobs, combine evidence, and display artifacts. Provider boundaries do not reduce the AI
to a fixed single-source command wrapper.

No G1 code or bundle replaces the protected baseline until v4 G0 is `GO` and G1's own tests pass.

## 9. Fixed Desktop handoff

The deliverable is always `/Users/USER/Desktop/osgSol Earth.app`; no version-suffixed sibling
application is created for ordinary updates. Packaging occurs in a verified staging directory.
Only after automated gates pass is the existing app atomically replaced, with a rollback copy
retained until manual acceptance.

The handoff report includes exact build commit, dependency/patch hashes, G0/G1 gate results, app
fingerprint, launch command, rollback location, and a manual matrix covering at least:

- panel wheel scrolling down and back up;
- clean/reset removes selected satellite tracks and circular range lines;
- Hong Kong 3D Tiles refine with altitude without holes, scale drift, or terrain distortion;
- middle-button tilt near ground does not shift the selected point or oscillate altitude;
- NVIDIA photo uses the currently visible flown-to camera, does not force top view, and does not
  reuse Hong Kong imagery;
- AlphaEarth NVIDIA and Hong Kong data show the correct tile, year, RGB mapping, and evidence;
- agent search/start/poll/show works without moving the camera;
- offline, missing-provider, cancel, cache, reset, relaunch, and update-in-place behavior.

Human verification may find a defect. In that case the Goal remains active, the fixed app is
rolled back or repaired as appropriate, and the same acceptance item is reissued with fresh
evidence. Tag and push happen only after the user separately approves the manually tested build.

## 10. Alternatives rejected

### 10.1 Retry the whole HEAD plus Range pair

This repeats a successful HEAD, weakens the one-HEAD proof, and adds avoidable latency. Only the
failed exact Range is retried.

### 10.2 Accept ordinary fallback as a successful prefetch

That would make the optimization proof indistinguishable from the rejected v3 behavior. Passing
evidence still requires zero fallback.

### 10.3 Add a custom retry list or global retry switch

Duplicating GDAL's native configuration risks policy drift and affects unrelated data paths. The
coordinator consumes the already configured `CPLHTTPRetryContext` only within its isolated path.

### 10.4 Package after unit tests but before public/formal gates

The transient behavior and latency target are public-network properties. The fixed Desktop app is
updated only after the one-shot candidate, formal gate, G1 slice, and full audit all pass.
