#include "ScienceResearchSequencer.h"

#include "ScienceQueryService.h"
#include "ScienceResearchManager.h"

#include <picojson.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <unistd.h>

namespace earthscience
{
namespace
{
    constexpr const char* SEQUENCE_SCHEMA =
        "osgsol-science-research-sequence-v1";
    constexpr std::size_t MAX_SEQUENCE_STEPS = 16;
    constexpr std::uintmax_t MAX_SEQUENCE_DOCUMENT_BYTES = 1024u * 1024u;

    bool live(ScienceJobState state)
    {
        return state == ScienceJobState::Idle ||
            state == ScienceJobState::Queued ||
            state == ScienceJobState::Fetching;
    }

    picojson::array stringsJson(const std::vector<std::string>& values)
    {
        picojson::array result;
        for (const std::string& value : values)
            result.emplace_back(value);
        return result;
    }

    picojson::array yearsJson(const std::vector<int>& values)
    {
        picojson::array result;
        for (int value : values)
            result.emplace_back(static_cast<double>(value));
        return result;
    }

    picojson::value queryJson(const GeoTemporalQuery& query)
    {
        picojson::object geometry;
        geometry["kind"] = picojson::value(
            static_cast<double>(query.geometry.kind));
        geometry["latitude"] = picojson::value(query.geometry.point.latitude);
        geometry["longitude"] = picojson::value(query.geometry.point.longitude);
        geometry["west"] = picojson::value(query.geometry.bounds.west);
        geometry["south"] = picojson::value(query.geometry.bounds.south);
        geometry["east"] = picojson::value(query.geometry.bounds.east);
        geometry["north"] = picojson::value(query.geometry.bounds.north);
        geometry["span_m"] = picojson::value(
            query.geometry.requestedSpanMeters);

        picojson::object time;
        time["mode"] = picojson::value(static_cast<double>(query.time.mode));
        time["instant"] = picojson::value(query.time.instant);
        time["interval_start"] = picojson::value(query.time.intervalStart);
        time["interval_end"] = picojson::value(query.time.intervalEnd);
        time["years"] = picojson::value(yearsJson(query.time.explicitYears));
        time["publication_time"] =
            picojson::value(query.time.publicationTime);
        time["forecast_reference_time"] =
            picojson::value(query.time.forecastReferenceTime);

        picojson::object limits;
        limits["maximum_area_m2"] =
            picojson::value(query.limits.maximumAreaSquareMeters);
        limits["maximum_bytes"] = picojson::value(
            std::to_string(query.limits.maximumBytes));
        limits["maximum_memory_bytes"] = picojson::value(
            std::to_string(query.limits.maximumMemoryBytes));
        limits["maximum_duration_s"] =
            picojson::value(query.limits.maximumDurationSeconds);
        limits["maximum_result_cells"] = picojson::value(
            std::to_string(query.limits.maximumResultCells));
        limits["allow_upsampling"] =
            picojson::value(query.limits.allowUpsampling);

        picojson::object filters;
        filters["maximum_cloud_percent"] = picojson::value(
            query.sceneFilters.maximumCloudCoverPercent);
        filters["maximum_scenes"] = picojson::value(
            static_cast<double>(query.sceneFilters.maximumScenes));

        picojson::array metrics;
        for (ScienceMetric metric : query.analysis.metrics)
            metrics.emplace_back(static_cast<double>(metric));
        picojson::object analysis;
        analysis["kind"] = picojson::value(
            static_cast<double>(query.analysis.kind));
        analysis["metrics"] = picojson::value(metrics);
        analysis["baseline_year"] = picojson::value(
            static_cast<double>(query.analysis.baselineYear));
        analysis["comparison_year"] = picojson::value(
            static_cast<double>(query.analysis.comparisonYear));
        analysis["grid_size"] = picojson::value(
            static_cast<double>(query.analysis.gridSize));
        analysis["hotspot_quantile"] =
            picojson::value(query.analysis.hotspotQuantile);
        analysis["enable_pca"] = picojson::value(query.analysis.enablePca);
        analysis["enable_clustering"] =
            picojson::value(query.analysis.enableClustering);
        analysis["confirmed_large_request"] =
            picojson::value(query.analysis.confirmedLargeRequest);
        analysis["pca_components"] = picojson::value(
            static_cast<double>(query.analysis.pcaComponents));
        analysis["cluster_count"] = picojson::value(
            static_cast<double>(query.analysis.clusterCount));

        picojson::object value;
        value["source_id"] = picojson::value(query.sourceId);
        value["geometry"] = picojson::value(geometry);
        value["time"] = picojson::value(time);
        value["variables"] = picojson::value(stringsJson(query.variables));
        value["target_resolution_m"] =
            picojson::value(query.targetResolutionMeters);
        value["aggregation"] = picojson::value(
            static_cast<double>(query.aggregation));
        value["output_kind"] = picojson::value(
            static_cast<double>(query.outputKind));
        value["limits"] = picojson::value(limits);
        value["scene_filters"] = picojson::value(filters);
        value["purpose"] = picojson::value(query.purpose);
        value["priority"] = picojson::value(
            static_cast<double>(query.priority));
        value["visualization_id"] = picojson::value(query.visualizationId);
        value["analysis"] = picojson::value(analysis);
        return picojson::value(value);
    }

