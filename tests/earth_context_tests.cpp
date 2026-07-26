// tests/earth_context_tests.cpp
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "../applications/earth_explorer/ai_tools.h"
#include "../applications/earth_explorer/earth_context.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; \
    std::exit(1); } } while (0)

namespace
{
    picojson::value parseJson(const std::string& text)
    {
        picojson::value value;
        CHECK(picojson::parse(value, text).empty());
        return value;
    }
}

int main()
{
    using namespace earthai;

    std::shared_ptr<EarthContextHub> hub(new EarthContextHub);
    int workspaceRevision = 1;
    hub->upsertSection(
        "workspace", "front_end", 100,
        [&workspaceRevision]() {
            picojson::object value;
            value["activeModule"] = picojson::value("science");
            value["drawerOpen"] = picojson::value(true);
            value["revision"] =
                picojson::value(static_cast<double>(workspaceRevision));
            return picojson::value(value);
        });
    hub->upsertSection(
        "scienceWorkbench", "report_and_sources", 90,
        []() {
            return parseJson(
                "{\"activeArtifact\":{\"artifactId\":\"era5-3\","
                "\"series\":[{\"variableId\":\"temperature_2m_mean\","
                "\"unit\":\"C\",\"values\":[22.7,22.589]}]},"
                "\"sources\":[{\"id\":\"era5-agricultural-climate\","
                "\"health\":\"ready\"}]}");
        });
    hub->upsertSection(
        "brokenPanel", "front_end", 80,
        []() -> picojson::value {
            throw std::runtime_error("panel disappeared");
        });

    const std::string firstJson = hub->snapshotJson(64 * 1024);
    CHECK(firstJson.size() <= 64 * 1024);
    const picojson::value first = parseJson(firstJson);
    CHECK(first.get("schema").to_str() == "earth-context-v1");
    CHECK(first.get("sections").get("workspace")
              .get("activeModule").to_str() == "science");
    CHECK(first.get("sections").get("scienceWorkbench")
              .get("activeArtifact").get("artifactId").to_str() ==
          "era5-3");
    CHECK(first.get("sectionStatus").get("brokenPanel")
              .get("status").to_str() == "unavailable");
    CHECK(first.get("sectionStatus").get("brokenPanel")
              .get("error").to_str().find("panel disappeared") !=
          std::string::npos);

    workspaceRevision = 2;
    const picojson::value second = parseJson(hub->snapshotJson(64 * 1024));
    CHECK(second.get("revision").get<double>() >
          first.get("revision").get<double>());
    CHECK(second.get("sections").get("workspace")
              .get("revision").get<double>() == 2.0);

    hub->upsertSection(
        "oversized", "diagnostic", 1,
        []() {
            picojson::object value;
            value["payload"] = picojson::value(std::string(96 * 1024, 'x'));
            return picojson::value(value);
        });
    const std::string boundedJson = hub->snapshotJson(32 * 1024);
    CHECK(boundedJson.size() <= 32 * 1024);
    const picojson::value bounded = parseJson(boundedJson);
    CHECK(bounded.get("sectionStatus").get("oversized")
              .get("status").to_str() == "omitted_oversize");
    CHECK(!bounded.get("sections").contains("oversized"));

    ToolRegistry tools;
    tools.add(makeEarthContextTool(hub));
    picojson::value toolResult;
    CHECK(tools.dispatch(
        "get_earth_context",
        parseJson("{\"section\":\"scienceWorkbench\"}"),
        toolResult));
    CHECK(toolResult.get("schema").to_str() == "earth-context-section-v1");
    CHECK(toolResult.get("sectionId").to_str() == "scienceWorkbench");
    CHECK(toolResult.get("value").get("activeArtifact")
              .get("artifactId").to_str() == "era5-3");

    CHECK(tools.dispatch(
        "get_earth_context",
        parseJson("{\"section\":\"missing\"}"),
        toolResult));
    CHECK(toolResult.contains("error"));

    std::cout << "EarthContextHub tests OK\n";
    return 0;
}
