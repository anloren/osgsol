#include "science_workbench_protocol.h"

#include <picojson.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

ScienceWorkbenchModel readyModel()
{
    ScienceWorkbenchModel model;
    earthscience::ScienceSourceDescriptor temporal;
    temporal.id = "era5-agricultural-climate";
    temporal.firstYear = 1940;
    temporal.lastYear = 2025;
    model.configureTemporalSources({temporal});
    ScienceWorkbenchAction source;
    source.kind = ScienceWorkbenchActionKind::SelectSource;
    source.sourceId = "era5-agricultural-climate";
    std::string error;
    expect(model.dispatch(source, error), "source selection must succeed");

    earthscience::ScienceGeometry center;
    center.kind = earthscience::ScienceGeometryKind::Point;
    center.point = {24.3658, 104.1796};
    earthscience::ScienceGeometry view;
    view.kind = earthscience::ScienceGeometryKind::BoundingBox;
    view.bounds = {100.0, 20.0, 108.0, 28.0};
    model.updateLiveCameraContext(center, view);

    ScienceWorkbenchAction lock;
    lock.kind = ScienceWorkbenchActionKind::LockMapCenter;
    expect(model.dispatch(lock, error), "target lock must succeed");

    ScienceWorkbenchAction years;
    years.kind = ScienceWorkbenchActionKind::SetYearRange;
    years.firstYear = 2017;
    years.lastYear = 2025;
    expect(model.dispatch(years, error), "year range must succeed");
    return model;
}

earthscience::ScienceSourceDescriptor sourceDescriptor()
{
    earthscience::ScienceSourceDescriptor source;
    source.id = "era5-agricultural-climate";
    source.name = "ERA5 agricultural climate";
    source.category = "agricultural climate";
    source.firstYear = 1940;
    source.lastYear = 2025;
    source.nativeResolutionMeters = 25000.0;
    source.spatialSupport = "nearest provider grid cell";
    source.temporalResolution = "annual summaries from daily values";
    source.license = "Copernicus License";
    source.documentationUrl = "https://cds.climate.copernicus.eu/";
    source.attribution = "ECMWF Copernicus Climate Change Service";
    source.qualityStatement = "Provider grid-cell values; not a field average";
    source.health = earthscience::ScienceSourceHealth::Ready;
    source.capabilities.pointQuery = true;
    source.capabilities.explicitYears = true;
    source.capabilities.timeSeriesOutput = true;
    earthscience::ScienceVariableDescriptor variable;
    variable.id = "temperature_2m_mean";
    variable.displayName = "Mean temperature at 2 m";
    variable.unit = "°C";
    variable.aggregationMethod = "annual mean of daily mean";
    source.variables.push_back(variable);
    return source;
}

std::shared_ptr<earthscience::ScienceArtifact> evidenceArtifact()
{
    auto artifact = std::make_shared<earthscience::ScienceArtifact>();
    artifact->artifactId = "era5-evidence-a";
    artifact->query.sourceId = "era5-agricultural-climate";
    artifact->query.geometry.kind = earthscience::ScienceGeometryKind::Point;
    artifact->query.geometry.point = {24.3658, 104.1796};
    artifact->processingVersion = "era5-agro-v1";
    artifact->createdAt = "2026-07-22T03:00:00Z";
    artifact->warnings = {"2020 contains a missing annual value"};

    earthscience::ScienceVariableSeries series;
    series.variableId = "temperature_2m_mean";
    series.displayName = "Mean temperature at 2 m";
    series.unit = "°C";
    series.aggregationMethod = "annual mean of daily mean";
    series.nativeResolutionMeters = 25000.0;
    series.years = std::make_shared<const std::vector<int>>(
        std::initializer_list<int>{2019, 2020, 2021});
    series.values = std::make_shared<const std::vector<double>>(
        std::initializer_list<double>{18.0, 0.0, 18.6});
    series.validity = std::make_shared<const std::vector<unsigned char>>(
        std::initializer_list<unsigned char>{1, 0, 1});
    artifact->variableSeries.push_back(series);

    earthscience::ScienceSourceReference reference;
    reference.sourceId = "era5-agricultural-climate";
    reference.providerVersion = "ERA5-Land 2026-06";
    reference.datasetId = "reanalysis-era5-land";
    reference.originalUrl = "https://cds.climate.copernicus.eu/datasets/reanalysis-era5-land";
    reference.requestedCoverage = artifact->query.geometry;
    reference.actualCoverage = {104.0, 24.25, 104.25, 24.5};
    reference.variables = {"temperature_2m_mean"};
    reference.units = {"°C"};
    reference.processingSteps = {"daily values", "annual mean"};
    reference.publicationTime = "2026-06";
    reference.attribution = "ECMWF Copernicus Climate Change Service";
    artifact->sourceReferences.push_back(reference);
    artifact->analysis.limitations =
        std::make_shared<const std::vector<std::string>>(
            std::initializer_list<std::string>{
                "Nearest provider grid cell; not a regional average"});
    return artifact;
}