    bool field(const picojson::value& parent, const char* name,
               const picojson::value*& output, std::string& error)
    {
        if (!parent.is<picojson::object>() || !parent.contains(name))
        {
            error = std::string("sequence JSON field is missing: ") + name;
            return false;
        }
        output = &parent.get(name);
        return true;
    }

    bool stringField(const picojson::value& parent, const char* name,
                     std::string& output, std::string& error)
    {
        const picojson::value* value = nullptr;
        if (!field(parent, name, value, error) || !value->is<std::string>())
        {
            if (error.empty())
                error = std::string("sequence JSON string expected: ") + name;
            return false;
        }
        output = value->get<std::string>();
        return true;
    }

    bool numberField(const picojson::value& parent, const char* name,
                     double& output, std::string& error)
    {
        const picojson::value* value = nullptr;
        if (!field(parent, name, value, error) || !value->is<double>() ||
            !std::isfinite(value->get<double>()))
        {
            if (error.empty())
                error = std::string("sequence JSON number expected: ") + name;
            return false;
        }
        output = value->get<double>();
        return true;
    }

    bool boolField(const picojson::value& parent, const char* name,
                   bool& output, std::string& error)
    {
        const picojson::value* value = nullptr;
        if (!field(parent, name, value, error) || !value->is<bool>())
        {
            if (error.empty())
                error = std::string("sequence JSON boolean expected: ") + name;
            return false;
        }
        output = value->get<bool>();
        return true;
    }

    bool enumField(const picojson::value& parent, const char* name,
                   int minimum, int maximum, int& output,
                   std::string& error)
    {
        double value = 0.0;
        if (!numberField(parent, name, value, error) ||
            std::floor(value) != value || value < minimum || value > maximum)
        {
            if (error.empty())
                error = std::string("sequence JSON enum is invalid: ") + name;
            return false;
        }
        output = static_cast<int>(value);
        return true;
    }

    bool intField(const picojson::value& parent, const char* name,
                  int& output, std::string& error)
    {
        double value = 0.0;
        if (!numberField(parent, name, value, error) ||
            std::floor(value) != value ||
            value < static_cast<double>(std::numeric_limits<int>::min()) ||
            value > static_cast<double>(std::numeric_limits<int>::max()))
        {
            if (error.empty())
                error = std::string("sequence JSON integer is invalid: ") + name;
            return false;
        }
        output = static_cast<int>(value);
        return true;
    }

    bool uint64StringField(const picojson::value& parent, const char* name,
                           std::uint64_t& output, std::string& error)
    {
        std::string value;
        if (!stringField(parent, name, value, error) || value.empty() ||
            value.find_first_not_of("0123456789") != std::string::npos)
        {
            if (error.empty())
                error = std::string("sequence JSON uint64 is invalid: ") + name;
            return false;
        }
        try
        {
            std::size_t parsed = 0;
            const unsigned long long converted = std::stoull(value, &parsed);
            if (parsed != value.size()) throw std::invalid_argument("trailing");
            output = static_cast<std::uint64_t>(converted);
            return true;
        }
        catch (...)
        {
            error = std::string("sequence JSON uint64 is invalid: ") + name;
            return false;
        }
    }

