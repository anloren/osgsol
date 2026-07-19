#include "../applications/earth_explorer/science_plugin_api.h"

#include <cstdio>
#include <cstring>

namespace
{
    int counters[7] = {};

    void* createSession(const char* indexPath, char* error, std::size_t errorSize)
    {
        ++counters[0];
        if (indexPath && std::strcmp(indexPath, "fail") == 0)
        {
            if (error && errorSize > 0)
                std::snprintf(error, errorSize, "%s", "fake create failure");
            return nullptr;
        }
        return counters;
    }

    void destroySession(void*) { ++counters[1]; }
    osg::Node* sceneNode(void*) { ++counters[6]; return nullptr; }
    void setVisible(void*, bool) { ++counters[2]; }
    void registerAiTools(void*, earthai::ToolRegistry*, LayerManager*,
                         osgVerse::EarthManipulator*) { ++counters[3]; }
    void drawOperations(void*, LayerManager*, osgVerse::EarthManipulator*)
    { ++counters[4]; }
    void drawResults(void*, LayerManager*) { ++counters[5]; }

    const OsgSolSciencePluginApiV1 api = {
        OSGSOL_SCIENCE_PLUGIN_ABI_V1,
        sizeof(OsgSolSciencePluginApiV1),
        createSession,
        destroySession,
        sceneNode,
        setVisible,
        registerAiTools,
        drawOperations,
        drawResults,
    };
}

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV1* osgsol_science_g0_probe_anchor()
{
    return &api;
}

extern "C" __attribute__((visibility("default")))
const int* osgsol_science_test_counters()
{
    return counters;
}
