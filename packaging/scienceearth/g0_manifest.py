"""Immutable G0 reference and release-ratchet manifest primitives."""

import copy
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
from pathlib import PurePosixPath
from urllib.parse import urlsplit


BOUNDARY_COMMIT = "0e91c7c4b121d80b929d595ea711d3dd0833ee67"
MANIFEST_SCHEMA_VERSION = 1
FINDING_SCHEMA_VERSION = 1
NORMALIZATION_PROFILE_SCHEMA = "scienceearth-g0-source-root-normalization"
NORMALIZATION_PROFILE_VERSION = 2
EXPECTED_G0_SOURCE_ROOT_DESCRIPTOR_ITEMS = (
    ("github.com/anloren/osgsol", "."),
    ("github.com/anloren/osgverse", "."),
)
EXPECTED_G0_SOURCE_ROOTS_SHA256 = (
    "646b5eb80be60524ca6aa8dad55921f2966cc40562b29489483f1184a04c1936")
LOWER_HEX_DIGITS = frozenset("0123456789abcdef")
SCP_REMOTE_PATTERN = re.compile(
    r"^(?:[^@/:]+@)?(?P<host>[^/:]+):(?P<path>.+)$")


def canonical_bytes(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def manifest_sha256(value):
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def expected_source_root_descriptors():
    descriptors = [
        {"repository": repository, "subpath": subpath}
        for repository, subpath in EXPECTED_G0_SOURCE_ROOT_DESCRIPTOR_ITEMS
    ]
    if manifest_sha256(descriptors) != EXPECTED_G0_SOURCE_ROOTS_SHA256:
        raise RuntimeError("immutable G0 source-root descriptor hash is inconsistent")
    return descriptors


def expected_normalization_profile():
    descriptors = expected_source_root_descriptors()
    return {
        "schema": NORMALIZATION_PROFILE_SCHEMA,
        "version": NORMALIZATION_PROFILE_VERSION,
        "source_root_count": len(descriptors),
        "source_roots": descriptors,
        "source_roots_sha256": EXPECTED_G0_SOURCE_ROOTS_SHA256,
    }


def _validate_sha256(value, label):
    if (not isinstance(value, str) or len(value) != 64 or
            any(character not in LOWER_HEX_DIGITS for character in value)):
        raise ValueError(f"{label} must contain exactly 64 lowercase hex characters")


def _validate_release_tag(release_tag):
    if not isinstance(release_tag, str) or not release_tag:
        raise ValueError("release tag must be a non-empty string")


def _validate_source_root_count(value, label):
    if type(value) is not int or value < 1:
        raise ValueError(f"{label} must be a positive integer")


def canonical_git_repository(remote):
    if not isinstance(remote, str) or not remote.strip():
        raise ValueError("Git remote must be a non-empty string")
    value = remote.strip()
    if "://" in value:
        parsed = urlsplit(value)
        if parsed.scheme.lower() not in {"git", "http", "https", "ssh"}:
            raise ValueError("Git remote scheme is not canonicalizable")
        host = parsed.hostname
        path = parsed.path
    else:
        match = SCP_REMOTE_PATTERN.fullmatch(value)
        if not match:
            raise ValueError("Git remote is not a canonical network remote")
        host = match.group("host")
        path = match.group("path")
    if not host or not path or "\\" in path:
        raise ValueError("Git remote is missing a canonical host or path")
    parts = []
    for part in path.split("/"):
        if not part or part == ".":
            continue
        if part == "..":
            raise ValueError("Git remote path cannot contain parent traversal")
        parts.append(part)
    if not parts:
        raise ValueError("Git remote is missing a repository path")
    if parts[-1].endswith(".git"):
        parts[-1] = parts[-1][:-4]
    if not parts[-1]:
        raise ValueError("Git remote is missing a repository name")
    return f"{host.lower()}/{'/'.join(parts)}"


def _run_git(root, *arguments):
    try:
        return subprocess.run(
            ["git", "-C", str(root), *arguments], check=True, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout.strip()
    except (OSError, subprocess.CalledProcessError) as error:
        raise ValueError(
            f"source root is not a usable Git worktree: {root}") from error


def source_root_descriptor(root):
    root = Path(root).resolve()
    if not root.is_dir():
        raise ValueError(f"source root is not an existing directory: {root}")
    top_level = Path(_run_git(root, "rev-parse", "--show-toplevel")).resolve()
    try:
        relative = root.relative_to(top_level)
    except ValueError as error:
        raise ValueError(
            f"source root is outside its Git worktree: {root}") from error
    try:
        remote = _run_git(top_level, "config", "--get", "remote.origin.url")
        repository = canonical_git_repository(remote)
    except ValueError as error:
        raise ValueError(
            f"source root has no canonical remote.origin.url: {root}") from error
    return {
        "repository": repository,
        "subpath": relative.as_posix() if relative.parts else ".",
    }


def source_root_descriptors(source_roots):
    roots = list(source_roots)
    _validate_source_root_count(len(roots), "normalization profile source_root_count")
    descriptors = sorted(
        (source_root_descriptor(root) for root in roots), key=canonical_bytes)
    encoded = [canonical_bytes(descriptor) for descriptor in descriptors]
    if len(set(encoded)) != len(encoded):
        raise ValueError("normalization profile source-root descriptors collide")
    return descriptors


def build_normalization_profile(source_roots):
    descriptors = source_root_descriptors(source_roots)
    return {
        "schema": NORMALIZATION_PROFILE_SCHEMA,
        "version": NORMALIZATION_PROFILE_VERSION,
        "source_root_count": len(descriptors),
        "source_roots": descriptors,
        "source_roots_sha256": manifest_sha256(descriptors),
    }


def validate_source_roots(source_roots):
    profile = build_normalization_profile(source_roots)
    if profile != expected_normalization_profile():
        raise ValueError(
            "normalization profile does not match the approved G0 source-root set")
    return profile


def _validate_repository_descriptor(value):
    if not isinstance(value, str) or "/" not in value or value.startswith("/"):
        raise ValueError("normalization profile repository is malformed")
    host, path = value.split("/", 1)
    if (not host or host != host.lower() or not path or "@" in host or
            ":" in host or path.endswith(".git")):
        raise ValueError("normalization profile repository is not canonical")
    if any(part in {"", ".", ".."} for part in path.split("/")):
        raise ValueError("normalization profile repository path is malformed")


def _validate_subpath(value):
    if not isinstance(value, str) or not value:
        raise ValueError("normalization profile source-root subpath is malformed")
    if value == ".":
        return
    path = PurePosixPath(value)
    if path.is_absolute() or path.as_posix() != value or ".." in path.parts:
        raise ValueError("normalization profile source-root subpath is not canonical")


def validate_normalization_profile(reference, expected_source_roots=None):
    metadata = reference.get("metadata") if isinstance(reference, dict) else None
    profile = metadata.get("normalization_profile") if isinstance(metadata, dict) else None
    if not isinstance(profile, dict):
        raise ValueError("normalization profile is missing or malformed")
    expected_fields = {
        "schema", "version", "source_root_count", "source_roots",
        "source_roots_sha256",
    }
    if set(profile) != expected_fields:
        raise ValueError("normalization profile fields are malformed")
    if profile["schema"] != NORMALIZATION_PROFILE_SCHEMA:
        raise ValueError("normalization profile schema does not match")
    if profile["version"] != NORMALIZATION_PROFILE_VERSION:
        raise ValueError("normalization profile version does not match")
    _validate_source_root_count(
        profile["source_root_count"], "normalization profile source_root_count")
    descriptors = profile["source_roots"]
    if not isinstance(descriptors, list):
        raise ValueError("normalization profile source_roots must be an array")
    if len(descriptors) != profile["source_root_count"]:
        raise ValueError("normalization profile source-root count does not match descriptors")
    for descriptor in descriptors:
        if not isinstance(descriptor, dict) or set(descriptor) != {"repository", "subpath"}:
            raise ValueError("normalization profile source-root descriptor is malformed")
        _validate_repository_descriptor(descriptor["repository"])
        _validate_subpath(descriptor["subpath"])
    if descriptors != sorted(descriptors, key=canonical_bytes):
        raise ValueError("normalization profile source-root descriptors are not sorted")
    encoded = [canonical_bytes(descriptor) for descriptor in descriptors]
    if len(set(encoded)) != len(encoded):
        raise ValueError("normalization profile source-root descriptors collide")
    _validate_sha256(
        profile["source_roots_sha256"], "normalization profile source-root hash")
    if profile["source_roots_sha256"] != manifest_sha256(descriptors):
        raise ValueError("normalization profile source-root hash does not match descriptors")
    if profile != expected_normalization_profile():
        raise ValueError(
            "normalization profile does not match the approved G0 source-root set")
    if expected_source_roots is not None:
        expected = build_normalization_profile(expected_source_roots)
        if profile != expected:
            raise ValueError(
                "normalization profile source-root set does not match audit inputs")


def validate_reference(reference):
    if not isinstance(reference, dict):
        raise ValueError("reference manifest must be an object")
    if reference.get("schema_version") != MANIFEST_SCHEMA_VERSION:
        raise ValueError("reference manifest schema version does not match")
    if reference.get("kind") != "reference":
        raise ValueError("reference manifest kind does not match")
    if reference.get("source_commit") != BOUNDARY_COMMIT:
        raise ValueError("reference source commit is not the immutable boundary")
    _validate_sha256(reference.get("bundle_fingerprint"), "bundle fingerprint")
    if not isinstance(reference.get("metadata"), dict):
        raise ValueError("reference metadata must be an object")
    validate_normalization_profile(reference)

    findings = reference.get("findings")
    finding_ids = reference.get("finding_ids")
    if not isinstance(findings, list) or not isinstance(finding_ids, list):
        raise ValueError("reference findings and finding_ids must be arrays")
    identities = []
    for finding in findings:
        if not isinstance(finding, dict):
            raise ValueError("reference findings must contain objects")
        if finding.get("schema_version") != FINDING_SCHEMA_VERSION:
            raise ValueError("reference finding schema version does not match")
        for field in ("owner", "category", "subject"):
            if not isinstance(finding.get(field), str):
                raise ValueError(f"reference finding {field} must be a string")
        identity = finding.get("identity")
        _validate_sha256(identity, "finding identity")
        expected_identity = hashlib.sha256(canonical_bytes({
            "owner": finding["owner"],
            "category": finding["category"],
            "subject": finding["subject"],
        })).hexdigest()
        if identity != expected_identity:
            raise ValueError("reference finding identity does not match its fields")
        identities.append(identity)
    for identity in finding_ids:
        _validate_sha256(identity, "finding identity")
    if len(set(finding_ids)) != len(finding_ids):
        raise ValueError("reference finding identities must be unique")
    if finding_ids != sorted(finding_ids):
        raise ValueError("reference finding identities must be sorted")
    if identities != finding_ids:
        raise ValueError("reference findings do not match finding_ids")


def build_reference(findings, source_commit, fingerprint, metadata):
    if source_commit != BOUNDARY_COMMIT:
        raise ValueError("reference source commit is not the immutable boundary")
    _validate_sha256(fingerprint, "bundle fingerprint")
    items = sorted(copy.deepcopy(list(findings)), key=lambda item: item["identity"])
    value = {
        "schema_version": MANIFEST_SCHEMA_VERSION,
        "kind": "reference",
        "source_commit": source_commit,
        "bundle_fingerprint": fingerprint,
        "metadata": dict(sorted(metadata.items())),
        "findings": items,
        "finding_ids": [item["identity"] for item in items],
    }
    validate_reference(value)
    return value


def build_ratchet(reference, release_tag):
    validate_reference(reference)
    _validate_release_tag(release_tag)
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
    _validate_release_tag(release_tag)
    previous = current_finding_ids(value)
    accepted = sorted(set(accepted_ids))
    for identity in accepted:
        _validate_sha256(identity, "finding identity")
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
    if not isinstance(ratchet, dict):
        raise ValueError("ratchet manifest must be an object")
    if ratchet.get("schema_version") != MANIFEST_SCHEMA_VERSION:
        raise ValueError("ratchet manifest schema version does not match")
    if ratchet.get("kind") != "ratchet":
        raise ValueError("ratchet manifest kind does not match")
    if ratchet.get("boundary_commit") != BOUNDARY_COMMIT:
        raise ValueError("ratchet boundary commit is not immutable")
    _validate_sha256(ratchet.get("reference_sha256"), "ratchet reference hash")
    if ratchet.get("reference_sha256") != manifest_sha256(reference):
        raise ValueError("ratchet parent does not match reference")

    releases = ratchet.get("releases")
    if not isinstance(releases, list) or not releases:
        raise ValueError("ratchet has no releases")
    previous = set(reference["finding_ids"])
    previous_ids = list(reference["finding_ids"])
    for release in releases:
        if not isinstance(release, dict):
            raise ValueError("ratchet releases must contain objects")
        _validate_release_tag(release.get("release_tag"))
        parent_hash = release.get("parent_finding_ids_sha256")
        _validate_sha256(parent_hash, "ratchet release parent hash")
        if parent_hash != manifest_sha256(previous_ids):
            raise ValueError("ratchet release parent does not match")
        finding_ids = release.get("finding_ids")
        if not isinstance(finding_ids, list):
            raise ValueError("ratchet release finding_ids must be an array")
        for identity in finding_ids:
            _validate_sha256(identity, "finding identity")
        current_ids = sorted(set(finding_ids))
        if not set(current_ids).issubset(previous):
            raise ValueError("ratchet finding set is not a subset of its parent")
        if finding_ids != current_ids:
            raise ValueError("ratchet finding identities must be sorted and unique")
        previous = set(current_ids)
        previous_ids = current_ids


def _file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def bundle_fingerprint(app):
    app = Path(app)
    if not app.is_dir():
        raise ValueError(f"bundle is not an existing directory: {app}")
    entries = []
    paths = sorted(app.rglob("*"), key=lambda path: path.relative_to(app).as_posix())
    for path in paths:
        details = path.lstat()
        entries.append({
            "path": path.relative_to(app).as_posix(),
            "mode": details.st_mode,
            "symlink_target": os.readlink(path) if path.is_symlink() else None,
            "size": details.st_size,
            "file_sha256": (
                _file_sha256(path) if path.is_file() and not path.is_symlink() else None),
        })
    return manifest_sha256(entries)
