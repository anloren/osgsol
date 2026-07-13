# Task 1 Report: Bounded Coordinator Retry

## Status

Complete. The private GDAL parallel HEAD/Range coordinator now retries only HTTP 429, 500, 502,
503, and 504 with the native bounded retry context. Runtime, replay, source-contract, dependency,
and focused regression gates are green.

## Scope and files

- `packaging/science_deps/gdal-3.13.1-parallel-head-range.patch`
  - Adds the five-code classifier and native Range-only retry loop.
  - Reuses the same Range easy handle and multi handle without recreating HEAD.
  - Accounts discarded retry bodies while retaining one logical GET completion for the final body.
  - Rejects terminal HTTP errors before cache publication; transport/add/remove failures retain the
    existing fallback path.
- `packaging/science_deps/versions.env`
  - Pins the final patch SHA256 to
    `5ce830f7853db1c6f53b833741261f55f382c0b8972161999a2d34b99c3ab15f`.
- `tests/science_http2_range_server.mjs`
  - Adds deterministic 429/500/502/503/504 once modes, 503 exhaustion, and path-controlled 404.
- `tests/science_gdal_network_test.cpp`
  - Adds runtime request/session/retry/fallback/rejection checks.
  - Adds anchored retry parsing, chronology, connection/protocol, and byte/accounting validation.
  - Adds replay mutations and per-iteration/aggregate JSON fields.
- `tests/science_deps_script_tests.sh`
  - Locks the bounded classifier and exact retry/fallback event source contract.
- `tests/data/science/prefetch_nvidia_transient_retry_trace.log`
- `tests/data/science/prefetch_nvidia_transient_retry_stats.json`
  - Credential-free successful-retry replay with one HEAD, two physical Range requests, one logical
    GET, and 131089 downloaded bytes.

## Deterministic TDD evidence

Baseline before Task 1 changes:

```text
ctest -R '^osgVerse_Test_ScienceHttpRanges$'
1/1 passed (4.94 s)
```

Runtime RED after adding only the new server/test cases:

```text
ScienceHttpRanges failure: range-429-once did not recover through one coordinator retry
```

The captured coordinator output contained `transient-fallback` and no `transient-retry`, which was
the required pre-production-change failure.

Replay/schema RED after adding the wished replay checks:

```text
HttpProof has no member named coordinatorTransientRetryCount
HttpProof has no member named coordinatorTransientRetryBytes
HttpProof has no member named coordinatorTransientRetryCodes
```

Source-contract RED before the production patch changed:

```text
[FAIL] prefetch patch must centralize coordinator transient status classification
```

During GREEN convergence, the first fresh-runtime failure showed the legacy `range-503` case now
correctly recovered through the new classifier rather than using its prior fallback. The next
failure showed exhausted 503 entering ordinary VSICurl reads after its coordinator budget. The
implementation was tightened to formal rejection, producing exactly three coordinator Range
requests. The same fail-closed boundary gives path-controlled 404 exactly one Range and zero
coordinator retries.

## Owned clean rebuild evidence

All safety checks used the fixed repository root from `git rev-parse --show-toplevel`. The owned
root was a non-symlink and the root, downloads, src, build, and prefix markers were all named
`.science-deps-owned` with the exact value:

```text
osgsol-science-deps-v2:/Users/USER/osgsol/.worktrees/v0.2-runtime-safety
ownership-check=PASS
```

Before removal, the cached archives were checked against
`packaging/science_deps/checksums.txt`:

```text
gdal-3.13.1.tar.gz: OK
proj-9.8.1.tar.gz: OK
zstd-1.5.7.tar.gz: OK
```

Only `build/science-deps-prefetch` was reset and reseeded. The final clean build command was:

```bash
repo_root=$(git rev-parse --show-toplevel)
SCIENCE_DEPS_ROOT="$repo_root/build/science-deps-prefetch" \
  bash "$repo_root/packaging/science_deps/build_science_deps.sh" --build --jobs 4
```

It ended with:

```text
[science-deps] independent GDAL #embed capability: ON
[science-deps] verified private static prefix and manifest
```

The tracked patch and a freshly reconstructed zero-context diff from pristine GDAL to the clean
rebuilt source both had the same SHA:

```text
tracked_patch_sha=5ce830f7853db1c6f53b833741261f55f382c0b8972161999a2d34b99c3ab15f
rebuilt_source_diff_sha=5ce830f7853db1c6f53b833741261f55f382c0b8972161999a2d34b99c3ab15f
```

The installed `science-deps-manifest.json` schema does not store a patch-pin field. Patch integrity
is instead enforced by the builder against `GDAL_PREFETCH_PATCH_SHA256` before applying it; the
clean builder and subsequent verifier both passed.

## Final verification

Private prefix verifier:

```text
gdal-3.13.1.tar.gz: OK
proj-9.8.1.tar.gz: OK
zstd-1.5.7.tar.gz: OK
[science-deps] independent GDAL #embed capability: ON
[science-deps] verified private static prefix and manifest
```

Source contract:

```text
[OK] ScienceEarth private dependency builder contract
```

Final focused CTest after the clean rebuild and final self-review cleanup:

```text
1/3 osgVerse_Test_ScienceGdalSpike ........ Passed 0.06 sec
2/3 osgVerse_Test_ScienceHttpRanges ....... Passed 6.95 sec
3/3 osgSol_Test_ScienceHttpRangeServer .... Passed 0.06 sec
100% tests passed, 0 tests failed out of 3
Total Test time (real) = 7.07 sec
```

`git diff --check` and the staged equivalent both passed. The self-review removed one unused retry
comparison helper, then rebuilt `osgVerse_Test_ScienceHttpRanges` and reran the full focused CTest
and shell contract shown above.

## Self-review

- The classifier has exactly five cases: 429, 500, 502, 503, and 504.
- Retry attempts are native-context bounded, contiguous, and Range-only; HEAD is never recreated or
  re-added.
- Retry event evidence carries exact Range, status, body bytes, attempt, positive native delay,
  connection, and HTTP major.
- Retry bodies contribute to physical byte statistics; the logical completion reports only the
  final successful interval or terminal body.
- A retry add/remove/transport failure cannot publish. Terminal HTTP errors are formally rejected,
  and `AddRegion()` is reached only after final exact range/content/transport validation.
- Replay proof parsing consumes one matching transient response per retry and validates the next
  exact request chronologically on the authoritative shared HTTP/2 connection.
- Missing, duplicated, malformed, nontransient, wrong-byte, wrong-attempt, zero-delay,
  wrong-connection, HTTP/1, wrong-Range, missing-request, extra-HEAD, fallback, and duplicate
  publication mutations all fail closed.
- No frozen v3 evidence, G1 code, Desktop app state, tag, or remote branch was changed.
- The pre-existing untracked `packaging/scienceearth/__pycache__/` remains untouched.

## Commits and concerns

- Implementation: `ca1302315746838c53d60e837b32e69b2ba35124`
- This report is committed separately as the report-only follow-up commit containing this file.

No remaining Task 1 blocker or correctness concern was found. The worktree and branch are preserved;
nothing was pushed or tagged.
