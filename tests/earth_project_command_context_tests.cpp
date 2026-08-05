#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include "../applications/earth_explorer/project/earth_project.cpp"
#include "../applications/earth_explorer/project/earth_project_command_bus.cpp"
#include "../applications/earth_explorer/earth_context.cpp"
#include "../applications/earth_explorer/project/earth_project_command_context.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; \
    std::exit(EXIT_FAILURE); } } while (0)

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
    using namespace earthproject;

    std::shared_ptr<EarthProjectCommandBus> bus(
        new EarthProjectCommandBus(EarthProject(), 64u));
    for (int index = 0; index < 24; ++index)
    {
        EarthProjectCommand command;
        command.id = "command-" + std::to_string(index);
        command.origin = index % 2 == 0
            ? CommandOrigin::User : CommandOrigin::Automation;
        command.kind = ProjectCommandKind::SetActiveModule;
        command.textValue = "module-" + std::to_string(index);
        command.label = std::string(4096u, 'L');
        CHECK(bus->execute(command).ok);
    }

    EarthProjectCommand artifact;
    artifact.id = "ai-associate-artifact";
    artifact.origin = CommandOrigin::AI;
    artifact.kind = ProjectCommandKind::AssociateArtifact;
    artifact.textValue = "science-artifact-42";
    CHECK(bus->execute(artifact).ok);
    CHECK(bus->undo());

    const picojson::value direct = commandHistoryContextValue(
        *bus, 8u, 4096u);
    const CommandHistorySnapshot retained = bus->historySnapshot(64u);
    CHECK(!retained.applied.empty());
    CHECK(retained.applied[0].label.size() <=
          kMaximumCommandLabelBytes);
    CHECK(direct.serialize().size() <= 4096u);
    CHECK(direct.get("schema").to_str() ==
          "earth-project-command-history-v1");
    CHECK(direct.get("applied").get<picojson::array>().size() <= 8u);
    CHECK(direct.get("omittedApplied").get<double>() > 0.0);
    CHECK(direct.get("canUndo").get<bool>());
    CHECK(direct.get("canRedo").get<bool>());
    CHECK(direct.get("undone").get<picojson::array>()[0]
              .get("origin").to_str() == "ai");
    CHECK(direct.get("undone").get<picojson::array>()[0]
              .get("kind").to_str() == "associate_artifact");
    CHECK(direct.get("artifactIds").get<picojson::array>()[0]
              .to_str() == "science-artifact-42");

    std::shared_ptr<earthai::EarthContextHub> hub(
        new earthai::EarthContextHub);
    registerProjectCommandContext(hub, bus);
    const picojson::value snapshot = parseJson(hub->snapshotJson(32u * 1024u));
    CHECK(snapshot.get("sections").contains("commandHistory"));
    CHECK(snapshot.get("sections").get("commandHistory")
              .get("schema").to_str() ==
          "earth-project-command-history-v1");
    CHECK(snapshot.get("sectionStatus").get("commandHistory")
              .get("status").to_str() == "ready");

    std::cout << "EarthProject command context tests OK\n";
    return 0;
}
