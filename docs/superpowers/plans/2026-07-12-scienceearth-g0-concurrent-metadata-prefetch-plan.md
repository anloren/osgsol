# ScienceEarth G0 Concurrent Metadata Prefetch Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Overlap the mandatory cold HTTP HEAD and exact first 128 KiB Range request in the private ScienceEarth GDAL runtime, then prove whether the change brings both formal AlphaEarth fixtures below the immutable latency limits without weakening any existing gate.

**Architecture:** Apply one checksummed patch to the private GDAL 3.13.1 source. The patch activates only through a path-specific option, schedules HEAD and `bytes=0-131071` on the existing libcurl multi handle, uses `CURLOPT_PIPEWAIT` for same-connection HTTP/2 multiplexing, and publishes bytes only after HEAD/Range consistency checks. A local Node HTTP/2 server supplies deterministic overlap and failure injection; the existing Python server remains the strict byte/correctness oracle.

**Tech Stack:** C++17, GDAL 3.13.1 private static build, libcurl multi API, HTTP/2, Node.js built-in `node:http2`, OpenSSL test certificates, Python 3 strict range fixture, CMake/CTest, JSON evidence, macOS arm64.

## Global Constraints

- G0 remains `STOP` until a new formal run passes; Tasks in G1 remain forbidden.
- Formal fixtures remain NVIDIA HQ `fid=9790` and Hong Kong `fid=181593`, year 2025.
- RGB remains A01/A16/A09 with exact `sign(v) * pow(abs(v) / 127.5, 2)` dequantization.
- Source orientation remains bottom-up and output normalization remains top-down.
- The exact 4x source overview and 256x256 source window to 64x64 output remain mandatory.
- `CPL_VSIL_CURL_USE_HEAD=YES` remains enabled; HEAD is never replaced by GET.
- The first GET remains exactly `Range: bytes=0-131071`; every GET carries one Range header and receives HTTP 206.
- Comma multi-range, GET 200, unbounded prefetch, complete COG download, warm data, and warm connection reuse between iterations are forbidden.
- Per-iteration conservative transfer remains `<= 16,777,216` bytes.
- Uncached median remains `<= 3000 ms`, P95 remains `<= 8000 ms`, with five cold-data plus cold-connection iterations per fixture.
- Runtime fallback may preserve availability after a benign Range failure, but any fallback, retry, duplicate metadata GET, protocol mismatch, or missing overlap fails formal evidence.
- The protected Desktop app, product runtime, camera, terrain, 3D Tiles, photo, media, and existing caches are not modified by this prototype.
- The previous formal evidence files and their SHA-256 values remain immutable:
  - baseline summary: `17ff3cd876e7995c5257fad1f2da7f27bf0ae7901d46716829f1440d16a321ed`;
  - optimized summary: `e4eb9ea1a8d5795db6197110ba96f4851c127dbe06b6bfcae5d3e9ea9b466dfe`.

---

## File Responsibility Map

- `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`: private GDAL request coordinator, validation, capped buffer, cache publication, fallback.
- `packaging/science_deps/versions.env`: immutable patch SHA-256 pin.
- `packaging/science_deps/build_science_deps.sh`: verify and apply the new patch exactly once.
- `tests/science_deps_script_tests.sh`: patch presence, pin, application order, and drift contract.
- `tests/science_http2_range_server.mjs`: local TLS HTTP/2 HEAD/Range server, overlap log, and failure injection.
- `tests/science_gdal_network_test.cpp`: server lifecycle, path-specific activation, proof schema, correctness and formal profiles.
- `tests/CMakeLists.txt`: Node executable/server path wiring and local/formal profile commands.
- `docs/scienceearth/g0-measurements.md`: diagnostic/formal request chronology, phase and latency evidence.
- `docs/scienceearth/g0-g1-baseline.md`: unchanged hard limits and resulting `STOP` or `GO` decision.

### Task 1: Prove the HTTP/2 RED, implement the private patch, and turn it GREEN

