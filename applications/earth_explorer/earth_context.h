#ifndef EARTH_AI_CONTEXT_H
#define EARTH_AI_CONTEXT_H

#include "ai_tools.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace earthai
{
    // Process-local context bus shared by the product shell, ScienceEarth and
    // the AI agent. Readers are invoked on the main thread when a user submits
    // a request or the model calls get_earth_context. The hub owns no UI or
    // data-source object; every subsystem publishes a bounded JSON snapshot.
    class EarthContextHub
    {
    public:
        using Reader = std::function<picojson::value()>;

        void upsertSection(const std::string& id, const std::string& kind,
                           int priority, const Reader& reader);

        // Produces a valid JSON document no larger than maxBytes. Higher
        // priority sections are retained first; omitted sections stay visible
        // in sectionStatus with an explicit reason.
        std::string snapshotJson(std::size_t maxBytes) const;

        // Reads one section without the automatic-snapshot budget. This is the
        // implementation boundary used by get_earth_context.
        picojson::value sectionValue(const std::string& id,
                                     std::size_t maxBytes) const;

    private:
        struct Section
        {
            std::string id;
            std::string kind;
            int priority = 0;
            Reader reader;
        };

        std::vector<Section> sectionsSnapshot() const;

        mutable std::mutex _mutex;
        std::vector<Section> _sections;
        mutable std::atomic<std::uint64_t> _revision{0};
    };

    Tool makeEarthContextTool(
        const std::shared_ptr<EarthContextHub>& context);
}

#endif
