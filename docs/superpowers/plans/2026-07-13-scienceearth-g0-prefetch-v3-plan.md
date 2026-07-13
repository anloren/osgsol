# ScienceEarth G0 Prefetch V3 Requalification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make parallel-prefetch transient fallback accounting explicit and fail-closed, then run one new v3 public requalification without changing the protected Desktop app or any previously frozen evidence.

**Architecture:** The private GDAL patch remains exact-path opt-in and emits one additional authoritative event only when its parallel first-Range request receives an allowed transient HTTP status and falls back to the ordinary `/vsicurl/` path. `buildHttpProof()` accounts that coordinator fallback separately from ordinary CPL retries, while `buildMetadataPrefetchProof()` continues to reject every fallback as ineligible for formal success. A sanitized replay of the frozen Hong Kong v2 iteration 5 proves the distinction before any new public request is permitted.

**Tech Stack:** C++17, GDAL 3.13.1 private static patch, libcurl 8.7.1 multi/HTTP2, CMake/CTest, Python bundle-audit tooling, macOS Mach-O/codesign audit.

## Global Constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on `codex/science-earth-g0-g1`; the v3 base commit is `c7d032506b6bb22d23d5aacbe82c42c909aea267`.
- The rejected Task 4 artifacts remain immutable: control summary `f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096`, candidate summary `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`, candidate raw log `3312a78a073140ea1422ece0ffbc927ce853a4d7b8cec02f3033734a7e217a3a`, and candidate stats `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe`.
- The older formal summaries remain immutable: baseline `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed`; optimized/live `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe`.
- The complete v2 evidence set remains immutable. At minimum, control summary must remain `0b59ba209faf574f1439f78f6fa53b16e81b20e05ae4b87ae2ef90c43de59fc0`, candidate summary must remain `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`, Hong Kong candidate iteration 5 raw log must remain `f32cfdfd05f50a3de8f34b4f69e1e2cefe61d667e20936ddcf86f1fa8b1a429e`, and its stats must remain `f54110c20ab81230c5d3af708f2df4d9d8754e5957366875c162b0395d2c1974`. Every other per-iteration v2 hash recorded in `docs/scienceearth/g0-measurements.md` must also remain exact.
- The protected Desktop `/Users/USER/Desktop/osgSol Earth.app` must remain canonical fingerprint `91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`, helper digest `14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214`, `414` entries, and `355` regular non-symlink files.
- Prefetch stays exact-path opt-in through `OSGSOL_VSICURL_PREFETCH_HEAD_RANGE=YES`; baseline and optimized remain option-free. No global activation, warm connection, no-HEAD shortcut, new retry, changed retry limit, threshold relaxation, or integrity-gate relaxation is allowed.
- The authoritative event is exactly `ParallelHeadRange: transient-fallback range=bytes=0-131071 status=<CODE> bytes=<N>`. It is observability for the already-existing coordinator fallback, not permission to treat fallback as candidate success.
- Allowed coordinator transient codes are exactly `429`, `500`, `502`, `503`, and `504`. Ordinary CPL retry evidence and coordinator transient-fallback evidence remain distinct in proof fields and reconciliation.
- A valid candidate still requires five independent iterations per fixture, complete correctness/range/byte/retry proof, exact first `bytes=0-131071`, HEAD/Range overlap, HTTP/2, equal explicit per-easy-handle connection IDs, zero fallback, median `<= 3000 ms`, and P95 `<= 8000 ms` for NVIDIA and Hong Kong.
- New public evidence may be written only below absent roots `build/science_g0_prefetch/requalification-evidence-v3/control`, `build/science_g0_prefetch/requalification-evidence-v3/prefetch`, and conditionally `build/science_g0_prefetch/formal-evidence-v3`. One control process and one candidate process are allowed; neither is rerun. A failed formal gate is not retried.
- Do not replace or package the Desktop app, tag, push, start G1, or alter the AI/agent/data-source architecture. Even after all v3 automated gates pass, this plan may report only `DESKTOP_PACKAGE=READY_FOR_AUTHORIZED_UPDATE`.

---

### Task 1: Reconcile coordinator transient fallback with a frozen-trace regression

