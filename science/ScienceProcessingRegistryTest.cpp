#include "ScienceProcessingRegistry.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
void require(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "ScienceProcessingRegistryTest: " << message << std::endl;
    std::exit(1);
}

earthscience::ScienceSourceDescriptor source()
{
    earthscience::ScienceSourceDescriptor value;
    value.id = "test-source";
    value.name = "Test source";
    value.providerVersion = "provider-v1";
    value.health = earthscience::ScienceSourceHealth::Ready;
    value.capabilities.pointQuery = true;
    value.capabilities.rasterLayerOutput = true;
    value.capabilities.timeSeriesOutput = true;
    return value;
}

earthscience::GeoTemporalQuery query(earthscience::ScienceOutputKind output)
{
    earthscience::GeoTemporalQuery value;
    value.sourceId = "test-source";
    value.geometry.kind = earthscience::ScienceGeometryKind::Point;
    value.outputKind = output;
    return value;
}
}

int main()
{
    earthscience::ScienceProcessingRegistry registry;
    std::string error;
    require(registry.addBuiltInSource(source(), error), error);
    require(registry.list().size() == 1,
            "built-in source should register one typed capability");

    const earthscience::ScienceProcessingCapability* raster =
        registry.resolve(query(earthscience::ScienceOutputKind::RasterLayer));
    require(raster && raster->id == "science-provider/test-source",
            "raster query did not resolve to its provider capability");
    require(raster->executionKind ==
                earthscience::ScienceProcessingExecutionKind::BuiltInProvider,
            "built-in provider capability has the wrong execution kind");
    require(raster->analysisKinds ==
                std::vector<earthscience::ScienceAnalysisKind>{
                    earthscience::ScienceAnalysisKind::None},
            "non-analysis source must not advertise PCA or clustering");
    require(registry.resolve(
                query(earthscience::ScienceOutputKind::VectorFeatures)) ==
                nullptr,
            "unsupported output unexpectedly resolved");

    const std::vector<earthscience::ScienceProcessingCapability> optional =
        earthscience::optionalScienceProcessingCapabilities();
    require(optional.size() == 3,
            "DuckDB Spatial, PMTiles, and Zarr slots must all be declared");
    for (const auto& capability : optional)
    {
        require(capability.optionalPlugin,
                "external engine must be an optional plugin");
        require(!capability.available,
                "unloaded external engine must not claim availability");
        require(capability.pluginAbiVersion ==
                    earthscience::SCIENCE_PROCESSING_PLUGIN_ABI_V1,
                "external engine has the wrong plugin ABI");
        require(!capability.inputFormats.empty(),
                "external engine must publish typed input formats");
        require(registry.add(capability, error), error);
    }

    earthscience::ScienceProcessingCapability duckdb = optional.front();
    duckdb.available = true;
    duckdb.pluginId = "org.example.duckdb";
    duckdb.pluginVersion = "1.0.0";
    require(!registry.add(duckdb, error),
            "duplicate capability id must be rejected");

    earthscience::ScienceProcessingRegistry pluginRegistry;
    require(pluginRegistry.registerPlugin(
                {earthscience::SCIENCE_PROCESSING_PLUGIN_ABI_V1,
                 "org.example.duckdb", "1.0.0", "DuckDB Spatial"},
                {duckdb}, error), error);
    const earthscience::ScienceProcessingCapability* installed =
        pluginRegistry.find(duckdb.id);
    require(installed && installed->available,
            "loaded plugin capability should be available");
    require(installed->pluginId == "org.example.duckdb",
            "plugin ownership was not retained");

    earthscience::ScienceProcessingPluginManifest badManifest = {
        earthscience::SCIENCE_PROCESSING_PLUGIN_ABI_V1 + 1,
        "org.example.future", "1.0.0", "Future"};
    require(!pluginRegistry.registerPlugin(
                badManifest, {duckdb}, error),
            "future plugin ABI must be rejected");
    earthscience::ScienceProcessingRegistry invalidManifestRegistry;
    earthscience::ScienceProcessingPluginManifest unnamedManifest = {
        earthscience::SCIENCE_PROCESSING_PLUGIN_ABI_V1,
        "org.example.unnamed", "1.0.0", ""};
    require(!invalidManifestRegistry.registerPlugin(
                unnamedManifest, {duckdb}, error),
            "plugin manifest without a display name must be rejected");
    return 0;
}
