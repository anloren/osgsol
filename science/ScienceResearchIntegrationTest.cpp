#include "ScienceResearchBrief.h"
#include "ScienceResearchManager.h"

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
                 "osgsol-research-integration-XXXXXX").string();
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
        const std::string& path() const { return _path; }

    private:
        std::string _path;
    };

    earthscience::ScienceSourceDescriptor source(const std::string& id)
    {
        earthscience::ScienceSourceDescriptor value;
        value.id = id;
        if (id == "alphaearth-foundations")
        {
            value.name = "AlphaEarth Foundations";
            value.providerVersion = "source-coop-v1.1";
            value.attribution = "Google / Google DeepMind / Source Cooperative";
        }
        else if (id == "sentinel-2-l2a")
        {
            value.name = "Sentinel-2 Level-2A";
            value.providerVersion = "earth-search-v1";
            value.attribution =
                "Contains modified Copernicus Sentinel data / Element 84";
        }
        else
        {
            value.name = "Copernicus DEM GLO-30";
            value.providerVersion = "aws-glo30-2021";
            value.attribution = "Copernicus DEM / European Union / ESA / AWS";
        }
        return value;
    }

    std::shared_ptr<earthscience::ScienceArtifact> artifact(
        const std::string& sourceId, std::uint64_t jobId)
    {
        auto value = std::make_shared<earthscience::ScienceArtifact>();
        value->artifactId = sourceId + "-deterministic-artifact";
        value->generation = jobId;
        value->query.sourceId = sourceId;
        value->query.geometry.kind =
            earthscience::ScienceGeometryKind::Point;
        value->query.geometry.point = {35.68, 139.76};
        value->query.geometry.requestedSpanMeters = 10000.0;
        value->createdAt = "2026-07-19T12:00:00Z";
        earthscience::ScienceSourceReference reference;
        reference.sourceId = sourceId;
        reference.providerVersion = source(sourceId).providerVersion;
        reference.originalUrl = "https://example.invalid/" + sourceId;
        reference.requestedCoverage = value->query.geometry;
        reference.actualCoverage = {139.70, 35.60, 139.82, 35.76};
        reference.attribution = source(sourceId).attribution;
        reference.processingSteps = {"bounded deterministic source read"};
        if (sourceId == "alphaearth-foundations")
        {
            value->query.outputKind = earthscience::ScienceOutputKind::Analysis;
            value->query.variables = {"embedding64"};
            value->query.time.explicitYears = {2017, 2025};
            value->analysis.kind =
                earthscience::ScienceAnalysisKind::RegionalChange;
            value->analysis.regionalChange.actualResolutionMeters = 10.0;
            value->analysis.regionalChange.validOverlapCount = 100;
            value->analysis.regionalChange.noDataCellCount = 2;
            value->analysis.metrics = std::make_shared<const std::vector<
                earthscience::ScienceMetricResult>>(
                    std::initializer_list<earthscience::ScienceMetricResult>{
                        {earthscience::ScienceMetric::CosineDistance,
                         2017, 2025, 0.12, "unitless"}});
            value->analysis.limitations =
                std::make_shared<const std::vector<std::string>>(
                    std::initializer_list<std::string>{
                        "Latent components are not named physical variables"});
            value->processingVersion = "alphaearth-analysis-v1";
            reference.datasetId = "alphaearth-2017-2025-tile";
            reference.variables = value->query.variables;
            reference.units = {"unitless latent value"};
            reference.acquisitionTime = "2017/2025";
        }
        else if (sourceId == "sentinel-2-l2a")
        {
            value->query.outputKind =
                earthscience::ScienceOutputKind::RasterLayer;
            value->query.variables = {"visual"};
            value->raster.bounds = reference.actualCoverage;
            value->raster.sourceResolutionMeters = 10.0;
            value->raster.displayResolutionMeters = 39.1;
            value->processingVersion = "sentinel-2-visual-v1";
            value->warnings = {
                "Scene-wide cloud cover is not a per-pixel cloud mask"};
            reference.datasetId = "S2B_20250701_TEST";
            reference.variables = value->query.variables;
            reference.units = {"display DN"};
            reference.acquisitionTime = "2025-07-01T01:23:45Z";
        }
        else
        {
            value->query.outputKind =
                earthscience::ScienceOutputKind::RasterLayer;
            value->query.time.mode = earthscience::ScienceTimeMode::Instant;
            value->query.time.publicationTime = "2021";
            value->query.variables = {"surface_elevation"};
            value->raster.bounds = reference.actualCoverage;
            value->raster.sourceResolutionMeters = 30.0;
            value->raster.displayResolutionMeters = 39.1;
            value->processingVersion = "copernicus-dem-hypsometric-v1";
            earthscience::ScienceScalarSummary summary;
            summary.variableId = "surface_elevation";
            summary.displayName = "Surface elevation";
            summary.unit = "m EGM2008";
            summary.centerValid = summary.minimumValid =
                summary.maximumValid = summary.meanValid = true;
            summary.center = 14.3;
            summary.minimum = 0.0;
            summary.maximum = 75.6;
            summary.mean = 17.1;
            summary.validCellCount = 100;
            value->scalarSummaries.push_back(summary);
            value->analysis.limitations =
                std::make_shared<const std::vector<std::string>>(
                    std::initializer_list<std::string>{
                        "Static DSM, not a bare-earth terrain model"});
            reference.datasetId =
                "Copernicus_DSM_COG_10_N35_00_E139_00_DEM";
            reference.variables = value->query.variables;
            reference.units = {"m EGM2008"};
            reference.publicationTime = "2021";
        }
        value->sourceReferences.push_back(reference);
        return value;
    }

    earthscience::ScienceJobSnapshot readySnapshot(
        const std::shared_ptr<earthscience::ScienceArtifact>& value)
    {
        earthscience::ScienceJobSnapshot snapshot;
        snapshot.jobId = value->generation;
        snapshot.state = earthscience::ScienceJobState::Ready;
        snapshot.query = value->query;
        snapshot.message = "Ready";
        snapshot.lastSuccessfulArtifact = value;
        return snapshot;
    }

    void attachReady(
        earthscience::ScienceResearchManager& manager,
        const std::string& researchId, const std::string& sourceId,
        std::uint64_t jobId)
    {
        earthscience::ScienceResearchRecord record;
        std::string error;
        require(manager.addStep(
                    researchId, jobId, sourceId, record, error),
                "research source step could not be added");
        auto result = artifact(sourceId, jobId);
        require(manager.observe(
                    researchId, readySnapshot(result), source(sourceId),
                    record, error),
                "ready research evidence could not be observed");
    }

    std::string persistedContents(const std::string& root)
    {
        std::string result;
        for (const auto& entry :
             std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file()) continue;
            std::ifstream stream(entry.path(), std::ios::binary);
            result.append(std::istreambuf_iterator<char>(stream),
                          std::istreambuf_iterator<char>());
        }
        return result;
    }
}

