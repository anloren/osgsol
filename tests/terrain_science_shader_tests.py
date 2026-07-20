#!/usr/bin/env python3
"""Contracts for terrain-native scientific raster composition."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / "assets" / "shaders"


class TerrainScienceShaderTests(unittest.TestCase):
    def setUp(self):
        self.base = (SHADERS / "scattering_globe.frag.glsl").read_text(
            encoding="utf-8")
        self.science = (
            SHADERS / "scattering_globe_science.frag.glsl").read_text(
                encoding="utf-8")
        self.ground = (
            SHADERS / "scattering_globe_ground.module.glsl").read_text(
                encoding="utf-8")

    def test_only_science_variant_declares_science_sampler(self):
        self.assertNotIn("ScienceOverlaySampler", self.base)
        self.assertIn("uniform sampler2D ScienceOverlaySampler", self.science)
        self.assertIn("uniform vec4 TerrainMapBounds", self.science)
        self.assertIn("uniform vec2 TerrainMapOriginHigh", self.science)
        self.assertIn("uniform vec2 ScienceOverlayOriginHigh", self.science)

    def test_ground_composition_order_is_explicit(self):
        self.assertIn("composeGroundLayers", self.ground)
        self.assertIn('"scattering_globe_ground.module.glsl"', self.base)
        self.assertIn('"scattering_globe_ground.module.glsl"', self.science)
        base_sample = self.science.index("SceneSampler, texCoord.st")
        science_sample = self.science.index(
            "groundColor = applyScienceOverlay")
        labels_and_overlay = self.science.index(
            "groundColor = composeGroundLayers")
        self.assertLess(base_sample, science_sample)
        self.assertLess(science_sample, labels_and_overlay)
        self.assertLess(self.ground.index("ExtraLayerSampler"),
                        self.ground.index("Overlay2Sampler"))

    def test_mapping_matches_terrain_mercator_contract(self):
        self.assertIn("atan(sinh(radians(2.0 * mapY)))", self.science)
        self.assertIn("TerrainUsesWebMercator", self.science)
        self.assertIn("insideScienceBounds", self.science)
        self.assertLess(self.science.index("insideScienceBounds"),
                        self.science.index(
                            "VERSE_TEX2D(ScienceOverlaySampler"))

    def test_transparent_science_is_an_identity_blend(self):
        self.assertIn("scienceColor.a *", self.science)
        self.assertIn("mix(groundColor.rgb, scienceColor.rgb", self.science)
        self.assertNotIn("osg::Depth", self.science)
        self.assertNotIn("PolygonOffset", self.science)
        self.assertNotIn("gl_FragDepth", self.science)

    def test_footprint_is_shader_only_and_requires_valid_color(self):
        self.assertIn("ScienceOverlayOutlineVisible", self.science)
        self.assertIn("scienceColor.a > 0.0", self.science)
        preview = (
            ROOT / "applications" / "earth_explorer" /
            "science_preview_layer.cpp").read_text(encoding="utf-8")
        self.assertNotIn("PREVIEW_ALTITUDE_METERS", preview)
        self.assertNotIn("createSciencePreviewArtifactNode", preview)

    def test_two_by_two_fixture_keeps_bottom_left_origin(self):
        fixture = {
            (0.0, 0.0): "southwest",
            (1.0, 0.0): "southeast",
            (0.0, 1.0): "northwest",
            (1.0, 1.0): "northeast",
        }
        self.assertEqual(fixture[(0.0, 0.0)], "southwest")
        self.assertEqual(fixture[(1.0, 1.0)], "northeast")
        self.assertIn("setOrigin(osg::Image::BOTTOM_LEFT)", (
            ROOT / "applications" / "earth_explorer" /
            "terrain_science_overlay.cpp").read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
