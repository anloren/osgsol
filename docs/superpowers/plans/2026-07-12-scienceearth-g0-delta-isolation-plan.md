# ScienceEarth G0 Delta-Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the impossible absolute all-bundle isolation gate with an immutable, identity-based baseline/ratchet audit while keeping the ScienceEarth closure absolutely private and clean.

**Architecture:** The bundle auditor first emits normalized structured findings, then applies two policies: absolute isolation for the science closure and candidate-subset-of-ratchet comparison for every non-science Mach-O. A separately guarded generator creates the immutable `v0.2.0` reference manifest and initial ratchet; dependency builds remove embedded workspace paths instead of hiding them in the delta.

**Tech Stack:** Python 3 stdlib, Bash, CMake, Apple `otool`/`nm`/`strings`/`codesign`, pinned GDAL 3.13.1, PROJ 9.8.1, ZSTD 1.5.7.

## Global Constraints

- Protected boundary: `ScienceEarth` / `v0.2.0` / `0e91c7c4b121d80b929d595ea711d3dd0833ee67` remains immutable.
- The main executable and every non-science library add zero new GDAL/PROJ/ZSTD or external-path finding identities.
- The science closure has no non-system dependency outside the app, no forbidden runtime/source/build path, no unresolved install name, and no exported dependency symbols.
- Existing OSG TIFF, terrain, 3D Tiles, camera, photo, media, and AI behavior remain authoritative in the protected science-off build.
- Added bundle size remains `<= 40 MiB`; `40-60 MiB` requires review; `> 60 MiB` is `STOP`.
- This plan does not relax the uncached RGB median `<= 3 s`, P95 `<= 8 s`, cache, memory, correctness, HTTP-range, or camera gates.
- No manifest is generated or advanced automatically during a normal audit.

---

## File Responsibility Map

- `packaging/audit_macos_bundle.py`: inspect bundles, normalize finding identities, apply Tier A/Tier B policies, and render JSON/text evidence.
- `packaging/scienceearth/g0_manifest.py`: manifest schema, hashing, validation, reference/ratchet subset rules.
- `packaging/scienceearth/generate_g0_reference.py`: explicit one-shot guarded reference/initial-ratchet generator.
- `packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json`: immutable normalized protected-bundle inventory.
- `packaging/scienceearth/baselines/current-macos-arm64-ratchet.json`: current non-science debt ceiling and parent hash.
- `packaging/science_deps/gdal-3.13.1-relocatable-static.patch`: omit absolute install fallbacks when resources are embedded.
- `packaging/science_deps/build_science_deps.sh`: apply the relocatable patch and stable file-prefix mapping, then verify no build-root strings remain.
- `tests/science_bundle_audit_tests.py`: auditor, CLI, baseline-delta, and real-Mach-O regression tests.
- `tests/science_g0_manifest_tests.py`: manifest schema, fingerprint, parent-chain, and overwrite-guard tests.
- `tests/science_deps_script_tests.sh`: pinned patch and path-scrubbing build-contract tests.
- `tests/scienceearth_release_tests.sh`: immutable-tag and committed-manifest presence/hash checks.
- `docs/scienceearth/g0-g1-baseline.md` and `docs/scienceearth/g0-measurements.md`: audited isolation result; latency remains a separate gate.

### Task 1: Introduce stable structured finding identities

**Files:**
- Modify: `packaging/audit_macos_bundle.py`
- Modify: `tests/science_bundle_audit_tests.py`

**Interfaces:**
- Produces: `make_finding(owner: str, category: str, subject: str, source_roots: list[Path]) -> dict`
- Produces: `finding_identity(owner: str, category: str, normalized_subject: str) -> str`
- Produces: `compare_identity_sets(candidate: list[dict], ceiling: list[dict]) -> dict`
- Consumed by: Tasks 2-5.

- [ ] **Step 1: Write failing identity and substitution tests**

Add these tests to `ScienceBundleAuditTests`:

