import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def read(relative_path):
    return (ROOT / relative_path).read_text(encoding="utf-8")


class ScienceQueryConsumerContractTests(unittest.TestCase):
    def test_all_consumers_use_query_service(self):
        for relative_path in (
            "applications/earth_explorer/EarthControlUI.h",
            "applications/earth_explorer/science_preview_layer.h",
            "applications/earth_explorer/science_preview_layer.cpp",
            "applications/earth_explorer/science_ai_tools.h",
            "applications/earth_explorer/science_ai_tools.cpp",
        ):
            source = read(relative_path)
            self.assertNotIn("SciencePreviewRuntime", source, relative_path)
            self.assertIn("ScienceQueryService", source, relative_path)

    def test_main_owns_one_registry_provider_and_service(self):
        source = read("applications/earth_explorer/earth_main.cpp")
        self.assertIn("ScienceSourceRegistry", source)
        self.assertIn("AlphaEarthProvider", source)
        self.assertIn("Sentinel2Provider", source)
        self.assertIn("ScienceQueryService", source)
        self.assertNotIn("new earthscience::SciencePreviewRuntime", source)
        self.assertIn("new SciencePreviewLayer(scienceService.get())", source)

    def test_ui_catalog_exposes_meaning_health_and_provenance(self):
        control = read("applications/earth_explorer/EarthControlUI.h")
        source = read("applications/earth_explorer/science_earth_panel.cpp")
        query_builder = read(
            "applications/earth_explorer/science_query_builder.h")
        self.assertIn("_sciencePanel.drawOperations(", control)
        self.assertIn("_sciencePanel.drawResults(", control)
        self.assertIn("finishLeftThenDrawScienceResults(", control)

        executable_contract = read("tests/earth_control_layout_tests.cpp")
        self.assertIn("finishLeftThenDrawScienceResults(", executable_contract)
        self.assertIn('scienceFrameOrder[0] == "end-left"', executable_contract)
        self.assertIn('scienceFrameOrder[1] == "draw-results"',
                      executable_contract)

        for token in (
            "ScienceQueryService",
            "listSources()",
            "source.health",
            "source.healthMessage",
            "source.providerVersion",
            "source.firstYear",
            "source.lastYear",
            "source.nativeResolutionMeters",
            "source.componentCount",
            "source.attribution",
            "computeViewPointLatLonHeight",
            "requestedSpanMeters",
            "resolveSciencePanelSource",
            "sciencePanelModesForSource",
            "expectedVisualizationId",
            "sciencePanelPrimaryActionLabel(activeMode, source.id)",
        ):
            self.assertIn(token, source, token)

        self.assertIn("source.visualizations", query_builder)
        self.assertIn("visualization->id != expectedVisualizationId", source)

        for camera_writer in (
            "setByEye",
            "setByMatrix",
            "setCenter",
            "setDistance",
            "flyTo",
        ):
            self.assertNotIn(camera_writer, source, camera_writer)

    def test_agent_tools_keep_stable_names_and_camera_authority(self):
        header = read("applications/earth_explorer/science_ai_tools.h")
        source = read("applications/earth_explorer/science_ai_tools.cpp")

        self.assertIn("ScienceQueryService", header)
        self.assertNotIn("SciencePreviewRuntime", header)
        for tool_name in (
            "search_science_sources",
            "start_science_research",
            "get_research_job",
            "show_science_artifact",
        ):
            self.assertIn(tool_name, source)

        for token in (
            "source_id",
            "visualization_id",
            "listSources()",
            "source.health",
            "source.attribution",
            "lastSuccessfulArtifact",
            "camera_changed",
        ):
            self.assertIn(token, source, token)

        for camera_writer in (
            "setByEye",
            "setByMatrix",
            "setCenter",
            "setDistance",
            "flyTo",
            "moveTo",
        ):
            self.assertNotIn(camera_writer, source, camera_writer)

    def test_consumer_contract_is_science_enabled_only(self):
        cmake = read("tests/CMakeLists.txt")
        registration = "osgSol_Test_ScienceQueryConsumer"
        self.assertIn(registration, cmake)
        science_guard = cmake.index("IF(OSGSOL_BUILD_SCIENCE)")
        self.assertGreater(cmake.index(registration), science_guard)


if __name__ == "__main__":
    unittest.main()
