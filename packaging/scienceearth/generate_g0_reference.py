#!/usr/bin/env python3
"""Generate the guarded immutable G0 reference and initial release ratchet."""

import argparse
import importlib.util
import json
import os
import platform
import re
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BOUNDARY_TAG = "ScienceEarth"
INITIAL_RELEASE_TAG = "v0.2.0"
MACHINE_HOME_PATTERN = re.compile(
    r"(?<![A-Za-z0-9_.${}~@%+\-/:])"
    r"/+(?:Users/[A-Za-z0-9._-]+|home/[A-Za-z0-9._-]+|var/root|root)"
    r"(?![A-Za-z0-9._-])")


def load_module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


AUDIT = load_module("scienceearth_audit_macos_bundle", ROOT / "packaging" /
                    "audit_macos_bundle.py")
MANIFEST = load_module("scienceearth_g0_manifest", Path(__file__).with_name(
    "g0_manifest.py"))


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
        _publish_temporary_json(Path(temporary), path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _publish_temporary_json(temporary, path):
    os.replace(temporary, path)
    try:
        os.chmod(path, 0o644)
    except BaseException:
        Path(path).unlink(missing_ok=True)
        raise


def _write_temporary_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", dir=path.parent)
    os.chmod(temporary, 0o600)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
        return Path(temporary)
    except BaseException:
        if os.path.exists(temporary):
            os.unlink(temporary)
        raise


def _validated_output_paths(reference_path, ratchet_path):
    reference_path = Path(reference_path).resolve(strict=False)
    ratchet_path = Path(ratchet_path).resolve(strict=False)
    if reference_path == ratchet_path:
        raise ValueError("reference and ratchet paths must be different")
    for path in (reference_path, ratchet_path):
        if path.exists():
            raise FileExistsError(f"refusing to overwrite manifest: {path}")
    return reference_path, ratchet_path


def write_manifest_pair(reference_path, reference, ratchet_path, ratchet):
    reference_path, ratchet_path = _validated_output_paths(
        reference_path, ratchet_path)

    temporary_reference = _write_temporary_json(reference_path, reference)
    temporary_ratchet = None
    published = []
    try:
        temporary_ratchet = _write_temporary_json(ratchet_path, ratchet)
        with temporary_reference.open(encoding="utf-8") as stream:
            written_reference = json.load(stream)
        with temporary_ratchet.open(encoding="utf-8") as stream:
            written_ratchet = json.load(stream)
        MANIFEST.validate_chain(written_reference, written_ratchet)
        _reject_machine_home_literals(written_reference, written_ratchet)
        for path in (reference_path, ratchet_path):
            if path.exists():
                raise FileExistsError(f"refusing to overwrite manifest: {path}")
        _publish_temporary_json(temporary_reference, reference_path)
        published.append(reference_path)
        _publish_temporary_json(temporary_ratchet, ratchet_path)
        published.append(ratchet_path)
    except BaseException:
        for path in published:
            if path.exists():
                path.unlink()
        raise
    finally:
        for temporary in (temporary_reference, temporary_ratchet):
            if temporary is not None and temporary.exists():
                temporary.unlink()


def resolve_tag(tag):
    return subprocess.run(
        ["git", "-C", str(ROOT), "rev-parse", f"{tag}^{{}}"],
        check=True, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE).stdout.strip()


def verify_boundary_tags(boundary_tag, release_tag):
    if boundary_tag != BOUNDARY_TAG:
        raise ValueError(f"boundary tag must be {BOUNDARY_TAG}")
    if release_tag != INITIAL_RELEASE_TAG:
        raise ValueError(f"initial release tag must be {INITIAL_RELEASE_TAG}")
    for tag in (BOUNDARY_TAG, INITIAL_RELEASE_TAG):
        if resolve_tag(tag) != MANIFEST.BOUNDARY_COMMIT:
            raise ValueError(
                f"tag {tag} does not resolve to the immutable G0 boundary")


def non_science_findings(result, science_plugin_name="osgdb_science.so"):
    graph = result["graph"]
    pending = [node for node in graph
               if Path(node).name == science_plugin_name]
    science_nodes = set()
    while pending:
        node = pending.pop()
        if node in science_nodes:
            continue
        science_nodes.add(node)
        pending.extend(
            edge["resolved"] for edge in graph.get(node, [])
            if edge.get("resolved"))
    findings = []
    for finding in result["findings"]:
        if finding["owner"] in science_nodes:
            continue
        subject = normalize_home_subject(finding["subject"])
        findings.append({
            "schema_version": finding["schema_version"],
            "owner": finding["owner"],
            "category": finding["category"],
            "subject": subject,
            "identity": AUDIT.finding_identity(
                finding["owner"], finding["category"], subject),
        })
    return findings


def normalize_home_subject(subject):
    return MACHINE_HOME_PATTERN.sub("${HOME}", str(subject))


def _reject_machine_home_literals(*manifests):
    serialized = json.dumps(manifests, sort_keys=True)
    match = MACHINE_HOME_PATTERN.search(serialized)
    if match:
        raise ValueError(
            f"manifest contains machine-home path literal: {match.group(0)}")


def normalized_generation_command(arguments):
    command = [
        "python3", "packaging/scienceearth/generate_g0_reference.py",
        "--app", "${APP}",
        "--reference", "${REFERENCE}",
        "--ratchet", "${RATCHET}",
        "--boundary-tag", arguments.boundary_tag,
        "--release-tag", arguments.release_tag,
    ]
    for index, _ in enumerate(arguments.source_root, start=1):
        command.extend(["--source-root", f"${{SOURCE_ROOT_{index}}}"])
    return command


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--ratchet", required=True)
    parser.add_argument("--boundary-tag", default=BOUNDARY_TAG)
    parser.add_argument("--release-tag", default=INITIAL_RELEASE_TAG)
    parser.add_argument("--source-root", action="append", default=[])
    return parser.parse_args(argv)


def main(argv=None):
    arguments = parse_arguments(argv)
    verify_boundary_tags(arguments.boundary_tag, arguments.release_tag)
    reference_path, ratchet_path = _validated_output_paths(
        arguments.reference, arguments.ratchet)

    app = Path(arguments.app).resolve()
    source_roots = arguments.source_root or [ROOT]
    result = AUDIT.audit_bundle(
        app=app,
        baseline=app,
        source_roots=source_roots,
        require_science_plugin=False,
    )
    reference = MANIFEST.build_reference(
        non_science_findings(result),
        MANIFEST.BOUNDARY_COMMIT,
        MANIFEST.bundle_fingerprint(app),
        {
            "architecture": platform.machine(),
            "audit_schema_version": result["schema_version"],
            "boundary_tag": arguments.boundary_tag,
            "generation_command": normalized_generation_command(arguments),
            "normalization_profile": MANIFEST.build_normalization_profile(source_roots),
            "release_tag": arguments.release_tag,
        },
    )
    ratchet = MANIFEST.build_ratchet(reference, arguments.release_tag)
    write_manifest_pair(reference_path, reference, ratchet_path, ratchet)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