**Files:**
- Modify: `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`
- Modify: `packaging/science_deps/versions.env`
- Modify: `tests/science_deps_script_tests.sh`
- Modify: `tests/science_gdal_network_test.cpp`
- Create: `tests/data/science/prefetch_hong_kong_transient_trace.log`
- Create: `tests/data/science/prefetch_hong_kong_transient_stats.json`

**Interfaces:**
- Produces exactly one coordinator-fallback event adjacent to the existing `status-<CODE>` decision when the initial parallel Range status is one of `429/500/502/503/504`: `ParallelHeadRange: transient-fallback range=bytes=0-131071 status=<CODE> bytes=<N>`.
- `HttpProof` exposes separate coordinator fields: `coordinatorTransientFallbackCount`, `coordinatorTransientFallbackBytes`, and `coordinatorTransientFallbackCodes`; existing `transientRetryCount` and `transientRetryCodes` continue to mean ordinary CPL retries only.
- `buildHttpProof()` reconciles physical GET count as successful 206 requests plus ordinary retries plus coordinator transient fallbacks. It reconciles stats downloaded bytes as successful Range bytes plus coordinator bytes explicitly logged by the coordinator.
- `buildMetadataPrefetchProof()` rejects the replay with `metadata prefetch formal proof contains a fallback`; no fallback is accepted as a valid metadata-prefetch proof.

- [ ] **Step 1: Create sanitized replay inputs without changing frozen v2 files**

Use the frozen `prefetch-hong_kong-5-curl-cpl.log` and matching stats as read-only source material. Add a credential-free, minimized trace fixture with the real timestamp order and all request, response, logical-GET, transport, detach, fallback, and stats-relevant events. Insert the wished authoritative row before the existing `fallback=status-500` row:

```text
VSICURL: ParallelHeadRange: transient-fallback range=bytes=0-131071 status=500 bytes=17
```

Create the stats fixture byte-identical to the frozen stats so its SHA-256 is `f54110c20ab81230c5d3af708f2df4d9d8754e5957366875c162b0395d2c1974`. Extend the delivery test to reject `Authorization`, `Proxy-Authorization`, bearer, token, key, password, signed query, and userinfo syntax in both replay fixtures.

- [ ] **Step 2: Write and observe the focused RED**

Add `verifyPrefetchTransientFallbackReplayRegression()`. Load the new files and require `buildHttpProof()` to return one coordinator transient fallback with code `500`, `17` coordinator bytes, stats GET count `3`, and reconciled physical GET/range counts. Then call `buildMetadataPrefetchProof()` through an exception helper that preserves the message and require exactly:

```text
metadata prefetch formal proof contains a fallback
```

Add mutations for missing event, duplicate event, wrong Range, wrong status, wrong bytes, status `404`, malformed numeric fields, and an event with no matching transient response. Run:

```bash
cmake --build build/science_g0_prefetch --target osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0_prefetch -R '^osgVerse_Test_ScienceHttpRanges$' --output-on-failure
```

Expected RED before production parsing: `transient HTTP responses do not reconcile with CPL retry events`. Preserve the RED command, exit code, and failure text in the task report.

- [ ] **Step 3: Emit the minimal production event**

In the private GDAL patch, keep the existing fallback behavior and add only the following guarded debug emission in the `nRangeCode >= 400` branch, before the generic fallback event:

```cpp
if (sParallelResult.nRangeCode == 429 ||
    sParallelResult.nRangeCode == 500 ||
    sParallelResult.nRangeCode == 502 ||
    sParallelResult.nRangeCode == 503 ||
    sParallelResult.nRangeCode == 504)
{
    CPLDebug(poFS->GetDebugKey(),
             "ParallelHeadRange: transient-fallback "
             "range=bytes=0-131071 status=%ld bytes=%zu",
             sParallelResult.nRangeCode,
             sParallelResult.nRangeSize);
}
```

Do not add a retry or change `bFallback`, `osReason`, buffer clearing, cache publication, or rejection logic.

- [ ] **Step 4: Implement fail-closed parser reconciliation**

Parse the event with one anchored exact regular expression. Reject substring matches, malformed integers, duplicate coordinator events, non-transient status, unknown Range, and status/body pairs not present in the transient HTTP response multiset. Subtract coordinator `(status, bytes)` observations from that multiset, then require the remaining response-code counts to equal ordinary CPL retry events.

Use these equations:

