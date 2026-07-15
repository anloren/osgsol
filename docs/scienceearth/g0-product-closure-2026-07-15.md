# ScienceEarth G0 Product Closure and Follow-up Register

**Recorded:** 2026-07-15

**Product baseline:** `v0.3.0` / `ScienceEarth-v0.3.0` / commit
`929fab38b14fba5b703f050d93eef26e7b744a07`

**Protected pre-science rollback:** `v0.2.0` / `ScienceEarth` / commit
`0e91c7c4b121d80b929d595ea711d3dd0833ee67`

**Next-development branch:** `codex/scienceearth-g2-query-service`

## 1. Authoritative status

```text
G0_PRODUCT_BASELINE=ACCEPTED
G0_FORMAL_GATE=NOT_GO
G2_PROCEED=YES
G0_V6_WIP=QUARANTINED_NOT_RELEASE_READY
V0_3_0_CLEAN_SOURCE_TEST_BASELINE=19_OF_20_DUE_TO_V6_RED_CONTRACT
V0_3_0_TAGS=MUST_NOT_MOVE
```

The user accepted the working `v0.3.0` product behavior before its paired tags were created and,
on 2026-07-15, approved proceeding to G2 after a short G0 isolation and documentation closeout.
This is a product decision. It does not rewrite the historical formal G0 result into a technical
`GO`.

The formal G0 documents remain truthful: the fixed three-second uncached first-RGB median was not
passed by every formal run, and the later v5 public requalification stopped after incomplete
transient-HTTP evidence. Those historical results are retained; they are no longer allowed to
block the facade-first G2 query-service slice.

## 2. Fresh state audit

The following checks were run immediately before this record was written. The first clean v5
science-off run exposed that nine committed v6 RED-only test changes were still present in the
`v0.3.0` history. It correctly failed `ScienceDepsScript` because the production patch remained
v5. After those tests were quarantined from the G2 line in commit
`bf776e51d158ecf36e552fac843a4402e0fd7d80`, the v5 source contract and complete science-off suite
were rerun from the clean G2 branch.

| Check | Result |
|---|---|
| Clean `v0.3.0`-history science-off baseline before test isolation | `19/20`; v6 RED source contract failed against v5 patch |
| Science-off baseline after G2 test isolation | `20/20` passed |
| Private dependency builder source contract after isolation | Passed against the committed v5 patch |
| Existing v6 private prefix verification | Passed against the quarantined v6 source state |
| Fresh clean v5 prefix build | Passed from locally cached, checksum-verified archives |
| Fresh clean v5 prefix verification | Passed; archives, embed capability, static prefix, and manifest verified |
| v6 local ownership/fault matrix | Failed; not release-ready |
| Desktop metadata | `0.3.0`, release tag `ScienceEarth-v0.3.0`, source commit `929fab3...` |
| v6-only runtime markers in the Desktop executable | Absent; the Desktop was not identified as a v6 build |
| Current Desktop strict signature verification | Failed because Finder/resource-fork extended attributes are present |
| Current Desktop direct clean-machine dependency audit | Existing Homebrew Python 3.14 dependency remains |

Absence of v6-only marker strings is a scope check, not a claim of byte-for-byte rebuild
reproducibility. A future release must be rebuilt from a clean committed source and independently
audited.

## 3. Quarantined v6 state

Six previously uncommitted v6 files were preserved without modification on:

```text
branch: codex/scienceearth-g0-v6-wip
commit: f37e5e9560d4d610efc31ae49359e035ab0897d2
patch:  939ea11953475e42adeac0f1d0b6fb54565715427a174ddcbed7afa0e23b387a
```

The WIP commit is explicitly non-release-ready. Fresh local fault verification exposed at least
these two unresolved paths:

- `v6-ownership-initial-head-add`: initial HEAD multi-add failure did not produce the required
  provisional detach behavior;
- `v6-ownership-head-405-ordinary-remove-persistent`: persistent ordinary compatibility removal
  did not produce the required retained-ownership evidence.

The already committed v6 RED-only tests are also retained on the WIP branch. On the G2 branch,
these nine test commits were reversed together by `bf776e51d158ecf36e552fac843a4402e0fd7d80`:

```text
02b053f 5ff7b47 77d9af2 0596540 55d89ae 5c877e4 4c5cbab b9b6555 c0f83bd
```

The immutable `v0.3.0` tags still contain those RED-only test commits. That does not change the
shipped application runtime, but a clean tag checkout reports `19/20` science-off tests because
`ScienceDepsScript` asks the v5 production patch for the deliberately absent v6 contract. The G2
branch correction is the authoritative clean development baseline; the tags must not be moved to
hide the historical mismatch.

The G2 branch does not contain those six changes. It is back on the committed v5 patch and pin:

```text
GDAL_PREFETCH_PATCH_SHA256=0e67079267a4adfc316f20c88ba22cf078e5d653d2f35c2e3b54fff7efd0de1e
```