```python
def test_finding_identity_is_stable_normalized_and_owner_sensitive(self):
    first = AUDIT.make_finding(
        "Contents/lib/libbase.dylib", "source_build_string",
        str(ROOT / "build" / "sdk_core" / "file.cpp"), [ROOT])
    same = AUDIT.make_finding(
        "Contents/lib/libbase.dylib", "source_build_string",
        str(ROOT / "build" / "sdk_core" / "file.cpp"), [ROOT])
    other_owner = AUDIT.make_finding(
        "Contents/MacOS/main", "source_build_string",
        str(ROOT / "build" / "sdk_core" / "file.cpp"), [ROOT])
    self.assertEqual(first["subject"], "${SOURCE_ROOT}/build/sdk_core/file.cpp")
    self.assertEqual(first["identity"], same["identity"])
    self.assertNotEqual(first["identity"], other_owner["identity"])

def test_identity_delta_rejects_replacement_at_equal_count(self):
    allowed = [AUDIT.make_finding("Contents/MacOS/main", "external_dependency",
                                  "/old/lib.dylib", [])]
    candidate = [AUDIT.make_finding("Contents/MacOS/main", "external_dependency",
                                    "/new/lib.dylib", [])]
    delta = AUDIT.compare_identity_sets(candidate, allowed)
    self.assertEqual(delta["new"], candidate)
    self.assertEqual(delta["removed"], allowed)
    self.assertFalse(delta["ok"])
```

- [ ] **Step 2: Run the focused tests and verify RED**

Run:

```bash
cd /Users/USER/osgsol/.worktrees/v0.2-runtime-safety
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_bundle_audit_tests.ScienceBundleAuditTests.test_finding_identity_is_stable_normalized_and_owner_sensitive \
  tests.science_bundle_audit_tests.ScienceBundleAuditTests.test_identity_delta_rejects_replacement_at_equal_count -v
```

Expected: both tests fail with `AttributeError` for missing structured-finding functions.

- [ ] **Step 3: Add normalization, identity, and set-comparison primitives**

Add `hashlib` to the imports and add the following before `CommandInspector`:

```python
FINDING_SCHEMA_VERSION = 1


def normalize_subject(subject, source_roots):
    value = str(subject)
    roots = sorted((str(Path(root).resolve()) for root in source_roots),
                   key=len, reverse=True)
    for root in roots:
        value = value.replace(root, "${SOURCE_ROOT}")
    return value


def finding_identity(owner, category, normalized_subject):
    payload = json.dumps({
        "owner": owner,
        "category": category,
        "subject": normalized_subject,
    }, sort_keys=True, separators=(",", ":")).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def make_finding(owner, category, subject, source_roots):
    normalized = normalize_subject(subject, source_roots)
    return {
        "schema_version": FINDING_SCHEMA_VERSION,
        "owner": owner,
        "category": category,
        "subject": normalized,
        "identity": finding_identity(owner, category, normalized),
    }


def compare_identity_sets(candidate, ceiling):
    candidate_by_id = {item["identity"]: item for item in candidate}
    ceiling_by_id = {item["identity"]: item for item in ceiling}
    new_ids = sorted(set(candidate_by_id) - set(ceiling_by_id))
    removed_ids = sorted(set(ceiling_by_id) - set(candidate_by_id))
    return {
        "ok": not new_ids,
        "new": [candidate_by_id[item] for item in new_ids],
        "removed": [ceiling_by_id[item] for item in removed_ids],
    }
```

Convert each current string violation site to append a `make_finding(...)` object first. Preserve the existing human message as `finding["message"]`, but exclude `message` from the identity hash. Use these exact categories:

```text
missing_science_plugin
forbidden_runtime_reference
dynamic_science_dependency
forbidden_rpath
forbidden_string
static_science_string
static_science_symbol
unresolved_dependency
external_dependency
main_reaches_science_dependency
```

