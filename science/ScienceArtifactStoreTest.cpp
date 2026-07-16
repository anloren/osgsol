#include "ScienceArtifactStore.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    std::shared_ptr<const earthscience::ScienceArtifact> makeArtifact(
        const std::string& id)
    {
        auto artifact = std::make_shared<earthscience::ScienceArtifact>();
        artifact->artifactId = id;
        return artifact;
    }

    void testEvictsOldestUnpinnedAndAccountsExactBytes()
    {
        earthscience::ScienceArtifactStore store(3, 4096);
        std::vector<std::shared_ptr<const earthscience::ScienceArtifact>> artifacts;
        std::string error;
        for (int index = 0; index < 10; ++index)
        {
            auto artifact = makeArtifact("artifact-" + std::to_string(index));
            artifacts.push_back(artifact);
            require(store.put(artifact, {}, error),
                    "bounded store rejected an in-budget artifact");
            require(error.empty(),
                    "successful bounded-store insert retained an error");
        }

        require(store.size() == 2,
                "bounded store exceeded its byte limit");
        require(!store.find("artifact-7") && store.find("artifact-8") &&
                    store.find("artifact-9"),
                "bounded store did not evict the oldest unpinned artifact");
        const std::uint64_t expectedBytes =
            earthscience::estimatedArtifactBytes(*artifacts[8]) +
            earthscience::estimatedArtifactBytes(*artifacts[9]);
        require(store.bytes() == expectedBytes,
                "bounded store byte accounting is not exact");
    }

    void testRejectsOversizedInvalidAndOverflowingArtifacts()
    {
        earthscience::ScienceArtifactStore store(3, 4096);
        std::string error;
        auto oversized = std::make_shared<earthscience::ScienceArtifact>();
        oversized->artifactId = "oversized";
        oversized->warnings.push_back(std::string(8192, 'x'));
        require(!store.put(oversized, {}, error) && !error.empty() &&
                    store.size() == 0 && store.bytes() == 0,
                "bounded store accepted an oversized artifact");

        error.clear();
        require(!store.put(nullptr, {}, error) && !error.empty(),
                "bounded store accepted a null artifact");
        auto unnamed = makeArtifact("");
        error.clear();
        require(!store.put(unnamed, {}, error) && !error.empty(),
                "bounded store accepted an unnamed artifact");

        auto overflowing = std::make_shared<earthscience::ScienceArtifact>();
        overflowing->artifactId = "overflow";
        overflowing->embedding.years =
            std::make_shared<const std::vector<int>>(2, 2025);
        overflowing->embedding.width = std::numeric_limits<int>::max();
        overflowing->embedding.height = std::numeric_limits<int>::max();
        error.clear();
        require(!store.put(overflowing, {}, error) && !error.empty() &&
                    store.size() == 0 && store.bytes() == 0,
                "bounded store accepted an overflowing byte estimate");
    }

    void testNeverEvictsPinnedArtifactsAndClearResetsAccounting()
    {
        earthscience::ScienceArtifactStore store(3, 8192);
        std::string error;
        const auto display = makeArtifact("display");
        const auto preview = makeArtifact("last-preview");
        const auto analysis = makeArtifact("last-analysis");
        require(store.put(display, {}, error) &&
                    store.put(preview, {}, error) &&
                    store.put(analysis, {}, error),
                "bounded store fixture insert failed");
        const std::uint64_t beforeBytes = store.bytes();

        const std::vector<std::string> pinned = {
            display->artifactId, preview->artifactId, analysis->artifactId};
        require(!store.put(makeArtifact("replacement"), pinned, error) &&
                    error.find("pinned") != std::string::npos,
                "bounded store evicted a pinned artifact");
        require(store.size() == 3 && store.bytes() == beforeBytes &&
                    store.find("display") && store.find("last-preview") &&
                    store.find("last-analysis") && !store.find("replacement"),
                "rejected pinned insert mutated the store");

        store.clear();
        require(store.size() == 0 && store.bytes() == 0 &&
                    !store.find("display"),
                "bounded store clear retained artifacts or byte accounting");
    }
}

int main()
{
    testEvictsOldestUnpinnedAndAccountsExactBytes();
    testRejectsOversizedInvalidAndOverflowingArtifacts();
    testNeverEvictsPinnedArtifactsAndClearResetsAccounting();
    std::cout << "[OK] ScienceEarth bounded artifact store\n";
    return 0;
}