int main()
{
    TempDirectory temporary;
    std::string researchId;
    std::string originalMarkdown;
    {
        earthscience::ScienceResearchManager manager(temporary.path());
        earthscience::ScienceResearchRecord record;
        std::string error;
        require(manager.create(
                    "Compare three complementary scientific sources",
                    record, error),
                "integrated research creation failed");
        researchId = record.researchId;
        attachReady(manager, researchId, "alphaearth-foundations", 1);
        attachReady(manager, researchId, "sentinel-2-l2a", 2);
        attachReady(manager, researchId, "copernicus-dem-glo-30", 3);
        std::vector<earthscience::ScienceEvidenceRecord> evidence;
        require(manager.get(researchId, record, error) &&
                    record.state == earthscience::ScienceResearchState::Ready &&
                    manager.loadEvidence(researchId, evidence, error) &&
                    evidence.size() == 3,
                "integrated three-source evidence was not ready");
        earthscience::ScienceResearchBrief brief;
        require(earthscience::buildScienceResearchBrief(
                    record, evidence, brief, error),
                "integrated three-source brief failed");
        originalMarkdown = brief.markdown;
    }

    earthscience::ScienceResearchManager restarted(temporary.path());
    earthscience::ScienceResearchRecord restartedRecord;
    std::vector<earthscience::ScienceEvidenceRecord> restartedEvidence;
    earthscience::ScienceResearchBrief restartedBrief;
    std::string error;
    require(restarted.get(researchId, restartedRecord, error) &&
                restarted.loadEvidence(
                    researchId, restartedEvidence, error) &&
                earthscience::buildScienceResearchBrief(
                    restartedRecord, restartedEvidence,
                    restartedBrief, error) &&
                restartedBrief.markdown == originalMarkdown,
            "restart did not reproduce a byte-stable three-source brief");

    earthscience::ScienceResearchRecord partial;
    require(restarted.create("Partial source evidence", partial, error),
            "partial research creation failed");
    attachReady(restarted, partial.researchId,
                "alphaearth-foundations", 11);
    require(restarted.addStep(
                partial.researchId, 12, "sentinel-2-l2a", partial, error),
            "partial failed step could not be added");
    earthscience::ScienceJobSnapshot failed;
    failed.jobId = 12;
    failed.query.sourceId = "sentinel-2-l2a";
    failed.state = earthscience::ScienceJobState::Failed;
    failed.message = "No matching acquisition";
    require(restarted.observe(
                partial.researchId, failed, source("sentinel-2-l2a"),
                partial, error) &&
                partial.state == earthscience::ScienceResearchState::Partial,
            "failed provider did not produce partial research");
    std::vector<earthscience::ScienceEvidenceRecord> partialEvidence;
    earthscience::ScienceResearchBrief partialBrief;
    require(restarted.loadEvidence(
                partial.researchId, partialEvidence, error) &&
                earthscience::buildScienceResearchBrief(
                    partial, partialEvidence, partialBrief, error) &&
                partialBrief.markdown.find("No matching acquisition") !=
                    std::string::npos &&
                partialBrief.citations.size() == 1,
            "partial brief lost successful evidence or failed-source status");

    const std::string persisted = persistedContents(temporary.path());
    require(persisted.find("rgba") == std::string::npos &&
                persisted.find("embedding.values") == std::string::npos &&
                persisted.find("hotspot_mask") == std::string::npos &&
                persisted.find("pca_scores") == std::string::npos,
            "persistent research serialized a raw scientific payload");

    std::cout << "[OK] ScienceEarth three-source research survives restart\n";
    return 0;
}