- [ ] **Step 4: Run the identity tests and the complete auditor suite**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests.science_bundle_audit_tests -v
```

Expected: identity tests pass and all pre-existing 17 auditor/builder tests remain green after their assertions are updated to read `result["findings"]` and `finding["message"]`.

- [ ] **Step 5: Commit the structured inventory**

```bash
git add packaging/audit_macos_bundle.py tests/science_bundle_audit_tests.py
git commit -m "refactor(scienceearth): structure bundle audit findings"
```

### Task 2: Add immutable reference and chained ratchet manifests

**Files:**
- Create: `packaging/scienceearth/g0_manifest.py`
- Create: `packaging/scienceearth/generate_g0_reference.py`
- Create: `tests/science_g0_manifest_tests.py`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: Task 1 structured finding dictionaries.
- Produces: `bundle_fingerprint(app: Path) -> str`
- Produces: `build_reference(findings, source_commit, bundle_fingerprint, metadata) -> dict`
- Produces: `build_ratchet(reference, release_tag) -> dict`
- Produces: `advance_ratchet(ratchet, accepted_ids, release_tag) -> dict`
- Produces: `current_finding_ids(ratchet) -> list[str]`
- Produces: `validate_chain(reference, ratchet) -> None`
- Produces CLI: `generate_g0_reference.py --app APP --reference FILE --ratchet FILE --boundary-tag ScienceEarth --release-tag v0.2.0 --source-root PATH...`

- [ ] **Step 1: Write manifest schema, tamper, parent, and overwrite tests**

Create `tests/science_g0_manifest_tests.py` with tests that assert:

```python
class G0ManifestTests(unittest.TestCase):
    def test_reference_and_ratchet_chain(self):
        finding = AUDIT.make_finding("Contents/MacOS/main", "external_dependency",
                                     "/opt/example/lib.dylib", [])
        reference = MANIFEST.build_reference(
            [finding], "0e91c7c4b121d80b929d595ea711d3dd0833ee67",
            "a" * 64, {"architecture": "arm64"})
        ratchet = MANIFEST.build_ratchet(reference, "v0.2.0")
        MANIFEST.validate_chain(reference, ratchet)
        self.assertEqual(MANIFEST.current_finding_ids(ratchet),
                         [finding["identity"]])

    def test_ratchet_cannot_add_identity_or_change_parent(self):
        reference, ratchet = self.make_chain()
        ratchet["releases"][0]["finding_ids"].append("f" * 64)
        with self.assertRaisesRegex(ValueError, "not a subset"):
            MANIFEST.validate_chain(reference, ratchet)
        ratchet = MANIFEST.build_ratchet(reference, "v0.2.0")
        ratchet["reference_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "parent"):
            MANIFEST.validate_chain(reference, ratchet)

    def test_advanced_ratchet_cannot_restore_removed_identity(self):
        reference, ratchet = self.make_chain(two_findings=True)
        first = MANIFEST.current_finding_ids(ratchet)[0]
        ratchet = MANIFEST.advance_ratchet(ratchet, [first], "v0.2.1")
        with self.assertRaisesRegex(ValueError, "restore"):
            MANIFEST.advance_ratchet(
                ratchet, reference["finding_ids"], "v0.2.2")

    def test_generator_refuses_existing_output(self):
        reference = self.root / "reference.json"
        reference.write_text("do not overwrite\n")
        result = self.run_generator(reference)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(reference.read_text(), "do not overwrite\n")
```

- [ ] **Step 2: Run the tests and verify RED**

Run:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests.science_g0_manifest_tests -v
```

Expected: import failure because `packaging/scienceearth/g0_manifest.py` does not exist.

- [ ] **Step 3: Implement the manifest module**

Implement `g0_manifest.py` with `copy`, `hashlib`, and `json`; schema `1`; canonical JSON using `sort_keys=True` and compact separators for hashing; sorted unique finding IDs; exact 64-hex validation; immutable boundary commit validation; and these objects:

```python
BOUNDARY_COMMIT = "0e91c7c4b121d80b929d595ea711d3dd0833ee67"
MANIFEST_SCHEMA_VERSION = 1


def canonical_bytes(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def manifest_sha256(value):
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def build_reference(findings, source_commit, fingerprint, metadata):
    if source_commit != BOUNDARY_COMMIT:
        raise ValueError("reference source commit is not the immutable boundary")
    items = sorted(findings, key=lambda item: item["identity"])
    return {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "kind": "reference",
        "source_commit": source_commit,
        "bundle_fingerprint": fingerprint,
        "metadata": dict(sorted(metadata.items())),
        "findings": items,
        "finding_ids": [item["identity"] for item in items],
    }


def build_ratchet(reference, release_tag):
    validate_reference(reference)
    finding_ids = list(reference["finding_ids"])
    return {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "kind": "ratchet",
        "boundary_commit": BOUNDARY_COMMIT,
        "reference_sha256": manifest_sha256(reference),
        "releases": [{
            "release_tag": release_tag,
            "parent_finding_ids_sha256": manifest_sha256(finding_ids),
            "finding_ids": finding_ids,
        }],
    }


def current_finding_ids(ratchet):
    releases = ratchet.get("releases", [])
    if not releases:
        raise ValueError("ratchet has no releases")
    return list(releases[-1]["finding_ids"])


def advance_ratchet(ratchet, accepted_ids, release_tag):
    value = copy.deepcopy(ratchet)
    previous = current_finding_ids(value)
    accepted = sorted(set(accepted_ids))
    if not set(accepted).issubset(previous):
        raise ValueError("ratchet cannot restore a removed finding")
    value["releases"].append({
        "release_tag": release_tag,
        "parent_finding_ids_sha256": manifest_sha256(previous),
        "finding_ids": accepted,
    })
    return value


def validate_chain(reference, ratchet):
    validate_reference(reference)
    if ratchet.get("reference_sha256") != manifest_sha256(reference):
        raise ValueError("ratchet parent does not match reference")
    previous = set(reference["finding_ids"])
    previous_ids = list(reference["finding_ids"])
    for release in ratchet.get("releases", []):
        if release.get("parent_finding_ids_sha256") != manifest_sha256(previous_ids):
            raise ValueError("ratchet release parent does not match")
        current_ids = sorted(set(release.get("finding_ids", [])))
        if not set(current_ids).issubset(previous):
            raise ValueError("ratchet finding set is not a subset of its parent")
        previous = set(current_ids)
        previous_ids = current_ids
```

`bundle_fingerprint()` hashes relative path, mode, symlink target, size, and file SHA-256 for every bundle entry in sorted order. It never includes the absolute app path.

- [ ] **Step 4: Implement the explicit guarded generator**

`generate_g0_reference.py` must:

1. verify `git rev-parse 'ScienceEarth^{}'` and `git rev-parse 'v0.2.0^{}'` both equal `BOUNDARY_COMMIT`;
2. refuse if either output exists;
3. inspect the baseline with `require_science_plugin=False`;
4. include only non-science finding identities;
5. write to mode-`0600` temporary files in each destination directory;
6. validate the pair after writing temporary files;
7. atomically rename them and chmod `0644`.

Use this atomic writer:

```python
def write_new_json(path, value):
    path = Path(path)
    if path.exists():
        raise FileExistsError(f"refusing to overwrite manifest: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
        os.chmod(temporary, 0o644)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)
```

- [ ] **Step 5: Register and run the manifest tests**

Add `osgSol_Test_ScienceG0Manifest` beside the existing Python science tests in `tests/CMakeLists.txt` and run:

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest tests.science_g0_manifest_tests -v
cmake --build build/science_g0 --target osgVerse_Test_ScienceBuildContract --parallel 8
ctest --test-dir build/science_g0 -R 'ScienceG0Manifest|ScienceBuildContract' --output-on-failure
```

Expected: manifest tests and build-contract test pass.

- [ ] **Step 6: Commit manifest tooling**

```bash
git add packaging/scienceearth/g0_manifest.py \
  packaging/scienceearth/generate_g0_reference.py \
  tests/science_g0_manifest_tests.py tests/CMakeLists.txt
git commit -m "feat(scienceearth): add immutable G0 audit manifests"
```

### Task 3: Apply Tier A absolute and Tier B ratchet policies

**Files:**
- Modify: `packaging/audit_macos_bundle.py`
- Modify: `tests/science_bundle_audit_tests.py`

**Interfaces:**
- Consumes: Task 2 `reference` and `ratchet` dictionaries.
- Extends: `audit_bundle(..., reference_manifest, ratchet_manifest) -> dict`.
- CLI requires: `--reference-manifest FILE --ratchet-manifest FILE` for policy-bearing audits.

- [ ] **Step 1: Write failing policy tests**

Add tests for all of these cases:

```python
def test_baseline_debt_passes_but_new_non_science_identity_stops(self):
    baseline_finding = AUDIT.make_finding(
        "Contents/lib/libbase.dylib", "static_science_symbol", "_ZSTD_decompress", [])
    reference, ratchet = self.manifest_chain([baseline_finding])
    result = audit(probe.app, self.baseline.app, inspector,
                   reference_manifest=reference, ratchet_manifest=ratchet)
    self.assertEqual(result["delta"]["new"], [])
    inspector._symbols["libbase.dylib"].append("_ZSTD_compress")
    result = audit(probe.app, self.baseline.app, inspector,
                   reference_manifest=reference, ratchet_manifest=ratchet)
    self.assertEqual(result["status"], "STOP")
    self.assertEqual(len(result["delta"]["new"]), 1)

def test_science_external_lookup_stops_even_if_reference_contains_same_text(self):
    inspector = self.valid_inspector()
    inspector._rpaths["osgdb_science.so"] = ["/opt/homebrew/lib"]
    reference, ratchet = self.manifest_chain([])
    result = audit(probe.app, self.baseline.app, inspector,
                   reference_manifest=reference, ratchet_manifest=ratchet)
    self.assertEqual(result["tier_a"]["status"], "STOP")

def test_removed_debt_is_reported_and_cannot_be_substituted(self):
    old = AUDIT.make_finding("Contents/lib/libbase.dylib",
                             "static_science_symbol", "_ZSTD_decompress", [])
    reference, ratchet = self.manifest_chain([old])
    result = audit(probe.app, self.baseline.app, self.valid_inspector(),
                   reference_manifest=reference, ratchet_manifest=ratchet)
    self.assertEqual(result["delta"]["removed"], [old])
```

- [ ] **Step 2: Run policy tests and verify RED**

Expected: calls fail because `audit_bundle` does not accept manifests or emit `tier_a`/`delta`.

- [ ] **Step 3: Implement ownership and policy evaluation**

After graph construction, calculate `science_reachable`; classify a finding as science-owned when its `owner` is in that set. Implement:

```python
def evaluate_isolation(findings, science_nodes, reference, ratchet_ids):
    science = [item for item in findings if item["owner"] in science_nodes]
    non_science = [item for item in findings if item["owner"] not in science_nodes]
    tier_a_categories = {
        "forbidden_runtime_reference", "forbidden_rpath", "forbidden_string",
        "unresolved_dependency", "external_dependency",
        "dynamic_science_dependency", "main_reaches_science_dependency",
    }
    tier_a_failures = [item for item in science
                       if item["category"] in tier_a_categories]
    reference_by_id = {item["identity"]: item for item in reference["findings"]}
    ceiling = [reference_by_id[item] for item in sorted(ratchet_ids)]
    delta = compare_identity_sets(non_science, ceiling)
    return {
        "tier_a": {"status": "PASS" if not tier_a_failures else "STOP",
                   "violations": tier_a_failures},
        "tier_b": {"status": "PASS" if delta["ok"] else "STOP"},
        "delta": delta,
        "absolute": {"science": science, "non_science": non_science},
    }
```

When comparing IDs, use the ratchet's full identity-to-finding map resolved from the reference so removed findings remain human-readable. Overall status is `STOP` if Tier A or Tier B stops, otherwise it follows the existing size gate.

- [ ] **Step 4: Extend reports and CLI**

JSON schema becomes `3` and includes `manifests`, `tier_a`, `tier_b`, `absolute`, and `delta`. Text output adds these exact sections before `Violations`:

```text
Isolation manifests
Tier A science closure
Tier B non-science delta
Historical absolute debt
Removed debt
New findings
```

The CLI loads and validates both manifests before calling `audit_bundle`. A missing, malformed, parent-mismatched, bundle-fingerprint-mismatched, or schema-mismatched manifest exits `1` and still writes a JSON error report; it never generates a replacement.

- [ ] **Step 5: Run all audit and CLI tests**

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_bundle_audit_tests tests.science_g0_manifest_tests -v
```

Expected: all tests pass; equal-count substitution, moved-owner debt, science external lookup, missing manifests, and tampered parent all stop.

- [ ] **Step 6: Commit policy enforcement**

```bash
git add packaging/audit_macos_bundle.py tests/science_bundle_audit_tests.py
git commit -m "feat(scienceearth): enforce G0 delta isolation policy"
```

### Task 4: Remove private dependency build-root strings

**Files:**
- Create: `packaging/science_deps/gdal-3.13.1-relocatable-static.patch`
- Modify: `packaging/science_deps/versions.env`
- Modify: `packaging/science_deps/build_science_deps.sh`
- Modify: `tests/science_deps_script_tests.sh`

**Interfaces:**
- Produces: rebuilt `build/science-deps/prefix` whose linked probe contains no workspace path.
- Preserves: embedded GDAL and PROJ resources and exact pinned capabilities.

- [ ] **Step 1: Write failing patch/path-map contract checks**

Extend `science_deps_script_tests.sh` with:

```bash
relocatable_patch="$repo_root/packaging/science_deps/gdal-3.13.1-relocatable-static.patch"
[[ -f "$relocatable_patch" ]] || fail "missing pinned relocatable GDAL patch"
[[ ${GDAL_RELOCATABLE_PATCH_SHA256:-} =~ ^[0-9a-f]{64}$ ]] ||
    fail "GDAL_RELOCATABLE_PATCH_SHA256 must contain SHA-256"
assert_contains "$builder" 'ffile-prefix-map' \
    "dependency build must normalize compiled source paths"
assert_contains "$builder" 'verify_no_workspace_strings' \
    "verify must scan linked runtime artifacts for workspace strings"
assert_contains "$builder" 'gdal-3.13.1-relocatable-static.patch' \
    "builder must apply the pinned relocatable GDAL patch"
```

- [ ] **Step 2: Run dependency contract and verify RED**

Run `bash tests/science_deps_script_tests.sh`.

Expected: failure at `missing pinned relocatable GDAL patch`.

- [ ] **Step 3: Add the relocatable embedded-resource patch**

The patch must wrap both GDAL absolute install-path definitions:

```diff
--- a/port/CMakeLists.txt
+++ b/port/CMakeLists.txt
@@
-file(TO_CMAKE_PATH "${CMAKE_INSTALL_PREFIX}/${GDAL_RESOURCE_PATH}" INST_DATA_PATH)
-target_compile_definitions(cpl PRIVATE INST_DATA="${INST_DATA_PATH}")
-if(CMAKE_INSTALL_FULL_SYSCONFDIR)
-    target_compile_definitions(cpl PRIVATE SYSCONFDIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}")
-endif()
+if(NOT USE_ONLY_EMBEDDED_RESOURCE_FILES)
+    file(TO_CMAKE_PATH "${CMAKE_INSTALL_PREFIX}/${GDAL_RESOURCE_PATH}" INST_DATA_PATH)
+    target_compile_definitions(cpl PRIVATE INST_DATA="${INST_DATA_PATH}")
+    if(CMAKE_INSTALL_FULL_SYSCONFDIR)
+        target_compile_definitions(cpl PRIVATE SYSCONFDIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}")
+    endif()
+endif()
--- a/gcore/CMakeLists.txt
+++ b/gcore/CMakeLists.txt
@@
-file(TO_CMAKE_PATH "${CMAKE_INSTALL_PREFIX}/${GDAL_RESOURCE_PATH}" INST_DATA_PATH)
-set_property(SOURCE gdaldrivermanager.cpp APPEND PROPERTY COMPILE_DEFINITIONS
-  INST_DATA="${INST_DATA_PATH}" INSTALL_PLUGIN_FULL_DIR="${INSTALL_PLUGIN_FULL_DIR}")
+if(NOT USE_ONLY_EMBEDDED_RESOURCE_FILES)
+  file(TO_CMAKE_PATH "${CMAKE_INSTALL_PREFIX}/${GDAL_RESOURCE_PATH}" INST_DATA_PATH)
+  set_property(SOURCE gdaldrivermanager.cpp APPEND PROPERTY COMPILE_DEFINITIONS
+    INST_DATA="${INST_DATA_PATH}" INSTALL_PLUGIN_FULL_DIR="${INSTALL_PLUGIN_FULL_DIR}")
+endif()
```

Compute its SHA-256 and store it as `GDAL_RELOCATABLE_PATCH_SHA256` in `versions.env`. `verify_pin_contract` verifies both GDAL patch hashes, and `extract_all` applies both with `patch --batch --forward -p1`.

- [ ] **Step 4: Add stable source-prefix maps and artifact scanning**

Add to `common_cmake_args`:

```bash
source_map="-ffile-prefix-map=${science_root}=ScienceEarthDeps"
common_cmake_args+=(
    "-DCMAKE_C_FLAGS=${source_map}"
    "-DCMAKE_CXX_FLAGS=${source_map}"
)
```

Add and call this verification after building the runtime probe:

```bash
verify_no_workspace_strings()
{
    local artifact
    for artifact in \
        "$build_dir/runtime-probe/science_deps_runtime_probe" \
        "$prefix/lib/libgdal.a" "$prefix/lib/libproj.a" "$prefix/lib/libzstd.a"; do
        if strings -a "$artifact" | grep -F "$science_root"; then
            die "workspace path leaked into runtime artifact: $artifact"
        fi
    done
}
```

- [ ] **Step 5: Run contract, rebuild, and verify the private prefix**

```bash
bash tests/science_deps_script_tests.sh
packaging/science_deps/build_science_deps.sh --build --jobs 8
packaging/science_deps/build_science_deps.sh --verify
cmake --build build/science_g0 --target osgdb_science_g0_probe --parallel 8
```

Expected: all commands pass; `strings -a build/science_g0/lib/osgdb_science_g0_probe.so | rg '/Users/USER/osgsol/.worktrees|build/science-deps'` returns no matches; `nm -gU` still exposes only `_osgsol_science_g0_probe_anchor`.

- [ ] **Step 6: Commit the relocatable private build**

```bash
git add packaging/science_deps/gdal-3.13.1-relocatable-static.patch \
  packaging/science_deps/versions.env \
  packaging/science_deps/build_science_deps.sh \
  tests/science_deps_script_tests.sh
git commit -m "fix(scienceearth): remove private dependency build paths"
```

### Task 5: Generate the protected manifests and record isolation PASS

**Files:**
- Create: `packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json`
- Create: `packaging/scienceearth/baselines/current-macos-arm64-ratchet.json`
- Modify: `tests/scienceearth_release_tests.sh`
- Modify: `packaging/build_science_g0_probe.sh`
- Modify: `docs/scienceearth/g0-g1-baseline.md`
- Modify: `docs/scienceearth/g0-measurements.md`

**Interfaces:**
- Consumes: Tasks 1-4.
- Produces: signed disposable candidate, JSON/text audit, zero Tier A/Tier B delta result.
- Leaves: overall G0 `STOP` until the separate latency plan passes and human sign-off is recorded.

- [ ] **Step 1: Add failing release-contract checks for committed manifests**

In `scienceearth_release_tests.sh`, require both files, validate them with `g0_manifest.py`, and assert the reference source commit equals the immutable tag. Run the script and expect failure because the manifests are absent.

- [ ] **Step 2: Generate the one-time immutable reference and ratchet**

```bash
cd /Users/USER/osgsol/.worktrees/v0.2-runtime-safety
python3 packaging/scienceearth/generate_g0_reference.py \
  --app '/Users/USER/Desktop/osgSol Earth.app' \
  --reference packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json \
  --ratchet packaging/scienceearth/baselines/current-macos-arm64-ratchet.json \
  --boundary-tag ScienceEarth \
  --release-tag v0.2.0 \
  --source-root /Users/USER/osgverse
```

Expected: generator reports the immutable commit, bundle fingerprint, reference finding count, and matching initial ratchet count; neither JSON contains an absolute `/Users/USER` path.

- [ ] **Step 3: Rebuild the disposable probe without touching the baseline**

```bash
before=$(find '/Users/USER/Desktop/osgSol Earth.app' -type f -exec shasum -a 256 {} + | shasum -a 256)
SCIENCE_G0_BASELINE_APP='/Users/USER/Desktop/osgSol Earth.app' \
  packaging/build_science_g0_probe.sh
after=$(find '/Users/USER/Desktop/osgSol Earth.app' -type f -exec shasum -a 256 {} + | shasum -a 256)
test "$before" = "$after"
```

Expected: candidate is rebuilt and ad-hoc signed; protected baseline digest is unchanged.

- [ ] **Step 4: Run the policy-bearing audit**

```bash
python3 packaging/audit_macos_bundle.py \
  --app 'build/science_g0/osgSol Science G0 Probe.app' \
  --baseline '/Users/USER/Desktop/osgSol Earth.app' \
  --reference-manifest packaging/scienceearth/baselines/v0.2.0-macos-arm64-reference.json \
  --ratchet-manifest packaging/scienceearth/baselines/current-macos-arm64-ratchet.json \
  --source-root /Users/USER/osgverse \
  --source-root /Users/USER/osgsol/.worktrees/v0.2-runtime-safety \
  --json build/science_g0/bundle-audit.json \
  --text build/science_g0/bundle-audit.txt
```

Expected: exit `0`; Tier A `PASS`; Tier B `PASS`; new non-science findings `0`; science external/source/build/unresolved findings `0`; historical debt remains visible; size remains below 40 MiB.

- [ ] **Step 5: Run the isolation regression set**

```bash
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest \
  tests.science_bundle_audit_tests tests.science_g0_manifest_tests -v
bash tests/science_deps_script_tests.sh
bash tests/scienceearth_release_tests.sh
packaging/science_deps/build_science_deps.sh --verify
/usr/bin/codesign --verify --deep --strict \
  'build/science_g0/osgSol Science G0 Probe.app'
ctest --test-dir build/science_test \
  -R '^(osgVerse_Test_Ai_Chat|osgVerse_Test_Feeds|osgVerse_Test_TileOverlay|osgVerse_Test_TerrainGrid|osgVerse_Test_Tiles3dPaging|osgVerse_Test_Satellite|osgVerse_Test_Geospatial|osgVerse_Test_EarthManipulator|osgVerse_Test_Ais|osgVerse_Test_WorldTools|osgVerse_Test_ImGuiSettings|osgVerse_Test_McpSafety|osgVerse_Test_ImGuiThreading|osgVerse_Test_MediaThreading|osgVerse_Test_ScienceBuildContract)$' \
  --output-on-failure
```

Expected: all tests and signature checks pass; protected science-off selection remains 15/15.

- [ ] **Step 6: Record only the isolation decision**

Update the two G0 documents with manifest hashes, absolute historical counts, Tier A zero findings, Tier B zero new identities, removed identities, sizes, commands, and test totals. Keep `G0_DECISION=STOP` and state that only the independent latency gate remains failed.

- [ ] **Step 7: Commit the manifests and isolation evidence**

```bash
git add packaging/scienceearth/baselines \
  tests/scienceearth_release_tests.sh packaging/build_science_g0_probe.sh \
  docs/scienceearth/g0-g1-baseline.md docs/scienceearth/g0-measurements.md
git commit -m "test(scienceearth): prove G0 baseline delta isolation"
```

## Isolation Plan Completion Gate

Do not begin G1. Proceed to the separate first-RGB latency plan only after Task 5 reports Tier A
and Tier B `PASS`. If manifest provenance, clean private closure, or zero new identity cannot be
proved, leave `G0_DECISION=STOP` and report the exact failing identity; never regenerate the
reference or loosen the ratchet to make a candidate pass.
