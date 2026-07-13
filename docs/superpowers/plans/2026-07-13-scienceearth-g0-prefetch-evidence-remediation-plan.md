# ScienceEarth G0 Prefetch Evidence Remediation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Repair the two fail-closed evidence defects exposed by the preserved public candidate trace, re-qualify the private GDAL prefetch profile once in new evidence paths, and report Desktop readiness without replacing the protected app.

**Architecture:** The private GDAL patch remains path-specific and off by default. It will emit one authoritative logical-GET completion event adjacent to `NetworkStatisticsLogger::LogGET()` and one authoritative per-easy-handle transport event derived from the already-collected `CURLINFO_CONN_ID` and HTTP-version values. The test harness will replay the preserved NVIDIA chronology, count the coordinator GET without conflating physical requests, and prove shared HTTP/2 identity from the explicit role-labelled event rather than curl's single generic connection marker.

**Tech Stack:** C++17, GDAL 3.13.1 private static patch, libcurl 8.7.1 multi/HTTP2, CMake/CTest, Python bundle audit tooling, macOS Mach-O/codesign audit.

## Global Constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on `codex/science-earth-g0-g1`; base commit is `3e7ce0b24c642bca675c2e22dc526ab61054ea91`.
- The rejected Task 4 artifacts are immutable and must retain these SHA-256 values: control summary `f793f1c3561e3f746ace2fca164637cd5d238fa81c39298b9a2e29ec2b781096`, candidate summary `1fc1f695aec9930ac6bfe540dd11829bc6ffb0b12c01ec4f952a1b3879eea5ff`, candidate raw log `3312a78a073140ea1422ece0ffbc927ce853a4d7b8cec02f3033734a7e217a3a`, and candidate stats `0dd2414c27c7cfc04ff296f4bad1b49ed406d5706e0ff4b6a158134a947254fe`.
- The older formal summaries remain immutable: baseline `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed`; optimized/live `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe`.
- The protected Desktop must remain fingerprint `91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18` and reproducible helper digest `14d88b71426109ada05b3caee0539195bc2b6938b08d72a7025048b9b4845214` with `414` entries and `355` regular non-symlink files.
- Do not reinterpret, delete, overwrite, or rerun the rejected Task 4 sample set. Any new diagnostic uses new absent directories and constitutes a new authorized evidence set.
- Prefetch stays exact-path opt-in through `OSGSOL_VSICURL_PREFETCH_HEAD_RANGE=YES`; baseline and optimized remain option-free. No global activation, warm connection, no-HEAD shortcut, or relaxed integrity gate is allowed.
- A valid candidate requires five independent iterations per fixture, complete correctness/range/byte/retry proof, exact first `bytes=0-131071`, HEAD/Range overlap, HTTP/2, equal explicit per-easy-handle connection IDs, median `<= 3000 ms`, and P95 `<= 8000 ms` for NVIDIA and Hong Kong.
- A rejected new sample set is not rerun. Formal promotion occurs only after a complete candidate diagnostic PASS; any later formal failure returns `G0_DECISION=STOP` without threshold changes.
- Do not replace or package `/Users/USER/Desktop/osgSol Earth.app`, tag, push, start G1, or alter the AI/agent/data-source architecture. A successful run may report only `DESKTOP_PACKAGE=READY_FOR_AUTHORIZED_UPDATE`.

---

### Task 1: Add authoritative transport events and replay regressions

**Files:**
- Modify: `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`
- Modify: `packaging/science_deps/versions.env`
- Modify: `tests/science_deps_script_tests.sh`
- Modify: `tests/science_gdal_network_test.cpp`
- Create: `tests/data/science/prefetch_nvidia_partial_trace.log`
- Create: `tests/data/science/prefetch_nvidia_partial_stats.json`

**Interfaces:**
- Produces exactly one coordinator event: `ParallelHeadRange: logical-get-complete bytes=<N>` immediately after the matching `NetworkStatisticsLogger::LogGET(N)` call.
- Produces exactly one role-labelled event after both `CURLINFO_CONN_ID` values are finalized: `ParallelHeadRange: transport head-connection=<ID> range-connection=<ID> head-http=<MAJOR> range-http=<MAJOR>`.
- `buildHttpProof()` counts the coordinator completion as one logical GET and still reconciles physical GET headers, 206 bodies, retries, and stats bytes independently.
- `buildMetadataPrefetchProof()` uses the explicit role-labelled IDs as the authoritative shared-connection proof; generic `Connection #N ... left intact` text is not assigned to HEAD or Range roles.

