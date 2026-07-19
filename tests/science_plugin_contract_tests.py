#!/usr/bin/env python3
"""Static product contract for the ScienceEarth G0 plugin boundary."""

import os
import re
import subprocess
import unittest
from pathlib import Path


SCIENCE_SYMBOL = re.compile(
    r"(?:^|\s)_?(?:GDAL[A-Z_]|OGR[A-Z_]|OSR[A-Z_]|proj_[a-z]|ZSTD_[A-Za-z])")
SCIENCE_STRING = re.compile(
    r"(?:GDALAllRegister|GDALOpen(?:Ex)?|GDAL_DATA|PROJ_LIB|"
    r"proj_(?:context_create|create_crs_to_crs)|"
    r"ZSTD_(?:compress|decompress|createDStream))")


def command(*args):
    return subprocess.run(
        args, check=True, text=True, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE).stdout


class SciencePluginContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.plugin = Path(os.environ["OSGSOL_SCIENCE_PLUGIN"])
        cls.main = Path(os.environ["OSGSOL_SCIENCE_MAIN"])
        cls.link_file = Path(os.environ["OSGSOL_SCIENCE_MAIN_LINK_FILE"])

    def test_plugin_has_the_single_approved_export(self):
        self.assertTrue(self.plugin.is_file())
        exports = [line.split()[-1] for line in
                   command("nm", "-gU", str(self.plugin)).splitlines()
                   if line.strip()]
        self.assertEqual(exports, ["_osgsol_science_g0_probe_anchor"])

    def test_main_link_command_has_no_science_static_stack(self):
        link = self.link_file.read_text(encoding="utf-8")
        for forbidden in ("osgSolSciencePreview", "libgdal.a",
                          "libproj.a", "libzstd.a"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, link)

    def test_main_contains_no_science_stack_symbols_or_strings(self):
        self.assertTrue(self.main.is_file())
        symbols = command("nm", str(self.main)).splitlines()
        strings = command("strings", "-a", str(self.main)).splitlines()
        self.assertEqual([line for line in symbols if SCIENCE_SYMBOL.search(line)], [])
        self.assertEqual([line for line in strings if SCIENCE_STRING.search(line)], [])

    def test_macos_plugin_installs_into_the_packager_plugin_root(self):
        cmake = (Path(__file__).parents[1] / "science" / "CMakeLists.txt").read_text(
            encoding="utf-8")
        self.assertIn(
            'LIBRARY DESTINATION "lib/osgPlugins-${OSG_MAJOR_VERSION}.'
            '${OSG_MINOR_VERSION}.${OSG_PATCH_VERSION}"',
            cmake)


if __name__ == "__main__":
    unittest.main()
