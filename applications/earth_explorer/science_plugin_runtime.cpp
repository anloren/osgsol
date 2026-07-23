#include "science_plugin_runtime.h"
#include "terrain_science_overlay.h"

#include <dlfcn.h>

#include <array>
#include <cstdio>
#include <limits>
#include <sstream>
#include <vector>

namespace
{
    void copyBridgeError(char* output, std::size_t outputSize,
                         const std::string& message)
    {
        if (output && outputSize > 0)
            std::snprintf(output, outputSize, "%s", message.c_str());
    }

    bool publishGeoRasterCopy(const OsgSolGeoRasterFrameV1* frame,
                              void* userData, char* error,
                              std::size_t errorSize)
    {
        terrainoverlay::TerrainScienceOverlay* overlay =
            static_cast<terrainoverlay::TerrainScienceOverlay*>(userData);
        if (!overlay || !frame ||
            frame->structSize < sizeof(OsgSolGeoRasterFrameV1))
        {
            copyBridgeError(error, errorSize,
                            "ScienceEarth raster frame is incompatible");
            return false;
        }
        if (frame->rowBytes >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()))
        {
            copyBridgeError(error, errorSize,
                            "ScienceEarth raster row stride is too large");
            return false;
        }
        terrainoverlay::GeoRasterFrameView view;
        view.generation = frame->generation;
        view.width = frame->width;
        view.height = frame->height;
        view.rowBytes = static_cast<std::size_t>(frame->rowBytes);
        view.rgba = frame->rgba;
        view.bounds = {frame->west, frame->south, frame->east, frame->north};
        std::string detail;
        const bool accepted = overlay->enqueueCopy(view, detail);
        if (!accepted) copyBridgeError(error, errorSize, detail);
        return accepted;
    }

    void clearGeoRaster(std::uint64_t generation, void* userData)
    {
        terrainoverlay::TerrainScienceOverlay* overlay =
            static_cast<terrainoverlay::TerrainScienceOverlay*>(userData);
        if (overlay) overlay->enqueueClear(generation);
    }
}

SciencePluginRuntime::SciencePluginRuntime()
    : _module(nullptr), _api(nullptr), _session(nullptr)
{
}

SciencePluginRuntime::~SciencePluginRuntime()
{
    if (_session && _api)
    {
        // The viewer scene graph can retain the plugin-created preview node
        // beyond the session facade. Remove its raw host callback before the
        // host TerrainScienceOverlay can be destroyed; otherwise the node's
        // later destructor can call clear() through a dangling userData.
        if (_api->bindGeoRaster)
            _api->bindGeoRaster(_session, nullptr);
        if (_api->destroy)
            _api->destroy(_session);
    }

    // OSG may retain a plugin-created node until after this facade is destroyed.
    // Keep the successfully loaded image resident until process exit so its node
    // destructors and callbacks can never jump into unloaded code.
    _session = nullptr;
    _api = nullptr;
    _module = nullptr;
}

bool SciencePluginRuntime::reject(void* handle, const std::string& error)
{
    if (handle) dlclose(handle);
    _error = error;
    _module = nullptr;
    _api = nullptr;
    _session = nullptr;
    return false;
}

bool SciencePluginRuntime::load(const std::string& pluginPath,
                                const std::string& indexPath)
{
    if (_module || _session)
    {
        _error = "ScienceEarth plugin runtime is already loaded";
        return false;
    }

    dlerror();
    void* handle = dlopen(pluginPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        const char* detail = dlerror();
        return reject(nullptr, std::string("ScienceEarth plugin load failed: ") +
            (detail ? detail : "unknown dynamic-loader error"));
    }

    dlerror();
    OsgSolScienceAnchor anchor = reinterpret_cast<OsgSolScienceAnchor>(
        dlsym(handle, "osgsol_science_g0_probe_anchor"));
    const char* symbolError = dlerror();
    if (symbolError || !anchor)
        return reject(handle, "ScienceEarth plugin anchor is missing");

    const OsgSolSciencePluginApiV3* api = anchor();
    if (!api ||
        (api->abiVersion != OSGSOL_SCIENCE_PLUGIN_ABI_V3 &&
         api->abiVersion != OSGSOL_SCIENCE_PLUGIN_ABI_V4) ||
        api->structSize < sizeof(OsgSolSciencePluginApiV3))
        return reject(handle, "ScienceEarth plugin ABI is incompatible");
    if (!api->create || !api->destroy || !api->sceneNode ||
        !api->setVisible || !api->bindGeoRaster ||
        !api->registerAiTools || !api->bindGui ||
        !api->drawOperations || !api->drawResults)
        return reject(handle, "ScienceEarth plugin function table is incomplete");
    if (api->abiVersion == OSGSOL_SCIENCE_PLUGIN_ABI_V4)
    {
        if (api->structSize < sizeof(OsgSolSciencePluginApiV4))
            return reject(handle, "ScienceEarth plugin ABI v4 is truncated");
        const OsgSolSciencePluginApiV4* apiV4 =
            reinterpret_cast<const OsgSolSciencePluginApiV4*>(api);
        if (!apiV4->copyWorkbenchSnapshot ||
            !apiV4->dispatchWorkbenchAction)
            return reject(handle,
                          "ScienceEarth plugin ABI v4 function table is incomplete");
    }

    std::array<char, 1024> error = {};
    void* session = api->create(indexPath.c_str(), error.data(), error.size());
    if (!session)
    {
        const std::string detail = error[0]
            ? std::string(error.data()) : "ScienceEarth plugin create failed";
        return reject(handle, detail);
    }

    _module = handle;
    _api = api;
    _session = session;
    _error.clear();
    return true;
}

