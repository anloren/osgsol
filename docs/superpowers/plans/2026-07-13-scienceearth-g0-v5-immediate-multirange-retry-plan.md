# ScienceEarth G0 v5 Immediate Multi-Range Retry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:subagent-driven-development` to execute this plan task by task. Every implementation
> task gets a fresh implementer, specification review, and code-quality review. Use
> `superpowers:test-driven-development`, `superpowers:systematic-debugging`, and
> `superpowers:verification-before-completion` at their required boundaries.

**Goal:** Close the two v4 coordinator retry safety gaps, remove the observed all-siblings retry
barrier for only the AlphaEarth object path, requalify G0 without weakening science or latency
gates, then continue the approved G1-to-fixed-Desktop workflow until the app is ready for human
verification.

**Architecture:** The private GDAL patch validates immutable HEAD and failed Range transport before
coordinator retry. A separate exact-path option selects an event-driven `ReadMultiRange()` branch
where each failed HTTP/2 Range keeps its own native retry due time on the same easy/multi handle
while unrelated siblings stay in flight. Upstream behavior remains intact when the option is
absent or only global. Frozen v4 formal evidence is the pre-v5 control; v5 candidate and formal
evidence live in new roots.

**Tech stack:** C++17, private static GDAL 3.13.1 patch, Apple/libcurl 8.7.1 HTTP/2 multi API,
Node.js TLS HTTP/2 fixture, test-only curl interposition, CMake/CTest, Bash/Python audit tooling,
macOS Mach-O/codesign packaging.

## Global constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on
  `codex/science-earth-g0-g1`. The v5 design base is `f0cfbc3` and the exact-five audit correction
  is `377dff7`.
- Never edit, chmod, delete, recreate, or rerun old, v2, v3, or v4 evidence. In particular,
  `build/science_g0_prefetch/formal-evidence-v4` remains the immutable pre-v5 control.
- Coordinator retry codes and immediate ordinary retry codes are exactly
  `429,500,502,503,504`; maximum retry remains `3`; initial delay remains `0.1` seconds.
- HEAD is sent once by the coordinator. No coordinator retry is allowed before HEAD is complete,
  valid, redirect-free HTTP/2 with a positive content length and an available connection ID.
- Every failed coordinator Range attempt must be redirect-free HTTP/2 on that HEAD connection
  before `CanRetry()` is called. Invalid transport means zero retry and zero cache publication.
- Immediate ordinary retry is enabled only by path-specific
  `OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY=YES`. A global value alone must not activate it.
- Baseline/optimized profiles and all unrelated application features retain upstream GDAL
  `ReadMultiRange()` behavior. The prefetch profile installs both path options only for its exact
  dataset lifetime.
- No Range widening, gap merge, extra successful Range, warm-up request, no-HEAD shortcut,
  threshold change, byte-ceiling change, or retry-policy broadening is allowed.
- NVIDIA HQ and Hong Kong each require five complete proofs, median `<=3000 ms`, P95 `<=8000 ms`,
  conservative body upper bound `<=16 MiB`, exact scientific/georeference/RGB/NoData correctness,
  and zero terminal metadata fallback.
- `/Users/USER/Desktop/osgSol Earth.app` remains untouched until formal G0 `GO`, the approved
  G1 implementation/gates, package audit, codesign, and launch smoke all pass. Update that fixed
  app path; do not create another app name.
- Do not tag, push, publish a release, or modify another repository. Tag/sync remain a separate
  explicit user decision after human testing.

---

## Task 1: Add deterministic RED contracts for safety and overlap

**Files:**

- Modify: `tests/science_curl_remove_fault.cpp`
- Modify: `tests/science_http2_range_server.mjs`
- Modify: `tests/science_gdal_network_test.cpp`
- Modify: `tests/science_deps_script_tests.sh`

**Produces:**

- local coordinator cases that fail if HEAD/transport is checked after retry;
- local `VSIFReadMultiRangeL()` chronology that fails behind the current batch barrier;
- exact blocked and immediate-retry proof schemas;
- source contracts for exact-path activation and exact five statuses.

- [ ] **Step 1: Extend only the test interposer for deterministic transport faults**

