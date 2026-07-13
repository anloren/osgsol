# ScienceEarth G0 v6 HEAD Retry and Transport Attribution Design

## Status and authority

This design continues the active ScienceEarth goal after the immutable v5 public decision:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V5=FAIL
DESKTOP_PACKAGE=NOT_READY
```

The user selected the bounded remediation direction and instructed the work to continue until a
human can verify the fixed Desktop app. That authorizes local TDD, private-GDAL rebuilding, complete
offline authorization, exactly one new v6 candidate, and—only after candidate `PASS`—exactly one
v6 formal process. It does not authorize a tag, push, release, a v6 resample, G1 before formal G0
`GO`, or replacement of `/Users/USER/Desktop/osgSol Earth.app` before all G0, G1, package,
codesign, and launch gates pass.

The v4 and v5 evidence trees and the current Desktop app are immutable inputs. They are never
edited, chmodded, deleted, recreated, rerun, or used as v6 output roots.

## Frozen v5 facts

The single authorized v5 candidate process is a permanent `STOP`, not a sample to repair or rerun.
It produced one complete NVIDIA proof and then stopped during NVIDIA iteration 2. The frozen set has
two raw logs, two statistics files, one proof, one `ERROR` summary, and no v5 formal directory.

Three independent diagnoses separate two bugs that must not be conflated:

1. **Production recovery bug.** In NVIDIA iteration 2, the initial concurrent HEAD returned HTTP/2
   `500` while the exact coordinator Range returned a strict HTTP/2 `206`. The v5 coordinator only
   knows how to retry Range. It therefore classified HEAD as invalid, left the coordinator, ran the
   ordinary HEAD compatibility path, downloaded the first Range again, did not publish the
   coordinator result, and recorded a terminal `fallback=head-invalid`.
2. **Evidence attribution bug.** The parser assigns anonymous incoming header blocks to HEAD or
   Range using status/header order heuristics because curl's global `HEADER_IN` stream lacks easy
   handle identity. In the same iteration it misread the HEAD `500` as a Range response and the
   Range `206` as HEAD, then stopped with `HEAD request/response count mismatch`. FIFO, timestamp,
   status, or header-shape heuristics cannot repair this because HTTP/2 completion order is not role
   order.
3. **Corrected semantic ledger.** With roles attributed from the known handles, iteration 2 is
   `HEAD=2` (`500,200`), physical GET `=10` (`206 x 8`, `500 x 2`), network statistics
   `HEAD=2 / logical GET=3`, successful GET body bytes `5,141,883`, one HEAD transient response
   declaring 17 bytes but transferring zero body bytes, and two immediate Range transient bodies
   totaling 34 bytes. Correct attribution changes the outcome from parser `ERROR` to semantic
   `FAIL`: the fallback, repeated first Range, absent coordinator publication, incomplete proof set,
   and unavailable two-case latency aggregates still independently require `STOP`.

v6 must fix the production recovery and evidence attribution independently. A better parser must
not reinterpret v5 as a success, and a production recovery must not be accepted without
authoritative per-attempt transport evidence.

## Boundary and invariants

The production change remains inside the existing private GDAL 3.13.1 patch and the existing exact
path opt-in. It activates only when all current metadata-prefetch predicates hold:

- ordinary read-only `/vsicurl/http(s)`;
- path-specific `OSGSOL_VSICURL_PREFETCH_HEAD_RANGE=YES`;
- a nonempty path-specific `OSGSOL_VSICURL_PREFETCH_OPERATION_ID`;
- cold file properties and cold exact first interval `bytes=0-131071`;
- effective HEAD enabled, chunk size exactly 131072, and handler safety latch open.

The existing exact-path immediate multi-range scheduler remains separately controlled by
`OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY=YES`. Option-absent, global-only, missing-token,
baseline, optimized, unrelated `/vsicurl/`, application, terrain, 3D Tiles, photo, satellite,
panel, camera, AI/agent, and cross-source research behavior is unchanged.

The only retry status set is:

```text
429,500,502,503,504
```

Maximum retry remains `3`, initial delay remains `0.1` seconds, and every delay uses the current
native `CPLHTTPRetryContext` plus the validated finite/nonnegative/integer/steady-clock conversion.
No 408, broad 5xx class, transport error, redirect, or HTTP/1 response is admitted.

No Range widening, gap merge, extra successful Range, connection warm-up, no-HEAD shortcut,
threshold relaxation, byte-ceiling relaxation, science relaxation, or global GDAL behavior change
is allowed.

## Chosen production state machine

### Initial pair remains concurrent

The coordinator still adds the initial HEAD easy handle and exact first Range easy handle to the
same existing multi handle. Both initial transfers complete before recovery is scheduled. Their
immutable transport snapshots include curl result, HTTP status, HTTP version, redirect count,
connection ID, content header counts/validity, declared representation length, Range coordinates,
and actual writer body bytes.

The initial HEAD and Range must be redirect-free HTTP/2 with valid connection IDs on the same
connection. Range validation has two deliberately separate branches:

- A successful `206` is retainable only when it has one valid Content-Length equal to the bounded
  writer body, one strict Content-Range equal to `bytes=0-131071` with a valid total, and exact body
  length. Missing, malformed, duplicate, spoofed, contradictory, short, or overflowing fields fail
  closed.
- An exact-five transient is retainable for later Range retry only when curl transport is OK, its
  bounded error body and actual byte count are retained for transient accounting, and its response
  headers are noncontradictory. Content-Length may be absent or occur exactly once; when present it
  must parse and equal actual body bytes. Content-Range count 0 is valid for an error response; when
  present it must occur exactly once, parse strictly, and not contradict the requested first
  interval or total. A duplicate, malformed, spoofed, conflicting, oversized, or unaccounted error
  body fails closed.

Other status or transport shapes are neither successful nor retryable. A later valid response can
never legitimize an invalid initial attempt.

### Recover HEAD first

Before `CanRetry()` is called for an exact-five HEAD, that failed attempt must satisfy every
production admission rule:

- curl result is `CURLE_OK`, redirect count is zero, HTTP version is 2, and its nonnegative
  connection ID equals the initial Range connection ID;
- actual HEAD writer body is exactly zero;
- Content-Length count is 0 or 1; if 1, it parses as valid response representation metadata;
- Content-Range count is exactly 0;
- there is no duplicate/malformed length, writer body, Content-Range, contradictory header, or
  overflow state.

Any violation fails closed before `CanRetry()`, schedules no retry, and publishes nothing. A later
valid attempt cannot wash out an invalid initial attempt. If the completed HEAD passes these rules
and its status is one exact-five transient, the coordinator sets a sticky operation-local
`headRecoveryOwned` state and retries only HEAD. Once set, that state is never cleared or handed to
ordinary compatibility code:

1. Pass the failed HEAD status to an independent `CPLHTTPRetryContext`.
2. Validate its delay before detach or time conversion; permit at most attempts 2, 3, and 4
   (three retries after the initial attempt).
3. Detach the HEAD easy handle from the same multi handle, wait only the validated delay, reset
   only HEAD per-attempt state, and re-add the same easy handle to the same multi handle.
4. Drive the same multi until that HEAD attempt completes and emit its authoritative completion
   event before any retry, fallback, block, or publication event.
5. Require every retry attempt to remain redirect-free HTTP/2 and to use the same connection ID as
   the initial Range. A different connection fails closed; it is never silently accepted.
6. Stop only when HEAD returns status 200 with exactly one valid positive Content-Length,
   `CURLE_OK`, redirect 0, HTTP/2, the initial Range connection ID, body 0, and Content-Range count
   0. A later exact-five status may consume the remaining retry budget. Any other later status,
   including 400, 404, or 405, emits terminal block reason `retry-status`, with zero publication and
   zero ordinary fallback. Invalid transport/metadata, exact-five exhaustion, or unproven multi
   ownership is likewise terminal within the coordinator.

Every scheduled transition emits this anchored event after the failed completion and a successful
detach, but before wait/reset/re-add:

```text
VSICURL: ParallelHeadRange: head-transient-retry context=<32-lower-hex> retry=<1..3> request=<N> failed-attempt=<1..3> scheduled-attempt=<2..4> status=<CODE> delay-ms=<N> connection=<ID> http=2 declared-content-length=<N> actual-body-bytes=0
```

All response fields name the failed completion; the scheduled attempt has no response fields until
its own `response-v1` completion. Exhaustion emits a terminal block for failed attempt 4 and never
emits retry 4 or scheduled attempt 5.

The remove/re-add mechanism deliberately preserves the multi handle's connection pool rather than
creating a new easy/multi pair. The normative libcurl references are
[`curl_multi_remove_handle`](https://curl.se/libcurl/c/curl_multi_remove_handle.html),
[`curl_multi_add_handle`](https://curl.se/libcurl/c/curl_multi_add_handle.html), and the
[`multi interface`](https://curl.se/libcurl/c/libcurl-multi.html). Reuse opportunity is not treated
as proof: the runtime `CURLINFO_CONN_ID` equality gate remains mandatory. The implementation must
not set `CURLOPT_FRESH_CONNECT`, destroy the multi, or recreate the pair on the success path.

### Then recover Range

The initial Range header/body/transport snapshot remains untouched while HEAD retries. If it was a
strict exact `206`, successful HEAD recovery proceeds directly to joint validation. If it was an
exact-five transient, successful HEAD recovery then enters the existing bounded coordinator Range
retry with its independent retry context, same Range easy handle, same multi handle, exact Range,
and same HTTP/2 connection.

Thus simultaneous transient responses are always ordered:

```text
initial HEAD + Range complete
HEAD retry/recovery
Range retry/recovery
joint validation
one cache publication + one file-property publication
```

HEAD and Range retry budgets are independent and each remains at most three. There is no loop that
can multiply them into unbounded attempts. Production publication occurs exactly once only after
the coordinator's local immutable snapshots prove final HEAD 200 length, strict Range
206/Content-Length/Content-Range/body/total, shared connection, HTTP/2, no redirect, and internal
ownership/state invariants. Environment-appropriate raw/event/statistics/oracle reconciliation is
a later proof-qualification gate; it is not available to and must not control the production
publication path.

### HEAD attempt reset

Before re-adding a detached HEAD easy handle, reset all attempt-owned state so the failed response
cannot contaminate the next attempt:

- free and reinitialize `sWriteFuncHeaderData`;
- clear and reinitialize the HEAD body writer/counter state;
- rebind `sHeadHeaderWriter.psHeaderData` to live storage and reset its connection ID to `-1`;
- clear the curl error buffer;
- clear completion, curl result, status, HTTP version, redirect, connection, header counts,
  declared length, and actual-body counters;
- rebind the existing header, write, error, and userdata callbacks explicitly.

URL, request headers, HEAD method, redirect policy, easy handle, and multi handle are retained.
The completed Range header buffer, body buffer, strict `Content-Range` fields, actual byte count,
and connection snapshot are not cleared while HEAD recovers.

### Exact-five terminal block and compatibility boundary

An exact-five HEAD response is owned by this coordinator once detected. Retry exhaustion, invalid
delay, invalid retry transport, connection change, detach/add/perform failure, final HEAD/Range
size disagreement, or simultaneous Range failure must:

- publish neither file properties nor cache bytes;
- clear any detached, unpublished Range data;
- mark the current `(path, operation token)` blocked;
- emit one fixed terminal block event after the relevant completion event;
- prevent the exact-five failure from entering ordinary HEAD fallback or causing a second first
  Range request.

The new terminal marker is specific to this coordinator operation. Existing nontransient
compatibility behavior, including the already-tested HEAD 405/header-only GET route, is preserved
only when the **initial** HEAD itself is non-exact-five and `headRecoveryOwned` was never set. It
cannot recover, legitimize, or publish an exact-five-owned operation; `500 -> 400/404/405` always
blocks with `retry-status`.

Every easy detach still gets at most two removal attempts. If an attached easy cannot be removed,
the multi is abandoned once, all callback/header/buffer state referenced by attached handles is
retained, and the handler-wide atomic safety latch is set. Both future coordinator and immediate
multi-range paths consult that latch before acquiring a multi handle or setting easy options. An
already-detached easy is cleaned exactly once; no attached state is freed or rebound after
abandonment.

## Operation-local evidence ownership

An `EvidenceContext` is created only when the exact `(path, nonempty operation token)` lease is
created and is destroyed only after that lease ends and every associated callback can no longer
run. Coordinator, immediate multi-range, and nontransient `ordinary-head` compatibility work for
that operation share the same context. It owns:

- a mutex-protected contiguous completion ordinal allocator;
- a mutex-protected request-ID allocator and stable request/attempt registry;
- a unique opaque 128-bit per-operation correlation ID serialized as 32 lowercase hex characters
  and containing neither the path nor operation token;
- the event sink lifetime lease and only the bookkeeping needed by this operation.

The context is neither process-global evidence state nor thread-local state. Multiple threads in
one operation serialize through its mutex; unrelated paths/tokens have different contexts and
cannot consume each other's ordinals or request IDs. Request registration holds the mutex from
request-ID allocation through correlation-header serialization and registry insertion. Completion
emission holds it from ordinal allocation through complete line serialization and ordered sink
enqueue. Retry, fallback, block, and publication emission uses the same ordered enqueue. The mutex
is released before any libcurl call, CPL callback, sleep, user hook, or other potentially reentrant
path. A single FIFO sink drains already serialized lines without reordering them. Thus allocating
ordinal A and being preempted cannot let competing ordinal B appear first in that context's log.
Events are emitted only while the exact path/token context is active, so ordinary GDAL traffic is
filtered out.

Every easy callback/userdata object that can emit or complete holds a strong lifetime lease to its
context. Normal detach releases it after final callback completion. If detach fails persistently,
the retained attached-state owner also retains the context and event sink; it cannot be destroyed,
rebound, or reused while libcurl may still call it. Multi abandonment sets the handler latch before
new custom work, bounding retained contexts and preventing use-after-free.

Each exact-path request carries one production correlation header:

```text
X-OSGSol-Science-Correlation: <32-lower-hex>/<request>/<attempt>
```

It is emitted unconditionally for this already opt-in coordinator/multi-range operation, not by a
test switch, and contains no source path, URL credential, or operation token. The local server
records that header with method, Range, HTTP/2 stream/session, start, and completion. The parser
joins the server oracle to the completion event by this exact ID; it never infers a request from
arrival order, status, timestamp, or same-Range counting. Production adds no test-only environment
option or fault control.

## Authoritative transport completion evidence

### Fixed event schema

Every relevant completed v6 transfer emits exactly one anchored, role-labelled event while the
runtime still knows the completing easy handle:

```text
VSICURL: ScienceTransport: response-v1 ordinal=<N> context=<32-lower-hex> scope=<coordinator|multirange|ordinary-head> role=<head|range> request=<N> attempt=<1..4> method=<HEAD|GET> range=<none|bytes=N-N> curl=<N> status=<N> http=<0|1|2> redirects=<N> connection=<-1|N> content-length-count=<N> content-length-valid=<0|1> declared-content-length=<N> content-range-count=<N> content-range-valid=<0|1> content-start=<-1|N> content-end=<-1|N> content-total=<-1|N> actual-body-bytes=<N>
```

The line permits no missing, duplicate, reordered, unknown, or trailing fields. `ordinal` is
strictly contiguous from 1 after events are first partitioned by `context`.
`(context, scope, request, attempt)` is unique; `request` is stable across retries; `attempt` starts
at 1 and is contiguous through at most 4. Coordinator HEAD and Range use different request IDs.
Every initial/retry multi-range success and failure emits the event, not only responses that retry.

Every participating `head-transient-retry`, Range `transient-retry`, immediate retry, fallback,
terminal block, file-property publication, and cache publication event also carries the same
`context=<32-lower-hex>` field in its fixed schema. Such an event cannot be joined across contexts
or appear without a matching active context.

Only these scope/role/method/range tuples are legal:

| Scope | Role | Method | Range |
|---|---|---|---|
| `coordinator` | `head` | `HEAD` | `none` |
| `coordinator` | `range` | `GET` | `bytes=N-N` |
| `multirange` | `range` | `GET` | `bytes=N-N` |
| `ordinary-head` | `head` | `HEAD` | `none` |
| `ordinary-head` | `head` | `GET` | `none` |

The last tuple is the existing header-only GET used by nontransient compatibility. It has
`actual-body-bytes=0`; any accepted writer body makes proof qualification fail. It is a physical
GET and increments GET, not HEAD, network statistics. Consequently `role=head` describes metadata
purpose and does not imply method HEAD. No other tuple is accepted.

The event is emitted after the attempt's data and getinfo snapshot is final and before any
`head-transient-retry`, Range `transient-retry`, immediate retry, fallback, terminal block,
file-property publication, or cache publication event. Outgoing request, completion, retry, and
publication chronology must therefore be acyclic and uniquely replayable.

### HEAD and Range byte semantics

For method HEAD, `range=none`, all Content-Range coordinate fields are `-1`, and
`actual-body-bytes` must be zero. `declared-content-length` is response representation metadata;
for the v5-shaped HTTP 500 it may be 17 while the body actually transferred remains zero. Declared
HEAD length is recorded in head-retry evidence but is never added to GET successful, transient,
actual, or conservative downloaded-body totals. The `ordinary-head/head/GET/none` tuple follows the
separate header-only GET rules above and is accounted as a physical GET.

For GET 206, actual body bytes come only from the request's independent writer. Exactly one valid
Content-Length must equal the writer length, and exactly one strict Content-Range must match the
request interval and total. Missing Content-Range has count 0; one malformed line has count 1 and
valid 0; duplicates have count at least 2 and valid 0; a spoofed non-header occurrence does not
increment the count.

### Parser modes and reconciliation

The proof builder has two explicit modes:

- `AttributedV6`: required for every v6 live candidate and formal proof. If any `response-v1`
  event exists, the complete required event set must exist. Missing, malformed, duplicate,
  noncontiguous, unknown, contradictory, or unattributed events are hard proof failures. It never
  falls back to FIFO/time/status/header-shape role inference.
- `LegacyFrozen`: permits old frozen evidence to remain auditable but is never eligible to qualify
  v6. It is an explicit call-site choice, not automatic fallback when v6 events are absent.

Raw `HEADER_OUT` method/Range/correlation records reconcile one-to-one with completion event
requests using the exact `context/request/attempt` header value. Raw `HEADER_IN` data is used only
as an aggregate multiset of complete response tuples, never to assign roles. The local HTTP/2
server independently records the exact correlation header, method, Range, stream, session, start,
and completion for a correlation-ID join and one-to-one local oracle reconciliation.

Qualification has two deliberately different evidence envelopes:

- Offline/local fixture gates require raw curl output, context-correlated completion/decision
  events, VSINetworkStats, and the local server oracle joined by the correlation header.
- Public candidate/formal gates require raw curl output, context-correlated completion/decision
  events, VSINetworkStats, and the existing science oracle for RGB values, CRS/georeference,
  WGS84 bounds, NoData, requested intervals, and byte limits. Public origin/CDN server logs do not
  exist in this workflow and are never required or claimed.

No public proxy, capture endpoint, provider cooperation, header echo, or network scope expansion is
introduced. The public proof uses only client-side artifacts plus the existing science oracle.

A sanitized v5-shaped `HEAD 500 + Range 206 -> ordinary fallback` replay, augmented only with
test-owned authoritative completion events, must parse to a complete semantic `FAIL`, not parser
`ERROR`: one invalid HEAD completion, one fallback, repeated first Range, zero coordinator
publication, and no qualification. Frozen v5 files are not modified. The mirror scenario
`HEAD 200 + Range 500` must attribute the transient to Range and never increment head retry fields.

### Proof and statistics fields

In addition to existing Range retry fields, each proof records:

```text
metadata_prefetch.head_retries[] = {
  retry_ordinal, request, failed_attempt, scheduled_attempt, delay_ms,
  failed_status, failed_connection_id, failed_http_major,
  failed_declared_content_length, failed_actual_body_bytes
}
coordinator_head_transient_retry_count
coordinator_head_transient_retry_declared_bytes
coordinator_head_transient_retry_actual_body_bytes
coordinator_head_transient_retry_codes
science_transport_response_count
science_transport_attribution = "response-v1"
```

Each `head_retries[]` row describes a scheduled transition, not a response: status, connection,
HTTP version, declared length, and actual body belong to `failed_attempt`; `scheduled_attempt` is
exactly `failed_attempt + 1`; `delay_ms` lies between those two attempts; and `retry_ordinal` is
contiguous `1..3`. Physical completion event `attempt` remains contiguous `1..4`.

If HEAD succeeds after `k` scheduled retries (`1 <= k <= 3`), there are `k + 1` HEAD request/
completion events and exactly `k` retry rows; the final 200 completion creates no retry row. On
exhaustion there are exactly four HEAD requests/completions and three retry rows. The fourth
exact-five completion is represented only by its completion plus terminal-block evidence—there is
no scheduled attempt 5 and no fourth retry row. Failure before a delay is validated or scheduled
likewise creates a completion plus terminal evidence but no retry row.

For method HEAD, actual request count equals `response-v1` method-HEAD count and VSINetworkStats
HEAD count. A header-only ordinary GET is excluded from that formula and increments the physical
GET accounting instead. HEAD actual body total is zero. GET physical attempts, logical GET
statistics, successful bytes, transient actual bytes, response codes, Range retry fields, and
conservative body ceiling retain their existing independent reconciliations. A HEAD exact-five
response can never match a Range retry event.

## Deterministic TDD matrix

The local TLS HTTP/2 fixture and test-only curl interposer must prove RED before production edits
and GREEN afterward.

The test binary also adds `--completion-json <fixed-absent-path>`. After closing/fsyncing all
evidence and summary data it builds the actual evidence manifest, then publishes both manifest and
one `osgsol.scienceearth.v6-completion.v1` PASS record through the final-mode-before-fsync,
rename-only durability primitive defined below. The completion contains PID,
execution-snapshot binary SHA-256, summary SHA-256, and actual-evidence-manifest SHA-256. No sample
may change after publication. CLI missing/duplicate arguments, symlink/existing target,
interrupted/duplicate publication, and post-completion mutation are test-locked.

### HEAD-first recovery

- Parameterize HEAD `429,500,502,503,504 -> 200`: HEAD 2, Range 1, same session/connection,
  no repeated first Range, one head retry, zero fallback, one cache publication, one property
  publication.
- For every exact-five HEAD, admit Content-Length absent or one valid declaration only when curl is
  OK, redirect is 0, HTTP is 2, connection matches Range, body is 0, and Content-Range count is 0.
  Duplicate/malformed Content-Length, any Content-Range, nonzero body, transport error, redirect,
  HTTP/1, or connection mismatch must produce zero `CanRetry()` calls. A later good attempt cannot
  wash out that initial invalid transport. Final HEAD 200 requires exactly one valid positive
  Content-Length.
- Parameterize `500 -> 400`, `500 -> 404`, and `500 -> 405`: each records the second completion,
  one scheduled retry, terminal `retry-status`, zero publication, zero ordinary fallback, and no
  header-only compatibility GET. Keep a separate initial-405 case proving the old compatibility
  route still works before exact-five ownership exists.
- Three retries then success: HEAD 4, Range 1, attempts 1..4, three validated delay envelopes,
  one publication.
- Both completion orders: Range success before HEAD transient and HEAD transient before Range
  success; retry begins only after both initial responses complete.
- Simultaneous transient HEAD/Range, including the exact-five matrix: recover HEAD first, then
  Range, HEAD 2, Range 2 for a one-retry case, independent budgets, same HTTP/2 connection, one
  publication.
- HEAD exhaustion: initial plus three retries, Range remains once if already valid, exact terminal
  block, no ordinary fallback, no duplicate first Range, no publication, operation token blocked.
- HEAD recovers but Range exhausts: no publication and no ordinary fallback.
- Invalid delay, transport, redirect, HTTP/1, connection, add/remove/perform, or size mismatch:
  fail closed before publication and preserve ownership/latch invariants.
- Nontransient 400/404/405 behavior remains compatible; only the existing nontransient route may
  fall back. Exact-five never does.

### Attribution and mutation rejection

- v5-shaped HEAD 500/Range 206 and mirror HEAD 200/Range 500;
- siblings with identical statuses completing out of order;
- missing/duplicate/malformed completion, ordinal gap/reorder, attempt 0/5/skip, duplicate request,
  role/method swap, Range changed to a sibling, unknown scope, and trailing field;
- two contexts forcibly interleaved as a valid case, with each context independently starting at
  ordinal 1; missing/wrong context, cross-context retry/publication, or correlation mismatch;
- a deterministic race that pauses context A after ordinal allocation and lets another thread
  contend, proving A's serialized/enqueued line still precedes the next ordinal in that context;
- HEAD actual body changed from 0 to 17; declared/actual mismatch;
- Content-Length count/validity/value mutation;
- Content-Range count/validity/coordinates/total mutation, including spoof and duplicate;
- curl result, HTTP version, redirect, connection, status, retry delay/order mutation;
- raw request or response tuple missing/extra and local-server-oracle request missing/extra;
- correlation header missing, duplicated, token/path-bearing, or mismatched across raw/event/server;
- fallback changed to publication, publication before completion, or duplicate publication;
- HEAD 500 falsely matched to a Range retry, simultaneous transients swapped, or completion event
  emitted after its retry/fallback.

Every mutation must be an authentic copy/edit of a passing proof input and independently fail.
Existing coordinator Range, immediate multi-range, strict Content-Range, abandonment, latch,
capacity, path lease, global-only, missing-token, and option-absent tests remain green.

## Build, pin, and offline authorization

The implementation patch must include both `port/cpl_vsil_curl.cpp` and
`port/cpl_vsil_curl_class.h` as required by ownership/latch state. Update only
`GDAL_PREFETCH_PATCH_SHA256`. Rebuild the owned `build/science-deps-prefetch` tree from the pinned,
checksum-verified local `gdal-3.13.1.tar.gz`; do not download. A second fresh archive extraction
must apply the three pinned GDAL patches and byte-compare both patched source/header files with the
source used for the built prefix. Private prefix verification, archive manifest, focused local
HTTP/2 tests, complete Python/offline CTest suites, disposable probe closure, Mach-O dependency,
export, path-leak, codesign, and protected regression gates must all pass before public access.

Offline authorization binds exact commits, allowed files, patch hash, patched cpp/header bytes,
prefix manifest, dynamic test totals, disposable probe, every frozen v4/v5 artifact/hash/mode,
Desktop tuple, formal listing, and all three absent/non-symlink v6 future paths:

```text
build/science_g0_prefetch/requalification-evidence-v6/candidate
build/science_g0_prefetch/requalification-evidence-v6/candidate-summary.json
build/science_g0_prefetch/formal-evidence-v6
```

It ends:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V6=AUTHORIZED_NOT_RUN
DESKTOP_PACKAGE=NOT_READY
```

