# ScienceEarth G0 v2 Audit Readiness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce a trustworthy G0 v2 macOS audit that keeps the immutable v0.2 baseline, gives verified scientific data a separate bounded size budget, removes symbol-address false positives, and reports the remaining isolation failures by root cause.

**Architecture:** Keep the approved v0.2 reference and ratchet byte-for-byte immutable. Add a v2 evaluation layer that validates a signed-in-package scientific-data manifest, separates runtime and scientific-data bytes, and compares `nm` findings through an address-independent comparison identity without changing stored historical identities. Preserve the complete raw finding list in JSON while adding a compact root-cause summary for decisions.

**Tech Stack:** Python 3 standard library, `unittest`, Bash 3.2-compatible packaging, CMake/CTest, Mach-O tools (`otool`, `nm`, `strings`, `codesign`), JSON and SHA-256.

## Global Constraints

- The immutable baseline remains `v0.2.0` / `ScienceEarth`, commit `0e91c7c4b121d80b929d595ea711d3dd0833ee67`.
- Do not edit or regenerate `packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json` or rewrite its approved SHA-256.
- Do not move the immutable `ScienceEarth` tag or any existing release tag.
- Runtime delta budget remains strict: PASS at `<= 40 MiB`, REVIEW at `> 40 MiB` and `<= 60 MiB`, STOP at `> 60 MiB`.
- Verified scientific-data budget: PASS at `<= 128 MiB`, REVIEW at `> 128 MiB` and `<= 160 MiB`, STOP at `> 160 MiB`.
- Combined package delta budget: PASS at `<= 160 MiB`, REVIEW at `> 160 MiB` and `<= 192 MiB`, STOP at `> 192 MiB`.
- Science plugin-closure budget remains PASS at `<= 40 MiB`, REVIEW through `60 MiB`, and STOP above `60 MiB`.
- A file counts as scientific data only when an exact relative path, byte count, role, source id, and SHA-256 are validated from `Contents/misc/science/data-manifest.json`.
- Scientific-data classification never applies to Mach-O, symlinks, executables, libraries, plugins, source/build paths, or files outside `Contents/misc/science/`.
- Size relaxation must not ratchet or waive missing-plugin, GDAL/PROJ/ZSTD isolation, unresolved dependency, forbidden path, export, signature, provenance, or science-off failures.
- The only user-facing app remains `/Users/USER/Desktop/osgSol Earth.app`; audit work uses staging bundles and never packages directly onto the Desktop.
- Do not launch, open, focus, render, or execute the Desktop app. Do not create local listeners or run tests labelled `network-local`.
- Do not change macOS settings, security policy, signing identity, product name, bundle id, repository, or app location.
- DEM color/contrast changes are outside this G0 audit plan; this plan measures packaging and isolation only.

---

## File map

- `packaging/audit_macos_bundle.py`: v2 budgets, stable comparison identities, data-manifest validation, root-cause summary, JSON/text output.
- `packaging/scienceearth/data_manifest.py`: canonical scientific-data manifest writer and validator shared by packaging and audit.
- `packaging/package_macos.sh`: write the manifest and plist manifest hash before signing.
- `tests/science_bundle_audit_tests.py`: address-stability, budget, invalid-manifest, and aggregation regressions.
- `tests/package_macos_tests.sh`: formal package manifest/hash contract.
- `docs/scienceearth/g0-v2-audit-policy.md`: approved v2 policy and exact non-waivable gates.
- `docs/scienceearth/g0-v2-audit-readiness.md`: measured v0.5/v0.6 evidence and remaining root causes.

---

### Task 1: Freeze the G0 v2 policy in executable tests

**Files:**
- Create: `docs/scienceearth/g0-v2-audit-policy.md`
- Modify: `tests/science_bundle_audit_tests.py`
- Modify: `packaging/audit_macos_bundle.py`

**Interfaces:**
- Consumes: immutable v0.2 reference/ratchet validation already performed by `MANIFEST.validate_chain()`.
- Produces: `evaluate_v2_size_gates(runtime_delta_bytes, science_data_bytes, total_delta_bytes, science_closure_bytes) -> dict`.

- [ ] **Step 1: Write failing budget-boundary tests**

Add table-driven assertions to `ScienceBundleAuditTests`:

