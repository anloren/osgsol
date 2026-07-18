#include "ScienceEvidenceStore.h"

#include "picojson.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#include <fcntl.h>
#include <unistd.h>

namespace earthscience
{
namespace
{
    constexpr std::uintmax_t MAX_DOCUMENT_BYTES = 1024u * 1024u;

    std::string utcNow()
    {
        const std::time_t now = std::time(nullptr);
        std::tm utc = {};
        if (!gmtime_r(&now, &utc)) return "1970-01-01T00:00:00Z";
        char value[21] = {};
        if (std::strftime(value, sizeof(value), "%Y-%m-%dT%H:%M:%SZ", &utc) == 0)
            return "1970-01-01T00:00:00Z";
        return value;
    }

    std::uint64_t fnv1a(const std::string& value)
    {
        std::uint64_t hash = 1469598103934665603ull;
        for (unsigned char byte : value)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return hash;
    }

    std::string evidenceId(const ScienceArtifact& artifact,
                           const ScienceSourceReference& reference)
    {
        const std::string identity = artifact.artifactId + "\n" +
            artifact.query.sourceId + "\n" + reference.providerVersion +
            "\n" + reference.datasetId;
        std::ostringstream stream;
        stream << "evidence-" << std::hex << std::setfill('0')
               << std::setw(16) << fnv1a(identity);
        return stream.str();
    }

    picojson::array stringsJson(const std::vector<std::string>& values)
    {
        picojson::array result;
        for (const std::string& value : values)
            result.emplace_back(value);
        return result;
    }

    picojson::object boundsJson(const ScienceWgs84Bounds& bounds)
    {
        picojson::object value;
        value["west"] = picojson::value(bounds.west);
        value["south"] = picojson::value(bounds.south);
        value["east"] = picojson::value(bounds.east);
        value["north"] = picojson::value(bounds.north);
        return value;
    }

    picojson::object geometryJson(const ScienceGeometry& geometry)
    {
        picojson::object value;
        value["kind"] = picojson::value(static_cast<double>(geometry.kind));
        value["latitude"] = picojson::value(geometry.point.latitude);
        value["longitude"] = picojson::value(geometry.point.longitude);
        value["span_meters"] =
            picojson::value(geometry.requestedSpanMeters);
        value["bounds"] = picojson::value(boundsJson(geometry.bounds));
        return value;
    }

    picojson::object scalarJson(const ScienceScalarSummary& summary)
    {
        picojson::object value;
        value["variable_id"] = picojson::value(summary.variableId);
        value["name"] = picojson::value(summary.displayName);
        value["unit"] = picojson::value(summary.unit);
        value["center_valid"] = picojson::value(summary.centerValid);
        value["center"] = picojson::value(summary.center);
        value["minimum_valid"] = picojson::value(summary.minimumValid);
        value["minimum"] = picojson::value(summary.minimum);
        value["maximum_valid"] = picojson::value(summary.maximumValid);
        value["maximum"] = picojson::value(summary.maximum);
        value["mean_valid"] = picojson::value(summary.meanValid);
        value["mean"] = picojson::value(summary.mean);
        value["valid_cells"] =
            picojson::value(static_cast<double>(summary.validCellCount));
        value["no_data_cells"] =
            picojson::value(static_cast<double>(summary.noDataCellCount));
        return value;
    }

