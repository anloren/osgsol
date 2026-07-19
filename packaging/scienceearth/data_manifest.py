#!/usr/bin/env python3
"""Write and validate the canonical ScienceEarth package-data manifest."""

import argparse
import hashlib
import json
import re
import stat
from pathlib import Path, PurePosixPath


SCHEMA = "osgsol-science-data-v1"
MANIFEST_RELATIVE = PurePosixPath(
    "Contents/misc/science/data-manifest.json")
TOP_LEVEL_KEYS = frozenset({"entries", "schema"})
ENTRY_KEYS = frozenset({
    "bytes", "path", "role", "sha256", "source_id",
})
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")
IDENTIFIER_PATTERN = re.compile(r"^[a-z0-9][a-z0-9._-]*$")
MACHO_MAGICS = frozenset({
    bytes.fromhex("feedface"),
    bytes.fromhex("feedfacf"),
    bytes.fromhex("cefaedfe"),
    bytes.fromhex("cffaedfe"),
    bytes.fromhex("cafebabe"),
    bytes.fromhex("cafebabf"),
    bytes.fromhex("bebafeca"),
    bytes.fromhex("bfbafeca"),
})


def canonical_bytes(value):
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) +
            "\n").encode("utf-8")


def sha256_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _checked_relative_path(value):
    if not isinstance(value, str) or not value:
        raise ValueError("science data path must be a non-empty string")
    if "\\" in value:
        raise ValueError("science data path must use POSIX separators")
    raw_parts = value.split("/")
    if any(part in ("", ".", "..") for part in raw_parts):
        raise ValueError("science data path contains traversal or empty segments")
    relative = PurePosixPath(value)
    if relative.is_absolute():
        raise ValueError("science data path must be relative to the app")
    if relative.parts[:3] != ("Contents", "misc", "science"):
        raise ValueError("science data path is outside Contents/misc/science")
    if relative == MANIFEST_RELATIVE:
        raise ValueError("science data manifest cannot declare itself as data")
    return relative


def _regular_file_without_symlinks(app, relative):
    current = app
    for component in relative.parts:
        current = current / component
        try:
            mode = current.lstat().st_mode
        except FileNotFoundError as error:
            raise ValueError(f"science data file is missing: {relative}") from error
        if stat.S_ISLNK(mode):
            raise ValueError(f"science data path contains a symlink: {relative}")
    if not stat.S_ISREG(current.lstat().st_mode):
        raise ValueError(f"science data entry is not a regular file: {relative}")
    try:
        current.relative_to(app)
    except ValueError as error:
        raise ValueError(f"science data path escapes app: {relative}") from error
    return current


def _validate_identifier(value, label):
    if not isinstance(value, str) or not IDENTIFIER_PATTERN.fullmatch(value):
        raise ValueError(f"{label} must be a stable lowercase identifier")


def _validate_entry(app, entry):
    if not isinstance(entry, dict) or set(entry) != ENTRY_KEYS:
        raise ValueError("science data entry has missing or unknown keys")
    byte_count = entry["bytes"]
    if isinstance(byte_count, bool) or not isinstance(byte_count, int):
        raise ValueError("science data byte count must be an integer")
    if byte_count < 0:
        raise ValueError("science data byte count cannot be negative")
    if not isinstance(entry["sha256"], str) or not SHA256_PATTERN.fullmatch(
            entry["sha256"]):
        raise ValueError("science data SHA-256 must be lowercase hexadecimal")
    _validate_identifier(entry["role"], "science data role")
    _validate_identifier(entry["source_id"], "science data source id")
    relative = _checked_relative_path(entry["path"])
    payload = _regular_file_without_symlinks(app, relative)
    actual_bytes = payload.stat().st_size
    if actual_bytes != byte_count:
        raise ValueError(
            f"science data byte count mismatch for {relative}: "
            f"expected {byte_count}, found {actual_bytes}")
    with payload.open("rb") as stream:
        if stream.read(4) in MACHO_MAGICS:
            raise ValueError(f"science data entry is Mach-O: {relative}")
    actual_hash = sha256_file(payload)
    if actual_hash != entry["sha256"]:
        raise ValueError(f"science data SHA-256 mismatch for {relative}")
    return dict(entry)


def validate(app):
    app = Path(app).resolve()
    if not app.is_dir():
        raise ValueError("science data app must be an existing directory")
    manifest = app / Path(*MANIFEST_RELATIVE.parts)
    try:
        manifest_mode = manifest.lstat().st_mode
    except FileNotFoundError as error:
        raise ValueError("science data manifest is missing") from error
    if stat.S_ISLNK(manifest_mode) or not stat.S_ISREG(manifest_mode):
        raise ValueError("science data manifest must be a regular non-symlink file")
    encoded = manifest.read_bytes()
    try:
        document = json.loads(encoded.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("science data manifest is not valid UTF-8 JSON") from error
    if not isinstance(document, dict) or set(document) != TOP_LEVEL_KEYS:
        raise ValueError("science data manifest has missing or unknown keys")
    if document["schema"] != SCHEMA:
        raise ValueError("science data manifest schema is unsupported")
    entries = document["entries"]
    if not isinstance(entries, list) or not entries:
        raise ValueError("science data manifest entries must be a non-empty list")
    if encoded != canonical_bytes(document):
        raise ValueError("science data manifest is not canonical JSON")
    validated = []
    seen = set()
    for entry in entries:
        checked = _validate_entry(app, entry)
        if checked["path"] in seen:
            raise ValueError(
                f"duplicate science data path: {checked['path']}")
        seen.add(checked["path"])
        validated.append(checked)
    return {
        "schema": SCHEMA,
        "bytes": sum(entry["bytes"] for entry in validated),
        "entries": validated,
        "manifest_sha256": hashlib.sha256(encoded).hexdigest(),
    }


def write(app, alphaearth_index):
    app = Path(app).resolve()
    relative = _checked_relative_path(alphaearth_index)
    payload = _regular_file_without_symlinks(app, relative)
    document = {
        "entries": [{
            "bytes": payload.stat().st_size,
            "path": relative.as_posix(),
            "role": "spatial-index",
            "sha256": sha256_file(payload),
            "source_id": "alphaearth-foundations",
        }],
        "schema": SCHEMA,
    }
    manifest = app / Path(*MANIFEST_RELATIVE.parts)
    manifest.parent.mkdir(parents=True, exist_ok=True)
    manifest.write_bytes(canonical_bytes(document))
    return validate(app)


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--write", metavar="APP")
    parser.add_argument("--validate", metavar="APP")
    parser.add_argument("--alphaearth-index")
    arguments = parser.parse_args(argv)
    if bool(arguments.write) == bool(arguments.validate):
        parser.error("choose exactly one of --write or --validate")
    if arguments.write and not arguments.alphaearth_index:
        parser.error("--write requires --alphaearth-index")
    if arguments.validate and arguments.alphaearth_index:
        parser.error("--alphaearth-index is valid only with --write")
    return arguments


def main(argv=None):
    arguments = parse_arguments(argv)
    result = (write(arguments.write, arguments.alphaearth_index)
              if arguments.write else validate(arguments.validate))
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