The network-denied public preflight has fixed ignored supporting paths
`.superpowers/sdd/task-4-v6-one-shot-preflight.sh` and
`.superpowers/sdd/task-4-v6-one-shot-preflight.log`. Its exact template/hash/mode is declared by
authorization, it runs the committed verifier's `authorization` phase, and its frozen wrapper/log
hashes and `0400` modes are required before candidate execution. The later `preformal` phase and
final decision auditor bind the same paths and hashes. They remain ignored evidence and never
expand a tracked commit.

## One-shot v6 decision and continuation

After clean reviewed authorization, run exactly one five-iteration/two-case v6 candidate process
into the absent v6 candidate paths. It must produce ten complete `AttributedV6` proofs using the
public envelope only: exact raw request/response accounting, context-correlated completion and
decision events, VSINetworkStats, and the existing RGB/CRS/georeference/WGS84/NoData/interval/byte
science oracle. Public server logs are neither available nor required. The set additionally needs
zero fallback, one publication per iteration, conservative downloaded body `<=16 MiB`, NVIDIA and
Hong Kong median `<=3000 ms`, and P95 `<=8000 ms`.

Task 3 commits the exact byte templates and SHA-256 values for two shared ignored components:
`.superpowers/sdd/task-4-v6-runner.py` and `.superpowers/sdd/task-4-v6-finalize.py`. Task 4
materializes both as regular non-symlinks at mode `0400`. It also materializes both candidate and
formal wrappers as regular non-symlinks at mode `0500`. Each wrapper is only a shebang and one final
`exec /usr/bin/python3 <fixed-absolute-runner.py> --candidate|--formal`; it performs no redirect,
file I/O, `mkdir`, trap, wait, or child management. Authorization, preformal, and the final auditor
all bind the exact templates, hashes, and modes of runner, finalizer, and both wrappers.