    bool stringList(const picojson::value& parent, const char* name,
                    std::vector<std::string>& output, std::string& error)
    {
        const picojson::value* value = nullptr;
        if (!field(parent, name, value, error) ||
            !value->is<picojson::array>())
            return false;
        output.clear();
        for (const picojson::value& item : value->get<picojson::array>())
        {
            if (!item.is<std::string>())
            {
                error = std::string("sequence JSON string array is invalid: ") +
                    name;
                return false;
            }
            output.push_back(item.get<std::string>());
        }
        return true;
    }

    bool intList(const picojson::value& parent, const char* name,
                 std::vector<int>& output, std::string& error)
    {
        const picojson::value* value = nullptr;
        if (!field(parent, name, value, error) ||
            !value->is<picojson::array>())
            return false;
        output.clear();
        for (const picojson::value& item : value->get<picojson::array>())
        {
            if (!item.is<double>() || !std::isfinite(item.get<double>()) ||
                std::floor(item.get<double>()) != item.get<double>() ||
                item.get<double>() <
                    static_cast<double>(std::numeric_limits<int>::min()) ||
                item.get<double>() >
                    static_cast<double>(std::numeric_limits<int>::max()))
            {
                error = std::string("sequence JSON integer array is invalid: ") +
                    name;
                return false;
            }
            output.push_back(static_cast<int>(item.get<double>()));
        }
        return true;
    }

