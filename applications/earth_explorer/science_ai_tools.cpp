#include "science_ai_tools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <ScienceEmbedding.h>
#include <ScienceQueryService.h>
#include <ScienceResearchBrief.h>
#include <ScienceResearchManager.h>
#include <modeling/Math.h>
#include <readerwriter/EarthManipulator.h>

#include "LayerManager.h"
#include "ai_tools.h"
#include "science_preview_layer.h"
#include "science_query_builder.h"

namespace
{
    picojson::value errorJson(const std::string& message)
    {
        picojson::object error;
        error["error"] = picojson::value(message);
        return picojson::value(error);
    }

    bool optionalNumber(const picojson::value& args, const char* key,
                        double& value)
    {
        if (!args.is<picojson::object>() || !args.contains(key)) return true;
        if (!args.get(key).is<double>()) return false;
        value = args.get(key).get<double>();
        return true;
    }

    bool optionalString(const picojson::value& args, const char* key,
                        std::string& value)
    {
        if (!args.is<picojson::object>() || !args.contains(key)) return true;
        if (!args.get(key).is<std::string>()) return false;
        value = args.get(key).get<std::string>();
        return true;
    }

    bool optionalBoolean(const picojson::value& args, const char* key,
                         bool& value)
    {
        if (!args.is<picojson::object>() || !args.contains(key)) return true;
        if (!args.get(key).is<bool>()) return false;
        value = args.get(key).get<bool>();
        return true;
    }

    bool optionalInteger(const picojson::value& args, const char* key,
                         int& value)
    {
        double number = static_cast<double>(value);
        if (!optionalNumber(args, key, number) || !std::isfinite(number) ||
            std::floor(number) != number ||
            number < static_cast<double>(std::numeric_limits<int>::min()) ||
            number > static_cast<double>(std::numeric_limits<int>::max()))
            return false;
        value = static_cast<int>(number);
        return true;
    }

    bool requiredString(const picojson::value& args, const char* key,
                        std::string& value)
    {
        if (!args.is<picojson::object>() || !args.contains(key) ||
            !args.get(key).is<std::string>())
            return false;
        value = args.get(key).get<std::string>();
        return !value.empty();
    }

    bool requiredInteger(const picojson::value& args, const char* key,
                         int& value)
    {
        return args.is<picojson::object>() && args.contains(key) &&
               optionalInteger(args, key, value);
    }

    bool requiredNumber(const picojson::value& args, const char* key,
                        double& value)
    {
        return args.is<picojson::object>() && args.contains(key) &&
               optionalNumber(args, key, value) && std::isfinite(value);
    }

    picojson::array stringsJson(const std::vector<std::string>& values)
    {
        picojson::array result;
        result.reserve(values.size());
        for (const std::string& value : values)
            result.push_back(picojson::value(value));
        return result;
    }

    picojson::array yearsJson(const std::vector<int>& years)
    {
        picojson::array result;
        result.reserve(years.size());
        for (int year : years)
            result.push_back(picojson::value(static_cast<double>(year)));
        return result;
    }

    std::string sourceVersion(const earthscience::ScienceArtifact& artifact)
    {
        return artifact.sourceReferences.empty()
            ? std::string() : artifact.sourceReferences.front().providerVersion;
    }

    picojson::value progressJson(
        const earthscience::ScienceProgress& progress)
    {
        picojson::object item;
        item["stage"] = picojson::value(std::string(
            earthscience::scienceProgressStageName(progress.stage)));
        item["completed"] = picojson::value(
            static_cast<double>(progress.completedUnits));
        item["total"] = picojson::value(
            static_cast<double>(progress.totalUnits));
        item["unit"] = picojson::value(progress.unit);
        item["determinate"] = picojson::value(progress.determinate);
        if (progress.determinate && progress.totalUnits != 0)
        {
            const double fraction = static_cast<double>(progress.completedUnits) /
                static_cast<double>(progress.totalUnits);
            item["percent"] = picojson::value(
                std::clamp(fraction * 100.0, 0.0, 100.0));
        }
        return picojson::value(item);
    }

    constexpr std::size_t MAX_PRIMARY_METRIC_ENTRIES = 32;

    struct CoverageEvidence
    {
        std::string basis;
        earthscience::ScienceWgs84Bounds bounds;
        double sourceResolutionMeters = 0.0;
        double displayResolutionMeters = 0.0;
        double actualResolutionMeters = 0.0;
        double fraction = 0.0;
        std::uint64_t validCells = 0;
        std::uint64_t noDataCells = 0;
        bool hasStatistics = false;
        bool hasActualResolution = false;
    };

    CoverageEvidence coverageEvidence(
        const earthscience::ScienceArtifact& artifact,
        earthscience::ScienceAnalysisKind analysisKind)
    {
        CoverageEvidence evidence;
        if (artifact.query.outputKind ==
            earthscience::ScienceOutputKind::RasterLayer)
        {
            evidence.basis = "raster";
            evidence.bounds = artifact.raster.bounds;
            evidence.sourceResolutionMeters =
                artifact.raster.sourceResolutionMeters;
            evidence.displayResolutionMeters =
                artifact.raster.displayResolutionMeters;
            return evidence;
        }

        if (analysisKind == earthscience::ScienceAnalysisKind::RegionalChange)
        {
            const earthscience::ScienceRegionalChangeSummary& regional =
                artifact.analysis.regionalChange;
            if (regional.totalCellCount != 0 ||
                regional.validOverlapCount != 0 ||
                regional.noDataCellCount != 0 ||
                regional.actualResolutionMeters > 0.0)
            {
                evidence.basis = "regional-change";
                evidence.bounds = regional.bounds;
                evidence.fraction = regional.coverageFraction;
                evidence.validCells = regional.validOverlapCount;
                evidence.noDataCells = regional.noDataCellCount;
                evidence.actualResolutionMeters =
                    regional.actualResolutionMeters;
                evidence.hasStatistics = true;
                evidence.hasActualResolution = true;
                evidence.sourceResolutionMeters =
                    evidence.actualResolutionMeters;
                evidence.displayResolutionMeters =
                    evidence.actualResolutionMeters;
                return evidence;
            }

            const earthscience::ScienceScalarChangeRaster& scalar =
                artifact.analysis.scalarChangeRaster;
            if (scalar.width > 0 || scalar.height > 0 ||
                scalar.validCellCount != 0 || scalar.noDataCellCount != 0 ||
                scalar.actualResolutionMeters > 0.0)
            {
                evidence.basis = "scalar-change";
                evidence.bounds = scalar.bounds;
                evidence.fraction = scalar.coverageFraction;
                evidence.validCells = scalar.validCellCount;
                evidence.noDataCells = scalar.noDataCellCount;
                evidence.actualResolutionMeters =
                    scalar.actualResolutionMeters;
                evidence.hasStatistics = true;
                evidence.hasActualResolution = true;
                evidence.sourceResolutionMeters =
                    evidence.actualResolutionMeters;
                evidence.displayResolutionMeters =
                    evidence.actualResolutionMeters;
                return evidence;
            }
        }

        evidence.basis = "embedding";
        evidence.bounds = artifact.embedding.bounds;
        evidence.fraction = artifact.embedding.coverageFraction;
        evidence.validCells = artifact.embedding.validCellCount;
        evidence.noDataCells = artifact.embedding.noDataCellCount;
        evidence.actualResolutionMeters =
            artifact.embedding.actualResolutionMeters;
        evidence.hasStatistics = true;
        evidence.hasActualResolution = true;
        evidence.sourceResolutionMeters = evidence.actualResolutionMeters;
        evidence.displayResolutionMeters = evidence.actualResolutionMeters;
        return evidence;
    }

