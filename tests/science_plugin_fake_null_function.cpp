#include "../applications/earth_explorer/science_plugin_api.h"

namespace
{
    void* createSession(const char*, char*, std::size_t) { return nullptr; }
    void destroySession(void*) {}

    const OsgSolSciencePluginApiV2 api = {
        OSGSOL_SCIENCE_PLUGIN_ABI_V2,
        sizeof(OsgSolSciencePluginApiV2),
        createSession,
        destroySession,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
}

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV2* osgsol_science_g0_probe_anchor()
{
    return &api;
}