    bool parseQuery(const picojson::value& value, GeoTemporalQuery& query,
                    std::string& error)
    {
        query = GeoTemporalQuery();
        const picojson::value* geometry = nullptr;
        const picojson::value* time = nullptr;
        const picojson::value* limits = nullptr;
        const picojson::value* filters = nullptr;
        const picojson::value* analysis = nullptr;
        int enumValue = 0;
        if (!stringField(value, "source_id", query.sourceId, error) ||
            !field(value, "geometry", geometry, error) ||
            !field(value, "time", time, error) ||
            !stringList(value, "variables", query.variables, error) ||
            !numberField(value, "target_resolution_m",
                         query.targetResolutionMeters, error) ||
            !enumField(value, "aggregation", 0, 3, enumValue, error))
            return false;
        query.aggregation = static_cast<ScienceAggregation>(enumValue);
        if (!enumField(value, "output_kind", 0, 6, enumValue, error))
            return false;
        query.outputKind = static_cast<ScienceOutputKind>(enumValue);
        if (!field(value, "limits", limits, error) ||
            !field(value, "scene_filters", filters, error) ||
            !stringField(value, "purpose", query.purpose, error) ||
            !enumField(value, "priority", 0, 2, enumValue, error))
            return false;
        query.priority = static_cast<SciencePriority>(enumValue);
        if (!stringField(value, "visualization_id",
                         query.visualizationId, error) ||
            !field(value, "analysis", analysis, error))
            return false;

        if (!enumField(*geometry, "kind", 0, 2, enumValue, error))
            return false;
        query.geometry.kind = static_cast<ScienceGeometryKind>(enumValue);
        if (!numberField(*geometry, "latitude",
                         query.geometry.point.latitude, error) ||
            !numberField(*geometry, "longitude",
                         query.geometry.point.longitude, error) ||
            !numberField(*geometry, "west", query.geometry.bounds.west, error) ||
            !numberField(*geometry, "south", query.geometry.bounds.south, error) ||
            !numberField(*geometry, "east", query.geometry.bounds.east, error) ||
            !numberField(*geometry, "north", query.geometry.bounds.north, error) ||
            !numberField(*geometry, "span_m",
                         query.geometry.requestedSpanMeters, error))
            return false;

        if (!enumField(*time, "mode", 0, 2, enumValue, error)) return false;
        query.time.mode = static_cast<ScienceTimeMode>(enumValue);
        if (!stringField(*time, "instant", query.time.instant, error) ||
            !stringField(*time, "interval_start",
                         query.time.intervalStart, error) ||
            !stringField(*time, "interval_end",
                         query.time.intervalEnd, error) ||
            !intList(*time, "years", query.time.explicitYears, error) ||
            !stringField(*time, "publication_time",
                         query.time.publicationTime, error) ||
            !stringField(*time, "forecast_reference_time",
                         query.time.forecastReferenceTime, error))
            return false;

        if (!numberField(*limits, "maximum_area_m2",
                         query.limits.maximumAreaSquareMeters, error) ||
            !uint64StringField(*limits, "maximum_bytes",
                               query.limits.maximumBytes, error) ||
            !uint64StringField(*limits, "maximum_memory_bytes",
                               query.limits.maximumMemoryBytes, error) ||
            !numberField(*limits, "maximum_duration_s",
                         query.limits.maximumDurationSeconds, error) ||
            !uint64StringField(*limits, "maximum_result_cells",
                               query.limits.maximumResultCells, error) ||
            !boolField(*limits, "allow_upsampling",
                       query.limits.allowUpsampling, error))
            return false;

        int maximumScenes = 0;
        if (!numberField(*filters, "maximum_cloud_percent",
                         query.sceneFilters.maximumCloudCoverPercent, error) ||
            !intField(*filters, "maximum_scenes", maximumScenes, error) ||
            maximumScenes < 0)
            return false;
        query.sceneFilters.maximumScenes =
            static_cast<std::uint32_t>(maximumScenes);

        if (!enumField(*analysis, "kind", 0, 4, enumValue, error))
            return false;
        query.analysis.kind = static_cast<ScienceAnalysisKind>(enumValue);
        std::vector<int> metrics;
        if (!intList(*analysis, "metrics", metrics, error)) return false;
        query.analysis.metrics.clear();
        for (int metric : metrics)
        {
            if (metric < 0 || metric > 4)
            {
                error = "sequence JSON science metric is invalid";
                return false;
            }
            query.analysis.metrics.push_back(
                static_cast<ScienceMetric>(metric));
        }
        if (!intField(*analysis, "baseline_year",
                      query.analysis.baselineYear, error) ||
            !intField(*analysis, "comparison_year",
                      query.analysis.comparisonYear, error) ||
            !intField(*analysis, "grid_size",
                      query.analysis.gridSize, error) ||
            !numberField(*analysis, "hotspot_quantile",
                         query.analysis.hotspotQuantile, error) ||
            !boolField(*analysis, "enable_pca",
                       query.analysis.enablePca, error) ||
            !boolField(*analysis, "enable_clustering",
                       query.analysis.enableClustering, error) ||
            !boolField(*analysis, "confirmed_large_request",
                       query.analysis.confirmedLargeRequest, error) ||
            !intField(*analysis, "pca_components",
                      query.analysis.pcaComponents, error) ||
            !intField(*analysis, "cluster_count",
                      query.analysis.clusterCount, error))
            return false;
        return true;
    }

    bool atomicWrite(const std::filesystem::path& path,
                     const std::string& contents, std::string& error)
    {
        static std::atomic<std::uint64_t> sequence{0};
        const std::filesystem::path temporary = path.string() + "." +
            std::to_string(static_cast<unsigned long long>(::getpid())) + "." +
            std::to_string(sequence.fetch_add(1)) + ".tmp";
        const int descriptor = ::open(
            temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor < 0)
        {
            error = "research sequence temporary file could not be created";
            return false;
        }
        std::size_t offset = 0;
        bool succeeded = true;
        while (offset < contents.size())
        {
            const ssize_t written = ::write(
                descriptor, contents.data() + offset,
                contents.size() - offset);
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
            error = "research sequence atomic write failed";
            return false;
        }
        return true;
    }
}

class ScienceResearchSequencer::Impl
{
public:
    struct Plan
    {
        std::string researchId;
        std::vector<ScienceResearchRequestStep> steps;
        std::size_t nextIndex = 0;
        int activeIndex = -1;
        std::uint64_t activeJobId = 0;
        bool cancelled = false;
    };

    Impl(ScienceQueryService* serviceValue,
         ScienceResearchManager* managerValue)
        : service(serviceValue), manager(managerValue) {}

