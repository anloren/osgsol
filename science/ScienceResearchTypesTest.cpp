#include "ScienceResearchTypes.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    earthscience::ScienceEvidenceRecord evidence()
    {
        earthscience::ScienceEvidenceRecord value;
        value.schemaVersion = earthscience::SCIENCE_EVIDENCE_SCHEMA_V1;
        value.evidenceId = "evidence-0123456789abcdef";
        value.artifactId = "copernicus-dem-glo-30-1";
        value.sourceId = "copernicus-dem-glo-30";
        value.sourceName = "Copernicus DEM GLO-30";
        value.providerVersion = "aws-glo30-2021";
        value.datasetId = "Copernicus_DSM_COG_10_N35_00_E139_00_DEM";
        value.originalUrl = "https://copernicus-dem-30m.s3.amazonaws.com/test.tif";
        value.attribution = "Copernicus DEM / European Union / ESA / AWS";
        value.requestedCoverage.kind =
            earthscience::ScienceGeometryKind::Point;
        value.requestedCoverage.point = {35.68, 139.76};
        value.requestedCoverage.requestedSpanMeters = 10000.0;
        value.actualCoverage = {139.70, 35.60, 139.82, 35.76};
        value.variables = {"surface_elevation"};
        value.units = {"m"};
        value.publicationTime = "2021";
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
        value.validCellCount = 100;
        value.noDataCellCount = 2;
        value.sourceResolutionMeters = 30.0;
        value.displayResolutionMeters = 39.1;
        value.processingVersion = "copernicus-dem-hypsometric-v1";
        value.processingSteps = {"bounded COG window", "scalar statistics"};
        value.warnings = {"DSM is not bare-earth terrain"};
        value.limitations = {"Static product; no annual change"};
        value.createdAt = "2026-07-19T12:00:00Z";
        return value;
    }

    earthscience::ScienceResearchRecord research()
    {
        earthscience::ScienceResearchRecord value;
        value.schemaVersion = earthscience::SCIENCE_RESEARCH_SCHEMA_V1;
        value.researchId = "research-20260719T120000Z-0001";
        value.question = "What evidence describes this location?";
        value.state = earthscience::ScienceResearchState::Ready;
        earthscience::ScienceResearchStep step;
        step.liveJobId = 1;
        step.sourceId = "copernicus-dem-glo-30";
        step.state = earthscience::ScienceJobState::Ready;
        step.artifactId = "copernicus-dem-glo-30-1";
        step.evidenceId = "evidence-0123456789abcdef";
        step.message = "Ready";
        value.steps.push_back(step);
        value.createdAt = value.updatedAt = "2026-07-19T12:00:00Z";
        return value;
    }
}

int main()
{
    require(std::string(earthscience::scienceResearchStateName(
                earthscience::ScienceResearchState::Draft)) == "draft" &&
            std::string(earthscience::scienceResearchStateName(
                earthscience::ScienceResearchState::Running)) == "running" &&
            std::string(earthscience::scienceResearchStateName(
                earthscience::ScienceResearchState::Partial)) == "partial" &&
            std::string(earthscience::scienceResearchStateName(
                earthscience::ScienceResearchState::Ready)) == "ready" &&
            std::string(earthscience::scienceResearchStateName(
                earthscience::ScienceResearchState::Failed)) == "failed",
            "research state names are not stable");

    std::string error;
    earthscience::ScienceEvidenceRecord validEvidence = evidence();
    require(earthscience::validateScienceEvidence(validEvidence, error) &&
                error.empty(),
            "valid compact evidence was rejected");
    earthscience::ScienceResearchRecord validResearch = research();
    require(earthscience::validateScienceResearch(validResearch, error) &&
                error.empty(),
            "valid research record was rejected");

    earthscience::ScienceEvidenceRecord changed = validEvidence;
    changed.schemaVersion = "future-schema";
    require(!earthscience::validateScienceEvidence(changed, error) &&
                error.find("schema") != std::string::npos,
            "unknown evidence schema was accepted");
    changed = validEvidence;
    changed.evidenceId = "../escape";
    require(!earthscience::validateScienceEvidence(changed, error),
            "unsafe evidence id was accepted");
    changed = validEvidence;
    changed.scalarSummaries.front().mean =
        std::numeric_limits<double>::infinity();
    require(!earthscience::validateScienceEvidence(changed, error) &&
                error.find("finite") != std::string::npos,
            "non-finite scalar evidence was accepted");
    changed = validEvidence;
    changed.processingSteps.resize(65, "step");
    require(!earthscience::validateScienceEvidence(changed, error) &&
                error.find("limit") != std::string::npos,
            "oversized evidence list was accepted");
    changed = validEvidence;
    changed.originalUrl = std::string(9000, 'x');
    require(!earthscience::validateScienceEvidence(changed, error),
            "oversized evidence string was accepted");

    earthscience::ScienceResearchRecord changedResearch = validResearch;
    changedResearch.steps.push_back(changedResearch.steps.front());
    require(!earthscience::validateScienceResearch(changedResearch, error) &&
                error.find("duplicate") != std::string::npos,
            "duplicate research evidence was accepted");
    changedResearch = validResearch;
    changedResearch.steps.resize(33, changedResearch.steps.front());
    for (std::size_t index = 0; index < changedResearch.steps.size(); ++index)
        changedResearch.steps[index].liveJobId = index + 1;
    require(!earthscience::validateScienceResearch(changedResearch, error) &&
                error.find("limit") != std::string::npos,
            "oversized research step list was accepted");

    std::cout << "[OK] ScienceEarth persistent research value contracts\n";
    return 0;
}
