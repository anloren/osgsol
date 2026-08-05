#include "ScienceProcessingPluginLoader.h"
#include "ScienceProcessingRegistry.h"

#include <cstdlib>
#include <iostream>
#include <string>

#ifndef OSGSOL_TEST_PROCESSING_PLUGIN
#error OSGSOL_TEST_PROCESSING_PLUGIN must name the fake plugin
#endif

namespace
{
void require(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "ScienceProcessingPluginLoaderTest: " << message << std::endl;
    std::exit(1);
}
}

int main()
{
    earthscience::ScienceProcessingRegistry registry;
    std::string error;
    for (const auto& capability :
         earthscience::optionalScienceProcessingCapabilities())
        require(registry.add(capability, error), error);

    earthscience::ScienceProcessingPluginLoader loader;
    earthscience::ScienceProcessingPluginManifest manifest;
    std::vector<earthscience::ScienceProcessingCapability> capabilities;
    require(loader.load(OSGSOL_TEST_PROCESSING_PLUGIN, manifest,
                        capabilities, error), error);
    require(manifest.pluginId == "org.osgsol.test.duckdb" &&
                capabilities.size() == 1,
            "plugin manifest or capability was not decoded");
    require(registry.registerPlugin(manifest, capabilities, error), error);
    const auto* duckdb = registry.find("duckdb-spatial");
    require(duckdb && duckdb->available &&
                duckdb->pluginId == manifest.pluginId,
            "loaded plugin did not replace its unavailable catalog slot");

    std::uint64_t token = 0;
    require(loader.submit(manifest.pluginId,
                          "{\"operation\":\"scan-geoparquet\"}",
                          token, error) && token == 9,
            "loaded native plugin did not accept a typed request");
    std::string result;
    require(loader.snapshot(manifest.pluginId, token, result, error) &&
                result.find("duckdb-spatial") != std::string::npos,
            "loaded native plugin did not return a bounded result");
    require(loader.cancel(manifest.pluginId, token, error),
            "loaded native plugin did not accept cancellation");
    require(loader.snapshot(manifest.pluginId, token, result, error) &&
                result == "{\"state\":\"cancelled\"}",
            "loaded native plugin did not expose cancellation state");

    earthscience::ScienceProcessingPluginManifest missingManifest;
    std::vector<earthscience::ScienceProcessingCapability> missing;
    require(!loader.load("/definitely/missing/osgsol-processing.so",
                         missingManifest, missing, error),
            "missing plugin unexpectedly loaded");
    return 0;
}
