#!/usr/bin/env python3
"""Remove configured source-root literals from staged unsigned Mach-Os.

The compiler may retain ``__FILE__`` and compiled plugin search directories in
release binaries.  Formal packaging replaces only the configured canonical
source-root byte sequences with deterministic, equal-length opaque tokens.
Mach-O layout, file size, and executable mode are preserved.
"""

import argparse
import hashlib
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path


class RelocationError(RuntimeError):
    """The compiled-path relocation contract was not satisfied."""


MACHO_MAGICS = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
}
RELOCATION_MARKER = b"@src/"


def _has_valid_signature(path):
    result = subprocess.run(
        ["codesign", "--verify", "--strict", str(path)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False)
    return result.returncode == 0


def _prepare_roots(source_roots):
    roots = [Path(root).resolve() for root in source_roots]
    canonical = [str(root) for root in roots]
    if len(set(canonical)) != len(canonical):
        raise RelocationError("duplicate canonical source root")
    prepared = []
    for root in roots:
        value = os.fsencode(str(root))
        digest = hashlib.sha256(value).hexdigest()[:12].encode("ascii")
        token = RELOCATION_MARKER + digest
        if str(root) == "/" or len(value) < len(token):
            raise RelocationError(f"unsafe or too-short source root: {root}")
        replacement = token + (b"_" * (len(value) - len(token)))
        prepared.append((root, value, replacement))
    return sorted(prepared, key=lambda item: len(item[1]), reverse=True)


def _write_relocated(path, data, mode):
    temporary_name = None
    try:
        with tempfile.NamedTemporaryFile(
                mode="wb", dir=path.parent, prefix=f".{path.name}.",
                delete=False) as temporary:
            temporary_name = temporary.name
            temporary.write(data)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_name, mode)
        os.replace(temporary_name, path)
        temporary_name = None
    finally:
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass


def relocate_tree(binary_root, source_roots):
    """Relocate source roots in every regular Mach-O below ``binary_root``."""
    binary_root = Path(binary_root)
    try:
        root_metadata = binary_root.lstat()
    except FileNotFoundError as error:
        raise RelocationError(f"missing binary root: {binary_root}") from error
    if stat.S_ISLNK(root_metadata.st_mode) or not stat.S_ISDIR(root_metadata.st_mode):
        raise RelocationError(f"binary root must be a real directory: {binary_root}")

    prepared = _prepare_roots(source_roots)
    counts = {str(root): 0 for root, _, _ in prepared}
    pending = []

    for path in sorted(binary_root.rglob("*")):
        if path.is_symlink() or not path.is_file():
            continue
        metadata = path.stat()
        data = path.read_bytes()
        if len(data) < 4 or data[:4] not in MACHO_MAGICS:
            continue
        if RELOCATION_MARKER in data:
            raise RelocationError(f"Mach-O is already relocated: {path}")

        relocated = data
        matched = False
        for root, source, replacement in prepared:
            occurrence_count = relocated.count(source)
            counts[str(root)] += occurrence_count
            if occurrence_count:
                matched = True
                relocated = relocated.replace(source, replacement)

        if not matched:
            continue
        if _has_valid_signature(path):
            raise RelocationError(f"refusing to modify a validly signed binary: {path}")
        if len(relocated) != len(data):
            raise RelocationError(f"compiled-path relocation changed size: {path}")
        for _, source, _ in prepared:
            if source in relocated:
                raise RelocationError(f"source root remains after relocation: {path}")
        pending.append((path, relocated, stat.S_IMODE(metadata.st_mode)))

    for path, data, mode in pending:
        _write_relocated(path, data, mode)

    return counts


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-root", type=Path, required=True)
    parser.add_argument("--source-root", action="append", type=Path,
                        required=True)
    arguments = parser.parse_args(argv)
    try:
        counts = relocate_tree(arguments.binary_root, arguments.source_root)
    except RelocationError as error:
        print(f"[error] {error}", file=sys.stderr)
        return 1
    for root, count in sorted(counts.items()):
        print(f"[compiled-paths] {root}: {count} replacements")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