The v6-built prefix at `build/science-deps-prefetch` must not be used for G2. The clean G2 prefix
is `build/science-deps-g2-v5/prefix`.

## 4. Unconditional required follow-ups

### 4.1 Before the next tagged ScienceEarth version

- Build only from a clean, committed source tree and record the exact source commit in the app.
- Use a dependency prefix whose source patch hash matches the tracked pin; do not reuse the v6 WIP
  prefix.
- Run the complete science-off suite and the G2 science-core/provider/preview suites with zero
  failures.
- Verify source catalog, load, year change, replacement failure retention, cancellation,
  hide/show/remove, camera invariance, and Quit in the fixed Desktop application.
- Remove package extended attributes before signing, then require
  `codesign --verify --deep --strict` to pass after the Desktop copy is in its final location.
- Run the offscreen render/package smoke and inspect its log for OpenGL or shader failures.
- Do not move or overwrite `v0.3.0`, `ScienceEarth-v0.3.0`, or the protected `ScienceEarth` tag.

### 4.2 Before adding or enabling another remote science provider

- Give the provider an explicit latency, transfer, memory, cache, cancellation, and failure-retention
  acceptance budget based on its real data shape.
- Keep progress and cancellation visible and retain the last successful artifact while a
  replacement is loading or fails.
- Record source, dataset, requested and actual coverage, time, variables, units, visualization,
  processing steps, and attribution in the resulting artifact.
- Measure normal user-visible load behavior. The historical AlphaEarth three-second number is a
  useful observation, not a universal threshold for Sentinel-2, DEM, or later providers.

### 4.3 Before claiming clean-machine macOS distribution

- Remove, bundle, or otherwise make relocatable the existing direct
  `/opt/homebrew/opt/python@3.14` runtime dependency.
- Test the signed app on a Mac without the development Homebrew tree or private build prefixes.
- Re-run forbidden-path, unresolved-dependency, signature, launch, and fixed-Desktop smoke checks.

## 5. Conditional required follow-up if v6 is ever adopted

The quarantined v6 branch is optional. If it is abandoned, no v6 fault-matrix work blocks G2. If
any v6 code is proposed for merge or packaging, all of the following become mandatory:

- fix both known ownership failures without weakening their assertions;
- rebuild a new private dependency prefix from the final committed patch and matching hash;
- pass the complete offline HTTP/2, ownership, abandonment, capacity, dependency, science-off,
  packaging, and signature suites;
- review callback lifetime, multi/easy-handle ownership, cancellation, and double-cleanup paths;
- commit the implementation, pin, tests, and decision record before any package is built.

Never cherry-pick the WIP production patch or updated hash by itself.

## 6. Deferred product work, not a G2-1 blocker

- further AlphaEarth first-RGB optimization beyond the current usable product behavior;
- exhaustive curl add/remove/perform/getinfo fault injection;
- immutable one-shot evidence roots, negative process-history ledgers, and mutation matrices for
  symlinks, FIFOs, and unexpected evidence entries;
- a new public v6 live requalification campaign.

These items may be reconsidered when a measured product failure or distribution requirement
justifies them. They are not prerequisites for the unified query-service migration.

## 7. G2 continuation boundary

G2-1 may now implement only the approved facade-first slice:

- GDAL-free generic query/artifact contracts and `osgSolScienceCore`;
- source registry and single-active-job query service;
- AlphaEarth adapter around the verified v5 runtime rather than a runtime rewrite;
- migration of UI, Agent tools, and preview consumption to the service;
- visible source catalog and last-good-artifact retention;
- no Sentinel-2, Copernicus DEM, STAC request, camera change, or release tag in this slice.

The authoritative G2-1 design remains
`docs/superpowers/specs/2026-07-14-scienceearth-g2-query-service-design.md`.

## 8. G2-1 progress snapshot after full automated verification

**Current packaged manual-candidate state:** `9234c4b`

```text
G2_TASKS_1_TO_7=COMPLETE
G2_FOCUSED_TESTS=5_OF_5_PASS
G2_EARTHEXPLORER_COMPILE=PASS
G2_FULL_OFFLINE_REGRESSION=SCIENCE_ON_34_OF_34_AND_OFF_20_OF_20_PASS
G2_LOCAL_INDEX_SMOKE=2018_DATASET_7851_AND_2025_DATASET_9790_PASS
G2_OFF_BUILD_ISOLATION=PASS
G2_STAGING_PACKAGE=PASS
G2_MANUAL_CANDIDATE=INSTALLED_UNTAGGED
G2_STAGING_FINAL_SIGNATURE=PASS
G2_DESKTOP_SIGNATURE=DEEP_PASS_STRICT_BLOCKED_BY_FILEPROVIDER_FINDERINFO
G2_DESKTOP_FIRST_FOREGROUND_APPROVAL=COMPLETE
G2_DESKTOP_POST_APPROVAL_OFFSCREEN=PASS
G2_PANEL_LAYOUT_REGRESSION=FIXED_PENDING_HUMAN_RETEST
G0_IMMUTABLE_V0_2_AUDIT=STOP_UNCHANGED
DESKTOP_APP=UPDATED_IN_PLACE_WITH_ROLLBACK
TAG_CREATED_OR_MOVED=NO
REMOTE_PUSH=NO
```

