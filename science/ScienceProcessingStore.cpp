#include "ScienceProcessingStore.h"

#include <picojson.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

namespace earthscience
{
namespace
{
    constexpr std::uintmax_t MAX_DOCUMENT_BYTES = 1024u * 1024u;

    picojson::value unsignedValue(std::uint64_t value)
    {
        return picojson::value(std::to_string(value));
    }

    picojson::array stringArray(const std::vector<std::string>& values)
    {
        picojson::array output;
        for (const std::string& value : values)
            output.emplace_back(value);
        return output;
    }

    picojson::object costObject(const ScienceQueryCost& cost)
    {
        picojson::object output;
        output["sourceBytesUpperBound"] =
            unsignedValue(cost.sourceBytesUpperBound);
        output["residentBytesUpperBound"] =
            unsignedValue(cost.residentBytesUpperBound);
        output["resultCells"] = unsignedValue(cost.resultCells);
        output["estimatedDurationSeconds"] =
            picojson::value(cost.estimatedDurationSeconds);
        output["durationDeterminate"] =
            picojson::value(cost.durationDeterminate);
        output["requiresConfirmation"] =
            picojson::value(cost.requiresConfirmation);
        return output;
    }

    picojson::object progressObject(const ScienceProgress& progress)
    {
        picojson::object output;
        output["stage"] = picojson::value(
            scienceProgressStageName(progress.stage));
        output["completedUnits"] = unsignedValue(progress.completedUnits);
        output["totalUnits"] = unsignedValue(progress.totalUnits);
        output["determinate"] = picojson::value(progress.determinate);
        output["unit"] = picojson::value(progress.unit);
        output["elapsedSeconds"] = picojson::value(progress.elapsedSeconds);
        return output;
    }

    picojson::object provenanceObject(
        const ScienceProcessingProvenance& provenance)
    {
        picojson::object output;
        output["sourceId"] = picojson::value(provenance.sourceId);
        output["providerVersion"] = picojson::value(
            provenance.providerVersion);
        output["datasetId"] = picojson::value(provenance.datasetId);
        output["originalUrl"] = picojson::value(provenance.originalUrl);
        output["attribution"] = picojson::value(provenance.attribution);
        output["acquisitionTime"] = picojson::value(
            provenance.acquisitionTime);
        return output;
    }

    picojson::value recordJson(const ScienceProcessingRecord& record)
    {
        picojson::object output;
        output["schemaVersion"] = picojson::value(record.schemaVersion);
        output["recordId"] = picojson::value(record.recordId);
        output["liveJobId"] = unsignedValue(record.liveJobId);
        output["capabilityId"] = picojson::value(record.capabilityId);
        output["sourceId"] = picojson::value(record.sourceId);
        output["state"] = picojson::value(scienceJobStateName(record.state));
        output["cost"] = picojson::value(costObject(record.cost));
        output["progress"] = picojson::value(progressObject(record.progress));
        output["cancelRequested"] = picojson::value(record.cancelRequested);
        output["resultArtifactId"] = picojson::value(record.resultArtifactId);
        output["warnings"] = picojson::value(stringArray(record.warnings));
        picojson::array provenance;
        for (const ScienceProcessingProvenance& source : record.provenance)
            provenance.emplace_back(provenanceObject(source));
        output["provenance"] = picojson::value(provenance);
        output["message"] = picojson::value(record.message);
        output["createdAt"] = picojson::value(record.createdAt);
        output["updatedAt"] = picojson::value(record.updatedAt);
        return picojson::value(output);
    }

    bool stringField(const picojson::object& object, const char* key,
                     std::string& value)
    {
        const auto found = object.find(key);
        if (found == object.end() || !found->second.is<std::string>())
            return false;
        value = found->second.get<std::string>();
        return true;
    }

    bool boolField(const picojson::object& object, const char* key,
                   bool& value)
    {
        const auto found = object.find(key);
        if (found == object.end() || !found->second.is<bool>()) return false;
        value = found->second.get<bool>();
        return true;
    }

    bool numberField(const picojson::object& object, const char* key,
                     double& value)
    {
        const auto found = object.find(key);
        if (found == object.end() || !found->second.is<double>()) return false;
        value = found->second.get<double>();
        return true;
    }

    bool unsignedField(const picojson::object& object, const char* key,
                       std::uint64_t& value)
    {
        std::string encoded;
        if (!stringField(object, key, encoded) || encoded.empty()) return false;
        std::uint64_t parsed = 0;
        for (char character : encoded)
        {
            if (character < '0' || character > '9') return false;
            const std::uint64_t digit =
                static_cast<std::uint64_t>(character - '0');
            if (parsed > (std::numeric_limits<std::uint64_t>::max() - digit) /
                    10u)
                return false;
            parsed = parsed * 10u + digit;
        }
        value = parsed;
        return true;
    }