The only verifier source is independent tracked pure-API file
`tests/science_v6_authorizer.py`; it contains no runner/finalizer/wrapper template or expected hash.
The committed runner template contains the verifier's computed SHA-256 as a literal constant.
Clean-HEAD authorization independently binds the Task 3 commit tree and runner-template SHA-256,
so neither component can rewrite the other's trust anchor. At runtime runner opens the tracked
source through `O_NOFOLLOW`, uses the same FD for UTF-8/LF/schema checks, `fstat`, and SHA-256, and
accepts only the literal hash before compiling/executing verified bytes in an isolated namespace.
Any SHA printed in decision documentation is reporting only and never a trust root.

Every framed/JSON/data/executable regular temp-to-target publication uses one indivisible, audited
durability primitive. The temp open flags are exactly
`O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC` at mode `0600`; `O_RDONLY`, `O_WRONLY`, any missing
flag, or any extra access/create flag is rejected before write. `O_RDWR` is mandatory because the
same FD writes and then reads/hashes the bytes. Runner writes the complete bounded bytes, calls
same-FD `fchmod` to final `0400` data/JSON or `0500` executable mode, same-FD fsyncs only after
chmod, and repeats same-FD fstat/hash/size/uid/`nlink==1`/mode checks while requiring the temp itself
not to carry `UF_IMMUTABLE`. Only then may `renameatx_np(RENAME_EXCL)` publish the target. Runner
immediately fsyncs the held parent dirfd and no-follow reopens/relooks up the target as the same
inode/hash/size/uid/`nlink`/final mode using
`O_RDONLY|O_NOFOLLOW|O_CLOEXEC`. This read-only target reopen is distinct from and does not weaken
the exact `O_RDWR` temp-creation contract.

