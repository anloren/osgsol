import hashlib
import importlib.util
import json
import os
import plistlib
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUDIT_PATH = ROOT / "packaging" / "audit_macos_bundle.py"
MANIFEST_PATH = ROOT / "packaging" / "scienceearth" / "g0_manifest.py"
BUILDER_PATH = ROOT / "packaging" / "build_science_g0_probe.sh"
SPEC = importlib.util.spec_from_file_location("audit_macos_bundle", AUDIT_PATH)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
MANIFEST_SPEC = importlib.util.spec_from_file_location("g0_manifest", MANIFEST_PATH)
MANIFEST = importlib.util.module_from_spec(MANIFEST_SPEC)
MANIFEST_SPEC.loader.exec_module(MANIFEST)


class FakeInspector:
    def __init__(self, dependencies, rpaths=None, strings=None, symbols=None):
        self._dependencies = dependencies
        self._rpaths = rpaths or {}
        self._strings = strings or {}
        self._symbols = symbols or {}

    def is_macho(self, path):
        return path.name in self._dependencies

    def dependencies(self, path):
        return self._dependencies[path.name]

    def rpaths(self, path):
        return self._rpaths.get(path.name, [])

    def string_references(self, path):
        return self._strings.get(path.name, [])

    def symbols(self, path):
        return self._symbols.get(path.name, [])


class BundleFixture:
    def __init__(self, root, name="Probe.app"):
        self.app = Path(root) / name
        self.executable = self.add("Contents/MacOS/main", 100)
        self.library = self.add("Contents/lib/libbase.dylib", 80)
        self.plugin = self.add(
            "Contents/lib/osgPlugins-3.6.5/osgdb_science.so", 60)

    def add(self, relative, size):
        path = self.app / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"x" * size)
        return path


def audit(app, baseline, inspector, **kwargs):
    return AUDIT.audit_bundle(
        app=app,
        baseline=baseline,
        inspector=inspector,
        source_roots=[ROOT],
        main_relative="Contents/MacOS/main",
        science_plugin_name="osgdb_science.so",
        **kwargs,
    )


def compile_macho(path, source, bundle=False):
    source_path = Path(path).with_suffix(".c")
    source_path.parent.mkdir(parents=True, exist_ok=True)
    source_path.write_text(source)
    command = ["/usr/bin/clang", str(source_path), "-o", str(path)]
    if bundle:
        command[1:1] = ["-bundle", "-undefined", "dynamic_lookup"]
    subprocess.run(command, check=True, stdout=subprocess.PIPE,
                   stderr=subprocess.PIPE)
    source_path.unlink()


def write_app_plist(app, executable="main"):
    contents = Path(app) / "Contents"
    contents.mkdir(parents=True, exist_ok=True)
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump({
            "CFBundleExecutable": executable,
            "CFBundleIdentifier": "org.osgsol.science-g0-test",
            "CFBundlePackageType": "APPL",
        }, stream)


class ScienceBundleAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="osgsol-bundle-audit-")
        self.root = Path(self.temporary.name)
        self.baseline = BundleFixture(self.root, "Baseline.app")
        self.baseline.plugin.unlink()

    def tearDown(self):
        self.temporary.cleanup()

    def valid_inspector(self):
        return FakeInspector(
            {
                "main": ["@rpath/libbase.dylib", "/usr/lib/libSystem.B.dylib"],
                "libbase.dylib": ["/usr/lib/libSystem.B.dylib"],
                "osgdb_science.so": [
                    "@loader_path/../libbase.dylib",
                    "/usr/lib/libSystem.B.dylib",
                ],
            },
            {
                "main": ["@executable_path/../lib"],
                "osgdb_science.so": ["@loader_path/.."],
            },
            symbols={"libbase.dylib": []},
        )

    def manifest_chain(self, findings, baseline=None):
        baseline = baseline or self.baseline.app
        reference = MANIFEST.build_reference(
            findings,
            MANIFEST.BOUNDARY_COMMIT,
            MANIFEST.bundle_fingerprint(baseline),
            {"architecture": "test"},
        )
        return reference, MANIFEST.build_ratchet(reference, "v0.2.0")

    def write_manifest_chain(self, baseline, findings=None):
        reference, ratchet = self.manifest_chain(findings or [], baseline)
        reference_path = self.root / "reference.json"
        ratchet_path = self.root / "ratchet.json"
        reference_path.write_text(json.dumps(reference))
        ratchet_path.write_text(json.dumps(ratchet))
        return reference_path, ratchet_path

    def test_baseline_debt_passes_but_new_non_science_identity_stops(self):
        probe = BundleFixture(self.root)
        inspector = self.valid_inspector()
        inspector._symbols["libbase.dylib"] = ["_ZSTD_decompress"]
        baseline_finding = AUDIT.make_finding(
            "Contents/lib/libbase.dylib", "static_science_symbol",
            "_ZSTD_decompress", [])
        reference, ratchet = self.manifest_chain([baseline_finding])
        result = audit(
            probe.app, self.baseline.app, inspector,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["schema_version"], 3)
        self.assertTrue(all(
            key in result for key in
            ("manifests", "tier_a", "tier_b", "absolute", "delta")))
        self.assertEqual(result["delta"]["new"], [])
        inspector._symbols["libbase.dylib"].append("_ZSTD_compress")
        result = audit(
            probe.app, self.baseline.app, inspector,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["status"], "STOP")
        self.assertEqual(len(result["delta"]["new"]), 1)

    def test_science_external_lookup_stops_even_if_reference_contains_same_text(self):
        probe = BundleFixture(self.root)
        inspector = self.valid_inspector()
        inspector._rpaths["osgdb_science.so"] = ["/opt/homebrew/lib"]
        historical = AUDIT.make_finding(
            "Contents/lib/libbase.dylib", "forbidden_rpath",
            "/opt/homebrew/lib", [])
        reference, ratchet = self.manifest_chain([historical])
        result = audit(
            probe.app, self.baseline.app, inspector,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["tier_a"]["status"], "STOP")

    def test_removed_debt_is_reported_and_cannot_be_substituted(self):
        probe = BundleFixture(self.root)
        old = AUDIT.make_finding(
            "Contents/lib/libbase.dylib", "static_science_symbol",
            "_ZSTD_decompress", [])
        reference, ratchet = self.manifest_chain([old])
        result = audit(
            probe.app, self.baseline.app, self.valid_inspector(),
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["delta"]["removed"], [old])

        inspector = self.valid_inspector()
        inspector._symbols["main"] = ["_ZSTD_decompress"]
        result = audit(
            probe.app, self.baseline.app, inspector,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["status"], "STOP")
        self.assertEqual(result["delta"]["removed"], [old])
        self.assertEqual(len(result["delta"]["new"]), 1)

    def test_equal_count_replacement_stops_tier_b(self):
        probe = BundleFixture(self.root)
        old = AUDIT.make_finding(
            "Contents/lib/libbase.dylib", "static_science_symbol",
            "_ZSTD_decompress", [])
        reference, ratchet = self.manifest_chain([old])
        inspector = self.valid_inspector()
        inspector._symbols["libbase.dylib"] = ["_ZSTD_compress"]
        result = audit(
            probe.app, self.baseline.app, inspector,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["tier_b"]["status"], "STOP")
        self.assertEqual(len(result["delta"]["new"]), 1)
        self.assertEqual(result["delta"]["removed"], [old])

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

    def test_recursively_audits_main_libraries_and_unreferenced_plugins(self):
        probe = BundleFixture(self.root)
        result = audit(probe.app, self.baseline.app, self.valid_inspector())
        self.assertTrue(result["ok"], result["findings"])
        self.assertEqual(
            set(result["graph"]),
            {
                "Contents/MacOS/main",
                "Contents/lib/libbase.dylib",
                "Contents/lib/osgPlugins-3.6.5/osgdb_science.so",
            },
        )
        self.assertEqual(
            result["science_only_closure"],
            ["Contents/lib/osgPlugins-3.6.5/osgdb_science.so"],
        )
        self.assertEqual(result["sizes"]["baseline_bytes"], 180)
        self.assertEqual(result["sizes"]["total_bytes"], 240)
        self.assertEqual(result["sizes"]["delta_bytes"], 60)
        self.assertIn("Dependency graph", AUDIT.render_text(result))

    def test_rejects_unresolved_loader_reference(self):
        probe = BundleFixture(self.root)
        inspector = self.valid_inspector()
        inspector._dependencies["main"] = ["@rpath/libmissing.dylib"]
        result = audit(probe.app, self.baseline.app, inspector)
        self.assertFalse(result["ok"])
        self.assertTrue(any(
            "unresolved" in item["message"] for item in result["findings"]))

    def test_rpath_uses_first_existing_candidate_even_when_external(self):
        probe = BundleFixture(self.root)
        external = self.root / "build" / "sdk_core" / "lib"
        external.mkdir(parents=True)
        (external / "libbase.dylib").write_bytes(b"source-copy")
        inspector = self.valid_inspector()
        inspector._rpaths["main"] = [
            str(external),
            "@executable_path/../lib",
        ]
        result = audit(probe.app, self.baseline.app, inspector)
        edge = result["graph"]["Contents/MacOS/main"][0]
        self.assertEqual(edge["external"], str(
            (external / "libbase.dylib").resolve()))
        self.assertIsNone(edge["resolved"])
        self.assertEqual(result["unresolved"], [])

    def test_loader_chain_propagates_nested_runpath_stack(self):
        probe = BundleFixture(self.root)
        plugin = probe.add("Contents/PlugIns/Nested/osgdb_nested.so", 20)
        middle = probe.add("Contents/Runtime/libmiddle.dylib", 20)
        leaf = probe.add("Contents/Runtime/libleaf.dylib", 20)
        inspector = self.valid_inspector()
        inspector._dependencies["main"] = ["@rpath/osgdb_nested.so"]
        inspector._rpaths["main"] = ["@executable_path/../PlugIns/Nested"]
        inspector._dependencies[plugin.name] = ["@rpath/libmiddle.dylib"]
        inspector._rpaths[plugin.name] = ["@loader_path/../../Runtime"]
        inspector._dependencies[middle.name] = ["@rpath/libleaf.dylib"]
        inspector._dependencies[leaf.name] = ["/usr/lib/libSystem.B.dylib"]
        result = audit(probe.app, self.baseline.app, inspector)
        middle_edge = result["graph"]["Contents/Runtime/libmiddle.dylib"][0]
        self.assertEqual(middle_edge["resolved"],
                         "Contents/Runtime/libleaf.dylib")

    def test_unreferenced_plugin_root_inherits_main_runpath_stack(self):
        probe = BundleFixture(self.root)
        plugin = probe.add("Contents/PlugIns/osgdb_runtime_loaded.so", 20)
        inspector = self.valid_inspector()
        inspector._dependencies[plugin.name] = ["@rpath/libbase.dylib"]
        result = audit(probe.app, self.baseline.app, inspector)
        edge = result["graph"]["Contents/PlugIns/osgdb_runtime_loaded.so"][0]
        self.assertEqual(edge["resolved"], "Contents/lib/libbase.dylib")

    def test_rejects_forbidden_runtime_and_source_build_references(self):
        probe = BundleFixture(self.root)
        inspector = self.valid_inspector()
        inspector._dependencies["main"].append(
            "/opt/homebrew/lib/libforbidden.dylib")
        inspector._strings["libbase.dylib"] = [
            str(ROOT / "build" / "science-deps" / "src" / "gdal.cpp"),
            "/usr/local/lib/libalso-forbidden.dylib",
        ]
        result = audit(probe.app, self.baseline.app, inspector)
        self.assertFalse(result["ok"])
        report = "\n".join(item["message"] for item in result["findings"])
        self.assertIn("/opt/homebrew", report)
        self.assertIn("/usr/local", report)
        self.assertIn("source/build", report)

    def test_rejects_science_dependencies_reachable_from_main(self):
        probe = BundleFixture(self.root)
        gdal = probe.add("Contents/lib/libgdal.37.dylib", 20)
        inspector = self.valid_inspector()
        inspector._dependencies["main"].append("@rpath/libgdal.37.dylib")
        inspector._dependencies[gdal.name] = ["/usr/lib/libSystem.B.dylib"]
        result = audit(probe.app, self.baseline.app, inspector)
        self.assertFalse(result["ok"])
        self.assertTrue(any("reachable from main" in item["message"]
                            for item in result["findings"]))

    def test_rejects_dynamic_science_dependency_in_unreferenced_plugin(self):
        probe = BundleFixture(self.root)
        plugin = probe.add("Contents/PlugIns/osgdb_unreferenced.so", 20)
        inspector = self.valid_inspector()
        inspector._dependencies[plugin.name] = [
            "/opt/homebrew/lib/libgdal.37.dylib"]
        result = audit(probe.app, self.baseline.app, inspector)
        self.assertTrue(any(
            "dynamic science dependency in Contents/PlugIns/osgdb_unreferenced.so"
            in item["message"] for item in result["findings"]), result["findings"])

    def test_rejects_static_science_markers_in_unreferenced_non_science_machos(self):
        probe = BundleFixture(self.root)
        symbol_plugin = probe.add("Contents/PlugIns/osgdb_symbol.so", 20)
        string_library = probe.add("Contents/lib/libembedded.dylib", 20)
        inspector = self.valid_inspector()
        inspector._dependencies[symbol_plugin.name] = [
            "/usr/lib/libSystem.B.dylib"]
        inspector._dependencies[string_library.name] = [
            "/usr/lib/libSystem.B.dylib"]
        inspector._symbols[symbol_plugin.name] = ["_GDALAllRegister"]
        inspector._strings[string_library.name] = ["proj_context_create"]
        result = audit(probe.app, self.baseline.app, inspector)
        report = "\n".join(item["message"] for item in result["findings"])
        self.assertIn("static science symbol", report)
        self.assertIn("static science string", report)

    def test_allows_private_science_dependency_only_below_science_plugin(self):
        probe = BundleFixture(self.root)
        gdal = probe.add("Contents/lib/libgdal.37.dylib", 20)
        inspector = self.valid_inspector()
        inspector._dependencies["osgdb_science.so"].append(
            "@loader_path/../libgdal.37.dylib")
        inspector._dependencies[gdal.name] = ["/usr/lib/libSystem.B.dylib"]
        result = audit(probe.app, self.baseline.app, inspector)
        self.assertTrue(result["ok"], result["findings"])
        self.assertIn("Contents/lib/libgdal.37.dylib",
                      result["science_only_closure"])

    def test_size_gate_has_immutable_exact_boundaries(self):
        self.assertEqual(AUDIT.classify_size(40 * AUDIT.MIB), "PASS")
        self.assertEqual(AUDIT.classify_size(40 * AUDIT.MIB + 1),
                         "REVIEW_REQUIRED")
        self.assertEqual(AUDIT.classify_size(60 * AUDIT.MIB),
                         "REVIEW_REQUIRED")
        self.assertEqual(AUDIT.classify_size(60 * AUDIT.MIB + 1), "STOP")

    def test_combined_size_gate_has_exact_cli_status_and_exit_codes(self):
        cases = [
            (40 * AUDIT.MIB, 40 * AUDIT.MIB, "PASS", 0),
            (40 * AUDIT.MIB + 1, 40 * AUDIT.MIB,
             "REVIEW_REQUIRED", 2),
            (40 * AUDIT.MIB, 60 * AUDIT.MIB,
             "REVIEW_REQUIRED", 2),
            (60 * AUDIT.MIB + 1, 40 * AUDIT.MIB, "STOP", 1),
            (40 * AUDIT.MIB, 60 * AUDIT.MIB + 1, "STOP", 1),
        ]
        for delta, closure, status, exit_code in cases:
            with self.subTest(delta=delta, closure=closure):
                result = AUDIT.evaluate_size_gates(delta, closure)
                self.assertEqual(result["status"], status)
                self.assertEqual(result["exit_code"], exit_code)

    def test_json_report_round_trips(self):
        probe = BundleFixture(self.root)
        result = audit(probe.app, self.baseline.app, self.valid_inspector())
        encoded = json.dumps(result, sort_keys=True)
        self.assertEqual(json.loads(encoded)["sizes"]["delta_bytes"], 60)

    def test_missing_science_plugin_is_rejected_by_default(self):
        result = AUDIT.audit_bundle(
            app=self.baseline.app,
            baseline=self.baseline.app,
            inspector=self.valid_inspector(),
            source_roots=[ROOT],
            main_relative="Contents/MacOS/main",
        )
        self.assertEqual(
            [item["category"] for item in result["findings"]],
            ["missing_science_plugin"],
        )

    def test_baseline_inventory_suppresses_only_missing_science_plugin(self):
        inspector = self.valid_inspector()
        inspector._strings["main"] = [str(ROOT / "build" / "sdk_core" / "main.cpp")]
        result = AUDIT.audit_bundle(
            app=self.baseline.app,
            baseline=self.baseline.app,
            inspector=inspector,
            source_roots=[ROOT],
            main_relative="Contents/MacOS/main",
            require_science_plugin=False,
        )
        self.assertEqual(
            [item["category"] for item in result["findings"]],
            ["forbidden_string"],
        )

    def test_cli_missing_manifests_stops_and_writes_json_error_report(self):
        baseline = self.root / "MissingManifestBaseline.app"
        candidate = self.root / "MissingManifestCandidate.app"
        write_app_plist(baseline)
        compile_macho(
            baseline / "Contents" / "MacOS" / "main",
            "int main(void) { return 0; }")
        shutil.copytree(baseline, candidate)
        compile_macho(
            candidate / "Contents" / "PlugIns" / "osgdb_science.so",
            "int science_anchor(void) { return 0; }", bundle=True)
        json_path = self.root / "missing-manifest.json"
        result = subprocess.run([
            "python3", str(AUDIT_PATH),
            "--app", str(candidate),
            "--baseline", str(baseline),
            "--json", str(json_path),
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        payload = json.loads(json_path.read_text())
        self.assertEqual(payload["status"], "STOP")
        self.assertIn("manifest", payload["error"].lower())

    def test_cli_rejects_malformed_schema_parent_and_bundle_fingerprint(self):
        baseline = self.root / "InvalidManifestBaseline.app"
        candidate = self.root / "InvalidManifestCandidate.app"
        write_app_plist(baseline)
        compile_macho(
            baseline / "Contents" / "MacOS" / "main",
            "int main(void) { return 0; }")
        shutil.copytree(baseline, candidate)
        compile_macho(
            candidate / "Contents" / "PlugIns" / "osgdb_science.so",
            "int science_anchor(void) { return 0; }", bundle=True)

        cases = {}
        reference, ratchet = self.manifest_chain([], baseline)
        malformed_reference = self.root / "malformed-reference.json"
        malformed_reference.write_text("{not json")
        valid_ratchet = self.root / "valid-ratchet.json"
        valid_ratchet.write_text(json.dumps(ratchet))
        reference_path = self.root / "valid-reference.json"
        reference_path.write_text(json.dumps(reference))
        cases["malformed"] = (malformed_reference, valid_ratchet)

        schema_reference = dict(reference)
        schema_reference["schema_version"] = 999
        schema_path = self.root / "schema-reference.json"
        schema_path.write_text(json.dumps(schema_reference))
        cases["schema"] = (schema_path, valid_ratchet)

        schema_ratchet = json.loads(json.dumps(ratchet))
        schema_ratchet["schema_version"] = 999
        schema_ratchet_path = self.root / "schema-ratchet.json"
        schema_ratchet_path.write_text(json.dumps(schema_ratchet))
        cases["ratchet-schema"] = (reference_path, schema_ratchet_path)

        parent_ratchet = json.loads(json.dumps(ratchet))
        parent_ratchet["reference_sha256"] = "0" * 64
        parent_path = self.root / "parent-ratchet.json"
        parent_path.write_text(json.dumps(parent_ratchet))
        cases["parent"] = (reference_path, parent_path)

        fingerprint_reference = json.loads(json.dumps(reference))
        fingerprint_reference["bundle_fingerprint"] = "f" * 64
        fingerprint_ratchet = MANIFEST.build_ratchet(
            fingerprint_reference, "v0.2.0")
        fingerprint_reference_path = self.root / "fingerprint-reference.json"
        fingerprint_ratchet_path = self.root / "fingerprint-ratchet.json"
        fingerprint_reference_path.write_text(json.dumps(fingerprint_reference))
        fingerprint_ratchet_path.write_text(json.dumps(fingerprint_ratchet))
        cases["fingerprint"] = (
            fingerprint_reference_path, fingerprint_ratchet_path)

        for label, (reference_file, ratchet_file) in cases.items():
            with self.subTest(label=label):
                json_path = self.root / f"invalid-{label}.json"
                result = subprocess.run([
                    "python3", str(AUDIT_PATH),
                    "--app", str(candidate),
                    "--baseline", str(baseline),
                    "--reference-manifest", str(reference_file),
                    "--ratchet-manifest", str(ratchet_file),
                    "--json", str(json_path),
                ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                self.assertEqual(result.returncode, 1, result.stderr)
                payload = json.loads(json_path.read_text())
                self.assertEqual(payload["status"], "STOP")
                self.assertIn("error", payload)

    def test_cli_stop_writes_json_and_text_reports_for_real_machos(self):
        baseline = self.root / "CliBaseline.app"
        candidate = self.root / "CliCandidate.app"
        write_app_plist(baseline)
        main = baseline / "Contents" / "MacOS" / "main"
        compile_macho(main, "int main(void) { return 0; }")
        shutil.copytree(baseline, candidate)
        plugin_dir = candidate / "Contents" / "PlugIns"
        compile_macho(
            plugin_dir / "osgdb_science.so",
            "int science_anchor(void) { return 0; }", bundle=True)
        compile_macho(
            plugin_dir / "osgdb_bad.so",
            "extern void GDALAllRegister(void); "
            "void bad(void) { GDALAllRegister(); }", bundle=True)
        reference_path, ratchet_path = self.write_manifest_chain(baseline)
        json_path = self.root / "audit.json"
        text_path = self.root / "audit.txt"
        result = subprocess.run([
            "python3", str(AUDIT_PATH),
            "--app", str(candidate),
            "--baseline", str(baseline),
            "--reference-manifest", str(reference_path),
            "--ratchet-manifest", str(ratchet_path),
            "--json", str(json_path),
            "--text", str(text_path),
        ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        payload = json.loads(json_path.read_text())
        self.assertEqual(payload["status"], "STOP")
        report = text_path.read_text()
        for section in (
                "Dependency graph", "Science-only closure", "Size gates",
                "Isolation manifests", "Tier A science closure",
                "Tier B non-science delta", "Historical absolute debt",
                "Removed debt", "New findings", "Violations"):
            self.assertIn(section, report)

    def test_real_cli_uses_pass_review_and_stop_exit_codes(self):
        baseline = self.root / "TierBaseline.app"
        candidate = self.root / "TierCandidate.app"
        write_app_plist(baseline)
        compile_macho(
            baseline / "Contents" / "MacOS" / "main",
            "int main(void) { return 0; }")
        shutil.copytree(baseline, candidate)
        compile_macho(
            candidate / "Contents" / "PlugIns" / "osgdb_science.so",
            "int science_anchor(void) { return 0; }", bundle=True)
        reference_path, ratchet_path = self.write_manifest_chain(baseline)

        def run_cli(label):
            json_path = self.root / f"tier-{label}.json"
            result = subprocess.run([
                "python3", str(AUDIT_PATH),
                "--app", str(candidate),
                "--baseline", str(baseline),
                "--reference-manifest", str(reference_path),
                "--ratchet-manifest", str(ratchet_path),
                "--json", str(json_path),
            ], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            return result.returncode, json.loads(json_path.read_text())

        returncode, payload = run_cli("pass")
        self.assertEqual((returncode, payload["status"]), (0, "PASS"))
        base_delta = AUDIT.bundle_size(candidate) - AUDIT.bundle_size(baseline)
        padding = candidate / "Contents" / "tier-padding.bin"
        with padding.open("wb") as stream:
            stream.truncate(AUDIT.TARGET_ADDED_BYTES + 1 - base_delta)
        returncode, payload = run_cli("review")
        self.assertEqual(
            (returncode, payload["status"]), (2, "REVIEW_REQUIRED"))
        with padding.open("wb") as stream:
            stream.truncate(AUDIT.HARD_STOP_ADDED_BYTES + 1 - base_delta)
        returncode, payload = run_cli("stop")
        self.assertEqual((returncode, payload["status"]), (1, "STOP"))


class ScienceProbeBuilderTests(unittest.TestCase):
    def tree_digest(self, root):
        digest = hashlib.sha256()
        for path in sorted(Path(root).rglob("*")):
            digest.update(str(path.relative_to(root)).encode())
            if path.is_file() and not path.is_symlink():
                digest.update(path.read_bytes())
        return digest.hexdigest()

    def test_builder_creates_disposable_probe_without_mutating_baseline(self):
        with tempfile.TemporaryDirectory(prefix="osgsol-probe-builder-") as root:
            root_path = Path(root)
            baseline = root_path / "Baseline.app"
            executable = baseline / "Contents" / "MacOS" / "osgSol_Earth"
            write_app_plist(baseline, "osgSol_Earth")
            compile_macho(
                executable,
                "int baseline_main_marker(void) { return 0; } "
                "int main(void) { return baseline_main_marker(); }")
            plugins = baseline / "Contents" / "lib" / "osgPlugins-3.6.5"
            plugins.mkdir(parents=True)
            (plugins / "osgdb_base.so").write_bytes(b"base-plugin")
            subprocess.run(
                ["xattr", "-w", "com.osgsol.probe-test", "copied-metadata",
                 str(executable)], check=True)
            executable.parent.chmod(0o555)
            current = root_path / "current-main"
            compile_macho(
                current,
                "int current_probe_main_marker(void) { return 1; } "
                "int main(void) { return current_probe_main_marker(); }")
            science = root_path / "osgdb_science_g0_probe.so"
            compile_macho(
                science, "int science_anchor(void) { return 0; }", bundle=True)
            output = root_path / "build" / "Science Probe.app"
            before = self.tree_digest(baseline)
            environment = os.environ.copy()
            environment.update({
                "SCIENCE_G0_BASELINE_APP": str(baseline),
                "SCIENCE_G0_CURRENT_EXECUTABLE": str(current),
                "SCIENCE_G0_PROBE_PLUGIN": str(science),
                "SCIENCE_G0_OUTPUT_APP": str(output),
                "CODESIGN_BIN": "/usr/bin/false",
            })
            subprocess.run(["bash", str(BUILDER_PATH)], check=True, env=environment)
            self.assertEqual(before, self.tree_digest(baseline))
            output_main = output / "Contents" / "MacOS" / "osgSol_Earth"
            output_plugin = (output / "Contents" / "lib" /
                             "osgPlugins-3.6.5" / "osgdb_science.so")
            main_symbols = subprocess.run(
                ["nm", "-gU", str(output_main)], check=True, text=True,
                stdout=subprocess.PIPE).stdout
            plugin_symbols = subprocess.run(
                ["nm", "-gU", str(output_plugin)], check=True, text=True,
                stdout=subprocess.PIPE).stdout
            self.assertIn("_current_probe_main_marker", main_symbols)
            self.assertNotIn("_baseline_main_marker", main_symbols)
            self.assertIn("_science_anchor", plugin_symbols)
            subprocess.run(
                ["/usr/bin/codesign", "--verify", "--deep", "--strict", str(output)],
                check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            xattrs = subprocess.run(
                ["xattr", "-lr", str(output)], check=True, text=True,
                stdout=subprocess.PIPE).stdout
            self.assertNotIn("com.osgsol.probe-test", xattrs)
            for directory in (output / "Contents" / "MacOS",
                              output / "Contents" / "lib" / "osgPlugins-3.6.5"):
                self.assertTrue(directory.stat().st_mode & stat.S_IWUSR)

    def test_builder_rejects_canonical_alias_of_baseline_output(self):
        with tempfile.TemporaryDirectory(prefix="osgsol-probe-alias-") as root:
            root_path = Path(root)
            baseline = root_path / "Baseline.app"
            executable = baseline / "Contents" / "MacOS" / "osgSol_Earth"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"protected-main")
            current = root_path / "current-main"
            current.write_bytes(b"current-main")
            current.chmod(0o755)
            science = root_path / "osgdb_science_g0_probe.so"
            science.write_bytes(b"science-plugin")
            environment = os.environ.copy()
            environment.update({
                "SCIENCE_G0_BASELINE_APP": str(baseline),
                "SCIENCE_G0_CURRENT_EXECUTABLE": str(current),
                "SCIENCE_G0_PROBE_PLUGIN": str(science),
                "SCIENCE_G0_OUTPUT_APP": str(root_path) + "/./Baseline.app",
            })
            result = subprocess.run(
                ["bash", str(BUILDER_PATH)], env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(baseline.is_dir())
            self.assertEqual(executable.read_bytes(), b"protected-main")


if __name__ == "__main__":
    unittest.main()