    bool jobState(const std::string& value, ScienceJobState& state)
    {
        const ScienceJobState values[] = {
            ScienceJobState::Unavailable, ScienceJobState::Idle,
            ScienceJobState::Queued, ScienceJobState::Fetching,
            ScienceJobState::Ready, ScienceJobState::Failed,
            ScienceJobState::Cancelled};
        for (ScienceJobState candidate : values)
        {
            if (value == scienceJobStateName(candidate))
            {
                state = candidate;
                return true;
            }
        }
        return false;
    }

    bool progressStage(const std::string& value, ScienceProgressStage& stage)
    {
        const ScienceProgressStage values[] = {
            ScienceProgressStage::Idle, ScienceProgressStage::Queued,
            ScienceProgressStage::Locating, ScienceProgressStage::Reading,
            ScienceProgressStage::Decoding, ScienceProgressStage::Validating,
            ScienceProgressStage::Aligning, ScienceProgressStage::Analyzing,
            ScienceProgressStage::Materializing,
            ScienceProgressStage::Cancelling, ScienceProgressStage::Ready,
            ScienceProgressStage::Failed, ScienceProgressStage::Cancelled};
        for (ScienceProgressStage candidate : values)
        {
            if (value == scienceProgressStageName(candidate))
            {
                stage = candidate;
                return true;
            }
        }
        return false;
    }

    bool parseStringArray(const picojson::value& value,
                          std::vector<std::string>& output)
    {
        if (!value.is<picojson::array>()) return false;
        output.clear();
        for (const picojson::value& item : value.get<picojson::array>())
        {
            if (!item.is<std::string>()) return false;
            output.push_back(item.get<std::string>());
        }
        return true;
    }

    bool parseRecord(const picojson::value& document,
                     ScienceProcessingRecord& record, std::string& error)
    {
        if (!document.is<picojson::object>())
        {
            error = "processing document is not an object";
            return false;
        }
        const picojson::object& object = document.get<picojson::object>();
        ScienceProcessingRecord parsed;
        std::string state, stage;
        if (!stringField(object, "schemaVersion", parsed.schemaVersion) ||
            !stringField(object, "recordId", parsed.recordId) ||
            !unsignedField(object, "liveJobId", parsed.liveJobId) ||
            !stringField(object, "capabilityId", parsed.capabilityId) ||
            !stringField(object, "sourceId", parsed.sourceId) ||
            !stringField(object, "state", state) || !jobState(state, parsed.state) ||
            !boolField(object, "cancelRequested", parsed.cancelRequested) ||
            !stringField(object, "resultArtifactId", parsed.resultArtifactId) ||
            !stringField(object, "message", parsed.message) ||
            !stringField(object, "createdAt", parsed.createdAt) ||
            !stringField(object, "updatedAt", parsed.updatedAt))
        {
            error = "processing document fields are invalid";
            return false;
        }
        const auto cost = object.find("cost");
        const auto progress = object.find("progress");
        const auto warnings = object.find("warnings");
        const auto provenance = object.find("provenance");
        if (cost == object.end() || !cost->second.is<picojson::object>() ||
            progress == object.end() ||
                !progress->second.is<picojson::object>() ||
            warnings == object.end() || provenance == object.end() ||
            !parseStringArray(warnings->second, parsed.warnings) ||
            !provenance->second.is<picojson::array>())
        {
            error = "processing document compound fields are invalid";
            return false;
        }
        const picojson::object& costValue = cost->second.get<picojson::object>();
        if (!unsignedField(costValue, "sourceBytesUpperBound",
                           parsed.cost.sourceBytesUpperBound) ||
            !unsignedField(costValue, "residentBytesUpperBound",
                           parsed.cost.residentBytesUpperBound) ||
            !unsignedField(costValue, "resultCells", parsed.cost.resultCells) ||
            !numberField(costValue, "estimatedDurationSeconds",
                         parsed.cost.estimatedDurationSeconds) ||
            !boolField(costValue, "durationDeterminate",
                       parsed.cost.durationDeterminate) ||
            !boolField(costValue, "requiresConfirmation",
                       parsed.cost.requiresConfirmation))
        {
            error = "processing cost is invalid";
            return false;
        }
        const picojson::object& progressValue =
            progress->second.get<picojson::object>();
        if (!stringField(progressValue, "stage", stage) ||
            !progressStage(stage, parsed.progress.stage) ||
            !unsignedField(progressValue, "completedUnits",
                           parsed.progress.completedUnits) ||
            !unsignedField(progressValue, "totalUnits",
                           parsed.progress.totalUnits) ||
            !boolField(progressValue, "determinate",
                       parsed.progress.determinate) ||
            !stringField(progressValue, "unit", parsed.progress.unit) ||
            !numberField(progressValue, "elapsedSeconds",
                         parsed.progress.elapsedSeconds))
        {
            error = "processing progress is invalid";
            return false;
        }
        for (const picojson::value& value :
             provenance->second.get<picojson::array>())
        {
            if (!value.is<picojson::object>())
            {
                error = "processing provenance entry is invalid";
                return false;
            }
            const picojson::object& item = value.get<picojson::object>();
            ScienceProcessingProvenance source;
            if (!stringField(item, "sourceId", source.sourceId) ||
                !stringField(item, "providerVersion", source.providerVersion) ||
                !stringField(item, "datasetId", source.datasetId) ||
                !stringField(item, "originalUrl", source.originalUrl) ||
                !stringField(item, "attribution", source.attribution) ||
                !stringField(item, "acquisitionTime", source.acquisitionTime))
            {
                error = "processing provenance fields are invalid";
                return false;
            }
            parsed.provenance.push_back(std::move(source));
        }
        if (!validateScienceProcessingRecord(parsed, error)) return false;
        record = std::move(parsed);
        error.clear();
        return true;
    }