Immutability is a separate post-rename phase. For every target whose contract requires it, runner
uses a checked `ctypes.CDLL(None, use_errno=True)` binding to macOS
`fchflags(int, unsigned int)` with `argtypes=[ctypes.c_int,ctypes.c_uint]`, `restype=ctypes.c_int`,
and errno captured on `-1`, on the reopened target FD to add `UF_IMMUTABLE`; path `chflags`, shell
`chflags`, and any pathname flag mutation are forbidden. It then same-FD fsyncs, fstats and requires
the exact `st_flags`, fsyncs the held parent dirfd again, and no-follow reopens/relooks up the same
inode/hash/mode/flags. A missing flag after target rename with correct final mode/bytes/inode is a
legal interrupted-publication state, not PASS. Under a gate-absent held lease plus the existing
owner-death and cleanup-lock proofs, recovery may resume only this FD-level flag phase or clean the
allowlisted target; it may not rewrite/replace it. Any pre-exec/preformal target remains ineligible
for exec or `PREFORMAL_PASS` until the full flag phase completes; post-exec records cannot reach
`COMPLETE_PASS` until it completes. There is no hard-link or replacing fallback.

Directory temps are created only by `mkdirat`, never regular-file open. A directory temp finishes
children, reaches final mode, same-dirfd fsyncs after chmod, and passes same-FD metadata/enumeration
recheck with no self `UF_IMMUTABLE` before its no-replace rename. After parent fsync and same-inode
target reopen with `O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`, a flag-bearing directory target
receives `UF_IMMUTABLE` only through the same ctypes
`fchflags` FD path, followed by directory-FD fsync/`st_flags` recheck, parent fsync, and final
same-inode reopen/re-enumeration. A published final-mode directory lacking only its target flag is
the analogous legal gate-absent recovery window. The fixed permanent gate is the deliberate stricter
exception: its final-name presence has already consumed one-shot, so missing only its self flag may
be FD-resumed but never cleaned or publicly retried. The final lease container intentionally remains
mutable until gate publication because snapshots are created beneath it; it is not a directory-temp
flag exception hidden inside this primitive. A directly named lease child file inside an
unpublished prepare directory completes final mode/fsync/recheck and may be FD-flagged because that
child itself is not renamed; only its containing directory moves. Every stage is a separate
crash-mutation point.

The runner owns supporting paths through directory-FD-relative, no-follow operations. Before any
snapshot temp, it validates authorization/preflight, wrapper/common hashes, gate absence, and
absence of log/marker/evidence/summary/completion. Under fixed `LC_ALL=C`, `LANG=C`, `TZ=UTC`, it
obtains its PID and one canonical ASCII `/bin/ps` birth line with `shell=False`,
`close_fds=True`, `pass_fds=()`, exact argv, and strict regex, then computes
`birth64hex = SHA256(canonical-line-bytes)`. Only that lowercase 64-hex digest
is persisted; the raw birth line is never stored. A live PID is always compared by freshly hashing
the newly parsed canonical line. A dead PID requires no raw birth value.

Before creation, runner opens the fixed `.superpowers/sdd` parent as a no-follow directory FD and
performs a finite exact enumeration. For each mode there may be at most one member of the set
{matching prepare directory, fixed final lease}; multiple members, an extra prefix-colliding entry,
or an invalidly formatted prepare name is read-only `OFFLINE_STOP`/`PREFORMAL_STOP`. The only valid
prepare basenames match exactly
`^task-4-v6-candidate-pregate\.lease\.prepare\.(0|[1-9][0-9]{0,9})\.([1-9][0-9]{0,9})\.([0-9a-f]{64})$`
or
`^task-4-v6-formal-pregate\.lease\.prepare\.(0|[1-9][0-9]{0,9})\.([1-9][0-9]{0,9})\.([0-9a-f]{64})$`.
Parsed uid must round-trip canonically, fit `uid_t`, and equal the runner uid; PID must round-trip,
fit positive `pid_t`, and equal the captured runner PID on creation. These bounds and the fixed
prefix keep even 10-digit uid plus 10-digit PID comfortably below macOS `NAME_MAX=255`.

With fixed process umask `077`, runner atomically calls
`mkdirat(parent-dirfd, unique-prepare-basename, 0700)` and immediately
fsyncs the parent, then repeats the parent enumeration and requires this to be the sole per-mode
prepare/final entry with the same inode before writing content. From that first durable directory
entry, the name itself is a partial, verifiable
owner frame even when the process crashes before writing a byte. Within it runner first creates
fixed regular `lease.lock` through `O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC`, validates same-FD stable
inode, runner uid, `nlink==1`, zero size and mode `0600`, obtains `flock(LOCK_EX)`, then fchmods
`0400`, same-FD fsyncs only after chmod, rechecks same-FD inode/uid/`nlink`/size/mode, adds
`UF_IMMUTABLE` with FD-level `fchflags`, same-FD fsyncs/rechecks `st_flags`, and fsyncs the prepare
dirfd. It retains that open file description for the pre-gate lifetime. Runner then creates
the canonical framed `owner.frame`, canonical `lease.json` containing magic/schema,
candidate|formal mode, uid, PID, `birth_sha256`, fixed temp/final paths, expected source/component
hashes, and lock inode/mode bindings, plus separate `lease.sha256`. Each directly named file is
created `O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC`, written completely at `0600`, fchmoded
`0400`, same-FD fsynced after chmod, same-FD fstat/hash/mode rechecked, then FD-flagged immutable and
same-FD fsynced/`st_flags` rechecked. Once all four pass, runner fchmods the prepare dirfd to final
`0700`, requires no self `UF_IMMUTABLE`, fsyncs it after chmod, and rechecks the directory
enumeration/inodes/modes/child flags before a same-directory
no-replace atomic rename to fixed `task-4-v6-<mode>-pregate.lease` using
`renameatx_np(RENAME_EXCL)`, immediately fsyncs the parent, then reopens/re-enumerates the final
lease as the same inode with the same four hashes/modes/flags. The final lease directory itself
remains unflagged and mutable so later snapshot temps can be created. An unavailable/failed primitive is
PENDING/STOP without substitution or deletion. Only after fixed final-lease publication may any
snapshot temp be created.

All candidate live, formal live, CTest executable, CTestTestfile, and listing-build snapshots use
the unified primitive and exact regular temp flags: final `0500` executable or `0400` data mode is
same-FD fsynced/rechecked with no temp flag before no-replace rename, parent-dir fsync, and
same-inode target reopen. Only then does FD-level `fchflags` set target `UF_IMMUTABLE`, followed by
same-FD fsync/`st_flags` recheck, parent fsync, and final same-inode reopen/relookup. Snapshot
directory temps likewise rename without their own flag and acquire it only on the target inode;
immutable child files may already exist because the tested parent-directory rename remains legal.
Immediately before exec/use, runner revalidates the held FD and immutable/read-only parent.

A later gate-absent runner first repeats the strict parent enumeration. A matching dynamic prepare
is recoverable from its regex name identity even when it is empty, `owner.frame` is absent or
truncated, or state/checksum is absent or truncated. Through parent and child dirfds it requires the
prepare directory to remain the same inode, expected uid, directory type, `nlink==2`, and mode
`0700`, and all
public gate/log/marker/evidence/summary/completion paths to remain absent. A prepare directory's
finite allowlist is only the ordered creation prefix of `lease.lock`, `owner.frame`, `lease.json`,
and `lease.sha256`; a later file without its predecessors is tamper. `lease.lock` is always zero
bytes, regular, runner-uid, stable-inode, `nlink==1`, and mode `0600` only before complete lock setup
or `0400` afterward. Snapshots cannot start
until the fixed final lease exists. Every present child must have the expected regular-file type,
uid, `nlink==1`, and stable inode. Except for zero-byte lock, mode `0600` is allowed during write;
mode `0400` is allowed only
for a complete post-chmod same-FD-fsynced/rechecked file; missing `UF_IMMUTABLE` is then a legal
pre-flag crash state recoverable only after death/lock proof. An incomplete regular file must be a byte prefix of its specified
magic/frame/checksum encoding; a complete one must pass the full schema/hash/path binding. Before lease rename all
four must be complete, `0400`, flag-complete, and revalidated. Unexpected
names, metadata, type, or non-prefix bytes are tamper and read-only STOP.

Prepare-owner liveness is decided from the name's uid/PID/digest, never from missing partial
content; fixed-final-lease liveness uses the same fields only after full lease verification. If
`kill(pid,0)` returns `ESRCH`, the fixed-env shell-false ps must return exactly `rc=1`, empty stdout,
empty stderr, and a second kill must again return `ESRCH` before the owner is dead. If the first
kill succeeds, ps must return one strict canonical birth line and its fresh SHA-256 is compared with
the name: equality is alive `OFFLINE_PENDING`/`PREFORMAL_PENDING`; inequality is only a possible
PID reuse. Reuse proves the old owner dead only if a second kill succeeds, a second strict ps parse
produces the same new digest (still different from the stored digest), and a third kill succeeds.
Any exit/change between checks, `EPERM`, other errno, launch/rc/output/ASCII/parse failure, or
nonmatching metadata is pending or STOP as appropriate and deletes nothing. Every runner,
recovery, finalizer, listing child, and listing parent `/bin/ps` subprocess uses
`close_fds=True, pass_fds=()`, `stdin=DEVNULL`, and bounded private stdout/stderr pipes; no liveness
probe inherits a lease, directory, READY, GO, or parent evidence-pipe FD.

