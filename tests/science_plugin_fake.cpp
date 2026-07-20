#include "../applications/earth_explorer/science_plugin_api.h"

#include <cstdio>
#include <cstring>

namespace
{
    int counters[10] = {};
    OsgSolScienceGuiBridgeV1 lastGuiBridge = {};

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
    void bindGeoRaster(void*, const OsgSolGeoRasterBridgeV1*)
    {
        ++counters[9];
    }
    void registerAiTools(void*, earthai::ToolRegistry*, LayerManager*,
                         osgVerse::EarthManipulator*) { ++counters[3]; }
    void bindGui(void*, const OsgSolScienceGuiBridgeV1* bridge)
    {
        ++counters[7];
        counters[8] = bridge && bridge->context && bridge->allocate &&
            bridge->deallocate ? 1 : -1;
        if (bridge) lastGuiBridge = *bridge;
    }
    void drawOperations(void*, LayerManager*, osgVerse::EarthManipulator*)
    {
        if (counters[8] != 1) counters[8] = -1;
        else counters[8] = 0;
        ++counters[4];
    }
    void drawResults(void*, LayerManager*)
    {
        if (counters[8] != 1) counters[8] = -1;
        else counters[8] = 0;
        ++counters[5];
    }

    const OsgSolSciencePluginApiV3 api = {
        OSGSOL_SCIENCE_PLUGIN_ABI_V3,
        sizeof(OsgSolSciencePluginApiV3),
        createSession,
        destroySession,
        sceneNode,
        setVisible,
        bindGeoRaster,
        registerAiTools,
        bindGui,
        drawOperations,
        drawResults,
    };
}

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV3* osgsol_science_g0_probe_anchor()
{
    return &api;
}

extern "C" __attribute__((visibility("default")))
const int* osgsol_science_test_counters()
{
    return counters;
}

extern "C" __attribute__((visibility("default")))
const OsgSolScienceGuiBridgeV1* osgsol_science_test_gui_bridge()
{
    return &lastGuiBridge;
}
