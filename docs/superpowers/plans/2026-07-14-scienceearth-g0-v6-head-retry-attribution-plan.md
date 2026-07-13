# ScienceEarth G0 v6 HEAD Retry and Transport Attribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` to execute this plan task by task. Every task receives
> a fresh implementer, then a fresh specification reviewer, then a fresh code-quality reviewer.
> Use `superpowers:test-driven-development`, `superpowers:systematic-debugging`, and
> `superpowers:verification-before-completion` at their required boundaries. Do not advance past a
> review with an unresolved Critical or Important finding.

**Goal:** Recover exact-five transient coordinator HEAD responses before Range recovery, replace
ambiguous HTTP/2 response attribution with authoritative per-handle completion evidence, and carry
the unchanged ScienceEarth gates through one v6 decision to a human-verifiable fixed Desktop app.

**Architecture:** The existing exact-path private-GDAL coordinator keeps its initial concurrent
HEAD/Range pair, retries HEAD first on the same easy/multi connection pool, then retries Range only
if needed. Fixed `ScienceTransport: response-v1` events make every attempt role-attributable;
current live proofs require complete v6 events and never infer roles from anonymous headers.

**Tech stack:** C++17, private static GDAL 3.13.1 patch, Apple/libcurl 8.7.1 multi/HTTP2, Node.js
TLS HTTP/2 fixture, test-only curl interposition, CMake/CTest, Bash/Python audit tooling, macOS
Mach-O/codesign packaging.

## Global constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on
  `codex/science-earth-g0-g1`. The v6 design base is clean commit `c555a0f`.
- Never edit, chmod, delete, recreate, rerun, or use as output any v4 or v5 evidence. Bind their
  exact hashes, topology, and `0400/0500` modes read-only.
- Preserve the v5 decision exactly: `G0_DECISION=STOP`,
  `PUBLIC_REQUALIFICATION_V5=FAIL`, `DESKTOP_PACKAGE=NOT_READY`.
- Before v6 authorization, each of these must be absent and non-symlink:

  ```text
  build/science_g0_prefetch/requalification-evidence-v6/candidate
  build/science_g0_prefetch/requalification-evidence-v6/candidate-summary.json
  build/science_g0_prefetch/formal-evidence-v6
  ```

- Retry codes are exactly `429,500,502,503,504`; maximum retry is `3`; initial delay is `0.1`
  seconds. Status 408, broad 5xx matching, redirect, HTTP/1, and curl transport errors are excluded.