void testSnapshotIsDeterministicAndBounded()
{
    ScienceWorkbenchModel model = readyModel();
    const std::vector<earthscience::ScienceSourceDescriptor> sources = {
        sourceDescriptor()};
    const std::string first = serializeScienceWorkbenchSnapshot(
        model.viewModel(), sources, nullptr);
    const std::string second = serializeScienceWorkbenchSnapshot(
        model.viewModel(), sources, nullptr);
    expect(first == second, "snapshot serialization must be deterministic");
    expect(first.size() < OSGSOL_SCIENCE_UI_SNAPSHOT_MAX_BYTES,
           "normal snapshot must fit protocol bound");

    picojson::value parsed;
    const std::string parseError = picojson::parse(parsed, first);
    expect(parseError.empty(), "snapshot must be valid JSON");
    const picojson::object& root = parsed.get<picojson::object>();
    expect(root.at("schema").get<std::string>() == "science-workbench-ui-v1",
           "snapshot schema must be versioned");
    expect(root.at("phase").get<std::string>() == "ready-to-run",
           "snapshot must expose workbench phase");
    expect(root.at("target").get<picojson::object>()
               .at("locked").get<bool>(),
           "snapshot must expose target lock");
    expect(root.at("sources").get<picojson::array>().size() == 1,
           "snapshot must expose source descriptors");
    const picojson::object& temporal =
        root.at("temporal").get<picojson::object>();
    expect(temporal.at("loadState").get<std::string>() == "idle",
           "snapshot must distinguish a selected request from loading");
    expect(temporal.at("requested").get<picojson::object>()
               .at("values").get<picojson::array>().size() == 9,
           "snapshot must expose the requested years");
    expect(temporal.at("availability").get<picojson::object>()
               .at("firstValue").get<std::string>() == "1940",
           "snapshot must expose provider availability separately");
    expect(!temporal.at("hasApplied").get<bool>(),
           "snapshot must not claim an unapplied selection is on the map");

    OsgSolScienceUiBufferV1 sizeProbe = {};
    sizeProbe.structSize = sizeof(sizeProbe);
    expect(!copyScienceWorkbenchSnapshot(first, &sizeProbe),
           "size probe must not claim a copy");
    expect(sizeProbe.bytesRequired == first.size() + 1,
           "size probe must return exact required bytes");

    std::vector<char> shortBuffer(first.size(), 'x');
    OsgSolScienceUiBufferV1 shortCopy = {};
    shortCopy.structSize = sizeof(shortCopy);
    shortCopy.utf8 = shortBuffer.data();
    shortCopy.capacity = shortBuffer.size();
    expect(!copyScienceWorkbenchSnapshot(first, &shortCopy),
           "undersized buffer must fail");
    expect(shortCopy.bytesWritten == 0,
           "undersized buffer must not publish partial JSON");

    std::vector<char> buffer(first.size() + 1, '\0');
    OsgSolScienceUiBufferV1 copy = {};
    copy.structSize = sizeof(copy);
    copy.utf8 = buffer.data();
    copy.capacity = buffer.size();
    expect(copyScienceWorkbenchSnapshot(first, &copy),
           "sized buffer must copy snapshot");
    expect(copy.bytesWritten == first.size(),
           "copy must report bytes excluding terminator");
    expect(std::string(buffer.data()) == first,
           "copied snapshot must match source");
}