- [ ] **Step 1: Create a credential-free replay fixture from the rejected evidence**

Copy only the timestamped curl/CPL lines needed by `buildHttpProof()` and `buildMetadataPrefetchProof()` from the preserved NVIDIA partial trace. Keep the public object URI and credential-free loopback proxy class, but reject `Authorization`, `Proxy-Authorization`, bearer/token/key/password strings in `tests/science_deps_script_tests.sh`. Copy the exact preserved network-stat JSON. Record their new fixture hashes in the Task 1 report.

- [ ] **Step 2: Write the logical-GET RED**

Load the replay fixture into `DebugCapture`, insert the wished-for line at the preserved `LogGET` boundary:

```text
ParallelHeadRange: logical-get-complete bytes=131072
```

Call `buildHttpProof()` with the fixture stats and require `statsGetOperationCount == 2`. Run:

```bash
cmake --build build/science_g0_prefetch --target osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0_prefetch -R '^osgVerse_Test_ScienceHttpRanges$' --output-on-failure
```

Expected RED: `VSINetworkStats GET operations disagree with CPL read operations` because the parser ignores the coordinator event.

- [ ] **Step 3: Write the per-handle connection RED**

Keep the real chronology where both response header blocks finish before one generic curl connection marker. Insert the wished-for role-labelled event with equal IDs and HTTP/2:

```text
ParallelHeadRange: transport head-connection=0 range-connection=0 head-http=2 range-http=2
```

Require the metadata proof to pass with `sharedConnection=true`. Add negative rows for missing event, duplicate event, negative ID, distinct IDs, head/range HTTP/1, malformed integer, and an otherwise-valid trace containing a misleading second generic `Connection #8` marker. Expected RED: the current parser rejects the one-marker chronology or ignores the explicit event.

- [ ] **Step 4: Implement minimal production logging and parsing**

Add the logical event adjacent to `NetworkStatisticsLogger::LogGET()` so one debug event corresponds to one stats operation even when the prefetch later falls back or is rejected. Emit the role-labelled transport event only after callback-captured/post-transfer IDs have been reconciled. Convert libcurl versions to canonical major values (`2` for `CURL_HTTP_VERSION_2_0`, `1` for HTTP/1.x, `0` otherwise).

Parse both events with anchored exact regular expressions, require exactly one of each in a successful prefetch proof, require logical-event bytes to equal the first successful interval size, require both explicit IDs non-negative/equal, and require both explicit HTTP majors equal `2`. Do not infer handle identity from generic curl connection-completion lines.

- [ ] **Step 5: Re-pin and run local RED-to-GREEN gates**

Update the patch SHA in `versions.env` and make the delivery test require both exact event formats. Verify the existing `build/science-deps-prefetch` root and its child markers identify this repository, reject symlink/path mismatches, remove only that owned root, re-seed the three already-pinned archives from the verified local cache, and rebuild the private prefix from the final pinned patch before claiming GREEN:

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

The preserved replay must pass accounting and connection proof with the two new authoritative events; all existing local failure/fallback rows must remain green.

- [ ] **Step 6: Commit**

```bash
git add packaging/science_deps/gdal-3.13.1-parallel-head-range.patch \
        packaging/science_deps/versions.env tests/science_deps_script_tests.sh \
        tests/science_gdal_network_test.cpp \
        tests/data/science/prefetch_nvidia_partial_trace.log \
        tests/data/science/prefetch_nvidia_partial_stats.json
git commit -m "fix(scienceearth): reconcile prefetch transport proof"
```

---

### Task 2: Rebuild, audit, and authorize one new evidence set

**Files:**
- Modify: `docs/scienceearth/gdal-build.md`
- Modify: `docs/scienceearth/g0-measurements.md`

**Interfaces:**
- Consumes Task 1's final pinned private prefix and replay proof.
- Produces a non-public readiness record; it does not alter the formal CTest profile.

- [ ] **Step 1: Run the complete non-public regression set**

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
bash tests/science_deps_script_tests.sh
ctest --test-dir build/osgsol_core --output-on-failure
ctest --test-dir build/science_g0_prefetch --output-on-failure \
  -R 'ScienceEarthRelease|AefIndexTool|ScienceG0Manifest|ScienceDepsScript|ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer'