    std::filesystem::path path(const std::string& researchId) const
    {
        return std::filesystem::path(manager->store().root()) /
            "research" / researchId / "sequence.json";
    }

    picojson::value planJson(const Plan& plan) const
    {
        picojson::object value;
        value["schema_version"] = picojson::value(SEQUENCE_SCHEMA);
        value["research_id"] = picojson::value(plan.researchId);
        value["next_index"] = picojson::value(
            static_cast<double>(plan.nextIndex));
        value["active_index"] = picojson::value(
            static_cast<double>(plan.activeIndex));
        value["active_job_id"] =
            picojson::value(std::to_string(plan.activeJobId));
        value["cancelled"] = picojson::value(plan.cancelled);
        picojson::array steps;
        for (const ScienceResearchRequestStep& step : plan.steps)
        {
            picojson::object item;
            item["source_id"] = picojson::value(step.sourceId);
            item["query"] = queryJson(step.query);
            steps.emplace_back(item);
        }
        value["steps"] = picojson::value(steps);
        return picojson::value(value);
    }

    bool save(const Plan& plan, std::string& error) const
    {
        const std::filesystem::path document = path(plan.researchId);
        std::error_code code;
        const auto directoryStatus =
            std::filesystem::symlink_status(document.parent_path(), code);
        if (code || !std::filesystem::is_directory(directoryStatus) ||
            std::filesystem::is_symlink(directoryStatus))
        {
            error = "research sequence directory is missing or unsafe";
            return false;
        }
        return atomicWrite(
            document, planJson(plan).serialize(false), error);
    }

    bool load(const std::string& researchId, Plan& plan,
              std::string& error) const
    {
        if (!isSafeScienceRecordId(researchId))
        {
            error = "research id is unsafe";
            return false;
        }
        const std::filesystem::path document = path(researchId);
        std::error_code code;
        const auto status = std::filesystem::symlink_status(document, code);
        if (code || !std::filesystem::is_regular_file(status) ||
            std::filesystem::is_symlink(status))
        {
            error = "research sequence is not found";
            return false;
        }
        const std::uintmax_t size = std::filesystem::file_size(document, code);
        if (code || size > MAX_SEQUENCE_DOCUMENT_BYTES)
        {
            error = "research sequence document exceeds the size limit";
            return false;
        }
        std::ifstream stream(document, std::ios::binary);
        const std::string raw{
            std::istreambuf_iterator<char>(stream),
            std::istreambuf_iterator<char>()};
        if ((!stream.good() && !stream.eof()) || raw.empty())
        {
            error = "research sequence document could not be read";
            return false;
        }
        picojson::value value;
        if (!picojson::parse(value, raw).empty() ||
            !value.is<picojson::object>())
        {
            error = "research sequence document JSON is malformed";
            return false;
        }
        Plan parsed;
        std::string schema;
        double nextIndex = 0.0, activeIndex = -1.0;
        const picojson::value* steps = nullptr;
        if (!stringField(value, "schema_version", schema, error) ||
            schema != SEQUENCE_SCHEMA ||
            !stringField(value, "research_id", parsed.researchId, error) ||
            parsed.researchId != researchId ||
            !numberField(value, "next_index", nextIndex, error) ||
            !numberField(value, "active_index", activeIndex, error) ||
            std::floor(nextIndex) != nextIndex || nextIndex < 0.0 ||
            std::floor(activeIndex) != activeIndex || activeIndex < -1.0 ||
            !uint64StringField(value, "active_job_id",
                               parsed.activeJobId, error) ||
            !boolField(value, "cancelled", parsed.cancelled, error) ||
            !field(value, "steps", steps, error) ||
            !steps->is<picojson::array>())
        {
            if (error.empty()) error = "research sequence document is invalid";
            return false;
        }
        if (steps->get<picojson::array>().empty() ||
            steps->get<picojson::array>().size() > MAX_SEQUENCE_STEPS)
        {
            error = "research sequence step count is invalid";
            return false;
        }
        for (const picojson::value& item : steps->get<picojson::array>())
        {
            ScienceResearchRequestStep step;
            const picojson::value* query = nullptr;
            if (!stringField(item, "source_id", step.sourceId, error) ||
                !field(item, "query", query, error) ||
                !parseQuery(*query, step.query, error) ||
                step.sourceId.empty() ||
                step.query.sourceId != step.sourceId)
            {
                if (error.empty())
                    error = "research sequence step is invalid";
                return false;
            }
            parsed.steps.push_back(std::move(step));
        }
        if (nextIndex > static_cast<double>(parsed.steps.size()) ||
            activeIndex >= static_cast<double>(parsed.steps.size()) ||
            (activeIndex < 0.0 && parsed.activeJobId != 0))
        {
            error = "research sequence cursor is invalid";
            return false;
        }
        parsed.nextIndex = static_cast<std::size_t>(nextIndex);
        parsed.activeIndex = static_cast<int>(activeIndex);
        plan = std::move(parsed);
        error.clear();
        return true;
    }

