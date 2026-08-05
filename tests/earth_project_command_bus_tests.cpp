// Renderer-independent command bus tests. The bus mutates only EarthProject
// value state; runtime render callbacks and artifact storage stay outside it.
#include <cstdlib>
#include <iostream>
#include <string>

#include "../applications/earth_explorer/project/earth_project.cpp"
#include "../applications/earth_explorer/project/earth_project_command_bus.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; \
    std::exit(EXIT_FAILURE); } } while (0)

namespace
{
    using namespace earthproject;

    EarthProject initialProject()
    {
        EarthProject project;
        project.workspace.activeModule = "explore";
        project.workspace.drawerOpen = true;

        EarthLayerDescriptor layer;
        layer.id = "alphaearth";
        layer.displayName = "AlphaEarth Foundations";
        layer.render.visible = true;
        layer.render.opacity = 0.8;
        layer.temporal.mode = TemporalMode::Discrete;
        layer.temporal.currentValue = "2024";
        layer.temporal.availableValues.push_back("2023");
        layer.temporal.availableValues.push_back("2024");
        layer.temporal.availableValues.push_back("2025");
        layer.capabilities.temporal = true;
        project.layers.push_back(layer);
        return project;
    }

    EarthProjectCommand command(
        const std::string& id, CommandOrigin origin,
        ProjectCommandKind kind)
    {
        EarthProjectCommand value;
        value.id = id;
        value.origin = origin;
        value.kind = kind;
        return value;
    }

    void testOneEnvelopeRecordsEveryOrigin()
    {
        EarthProjectCommandBus bus(initialProject(), 8u);

        EarthProjectCommand user = command(
            "user-hide-alphaearth", CommandOrigin::User,
            ProjectCommandKind::SetLayerVisibility);
        user.targetId = "alphaearth";
        user.boolValue = false;
        CHECK(bus.execute(user).ok);
        CHECK(!bus.project().layers[0].render.visible);

        EarthProjectCommand ai = command(
            "ai-open-science", CommandOrigin::AI,
            ProjectCommandKind::SetActiveModule);
        ai.textValue = "science";
        CHECK(bus.execute(ai).ok);
        CHECK(bus.project().workspace.activeModule == "science");

        EarthProjectCommand automation = command(
            "automation-collapse-drawer", CommandOrigin::Automation,
            ProjectCommandKind::SetDrawerOpen);
        automation.boolValue = false;
        CHECK(bus.execute(automation).ok);
        CHECK(!bus.project().workspace.drawerOpen);

        const CommandHistorySnapshot history = bus.historySnapshot(8u);
        CHECK(history.applied.size() == 3u);
        CHECK(history.applied[0].origin == CommandOrigin::User);
        CHECK(history.applied[1].origin == CommandOrigin::AI);
        CHECK(history.applied[2].origin == CommandOrigin::Automation);
    }

    void testInvalidTargetDoesNotMutateOrEnterHistory()
    {
        EarthProjectCommandBus bus(initialProject(), 8u);
        const std::string before = saveProjectJson(bus.project());

        EarthProjectCommand invalid = command(
            "ai-hide-missing", CommandOrigin::AI,
            ProjectCommandKind::SetLayerVisibility);
        invalid.targetId = "plugin-not-installed";
        invalid.boolValue = false;
        const CommandExecutionResult result = bus.execute(invalid);

        CHECK(!result.ok);
        CHECK(result.errorCode == "unknown_layer");
        CHECK(saveProjectJson(bus.project()) == before);
        CHECK(bus.historySnapshot(8u).applied.empty());
    }

    void testOpacityAndTimeEditsCoalesceWithoutLosingUndoOrigin()
    {
        EarthProjectCommandBus bus(initialProject(), 8u);

        EarthProjectCommand opacity = command(
            "opacity-drag-1", CommandOrigin::User,
            ProjectCommandKind::SetLayerOpacity);
        opacity.targetId = "alphaearth";
        opacity.coalesceKey = "layer:alphaearth:opacity";
        opacity.numberValue = 0.7;
        CHECK(bus.execute(opacity).ok);

        opacity.id = "opacity-drag-2";
        opacity.numberValue = 0.6;
        const CommandExecutionResult opacityResult = bus.execute(opacity);
        CHECK(opacityResult.ok);
        CHECK(opacityResult.coalesced);
        CHECK(bus.project().layers[0].render.opacity == 0.6);
        CHECK(bus.historySnapshot(8u).applied.size() == 1u);
        CHECK(bus.undo());
        CHECK(bus.project().layers[0].render.opacity == 0.8);
        CHECK(bus.redo());
        CHECK(bus.project().layers[0].render.opacity == 0.6);

        EarthProjectCommand time = command(
            "time-drag-1", CommandOrigin::AI,
            ProjectCommandKind::SetLayerTime);
        time.targetId = "alphaearth";
        time.coalesceKey = "layer:alphaearth:time";
        time.textValue = "2025";
        CHECK(bus.execute(time).ok);
        CHECK(bus.project().layers[0].temporal.currentValue == "2025");

        time.id = "time-drag-2";
        time.textValue = "2023";
        CHECK(bus.execute(time).coalesced);
        CHECK(bus.project().layers[0].temporal.currentValue == "2023");
        CHECK(bus.historySnapshot(8u).applied.size() == 2u);
        CHECK(bus.undo());
        CHECK(bus.project().layers[0].temporal.currentValue == "2024");
    }

