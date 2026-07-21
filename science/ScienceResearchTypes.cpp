#include "ScienceResearchTypes.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace earthscience
{
namespace
{
    constexpr std::size_t MAX_STRING_BYTES = 8192;
    constexpr std::size_t MAX_LIST_ITEMS = 64;
    constexpr std::size_t MAX_STEPS = 32;

    bool validString(const std::string& value, bool required,
                     const char* field, std::string& error)
    {
        if (required && value.empty())
        {
            error = std::string(field) + " is required";
            return false;
        }
        if (value.size() > MAX_STRING_BYTES)
        {
            error = std::string(field) + " exceeds the string limit";
            return false;
        }
        return true;
    }

    bool validStrings(const std::vector<std::string>& values,
                      const char* field, std::string& error)
    {
        if (values.size() > MAX_LIST_ITEMS)
        {
            error = std::string(field) + " exceeds the list limit";
            return false;
        }
        for (const std::string& value : values)
            if (!validString(value, false, field, error)) return false;
        return true;
    }

    bool finiteNonNegative(double value)
    {
        return std::isfinite(value) && value >= 0.0;
    }

    bool validBounds(const ScienceWgs84Bounds& bounds)
    {
        return std::isfinite(bounds.west) && std::isfinite(bounds.south) &&
            std::isfinite(bounds.east) && std::isfinite(bounds.north) &&
            bounds.west >= -180.0 && bounds.east <= 180.0 &&
            bounds.south >= -90.0 && bounds.north <= 90.0 &&
            bounds.west < bounds.east && bounds.south < bounds.north;
    }

    bool emptyBounds(const ScienceWgs84Bounds& bounds)
    {
        return bounds.west == 0.0 && bounds.south == 0.0 &&
            bounds.east == 0.0 && bounds.north == 0.0;
    }

    bool validGeometry(const ScienceGeometry& geometry)
    {
        if (geometry.kind == ScienceGeometryKind::Point)
            return std::isfinite(geometry.point.latitude) &&
                geometry.point.latitude >= -90.0 &&
                geometry.point.latitude <= 90.0 &&
                std::isfinite(geometry.point.longitude) &&
                geometry.point.longitude >= -180.0 &&
                geometry.point.longitude <= 180.0 &&
                finiteNonNegative(geometry.requestedSpanMeters);
        return validBounds(geometry.bounds);
    }

    bool validScalar(const ScienceScalarSummary& summary,
                     std::string& error)
    {
        if (!validString(summary.variableId, true, "scalar variable", error) ||
            !validString(summary.displayName, false, "scalar name", error) ||
            !validString(summary.unit, false, "scalar unit", error))
            return false;
        const struct { bool valid; double value; } values[] = {
            {summary.centerValid, summary.center},
            {summary.minimumValid, summary.minimum},
            {summary.maximumValid, summary.maximum},
            {summary.meanValid, summary.mean},
        };
        for (const auto& value : values)
            if (value.valid && !std::isfinite(value.value))
            {
                error = "scalar evidence must be finite";
                return false;
            }
        return true;
    }

    bool validVariableSeries(
        const ScienceEvidenceVariableSeries& series, std::string& error)
    {
        if (!validString(series.variableId, true, "series variable", error) ||
            !validString(series.displayName, false, "series name", error) ||
            !validString(series.unit, false, "series unit", error) ||
            !validString(series.aggregationMethod, false,
                         "series aggregation", error))
            return false;
        if (!finiteNonNegative(series.nativeResolutionMeters))
        {
            error = "series resolution must be finite and non-negative";
            return false;
        }
        if (series.points.empty() || series.points.size() > 9)
        {
            error = "variable series must contain one to nine annual points";
            return false;
        }
        std::set<int> years;
        for (const ScienceEvidenceVariablePoint& point : series.points)
        {
            if (!years.insert(point.year).second)
            {
                error = "variable series years must be unique";
                return false;
            }
            if (point.valid && !std::isfinite(point.value))
            {
                error = "valid variable series values must be finite";
                return false;
            }
        }
        return true;
    }
}

const char* scienceResearchStateName(ScienceResearchState state)
{
    switch (state)
    {
    case ScienceResearchState::Draft: return "draft";
    case ScienceResearchState::Running: return "running";
    case ScienceResearchState::Partial: return "partial";
    case ScienceResearchState::Ready: return "ready";
    case ScienceResearchState::Failed: return "failed";
    }
    return "failed";
}

bool parseScienceResearchState(
    const std::string& value, ScienceResearchState& state)
{
    const ScienceResearchState states[] = {
        ScienceResearchState::Draft, ScienceResearchState::Running,
        ScienceResearchState::Partial, ScienceResearchState::Ready,
        ScienceResearchState::Failed};
    for (ScienceResearchState candidate : states)
        if (value == scienceResearchStateName(candidate))
        {
            state = candidate;
            return true;
        }
    return false;
}

bool isSafeScienceRecordId(const std::string& value)
{
    if (value.empty() || value.size() > 128 || value.front() == '.')
        return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char byte)
    {
        return std::isalnum(byte) || byte == '-' || byte == '_';
    });
}