    picojson::value artifactSummaryJson(
        const earthscience::ScienceArtifact& artifact)
    {
        picojson::object item;
        item["artifact_id"] = picojson::value(artifact.artifactId);
        item["generation"] = picojson::value(
            static_cast<double>(artifact.generation));
        item["visualization_id"] = picojson::value(artifact.visualizationId);
        const earthscience::ScienceAnalysisKind analysisKind =
            artifact.analysis.kind != earthscience::ScienceAnalysisKind::None
                ? artifact.analysis.kind : artifact.query.analysis.kind;
        item["kind"] = picojson::value(std::string(
            analysisKind == earthscience::ScienceAnalysisKind::None
                ? earthscience::scienceOutputKindName(artifact.query.outputKind)
                : earthscience::scienceAnalysisKindName(analysisKind)));
        item["years"] = picojson::value(
            yearsJson(artifact.query.time.explicitYears));
        item["time_start"] = picojson::value(
            artifact.query.time.intervalStart);
        item["time_end"] = picojson::value(
            artifact.query.time.intervalEnd);

        picojson::array metrics;
        if (artifact.analysis.metrics)
        {
            for (const earthscience::ScienceMetricResult& metric :
                 *artifact.analysis.metrics)
            {
                if (!std::isfinite(metric.value) ||
                    metrics.size() >= MAX_PRIMARY_METRIC_ENTRIES)
                    continue;
                picojson::object entry;
                entry["metric"] = picojson::value(std::string(
                    earthscience::scienceMetricName(metric.metric)));
                entry["baseline_year"] = picojson::value(
                    static_cast<double>(metric.baselineYear));
                entry["comparison_year"] = picojson::value(
                    static_cast<double>(metric.comparisonYear));
                entry["value"] = picojson::value(metric.value);
                entry["unit"] = picojson::value(metric.unit);
                metrics.push_back(picojson::value(entry));
            }
        }
        item["primary_metrics"] = picojson::value(metrics);
        item["primary_metrics_limit"] = picojson::value(
            static_cast<double>(MAX_PRIMARY_METRIC_ENTRIES));
        item["primary_metrics_truncated"] = picojson::value(
            artifact.analysis.metrics &&
            artifact.analysis.metrics->size() > metrics.size());

        constexpr std::size_t MAX_SCALAR_SUMMARIES = 16;
        picojson::array scalarSummaries;
        for (const earthscience::ScienceScalarSummary& summary :
             artifact.scalarSummaries)
        {
            if (scalarSummaries.size() >= MAX_SCALAR_SUMMARIES) break;
            picojson::object entry;
            entry["variable_id"] = picojson::value(summary.variableId);
            entry["name"] = picojson::value(summary.displayName);
            entry["unit"] = picojson::value(summary.unit);
            if (summary.centerValid && std::isfinite(summary.center))
                entry["center"] = picojson::value(summary.center);
            if (summary.minimumValid && std::isfinite(summary.minimum))
                entry["minimum"] = picojson::value(summary.minimum);
            if (summary.maximumValid && std::isfinite(summary.maximum))
                entry["maximum"] = picojson::value(summary.maximum);
            if (summary.meanValid && std::isfinite(summary.mean))
                entry["mean"] = picojson::value(summary.mean);
            entry["valid_cells"] = picojson::value(
                static_cast<double>(summary.validCellCount));
            entry["no_data_cells"] = picojson::value(
                static_cast<double>(summary.noDataCellCount));
            scalarSummaries.push_back(picojson::value(entry));
        }
        item["scalar_summaries"] = picojson::value(scalarSummaries);
        item["scalar_summaries_truncated"] = picojson::value(
            artifact.scalarSummaries.size() > scalarSummaries.size());

        const earthscience::ScienceRegionalChangeSummary& regional =
            artifact.analysis.regionalChange;
        if (analysisKind == earthscience::ScienceAnalysisKind::RegionalChange)
        {
            picojson::object statistics;
            if (std::isfinite(regional.mean))
                statistics["mean"] = picojson::value(regional.mean);
            if (std::isfinite(regional.median))
                statistics["median"] = picojson::value(regional.median);
            if (std::isfinite(regional.standardDeviation))
                statistics["standard_deviation"] =
                    picojson::value(regional.standardDeviation);
            if (std::isfinite(regional.minimum))
                statistics["minimum"] = picojson::value(regional.minimum);
            if (std::isfinite(regional.maximum))
                statistics["maximum"] = picojson::value(regional.maximum);
            item["regional_statistics"] = picojson::value(statistics);
        }

        const CoverageEvidence evidence = coverageEvidence(
            artifact, analysisKind);
        picojson::object coverage;
        coverage["basis"] = picojson::value(evidence.basis);
        coverage["west"] = picojson::value(evidence.bounds.west);
        coverage["south"] = picojson::value(evidence.bounds.south);
        coverage["east"] = picojson::value(evidence.bounds.east);
        coverage["north"] = picojson::value(evidence.bounds.north);
        coverage["source_resolution_m"] = picojson::value(
            evidence.sourceResolutionMeters);
        coverage["display_resolution_m"] = picojson::value(
            evidence.displayResolutionMeters);
        if (evidence.hasActualResolution)
            coverage["actual_resolution_m"] = picojson::value(
                evidence.actualResolutionMeters);
        if (evidence.hasStatistics)
        {
            coverage["fraction"] = picojson::value(evidence.fraction);
            coverage["valid_cells"] = picojson::value(
                static_cast<double>(evidence.validCells));
            coverage["no_data_cells"] = picojson::value(
                static_cast<double>(evidence.noDataCells));
        }
        item["coverage"] = picojson::value(coverage);

        picojson::object source;
        source["id"] = picojson::value(artifact.query.sourceId);
        source["version"] = picojson::value(sourceVersion(artifact));
        if (!artifact.sourceReferences.empty())
        {
            const earthscience::ScienceSourceReference& reference =
                artifact.sourceReferences.front();
            source["dataset_id"] = picojson::value(reference.datasetId);
            source["url"] = picojson::value(reference.originalUrl);
            source["attribution"] = picojson::value(reference.attribution);
            item["dataset_id"] = picojson::value(reference.datasetId);
            item["source_url"] = picojson::value(reference.originalUrl);
            item["source_version"] = picojson::value(reference.providerVersion);
            item["attribution"] = picojson::value(reference.attribution);
            item["acquisition_time"] = picojson::value(
                reference.acquisitionTime);

            constexpr std::size_t MAX_SOURCE_EVIDENCE_ENTRIES = 32;
            picojson::array sourceEvidence;
            for (const earthscience::ScienceEvidenceField& field :
                 reference.fields)
            {
                if (sourceEvidence.size() >= MAX_SOURCE_EVIDENCE_ENTRIES) break;
                picojson::object entry;
                entry["id"] = picojson::value(field.id);
                entry["label"] = picojson::value(field.displayName);
                entry["value"] = picojson::value(field.value);
                entry["unit"] = picojson::value(field.unit);
                sourceEvidence.push_back(picojson::value(entry));
                if (field.id == "scene_id")
                    item["scene_id"] = picojson::value(field.value);
                else if (field.id == "scene_cloud_cover")
                {
                    char* end = nullptr;
                    const double cloud = std::strtod(field.value.c_str(), &end);
                    if (end && *end == '\0' && std::isfinite(cloud))
                        item["scene_cloud_cover_percent"] =
                            picojson::value(cloud);
                }
                else if (field.id == "visual_asset")
                    item["cog_url"] = picojson::value(field.value);
            }
            item["source_evidence"] = picojson::value(sourceEvidence);
            source["acquisition_time"] = picojson::value(
                reference.acquisitionTime);
            source["evidence"] = picojson::value(sourceEvidence);
        }
        item["source"] = picojson::value(source);

        picojson::object processing;
        processing["version"] = picojson::value(artifact.processingVersion);
        std::vector<std::string> steps;
        if (artifact.embedding.processingSteps)
            steps.insert(steps.end(), artifact.embedding.processingSteps->begin(),
                         artifact.embedding.processingSteps->end());
        for (const earthscience::ScienceSourceReference& reference :
             artifact.sourceReferences)
            steps.insert(steps.end(), reference.processingSteps.begin(),
                         reference.processingSteps.end());
        processing["steps"] = picojson::value(stringsJson(steps));
        item["processing"] = picojson::value(processing);
        item["processing_version"] = picojson::value(
            artifact.processingVersion);

        std::vector<std::string> warnings = artifact.warnings;
        if (artifact.embedding.warnings)
            warnings.insert(warnings.end(), artifact.embedding.warnings->begin(),
                            artifact.embedding.warnings->end());
        item["warnings"] = picojson::value(stringsJson(warnings));
        item["limitations"] = picojson::value(artifact.analysis.limitations
            ? stringsJson(*artifact.analysis.limitations) : picojson::array{});

        item["west"] = picojson::value(evidence.bounds.west);
        item["south"] = picojson::value(evidence.bounds.south);
        item["east"] = picojson::value(evidence.bounds.east);
        item["north"] = picojson::value(evidence.bounds.north);
        item["source_resolution_m"] = picojson::value(
            evidence.sourceResolutionMeters);
        item["display_resolution_m"] = picojson::value(
            evidence.displayResolutionMeters);
        const int year = artifact.query.time.explicitYears.empty()
            ? 0 : artifact.query.time.explicitYears.front();
        item["year"] = picojson::value(static_cast<double>(year));
        return picojson::value(item);
    }