**Files:**
- Create: `tests/science_http2_range_server.mjs`
- Create: `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`
- Modify: `tests/science_gdal_network_test.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `packaging/science_deps/versions.env`
- Modify: `packaging/science_deps/build_science_deps.sh`
- Modify: `tests/science_deps_script_tests.sh`

**Interfaces:**
- Produces server JSONL events: `session_start`, `stream_start`, `response_headers`, `stream_end`.
- Server CLI: `--file`, `--ready-file`, `--log-file`, `--cert`, `--key`, `--budget-bytes`, `--mode`.
- Modes: `success`, `range-200`, `range-200-body`, `short-range`, `size-mismatch`, `range-503`.
- Consumes: GDAL 3.13.1 `VSICurlHandle::GetFileSizeOrHeaders()`, `GetCurlMultiHandleFor()`, `AddRegion()`, and `VSIGetPathSpecificOption()`.
- Produces: path option `OSGSOL_VSICURL_PREFETCH_HEAD_RANGE=YES`.
- Produces: debug events `ParallelHeadRange: started`, `published`, or `fallback=<reason>`.
- Produces a GREEN behavioral contract consumed by Tasks 2-4.

- [ ] **Step 1: Add the local HTTP/2 server and a failing overlap regression**

Use Node's built-in `node:http2` only. Every stream gets one stable `session_id` and its native
HTTP/2 `stream.id`. In `success` mode, delay the HEAD response by 200 ms. A Range stream must begin
before the delayed HEAD completes and share the same `session_id`.

Add a C++ `verifyParallelMetadataPrefetch()` that generates a temporary self-signed certificate
with `/usr/bin/openssl`, starts the server, sets:

```cpp
VSISetPathSpecificOption(vsiUrl.c_str(),
    "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
```

opens the fixture, reads byte zero, closes the dataset, clears the option, and verifies:

```cpp
require(headStart < rangeStart && rangeStart < headEnd,
        "HEAD and first Range did not overlap");
require(headSession == rangeSession,
        "HEAD and first Range used different HTTP/2 sessions");
require(firstRange == "bytes=0-131071",
        "prefetch Range changed");
```

- [ ] **Step 2: Run the unpatched integration and verify RED**

Build/run against the current prefix before rebuilding Task 1:

```bash
cmake --build build/science_g0 --target osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0 -R '^osgVerse_Test_ScienceHttpRanges$' \
  --output-on-failure
```

Expected: FAIL with `HEAD and first Range did not overlap` because the installed prefix does not
yet contain the patch.

- [ ] **Step 3: Implement strict server accounting and failure modes before production code**

The server rejects a missing Range, comma Range, any interval other than `0-131071` for the first
GET, and any body that exceeds the total budget. It emits no fixture bytes for `range-200`; that
mode returns an empty 200 so the client must reject it without downloading the object.

Add a table-driven C++ regression with the desired outcomes:

```cpp
const PrefetchCase cases[] = {
    {"success", true, false},
    {"range-503", true, true},
    {"range-200", false, false},
    {"range-200-body", false, false},
    {"short-range", false, false},
    {"size-mismatch", false, false},
};
```

`range-503` must require runtime fallback and a second normal exact Range. All other invalid
integrity responses must require open failure, zero cache publication, and at most 131072 received
body bytes. `range-200-body` attempts to stream more than 131072 bytes and must prove the capped
writer aborts before byte 131073. Against the unpatched prefix, the success/invalid cases must fail for the intended
missing behavior rather than test setup errors.

- [ ] **Step 4: Write and run the failing patch-delivery contract**

Add a `prefetch_patch` path and require the file, checksum variable, builder verification,
application order, path option, and `CURLOPT_PIPEWAIT`. Run
`bash tests/science_deps_script_tests.sh` and observe the expected missing-patch RED.

- [ ] **Step 5: Add the independent checksum pin and builder application**

After the patch bytes are final, run:

```bash
shasum -a 256 packaging/science_deps/gdal-3.13.1-parallel-head-range.patch
```

Add the printed lowercase 64-character value literally as `GDAL_PREFETCH_PATCH_SHA256` in
`versions.env`. Extend `verify_pin_contract()` to hash the file and compare it to the variable.
Apply the patch after the existing Shapelib-disable and relocatable patches:

```bash
patch --batch --forward -p1 \
    <"$script_dir/gdal-3.13.1-parallel-head-range.patch"
```

- [ ] **Step 6: Implement the opt-in request coordinator in the patch**

Add `ParallelHeadRangeResult` with attempted, HEAD/Range validity, cache publication, fallback,
codes, sizes, and reason. Eligibility must require ordinary read-only HTTP(S), effective HEAD,
path-specific opt-in, a cold cache, no retry/header-only path, and exact chunk size 131072.

The Range easy handle uses `CURLOPT_PIPEWAIT=1L`, exact `Range: bytes=0-131071`, the same
URL/header policy, and a 131072-byte capped writer. Both handles use the existing filesystem multi
handle. Every exit removes both easy handles. Only validated HEAD populates file properties; only
matching 206 Range bytes call:

```cpp
poFS->AddRegion(m_pszURL, 0, 131072, rangeBuffer.data());
```

GET 200, overflow, malformed interval, or size mismatch invalidates the URL and fails. A benign
Range timeout/status discards bytes and allows the unchanged normal ranged-read fallback.

- [ ] **Step 7: Verify patch applicability before a build**

Run the script test plus a dry-run apply against a newly extracted GDAL 3.13.1 archive. Both must
pass, and a second `--forward` application must fail rather than silently duplicate the patch.

- [ ] **Step 8: Rebuild the private prefix from clean source**

```bash
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --build --jobs 4
SCIENCE_DEPS_ROOT="$PWD/build/science-deps-prefetch" \
  bash packaging/science_deps/build_science_deps.sh --verify
```

Reconfigure `build/science_g0_prefetch` against that prefix; never overwrite `build/science_g0`.

- [ ] **Step 9: Run the behavioral GREEN and existing local gates**

```bash
cmake --build build/science_g0_prefetch --target \
  osgVerse_Test_ScienceGdalSpike osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0_prefetch \
  -R 'ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer' \
  --output-on-failure
```

Expected: overlap occurs on one HTTP/2 session; failure modes match Task 1; the existing Python
strict oracle remains green.

- [ ] **Step 10: Commit the tested server, pinned patch, and GREEN implementation**

```bash
git add packaging/science_deps/gdal-3.13.1-parallel-head-range.patch \
        packaging/science_deps/versions.env \
        packaging/science_deps/build_science_deps.sh \
        tests/science_deps_script_tests.sh \
        tests/science_http2_range_server.mjs \
        tests/science_gdal_network_test.cpp tests/CMakeLists.txt
git commit -m "perf(scienceearth): overlap initial metadata requests"
```

### Task 2: Add a non-promoted prefetch profile and formal proof schema

**Files:**
- Modify: `tests/science_gdal_network_test.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Extends: `RangeProfile::{Baseline, Optimized, Prefetch}`.
- CLI accepts: `--profile baseline|optimized|prefetch`.
- Live CLI accepts: `--evidence-dir PATH`; diagnostic/formal commands must provide it explicitly.
- Produces: `metadata_prefetch` JSON proof object.
- Preserves: formal CTest profile remains `optimized` in this task.

- [ ] **Step 1: Write failing parser, activation, and proof regressions**

Require `prefetch` to inherit the optimized 128 KiB/multirange settings while enabling only the
path-specific option. Add an exact serialized object assertion:

```json
"metadata_prefetch": {
  "enabled": true,
  "head_request_count": 1,
  "range_request_count": 1,
  "range_start": 0,
  "range_end": 131071,
  "head_http_version": 2,
  "range_http_version": 2,
  "shared_connection": true,
  "requests_overlapped": true,
  "cache_published": true,
  "fallback_reason": ""
}
```

- [ ] **Step 2: Build and verify RED**

Expected: compile/parser regression fails because `Prefetch` and the proof object do not exist.

- [ ] **Step 3: Implement RAII path activation**

Add:

```cpp
class ScopedPathSpecificOption
{
public:
    ScopedPathSpecificOption(const std::string& path,
                             const char* key, const char* value);
    ~ScopedPathSpecificOption();
private:
    std::string _path;
    std::string _key;
};
```

Construct it immediately before `GDALOpenEx()` only for `Prefetch`, and destroy it only after
dataset close. Baseline and Optimized must observe the option as absent.

- [ ] **Step 4: Parse timestamped curl/CPL evidence fail closed**

Extend `DebugCapture` with steady-clock timestamps while preserving the existing message vector.
The parser must prove exactly one HEAD and the first GET, header-out overlap before HEAD completion,
HTTP/2 for both, the same connection identity, one 206 interval `[0,131071]`, cache publication,
and an empty fallback reason. A missing or ambiguous event fails Prefetch evidence.

Thread an explicit live evidence directory through `LiveCommand`, `runLive()`,
`runLiveIteration()`, `writeRawTransportEvidence()`, and `writeParsedProof()`. Reject an existing
non-directory, a symlink, the protected old evidence directory, and a path outside the active
build tree. Create it atomically before the first iteration. Offline mode retains the compiled
default directory.

- [ ] **Step 5: Keep formal CTest on the old optimized profile**

`osgVerse_Test_ScienceGdalLive` remains `--profile optimized`. Add no promoted CTest yet. The
prefetch profile is callable only by the isolated diagnostic command in Task 4.

- [ ] **Step 6: Run local profile regressions and commit**

```bash
cmake --build build/science_g0_prefetch --target osgVerse_Test_ScienceHttpRanges --parallel 8
ctest --test-dir build/science_g0_prefetch \
  -R 'ScienceHttpRanges|ScienceHttpRangeServer' --output-on-failure
git add tests/science_gdal_network_test.cpp tests/CMakeLists.txt
git commit -m "test(scienceearth): gate metadata prefetch evidence"
```

### Task 3: Rebuild, audit, and prove the prototype remains isolated

**Files:**
- Modify: `docs/scienceearth/gdal-build.md`
- Modify: `docs/scienceearth/g0-measurements.md`

**Interfaces:**
- Consumes: Tasks 1-2 private prefix and test tree.
- Produces: build/audit evidence; no product package.

- [ ] **Step 1: Run clean private dependency build and verification**

Delete only the owned `build/science-deps-prefetch` directory after its marker/path guards pass,
then run `--build --jobs 4` and `--verify`. Record build wall time, prefix size, resolved patch
hash, driver set, `/vsicurl/` capability, and runtime probe result.

- [ ] **Step 2: Run complete non-public regressions**

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
bash tests/science_deps_script_tests.sh
ctest --test-dir build/osgsol_core --output-on-failure
ctest --test-dir build/science_g0_prefetch --output-on-failure \
  -R 'ScienceEarthRelease|AefIndexTool|ScienceG0Manifest|ScienceDepsScript|ScienceGdalSpike|ScienceHttpRanges|ScienceHttpRangeServer'
```

- [ ] **Step 3: Build the disposable probe and run canonical audit**

The canonical audit must report Tier A 0, Tier B new/removed 0/0, size delta below 40 MiB, no
workspace/private prefix, no exported science symbol beyond the anchor, and unchanged protected
Desktop fingerprint.

- [ ] **Step 4: Prove previous formal evidence did not change**

Run `shasum -a 256` on the two previous summaries and compare to the Global Constraints. Also
require `git diff` to contain no path under `build/science_g0/science-network-evidence`.

- [ ] **Step 5: Document and commit prototype readiness**

Record only verified build/local/audit results. Keep `G0_DECISION=STOP` and state that public
diagnostic is pending.

```bash
git add docs/scienceearth/gdal-build.md docs/scienceearth/g0-measurements.md
git commit -m "docs(scienceearth): record metadata prefetch prototype"
```

### Task 4: Run isolated public A/B and decide formal promotion

**Files:**
- Modify: `docs/scienceearth/g0-measurements.md`
- Modify: `docs/scienceearth/g0-g1-baseline.md`
- Conditional modify after diagnostic PASS: `tests/CMakeLists.txt`

**Interfaces:**
- Diagnostic summaries:
  - `build/science_g0_prefetch/diagnostic-evidence/control-summary.json`;
  - `build/science_g0_prefetch/diagnostic-evidence/prefetch-summary.json`.
- Formal summary after promotion only:
  - `build/science_g0_prefetch/formal-evidence/live-summary.json`.

- [ ] **Step 1: Snapshot immutable evidence and Desktop fingerprints**

Record the two old summary hashes, the protected Desktop bundle fingerprint, current commit, proxy
endpoint class without credentials, and wall-clock start. Abort if any old hash differs.

- [ ] **Step 2: Run separate-process five-iteration diagnostic A/B**

Run control and candidate as separate processes and separate output paths:

```bash
build/science_g0_prefetch/bin/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile optimized \
  --evidence-dir build/science_g0_prefetch/diagnostic-evidence/control \
  --summary-json build/science_g0_prefetch/diagnostic-evidence/control-summary.json
build/science_g0_prefetch/bin/osgVerse_Test_ScienceHttpRanges \
  --live-cases tests/data/science/alphaearth_rgb_cases.json \
  --iterations 5 --profile prefetch \
  --evidence-dir build/science_g0_prefetch/diagnostic-evidence/prefetch \
  --summary-json build/science_g0_prefetch/diagnostic-evidence/prefetch-summary.json \
  --enforce-latency
```

The diagnostic command must be configured to write raw files below `diagnostic-evidence`, never
the old formal directory.

- [ ] **Step 3: Apply the immutable diagnostic decision**

Reject promotion and keep `G0_DECISION=STOP` if any candidate iteration lacks overlap, shared
HTTP/2 connection, exact first Range, byte/correctness proof, or if either fixture exceeds median
3000/P95 8000. Do not rerun a rejected sample set.

If and only if the complete candidate set passes, change the formal CTest profile from
`optimized` to `prefetch`, keep five iterations and `--enforce-latency`, and commit that promotion.

- [ ] **Step 4: Run formal gates only after diagnostic PASS**

Run the promoted formal CTest once in a fresh process/evidence directory, followed by protected
science-off, private dependency verification, canonical bundle audit, signature, size, memory,
correctness, range, camera/cache, and clean-machine launch gates. Any failure restores
`G0_DECISION=STOP`; it does not trigger a rerun or threshold change.

- [ ] **Step 5: Record the final decision and experience readiness**

Document both control and candidate results, hashes, phases, request chronology, bytes, retries,
and audit outcome.

- If diagnostic or formal evidence fails: record the precise failure and state `DESKTOP_PACKAGE=NOT_READY`.
- If every automated gate passes: record `G0_AUTOMATED_GATES=PASS`, retain
  `HUMAN_PRODUCT_SIGN_OFF=PENDING`, and state `DESKTOP_PACKAGE=READY_FOR_AUTHORIZED_UPDATE`.

Do not replace the Desktop app, tag, push, or start G1 in this task. A fixed-name desktop update
requires the user's explicit packaging instruction after the readiness report.

- [ ] **Step 6: Commit the evidence decision**

```bash
git add tests/CMakeLists.txt docs/scienceearth/g0-measurements.md \
        docs/scienceearth/g0-g1-baseline.md
git commit -m "docs(scienceearth): decide metadata prefetch gate"
```