void testProcessingStateIsVisibleToAiContext()
{
    ScienceWorkbenchModel model = readyModel();
    auto processing = std::make_shared<earthscience::ScienceProcessingRecord>();
    processing->recordId = "processing-1";
    processing->liveJobId = 7;
    processing->capabilityId =
        "science-provider/era5-agricultural-climate";
    processing->sourceId = "era5-agricultural-climate";
    processing->state = earthscience::ScienceJobState::Fetching;
    processing->cost.sourceBytesUpperBound = 1200;
    processing->progress.stage = earthscience::ScienceProgressStage::Reading;
    processing->progress.completedUnits = 3;
    processing->progress.totalUnits = 9;
    processing->progress.determinate = true;
    processing->progress.unit = "year";
    processing->message = "Reading";
    processing->createdAt = "2026-08-05T10:00:00Z";
    processing->updatedAt = "2026-08-05T10:00:01Z";
    earthscience::ScienceJobSnapshot job;
    job.jobId = 7;
    job.state = earthscience::ScienceJobState::Fetching;
    job.progress = processing->progress;
    job.processing = processing;
    model.applyJobSnapshot(job);

    earthscience::ScienceProcessingRegistry registry;
    std::string error;
    expect(registry.addBuiltInSource(sourceDescriptor(), error),
           "test processing capability must register");
    for (const auto& capability :
         earthscience::optionalScienceProcessingCapabilities())
        expect(registry.add(capability, error),
               "optional processing capability must register");
    const std::string snapshot = serializeScienceWorkbenchSnapshot(
        model.viewModel(), {sourceDescriptor()}, nullptr, registry.list());
    picojson::value parsed;
    expect(picojson::parse(parsed, snapshot).empty(),
           "processing context snapshot must parse");
    const picojson::object& root = parsed.get<picojson::object>();
    const picojson::object& state =
        root.at("processing").get<picojson::object>();
    expect(state.at("recordId").get<std::string>() == "processing-1" &&
               state.at("capabilityId").get<std::string>() ==
                   "science-provider/era5-agricultural-climate" &&
               state.at("progress").get<picojson::object>()
                   .at("completedUnits").get<double>() == 3.0,
           "AI context lost processing identity or progress");
    const picojson::array& capabilities =
        root.at("processingCapabilities").get<picojson::array>();
    expect(capabilities.size() == 4,
           "AI context did not receive built-in and optional capabilities");
    expect(!capabilities.back().get<picojson::object>()
                .at("available").get<bool>(),
           "unloaded optional engine must remain explicitly unavailable");
}

void testActionValidation()
{
    ScienceWorkbenchAction action;
    std::string error;
    const std::string years =
        R"({"schema":"science-workbench-action-v1","action":"set-year-range","firstYear":2017,"lastYear":2025})";
    expect(parseScienceWorkbenchAction(years.data(), years.size(), action, error),
           "valid year action must parse");
    expect(action.kind == ScienceWorkbenchActionKind::SetYearRange,
           "year action kind must match");
    expect(action.firstYear == 2017 && action.lastYear == 2025,
           "year action values must match");

    const std::string unknown =
        R"({"schema":"science-workbench-action-v1","action":"teleport"})";
    expect(!parseScienceWorkbenchAction(
        unknown.data(), unknown.size(), action, error),
        "unknown action must be rejected");
    expect(error == "workbench-action-unknown",
           "unknown action must have stable error");

    const std::string reversed =
        R"({"schema":"science-workbench-action-v1","action":"set-year-range","firstYear":2025,"lastYear":2017})";
    expect(!parseScienceWorkbenchAction(
        reversed.data(), reversed.size(), action, error),
        "reversed year range must be rejected");
    expect(error == "workbench-year-range-invalid",
           "reversed years must have stable error");

    std::string invalidUtf8 =
        "{\"schema\":\"science-workbench-action-v1\","
        "\"action\":\"select-source\",\"sourceId\":\"";
    invalidUtf8.push_back(static_cast<char>(0xc0));
    invalidUtf8.push_back(static_cast<char>(0xaf));
    invalidUtf8 += "\"}";
    expect(!parseScienceWorkbenchAction(
        invalidUtf8.data(), invalidUtf8.size(), action, error),
        "invalid UTF-8 must be rejected");
    expect(error == "workbench-action-invalid-utf8",
           "invalid UTF-8 must have stable error");

    const std::string oversized(
        OSGSOL_SCIENCE_UI_ACTION_MAX_BYTES + 1, 'x');
    expect(!parseScienceWorkbenchAction(
        oversized.data(), oversized.size(), action, error),
        "oversized action must be rejected before parsing");
    expect(error == "workbench-action-too-large",
           "oversized action must have stable error");
}