    bool validCoordinates(double latitude, double longitude)
    {
        return std::isfinite(latitude) && latitude >= -90.0 &&
               latitude <= 90.0 && std::isfinite(longitude) &&
               longitude >= -180.0 && longitude <= 180.0;
    }

    bool validYear(const earthscience::ScienceSourceDescriptor& source,
                   int year)
    {
        return year >= source.firstYear && year <= source.lastYear;
    }

    bool meanDirection(const earthscience::ScienceArtifact& artifact,
                       std::array<float,
                           earthscience::SCIENCE_EMBEDDING_COMPONENTS>& direction,
                       double& concentration, std::string& error)
    {
        const earthscience::ScienceEmbeddingPayload& embedding =
            artifact.embedding;
        if (!embedding.years || embedding.years->empty() ||
            embedding.width <= 0 || embedding.height <= 0 ||
            !embedding.values || !embedding.mask)
        {
            error = "science artifact has no compatible 64D embedding";
            return false;
        }
        const std::size_t width = static_cast<std::size_t>(embedding.width);
        const std::size_t height = static_cast<std::size_t>(embedding.height);
        const std::size_t yearCount = embedding.years->size();
        if (width > std::numeric_limits<std::size_t>::max() / height ||
            width * height > std::numeric_limits<std::size_t>::max() / yearCount)
        {
            error = "science artifact embedding shape overflowed";
            return false;
        }
        const std::size_t sampleCount = width * height * yearCount;
        if (sampleCount > std::numeric_limits<std::size_t>::max() /
                              earthscience::SCIENCE_EMBEDDING_COMPONENTS ||
            embedding.values->size() != sampleCount *
                earthscience::SCIENCE_EMBEDDING_COMPONENTS ||
            embedding.mask->size() != sampleCount)
        {
            error = "science artifact embedding shape is incompatible";
            return false;
        }
        return earthscience::aggregateEmbeddingVectors(
            embedding.values->data(), embedding.mask->data(), sampleCount,
            direction, concentration, error);
    }

    struct SourceEvidence
    {
        std::string sourceId;
        std::string providerVersion;
        std::vector<std::string> variables;
    };

    bool uniformSourceEvidence(
        const earthscience::ScienceArtifact& artifact,
        SourceEvidence& evidence)
    {
        if (artifact.query.sourceId.empty() ||
            artifact.sourceReferences.empty())
            return false;
        const earthscience::ScienceSourceReference& first =
            artifact.sourceReferences.front();
        if (first.sourceId.empty() || first.providerVersion.empty() ||
            first.variables.empty() || first.sourceId != artifact.query.sourceId)
            return false;
        evidence.sourceId = first.sourceId;
        evidence.providerVersion = first.providerVersion;
        evidence.variables = first.variables;
        for (const earthscience::ScienceSourceReference& reference :
             artifact.sourceReferences)
        {
            if (reference.sourceId != evidence.sourceId ||
                reference.providerVersion != evidence.providerVersion ||
                reference.variables != evidence.variables)
                return false;
        }
        return true;
    }

