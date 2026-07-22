#include "../applications/earth_explorer/science_plugin_api.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
int sessionValue = 7;

void* createSession(const char*, char*, std::size_t) { return &sessionValue; }
void destroySession(void*) {}
osg::Node* sceneNode(void*) { return nullptr; }
void setVisible(void*, bool) {}
void bindGeoRaster(void*, const OsgSolGeoRasterBridgeV1*) {}
void registerAiTools(void*, earthai::ToolRegistry*, LayerManager*,
                     osgVerse::EarthManipulator*) {}
void bindGui(void*, const OsgSolScienceGuiBridgeV1*) {}
void drawOperations(void*, LayerManager*, osgVerse::EarthManipulator*) {}
void drawResults(void*, LayerManager*) {}

bool copySnapshot(void*, OsgSolScienceUiBufferV1* output)
{
    static const std::string value =
        R"({"revision":7,"schema":"science-workbench-ui-v1"})";
    if (!output || output->structSize < sizeof(*output)) return false;
    output->revision = 7;
    output->bytesWritten = 0;
    output->bytesRequired = value.size() + 1;
    if (!output->utf8 || output->capacity < output->bytesRequired)
        return false;
    std::memcpy(output->utf8, value.data(), value.size());
    output->utf8[value.size()] = '\0';
    output->bytesWritten = value.size();
    return true;
}

bool dispatchAction(void*, const char* action, std::size_t actionSize,
                    char* error, std::size_t errorSize)
{
    const std::string value(action ? action : "", actionSize);
    if (value.find("\"action\":\"run\"") != std::string::npos)
        return true;
    if (error && errorSize > 0)
        std::snprintf(error, errorSize, "%s", "fake action rejected");
    return false;
}

const OsgSolSciencePluginApiV4 api = {
    {
        OSGSOL_SCIENCE_PLUGIN_ABI_V4,
        sizeof(OsgSolSciencePluginApiV4),
        createSession,
        destroySession,
        sceneNode,
        setVisible,
        bindGeoRaster,
        registerAiTools,
        bindGui,
        drawOperations,
        drawResults,
    },
    copySnapshot,
    dispatchAction,
};
}

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV3* osgsol_science_g0_probe_anchor()
{
    return &api.v3;
}