bool SciencePluginRuntime::supportsWorkbenchUi() const
{
    if (!available() || _api->abiVersion != OSGSOL_SCIENCE_PLUGIN_ABI_V4 ||
        _api->structSize < sizeof(OsgSolSciencePluginApiV4))
        return false;
    const OsgSolSciencePluginApiV4* api =
        reinterpret_cast<const OsgSolSciencePluginApiV4*>(_api);
    return api->copyWorkbenchSnapshot && api->dispatchWorkbenchAction;
}

osg::Node* SciencePluginRuntime::sceneNode() const
{
    return available() ? _api->sceneNode(_session) : nullptr;
}

void SciencePluginRuntime::setVisible(bool visible) const
{
    if (available()) _api->setVisible(_session, visible);
}

void SciencePluginRuntime::bindGeoRaster(
    terrainoverlay::TerrainScienceOverlay* overlay) const
{
    if (!available() || !overlay) return;
    const OsgSolGeoRasterBridgeV1 bridge = {
        sizeof(OsgSolGeoRasterBridgeV1),
        overlay,
        publishGeoRasterCopy,
        clearGeoRaster,
    };
    _api->bindGeoRaster(_session, &bridge);
}

void SciencePluginRuntime::registerAiTools(
    earthai::ToolRegistry* tools, LayerManager* layers,
    osgVerse::EarthManipulator* manipulator) const
{
    if (available())
        _api->registerAiTools(_session, tools, layers, manipulator);
}

void SciencePluginRuntime::drawOperations(
    LayerManager* layers, osgVerse::EarthManipulator* manipulator,
    const OsgSolScienceGuiBridgeV1& gui) const
{
    if (!available() || !gui.context || !gui.allocate || !gui.deallocate)
        return;
    _api->bindGui(_session, &gui);
    _api->drawOperations(_session, layers, manipulator);
}

void SciencePluginRuntime::drawResults(
    LayerManager* layers, const OsgSolScienceGuiBridgeV1& gui) const
{
    if (!available() || !gui.context || !gui.allocate || !gui.deallocate)
        return;
    _api->bindGui(_session, &gui);
    _api->drawResults(_session, layers);
}

bool SciencePluginRuntime::copyWorkbenchSnapshot(
    std::string& snapshot, std::string& error) const
{
    snapshot.clear();
    error.clear();
    if (!supportsWorkbenchUi())
    {
        error = "ScienceEarth workbench UI is unavailable";
        return false;
    }
    const OsgSolSciencePluginApiV4* api =
        reinterpret_cast<const OsgSolSciencePluginApiV4*>(_api);
    OsgSolScienceUiBufferV1 probe = {};
    probe.structSize = sizeof(probe);
    api->copyWorkbenchSnapshot(_session, &probe);
    if (probe.bytesRequired == 0 ||
        probe.bytesRequired > OSGSOL_SCIENCE_UI_SNAPSHOT_MAX_BYTES + 1)
    {
        error = "ScienceEarth workbench snapshot size is invalid";
        return false;
    }
    std::size_t capacity = probe.bytesRequired;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        std::vector<char> buffer(capacity, '\0');
        OsgSolScienceUiBufferV1 output = {};
        output.structSize = sizeof(output);
        output.utf8 = buffer.data();
        output.capacity = buffer.size();
        if (!api->copyWorkbenchSnapshot(_session, &output))
        {
            // The science job can advance between the size probe and copy.
            // Retry with the new requirement instead of treating a normal
            // asynchronous snapshot growth as an ABI contract failure.
            if (output.bytesRequired > capacity &&
                output.bytesRequired <=
                    OSGSOL_SCIENCE_UI_SNAPSHOT_MAX_BYTES + 1)
            {
                capacity = output.bytesRequired;
                continue;
            }
            error = "ScienceEarth workbench snapshot copy failed";
            return false;
        }
        if (output.bytesRequired == 0 ||
            output.bytesRequired >
                OSGSOL_SCIENCE_UI_SNAPSHOT_MAX_BYTES + 1 ||
            output.bytesWritten + 1 != output.bytesRequired ||
            output.bytesWritten >= buffer.size() ||
            buffer[output.bytesWritten] != '\0')
        {
            error = "ScienceEarth workbench snapshot contract is invalid";
            return false;
        }
        snapshot.assign(buffer.data(), output.bytesWritten);
        return true;
    }
    error = "ScienceEarth workbench snapshot changed too quickly";
    return false;
}

bool SciencePluginRuntime::dispatchWorkbenchAction(
    const std::string& action, std::string& error) const
{
    error.clear();
    if (!supportsWorkbenchUi())
    {
        error = "ScienceEarth workbench UI is unavailable";
        return false;
    }
    if (action.size() > OSGSOL_SCIENCE_UI_ACTION_MAX_BYTES)
    {
        error = "ScienceEarth workbench action is too large";
        return false;
    }
    const OsgSolSciencePluginApiV4* api =
        reinterpret_cast<const OsgSolSciencePluginApiV4*>(_api);
    std::array<char, 1024> detail = {};
    const bool accepted = api->dispatchWorkbenchAction(
        _session, action.data(), action.size(), detail.data(), detail.size());
    if (!accepted)
        error = detail[0] ? detail.data() :
            "ScienceEarth workbench action failed";
    return accepted;
}
