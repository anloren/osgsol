#include "ScienceResearchManager.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <utility>

namespace earthscience
{
namespace
{
    constexpr std::size_t MAX_LOADED_RECORDS = 128;

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

    std::string researchId(std::uint64_t counter)
    {
        const std::time_t now = std::time(nullptr);
        std::tm utc = {};
        gmtime_r(&now, &utc);
        char timestamp[17] = {};
        std::strftime(timestamp, sizeof(timestamp), "%Y%m%dT%H%M%SZ", &utc);
        std::ostringstream stream;
        stream << "research-" << timestamp << '-' << std::setfill('0')
               << std::setw(4) << counter;
        return stream.str();
    }

    bool live(ScienceJobState state)
    {
        return state == ScienceJobState::Idle ||
            state == ScienceJobState::Queued ||
            state == ScienceJobState::Fetching;
    }
}

ScienceResearchManager::ScienceResearchManager(std::string root)
    : _store(std::move(root))
{
}

bool ScienceResearchManager::create(
    const std::string& question, ScienceResearchRecord& output,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (question.empty() || question.size() > 8192)
    {
        error = "research question is empty or exceeds the string limit";
        return false;
    }
    if (_records.size() >= MAX_LOADED_RECORDS)
    {
        error = "loaded research record limit reached";
        return false;
    }
    ScienceResearchRecord record;
    record.question = question;
    record.createdAt = record.updatedAt = utcNow();
    for (std::size_t attempt = 0; attempt < 10000; ++attempt)
    {
        record.researchId = researchId(++_counter);
        const std::filesystem::path path = std::filesystem::path(_store.root()) /
            "research" / record.researchId / "research.json";
        std::error_code ignored;
        if (!std::filesystem::exists(path, ignored)) break;
        record.researchId.clear();
    }
    if (record.researchId.empty())
    {
        error = "research id space is exhausted";
        return false;
    }
    if (!_store.saveResearch(record, error)) return false;
    _records[record.researchId] = record;
    output = record;
    return true;
}

bool ScienceResearchManager::loadUnlocked(
    const std::string& id, ScienceResearchRecord*& record,
    std::string& error)
{
    if (!isSafeScienceRecordId(id))
    {
        error = "research id is unsafe";
        return false;
    }
    auto found = _records.find(id);
    if (found != _records.end())
    {
        record = &found->second;
        return true;
    }
    if (_records.size() >= MAX_LOADED_RECORDS)
    {
        error = "loaded research record limit reached";
        return false;
    }
    ScienceResearchRecord loadedRecord;
    if (!_store.loadResearch(id, loadedRecord, error)) return false;
    auto inserted = _records.emplace(id, std::move(loadedRecord));
    record = &inserted.first->second;
    return true;
}

bool ScienceResearchManager::get(
    const std::string& id, ScienceResearchRecord& output,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ScienceResearchRecord* record = nullptr;
    if (!loadUnlocked(id, record, error)) return false;
    output = *record;
    error.clear();
    return true;
}

bool ScienceResearchManager::persistUnlocked(
    ScienceResearchRecord& record, std::string& error)
{
    record.updatedAt = utcNow();
    return _store.saveResearch(record, error);
}

void ScienceResearchManager::updateState(ScienceResearchRecord& record)
{
    if (record.steps.empty())
    {
        record.state = ScienceResearchState::Draft;
        return;
    }
    std::size_t successes = 0;
    bool hasLive = false;
    for (const ScienceResearchStep& step : record.steps)
    {
        hasLive = hasLive || live(step.state);
        if (!step.evidenceId.empty()) ++successes;
    }
    if (hasLive) record.state = ScienceResearchState::Running;
    else if (successes == record.steps.size())
        record.state = ScienceResearchState::Ready;
    else if (successes != 0)
        record.state = ScienceResearchState::Partial;
    else
        record.state = ScienceResearchState::Failed;
}

bool ScienceResearchManager::addStep(
    const std::string& id, std::uint64_t liveJobId,
    const std::string& sourceId, ScienceResearchRecord& output,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ScienceResearchRecord* record = nullptr;
    if (!loadUnlocked(id, record, error)) return false;
    if (liveJobId == 0 || sourceId.empty())
    {
        error = "research step requires a live job id and source";
        return false;
    }
    if (record->steps.size() >= 32)
    {
        error = "research steps exceed the limit";
        return false;
    }
    if (std::any_of(record->steps.begin(), record->steps.end(),
            [liveJobId](const ScienceResearchStep& step)
            { return step.liveJobId == liveJobId; }))
    {
        error = "duplicate live job id";
        return false;
    }
    ScienceResearchStep step;
    step.liveJobId = liveJobId;
    step.sourceId = sourceId;
    step.state = ScienceJobState::Queued;
    step.message = "Queued";
    record->steps.push_back(std::move(step));
    updateState(*record);
    if (!persistUnlocked(*record, error)) return false;
    output = *record;
    return true;
}

bool ScienceResearchManager::planSteps(
    const std::string& id, const std::vector<std::string>& sourceIds,
    ScienceResearchRecord& output, std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ScienceResearchRecord* record = nullptr;
    if (!loadUnlocked(id, record, error)) return false;
    if (!record->steps.empty())
    {
        error = "research steps are already planned";
        return false;
    }
    if (sourceIds.empty() || sourceIds.size() > 16)
    {
        error = "research sequence requires between 1 and 16 steps";
        return false;
    }
    for (const std::string& sourceId : sourceIds)
    {
        if (sourceId.empty())
        {
            error = "research step source is empty";
            return false;
        }
        ScienceResearchStep step;
        step.sourceId = sourceId;
        step.state = ScienceJobState::Idle;
        step.message = "Waiting";
        record->steps.push_back(std::move(step));
    }
    updateState(*record);
    if (!persistUnlocked(*record, error)) return false;
    output = *record;
    error.clear();
    return true;
}

bool ScienceResearchManager::activateStep(
    const std::string& id, std::size_t stepIndex,
    std::uint64_t liveJobId, ScienceResearchRecord& output,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ScienceResearchRecord* record = nullptr;
    if (!loadUnlocked(id, record, error)) return false;
    if (stepIndex >= record->steps.size())
    {
        error = "research step index is out of range";
        return false;
    }
    if (liveJobId == 0)
    {
        error = "research step requires a live job id";
        return false;
    }
    if (std::any_of(record->steps.begin(), record->steps.end(),
            [liveJobId](const ScienceResearchStep& step)
            { return step.liveJobId == liveJobId; }))
    {
        error = "duplicate live job id";
        return false;
    }
    ScienceResearchStep& step = record->steps[stepIndex];
    if (step.state != ScienceJobState::Idle || step.liveJobId != 0)
    {
        error = "research step is not waiting";
        return false;
    }
    step.liveJobId = liveJobId;
    step.state = ScienceJobState::Queued;
    step.message = "Queued";
    updateState(*record);
    if (!persistUnlocked(*record, error)) return false;
    output = *record;
    error.clear();
    return true;
}

bool ScienceResearchManager::markStepTerminal(
    const std::string& id, std::size_t stepIndex, ScienceJobState state,
    const std::string& message, ScienceResearchRecord& output,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ScienceResearchRecord* record = nullptr;
    if (!loadUnlocked(id, record, error)) return false;
    if (stepIndex >= record->steps.size())
    {
        error = "research step index is out of range";
        return false;
    }
    if (state != ScienceJobState::Failed &&
        state != ScienceJobState::Cancelled &&
        state != ScienceJobState::Unavailable)
    {
        error = "research step terminal state is invalid";
        return false;
    }
    ScienceResearchStep& step = record->steps[stepIndex];
    if (step.state == ScienceJobState::Ready || !step.evidenceId.empty())
    {
        error = "successful research evidence cannot be replaced";
        return false;
    }
    step.state = state;
    step.message = message.empty() ? scienceJobStateName(state) : message;
    updateState(*record);
    if (!persistUnlocked(*record, error)) return false;
    output = *record;
    error.clear();
    return true;
}

bool ScienceResearchManager::observe(
    const std::string& id, const ScienceJobSnapshot& snapshot,
    const ScienceSourceDescriptor& source,
    ScienceResearchRecord& output, std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ScienceResearchRecord* record = nullptr;
    if (!loadUnlocked(id, record, error)) return false;
    const auto found = std::find_if(
        record->steps.begin(), record->steps.end(),
        [&snapshot](const ScienceResearchStep& step)
        { return step.liveJobId == snapshot.jobId; });
    if (found == record->steps.end())
    {
        error = "research step does not match the live job";
        return false;
    }
    if (found->sourceId != snapshot.query.sourceId ||
        source.id != found->sourceId)
    {
        error = "research step source does not match the live job";
        return false;
    }
    found->state = snapshot.state;
    found->message = snapshot.message;
    if (snapshot.state == ScienceJobState::Ready)
    {
        const std::shared_ptr<const ScienceArtifact> artifact =
            snapshot.lastSuccessfulArtifact;
        if (!artifact || artifact->generation != snapshot.jobId)
        {
            error = "ready research step has no matching artifact";
            return false;
        }
        if (!found->evidenceId.empty())
        {
            if (found->artifactId != artifact->artifactId)
            {
                error = "ready research step changed its attached artifact";
                return false;
            }
        }
        else
        {
            ScienceEvidenceRecord evidence;
            if (!makeScienceEvidence(*artifact, source, evidence, error) ||
                !_store.saveEvidence(evidence, error))
                return false;
            found->artifactId = artifact->artifactId;
            found->evidenceId = evidence.evidenceId;
        }
    }
    updateState(*record);
    if (!persistUnlocked(*record, error)) return false;
    output = *record;
    return true;
}

bool ScienceResearchManager::loadEvidence(
    const std::string& id, std::vector<ScienceEvidenceRecord>& evidence,
    std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    ScienceResearchRecord* record = nullptr;
    if (!loadUnlocked(id, record, error)) return false;
    evidence.clear();
    for (const ScienceResearchStep& step : record->steps)
    {
        if (step.evidenceId.empty()) continue;
        ScienceEvidenceRecord item;
        if (!_store.loadEvidence(step.evidenceId, item, error)) return false;
        evidence.push_back(std::move(item));
    }
    error.clear();
    return true;
}
}
