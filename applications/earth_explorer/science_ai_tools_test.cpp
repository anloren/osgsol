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
            {"embedding64", "Embedding A00-A63", "1", "embedding", 64},
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
        source.capabilities.boundingBoxQuery = true;
        source.capabilities.explicitYears = true;
        source.capabilities.rasterLayerOutput = true;
        source.capabilities.timeSeriesOutput = true;
        source.capabilities.analysisOutput = true;
        source.capabilities.minimumSpanMeters = 2560.0;
        source.capabilities.maximumSpanMeters = 81920.0;
        return source;
    }

    earthscience::ScienceSourceDescriptor makeSentinelDescriptor()
    {
        earthscience::ScienceSourceDescriptor source;
        source.id = "sentinel-2-l2a";
        source.name = "Sentinel-2 Level-2A";
        source.category = "optical satellite natural-color scene";
        source.providerVersion = "earth-search-v1";
        source.attribution =
            "Copernicus Sentinel data / Element 84 Earth Search / AWS Open Data";
        source.firstYear = 2015;
        source.lastYear = 2026;
        source.nativeResolutionMeters = 10.0;
        source.componentCount = 3;
        source.health = earthscience::ScienceSourceHealth::Ready;
        source.healthMessage = "Ready on demand";
        source.experimental = true;
        source.variables = {
            {"visual", "True color image", "display DN", "display RGB", 3},
        };
        earthscience::ScienceVisualizationDescriptor visualization;
        visualization.id = "natural-color-visual";
        visualization.displayName = "Natural color scene";
        visualization.kind =
            earthscience::ScienceVisualizationKind::NaturalColor;
        visualization.channelVariables = {"visual"};
        visualization.displayMinimum = 0.0;
        visualization.displayMaximum = 255.0;
        visualization.legend = "Sentinel-2 true-color display product";
        source.visualizations.push_back(visualization);
        source.capabilities.pointQuery = true;
        source.capabilities.intervalTime = true;
        source.capabilities.rasterLayerOutput = true;
        source.capabilities.minimumSpanMeters = 2560.0;
        source.capabilities.maximumSpanMeters = 81920.0;
        return source;
    }

    earthscience::ScienceSourceDescriptor makeDemDescriptor()
    {
        earthscience::ScienceSourceDescriptor source;
        source.id = "copernicus-dem-glo-30";
        source.name = "Copernicus DEM GLO-30";
        source.category = "static digital surface model elevation";
        source.providerVersion = "aws-glo30-2021";
        source.attribution =
            "Copernicus DEM / European Union / ESA / AWS Open Data";
        source.firstYear = source.lastYear = 2021;
        source.nativeResolutionMeters = 30.0;
        source.componentCount = 1;
        source.health = earthscience::ScienceSourceHealth::Ready;
        source.healthMessage = "Ready on demand";
        source.experimental = true;
        source.variables = {
            {"surface_elevation", "DSM surface elevation", "m",
             "digital surface model height", 1, 4},
        };
        earthscience::ScienceVisualizationDescriptor visualization;
        visualization.id = "surface-elevation-hypsometric";
        visualization.displayName = "Surface elevation (hypsometric)";
        visualization.kind = earthscience::ScienceVisualizationKind::Continuous;
        visualization.channelVariables = {"surface_elevation"};
        visualization.displayMinimum = -500.0;
        visualization.displayMaximum = 9000.0;
        visualization.legend = "Fixed DSM elevation colors in EGM2008 metres";
        source.visualizations.push_back(visualization);
        source.capabilities.pointQuery = true;
        source.capabilities.instantTime = true;
        source.capabilities.rasterLayerOutput = true;
        source.capabilities.minimumSpanMeters = 2560.0;
        source.capabilities.maximumSpanMeters = 81920.0;
        return source;
    }

    class ToolProvider : public earthscience::IScienceProvider
    {
    public:
        explicit ToolProvider(
            earthscience::ScienceSourceDescriptor source = makeDescriptor())
            : _source(std::move(source))
        {
        }

        earthscience::ScienceSourceDescriptor descriptor() const override
        {
            return _source;
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

        void publishReady(
            const std::string& artifactId = "test-artifact",
            int direction = 0,
            const std::string& processingVersion = "test-processing-v1",
            const std::string& providerVersion = "1.1",
            bool includeRegionalSummary = true)
        {
            auto artifact = std::make_shared<earthscience::ScienceArtifact>();
            artifact->artifactId = artifactId;
            artifact->generation = _generation;
            artifact->visualizationId = lastQuery.visualizationId;
            artifact->processingVersion = processingVersion;
            const bool preview = lastQuery.outputKind ==
                earthscience::ScienceOutputKind::RasterLayer;
            if (preview)
            {
                artifact->raster.bounds = {-122.2, 37.4, -122.0, 37.6};
                const bool dem =
                    lastQuery.sourceId == "copernicus-dem-glo-30";
                artifact->raster.sourceResolutionMeters = dem ? 30.0 : 10.0;
                artifact->raster.displayResolutionMeters = dem ? 39.1 : 80.0;
                if (dem)
                {
                    earthscience::ScienceScalarSummary summary;
                    summary.variableId = "surface_elevation";
                    summary.displayName = "Surface elevation";
                    summary.unit = "m";
                    summary.centerValid = true;
                    summary.center = 42.0;
                    summary.minimumValid = summary.maximumValid =
                        summary.meanValid = true;
                    summary.minimum = 1.0;
                    summary.maximum = 88.0;
                    summary.mean = 35.0;
                    summary.validCellCount = 111556;
                    summary.noDataCellCount = 0;
                    artifact->scalarSummaries.push_back(summary);
                }
            }
            earthscience::ScienceSourceReference reference;
            reference.sourceId = lastQuery.sourceId;
            reference.providerVersion = providerVersion;
            const bool sentinel = lastQuery.sourceId == "sentinel-2-l2a";
            const bool dem = lastQuery.sourceId == "copernicus-dem-glo-30";
            reference.datasetId = sentinel
                ? "S2C_54SUE_20260710_0_L2A"
                : dem ? "Copernicus_DSM_COG_10_N35_00_E139_00_DEM"
                      : "alphaearth-test-cog";
            reference.originalUrl = sentinel
                ? "https://sentinel-cogs.s3.us-west-2.amazonaws.com/test/TCI.tif"
                : dem ? "https://copernicus-dem-30m.s3.amazonaws.com/test.tif"
                      : "https://example.invalid/test.tif";
            reference.attribution = _source.attribution;
            reference.actualCoverage = preview ? artifact->raster.bounds
                : earthscience::ScienceWgs84Bounds{10.0, 20.0, 30.0, 40.0};
            reference.variables = lastQuery.variables;
            reference.processingSteps = sentinel
                ? std::vector<std::string>{
                    "Earth Search STAC Item Search",
                    "bounded visual COG window read"}
                : std::vector<std::string>{
                    "read 64D embedding", "validate mask"};
            if (sentinel)
            {
                reference.acquisitionTime = "2026-07-10T01:37:22.464000Z";
                reference.fields = {
                    {"scene_id", "Scene ID", reference.datasetId, ""},
                    {"scene_cloud_cover", "Scene cloud cover", "11.17", "%"},
                    {"visual_asset", "Selected visual COG",
                     reference.originalUrl, ""},
                };
                artifact->warnings = {
                    "Scene cloud cover is scene-wide, not a per-pixel cloud mask"};
            }
            artifact->sourceReferences.push_back(reference);
            const std::vector<int> years = lastQuery.time.explicitYears.empty()
                ? std::vector<int>{2025} : lastQuery.time.explicitYears;
            if (!preview)
            {
                artifact->embedding.years =
                    std::make_shared<const std::vector<int>>(years);
                artifact->embedding.width = 1;
                artifact->embedding.height = 1;
                std::vector<float> values(
                    years.size() *
                        earthscience::ScienceEmbeddingPayload::componentCount,
                    0.0f);
                const int axis = direction == 0 ? 0 : 1;
                for (std::size_t year = 0; year < years.size(); ++year)
                    values[year *
                               earthscience::ScienceEmbeddingPayload::componentCount +
                           static_cast<std::size_t>(axis)] = 1.0f;
                artifact->embedding.values =
                    std::make_shared<const std::vector<float>>(std::move(values));
                artifact->embedding.mask =
                    std::make_shared<const std::vector<unsigned char>>(
                        years.size(), 1);
                artifact->embedding.bounds = {10.0, 20.0, 30.0, 40.0};
                artifact->embedding.actualResolutionMeters = 12.5;
                artifact->embedding.validCellCount = years.size();
                artifact->embedding.noDataCellCount = 1;
                artifact->embedding.coverageFraction = 0.875;
                artifact->embedding.processingSteps =
                    std::make_shared<const std::vector<std::string>>(
                        std::initializer_list<std::string>{
                            "dequantize A00-A63"});
            }
            artifact->analysis.kind = lastQuery.analysis.kind;
            std::vector<earthscience::ScienceMetricResult> metrics;
            if (years.size() == 1)
            {
                metrics.push_back({earthscience::ScienceMetric::CosineDistance,
                                   years.front(), years.front(), 0.0,
                                   "unitless"});
            }
            else
            {
                for (std::size_t index = 1; index < years.size(); ++index)
                {
                    metrics.push_back({
                        earthscience::ScienceMetric::CosineDistance,
                        years[index - 1], years[index],
                        0.05 * static_cast<double>(index), "unitless"});
                }
            }
            artifact->analysis.metrics =
                std::make_shared<const std::vector<earthscience::ScienceMetricResult>>(
                    std::move(metrics));
            artifact->analysis.regionalChange.baselineYear = years.front();
            artifact->analysis.regionalChange.comparisonYear = years.back();
            if (includeRegionalSummary)
            {
                artifact->analysis.regionalChange.totalCellCount = 4;
                artifact->analysis.regionalChange.coverageFraction = 0.75;
                artifact->analysis.regionalChange.validOverlapCount = 3;
                artifact->analysis.regionalChange.noDataCellCount = 1;
                artifact->analysis.regionalChange.mean = 0.25;
                artifact->analysis.regionalChange.median = 0.20;
                artifact->analysis.regionalChange.standardDeviation = 0.05;
                artifact->analysis.regionalChange.minimum = 0.10;
                artifact->analysis.regionalChange.maximum = 0.40;
                artifact->analysis.regionalChange.bounds =
                    {11.0, 21.0, 31.0, 41.0};
                artifact->analysis.regionalChange.actualResolutionMeters = 25.0;
            }
            artifact->analysis.scalarChangeRaster.bounds =
                {12.0, 22.0, 32.0, 42.0};
            artifact->analysis.scalarChangeRaster.actualResolutionMeters = 50.0;
            artifact->analysis.scalarChangeRaster.validCellCount = 2;
            artifact->analysis.scalarChangeRaster.noDataCellCount = 2;
            artifact->analysis.scalarChangeRaster.coverageFraction = 0.5;
            artifact->analysis.limitations =
                std::make_shared<const std::vector<std::string>>(
                    std::initializer_list<std::string>{
                        "Embedding-space change is not a semantic label."});
            artifact->warnings = {"Synthetic test evidence"};
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
        earthscience::ScienceSourceDescriptor _source;
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
            {"embedding64", "Embedding A00-A63", "1", "embedding", 64});
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
        auto sentinelProvider =
            std::make_unique<ToolProvider>(makeSentinelDescriptor());
        ToolProvider* sentinelProviderPointer = sentinelProvider.get();
        auto demProvider =
            std::make_unique<ToolProvider>(makeDemDescriptor());
        ToolProvider* demProviderPointer = demProvider.get();
        std::string registrationError;
        require(registry->add(std::move(provider), registrationError),
                "tool provider registration failed");
        require(registry->add(std::move(sentinelProvider), registrationError),
                "Sentinel tool provider registration failed");
        require(registry->add(std::move(demProvider), registrationError),
                "DEM tool provider registration failed");
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
            "get_research_job", "show_science_artifact",
            "compare_science_artifacts", "run_change_analysis"};
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
        require(sources.size() == 3 &&
                    sources.front().get("health").get<std::string>() == "ready" &&
                    !sources.front().get("attribution").get<std::string>().empty() &&
                    sources[1].get("id").get<std::string>() ==
                        "copernicus-dem-glo-30" &&
                    sources[1].get("time_modes").get<picojson::array>().front().
                        get<std::string>() == "instant" &&
                    sources[1].get("visualizations").get<picojson::array>().
                        front().get("legend").get<std::string>().find("EGM2008") !=
                        std::string::npos &&
                    sources[2].get("id").get<std::string>() == "sentinel-2-l2a" &&
                    sources[2].get("visualizations").get<picojson::array>().size() == 1,
                "search result omitted health or attribution");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        const earthai::Tool& start = findTool(tools, "start_science_research");
        picojson::value startSchema;
        require(picojson::parse(startSchema, start.parametersJson).empty() &&
                    startSchema.is<picojson::object>(),
                "start schema is not valid JSON");
        require(start.parametersJson.find("source_id") != std::string::npos &&
                    start.parametersJson.find("visualization_id") !=
                        std::string::npos &&
                    start.parametersJson.find("mode") != std::string::npos &&
                    start.parametersJson.find("first_year") != std::string::npos &&
                    start.parametersJson.find("last_year") != std::string::npos &&
                    start.parametersJson.find("baseline_year") != std::string::npos &&
                    start.parametersJson.find("comparison_year") !=
                        std::string::npos &&
                    start.parametersJson.find("grid_size") != std::string::npos &&
                    start.parametersJson.find("enable_pca") != std::string::npos &&
                    start.parametersJson.find("cluster_count") !=
                        std::string::npos &&
                    start.parametersJson.find("time_start") != std::string::npos &&
                    start.parametersJson.find("time_end") != std::string::npos &&
                    start.parametersJson.find("max_cloud_percent") !=
                        std::string::npos,
                "start schema omitted compatible preview or 64D research options");
        layer->setVisible(false);
        layers.setEnabled("alphaearth", false);
        require(tools.dispatch("start_science_research", emptyArgs(), result),
                "default start tool did not dispatch");
        require(std::abs(providerPointer->lastQuery.geometry.point.latitude -
                         osg::RadiansToDegrees(expectedTarget[0])) < 1e-9 &&
                    std::abs(providerPointer->lastQuery.geometry.point.longitude -
                             osg::RadiansToDegrees(expectedTarget[1])) < 1e-9,
                "omitted coordinates did not use the viewed ground target");
        require(result.get("progress").is<picojson::object>() &&
                    result.get("progress").contains("stage") &&
                    result.get("progress").contains("completed") &&
                    result.get("progress").contains("total") &&
                    result.get("progress").contains("unit") &&
                    result.get("progress").contains("determinate") &&
                    !result.get("progress").contains("percent"),
                "indeterminate job progress was not honest and structured");
        require(providerPointer->lastQuery.sourceId ==
                    "alphaearth-foundations" &&
                    !layer->isVisible() && layers.find("alphaearth") &&
                    !layers.find("alphaearth")->enabled,
                "default research changed source compatibility or map visibility");
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
                    result.get("artifact").contains("visualization_id") &&
                    result.get("artifact").contains("artifact_id") &&
                    result.get("artifact").contains("kind") &&
                    result.get("artifact").contains("years") &&
                    result.get("artifact").contains("primary_metrics") &&
                    result.get("artifact").contains("coverage") &&
                    result.get("artifact").contains("source") &&
                    result.get("artifact").contains("processing") &&
                    result.get("artifact").contains("warnings") &&
                    result.get("artifact").contains("limitations"),
                "job result omitted compact artifact evidence");
        const picojson::value& previewCoverage =
            result.get("artifact").get("coverage");
        require(previewCoverage.contains("basis") &&
                    previewCoverage.contains("source_resolution_m") &&
                    previewCoverage.contains("display_resolution_m") &&
                    previewCoverage.get("basis").get<std::string>() == "raster" &&
                    previewCoverage.get("west").get<double>() == -122.2 &&
                    previewCoverage.get("source_resolution_m").get<double>() ==
                        10.0 &&
                    previewCoverage.get("display_resolution_m").get<double>() ==
                        80.0,
                "preview summary did not use raster coverage and resolution");
        require(result.get("progress").contains("percent"),
                "determinate job progress omitted percent");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        picojson::object demArgs;
        demArgs["source_id"] = picojson::value("copernicus-dem-glo-30");
        demArgs["visualization_id"] =
            picojson::value("surface-elevation-hypsometric");
        demArgs["mode"] = picojson::value("preview");
        demArgs["lat"] = picojson::value(35.68);
        demArgs["lon"] = picojson::value(139.76);
        require(tools.dispatch(
                    "start_science_research", picojson::value(demArgs), result),
                "DEM research did not dispatch");
        require(demProviderPointer->lastQuery.sourceId ==
                    "copernicus-dem-glo-30" &&
                    demProviderPointer->lastQuery.time.mode ==
                        earthscience::ScienceTimeMode::Instant &&
                    demProviderPointer->lastQuery.time.publicationTime == "2021" &&
                    demProviderPointer->lastQuery.time.explicitYears.empty() &&
                    demProviderPointer->lastQuery.variables ==
                        std::vector<std::string>({"surface_elevation"}) &&
                    demProviderPointer->lastQuery.visualizationId ==
                        "surface-elevation-hypsometric" &&
                    !layer->isVisible() && layers.find("alphaearth") &&
                    !layers.find("alphaearth")->enabled,
                "DEM research lost static DSM intent or changed visibility");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        demProviderPointer->publishReady("dem-artifact", 0,
                                         "copernicus-dem-hypsometric-v1",
                                         "aws-glo30-2021");
        const std::uint64_t demJob = service.snapshot().jobId;
        picojson::object demGetArgs;
        demGetArgs["job_id"] = picojson::value(static_cast<double>(demJob));
        require(tools.dispatch(
                    "get_research_job", picojson::value(demGetArgs), result),
                "DEM result did not dispatch");
        require(result.get("artifact").contains("scalar_summaries") &&
                    result.get("artifact").get("scalar_summaries").
                        get<picojson::array>().size() == 1 &&
                    result.get("artifact").serialize(false).find("rgba") ==
                        std::string::npos,
                "DEM result omitted compact numeric evidence or leaked pixels");

        const std::uint64_t beforeInvalidDem = demProviderPointer->generation();
        picojson::object invalidDem = demArgs;
        invalidDem["year"] = picojson::value(2021.0);
        require(tools.dispatch(
                    "start_science_research", picojson::value(invalidDem), result) &&
                    result.contains("error") &&
                    demProviderPointer->generation() == beforeInvalidDem,
                "DEM accepted a misleading year selector");
        invalidDem = demArgs;
        invalidDem["mode"] = picojson::value("point_series");
        require(tools.dispatch(
                    "start_science_research", picojson::value(invalidDem), result) &&
                    result.contains("error") &&
                    demProviderPointer->generation() == beforeInvalidDem,
                "DEM accepted unsupported time-series analysis");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        picojson::object sentinelArgs;
        sentinelArgs["source_id"] = picojson::value("sentinel-2-l2a");
        sentinelArgs["visualization_id"] =
            picojson::value("natural-color-visual");
        sentinelArgs["mode"] = picojson::value("preview");
        sentinelArgs["lat"] = picojson::value(35.68);
        sentinelArgs["lon"] = picojson::value(139.76);
        sentinelArgs["time_start"] =
            picojson::value("2026-06-18T00:00:00Z");
        sentinelArgs["time_end"] =
            picojson::value("2026-07-18T00:00:00Z");
        sentinelArgs["max_cloud_percent"] = picojson::value(20.0);
        require(tools.dispatch(
                    "start_science_research", picojson::value(sentinelArgs), result),
                "Sentinel research did not dispatch");
        require(sentinelProviderPointer->lastQuery.sourceId == "sentinel-2-l2a" &&
                    sentinelProviderPointer->lastQuery.time.mode ==
                        earthscience::ScienceTimeMode::Interval &&
                    sentinelProviderPointer->lastQuery.time.intervalStart ==
                        "2026-06-18T00:00:00Z" &&
                    sentinelProviderPointer->lastQuery.time.intervalEnd ==
                        "2026-07-18T00:00:00Z" &&
                    sentinelProviderPointer->lastQuery.sceneFilters.
                        maximumCloudCoverPercent == 20.0 &&
                    sentinelProviderPointer->lastQuery.variables ==
                        std::vector<std::string>({"visual"}) &&
                    sentinelProviderPointer->lastQuery.visualizationId ==
                        "natural-color-visual" &&
                    !layer->isVisible() && layers.find("alphaearth") &&
                    !layers.find("alphaearth")->enabled,
                "Sentinel research lost interval/cloud intent or changed visibility");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        sentinelProviderPointer->publishReady("sentinel-artifact");
        const std::uint64_t sentinelJob = service.snapshot().jobId;
        picojson::object sentinelGetArgs;
        sentinelGetArgs["job_id"] =
            picojson::value(static_cast<double>(sentinelJob));
        require(tools.dispatch(
                    "get_research_job", picojson::value(sentinelGetArgs), result),
                "Sentinel result did not dispatch");
        const picojson::value& sentinelArtifact = result.get("artifact");
        require(sentinelArtifact.get("time_start").get<std::string>() ==
                    "2026-06-18T00:00:00Z" &&
                    sentinelArtifact.get("time_end").get<std::string>() ==
                    "2026-07-18T00:00:00Z" &&
                    sentinelArtifact.get("acquisition_time").get<std::string>() ==
                    "2026-07-10T01:37:22.464000Z" &&
                    sentinelArtifact.get("scene_id").get<std::string>() ==
                    "S2C_54SUE_20260710_0_L2A" &&
                    sentinelArtifact.get("scene_cloud_cover_percent").get<double>() ==
                    11.17 &&
                    sentinelArtifact.get("source_url").get<std::string>().find(
                        "TCI.tif") != std::string::npos &&
                    sentinelArtifact.get("source_evidence").
                        get<picojson::array>().size() == 3,
                "Sentinel result omitted interval, scene, cloud, COG, or evidence");
        require(sentinelArtifact.serialize(false).find("rgba") ==
                    std::string::npos,
                "Sentinel result leaked raster pixels to the Agent");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        const std::uint64_t beforeInvalidSentinel =
            sentinelProviderPointer->generation();
        picojson::object invalidSentinel;
        invalidSentinel["source_id"] = picojson::value("sentinel-2-l2a");
        invalidSentinel["year"] = picojson::value(2026.0);
        require(tools.dispatch(
                    "start_science_research", picojson::value(invalidSentinel),
                    result) && result.contains("error") &&
                    sentinelProviderPointer->generation() == beforeInvalidSentinel,
                "Sentinel accepted ambiguous year-only time selection");
        invalidSentinel = sentinelArgs;
        invalidSentinel["max_cloud_percent"] = picojson::value(101.0);
        require(tools.dispatch(
                    "start_science_research", picojson::value(invalidSentinel),
                    result) && result.contains("error") &&
                    sentinelProviderPointer->generation() == beforeInvalidSentinel,
                "Sentinel accepted cloud threshold outside [0, 100]");
        invalidSentinel = sentinelArgs;
        invalidSentinel["mode"] = picojson::value("point_series");
        require(tools.dispatch(
                    "start_science_research", picojson::value(invalidSentinel),
                    result) && result.contains("error") &&
                    sentinelProviderPointer->generation() == beforeInvalidSentinel,
                "Sentinel accepted unsupported 64D point-series mode");
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
        const earthscience::GeoTemporalQuery legacyPreview =
            providerPointer->lastQuery;
        explicitArgs["mode"] = picojson::value("preview");
        require(tools.dispatch(
                    "start_science_research", picojson::value(explicitArgs), result),
                "explicit preview mode did not dispatch");
        require(providerPointer->lastQuery.sourceId == legacyPreview.sourceId &&
                    providerPointer->lastQuery.visualizationId ==
                        legacyPreview.visualizationId &&
                    providerPointer->lastQuery.geometry.point.latitude ==
                        legacyPreview.geometry.point.latitude &&
                    providerPointer->lastQuery.geometry.point.longitude ==
                        legacyPreview.geometry.point.longitude &&
                    providerPointer->lastQuery.time.explicitYears ==
                        legacyPreview.time.explicitYears &&
                    providerPointer->lastQuery.variables == legacyPreview.variables &&
                    providerPointer->lastQuery.outputKind ==
                        legacyPreview.outputKind,
                "explicit preview mode changed the legacy preview query");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        layer->setVisible(false);
        layers.setEnabled("alphaearth", false);
        picojson::object seriesArgs;
        seriesArgs["mode"] = picojson::value("point_series");
        seriesArgs["lat"] = picojson::value(35.36);
        seriesArgs["lon"] = picojson::value(138.73);
        seriesArgs["first_year"] = picojson::value(2019.0);
        seriesArgs["last_year"] = picojson::value(2022.0);
        require(tools.dispatch(
                    "start_science_research", picojson::value(seriesArgs), result),
                "point-series research did not dispatch");
        require(providerPointer->lastQuery.outputKind ==
                    earthscience::ScienceOutputKind::TimeSeries &&
                    providerPointer->lastQuery.analysis.kind ==
                        earthscience::ScienceAnalysisKind::PointSeries &&
                    providerPointer->lastQuery.time.explicitYears ==
                        std::vector<int>({2019, 2020, 2021, 2022}),
                "point-series mode lost its exact 64D query intent");
        require(!layer->isVisible() && layers.find("alphaearth") &&
                    !layers.find("alphaearth")->enabled,
                "point-series mode changed map visibility");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        providerPointer->publishReady("series-artifact");
        const std::uint64_t seriesJob = service.snapshot().jobId;
        getArgs["job_id"] = picojson::value(static_cast<double>(seriesJob));
        require(tools.dispatch("get_research_job", picojson::value(getArgs), result),
                "point-series artifact job did not dispatch");
        const picojson::value& seriesArtifact = result.get("artifact");
        const picojson::value& seriesCoverage = seriesArtifact.get("coverage");
        require(seriesCoverage.contains("basis") &&
                    seriesCoverage.contains("actual_resolution_m") &&
                    seriesCoverage.get("basis").get<std::string>() == "embedding" &&
                    seriesCoverage.get("west").get<double>() == 10.0 &&
                    seriesCoverage.get("actual_resolution_m").get<double>() ==
                        12.5 &&
                    seriesArtifact.get("source_resolution_m").get<double>() ==
                        12.5 &&
                    seriesArtifact.get("display_resolution_m").get<double>() ==
                        12.5,
                "point-series summary did not use embedding coverage and resolution");
        require(seriesArtifact.get("primary_metrics").is<picojson::array>(),
                "point-series primary metrics were not bounded entries");
        const picojson::array& seriesMetrics =
            seriesArtifact.get("primary_metrics").get<picojson::array>();
        require(seriesMetrics.size() == 3 && seriesMetrics.size() <= 32 &&
                    seriesArtifact.get("primary_metrics_limit").get<double>() ==
                        32.0,
                "point-series summary lost or failed to bound metric intervals");
        for (std::size_t index = 0; index < seriesMetrics.size(); ++index)
        {
            const picojson::value& metric = seriesMetrics[index];
            require(metric.contains("metric") &&
                        metric.contains("baseline_year") &&
                        metric.contains("comparison_year") &&
                        metric.get("metric").get<std::string>() ==
                        "cosine-distance" &&
                        metric.get("baseline_year").get<double>() ==
                            2019.0 + static_cast<double>(index) &&
                        metric.get("comparison_year").get<double>() ==
                            2020.0 + static_cast<double>(index) &&
                        metric.contains("value") && metric.contains("unit"),
                    "point-series metric entry lost its year interval evidence");
        }
        requireMatrixUnchanged(originalMatrix, *manipulator);

        const std::uint64_t beforeMalformed = providerPointer->generation();
        seriesArgs["first_year"] = picojson::value(2010.0);
        require(tools.dispatch(
                    "start_science_research", picojson::value(seriesArgs), result) &&
                    result.contains("error") &&
                    providerPointer->generation() == beforeMalformed,
                "malformed point-series years were silently clamped by a builder");
        requireMatrixUnchanged(originalMatrix, *manipulator);
        seriesArgs["first_year"] = picojson::value(2019.0);

        picojson::object regionalArgs;
        regionalArgs["mode"] = picojson::value("regional_embedding");
        regionalArgs["lat"] = picojson::value(35.36);
        regionalArgs["lon"] = picojson::value(138.73);
        regionalArgs["baseline_year"] = picojson::value(2019.0);
        regionalArgs["comparison_year"] = picojson::value(2022.0);
        regionalArgs["grid_size"] = picojson::value(8.0);
        regionalArgs["enable_pca"] = picojson::value(true);
        regionalArgs["cluster_count"] = picojson::value(3.0);
        require(tools.dispatch(
                    "start_science_research", picojson::value(regionalArgs), result),
                "regional embedding research did not dispatch");
        require(providerPointer->lastQuery.outputKind ==
                    earthscience::ScienceOutputKind::Analysis &&
                    providerPointer->lastQuery.analysis.kind ==
                        earthscience::ScienceAnalysisKind::RegionalChange &&
                    providerPointer->lastQuery.analysis.gridSize == 8 &&
                    providerPointer->lastQuery.analysis.enablePca &&
                    providerPointer->lastQuery.analysis.enableClustering &&
                    providerPointer->lastQuery.analysis.clusterCount == 3,
                "regional embedding mode lost its analysis options");
        require(!layer->isVisible() && layers.find("alphaearth") &&
                    !layers.find("alphaearth")->enabled,
                "regional embedding mode changed map visibility");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        providerPointer->publishReady("regional-artifact");
        const std::uint64_t regionalJob = service.snapshot().jobId;
        getArgs["job_id"] = picojson::value(static_cast<double>(regionalJob));
        require(tools.dispatch("get_research_job", picojson::value(getArgs), result),
                "regional artifact job did not dispatch");
        const std::string regionalJson = result.serialize(false);
        const picojson::value& regionalArtifact = result.get("artifact");
        require(regionalArtifact.get("artifact_id").get<std::string>() ==
                    "regional-artifact" &&
                    regionalArtifact.contains("kind") &&
                    regionalArtifact.contains("years") &&
                    regionalArtifact.contains("primary_metrics") &&
                    regionalArtifact.contains("coverage") &&
                    regionalArtifact.contains("source") &&
                    regionalArtifact.contains("processing") &&
                    regionalArtifact.contains("warnings") &&
                    regionalArtifact.contains("limitations"),
                "analysis artifact omitted required compact evidence");
        const picojson::value& regionalCoverage =
            regionalArtifact.get("coverage");
        require(regionalCoverage.contains("basis") &&
                    regionalCoverage.contains("actual_resolution_m") &&
                    regionalCoverage.get("basis").get<std::string>() ==
                    "regional-change" &&
                    regionalCoverage.get("west").get<double>() == 11.0 &&
                    regionalCoverage.get("fraction").get<double>() == 0.75 &&
                    regionalCoverage.get("valid_cells").get<double>() == 3.0 &&
                    regionalCoverage.get("actual_resolution_m").get<double>() ==
                        25.0 &&
                    regionalArtifact.get("source_resolution_m").get<double>() ==
                        25.0,
                "regional summary did not prefer regional-change evidence");
        require(regionalJson.find("embedding_values") == std::string::npos &&
                    regionalJson.find("change_values") == std::string::npos &&
                    regionalJson.find("rgba") == std::string::npos &&
                    regionalJson.find("\"A01\"") == std::string::npos &&
                    regionalJson.find("\"A64\"") == std::string::npos,
                "analysis artifact leaked raw scientific arrays");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        require(tools.dispatch(
                    "start_science_research", picojson::value(regionalArgs), result),
                "scalar fallback research did not dispatch");
        providerPointer->publishReady(
            "scalar-fallback-artifact", 0, "test-processing-v1", "1.1", false);
        const std::uint64_t scalarJob = service.snapshot().jobId;
        getArgs["job_id"] = picojson::value(static_cast<double>(scalarJob));
        require(tools.dispatch("get_research_job", picojson::value(getArgs), result),
                "scalar fallback artifact job did not dispatch");
        const picojson::value& scalarCoverage =
            result.get("artifact").get("coverage");
        require(scalarCoverage.get("basis").get<std::string>() ==
                    "scalar-change" &&
                    scalarCoverage.get("west").get<double>() == 12.0 &&
                    scalarCoverage.get("actual_resolution_m").get<double>() ==
                        50.0,
                "regional summary did not fall back to scalar-change evidence");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        regionalArgs["grid_size"] = picojson::value(0.0);
        const std::uint64_t beforeBadGrid = providerPointer->generation();
        require(tools.dispatch(
                    "start_science_research", picojson::value(regionalArgs), result) &&
                    result.contains("error") &&
                    providerPointer->generation() == beforeBadGrid,
                "malformed regional grid was silently defaulted by a builder");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        seriesArgs["first_year"] = picojson::value(2020.0);
        seriesArgs["last_year"] = picojson::value(2020.0);
        require(tools.dispatch(
                    "start_science_research", picojson::value(seriesArgs), result),
                "left comparison research did not dispatch");
        providerPointer->publishReady("left-artifact", 0);
        service.snapshot();
        require(tools.dispatch(
                    "start_science_research", picojson::value(seriesArgs), result),
                "right comparison research did not dispatch");
        providerPointer->publishReady("right-artifact", 1);
        service.snapshot();
        picojson::object compareArgs;
        compareArgs["left_artifact_id"] = picojson::value("left-artifact");
        compareArgs["right_artifact_id"] = picojson::value("right-artifact");
        require(tools.dispatch(
                    "compare_science_artifacts", picojson::value(compareArgs), result),
                "artifact comparison did not dispatch");
        require(result.contains("artifact_id") && result.contains("kind") &&
                    result.contains("years") &&
                    result.contains("primary_metrics") &&
                    result.contains("coverage") && result.contains("source") &&
                    result.contains("processing") && result.contains("warnings") &&
                    result.contains("limitations"),
                "artifact comparison omitted compact evidence");
        const picojson::value& metrics = result.get("primary_metrics");
        require(metrics.get("cosine_similarity").get<double>() >= -1.0 &&
                    metrics.get("cosine_similarity").get<double>() <= 1.0 &&
                    metrics.get("cosine_distance").get<double>() >= 0.0 &&
                    metrics.get("cosine_distance").get<double>() <= 2.0 &&
                    metrics.get("angular_distance_radians").get<double>() >= 0.0 &&
                    metrics.get("angular_distance_radians").get<double>() <=
                        osg::PI,
                "artifact comparison returned unbounded 64D metrics");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        require(tools.dispatch(
                    "start_science_research", picojson::value(seriesArgs), result),
                "mismatched comparison research did not dispatch");
        providerPointer->publishReady(
            "mismatched-processing-artifact", 0, "test-processing-v2");
        service.snapshot();
        compareArgs["right_artifact_id"] =
            picojson::value("mismatched-processing-artifact");
        require(tools.dispatch(
                    "compare_science_artifacts", picojson::value(compareArgs), result) &&
                    result.contains("error"),
                "artifact comparison accepted different processing versions");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        require(tools.dispatch(
                    "start_science_research", picojson::value(seriesArgs), result),
                "mismatched source-version research did not dispatch");
        providerPointer->publishReady(
            "mismatched-source-artifact", 0, "test-processing-v1", "2.0");
        service.snapshot();
        compareArgs["right_artifact_id"] =
            picojson::value("mismatched-source-artifact");
        require(tools.dispatch(
                    "compare_science_artifacts", picojson::value(compareArgs), result) &&
                    result.contains("error"),
                "artifact comparison accepted different source evidence");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        layer->setVisible(false);
        picojson::object showArgs;
        showArgs["artifact_id"] = picojson::value("left-artifact");
        require(tools.dispatch(
                    "show_science_artifact", picojson::value(showArgs), result) &&
                    service.snapshot().displayArtifact &&
                    service.snapshot().displayArtifact->artifactId == "left-artifact",
                "show did not explicitly materialize a retained artifact id");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        const std::string displayBeforeInvalidShow =
            service.snapshot().displayArtifact->artifactId;
        picojson::object invalidShowArgs;
        invalidShowArgs["artifact_id"] = picojson::value(17.0);
        require(tools.dispatch(
                    "show_science_artifact", picojson::value(invalidShowArgs),
                    result) &&
                    result.contains("error") &&
                    service.snapshot().displayArtifact->artifactId ==
                        displayBeforeInvalidShow,
                "show accepted a non-string artifact_id");
        invalidShowArgs["artifact_id"] = picojson::value("right-artifact");
        invalidShowArgs["job_id"] = picojson::value("not-an-integer");
        require(tools.dispatch(
                    "show_science_artifact", picojson::value(invalidShowArgs),
                    result) &&
                    result.contains("error") &&
                    service.snapshot().displayArtifact->artifactId ==
                        displayBeforeInvalidShow,
                "show mutated display before validating job_id type");
        invalidShowArgs["job_id"] = picojson::value(999999.0);
        require(tools.dispatch(
                    "show_science_artifact", picojson::value(invalidShowArgs),
                    result) &&
                    result.contains("error") &&
                    service.snapshot().displayArtifact->artifactId ==
                        displayBeforeInvalidShow,
                "show accepted mismatched artifact_id and job_id");
        const std::shared_ptr<const earthscience::ScienceArtifact> rightArtifact =
            service.findArtifact("right-artifact");
        require(static_cast<bool>(rightArtifact),
                "right comparison artifact was not retained");
        invalidShowArgs["job_id"] = picojson::value(
            static_cast<double>(rightArtifact->generation));
        require(tools.dispatch(
                    "show_science_artifact", picojson::value(invalidShowArgs),
                    result) &&
                    result.get("ok").get<bool>() &&
                    service.snapshot().displayArtifact->artifactId ==
                        "right-artifact",
                "show rejected matching artifact_id and job_id");
        requireMatrixUnchanged(originalMatrix, *manipulator);

        layer->setVisible(false);
        layers.setEnabled("alphaearth", false);
        picojson::object changeArgs;
        changeArgs["lat"] = picojson::value(35.36);
        changeArgs["lon"] = picojson::value(138.73);
        changeArgs["baseline_year"] = picojson::value(2019.0);
        changeArgs["comparison_year"] = picojson::value(2022.0);
        require(tools.dispatch(
                    "run_change_analysis", picojson::value(changeArgs), result),
                "change analysis did not dispatch");
        require(providerPointer->lastQuery.analysis.kind ==
                    earthscience::ScienceAnalysisKind::RegionalChange &&
                    providerPointer->lastQuery.time.explicitYears ==
                        std::vector<int>({2019, 2022}) &&
                    !layer->isVisible() && layers.find("alphaearth") &&
                    !layers.find("alphaearth")->enabled,
                "change analysis did not submit a hidden regional query");
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