Interpose `curl_easy_getinfo()` alongside the existing remove-handle fault. Forward every output
pointer to real libcurl, then consume a one-shot environment request only for the next matching
`CURLINFO_CONN_ID` or `CURLINFO_REDIRECT_COUNT` query. The C++ test sets the request immediately
before opening the selected case and verifies it was consumed. Do not add any production test
option or patch string such as `PREFETCH_TEST_*`.

Mac and Linux implementations must preserve the existing remove fault and use one shared,
thread-safe one-shot counter. A variadic wrapper extracts the single output pointer and forwards it
to the real function before changing only the requested test value.

- [ ] **Step 2: Add combined coordinator fail-closed cases**

Extend the TLS fixture/case matrix with:

```text
range-500-once + head-503
range-500-once over HTTP/1.1
range-500-once + interposed different connection ID
range-500-once + interposed redirect count 1
```

The server returns a valid 206 on a second Range, so an erroneous retry is observable. Require for
each invalid initial attempt:

- the first Range count is exactly one;
- `ParallelHeadRange: transient-retry` count is zero;
- `ParallelHeadRange: published` count is zero;
- one anchored `transient-retry-blocked` event names the exact status and reason;
- fallback/rejection stays safe and no stale first interval enters the cache;
- any later ordinary HEAD recovery cannot legitimize or publish the invalid Range.

Keep all five existing valid transient-once cases and require their current successful retry and
publication behavior.

- [ ] **Step 3: Add a real HTTP/2 multi-range overlap fixture**

Add fixture mode `multirange-500-overlap`. It accepts exactly these independent intervals:

```text
bytes=262144-327679   # valid 206 delayed about 700 ms
bytes=393216-458751   # immediate 500 once, then exact 206
bytes=524288-589823   # valid 206 delayed about 200 ms
```

The total attempted body remains within the existing local fixture budget. Log monotonic stream
start, headers, and end timestamps. Reject commas, any other interval, full-body GET, wrong method,
or budget overrun.

Call `VSIFReadMultiRangeL()` with three buffers and compare every byte with the fixture. With the
path option active, require:

- three overlapping initial streams on one HTTP/2 session;
- only the failed interval appears twice;
- one exact immediate event;
- retry stream start at least declared delay minus 999 microseconds after the transient response;
- retry stream start before the slow sibling ends;
- exact response/request/body/network-stat reconciliation.

Run a global-only activation case and require no immediate event plus upstream batch chronology.
The existing optimized local read remains the option-absent case.

- [ ] **Step 4: Extend fail-closed proof parsing**

Add:

```cpp
struct ImmediateRetryEvidence
{
    std::string range;
    int code = 0;
    std::uint64_t bytes = 0;
    int attempt = 0;
    long long delayMs = 0;
    long long connectionId = -1;
    int httpMajor = 0;
};
```

Use one anchored regex for the exact event:

```text
VSICURL: ReadMultiRange: immediate-retry range=bytes=<START>-<END> status=<CODE> bytes=<N> attempt=<N> delay-ms=<N> connection=<ID> http=2
```

Require one-to-one reconciliation with the existing ordinary `HTTP error code for ... Retrying`
event, one transient response body, and the next request for the same interval. Require contiguous
attempts `1..3`, native delay envelopes, HTTP/2, nonnegative connection, no early request, and no
malformed/duplicate/unmatched event. Serialize per-iteration and aggregate immediate retry counts,
bytes, and code maps without changing ordinary retry totals.

- [ ] **Step 5: Lock source contracts and observe RED**

The shell contract must require:

- exactly five cases in the shared transient classifier;
- the exact new path option and a comparison that rejects global-only activation;
- `curl_multi_info_read`, `curl_multi_poll`, per-request due time, same-handle re-add, and exact
  immediate event fields;
- HEAD validation and Range protocol/redirect/connection checks textually before the coordinator
  `CanRetry()` call;
- the exact blocked event;
- absence of production fault controls and status 408 in either classifier.

Run:

```bash
cmake --build build/science_g0_prefetch \
  --target osgVerse_Test_ScienceHttpRanges osgSol_Test_CurlRemoveFault --parallel 8
ctest --test-dir build/science_g0_prefetch \
  -R '^osgVerse_Test_ScienceHttpRanges$' --output-on-failure
bash tests/science_deps_script_tests.sh
```

