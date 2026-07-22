import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
CONFIG = ROOT / "helpers" / "toolchain_builder" / "rmlui" / "CMakeLists.txt"


class RmlUiDependencyContractTests(unittest.TestCase):
    def test_official_release_is_pinned_and_hash_verified(self):
        text = CONFIG.read_text(encoding="utf-8")
        self.assertIn("RmlUi/archive/refs/tags/6.2.tar.gz", text)
        self.assertIn(
            "814c3ff7b9666280338d8f0dda85979f5daf028d01c85fc8975431d1e2fd8e8b",
            text,
        )
        self.assertIn("EXPECTED_HASH", text)

    def test_product_build_is_static_and_excludes_sample_features(self):
        text = CONFIG.read_text(encoding="utf-8")
        self.assertRegex(text, r"BUILD_SHARED_LIBS[^\n]*OFF")
        self.assertRegex(text, r"RMLUI_SAMPLES[^\n]*OFF")
        self.assertRegex(text, r"RMLUI_LUA_BINDINGS[^\n]*OFF")
        self.assertRegex(text, r"RMLUI_LOTTIE_PLUGIN[^\n]*OFF")
        self.assertRegex(text, r"RMLUI_SVG_PLUGIN[^\n]*OFF")
        self.assertIn("Freetype::Freetype", text)

    def test_license_is_retained_in_dependency_cache(self):
        text = CONFIG.read_text(encoding="utf-8")
        self.assertIn("LICENSE.txt", text)


if __name__ == "__main__":
    unittest.main()