    bool loadOnce(const std::string& researchId, std::string& error)
    {
        if (plans.find(researchId) != plans.end()) return true;
        Plan plan;
        if (!load(researchId, plan, error)) return false;
        plans.emplace(researchId, std::move(plan));
        return true;
    }

    void enqueue(const std::string& researchId)
    {
        if (std::find(queue.begin(), queue.end(), researchId) == queue.end())
            queue.push_back(researchId);
    }

    void removeQueued(const std::string& researchId)
    {
        queue.erase(std::remove(queue.begin(), queue.end(), researchId),
                    queue.end());
    }

    const ScienceSourceDescriptor* source(
        const std::string& sourceId,
        std::vector<ScienceSourceDescriptor>& sources) const
    {
        const auto found = std::find_if(
            sources.begin(), sources.end(),
            [&sourceId](const ScienceSourceDescriptor& candidate)
            { return candidate.id == sourceId; });
        return found == sources.end() ? nullptr : &*found;
    }

    bool markInterrupted(Plan& plan, const std::string& message,
                         std::string& error)
    {
        ScienceResearchRecord record;
        if (plan.activeIndex < 0 ||
            !manager->markStepTerminal(
                plan.researchId,
                static_cast<std::size_t>(plan.activeIndex),
                ScienceJobState::Failed, message, record, error))
            return false;
        plan.activeIndex = -1;
        plan.activeJobId = 0;
        activeResearchId.clear();
        return save(plan, error);
    }

    bool drive(std::string& error)
    {
        for (std::size_t transition = 0;
             transition < MAX_SEQUENCE_STEPS * 4; ++transition)
        {
            if (!activeResearchId.empty())
            {
                Plan& plan = plans.at(activeResearchId);
                if (plan.activeIndex < 0 ||
                    static_cast<std::size_t>(plan.activeIndex) >=
                        plan.steps.size())
                {
                    error = "research sequence active cursor is invalid";
                    return false;
                }
                if (plan.activeJobId == 0)
                {
                    if (!markInterrupted(
                            plan,
                            "Interrupted before provider submission completed; "
                            "the step was not retried automatically",
                            error))
                        return false;
                    continue;
                }

                const ScienceJobSnapshot snapshot = service->snapshot();
                if (snapshot.jobId != plan.activeJobId ||
                    snapshot.query.sourceId !=
                        plan.steps[static_cast<std::size_t>(
                            plan.activeIndex)].sourceId)
                {
                    if (!markInterrupted(
                            plan,
                            "Interrupted before completion; the step was not "
                            "retried automatically",
                            error))
                        return false;
                    continue;
                }

                std::vector<ScienceSourceDescriptor> sources =
                    service->listSources();
                const ScienceSourceDescriptor* descriptor = source(
                    snapshot.query.sourceId, sources);
                if (!descriptor)
                {
                    if (!markInterrupted(
                            plan,
                            "Research source is no longer registered",
                            error))
                        return false;
                    continue;
                }
                ScienceResearchRecord record;
                if (!manager->observe(
                        plan.researchId, snapshot, *descriptor,
                        record, error))
                    return false;
                if (live(snapshot.state))
                {
                    error.clear();
                    return true;
                }

                plan.activeIndex = -1;
                plan.activeJobId = 0;
                activeResearchId.clear();
                if (!save(plan, error)) return false;
                if (plan.cancelled || plan.nextIndex >= plan.steps.size())
                    removeQueued(plan.researchId);
                continue;
            }

            while (!queue.empty())
            {
                const auto found = plans.find(queue.front());
                if (found != plans.end() && !found->second.cancelled &&
                    found->second.nextIndex < found->second.steps.size())
                    break;
                queue.pop_front();
            }
            if (queue.empty())
            {
                error.clear();
                return true;
            }

            Plan& plan = plans.at(queue.front());
            const std::size_t stepIndex = plan.nextIndex;
            plan.activeIndex = static_cast<int>(stepIndex);
            plan.activeJobId = 0;
            if (!save(plan, error)) return false;

            const std::uint64_t jobId =
                service->submit(plan.steps[stepIndex].query);
            ScienceResearchRecord record;
            if (!manager->activateStep(
                    plan.researchId, stepIndex, jobId, record, error))
            {
                service->cancel(jobId);
                return false;
            }
            plan.activeJobId = jobId;
            plan.nextIndex = stepIndex + 1;
            activeResearchId = plan.researchId;
            if (!save(plan, error))
            {
                service->cancel(jobId);
                return false;
            }
        }
        error = "research sequence transition limit reached";
        return false;
    }

