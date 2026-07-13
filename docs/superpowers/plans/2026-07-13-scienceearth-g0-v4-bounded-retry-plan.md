# ScienceEarth G0 v4 Bounded Retry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Recover the isolated concurrent AlphaEarth metadata Range from configured transient HTTP responses, prove the behavior without weakening G0, and either freeze an evidence-backed v4 `STOP` or produce the formal `GO` that unblocks the already approved G1-to-manual-test continuation.

**Architecture:** The private GDAL patch reuses the coordinator's Range easy handle and multi-handle connection cache, applies only `CPLHTTPRetryContext`, and never repeats HEAD. The C++ proof builder distinguishes coordinator retries, coordinator terminal fallback, and ordinary CPL retries, while the local HTTP/2 server and a sanitized v3 chronology test exact events, connection identity, byte accounting, and fail-closed mutations. Public qualification remains one control process plus one candidate process, followed conditionally by one formal run.

**Tech Stack:** C++17, GDAL 3.13.1 private static patch, libcurl 8.7.1 multi/HTTP2, Node.js HTTP/2 fixture, CMake/CTest, Bash/Python audit tooling, macOS Mach-O/codesign verification.

## Global Constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on `codex/science-earth-g0-g1`; the v4 implementation base is `365b250`.
- Prefetch remains exact-path opt-in through `OSGSOL_VSICURL_PREFETCH_HEAD_RANGE=YES`. Baseline, optimized, normal osgVerse/osgSol GDAL, terrain, 3D Tiles, photo, media, and unrelated `/vsicurl/` paths remain unchanged.
- Retry only the coordinator's exact initial `Range: bytes=0-131071`; send HEAD exactly once; reuse the same Range easy handle and multi handle; consume only `CPLHTTPRetryContext` with the configured maximum `3`, initial delay `0.1` seconds, and codes exactly `429,500,502,503,504`.
- The exact retry event is one line with fixed field order: `ParallelHeadRange: transient-retry range=bytes=0-131071 status=<CODE> bytes=<N> attempt=<1..3> delay-ms=<N> range-connection=<ID> range-http=<MAJOR>`.
- The terminal event remains `ParallelHeadRange: transient-fallback range=bytes=0-131071 status=<CODE> bytes=<N>`. Passing evidence permits zero terminal fallback.
- `ParallelHeadRange: logical-get-complete bytes=<N>` keeps its v3 meaning: final successful or terminal-fallback response bytes only. VSINetworkStats logs one logical GET whose downloaded bytes include discarded coordinator retry bodies plus the final body.
- Every successful retry proof requires exact final HTTP 206, exact 131072 bytes, one cache publication, HTTP/2, the HEAD connection identity on every attempt, exact request/event/response accounting, and no fallback.
- NVIDIA HQ and Hong Kong each require five complete candidate iterations, median `<= 3000 ms`, P95 `<= 8000 ms`, the existing `<= 16 MiB` byte ceiling, and unchanged scientific/georeference correctness.
- The rejected control summary remains `f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096`; rejected candidate summary `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`; rejected candidate raw `3312a78a073140ea1422ece0ffbc927ce853a4d7b8cec02f3033734a7e217a3a`; rejected candidate stats `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe`.
- Older formal summaries remain baseline `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed` and optimized/live `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe`.
- The v2 control summary remains `0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0`; v2 candidate summary remains `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`; v2 Hong Kong iteration 5 raw remains `f32cfdfd05f50a3de8f34b4f69e1e2cefe61d667e20936ddcf86f1fa8b1a429e`; matching stats remains `f54110c20ab81230c5d3af708f2df4d9d8754e5957366875c162b0395d2c1974`. Every other v2/v3 hash recorded in `docs/scienceearth/g0-measurements.md` remains exact and every frozen artifact stays non-writable.
- `/Users/USER/Desktop/osgSol Earth.app` remains fingerprint `91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`, helper digest `14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214`, `414` entries, and `355` regular non-symlink files until G0 `GO`, G1 completion, and package verification.
- New public evidence may be written only below initially absent roots `build/science_g0_prefetch/requalification-evidence-v4/control`, `build/science_g0_prefetch/requalification-evidence-v4/prefetch`, and conditionally `build/science_g0_prefetch/formal-evidence-v4`. A profile is never rerun to obtain favorable samples.
- Do not tag or push. G0 `GO` continues under a refreshed G1 Tasks 7-15 plan and may update only `/Users/USER/Desktop/osgSol Earth.app` through verified staging and atomic replacement. Release remains a separate explicit decision.