For a fixed final lease, before any flag change or unlink recovery opens bound `lease.lock` with
`O_RDONLY|O_NOFOLLOW|O_CLOEXEC`, revalidates regular type, exact inode/uid/`nlink==1`/mode `0400`/size zero,
and must obtain `flock(LOCK_EX|LOCK_NB)` on that separately opened FD. Contention, `EINTR`, or any
other error is PENDING and authorizes no cleanup. Success is necessary but not sufficient: all
runner, optional listing-child, and PGID death proofs still apply. Recovery holds its lock FD across
bottom-up deletion and every parent fsync, closing only after cleanup is durably complete.
Mutation coverage includes lock create-before-flock, held/unheld state, metadata replacement,
nonblocking contention/error, parent death around spawn before child identity, same-open-description
fd 5 inheritance, fd 6 directory-identity/CLOEXEC checks, CLOEXEC mistakes, CTest exec retention, child exit release, and lock retention
through final cleanup fsync.

Only after one of those stable death proofs may recovery clear `UF_IMMUTABLE` exclusively with the
ctypes `fchflags` binding on each held no-follow FD, same-FD fsync/recheck the cleared `st_flags`,
and delete the fully dirfd-enumerated allowlisted tree bottom-up. Path/shell `chflags` is never used;
runner rechecks inode/uid/`nlink`/type immediately before each removal and fsyncs every affected
parent. A fixed final lease is less permissive: its four
lease files must pass full owner/schema/hash/path verification, after which a separate finite
allowlist covers the mode's known partial snapshot temp/final directories and files (candidate
execution snapshot; or formal execution and CTest-listing snapshot, `Testing/Temporary`, and
`LastTest.log`). It also binds the fixed sibling gate-preparation directory
`task-4-v6-one-shot-<mode>.invoked.prepare`. That directory is mode `0700` while incomplete and may
retain only runner uid, stable directory inode/type and `nlink==2`; it may contain only this ordered
state machine: empty; `runner-state.json.tmp`; complete
`runner-state.json` target missing only its post-rename flag; that flag-complete target; the flag-
complete runner target plus `exec-authorized.json.tmp`; both targets with only exec-authorized's
post-rename flag missing; or both flag-complete targets. Exec-authorized temp creation is forbidden
until runner-state is flag-complete. Temps
are regular `nlink==1`, runner-uid, stable-inode files, mode `0600` while partial and `0400` only
when complete; each partial must be a prefix of its framed magic and each complete payload is
bounded to 64 KiB and must pass canonical JSON schema/checksum plus lease, uid, PID,
`birth_sha256`, mode, snapshot, argv/cwd, completion, source, and inode bindings. Each exact-flag
regular temp uses the unified file primitive: final-`0400` fsync/recheck without a temp flag,
same-directory `renameatx_np(RENAME_EXCL)`, child-dir fsync, same-inode target reopen, then target-FD
`fchflags(UF_IMMUTABLE)`, post-flag fsync/`st_flags` recheck, child-dir fsync, and final reopen. A
target with correct bytes/mode but its flag missing is a legal gate-absent crash window recoverable
under the held lease/death/lock rules; it is not a complete gate record. With both targets flag-
complete and temps absent, mode `0700` is the legal post-target/pre-freeze crash window and `0500`
is the legal frozen state; gate rename requires `0500`. Every other state/mode pairing, order, name,
type, magic, size, uid, inode, or `nlink` is STOP and deletes nothing. Either legal complete mode is
recoverable after lock acquisition and owner death while permanent gate remains absent.

Gate-absent recovery may delete that exact preparation tree with the final lease only after the
owner death proof and the same last-moment metadata rechecks. It covers crashes after preparation
mkdir/fsync and, for every publisher, after exact temp open/partial write, complete write, fchmod to
final mode, post-chmod same-FD fsync, same-FD fstat/hash/mode/no-temp-flag recheck, no-replace
rename, first parent-dir fsync, target reopen, target-FD fchflags, post-flag fsync/`st_flags` recheck,
second parent fsync, and final reopen. It separately covers permanent-gate directory fchmod `0500`,
no-self-flag dir fsync/recheck, first rename, first parent fsync, fixed-name same-inode reopen,
target-dir FD `fchflags(UF_IMMUTABLE)`, gate-dirfd fsync/exact `st_flags` recheck, second parent fsync,
and final gate/flagged-child reopen/re-enumeration. The first observation of fixed
`task-4-v6-one-shot-<mode>.invoked` changes the state permanently: lease recovery must not clear
flags, unlink the preparation path, snapshot, listing-child state, scratch, or lease, regardless of
whether the permanent gate is self-flag-complete, missing only its self flag, damaged, or accompanied
by a stale prepare entry. If a trusted runner, finalizer, manifest, or auditor has observed/recorded
that fixed name and it is later absent, state is permanent read-only STOP and no public retry is
allowed. A legitimate crash at
prepare `mkdir`, empty directory, partial owner frame, partial state, partial checksum, final-lease
publication, snapshot write/publish, listing scratch, verifier return, or immediately before gate
publication therefore resolves only to safe recovery or PENDING, never `OFFLINE_STOP` merely for
being partial. After cleanup the permanent gate remains absent and bottom invocation remains 0, so
a new lease is not a second public invocation. A lease surviving successful gate publication is
never recovered; it is frozen as evidence.

Before public consumption, runner creates the fixed gate-preparation directory through the held
parent dirfd at `0700`, immediately fsyncs the parent, and writes the two framed records in the exact
order above. `runner-state.json` uses magic `osgsol.scienceearth.v6-runner-state.v1`;
`exec-authorized.json` uses magic `osgsol.scienceearth.v6-exec-authorized.v1` and binds the complete
runner-state hash/inode. For formal it also binds the persisted listing-child PID/PGID/birth digest,
child-state hash/inode, lease-lock inode/mode, inherited-fd/alarm contract, and proved-empty post-listing
process group plus `A/R/G/E` deadlines, frozen child-state sibling durability proof, and LastTest
directory durability chain. Candidate state also binds its lease lock. Both record schemas bind the
preparation inode, fixed final basename, mode `0500`, and required post-rename directory-FD self-
flag/two-fsync/final-reopen contract; `exec-authorized.json` binds that expectation through the
runner-state hash and independently bound runner-template SHA. Both record targets must already
carry verified `UF_IMMUTABLE`. Runner fchmods the complete preparation directory to `0500`,
explicitly requires its own `UF_IMMUTABLE` absent, fsyncs the held directory FD after chmod, and
revalidates its same-FD inode/enumeration plus final lease. A real local Darwin filesystem test—not
a mock—requires rename of an itself-immutable file or directory to fail `EPERM`, but rename of this
`0500` unflagged parent with immutable children to succeed. Runner then same-directory
`renameatx_np(RENAME_EXCL)` renames the parent to permanent `.invoked` while that directory itself
is still unflagged and immediately performs the first parent-dir fsync. Through the held parent
dirfd it opens the fixed name with exact `O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`, proves the
same inode, mode `0500`, exact two immutable record children, their schemas/hashes/flags, and final-
lease bindings, then uses only FD-level `fchflags(UF_IMMUTABLE)` on that gate dirfd. It fsyncs the
gate dirfd, same-FD fstats exact inode/mode/`st_flags`, performs a second parent-dir fsync, and
finally reopens/re-enumerates the same immutable gate inode and both immutable records.

The fixed gate name's first appearance consumes the public one-shot even before the first parent
fsync or self-flag phase finishes. A rename-before-fsync, post-first-fsync, reopen, flag, gate-fsync,
or second-parent-fsync crash permanently forbids lease cleanup and public retry. A complete schema/
hash/record-flag/final-lease-bound fixed gate missing only its directory `UF_IMMUTABLE` is a legal
post-rename state, but only `FINALIZATION_PENDING`/`NO_EXEC`: the same runner while holding its
original lease may finish the FD-only flag sequence, or after stable owner-death proof the fixed
finalizer may durably replay the first parent fsync, reopen the same inode, and perform only that FD-
flag/gate-fsync/second-parent-fsync/final-reopen tail. Neither path
may change content, execute, delete/move the gate, or authorize PASS while the self flag is missing.
Damaged/incomplete fixed-gate content is read-only blocked and is never repaired. After self-flag
completion, attempting to rename or move the fixed gate itself must fail `EPERM`.

The permanent gate therefore contains both
complete fsynced records and all PID/`birth_sha256`, snapshot, argv/cwd, completion, and source
bindings; `exec-authorized.json` is the permanent marker and is never written after publication.
The normal parent retains its original exclusive lock through all pre-gate work. That FD remains
`FD_CLOEXEC`; only after gate self-flag, gate fsync/recheck, second parent fsync, and final reopen
make immutability durable may runner create/persist the post-gate log, arm the alarm, and execute;
the later public
`execve` closes it. Only the formal listing-child fd 5 copy has CLOEXEC cleared, and only until its
CTest exec/process exits.

Only after the immutable fixed-gate proof completes does runner enter post-gate execution, and it
spawns no child. Through a held no-follow log-parent directory FD it
creates only the runner-owned log with `O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW` at mode `0600`. Before
installing it on stdout/stderr, alarming, or execing, the same open FD must verify regular type,
stable inode, runner uid, `nlink==1`, and mode `0600`, fsync the log file, fsync the log parent,
re-enumerate the exact basename through the held parent dirfd without following links, confirm the
same inode/type/uid/`nlink`/mode, and repeat same-FD `fstat`. It also fsyncs the complete gate. Any
failure is post-gate STOP/blocked and cannot reach alarm/exec; later `NO_EXEC` may rely on log
absence only because this durable same-inode claim is a mandatory predecessor. Runner then installs
that verified FD on stdout/stderr. This direct append/output log is not a temp-to-target publication
and is the sole intentional `O_WRONLY` creation exception; it is never renamed or same-FD hashed.