    picojson::value evidenceJson(const ScienceEvidenceRecord& record)
    {
        picojson::object value;
        value["schema_version"] = picojson::value(record.schemaVersion);
        value["evidence_id"] = picojson::value(record.evidenceId);
        value["artifact_id"] = picojson::value(record.artifactId);
        value["source_id"] = picojson::value(record.sourceId);
        value["source_name"] = picojson::value(record.sourceName);
        value["provider_version"] = picojson::value(record.providerVersion);
        value["dataset_id"] = picojson::value(record.datasetId);
        value["original_url"] = picojson::value(record.originalUrl);
        value["attribution"] = picojson::value(record.attribution);
        value["requested_coverage"] =
            picojson::value(geometryJson(record.requestedCoverage));
        value["actual_coverage"] =
            picojson::value(boundsJson(record.actualCoverage));
        value["variables"] = picojson::value(stringsJson(record.variables));
        value["units"] = picojson::value(stringsJson(record.units));
        value["acquisition_time"] = picojson::value(record.acquisitionTime);
        value["publication_time"] = picojson::value(record.publicationTime);
        value["forecast_reference_time"] =
            picojson::value(record.forecastReferenceTime);
        picojson::array scalars;
        for (const auto& summary : record.scalarSummaries)
            scalars.emplace_back(scalarJson(summary));
        value["scalar_summaries"] = picojson::value(scalars);
        picojson::array metrics;
        for (const auto& metric : record.primaryMetrics)
        {
            picojson::object item;
            item["metric"] = picojson::value(static_cast<double>(metric.metric));
            item["baseline_year"] =
                picojson::value(static_cast<double>(metric.baselineYear));
            item["comparison_year"] =
                picojson::value(static_cast<double>(metric.comparisonYear));
            item["value"] = picojson::value(metric.value);
            item["unit"] = picojson::value(metric.unit);
            metrics.emplace_back(item);
        }
        value["primary_metrics"] = picojson::value(metrics);
        value["valid_cells"] =
            picojson::value(static_cast<double>(record.validCellCount));
        value["no_data_cells"] =
            picojson::value(static_cast<double>(record.noDataCellCount));
        value["source_resolution_m"] =
            picojson::value(record.sourceResolutionMeters);
        value["display_resolution_m"] =
            picojson::value(record.displayResolutionMeters);
        value["actual_resolution_m"] =
            picojson::value(record.actualResolutionMeters);
        value["processing_version"] =
            picojson::value(record.processingVersion);
        value["processing_steps"] =
            picojson::value(stringsJson(record.processingSteps));
        value["warnings"] = picojson::value(stringsJson(record.warnings));
        value["interpretations"] =
            picojson::value(stringsJson(record.interpretations));
        value["limitations"] =
            picojson::value(stringsJson(record.limitations));
        value["created_at"] = picojson::value(record.createdAt);
        return picojson::value(value);
    }

    picojson::value researchJson(const ScienceResearchRecord& record)
    {
        picojson::object value;
        value["schema_version"] = picojson::value(record.schemaVersion);
        value["research_id"] = picojson::value(record.researchId);
        value["question"] = picojson::value(record.question);
        value["state"] = picojson::value(std::string(
            scienceResearchStateName(record.state)));
        picojson::array steps;
        for (const ScienceResearchStep& step : record.steps)
        {
            picojson::object item;
            item["live_job_id"] =
                picojson::value(static_cast<double>(step.liveJobId));
            item["source_id"] = picojson::value(step.sourceId);
            item["state"] = picojson::value(std::string(
                scienceJobStateName(step.state)));
            item["artifact_id"] = picojson::value(step.artifactId);
            item["evidence_id"] = picojson::value(step.evidenceId);
            item["message"] = picojson::value(step.message);
            steps.emplace_back(item);
        }
        value["steps"] = picojson::value(steps);
        value["created_at"] = picojson::value(record.createdAt);
        value["updated_at"] = picojson::value(record.updatedAt);
        return picojson::value(value);
    }

    bool objectField(const picojson::value& value, const char* key,
                     const picojson::value*& output, std::string& error)
    {
        if (!value.is<picojson::object>() || !value.contains(key))
        {
            error = std::string("JSON field is missing: ") + key;
            return false;
        }
        output = &value.get(key);
        return true;
    }

    bool stringField(const picojson::value& value, const char* key,
                     std::string& output, std::string& error)
    {
        const picojson::value* field = nullptr;
        if (!objectField(value, key, field, error) ||
            !field->is<std::string>())
        {
            if (error.empty()) error = std::string("JSON string expected: ") + key;
            return false;
        }
        output = field->get<std::string>();
        return true;
    }

