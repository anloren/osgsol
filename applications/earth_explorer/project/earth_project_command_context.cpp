#include "earth_project_command_context.h"

#include "../earth_context.h"

#include <algorithm>
#include <string>

namespace earthproject
{
    namespace
    {
        const std::size_t kMinimumCommandContextBytes = 1024u;
        const std::size_t kMaximumContextStringBytes = 160u;

        std::string boundedString(const std::string& value)
        {
            return value.size() <= kMaximumContextStringBytes
                ? value : value.substr(0u, kMaximumContextStringBytes);
        }

        const char* originName(CommandOrigin origin)
        {
            switch (origin)
            {
            case CommandOrigin::AI: return "ai";
            case CommandOrigin::Automation: return "automation";
            case CommandOrigin::User:
            default: return "user";
            }
        }

        const char* kindName(ProjectCommandKind kind)
        {
            switch (kind)
            {
            case ProjectCommandKind::SetLayerOpacity:
                return "set_layer_opacity";
            case ProjectCommandKind::SetLayerTime:
                return "set_layer_time";
            case ProjectCommandKind::SetActiveModule:
                return "set_active_module";
            case ProjectCommandKind::SetDrawerOpen:
                return "set_drawer_open";
            case ProjectCommandKind::AssociateArtifact:
                return "associate_artifact";
            case ProjectCommandKind::SetLayerVisibility:
            default:
                return "set_layer_visibility";
            }
        }

        picojson::object commandObject(const EarthProjectCommand& command)
        {
            picojson::object output;
            output["id"] = picojson::value(boundedString(command.id));
            output["origin"] = picojson::value(originName(command.origin));
            output["kind"] = picojson::value(kindName(command.kind));
            output["targetId"] = picojson::value(
                boundedString(command.targetId));
            output["label"] = picojson::value(boundedString(command.label));
            switch (command.kind)
            {
            case ProjectCommandKind::SetLayerVisibility:
            case ProjectCommandKind::SetDrawerOpen:
                output["value"] = picojson::value(command.boolValue);
                break;
            case ProjectCommandKind::SetLayerOpacity:
                output["value"] = picojson::value(command.numberValue);
                break;
            case ProjectCommandKind::SetLayerTime:
            case ProjectCommandKind::SetActiveModule:
            case ProjectCommandKind::AssociateArtifact:
                output["value"] = picojson::value(
                    boundedString(command.textValue));
                break;
            }
            return output;
        }

        picojson::array commandArray(
            const std::vector<EarthProjectCommand>& commands)
        {
            picojson::array output;
            output.reserve(commands.size());
            for (const EarthProjectCommand& command : commands)
                output.push_back(picojson::value(commandObject(command)));
            return output;
        }

        picojson::object historyObject(
            const EarthProjectCommandBus& bus,
            const CommandHistorySnapshot& snapshot)
        {
            picojson::object output;
            output["schema"] = picojson::value(
                "earth-project-command-history-v1");
            output["capacity"] = picojson::value(
                static_cast<double>(snapshot.capacity));
            output["canUndo"] = picojson::value(!snapshot.applied.empty());
            output["canRedo"] = picojson::value(!snapshot.undone.empty());
            output["omittedApplied"] = picojson::value(
                static_cast<double>(snapshot.omittedApplied));
            output["omittedUndone"] = picojson::value(
                static_cast<double>(snapshot.omittedUndone));
            output["applied"] = picojson::value(
                commandArray(snapshot.applied));
            output["undone"] = picojson::value(
                commandArray(snapshot.undone));

            picojson::array artifacts;
            for (const std::string& id : bus.referencedArtifactIds())
                artifacts.push_back(picojson::value(boundedString(id)));
            output["artifactIds"] = picojson::value(artifacts);
            return output;
        }
    }

    picojson::value commandHistoryContextValue(
        const EarthProjectCommandBus& bus, std::size_t maxEntries,
        std::size_t maxBytes)
    {
        maxBytes = std::max(maxBytes, kMinimumCommandContextBytes);
        CommandHistorySnapshot snapshot = bus.historySnapshot(maxEntries);
        picojson::object output = historyObject(bus, snapshot);
        while (picojson::value(output).serialize().size() > maxBytes)
        {
            picojson::array& applied =
                output["applied"].get<picojson::array>();
            picojson::array& undone =
                output["undone"].get<picojson::array>();
            picojson::array& artifacts =
                output["artifactIds"].get<picojson::array>();
            if (!applied.empty())
            {
                applied.erase(applied.begin());
                output["omittedApplied"] = picojson::value(
                    output["omittedApplied"].get<double>() + 1.0);
            }
            else if (!undone.empty())
            {
                undone.pop_back();
                output["omittedUndone"] = picojson::value(
                    output["omittedUndone"].get<double>() + 1.0);
            }
            else if (!artifacts.empty())
                artifacts.pop_back();
            else break;
        }
        return picojson::value(output);
    }

    void registerProjectCommandContext(
        const std::shared_ptr<earthai::EarthContextHub>& context,
        const std::shared_ptr<EarthProjectCommandBus>& bus)
    {
        if (!context || !bus) return;
        const std::weak_ptr<EarthProjectCommandBus> weakBus(bus);
        context->upsertSection(
            "commandHistory", "workspace_command_history", 80,
            [weakBus]()
            {
                const std::shared_ptr<EarthProjectCommandBus> locked =
                    weakBus.lock();
                if (!locked)
                {
                    picojson::object unavailable;
                    unavailable["schema"] = picojson::value(
                        "earth-project-command-history-v1");
                    unavailable["status"] = picojson::value("unavailable");
                    return picojson::value(unavailable);
                }
                return commandHistoryContextValue(*locked);
            });
    }
}