```

- [ ] **Step 2: Rebuild the disposable probe and run the canonical audit**

Require Tier A `0`, Tier B new/removed `0/0`, unresolved `0`, delta below `40 MiB`, science closure below `60 MiB`, anchor-only exported science symbol, valid signature, no worktree/private-prefix strings, and exact protected Desktop fingerprint/digest/counts.

- [ ] **Step 3: Verify every immutable artifact**

Recompute all four rejected-diagnostic hashes, both older formal hashes, and the Desktop pair. Require the new requalification roots below to be absent:

```text
build/science_g0_prefetch/requalification-evidence-v2/control
build/science_g0_prefetch/requalification-evidence-v2/prefetch
build/science_g0_prefetch/formal-evidence-v2
```

- [ ] **Step 4: Document offline readiness and commit**

Record exact build/test/audit/hash results, keep `G0_DECISION=STOP`, and state `PUBLIC_REQUALIFICATION_V2=AUTHORIZED_NOT_RUN`.

```bash
git add docs/scienceearth/gdal-build.md docs/scienceearth/g0-measurements.md
git commit -m "docs(scienceearth): ready prefetch requalification"
```

---

### Task 3: Run the authorized one-shot public requalification and decide readiness

**Files:**
- Modify: `docs/scienceearth/g0-measurements.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`
- Conditional modify after diagnostic PASS: `tests/CMakeLists.txt`

**Interfaces:**
- New diagnostic summaries: `build/science_g0_prefetch/requalification-evidence-v2/control-summary.json` and `prefetch-summary.json`.
- Conditional formal summary: `build/science_g0_prefetch/formal-evidence-v2/live-summary.json`.

- [ ] **Step 1: Snapshot immutable inputs immediately before network use**

Record current commit, wall clock, credential-free proxy endpoint class, all protected hashes/counts, and absence of all v2 evidence roots. Abort before any request on mismatch.

- [ ] **Step 2: Run one control process and one candidate process**

```bash
build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile optimized \
  --evidence-dir build/science_g0_prefetch/requalification-evidence-v2/control \
  --summary-json build/science_g0_prefetch/requalification-evidence-v2/control-summary.json

build/science_g0_prefetch/tests/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile prefetch \
  --evidence-dir build/science_g0_prefetch/requalification-evidence-v2/prefetch \
  --summary-json build/science_g0_prefetch/requalification-evidence-v2/prefetch-summary.json \
  --enforce-latency
```

Never rerun either process. On any incomplete proof, accounting mismatch, correctness/range/byte/retry failure, median above `3000 ms`, or P95 above `8000 ms`, record immutable STOP and `DESKTOP_PACKAGE=NOT_READY`.

- [ ] **Step 3: Promote only after a complete candidate PASS**

Change only `osgVerse_Test_ScienceGdalLive` from `--profile optimized` to `--profile prefetch`, retaining five iterations, explicit v2 formal evidence directory, and `--enforce-latency`. Commit the conditional promotion before running it.

- [ ] **Step 4: Run formal gates once after promotion**

Run the promoted formal CTest in a fresh process, then the protected science-off 16/16 suite, dependency verify, canonical bundle audit, signature/size/export/path checks, memory/correctness/range/camera/cache gates, and clean-machine launch checks. Do not retry any failed formal gate.

- [ ] **Step 5: Record decision and commit**

Document exact summary/raw/proof hashes, all iteration/phase/count/byte/retry results, audit outcomes, and one of:

```text
G0_DECISION=STOP
PUBLIC_REQUALIFICATION_V2=FAIL
DESKTOP_PACKAGE=NOT_READY
```

or, only after every automated gate passes:

```text
G0_DECISION=GO
G0_AUTOMATED_GATES=PASS
PUBLIC_REQUALIFICATION_V2=PASS
HUMAN_PRODUCT_SIGN_OFF=PENDING
DESKTOP_PACKAGE=READY_FOR_AUTHORIZED_UPDATE
```

```bash
git add docs/scienceearth/g0-measurements.md \
        docs/scienceearth/g0-g1-baseline.md tests/CMakeLists.txt
git commit -m "docs(scienceearth): decide prefetch requalification"
```

Do not package or replace the Desktop app in this task.
