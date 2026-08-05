#include "ScienceProcessingPluginLoader.h"

#include "ScienceProcessingPluginApi.h"

#include <dlfcn.h>

#include <filesystem>

namespace earthscience
{
namespace
{
    constexpr std::size_t MAX_CAPABILITIES = 64;
    constexpr std::size_t MAX_FORMATS = 64;
    constexpr std::size_t MAX_REQUEST_BYTES = 1024u * 1024u;
    constexpr std::size_t MAX_RESULT_BYTES = 16u * 1024u * 1024u;
    constexpr const char* ANCHOR = "osgSolScienceProcessingPlugin";

    bool text(const char* value, std::string& output)
    {
        if (!value) return false;
        output = value;
        return !output.empty() && output.size() <= 4096;
    }
}

ScienceProcessingPluginLoader::~ScienceProcessingPluginLoader()
{
    for (auto plugin = _plugins.rbegin(); plugin != _plugins.rend(); ++plugin)
    {
        if (plugin->api && plugin->api->destroy && plugin->context)
            plugin->api->destroy(plugin->context);
        if (plugin->handle) dlclose(plugin->handle);
    }
}

bool ScienceProcessingPluginLoader::load(
    const std::string& path, ScienceProcessingPluginManifest& manifest,
    std::vector<ScienceProcessingCapability>& capabilities,
    std::string& error)
{
    if (path.empty() || path.size() > 4096)
    {
        error = "processing plugin path is invalid";
        return false;
    }
    std::error_code filesystemError;
    const std::filesystem::path pluginPath(path);
    if (std::filesystem::is_symlink(
            std::filesystem::symlink_status(pluginPath, filesystemError)) ||
        !std::filesystem::is_regular_file(pluginPath, filesystemError))
    {
        error = "processing plugin is missing or unsafe";
        return false;
    }
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
    {
        const char* loaderError = dlerror();
        error = std::string("processing plugin could not be loaded: ") +
            (loaderError ? loaderError : "unknown loader error");
        return false;
    }
    dlerror();
    auto anchor = reinterpret_cast<OsgSolScienceProcessingPluginAnchor>(
        dlsym(handle, ANCHOR));
    const char* symbolError = dlerror();
    if (symbolError || !anchor)
    {
        error = "processing plugin anchor is missing";
        dlclose(handle);
        return false;
    }
    const OsgSolScienceProcessingPluginApiV1* api = anchor();
    if (!api || api->abiVersion != OSGSOL_SCIENCE_PROCESSING_PLUGIN_ABI_V1 ||
        api->structSize < sizeof(OsgSolScienceProcessingPluginApiV1) ||
        !api->capability || !api->create || !api->destroy || !api->submit ||
        !api->snapshot || !api->cancel || api->capabilityCount == 0 ||
        api->capabilityCount > MAX_CAPABILITIES)
    {
        error = "processing plugin ABI is incompatible";
        dlclose(handle);
        return false;
    }
    ScienceProcessingPluginManifest parsed;
    parsed.abiVersion = api->abiVersion;
    if (!text(api->pluginId, parsed.pluginId) ||
        !text(api->pluginVersion, parsed.pluginVersion) ||
        !text(api->displayName, parsed.displayName))
    {
        error = "processing plugin manifest text is invalid";
        dlclose(handle);
        return false;
    }
    if (!validateScienceProcessingPluginManifest(parsed, error))
    {
        dlclose(handle);
        return false;
    }
    if (find(parsed.pluginId))
    {
        error = "processing plugin id is already loaded";
        dlclose(handle);
        return false;
    }
    std::vector<ScienceProcessingCapability> parsedCapabilities;
    for (std::size_t index = 0; index < api->capabilityCount; ++index)
    {
        OsgSolScienceProcessingCapabilityV1 raw = {};
        raw.structSize = sizeof(raw);
        if (!api->capability(index, &raw) ||
            raw.structSize < sizeof(raw) ||
            raw.inputFormatCount == 0 ||
            raw.inputFormatCount > MAX_FORMATS || !raw.inputFormats)
        {
            error = "processing plugin capability is invalid";
            dlclose(handle);
            return false;
        }
        ScienceProcessingCapability capability;
        if (!text(raw.id, capability.id) ||
            !text(raw.displayName, capability.displayName) ||
            !text(raw.engineId, capability.engineId))
        {
            error = "processing plugin capability text is invalid";
            dlclose(handle);
            return false;
        }
        for (std::size_t format = 0; format < raw.inputFormatCount; ++format)
        {
            std::string value;
            if (!text(raw.inputFormats[format], value))
            {
                error = "processing plugin input format is invalid";
                dlclose(handle);
                return false;
            }
            capability.inputFormats.push_back(std::move(value));
        }
        for (unsigned int output = 0;
             output <= static_cast<unsigned int>(ScienceOutputKind::Export);
             ++output)
        {
            if ((raw.outputKindMask & (std::uint64_t(1) << output)) != 0)
                capability.outputKinds.push_back(
                    static_cast<ScienceOutputKind>(output));
        }
        capability.executionKind =
            ScienceProcessingExecutionKind::NativePlugin;
        capability.cancellable = raw.cancellable;
        capability.persistentRecord = raw.persistentRecord;
        capability.optionalPlugin = true;
        capability.available = true;
        capability.pluginAbiVersion = api->abiVersion;
        capability.pluginId = parsed.pluginId;
        capability.pluginVersion = parsed.pluginVersion;
        if (!validateScienceProcessingCapability(capability, error))
        {
            dlclose(handle);
            return false;
        }
        parsedCapabilities.push_back(std::move(capability));
    }
    manifest = std::move(parsed);
    capabilities = std::move(parsedCapabilities);
    char createError[512] = {};
    void* context = api->create(createError, sizeof(createError));
    if (!context)
    {
        error = createError[0] ? createError :
            "processing plugin context could not be created";
        dlclose(handle);
        return false;
    }
    _plugins.push_back({handle, api, context, manifest.pluginId});
    error.clear();
    return true;
}

ScienceProcessingPluginLoader::LoadedPlugin*
ScienceProcessingPluginLoader::find(const std::string& pluginId)
{
    for (LoadedPlugin& plugin : _plugins)
        if (plugin.pluginId == pluginId) return &plugin;
    return nullptr;
}

bool ScienceProcessingPluginLoader::submit(
    const std::string& pluginId, const std::string& requestJson,
    std::uint64_t& token, std::string& error)
{
    LoadedPlugin* plugin = find(pluginId);
    if (!plugin)
    {
        error = "processing plugin is not loaded";
        return false;
    }
    if (requestJson.empty() || requestJson.size() > MAX_REQUEST_BYTES)
    {
        error = "processing request is empty or too large";
        return false;
    }
    char pluginError[512] = {};
    token = 0;
    if (!plugin->api->submit(plugin->context, requestJson.data(),
                            requestJson.size(), &token,
                            pluginError, sizeof(pluginError)) || token == 0)
    {
        error = pluginError[0] ? pluginError :
            "processing plugin rejected the request";
        return false;
    }
    error.clear();
    return true;
}

bool ScienceProcessingPluginLoader::snapshot(
    const std::string& pluginId, std::uint64_t token,
    std::string& resultJson, std::string& error)
{
    LoadedPlugin* plugin = find(pluginId);
    if (!plugin || token == 0)
    {
        error = "processing plugin or token is unavailable";
        return false;
    }
    char pluginError[512] = {};
    OsgSolScienceProcessingBufferV1 probe = {};
    probe.structSize = sizeof(probe);
    if (plugin->api->snapshot(plugin->context, token, &probe,
                              pluginError, sizeof(pluginError)))
    {
        error = "processing plugin accepted an empty result buffer";
        return false;
    }
    if (probe.bytesRequired == 0 || probe.bytesRequired > MAX_RESULT_BYTES)
    {
        error = pluginError[0] ? pluginError :
            "processing plugin result size is invalid";
        return false;
    }
    std::vector<char> buffer(probe.bytesRequired, '\0');
    OsgSolScienceProcessingBufferV1 output = {};
    output.structSize = sizeof(output);
    output.utf8 = buffer.data();
    output.capacity = buffer.size();
    if (!plugin->api->snapshot(plugin->context, token, &output,
                               pluginError, sizeof(pluginError)) ||
        output.bytesWritten >= output.capacity ||
        buffer[output.bytesWritten] != '\0')
    {
        error = pluginError[0] ? pluginError :
            "processing plugin result could not be copied";
        return false;
    }
    resultJson.assign(buffer.data(), output.bytesWritten);
    error.clear();
    return true;
}

bool ScienceProcessingPluginLoader::cancel(
    const std::string& pluginId, std::uint64_t token, std::string& error)
{
    LoadedPlugin* plugin = find(pluginId);
    if (!plugin || token == 0)
    {
        error = "processing plugin or token is unavailable";
        return false;
    }
    char pluginError[512] = {};
    if (!plugin->api->cancel(plugin->context, token,
                             pluginError, sizeof(pluginError)))
    {
        error = pluginError[0] ? pluginError :
            "processing plugin cancellation failed";
        return false;
    }
    error.clear();
    return true;
}
}
