#include "ScienceProcessingRegistry.h"

#include <algorithm>

namespace earthscience
{
namespace
{
    void appendOutput(bool supported, ScienceOutputKind kind,
                      std::vector<ScienceOutputKind>& outputs)
    {
        if (supported) outputs.push_back(kind);
    }

    bool contains(const std::vector<ScienceOutputKind>& values,
                  ScienceOutputKind value)
    {
        return std::find(values.begin(), values.end(), value) != values.end();
    }
}

bool ScienceProcessingRegistry::add(
    const ScienceProcessingCapability& capability, std::string& error)
{
    if (!validateScienceProcessingCapability(capability, error)) return false;
    if (_capabilities.find(capability.id) != _capabilities.end())
    {
        error = "processing capability id already exists: " + capability.id;
        return false;
    }
    _capabilities.emplace(capability.id, capability);
    error.clear();
    return true;
}

bool ScienceProcessingRegistry::addBuiltInSource(
    const ScienceSourceDescriptor& source, std::string& error)
{
    ScienceProcessingCapability capability;
    capability.id = "science-provider/" + source.id;
    capability.displayName = source.name.empty() ? source.id : source.name;
    capability.engineId = "science-query-service";
    capability.executionKind =
        ScienceProcessingExecutionKind::BuiltInProvider;
    capability.sourceIds = {source.id};
    capability.inputFormats = {"geotemporal-query-v1"};
    appendOutput(source.capabilities.rasterLayerOutput,
                 ScienceOutputKind::RasterLayer, capability.outputKinds);
    appendOutput(source.capabilities.tableOutput,
                 ScienceOutputKind::Table, capability.outputKinds);
    appendOutput(source.capabilities.vectorOutput,
                 ScienceOutputKind::VectorFeatures, capability.outputKinds);
    appendOutput(source.capabilities.embeddingOutput,
                 ScienceOutputKind::Embedding, capability.outputKinds);
    appendOutput(source.capabilities.timeSeriesOutput,
                 ScienceOutputKind::TimeSeries, capability.outputKinds);
    appendOutput(source.capabilities.analysisOutput,
                 ScienceOutputKind::Analysis, capability.outputKinds);
    appendOutput(source.capabilities.exportOutput,
                 ScienceOutputKind::Export, capability.outputKinds);
    capability.analysisKinds = {ScienceAnalysisKind::None};
    if (source.capabilities.analysisOutput)
    {
        capability.analysisKinds.insert(capability.analysisKinds.end(), {
            ScienceAnalysisKind::PointSeries,
            ScienceAnalysisKind::RegionalChange,
            ScienceAnalysisKind::PrincipalComponents,
            ScienceAnalysisKind::SphericalClusters});
    }
    capability.available = source.health != ScienceSourceHealth::Unavailable;
    capability.availabilityMessage = source.healthMessage;
    return add(capability, error);
}

bool ScienceProcessingRegistry::registerPlugin(
    const ScienceProcessingPluginManifest& manifest,
    const std::vector<ScienceProcessingCapability>& capabilities,
    std::string& error)
{
    if (!validateScienceProcessingPluginManifest(manifest, error) ||
        capabilities.empty())
    {
        if (error.empty()) error = "processing plugin has no capabilities";
        return false;
    }
    std::vector<ScienceProcessingCapability> normalized = capabilities;
    std::vector<std::string> replaceable;
    for (ScienceProcessingCapability& capability : normalized)
    {
        capability.executionKind =
            ScienceProcessingExecutionKind::NativePlugin;
        capability.optionalPlugin = true;
        capability.available = true;
        capability.pluginAbiVersion = manifest.abiVersion;
        capability.pluginId = manifest.pluginId;
        capability.pluginVersion = manifest.pluginVersion;
        capability.availabilityMessage.clear();
        const auto existing = _capabilities.find(capability.id);
        if (existing != _capabilities.end() &&
            !(existing->second.optionalPlugin &&
              !existing->second.available &&
              existing->second.engineId == capability.engineId))
        {
            error = "processing capability id already exists: " +
                capability.id;
            return false;
        }
        if (!validateScienceProcessingCapability(capability, error))
            return false;
        if (existing != _capabilities.end())
            replaceable.push_back(capability.id);
    }
    for (const std::string& id : replaceable) _capabilities.erase(id);
    for (const ScienceProcessingCapability& capability : normalized)
        _capabilities.emplace(capability.id, capability);
    error.clear();
    return true;
}

const ScienceProcessingCapability* ScienceProcessingRegistry::find(
    const std::string& id) const
{
    const auto found = _capabilities.find(id);
    return found == _capabilities.end() ? nullptr : &found->second;
}

const ScienceProcessingCapability* ScienceProcessingRegistry::resolve(
    const GeoTemporalQuery& query) const
{
    for (const auto& entry : _capabilities)
    {
        const ScienceProcessingCapability& capability = entry.second;
        if (!capability.available || capability.executionKind !=
                ScienceProcessingExecutionKind::BuiltInProvider ||
            std::find(capability.sourceIds.begin(), capability.sourceIds.end(),
                      query.sourceId) == capability.sourceIds.end() ||
            !contains(capability.outputKinds, query.outputKind))
            continue;
        return &capability;
    }
    return nullptr;
}

std::vector<ScienceProcessingCapability> ScienceProcessingRegistry::list() const
{
    std::vector<ScienceProcessingCapability> output;
    output.reserve(_capabilities.size());
    for (const auto& entry : _capabilities) output.push_back(entry.second);
    return output;
}

std::vector<ScienceProcessingCapability>
optionalScienceProcessingCapabilities()
{
    const auto plugin = [](const std::string& id,
                           const std::string& name,
                           const std::vector<std::string>& formats,
                           const std::vector<ScienceOutputKind>& outputs)
    {
        ScienceProcessingCapability capability;
        capability.id = id;
        capability.displayName = name;
        capability.engineId = id;
        capability.executionKind = ScienceProcessingExecutionKind::NativePlugin;
        capability.inputFormats = formats;
        capability.outputKinds = outputs;
        capability.optionalPlugin = true;
        capability.available = false;
        capability.pluginAbiVersion = SCIENCE_PROCESSING_PLUGIN_ABI_V1;
        capability.availabilityMessage =
            "optional native plugin is not loaded";
        return capability;
    };
    return {
        plugin("duckdb-spatial", "DuckDB Spatial / GeoParquet",
               {"geoparquet", "parquet"},
               {ScienceOutputKind::Table,
                ScienceOutputKind::VectorFeatures,
                ScienceOutputKind::Analysis}),
        plugin("pmtiles-v3", "PMTiles v3",
               {"pmtiles-v3"},
               {ScienceOutputKind::RasterLayer,
                ScienceOutputKind::VectorFeatures}),
        plugin("zarr-family", "Zarr / cloud arrays",
               {"zarr-v2", "zarr-v3", "kerchunk-reference"},
               {ScienceOutputKind::RasterLayer,
                ScienceOutputKind::TimeSeries,
                ScienceOutputKind::Analysis})};
}
}
