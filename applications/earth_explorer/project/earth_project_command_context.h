#ifndef OSGSOL_EARTH_PROJECT_COMMAND_CONTEXT_H
#define OSGSOL_EARTH_PROJECT_COMMAND_CONTEXT_H

#include "earth_project_command_bus.h"

#include <picojson.h>

#include <cstddef>
#include <memory>

namespace earthai
{
    class EarthContextHub;
}

namespace earthproject
{
    constexpr std::size_t kDefaultCommandContextEntries = 16u;
    constexpr std::size_t kDefaultCommandContextBytes = 24u * 1024u;

    picojson::value commandHistoryContextValue(
        const EarthProjectCommandBus& bus,
        std::size_t maxEntries = kDefaultCommandContextEntries,
        std::size_t maxBytes = kDefaultCommandContextBytes);

    void registerProjectCommandContext(
        const std::shared_ptr<earthai::EarthContextHub>& context,
        const std::shared_ptr<EarthProjectCommandBus>& bus);
}

#endif
