#include "ScienceEvidenceStore.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
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
                 "osgsol-evidence-store-XXXXXX").string();
            std::vector<char> writable(pattern.begin(), pattern.end());
            writable.push_back('\0');
            char* created = mkdtemp(writable.data());
            require(created != nullptr, "temporary directory creation failed");
            _path = created;
        }
        ~TempDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(_path, ignored);
        }
        const std::filesystem::path& path() const { return _path; }
    private:
        std::filesystem::path _path;
    };

    earthscience::ScienceSourceDescriptor descriptor()
    {
        earthscience::ScienceSourceDescriptor source;
        source.id = "copernicus-dem-glo-30";
        source.name = "Copernicus DEM GLO-30";
        source.providerVersion = "aws-glo30-2021";
        source.attribution = "Copernicus DEM / European Union / ESA / AWS";
        return source;
    }

    earthscience::ScienceArtifact artifact()
    {
        earthscience::ScienceArtifact value;
        value.artifactId = "copernicus-dem-glo-30-1";
        value.generation = 1;
        value.query.sourceId = "copernicus-dem-glo-30";
        value.query.geometry.kind = earthscience::ScienceGeometryKind::Point;
        value.query.geometry.point = {35.68, 139.76};
        value.query.geometry.requestedSpanMeters = 10000.0;
        value.query.time.mode = earthscience::ScienceTimeMode::Instant;
        value.query.time.instant = "2021";
        value.query.time.publicationTime = "2021";
        value.query.variables = {"surface_elevation"};
        value.raster.bounds = {139.70, 35.60, 139.82, 35.76};
        value.raster.sourceResolutionMeters = 30.0;
        value.raster.displayResolutionMeters = 39.1;
        value.raster.rgba =
            std::make_shared<const std::vector<unsigned char>>(1024, 127);
        value.embedding.values =
            std::make_shared<const std::vector<float>>(64, 0.5f);
        value.embedding.mask =
            std::make_shared<const std::vector<unsigned char>>(1, 1);
        earthscience::ScienceScalarSummary summary;
        summary.variableId = "surface_elevation";
        summary.displayName = "Surface elevation";
        summary.unit = "m";
        summary.centerValid = summary.minimumValid = summary.maximumValid =
            summary.meanValid = true;
        summary.center = 42.0;
        summary.minimum = 1.0;
        summary.maximum = 88.0;
        summary.mean = 35.0;
        summary.validCellCount = 100;
        summary.noDataCellCount = 2;
        value.scalarSummaries.push_back(summary);
        earthscience::ScienceSourceReference reference;
        reference.sourceId = value.query.sourceId;
        reference.providerVersion = "aws-glo30-2021";
        reference.datasetId = "Copernicus_DSM_COG_10_N35_00_E139_00_DEM";
        reference.originalUrl =
            "https://copernicus-dem-30m.s3.amazonaws.com/test.tif";
        reference.requestedCoverage = value.query.geometry;
        reference.actualCoverage = value.raster.bounds;
        reference.variables = value.query.variables;
        reference.units = {"m"};
        reference.publicationTime = "2021";
        reference.attribution = descriptor().attribution;
        reference.processingSteps = {"bounded COG window", "statistics"};
        value.sourceReferences.push_back(reference);
        value.processingVersion = "copernicus-dem-hypsometric-v1";
        value.warnings = {"DSM is not bare-earth terrain"};
        value.createdAt = "2026-07-19T12:00:00Z";
        return value;
    }

    earthscience::ScienceResearchRecord research(
        const std::string& evidenceId)
    {
        earthscience::ScienceResearchRecord value;
        value.researchId = "research-20260719T120000Z-0001";
        value.question = "What evidence describes this location?";
        value.state = earthscience::ScienceResearchState::Ready;
        earthscience::ScienceResearchStep step;
        step.liveJobId = 1;
        step.sourceId = "copernicus-dem-glo-30";
        step.state = earthscience::ScienceJobState::Ready;
        step.artifactId = "copernicus-dem-glo-30-1";
        step.evidenceId = evidenceId;
        step.message = "Ready";
        value.steps.push_back(step);
        value.createdAt = value.updatedAt = "2026-07-19T12:00:00Z";
        return value;
    }

    std::string readFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>());
    }
}

