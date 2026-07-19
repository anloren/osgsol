#!/usr/bin/env python3
"""Relocate audited build-prefix strings in staged macOS runtime libraries.

This tool is deliberately narrow.  It only accepts the two runtime libraries
and exact occurrence counts audited for the formal macOS bundle.  Replacements
are byte-for-byte equal in length so Mach-O offsets and load commands cannot
move.  It must run after install_name_tool has invalidated upstream signatures
and before the formal package is signed.
"""

import argparse
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path


class RelocationError(RuntimeError):
    """The staged binary does not satisfy the relocation contract."""


REPLACEMENTS = {
    b"/opt/homebrew": b"@bundleprefix",
    b"/usr/local": b"@usr_local",
}

POLICIES = {
    "libfontconfig.1.dylib": {
        b"/opt/homebrew": 5,
        b"/usr/local": 1,
    },
    "libintl.8.dylib": {
        b"/opt/homebrew": 1,
        b"/usr/local": 0,
    },
    "osgdb_lua.so": {
        b"/opt/homebrew": 0,
        b"/usr/local": 6,
    },
}

MACHO_MAGICS = {
    b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
    b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
    b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca",
    b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca",
}


def _has_valid_signature(path):
    result = subprocess.run(
        ["codesign", "--verify", "--strict", str(path)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False)
    return result.returncode == 0


def relocate_binary(path):
    """Apply the fixed relocation policy to one staged unsigned Mach-O."""
    path = Path(path)
    try:
        metadata = path.lstat()
    except FileNotFoundError as error:
        raise RelocationError(f"missing runtime library: {path}") from error

    if stat.S_ISLNK(metadata.st_mode):
        raise RelocationError(f"runtime library must not be a symlink: {path}")
    if not stat.S_ISREG(metadata.st_mode):
        raise RelocationError(f"runtime library must be a regular file: {path}")

    data = path.read_bytes()
    if len(data) < 4 or data[:4] not in MACHO_MAGICS:
        raise RelocationError(f"runtime library is not a Mach-O binary: {path}")
    if _has_valid_signature(path):
        raise RelocationError(f"refusing to modify a validly signed binary: {path}")

    policy = POLICIES.get(path.name)
    if policy is None:
        raise RelocationError(f"runtime library is not allowlisted: {path.name}")

    for replacement in REPLACEMENTS.values():
        if replacement in data:
            raise RelocationError(f"runtime library is already relocated: {path}")

    for source, expected_count in policy.items():
        actual_count = data.count(source)
        if actual_count != expected_count:
            raise RelocationError(
                f"{path.name}: expected {expected_count} occurrences of "
                f"{source.decode()}, found {actual_count}")

    relocated = data
    for source, replacement in REPLACEMENTS.items():
        if len(source) != len(replacement):
            raise RelocationError("internal relocation tokens are not equal length")
        relocated = relocated.replace(source, replacement)

    if len(relocated) != len(data):
        raise RelocationError(f"relocation changed binary size: {path}")
    for source in REPLACEMENTS:
        if source in relocated:
            raise RelocationError(f"forbidden prefix remains after relocation: {path}")
    for source, expected_count in policy.items():
        replacement = REPLACEMENTS[source]
        if relocated.count(replacement) != expected_count:
            raise RelocationError(f"replacement count verification failed: {path}")

    temporary_name = None
    try:
        with tempfile.NamedTemporaryFile(
                mode="wb", dir=path.parent, prefix=f".{path.name}.",
                delete=False) as temporary:
            temporary_name = temporary.name
            temporary.write(relocated)
            temporary.flush()
            os.fsync(temporary.fileno())
        os.chmod(temporary_name, stat.S_IMODE(metadata.st_mode))
        os.replace(temporary_name, path)
        temporary_name = None
    finally:
        if temporary_name is not None:
            try:
                os.unlink(temporary_name)
            except FileNotFoundError:
                pass


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", nargs="+", type=Path)
    arguments = parser.parse_args(argv)
    try:
        for binary in arguments.binary:
            relocate_binary(binary)
    except RelocationError as error:
        print(f"[error] {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
