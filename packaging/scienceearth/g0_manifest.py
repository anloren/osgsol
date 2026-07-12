"""Immutable G0 reference and release-ratchet manifest primitives."""

import copy
import hashlib
import json
import os
from pathlib import Path


BOUNDARY_COMMIT = "0e91c7c4b121d80b929d595ea711d3dd0833ee67"
MANIFEST_SCHEMA_VERSION = 1
FINDING_SCHEMA_VERSION = 1
LOWER_HEX_DIGITS = frozenset("0123456789abcdef")


def canonical_bytes(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":")).encode("utf-8")


def manifest_sha256(value):
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def _validate_sha256(value, label):
    if (not isinstance(value, str) or len(value) != 64 or
            any(character not in LOWER_HEX_DIGITS for character in value)):
        raise ValueError(f"{label} must contain exactly 64 lowercase hex characters")


def _validate_release_tag(release_tag):
    if not isinstance(release_tag, str) or not release_tag:
        raise ValueError("release tag must be a non-empty string")


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