    bool numberField(const picojson::value& value, const char* key,
                     double& output, std::string& error)
    {
        const picojson::value* field = nullptr;
        if (!objectField(value, key, field, error) || !field->is<double>())
        {
            if (error.empty()) error = std::string("JSON number expected: ") + key;
            return false;
        }
        output = field->get<double>();
        return true;
    }

    bool boolField(const picojson::value& value, const char* key,
                   bool& output, std::string& error)
    {
        const picojson::value* field = nullptr;
        if (!objectField(value, key, field, error) || !field->is<bool>())
        {
            if (error.empty()) error = std::string("JSON boolean expected: ") + key;
            return false;
        }
        output = field->get<bool>();
        return true;
    }

    bool stringsField(const picojson::value& value, const char* key,
                      std::vector<std::string>& output, std::string& error)
    {
        const picojson::value* field = nullptr;
        if (!objectField(value, key, field, error) ||
            !field->is<picojson::array>())
        {
            if (error.empty()) error = std::string("JSON array expected: ") + key;
            return false;
        }
        output.clear();
        for (const picojson::value& item : field->get<picojson::array>())
        {
            if (!item.is<std::string>())
            {
                error = std::string("JSON string array expected: ") + key;
                return false;
            }
            output.push_back(item.get<std::string>());
        }
        return true;
    }

    bool parseBounds(const picojson::value& value,
                     ScienceWgs84Bounds& bounds, std::string& error)
    {
        return numberField(value, "west", bounds.west, error) &&
            numberField(value, "south", bounds.south, error) &&
            numberField(value, "east", bounds.east, error) &&
            numberField(value, "north", bounds.north, error);
    }

    bool parseGeometry(const picojson::value& value,
                       ScienceGeometry& geometry, std::string& error)
    {
        double kind = 0.0;
        const picojson::value* bounds = nullptr;
        if (!numberField(value, "kind", kind, error) ||
            kind < 0.0 || kind > 2.0 || std::floor(kind) != kind ||
            !numberField(value, "latitude", geometry.point.latitude, error) ||
            !numberField(value, "longitude", geometry.point.longitude, error) ||
            !numberField(value, "span_meters",
                         geometry.requestedSpanMeters, error) ||
            !objectField(value, "bounds", bounds, error) ||
            !parseBounds(*bounds, geometry.bounds, error))
            return false;
        geometry.kind = static_cast<ScienceGeometryKind>(static_cast<int>(kind));
        return true;
    }

    bool uint64Field(const picojson::value& value, const char* key,
                     std::uint64_t& output, std::string& error)
    {
        double number = 0.0;
        if (!numberField(value, key, number, error) ||
            !std::isfinite(number) || number < 0.0 ||
            std::floor(number) != number ||
            number > static_cast<double>(
                std::numeric_limits<std::uint64_t>::max()))
        {
            if (error.empty()) error = std::string("JSON uint64 expected: ") + key;
            return false;
        }
        output = static_cast<std::uint64_t>(number);
        return true;
    }

