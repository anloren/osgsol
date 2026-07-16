#ifndef OSGSOL_SCIENCE_ARTIFACT_STORE_H
#define OSGSOL_SCIENCE_ARTIFACT_STORE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "ScienceQueryTypes.h"

namespace earthscience
{
    class ScienceArtifactStore
    {
    public:
        explicit ScienceArtifactStore(
            std::size_t maxArtifacts = 8,
            std::uint64_t maxBytes = 256ull * 1024ull * 1024ull);

        bool put(std::shared_ptr<const ScienceArtifact> artifact,
                 const std::vector<std::string>& pinnedIds,
                 std::string& error);
        std::shared_ptr<const ScienceArtifact> find(
            const std::string& id) const;
        void clear();

        std::size_t size() const { return _entries.size(); }
        std::uint64_t bytes() const { return _bytes; }

    private:
        struct Entry
        {
            std::shared_ptr<const ScienceArtifact> artifact;
            std::uint64_t bytes = 0;
        };

        std::size_t _maxArtifacts = 0;
        std::uint64_t _maxBytes = 0;
        std::uint64_t _bytes = 0;
        std::vector<Entry> _entries;
    };
}

#endif