    bool safeDirectory(const std::filesystem::path& root,
                       std::filesystem::path& directory,
                       std::string& error)
    {
        std::error_code code;
        if (std::filesystem::is_symlink(
                std::filesystem::symlink_status(root, code)))
        {
            error = "processing storage root is a symlink";
            return false;
        }
        std::filesystem::create_directories(root, code);
        if (code || !std::filesystem::is_directory(root, code))
        {
            error = "processing storage root is unavailable";
            return false;
        }
        directory = root / "processing";
        if (std::filesystem::is_symlink(
                std::filesystem::symlink_status(directory, code)))
        {
            error = "processing storage directory is a symlink";
            return false;
        }
        std::filesystem::create_directories(directory, code);
        if (code || !std::filesystem::is_directory(directory, code))
        {
            error = "processing storage directory is unavailable";
            return false;
        }
        return true;
    }
}

std::string scienceProcessingUtcNow()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc = {};
#if defined(_WIN32)
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

ScienceProcessingStore::ScienceProcessingStore(std::string root)
    : _root(std::move(root))
{
}

bool ScienceProcessingStore::prepare(std::string& error) const
{
    std::filesystem::path directory;
    return safeDirectory(std::filesystem::path(_root), directory, error);
}

bool ScienceProcessingStore::save(
    const ScienceProcessingRecord& record, std::string& error) const
{
    if (!validateScienceProcessingRecord(record, error)) return false;
    std::filesystem::path directory;
    if (!safeDirectory(std::filesystem::path(_root), directory, error))
        return false;
    const std::filesystem::path path = directory / (record.recordId + ".json");
    const std::filesystem::path temporary = directory /
        (record.recordId + ".tmp");
    std::error_code code;
    if (std::filesystem::is_symlink(
            std::filesystem::symlink_status(path, code)))
    {
        error = "processing record path is a symlink";
        return false;
    }
    const std::string contents = recordJson(record).serialize(false);
    if (contents.size() > MAX_DOCUMENT_BYTES)
    {
        error = "processing record exceeds the size limit";
        return false;
    }
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        stream.flush();
        if (!stream.good())
        {
            error = "processing record could not be written";
            std::filesystem::remove(temporary, code);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, code);
    if (code)
    {
        std::filesystem::remove(path, code);
        code.clear();
        std::filesystem::rename(temporary, path, code);
    }
    if (code)
    {
        error = "processing record could not be committed";
        std::filesystem::remove(temporary, code);
        return false;
    }
    error.clear();
    return true;
}

bool ScienceProcessingStore::load(
    const std::string& recordId, ScienceProcessingRecord& record,
    std::string& error) const
{
    if (!isSafeScienceProcessingRecordId(recordId))
    {
        error = "processing record id is unsafe";
        return false;
    }
    std::filesystem::path directory;
    if (!safeDirectory(std::filesystem::path(_root), directory, error))
        return false;
    const std::filesystem::path path = directory / (recordId + ".json");
    std::error_code code;
    if (std::filesystem::is_symlink(
            std::filesystem::symlink_status(path, code)) ||
        !std::filesystem::is_regular_file(path, code))
    {
        error = "processing record is missing or unsafe";
        return false;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code || size > MAX_DOCUMENT_BYTES)
    {
        error = "processing record exceeds the size limit";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    const std::string contents((std::istreambuf_iterator<char>(stream)),
                               std::istreambuf_iterator<char>());
    if (!stream.good() && !stream.eof())
    {
        error = "processing record could not be read";
        return false;
    }
    picojson::value document;
    const std::string parseError = picojson::parse(document, contents);
    if (!parseError.empty())
    {
        error = "processing record JSON is malformed";
        return false;
    }
    return parseRecord(document, record, error);
}
}
