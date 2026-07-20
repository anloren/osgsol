#!/usr/bin/env python3
"""Static contract for terrain-native ScienceEarth raster display."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EARTH = ROOT / "applications" / "earth_explorer"
SHADERS = ROOT / "assets" / "shaders"


class TerrainScienceOverlayContractTests(unittest.TestCase):
    def test_fixed_altitude_preview_surface_is_removed(self):
        preview = (EARTH / "science_preview_layer.cpp").read_text(
            encoding="utf-8")
        self.assertNotIn("PREVIEW_ALTITUDE_METERS", preview)
        self.assertNotIn("osg::Depth::ALWAYS", preview)
        self.assertNotIn("createSciencePreviewArtifactNode", preview)

    def test_dedicated_globe_shader_samples_science_raster(self):
        path = SHADERS / "scattering_globe_science.frag.glsl"
        self.assertTrue(path.is_file(), "dedicated science globe shader is missing")
        shader = path.read_text(encoding="utf-8")
        self.assertIn("ScienceOverlaySampler", shader)
        self.assertIn("ScienceOverlayBounds", shader)
        self.assertIn("TerrainMapBounds", shader)

    def test_plugin_uses_v3_host_copy_raster_bridge(self):
        api = (EARTH / "science_plugin_api.h").read_text(encoding="utf-8")
        self.assertIn("OSGSOL_SCIENCE_PLUGIN_ABI_V3", api)
        self.assertIn("OsgSolGeoRasterFrameV1", api)
        self.assertIn("OsgSolGeoRasterBridgeV1", api)
        self.assertIn("publishCopy", api)
        self.assertIn("bindGeoRaster", api)

    def test_host_overlay_is_generic_and_science_stack_free(self):
        path = EARTH / "terrain_science_overlay.cpp"
        self.assertTrue(path.is_file(), "generic host terrain overlay is missing")
        source = path.read_text(encoding="utf-8")
        main = (EARTH / "earth_main.cpp").read_text(encoding="utf-8")
        for forbidden in (
                "GDAL", "ScienceQueryService", "AlphaEarthProvider",
                "Sentinel2Provider", "CopernicusDemProvider"):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, source)
                self.assertNotIn(forbidden, main)

    def test_science_specific_host_sources_are_feature_gated(self):
        cmake = (EARTH / "CMakeLists.txt").read_text(encoding="utf-8")
        start = cmake.index("IF(OSGSOL_BUILD_SCIENCE)")
        end = cmake.index("ENDIF()", start)
        science_sources = cmake[start:end]
        self.assertIn("terrain_science_overlay.cpp", science_sources)
        self.assertIn("science_plugin_runtime.cpp", science_sources)


if __name__ == "__main__":
    unittest.main()