```python
def test_v2_size_budgets_have_exact_pass_review_stop_boundaries(self):
    mib = 1024 * 1024
    cases = (
        ((40 * mib, 128 * mib, 160 * mib, 40 * mib), "PASS"),
        ((40 * mib + 1, 128 * mib, 160 * mib, 40 * mib), "REVIEW_REQUIRED"),
        ((60 * mib + 1, 1, 1, 1), "STOP"),
        ((1, 128 * mib + 1, 1, 1), "REVIEW_REQUIRED"),
        ((1, 160 * mib + 1, 1, 1), "STOP"),
        ((1, 1, 160 * mib + 1, 1), "REVIEW_REQUIRED"),
        ((1, 1, 192 * mib + 1, 1), "STOP"),
        ((1, 1, 1, 60 * mib + 1), "STOP"),
    )
    for values, expected in cases:
        with self.subTest(values=values):
            self.assertEqual(
                AUDIT.evaluate_v2_size_gates(*values)["status"], expected)
```

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_bundle_audit_tests.ScienceBundleAuditTests.\
test_v2_size_budgets_have_exact_pass_review_stop_boundaries -v
```

Expected: FAIL because `evaluate_v2_size_gates` does not exist.

- [ ] **Step 3: Implement the four independent gates**

Add constants and an evaluator to `audit_macos_bundle.py`:

```python
RUNTIME_TARGET_BYTES = 40 * MIB
RUNTIME_STOP_BYTES = 60 * MIB
SCIENCE_DATA_TARGET_BYTES = 128 * MIB
SCIENCE_DATA_STOP_BYTES = 160 * MIB
COMBINED_TARGET_BYTES = 160 * MIB
COMBINED_STOP_BYTES = 192 * MIB
SCIENCE_CLOSURE_TARGET_BYTES = 40 * MIB
SCIENCE_CLOSURE_STOP_BYTES = 60 * MIB

def classify_bounded_size(value, target, stop):
    if value <= target:
        return "PASS"
    if value <= stop:
        return "REVIEW_REQUIRED"
    return "STOP"

def evaluate_v2_size_gates(runtime_delta_bytes, science_data_bytes,
                           total_delta_bytes, science_closure_bytes):
    values = {
        "runtime_delta": (runtime_delta_bytes,
                          RUNTIME_TARGET_BYTES, RUNTIME_STOP_BYTES),
        "science_data": (science_data_bytes,
                         SCIENCE_DATA_TARGET_BYTES, SCIENCE_DATA_STOP_BYTES),
        "combined_delta": (total_delta_bytes,
                           COMBINED_TARGET_BYTES, COMBINED_STOP_BYTES),
        "science_closure": (science_closure_bytes,
                            SCIENCE_CLOSURE_TARGET_BYTES,
                            SCIENCE_CLOSURE_STOP_BYTES),
    }
    gates = {
        name: classify_bounded_size(value, target, stop)
        for name, (value, target, stop) in values.items()
    }
    status = ("STOP" if "STOP" in gates.values() else
              "REVIEW_REQUIRED" if "REVIEW_REQUIRED" in gates.values()
              else "PASS")
    return {
        "status": status,
        "exit_code": {"PASS": 0, "REVIEW_REQUIRED": 2, "STOP": 1}[status],
        "size_gates": gates,
        "values": {name: value for name, (value, _, _) in values.items()},
    }
```

- [ ] **Step 4: Document why only data receives the relaxed budget**

Write `g0-v2-audit-policy.md` with the exact thresholds from Global Constraints, the scientific-data validation rules, and this decision statement:

```text
G0 v2 does not raise the runtime or science-closure budget. It separates
scientific data from executable code only after exact path, size, SHA-256,
role, source-id, regular-file, non-symlink, non-Mach-O, and package-boundary
validation. Invalid or undeclared data remains in runtime delta.
```

- [ ] **Step 5: Run tests and commit**

Run the focused test, then:

```sh
git add packaging/audit_macos_bundle.py \
  tests/science_bundle_audit_tests.py \
  docs/scienceearth/g0-v2-audit-policy.md
