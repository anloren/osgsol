#include "../applications/earth_explorer/science_plugin_api.h"

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV2* osgsol_science_g0_probe_anchor()
{
    static OsgSolSciencePluginApiV2 api = {};
    api.abiVersion = OSGSOL_SCIENCE_PLUGIN_ABI_V2 + 1;
    api.structSize = sizeof(api);
    return &api;
}
