#include "ScienceResearchManager.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    class TempDirectory
    {
    public:
        TempDirectory()
        {
            std::string pattern =
                (std::filesystem::temp_directory_path() /
                 "osgsol-research-manager-XXXXXX").string();
            std::vector<char> writable(pattern.begin(), pattern.end());
            writable.push_back('\0');
            _path = mkdtemp(writable.data());
            require(!_path.empty(), "temporary directory creation failed");
        }
        ~TempDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(_path, ignored);
        }
        const std::string& path() const { return _path; }
    private:
        std::string _path;
    };

    earthscience::ScienceSourceDescriptor source(const std::string& id)
    {
        earthscience::ScienceSourceDescriptor value;
        value.id = id;
        value.name = id == "sentinel-2-l2a"
            ? "Sentinel-2 Level-2A" : "Copernicus DEM GLO-30";
        value.providerVersion = id == "sentinel-2-l2a"
            ? "earth-search-v1" : "aws-glo30-2021";
        value.attribution = "official test attribution";
        return value;
    }

    std::shared_ptr<earthscience::ScienceArtifact> artifact(
        const std::string& id, const std::string& sourceId,
        std::uint64_t generation)
    {
        auto value = std::make_shared<earthscience::ScienceArtifact>();
        value->artifactId = id;
        value->generation = generation;
        value->query.sourceId = sourceId;
        value->query.geometry.kind = earthscience::ScienceGeometryKind::Point;
        value->query.geometry.point = {35.68, 139.76};
        value->query.geometry.requestedSpanMeters = 10000.0;
        value->query.variables = sourceId == "sentinel-2-l2a"
            ? std::vector<std::string>{"visual"}
            : std::vector<std::string>{"surface_elevation"};
        value->raster.bounds = {139.70, 35.60, 139.82, 35.76};
        value->raster.sourceResolutionMeters =
            sourceId == "sentinel-2-l2a" ? 10.0 : 30.0;
        value->raster.displayResolutionMeters = 39.1;
        earthscience::ScienceSourceReference reference;
        reference.sourceId = sourceId;
        reference.providerVersion = source(sourceId).providerVersion;
        reference.datasetId = id + "-dataset";
        reference.originalUrl = "https://example.invalid/" + id;
        reference.requestedCoverage = value->query.geometry;
        reference.actualCoverage = value->raster.bounds;
        reference.variables = value->query.variables;
        reference.units = sourceId == "sentinel-2-l2a"
            ? std::vector<std::string>{"display DN"}
            : std::vector<std::string>{"m"};
        reference.attribution = source(sourceId).attribution;
        reference.processingSteps = {"bounded source read"};
        value->sourceReferences.push_back(reference);
        value->processingVersion = "test-processing-v1";
        value->createdAt = "2026-07-19T12:00:00Z";
        return value;
    }

    earthscience::ScienceJobSnapshot snapshot(
        std::uint64_t jobId, const std::string& sourceId,
        earthscience::ScienceJobState state,
        std::shared_ptr<earthscience::ScienceArtifact> result = nullptr)
    {
        earthscience::ScienceJobSnapshot value;
        value.jobId = jobId;
        value.query.sourceId = sourceId;
        value.state = state;
        value.message = earthscience::scienceJobStateName(state);
        if (result)
        {
            value.lastSuccessfulArtifact = result;
            value.lastSuccessfulPreviewArtifact = result;
        }
        return value;
    }
}

int main()
{
    TempDirectory temporary;
    earthscience::ScienceResearchManager manager(temporary.path());
    std::string error;
    earthscience::ScienceResearchRecord record;
    require(manager.create(
                "Compare visible imagery and static surface elevation",
                record, error) &&
                record.researchId.rfind("research-", 0) == 0 &&
                record.state == earthscience::ScienceResearchState::Draft,
            "stable research creation failed");
    const std::string researchId = record.researchId;

    require(manager.addStep(
                researchId, 11, "sentinel-2-l2a", record, error) &&
                record.state == earthscience::ScienceResearchState::Running &&
                record.steps.size() == 1,
            "live research step registration failed");
    require(manager.observe(
                researchId,
                snapshot(11, "sentinel-2-l2a",
                         earthscience::ScienceJobState::Fetching),
                source("sentinel-2-l2a"), record, error) &&
                record.state == earthscience::ScienceResearchState::Running,
            "live research polling failed");

    require(manager.addStep(
                researchId, 12, "copernicus-dem-glo-30", record, error),
            "second source step registration failed");
    auto sentinelArtifact = artifact("sentinel-artifact", "sentinel-2-l2a", 11);
    require(manager.observe(
                researchId,
                snapshot(11, "sentinel-2-l2a",
                         earthscience::ScienceJobState::Ready,
                         sentinelArtifact),
                source("sentinel-2-l2a"), record, error) &&
                !record.steps.front().evidenceId.empty() &&
                record.state == earthscience::ScienceResearchState::Running,
            "ready artifact was not persisted while another step ran");
    const std::string sentinelEvidence = record.steps.front().evidenceId;
    sentinelArtifact->createdAt.clear();
    require(manager.observe(
                researchId,
                snapshot(11, "sentinel-2-l2a",
                         earthscience::ScienceJobState::Ready,
                         sentinelArtifact),
                source("sentinel-2-l2a"), record, error) &&
                record.steps.front().evidenceId == sentinelEvidence,
            "duplicate poll was not idempotent");

    require(manager.observe(
                researchId,
                snapshot(12, "copernicus-dem-glo-30",
                         earthscience::ScienceJobState::Failed),
                source("copernicus-dem-glo-30"), record, error) &&
                record.state == earthscience::ScienceResearchState::Partial,
            "one failed source did not produce partial research");

    std::vector<earthscience::ScienceEvidenceRecord> evidence;
    require(manager.loadEvidence(researchId, evidence, error) &&
                evidence.size() == 1 &&
                evidence.front().evidenceId == sentinelEvidence,
            "manager did not load durable evidence");

    earthscience::ScienceResearchManager restarted(temporary.path());
    earthscience::ScienceResearchRecord restartedRecord;
    require(restarted.get(researchId, restartedRecord, error) &&
                restartedRecord.state ==
                    earthscience::ScienceResearchState::Partial &&
                restartedRecord.steps.size() == 2 &&
                restartedRecord.steps.front().evidenceId == sentinelEvidence,
            "research record did not survive restart");

    earthscience::ScienceResearchRecord failed;
    require(restarted.create("Unavailable evidence", failed, error) &&
                restarted.addStep(
                    failed.researchId, 21, "sentinel-2-l2a", failed, error) &&
                restarted.observe(
                    failed.researchId,
                    snapshot(21, "sentinel-2-l2a",
                             earthscience::ScienceJobState::Cancelled),
                    source("sentinel-2-l2a"), failed, error) &&
                failed.state == earthscience::ScienceResearchState::Failed,
            "all-cancelled research did not become failed");
    require(!restarted.addStep(
                researchId, 11, "sentinel-2-l2a", record, error) &&
                error.find("duplicate") != std::string::npos,
            "duplicate live job id was accepted");
    require(!restarted.get("../escape", record, error),
            "unsafe research id was accepted");

    std::cout << "[OK] ScienceEarth persistent research manager\n";
    return 0;
}
