#!/usr/bin/env python3
"""Tests for byte-stable compiled source-path relocation in staged Mach-Os."""

import importlib.util
import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = (Path(__file__).parents[1] / "packaging" /
               "relocate_compiled_paths.py")
SPEC = importlib.util.spec_from_file_location("relocate_compiled_paths", MODULE_PATH)
RELOCATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RELOCATOR)
RelocationError = RELOCATOR.RelocationError
relocate_tree = RELOCATOR.relocate_tree

MACHO64 = b"\xcf\xfa\xed\xfe" + (b"\x00" * 28)


class CompiledPathRelocationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.contents = self.root / "Candidate.app" / "Contents"
        self.contents.mkdir(parents=True)
        self.source_a = self.root / "sources" / "osgverse-source-tree"
        self.source_b = self.root / "worktrees" / "osgsol-source-tree"
        self.source_a.mkdir(parents=True)
        self.source_b.mkdir(parents=True)
        self.source_a = self.source_a.resolve()
        self.source_b = self.source_b.resolve()

    def tearDown(self):
        self.temp.cleanup()

    def write_macho(self, relative, payload):
        path = self.contents / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(MACHO64 + payload)
        path.chmod(0o755)
        return path

    def test_relocates_every_configured_root_without_changing_bytes_or_mode(self):
        first = self.write_macho(
            "MacOS/main",
            os.fsencode(self.source_a) + b"/main.cpp\x00" +
            b"/" + os.fsencode(self.source_b) + b"/thirdparty.c\x00")
        second = self.write_macho(
            "lib/plugin.so", os.fsencode(self.source_b) + b"/plugin.cpp\x00")
        ignored = self.contents / "misc" / "plain.txt"
        ignored.parent.mkdir(parents=True)
        ignored.write_bytes(os.fsencode(self.source_a))
        before_sizes = {path: path.stat().st_size for path in (first, second)}
        before_modes = {
            path: stat.S_IMODE(path.stat().st_mode) for path in (first, second)}

        counts = relocate_tree(self.contents, [self.source_a, self.source_b])

        self.assertEqual(counts[str(self.source_a.resolve())], 1)
        self.assertEqual(counts[str(self.source_b.resolve())], 2)
        for path in (first, second):
            data = path.read_bytes()
            self.assertNotIn(os.fsencode(self.source_a.resolve()), data)
            self.assertNotIn(os.fsencode(self.source_b.resolve()), data)
            self.assertEqual(path.stat().st_size, before_sizes[path])
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), before_modes[path])
        self.assertEqual(ignored.read_bytes(), os.fsencode(self.source_a))

    def test_rejects_validly_signed_binary_before_writing(self):
        target = self.contents / "MacOS" / "main"
        target.parent.mkdir(parents=True)
        source = self.root / "signed_fixture.c"
        source.write_text(
            'const char *compiled_path = "' + str(self.source_a) + '";\n'
            'int main(void) { return compiled_path[0] == 0; }\n',
            encoding="utf-8")
        subprocess.run(
            ["/usr/bin/clang", str(source), "-o", str(target)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        subprocess.run(
            ["codesign", "--force", "--sign", "-", str(target)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        before = target.read_bytes()

        with self.assertRaisesRegex(RelocationError, "signed"):
            relocate_tree(self.contents, [self.source_a])
        self.assertEqual(target.read_bytes(), before)

    def test_rejects_already_relocated_binary(self):
        target = self.write_macho("MacOS/main", b"@src/0123456789ab____")
        with self.assertRaisesRegex(RelocationError, "already relocated"):
            relocate_tree(self.contents, [self.source_a])
        self.assertTrue(target.exists())

    def test_rejects_unsafe_or_duplicate_roots(self):
        with self.assertRaises(RelocationError):
            relocate_tree(self.contents, [Path("/")])
        with self.assertRaisesRegex(RelocationError, "duplicate"):
            relocate_tree(self.contents, [self.source_a, self.source_a])

    def test_formal_packager_relocates_both_source_roots_before_signing(self):
        script = (Path(__file__).parents[1] / "packaging" /
                  "package_macos.sh").read_text(encoding="utf-8")
        call = 'relocate_compiled_paths.py"'
        self.assertIn(call, script)
        self.assertIn('--source-root "$REPO"', script)
        self.assertIn('--source-root "$OSG_RUNTIME_SOURCE_ROOT"', script)
        self.assertLess(script.index(call), script.index("codesign --force --sign"))


if __name__ == "__main__":
    unittest.main()