Every watchdog—listing startup, listing exec, and public 300-second execution—uses one exact
`normalizeAndArmAlarm(N)` primitive. It first calls
`pthread_sigmask(SIG_BLOCK, {SIGALRM})` and retains the returned old mask while SIGALRM is blocked;
only in that blocked state does it call `alarm(0)`. It reads `sigpending()`. If and only if SIGALRM
is present, it calls `sigwait({SIGALRM})` and requires the exact SIGALRM result (a standard signal
has at most one pending instance), then requires a second `sigpending()` to contain no SIGALRM. It
sets disposition `SIG_DFL`, calls `pthread_sigmask(SIG_UNBLOCK, {SIGALRM})`, queries the resulting
mask with `pthread_sigmask(SIG_BLOCK, empty-set)` without changing it, and requires SIGALRM absent.
Only then does it call `alarm(N)` and require
the returned previous remainder to be zero. It never consumes another signal. Any missing API,
exception, return/mask/pending mismatch, or SIGALRM still pending/blocked is failure. Because pending
SIGALRM is consumed while blocked before default+unblock, normalize cannot kill the process at the
unblock edge even when SIGALRM was inherited ignored, blocked, or pending+blocked.

Public runner invokes `normalizeAndArmAlarm(300)` only after the durable log claim; any failure
occurs after marker publication and is `EXEC_STOP`. Finally it revalidates the open
snapshot FD plus immutable parent and `os.execve`s the snapshot. Runner and binary retain one PID.

Runner-owned `O_EXCL` claims apply only to gate preparation/state/log/marker/snapshot. The unique
binary owns evidence root, evidence manifest, summary, completion temp, and completion record.
Final actual manifests bind lease-lock inode/uid/`nlink`/mode/size, ordered gate-state records,
observed post-gate log inode/uid/`nlink`/mode under the durable-claim contract, and,
for formal, child identity/state, inherited-fd/dual-watchdog/ACK policy, frozen child-state sibling
durability, LastTest directory chain, and PID/PGID/group proofs. They also bind each target's first
post-rename reopen, FD-only flag transition/final reopen, and the permanent gate directory's first-
rename/first-parent-fsync/same-inode reopen/FD-self-flag/gate-fsync/second-parent-fsync/final-reopen
proof.
Evidence/summary may be created and updated by that process before completion. After it closes and
fsyncs every evidence/summary file, it computes their bound hashes. It opens each actual-manifest
and completion fixed temp with the exact global `O_RDWR` flags, writes completely, fchmods `0400`,
same-FD fsyncs/rechecks with no temp flag, no-replace renames manifest first and completion second,
fsyncs/reopens each target, then FD-flags it immutable, same-FD fsyncs/rechecks `st_flags`, fsyncs
the parent again, and performs the final same-inode/hash/mode/flags reopen. The
completion binds PID, snapshot-binary hash, summary hash, and actual-evidence-manifest hash; after
publication it performs no sample write. Missing/invalid
completion always prevents PASS. Qualification never claims or depends on an OS exit status.
Candidate completion is fixed absolutely at
`/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/science_g0_prefetch/requalification-evidence-v6/candidate-completion.json`;
formal completion is fixed absolutely at
`/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/science_g0_prefetch/formal-evidence-v6/live-completion.json`.
Their fixed binary-owned temps append exactly `.tmp`; completion targets must already be durable
`0400` and immutable before finalization. After process death, a correct target missing only its
post-rename flag is `FINALIZATION_PENDING` and may be FD-flag-resumed, never treated as PASS.

The fixed finalizer first validates the fixed gate through a held parent dirfd and exact
`O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`, including same inode/mode, complete gate schema,
both immutable record hashes/flags, and final-lease bindings. A gate missing only its own
`UF_IMMUTABLE` is not damaged content: it is `FINALIZATION_PENDING`, never `RUNNING`/exec/PASS.
The original runner may complete only the prescribed FD flag sequence while it owns the lease. A
finalizer may do so only after the stable owner-death proof; it revalidates the same complete gate,
fsyncs the held parent first, reopens the same fixed inode, sets only directory `UF_IMMUTABLE`,
fsyncs/rechecks the gate FD, fsyncs the parent a second time, and final-reopens.
It does not chmod, rewrite records, create the log, execute, unlink, or move anything during this
repair. Successful repair with no durable post-gate log becomes finalized `NO_EXEC`. Missing/
invalid/damaged/replaced content, or absence after trusted observation/recording of the fixed name,
is immediate read-only `FINALIZATION_BLOCKED`; finalizer does not guess a PID or repair it. With
valid state it
calls `os.kill(pid, 0)`. `ESRCH` proves the original PID absent;
`EPERM` or any other error is pending. If the call succeeds, it runs the same fixed-env,
`shell=False` ps command and freshly hashes the canonical line: exact same digest is `RUNNING`,
while a different valid digest is only possible PID reuse. As in pre-gate recovery, it proves the
original dead only after second kill success, a second strict ps parse with the same new digest, and
third kill success; any exit/change/anomaly is pending. Launch/rc/ASCII/line/regex failure never
proves death. Raw birth text is not persisted by the gate or finalizer.
Here `RUNNING` is allowed only when the gate self flag was already complete; the same live identity
with a missing self flag remains `FINALIZATION_PENDING` for the original runner's flag-only tail.

After death proof, finalizer opens paths by trusted dirfd with `O_NOFOLLOW` and uses each same FD for
`fstat`, hash, and `fchmod`. It rejects symlink/special/owner/link/topology anomalies; mutable files
become `0400`, directories `0500`, while snapshot/wrappers remain `0500` and common Python `0400`.
After every chmod/flag change it same-FD fsyncs and rechecks the file or directory before moving to
its parent; every flag change uses only ctypes `fchflags` on that held FD. It then fsyncs parents
bottom-up and reopens/re-enumerates final presence/hash/size/mode/flags.
Each manifest/receipt fixed temp is opened with the exact global `O_RDWR` flags and written as
`magic+length+payload+checksum` through the unified primitive: complete bytes, same-FD
`fchmod(0400)`, post-chmod same-FD fsync and fstat/hash/mode/no-temp-flag recheck,
`renameatx_np(RENAME_EXCL)`, first parent fsync and same-inode target reopen, then target-FD
`fchflags(UF_IMMUTABLE)`, post-flag same-FD fsync/`st_flags` recheck, second parent fsync, and final
same-inode/hash/mode/flags reopen. Manifest publishes before receipt.

Publication uses only `renameatx_np(RENAME_EXCL)`; unavailable or failed no-replace rename remains
`FINALIZATION_PENDING` and never substitutes a hard link or replacing rename. Any simultaneous
target/temp collision is `FINALIZATION_BLOCKED`, even when their bytes match. A complete fixed temp
may resume only the final-mode/fsync/recheck/rename half after read-only validation; a target with
no temp and only its flag absent may resume only the FD-level flag half. Only a proven
finalizer-owned partial regular temp with matching dirfd/inode/uid, `nlink==1`, and magic prefix may
be removed and rebuilt. Any other collision/tamper is irreconcilable and never overwritten.

State evaluation is first-match and non-overlapping. Gate-absent work with a correctly named
partial prepare, held/contended lease lock, recoverable complete lease, or not-proven-dead owner is
`OFFLINE_PENDING`; a
legitimate write crash is never `OFFLINE_STOP`. Extra/malformed lease names, multiple prepare/final
entries, metadata tamper, or an irreconcilable public-path mismatch may be user-bound read-only
`OFFLINE_STOP`, bottom 0, with no cleanup or public invocation. After a gate exists use this priority:

| Priority | Candidate state | Exact predicate | Bottom | Effect |
|---:|---|---|---:|---|
| 1 | `FINALIZATION_BLOCKED` | Gate identity content is missing/invalid/damaged/replaced; fixed gate is absent after trusted observation/recording; a present log fails exact type/inode/uid/`nlink`/mode contract; or valid identity plus proved death exposes irreconcilable symlink/special/collision/tamper | 0..1 | Read-only `STOP`; no chmod, PID guess, public rerun, or fabricated manifest |
| 2 | `FINALIZATION_PENDING` | Complete fixed gate is missing only its self flag; original may still finish that exact phase, liveness is inconclusive, or proved-dead state has recoverable gate-self-flag/manifest/receipt publication incomplete | 0..1 | FD-only self-flag or offline finalizer retry; no log/exec/public retry |
| 3 | `RUNNING` | Self-flag-complete valid gate state; `kill(0)` succeeds; ps parses and freshly hashed birth equals recorded digest | 0..1 | Wait only; no chmod/publication |
| 4 | `NO_EXEC` | Finalization complete; original dead; valid permanent gate/marker and durable-log-before-alarm contract bind, but exact post-gate log is absent | 0 | `STOP` |
| 5 | `EXEC_STOP` | Finalization complete; original dead; marker and exact same-inode durable log exist; valid completion plus legal PASS proofs are absent | 0..1 | `STOP` |
| 6 | `COMPLETE_PASS` | Finalization complete; original dead; marker, exact durable log, and valid bound completion exist; summary/proofs are complete and legal | 1 | formal may be authorized |

Missing/invalid completion, artifact/event/parser/science/byte/latency failure, or incomplete proof
set is `EXEC_STOP`. Do not resample or rerun after the permanent gate.

Only complete candidate `PASS` authorizes a one-file `tests/CMakeLists.txt` promotion with exactly
two semantic edits: formal root v4 to `formal-evidence-v6`, and fixed absolute
`--completion-json /Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/science_g0_prefetch/formal-evidence-v6/live-completion.json`.
Profile `prefetch`, five iterations, and `--enforce-latency` remain unchanged, followed by exactly
one formal process. Formal uses the same public client-side envelope as candidate—raw
curl, context-correlated completion/decision events, VSINetworkStats, and the existing science
oracle—with no public server-log claim. After that process exits, every later CTest/regression gate
must explicitly exclude `osgVerse_Test_ScienceGdalLive` or name an exact offline whitelist; no
wrapper may pass `--live-cases` or write the formal path. The frozen process ledger must remain
exactly one formal invocation through G1/package handoff. Formal failure is an immutable `STOP`;
formal `GO` permits the already approved G1 plan. Only G0 `GO`, G1 `GO`, packaging audit, codesign,
and launch smoke permit an atomic update of the fixed Desktop app and a human-verification
checklist. Tag and sync remain a separate explicit user action.