Completed and committed on the G2 branch:

- `8d8f021`: generic GDAL-free query/artifact contracts and isolated core target;
- `ec39665`: provider interface and deterministic owning source registry;
- `a9496f4`: single-active-job service with validation, cancellation, stale-result rejection, and
  last-good retention;
- `5226f61`: AlphaEarth adapter around the accepted v5 runtime;
- `3fa9f60`: atomic renderer/UI/Agent consumer migration, visible generic source catalog, shared
  bounded point-query builder, retained-result rendering, and camera-invariant Agent tests.
- `9234c4b`: responsive Earth control-panel constraints, standard bidirectional wheel scrolling,
  and a pure viewport-layout regression test after the first manual candidate exposed an
  unbounded half-screen panel.

The last commit intentionally contains Tasks 5-7 together. Changing the preview-layer constructor
alone would leave `earth_main.cpp` uncompilable until UI and Agent injection changed, so the first
safe commit boundary was the complete consumer cutover.

Task 8 is now complete. Its exact commands, timings, index fingerprint, dataset ids, and known
warnings are recorded in
`docs/superpowers/plans/2026-07-15-scienceearth-g2-query-service-plan.md`.

### Mandatory next work before the user can complete manual verification

1. Re-test the repaired panel in `/Users/USER/Desktop/osgSol Earth.app`: it must open at about
   one third of the compact Retina viewport, keep the globe and bottom AI bar usable, allow manual
   resize/collapse, and scroll both down and back up.
2. Run the remaining Task 10 human matrix: catalog meaning, load, year change,
   replacement-failure retention, cancel, hide/show/remove, panel scrolling, low-altitude placement,
   camera invariance, Agent research, photo regression, 3D Tiles regression, and Quit.
3. Only after explicit human acceptance decide the next version/tag and remote synchronization.
   Never move the protected `v0.3.0`, `ScienceEarth-v0.3.0`, `v0.2.0`, or `ScienceEarth` tags.

The staging package, fixed identity, private-path/RPATH audits, package-contract offscreen run, and
staging strict signatures passed. The accepted pre-G2 rollback remains at
`build/desktop-backups/pre-g2-243f00b/osgSol Earth.app`; the immediately previous G2 candidate is at
`build/desktop-backups/pre-panel-layout-243f00b/osgSol Earth.app`.

The user completed the first foreground launch and reported the panel-layout defect with a
2048x1152 Retina screenshot. After the `9234c4b` repair, the Desktop-path offscreen smoke reached
application code, created its 1920x1080 context and capture, logged no selected OpenGL/shader fatal
marker, and exited zero. FileProvider immediately recreates an empty `com.apple.FinderInfo` on the
Desktop bundle root: `codesign --verify --deep` passes, while `--deep --strict` rejects that external
metadata. This remains a release-packaging issue; it is not reported as a strict-signature pass.

### Mandatory before the next tagged ScienceEarth release

- Resolve the immutable G0 audit mismatch. The current static GDAL/PROJ executable exceeds the
  v0.2 hard size stop and does not match the audit policy's science-plugin ownership model. Either
  restore a compliant isolated plugin/closure or formally approve and ratchet a replacement policy;
  do not label the current result a formal G0 `GO`.
- Remove, bundle, or make relocatable the direct Homebrew Python 3.14 dependency, then test on a
  clean Mac without the development Homebrew tree.
- Pin and validate the GLCore OSG runtime in the release profile. Formal packaging must continue to
  use the accepted GLCore SDK and the offscreen shader/OpenGL assertion; a legacy Homebrew OSG
  default must never be allowed to recreate the GLSL-130 failure.
- Complete the twelve-item human matrix and the Desktop post-approval smoke/cleanup/re-sign gate.
- Resolve final-location strict signing independently of Desktop FileProvider metadata, for example
  by selecting a release location/installation model that does not rewrite the signed bundle.
- Investigate the NASA GIBS dated-layer 404 warnings seen in offscreen logs if the same layer is
  visibly missing during manual verification; add a last-available-date fallback before release if
  reproducible.

### Required later, but not a blocker for this local manual candidate

- Add no second remote science provider until its latency/transfer/memory/cache/cancellation budget
  and complete provenance contract are defined and verified.
- Keep the v6 WIP quarantined unless its two documented ownership failures and full verification
  matrix are resolved.