Expected RED against the existing private archive/patch: invalid coordinator transport still
retries, immediate event is absent, the second multirange request starts only after the slow sibling
finishes, and the source contract lacks the new implementation.

- [ ] **Step 6: Commit only the RED contract**

```bash
git diff --check
git add tests/science_curl_remove_fault.cpp tests/science_http2_range_server.mjs \
        tests/science_gdal_network_test.cpp tests/science_deps_script_tests.sh
git commit -m "test(scienceearth): expose v5 retry barriers"
```

Record the exact RED command and failure in `.superpowers/sdd/progress.md` with `apply_patch`.

---

## Task 2: Implement coordinator guards and per-handle retry scheduling

**Files:**

- Modify: `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`
- Modify: `packaging/science_deps/versions.env`
- Modify if the proof schema requires final wiring:
  `tests/science_gdal_network_test.cpp`

**Produces:**

- immutable HEAD validation before coordinator retry;
- transport-valid coordinator retry only;
- exact-path event-driven ordinary retry;
- one clean rebuilt and pinned private dependency.

- [ ] **Step 1: Add a pure coordinator retry-eligibility helper**

Represent immutable HEAD and current Range attempt transport in small internal structs or explicit
arguments. The helper returns one of:

```text
eligible, head-invalid, head-transport, head-redirect, head-protocol,
head-connection, range-transport, range-redirect, range-protocol, range-connection
```

Capture HEAD status, content length, HTTP version, redirect count, and connection ID immediately
after the first parallel multi completes. Before every transient `CanRetry()` call, require the
helper result `eligible`. Otherwise emit the exact blocked event, set the existing safe reason,
clear any Range buffer, and leave the loop without publication.

Do not call `CanRetry()` while HEAD is incomplete/invalid or current Range transport differs. Do
not defer this check to final 206 publication validation.

- [ ] **Step 2: Add exact-path detection to `ReadMultiRange()`**

Resolve `OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY` for `m_osFilename` and compare it with the
global option exactly as the approved path-only activation contract requires. Enter the new branch
only for ordinary HTTP(S), path-specific true, `GDAL_HTTP_MULTIRANGE=PARALLEL`, and more than one
merged request. Every other call executes the untouched upstream loop.

- [ ] **Step 3: Implement a per-request state machine**

Each merged request owns stable attempt state:

```text
easy handle, headers/range string, body/header writers, curl error buffer,
attached/completed/pending flags, retry due time, retry context, attempt connection
```

Initial handles are added together. The event loop must:

1. call `curl_multi_perform()`;
2. drain all `CURLMSG_DONE` records;
3. remove each completed handle immediately;
4. validate exact success or classify an exact transient;
5. schedule eligible retries independently;
6. reset only that handle's writers/error buffer before re-add;
7. re-add the same easy handle and identical Range at its due time;
8. poll no longer than the earliest due time while siblings remain active;
9. use a bounded sleep only when no handle is active;
10. clean every owned handle/header/buffer once on every exit.

An immediate attempt is eligible only when its failed transfer is `CURLE_OK`, redirect-free
HTTP/2, exact transient status, and native retry budget remains. Permanent failure, exhausted
budget, interruption, malformed/short 206, add/remove error, or non-HTTP/2 transport sets
`nRet=-1` and cannot copy that request into caller output.

Use the same `CPLHTTPRetryContext` delay and emit the exact event when the due time is fixed. Keep
`NetworkStatisticsLogger::LogGET()` logical-success semantics unchanged; raw curl and network
statistics continue to account physical transient bodies.

- [ ] **Step 4: Update the patch pin and clean rebuild once**

Update `OSGSOL_GDAL_PATCH_SHA256`. Verify ownership markers and real paths before deleting only the
owned `build/science-deps-prefetch` root. Re-seed only checksum-verified pinned archives from the
local cache and run:

```bash
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --build --jobs 4
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --verify
cmake --build build/science_g0_prefetch --target \
  osgVerse_Test_ScienceGdalSpike osgVerse_Test_ScienceHttpRanges --parallel 8
bash tests/science_deps_script_tests.sh
ctest --test-dir build/science_g0_prefetch \
  -R 'ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer' \
  --output-on-failure
```

