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
RELEASE_TEST_PATH = ROOT / "tests" / "scienceearth_release_tests.sh"


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

    def metadata(self, **values):
        metadata = {
            "normalization_profile": MANIFEST.expected_normalization_profile(),
        }
        metadata.update(values)
        return metadata

    def substitute_profile(self):
        descriptors = [
            {"repository": "github.com/example/unrelated-one", "subpath": "."},
            {"repository": "github.com/example/unrelated-two", "subpath": "."},
        ]
        return {
            "schema": MANIFEST.NORMALIZATION_PROFILE_SCHEMA,
            "version": MANIFEST.NORMALIZATION_PROFILE_VERSION,
            "source_root_count": len(descriptors),
            "source_roots": descriptors,
            "source_roots_sha256": MANIFEST.manifest_sha256(descriptors),
        }

    def substitute_chain_and_rehash(self, reference, ratchet):
        substituted_reference = json.loads(json.dumps(reference))
        substituted_reference["metadata"]["normalization_profile"] = (
            self.substitute_profile())
        substituted_ratchet = json.loads(json.dumps(ratchet))
        substituted_ratchet["reference_sha256"] = MANIFEST.manifest_sha256(
            substituted_reference)
        return substituted_reference, substituted_ratchet

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
            "a" * 64, self.metadata(architecture="arm64"))
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
            "a" * 64, self.metadata(architecture="arm64"))
        ratchet = MANIFEST.build_ratchet(reference, "v0.2.0")
        MANIFEST.validate_chain(reference, ratchet)
        self.assertEqual(MANIFEST.current_finding_ids(ratchet),
                         [finding["identity"]])

    def test_git_remote_forms_have_one_canonical_repository_identity(self):
        forms = (
            "https://User:token@GitHub.com/Example/Repo.git",
            "ssh://git@github.com/Example/Repo.git",
            "git@GITHUB.COM:Example/Repo.git",
        )
        self.assertEqual(
            {MANIFEST.canonical_git_repository(value) for value in forms},
            {"github.com/Example/Repo"})

    def test_reference_and_chain_reject_independently_rehashed_root_substitution(self):
        reference, ratchet = self.make_chain()
        substituted_reference, substituted_ratchet = (
            self.substitute_chain_and_rehash(reference, ratchet))

        with self.assertRaisesRegex(ValueError, "approved G0 source-root set"):
            MANIFEST.validate_reference(substituted_reference)
        with self.assertRaisesRegex(ValueError, "approved G0 source-root set"):
            MANIFEST.validate_chain(substituted_reference, substituted_ratchet)

    def test_profile_rejects_missing_remote_and_descriptor_collision(self):
        no_remote = self.root / "no-remote"
        no_remote.mkdir()
        subprocess.run(
            ["git", "init", "-q", str(no_remote)], check=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        with self.assertRaisesRegex(ValueError, "canonical remote.origin.url"):
            MANIFEST.build_normalization_profile([no_remote])

        root = self.make_git_root(
            "collision", "https://github.com/example/collision.git")
        with self.assertRaisesRegex(ValueError, "descriptors collide"):
            MANIFEST.build_normalization_profile([root, root])

    def test_reference_chain_rejects_missing_normalization_profile(self):
        with self.assertRaisesRegex(ValueError, "normalization profile"):
            MANIFEST.build_reference(
                [], MANIFEST.BOUNDARY_COMMIT, "a" * 64,
                {"architecture": "arm64"})

    def test_release_validator_rejects_missing_or_tampered_profile(self):
        reference, ratchet = self.make_chain()
        reference_path = self.root / "release-reference.json"
        ratchet_path = self.root / "release-ratchet.json"
        reference_path.write_text(json.dumps(reference))
        ratchet_path.write_text(json.dumps(ratchet))

        def validate():
            return subprocess.run([
                "bash", str(RELEASE_TEST_PATH), "--validate-manifests",
                str(reference_path), str(ratchet_path),
            ], cwd=ROOT, env=dict(os.environ, PYTHONDONTWRITEBYTECODE="1"),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

        self.assertEqual(validate().returncode, 0)
        missing = json.loads(json.dumps(reference))
        missing["metadata"].pop("normalization_profile")
        reference_path.write_text(json.dumps(missing))
        self.assertNotEqual(validate().returncode, 0)
        tampered = json.loads(json.dumps(reference))
        tampered["metadata"]["normalization_profile"]["source_roots"][0][
            "subpath"] = "tampered"
        reference_path.write_text(json.dumps(tampered))
        self.assertNotEqual(validate().returncode, 0)
        substituted_reference, substituted_ratchet = (
            self.substitute_chain_and_rehash(reference, ratchet))
        reference_path.write_text(json.dumps(substituted_reference))
        ratchet_path.write_text(json.dumps(substituted_ratchet))
        self.assertNotEqual(validate().returncode, 0)

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
                "a" * 64, self.metadata())

    def test_reference_sorts_ids_and_rejects_duplicates(self):
        first = AUDIT.make_finding(
            "Contents/MacOS/main", "external_dependency", "/z/lib.dylib", [])
        second = AUDIT.make_finding(
            "Contents/MacOS/main", "external_dependency", "/a/lib.dylib", [])
        reference = MANIFEST.build_reference(
            [first, second], "0e91c7c4b121d80b929d595ea711d3dd0833ee67",
            "a" * 64, self.metadata())
        self.assertEqual(reference["finding_ids"],
                         sorted([first["identity"], second["identity"]]))
        with self.assertRaisesRegex(ValueError, "unique"):
            MANIFEST.build_reference(
                [first, first], "0e91c7c4b121d80b929d595ea711d3dd0833ee67",
                "a" * 64, self.metadata())

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

    def test_generator_records_versioned_source_root_normalization_profile(self):
        generator = load_module("generate_g0_reference_profile", GENERATOR_PATH)
        first = self.make_git_root(
            "generator-first", "https://github.com/anloren/osgverse.git")
        second = self.make_git_root(
            "generator-second", "git@github.com:anloren/osgsol.git")
        with mock.patch.object(
                generator, "verify_boundary_tags"), mock.patch.object(
                    generator.AUDIT, "audit_bundle", return_value={
                        "schema_version": 3,
                        "graph": {"Contents/MacOS/main": []},
                        "findings": [],
                    }), mock.patch.object(
                        generator.MANIFEST, "bundle_fingerprint",
                        return_value="a" * 64), mock.patch.object(
                            generator, "write_manifest_pair") as write_pair:
            result = generator.main([
                "--app", str(self.root / "Protected.app"),
                "--reference", str(self.root / "reference.json"),
                "--ratchet", str(self.root / "ratchet.json"),
                "--source-root", str(first),
                "--source-root", str(second),
            ])

        self.assertEqual(result, 0)
        reference = write_pair.call_args.args[1]
        descriptors = [
            {"repository": "github.com/anloren/osgsol", "subpath": "."},
            {"repository": "github.com/anloren/osgverse", "subpath": "."},
        ]
        self.assertEqual(reference["metadata"]["normalization_profile"], {
            "schema": "scienceearth-g0-source-root-normalization",
            "version": 2,
            "source_root_count": 2,
            "source_roots": descriptors,
            "source_roots_sha256": MANIFEST.manifest_sha256(descriptors),
        })

    def test_generator_rejects_substituted_source_roots_before_audit(self):
        generator = load_module("generate_g0_reference_substitution", GENERATOR_PATH)
        first = self.make_git_root(
            "generator-substitute-first",
            "https://github.com/example/unrelated-one.git")
        second = self.make_git_root(
            "generator-substitute-second",
            "git@github.com:example/unrelated-two.git")
        with mock.patch.object(generator, "verify_boundary_tags"), \
                mock.patch.object(generator.AUDIT, "audit_bundle") as audit_bundle, \
                mock.patch.object(generator, "write_manifest_pair") as write_pair:
            with self.assertRaisesRegex(ValueError, "approved G0 source-root set"):
                generator.main([
                    "--app", str(self.root / "Protected.app"),
                    "--reference", str(self.root / "reference.json"),
                    "--ratchet", str(self.root / "ratchet.json"),
                    "--source-root", str(first),
                    "--source-root", str(second),
                ])
        audit_bundle.assert_not_called()
        write_pair.assert_not_called()

    def test_generator_normalizes_unconfigured_home_subjects_across_usernames(self):
        generator = load_module("generate_g0_reference_home", GENERATOR_PATH)
        filtered = []
        for username in ("USER", "another-user"):
            finding = AUDIT.make_finding(
                "Contents/MacOS/main", "external_dependency",
                f"/Users/{username}/local/libexample.dylib", [ROOT])
            result = {
                "graph": {"Contents/MacOS/main": []},
                "findings": [finding],
            }
            filtered.append(generator.non_science_findings(result)[0])

        self.assertEqual(filtered[0]["subject"],
                         "${HOME}/local/libexample.dylib")
        self.assertEqual(filtered[0]["identity"], filtered[1]["identity"])

    def test_generator_normalizes_embedded_and_repeated_home_paths(self):
        generator = load_module("generate_g0_reference_embedded_home", GENERATOR_PATH)
        subject = (
            "@rpath/libexample.dylib -> /Users/USER/local/libexample.dylib; "
            "fallback=/Users/USER/alternate/libexample.dylib")
        finding = AUDIT.make_finding(
            "Contents/MacOS/main", "external_dependency", subject, [ROOT])
        result = {
            "graph": {"Contents/MacOS/main": []},
            "findings": [finding],
        }
        filtered = generator.non_science_findings(result)
        reference = MANIFEST.build_reference(
            filtered, MANIFEST.BOUNDARY_COMMIT, "a" * 64, self.metadata())
        ratchet = MANIFEST.build_ratchet(reference, "v0.2.0")
        reference_path = self.root / "reference.json"
        ratchet_path = self.root / "ratchet.json"

        generator.write_manifest_pair(
            reference_path, reference, ratchet_path, ratchet)

        written_reference = json.loads(reference_path.read_text())
        written_ratchet = json.loads(ratchet_path.read_text())
        self.assertEqual(
            written_reference["findings"][0]["subject"],
            "@rpath/libexample.dylib -> ${HOME}/local/libexample.dylib; "
            "fallback=${HOME}/alternate/libexample.dylib")
        self.assertNotIn("/Users/USER", reference_path.read_text())
        MANIFEST.validate_chain(written_reference, written_ratchet)

    def test_generator_normalizes_repeated_leading_slashes_at_path_boundaries(self):
        generator = load_module("generate_g0_reference_repeated_slashes", GENERATOR_PATH)
        subject = (
            "mac=//Users/USER/a mac2=///Users/USER/b "
            "linux=////home/bob/c root=//root/d varroot=///var/root/e "
            "url=https://Users/USER/f nonpath=prefix//Users/USER/g")

        self.assertEqual(
            generator.normalize_home_subject(subject),
            "mac=${HOME}/a mac2=${HOME}/b linux=${HOME}/c "
            "root=${HOME}/d varroot=${HOME}/e "
            "url=https://Users/USER/f nonpath=prefix//Users/USER/g")

    def test_home_normalization_is_path_boundary_aware_for_all_platforms(self):
        generator = load_module("generate_g0_reference_home_boundaries", GENERATOR_PATH)
        subject = (
            "mac=/Users/USER/a linux=/home/bob/b root=/root/c "
            "varroot=/var/root/d keep=prefix/Users/USER/e keep2=/rooted/f")

        self.assertEqual(
            generator.normalize_home_subject(subject),
            "mac=${HOME}/a linux=${HOME}/b root=${HOME}/c "
            "varroot=${HOME}/d keep=prefix/Users/USER/e keep2=/rooted/f")

    def test_file_uri_home_normalization_is_scheme_aware_and_rehashes_identity(self):
        generator = load_module("generate_g0_reference_file_uri", GENERATOR_PATH)
        subject = (
            "mac=file:///Users/USER/a linux=file:///home/alice/b "
            "root=file:///root/c linux_short=file:/home/bob/d "
            "varroot=file://var/root/e "
            "network=https://Users/USER/e")
        finding = AUDIT.make_finding(
            "Contents/MacOS/main", "forbidden_string", subject, [])
        filtered = generator.non_science_findings({
            "graph": {"Contents/MacOS/main": []},
            "findings": [finding],
        })[0]
        expected_subject = (
            "mac=file://${HOME}/a linux=file://${HOME}/b "
            "root=file://${HOME}/c linux_short=file://${HOME}/d "
            "varroot=file://${HOME}/e "
            "network=https://Users/USER/e")

        self.assertEqual(filtered["subject"], expected_subject)
        self.assertEqual(
            filtered["identity"],
            AUDIT.finding_identity(
                finding["owner"], finding["category"], expected_subject))
        self.assertNotEqual(filtered["identity"], finding["identity"])

    def test_pair_rejects_any_remaining_machine_home_literal(self):
        generator = load_module("generate_g0_reference_home_guard", GENERATOR_PATH)
        reference, _ = self.make_chain()
        reference["metadata"]["build_log"] = "built in /Users/USER/work"
        ratchet = MANIFEST.build_ratchet(reference, "v0.2.0")
        reference_path = self.root / "reference.json"
        ratchet_path = self.root / "ratchet.json"

        with self.assertRaisesRegex(ValueError, "machine-home"):
            generator.write_manifest_pair(
                reference_path, reference, ratchet_path, ratchet)
        self.assertFalse(reference_path.exists())
        self.assertFalse(ratchet_path.exists())

    def test_pair_rejects_repeated_leading_slash_machine_home_literal(self):
        generator = load_module("generate_g0_reference_repeated_home_guard", GENERATOR_PATH)
        reference, _ = self.make_chain()
        reference["metadata"]["build_log"] = (
            "built in //Users/USER/work then copied from ///home/alice/work")
        ratchet = MANIFEST.build_ratchet(reference, "v0.2.0")
        reference_path = self.root / "reference.json"
        ratchet_path = self.root / "ratchet.json"

        with self.assertRaisesRegex(ValueError, "machine-home"):
            generator.write_manifest_pair(
                reference_path, reference, ratchet_path, ratchet)
        self.assertFalse(reference_path.exists())
        self.assertFalse(ratchet_path.exists())

    def test_pair_rejects_file_uri_machine_home_literals(self):
        generator = load_module("generate_g0_reference_file_home_guard", GENERATOR_PATH)
        for index, literal in enumerate((
                "file:///Users/USER/work",
                "file:///home/alice/work",
                "file:///root/work",
                "file:/home/alice/work",
                "file:////var/root/work")):
            with self.subTest(literal=literal):
                reference, _ = self.make_chain()
                reference["metadata"]["build_log"] = literal
                ratchet = MANIFEST.build_ratchet(reference, "v0.2.0")
                reference_path = self.root / f"reference-{index}.json"
                ratchet_path = self.root / f"ratchet-{index}.json"

                with self.assertRaisesRegex(ValueError, "machine-home"):
                    generator.write_manifest_pair(
                        reference_path, reference, ratchet_path, ratchet)
                self.assertFalse(reference_path.exists())
                self.assertFalse(ratchet_path.exists())

    def test_pair_rejects_parent_directory_alias_without_publication(self):
        generator = load_module("generate_g0_reference_parent_alias", GENERATOR_PATH)
        reference, ratchet = self.make_chain()
        output = self.root / "out" / "manifest.json"
        alias = self.root / "out" / "sub" / ".." / "manifest.json"

        with self.assertRaisesRegex(ValueError, "must be different"):
            generator.write_manifest_pair(alias, reference, output, ratchet)
        self.assertFalse(output.exists())

    def test_pair_rejects_symlink_parent_alias_without_publication(self):
        generator = load_module("generate_g0_reference_symlink_alias", GENERATOR_PATH)
        reference, ratchet = self.make_chain()
        output_directory = self.root / "out"
        output_directory.mkdir()
        alias_directory = self.root / "alias"
        alias_directory.symlink_to(output_directory, target_is_directory=True)
        output = output_directory / "manifest.json"
        alias = alias_directory / "manifest.json"

        with self.assertRaisesRegex(ValueError, "must be different"):
            generator.write_manifest_pair(alias, reference, output, ratchet)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