void testReportEvidenceUsesOnlyDeclaredFacts()
{
    ScienceWorkbenchModel model = readyModel();
    earthscience::ScienceProgress progress;
    progress.elapsedSeconds = 3.75;
    model.applyProgress(progress);
    const std::shared_ptr<earthscience::ScienceArtifact> artifact =
        evidenceArtifact();
    model.applyArtifact(artifact);
    const std::string snapshot = serializeScienceWorkbenchSnapshot(
        model.viewModel(), {sourceDescriptor()}, artifact);
    picojson::value parsed;
    expect(picojson::parse(parsed, snapshot).empty(),
           "evidence snapshot must parse");
    const picojson::object& root = parsed.get<picojson::object>();
    const picojson::object& evidence =
        root.at("reportEvidence").get<picojson::object>();
    expect(evidence.at("availability").get<std::string>() == "ready",
           "active artifact evidence must be ready");
    expect(evidence.at("requestedGeometry").is<picojson::object>() &&
               evidence.at("actualCoverage").is<picojson::object>(),
           "requested and actual spatial facts must stay separate");
    expect(evidence.at("sourceNativeResolutionMeters").get<double>() ==
               25000.0,
           "declared source resolution must be preserved");
    const picojson::array& completeness =
        evidence.at("seriesCompleteness").get<picojson::array>();
    expect(completeness.size() == 1 &&
               completeness.front().get<picojson::object>()
                   .at("validPoints").get<double>() == 2.0,
           "series completeness must count declared validity");
    expect(evidence.at("executionTimingSeconds").get<double>() == 3.75,
           "observed execution time must be exposed");
    expect(evidence.at("limitationsAvailability").get<std::string>() ==
               "provided",
           "declared limitations must be distinguishable from missing facts");
    expect(evidence.at("citations").get<picojson::array>().size() == 1,
           "provider citation must be retained");
    expect(evidence.at("exportCapabilities").get<picojson::object>()
               .at("artifactExport").is<bool>(),
           "declared export capability must be explicit");

    const std::string emptySnapshot = serializeScienceWorkbenchSnapshot(
        readyModel().viewModel(), {}, nullptr);
    expect(picojson::parse(parsed, emptySnapshot).empty(),
           "empty evidence snapshot must parse");
    const picojson::object& unavailable = parsed.get<picojson::object>()
        .at("reportEvidence").get<picojson::object>();
    expect(unavailable.at("availability").get<std::string>() ==
               "unavailable" &&
               unavailable.at("actualCoverage").is<picojson::null>() &&
               unavailable.at("executionTimingSeconds").is<picojson::null>(),
           "missing evidence must be explicit null, never inferred");
}
}

int main()
{
    testSnapshotIsDeterministicAndBounded();
    testProcessingStateIsVisibleToAiContext();
    testActionValidation();
    testReportEvidenceUsesOnlyDeclaredFacts();
    std::cout << "ScienceWorkbenchProtocol tests passed" << std::endl;
    return 0;
}
