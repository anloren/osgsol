import hashlib
import importlib.util
import json
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
AUDIT_PATH = ROOT / "packaging" / "audit_macos_bundle.py"
BUILDER_PATH = ROOT / "packaging" / "build_science_g0_probe.sh"
SPEC = importlib.util.spec_from_file_location("audit_macos_bundle", AUDIT_PATH)
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)


class FakeInspector:
    def __init__(self, dependencies, rpaths=None, strings=None):
        self._dependencies = dependencies
        self._rpaths = rpaths or {}
        self._strings = strings or {}

    def is_macho(self, path):
        return path.name in self._dependencies

    def dependencies(self, path):
        return self._dependencies[path.name]

    def rpaths(self, path):
        return self._rpaths.get(path.name, [])

    def string_references(self, path):
        return self._strings.get(path.name, [])


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


def audit(app, baseline, inspector, max_added_bytes=40 * 1024 * 1024):
    return AUDIT.audit_bundle(
        app=app,
        baseline=baseline,
        inspector=inspector,
        source_roots=[ROOT],
        max_added_bytes=max_added_bytes,
        main_relative="Contents/MacOS/main",
        science_plugin_name="osgdb_science.so",
    )


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
        )

    def test_recursively_audits_main_libraries_and_unreferenced_plugins(self):
        probe = BundleFixture(self.root)
        result = audit(probe.app, self.baseline.app, self.valid_inspector())
        self.assertTrue(result["ok"], result["violations"])
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
        self.assertTrue(any("unresolved" in item for item in result["violations"]))

    def test_prefers_bundled_rpath_target_over_existing_source_copy(self):
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
        self.assertEqual(edge["resolved"], "Contents/lib/libbase.dylib")
        self.assertEqual(result["unresolved"], [])

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
        report = "\n".join(result["violations"])
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
        self.assertTrue(any("reachable from main" in item
                            for item in result["violations"]))

    def test_allows_private_science_dependency_only_below_science_plugin(self):
        probe = BundleFixture(self.root)
        gdal = probe.add("Contents/lib/libgdal.37.dylib", 20)
        inspector = self.valid_inspector()
        inspector._dependencies["osgdb_science.so"].append(
            "@loader_path/../libgdal.37.dylib")
        inspector._dependencies[gdal.name] = ["/usr/lib/libSystem.B.dylib"]
        result = audit(probe.app, self.baseline.app, inspector)
        self.assertTrue(result["ok"], result["violations"])
        self.assertIn("Contents/lib/libgdal.37.dylib",
                      result["science_only_closure"])

    def test_rejects_added_bundle_size_above_limit(self):
        probe = BundleFixture(self.root)
        probe.add("Contents/science-large.bin", 41)
        result = audit(probe.app, self.baseline.app, self.valid_inspector(),
                       max_added_bytes=40)
        self.assertFalse(result["ok"])
        self.assertTrue(any("added bundle size" in item
                            for item in result["violations"]))

    def test_json_report_round_trips(self):
        probe = BundleFixture(self.root)
        result = audit(probe.app, self.baseline.app, self.valid_inspector())
        encoded = json.dumps(result, sort_keys=True)
        self.assertEqual(json.loads(encoded)["sizes"]["delta_bytes"], 60)


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
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"baseline-main")
            plugins = baseline / "Contents" / "lib" / "osgPlugins-3.6.5"
            plugins.mkdir(parents=True)
            (plugins / "osgdb_base.so").write_bytes(b"base-plugin")
            subprocess.run(
                ["xattr", "-w", "com.osgsol.probe-test", "copied-metadata",
                 str(executable)], check=True)
            executable.parent.chmod(0o555)
            current = root_path / "current-main"
            current.write_bytes(b"current-main")
            current.chmod(0o755)
            science = root_path / "osgdb_science_g0_probe.so"
            science.write_bytes(b"science-plugin")
            output = root_path / "build" / "Science Probe.app"
            before = self.tree_digest(baseline)
            environment = os.environ.copy()
            environment.update({
                "SCIENCE_G0_BASELINE_APP": str(baseline),
                "SCIENCE_G0_CURRENT_EXECUTABLE": str(current),
                "SCIENCE_G0_PROBE_PLUGIN": str(science),
                "SCIENCE_G0_OUTPUT_APP": str(output),
                "CODESIGN_BIN": "/usr/bin/true",
            })
            subprocess.run(["bash", str(BUILDER_PATH)], check=True, env=environment)
            self.assertEqual(before, self.tree_digest(baseline))
            self.assertEqual(
                (output / "Contents" / "MacOS" / "osgSol_Earth").read_bytes(),
                b"current-main",
            )
            self.assertEqual(
                (output / "Contents" / "lib" / "osgPlugins-3.6.5" /
                 "osgdb_science.so").read_bytes(),
                b"science-plugin",
            )
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
                "CODESIGN_BIN": "/usr/bin/true",
            })
            result = subprocess.run(
                ["bash", str(BUILDER_PATH)], env=environment,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(baseline.is_dir())
            self.assertEqual(executable.read_bytes(), b"protected-main")


if __name__ == "__main__":
    unittest.main()
