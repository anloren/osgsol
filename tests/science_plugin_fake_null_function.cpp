#include "../applications/earth_explorer/science_plugin_api.h"

namespace
{
    void* createSession(const char*, char*, std::size_t) { return nullptr; }
    void destroySession(void*) {}

    const OsgSolSciencePluginApiV3 api = {
        OSGSOL_SCIENCE_PLUGIN_ABI_V3,
        sizeof(OsgSolSciencePluginApiV3),
        createSession,
        destroySession,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
}

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV3* osgsol_science_g0_probe_anchor()
{
    return &api;
}