---

### Task 1: Implement bounded coordinator retry under deterministic TDD

**Files:**
- Modify: `tests/science_http2_range_server.mjs`
- Modify: `tests/science_gdal_network_test.cpp`
- Modify: `tests/science_deps_script_tests.sh`
- Create: `tests/data/science/prefetch_nvidia_transient_retry_trace.log`
- Create: `tests/data/science/prefetch_nvidia_transient_retry_stats.json`
- Modify: `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`
- Modify: `packaging/science_deps/versions.env`

**Interfaces:**
- Produces `IsParallelHeadRangeTransientStatus(long)` in the private GDAL patch as the sole coordinator status classifier.
- Produces `CoordinatorRetryEvidence { range, code, bytes, attempt, delayMs, connectionId, httpMajor }` in the test proof builder.
- Extends `HttpProof` with `coordinatorTransientRetryCount`, `coordinatorTransientRetryBytes`, and `coordinatorTransientRetryCodes` while preserving ordinary `transientRetry*` and terminal `coordinatorTransientFallback*` fields.
- Extends `MetadataPrefetchProof` with ordered coordinator retry evidence and accepts one to three retries only when every request, status, event, connection, protocol, delay, and byte count reconciles.

- [ ] **Step 1: Add all five positive HTTP/2 retry cases and observe the current runtime fail**

Extend the server mode set and response selection exactly as follows:

```javascript
const TRANSIENT_ONCE_MODES = new Map([
    ['range-429-once', 429],
    ['range-500-once', 500],
    ['range-502-once', 502],
    ['range-503-once', 503],
    ['range-504-once', 504],
]);
const MODES = new Set([
    'success', 'range-200', 'range-200-body', 'short-range',
    'size-mismatch', 'range-503', 'range-503-exhaust',
    ...TRANSIENT_ONCE_MODES.keys(),
]);

const transientStatus = TRANSIENT_ONCE_MODES.get(options.mode);
if (transientStatus !== undefined && getCount === 1)
{
    sendRangeBody(stream, context, transientStatus,
        { 'content-length': '17' }, Buffer.from('transient-error!\n'));
    return;
}
if (options.mode === 'range-503-exhaust')
{
    sendRangeBody(stream, context, 503,
        { 'content-length': '17' }, Buffer.from('transient-error!\n'));
    return;
}
if (path.includes('range-404'))
{
    sendRangeBody(stream, context, 404,
        { 'content-length': '0' }, Buffer.alloc(0));
    return;
}
```

In `verifyParallelMetadataPrefetch()`, add one case per `TRANSIENT_ONCE_MODES` entry. Require one
HEAD, two exact Range streams, one session, one retry event, no fallback, one publication, and a
successful open. Add exhaustion with `MAX_RETRY=2`: one HEAD, three exact Range streams, two retry
events, one terminal fallback, no publication, and formal rejection. Add 404 as a path-controlled
nontransient response and require one Range with zero coordinator retries.

Run:

```bash
cmake --build build/science_g0_prefetch --target osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0_prefetch \
  -R '^osgVerse_Test_ScienceHttpRanges$' --output-on-failure
```

Expected RED on the unchanged patch: the first transient attempt produces
`ParallelHeadRange: transient-fallback` and lacks `ParallelHeadRange: transient-retry`.

- [ ] **Step 2: Add the wished successful-retry replay and observe proof-schema RED**