    ScienceQueryService* service = nullptr;
    ScienceResearchManager* manager = nullptr;
    std::map<std::string, Plan> plans;
    std::deque<std::string> queue;
    std::string activeResearchId;
    std::mutex mutex;
};

ScienceResearchSequencer::ScienceResearchSequencer(
    ScienceQueryService* service, ScienceResearchManager* manager)
    : _impl(new Impl(service, manager))
{
}

ScienceResearchSequencer::~ScienceResearchSequencer() = default;

std::string ScienceResearchSequencer::start(
    const std::string& question,
    const std::vector<ScienceResearchRequestStep>& steps,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    error.clear();
    if (!_impl->service || !_impl->manager)
    {
        error = "research sequencer dependencies are missing";
        return std::string();
    }
    if (steps.empty() || steps.size() > MAX_SEQUENCE_STEPS)
    {
        error = "research sequence requires between 1 and 16 steps";
        return std::string();
    }
    std::set<std::string> identities;
    std::vector<std::string> sourceIds;
    sourceIds.reserve(steps.size());
    for (const ScienceResearchRequestStep& step : steps)
    {
        if (step.sourceId.empty() || step.query.sourceId != step.sourceId)
        {
            error = "research step source does not match its query";
            return std::string();
        }
        std::string validationError;
        if (!_impl->service->validateQuery(step.query, validationError))
        {
            error = "invalid research step for " + step.sourceId + ": " +
                validationError;
            return std::string();
        }
        const std::string identity =
            step.sourceId + "\n" + queryJson(step.query).serialize(false);
        if (!identities.insert(identity).second)
        {
            error = "research sequence contains a duplicate source/query step";
            return std::string();
        }
        sourceIds.push_back(step.sourceId);
    }

    ScienceResearchRecord record;
    if (!_impl->manager->create(question, record, error))
        return std::string();
    Impl::Plan plan;
    plan.researchId = record.researchId;
    plan.steps = steps;
    if (!_impl->save(plan, error)) return std::string();
    if (!_impl->manager->planSteps(
            record.researchId, sourceIds, record, error))
        return std::string();
    _impl->plans.emplace(record.researchId, plan);
    _impl->enqueue(record.researchId);
    if (!_impl->drive(error)) return std::string();
    return record.researchId;
}

ScienceResearchRecord ScienceResearchSequencer::poll(
    const std::string& researchId, std::string& error)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    ScienceResearchRecord record;
    if (!_impl->service || !_impl->manager)
    {
        error = "research sequencer dependencies are missing";
        return record;
    }
    if (!_impl->loadOnce(researchId, error))
    {
        if (error == "research sequence is not found" &&
            _impl->manager->get(researchId, record, error))
        {
            const ScienceJobSnapshot snapshot = _impl->service->snapshot();
            const auto active = std::find_if(
                record.steps.begin(), record.steps.end(),
                [&snapshot](const ScienceResearchStep& step)
                { return step.liveJobId == snapshot.jobId; });
            if (active == record.steps.end()) return record;
            std::vector<ScienceSourceDescriptor> sources =
                _impl->service->listSources();
            const ScienceSourceDescriptor* descriptor =
                _impl->source(active->sourceId, sources);
            if (!descriptor)
            {
                error = "research source is no longer registered";
                return ScienceResearchRecord();
            }
            if (!_impl->manager->observe(
                    researchId, snapshot, *descriptor, record, error))
                return ScienceResearchRecord();
            return record;
        }
        return ScienceResearchRecord();
    }
    Impl::Plan& plan = _impl->plans.at(researchId);
    if (!plan.cancelled && plan.nextIndex < plan.steps.size())
        _impl->enqueue(researchId);
    if (plan.activeIndex >= 0 && _impl->activeResearchId.empty())
    {
        _impl->activeResearchId = researchId;
        _impl->removeQueued(researchId);
        _impl->queue.push_front(researchId);
    }
    if (!_impl->drive(error) ||
        !_impl->manager->get(researchId, record, error))
        return ScienceResearchRecord();
    error.clear();
    return record;
}