    bool parseEvidence(const picojson::value& value,
                       ScienceEvidenceRecord& record, std::string& error)
    {
        record = ScienceEvidenceRecord();
        const picojson::value* requested = nullptr;
        const picojson::value* actual = nullptr;
        const picojson::value* scalars = nullptr;
        const picojson::value* metrics = nullptr;
        if (!stringField(value, "schema_version", record.schemaVersion, error) ||
            !stringField(value, "evidence_id", record.evidenceId, error) ||
            !stringField(value, "artifact_id", record.artifactId, error) ||
            !stringField(value, "source_id", record.sourceId, error) ||
            !stringField(value, "source_name", record.sourceName, error) ||
            !stringField(value, "provider_version", record.providerVersion, error) ||
            !stringField(value, "dataset_id", record.datasetId, error) ||
            !stringField(value, "original_url", record.originalUrl, error) ||
            !stringField(value, "attribution", record.attribution, error) ||
            !objectField(value, "requested_coverage", requested, error) ||
            !parseGeometry(*requested, record.requestedCoverage, error) ||
            !objectField(value, "actual_coverage", actual, error) ||
            !parseBounds(*actual, record.actualCoverage, error) ||
            !stringsField(value, "variables", record.variables, error) ||
            !stringsField(value, "units", record.units, error) ||
            !stringField(value, "acquisition_time", record.acquisitionTime, error) ||
            !stringField(value, "publication_time", record.publicationTime, error) ||
            !stringField(value, "forecast_reference_time",
                         record.forecastReferenceTime, error) ||
            !objectField(value, "scalar_summaries", scalars, error) ||
            !scalars->is<picojson::array>() ||
            !objectField(value, "primary_metrics", metrics, error) ||
            !metrics->is<picojson::array>() ||
            !uint64Field(value, "valid_cells", record.validCellCount, error) ||
            !uint64Field(value, "no_data_cells", record.noDataCellCount, error) ||
            !numberField(value, "source_resolution_m",
                         record.sourceResolutionMeters, error) ||
            !numberField(value, "display_resolution_m",
                         record.displayResolutionMeters, error) ||
            !numberField(value, "actual_resolution_m",
                         record.actualResolutionMeters, error) ||
            !stringField(value, "processing_version",
                         record.processingVersion, error) ||
            !stringsField(value, "processing_steps", record.processingSteps, error) ||
            !stringsField(value, "warnings", record.warnings, error) ||
            !stringsField(value, "interpretations", record.interpretations, error) ||
            !stringsField(value, "limitations", record.limitations, error) ||
            !stringField(value, "created_at", record.createdAt, error))
            return false;

        for (const picojson::value& item : scalars->get<picojson::array>())
        {
            ScienceScalarSummary summary;
            if (!stringField(item, "variable_id", summary.variableId, error) ||
                !stringField(item, "name", summary.displayName, error) ||
                !stringField(item, "unit", summary.unit, error) ||
                !boolField(item, "center_valid", summary.centerValid, error) ||
                !numberField(item, "center", summary.center, error) ||
                !boolField(item, "minimum_valid", summary.minimumValid, error) ||
                !numberField(item, "minimum", summary.minimum, error) ||
                !boolField(item, "maximum_valid", summary.maximumValid, error) ||
                !numberField(item, "maximum", summary.maximum, error) ||
                !boolField(item, "mean_valid", summary.meanValid, error) ||
                !numberField(item, "mean", summary.mean, error) ||
                !uint64Field(item, "valid_cells", summary.validCellCount, error) ||
                !uint64Field(item, "no_data_cells", summary.noDataCellCount, error))
                return false;
            record.scalarSummaries.push_back(std::move(summary));
        }
        for (const picojson::value& item : metrics->get<picojson::array>())
        {
            ScienceEvidenceMetric metric;
            double metricKind = 0.0, baseline = 0.0, comparison = 0.0;
            if (!numberField(item, "metric", metricKind, error) ||
                metricKind < 0.0 || metricKind > 4.0 ||
                std::floor(metricKind) != metricKind ||
                !numberField(item, "baseline_year", baseline, error) ||
                !numberField(item, "comparison_year", comparison, error) ||
                !numberField(item, "value", metric.value, error) ||
                !stringField(item, "unit", metric.unit, error))
                return false;
            metric.metric = static_cast<ScienceMetric>(static_cast<int>(metricKind));
            metric.baselineYear = static_cast<int>(baseline);
            metric.comparisonYear = static_cast<int>(comparison);
            record.primaryMetrics.push_back(std::move(metric));
        }
        return validateScienceEvidence(record, error);
    }