Expected GREEN: all coordinator invalid-transport cases have zero retry/publication; all valid
exact-five cases recover; the local immediate retry begins before the slow sibling ends; exact
bytes match; global-only/absent cases retain upstream behavior; prior overflow, detach, content
range, HTTP version, connection, replay, stats, and oracle tests stay green.

- [ ] **Step 5: Commit and run task reviews**

```bash
git diff --check
git add packaging/science_deps/gdal-3.13.1-parallel-head-range.patch \
        packaging/science_deps/versions.env tests/science_gdal_network_test.cpp
git commit -m "fix(scienceearth): overlap bounded range retries"
```

Specification review must inspect the actual patch order around `CanRetry()` and the event loop,
not only shell substrings. Code-quality review must focus on multi-handle ownership, double cleanup,
buffer lifetime, retry timer precision, interruption, partial caller output, and unchanged default
branch behavior. Address every Critical/Important finding before proceeding.

---

## Task 3: Run complete offline authorization and freeze v5 absence

**Files:**

- Modify: `docs/scienceearth/gdal-build.md`
- Modify: `docs/scienceearth/g0-measurements.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`

**Produces:** `PUBLIC_REQUALIFICATION_V5=AUTHORIZED_NOT_RUN` with G0 still `STOP` and Desktop
unchanged.

- [ ] **Step 1: Run all non-public gates from clean tracked HEAD**

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
bash tests/science_deps_script_tests.sh
ctest --test-dir build/osgsol_core --output-on-failure
ctest --test-dir build/science_g0_prefetch --output-on-failure \
  -R 'ScienceEarthRelease|AefIndexTool|ScienceG0Manifest|ScienceDepsScript|ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer'
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --verify
```

Record discovered/run/pass totals rather than assuming old counts. Zero skip/failure is required.

- [ ] **Step 2: Rebuild and audit the disposable probe**

Require strict/deep codesign, no unresolved dependencies, Tier A `0`, Tier B new/removed `0/0`,
delta below `40 MiB`, science closure below `60 MiB`, only the approved probe export, and no
worktree/private-prefix strings or load commands.

- [ ] **Step 3: Verify every protected invariant**

Recompute:

- private patch/prefix/archive provenance;
- all old/v2/v3/v4 evidence file hashes, complete sets, and 0400/0500 modes;
- corrected v4 auditor clean-HEAD wrapper digests and PASS;
- fixed Desktop fingerprint `91fa216f...e18`, helper `14d88b...214`, 414 entries, 355 regular
  non-symlink files;
- no forbidden source/build graph dependency in the main app;
- no protected camera/photo/terrain/3D Tiles/panel/satellite regression diff.

Require these v5 paths to be absent and non-symlink before authorization:

```text
build/science_g0_prefetch/requalification-evidence-v5/candidate
build/science_g0_prefetch/requalification-evidence-v5/candidate-summary.json
build/science_g0_prefetch/formal-evidence-v5
```

- [ ] **Step 4: Add a reproducible, base-bound authorization verifier**

The committed verifier must bind the exact implementation commit, allowed tracked scope, patch and
prefix hashes, all offline exits/totals, immutable evidence sets, Desktop tuple, formal CMake
profile/path, and v5 absence. It must reject dirty protected files, unexpected v5 artifacts, hash
drift, or broader retry codes.

- [ ] **Step 5: Commit authorization and review**

End the record with:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V5=AUTHORIZED_NOT_RUN
DESKTOP_PACKAGE=NOT_READY
```

Commit documentation and obtain specification/code-quality review plus a whole-range review from
the v5 design base through authorization HEAD. No public request occurs in this task.

---

## Task 4: Run the one-shot v5 candidate and authoritative formal gate

**Files:**

- Modify: `tests/CMakeLists.txt` only if the formal path is not already `formal-evidence-v5`
- Modify: `docs/scienceearth/g0-measurements.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`

**Produces:** immutable v5 candidate/formal artifacts and one honest G0 decision.

- [ ] **Step 1: Run preflight with network disabled**

Verify clean HEAD, authorization binding, exact absent roots, frozen v4 control, proxy/environment
record, local clock, available disk, patch/prefix, target binary hash, Desktop tuple, and formal
CTest command. Preserve and hash the preflight transcript.

- [ ] **Step 2: Run exactly one candidate process**

