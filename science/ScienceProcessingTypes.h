#ifndef OSGSOL_SCIENCE_PROCESSING_TYPES_H
#define OSGSOL_SCIENCE_PROCESSING_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

#include "ScienceQueryTypes.h"

namespace earthscience
{
    inline constexpr const char* SCIENCE_PROCESSING_SCHEMA_V1 =
        "osgsol-science-processing-v1";
    inline constexpr std::uint32_t SCIENCE_PROCESSING_PLUGIN_ABI_V1 = 1u;

    enum class ScienceProcessingExecutionKind
    {
        BuiltInProvider,
        NativePlugin,
    };

    const char* scienceProcessingExecutionKindName(
        ScienceProcessingExecutionKind kind);
    bool parseScienceProcessingExecutionKind(
        const std::string& value, ScienceProcessingExecutionKind& kind);

    struct ScienceProcessingPluginManifest
    {
        std::uint32_t abiVersion = SCIENCE_PROCESSING_PLUGIN_ABI_V1;
        std::string pluginId;
        std::string pluginVersion;
        std::string displayName;
    };

    struct ScienceProcessingCapability
    {
        std::string id;
        std::string displayName;
        std::string engineId;
        ScienceProcessingExecutionKind executionKind =
            ScienceProcessingExecutionKind::BuiltInProvider;
        std::vector<std::string> sourceIds;
        std::vector<std::string> inputFormats;
        std::vector<ScienceOutputKind> outputKinds;
        std::vector<ScienceAnalysisKind> analysisKinds;
        bool cancellable = true;
        bool persistentRecord = true;
        bool optionalPlugin = false;
        bool available = false;
        std::uint32_t pluginAbiVersion = 0;
        std::string pluginId;
        std::string pluginVersion;
        std::string availabilityMessage;
    };

    struct ScienceProcessingProvenance
    {
        std::string sourceId;
        std::string providerVersion;
        std::string datasetId;
        std::string originalUrl;
        std::string attribution;
        std::string acquisitionTime;
    };

    struct ScienceProcessingRecord
    {
        std::string schemaVersion = SCIENCE_PROCESSING_SCHEMA_V1;
        std::string recordId;
        std::uint64_t liveJobId = 0;
        std::string capabilityId;
        std::string sourceId;
        ScienceJobState state = ScienceJobState::Idle;
        ScienceQueryCost cost;
        ScienceProgress progress;
        bool cancelRequested = false;
        std::string resultArtifactId;
        std::vector<std::string> warnings;
        std::vector<ScienceProcessingProvenance> provenance;
        std::string message;
        std::string createdAt;
        std::string updatedAt;
    };

    bool validateScienceProcessingCapability(
        const ScienceProcessingCapability& capability,
        std::string& error);
    bool validateScienceProcessingPluginManifest(
        const ScienceProcessingPluginManifest& manifest,
        std::string& error);
    bool validateScienceProcessingRecord(
        const ScienceProcessingRecord& record, std::string& error);
    bool isSafeScienceProcessingRecordId(const std::string& value);
}

#endif