    bool parseJobState(const std::string& value, ScienceJobState& state)
    {
        const ScienceJobState states[] = {
            ScienceJobState::Unavailable, ScienceJobState::Idle,
            ScienceJobState::Queued, ScienceJobState::Fetching,
            ScienceJobState::Ready, ScienceJobState::Failed,
            ScienceJobState::Cancelled};
        for (ScienceJobState candidate : states)
            if (value == scienceJobStateName(candidate))
            {
                state = candidate;
                return true;
            }
        return false;
    }

    bool parseResearch(const picojson::value& value,
                       ScienceResearchRecord& record, std::string& error)
    {
        record = ScienceResearchRecord();
        std::string state;
        const picojson::value* steps = nullptr;
        if (!stringField(value, "schema_version", record.schemaVersion, error) ||
            !stringField(value, "research_id", record.researchId, error) ||
            !stringField(value, "question", record.question, error) ||
            !stringField(value, "state", state, error) ||
            !parseScienceResearchState(state, record.state) ||
            !objectField(value, "steps", steps, error) ||
            !steps->is<picojson::array>() ||
            !stringField(value, "created_at", record.createdAt, error) ||
            !stringField(value, "updated_at", record.updatedAt, error))
        {
            if (error.empty()) error = "research JSON is invalid";
            return false;
        }
        for (const picojson::value& item : steps->get<picojson::array>())
        {
            ScienceResearchStep step;
            std::string jobState;
            if (!uint64Field(item, "live_job_id", step.liveJobId, error) ||
                !stringField(item, "source_id", step.sourceId, error) ||
                !stringField(item, "state", jobState, error) ||
                !parseJobState(jobState, step.state) ||
                !stringField(item, "artifact_id", step.artifactId, error) ||
                !stringField(item, "evidence_id", step.evidenceId, error) ||
                !stringField(item, "message", step.message, error))
            {
                if (error.empty()) error = "research step JSON is invalid";
                return false;
            }
            record.steps.push_back(std::move(step));
        }
        return validateScienceResearch(record, error);
    }

    bool ensureDirectory(const std::filesystem::path& path,
                         std::string& error)
    {
        std::error_code code;
        const auto status = std::filesystem::symlink_status(path, code);
        if (!code && std::filesystem::is_symlink(status))
        {
            error = "research storage directory is a symlink";
            return false;
        }
        code.clear();
        std::filesystem::create_directories(path, code);
        if (code || !std::filesystem::is_directory(path, code))
        {
            error = "research storage directory could not be created";
            return false;
        }
        if (std::filesystem::is_symlink(
                std::filesystem::symlink_status(path, code)))
        {
            error = "research storage directory is a symlink";
            return false;
        }
        return true;
    }

    bool atomicWrite(const std::filesystem::path& path,
                     const std::string& contents, std::string& error)
    {
        if (contents.size() > MAX_DOCUMENT_BYTES)
        {
            error = "research document exceeds the size limit";
            return false;
        }
        static std::atomic<std::uint64_t> sequence{0};
        const std::filesystem::path temporary = path.string() + "." +
            std::to_string(getpid()) + "." +
            std::to_string(sequence.fetch_add(1)) + ".tmp";
        const int descriptor = ::open(
            temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0)
        {
            error = "research temporary file could not be created";
            return false;
        }
        std::size_t offset = 0;
        bool succeeded = true;
        while (offset < contents.size())
        {
            const ssize_t written = ::write(
                descriptor, contents.data() + offset, contents.size() - offset);
            if (written <= 0)
            {
                succeeded = false;
                break;
            }
            offset += static_cast<std::size_t>(written);
        }
        if (succeeded) succeeded = ::fsync(descriptor) == 0;
        if (::close(descriptor) != 0) succeeded = false;
        if (succeeded)
            succeeded = ::rename(temporary.c_str(), path.c_str()) == 0;
        if (!succeeded)
        {
            ::unlink(temporary.c_str());
            error = "research document atomic write failed";
            return false;
        }
        return true;
    }