Formal runner reads `CMAKE_CTEST_COMMAND` from the bound `CMakeCache.txt`, resolves and validates its
absolute realpath, then copies the CTest executable from an `O_NOFOLLOW` source FD into a fixed
runner-owned immutable `0500` execution snapshot. It likewise copies the generated
`CTestTestfile.cmake` from an `O_NOFOLLOW` source FD into a fixed immutable listing-build snapshot.
That test-dir is prebuilt with immutable/read-only root, `ctest`, `CTestTestfile.cmake`, and
`Testing` directory entries, plus the sole writable scratch directory `Testing/Temporary` at mode
`0700`. Before listing, scratch must be empty and its path/owner/inode/uid/`nlink`/mode are bound;
`LastTest.log` is absent/non-symlink. Source/destination inode/hash/mode and immutable flags are
bound before and after.

The formal final lease predeclares exactly one optional child-identity basename under itself:
`^.listing-child\.prepare\.(0|[1-9][0-9]{0,9})\.([1-9][0-9]{0,9})\.([0-9a-f]{64})$`.
Candidate lease enumeration rejects it. To run the sole listing, parent uses `shell=False`,
`close_fds=True`, and the exact minimal `pass_fds=(3,4,5,6)` to spawn the same fixed runner as
`--formal-listing-child`; bounded stdout/stderr are `Popen` pipe parameters rather than inherited
ambient descriptors. Parent keeps its original CLOEXEC lock FD and passes a dup of the same
locked open file description as fixed inheritable fd 5 plus verified lease dirfd 6; every unrelated
FD is CLOEXEC/closed. Child verifies fd 5 against the lease-bound lock inode/uid/mode and fd 6
against the final-lease directory inode/uid/mode. It clears only fd 5's `FD_CLOEXEC`; fd 6 remains
CLOEXEC after identity/state access, so the same lock description—but not the directory FD—survives
child-to-CTest exec until CTest exits. These minimal nonblocking FD checks precede all child alarm,
session, ps, filesystem, and pipe work. Before final CTest `execve`, fixed ps is the only permitted
child helper subprocess; any additional helper must use the same empty-pass-fds/private-stdio
contract or fail before launch.

Immediately after those checks, child calls `normalizeAndArmAlarm(15)` before `setsid`, ps, mkdir,
fsync, or any other possibly blocking operation. Only after successful arm does it send exact
one-byte `A` (`startup-watchdog armed`) on READY. It then calls `setsid()`, requires PID=PGID, obtains and
strictly hashes its own C/UTC ps birth using `shell=False, close_fds=True, pass_fds=()`,
`stdin=DEVNULL`, and bounded private stdout/stderr pipes so fd 3/4/5/6 cannot enter `/bin/ps`, then
atomically `mkdirat`s mode `0700`
`.listing-child.prepare.<uid>.<pid=pgid>.<birth64hex>` under the held final-lease dirfd, fsyncs that
parent directory FD, sends exact one-byte `R` (`identity ready`), and blocks for exact one-byte `G`.
Identity failure, wrong byte, or GO-pipe EOF exits without CTest. Once spawned, child attempts
startup arm and identity before checking parent/GO,
so parent death during startup still leaves identity unless identity creation itself fails, in which
case child exits without CTest. Thus a parent death before durable identity cannot have sent GO,
and a parent death after GO always leaves identity. From successful spawn until child/CTest exit,
inherited fd 5 keeps recovery's nonblocking flock from succeeding even before identity mkdir,
closing that race. Parent death before spawn has no inherited copy, so its lock releases normally;
parent death after spawn leaves fd 5 held until child/CTest exit. The directory is never removed on
normal flow.

Parent records monotonic `handshake_start` immediately before spawn and enforces one fixed
20-second deadline over ordered READY bytes `A`, then `R`, durable child-state publication, sending
`G`, and receiving final byte `E`. Missing, duplicate, reordered, extra, late, EOF, pipe error, or
child exit closes both READY/GO ends and waits for EOF/startup watchdog; parent does not guess or
signal an unknown child. The 15-second startup watchdog remains armed across setsid/ps/mkdir/fsync,
READY wait, parent validation, child-state publication, and GO wait, so it expires before the
parent's 20-second handshake ceiling.

After ordered `A` then `R`, parent requires exactly one matching directory, opens it
`O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`, holds that child dirfd through post-wait freeze, and revalidates
its stable inode, uid, `nlink==2`, mode `0700`, PID=PGID via `getpgid`, and freshly hashed ps birth against the name. It
also requires the process group to contain exactly that child. Parent then writes bounded framed
`child-state.json.tmp` with magic `osgsol.scienceearth.v6-listing-child.v1`, canonical schema and
checksum binding uid, PID, PGID, birth digest, parent runner identity, final-lease hash/inode,
lease-lock inode/fd inheritance, lease-dirfd identity/CLOEXEC policy, immutable CTest/listing snapshots, exact argv/cwd/env, pipe
protocol and `A/R/G/E` bytes, exact alarm normalization with startup/handshake/exec/listing deadlines
15/20/25/30 seconds and monotonic timeout origins, and scratch inode. It opens the temp with exact
`O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC`, writes complete bytes, fchmods `0400`, same-FD
fsyncs/rechecks with no temp flag, same-directory `renameatx_np(RENAME_EXCL)` renames to
`child-state.json`, fsyncs the child/final-lease dirfds, and reopens the same target inode/hash/mode.
It then target-FD `fchflags(UF_IMMUTABLE)`, same-FD fsyncs/rechecks `st_flags`, fsyncs child and final-
lease dirfds again, and performs a final same-inode/hash/mode/flags reopen. The initial lease schema prebinds this regex and state contract; this
durable child-state completes the formal final-lease state, and its hash/inode/PID/PGID/birth digest
must appear in gate runner-state and every manifest. Only then may parent send `G`.
After exact `G`, child reruns the full `normalizeAndArmAlarm(25)` primitive, then sends exact
one-byte `E` (`exec-watchdog armed`). Only after successful `E` does it close READY/GO FDs 3/4 and
confirm lease dirfd 6 is CLOEXEC; stdout/stderr remain bounded parent pipes and fd 5 is the sole
extra descriptor across CTest exec. Parent starts the independent 30-second listing timeout at the
monotonic instant it validates `E`, never at spawn or `G`. The default disposition and 25-second
kernel timer survive `execve` and expire before that parent deadline. Normalize or ACK failure exits
without CTest and is `PREFORMAL_PENDING`. If parent dies at any stage, startup or exec watchdog
still terminates a stuck child/CTest and releases inherited lock fd 5.

After GO, child keeps the same PID/PGID and directly `execve`s the immutable CTest snapshot with
argv
`[<ctest-snapshot>,--test-dir,<absolute-immutable-listing-build-snapshot>,-N,--show-only=json-v1,-R,^osgVerse_Test_ScienceGdalLive$]`
with fixed cwd `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety`, C/UTC environment,
30-second timeout, stdout `<=1 MiB`, stderr `<=64 KiB`, rc 0, and empty stderr. The executed program
is the snapshot, and CTest reads the snapshotted CTest file. Parent uses fixed
`[/bin/ps,-axo,pid=,pgid=,lstart=]` under C/UTC, `shell=False`, `close_fds=True`,
`pass_fds=()`, `stdin=DEVNULL`, bounded private pipes, timeout 5 seconds, rc 0, empty
stderr, and ASCII stdout `<=8 MiB`; every line must match the anchored numeric-PID/numeric-PGID/
canonical-lstart grammar and PIDs are unique. It uses these snapshots to audit that the group has
only the child before GO and at every bounded nonblocking wait loop
iteration (maximum 50 ms interval), with no extra member at all; `-N` must never spawn a test
process. After `waitpid` returns the recorded child, empty-group proof
requires `killpg(pgid,0)==ESRCH`, a fresh strict ps enumeration with zero matching PGID rows, and a
second `killpg==ESRCH`. Only then, through the held child dirfd, parent reopens
`child-state.json` with `O_NOFOLLOW`, requires same inode/hash/uid/`nlink==1`/mode `0400` plus
`UF_IMMUTABLE`, same-FD fsyncs and repeats fstat/hash/mode/flags checks. It fchmods the child
directory `0500`, requires no self flag, fsyncs/rechecks after chmod, then uses the ctypes FD-level
`fchflags` binding to add directory `UF_IMMUTABLE`, same-dirfd fsyncs/rechecks `st_flags`, fsyncs the
parent final-lease dirfd, and no-follow reopens/re-enumerates child directory and state to require
the same directory inode/flags and same state inode/hash/mode/flags. Only this sibling durability proof permits
preformal continuation; any failure is `PREFORMAL_PENDING` with no gate.
Recovery never signals an unknown or surviving child; it waits for lock release and then repeats
the strict child/PID/PGID death proofs.
Any child SIGALRM/other signal, nonzero status, parent timeout, or nonempty stderr is
`PREFORMAL_PENDING` and no gate; listing is never repeated within that lease.
An offline fault fixture wedges the child's external `/bin/ps` until the 15-second SIGALRM kills the
child. The fixture proves the ps process inherited none of fd 3/4/5/6, especially lease-lock fd 5,
and that `flock(LOCK_EX|LOCK_NB)` eventually succeeds after the child dies even if the external ps
has not yet exited. PGID cleanup still waits for the strict empty-group proof and never kills an
unknown process.