```text
requestedRanges[range] = successfulRanges[range]
                       + retriesByRange[range]
                       + coordinatorFallbacksByRange[range]
actualGetCount = successfulGetCount
               + ordinaryRetryEventCount
               + coordinatorTransientFallbackCount
statsDownloadedBytes = successfulRangeBytes
                     + coordinatorTransientFallbackBytes
```

Keep `transientRetryCount` and `transientRetryCodes` ordinary-only. Keep `declaredTransientBytes` and `conservativeBodyUpperBound` inclusive of every transient response. For the existing logical coordinator event, require its bytes to equal the first successful interval when no coordinator fallback exists, or to equal `coordinatorTransientFallbackBytes` when exactly one exists. Move the no-fallback requirement early enough in `buildMetadataPrefetchProof()` that a fully reconciled fallback trace fails with the exact formal-fallback message rather than a misleading retry/count error.

- [ ] **Step 5: Re-pin, clean rebuild, and turn every focused gate GREEN**

Update `OSGSOL_GDAL_PATCH_SHA256` in `versions.env`; require the exact new event format in `tests/science_deps_script_tests.sh`. Verify that `build/science-deps-prefetch` and its child markers belong to this exact repository, remove only that owned non-symlink root, verify and reseed the pinned GDAL/PROJ/ZSTD archives from the local cache, and rebuild from the final patch:

```bash
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --build --jobs 4
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --verify
cmake --build build/science_g0_prefetch --target \
  osgVerse_Test_ScienceGdalSpike osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0_prefetch \
  -R 'ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer' \
  --output-on-failure
```

The sanitized HK5 replay must reconcile at the HTTP layer and then be rejected only by the formal no-fallback gate. All existing retry, malformed-evidence, local HTTP/2, removal-fault, range, byte, and proof tests must stay green.

- [ ] **Step 6: Commit**

```bash
git add packaging/science_deps/gdal-3.13.1-parallel-head-range.patch \
        packaging/science_deps/versions.env tests/science_deps_script_tests.sh \
        tests/science_gdal_network_test.cpp \
        tests/data/science/prefetch_hong_kong_transient_trace.log \
        tests/data/science/prefetch_hong_kong_transient_stats.json
git commit -m "fix(scienceearth): account for prefetch transient fallback"
```

---

### Task 2: Re-audit the private runtime and authorize absent v3 roots

**Files:**
- Modify: `docs/scienceearth/gdal-build.md`
- Modify: `docs/scienceearth/g0-measurements.md`

**Interfaces:**
- Consumes Task 1's final patch pin, clean private prefix, and replay GREEN.
- Produces a non-public readiness record with `PUBLIC_REQUALIFICATION_V3=AUTHORIZED_NOT_RUN`; it does not alter `tests/CMakeLists.txt` or run public network measurements.

- [ ] **Step 1: Run the complete non-public regression set**

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

Expected results are Python `66/66`, core CTest `16/16`, selected ScienceEarth CTest `7/7`, dependency contract PASS, and private-prefix verifier PASS. Remove only generated `__pycache__` directories after recording their output.

- [ ] **Step 2: Rebuild the disposable probe and run canonical isolation gates**

Rebuild `osgdb_science_g0_probe`, construct the disposable `build/science_g0_prefetch/osgSol Science G0 Probe.app`, ad-hoc sign it, and run the committed reference/ratchet audit using the canonical osgVerse and osgSol source roots. Require Tier A `0`, Tier B new/removed `0/0`, unresolved dependencies `0`, delta below `40 MiB`, science closure below `60 MiB`, exactly `_osgsol_science_g0_probe_anchor` exported from the science plugin, valid deep/strict signature, and no worktree or private-prefix path in Mach-O load commands or strings.

- [ ] **Step 3: Recompute every protected hash and prove v3 roots absent**

Recompute every old diagnostic/formal and v2 summary/raw/stats/proof hash recorded in `docs/scienceearth/g0-measurements.md`. Explicitly require the eight critical hashes in Global Constraints, the Desktop fingerprint/helper digest/counts, and the current formal `--profile optimized` CTest command. Abort authorization unless all three roots are absent:

```text
build/science_g0_prefetch/requalification-evidence-v3/control
build/science_g0_prefetch/requalification-evidence-v3/prefetch
build/science_g0_prefetch/formal-evidence-v3
```

