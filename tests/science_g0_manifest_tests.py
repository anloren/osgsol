import importlib.util
import json
import os
import shutil
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
AUDIT_PATH = ROOT / "packaging" / "audit_macos_bundle.py"
MANIFEST_PATH = ROOT / "packaging" / "scienceearth" / "g0_manifest.py"
GENERATOR_PATH = ROOT / "packaging" / "scienceearth" / "generate_g0_reference.py"


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


AUDIT = load_module("audit_macos_bundle", AUDIT_PATH)
MANIFEST = load_module("g0_manifest", MANIFEST_PATH)


class G0ManifestTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="osgsol-g0-manifest-")
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()

    def make_chain(self, two_findings=False):
        findings = [AUDIT.make_finding(
            "Contents/MacOS/main", "external_dependency",
            "/opt/example/lib.dylib", [])]
        if two_findings:
            findings.append(AUDIT.make_finding(
                "Contents/lib/libbase.dylib", "forbidden_rpath",
                "/usr/local/lib", []))
        reference = MANIFEST.build_reference(
            findings, "0e91c7c4b121d80b929d595ea711d3dd0833ee67",
            "a" * 64, {"architecture": "arm64"})
        return reference, MANIFEST.build_ratchet(reference, "v0.2.0")

    def run_generator(self, reference):
        return subprocess.run([
            "python3", str(GENERATOR_PATH),
            "--app", str(self.root / "Protected.app"),
            "--reference", str(reference),
            "--ratchet", str(self.root / "ratchet.json"),
            "--boundary-tag", "ScienceEarth",
            "--release-tag", "v0.2.0",
            "--source-root", str(ROOT),
        ], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

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

    def test_reference_rejects_wrong_boundary_and_malformed_hashes(self):
        finding = AUDIT.make_finding(
            "Contents/MacOS/main", "external_dependency",
            "/opt/example/lib.dylib", [])
        with self.assertRaisesRegex(ValueError, "immutable boundary"):
            MANIFEST.build_reference([finding], "0" * 40, "a" * 64, {})
        finding["identity"] = "A" * 64
        with self.assertRaisesRegex(ValueError, "64 lowercase hex"):
            MANIFEST.build_reference(
                [finding], "0e91c7c4b121d80b929d595ea711d3dd0833ee67",
                "a" * 64, {})

    def test_reference_sorts_ids_and_rejects_duplicates(self):
        first = AUDIT.make_finding(
            "Contents/MacOS/main", "external_dependency", "/z/lib.dylib", [])
        second = AUDIT.make_finding(
            "Contents/MacOS/main", "external_dependency", "/a/lib.dylib", [])
        reference = MANIFEST.build_reference(
            [first, second], "0e91c7c4b121d80b929d595ea711d3dd0833ee67",
            "a" * 64, {})
        self.assertEqual(reference["finding_ids"],
                         sorted([first["identity"], second["identity"]]))
        with self.assertRaisesRegex(ValueError, "unique"):
            MANIFEST.build_reference(
                [first, first], "0e91c7c4b121d80b929d595ea711d3dd0833ee67",
                "a" * 64, {})

    def test_reference_rejects_finding_field_tampering(self):
        reference, _ = self.make_chain()
        reference["findings"][0]["owner"] = "Contents/MacOS/replacement"
        with self.assertRaisesRegex(ValueError, "identity"):
            MANIFEST.validate_reference(reference)

    def test_bundle_fingerprint_is_location_independent_and_content_sensitive(self):
        first = self.root / "First.app"
        executable = first / "Contents" / "MacOS" / "main"
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b"main-v1")
        executable.chmod(0o755)
        link = first / "Contents" / "current-main"
        link.symlink_to("MacOS/main")
        second = self.root / "relocated" / "Second.app"
        shutil.copytree(first, second, symlinks=True)
        original = MANIFEST.bundle_fingerprint(first)
        self.assertEqual(original, MANIFEST.bundle_fingerprint(second))
        (second / "Contents" / "MacOS" / "main").write_bytes(b"main-v2")
        self.assertNotEqual(original, MANIFEST.bundle_fingerprint(second))

    def test_write_new_json_uses_public_mode_and_never_overwrites(self):
        generator = load_module("generate_g0_reference", GENERATOR_PATH)
        output = self.root / "nested" / "manifest.json"
        generator.write_new_json(output, {"value": 1})
        self.assertEqual(stat.S_IMODE(output.stat().st_mode), 0o644)
        with self.assertRaisesRegex(FileExistsError, "refusing to overwrite"):
            generator.write_new_json(output, {"value": 2})

    def test_pair_publishes_the_validated_mode_0600_temporary_bytes(self):
        generator = load_module("generate_g0_reference_pair", GENERATOR_PATH)
        reference, ratchet = self.make_chain()
        reference_path = self.root / "reference.json"
        ratchet_path = self.root / "ratchet.json"
        original_validate = generator.MANIFEST.validate_chain

        def validate_written_pair(written_reference, written_ratchet):
            temporary_files = sorted(self.root.glob(".*.json.*"))
            self.assertEqual(len(temporary_files), 2)
            self.assertTrue(all(
                stat.S_IMODE(path.stat().st_mode) == 0o600
                for path in temporary_files))
            original_validate(written_reference, written_ratchet)

        with mock.patch.object(
                generator.MANIFEST, "validate_chain",
                side_effect=validate_written_pair), mock.patch.object(
                    generator.json, "dump", wraps=generator.json.dump) as dump:
            generator.write_manifest_pair(
                reference_path, reference, ratchet_path, ratchet)
        self.assertEqual(dump.call_count, 2)
        self.assertEqual(stat.S_IMODE(reference_path.stat().st_mode), 0o644)
        self.assertEqual(stat.S_IMODE(ratchet_path.stat().st_mode), 0o644)
        generator.MANIFEST.validate_chain(
            json.loads(reference_path.read_text()),
            json.loads(ratchet_path.read_text()),
        )

    def test_atomic_publisher_rolls_back_when_final_chmod_fails(self):
        generator = load_module("generate_g0_reference_rollback", GENERATOR_PATH)
        temporary = self.root / ".reference.json.temporary"
        destination = self.root / "reference.json"
        temporary.write_text("{}\n")
        temporary.chmod(0o600)
        with mock.patch.object(
                generator.os, "chmod", side_effect=PermissionError("denied")):
            with self.assertRaises(PermissionError):
                generator._publish_temporary_json(temporary, destination)
        self.assertFalse(temporary.exists())
        self.assertFalse(destination.exists())

    def test_generator_requires_literal_boundary_and_release_tags(self):
        generator = load_module("generate_g0_reference_tags", GENERATOR_PATH)
        with self.assertRaisesRegex(ValueError, "v0.2.0"):
            generator.verify_boundary_tags("ScienceEarth", "ScienceEarth")

    def test_generator_filters_science_findings_and_machine_paths(self):
        generator = load_module("generate_g0_reference_filter", GENERATOR_PATH)
        main_finding = AUDIT.make_finding(
            "Contents/MacOS/main", "forbidden_string", "${SOURCE_ROOT}/main", [])
        main_finding["message"] = "source string: /Users/USER/private/main"
        science_finding = AUDIT.make_finding(
            "Contents/lib/osgdb_science.so", "external_dependency",
            "/opt/example/lib.dylib", [])
        result = {
            "graph": {
                "Contents/MacOS/main": [],
                "Contents/lib/osgdb_science.so": [{
                    "resolved": "Contents/lib/libgdal.dylib",
                }],
                "Contents/lib/libgdal.dylib": [],
            },
            "findings": [main_finding, science_finding],
        }
        filtered = generator.non_science_findings(result)
        self.assertEqual(filtered, [{
            "schema_version": main_finding["schema_version"],
            "owner": main_finding["owner"],
            "category": main_finding["category"],
            "subject": main_finding["subject"],
            "identity": main_finding["identity"],
        }])
        arguments = generator.parse_arguments([
            "--app", "/Users/USER/Desktop/Protected.app",
            "--reference", "/Users/USER/reference.json",
            "--ratchet", "/Users/USER/ratchet.json",
            "--source-root", "/Users/USER/osgverse",
        ])
        command = generator.normalized_generation_command(arguments)
        self.assertNotIn("/Users/USER", json.dumps(command))


if __name__ == "__main__":
    unittest.main()