    picojson::value sourceJson(
        const earthscience::ScienceSourceDescriptor& source)
    {
        picojson::object item;
        item["id"] = picojson::value(source.id);
        item["name"] = picojson::value(source.name);
        item["category"] = picojson::value(source.category);
        item["provider_version"] = picojson::value(source.providerVersion);
        item["attribution"] = picojson::value(source.attribution);
        item["health"] = picojson::value(std::string(
            earthscience::scienceSourceHealthName(source.health)));
        item["health_message"] = picojson::value(source.healthMessage);
        item["first_year"] = picojson::value(
            static_cast<double>(source.firstYear));
        item["last_year"] = picojson::value(
            static_cast<double>(source.lastYear));
        item["resolution_m"] = picojson::value(
            source.nativeResolutionMeters);
        item["components"] = picojson::value(
            static_cast<double>(source.componentCount));
        item["experimental"] = picojson::value(source.experimental);

        picojson::array timeModes;
        if (source.capabilities.explicitYears)
            timeModes.push_back(picojson::value("explicit-years"));
        if (source.capabilities.intervalTime)
            timeModes.push_back(picojson::value("interval"));
        if (source.capabilities.instantTime)
            timeModes.push_back(picojson::value("instant"));
        item["time_modes"] = picojson::value(timeModes);

        picojson::array visualizations;
        for (const auto& visualization : source.visualizations)
        {
            picojson::object entry;
            entry["id"] = picojson::value(visualization.id);
            entry["name"] = picojson::value(visualization.displayName);
            entry["display_min"] = picojson::value(
                visualization.displayMinimum);
            entry["display_max"] = picojson::value(
                visualization.displayMaximum);
            entry["legend"] = picojson::value(visualization.legend);
            picojson::array channels;
            for (const std::string& channel : visualization.channelVariables)
                channels.push_back(picojson::value(channel));
            entry["channels"] = picojson::value(channels);
            visualizations.push_back(picojson::value(entry));
        }
        item["visualizations"] = picojson::value(visualizations);
        return picojson::value(item);
    }

    picojson::value snapshotJson(
        const earthscience::ScienceJobSnapshot& snapshot)
    {
        picojson::object result;
        result["job_id"] = picojson::value(
            static_cast<double>(snapshot.jobId));
        result["source_id"] = picojson::value(snapshot.query.sourceId);
        result["state"] = picojson::value(std::string(
            earthscience::scienceJobStateName(snapshot.state)));
        result["progress"] = progressJson(snapshot.progress);
        result["message"] = picojson::value(snapshot.message);
        result["lat"] = picojson::value(
            snapshot.query.geometry.point.latitude);
        result["lon"] = picojson::value(
            snapshot.query.geometry.point.longitude);
        const int year = snapshot.query.time.explicitYears.empty()
            ? 0 : snapshot.query.time.explicitYears.front();
        result["year"] = picojson::value(static_cast<double>(year));
        result["time_start"] = picojson::value(
            snapshot.query.time.intervalStart);
        result["time_end"] = picojson::value(
            snapshot.query.time.intervalEnd);
        result["camera_changed"] = picojson::value(false);
        result["layer_changed"] = picojson::value(false);

        std::shared_ptr<const earthscience::ScienceArtifact> resultArtifact =
            snapshot.lastSuccessfulArtifact;
        if (snapshot.state == earthscience::ScienceJobState::Ready &&
            snapshot.lastSuccessfulAnalysisArtifact &&
            snapshot.lastSuccessfulAnalysisArtifact->generation == snapshot.jobId)
            resultArtifact = snapshot.lastSuccessfulAnalysisArtifact;
        if (resultArtifact)
        {
            const earthscience::ScienceArtifact& artifact =
                *resultArtifact;
            result["artifact"] = artifactSummaryJson(artifact);
        }
        return picojson::value(result);
    }

    picojson::value researchJson(
        const earthscience::ScienceResearchRecord& research)
    {
        picojson::object result;
        result["research_id"] = picojson::value(research.researchId);
        result["question"] = picojson::value(research.question);
        result["state"] = picojson::value(std::string(
            earthscience::scienceResearchStateName(research.state)));
        result["created_at"] = picojson::value(research.createdAt);
        result["updated_at"] = picojson::value(research.updatedAt);
        picojson::array steps;
        std::size_t evidenceCount = 0;
        for (const earthscience::ScienceResearchStep& step : research.steps)
        {
            picojson::object item;
            item["job_id"] = picojson::value(
                static_cast<double>(step.liveJobId));
            item["source_id"] = picojson::value(step.sourceId);
            item["state"] = picojson::value(std::string(
                earthscience::scienceJobStateName(step.state)));
            item["artifact_id"] = picojson::value(step.artifactId);
            item["evidence_id"] = picojson::value(step.evidenceId);
            item["message"] = picojson::value(step.message);
            if (!step.evidenceId.empty()) ++evidenceCount;
            steps.emplace_back(item);
        }
        result["steps"] = picojson::value(steps);
        result["evidence_count"] = picojson::value(
            static_cast<double>(evidenceCount));
        result["camera_changed"] = picojson::value(false);
        result["layer_changed"] = picojson::value(false);
        return picojson::value(result);
    }

    picojson::array statementsJson(
        const std::vector<earthscience::ScienceBriefStatement>& statements)
    {
        picojson::array result;
        for (const earthscience::ScienceBriefStatement& statement : statements)
        {
            picojson::object item;
            item["label"] = picojson::value(statement.label);
            item["text"] = picojson::value(statement.text);
            item["evidence_ids"] = picojson::value(
                stringsJson(statement.evidenceIds));
            result.emplace_back(item);
        }
        return result;
    }

