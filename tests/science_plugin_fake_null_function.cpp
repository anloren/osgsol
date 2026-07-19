#include "../applications/earth_explorer/science_plugin_api.h"

namespace
{
    void* createSession(const char*, char*, std::size_t) { return nullptr; }
    void destroySession(void*) {}

    const OsgSolSciencePluginApiV1 api = {
        OSGSOL_SCIENCE_PLUGIN_ABI_V1,
        sizeof(OsgSolSciencePluginApiV1),
        createSession,
        destroySession,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
    };
}

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV1* osgsol_science_g0_probe_anchor()
{
    return &api;
}