- HEAD exact-five recovery uses the same HEAD easy and same multi handle. Remove/re-add preserves
  access to the multi connection pool per
  [`curl_multi_remove_handle`](https://curl.se/libcurl/c/curl_multi_remove_handle.html),
  [`curl_multi_add_handle`](https://curl.se/libcurl/c/curl_multi_add_handle.html), and
  [`libcurl-multi`](https://curl.se/libcurl/c/libcurl-multi.html); runtime equality with the initial
  Range `CURLINFO_CONN_ID` is still mandatory.
- Exact-five exhaustion/failure blocks the operation and never enters ordinary HEAD fallback.
  Existing nontransient compatibility behavior alone remains eligible for its existing fallback.
- Preserve exact-path activation/token lease, strict 206/Content-Range/body validation,
  two-attempt detach bound, multi abandonment, retained callback lifetime, and handler-wide latch.
- One mutex-protected operation-local `EvidenceContext` is shared across coordinator, multirange,
  and ordinary-head work for the exact `(path, nonempty token)` lease. It owns contiguous ordinal/
  request allocation and outlives every retained callback; it never emits for unrelated traffic.
- Baseline/optimized, option-absent/global-only/missing-token, unrelated GDAL, application, AI,
  research, terrain, 3D Tiles, photo, satellite, panel, and camera behavior stays unchanged.
- `/Users/USER/Desktop/osgSol Earth.app` remains untouched until formal G0 `GO`, G1 `GO`,
  package audit, codesign, and launch smoke all pass. Do not create a differently named app.
- Do not tag, push, publish, or modify another repository. Tag/sync remains a separate explicit
  user action after human testing.
- After the one formal v6 process exits, every remaining Task 4/5 CTest command must use an exact
  offline whitelist or `-E '^osgVerse_Test_ScienceGdalLive$'`; every wrapper must be proven unable
  to pass `--live-cases` or write the formal path, and the formal process ledger must remain 1.

## File map and commit scope

| Deliverable | Allowed tracked files | Planned commit |
|---|---|---|
| v6 design | the two v6 spec/plan documents | `docs(scienceearth): plan v6 head attribution` |
| RED contract/parser | `tests/science_curl_remove_fault.cpp`, `tests/science_http2_range_server.mjs`, `tests/science_gdal_network_test.cpp`, `tests/science_deps_script_tests.sh` | `test(scienceearth): expose v6 head attribution gaps` |
| private runtime | `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`, `packaging/science_deps/versions.env` only | `fix(scienceearth): recover transient head before range` |
| offline authorization | `tests/science_v6_authorizer.py`, `tests/science_v6_authorizer_tests.py`, and the three authorization docs | `test(scienceearth): authorize v6 requalification` |
| conditional formal promotion | `tests/CMakeLists.txt` only after candidate `PASS` | `build(scienceearth): promote v6 formal evidence` |
| public decision binding | the two G0 decision docs; `gdal-build.md` only for a newly measured prefix binding | `docs(scienceearth): preserve v6 decision` |

No design worker commits the first row until the root worker accepts both documents. No other file
is allowed without a new design review.

---

## Task 1: Build the RED transport-attribution and HEAD-first contract

**Files:**

- Modify: `tests/science_curl_remove_fault.cpp`
- Modify: `tests/science_http2_range_server.mjs`
- Modify: `tests/science_gdal_network_test.cpp`
- Modify: `tests/science_deps_script_tests.sh`

**Interfaces:**

- Produces parser mode `enum class AttributionMode { AttributedV6, LegacyFrozen };`.
- Produces exact `ScienceTransportCompletion` fields matching `response-v1` in the design.
- Produces local server records keyed by exact opaque correlation header plus
  `method/range/stream/session`; no order/status heuristic or test-only production switch.
- Produces test-binary-only CLI `--completion-json <fixed-absent-path>` and schema
  `osgsol.scienceearth.v6-completion.v1`; this is not a GDAL/production-patch interface.
- Does not modify the private patch or prefix.

- [ ] **Step 1: Add synthetic attribution tests and observe parser RED**

Add a pure replay builder for anchored fixed-format events. The exact line shape under test is:

```cpp
static const std::regex responseV1(
    R"(^VSICURL: ScienceTransport: response-v1 ordinal=([0-9]+) )"
    R"(context=([0-9a-f]{32}) )"
    R"(scope=(coordinator|multirange|ordinary-head) role=(head|range) )"
    R"(request=([0-9]+) attempt=([1-4]) method=(HEAD|GET) )"
    R"(range=(none|bytes=[0-9]+-[0-9]+) curl=([0-9]+) status=([0-9]+) )"
    R"(http=([0-2]) redirects=([0-9]+) connection=(-1|[0-9]+) )"
    R"(content-length-count=([0-9]+) content-length-valid=([01]) )"
    R"(declared-content-length=([0-9]+) content-range-count=([0-9]+) )"
    R"(content-range-valid=([01]) content-start=(-1|[0-9]+) )"
    R"(content-end=(-1|[0-9]+) content-total=(-1|[0-9]+) )"
    R"(actual-body-bytes=([0-9]+)$)");
```

Construct a v5-shaped HEAD 500/Range 206/fallback replay, its HEAD 200/Range 500 mirror, and two
same-status out-of-order siblings. Require role-correct counts and semantic `FAIL` for the fallback,
not parser `ERROR`. In `AttributedV6`, reject missing, duplicate, malformed, noncontiguous,
reordered, unknown, trailing, role-swapped, sibling-Range, attempt, byte/header, protocol,
redirect, connection, retry-order, raw-multiset, server-oracle, fallback/publication, and false
HEAD-to-Range mutations listed in the design. `LegacyFrozen` must be explicit and ineligible for v6.
Require the five legal scope/role/method/range tuples from the design and reject all other tuples;
in particular, accept `ordinary-head/head/GET/none` only as a zero-writer-body physical GET.
Add a positive replay with two forcibly interleaved contexts, each independently starting at
ordinal 1. Reject missing/wrong context, cross-context decision events, and a raw correlation header
whose `context/request/attempt` differs from its completion event.

Run:

```bash
cmake --build build/science_g0_prefetch --target \
  osgVerse_Test_ScienceHttpRanges osgSol_Test_CurlRemoveFault --parallel 8
build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges
```

Expected RED before parser changes: at least the v5-shaped role attribution or a required mutation
fails because current parsing assigns anonymous response blocks heuristically.

- [ ] **Step 2: Implement the strict test-side parser and make synthetic replay GREEN**

Parse `response-v1` only when `AttributionMode::AttributedV6` is explicitly selected. Partition by
context first; then require ordinal `1..N` in logged order for each context. Enforce unique
`(context,scope,request,attempt)`, stable request identity, legal tuple, and event-before-retry/
fallback/publication chronology. Every retry/fallback/block/publication event must carry and match
the same context. Reconcile raw outgoing method/Range/correlation headers one-to-one, raw incoming
tuples only as a multiset, and local fixture server records by exact correlation ID.

Record these proof fields without changing existing Range aggregates:

```cpp
std::vector<HeadRetryEvidence> headRetries;  // scheduled transitions only
int coordinatorHeadTransientRetryCount = 0;
std::uint64_t coordinatorHeadTransientRetryDeclaredBytes = 0;
std::uint64_t coordinatorHeadTransientRetryActualBodyBytes = 0;
std::map<int, int> coordinatorHeadTransientRetryCodes;
int scienceTransportResponseCount = 0;
std::string scienceTransportAttribution = "response-v1";
```

Each head retry row contains `retryOrdinal`, `request`, `failedAttempt`, `scheduledAttempt`,
`delayMs`, and failed-completion status/connection/HTTP/declared/actual fields. Require
`scheduledAttempt == failedAttempt + 1`; completion attempts are `1..4`, while scheduled retry
ordinals are only `1..3`. Success after `k` retries has `k+1` completions and `k` rows; exhaustion
has four completions and three rows, with terminal attempt 4 never scheduling attempt 5.

Method HEAD requires actual body 0; declared length is separate and excluded from GET budgets.
Strict GET 206 requires exact bounded writer/Content-Length/Content-Range equality. An exact-five
GET may omit Content-Range; if present it must be one valid noncontradictory line, while its bounded
error body is retained and counted. Require method-HEAD requests/completions/stats to agree;
`ordinary-head/head/GET/none` is excluded from that formula and increments physical GET stats.
Run the binary again. Expected: all synthetic attribution and mutation tests pass; live local v6
coverage is still absent.

- [ ] **Step 3: Add local HTTP/2 HEAD-first recovery cases**

Extend the server with parameterized HEAD exact-five-once and exhaustion modes, simultaneous
HEAD/Range transients, both completion orders, and independent correlated server records. Every
request carries
`X-OSGSol-Science-Correlation: <32-lower-hex>/<request>/<attempt>`; the fixture joins on that
value and rejects missing/duplicate IDs. The header is unconditional for the production exact-path
operation and contains no path/token/credential, so no production test switch is introduced.
For each exact-five-once code require HEAD 2, Range 1, one session/connection, no duplicate first
Range, head retry 1, fallback 0, and publication 1. Require initial+3 retries for exhaustion and no
ordinary fallback/publication. Simultaneous transient requires HEAD retry completion before Range
retry emission. Parameterize transient Range with Content-Range absent and one strict matching line
as eligible; duplicate, malformed, spoofed, contradictory, oversized, or unaccounted error bodies
must block before HEAD retry.

Add context-lifetime cases: coordinator, multirange, and ordinary-head on one `(path, token)` share
one contiguous mutex-protected ordinal/request space across threads; a different path/token starts
an independent context and is filtered from the first proof. Persistent detach failure must retain
the event context with callback storage through abandonment, set the latch before later custom
setup, and show zero use-after-free/double-release under the existing fault interposer.
Add a deterministic sink race: pause thread A after it obtains the next ordinal but before enqueue,
let thread B contend, then release A. The log must still contain A before B within that context;
neither thread may hold the evidence mutex while calling libcurl or a reentrant callback.

HEAD exact-five admission is parameterized separately from Range: accept Content-Length count 0
and one valid declared length, always with `CURLE_OK`, redirect 0, HTTP/2, same valid connection,
body 0, and Content-Range count 0. Reject duplicate/malformed Content-Length, any Content-Range,
nonzero writer body, curl error, redirect, HTTP/1, or connection mismatch before `CanRetry()`. After
retry, final HEAD 200 requires exactly one valid positive Content-Length; a later good attempt must
not wash out an invalid initial attempt.
Add `500 -> 400`, `500 -> 404`, and `500 -> 405`: after initial exact-five ownership, each must emit
the second completion and terminal `retry-status`, with one scheduled retry, zero publication, zero
ordinary fallback, and no header-only GET. Preserve a separate initial-405 compatibility case.

Extend the interposer only for deterministic add/remove/perform/getinfo ownership faults. It must
forward real libcurl first, consume a thread-safe one-shot test request, and expose no production
environment or `PREFETCH_TEST_*` switch.

- [ ] **Step 4: Lock patch source contracts and observe runtime RED**

The shell contract requires:

```text
ScienceTransport: response-v1
head-transient-retry
head-transient-retry-blocked
CPLHTTPRetryContext oHeadRetryContext
curl_multi_remove_handle
curl_multi_add_handle
GetValidatedRetryDelay
429,500,502,503,504
```

It also requires the HEAD reset callbacks/buffers, event before retry/fallback/publication, same
easy/multi re-add, strict connection equality, independent HEAD/Range budgets, exact-five terminal
block, cpp+class-header patch hunks, and absence of 408 or production fault controls. The anchored
HEAD retry event must contain `context`, `retry`, `request`, `failed-attempt`, `scheduled-attempt`, failed
`status/connection/http/declared-content-length/actual-body-bytes`, and `delay-ms` in fixed order.
Every Range retry, immediate retry, fallback, block, and publication source event must also include
the same context field.

Add CLI/atomic/mutation tests proving missing/duplicate `--completion-json`, relative/unapproved or
pre-existing/symlink target, interrupted temp, duplicate publish, checksum/hash mismatch, and any
attempted evidence/summary mutation after completion all fail. The success case closes/fsyncs the
evidence set and summary, then publishes actual evidence manifest and PASS record through the
shared final-mode-before-fsync, rename-only primitive. The PASS record binds PID,
execution-snapshot binary hash, summary hash, and evidence-manifest hash; no sample write follows
that publication.

Run:

```bash
bash tests/science_deps_script_tests.sh
ctest --test-dir build/science_g0_prefetch \
  -R 'ScienceHttpRanges|ScienceHttpRangeServer' --output-on-failure
```

Expected RED: current patch lacks `response-v1` and HEAD retry; the v5-shaped local case takes
`fallback=head-invalid` or duplicates the first Range.

- [ ] **Step 5: Commit the exact RED scope and obtain reviews**

```bash
git diff --check
git diff --name-only | sort
git add tests/science_curl_remove_fault.cpp tests/science_http2_range_server.mjs \
        tests/science_gdal_network_test.cpp tests/science_deps_script_tests.sh
git commit -m "test(scienceearth): expose v6 head attribution gaps"
```

Fresh specification review checks every design mutation and true RED. Fresh quality review checks
parser fail-closed behavior, regex anchoring, integer bounds, test interposer safety, server oracle
identity, and absence of flaky time/status inference. Fix findings in the same task and repeat both
reviews.

---

## Task 2: Implement HEAD-first retry and authoritative completion events

**Files:**

- Modify: `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`
- Modify: `packaging/science_deps/versions.env`

Task 2 is forbidden from modifying tests. If the reviewed RED interface cannot compile against the
implementation, stop Task 2 and return to a separate Task 1 test-only correction/review commit;
then restart Task 2 from a clean tree. No conditional test file may remain dirty or be staged with
the production commit.

**Interfaces:**

- Consumes `response-v1`, `HeadRetryEvidence`, exact-five fixture, and fault hooks from Task 1.
- Produces same-easy/same-multi HEAD retry, HEAD-first simultaneous recovery, fixed completion events,
  and a newly pinned private prefix.

- [ ] **Step 1: Add the operation-local context and completion emitter inside the private patch**

Create one ref-counted `EvidenceContext` with the exact `(path, nonempty token)` lease and unique
32-lower-hex opaque ID. Its mutex covers request-ID allocation through correlation-header
serialization/registry insertion, and completion ordinal allocation through complete event-line
serialization and ordered sink enqueue. Retry/fallback/block/publication uses that same queue.
Release the mutex before libcurl, CPL callbacks, sleep, hooks, or other reentrant paths; drain the
serialized FIFO without reordering. Coordinator, multirange, and ordinary-head handles retain the
context through callback completion; abandoned attached-state storage also retains it until safe
teardown. Filter emission by that context and never use process-global or thread-local attribution
state. Add the opaque correlation header for every associated request; never serialize path, token,
URL credentials, or a test flag.

Patch `port/cpl_vsil_curl.cpp` with an internal attempt snapshot whose serializer emits fields in
the exact fixed order. Emit once per known completed handle, before any decision event:

```cpp
EmitScienceTransportResponse(
    nOrdinal++, osContextId, "coordinator", "head", nHeadRequestId, nHeadAttempt,
    "HEAD", "none", eHeadCurlCode, nHeadStatus, nHeadHttpMajor,
    nHeadRedirects, nHeadConnectionId, sHeadHeaders, nHeadActualBodyBytes);
```

Range and immediate multi-range call the same helper with role `range`, stable request ID, attempt,
exact Range, and independent writer bytes. A 206 takes the strict Content-Length/Content-Range/body
branch; an exact-five error takes the separate bounded-body branch where Content-Range may be absent
but cannot be duplicate, malformed, or contradictory. Instrument the existing nontransient outer
compatibility request as `ordinary-head` while the exact path/token lease is active. Its
header-only GET tuple is `ordinary-head/head/GET/none`, actual writer body 0, and physical GET—not
HEAD—statistics. Reject every tuple not listed in the design and emit nothing for unrelated paths.

- [ ] **Step 2: Add an independent bounded HEAD retry context**

After both initial handles complete, validate Range retention and same HTTP/2 connection before
calling HEAD `CanRetry()`. The failed HEAD itself must first be `CURLE_OK`, redirect-free HTTP/2 on
that valid connection, actual body 0, Content-Length count 0 or one valid declaration, and
Content-Range count 0; duplicate/malformed/conflicting headers fail before retry classification.
Final HEAD 200 requires exactly one valid positive Content-Length. Implement this ordered state
sequence; the names below are state labels, not claimed production function signatures:

```cpp
failed HEAD completion event
-> exact-five + CanRetry classification
-> validate finite/nonnegative/integer/steady-clock delay
-> detach same HEAD easy, at most twice; abandon safely on persistent attachment
-> emit scheduled retry row (failed_attempt, scheduled_attempt, delay_between)
-> wait the validated delay; cancellation cleans detached state without re-add
-> reset attempt buffers/getinfo/error state and rebind callbacks/userdata
-> re-add same HEAD easy to the same multi
-> perform through the next physical completion
-> emit that completion before the next retry/block/publication decision
```

Set sticky `headRecoveryOwned` when the admitted initial exact-five HEAD enters this sequence.
Thereafter only a strict valid 200 succeeds; another exact-five may use remaining budget; every
other response status, including 400/404/405, terminates with reason `retry-status`. No later branch
may call ordinary compatibility fallback. Only an initial non-exact-five HEAD may use the existing
405/header-only GET path.

The real code must preserve the patch's return/ownership conventions rather than introduce
exceptions. Reset occurs after the wait and before re-add; stale failed buffers remain detached and
cannot receive callbacks during the wait. Reset header/body/error/getinfo state exactly as the
design specifies. Require retry HTTP/2 and connection ID equal to the initial Range. Call
network-stat HEAD accounting exactly once per physical method-HEAD request; ordinary header-only
GET uses GET accounting. On exhaustion, physical attempt 4 emits completion plus terminal evidence
but no scheduled retry row.

- [ ] **Step 3: Sequence simultaneous recovery and terminal blocking**

Retain a strict successful Range unchanged while HEAD retries. If Range is an exact-five response
with bounded retained error body and optional-but-noncontradictory Content-Range, defer its existing
retry until HEAD reaches valid 200. Then reuse the Range easy/multi path and perform joint
size/206/Content-Length/Content-Range/body/connection validation. Exact-five exhaustion or any
ownership/transport failure sets the operation block, publishes nothing, and cannot reach ordinary
fallback. Nontransient 405 and other existing compatibility cases retain their current path.

Production publication uses only its local completed snapshots and ownership/state invariants.
Raw/event/statistics/oracle reconciliation is performed later by the proof builder and can reject
qualification, but is not a runtime publication dependency. Offline fixture qualification includes
the correlation-joined local server oracle; public live qualification has no server log and instead
uses raw/context events/statistics plus the existing RGB/georeference/NoData science oracle. Do not
add a public proxy, provider-log dependency, or network scope.

Update `port/cpl_vsil_curl_class.h` within the patch only for state/lifetime declarations required
by operation block or retained attached callback storage. Keep two detach attempts, one abandon,
handler latch before future multi acquisition/setopt, and no cleanup/rebind of attached state.

- [ ] **Step 4: Update the pin, rebuild from local archives, and prove source identity**

Set `GDAL_PREFETCH_PATCH_SHA256` to the exact new patch digest. Before rebuilding, require all three
archives already exist in `build/science-deps-prefetch/downloads` and pass `checksums.txt`; do not
download. Delete only build-owned roots after verifying `.science-deps-owned` and real paths.

Run:

```bash
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --build --jobs 4
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --verify
cmake --build build/science_g0_prefetch --target \
  osgVerse_Test_ScienceGdalSpike osgVerse_Test_ScienceHttpRanges \
  osgSol_Test_CurlRemoveFault --parallel 8
```

In a new temporary directory, extract the pinned `gdal-3.13.1.tar.gz`, verify its SHA-256, apply
`disable-shapelib`, `relocatable-static`, and `parallel-head-range` in that order, then require:

```bash
cmp "$fresh/gdal-3.13.1/port/cpl_vsil_curl.cpp" \
    build/science-deps-prefetch/src/gdal-3.13.1/port/cpl_vsil_curl.cpp
cmp "$fresh/gdal-3.13.1/port/cpl_vsil_curl_class.h" \
    build/science-deps-prefetch/src/gdal-3.13.1/port/cpl_vsil_curl_class.h
```

Expected: patch/pin/archive verification and both byte comparisons pass.

- [ ] **Step 5: Run GREEN and protected regression tests**

```bash
bash tests/science_deps_script_tests.sh
ctest --test-dir build/science_g0_prefetch --output-on-failure \
  -R 'ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer'
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
```

Expected: all exact-five HEAD cases, exhaustion, simultaneous transient, invalid transport/delay,
ownership/latch, strict event parser/replay/mutations, coordinator Range, immediate multi-range,
Content-Range, global-only, missing-token, and option-absent regressions pass with zero skips.

- [ ] **Step 6: Commit and obtain task plus whole-runtime reviews**

```bash
git diff --check
git add packaging/science_deps/gdal-3.13.1-parallel-head-range.patch \
        packaging/science_deps/versions.env
git commit -m "fix(scienceearth): recover transient head before range"
```

Fresh specification review reads the actual patched cpp/header, not only shell substrings. Fresh
quality review audits easy/multi ownership, reset/UAF/double-cleanup, callback lifetime, delay
conversion, request IDs, event ordering, statistics, and unchanged nontransient/default behavior.
Repeat reviews after fixes until no Critical/Important remains.

---

## Task 3: Complete offline authorization and freeze v6 absence

**Files:**

- Create: `tests/science_v6_authorizer.py`
- Create: `tests/science_v6_authorizer_tests.py`
- Modify: `docs/scienceearth/gdal-build.md`
- Modify: `docs/scienceearth/g0-measurements.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`

**Produces:** `PUBLIC_REQUALIFICATION_V6=AUTHORIZED_NOT_RUN`; no live request or v6 artifact.

- [ ] **Step 1: Run every non-public test from clean tracked HEAD**

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests \
  tests.science_v6_authorizer_tests -v
bash tests/science_deps_script_tests.sh
ctest --test-dir build/osgsol_core --output-on-failure
ctest --test-dir build/science_g0_prefetch --output-on-failure \
  -R 'ScienceEarthRelease|AefIndexTool|ScienceG0Manifest|ScienceDepsScript|ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer'
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --verify
```

Record discovered/run/pass totals dynamically. Require zero skip/failure, no `__pycache__`, exact
patch/prefix/archive pin, and fresh cpp/header byte comparisons.

- [ ] **Step 2: Rebuild and audit the disposable probe**

Use the existing science probe workflow. Require strict/deep codesign, unresolved dependencies 0,
Tier A 0, Tier B new/removed 0/0, delta `<40 MiB`, science closure `<60 MiB`, only the approved
probe export, system-resolved runtime closure, and no worktree/private-prefix strings or load
commands. The protected main app remains science-free.

- [ ] **Step 3: Bind immutable evidence and product boundaries**

Recompute exact topology/hash/mode manifests for every old/v2/v3/v4 evidence root and the complete
frozen six-file v5 set. Re-run only their committed offline auditors, never their public commands.
Bind the v5 corrected ledger and permanent STOP, frozen v4 tree, Desktop fingerprint/tree/count
tuple, no protected application diff, formal CTest still `prefetch`, five iterations,
`--enforce-latency`, and still writing v4. Require all three v6 paths absent and non-symlink.

- [ ] **Step 4: Commit a base-bound two-phase fail-closed verifier**

Create the only verifier source as independent tracked pure-API file
`tests/science_v6_authorizer.py`, UTF-8/LF-only, with no runner/finalizer/wrapper template and no
expected self-hash. Compute its SHA-256, place that value as a literal constant in the separately
committed runner template, and let clean-HEAD authorization independently bind the Task 3 tree and
runner-template SHA. They bind design base
`c555a0f`, exact allowed implementation commits/files, patch and prefix hashes, archive cpp/header
identity, dynamic offline logs/totals, disposable probe, frozen v4/v5, Desktop tuple, exact-five
set, formal listing, and v6 absence. Authentic helper copies must reject at least:

```text
patch hash drift
unexpected regular v6 artifact
dangling v6 symlink
dirty protected application file
broadened retry classifier
missing/duplicate/malformed response-v1 event
HEAD actual-body nonzero
role-swapped v5-shaped replay
```

The independent verifier exports fixed pure APIs including
`verify_preformal(canonical_tuple) -> 64-lowercase-hex-digest`. Authenticity tests read it through
`O_NOFOLLOW`, bind same-FD `fstat`/hash, accept only the runner's literal expected SHA, compile/exec
verified bytes in an isolated namespace, and test canonical-object length/type/key/digest, source
mutation, expected-literal mutation, runner-template mutation, and explicit absence of circular
runner/verifier inclusion. Documentation SHA is report-only. Offline tests also lock absolute completion paths, the two-edit formal
promotion, CTest executable/CTestTestfile immutable snapshots, strict json-v1, alarm reset order,
CTest scratch mutations, pre-gate lease crash recovery, the fixed-gate post-rename FD-self-flag/two-
parent-fsync/final-reopen transition, damaged runner-state blocking, and rename-only final-mode
durability recovery. `authorization` requires all process/snapshot/completion
paths absent before ignored components are materialized. `preformal` requires frozen candidate
manifest/finalizer/completion/summary/proofs, PID/birth-digest/gate/log/snapshot bindings, fixed common/
wrapper hashes/modes, validated formal offline snapshots plus CTest scratch/LastTest manifest, and
absence of formal gate/log/state/
output/completion/manifest/finalizer, the in-memory listing tuple, and the one-file
promotion commit. Its parent is authorization HEAD; it uniquely verifies all five trailers and never
delegates a missing condition to a future auditor.
The exact runner-template SHA contract includes all of those completion, promotion, snapshot/JSON,
verifier-source, alarm, state-obstruction, dynamic prepare-name identity, finite enumeration,
stable PID-reuse checks, ordered gate-preparation recovery, listing-child `A/R/G/E` identity and
PID/PGID death proofs, inherited lease-flock/CLOEXEC protocol, durable post-gate log claim,
unified pending-safe alarm normalization, listing-child `A/R/G/E` dual-watchdog/deadlines,
frozen child-state sibling durability, LastTest directory durability chain, final-mode-before-fsync
rename-only publication, exact `O_RDWR` temp opens, post-rename FD-only immutability, missing-target-
flag recovery, and the permanent gate's unflagged-prepare/first-rename/first-parent-fsync/same-inode
reopen/FD-self-flag/gate-fsync/second-parent-fsync/final-reopen transition with immutable children
and atomic-recovery behaviors. This transition changes the independently bound runner-template
SHA-256 value and is included by runner state, manifests, and authorizer schema/hash expectations.
A common crash-mutation matrix applies independently to lease records/directory publication, every
candidate/formal/CTest/CTestTestfile snapshot file and directory, child-state, runner-state,
exec-authorized, actual manifest, completion, manifest receipt, and finalizer receipt. For each it
stops after exact `O_RDWR` temp create/partial write, complete write, final fchmod, post-mode same-FD
fsync, same-FD fstat/hash/mode/no-temp-flag recheck, no-replace rename, first parent-dir fsync, and
same-inode target reopen; then after target-FD `fchflags(UF_IMMUTABLE)`, post-flag same-FD fsync/
`st_flags` recheck, second parent-dir fsync, and final reopen/relookup. Directory cases stop after
`mkdirat`, child completion, final directory chmod, post-mode dir fsync/no-self-flag recheck,
directory rename, first parent fsync/reopen, target-dir `fchflags`, post-flag dir fsync/recheck,
second parent fsync, and final reopen/re-enumeration. Tests assert a target never persists as `0600`,
missing-target-flag is recoverable but cannot PASS/exec, order mutation fails closed, and no
hard-link/replacing/path-chflags fallback exists.
Lease crash tests also stop after dynamic prepare `mkdir` while empty and after every direct
`lease.lock`/`owner.frame`/`lease.json`/`lease.sha256` final-mode fsync/recheck, FD-level flag,
post-flag fsync/`st_flags` recheck, and prepare-dir fsync stage before complete lease-directory
rename. They cover `LastTest.log` creation, verifier return, and immediately before
gate publish. Recovery tests require gate/log/marker/evidence/summary/completion
absent; exercise regex-name-only dead-owner cleanup, ESRCH/ps-rc1/ESRCH, and pre-gate/finalizer
stable three-kill/two-ps PID-reuse proof; and prove alive/EPERM/ps anomaly/changing birth digest or
metadata mismatch never deletes. Mutations also reject two prepares, prepare plus final lease,
malformed/overlength names,
unexpected children, wrong uid/inode/`nlink`/type/mode, and complete-state hash/path drift. Every
legitimate crash stage must recover or remain PENDING, never become STOP solely due truncation.
CTest scratch mutations cover missing/extra `LastTest.log`, symlink/hard-link/special,
owner/mode/size/content drift, and any apparent live-test start.
Gate-preparation mutations use that common matrix and additionally stop after `.invoked.prepare`
mkdir/fsync; each record's post-rename FD-level flag completion; directory fchmod `0500`, post-mode
dir fsync/recheck with self flag absent; first permanent-gate rename; first parent fsync; same-inode
fixed-gate reopen; target-dir FD `fchflags(UF_IMMUTABLE)`; gate-dirfd fsync/exact `st_flags` recheck;
second parent fsync; and final immutable-gate/child reopen/re-enumeration. They validate every legal ordered
prefix, reject gaps/extras/schema/metadata drift, recover only while permanent gate is absent, and
prove that any permanent `.invoked` presence—even missing its own flag, damaged, or accompanied by
a stale prepare—permanently disables lease cleanup and public retry. A trusted recorded/observed
fixed gate that is later absent is read-only STOP, never a new public run.
Dedicated real local filesystem Darwin tests—not mocks—prove renaming a file or directory that
itself has `UF_IMMUTABLE` fails with `EPERM`, while renaming a `0500` parent directory whose
complete children carry `UF_IMMUTABLE` succeeds before its own flag is set; after fixed-gate self-
flag completion, renaming/moving that gate itself fails `EPERM`. Open-mode tests reject temp `O_RDONLY` and
`O_WRONLY` variants and accept only the exact
`O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC` combination, including same-FD write then read/hash.
Target-file and target-directory missing-
flag crash tests resume by FD under the held lease/death/lock contract or clean while gate-absent.
They explicitly accept both-target/no-temp directory mode `0700` before freeze and `0500` after
freeze, reject every other state/mode pairing, and recover either legal mode after lock/owner proof.
Formal-listing-child mutations stop before spawn; after fd validation/startup N=15/`A`; after
setsid/identity mkdir/parent fsync/`R`; during state partial `0600`, complete `0400` temp before
rename, every child-state common-matrix edge, target publication and `G`; after exec N=25/`E`; and
at CTest exec, scratch write, child exit, and each PID/PGID death proof. After group proof they stop
after state no-follow reopen/hash/mode/flag check, state same-FD fsync/recheck, child-dir fchmod with
self flag absent, post-mode child-dir fsync/recheck, FD-level directory flag, post-flag fsync/
`st_flags` recheck, final-lease fsync, and sibling reopen/re-enumeration; none may reach gate
without the full sibling proof. They prove no GO before durable identity/state; parent death before
GO causes EOF/child exit; parent death after GO leaves identity; alive/EPERM/changed birth or group,
extra PGID member, or ps/killpg anomaly retains scratch/snapshots and remains PENDING. Direct child
mode with missing/swapped/unbound fds must exit before any filesystem or CTest side effect.
All runner/recovery/finalizer/child/parent ps subprocess tests require
`close_fds=True, pass_fds=()`, while the one parent-to-child spawn alone requires exact
`close_fds=True, pass_fds=(3,4,5,6)`. A wedged child ps fixture waits past startup N=15: SIGALRM
kills the child, the external ps is proven to hold none of fd 3/4/5/6, fd 5 flock eventually
releases even if ps survives briefly, and PGID cleanup remains PENDING until strict empty-group
proof rather than signaling an unknown process.
The parent monotonic handshake test enforces exact `A -> R -> G -> E` within 20 seconds and starts
the 30-second listing timeout only at E. Missing/reordered/duplicate/late bytes close pipes and wait
for watchdog/EOF. An offline silent fixture CTest blocks beyond 25 seconds while the harness
SIGKILLs parent after G/E; child must die from kernel SIGALRM before the listing deadline, release fd 5,
leave recoverable identity/state, and permit later strict lock/death cleanup. A companion case starts
with inherited `SIGALRM=SIG_IGN`. Shared alarm tests cover inherited ignored, inherited blocked, and
pending+blocked SIGALRM for N=15/25/300; each must consume pending while blocked, install default,
unblock with no edge delivery, verify mask, arm, and fail closed on every API/return anomaly.
Lease-lock mutations cover create-before-flock, held/unheld `LOCK_EX`, inode/uid/`nlink`/mode/size
replacement, nonblocking contention/error, parent death before/after spawn but before child identity,
fd 5 same-open-description inheritance/CLOEXEC/CTest retention, fd 6 directory identity/CLOEXEC,
child-exit lock release, and cleanup holding the acquired lock through final parent fsync. Any
held/inherited lock must force PENDING even when runner PID is dead and no child identity is yet
visible.
Post-gate log mutations stop after O_EXCL create, same-FD metadata check, file fsync, parent-dir
fsync, held-dirfd enumeration, same-inode confirmation, and final same-FD recheck. Alarm/exec must
remain unreachable until all stages pass; missing log may classify `NO_EXEC` only under this bound
contract, while malformed/replaced logs block instead. Every gate mutation before final immutable-
gate reopen proves the log create syscall itself remains unreachable.
LastTest mutations stop after same-FD validate/hash/fchmod, file fsync/recheck, file-FD flag and
post-flag fsync/recheck, first scratch fsync, scratch `0500` with self flag absent, post-mode fsync,
scratch-dir FD flag and post-flag fsync/recheck, and every bottom-up scratch/Testing/listing-root/parent/
final-lease fsync plus reopen/re-enumeration. The bound sibling child-state durability chain is
mutated alongside it. Missing/truncated legal crash states remain
`PREFORMAL_PENDING`; no mutation may reach preformal PASS or gate before the full chain binds.
The promotion diff verifier rejects any semantic or byte change beyond the exact v4-to-v6 root edit
and insertion of the fixed absolute formal `--completion-json` argument.

End all three documents with:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V6=AUTHORIZED_NOT_RUN
DESKTOP_PACKAGE=NOT_READY
```

Commit only the verifier source/tests and three authorization docs:

```bash
git add tests/science_v6_authorizer.py tests/science_v6_authorizer_tests.py \
        docs/scienceearth/gdal-build.md docs/scienceearth/g0-measurements.md \
        docs/scienceearth/g0-g1-baseline.md
git commit -m "test(scienceearth): authorize v6 requalification"
```

- [ ] **Step 5: Obtain three independent authorization reviews**

Fresh specification review verifies Task 3 line by line. Fresh quality review executes the
committed verifier and authentic mutations. A fresh whole-range reviewer inspects
`c555a0f..authorization-HEAD`, including real patch cpp/header order and protected scope. Fix and
repeat until all three report no Critical/Important. No public process occurs in Task 3.

---

## Task 4: Run exactly one v6 candidate and conditionally one formal gate

**Tracked files:** conditional promotion `tests/CMakeLists.txt`; decision bindings only in
`docs/scienceearth/g0-measurements.md`, `docs/scienceearth/g0-g1-baseline.md`, and conditionally
`docs/scienceearth/gdal-build.md`.

**Ignored fixed components, never staged:**

- common: `.superpowers/sdd/task-4-v6-runner.py` and
  `.superpowers/sdd/task-4-v6-finalize.py`;
- wrappers: `.superpowers/sdd/task-4-v6-one-shot-candidate.sh` and
  `.superpowers/sdd/task-4-v6-one-shot-formal.sh`;
- frozen preflight: `.superpowers/sdd/task-4-v6-one-shot-preflight.sh` and
  `.superpowers/sdd/task-4-v6-one-shot-preflight.log`;
- candidate process paths: `.superpowers/sdd/task-4-v6-one-shot-candidate.log`, the single allowed
  dynamic prepare matching exact basename regex
  `^task-4-v6-candidate-pregate\.lease\.prepare\.(0|[1-9][0-9]{0,9})\.([1-9][0-9]{0,9})\.([0-9a-f]{64})$`, published
  `.superpowers/sdd/task-4-v6-candidate-pregate.lease` with `lease.lock`, `owner.frame`, `lease.json`, `lease.sha256`, and fixed
  `candidate-exec-snapshot.tmp`, gate prepare
  `.superpowers/sdd/task-4-v6-one-shot-candidate.invoked.prepare` with ordered
  `runner-state.json.tmp`/`runner-state.json` then
  `exec-authorized.json.tmp`/`exec-authorized.json`, permanent self-immutable `0500` gate
  `.superpowers/sdd/task-4-v6-one-shot-candidate.invoked` after the two-parent-fsync transition, with `runner-state.json` and
  `exec-authorized.json`, `.superpowers/sdd/task-4-v6-candidate-actual-manifest.sha256` plus `.tmp`,
  `.superpowers/sdd/task-4-v6-candidate-finalizer.json` plus `.tmp`, execution snapshot directory
  `.superpowers/sdd/task-4-v6-candidate-exec-snapshot` and its
  `osgVerse_Test_ScienceHttpRanges`, candidate evidence root and its `evidence-manifest.sha256`,
  candidate summary, and absolute
  `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/science_g0_prefetch/requalification-evidence-v6/candidate-completion.json`
  plus fixed `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/science_g0_prefetch/requalification-evidence-v6/candidate-completion.json.tmp`;
- formal process paths: `.superpowers/sdd/task-4-v6-one-shot-formal.log`, the single allowed
  dynamic prepare matching exact basename regex
  `^task-4-v6-formal-pregate\.lease\.prepare\.(0|[1-9][0-9]{0,9})\.([1-9][0-9]{0,9})\.([0-9a-f]{64})$`, published
  `.superpowers/sdd/task-4-v6-formal-pregate.lease` with `lease.lock`, `owner.frame`, `lease.json`, `lease.sha256`, fixed
  `formal-ctest-listing-snapshot.tmp`, fixed `formal-exec-snapshot.tmp`, and at most one dynamic
  child directory matching exact basename regex
  `^\.listing-child\.prepare\.(0|[1-9][0-9]{0,9})\.([1-9][0-9]{0,9})\.([0-9a-f]{64})$`
  with `child-state.json.tmp` or `child-state.json`,
  `.superpowers/sdd/task-4-v6-one-shot-formal.invoked.prepare` with ordered
  `runner-state.json.tmp`/`runner-state.json` then
  `exec-authorized.json.tmp`/`exec-authorized.json`, permanent self-immutable `0500`
  `.superpowers/sdd/task-4-v6-one-shot-formal.invoked` after the two-parent-fsync transition, with `runner-state.json` and
  `exec-authorized.json`, `.superpowers/sdd/task-4-v6-formal-actual-manifest.sha256` plus `.tmp`,
  `.superpowers/sdd/task-4-v6-formal-finalizer.json` plus `.tmp`,
  `.superpowers/sdd/task-4-v6-formal-exec-snapshot` and its `osgVerse_Test_ScienceHttpRanges`,
  immutable `.superpowers/sdd/task-4-v6-formal-ctest-listing-snapshot` with `ctest` and
  `CTestTestfile.cmake`, immutable `Testing`, precreated writable `Testing/Temporary`, and exact
  post-listing `Testing/Temporary/LastTest.log`,
  `build/science_g0_prefetch/formal-evidence-v6`, and
  `build/science_g0_prefetch/formal-evidence-v6/evidence-manifest.sha256`,
  `build/science_g0_prefetch/formal-evidence-v6/live-summary.json`, and absolute
  `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/science_g0_prefetch/formal-evidence-v6/live-completion.json`
  plus fixed `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/science_g0_prefetch/formal-evidence-v6/live-completion.json.tmp`.

**Produces:** immutable actual-state artifacts and one authoritative v6 G0 decision.

- [ ] **Step 1: Materialize fixed components and pass offline authorization**

First run the committed `authorization` verifier under deny-network with every candidate/formal
process path absent/non-symlink and frozen v4/v5/Desktop unchanged. Materialize runner/finalizer by
no-follow `O_EXCL` creation as regular mode `0400`, and both wrappers as regular mode `0500`, using
the exact committed templates/hashes. The candidate wrapper is exactly:

```sh
#!/bin/sh
exec /usr/bin/python3 /Users/USER/osgsol/.worktrees/v0.2-runtime-safety/.superpowers/sdd/task-4-v6-runner.py --candidate
```

The formal wrapper differs only by `--formal`. Neither wrapper performs file I/O, redirection,
mkdir, wait, trap, or any other command. Run and freeze the exact preflight wrapper/log at `0400`.
Bind exact binary/fixture/CMake hashes, credential-free proxy class, clean allowed scope, frozen
v4/v5/Desktop, sticky-HEAD retry contract, and all exhaustive path expectations. Every later phase
rechecks common/wrapper hashes and modes.

- [ ] **Step 2: Run the candidate permanent gate once**

Before a gate exists, candidate runner validates all fixed components and requires its log, gate
preparation, gate/state/marker, manifest/finalizer targets and temps, evidence root, and summary
absent/non-symlink. Lease prepare/lease/temp/final snapshot must also be absent, unless an existing
lease first passes the strict dead-owner recovery below. It binds this exact live command:

```bash
build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile prefetch \
  --evidence-dir build/science_g0_prefetch/requalification-evidence-v6/candidate \
  --summary-json build/science_g0_prefetch/requalification-evidence-v6/candidate-summary.json \
  --completion-json /Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/science_g0_prefetch/requalification-evidence-v6/candidate-completion.json \
  --enforce-latency
```

Every framed/JSON/data/executable regular temp-to-target publisher uses one shared primitive. Open
the fixed `0600` temp with exactly
`O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC`; reject `O_RDONLY`, `O_WRONLY`, missing, or extra flags.
Write complete bounded bytes; same-FD fchmod to final `0400` data/JSON or `0500` executable mode;
same-FD fsync only after final mode; same-FD fstat/hash/size/uid/`nlink==1`/mode recheck while
requiring no temp `UF_IMMUTABLE`; `renameatx_np(RENAME_EXCL)` with no hard-link/replacing fallback;
held parent-dir fsync; then no-follow target reopen/relookup proving the same inode/hash/mode.
Target reopen uses exact `O_RDONLY|O_NOFOLLOW|O_CLOEXEC`; this is distinct from temp creation and
does not weaken its `O_RDWR` requirement.

Only after rename durability may a flag-bearing target be made immutable. Use a checked
`ctypes.CDLL(None, use_errno=True)` binding to macOS `fchflags(int,unsigned int)` on the target FD;
set `argtypes=[ctypes.c_int,ctypes.c_uint]`, `restype=ctypes.c_int`, and capture errno only on `-1`.
Path or shell `chflags` is forbidden. Add `UF_IMMUTABLE`, same-FD fsync/fstat exact `st_flags`
recheck, parent-dir fsync, and same-inode/hash/mode/flags reopen/relookup. A correct final-mode target
missing only this post-rename flag is a legal interrupted state: with permanent gate absent and the
lease held after owner-death/cleanup-lock proofs, recover by resuming FD-level flagging or exact
allowlisted cleanup, never rewrite/replacement. No pre-exec/preformal target may feed exec or PASS
until flag-complete; post-exec records cannot feed `COMPLETE_PASS` until flag-complete.

Directory temps use `mkdirat`, not regular-file open. Finish children, fchmod final mode, same-dirfd
fsync/recheck with no self flag, rename, parent fsync, and same-inode target reopen through
`O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`; only then apply target `UF_IMMUTABLE` through FD-level
`fchflags`, fsync/`st_flags` recheck, parent fsync, and final
reopen/re-enumeration. A final-mode directory target missing only its flag is the same legal
gate-absent crash window. The fixed permanent gate is stricter: final-name presence already consumes
one-shot, so missing only its self flag may be FD-resumed but never cleaned or publicly retried.
Direct lease files inside an unpublished prepare directory may be
FD-flagged because only their containing directory is renamed; the final lease container remains
mutable until gate publication so snapshots can be added. This primitive governs lease records,
snapshots, child-state, runner-state, exec-authorized, actual manifests, completion, finalizer and
manifest receipts. Fault tests stop after every listed stage for every publisher class.

With fixed `LC_ALL=C LANG=C TZ=UTC`, runner invokes
`[/bin/ps,-o,lstart=,-p,<runner-PID>]` using `shell=False`, `close_fds=True`, `pass_fds=()`, waits,
and accepts only rc 0 plus one
ASCII result matching `^(Mon|Tue|Wed|Thu|Fri|Sat|Sun) (Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) ( [1-9]|[12][0-9]|3[01]) ([01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9] [0-9]{4}\n$`.

Canonicalize the accepted ps line as ASCII bytes and compute lowercase
`birth64hex = SHA256(canonical-line-bytes)`; persist only this digest, never raw birth text. Open
`.superpowers/sdd` as a no-follow directory FD and enumerate it exactly before creation. Per mode,
the union of the one anchored-regex prepare name and the fixed final lease has cardinality at most
one; duplicates, extra prefix-colliding names, or malformed uid/PID/digest names are read-only STOP.
Require canonical decimal round-trip, uid fitting `uid_t` and equaling runner uid, and PID fitting
positive `pid_t` and equaling the runner PID at creation. The largest allowed 10-digit uid,
10-digit PID, and 64-hex digest still keep both basenames safely below macOS `NAME_MAX=255`.

With runner umask fixed to `077`, before any snapshot write atomically
`mkdirat(parent-dirfd, task-4-v6-candidate-pregate.lease.prepare.<uid>.<pid>.<birth64hex>, 0700)`
and immediately fsync the parent. Re-enumerate and require this same inode to be the sole per-mode
prepare/final entry before any content write. The directory name is the first durable partial owner
identity, including if the directory is empty. First create zero-byte regular `lease.lock` through
`O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC`, validate stable inode/uid/`nlink==1`/mode `0600`, obtain
`flock(LOCK_EX)`, fchmod `0400`, same-FD fsync only after chmod, same-FD metadata/mode recheck,
FD-level `fchflags(UF_IMMUTABLE)`, same-FD fsync/`st_flags` recheck, and prepare-dir fsync; parent
retains that open file description through all pre-gate work. Create
framed `owner.frame`, then canonical `lease.json` containing
magic/schema, mode, uid, PID, `birth_sha256`, fixed temp/final paths, component/source hashes and
lock inode/mode bindings, plus separate `lease.sha256`. Create every direct file with
`O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC`, write it completely at `0600`, fchmod `0400`, same-FD
fsync/hash/mode recheck, then FD-flag immutable and same-FD fsync/`st_flags` recheck. After all four
are complete, fchmod the prepare dirfd to final `0700`, require its own immutable flag absent, fsync
it after chmod, and re-enumerate/recheck child flags before using
same-directory `renameatx_np(RENAME_EXCL)` to publish fixed
`task-4-v6-candidate-pregate.lease`, immediately fsync the parent, and reopen/re-enumerate the same
lease inode and four file hashes/modes/flags; the final lease directory remains unflagged/mutable so
snapshot temps can be created. If that primitive is
unavailable or fails, stop/pending without fallback or deletion. Only then copy the
`O_NOFOLLOW` source binary into fixed `candidate-exec-snapshot.tmp` opened with the exact global
`O_RDWR` flags, bind same-FD `fstat`/hash/uid/`nlink`, fchmod final `0500`, same-FD fsync/recheck with
no temp flag, no-replace rename, parent fsync, and same-inode target reopen. Then FD-level
`fchflags(UF_IMMUTABLE)`, same-FD fsync/`st_flags` recheck, second parent fsync, and final reopen
complete publication. Data snapshots use final `0400`; snapshot directory temps likewise rename
without their own flag and flag only the reopened target. Formal live/CTest/CTestfile/listing
snapshots use the identical lease-before-temp-before-final sequence.

On every gate-absent retry, enumerate via the same parent dirfd. A single regex-valid prepare may be
empty or have only an ordered creation prefix of `lease.lock`, `owner.frame`, `lease.json`, and
`lease.sha256`; a later file without predecessors is tamper. Its
name supplies uid/PID/birth digest even if `owner.frame` is missing or truncated. Snapshots are
forbidden before fixed final-lease publication. Require stable prepare inode, uid, `nlink==2`,
directory type and mode `0700`,
plus absence of all public gate/log/marker/evidence/summary/completion temp/target paths. Enumerate a
finite exact allowlist recursively. In a prepare this is only the four lease files. A fixed final
lease first requires all four full lock/owner/schema/hash/path checks, then candidate permits only
`candidate-exec-snapshot.tmp`/final plus its fixed binary, while formal additionally permits only
`formal-exec-snapshot.tmp`/final, `formal-ctest-listing-snapshot.tmp`/final, `ctest`,
`CTestTestfile.cmake`, `Testing`, `Testing/Temporary`, and `LastTest.log`. Every child must preserve
expected inode/uid/`nlink`/type/mode. Prepare files have `nlink==1`; `0600` is legal while writing,
whereas `0400` requires complete post-chmod same-FD-fsynced/rechecked content; its missing flag is a
legal pre-flag crash window under death/lock recovery. `lease.lock` is always zero bytes with mode
`0600` only before lock setup or `0400` afterward, and all four must be complete/revalidated `0400+UF_IMMUTABLE`
before lease rename. A partial non-lock regular file must be a byte prefix of its fixed
magic/frame/checksum encoding; a
complete one must pass full schema/hash/path validation. Any extra name,
special/symlink, metadata mismatch, or non-prefix bytes is STOP and deletes nothing. Truncation
leniency for lease files applies only to regex-valid prepare.

Use name identity for prepare liveness and the fully verified lease identity for final-lease
liveness. If first `kill(pid,0)` is `ESRCH`, require exact fixed-env ps
no-process result `rc=1`, empty stdout and stderr, then a second `kill` returning `ESRCH`. If first
kill succeeds, hash the fresh strict canonical ps line: equality with the stored digest is an alive
owner and PENDING. A different digest is possible PID reuse, not immediate death; prove the old
owner dead only when a second kill succeeds, a second strict ps parse hashes to the same new digest
still different from stored, and a third kill succeeds. Any changing/exiting process, `EPERM`,
other errno, ps launch/rc/output/ASCII/parse failure, or metadata mismatch is PENDING/STOP and never
authorizes deletion. Every runner, recovery, finalizer, listing child, and listing parent `/bin/ps`
subprocess uses `shell=False, close_fds=True, pass_fds=()`; no probe inherits lease, directory,
READY, GO, or evidence-pipe descriptors. Each uses `stdin=DEVNULL` and bounded private stdout/
stderr pipes.

For a fixed final lease, before any cleanup open bound `lease.lock` through
`O_RDONLY|O_NOFOLLOW|O_CLOEXEC`, revalidate exact regular type/inode/runner uid/`nlink==1`/mode `0400`/size
zero, and require `flock(LOCK_EX|LOCK_NB)` success on that separately opened FD. Contention,
`EINTR`, or any error is PENDING and deletes nothing. A lock success does not replace runner,
listing-child, or PGID death proofs. Recovery holds the acquired lock through every bottom-up
unlink and parent fsync, closing only after cleanup is durable.

After either stable death proof only, clear `UF_IMMUTABLE` through ctypes `fchflags` on each held
no-follow FD, same-FD fsync/recheck cleared `st_flags`, recheck every allowlisted inode/uid/
`nlink`/type, delete bottom-up, and fsync every affected parent. Path/shell `chflags` is forbidden.
Crash injection after empty
mkdir, partial owner frame, partial lease state, partial checksum, final-lease rename, every
snapshot write/publish, listing scratch, verifier return, and immediately before gate publication
must yield recovery or PENDING, never `OFFLINE_STOP` solely because a legitimate write crashed.
Then establish a fresh lease; gate remains absent and bottom invocation remains 0.

The verified final lease also enumerates the fixed sibling
`task-4-v6-one-shot-<mode>.invoked.prepare`. While gate is absent this directory may be `0700` and
must retain runner uid, stable directory inode/type and `nlink==2`; it may contain only the ordered
states: empty; `runner-state.json.tmp`; a complete runner target missing only its post-rename flag;
that flag-complete runner target; flag-complete runner target plus `exec-authorized.json.tmp`; both
targets with only exec-authorized's post-rename flag missing; or both flag-complete targets.
Exec-authorized temp creation is forbidden until runner-state is flag-complete. Each temp is runner-owned
regular `nlink==1`, stable inode, `0600` while partial and `0400` only when its bounded `<=64 KiB`
framed magic/canonical JSON/checksum/schema is complete. Each target uses the unified exact-open
sequence: complete write, final `0400` fchmod, post-chmod same-FD fsync/no-temp-flag recheck,
no-replace same-directory `renameatx_np(RENAME_EXCL)`, child-dir fsync, same-inode target reopen,
target-FD `fchflags(UF_IMMUTABLE)`, post-flag fsync/`st_flags` recheck, second child-dir fsync, and
final reopen. A correct target missing only the flag is legal/recoverable while gate-absent but not
gate-complete. Runner-state binds mode, uid,
PID/birth digest, final lease, snapshots,
lease-lock inode/mode, argv/cwd/completion and source/component hashes; formal additionally binds
listing-child PID/PGID/birth, child-state hash/inode, alarm contract, and empty-group proof. Both
schemas bind the preparation inode, fixed final basename, mode `0500`, and required post-rename gate-
dirfd self-flag/two-parent-fsync/final-reopen contract; exec-authorized binds those expectations via
runner-state hash/inode and independently bound runner-template SHA. Formal state also binds `A/R/G/E` deadlines, frozen child-state sibling
durability, and the LastTest durability chain. After both targets are flag-complete and no temps
remain, directory `0700` is the legal post-target/pre-freeze window and unflagged `0500` is the
frozen state; gate rename requires exactly that unflagged `0500` state. Either
complete mode is dead-owner recoverable after cleanup-lock acquisition while gate is absent. Any
other state/mode pairing, gap, extra, special/symlink, wrong type/magic/size/uid/inode/`nlink` or
schema is STOP.

The same dead-owner proof permits bottom-up cleanup of this exact gate-preparation tree and final
lease while permanent gate remains absent. Mutation points use the full exact-open, pre-rename
mode-durability, first parent-fsync/reopen, target-FD flag, post-flag durability, and final reopen
matrix. Gate-directory mutations separately cover fchmod `0500`, rejected prepare self flag, post-
mode dir fsync/recheck, first gate rename, first parent fsync, fixed-name same-inode reopen, gate-dirfd
`fchflags(UF_IMMUTABLE)`, gate fsync/exact flag recheck, second parent fsync, and final immutable-gate/
flagged-child reopen/re-enumeration. Once fixed `.invoked` is observed, lease recovery is permanently
forbidden: do not clear flags or remove prepare, snapshot, child identity, scratch, or lease even if
gate is missing only its self flag, damaged, or both prepare and gate exist. If a trusted runner,
finalizer, manifest, or auditor has observed/recorded the fixed name and it is later absent, bind
permanent read-only STOP and never run publicly again.

Using trusted dirfds, runner requires both record targets immutable, fchmods the preparation
directory `0500`, explicitly requires its own `UF_IMMUTABLE` absent, fsyncs after chmod, and same-FD
revalidates its enumeration/final lease. The Darwin fixture proves self-immutable file/directory
rename fails `EPERM` while renaming this `0500` unflagged parent containing immutable children
succeeds. Runner then same-directory `renameatx_np(RENAME_EXCL)` renames it to permanent `.invoked`
while it remains unflagged and performs the first parent-dir fsync. Through the held parent dirfd it
opens fixed `.invoked` with exact `O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`, proves the same inode,
mode `0500`, exact complete schema/hash, both immutable record flags, and final-lease bindings, then
uses only target-dirfd `fchflags(UF_IMMUTABLE)`. It fsyncs the gate dirfd, same-FD fstats exact inode/
mode/`st_flags`, performs the second parent-dir fsync, and final-reopens/re-enumerates the same
immutable gate and records. The fixed name's first appearance—not self-flag completion—is the one-
shot consumption boundary. Seeing it after rename-before-first-fsync or at any later phase
permanently forbids cleanup/public retry.

A complete bound fixed gate missing only directory `UF_IMMUTABLE` is legal only as
`FINALIZATION_PENDING`/`NO_EXEC`. The same runner under its original held lease may finish only the
FD flag/fsync/recheck tail. After stable owner-death proof, finalizer may revalidate complete gate
schema/hashes/record flags/final-lease binding, durably replay the first parent fsync, reopen the same
inode, and perform only that FD flag/gate-fsync/second-parent-fsync/final-reopen tail; it may
not rewrite, create log, execute, delete, move, or authorize PASS. Damaged fixed-gate content is
read-only blocked. Only after durable gate self-flag proof may runner persist the log, arm the alarm,
or exec. A real Darwin test also proves renaming/moving the final self-flagged gate fails `EPERM`.
The permanent gate already contains
complete runner-state and exec-authorized marker. The lease survives and is later frozen in the
manifest. Parent's original exclusive-lock FD remains CLOEXEC and held through pre-gate work; after
the full self-flag/two-parent-fsync/final-reopen proof, the later public `execve` closes it. Only formal
listing-child fd 5 is explicitly inheritable through its CTest exec. Any legitimate-crash or
otherwise recoverable gate-absent failure is `OFFLINE_PENDING`,
bottom 0; only the strict lease protocol may repair it, so recovery never becomes a second public
invocation. An irreconcilable lease/public-path mismatch may instead be bound read-only as
`OFFLINE_STOP`, bottom 0, without deletion.

Only after the final immutable-gate reopen proof does runner enter post-gate execution; it spawns no
child. Through a held no-follow log-parent dirfd it creates
the fixed log with `O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW` mode `0600`. Before descriptor install,
alarm, or exec it uses that same FD to verify regular type/stable inode/runner uid/`nlink==1`/mode,
fsyncs the file, fsyncs the log parent, re-enumerates the exact basename through the held parent
dirfd without following, confirms the same inode/type/uid/`nlink`/mode, repeats same-FD `fstat`, and
fsyncs the complete gate. Any failure cannot reach alarm/exec and makes malformed/replaced presence
blocked; later log absence means `NO_EXEC` only because this durable claim is a mandatory
predecessor. Runner installs the verified FD. This direct output log is not temp-to-target and is
the sole intentional `O_WRONLY` creation exception; it is never renamed or same-FD hashed. Runner then
calls the shared `normalizeAndArmAlarm(300)` primitive. That primitive first
`pthread_sigmask(SIG_BLOCK,{SIGALRM})` and retains the old mask, then while blocked calls `alarm(0)`.
It reads `sigpending`; when SIGALRM is present it calls `sigwait({SIGALRM})`, requires exact
SIGALRM (standard signals have at most one pending instance), and requires a second pending set with
no SIGALRM. It sets `SIG_DFL`, unblocks SIGALRM with `pthread_sigmask`, reads the resulting mask
without changing it via `pthread_sigmask(SIG_BLOCK,empty-set)` and requires SIGALRM unblocked, then
calls `alarm(N)` and requires prior
remainder zero. It consumes no other signal. Missing API, exception, or any return/mask/pending
mismatch fails. Clearing pending while blocked before default+unblock proves no inherited ignored,
blocked, or pending+blocked SIGALRM can kill at the unblock edge. Any public N=300 failure is
post-marker `EXEC_STOP`. Runner revalidates
inode/hash/mode using the same open snapshot FD while its
parent/file remain read-only and immutable. It then calls `os.execve` on the snapshot realpath;
this is the no-path-replacement macOS equivalent. Runner and binary keep one PID.

Runner-owned `O_EXCL` covers only preparation/gate/state/log/marker/snapshot. The binary uniquely
owns evidence, evidence manifest, summary, completion temp, and completion target. It may update
evidence/summary internally, then closes/fsyncs all samples and computes bound hashes. It opens the
actual-manifest and completion temps with exact global `O_RDWR` flags and publishes manifest first,
`osgsol.scienceearth.v6-completion.v1` second through complete write, final `0400` fchmod,
post-chmod same-FD fsync/no-temp-flag recheck, no-replace rename, first parent fsync/reopen,
target-FD `fchflags(UF_IMMUTABLE)`, post-flag same-FD fsync/`st_flags` recheck, second parent fsync,
and final same-inode/hash/mode/flags reopen, then performs no later sample write.
Actual manifests additionally bind lock inode/uid/`nlink`/mode/size, ordered gate-preparation
records, observed post-gate log inode/uid/`nlink`/mode and durable-claim contract, and formal child
identity/state/fd/dual-watchdog/ACK/frozen-sibling/LastTest-chain/PID/PGID/group proofs. They bind
each target's first reopen, post-rename FD flag transition/final reopen, and the permanent gate's
first-rename/first-parent-fsync/same-inode reopen/FD-self-flag/gate-fsync/second-parent-fsync/final-
reopen proof.

Invoke fixed finalizer as `/usr/bin/python3 .../task-4-v6-finalize.py --candidate`. It first opens the
fixed gate through a held parent dirfd with exact `O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC` and
validates same inode/mode, complete schema, both immutable record hashes/flags, and final-lease
binding. Missing only the gate's own `UF_IMMUTABLE` is `FINALIZATION_PENDING`, never execution: the
original runner under its held lease may finish only the prescribed FD flag tail, or after stable
owner-death proof finalizer may revalidate, first fsync the held parent, reopen the same inode, and
set only that directory flag, gate-fsync/exact-`st_flags` check, second parent-fsync, and final
reopen. It must not chmod/rewrite records, create log,
execute, delete, or move anything. Successful repair with no durable log becomes `NO_EXEC`.
Missing/invalid/damaged/replaced content, or fixed-gate absence after trusted observation/recording,
is read-only `FINALIZATION_BLOCKED`: do not guess PID or repair. With valid state it calls
`os.kill(pid, 0)`: `ESRCH` proves the original absent; `EPERM` or any other error is pending. If
kill succeeds, the exact fixed-env/shell-false ps command must launch, return 0, and strictly parse;
freshly hashing the canonical line to the recorded digest is `RUNNING`, a different valid digest
is only possible PID reuse. Prove the original dead only after second kill success, a second strict
ps parse with the same new digest, and third kill success; any exit/change or launch/rc/parse anomaly
is pending. `RUNNING` requires an already self-flag-complete gate; the same live identity with a
missing self flag remains `FINALIZATION_PENDING` for the original runner's flag-only tail. Raw birth
text is never persisted. Every such ps call uses
`close_fds=True, pass_fds=()`. It then opens through dirfds with `O_NOFOLLOW`, using each same FD for
`fstat`, hash, and `fchmod`.
Reject symlink/special/owner/link/topology anomalies; freeze mutable files `0400`, then directories
`0500`. After every chmod/flag change, same-FD fsync/recheck the file or directory, fsync parents
bottom-up, and reopen/re-enumerate final presence/hash/size/mode/flags. Common Python stays `0400`;
wrappers stay `0500`.

Manifest and finalizer JSON use fixed same-directory temps opened with exact global `O_RDWR` flags,
containing `magic+length+payload+checksum`, and the unified sequence: complete write, same-FD
`fchmod(0400)`, post-chmod same-FD fsync/fstat/hash/mode/no-temp-flag recheck, no-replace
`renameatx_np(RENAME_EXCL)`, first parent fsync and same-inode target reopen, target-FD
`fchflags(UF_IMMUTABLE)`, post-flag same-FD fsync/`st_flags` recheck, second parent fsync, and final
same-inode/hash/mode/flags reopen, manifest first and finalizer JSON second.
There is no hard-link or replacing fallback; unavailable/failed rename remains
`FINALIZATION_PENDING`. Any simultaneous target/temp collision is `FINALIZATION_BLOCKED`, even if
bytes match. With target absent, a complete valid temp may resume the exact sequence; with temp
absent, an existing target missing only its flag may resume the FD-level flag half and a flag-
complete target is accepted only if its full payload matches the recomputed expected bytes. Only a proven finalizer-owned
partial regular temp with matching dirfd/inode/uid, `nlink==1`, and magic prefix may be unlinked and
rebuilt; otherwise the state is `FINALIZATION_BLOCKED`. Finalization may be retried indefinitely
offline and never permits a public rerun.

Post-gate state uses this first-match priority:

| Priority | Candidate state | Predicate | Bottom | Next |
|---:|---|---|---:|---|
| 1 | `FINALIZATION_BLOCKED` | gate content missing/invalid/damaged/replaced; fixed gate absent after trusted observation/recording; present log fails exact type/inode/uid/`nlink`/mode contract; or valid identity plus proved death exposes irreconcilable obstruction | 0..1 | read-only `STOP`; no chmod, PID guess, rerun, or fake manifest |
| 2 | `FINALIZATION_PENDING` | complete fixed gate lacks only its self flag; liveness is inconclusive; or proved-dead state has recoverable gate-self-flag/publication incomplete | 0..1 | FD-only flag/offline finalizer retry; no log/exec/rerun |
| 3 | `RUNNING` | self-flag-complete valid state; kill succeeds; ps parses and hashes to the recorded birth digest | 0..1 | wait; no chmod |
| 4 | `NO_EXEC` | finalization complete; original dead; valid gate/marker and durable-log-before-alarm contract bind, but exact post-gate log is absent | 0 | `STOP`, Step 5 |
| 5 | `EXEC_STOP` | finalization complete; original dead; marker and exact same-inode durable log exist; valid completion/PASS absent | 0..1 | `STOP`, Step 5 |
| 6 | `COMPLETE_PASS` | finalization complete; original dead; marker/exact durable log plus valid completion, summary, and legal proofs | 1 | Step 4 |

- [ ] **Step 3: Decide candidate qualification from artifacts**

Require ten complete `AttributedV6` public-envelope proofs: context-correlated `response-v1` and
decision coverage, raw request/response plus VSINetworkStats reconciliation, strict RGB/CRS/
georeference/WGS84/NoData/interval/byte oracle, exact first Range, HTTP/2 sharing, zero fallback,
one publication, downloaded body `<=16 MiB`, fixture median `<=3000 ms`, and P95 `<=8000 ms`.
Public origin/CDN logs are neither required nor claimed. Any missing/invalid proof is `EXEC_STOP`.
`COMPLETE_PASS` additionally requires the frozen valid completion record to bind the recorded PID,
execution-snapshot hash, summary hash, and actual-evidence-manifest hash. It does not assert or use
an OS exit code; missing/invalid completion is `EXEC_STOP`.

- [ ] **Step 4: On candidate PASS only, promote and run formal through runner**

The unique `tests/CMakeLists.txt` promotion contains exactly two semantic edits: change the formal
root from v4 to v6 and add fixed absolute argument
`--completion-json /Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/science_g0_prefetch/formal-evidence-v6/live-completion.json`.
Retain `prefetch`, five iterations, and `--enforce-latency`. Commit this one-file-only diff with
authorization HEAD as parent and exactly one each of the existing candidate manifest, summary,
wrapper, transcript, and gate SHA-256 trailers. Reconfigure with the authorized CMake command.

Invoke the fixed formal wrapper. Before creating formal preparation/gate/log/output, runner obtains
PID and canonical birth-line SHA-256 under `LC_ALL=C LANG=C TZ=UTC`; it stores only the digest. It
uses the formal anchored-regex prepare name and the same strict single-entry enumeration, partial
prepare recovery, stable PID-reuse proof, and final fixed-lease publication defined in Step 2. It
publishes the exact formal pre-gate lease/state through the unified post-mode-fsync/recheck,
no-replace rename, parent-fsync, target-reopen sequence before any CTest, CTestTestfile,
listing-build, or live-binary snapshot temp. Every temp is fixed beneath that lease and every final
snapshot uses the same complete sequence. Runner then reads bound
`CMakeCache.txt` through `O_NOFOLLOW` and
same-FD pre/post inode/hash/mode checks, extracts key `CMAKE_CTEST_COMMAND`, resolves and
validates its realpath via `O_NOFOLLOW` FD, and copies those bytes to immutable `0500` `ctest` in the
fixed listing snapshot. It separately copies generated `tests/CTestTestfile.cmake` from an
`O_NOFOLLOW` FD into that immutable directory. Source/snapshot inode/hash/mode/flags are bound
pre/post. Before freezing directory entries it precreates immutable/read-only `Testing` and sole
writable scratch `Testing/Temporary` mode `0700`; scratch must be empty, exact owner/inode/uid/
`nlink`/mode bound, and `LastTest.log` absent/non-symlink.

The formal lease schema predeclares at most one exact child basename regex
`^\.listing-child\.prepare\.(0|[1-9][0-9]{0,9})\.([1-9][0-9]{0,9})\.([0-9a-f]{64})$`
and its child-state contract; candidate rejects this entry. Parent uses `shell=False`,
`close_fds=True`, and exact minimal `pass_fds=(3,4,5,6)` to spawn the same fixed
`/usr/bin/python3 .../task-4-v6-runner.py --formal-listing-child`; bounded stdout/stderr are `Popen`
pipe parameters, not ambient inherited FDs.
Parent retains original CLOEXEC FDs and passes fd 5 as a dup of the same locked open file
description; all unrelated FDs remain CLOEXEC/closed. Child validates fd 5 against the lease-bound
lock and fd 6 against final-lease directory inode/uid/mode. It clears only fd 5's `FD_CLOEXEC`;
fd 6 remains CLOEXEC after mkdir/state access, so the lock—but not dirfd—survives CTest exec until
process exit. These minimal nonblocking FD checks precede all other child work. Before final CTest
`execve`, fixed ps is the only permitted child helper subprocess; any additional helper must use
the same empty-pass-fds/private-stdio contract or fail before launch. Child immediately
calls shared `normalizeAndArmAlarm(15)` before setsid, ps, mkdir, fsync, or pipe work and only after
success sends byte `A` (`startup-watchdog armed`) on READY. Child's next actions are `setsid()`, require
PID=PGID, strict C/UTC ps birth hashing with `shell=False, close_fds=True, pass_fds=()`,
`stdin=DEVNULL`, and bounded private stdout/stderr pipes so none of fd 3/4/5/6 reaches `/bin/ps`,
and atomic mode-`0700` mkdirat of
`.listing-child.prepare.<uid>.<pid=pgid>.<birth64hex>` under the final-lease dirfd, then child fsyncs
that parent directory FD. It sends byte `R` (`identity ready`) and blocks for exact byte `G`; identity failure, unexpected byte, or
GO EOF exits without CTest. A spawned child attempts identity before checking parent/GO, so parent
death during startup still leaves identity unless identity creation fails, which exits without
CTest. Therefore parent death before identity/GO cannot orphan CTest, while parent death after GO
always leaves a durable identity. From spawn through child/CTest exit, inherited fd 5 also prevents
any new runner from acquiring the nonblocking cleanup lock before identity mkdir. Parent death
before spawn has no inherited copy and releases the lock; after spawn, fd 5 holds it through child/
CTest exit.

Parent records monotonic handshake start immediately before spawn and enforces a fixed 20-second
deadline over exact ordered bytes `A`, then `R`, durable child-state publication, sending `G`, and
receiving `E`. Missing/duplicate/reordered/extra/late bytes, EOF, pipe error, or child exit closes
READY/GO and waits for EOF/startup watchdog without signaling an unknown child. Startup N=15 stays
armed across setsid/ps/mkdir/fsync, READY, state publication and GO wait, expiring before parent N=20.

After ordered `A` then `R`, parent re-enumerates exactly one child directory, opens it
`O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`, holds that child dirfd through post-wait freeze, and binds stable
inode, runner uid, `nlink==2`, mode `0700`, canonical name numbers, `getpgid(pid)==pid`, live ps birth hash, and a
process group containing exactly that child. It writes `child-state.json.tmp` with framed magic
`osgsol.scienceearth.v6-listing-child.v1`, max 64 KiB, canonical JSON/checksum binding
uid/PID/PGID/birth digest, parent runner, lease hash/inode, immutable CTest/listing snapshots,
lease-lock inode/fd inheritance, lease-dirfd identity/CLOEXEC policy, exact argv/cwd/env, handshake
FDs/bytes, exact alarm normalization and 15/20/25/30-second deadlines/monotonic origins, and scratch
inode. It opens the temp with exact global `O_RDWR` flags, writes complete bytes, fchmods `0400`,
same-FD fsyncs/rechecks with no temp flag, same-directory `renameatx_np(RENAME_EXCL)` renames to
`child-state.json`, fsyncs child directory/final lease, and reopens the same target inode/hash/mode.
Then target-FD `fchflags(UF_IMMUTABLE)`, same-FD fsync/`st_flags` recheck, second child/final-lease
fsync, and final same-inode/hash/mode/flags reopen complete the target. This target completes the
predeclared formal lease state; gate runner-state and every manifest bind its hash/inode and child
identity. Parent sends `G` only after all bindings are durable.
After exact `G`, child reruns full `normalizeAndArmAlarm(25)` and sends byte `E`
(`exec-watchdog armed`). Only after successful E does it close READY/GO fds 3/4, confirm dirfd 6
remains CLOEXEC, and allow bounded stdout/stderr plus fd 5 across CTest exec. Parent starts its
30-second listing timeout at monotonic receipt/validation of E, never spawn or G. Default
disposition and N=25 survive exec and expire before parent N=30. Any normalize/ACK failure exits
without CTest as `PREFORMAL_PENDING`; parent death at any stage leaves startup or exec watchdog to
terminate a stuck child/CTest and release fd 5.

On GO child keeps the same PID/PGID and `execve`s the CTest snapshot—not source path—with fixed cwd,
argv/env, 30-second parent timeout, stdout `<=1 MiB`, stderr `<=64 KiB`, rc 0, and empty stderr:

```text
<absolute-ctest-snapshot> --test-dir <absolute-immutable-listing-snapshot> \
  -N --show-only=json-v1 -R ^osgVerse_Test_ScienceGdalLive$
```

Parent audits group membership using C/UTC `shell=False`, `close_fds=True`, `pass_fds=()`,
`stdin=DEVNULL`, bounded private stdout/stderr pipes, and
`[/bin/ps,-axo,pid=,pgid=,lstart=]`, timeout 5 seconds, rc 0, empty stderr, ASCII stdout `<=8 MiB`,
anchored numeric-PID/numeric-PGID/canonical-lstart lines, and unique PIDs: before GO the sole matching row is the child, `-N` must never
spawn a test process, and every nonblocking wait loop iteration at maximum 50 ms interval requires
the sole row to remain the child; any extra member fails. After `waitpid` returns the recorded
child, require `killpg(pgid,0)==ESRCH`, one strict ps enumeration with zero matching PGID rows, and
a second `killpg==ESRCH`. Through the held child dirfd reopen `child-state.json` with `O_NOFOLLOW`,
require same inode/hash/uid/`nlink==1`/mode `0400`/`UF_IMMUTABLE`, and same-FD fsync/recheck. Fchmod
child directory `0500`, require no self flag, fsync/recheck after mode, then FD-level
`fchflags(UF_IMMUTABLE)`, same-dirfd fsync/`st_flags` recheck, final-lease dirfsync, and no-follow
reopen/re-enumeration of child directory/state as the same inodes/hash/modes/flags. Any
failure is `PREFORMAL_PENDING`; only this sibling durability proof permits preformal. It keeps stdout
bytes only in memory. Any SIGALRM/other signal, nonzero status, parent timeout, or nonempty stderr is
`PREFORMAL_PENDING` with no gate; a second listing is forbidden. Strict JSON parsing rejects BOM,
invalid UTF-8, duplicate keys, NaN/Infinity, trailing bytes, and exceeded depth/count/string limits.
Require json-v1 root `kind=ctestInfo`, version major 1, bounded `backtraceGraph`, and exactly one test
named `osgVerse_Test_ScienceGdalLive` with a string-only command array and one effective
`WORKING_DIRECTORY`; reject missing/unknown/type-invalid fields. Cross-parse only the immutable
CTestTestfile snapshot. Derive one canonical length-bounded absolute-realpath binary/full argv/cwd
object binding v6 evidence/summary/absolute completion, five prefetch iterations, and latency, with
no v4/v5 path; create and pre/post bind the formal immutable live-binary snapshot.

After rc 0 and empty stderr, enumerate scratch without following links. Require exactly one regular
runner-owned `Testing/Temporary/LastTest.log`, `nlink==1`, size `<=256 KiB`, bounded UTF-8 text, and
content consistent with listing-only/zero test start, network request, evidence, or completion.
Validate every line against the finite anchored CTest json-v1 log grammar embedded in the runner
template; reject any nonmatching line.

Through held scratch dirfd open LastTest once with `O_RDWR|O_NOFOLLOW|O_CLOEXEC`; that same FD
validates regular type/inode/uid/`nlink==1`/size/content and computes raw SHA-256, fchmods `0400`,
fsyncs/rechecks, then FD-level `fchflags(UF_IMMUTABLE)` and same-FD fsync/`st_flags` recheck. Fsync
scratch, fchmod scratch `0500`, require its self flag absent, fsync/recheck after mode, then FD-level
`fchflags(UF_IMMUTABLE)`, same-dirfd fsync/`st_flags` recheck, and `Testing` dirfsync. Reopen scratch via held `Testing` dirfd with
`O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC`, require identical inode/uid/`nlink`/mode/flags, enumerate only
LastTest, and reopen it no-follow to require identical inode/hash/uid/`nlink`/mode/flags.

Fsync bottom-up every held directory in the actual path: reopened scratch, `Testing`, immutable
listing root, each no-follow parent through `.superpowers/sdd`, and final lease after recording the
binding. Treat the frozen child-directory/state sibling proof as part of this same gate-predecessor
chain. Reopen/re-enumerate the full chain from held parent dirfds and reconfirm each
inode/type/uid/`nlink`/mode/flags plus final log hash. Only this entire chain may feed preformal PASS
or gate preparation. File fsync, scratch fsync/freeze, each-level fsync, reopen, enumeration, or
recheck failure is `PREFORMAL_PENDING` with no gate. Missing/truncated LastTest from a legitimate
crash is recoverable PENDING, never gate permission; extra/symlink/hard-link/special or
invalid-content tamper is STOP. Bind the log hash and complete directory inode/mode/flag chain in
preformal, gate
runner-state, manifests and final auditor. No live test or live-output path may be touched.

Runner does not execute a verifier path or any second child. It opens tracked
`tests/science_v6_authorizer.py` with `O_NOFOLLOW`, uses the same FD for UTF-8/LF/schema,
`fstat`, and SHA-256, and accepts only the expected SHA literal embedded in the independently bound
runner template. It then compiles/executes only those verified bytes in an isolated namespace and directly calls
`verify_preformal(canonical_tuple) -> <64-lowercase-hex-digest>`. The object remains in-process and
never enters argv/env/temp. Runner independently canonicalizes/recomputes the digest. The API binds
candidate PASS, promotion, frozen v4/v5, components/wrappers/snapshots, child identity/state,
PID/PGID/birth digest, `A/R/G/E`, normalized dual-watchdog/deadline and empty-group proofs, frozen
child-directory/state sibling durability, plus LastTest/scratch/Testing/listing-root durability
chain, exact temp-open/post-rename-FD-flag contracts, and the permanent gate's unflagged-prepare/
first-rename/first-parent-fsync/same-inode reopen/FD-self-flag/gate-fsync/second-parent-fsync/final-
reopen contract, plus formal public-path absence. Update the independently bound authorizer schema,
runner-state/manifest expectations, runner template, and authorization-bound runner-template SHA-256
as one reviewed Task 3 change; its separate embedded verifier-source SHA check remains mandatory,
and any stale hash must fail authorization.

Listing/preformal/configuration/path failure leaves gate/log/state/output/summary/completion/
manifest/finalizer absent but may leave fully validated immutable offline CTest/live snapshots.
This is `PREFORMAL_PENDING`, invocation 0, and can be fixed/retried because no gate exists; user may
retry only through the same strict dead-owner lease cleanup. Empty/truncated regex-valid prepares
from legitimate crashes remain PENDING/recoverable, not STOP. A present child identity is recovered
from its name even when empty or child-state temp is truncated. Require stable name-bound uid/inode,
directory `nlink==2` and mode `0700`; after verified target/death/group proof, unflagged `0500` is
the post-mode recovery window and `0500+UF_IMMUTABLE` is sibling-complete. Permit only empty; one
regular same-uid stable-inode `nlink==1` exact-open `0600` temp `<=64 KiB` whose bytes are any valid
frame prefix; one unflagged `0400` temp only when post-chmod same-FD fsync/recheck, the entire frame/
schema/checksum, and all lease/child/snapshot/fd/scratch plus watchdog/ACK/deadline cross-bindings
pass; one equally complete `0400` target missing only post-rename `UF_IMMUTABLE`; or the same target
flag-complete. Missing flag remains PENDING and may resume only under death/lock proof. Temp and target never
coexist. Cleanup then requires independent
stable death proofs for runner and child PID/birth, plus
`killpg-ESRCH -> strict ps zero group rows -> killpg-ESRCH`. Live/EPERM, extra group member,
ps timeout/rc/stderr/size/ASCII/line/duplicate/parse or killpg anomaly, PID/PGID or birth/group
change deletes no child state, scratch, or snapshot.
Recovery never signals an unknown or surviving child; it waits for fd 5 lock release, then repeats
the strict child PID/birth and PGID death proof.
Only after both owners and group are dead may final-lease allowlisted cleanup proceed. With no
child identity, the GO invariant proves no CTest exec authorization, while any pre-identity child
still holds fd 5 and blocks cleanup until its EOF exit. Only malformed/multiple names,
metadata tamper, unexpected entries, or an
irreconcilable complete-lease/public-path mismatch may be bound as `PREFORMAL_STOP`.
`NOT_AUTHORIZED` means candidate was not
PASS: common/wrappers exist, while all formal snapshots and public paths are absent, invocation 0.

Only after preformal PASS does runner complete formal gate first rename, first parent fsync, same-
inode reopen, FD self-flag, gate fsync/recheck, second parent fsync, and final immutable reopen. Only
then does it spawn no child and create/fsync/parent-fsync/re-enumerate the same-inode log through the identical durable
protocol while the gate already contains its marker, invokes shared `normalizeAndArmAlarm(300)`
(any failure becomes post-marker `EXEC_STOP`), revalidates the same snapshot FD plus immutable
read-only directory/file, and `os.execve`s the snapshot with exact argv/cwd including fixed formal
`--completion-json`. No live CTest is allowed. Apply the identical prioritized post-gate states:

| Priority | Formal state | Predicate | Bottom | Decision |
|---:|---|---|---:|---|
| 1 | `FINALIZATION_BLOCKED` | gate content missing/invalid/damaged/replaced; fixed gate absent after trusted observation/recording; present log fails exact type/inode/uid/`nlink`/mode contract; or valid identity/death plus irreconcilable obstruction | 0..1 | read-only `STOP`; no chmod/PID guess/rerun/fake manifest |
| 2 | `FINALIZATION_PENDING` | complete fixed gate lacks only self flag; identity liveness is inconclusive; or proved-dead gate-self-flag/publication is recoverably incomplete | 0..1 | FD-only flag/offline finalizer retry; no log/exec/rerun |
| 3 | `RUNNING` | self-flag-complete valid identity and live PID hashes to the recorded birth digest under strict kill/ps check | 0..1 | wait only |
| 4 | `NO_EXEC` | finalized; original dead; valid gate/marker and durable-log-before-alarm contract bind, but exact post-gate log is absent | 0 | `STOP` |
| 5 | `EXEC_STOP` | finalized; marker and exact same-inode durable log exist; valid completion/PASS absent | 0..1 | `STOP` |
| 6 | `COMPLETE_PASS` | finalized; marker/exact durable log and snapshot-bound completion plus complete summary/proofs valid | 1 | `GO` |

After formal PASS, every CTest regression must use an exact offline whitelist or exclude
`^osgVerse_Test_ScienceGdalLive$`; every other wrapper must prove it cannot pass `--live-cases` or
write formal-v6. Re-audit the frozen formal invocation count as exactly 1 after every later gate.

- [ ] **Step 5: Bind one honest decision and obtain final reviews**

`OFFLINE_PENDING`, `PREFORMAL_PENDING`, `RUNNING`, or recoverable `FINALIZATION_PENDING` is
nonterminal and cannot be committed. Repair/retry only offline work; never repeat a runner whose
permanent gate exists. A held/contended lease lock is PENDING, never cleanup authority.
`FINALIZATION_BLOCKED` is the sole incomplete-publication STOP exception: a
read-only auditor binds the exact obstruction and every observable presence/type without inventing
a manifest. The final auditor selects exactly one branch:

| Branch | Candidate binding | Formal binding | Decision |
|---|---|---|---|
| Candidate non-PASS | user-bound read-only `OFFLINE_STOP`, finalized `NO_EXEC`/`EXEC_STOP`, or read-only `FINALIZATION_BLOCKED`; bind lease/common/wrappers/snapshot/completion presence and exact obstruction/final artifacts | formal wrapper/common exist; formal lease/snapshots and all public paths/completion absent, invocation 0 (`NOT_AUTHORIZED`) | `STOP` |
| Candidate PASS + formal non-PASS | complete candidate including valid completion; formal is user-bound `PREFORMAL_STOP`, finalized `NO_EXEC`/`EXEC_STOP`, or read-only `FINALIZATION_BLOCKED` | preformal stop binds actual immutable offline-snapshot presence/absence while gate/log/state/output/completion/finalizer remain absent and invocation 0; post-gate branches bind all snapshots/completion and exact 0..1 range | `STOP` |
| Candidate and formal PASS | both frozen gates/state/markers/log/snapshots/output/manifests/summaries/completions/finalizer records present; temps absent | candidate bottom 1 and formal bottom 1; neither PASS depends on exit status | `GO` |

Every terminal branch commits only the honest decision documents and obtains fresh specification,
quality, and whole-range reviews. Do not tag, push, package, or touch Desktop in Task 4.

---

## Task 5: On formal G0 GO, continue G1 to the fixed human-test app

**Authority:** The user's Goal instruction authorizes this conditional continuation after formal
G0 `GO`, but not tag or sync.

- [ ] **Step 1: Refresh the approved G1 execution map**

Map Tasks 7–15 of
`docs/superpowers/plans/2026-07-11-scienceearth-g0-g1-alphaearth-vertical-slice-plan.md` to the
actual v6 GO HEAD. Preserve GDAL-free ScienceCore, isolated optional GDAL provider, cancellable
jobs, bounded science cache, one shared Agent/UI runtime, cross-source research, artifact layer
without camera mutation, and protected app behavior. Explicitly remove that older plan's tag/push
steps from execution authority.

- [ ] **Step 2: Execute G1 task by task with fresh reviews**

Every G1 task gets a fresh implementer, RED/GREEN focused tests, specification review,
code-quality review, and commit. Complete science-off/on, offline, cancellation, cache, dependency,
live, application regression, and package-readiness gates before staging.
Any G1 CTest command still excludes `osgVerse_Test_ScienceGdalLive` (or uses an exact whitelist),
and any distinct G1 network check is explicitly named and cannot reuse the G0 formal command,
evidence root, or `--live-cases` wrapper. Re-audit the formal process ledger after every G1 batch.

- [ ] **Step 3: Stage, audit, sign, launch, and atomically update the fixed app**

Build into a sibling staging bundle. Require package manifest, dependency closure, no private path
leak, strict/deep codesign, clean-machine launch smoke, rollback bundle, and unchanged user data.
Only then atomically replace:

```text
/Users/USER/Desktop/osgSol Earth.app
```

- [ ] **Step 4: Deliver the human-verification report**

Report build commit, app fingerprint, G0/G1 totals, package/codesign/launch results, rollback path,
and manual checks for AlphaEarth NVIDIA/Hong Kong correctness/latency; Agent cross-source
search/start/poll/show; panel scroll down/back up; clean reset of science/satellite tracks/range
circles; Hong Kong 3D Tiles/terrain quality; middle-drag selection stability and altitude; visible
NVIDIA photo viewpoint with no Hong Kong reuse; and same-path relaunch/update.

The Goal reaches human-verifiable readiness only after this report and fixed app exist. Do not mark
it complete merely because v6 G0 or G1 code compiled.
