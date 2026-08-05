#include "earth_project_command_bus.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace earthproject
{
    namespace
    {
        EarthLayerDescriptor* findLayer(
            EarthProject& project, const std::string& id)
        {
            for (EarthLayerDescriptor& layer : project.layers)
                if (layer.id == id) return &layer;
            return nullptr;
        }

        CommandExecutionResult failure(
            const std::string& code, const std::string& message)
        {
            CommandExecutionResult result;
            result.errorCode = code;
            result.errorMessage = message;
            return result;
        }

        std::string boundedMetadata(
            const std::string& value, std::size_t maximum)
        {
            return value.size() <= maximum
                ? value : value.substr(0u, maximum);
        }

        bool canCoalesce(const EarthProjectCommand& previous,
                         const EarthProjectCommand& current)
        {
            const bool continuous =
                current.kind == ProjectCommandKind::SetLayerOpacity ||
                current.kind == ProjectCommandKind::SetLayerTime;
            return continuous && !current.coalesceKey.empty() &&
                previous.coalesceKey == current.coalesceKey &&
                previous.origin == current.origin &&
                previous.kind == current.kind &&
                previous.targetId == current.targetId;
        }
    }

    EarthProjectCommandBus::EarthProjectCommandBus(
        const EarthProject& initialProject, std::size_t capacity)
        : _project(normalizeProject(initialProject)),
          _capacity(std::max<std::size_t>(
              1u, std::min(capacity, kMaximumCommandHistoryCapacity)))
    {
    }

    CommandExecutionResult EarthProjectCommandBus::execute(
        const EarthProjectCommand& input)
    {
        if (input.id.empty())
            return failure("missing_command_id",
                           "Command id must not be empty");
        if (input.id.size() > kMaximumCommandIdBytes)
            return failure("command_id_too_long",
                           "Command id exceeds its storage limit");
        if (input.targetId.size() > kMaximumCommandIdBytes)
            return failure("command_target_too_long",
                           "Command target exceeds its storage limit");
        if (input.textValue.size() > kMaximumCommandValueBytes)
            return failure("command_value_too_long",
                           "Command value exceeds its storage limit");

        EarthProjectCommand command = input;
        command.label = boundedMetadata(
            command.label, kMaximumCommandLabelBytes);
        command.coalesceKey = boundedMetadata(
            command.coalesceKey, kMaximumCommandIdBytes);

        Entry entry;
        entry.command = command;
        bool changed = false;
        switch (command.kind)
        {
        case ProjectCommandKind::SetLayerVisibility:
        {
            EarthLayerDescriptor* layer = findLayer(_project, command.targetId);
            if (!layer)
                return failure("unknown_layer",
                               "Command target layer is unavailable");
            entry.before.boolValue = layer->render.visible;
            entry.after.boolValue = command.boolValue;
            changed = entry.before.boolValue != entry.after.boolValue;
            layer->render.visible = entry.after.boolValue;
            break;
        }
        case ProjectCommandKind::SetLayerOpacity:
        {
            EarthLayerDescriptor* layer = findLayer(_project, command.targetId);
            if (!layer)
                return failure("unknown_layer",
                               "Command target layer is unavailable");
            if (!std::isfinite(command.numberValue))
                return failure("invalid_opacity",
                               "Layer opacity must be finite");
            entry.before.numberValue = layer->render.opacity;
            entry.after.numberValue = std::max(
                0.0, std::min(1.0, command.numberValue));
            changed = entry.before.numberValue != entry.after.numberValue;
            layer->render.opacity = entry.after.numberValue;
            break;
        }
        case ProjectCommandKind::SetLayerTime:
        {
            EarthLayerDescriptor* layer = findLayer(_project, command.targetId);
            if (!layer)
                return failure("unknown_layer",
                               "Command target layer is unavailable");
            if (!layer->capabilities.temporal ||
                layer->temporal.mode == TemporalMode::None)
                return failure("layer_not_temporal",
                               "Command target layer has no time dimension");
            if (command.textValue.empty())
                return failure("invalid_time_value",
                               "Layer time value must not be empty");
            if (!layer->temporal.availableValues.empty() &&
                std::find(layer->temporal.availableValues.begin(),
                          layer->temporal.availableValues.end(),
                          command.textValue) ==
                    layer->temporal.availableValues.end())
                return failure("time_value_unavailable",
                               "Requested layer time is unavailable");
            entry.before.textValue = layer->temporal.currentValue;
            entry.after.textValue = command.textValue;
            changed = entry.before.textValue != entry.after.textValue;
            layer->temporal.currentValue = entry.after.textValue;
            break;
        }
        case ProjectCommandKind::SetActiveModule:
            if (command.textValue.empty())
                return failure("invalid_module",
                               "Active module must not be empty");
            entry.before.textValue = _project.workspace.activeModule;
            entry.after.textValue = command.textValue;
            changed = entry.before.textValue != entry.after.textValue;
            _project.workspace.activeModule = entry.after.textValue;
            break;
        case ProjectCommandKind::SetDrawerOpen:
            entry.before.boolValue = _project.workspace.drawerOpen;
            entry.after.boolValue = command.boolValue;
            changed = entry.before.boolValue != entry.after.boolValue;
            _project.workspace.drawerOpen = entry.after.boolValue;
            break;
        case ProjectCommandKind::AssociateArtifact:
        {
            if (command.textValue.empty())
                return failure("invalid_artifact_id",
                               "Artifact id must not be empty");
            EarthLayerDescriptor* layer = nullptr;
            if (!command.targetId.empty())
            {
                layer = findLayer(_project, command.targetId);
                if (!layer)
                    return failure("unknown_layer",
                                   "Artifact target layer is unavailable");
                entry.before.textValue = layer->artifactId;
                entry.after.textValue = command.textValue;
            }
            const auto active = std::find(
                _project.workspace.activeArtifactIds.begin(),
                _project.workspace.activeArtifactIds.end(),
                command.textValue);
            entry.before.boolValue =
                active != _project.workspace.activeArtifactIds.end();
            entry.after.boolValue = true;
            changed = !entry.before.boolValue ||
                (layer && entry.before.textValue != entry.after.textValue);
            if (!entry.before.boolValue)
                _project.workspace.activeArtifactIds.push_back(
                    command.textValue);
            if (layer) layer->artifactId = entry.after.textValue;
            break;
        }
        default:
            return failure("unsupported_command",
                           "Command kind is not implemented yet");
        }

        _project = normalizeProject(_project);
        CommandExecutionResult result;
        result.ok = true;
        result.changed = changed;
        if (!changed) return result;

        if (!_undo.empty() && canCoalesce(_undo.back().command, command))
        {
            _undo.back().command = command;
            _undo.back().after = entry.after;
            result.coalesced = true;
        }
        else
        {
            _undo.push_back(entry);
            if (_undo.size() > _capacity) _undo.erase(_undo.begin());
        }
        _redo.clear();
        return result;
    }

    void EarthProjectCommandBus::applyEntryState(
        const Entry& entry, const EntryState& state)
    {
        EarthLayerDescriptor* layer = entry.command.targetId.empty()
            ? nullptr : findLayer(_project, entry.command.targetId);
        switch (entry.command.kind)
        {
        case ProjectCommandKind::SetLayerVisibility:
            if (layer) layer->render.visible = state.boolValue;
            break;
        case ProjectCommandKind::SetLayerOpacity:
            if (layer) layer->render.opacity = state.numberValue;
            break;
        case ProjectCommandKind::SetLayerTime:
            if (layer) layer->temporal.currentValue = state.textValue;
            break;
        case ProjectCommandKind::SetActiveModule:
            _project.workspace.activeModule = state.textValue;
            break;
        case ProjectCommandKind::SetDrawerOpen:
            _project.workspace.drawerOpen = state.boolValue;
            break;
        case ProjectCommandKind::AssociateArtifact:
        {
            if (layer) layer->artifactId = state.textValue;
            std::vector<std::string>& active =
                _project.workspace.activeArtifactIds;
            const auto found = std::find(
                active.begin(), active.end(), entry.command.textValue);
            if (state.boolValue && found == active.end())
                active.push_back(entry.command.textValue);
            else if (!state.boolValue && found != active.end())
                active.erase(found);
            break;
        }
        }
        _project = normalizeProject(_project);
    }

    bool EarthProjectCommandBus::undo()
    {
        if (_undo.empty()) return false;
        const Entry entry = _undo.back();
        _undo.pop_back();
        applyEntryState(entry, entry.before);
        _redo.push_back(entry);
        return true;
    }

    bool EarthProjectCommandBus::redo()
    {
        if (_redo.empty()) return false;
        const Entry entry = _redo.back();
        _redo.pop_back();
        applyEntryState(entry, entry.after);
        _undo.push_back(entry);
        if (_undo.size() > _capacity) _undo.erase(_undo.begin());
        return true;
    }

    CommandHistorySnapshot EarthProjectCommandBus::historySnapshot(
        std::size_t maxApplied) const
    {
        CommandHistorySnapshot snapshot;
        snapshot.capacity = _capacity;
        const std::size_t begin = _undo.size() > maxApplied
            ? _undo.size() - maxApplied : 0u;
        snapshot.omittedApplied = begin;
        for (std::size_t index = begin; index < _undo.size(); ++index)
            snapshot.applied.push_back(_undo[index].command);
        std::size_t undoneCount = 0u;
        for (auto iterator = _redo.rbegin(); iterator != _redo.rend() &&
             undoneCount < maxApplied; ++iterator, ++undoneCount)
            snapshot.undone.push_back(iterator->command);
        snapshot.omittedUndone = _redo.size() - undoneCount;
        return snapshot;
    }

    std::vector<std::string>
    EarthProjectCommandBus::referencedArtifactIds() const
    {
        std::vector<std::string> output;
        std::set<std::string> seen;
        const auto append = [&output, &seen](const Entry& entry)
        {
            if (entry.command.kind !=
                    ProjectCommandKind::AssociateArtifact ||
                entry.command.textValue.empty() ||
                !seen.insert(entry.command.textValue).second)
                return;
            output.push_back(entry.command.textValue);
        };
        for (const Entry& entry : _undo) append(entry);
        for (auto iterator = _redo.rbegin(); iterator != _redo.rend();
             ++iterator)
            append(*iterator);
        return output;
    }
}