Create a credential-free minimized replay from the frozen v3 NVIDIA iteration 2 chronology. Keep
the initial concurrent HEAD and Range, the 17-byte HTTP 500, then insert exactly:

```text
VSICURL: ParallelHeadRange: transient-retry range=bytes=0-131071 status=500 bytes=17 attempt=1 delay-ms=100 range-connection=0 range-http=2
```

Follow it with a second exact Range request on the same URI/connection, exact 206, 131072 bytes,
one `published` event, and no fallback. The stats fixture records one HEAD, one logical GET, and
`131089` downloaded bytes.

Add `verifyPrefetchTransientRetryReplayRegression()` with these exact positive checks:

```cpp
require(proof.coordinatorTransientRetryCount == 1 &&
            proof.coordinatorTransientRetryBytes == 17 &&
            proof.coordinatorTransientRetryCodes == std::map<int, int>{{500, 1}},
        "NVIDIA retry replay did not preserve coordinator retry accounting");
require(proof.actualHeadCount == 1 && proof.actualGetCount == 2 &&
            proof.successfulGetCount == 1 &&
            proof.statsGetOperationCount == 1,
        "NVIDIA retry replay physical/logical request counts did not reconcile");
require(proof.metadataPrefetch.headRequestCount == 1 &&
            proof.metadataPrefetch.rangeRequestCount == 2 &&
            proof.metadataPrefetch.cachePublished,
        "NVIDIA retry replay did not prove retried prefetch publication");
```

Mutate the replay one property at a time and require fail-closed messages for missing/duplicate
event, wrong status, status 404, wrong body bytes, attempt 0/4, noncontiguous attempt, delay 0,
wrong connection, HTTP major 1, wrong Range, missing second request, extra HEAD, fallback present,
and duplicate publication.

Run the same focused CTest. Expected RED before parser changes: the coordinator retry fields do
not exist and the metadata proof still requires exactly one initial Range request.

- [ ] **Step 3: Lock the source contract before changing production behavior**

In `tests/science_deps_script_tests.sh`, replace independent status substring checks with a bounded
classifier extractor. Require one classifier definition, exactly five `case` labels inside that
function, the complete retry format split only at the C++ adjacent-string boundary, and the
unchanged fallback format string:

```bash
assert_contains "$prefetch_patch" \
    'static bool IsParallelHeadRangeTransientStatus(long nStatus)' \
    "prefetch patch must centralize coordinator transient status classification"
classifier=$(sed -n \
    '/^+static bool IsParallelHeadRangeTransientStatus(long nStatus)/,/^+}/p' \
    "$prefetch_patch")
for status in 429 500 502 503 504; do
    [[ $(grep -Ec "^+        case ${status}:$" <<<"$classifier") -eq 1 ]] ||
        fail "prefetch patch must classify transient status ${status} exactly once"
done
assert_contains "$prefetch_patch" \
    'ParallelHeadRange: transient-retry range=bytes=0-131071 ' \
    "prefetch patch must emit the exact coordinator retry prefix"
assert_contains "$prefetch_patch" \
    'status=%ld bytes=%zu attempt=%d delay-ms=%lld ' \
    "prefetch patch must emit exact coordinator retry numeric fields"
assert_contains "$prefetch_patch" \
    'range-connection=' \
    "prefetch patch must emit coordinator retry connection evidence"
assert_contains "$prefetch_patch" \
    'ParallelHeadRange: transient-fallback range=bytes=0-131071 status=%ld bytes=%zu' \
    "prefetch patch must retain the exact terminal fallback contract"
```

The shell test is expected to fail until Step 4 adds the classifier and retry event.

- [ ] **Step 4: Add the minimal native retry loop to the private GDAL patch**

Add the classifier near the existing parallel writer helpers:

```cpp
static bool IsParallelHeadRangeTransientStatus(long nStatus)
{
    switch (nStatus)
    {
        case 429:
        case 500:
        case 502:
        case 503:
        case 504:
            return true;
        default:
            return false;
    }
}
```

Add result fields for retry count/bytes and construct only the native context:

```cpp
int nRangeTransientRetryCount = 0;
size_t nRangeTransientRetryBytes = 0;
CPLHTTPRetryContext oRangeRetryContext(m_oRetryParameters);
```

After the initial Range reaches `CURLMSG_DONE`, read its status/body/headers/connection/version.
For a configured transient status, call:

```cpp
const bool bCanRetry = oRangeRetryContext.CanRetry(
    static_cast<int>(sParallelResult.nRangeCode),
    sRangeHeaderData.pBuffer, nullptr);
```

When true, increment the retry number, emit the exact event with
`llround(GetCurrentDelay() * 1000.0)`, add the failed body to
`nRangeTransientRetryBytes`, remove only the Range handle, sleep the native delay, clear
`sRangeWriter`, free/reinitialize `sRangeHeaderData`, reset the Range completion/result fields,
re-add the same `hRangeHandle` to the same `hCurlMultiHandle`, and drive it to one new
`CURLMSG_DONE`. Do not re-add or recreate HEAD. A retry add/remove/transport failure uses the
existing fallback path and cannot publish.

After the loop, capture the final Range connection/version, remove the Range handle once for final
cleanup ownership, and emit the existing transport event. Require the final connection/version to
match HEAD before publication. Change only the stats byte argument:

```cpp
const size_t nCoordinatorDownloadedBytes =
    sParallelResult.nRangeTransientRetryBytes + sParallelResult.nRangeSize;
NetworkStatisticsLogger::LogGET(nCoordinatorDownloadedBytes);
CPLDebug(poFS->GetDebugKey(),
         "ParallelHeadRange: logical-get-complete bytes=%zu",
         sParallelResult.nRangeSize);
```

On exhausted transient status, emit the unchanged terminal fallback event for only the final body.
The preceding retry events retain earlier discarded bodies. Never copy an earlier body/header into
`abyRangeBuffer` or call `AddRegion()` before final exact validation.

- [ ] **Step 5: Implement fail-closed proof parsing and accounting**

Use one anchored regex:

```cpp
static const std::regex coordinatorRetryPattern(
    R"(^VSICURL: ParallelHeadRange: transient-retry )"
    R"(range=(bytes=[0-9]+-[0-9]+) status=([0-9]+) bytes=([0-9]+) )"
    R"(attempt=([0-9]+) delay-ms=([0-9]+) )"
    R"(range-connection=(-?[0-9]+) range-http=([0-9]+)$)");
```

For each event consume exactly one matching transient HTTP response `(status, body bytes)` and one
matching exact Range request. Require attempts `1..N`, `N <= 3`, positive delay, the allowed code,
HTTP major 2, nonnegative connection, and chronological placement between the failed response and
next request. Reconcile:

```text
requestedRanges = successfulRanges + ordinaryRetries
                + coordinatorRetries + coordinatorTerminalFallbacks
actualGetCount = successfulGetCount + ordinaryRetryCount
               + coordinatorRetryCount + coordinatorTerminalFallbackCount
statsDownloadedBytes = successfulRangeBytes + coordinatorRetryBytes
                     + coordinatorTerminalFallbackBytes
```

Keep `coordinatorLogicalGetBytes` equal to the final successful interval or final terminal-fallback
body. In `buildMetadataPrefetchProof()`, allow `rangeRequestCount == 1 +
coordinatorTransientRetryCount`, preserve initial HEAD/Range overlap, and require every subsequent
exact Range to follow its retry event on the same HTTP/2 connection. Terminal fallback remains an
early formal rejection.

Serialize the new fields in per-iteration and aggregate JSON. Extend the synthetic JSON regression
so absence or type drift of any new field fails.

- [ ] **Step 6: Re-pin, clean-rebuild the owned private dependency, and turn focused gates GREEN**

Update `OSGSOL_GDAL_PATCH_SHA256` in `packaging/science_deps/versions.env`. Prove
`build/science-deps-prefetch` and all ownership markers resolve under this worktree, remove only
that owned non-symlink root, then rebuild from the locally cached pinned archives:

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