    picojson::value briefJson(
        const earthscience::ScienceResearchBrief& brief)
    {
        picojson::object result;
        result["research_id"] = picojson::value(brief.researchId);
        result["question"] = picojson::value(brief.question);
        result["state"] = picojson::value(std::string(
            earthscience::scienceResearchStateName(brief.state)));
        result["observations"] = picojson::value(
            statementsJson(brief.observations));
        result["inferences"] = picojson::value(
            statementsJson(brief.inferences));
        result["limitations"] = picojson::value(
            statementsJson(brief.limitations));

        picojson::array sources;
        for (const earthscience::ScienceBriefSourceRow& source : brief.sources)
        {
            picojson::object item;
            item["citation_number"] = picojson::value(
                static_cast<double>(source.citationNumber));
            item["evidence_id"] = picojson::value(source.evidenceId);
            item["source_id"] = picojson::value(source.sourceId);
            item["source_name"] = picojson::value(source.sourceName);
            item["dataset_id"] = picojson::value(source.datasetId);
            item["selected_time"] = picojson::value(source.selectedTime);
            picojson::object coverage;
            coverage["west"] = picojson::value(source.coverage.west);
            coverage["south"] = picojson::value(source.coverage.south);
            coverage["east"] = picojson::value(source.coverage.east);
            coverage["north"] = picojson::value(source.coverage.north);
            item["coverage"] = picojson::value(coverage);
            sources.emplace_back(item);
        }
        result["sources"] = picojson::value(sources);

        picojson::array citations;
        for (const earthscience::ScienceBriefCitation& citation :
             brief.citations)
        {
            picojson::object item;
            item["number"] = picojson::value(
                static_cast<double>(citation.number));
            item["evidence_id"] = picojson::value(citation.evidenceId);
            item["source_name"] = picojson::value(citation.sourceName);
            item["dataset_id"] = picojson::value(citation.datasetId);
            item["provider_version"] =
                picojson::value(citation.providerVersion);
            item["url"] = picojson::value(citation.originalUrl);
            item["attribution"] = picojson::value(citation.attribution);
            citations.emplace_back(item);
        }
        result["citations"] = picojson::value(citations);
        result["markdown"] = picojson::value(brief.markdown);
        result["camera_changed"] = picojson::value(false);
        result["layer_changed"] = picojson::value(false);
        return picojson::value(result);
    }
}

