#include "science_plugin_runtime.h"

#include <dlfcn.h>

#include <array>
#include <sstream>

SciencePluginRuntime::SciencePluginRuntime()
    : _module(nullptr), _api(nullptr), _session(nullptr)
{
}

SciencePluginRuntime::~SciencePluginRuntime()
{
    if (_session && _api && _api->destroy)
        _api->destroy(_session);

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

    const OsgSolSciencePluginApiV1* api = anchor();
    if (!api || api->abiVersion != OSGSOL_SCIENCE_PLUGIN_ABI_V1 ||
        api->structSize < sizeof(OsgSolSciencePluginApiV1))
        return reject(handle, "ScienceEarth plugin ABI is incompatible");
    if (!api->create || !api->destroy || !api->sceneNode ||
        !api->setVisible || !api->registerAiTools ||
        !api->drawOperations || !api->drawResults)
        return reject(handle, "ScienceEarth plugin function table is incomplete");

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

osg::Node* SciencePluginRuntime::sceneNode() const
{
    return available() ? _api->sceneNode(_session) : nullptr;
}

void SciencePluginRuntime::setVisible(bool visible) const
{
    if (available()) _api->setVisible(_session, visible);
}

void SciencePluginRuntime::registerAiTools(
    earthai::ToolRegistry* tools, LayerManager* layers,
    osgVerse::EarthManipulator* manipulator) const
{
    if (available())
        _api->registerAiTools(_session, tools, layers, manipulator);
}

void SciencePluginRuntime::drawOperations(
    LayerManager* layers, osgVerse::EarthManipulator* manipulator) const
{
    if (available()) _api->drawOperations(_session, layers, manipulator);
}

void SciencePluginRuntime::drawResults(LayerManager* layers) const
{
    if (available()) _api->drawResults(_session, layers);
}