    bool readDocument(const std::filesystem::path& path,
                      picojson::value& document, std::string& raw,
                      std::string& error)
    {
        std::error_code code;
        const auto status = std::filesystem::symlink_status(path, code);
        if (code || !std::filesystem::is_regular_file(status) ||
            std::filesystem::is_symlink(status))
        {
            error = "research document is missing or unsafe";
            return false;
        }
        const std::uintmax_t size = std::filesystem::file_size(path, code);
        if (code || size > MAX_DOCUMENT_BYTES)
        {
            error = "research document exceeds the size limit";
            return false;
        }
        std::ifstream stream(path, std::ios::binary);
        raw.assign(std::istreambuf_iterator<char>(stream),
                   std::istreambuf_iterator<char>());
        if (!stream.good() && !stream.eof())
        {
            error = "research document could not be read";
            return false;
        }
        const std::string parseError = picojson::parse(document, raw);
        if (!parseError.empty() || !document.is<picojson::object>())
        {
            error = "research document JSON is malformed";
            return false;
        }
        return true;
    }
}

std::string defaultScienceResearchRoot()
{
    const char* home = std::getenv("HOME");
    return (home && *home ? std::string(home) : std::string(".")) +
        "/Library/Application Support/osgSol Earth/research";
}

bool makeScienceEvidence(
    const ScienceArtifact& artifact,
    const ScienceSourceDescriptor& source,
    ScienceEvidenceRecord& output,
    std::string& error)
{
    output = ScienceEvidenceRecord();
    if (artifact.artifactId.empty() || artifact.sourceReferences.empty())
    {
        error = "science artifact has no source reference";
        return false;
    }
    const ScienceSourceReference& reference = artifact.sourceReferences.front();
    output.evidenceId = evidenceId(artifact, reference);
    output.artifactId = artifact.artifactId;
    output.sourceId = artifact.query.sourceId;
    output.sourceName = source.name;
    output.providerVersion = reference.providerVersion;
    output.datasetId = reference.datasetId;
    output.originalUrl = reference.originalUrl;
    output.attribution = reference.attribution.empty()
        ? source.attribution : reference.attribution;
    output.requestedCoverage = reference.requestedCoverage;
    if (output.requestedCoverage.requestedSpanMeters == 0.0 &&
        artifact.query.geometry.requestedSpanMeters != 0.0)
        output.requestedCoverage = artifact.query.geometry;
    output.actualCoverage = reference.actualCoverage;
    output.variables = reference.variables.empty()
        ? artifact.query.variables : reference.variables;
    output.units = reference.units;
    output.acquisitionTime = reference.acquisitionTime;
    output.publicationTime = reference.publicationTime.empty()
        ? artifact.query.time.publicationTime : reference.publicationTime;
    output.forecastReferenceTime = reference.forecastReferenceTime;
    output.scalarSummaries = artifact.scalarSummaries;
    if (artifact.analysis.metrics)
        for (const ScienceMetricResult& metric : *artifact.analysis.metrics)
        {
            if (output.primaryMetrics.size() == 64) break;
            output.primaryMetrics.push_back({
                metric.metric, metric.baselineYear, metric.comparisonYear,
                metric.value, metric.unit});
        }
    for (const ScienceScalarSummary& summary : output.scalarSummaries)
    {
        output.validCellCount += summary.validCellCount;
        output.noDataCellCount += summary.noDataCellCount;
    }
    if (artifact.query.outputKind == ScienceOutputKind::RasterLayer)
    {
        output.sourceResolutionMeters = artifact.raster.sourceResolutionMeters;
        output.displayResolutionMeters = artifact.raster.displayResolutionMeters;
    }
    else if (artifact.query.outputKind == ScienceOutputKind::TimeSeries)
    {
        output.actualResolutionMeters = artifact.embedding.actualResolutionMeters;
        output.validCellCount = artifact.embedding.validCellCount;
        output.noDataCellCount = artifact.embedding.noDataCellCount;
    }
    else if (artifact.query.outputKind == ScienceOutputKind::Analysis)
    {
        output.actualResolutionMeters =
            artifact.analysis.regionalChange.actualResolutionMeters;
        output.validCellCount =
            artifact.analysis.regionalChange.validOverlapCount;
        output.noDataCellCount =
            artifact.analysis.regionalChange.noDataCellCount;
    }
    output.processingVersion = artifact.processingVersion;
    output.processingSteps = reference.processingSteps;
    if (artifact.embedding.processingSteps)
        output.processingSteps.insert(
            output.processingSteps.end(),
            artifact.embedding.processingSteps->begin(),
            artifact.embedding.processingSteps->end());
    output.warnings = artifact.warnings;
    if (artifact.embedding.warnings)
        output.warnings.insert(output.warnings.end(),
            artifact.embedding.warnings->begin(),
            artifact.embedding.warnings->end());
    if (artifact.analysis.interpretation)
        output.interpretations = *artifact.analysis.interpretation;
    if (artifact.analysis.limitations)
        output.limitations = *artifact.analysis.limitations;
    output.createdAt = artifact.createdAt.empty() ? utcNow() : artifact.createdAt;
    return validateScienceEvidence(output, error);
}

ScienceEvidenceStore::ScienceEvidenceStore(std::string root)
    : _root(std::move(root))
{
}

bool ScienceEvidenceStore::saveEvidence(
    const ScienceEvidenceRecord& record, std::string& error) const
{
    if (!validateScienceEvidence(record, error)) return false;
    const std::filesystem::path directory =
        std::filesystem::path(_root) / "evidence";
    if (!ensureDirectory(directory, error)) return false;
    const std::filesystem::path path = directory / (record.evidenceId + ".json");
    std::error_code code;
    if (std::filesystem::exists(path, code))
    {
        ScienceEvidenceRecord existing;
        if (!loadEvidence(record.evidenceId, existing, error)) return false;
        if (evidenceJson(existing).serialize(false) !=
            evidenceJson(record).serialize(false))
        {
            error = "duplicate evidence id has incompatible content";
            return false;
        }
        return true;
    }
    return atomicWrite(path, evidenceJson(record).serialize(false), error);
}

bool ScienceEvidenceStore::loadEvidence(
    const std::string& id, ScienceEvidenceRecord& record,
    std::string& error) const
{
    if (!isSafeScienceRecordId(id))
    {
        error = "evidence id is unsafe";
        return false;
    }
    const std::filesystem::path directory =
        std::filesystem::path(_root) / "evidence";
    std::error_code code;
    if (std::filesystem::is_symlink(
            std::filesystem::symlink_status(directory, code)))
    {
        error = "research storage directory is a symlink";
        return false;
    }
    picojson::value document;
    std::string raw;
    if (!readDocument(directory / (id + ".json"),
                      document, raw, error)) return false;
    return parseEvidence(document, record, error);
}

bool ScienceEvidenceStore::saveResearch(
    const ScienceResearchRecord& record, std::string& error) const
{
    if (!validateScienceResearch(record, error)) return false;
    const std::filesystem::path directory =
        std::filesystem::path(_root) / "research" / record.researchId;
    if (!ensureDirectory(directory, error)) return false;
    return atomicWrite(directory / "research.json",
                       researchJson(record).serialize(false), error);
}

bool ScienceEvidenceStore::loadResearch(
    const std::string& id, ScienceResearchRecord& record,
    std::string& error) const
{
    if (!isSafeScienceRecordId(id))
    {
        error = "research id is unsafe";
        return false;
    }
    const std::filesystem::path directory =
        std::filesystem::path(_root) / "research" / id;
    std::error_code code;
    if (std::filesystem::is_symlink(
            std::filesystem::symlink_status(directory, code)))
    {
        error = "research storage directory is a symlink";
        return false;
    }
    picojson::value document;
    std::string raw;
    if (!readDocument(directory / "research.json",
                      document, raw, error)) return false;
    return parseResearch(document, record, error);
}
}