Expected GREEN: dependency contract PASS; private prefix verifier PASS; every 429/500/502/503/504
once case recovers with one coordinator retry; exhaustion and 404 fail/fallback exactly as
specified; all replay mutations fail closed; all existing overflow, redirect, detach, HTTP/1,
connection, range, byte, and scientific-oracle tests remain green.

- [ ] **Step 7: Commit the bounded runtime and evidence contract**

```bash
git diff --check
git add packaging/science_deps/gdal-3.13.1-parallel-head-range.patch \
        packaging/science_deps/versions.env tests/science_deps_script_tests.sh \
        tests/science_http2_range_server.mjs tests/science_gdal_network_test.cpp \
        tests/data/science/prefetch_nvidia_transient_retry_trace.log \
        tests/data/science/prefetch_nvidia_transient_retry_stats.json
git commit -m "fix(scienceearth): retry transient prefetch range"
```

---

### Task 2: Rebuild, audit, and authorize absent v4 evidence roots

**Files:**
- Modify: `docs/scienceearth/gdal-build.md`
- Modify: `docs/scienceearth/g0-measurements.md`

**Interfaces:**
- Consumes Task 1's pinned patch, clean private prefix, complete local HTTP/2 matrix, and replay GREEN.
- Produces `PUBLIC_REQUALIFICATION_V4=AUTHORIZED_NOT_RUN` while keeping `G0_DECISION=STOP` and the Desktop unchanged.

- [ ] **Step 1: Run the complete non-public regression set from the final patch**

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

Expected baseline totals are Python `66/66`, core CTest `16/16`, selected ScienceEarth CTest
`7/7`, dependency contract PASS, and private-prefix verifier PASS; if committed tests increase a
suite count, record the higher discovered/ran count and require zero skip/failure. Remove only
generated `__pycache__` after results are recorded.

- [ ] **Step 2: Rebuild the disposable probe and run the canonical isolation audit**

Rebuild `osgdb_science_g0_probe`, construct and ad-hoc sign the disposable
`build/science_g0_prefetch/osgSol Science G0 Probe.app`, then run the committed reference/ratchet
audit. Require Tier A `0`, Tier B new/removed `0/0`, unresolved dependencies `0`, delta below
`40 MiB`, science closure below `60 MiB`, exactly `_osgsol_science_g0_probe_anchor` exported from
the plugin, valid deep/strict signature, and no worktree/private-prefix path in load commands or
strings.

- [ ] **Step 3: Recompute all historical and Desktop invariants and prove v4 roots absent**

Recompute every hash documented in `docs/scienceearth/g0-measurements.md`, including all Global
Constraints and all 37 v3 artifacts. Require frozen files to remain non-writable. Recompute the
Desktop fingerprint/helper digest/counts and require no change. Require the three v4 roots and both
v4 summary JSON files to be absent. Require the formal CTest still uses `--profile optimized` and
not the future formal-v4 path.

- [ ] **Step 4: Record authorization and commit**

Append exact commands, exits, counts, timings, patch/prefix/probe hashes, audit results, immutable
hash checks, Desktop checks, and absent-root checks. End with:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V4=AUTHORIZED_NOT_RUN
DESKTOP_PACKAGE=NOT_READY
```

```bash
git diff --check
git add docs/scienceearth/gdal-build.md docs/scienceearth/g0-measurements.md
git commit -m "docs(scienceearth): authorize bounded retry v4"
```

---

### Task 3: Run the one-shot v4 public and conditional formal gates

**Files:**
- Modify: `docs/scienceearth/g0-measurements.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`
- Conditional modify after complete candidate PASS: `tests/CMakeLists.txt`

**Interfaces:**
- Produces `build/science_g0_prefetch/requalification-evidence-v4/control-summary.json` and `prefetch-summary.json` from exactly one process each.
- Produces `build/science_g0_prefetch/formal-evidence-v4/live-summary.json` only after a complete candidate PASS and a committed profile promotion.
- Produces either an immutable v4 `STOP` report or formal G0 `GO`; no Desktop package is modified in this task.

- [ ] **Step 1: Snapshot and authorize immutable inputs immediately before network use**

Record the exact commit, wall clock, credential-free proxy endpoint class, patch/prefix hashes, all
historical/Desktop invariants, current CMake command, and v4 root absence. Abort before the first
request on any mismatch. Write control and candidate console transcripts outside their evidence
roots so the exactly-once process claim remains independently inspectable.

- [ ] **Step 2: Run exactly one control process and one candidate process**

```bash
build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile optimized \
  --evidence-dir build/science_g0_prefetch/requalification-evidence-v4/control \
  --summary-json build/science_g0_prefetch/requalification-evidence-v4/control-summary.json

