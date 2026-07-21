#include "ScienceResearchBrief.h"

#include <cstdlib>
#include <iostream>
#include <set>
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

    earthscience::ScienceEvidenceRecord evidence(
        const std::string& id, const std::string& sourceId,
        const std::string& time, double west = 139.70,
        double south = 35.60, double east = 139.82,
        double north = 35.76)
    {
        earthscience::ScienceEvidenceRecord value;
        value.evidenceId = id;
        value.artifactId = sourceId + "-artifact";
        value.sourceId = sourceId;
        value.requestedCoverage.kind =
            earthscience::ScienceGeometryKind::Point;
        value.requestedCoverage.point = {35.68, 139.76};
        value.requestedCoverage.requestedSpanMeters = 10000.0;
        value.actualCoverage = {west, south, east, north};
        value.acquisitionTime = time;
        value.createdAt = "2026-07-19T12:00:00Z";
        value.processingVersion = "deterministic-test-v1";
        value.processingSteps = {"bounded source read"};
        value.validCellCount = 100;
        value.noDataCellCount = 2;
        value.sourceResolutionMeters = 10.0;
        value.displayResolutionMeters = 39.1;
        value.actualResolutionMeters = 39.1;
        if (sourceId == "alphaearth-foundations")
        {
            value.sourceName = "AlphaEarth Foundations";
            value.providerVersion = "source-coop-v1.1";
            value.datasetId = "alphaearth-2025-tile";
            value.originalUrl = "https://source.coop/alphaearth/test.tif";
            value.attribution = "Google / Google DeepMind / Source Cooperative";
            value.variables = {"embedding_64d"};
            value.units = {"unitless latent value"};
            earthscience::ScienceEvidenceMetric metric;
            metric.metric = earthscience::ScienceMetric::CosineDistance;
            metric.baselineYear = 2017;
            metric.comparisonYear = 2025;
            metric.value = 0.12;
            metric.unit = "unitless";
            value.primaryMetrics.push_back(metric);
            value.interpretations = {
                "PCA summarizes result-local variance in latent space.",
                "Spherical clusters summarize local embedding structure."};
            value.limitations = {
                "Latent components are not named physical variables",
                "PCA axes do not create semantic components.",
                "Clusters are structural groups, not validated land-cover classes."};
        }
        else if (sourceId == "sentinel-2-l2a")
        {
            value.sourceName = "Sentinel-2 Level-2A";
            value.providerVersion = "earth-search-v1";
            value.datasetId = "S2B_20250701_TEST";
            value.originalUrl = "https://earth-search.aws.element84.com/test";
            value.attribution = "Contains modified Copernicus Sentinel data";
            value.variables = {"visual"};
            value.units = {"display DN"};
            value.warnings = {"Scene-wide cloud cover: 18%"};
            value.limitations = {
                "Selected acquisition; not a cloud-free composite"};
        }
        else
        {
            value.sourceName = "Copernicus DEM GLO-30";
            value.providerVersion = "aws-glo30-2021";
            value.datasetId = "Copernicus_DSM_COG_10_N35_00_E139_00_DEM";
            value.originalUrl =
                "https://copernicus-dem-30m.s3.amazonaws.com/test.tif";
            value.attribution = "Copernicus DEM / European Union / ESA";
            value.variables = {"surface_elevation"};
            value.units = {"m EGM2008"};
            value.sourceResolutionMeters = 30.0;
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
            summary.noDataCellCount = 0;
            value.scalarSummaries.push_back(summary);
            value.limitations = {
                "Static DSM, not a bare-earth terrain model"};
        }
        return value;
    }

    earthscience::ScienceResearchRecord research(
        earthscience::ScienceResearchState state =
            earthscience::ScienceResearchState::Ready)
    {
        earthscience::ScienceResearchRecord value;
        value.researchId = "research-20260719T120000Z-0001";
        value.question = "What complementary evidence describes this area?";
        value.state = state;
        const std::vector<std::string> sources = {
            "alphaearth-foundations", "sentinel-2-l2a",
            "copernicus-dem-glo-30"};
        for (std::size_t i = 0; i < sources.size(); ++i)
        {
            earthscience::ScienceResearchStep step;
            step.liveJobId = i + 1;
            step.sourceId = sources[i];
            step.state = earthscience::ScienceJobState::Ready;
            step.artifactId = sources[i] + "-artifact";
            step.evidenceId = "evidence-" + std::to_string(i + 1);
            step.message = "Ready";
            value.steps.push_back(step);
        }
        value.createdAt = value.updatedAt = "2026-07-19T12:00:00Z";
        return value;
    }

    void requireCitationsCovered(
        const earthscience::ScienceResearchBrief& brief)
    {
        std::set<std::string> known;
        for (const auto& citation : brief.citations)
            known.insert(citation.evidenceId);
        const auto check = [&known](
            const std::vector<earthscience::ScienceBriefStatement>& values)
        {
            for (const auto& value : values)
            {
                require(!value.evidenceIds.empty(),
                        "material statement has no evidence id");
                for (const std::string& id : value.evidenceIds)
                    require(known.count(id) == 1,
                            "statement refers to an unknown citation");
            }
        };
        check(brief.observations);
        check(brief.inferences);
        check(brief.limitations);
    }
}

