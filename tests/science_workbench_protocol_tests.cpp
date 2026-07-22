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
}

int main()
{
    testSnapshotIsDeterministicAndBounded();
    testActionValidation();
    std::cout << "ScienceWorkbenchProtocol tests passed" << std::endl;
    return 0;
}
