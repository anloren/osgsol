#ifndef OSGSOL_SCIENCE_RESEARCH_TYPES_H
#define OSGSOL_SCIENCE_RESEARCH_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

#include "ScienceQueryTypes.h"

namespace earthscience
{
    inline constexpr const char* SCIENCE_EVIDENCE_SCHEMA_V1 =
        "osgsol-science-evidence-v1";
    inline constexpr const char* SCIENCE_RESEARCH_SCHEMA_V1 =
        "osgsol-science-research-v1";

    enum class ScienceResearchState
    {
        Draft,
        Running,
        Partial,
        Ready,
        Failed,
    };

    const char* scienceResearchStateName(ScienceResearchState state);
    bool parseScienceResearchState(
        const std::string& value, ScienceResearchState& state);

    struct ScienceEvidenceMetric
    {
        ScienceMetric metric = ScienceMetric::CosineSimilarity;
        int baselineYear = 0;
        int comparisonYear = 0;
        double value = 0.0;
        std::string unit;
    };

    struct ScienceEvidenceVariablePoint
    {
        int year = 0;
        double value = 0.0;
        bool valid = false;
    };

    struct ScienceEvidenceVariableSeries
    {
        std::string variableId;
        std::string displayName;
        std::string unit;
        std::string aggregationMethod;
        double nativeResolutionMeters = 0.0;
        std::vector<ScienceEvidenceVariablePoint> points;
    };

    struct ScienceEvidenceRecord
    {
        std::string schemaVersion = SCIENCE_EVIDENCE_SCHEMA_V1;
        std::string evidenceId;
        std::string artifactId;
        std::string sourceId;
        std::string sourceName;
        std::string providerVersion;
        std::string datasetId;
        std::string originalUrl;
        std::string attribution;
        ScienceGeometry requestedCoverage;
        ScienceWgs84Bounds actualCoverage;
        std::vector<std::string> variables;
        std::vector<std::string> units;
        std::string acquisitionTime;
        std::string publicationTime;
        std::string forecastReferenceTime;
        std::vector<ScienceScalarSummary> scalarSummaries;
        std::vector<ScienceEvidenceVariableSeries> variableSeries;
        std::vector<ScienceEvidenceMetric> primaryMetrics;
        std::uint64_t validCellCount = 0;
        std::uint64_t noDataCellCount = 0;
        double sourceResolutionMeters = 0.0;
        double displayResolutionMeters = 0.0;
        double actualResolutionMeters = 0.0;
        std::string processingVersion;
        std::vector<std::string> processingSteps;
        std::vector<std::string> warnings;
        std::vector<std::string> interpretations;
        std::vector<std::string> limitations;
        std::string createdAt;
    };

    struct ScienceResearchStep
    {
        std::uint64_t liveJobId = 0;
        std::string sourceId;
        ScienceJobState state = ScienceJobState::Idle;
        std::string artifactId;
        std::string evidenceId;
        std::string message;
    };

    struct ScienceResearchRecord
    {
        std::string schemaVersion = SCIENCE_RESEARCH_SCHEMA_V1;
        std::string researchId;
        std::string question;
        ScienceResearchState state = ScienceResearchState::Draft;
        std::vector<ScienceResearchStep> steps;
        std::string createdAt;
        std::string updatedAt;
    };

    bool isSafeScienceRecordId(const std::string& value);
    bool validateScienceEvidence(
        const ScienceEvidenceRecord& record, std::string& error);
    bool validateScienceResearch(
        const ScienceResearchRecord& record, std::string& error);
}

#endif