Gate-absent final-lease recovery treats an empty child identity or partial `child-state.json.tmp` as
recoverable from the name. The only legal contents are empty; one exact-open `0600` temp whose
bytes are any bounded valid frame prefix (including a complete frame before chmod); one unflagged
`0400` temp only when post-chmod same-FD fsync/recheck, the entire frame/schema/checksum, and all
lease/child/snapshot/fd/scratch plus watchdog/ACK/deadline cross-bindings pass (pre-rename); one
equally complete verified `0400` target missing only its post-rename flag; or the same target with
verified `UF_IMMUTABLE`. Temp and target may never coexist. The missing-target-flag state is
PENDING and may resume FD-flagging only under owner-death/lock proof. The directory must retain
name-bound uid, stable inode,
`nlink==2`, and mode `0700`; after verified target/death/group proof, mode `0500` without the
directory flag is a legal post-chmod recovery window and `0500+UF_IMMUTABLE` is the complete sibling
state. Any file is
regular, same uid, stable inode, `nlink==1`, and bounded `<=64 KiB`. If child identity exists,
recovery must independently prove both
runner and recorded/name child PID/birth dead using the stable kill/ps rules, then prove the entire
PGID empty with the killpg/strict-ps/killpg sequence. Alive/`EPERM`, extra group members, PID/PGID or
birth/group change, timeout/rc/stderr/size/ASCII/line/duplicate/parse anomaly, or metadata/schema drift is PENDING/STOP and deletes
nothing. A live child keeps listing scratch and every snapshot intact even when runner is dead.
Only after both identities and the group are dead may exact allowlisted cleanup proceed. If no
child identity exists, the GO invariant proves no listing exec was authorized; a pre-identity child
can only observe EOF and exit, while its inherited lock blocks cleanup until that exit. The
permanent gate still disables all lease cleanup.

Post-listing, the only permitted scratch entry is exact regular non-symlink
`Testing/Temporary/LastTest.log`; no wildcard is allowed. It must have the runner uid, `nlink==1`,
size `<=256 KiB`, valid bounded UTF-8 text, and content proving listing-only/no test start, live
command, request, evidence, or completion. The runner template carries a finite anchored line
grammar for the bound CTest json-v1 listing log and rejects every nonmatching line.

Using the held scratch dirfd, runner opens `LastTest.log` once with
`O_RDWR|O_NOFOLLOW|O_CLOEXEC`; that same FD supplies regular-type/inode/uid/`nlink==1`/size/content
validation and raw SHA-256. It fchmods `0400`, fsyncs the file, and repeats same-FD fstat/hash. It
then adds file `UF_IMMUTABLE` via FD-level `fchflags`, same-FD fsyncs/rechecks `st_flags`, and fsyncs
the scratch directory. It fchmods scratch to `0500`, requires no self flag, fsyncs/rechecks after
chmod, then FD-level `fchflags` adds scratch `UF_IMMUTABLE`, followed by same-dirfd fsync/`st_flags`
recheck and `Testing` dirfsync. Through the held `Testing` dirfd it reopens scratch with
`O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`, requires the same inode/uid/`nlink`/mode/flags, enumerates exactly
`LastTest.log`, and reopens that file no-follow to require the same inode/hash/uid/`nlink`/mode/flags.

Runner then fsyncs bottom-up every held directory in the actual chain: reopened scratch,
`Testing`, immutable listing root, every intervening no-follow parent through `.superpowers/sdd`,
and the final lease after recording this binding; the already proved child-state sibling chain is
part of the same gate-predecessor tuple. It reopens/re-enumerates the entire chain from
held parent dirfds and reconfirms every path's inode/type/uid/`nlink`/mode/flags plus final log hash.
Only this complete durability/freeze chain may enter preformal PASS or gate preparation. Any file
fsync, scratch fsync/freeze, per-level fsync, reopen, enumeration, or recheck failure is
`PREFORMAL_PENDING` with no gate. Missing/truncated LastTest from a legitimate crash remains
recoverable PENDING, never permission to publish; unexpected extra/symlink/hard-link/special or
invalid-content tamper is STOP. Listing still requires rc 0 and empty stderr. Gate state, authorizer
tuple, and all manifests bind the log hash and full directory inode/mode/flag chain.

Listing stdout is UTF-8 JSON without BOM, duplicate keys, NaN/Infinity, trailing bytes, or excess
depth/count/string lengths. Strict json-v1 validation requires root `kind=ctestInfo`, version major
1, bounded `backtraceGraph`, and exactly one `tests` item named
`osgVerse_Test_ScienceGdalLive` with one string-only `command` array and a single effective
`WORKING_DIRECTORY`; unknown/missing/type-invalid fields fail. Runner derives the canonical
length-bounded binary/argv/cwd object only from this JSON and cross-parses only the immutable
CTestTestfile snapshot. A second listing is forbidden. Any snapshot/JSON/pre-post binding failure
before gate is `PREFORMAL_PENDING`.

Runner does not execute a verifier path. It opens independent tracked
`tests/science_v6_authorizer.py` with `O_NOFOLLOW`, verifies same-FD `fstat`/hash against the literal
expected SHA in the independently bound runner template, compiles and executes those verified bytes
in an isolated namespace, then directly calls fixed
`verify_preformal(canonical_tuple) -> <64 lowercase hex digest>`. The tuple is never argv, env, or
temp data. It binds child-identity name/inode, child-state hash/schema, PID/PGID/birth digest,
`A/R/G/E`, normalized dual-watchdog/deadline, proved-dead child and empty-group proofs, the frozen
child-directory/state sibling durability proof, plus the LastTest/scratch/Testing/listing-root
durability chain, exact temp-open/post-rename-FD-flag contracts, and the permanent gate's unflagged-
prepare/first-rename/first-parent-fsync/same-inode reopen/FD-self-flag/gate-fsync/second-parent-fsync/
final-reopen contract, plus existing snapshots. The independently bound authorizer source and
runner-template SHA contract both include this exact gate transition; changing it requires the
Task 3 authorization-bound runner-template SHA and all state/manifest schema expectations to change
together, while the separate embedded verifier-source SHA check remains mandatory.
Runner independently canonicalizes and recomputes the digest. Any source/listing/API/
digest/snapshot/path failure before gate is `PREFORMAL_PENDING`, invocation 0, and safely retryable;
correctly named empty/truncated prepares from legitimate crashes remain PENDING/recoverable. The
user may bind exact lease/snapshot/scratch presence plus unchanged public-path absence as
`PREFORMAL_STOP` only for malformed/multiple lease names, metadata tamper, unexpected entries, or
an irreconcilable complete-lease/public-path mismatch.

Only after PASS does formal runner perform the full fixed-gate rename, two parent fsyncs, FD self-
flag, gate-fsync/recheck, and final-reopen proof; only then may it use the same marker, durable same-
inode log claim, completion path, immutable snapshot, and `os.execve` protocol. No live CTest
is executed.

Before candidate qualification, `NOT_AUTHORIZED` means common runner/finalizer and both fixed
wrappers exist at their bound modes, while formal offline snapshots, gate/log/state/manifest/
receipt/output/summary/temps are absent and formal public invocation is 0.
`PREFORMAL_PENDING`/`PREFORMAL_STOP` bind actual immutable offline-snapshot presence/absence but
preserve gate/log/state/output/completion/manifest/receipt absence. Once the formal gate exists, the same
priority applies: `FINALIZATION_BLOCKED`; `FINALIZATION_PENDING` for a complete gate missing only
self flag or another recoverable phase; `RUNNING` only with a self-flag-complete gate; finalized `NO_EXEC`;
finalized `EXEC_STOP` when marker/log exist without valid completion/PASS; or `COMPLETE_PASS` only when
marker, log, valid snapshot-bound completion, summary, and formal proofs all bind. `NO_EXEC` means
the valid gate/marker and durable-log-before-alarm contract bind but the exact post-gate log does
not. Bottom ranges remain
0 for `NO_EXEC`, 0..1 for blocked/pending/stop, and 1 for PASS.

The Task 3 authorization commit contains independent pure-API verifier source and, outside that
source, exact runner/finalizer/wrapper templates. Clean-HEAD authorization binds the commit tree and
each template hash separately. Runner template hashes cover completion CLI/schema, CTest
snapshot/scratch/json-v1, independent verifier literal/API, pre-gate lease recovery, the exact
fixed-gate post-rename FD-self-flag/two-parent-fsync/final-reopen transition, alarm reset,
state-obstruction, and atomic-recovery rules. Authorization
requires all candidate/formal process paths absent/non-symlink. Preformal requires frozen candidate
`COMPLETE_PASS`, all common/wrapper/snapshot hashes and modes, the fixed verifier API/digest,
exact json-v1 listing/binary/argv/cwd from immutable CTest snapshots, formal public-path absence,
and the exact two-semantic-edit one-file promotion commit. That commit has authorization HEAD
as parent and fixed trailers binding candidate actual manifest, summary, wrapper, log, and canonical
gate SHA-256 values. No future decision auditor supplies a missing preformal condition.

## Allowed files and commit boundaries

No implementation task may broaden its scope without a new design review.

1. Design commit: only this design and
   `docs/superpowers/plans/2026-07-14-scienceearth-g0-v6-head-retry-attribution-plan.md`.
2. RED contract commit: only `tests/science_curl_remove_fault.cpp`,
   `tests/science_http2_range_server.mjs`, `tests/science_gdal_network_test.cpp`, and
   `tests/science_deps_script_tests.sh`.
3. Production commit: only
   `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch` and
   `packaging/science_deps/versions.env`. Task 2 cannot modify tests; an interface mismatch returns
   to a separate Task 1 test-only correction and review before production resumes.
4. Offline authorization commit: only `tests/science_v6_authorizer.py`,
   `tests/science_v6_authorizer_tests.py`,
   `docs/scienceearth/gdal-build.md`, `docs/scienceearth/g0-measurements.md`, and
   `docs/scienceearth/g0-g1-baseline.md`.
5. Conditional formal promotion commit after candidate `PASS`: only `tests/CMakeLists.txt`.
6. Candidate/formal decision binding commit: only the two G0 decision documents above, plus
   `gdal-build.md` only if a newly measured private-prefix binding belongs there.

No application source, app resources, G1 source, Desktop bundle, v4 evidence, or v5 evidence is an
allowed v6 G0 implementation file.
