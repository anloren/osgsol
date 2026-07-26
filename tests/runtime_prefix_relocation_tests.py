#!/usr/bin/env python3
"""Fail-closed tests for staged macOS runtime-prefix relocation."""

import os
import shutil
import stat
import subprocess
import tempfile
import unittest
import importlib.util
from pathlib import Path


MODULE_PATH = (Path(__file__).parents[1] / "packaging" /
               "relocate_runtime_prefixes.py")
SPEC = importlib.util.spec_from_file_location("relocate_runtime_prefixes", MODULE_PATH)
RELOCATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(RELOCATOR)
RelocationError = RELOCATOR.RelocationError
relocate_binary = RELOCATOR.relocate_binary


MACHO64 = b"\xcf\xfa\xed\xfe" + (b"\x00" * 28)


class RuntimePrefixRelocationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)

    def tearDown(self):
        self.temp.cleanup()

    def write_fixture(self, name, homebrew_count, usr_local_count):
        path = self.root / name
        payload = MACHO64
        payload += b"before\x00"
        payload += (b"/opt/homebrew/example\x00" * homebrew_count)
        payload += (b"/usr/local/share\x00" * usr_local_count)
        payload += b"after\x00"
        path.write_bytes(payload)
        path.chmod(0o755)
        return path, payload

    def test_relocates_exact_allowlisted_counts_without_changing_size_or_mode(self):
        path, before = self.write_fixture("libfontconfig.1.dylib", 5, 1)
        mode = stat.S_IMODE(path.stat().st_mode)

        relocate_binary(path)

        after = path.read_bytes()
        self.assertEqual(len(after), len(before))
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), mode)
        self.assertEqual(after.count(b"@bundleprefix"), 5)
        self.assertEqual(after.count(b"@usr_local"), 1)
        self.assertNotIn(b"/opt/homebrew", after)
        self.assertNotIn(b"/usr/local", after)
        self.assertIn(b"before\x00", after)
        self.assertIn(b"after\x00", after)

    def test_rejects_wrong_occurrence_count_without_writing(self):
        path, before = self.write_fixture("libfontconfig.1.dylib", 4, 1)
        with self.assertRaisesRegex(RelocationError, "expected 5"):
            relocate_binary(path)
        self.assertEqual(path.read_bytes(), before)

    def test_rejects_already_relocated_input(self):
        path, _ = self.write_fixture("libintl.8.dylib", 1, 0)
        path.write_bytes(path.read_bytes() + b"@bundleprefix")
        with self.assertRaisesRegex(RelocationError, "already relocated"):
            relocate_binary(path)

    def test_rejects_symlink(self):
        target, _ = self.write_fixture("target", 1, 0)
        link = self.root / "libintl.8.dylib"
        link.symlink_to(target)
        with self.assertRaisesRegex(RelocationError, "symlink"):
            relocate_binary(link)

    def test_rejects_non_macho(self):
        path = self.root / "libintl.8.dylib"
        path.write_bytes(b"not a Mach-O /opt/homebrew")
        with self.assertRaisesRegex(RelocationError, "Mach-O"):
            relocate_binary(path)

    def test_rejects_validly_signed_input(self):
        path = self.root / "libfontconfig.1.dylib"
        shutil.copyfile("/usr/bin/true", path)
        os.chmod(path, 0o755)
        # A copied Apple platform binary can retain an embedded signature yet
        # fail strict verification outside its sealed system location on newer
        # macOS releases. Re-sign the disposable fixture ad hoc so the test
        # exercises the relocator's signed-input guard, not platform trust.
        subprocess.run(
            ["codesign", "--force", "--sign", "-", str(path)],
            check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        with self.assertRaisesRegex(RelocationError, "signed"):
            relocate_binary(path)

    def test_rejects_unapproved_library_name(self):
        path, _ = self.write_fixture("libother.dylib", 1, 0)
        with self.assertRaisesRegex(RelocationError, "not allowlisted"):
            relocate_binary(path)

    def test_relocates_the_fixed_lua_module_search_prefixes(self):
        path, _ = self.write_fixture("osgdb_lua.so", 0, 6)
        relocate_binary(path)
        self.assertEqual(path.read_bytes().count(b"@usr_local"), 6)

    def test_packager_relocates_the_two_libraries_before_signing(self):
        script = (Path(__file__).parents[1] / "packaging" /
                  "package_macos.sh").read_text(encoding="utf-8")
        call = 'relocate_runtime_prefixes.py"'
        self.assertIn(call, script)
        self.assertIn('"$BUILD_APP/Contents/lib/libfontconfig.1.dylib"', script)
        self.assertIn('"$BUILD_APP/Contents/lib/libintl.8.dylib"', script)
        self.assertIn(
            '"$BUILD_APP/Contents/lib/$PLUGVER/osgdb_lua.so"', script)
        self.assertLess(script.index(call), script.index("codesign --force --sign"))

    def test_earth_uses_bundle_fontconfig_without_overriding_user_choice(self):
        root = Path(__file__).parents[1]
        source = (root / "applications" / "earth_explorer" /
                  "earth_main.cpp").read_text(encoding="utf-8")
        self.assertIn('std::getenv("FONTCONFIG_FILE")', source)
        self.assertIn('MISC_DIR + std::string("fontconfig/fonts.conf")', source)
        config = (root / "assets" / "misc" / "fontconfig" /
                  "fonts.conf").read_text(encoding="utf-8")
        self.assertIn("/System/Library/Fonts", config)
        self.assertIn("~/Library/Fonts", config)
        self.assertNotIn("/opt/homebrew", config)
        self.assertNotIn("/usr/local", config)


if __name__ == "__main__":
    unittest.main()
