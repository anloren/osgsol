#ifndef OSGSOL_SCIENCE_PROCESSING_PLUGIN_LOADER_H
#define OSGSOL_SCIENCE_PROCESSING_PLUGIN_LOADER_H

#include <string>
#include <vector>

#include "ScienceProcessingPluginApi.h"
#include "ScienceProcessingTypes.h"

namespace earthscience
{
    class ScienceProcessingPluginLoader
    {
    public:
        ScienceProcessingPluginLoader() = default;
        ~ScienceProcessingPluginLoader();

        ScienceProcessingPluginLoader(
            const ScienceProcessingPluginLoader&) = delete;
        ScienceProcessingPluginLoader& operator=(
            const ScienceProcessingPluginLoader&) = delete;

        bool load(const std::string& path,
                  ScienceProcessingPluginManifest& manifest,
                  std::vector<ScienceProcessingCapability>& capabilities,
                  std::string& error);
        bool submit(const std::string& pluginId,
                    const std::string& requestJson,
                    std::uint64_t& token,
                    std::string& error);
        bool snapshot(const std::string& pluginId,
                      std::uint64_t token,
                      std::string& resultJson,
                      std::string& error);
        bool cancel(const std::string& pluginId,
                    std::uint64_t token,
                    std::string& error);

    private:
        struct LoadedPlugin
        {
            void* handle = nullptr;
            const OsgSolScienceProcessingPluginApiV1* api = nullptr;
            void* context = nullptr;
            std::string pluginId;
        };
        LoadedPlugin* find(const std::string& pluginId);
        std::vector<LoadedPlugin> _plugins;
    };
}

#endif
