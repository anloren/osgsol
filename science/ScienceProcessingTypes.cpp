#include "ScienceProcessingTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace earthscience
{
namespace
{
    constexpr std::size_t MAX_ID_BYTES = 160;
    constexpr std::size_t MAX_TEXT_BYTES = 4096;
    constexpr std::size_t MAX_LIST_ITEMS = 256;

    bool bounded(const std::string& value, std::size_t maximum)
    {
        return value.size() <= maximum;
    }

    bool validToken(const std::string& value, bool allowSlash)
    {
        if (value.empty() || value.size() > MAX_ID_BYTES) return false;
        return std::all_of(value.begin(), value.end(),
            [allowSlash](unsigned char character)
            {
                return std::isalnum(character) || character == '-' ||
                    character == '_' || character == '.' || character == ':' ||
                    (allowSlash && character == '/');
            });
    }

    bool validStringList(const std::vector<std::string>& values)
    {
        if (values.size() > MAX_LIST_ITEMS) return false;
        return std::all_of(values.begin(), values.end(),
            [](const std::string& value)
            { return !value.empty() && bounded(value, MAX_TEXT_BYTES); });
    }
}

const char* scienceProcessingExecutionKindName(
    ScienceProcessingExecutionKind kind)
{
    switch (kind)
    {
    case ScienceProcessingExecutionKind::BuiltInProvider:
        return "built-in-provider";
    case ScienceProcessingExecutionKind::NativePlugin:
        return "native-plugin";
    }
    return "built-in-provider";
}

bool parseScienceProcessingExecutionKind(
    const std::string& value, ScienceProcessingExecutionKind& kind)
{
    if (value == "built-in-provider")
        kind = ScienceProcessingExecutionKind::BuiltInProvider;
    else if (value == "native-plugin")
        kind = ScienceProcessingExecutionKind::NativePlugin;
    else return false;
    return true;
}

bool isSafeScienceProcessingRecordId(const std::string& value)
{
    return validToken(value, false) && value != "." && value != "..";
}

bool validateScienceProcessingCapability(
    const ScienceProcessingCapability& capability, std::string& error)
{
    if (!validToken(capability.id, true) ||
        !validToken(capability.engineId, false))
    {
        error = "processing capability id or engine id is invalid";
        return false;
    }
    if (capability.displayName.empty() ||
        !bounded(capability.displayName, MAX_TEXT_BYTES) ||
        !bounded(capability.availabilityMessage, MAX_TEXT_BYTES) ||
        !validStringList(capability.inputFormats) ||
        capability.outputKinds.empty() ||
        capability.outputKinds.size() > MAX_LIST_ITEMS ||
        capability.analysisKinds.size() > MAX_LIST_ITEMS ||
        !validStringList(capability.sourceIds))
    {
        error = "processing capability metadata is invalid";
        return false;
    }
    if (capability.executionKind ==
            ScienceProcessingExecutionKind::BuiltInProvider)
    {
        if (capability.optionalPlugin || !capability.pluginId.empty() ||
            capability.pluginAbiVersion != 0 ||
            capability.sourceIds.empty())
        {
            error = "built-in processing capability has plugin metadata";
            return false;
        }
    }
    else
    {
        if (!capability.optionalPlugin ||
            capability.pluginAbiVersion != SCIENCE_PROCESSING_PLUGIN_ABI_V1)
        {
            error = "native processing capability has an invalid plugin boundary";
            return false;
        }
        if (capability.available &&
            (!validToken(capability.pluginId, false) ||
             capability.pluginVersion.empty() ||
             !bounded(capability.pluginVersion, MAX_ID_BYTES)))
        {
            error = "available processing plugin has no identity";
            return false;
        }
    }
    error.clear();
    return true;
}

bool validateScienceProcessingPluginManifest(
    const ScienceProcessingPluginManifest& manifest, std::string& error)
{
    if (manifest.abiVersion != SCIENCE_PROCESSING_PLUGIN_ABI_V1 ||
        !validToken(manifest.pluginId, false) ||
        manifest.pluginVersion.empty() ||
        !bounded(manifest.pluginVersion, MAX_ID_BYTES) ||
        manifest.displayName.empty() ||
        !bounded(manifest.displayName, MAX_TEXT_BYTES))
    {
        error = "processing plugin manifest is incompatible";
        return false;
    }
    error.clear();
    return true;
}

bool validateScienceProcessingRecord(
    const ScienceProcessingRecord& record, std::string& error)
{
    if (record.schemaVersion != SCIENCE_PROCESSING_SCHEMA_V1)
    {
        error = "processing record schema is unsupported";
        return false;
    }
    if (!isSafeScienceProcessingRecordId(record.recordId) ||
        record.liveJobId == 0 || !validToken(record.capabilityId, true) ||
        !validToken(record.sourceId, false))
    {
        error = "processing record identity is invalid";
        return false;
    }
    if (!std::isfinite(record.cost.estimatedDurationSeconds) ||
        !std::isfinite(record.progress.elapsedSeconds) ||
        !bounded(record.progress.unit, MAX_ID_BYTES) ||
        !bounded(record.resultArtifactId, MAX_ID_BYTES) ||
        !bounded(record.message, MAX_TEXT_BYTES) ||
        !bounded(record.createdAt, MAX_ID_BYTES) ||
        !bounded(record.updatedAt, MAX_ID_BYTES) ||
        record.createdAt.empty() || record.updatedAt.empty() ||
        !validStringList(record.warnings) ||
        record.provenance.size() > MAX_LIST_ITEMS)
    {
        error = "processing record metadata is invalid";
        return false;
    }
    for (const ScienceProcessingProvenance& source : record.provenance)
    {
        if (!validToken(source.sourceId, false) ||
            !bounded(source.providerVersion, MAX_TEXT_BYTES) ||
            !bounded(source.datasetId, MAX_TEXT_BYTES) ||
            !bounded(source.originalUrl, MAX_TEXT_BYTES) ||
            !bounded(source.attribution, MAX_TEXT_BYTES) ||
            !bounded(source.acquisitionTime, MAX_ID_BYTES))
        {
            error = "processing provenance is invalid";
            return false;
        }
    }
    error.clear();
    return true;
}
}
