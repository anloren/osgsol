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
DATA_MANIFEST_PATH = (
    ROOT / "packaging" / "scienceearth" / "data_manifest.py")
BUILDER_PATH = ROOT / "packaging" / "build_science_g0_probe.sh"
SPEC = importlib.util.spec_from_file_location("audit_macos_bundle", AUDIT_PATH)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
MANIFEST_SPEC = importlib.util.spec_from_file_location("g0_manifest", MANIFEST_PATH)
MANIFEST = importlib.util.module_from_spec(MANIFEST_SPEC)
MANIFEST_SPEC.loader.exec_module(MANIFEST)


def load_data_manifest_module():
    spec = importlib.util.spec_from_file_location(
        "scienceearth_data_manifest", DATA_MANIFEST_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FakeInspector:
    def __init__(self, dependencies, rpaths=None, strings=None, symbols=None,
                 exported_symbols=None):
        self._dependencies = dependencies
        self._rpaths = rpaths or {}
        self._strings = strings or {}
        self._symbols = symbols or {}
        self._exported_symbols = exported_symbols or {}

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

    def exported_symbols(self, path):
        return self._exported_symbols.get(path.name, [])


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
    source_roots = kwargs.pop("source_roots", [ROOT])
    return AUDIT.audit_bundle(
        app=app,
        baseline=baseline,
        inspector=inspector,
        source_roots=source_roots,
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


class ScienceDataManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(
            prefix="osgsol-science-data-manifest-")
        self.app = Path(self.temporary.name) / "Science.app"
        self.data_path = (
            self.app / "Contents" / "misc" / "science" / "alphaearth" /
            "alphaearth.sqlite")
        self.data_path.parent.mkdir(parents=True)
        self.data_path.write_bytes(b"verified-alphaearth-data")
        self.relative = (
            "Contents/misc/science/alphaearth/alphaearth.sqlite")
        self.manifest_path = (
            self.app / "Contents" / "misc" / "science" /
            "data-manifest.json")

    def tearDown(self):
        self.temporary.cleanup()

    def document(self, **entry_changes):
        entry = {
            "bytes": self.data_path.stat().st_size,
            "path": self.relative,
            "role": "spatial-index",
            "sha256": hashlib.sha256(self.data_path.read_bytes()).hexdigest(),
            "source_id": "alphaearth-foundations",
        }
        entry.update(entry_changes)
        return {
            "entries": [entry],
            "schema": "osgsol-science-data-v1",
        }

    def write_document(self, document, canonical=True):
        self.manifest_path.parent.mkdir(parents=True, exist_ok=True)
        if canonical:
            encoded = json.dumps(
                document, sort_keys=True, separators=(",", ":")) + "\n"
        else:
            encoded = json.dumps(document, indent=2, sort_keys=True) + "\n"
        self.manifest_path.write_text(encoded)

    def test_validates_exact_canonical_science_data_document(self):
        self.write_document(self.document())

        result = load_data_manifest_module().validate(self.app)

        self.assertEqual(result["bytes"], self.data_path.stat().st_size)
        self.assertEqual(result["entries"], self.document()["entries"])

    def test_rejects_untrusted_manifest_and_payload_shapes(self):
        module = load_data_manifest_module()
        outside = self.app / "Contents" / "misc" / "outside.bin"
        outside.parent.mkdir(parents=True, exist_ok=True)
        outside.write_bytes(b"outside")
        link = self.data_path.with_name("linked.sqlite")
        link.symlink_to(self.data_path.name)
        macho = self.data_path.with_name("macho.bin")
        macho.write_bytes(bytes.fromhex("feedfacf") + b"not-data")
        cases = {
            "traversal": self.document(path=(
                "Contents/misc/science/../../outside.bin")),
            "absolute": self.document(path=str(self.data_path)),
            "symlink": self.document(
                path=str(link.relative_to(self.app)),
                bytes=link.stat().st_size,
                sha256=hashlib.sha256(link.read_bytes()).hexdigest()),
            "macho": self.document(
                path=str(macho.relative_to(self.app)),
                bytes=macho.stat().st_size,
                sha256=hashlib.sha256(macho.read_bytes()).hexdigest()),
            "missing": self.document(path=(
                "Contents/misc/science/alphaearth/missing.sqlite")),
            "wrong-bytes": self.document(bytes=1),
            "boolean-bytes": self.document(bytes=True),
            "wrong-hash": self.document(sha256="0" * 64),
            "outside": self.document(
                path=str(outside.relative_to(self.app)),
                bytes=outside.stat().st_size,
                sha256=hashlib.sha256(outside.read_bytes()).hexdigest()),
            "unknown-entry-key": self.document(extra="not-allowed"),
        }
        duplicate = self.document()
        duplicate["entries"].append(dict(duplicate["entries"][0]))
        cases["duplicate"] = duplicate
        unknown_top = self.document()
        unknown_top["unknown"] = True
        cases["unknown-top-key"] = unknown_top

        for label, document in cases.items():
            with self.subTest(label=label):
                self.write_document(document)
                with self.assertRaises(ValueError):
                    module.validate(self.app)

        self.write_document(self.document(), canonical=False)
        with self.assertRaisesRegex(ValueError, "canonical"):
            module.validate(self.app)


class ScienceBundleAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="osgsol-bundle-audit-")
        self.root = Path(self.temporary.name)
        self.baseline = BundleFixture(self.root, "Baseline.app")
        self.baseline.plugin.unlink()
        self.policy_source_roots = [
            self.make_git_root(
                "osgverse-source", "https://github.com/anloren/osgverse.git"),
            self.make_git_root(
                "osgsol-source", "git@github.com:anloren/osgsol.git"),
        ]

    def tearDown(self):
        self.temporary.cleanup()

    def make_git_root(self, name, remote):
        root = self.root / name
        root.mkdir()
        subprocess.run(
            ["git", "init", "-q", str(root)], check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run(
            ["git", "-C", str(root), "remote", "add", "origin", remote],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return root

    def source_root_arguments(self):
        arguments = []
        for root in self.policy_source_roots:
            arguments.extend(["--source-root", str(root)])
        return arguments

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
            exported_symbols={"osgdb_science.so": [
                "0000000000001000 T _osgsol_science_g0_probe_anchor",
            ]},
        )

    def manifest_chain(self, findings, baseline=None):
        baseline = baseline or self.baseline.app
        reference = MANIFEST.build_reference(
            findings,
            MANIFEST.BOUNDARY_COMMIT,
            MANIFEST.bundle_fingerprint(baseline),
            {
                "architecture": "test",
                "normalization_profile": MANIFEST.build_normalization_profile(
                    self.policy_source_roots),
                "toolchain_provenance": {
                    "schema": "scienceearth-g0-toolchain-provenance",
                    "version": 1,
                    "compiler": {"id": "TestCompiler", "version": "1.0"},
                    "sdk": {"name": "test-sdk", "version": "1.0", "build": "1A1"},
                },
            },
        )
        return reference, MANIFEST.build_ratchet(reference, "v0.2.0")

    def write_manifest_chain(self, baseline, findings=None):
        reference, ratchet = self.manifest_chain(findings or [], baseline)
        reference_path = self.root / "reference.json"
        ratchet_path = self.root / "ratchet.json"
        reference_path.write_text(json.dumps(reference))
        ratchet_path.write_text(json.dumps(ratchet))
        return reference_path, ratchet_path

    def bind_science_data(self, app, relative, payload):
        path = Path(app) / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(payload)
        result = load_data_manifest_module().write(app, relative)
        write_app_plist(app)
        plist_path = Path(app) / "Contents" / "Info.plist"
        with plist_path.open("rb") as stream:
            plist = plistlib.load(stream)
        plist["ScienceEarthDataManifestSha256"] = result["manifest_sha256"]
        with plist_path.open("wb") as stream:
            plistlib.dump(plist, stream)
        return path, result

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
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["schema_version"], 4)
        self.assertTrue(all(
            key in result for key in
            ("manifests", "tier_a", "tier_b", "absolute", "delta")))
        self.assertEqual(result["delta"]["new"], [])
        inspector._symbols["libbase.dylib"].append("_ZSTD_compress")
        result = audit(
            probe.app, self.baseline.app, inspector,
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["status"], "STOP")
        self.assertEqual(len(result["delta"]["new"]), 1)

    def test_audit_splits_only_plist_bound_verified_science_data(self):
        probe = BundleFixture(self.root)
        relative = "Contents/misc/science/alphaearth/alphaearth.sqlite"
        payload = b"alphaearth-verified-payload"
        _, manifest = self.bind_science_data(probe.app, relative, payload)
        reference, ratchet = self.manifest_chain([])

        result = audit(
            probe.app, self.baseline.app, self.valid_inspector(),
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)

        total_delta = (
            AUDIT.bundle_size(probe.app) - AUDIT.bundle_size(self.baseline.app))
        self.assertEqual(result["science_data"]["bytes"], len(payload))
        self.assertEqual(result["sizes"]["science_data_bytes"], len(payload))
        self.assertEqual(
            result["sizes"]["runtime_delta_bytes"],
            total_delta - len(payload))
        self.assertEqual(
            result["sizes"]["combined_delta_bytes"], total_delta)
        self.assertEqual(result["size_gates"]["science_data"], "PASS")
        self.assertEqual(
            result["science_data"]["manifest_sha256"],
            manifest["manifest_sha256"])

    def test_invalid_present_data_manifest_is_not_excluded_from_runtime(self):
        probe = BundleFixture(self.root)
        relative = "Contents/misc/science/alphaearth/alphaearth.sqlite"
        data, _ = self.bind_science_data(probe.app, relative, b"trusted")
        data.write_bytes(b"tampered")
        reference, ratchet = self.manifest_chain([])

        result = audit(
            probe.app, self.baseline.app, self.valid_inspector(),
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)

        total_delta = (
            AUDIT.bundle_size(probe.app) - AUDIT.bundle_size(self.baseline.app))
        self.assertEqual(result["science_data"]["bytes"], 0)
        self.assertEqual(result["sizes"]["runtime_delta_bytes"], total_delta)
        self.assertEqual(result["status"], "STOP")
        self.assertTrue(any(
            finding["category"] == "invalid_science_data_manifest"
            for finding in result["findings"]))

    def test_direct_audit_rejects_independently_rehashed_root_substitution(self):
        probe = BundleFixture(self.root)
        reference, ratchet = self.manifest_chain([])
        substituted_roots = [
            self.make_git_root(
                "other-one", "https://github.com/example/unrelated-one.git"),
            self.make_git_root(
                "other-two", "git@github.com:example/unrelated-two.git"),
        ]
        substituted_reference = json.loads(json.dumps(reference))
        substituted_reference["metadata"]["normalization_profile"] = (
            MANIFEST.build_normalization_profile(substituted_roots))
        substituted_ratchet = json.loads(json.dumps(ratchet))
        substituted_ratchet["reference_sha256"] = MANIFEST.manifest_sha256(
            substituted_reference)

        with self.assertRaisesRegex(ValueError, "approved G0 source-root set"):
            AUDIT.audit_bundle(
                app=probe.app,
                baseline=self.baseline.app,
                inspector=self.valid_inspector(),
                source_roots=substituted_roots,
                main_relative="Contents/MacOS/main",
                science_plugin_name="osgdb_science.so",
                reference_manifest=substituted_reference,
                ratchet_manifest=substituted_ratchet,
            )

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
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["tier_a"]["status"], "STOP")

    def test_science_closure_exports_only_the_probe_anchor(self):
        probe = BundleFixture(self.root)
        inspector = self.valid_inspector()
        reference, ratchet = self.manifest_chain([])
        result = audit(
            probe.app, self.baseline.app, inspector,
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertFalse(any(
            finding["category"] == "exported_science_symbol"
            for finding in result["findings"]))

        inspector._exported_symbols["osgdb_science.so"].append(
            "0000000000002000 T _GDALAllRegister")
        result = audit(
            probe.app, self.baseline.app, inspector,
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)
        violations = [
            finding for finding in result["findings"]
            if finding["category"] == "exported_science_symbol"]
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0]["subject"], "_GDALAllRegister")
        self.assertEqual(result["tier_a"]["status"], "STOP")

    def test_real_macho_science_export_is_found_but_anchor_is_allowed(self):
        baseline = self.root / "ExportBaseline.app"
        candidate = self.root / "ExportCandidate.app"
        write_app_plist(baseline)
        compile_macho(
            baseline / "Contents" / "MacOS" / "main",
            "int main(void) { return 0; }")
        shutil.copytree(baseline, candidate)
        plugin = candidate / "Contents" / "PlugIns" / "osgdb_science.so"
        compile_macho(
            plugin,
            "void osgsol_science_g0_probe_anchor(void) {} "
            "void GDALAllRegister(void) {}", bundle=True)
        result = AUDIT.audit_bundle(
            app=candidate, baseline=baseline, source_roots=[ROOT],
            main_relative="Contents/MacOS/main",
            science_plugin_name="osgdb_science.so")
        exported = [
            finding["subject"] for finding in result["findings"]
            if finding["category"] == "exported_science_symbol"]
        self.assertEqual(exported, ["_GDALAllRegister"])

    def test_main_science_reachability_stops_even_when_identically_ratcheted(self):
        probe = BundleFixture(self.root)
        gdal = probe.add("Contents/lib/libgdal.37.dylib", 20)
        alias = probe.app / "Contents/lib/libalias.dylib"
        alias.symlink_to(gdal.name)
        inspector = self.valid_inspector()
        inspector._dependencies["main"].append("@rpath/libalias.dylib")
        inspector._dependencies[gdal.name] = ["/usr/lib/libSystem.B.dylib"]
        historical = AUDIT.make_finding(
            "Contents/MacOS/main", "main_reaches_science_dependency",
            "Contents/lib/libgdal.37.dylib", [])
        reference, ratchet = self.manifest_chain([historical])

        result = audit(
            probe.app, self.baseline.app, inspector,
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)

        self.assertEqual(result["status"], "STOP")
        self.assertEqual(result["tier_a"]["status"], "STOP")
        self.assertEqual(result["tier_b"]["status"], "PASS")
        self.assertEqual(result["delta"]["new"], [])
        self.assertEqual(
            [item["identity"] for item in result["tier_a"]["violations"]],
            [historical["identity"]])

    def test_removed_debt_is_reported_and_cannot_be_substituted(self):
        probe = BundleFixture(self.root)
        old = AUDIT.make_finding(
            "Contents/lib/libbase.dylib", "static_science_symbol",
            "_ZSTD_decompress", [])
        reference, ratchet = self.manifest_chain([old])
        result = audit(
            probe.app, self.baseline.app, self.valid_inspector(),
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(result["delta"]["removed"], [old])

        inspector = self.valid_inspector()
        inspector._symbols["main"] = ["_ZSTD_decompress"]
        result = audit(
            probe.app, self.baseline.app, inspector,
            source_roots=self.policy_source_roots,
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
            source_roots=self.policy_source_roots,
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

    def test_static_symbol_comparison_ignores_address(self):
        old = AUDIT.make_finding(
            "Contents/lib/libbase.dylib", "static_science_symbol",
            "000000000036c794 T _ZSTD_compressBound", [])
        relocated = AUDIT.make_finding(
            "Contents/lib/libbase.dylib", "static_science_symbol",
            "00000000003dc794 T _ZSTD_compressBound", [])

        delta = AUDIT.compare_identity_sets([relocated], [old])

        self.assertTrue(delta["ok"])
        self.assertEqual(delta["new"], [])
        self.assertEqual(delta["removed"], [])
        self.assertNotEqual(old["identity"], relocated["identity"])

    def test_static_symbol_comparison_keeps_semantic_boundaries(self):
        old = AUDIT.make_finding(
            "Contents/lib/libbase.dylib", "static_science_symbol",
            "000000000036c794 T _ZSTD_compressBound", [])
        cases = (
            AUDIT.make_finding(
                "Contents/lib/libbase.dylib", "static_science_symbol",
                "00000000003dc794 T _ZSTD_decompressBound", []),
            AUDIT.make_finding(
                "Contents/MacOS/main", "static_science_symbol",
                "00000000003dc794 T _ZSTD_compressBound", []),
            AUDIT.make_finding(
                "Contents/lib/libbase.dylib", "static_science_symbol",
                "00000000003dc794 t _ZSTD_compressBound", []),
        )

        for changed in cases:
            with self.subTest(changed=changed):
                delta = AUDIT.compare_identity_sets([changed], [old])
                self.assertFalse(delta["ok"])
                self.assertEqual(delta["new"], [changed])
                self.assertEqual(delta["removed"], [old])

    def test_non_symbol_comparison_remains_exact(self):
        old = AUDIT.make_finding(
            "Contents/MacOS/main", "forbidden_string",
            "000000000036c794 /usr/local/lib", [])
        changed = AUDIT.make_finding(
            "Contents/MacOS/main", "forbidden_string",
            "00000000003dc794 /usr/local/lib", [])

        delta = AUDIT.compare_identity_sets([changed], [old])

        self.assertFalse(delta["ok"])
        self.assertEqual(delta["new"], [changed])
        self.assertEqual(delta["removed"], [old])

    def test_root_causes_group_noise_without_discarding_raw_findings(self):
        findings = []
        for index in range(100):
            findings.append(AUDIT.make_finding(
                "Contents/MacOS/main", "static_science_symbol",
                f"000000000000{index:04x} T _GDALSymbol{index}", []))
        for index in range(20):
            findings.append(AUDIT.make_finding(
                "Contents/lib/libreaderwriter.so", "static_science_symbol",
                f"000000000001{index:04x} T _ZSTDSymbol{index}", []))
        findings.append(AUDIT.make_finding(
            "osgdb_science.so", "missing_science_plugin", "0", []))
        findings.extend((
            AUDIT.make_finding(
                "Contents/MacOS/main", "forbidden_string",
                "/usr/local/lib/gdalplugins", []),
            AUDIT.make_finding(
                "Contents/lib/libfontconfig.dylib", "forbidden_string",
                "/opt/homebrew/etc/fonts", []),
        ))
        findings.append(AUDIT.make_finding(
            "Contents/MacOS/main", "unresolved_dependency",
            "@rpath/libmissing.dylib", []))

        root_causes = AUDIT.summarize_root_causes(findings)

        self.assertEqual(len(findings), 124)
        self.assertEqual(
            [item["id"] for item in root_causes],
            [
                "forbidden-compiled-paths",
                "missing-science-plugin",
                "science-static-linkage",
                "unresolved-dependencies",
            ])
        self.assertEqual(
            next(item for item in root_causes
                 if item["id"] == "science-static-linkage")["finding_count"],
            120)
        self.assertEqual(len(findings), 124)

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

    def test_v2_size_budgets_have_exact_pass_review_stop_boundaries(self):
        mib = 1024 * 1024
        cases = (
            ((40 * mib, 128 * mib, 160 * mib, 40 * mib), "PASS"),
            ((40 * mib + 1, 128 * mib, 160 * mib, 40 * mib),
             "REVIEW_REQUIRED"),
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
                    AUDIT.evaluate_v2_size_gates(*values)["status"],
                    expected)

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
            "void osgsol_science_g0_probe_anchor(void) {}", bundle=True)
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
                ] + self.source_root_arguments(), stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, text=True)
                self.assertEqual(result.returncode, 1, result.stderr)
                payload = json.loads(json_path.read_text())
                self.assertEqual(payload["status"], "STOP")
                self.assertIn("error", payload)

    def test_cli_rejects_invalid_or_mismatched_normalization_profile(self):
        baseline = self.root / "NormalizationBaseline.app"
        candidate = self.root / "NormalizationCandidate.app"
        write_app_plist(baseline)
        compile_macho(
            baseline / "Contents" / "MacOS" / "main",
            "int main(void) { return 0; }")
        shutil.copytree(baseline, candidate)
        compile_macho(
            candidate / "Contents" / "PlugIns" / "osgdb_science.so",
            "int science_anchor(void) { return 0; }", bundle=True)

        reference, ratchet = self.manifest_chain([], baseline)
        valid_profile = reference["metadata"]["normalization_profile"]
        cases = {
            "missing": None,
            "wrong-schema": dict(valid_profile, schema="other"),
            "wrong-version": dict(valid_profile, version=3),
            "malformed-count": dict(valid_profile, source_root_count="1"),
            "mismatched-count": dict(valid_profile, source_root_count=3),
            "tampered-hash": dict(valid_profile, source_roots_sha256="0" * 64),
        }
        for label, profile in cases.items():
            with self.subTest(label=label):
                invalid_reference = json.loads(json.dumps(reference))
                if profile is None:
                    invalid_reference["metadata"].pop("normalization_profile")
                else:
                    invalid_reference["metadata"]["normalization_profile"] = profile
                reference_path = self.root / f"normalization-{label}-reference.json"
                ratchet_path = self.root / f"normalization-{label}-ratchet.json"
                json_path = self.root / f"normalization-{label}-audit.json"
                reference_path.write_text(json.dumps(invalid_reference))
                ratchet_path.write_text(json.dumps(ratchet))

                result = subprocess.run([
                    "python3", str(AUDIT_PATH),
                    "--app", str(candidate),
                    "--baseline", str(baseline),
                    "--reference-manifest", str(reference_path),
                    "--ratchet-manifest", str(ratchet_path),
                    "--json", str(json_path),
                ] + self.source_root_arguments(), stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, text=True)

                self.assertEqual(result.returncode, 1, result.stdout)
                payload = json.loads(json_path.read_text())
                self.assertEqual(payload["status"], "STOP")
                self.assertIn("normalization profile", payload["error"])

    def test_stop_renders_full_text_report_for_real_machos(self):
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
        reference, ratchet = self.manifest_chain([], baseline)
        payload = AUDIT.audit_bundle(
            app=candidate, baseline=baseline,
            source_roots=self.policy_source_roots,
            reference_manifest=reference, ratchet_manifest=ratchet)
        self.assertEqual(payload["status"], "STOP")
        report = AUDIT.render_text(payload)
        for section in (
                "Root causes", "Unresolved dependencies", "Dependency graph",
                "Science-only closure", "Size gates",
                "Isolation manifests", "Tier A science closure",
                "Tier B non-science delta", "Historical absolute debt",
                "Removed debt", "New findings", "Violations"):
            self.assertIn(section, report)
        self.assertLess(report.index("Size gates"), report.index("Root causes"))
        self.assertLess(
            report.index("Root causes"), report.index("Unresolved dependencies"))
        self.assertLess(
            report.index("Unresolved dependencies"),
            report.index("Dependency graph"))

    def test_real_macho_audit_uses_pass_review_and_stop_exit_codes(self):
        baseline = self.root / "TierBaseline.app"
        candidate = self.root / "TierCandidate.app"
        write_app_plist(baseline)
        compile_macho(
            baseline / "Contents" / "MacOS" / "main",
            "int main(void) { return 0; }")
        shutil.copytree(baseline, candidate)
        compile_macho(
            candidate / "Contents" / "PlugIns" / "osgdb_science.so",
            "void osgsol_science_g0_probe_anchor(void) {}", bundle=True)
        reference, ratchet = self.manifest_chain([], baseline)

        def run_audit():
            payload = AUDIT.audit_bundle(
                app=candidate, baseline=baseline,
                source_roots=self.policy_source_roots,
                reference_manifest=reference, ratchet_manifest=ratchet)
            return payload["exit_code"], payload

        returncode, payload = run_audit()
        self.assertEqual((returncode, payload["status"]), (0, "PASS"))
        base_delta = AUDIT.bundle_size(candidate) - AUDIT.bundle_size(baseline)
        padding = candidate / "Contents" / "tier-padding.bin"
        with padding.open("wb") as stream:
            stream.truncate(AUDIT.TARGET_ADDED_BYTES + 1 - base_delta)
        returncode, payload = run_audit()
        self.assertEqual(
            (returncode, payload["status"]), (2, "REVIEW_REQUIRED"))
        with padding.open("wb") as stream:
            stream.truncate(AUDIT.HARD_STOP_ADDED_BYTES + 1 - base_delta)
        returncode, payload = run_audit()
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
            before_fingerprint = MANIFEST.bundle_fingerprint(baseline)
            environment = os.environ.copy()
            environment.update({
                "SCIENCE_G0_BASELINE_APP": str(baseline),
                "SCIENCE_G0_CURRENT_EXECUTABLE": str(current),
                "SCIENCE_G0_PROBE_PLUGIN": str(science),
                "SCIENCE_G0_OUTPUT_APP": str(output),
                "CODESIGN_BIN": "/usr/bin/false",
            })
            result = subprocess.run(
                ["bash", str(BUILDER_PATH)], check=True, env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertEqual(before, self.tree_digest(baseline))
            self.assertIn(
                f"protected baseline fingerprint unchanged: {before_fingerprint}",
                result.stdout)
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