git commit -m "test(scienceearth): define bounded G0 v2 size policy"
```

Expected: focused test PASS; commit contains no baseline JSON change.

---

### Task 2: Remove symbol-address false positives without changing history

**Files:**
- Modify: `packaging/audit_macos_bundle.py`
- Modify: `tests/science_bundle_audit_tests.py`

**Interfaces:**
- Consumes: stored v0.2 findings with their immutable schema-v1 identities.
- Produces: `comparison_identity(finding) -> str`, used only by `compare_identity_sets()`.

- [ ] **Step 1: Write the failing address-stability test**

```python
def test_static_symbol_comparison_ignores_address_but_not_symbol_or_owner(self):
    old = AUDIT.make_finding(
        "Contents/lib/libbase.dylib", "static_science_symbol",
        "000000000036c794 T _ZSTD_compressBound", [])
    relocated = AUDIT.make_finding(
        "Contents/lib/libbase.dylib", "static_science_symbol",
        "00000000003dc794 T _ZSTD_compressBound", [])
    changed = AUDIT.make_finding(
        "Contents/lib/libbase.dylib", "static_science_symbol",
        "00000000003dc794 T _ZSTD_decompressBound", [])
    self.assertTrue(AUDIT.compare_identity_sets([relocated], [old])["ok"])
    self.assertFalse(AUDIT.compare_identity_sets([changed], [old])["ok"])
```

- [ ] **Step 2: Verify RED**

Run the single test. Expected: FAIL because the two addresses currently produce different identities.

- [ ] **Step 3: Add comparison-only canonicalization**

```python
NM_SYMBOL_LINE = re.compile(
    r"^(?:[0-9A-Fa-f]+\s+)?(?P<kind>[A-Za-z?])\s+(?P<name>\S+)$")

def comparison_subject(finding):
    subject = finding["subject"]
    if finding["category"] != "static_science_symbol":
        return subject
    match = NM_SYMBOL_LINE.fullmatch(subject.strip())
    return (f"{match.group('kind')} {match.group('name')}"
            if match else subject)

def comparison_identity(finding):
    return finding_identity(
        finding["owner"], finding["category"], comparison_subject(finding))
```

Change `compare_identity_sets()` to index candidate and ceiling through `comparison_identity()` while returning the original full findings in JSON. Reject duplicate comparison identities with different owners/categories/names rather than silently overwriting them.

- [ ] **Step 4: Add regression coverage**

Add tests proving:

- a changed hex address passes;
- a changed symbol name stops;
- moving the same symbol to another Mach-O stops;
- `T` versus `t` remains distinct;
- immutable manifest validation still uses the original stored identity;
- path normalization and exact non-symbol finding comparison remain unchanged.

- [ ] **Step 5: Run the full audit unit suite and commit**

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
git add packaging/audit_macos_bundle.py tests/science_bundle_audit_tests.py
git commit -m "fix(scienceearth): stabilize G0 symbol comparison"
```

Expected: all tests PASS; approved reference/ratchet hashes remain unchanged.

---

### Task 3: Make scientific-data size classification cryptographically explicit

**Files:**
- Create: `packaging/scienceearth/data_manifest.py`
- Modify: `packaging/package_macos.sh`
- Modify: `packaging/audit_macos_bundle.py`
- Modify: `tests/package_macos_tests.sh`
- Modify: `tests/science_bundle_audit_tests.py`

**Interfaces:**
- Produces manifest schema `osgsol-science-data-v1`.
- Produces `load_science_data_manifest(app) -> {"bytes": int, "entries": list}`.
- Adds plist key `ScienceEarthDataManifestSha256`.

- [ ] **Step 1: Write failing manifest validation tests**

Use this exact valid document:

```json
{
  "entries": [
    {
      "bytes": 87183360,
      "path": "Contents/misc/science/alphaearth/alphaearth.sqlite",
      "role": "spatial-index",
      "sha256": "15875963d1bf4dd3f35a1f6c3ec6329378fef0fab6549fac677552f1d348f736",
      "source_id": "alphaearth-foundations"
    }
  ],
  "schema": "osgsol-science-data-v1"
}
```

Add negative tests for traversal, absolute paths, symlinks, Mach-O entries, missing files, incorrect byte count, incorrect hash, duplicate paths, unknown keys, files outside `Contents/misc/science/`, and a manifest not in canonical JSON order.

- [ ] **Step 2: Verify RED**

Run the two unit suites. Expected: FAIL because the data-manifest module and size classification do not exist.