void registerScienceResearchTools(
    earthai::ToolRegistry* tools,
    earthscience::ScienceQueryService* service,
    SciencePreviewLayer* layer,
    LayerManager* layers,
    osgVerse::EarthManipulator* manipulator,
    const std::string& researchRoot)
{
    if (!tools || !service || !layer || !layers || !manipulator) return;
    const auto researchManager =
        std::make_shared<earthscience::ScienceResearchManager>(
            researchRoot.empty()
                ? earthscience::defaultScienceResearchRoot()
                : researchRoot);

    earthai::Tool search;
    search.name = "search_science_sources";
    search.description = u8"查询所有已注册科学数据源的健康状态、时空范围、"
        u8"分辨率、可视化语义、版本与署名。";
    search.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
    search.execute = [service](const picojson::value&)
    {
        picojson::array sourcesJson;
        const std::vector<earthscience::ScienceSourceDescriptor> sources =
            service->listSources();
        for (const auto& source : sources)
            sourcesJson.push_back(sourceJson(source));
        picojson::object result;
        result["sources"] = picojson::value(sourcesJson);
        return picojson::value(result);
    };
    tools->add(search);

    earthai::Tool start;
    start.name = "start_science_research";
    start.description = u8"异步提交 AlphaEarth 预览/64D 研究、Sentinel-2 "
        u8"真彩场景，或 Copernicus DEM 静态 DSM 高程查询。Sentinel-2 必须提供 time_start/time_end，可用 "
        u8"max_cloud_percent 限制场景级云量。lat/lon 省略时使用当前视野"
        u8"中心；本工具不会改变相机或图层可见性，结果需显式调用 "
        u8"show_science_artifact 显示。";
    start.parametersJson = "{\"type\":\"object\",\"properties\":{" 
        "\"source_id\":{\"type\":\"string\"},"
        "\"visualization_id\":{\"type\":\"string\"},"
        "\"mode\":{\"type\":\"string\",\"enum\":[\"preview\","
            "\"point_series\",\"regional_embedding\"]},"
        "\"lat\":{\"type\":\"number\"},"
        "\"lon\":{\"type\":\"number\"},"
        "\"year\":{\"type\":\"integer\"},"
        "\"first_year\":{\"type\":\"integer\"},"
        "\"last_year\":{\"type\":\"integer\"},"
        "\"baseline_year\":{\"type\":\"integer\"},"
        "\"comparison_year\":{\"type\":\"integer\"},"
        "\"time_start\":{\"type\":\"string\"},"
        "\"time_end\":{\"type\":\"string\"},"
        "\"max_cloud_percent\":{\"type\":\"number\","
            "\"minimum\":0,\"maximum\":100},"
        "\"grid_size\":{\"type\":\"integer\"},"
        "\"enable_pca\":{\"type\":\"boolean\"},"
        "\"cluster_count\":{\"type\":\"integer\"},"
        "\"research_question\":{\"type\":\"string\"},"
        "\"research_id\":{\"type\":\"string\"}}}";
    start.execute = [service, manipulator, researchManager](
        const picojson::value& args)
    {
        if (!args.is<picojson::object>())
            return errorJson("science research arguments must be an object");
        const std::vector<earthscience::ScienceSourceDescriptor> sources =
            service->listSources();
        if (sources.empty()) return errorJson("no science sources are registered");

        std::string sourceId = "alphaearth-foundations";
        if (std::none_of(
                sources.begin(), sources.end(), [](const auto& candidate)
                { return candidate.id == "alphaearth-foundations"; }))
            sourceId = sources.front().id;
        if (!optionalString(args, "source_id", sourceId))
            return errorJson("source_id must be a string");
        const auto sourceIterator = std::find_if(
            sources.begin(), sources.end(), [&sourceId](const auto& source)
            { return source.id == sourceId; });
        if (sourceIterator == sources.end())
            return errorJson("unknown science source: " + sourceId);
        const earthscience::ScienceSourceDescriptor& source = *sourceIterator;

        std::string visualizationId = source.visualizations.empty()
            ? std::string() : source.visualizations.front().id;
        if (!optionalString(args, "visualization_id", visualizationId))
            return errorJson("visualization_id must be a string");
        const earthscience::ScienceVisualizationDescriptor* visualization =
            findScienceVisualization(source, visualizationId);
        if (!visualization || visualization->id != visualizationId)
            return errorJson("unknown science visualization: " + visualizationId);

        std::string mode = "preview";
        if (!optionalString(args, "mode", mode))
            return errorJson("mode must be a string");
        if (mode != "preview" && mode != "point_series" &&
            mode != "regional_embedding")
            return errorJson("mode must be preview, point_series, or "
                             "regional_embedding");

        const osg::Vec3d targetLla =
            manipulator->computeViewPointLatLonHeight();
        const osg::Vec3d eyeLla = manipulator->computeEyeLatLonHeight();
        double latitude = osg::RadiansToDegrees(targetLla[0]);
        double longitude = osg::RadiansToDegrees(targetLla[1]);
        if (!optionalNumber(args, "lat", latitude) ||
            !optionalNumber(args, "lon", longitude))
            return errorJson("lat and lon must be numbers");
        if (!validCoordinates(latitude, longitude))
            return errorJson("lat and lon must be finite WGS84 coordinates");

        earthscience::GeoTemporalQuery query;
        if (source.id == "copernicus-dem-glo-30")
        {
            if (mode != "preview")
                return errorJson(
                    "Copernicus DEM supports preview mode only; it is a static DSM");
            for (const char* timeKey : {
                     "year", "first_year", "last_year",
                     "baseline_year", "comparison_year",
                     "time_start", "time_end", "max_cloud_percent"})
                if (args.contains(timeKey))
                    return errorJson(
                        "Copernicus DEM is the static 2021 release; omit time and cloud fields");
            query = makeCopernicusDemPreviewQuery(
                source, latitude, longitude, eyeLla[2] * 0.85);
        }
        else if (source.id == "sentinel-2-l2a")
        {
            if (mode != "preview")
                return errorJson(
                    "Sentinel-2 supports preview mode only; it is not a 64D source");
            for (const char* yearKey : {
                     "year", "first_year", "last_year",
                     "baseline_year", "comparison_year"})
                if (args.contains(yearKey))
                    return errorJson(
                        "Sentinel-2 uses time_start/time_end, not year fields");
            std::string timeStart;
            std::string timeEnd;
            if (!requiredString(args, "time_start", timeStart) ||
                !requiredString(args, "time_end", timeEnd))
                return errorJson(
                    "Sentinel-2 requires non-empty time_start and time_end");
            if (timeStart > timeEnd)
                return errorJson(
                    "Sentinel-2 time_start must not be after time_end");
            double maximumCloudPercent = 20.0;
            if (!optionalNumber(
                    args, "max_cloud_percent", maximumCloudPercent) ||
                !std::isfinite(maximumCloudPercent) ||
                maximumCloudPercent < 0.0 || maximumCloudPercent > 100.0)
                return errorJson(
                    "max_cloud_percent must be a number inside [0, 100]");
            query = makeSentinel2PreviewIntervalQuery(
                source, latitude, longitude, timeStart, timeEnd,
                maximumCloudPercent, eyeLla[2] * 0.85);
        }
        else if (mode == "preview")
        {
            int year = source.lastYear;
            if (!optionalInteger(args, "year", year))
                return errorJson("year must be an integer");
            if (!validYear(source, year))
                return errorJson("year is outside the science source range");
            query = makeSciencePointQuery(
                source, *visualization, latitude, longitude, year,
                eyeLla[2] * 0.85);
        }
        else if (mode == "point_series")
        {
            int firstYear = source.firstYear;
            int lastYear = source.lastYear;
            if (!optionalInteger(args, "first_year", firstYear) ||
                !optionalInteger(args, "last_year", lastYear))
                return errorJson("first_year and last_year must be integers");
            if (!validYear(source, firstYear) ||
                !validYear(source, lastYear) || firstYear > lastYear)
                return errorJson(
                    "point-series years must be ordered inside the source range");
            query = makeSciencePointSeriesQuery(
                source, latitude, longitude, firstYear, lastYear);
        }
        else
        {
            int baselineYear = source.firstYear;
            int comparisonYear = source.lastYear;
            int gridSize = 128;
            bool enablePca = false;
            int clusterCount = 4;
            const bool enableClustering = args.contains("cluster_count");
            if (!optionalInteger(args, "baseline_year", baselineYear) ||
                !optionalInteger(args, "comparison_year", comparisonYear))
                return errorJson(
                    "baseline_year and comparison_year must be integers");
            if (!validYear(source, baselineYear) ||
                !validYear(source, comparisonYear) ||
                baselineYear >= comparisonYear)
                return errorJson(
                    "regional years must increase inside the source range");
            if (!optionalInteger(args, "grid_size", gridSize) ||
                gridSize < 1 || gridSize > 256)
                return errorJson("grid_size must be an integer inside [1, 256]");
            if (!optionalBoolean(args, "enable_pca", enablePca))
                return errorJson("enable_pca must be a boolean");
            if (!optionalInteger(args, "cluster_count", clusterCount) ||
                (enableClustering && (clusterCount < 2 || clusterCount > 8)))
                return errorJson("cluster_count must be an integer inside [2, 8]");
            earthscience::ScienceAnalysisOptions options;
            options.gridSize = gridSize;
            options.enablePca = enablePca;
            options.enableClustering = enableClustering;
            options.clusterCount = clusterCount;
            query = makeScienceRegionalAnalysisQuery(
                source, latitude, longitude, eyeLla[2] * 0.85,
                baselineYear, comparisonYear, options);
        }

        std::string persistentId, question;
        const bool hasResearchId = args.contains("research_id");
        const bool hasQuestion = args.contains("research_question");
        if (hasResearchId &&
            !requiredString(args, "research_id", persistentId))
            return errorJson("research_id must be a non-empty string");
        if (hasQuestion &&
            !requiredString(args, "research_question", question))
            return errorJson("research_question must be a non-empty string");
        if (hasResearchId && hasQuestion)
            return errorJson(
                "use research_id to attach or research_question to create, not both");

        earthscience::ScienceResearchRecord research;
        std::string persistenceError;
        if (hasResearchId && !researchManager->get(
                persistentId, research, persistenceError))
            return errorJson("research record unavailable: " + persistenceError);
        if (hasQuestion && !researchManager->create(
                question, research, persistenceError))
            return errorJson("research record could not be created: " +
                             persistenceError);
        if (hasQuestion) persistentId = research.researchId;

        service->submit(query);
        const earthscience::ScienceJobSnapshot snapshot = service->snapshot();
        picojson::value result = snapshotJson(snapshot);
        if (!persistentId.empty())
        {
            if (!researchManager->addStep(
                    persistentId, snapshot.jobId, source.id, research,
                    persistenceError))
                return errorJson("research step could not be saved: " +
                                 persistenceError);
            result.get<picojson::object>()["research_id"] =
                picojson::value(persistentId);
            result.get<picojson::object>()["research_state"] =
                picojson::value(std::string(
                    earthscience::scienceResearchStateName(research.state)));
        }
        return result;
    };
    tools->add(start);

    earthai::Tool get;
    get.name = "get_research_job";
    get.description = u8"查询当前科学研究任务的状态、进度、来源证据和结果范围。";
    get.parametersJson = "{\"type\":\"object\",\"properties\":{" 
        "\"job_id\":{\"type\":\"integer\"},"
        "\"research_id\":{\"type\":\"string\"}}}";
    get.execute = [service, researchManager](const picojson::value& args)
    {
        const earthscience::ScienceJobSnapshot snapshot = service->snapshot();
        if (args.is<picojson::object>() && args.contains("research_id"))
        {
            std::string researchId;
            if (!requiredString(args, "research_id", researchId))
                return errorJson("research_id must be a non-empty string");
            earthscience::ScienceResearchRecord research;
            std::string persistenceError;
            if (!researchManager->get(
                    researchId, research, persistenceError))
                return errorJson("research record unavailable: " +
                                 persistenceError);
            const auto step = std::find_if(
                research.steps.begin(), research.steps.end(),
                [&snapshot](const earthscience::ScienceResearchStep& value)
                { return value.liveJobId == snapshot.jobId; });
            if (step != research.steps.end())
            {
                const std::vector<earthscience::ScienceSourceDescriptor> sources =
                    service->listSources();
                const auto source = std::find_if(
                    sources.begin(), sources.end(),
                    [&step](const earthscience::ScienceSourceDescriptor& value)
                    { return value.id == step->sourceId; });
                if (source == sources.end())
                    return errorJson(
                        "research source is no longer registered: " +
                        step->sourceId);
                if (!researchManager->observe(
                        researchId, snapshot, *source, research,
                        persistenceError))
                    return errorJson("research status could not be saved: " +
                                     persistenceError);
            }
            return researchJson(research);
        }
        double requested = static_cast<double>(snapshot.jobId);
        if (!optionalNumber(args, "job_id", requested) ||
            std::floor(requested) != requested)
            return errorJson("job_id must be an integer");
        if (static_cast<std::uint64_t>(requested) != snapshot.jobId)
            return errorJson("science job is not current");
        return snapshotJson(snapshot);
    };
    tools->add(get);

    earthai::Tool show;
    show.name = "show_science_artifact";
    show.description = u8"显示最后一次成功的科学结果。只切换图层可见性，"
        u8"不会移动或重置相机。";
    show.parametersJson = "{\"type\":\"object\",\"properties\":{" 
        "\"job_id\":{\"type\":\"integer\"},"
        "\"artifact_id\":{\"type\":\"string\"}}}";
    show.execute = [service, layer, layers](const picojson::value& args)
    {
        if (!args.is<picojson::object>())
            return errorJson("show arguments must be an object");

        const earthscience::ScienceJobSnapshot snapshot = service->snapshot();
        std::string artifactId;
        const bool explicitArtifact = args.contains("artifact_id");
        if (explicitArtifact)
        {
            if (!requiredString(args, "artifact_id", artifactId))
                return errorJson("artifact_id must be a non-empty string");
        }

        const std::shared_ptr<const earthscience::ScienceArtifact> artifact =
            explicitArtifact ? service->findArtifact(artifactId)
                             : snapshot.lastSuccessfulArtifact;
        if (!artifact)
        {
            if (explicitArtifact)
                return errorJson("unknown science artifact: " + artifactId);
            return errorJson(
                "science artifact is not ready; call get_research_job first");
        }

        double requested = static_cast<double>(snapshot.jobId);
        if (!optionalNumber(args, "job_id", requested) ||
            !std::isfinite(requested) || requested < 0.0 ||
            requested >= static_cast<double>(
                std::numeric_limits<std::uint64_t>::max()) ||
            std::floor(requested) != requested)
            return errorJson("job_id must be an integer");
        const std::uint64_t requestedId =
            static_cast<std::uint64_t>(requested);
        if (explicitArtifact && args.contains("job_id") &&
            requestedId != artifact->generation)
            return errorJson("science artifact job_id does not match artifact_id");
        if (!explicitArtifact && requestedId != snapshot.jobId &&
            requestedId != artifact->generation)
            return errorJson("science artifact job_id is not current or retained");
        if (explicitArtifact && !service->showArtifact(artifactId))
            return errorJson("unknown science artifact: " + artifactId);

        layer->setVisible(true);
        layers->setEnabled("alphaearth", true);
        const earthscience::ScienceJobSnapshot current = service->snapshot();
        picojson::object result;
        result["ok"] = picojson::value(true);
        result["visible"] = picojson::value(true);
        result["camera_changed"] = picojson::value(false);
        result["current_job_id"] = picojson::value(
            static_cast<double>(current.jobId));
        result["current_job_state"] = picojson::value(std::string(
            earthscience::scienceJobStateName(current.state)));
        result["artifact_generation"] = picojson::value(
            static_cast<double>(artifact->generation));
        result["artifact_id"] = picojson::value(artifact->artifactId);
        return picojson::value(result);
    };
    tools->add(show);

    earthai::Tool compare;
    compare.name = "compare_science_artifacts";
    compare.description = u8"比较两个已就绪且兼容的 64D 科学结果，仅返回"
        u8"有界嵌入距离摘要、证据与限制。";
    compare.parametersJson =
        "{\"type\":\"object\","
        "\"properties\":"
        "{"
        "\"left_artifact_id\":{\"type\":\"string\"},"
        "\"right_artifact_id\":{\"type\":\"string\"}},"
        "\"required\":[\"left_artifact_id\",\"right_artifact_id\"]}";
    compare.execute = [service](const picojson::value& args)
    {
        std::string leftId, rightId;
        if (!requiredString(args, "left_artifact_id", leftId) ||
            !requiredString(args, "right_artifact_id", rightId))
            return errorJson(
                "left_artifact_id and right_artifact_id are required strings");
        const std::shared_ptr<const earthscience::ScienceArtifact> left =
            service->findArtifact(leftId);
        const std::shared_ptr<const earthscience::ScienceArtifact> right =
            service->findArtifact(rightId);
        if (!left || !right)
            return errorJson("science comparison requires two ready artifacts");
        SourceEvidence leftEvidence, rightEvidence;
        if (left->query.sourceId.empty() ||
            left->query.sourceId != right->query.sourceId ||
            left->processingVersion.empty() ||
            left->processingVersion != right->processingVersion ||
            left->query.variables != std::vector<std::string>({"embedding64"}) ||
            right->query.variables != std::vector<std::string>({"embedding64"}) ||
            !uniformSourceEvidence(*left, leftEvidence) ||
            !uniformSourceEvidence(*right, rightEvidence) ||
            leftEvidence.sourceId != rightEvidence.sourceId ||
            leftEvidence.providerVersion != rightEvidence.providerVersion ||
            leftEvidence.variables != rightEvidence.variables)
            return errorJson(
                "science artifacts do not share compatible 64D evidence");

        std::array<float, earthscience::SCIENCE_EMBEDDING_COMPONENTS>
            leftDirection{}, rightDirection{};
        double leftConcentration = 0.0, rightConcentration = 0.0;
        std::string metricError;
        if (!meanDirection(*left, leftDirection, leftConcentration,
                           metricError))
            return errorJson("left " + metricError);
        if (!meanDirection(*right, rightDirection, rightConcentration,
                           metricError))
            return errorJson("right " + metricError);
        earthscience::ScienceVectorMetrics vectorMetrics;
        if (!earthscience::compareEmbeddingVectors(
                leftDirection.data(), rightDirection.data(),
                vectorMetrics, metricError))
            return errorJson("science artifact comparison failed: " + metricError);

        picojson::object result;
        result["artifact_id"] = picojson::value(
            "comparison:" + leftId + ":" + rightId);
        result["left_artifact_id"] = picojson::value(leftId);
        result["right_artifact_id"] = picojson::value(rightId);
        result["kind"] = picojson::value("artifact-comparison");
        std::vector<int> years = left->query.time.explicitYears;
        years.insert(years.end(), right->query.time.explicitYears.begin(),
                     right->query.time.explicitYears.end());
        std::sort(years.begin(), years.end());
        years.erase(std::unique(years.begin(), years.end()), years.end());
        result["years"] = picojson::value(yearsJson(years));

        picojson::object metrics;
        metrics["cosine_similarity"] = picojson::value(std::clamp(
            vectorMetrics.cosineSimilarity, -1.0, 1.0));
        metrics["cosine_distance"] = picojson::value(std::clamp(
            vectorMetrics.cosineDistance, 0.0, 2.0));
        metrics["angular_distance_radians"] = picojson::value(std::clamp(
            vectorMetrics.angularDistanceRadians, 0.0,
            3.14159265358979323846));
        result["primary_metrics"] = picojson::value(metrics);

        picojson::object coverage;
        coverage["left_fraction"] = picojson::value(
            left->embedding.coverageFraction);
        coverage["right_fraction"] = picojson::value(
            right->embedding.coverageFraction);
        result["coverage"] = picojson::value(coverage);

        picojson::object source;
        source["id"] = picojson::value(left->query.sourceId);
        source["version"] = picojson::value(sourceVersion(*left));
        result["source"] = picojson::value(source);
        picojson::object processing;
        processing["left_version"] = picojson::value(
            left->processingVersion);
        processing["right_version"] = picojson::value(
            right->processingVersion);
        processing["method"] = picojson::value(
            "aggregate mean direction and 64D cosine metrics");
        result["processing"] = picojson::value(processing);

        std::vector<std::string> warnings = left->warnings;
        warnings.insert(warnings.end(), right->warnings.begin(),
                        right->warnings.end());
        result["warnings"] = picojson::value(stringsJson(warnings));
        result["limitations"] = picojson::value(stringsJson({
            "Metrics compare aggregate embedding directions, not semantic labels.",
            "Source-compatible embedding distance does not identify causation."}));
        earthscience::ScienceProgress progress;
        progress.stage = earthscience::ScienceProgressStage::Ready;
        progress.completedUnits = 1;
        progress.totalUnits = 1;
        progress.determinate = true;
        progress.unit = "comparison";
        result["progress"] = progressJson(progress);
        return picojson::value(result);
    };
    tools->add(compare);

    earthai::Tool change;
    change.name = "run_change_analysis";
    change.description = u8"提交两个年份之间的区域 64D 变化分析。"
        u8"本工具不会显示图层或改变相机。";
    change.parametersJson =
        "{\"type\":\"object\","
        "\"properties\":"
        "{"
        "\"source_id\":{\"type\":\"string\"},"
        "\"lat\":{\"type\":\"number\"},"
        "\"lon\":{\"type\":\"number\"},"
        "\"baseline_year\":{\"type\":\"integer\"},"
        "\"comparison_year\":{\"type\":\"integer\"},"
        "\"grid_size\":{\"type\":\"integer\"}},"
        "\"required\":[\"lat\",\"lon\",\"baseline_year\","
            "\"comparison_year\"]}";
    change.execute = [service](const picojson::value& args)
    {
        const std::vector<earthscience::ScienceSourceDescriptor> sources =
            service->listSources();
        if (sources.empty()) return errorJson("no science sources are registered");
        std::string sourceId = sources.front().id;
        if (!optionalString(args, "source_id", sourceId))
            return errorJson("source_id must be a string");
        const auto found = std::find_if(
            sources.begin(), sources.end(), [&sourceId](const auto& source)
            { return source.id == sourceId; });
        if (found == sources.end())
            return errorJson("unknown science source: " + sourceId);

        double latitude = 0.0, longitude = 0.0;
        int baselineYear = 0, comparisonYear = 0, gridSize = 128;
        if (!requiredNumber(args, "lat", latitude) ||
            !requiredNumber(args, "lon", longitude))
            return errorJson("lat and lon are required finite numbers");
        if (!validCoordinates(latitude, longitude))
            return errorJson("lat and lon must be finite WGS84 coordinates");
        if (!requiredInteger(args, "baseline_year", baselineYear) ||
            !requiredInteger(args, "comparison_year", comparisonYear))
            return errorJson(
                "baseline_year and comparison_year are required integers");
        if (!validYear(*found, baselineYear) ||
            !validYear(*found, comparisonYear) ||
            baselineYear >= comparisonYear)
            return errorJson(
                "change-analysis years must increase inside the source range");
        if (!optionalInteger(args, "grid_size", gridSize) ||
            gridSize < 1 || gridSize > 256)
            return errorJson("grid_size must be an integer inside [1, 256]");

        earthscience::ScienceAnalysisOptions options;
        options.metrics = {earthscience::ScienceMetric::CosineDistance};
        options.gridSize = gridSize;
        const double span = found->capabilities.minimumSpanMeters > 0.0
            ? found->capabilities.minimumSpanMeters : 2560.0;
        const earthscience::GeoTemporalQuery query =
            makeScienceRegionalAnalysisQuery(
                *found, latitude, longitude, span,
                baselineYear, comparisonYear, options);
        service->submit(query);
        return snapshotJson(service->snapshot());
    };
    tools->add(change);

    earthai::Tool brief;
    brief.name = "build_research_brief";
    brief.description = u8"为持久化 ScienceEarth 研究生成有来源编号的三源"
        u8"科学简报；只读取紧凑证据，不显示图层、不改变相机。";
    brief.parametersJson =
        "{\"type\":\"object\",\"properties\":{"
        "\"research_id\":{\"type\":\"string\"}},"
        "\"required\":[\"research_id\"]}";
    brief.execute = [researchManager](const picojson::value& args)
    {
        std::string researchId;
        if (!requiredString(args, "research_id", researchId))
            return errorJson("research_id is required");
        earthscience::ScienceResearchRecord research;
        std::vector<earthscience::ScienceEvidenceRecord> evidence;
        std::string briefError;
        if (!researchManager->get(researchId, research, briefError))
            return errorJson("research record unavailable: " + briefError);
        if (!researchManager->loadEvidence(
                researchId, evidence, briefError))
            return errorJson("research evidence unavailable: " + briefError);
        earthscience::ScienceResearchBrief result;
        if (!earthscience::buildScienceResearchBrief(
                research, evidence, result, briefError))
            return errorJson("research brief could not be built: " + briefError);
        return briefJson(result);
    };
    tools->add(brief);
}
