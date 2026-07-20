#ifndef EARTH_SCIENCE_PLUGIN_RUNTIME_H
#define EARTH_SCIENCE_PLUGIN_RUNTIME_H

#include "science_plugin_api.h"

#include <string>

namespace terrainoverlay { class TerrainScienceOverlay; }

class SciencePluginRuntime
{
public:
    SciencePluginRuntime();
    ~SciencePluginRuntime();

    SciencePluginRuntime(const SciencePluginRuntime&) = delete;
    SciencePluginRuntime& operator=(const SciencePluginRuntime&) = delete;

    bool load(const std::string& pluginPath, const std::string& indexPath);
    bool available() const { return _session != nullptr; }
    const std::string& error() const { return _error; }

    osg::Node* sceneNode() const;
    void setVisible(bool visible) const;
    void bindGeoRaster(
        terrainoverlay::TerrainScienceOverlay* overlay) const;
    void registerAiTools(earthai::ToolRegistry* tools,
                         LayerManager* layers,
                         osgVerse::EarthManipulator* manipulator) const;
    void drawOperations(LayerManager* layers,
                        osgVerse::EarthManipulator* manipulator,
                        const OsgSolScienceGuiBridgeV1& gui) const;
    void drawResults(LayerManager* layers,
                     const OsgSolScienceGuiBridgeV1& gui) const;

private:
    bool reject(void* handle, const std::string& error);

    void* _module;
    const OsgSolSciencePluginApiV3* _api;
    void* _session;
    std::string _error;
};

#endif