build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile prefetch \
  --evidence-dir build/science_g0_prefetch/requalification-evidence-v4/prefetch \
  --summary-json build/science_g0_prefetch/requalification-evidence-v4/prefetch-summary.json \
  --enforce-latency
```

Never rerun either process. Freeze and hash every created artifact even on nonzero exit. Require
ten complete candidate proofs, zero terminal fallback, coordinator retry count at most three per
iteration, exact retry/ordinary retry/request/byte reconciliation, correctness, range, HTTP/2,
connection, overlap, publication, byte ceiling, NVIDIA/Hong Kong median and P95 gates.

- [ ] **Step 3: Promote only after a complete candidate PASS**

If and only if Step 2 passes every gate, change only `osgVerse_Test_ScienceGdalLive` from
`--profile optimized` to `--profile prefetch`, retain `--iterations 5 --enforce-latency`, and set
its output to `build/science_g0_prefetch/formal-evidence-v4`. Commit before the formal request:

```bash
git add tests/CMakeLists.txt
git commit -m "test(scienceearth): promote bounded retry v4 formal profile"
```

If Step 2 fails, skip this step and every formal/package/G1 action.

- [ ] **Step 4: Run the formal and downstream gates exactly once after promotion**

Run `osgVerse_Test_ScienceGdalLive` once through CTest. On PASS, run once each: science-off core
suite, private dependency verifier, full Python suite, selected ScienceEarth suite, disposable
probe, canonical bundle audit, signature/size/export/path isolation, memory, correctness, range,
camera, cache, and clean-launch checks. A failed formal or downstream public gate is not rerun.

- [ ] **Step 5: Freeze evidence, document the decision, and commit**

Record exact process exit codes, transcript/summary/raw/stats/proof hashes, iteration timings,
medians/P95, response codes, coordinator/ordinary retries, requests, bytes, fallback, formal and
downstream counts, audit output, and protected before/after hashes. Use exactly one decision:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V4=FAIL
DESKTOP_PACKAGE=NOT_READY
```

or only after every diagnostic, formal, and downstream gate passes:

```text
G0_DECISION=GO
G0_AUTOMATED_GATES=PASS
PUBLIC_REQUALIFICATION_V4=PASS
HUMAN_PRODUCT_SIGN_OFF=PENDING
DESKTOP_PACKAGE=BLOCKED_ON_G1
```

```bash
git diff --check
git add docs/scienceearth/g0-measurements.md docs/scienceearth/g0-g1-baseline.md
git commit -m "docs(scienceearth): decide bounded retry v4"
```

## Conditional continuation after Task 3

If Task 3 records `STOP`, the active Goal does not rerun v4. Diagnose the newly measured cause,
write a separately approved bounded remediation design, and preserve all v4 evidence.

If Task 3 records `GO`, immediately refresh
`docs/superpowers/plans/2026-07-11-scienceearth-g0-g1-alphaearth-vertical-slice-plan.md` into a new
G1-only execution plan based on the actual GO commit and current test counts. Execute its Tasks
7-15 with subagent-driven development and two-stage review. The deliverable is the verified fixed
path `/Users/USER/Desktop/osgSol Earth.app` plus the ten-row manual matrix. Do not execute its
Task 16, create a tag, or push until the user separately approves the manually tested build.