int main()
{
    TempDirectory temporary;
    earthscience::ScienceEvidenceStore store(temporary.path().string());
    std::string error;
    earthscience::ScienceEvidenceRecord record;
    require(earthscience::makeScienceEvidence(
                artifact(), descriptor(), record, error),
            "artifact conversion failed");
    require(record.evidenceId.rfind("evidence-", 0) == 0 &&
                record.scalarSummaries.size() == 1 &&
                record.sourceId == "copernicus-dem-glo-30" &&
                record.publicationTime == "2021",
            "artifact conversion lost compact scientific evidence");
    require(store.saveEvidence(record, error),
            "evidence save failed");
    earthscience::ScienceEvidenceRecord loaded;
    require(store.loadEvidence(record.evidenceId, loaded, error) &&
                loaded.artifactId == record.artifactId &&
                loaded.scalarSummaries.front().mean == 35.0,
            "evidence round-trip failed");

    const std::filesystem::path evidencePath = temporary.path() /
        "evidence" / (record.evidenceId + ".json");
    const std::string raw = readFile(evidencePath);
    require(raw.find("rgba") == std::string::npos &&
                raw.find("embedding.values") == std::string::npos &&
                raw.size() < 1024u * 1024u,
            "evidence persisted a raw payload or exceeded its bound");
    require(std::filesystem::directory_iterator(
                evidencePath.parent_path()) !=
                std::filesystem::directory_iterator(),
            "evidence directory is empty after save");
    for (const auto& entry :
         std::filesystem::directory_iterator(evidencePath.parent_path()))
        require(entry.path().extension() != ".tmp",
                "atomic save left a temporary file");

    earthscience::ScienceResearchRecord researchRecord =
        research(record.evidenceId);
    require(store.saveResearch(researchRecord, error),
            "research save failed");
    earthscience::ScienceResearchRecord loadedResearch;
    require(store.loadResearch(
                researchRecord.researchId, loadedResearch, error) &&
                loadedResearch.steps.front().evidenceId == record.evidenceId,
            "research restart load failed");

    earthscience::ScienceEvidenceRecord incompatible = record;
    incompatible.datasetId = "changed-dataset";
    require(!store.saveEvidence(incompatible, error) &&
                error.find("incompatible") != std::string::npos,
            "duplicate incompatible evidence replaced the original");
    require(!store.loadEvidence("../escape", loaded, error),
            "path traversal id was accepted");

    {
        std::ofstream malformed(evidencePath, std::ios::trunc);
        malformed << "{not-json";
    }
    require(!store.loadEvidence(record.evidenceId, loaded, error) &&
                error.find("JSON") != std::string::npos,
            "malformed evidence JSON was accepted");
    {
        std::ofstream oversized(evidencePath, std::ios::binary | std::ios::trunc);
        oversized << std::string(1024u * 1024u + 1u, 'x');
    }
    require(!store.loadEvidence(record.evidenceId, loaded, error) &&
                error.find("size") != std::string::npos,
            "oversized evidence file was accepted");

    TempDirectory symlinkFixture;
    const std::filesystem::path outside = symlinkFixture.path() / "outside";
    std::filesystem::create_directories(outside);
    const std::filesystem::path root = symlinkFixture.path() / "root";
    std::filesystem::create_directories(root);
    std::filesystem::create_directory_symlink(outside, root / "evidence");
    earthscience::ScienceEvidenceStore symlinkStore(root.string());
    require(!symlinkStore.saveEvidence(record, error) &&
                error.find("symlink") != std::string::npos,
            "evidence directory symlink escape was accepted");

    std::cout << "[OK] ScienceEarth atomic compact evidence store\n";
    return 0;
}