- [ ] **Step 4: Document readiness and commit**

Record exact commands, totals, timings, patch/fixture/prefix/probe hashes, audit metrics, immutable hash checks, Desktop checks, and absent roots. Keep:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V3=AUTHORIZED_NOT_RUN
DESKTOP_PACKAGE=NOT_READY
```

```bash
git add docs/scienceearth/gdal-build.md docs/scienceearth/g0-measurements.md
git commit -m "docs(scienceearth): authorize prefetch v3 requalification"
```

---

### Task 3: Run the one-shot v3 requalification and conditionally promote formal proof

**Files:**
- Modify: `docs/scienceearth/g0-measurements.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`
- Conditional modify after complete candidate PASS: `tests/CMakeLists.txt`

**Interfaces:**
- New diagnostic summaries: `build/science_g0_prefetch/requalification-evidence-v3/control-summary.json` and `build/science_g0_prefetch/requalification-evidence-v3/prefetch-summary.json`.
- Conditional formal summary: `build/science_g0_prefetch/formal-evidence-v3/live-summary.json`.

- [ ] **Step 1: Snapshot immutable inputs immediately before network use**

Record commit, wall clock, credential-free proxy endpoint class, all protected hashes/counts, `--profile optimized`, and absence of all v3 evidence roots. Abort before the first request on any mismatch. Preserve command stdout/stderr in report files outside the evidence roots so process count and no-rerun claims remain independently inspectable.

- [ ] **Step 2: Run exactly one control process and one candidate process**

```bash
build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile optimized \
  --evidence-dir build/science_g0_prefetch/requalification-evidence-v3/control \
  --summary-json build/science_g0_prefetch/requalification-evidence-v3/control-summary.json

build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile prefetch \
  --evidence-dir build/science_g0_prefetch/requalification-evidence-v3/prefetch \
  --summary-json build/science_g0_prefetch/requalification-evidence-v3/prefetch-summary.json \
  --enforce-latency
```

Never rerun either process. Freeze every produced artifact and hash it even if the process exits nonzero. On any missing iteration/proof, fallback, accounting mismatch, correctness/range/byte/retry failure, median above `3000 ms`, or P95 above `8000 ms`, skip promotion and all formal gates and record immutable STOP/NOT_READY.

- [ ] **Step 3: Promote only after a complete candidate PASS**

If and only if both fixtures contain five valid candidate proofs with zero fallback and all latency gates pass, change only `osgVerse_Test_ScienceGdalLive` from `--profile optimized` to `--profile prefetch`, retain `--iterations 5` and `--enforce-latency`, and set its evidence paths to `build/science_g0_prefetch/formal-evidence-v3`. Commit this conditional promotion before running the formal test:

```bash
git add tests/CMakeLists.txt
git commit -m "test(scienceearth): promote prefetch v3 formal profile"
```

- [ ] **Step 4: Run formal and downstream gates once after promotion**

Run `osgVerse_Test_ScienceGdalLive` once through CTest. If it passes, run once each: protected science-off `16/16`, private dependency verify, Python `66/66`, selected ScienceEarth `7/7`, canonical bundle audit, signature/size/export/path checks, memory/correctness/range/camera/cache gates, and clean-machine launch/process checks. Do not retry a failed formal or downstream gate.

- [ ] **Step 5: Record the decision and commit**

Document exact process exit codes, console transcript hashes, summary/raw/stats/proof hashes, iteration timings, medians/P95, GET/HEAD/status/byte/retry/fallback evidence, formal/downstream process counts, audit outputs, and protected before/after hashes. Record one of:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V3=FAIL
DESKTOP_PACKAGE=NOT_READY
```

or, only after every diagnostic, formal, and downstream automated gate passes:

```text
G0_DECISION=GO
G0_AUTOMATED_GATES=PASS
PUBLIC_REQUALIFICATION_V3=PASS
HUMAN_PRODUCT_SIGN_OFF=PENDING
DESKTOP_PACKAGE=READY_FOR_AUTHORIZED_UPDATE
```

```bash
git add docs/scienceearth/g0-measurements.md \
        docs/scienceearth/g0-g1-baseline.md
git commit -m "docs(scienceearth): decide prefetch v3 requalification"
```

Do not package, replace the Desktop app, tag, push, or start G1 in this task.