bool ScienceResearchSequencer::cancel(
    const std::string& researchId, std::string& error)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->service || !_impl->manager)
    {
        error = "research sequencer dependencies are missing";
        return false;
    }
    if (!_impl->loadOnce(researchId, error))
    {
        if (error != "research sequence is not found") return false;
        ScienceResearchRecord legacy;
        if (!_impl->manager->get(researchId, legacy, error)) return false;
        const ScienceJobSnapshot snapshot = _impl->service->snapshot();
        const auto active = std::find_if(
            legacy.steps.begin(), legacy.steps.end(),
            [&snapshot](const ScienceResearchStep& step)
            { return step.liveJobId == snapshot.jobId && live(step.state); });
        if (active == legacy.steps.end())
        {
            error = "research record has no cancellable live step";
            return false;
        }
        _impl->service->cancel(snapshot.jobId);
        std::vector<ScienceSourceDescriptor> sources =
            _impl->service->listSources();
        const ScienceSourceDescriptor* descriptor =
            _impl->source(active->sourceId, sources);
        if (!descriptor)
        {
            error = "research source is no longer registered";
            return false;
        }
        return _impl->manager->observe(
            researchId, _impl->service->snapshot(), *descriptor,
            legacy, error);
    }

    Impl::Plan& plan = _impl->plans.at(researchId);
    ScienceResearchRecord record;
    if (!_impl->manager->get(researchId, record, error)) return false;
    if (plan.activeIndex >= 0 &&
        _impl->activeResearchId == researchId &&
        plan.activeJobId != 0)
    {
        _impl->service->cancel(plan.activeJobId);
        const ScienceJobSnapshot snapshot = _impl->service->snapshot();
        std::vector<ScienceSourceDescriptor> sources =
            _impl->service->listSources();
        const ScienceSourceDescriptor* descriptor =
            _impl->source(plan.steps[static_cast<std::size_t>(
                plan.activeIndex)].sourceId, sources);
        if (descriptor && snapshot.jobId == plan.activeJobId)
        {
            if (!_impl->manager->observe(
                    researchId, snapshot, *descriptor, record, error))
                return false;
        }
        else if (!_impl->manager->markStepTerminal(
                     researchId,
                     static_cast<std::size_t>(plan.activeIndex),
                     ScienceJobState::Cancelled, "Cancelled",
                     record, error))
            return false;
    }

    for (std::size_t index = 0; index < record.steps.size(); ++index)
    {
        if (!live(record.steps[index].state)) continue;
        if (!_impl->manager->markStepTerminal(
                researchId, index, ScienceJobState::Cancelled,
                "Cancelled before submission", record, error))
            return false;
    }
    plan.cancelled = true;
    plan.nextIndex = plan.steps.size();
    plan.activeIndex = -1;
    plan.activeJobId = 0;
    if (_impl->activeResearchId == researchId)
        _impl->activeResearchId.clear();
    _impl->removeQueued(researchId);
    if (!_impl->save(plan, error)) return false;
    return _impl->drive(error);
}
}