int main()
{
    std::vector<earthscience::ScienceEvidenceRecord> three = {
        evidence("evidence-1", "alphaearth-foundations", "2017/2025"),
        evidence("evidence-2", "sentinel-2-l2a", "2025-07-01T01:23:45Z"),
        evidence("evidence-3", "copernicus-dem-glo-30", "2021")};
    earthscience::ScienceResearchBrief brief;
    std::string error;
    require(earthscience::buildScienceResearchBrief(
                research(), three, brief, error),
            "three-source brief build failed");
    require(brief.state == earthscience::ScienceResearchState::Ready &&
                brief.sources.size() == 3 && brief.citations.size() == 3 &&
                brief.observations.size() == 5 &&
                !brief.inferences.empty() && !brief.limitations.empty(),
            "three-source brief sections are incomplete");
    requireCitationsCovered(brief);
    require(brief.markdown.find("# ScienceEarth research brief") !=
                std::string::npos &&
                brief.markdown.find("## Scope and status") !=
                std::string::npos &&
                brief.markdown.find("## Source, time, and coverage") !=
                std::string::npos &&
                brief.markdown.find("## Observations") != std::string::npos &&
                brief.markdown.find("## Cross-source inferences") !=
                std::string::npos &&
                brief.markdown.find("## Limitations") != std::string::npos &&
                brief.markdown.find("## Citations") != std::string::npos &&
                brief.markdown.find("[1]") != std::string::npos &&
                brief.markdown.find("[2]") != std::string::npos &&
                brief.markdown.find("[3]") != std::string::npos,
            "Markdown brief lacks required sections or numbered citations");
    require(brief.markdown.find("latent representation") != std::string::npos &&
                brief.markdown.find("named physical variable") !=
                    std::string::npos &&
                brief.markdown.find("named land-cover variable") !=
                    std::string::npos &&
                brief.markdown.find("result-local variance") !=
                    std::string::npos &&
                brief.markdown.find("do not create semantic components") !=
                    std::string::npos &&
                brief.markdown.find("not validated land-cover classes") !=
                    std::string::npos &&
                brief.markdown.find("cloud-free composite") !=
                    std::string::npos &&
                brief.markdown.find("selected acquisition") !=
                    std::string::npos &&
                brief.markdown.find("static EGM2008 DSM") !=
                    std::string::npos &&
                brief.markdown.find("bare-earth") != std::string::npos &&
                brief.markdown.find("does not establish cause") !=
                    std::string::npos,
            "scientific source semantics are not explicit");
    require(brief.markdown.find("vegetation change") == std::string::npos &&
                brief.markdown.find("validated as land-cover") ==
                    std::string::npos &&
                brief.markdown.find("cloud-free imagery") == std::string::npos &&
                brief.markdown.find("terrain elevation change") ==
                    std::string::npos,
            "brief makes a forbidden semantic overclaim");
    require(brief.markdown.find("Temporal mismatch") != std::string::npos,
            "temporal mismatch was hidden");

    earthscience::ScienceEvidenceRecord agro = evidence(
        "evidence-agro", "era5-land-surface-history",
        "2020-01-01/2021-12-31");
    agro.sourceName = "ERA5-Land Surface & Soil History";
    agro.providerVersion = "ecmwf-era5-land-openmeteo-v1";
    agro.datasetId = "ECMWF ERA5-Land daily aggregation";
    agro.originalUrl =
        "https://archive-api.open-meteo.com/v1/archive?fixture";
    agro.attribution = "ECMWF / Open-Meteo";
    agro.variables = {"temperature_2m_mean"};
    agro.units = {"°C"};
    agro.actualResolutionMeters = 11100.0;
    earthscience::ScienceEvidenceVariableSeries temperature;
    temperature.variableId = "temperature_2m_mean";
    temperature.displayName = "Mean temperature";
    temperature.unit = "°C";
    temperature.aggregationMethod = "annual mean";
    temperature.nativeResolutionMeters = 11100.0;
    temperature.points = {{2020, 15.2, true}, {2021, 15.8, true}};
    agro.variableSeries.push_back(temperature);
    agro.limitations = {
        "Reanalysis source grid; not a station or parcel observation"};
    earthscience::ScienceResearchRecord agroResearch;
    agroResearch.researchId = "research-agro-1";
    agroResearch.question = "What is the annual agricultural climate context?";
    agroResearch.state = earthscience::ScienceResearchState::Ready;
    agroResearch.steps.push_back({
        1, agro.sourceId, earthscience::ScienceJobState::Ready,
        agro.artifactId, agro.evidenceId, "Ready"});
    agroResearch.createdAt = agroResearch.updatedAt =
        "2026-07-21T12:00:00Z";
    earthscience::ScienceResearchBrief agroBrief;
    require(earthscience::buildScienceResearchBrief(
                agroResearch, {agro}, agroBrief, error) &&
                agroBrief.markdown.find("15.8 °C") != std::string::npos &&
                agroBrief.markdown.find("reanalysis source-grid") !=
                    std::string::npos &&
                agroBrief.markdown.find("parcel observation") !=
                    std::string::npos,
            "research brief lost annual agro values or scientific boundary");

    earthscience::ScienceResearchBrief repeated;
    require(earthscience::buildScienceResearchBrief(
                research(), three, repeated, error) &&
                repeated.markdown == brief.markdown,
            "brief output is not byte-stable");

    std::vector<earthscience::ScienceEvidenceRecord> separated = three;
    separated[2].actualCoverage = {-74.1, 40.6, -73.8, 40.9};
    earthscience::ScienceResearchBrief noOverlap;
    require(earthscience::buildScienceResearchBrief(
                research(), separated, noOverlap, error) &&
                noOverlap.inferences.empty() &&
                noOverlap.markdown.find("does not overlap") !=
                    std::string::npos,
            "non-overlapping evidence produced a cross-source inference");

    std::vector<earthscience::ScienceEvidenceRecord> noDemSummary = three;
    noDemSummary[2].scalarSummaries.clear();
    earthscience::ScienceResearchBrief noNumbers;
    require(earthscience::buildScienceResearchBrief(
                research(), noDemSummary, noNumbers, error) &&
                noNumbers.markdown.find("display colors are not interpreted") !=
                    std::string::npos &&
                noNumbers.markdown.find("14.3") == std::string::npos,
            "DEM colors were treated as numeric evidence");

    earthscience::ScienceResearchRecord partialResearch =
        research(earthscience::ScienceResearchState::Partial);
    partialResearch.steps[1].state = earthscience::ScienceJobState::Failed;
    partialResearch.steps[1].artifactId.clear();
    partialResearch.steps[1].evidenceId.clear();
    partialResearch.steps[1].message = "No matching acquisition";
    std::vector<earthscience::ScienceEvidenceRecord> partialEvidence = {
        three[0], three[2]};
    earthscience::ScienceResearchBrief partial;
    require(earthscience::buildScienceResearchBrief(
                partialResearch, partialEvidence, partial, error) &&
                partial.state == earthscience::ScienceResearchState::Partial &&
                partial.sources.size() == 2 &&
                partial.markdown.find("sentinel-2-l2a") !=
                    std::string::npos &&
                partial.markdown.find("No matching acquisition") !=
                    std::string::npos,
            "failed provider did not produce an honest partial brief");
    requireCitationsCovered(partial);

    std::vector<earthscience::ScienceEvidenceRecord> missing = {three[0]};
    require(!earthscience::buildScienceResearchBrief(
                research(), missing, brief, error) &&
                error.find("missing") != std::string::npos,
            "ready step without supplied evidence was accepted");

    std::cout << "[OK] ScienceEarth deterministic cited research brief\n";
    return 0;
}
