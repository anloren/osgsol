#ifndef OSGSOL_EARTH_PROJECT_COMMAND_BUS_H
#define OSGSOL_EARTH_PROJECT_COMMAND_BUS_H

#include "earth_project.h"

#include <cstddef>
#include <string>
#include <vector>

namespace earthproject
{
    constexpr std::size_t kDefaultCommandHistoryCapacity = 128u;
    constexpr std::size_t kMaximumCommandHistoryCapacity = 1024u;
    constexpr std::size_t kMaximumCommandIdBytes = 256u;
    constexpr std::size_t kMaximumCommandLabelBytes = 512u;
    constexpr std::size_t kMaximumCommandValueBytes = 1024u;

    enum class CommandOrigin
    {
        User,
        AI,
        Automation
    };

    enum class ProjectCommandKind
    {
        SetLayerVisibility,
        SetLayerOpacity,
        SetLayerTime,
        SetActiveModule,
        SetDrawerOpen,
        AssociateArtifact
    };

    // One renderer-independent envelope for human, AI and automated changes.
    // Only the value field matching kind is interpreted.
    struct EarthProjectCommand
    {
        std::string id;
        CommandOrigin origin = CommandOrigin::User;
        ProjectCommandKind kind = ProjectCommandKind::SetLayerVisibility;
        std::string targetId;
        std::string label;
        std::string coalesceKey;
        bool boolValue = false;
        double numberValue = 0.0;
        std::string textValue;
    };

    struct CommandExecutionResult
    {
        bool ok = false;
        bool changed = false;
        bool coalesced = false;
        std::string errorCode;
        std::string errorMessage;
    };

    struct CommandHistorySnapshot
    {
        std::size_t capacity = 0u;
        std::size_t omittedApplied = 0u;
        std::size_t omittedUndone = 0u;
        std::vector<EarthProjectCommand> applied;
        std::vector<EarthProjectCommand> undone;
    };

    class EarthProjectCommandBus
    {
    public:
        explicit EarthProjectCommandBus(
            const EarthProject& initialProject = EarthProject(),
            std::size_t capacity = kDefaultCommandHistoryCapacity);

        const EarthProject& project() const { return _project; }
        std::size_t capacity() const { return _capacity; }

        CommandExecutionResult execute(const EarthProjectCommand& command);
        bool undo();
        bool redo();

        CommandHistorySnapshot historySnapshot(
            std::size_t maxApplied) const;
        std::vector<std::string> referencedArtifactIds() const;

    private:
        struct EntryState
        {
            bool boolValue = false;
            double numberValue = 0.0;
            std::string textValue;
        };

        struct Entry
        {
            EarthProjectCommand command;
            EntryState before;
            EntryState after;
        };

        void applyEntryState(const Entry& entry, const EntryState& state);

        EarthProject _project;
        std::size_t _capacity;
        std::vector<Entry> _undo;
        std::vector<Entry> _redo;
    };
}

#endif
