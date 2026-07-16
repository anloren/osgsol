#include "science_ai_tools.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ScienceQueryService.h>
#include <osg/Math>
#include <readerwriter/EarthManipulator.h>

#include "LayerManager.h"
#include "ai_tools.h"
#include "science_preview_layer.h"
#include "science_query_builder.h"

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    earthscience::ScienceSourceDescriptor makeDescriptor()
    {
        earthscience::ScienceSourceDescriptor source;
        source.id = "alphaearth-foundations";
        source.name = "AlphaEarth Foundations";
        source.category = "annual surface embedding";
        source.providerVersion = "1.1";
        source.attribution = "Google / Google DeepMind / source.coop";
        source.firstYear = 2017;
        source.lastYear = 2025;
        source.nativeResolutionMeters = 10.0;
        source.componentCount = 64;
        source.health = earthscience::ScienceSourceHealth::Ready;
        source.healthMessage = "Ready";
        source.experimental = true;
        source.variables = {
            {"A01", "Embedding A01", "1", "embedding", 1},
            {"A16", "Embedding A16", "1", "embedding", 1},
            {"A09", "Embedding A09", "1", "embedding", 1},
        };
        earthscience::ScienceVisualizationDescriptor visualization;
        visualization.id = "false-color-a01-a16-a09";
        visualization.displayName = "False color A01/A16/A09";
        visualization.kind = earthscience::ScienceVisualizationKind::FalseColor;
        visualization.channelVariables = {"A01", "A16", "A09"};
        visualization.displayMinimum = -0.3;
        visualization.displayMaximum = 0.3;
        visualization.legend = "Embedding values; not natural color";
        source.visualizations.push_back(visualization);
        source.capabilities.pointQuery = true;
        source.capabilities.explicitYears = true;
        source.capabilities.rasterLayerOutput = true;
        source.capabilities.minimumSpanMeters = 2560.0;
        source.capabilities.maximumSpanMeters = 81920.0;
        return source;
    }

    class ToolProvider : public earthscience::IScienceProvider
    {
    public:
        earthscience::ScienceSourceDescriptor descriptor() const override
        {
            return makeDescriptor();
        }

        std::uint64_t submit(
            const earthscience::GeoTemporalQuery& query) override
        {
            lastQuery = query;
            _snapshot = earthscience::ScienceProviderSnapshot();
            _snapshot.generation = ++_generation;
            _snapshot.state = earthscience::ScienceJobState::Queued;
            _snapshot.message = "Queued";
            return _generation;
        }

        earthscience::ScienceProviderSnapshot snapshot() const override
        {
            return _snapshot;
        }

        void cancel(std::uint64_t generation) override
        {
            if (generation != _snapshot.generation) return;
            _snapshot.state = earthscience::ScienceJobState::Cancelled;
            _snapshot.progress = earthscience::ScienceProgress();
            _snapshot.progress.stage =
                earthscience::ScienceProgressStage::Cancelled;
            _snapshot.message = "Cancelled";
        }

        void clear() override
        {
            _snapshot = earthscience::ScienceProviderSnapshot();
        }

        void publishReady()
        {
            auto artifact = std::make_shared<earthscience::ScienceArtifact>();
            artifact->artifactId = "test-artifact";
            artifact->generation = _generation;
            artifact->visualizationId = lastQuery.visualizationId;
            artifact->processingVersion = "test-processing-v1";
            artifact->raster.bounds = {-122.2, 37.4, -122.0, 37.6};
            artifact->raster.sourceResolutionMeters = 10.0;
            artifact->raster.displayResolutionMeters = 80.0;
            earthscience::ScienceSourceReference reference;
            reference.sourceId = lastQuery.sourceId;
            reference.providerVersion = "1.1";
            reference.datasetId = "alphaearth-test-cog";
            reference.originalUrl = "https://example.invalid/test.tif";
            reference.attribution =
                "Google / Google DeepMind / source.coop";
            artifact->sourceReferences.push_back(reference);
            _snapshot.state = earthscience::ScienceJobState::Ready;
            _snapshot.progress.stage =
                earthscience::ScienceProgressStage::Ready;
            _snapshot.progress.completedUnits = 1;
            _snapshot.progress.totalUnits = 1;
            _snapshot.progress.determinate = true;
            _snapshot.progress.unit = "artifact";
            _snapshot.message = "Ready";
            _snapshot.artifact = std::move(artifact);
        }

        void publishFailure()
        {
            _snapshot.state = earthscience::ScienceJobState::Failed;
            _snapshot.progress = earthscience::ScienceProgress();
            _snapshot.progress.stage =
                earthscience::ScienceProgressStage::Failed;
            _snapshot.message = "Replacement failed";
            _snapshot.artifact.reset();
        }

        std::uint64_t generation() const { return _generation; }
        earthscience::GeoTemporalQuery lastQuery;

    private:
        std::uint64_t _generation = 0;
        earthscience::ScienceProviderSnapshot _snapshot;
    };

    picojson::value emptyArgs()
    {
        return picojson::value(picojson::object());
    }

    void requireMatrixUnchanged(const osg::Matrixd& expected,
                                osgVerse::EarthManipulator& manipulator)
    {
        const osg::Matrixd actual = manipulator.getMatrix();
        require(std::memcmp(expected.ptr(), actual.ptr(),
                            sizeof(double) * 16) == 0,
                "science tool changed the camera matrix");
    }

    void testAnalysisQueryBuildersKeepExactScientificIntent()
    {
        earthscience::ScienceSourceDescriptor source = makeDescriptor();
        source.variables.push_back(
            {"embedding64", "Embedding A01-A64", "1", "embedding", 64});
        source.capabilities.boundingBoxQuery = true;
        source.capabilities.timeSeriesOutput = true;
        source.capabilities.analysisOutput = true;

        const earthscience::GeoTemporalQuery point =
            makeSciencePointSeriesQuery(source, 91.0, -181.0, 2010, 2030);
        require(point.sourceId == source.id &&
                    point.geometry.kind ==
                        earthscience::ScienceGeometryKind::Point &&
                    point.geometry.point.latitude == 91.0 &&
                    point.geometry.point.longitude == -181.0,
                "point-series builder hid malformed coordinates from service");
        require(point.time.mode ==
                    earthscience::ScienceTimeMode::ExplicitYears &&
                    point.time.explicitYears == std::vector<int>({
                        2017, 2018, 2019, 2020, 2021,
                        2022, 2023, 2024, 2025}),
                "point-series builder did not select 2017-2025 exactly");
        require(point.variables == std::vector<std::string>({"embedding64"}) &&
                    point.outputKind ==
                        earthscience::ScienceOutputKind::TimeSeries &&
                    point.analysis.kind ==
                        earthscience::ScienceAnalysisKind::PointSeries &&
                    point.analysis.baselineYear == 2017,
                "point-series builder lost its 64D analysis contract");

        earthscience::ScienceAnalysisOptions options;
        options.metrics = {earthscience::ScienceMetric::CosineDistance};
        const earthscience::GeoTemporalQuery regional =
            makeScienceRegionalAnalysisQuery(
                source, 35.36, 138.73, 1.0, 2025, 2017, options);
        require(regional.geometry.kind ==
                    earthscience::ScienceGeometryKind::BoundingBox &&
                    regional.geometry.requestedSpanMeters == 2560.0 &&
                    regional.geometry.bounds.west < 138.73 &&
                    regional.geometry.bounds.east > 138.73 &&
                    regional.geometry.bounds.south < 35.36 &&
                    regional.geometry.bounds.north > 35.36,
                "regional builder did not create the clamped centered bbox");
        require(regional.time.explicitYears ==
                    std::vector<int>({2017, 2025}) &&
                    regional.variables ==
                        std::vector<std::string>({"embedding64"}) &&
                    regional.outputKind ==
                        earthscience::ScienceOutputKind::Analysis &&
                    regional.analysis.kind ==
                        earthscience::ScienceAnalysisKind::RegionalChange &&
                    regional.analysis.baselineYear == 2017 &&
                    regional.analysis.comparisonYear == 2025 &&
                    regional.analysis.gridSize == 128 &&
                    regional.analysis.metrics == options.metrics,
                "regional builder lost ordered years or analysis options");
    }

    const earthai::Tool& findTool(const earthai::ToolRegistry& registry,
                                  const std::string& name)
    {
        for (const earthai::Tool& tool : registry.tools())
            if (tool.name == name) return tool;
        std::cerr << "[FAIL] missing tool: " << name << '\n';
        std::exit(1);
    }

    void testScienceToolsUseServiceAndPreserveCamera()
    {
        auto registry =
            std::make_unique<earthscience::ScienceSourceRegistry>();
        auto provider = std::make_unique<ToolProvider>();
        ToolProvider* providerPointer = provider.get();
        std::string registrationError;
        require(registry->add(std::move(provider), registrationError),
                "tool provider registration failed");
        earthscience::ScienceQueryService service(std::move(registry));
        osg::ref_ptr<SciencePreviewLayer> layer =
            new SciencePreviewLayer(&service);
        LayerManager layers;
        OverlayLayer catalogLayer;
        catalogLayer.id = "alphaearth";
        catalogLayer.displayName = "AlphaEarth";
        catalogLayer.group = "Science";
        catalogLayer.apply = [](const OverlayLayer&) {};
        layers.add(catalogLayer);

        osg::ref_ptr<osgVerse::EarthManipulator> manipulator =
            new osgVerse::EarthManipulator;
        manipulator->setByEye(
            osg::inDegrees(37.4844), osg::inDegrees(-122.1480), 200000.0);
        const osg::Matrixd originalMatrix = manipulator->getMatrix();
        const osg::Vec3d expectedTarget =
            manipulator->computeViewPointLatLonHeight();

        earthai::ToolRegistry tools;
        registerScienceResearchTools(
            &tools, &service, layer.get(), &layers, manipulator.get());
        const std::vector<std::string> expectedNames = {
            "search_science_sources", "start_science_research",
            "get_research_job", "show_science_artifact"};
        require(tools.tools().size() == expectedNames.size(),
                "science tool count changed");
        for (std::size_t i = 0; i < expectedNames.size(); ++i)
            require(tools.tools()[i].name == expectedNames[i],
                    "science tool name or order changed");

        picojson::value result;
        require(tools.dispatch("search_science_sources", emptyArgs(), result),
                "search tool did not dispatch");
        require(result.is<picojson::object>() && result.contains("sources"),
                "search result omitted source catalog");
        const picojson::array& sources =
            result.get("sources").get<picojson::array>();
        require(sources.size() == 1 &&
                    sources.front().get("health").get<std::string>() == "ready" &&
                    !sources.front().get("attribution").get<std::string>().empty(),
                "search result omitted health or attribution");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        const earthai::Tool& start = findTool(tools, "start_science_research");
        require(start.parametersJson.find("source_id") != std::string::npos &&
                    start.parametersJson.find("visualization_id") !=
                        std::string::npos,
                "start schema omitted generic source selection");
        require(tools.dispatch("start_science_research", emptyArgs(), result),
                "default start tool did not dispatch");
        require(std::abs(providerPointer->lastQuery.geometry.point.latitude -
                         osg::RadiansToDegrees(expectedTarget[0])) < 1e-9 &&
                    std::abs(providerPointer->lastQuery.geometry.point.longitude -
                             osg::RadiansToDegrees(expectedTarget[1])) < 1e-9,
                "omitted coordinates did not use the viewed ground target");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        const std::uint64_t readyJob = service.snapshot().jobId;
        providerPointer->publishReady();
        picojson::object getArgs;
        getArgs["job_id"] = picojson::value(static_cast<double>(readyJob));
        require(tools.dispatch("get_research_job", picojson::value(getArgs), result),
                "get tool did not dispatch");
        require(result.contains("artifact") &&
                    result.get("artifact").contains("dataset_id") &&
                    result.get("artifact").contains("attribution") &&
                    result.get("artifact").contains("visualization_id"),
                "job result omitted artifact provenance");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        picojson::object explicitArgs;
        explicitArgs["source_id"] = picojson::value(
            "alphaearth-foundations");
        explicitArgs["visualization_id"] = picojson::value(
            "false-color-a01-a16-a09");
        explicitArgs["lat"] = picojson::value(35.36);
        explicitArgs["lon"] = picojson::value(138.73);
        explicitArgs["year"] = picojson::value(2022.0);
        require(tools.dispatch(
                    "start_science_research", picojson::value(explicitArgs), result),
                "explicit start tool did not dispatch");
        require(providerPointer->lastQuery.geometry.point.latitude == 35.36 &&
                    providerPointer->lastQuery.geometry.point.longitude == 138.73 &&
                    providerPointer->lastQuery.time.explicitYears.front() == 2022,
                "explicit research coordinates or year changed");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        const std::uint64_t failedJob = service.snapshot().jobId;
        providerPointer->publishFailure();
        getArgs["job_id"] = picojson::value(static_cast<double>(failedJob));
        require(tools.dispatch("get_research_job", picojson::value(getArgs), result),
                "failed replacement job did not dispatch");
        layer->setVisible(false);
        require(tools.dispatch("show_science_artifact", picojson::value(getArgs), result),
                "show retained artifact did not dispatch");
        require(result.get("ok").get<bool>() &&
                    !result.get("camera_changed").get<bool>() &&
                    result.get("current_job_state").get<std::string>() == "failed" &&
                    layer->isVisible(),
                "show did not expose retained artifact and current failed state");
        require(layers.find("alphaearth") && layers.find("alphaearth")->enabled,
                "show did not enable only the science layer");
        requireMatrixUnchanged(originalMatrix, *manipulator);
    }
}

int main()
{
    testAnalysisQueryBuildersKeepExactScientificIntent();
    testScienceToolsUseServiceAndPreserveCamera();
    std::cout << "[OK] ScienceEarth Agent tools use the query service without camera writes\n";
    return 0;
}