    void testHistoryIsBoundedAndNewExecutionClearsRedo()
    {
        EarthProjectCommandBus bus(initialProject(), 2u);

        EarthProjectCommand first = command(
            "module-science", CommandOrigin::User,
            ProjectCommandKind::SetActiveModule);
        first.textValue = "science";
        CHECK(bus.execute(first).ok);

        EarthProjectCommand second = command(
            "drawer-close", CommandOrigin::User,
            ProjectCommandKind::SetDrawerOpen);
        second.boolValue = false;
        CHECK(bus.execute(second).ok);

        EarthProjectCommand third = command(
            "layer-hide", CommandOrigin::User,
            ProjectCommandKind::SetLayerVisibility);
        third.targetId = "alphaearth";
        third.boolValue = false;
        CHECK(bus.execute(third).ok);
        CHECK(bus.historySnapshot(8u).applied.size() == 2u);

        CHECK(bus.undo());
        CHECK(bus.undo());
        CHECK(!bus.undo());
        CHECK(bus.project().workspace.activeModule == "science");

        EarthProjectCommand replacement = command(
            "drawer-close-again", CommandOrigin::Automation,
            ProjectCommandKind::SetDrawerOpen);
        replacement.boolValue = false;
        CHECK(bus.execute(replacement).ok);
        CHECK(!bus.redo());
        CHECK(bus.historySnapshot(8u).undone.empty());
    }

    void testArtifactAssociationIsUndoableWithoutErasingAuditReference()
    {
        EarthProjectCommandBus bus(initialProject(), 8u);
        EarthProjectCommand associate = command(
            "associate-era5-result", CommandOrigin::AI,
            ProjectCommandKind::AssociateArtifact);
        associate.targetId = "alphaearth";
        associate.textValue = "era5-agro-artifact-17";

        CHECK(bus.execute(associate).ok);
        CHECK(bus.project().layers[0].artifactId ==
              "era5-agro-artifact-17");
        CHECK(bus.project().workspace.activeArtifactIds.size() == 1u);
        CHECK(bus.project().workspace.activeArtifactIds[0] ==
              "era5-agro-artifact-17");

        CHECK(bus.undo());
        CHECK(bus.project().layers[0].artifactId.empty());
        CHECK(bus.project().workspace.activeArtifactIds.empty());
        const std::vector<std::string> audited =
            bus.referencedArtifactIds();
        CHECK(audited.size() == 1u);
        CHECK(audited[0] == "era5-agro-artifact-17");
        CHECK(bus.historySnapshot(8u).undone[0].textValue ==
              "era5-agro-artifact-17");

        CHECK(bus.redo());
        CHECK(bus.project().layers[0].artifactId ==
              "era5-agro-artifact-17");

        EarthProjectCommand invalid = associate;
        invalid.id = "associate-empty";
        invalid.textValue.clear();
        const std::size_t before =
            bus.historySnapshot(8u).applied.size();
        const CommandExecutionResult result = bus.execute(invalid);
        CHECK(!result.ok);
        CHECK(result.errorCode == "invalid_artifact_id");
        CHECK(bus.historySnapshot(8u).applied.size() == before);
    }
}

int main()
{
    testOneEnvelopeRecordsEveryOrigin();
    testInvalidTargetDoesNotMutateOrEnterHistory();
    testOpacityAndTimeEditsCoalesceWithoutLosingUndoOrigin();
    testHistoryIsBoundedAndNewExecutionClearsRedo();
    testArtifactAssociationIsUndoableWithoutErasingAuditReference();
    std::cout << "EarthProject command bus tests OK\n";
    return 0;
}