- [ ] **Step 3: Implement the canonical writer/validator**

`data_manifest.py` must:

- reject booleans as byte counts;
- accept only lowercase 64-character SHA-256;
- resolve each entry beneath the app without following symlinks;
- hash files in bounded chunks;
- reject a file whose first four bytes match a supported Mach-O/fat magic;
- serialize with `json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n"`;
- expose a CLI `--write APP --alphaearth-index FILE` and a Python `validate(app)` function.

- [ ] **Step 4: Generate and bind the manifest during packaging**

After copying `alphaearth.sqlite`, run:

```sh
python3 "$REPO/packaging/scienceearth/data_manifest.py" \
  --write "$BUILD_APP" \
  --alphaearth-index \
  'Contents/misc/science/alphaearth/alphaearth.sqlite'
SCIENCE_DATA_MANIFEST_SHA256="$(shasum -a 256 \
  "$BUILD_APP/Contents/misc/science/data-manifest.json" | awk '{print $1}')"
```

Write `ScienceEarthDataManifestSha256` into `Info.plist`. Package tests must recompute the hash and validate the manifest before accepting the staging app.

- [ ] **Step 5: Integrate split accounting into the audit**

Compute:

```python
science_data = load_science_data_manifest(app)
science_data_bytes = science_data["bytes"]
runtime_delta_bytes = total_bytes - baseline_bytes - science_data_bytes
```

If validation fails, emit `invalid_science_data_manifest`, classify zero bytes as data, and keep all bytes in runtime delta. Include `science_data`, `runtime_delta`, and `combined_delta` in JSON and text output.

- [ ] **Step 6: Run packaging/audit tests and commit**

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
env -u EARTH_AI_KEY OSGSOL_PACKAGE_TEST_SKIP_RUNTIME_SMOKE=1 \
  bash tests/package_macos_tests.sh
git add packaging/scienceearth/data_manifest.py \
  packaging/audit_macos_bundle.py packaging/package_macos.sh \
  tests/science_bundle_audit_tests.py tests/package_macos_tests.sh
git commit -m "feat(scienceearth): audit verified data separately"
```

Expected: tests PASS without launching an app; no listener is created.

---

### Task 4: Collapse raw findings into honest root causes

**Files:**
- Modify: `packaging/audit_macos_bundle.py`
- Modify: `tests/science_bundle_audit_tests.py`

**Interfaces:**
- Produces `root_causes: list[dict]` while preserving `findings`, `violations`, and `delta` unchanged.

- [ ] **Step 1: Write a failing aggregation test**

Create a fixture with 100 GDAL symbols in main, 20 ZSTD symbols in ReaderWriter, one missing plugin, two forbidden prefixes, and one unresolved dependency. Assert that raw findings remain 124 and the root-cause ids are exactly:

```python
[
    "forbidden-compiled-paths",
    "missing-science-plugin",
    "science-static-linkage",
    "unresolved-dependencies",
]
```

- [ ] **Step 2: Implement stable groups**

Map categories as follows:

```python
ROOT_CAUSE_CATEGORIES = {
    "science-static-linkage": {
        "static_science_symbol", "static_science_string",
        "dynamic_science_dependency", "main_reaches_science_dependency"},
    "missing-science-plugin": {"missing_science_plugin"},
    "forbidden-compiled-paths": {
        "forbidden_runtime_reference", "forbidden_rpath", "forbidden_string",
        "external_dependency"},
    "unresolved-dependencies": {"unresolved_dependency"},
    "science-export-surface": {"exported_science_symbol"},
    "invalid-science-data": {"invalid_science_data_manifest"},
}
```

Each group records id, finding count, affected owners, and up to five representative subjects. Never discard or cap the raw JSON arrays.

- [ ] **Step 3: Make the text report lead with decisions**

Render sections in this order:

1. verdict and policy version;
2. runtime/data/combined/plugin size gates;
3. root causes;
4. unresolved dependency count;
5. raw Tier A/Tier B and historical evidence.

- [ ] **Step 4: Run tests and commit**

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_bundle_audit_tests -v
git add packaging/audit_macos_bundle.py tests/science_bundle_audit_tests.py
git commit -m "feat(scienceearth): summarize G0 audit root causes"
```

Expected: raw evidence is byte-for-byte present in JSON; summary counts match it.

---

