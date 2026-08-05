#include "ScienceProcessingStore.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace
{
void require(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "ScienceProcessingStoreTest: " << message << std::endl;
    std::exit(1);
}

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        path = std::filesystem::temp_directory_path() /
            ("osgsol-processing-store-" +
             std::to_string(static_cast<unsigned long long>(
                 std::chrono::steady_clock::now().time_since_epoch().count())));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
    std::filesystem::path path;
};
}

int main()
{
    TemporaryDirectory temporary;
    earthscience::ScienceProcessingStore store(temporary.path.string());

    earthscience::ScienceProcessingRecord record;
    record.recordId = "processing-20260805-1";
    record.liveJobId = 41;
    record.capabilityId = "science-provider/alphaearth-foundations";
    record.sourceId = "alphaearth-foundations";
    record.state = earthscience::ScienceJobState::Fetching;
    record.cost.sourceBytesUpperBound = 4096;
    record.cost.residentBytesUpperBound = 8192;
    record.cost.resultCells = 256;
    record.cost.estimatedDurationSeconds = 2.5;
    record.cost.durationDeterminate = true;
    record.progress.stage = earthscience::ScienceProgressStage::Reading;
    record.progress.completedUnits = 3;
    record.progress.totalUnits = 8;
    record.progress.determinate = true;
    record.progress.unit = "tiles";
    record.cancelRequested = true;
    record.resultArtifactId = "alphaearth-41";
    record.warnings = {"coverage is partial"};
    record.provenance.push_back({
        "alphaearth-foundations", "provider-v1", "dataset-7",
        "https://example.test/data", "Google DeepMind", "2025"});
    record.message = "Reading";
    record.createdAt = "2026-08-05T10:00:00Z";
    record.updatedAt = "2026-08-05T10:00:01Z";

    std::string error;
    require(store.save(record, error), error);
    earthscience::ScienceProcessingRecord loaded;
    require(store.load(record.recordId, loaded, error), error);
    require(loaded.liveJobId == 41 && loaded.cancelRequested,
            "job identity or cancellation was not persisted");
    require(loaded.cost.sourceBytesUpperBound == 4096 &&
                loaded.progress.completedUnits == 3,
            "cost or progress was not persisted");
    require(loaded.resultArtifactId == "alphaearth-41" &&
                loaded.warnings == record.warnings,
            "result or warnings were not persisted");
    require(loaded.provenance.size() == 1 &&
                loaded.provenance.front().datasetId == "dataset-7",
            "provenance was not persisted");

    loaded.state = earthscience::ScienceJobState::Ready;
    loaded.progress.stage = earthscience::ScienceProgressStage::Ready;
    loaded.updatedAt = "2026-08-05T10:00:02Z";
    require(store.save(loaded, error), error);
    earthscience::ScienceProcessingRecord terminal;
    require(store.load(record.recordId, terminal, error), error);
    require(terminal.state == earthscience::ScienceJobState::Ready,
            "latest task state was not recovered");

    loaded.recordId = "../escape";
    require(!store.save(loaded, error),
            "unsafe processing record id must be rejected");
    return 0;
}