bool validateScienceEvidence(
    const ScienceEvidenceRecord& record, std::string& error)
{
    error.clear();
    if (record.schemaVersion != SCIENCE_EVIDENCE_SCHEMA_V1)
        error = "unsupported evidence schema";
    else if (!isSafeScienceRecordId(record.evidenceId))
        error = "evidence id is unsafe";
    else if (!validString(record.artifactId, true, "artifact id", error) ||
             !validString(record.sourceId, true, "source id", error) ||
             !validString(record.sourceName, false, "source name", error) ||
             !validString(record.providerVersion, true, "provider version", error) ||
             !validString(record.datasetId, true, "dataset id", error) ||
             !validString(record.originalUrl, false, "original URL", error) ||
             !validString(record.attribution, true, "attribution", error) ||
             !validString(record.acquisitionTime, false, "acquisition time", error) ||
             !validString(record.publicationTime, false, "publication time", error) ||
             !validString(record.forecastReferenceTime, false,
                          "forecast reference time", error) ||
             !validString(record.processingVersion, true,
                          "processing version", error) ||
             !validString(record.createdAt, true, "created at", error))
        return false;
    else if (!validGeometry(record.requestedCoverage) ||
             (!emptyBounds(record.actualCoverage) &&
              !validBounds(record.actualCoverage)))
        error = "evidence coverage is invalid";
    else if (!validStrings(record.variables, "variables", error) ||
             record.variables.empty() ||
             !validStrings(record.units, "units", error) ||
             !validStrings(record.processingSteps, "processing steps", error) ||
             !validStrings(record.warnings, "warnings", error) ||
             !validStrings(record.interpretations, "interpretations", error) ||
             !validStrings(record.limitations, "limitations", error))
        return false;
    else if (record.scalarSummaries.size() > MAX_LIST_ITEMS ||
             record.variableSeries.size() > MAX_LIST_ITEMS ||
             record.primaryMetrics.size() > MAX_LIST_ITEMS)
        error = "numeric evidence exceeds the list limit";
    else if (!finiteNonNegative(record.sourceResolutionMeters) ||
             !finiteNonNegative(record.displayResolutionMeters) ||
             !finiteNonNegative(record.actualResolutionMeters))
        error = "evidence resolution must be finite and non-negative";
    else
    {
        for (const ScienceScalarSummary& summary : record.scalarSummaries)
            if (!validScalar(summary, error)) return false;
        for (const ScienceEvidenceVariableSeries& series :
             record.variableSeries)
            if (!validVariableSeries(series, error)) return false;
        for (const ScienceEvidenceMetric& metric : record.primaryMetrics)
            if (!std::isfinite(metric.value) ||
                !validString(metric.unit, false, "metric unit", error))
            {
                if (error.empty()) error = "metric evidence must be finite";
                return false;
            }
        return true;
    }
    return false;
}

bool validateScienceResearch(
    const ScienceResearchRecord& record, std::string& error)
{
    error.clear();
    if (record.schemaVersion != SCIENCE_RESEARCH_SCHEMA_V1)
        error = "unsupported research schema";
    else if (!isSafeScienceRecordId(record.researchId))
        error = "research id is unsafe";
    else if (!validString(record.question, true, "research question", error) ||
             !validString(record.createdAt, true, "created at", error) ||
             !validString(record.updatedAt, true, "updated at", error))
        return false;
    else if (record.steps.size() > MAX_STEPS)
        error = "research steps exceed the limit";
    else
    {
        std::set<std::uint64_t> liveIds;
        std::set<std::string> evidenceIds;
        for (const ScienceResearchStep& step : record.steps)
        {
            if (!validString(step.sourceId, true, "step source", error) ||
                !validString(step.artifactId, false, "step artifact", error) ||
                !validString(step.evidenceId, false, "step evidence", error) ||
                !validString(step.message, false, "step message", error))
                return false;
            if (!step.evidenceId.empty() &&
                !isSafeScienceRecordId(step.evidenceId))
            {
                error = "step evidence id is unsafe";
                return false;
            }
            if (step.liveJobId != 0 && !liveIds.insert(step.liveJobId).second)
            {
                error = "duplicate live job id";
                return false;
            }
            if (!step.evidenceId.empty() &&
                !evidenceIds.insert(step.evidenceId).second)
            {
                error = "duplicate evidence id";
                return false;
            }
        }
        return true;
    }
    return false;
}
}