### Task 5: Run the prepared G0 v2 audit without touching the Desktop app

**Files:**
- Create: `docs/scienceearth/g0-v2-audit-readiness.md`
- Generated only: `build/science_g0_v2_audit/*.json`
- Generated only: `build/science_g0_v2_audit/*.txt`

**Interfaces:**
- Consumes: immutable v0.2 backup, current v0.6 staging candidate, approved manifests.
- Produces: one reproducible measurement record and a root-cause handoff for the later isolation-fix plan.

- [ ] **Step 1: Prove the immutable baseline before auditing**

```sh
python3 - <<'PY'
import importlib.util, json
from pathlib import Path
root = Path('.').resolve()
spec = importlib.util.spec_from_file_location(
    'g0_manifest', root / 'packaging/scienceearth/g0_manifest.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
reference = json.loads((root / 'packaging/scienceearth/baselines/'
                        'v0.2.0-macos-arm64-reference.json').read_text())
baseline = root / 'build/desktop-backup-pre-scienceearth/osgSol Earth.app'
assert module.bundle_fingerprint(baseline) == reference['bundle_fingerprint']
print(reference['bundle_fingerprint'])
PY
```

Expected fingerprint: `91fa216f3beb528fa71594cb6a425a2f657e490b6d3442ef497753a7c7843e18`.

- [ ] **Step 2: Run only non-GUI tests**

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_g0_manifest_tests tests.science_bundle_audit_tests -v
ctest --test-dir build/science_g3_release \
  -L offline -LE network-local --output-on-failure
```

Expected: all selected tests PASS; no Desktop process, Dock activation, listener, or new matching `.ips` report.

- [ ] **Step 3: Audit the staging candidate**

```sh
mkdir -p build/science_g0_v2_audit
python3 packaging/audit_macos_bundle.py \
  --app 'build/science_g3_delivery/osgSol Earth.app' \
  --baseline 'build/desktop-backup-pre-scienceearth/osgSol Earth.app' \
  --reference-manifest \
    packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json \
  --ratchet-manifest \
    packaging/scienceearth/baselines/current-macos-arm64-ratchet.json \
  --source-root /Users/USER/osgverse \
  --source-root /Users/USER/osgsol/.worktrees/v0.2-runtime-safety \
  --json build/science_g0_v2_audit/v0.6.json \
  --text build/science_g0_v2_audit/v0.6.txt
```

Expected size result with the current candidate:

```text
runtime delta: 33,635,402 bytes (PASS)
verified science data: 87,183,360 bytes (PASS)
combined delta: 120,818,762 bytes (PASS)
```

The overall audit is still expected to STOP until the missing `osgdb_science.so`, static GDAL/PROJ/ZSTD linkage, `/usr/local/lib/gdalplugins`, and packaged fontconfig/gettext prefix findings are fixed. Size PASS must not convert those isolation failures to PASS.

- [ ] **Step 4: Record the exact readiness boundary**

Write `g0-v2-audit-readiness.md` with:

- source commit and candidate hashes;
- all four size measurements and gate results;
- raw finding counts and compact root causes;
- unresolved dependencies;
- immutable manifest hashes;
- exact commands and durations;
- confirmation that Desktop app and macOS settings were untouched;
- the next implementation order: single-export science plugin, GDAL fallback-path patch, relocatable fontconfig/gettext runtime, then final audit.

- [ ] **Step 5: Verify repository hygiene and commit**

```sh
git diff --check
git status --short
git add docs/scienceearth/g0-v2-audit-readiness.md
git commit -m "docs(scienceearth): record G0 v2 audit readiness"
```

Expected: only intended source/docs changes are tracked; generated audit output and existing `__pycache__` directories are not staged.

---

## Self-review result

- Spec coverage: size relaxation is bounded and separate; every non-size G0 gate remains mandatory; immutable reference/tag and macOS delivery rules are preserved.
- Placeholder scan: the plan contains no deferred thresholds, unknown paths, or unspecified test commands.
- Interface consistency: the same manifest path, schema, plist key, four size fields, policy thresholds, and root-cause ids are used in packaging, audit, tests, and documentation.
- Scope boundary: this plan prepares a reliable G0 v2 audit. It intentionally does not implement DEM visualization changes or the later science-plugin/font-runtime fixes that the prepared audit will measure.