Use the existing live executable with `--profile prefetch`, two pinned cases, five iterations,
`--enforce-latency`, and only the v5 candidate root/summary. Never retry, resume, overwrite, or
delete it.

Freeze all candidate files 0400 and root 0500. Produce a complete manifest. A candidate `PASS`
requires all ten proofs, both latency limits, zero terminal fallback, exact coordinator/immediate
events, exact request/body/network-stat reconciliation, exact science correctness, and no broader
status.

- [ ] **Step 3: Only candidate PASS authorizes one formal process**

If needed, make a CMake-only commit changing the formal output path from v4 to v5; do not change
profile, iterations, limits, cases, binary, or implementation. Configure/build without network,
verify candidate/implementation hashes again, then run exactly:

```bash
ctest --test-dir build/science_g0_prefetch \
  -R '^osgVerse_Test_ScienceGdalLive$' --output-on-failure
```

Freeze formal files and root, create one complete manifest, and preserve the exact transcript/exit.
Any nonzero exit, incomplete proof, fallback, mismatch, or latency failure is `STOP`. Do not rerun.

- [ ] **Step 4: Record the decision fail closed**

For formal PASS:

```text
G0_DECISION=GO
PUBLIC_REQUALIFICATION_V5=PASS
DESKTOP_PACKAGE=NOT_READY_G1_PENDING
```

For any failure:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V5=FAIL
DESKTOP_PACKAGE=NOT_READY
```

A v5 STOP does not end the active user goal. Preserve it and return to a new v6 diagnostic/design
namespace. Never mutate or resample v5.

- [ ] **Step 5: Commit evidence bindings and obtain final v5 review**

The committed decision auditor must recompute substantive results from frozen raw/stats/proof
triplets and bind source/transcript digests. It must state honestly that absolute negative process
history cannot be proven. Whole-range review must report no Critical/Important production finding
before G1 begins.

---

## Task 5: On G0 GO, execute the approved G1 vertical slice to a fixed Desktop app

**Authority:** The user's latest instruction—make this a Goal and continue until a human can
verify—authorizes the conditional G1 continuation after formal G0 `GO`. It does not authorize tag
or sync.

**Plan source:** Execute Tasks 7–15 of
`docs/superpowers/plans/2026-07-11-scienceearth-g0-g1-alphaearth-vertical-slice-plan.md`, refreshed
against current HEAD before edits. Preserve its architecture boundaries:

- GDAL-free `ScienceCore` contracts and registry;
- cancellable asynchronous research jobs and bounded science-only cache;
- optional isolated GDAL provider plugin;
- compact AlphaEarth index/provider and exact RGB/science evidence;
- dedicated artifact layer that never changes camera, terrain, 3D Tiles, or photo state;
- Agent tools for search/start/poll/show across sources, with AI present end to end;
- one shared runtime for Agent and UI, no second query path;
- experimental panel section with scroll-down-and-back-up regression;
- complete science-off/science-on, offline, cancellation, cache, link, live, and package gates.

Before Task 7, produce an additive refreshed G1 execution plan that maps every old file/target to
current HEAD and rechecks the protected baseline. Then execute each G1 task with a fresh implementer,
specification review, code-quality review, focused tests, and commit.

Packaging occurs only after the complete G1 automated gate passes. Build into a sibling staging
bundle, audit and launch it, retain rollback, then atomically replace only:

```text
/Users/USER/Desktop/osgSol Earth.app
```

The final handoff report must include commit, app fingerprint, package audit/codesign/launch results,
all G0/G1 test totals, and a manual checklist covering:

1. AlphaEarth NVIDIA and Hong Kong source/year/orientation/RGB/evidence/latency;
2. Agent search/start/poll/show and cross-source research without camera movement;
3. panel scroll down and back up;
4. clean reset removes science, satellite tracks/selections, and range circles;
5. Hong Kong 3D Tiles refinement and terrain without tofu, holes, scale mismatch, or distortion;
6. middle-drag selected-ground stability and no altitude oscillation;
7. NVIDIA visible-view photo without top-view change or Hong Kong image reuse;
8. offline/plugin-missing/cancel/cache-clear/relaunch/update-at-same-path behavior.

At that point the Goal is ready for the user to perform real human verification. Do not mark the
Goal complete merely because the package was built; complete it only when all requested automated
work and the handoff are actually delivered.
