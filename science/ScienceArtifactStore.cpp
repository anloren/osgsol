#include "ScienceArtifactStore.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace earthscience
{
ScienceArtifactStore::ScienceArtifactStore(
    std::size_t maxArtifacts, std::uint64_t maxBytes)
    : _maxArtifacts(maxArtifacts), _maxBytes(maxBytes)
{
}

bool ScienceArtifactStore::put(
    std::shared_ptr<const ScienceArtifact> artifact,
    const std::vector<std::string>& pinnedIds, std::string& error)
{
    error.clear();
    if (!artifact)
    {
        error = "science artifact is null";
        return false;
    }
    if (artifact->artifactId.empty())
    {
        error = "science artifact id is empty";
        return false;
    }

    std::uint64_t artifactBytes = 0;
    try
    {
        artifactBytes = estimatedArtifactBytes(*artifact);
    }
    catch (const std::exception& exception)
    {
        error = "science artifact byte estimate failed: " +
            std::string(exception.what());
        return false;
    }
    catch (...)
    {
        error = "science artifact byte estimate failed";
        return false;
    }
    if (_maxArtifacts == 0 || artifactBytes > _maxBytes)
    {
        error = "science artifact exceeds store capacity";
        return false;
    }

    std::vector<Entry> candidates = _entries;
    std::uint64_t candidateBytes = _bytes;
    const auto existing = std::find_if(
        candidates.begin(), candidates.end(), [&artifact](const Entry& entry)
        {
            return entry.artifact->artifactId == artifact->artifactId;
        });
    if (existing != candidates.end())
    {
        candidateBytes -= existing->bytes;
        candidates.erase(existing);
    }

    const auto isPinned = [&pinnedIds](const Entry& entry)
    {
        return std::find(pinnedIds.begin(), pinnedIds.end(),
                         entry.artifact->artifactId) != pinnedIds.end();
    };
    const auto lacksCapacity = [&]()
    {
        return candidates.size() >= _maxArtifacts ||
               candidateBytes > _maxBytes - artifactBytes;
    };
    while (lacksCapacity())
    {
        const auto victim = std::find_if(
            candidates.begin(), candidates.end(),
            [&isPinned](const Entry& entry) { return !isPinned(entry); });
        if (victim == candidates.end())
        {
            error = "science artifact store capacity is pinned";
            return false;
        }
        candidateBytes -= victim->bytes;
        candidates.erase(victim);
    }

    candidateBytes += artifactBytes;
    candidates.push_back(Entry{std::move(artifact), artifactBytes});
    _entries = std::move(candidates);
    _bytes = candidateBytes;
    return true;
}

std::shared_ptr<const ScienceArtifact> ScienceArtifactStore::find(
    const std::string& id) const
{
    const auto found = std::find_if(
        _entries.begin(), _entries.end(), [&id](const Entry& entry)
        {
            return entry.artifact->artifactId == id;
        });
    return found == _entries.end() ? nullptr : found->artifact;
}

void ScienceArtifactStore::clear()
{
    _entries.clear();
    _bytes = 0;
}
}
