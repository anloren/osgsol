#include "ScienceQueryTypes.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    void testStableStateNames()
    {
        require(std::string(earthscience::scienceSourceHealthName(
                    earthscience::ScienceSourceHealth::Unavailable)) ==
                    "unavailable",
                "unavailable source health name changed");
        require(std::string(earthscience::scienceSourceHealthName(
                    earthscience::ScienceSourceHealth::Degraded)) ==
                    "degraded",
                "degraded source health name changed");
        require(std::string(earthscience::scienceJobStateName(
                    earthscience::ScienceJobState::Fetching)) == "fetching",
                "fetching job state name changed");
        require(std::string(earthscience::scienceJobStateName(
                    static_cast<earthscience::ScienceJobState>(999))) ==
                    "unknown",
                "unknown job states must remain machine-readable");
    }

    void testGenericDefaultsAreUnavailableAndBounded()
    {
        earthscience::ScienceSourceDescriptor source;
        earthscience::GeoTemporalQuery query;
        earthscience::ScienceJobSnapshot snapshot;
        earthscience::ScienceAnalysisOptions options;
        earthscience::ScienceProgress progress;

        require(source.health == earthscience::ScienceSourceHealth::Unavailable,
                "a default source must not claim availability");
        require(query.geometry.kind == earthscience::ScienceGeometryKind::Point,
                "the bounded G2 query default must be a point");
        require(query.time.mode == earthscience::ScienceTimeMode::ExplicitYears,
                "the bounded G2 query default must use explicit years");
        require(query.aggregation == earthscience::ScienceAggregation::None,
                "the bounded G2 query default must not aggregate");
        require(query.outputKind == earthscience::ScienceOutputKind::RasterLayer,
                "the bounded G2 query default must request a raster layer");
        require(snapshot.state == earthscience::ScienceJobState::Idle,
                "a default service snapshot must be idle");
        require(!snapshot.lastSuccessfulArtifact,
                "a default service snapshot must not invent an artifact");
        require(options.gridSize == 128 && options.hotspotQuantile == 0.90,
                "analysis defaults changed");
        require(options.pcaComponents == 3 && options.clusterCount == 4,
                "advanced defaults changed");
        require(progress.stage == earthscience::ScienceProgressStage::Idle &&
                    !progress.determinate && progress.completedUnits == 0 &&
                    progress.totalUnits == 0,
                "progress fabricated a percentage");
        require(progress.legacyFraction() == 0.0f,
                "indeterminate progress fabricated a legacy percentage");

        progress.stage = earthscience::ScienceProgressStage::Reading;
        progress.completedUnits = 1;
        progress.totalUnits = 4;
        progress.determinate = true;
        progress.unit = "tiles";
        require(progress.legacyFraction() == 0.25f,
                "measurable progress lost its legacy compatibility value");
    }

    void testStableAnalysisContractNames()
    {
        require(std::string(earthscience::scienceProgressStageName(
                    earthscience::ScienceProgressStage::Materializing)) ==
                    "materializing",
                "materializing progress stage name changed");
        require(std::string(earthscience::scienceOutputKindName(
                    earthscience::ScienceOutputKind::Embedding)) ==
                    "embedding",
                "embedding output name changed");
        require(std::string(earthscience::scienceAnalysisKindName(
                    earthscience::ScienceAnalysisKind::SphericalClusters)) ==
                    "spherical-clusters",
                "spherical cluster analysis name changed");
        require(std::string(earthscience::scienceMetricName(
                    earthscience::ScienceMetric::AngularDistance)) ==
                    "angular-distance",
                "angular distance metric name changed");
        require(std::string(earthscience::scienceMetricName(
                    static_cast<earthscience::ScienceMetric>(999))) ==
                    "unknown",
                "unknown science metrics must remain machine-readable");
    }

    void testIncompleteLargeProgressNeverRoundsToComplete()
    {
        earthscience::ScienceProgress progress;
        progress.determinate = true;
        progress.completedUnits =
            std::numeric_limits<std::uint64_t>::max() - 1;
        progress.totalUnits = std::numeric_limits<std::uint64_t>::max();

        const float fraction = progress.legacyFraction();
        require(std::isfinite(fraction) && fraction >= 0.0f &&
                    fraction < 1.0f,
                "incomplete large progress rounded to complete");

        progress.completedUnits = 1;
        progress.totalUnits = 1;
        require(progress.legacyFraction() == 1.0f,
                "ready 1/1 progress stopped reporting completion");
    }

    void testArtifactSharesImmutablePixelsAndExactGroundGrid()
    {
        static_assert(std::is_same<
            decltype(earthscience::ScienceRasterPayload::rgba),
            std::shared_ptr<const std::vector<unsigned char>>>::value,
            "science RGBA payload must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceRasterPayload::groundGrid),
            std::shared_ptr<const earthscience::ScienceGroundGrid>>::value,
            "science ground grid must be immutable");

        earthscience::ScienceArtifact artifact;
        artifact.artifactId = "artifact-7";
        artifact.generation = 7;
        artifact.raster.width = 2;
        artifact.raster.height = 2;
        artifact.raster.rgba =
            std::make_shared<const std::vector<unsigned char>>(16, 166);
        auto grid = std::make_shared<earthscience::ScienceGroundGrid>();
        grid->columns = 2;
        grid->rows = 2;
        grid->points = {
            {138.70, 35.34}, {138.76, 35.34},
            {138.70, 35.40}, {138.76, 35.40},
        };
        artifact.raster.groundGrid = grid;

        const earthscience::ScienceArtifact copied = artifact;
        require(copied.artifactId == "artifact-7" && copied.generation == 7,
                "artifact value metadata was not copied");
        require(copied.raster.rgba == artifact.raster.rgba,
                "artifact copy duplicated or lost immutable pixels");
        require(copied.raster.groundGrid == artifact.raster.groundGrid,
                "artifact copy duplicated or lost the exact ground grid");
    }

    void testArtifactSharesImmutableEmbeddingAndAnalysisBacking()
    {
        static_assert(
            earthscience::ScienceEmbeddingPayload::componentCount == 64,
            "embedding component count must remain exactly 64");
        static_assert(!std::is_assignable<
            decltype((static_cast<earthscience::ScienceEmbeddingPayload*>(
                nullptr)->componentCount)), int>::value,
            "embedding component count must not be mutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceEmbeddingPayload::years),
            std::shared_ptr<const std::vector<int>>>::value,
            "embedding years must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceEmbeddingPayload::values),
            std::shared_ptr<const std::vector<float>>>::value,
            "embedding values must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceEmbeddingPayload::mask),
            std::shared_ptr<const std::vector<unsigned char>>>::value,
            "embedding mask must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceScalarChangeRaster::values),
            std::shared_ptr<const std::vector<float>>>::value,
            "analysis raster values must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceEmbeddingPayload::processingSteps),
            std::shared_ptr<const std::vector<std::string>>>::value,
            "embedding processing steps must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceEmbeddingPayload::warnings),
            std::shared_ptr<const std::vector<std::string>>>::value,
            "embedding warnings must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceAnalysisPayload::metrics),
            std::shared_ptr<const std::vector<
                earthscience::ScienceMetricResult>>>::value,
            "analysis metrics must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceAnalysisPayload::annualSeries),
            std::shared_ptr<const std::vector<
                earthscience::ScienceAnnualSeries>>>::value,
            "analysis annual series must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceAnnualSeries::validity),
            std::shared_ptr<const std::vector<unsigned char>>>::value,
            "analysis annual validity must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceRegionalChangeSummary::quantiles),
            std::shared_ptr<const std::vector<
                earthscience::ScienceQuantileResult>>>::value,
            "regional quantiles must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceRegionalChangeSummary::hotspotMask),
            std::shared_ptr<const std::vector<unsigned char>>>::value,
            "regional hotspot mask must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceRegionalChangeSummary::hotspotIndices),
            std::shared_ptr<const std::vector<std::uint64_t>>>::value,
            "regional hotspot indices must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::SciencePcaResult::components),
            std::shared_ptr<const std::vector<float>>>::value,
            "PCA components must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::SciencePcaResult::explainedVarianceRatios),
            std::shared_ptr<const std::vector<double>>>::value,
            "PCA variance ratios must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceClusterResult::centroids),
            std::shared_ptr<const std::vector<float>>>::value,
            "cluster centroids must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceClusterResult::populations),
            std::shared_ptr<const std::vector<std::uint64_t>>>::value,
            "cluster populations must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceAnalysisPayload::interpretation),
            std::shared_ptr<const std::vector<std::string>>>::value,
            "analysis interpretation must be immutable");
        static_assert(std::is_same<
            decltype(earthscience::ScienceAnalysisPayload::limitations),
            std::shared_ptr<const std::vector<std::string>>>::value,
            "analysis limitations must be immutable");

        earthscience::ScienceArtifact artifact;
        artifact.embedding.years =
            std::make_shared<const std::vector<int>>(
                std::initializer_list<int>{2024, 2025});
        artifact.embedding.values =
            std::make_shared<const std::vector<float>>(128, 0.25f);
        artifact.embedding.mask =
            std::make_shared<const std::vector<unsigned char>>(2, 1);
        artifact.embedding.norms =
            std::make_shared<const std::vector<float>>(2, 1.0f);
        artifact.embedding.processingSteps =
            std::make_shared<const std::vector<std::string>>(
                std::initializer_list<std::string>{"align", "normalize"});
        artifact.embedding.warnings =
            std::make_shared<const std::vector<std::string>>(
                1, "coverage warning");
        auto embeddingGrid =
            std::make_shared<earthscience::ScienceGroundGrid>();
        embeddingGrid->columns = 1;
        embeddingGrid->rows = 1;
        embeddingGrid->points = {{138.70, 35.34}};
        artifact.embedding.groundGrid = embeddingGrid;

        earthscience::ScienceMetricResult metric;
        metric.metric = earthscience::ScienceMetric::CosineDistance;
        metric.value = 0.5;
        artifact.analysis.metrics =
            std::make_shared<const std::vector<
                earthscience::ScienceMetricResult>>(
                    std::initializer_list<earthscience::ScienceMetricResult>{
                        metric});
        earthscience::ScienceAnnualSeries annual;
        annual.years = artifact.embedding.years;
        annual.values =
            std::make_shared<const std::vector<double>>(
                std::initializer_list<double>{0.25, 0.5});
        annual.validity = artifact.embedding.mask;
        artifact.analysis.annualSeries =
            std::make_shared<const std::vector<
                earthscience::ScienceAnnualSeries>>(
                    std::initializer_list<earthscience::ScienceAnnualSeries>{
                        annual});
        artifact.analysis.scalarChangeRaster.values =
            std::make_shared<const std::vector<float>>(2, 0.5f);
        artifact.analysis.scalarChangeRaster.mask = artifact.embedding.mask;
        artifact.analysis.scalarChangeRaster.groundGrid = embeddingGrid;
        artifact.analysis.regionalChange.quantiles =
            std::make_shared<const std::vector<
                earthscience::ScienceQuantileResult>>(
                    std::initializer_list<
                        earthscience::ScienceQuantileResult>{{0.5, 0.5}});
        artifact.analysis.regionalChange.hotspotMask = artifact.embedding.mask;
        artifact.analysis.regionalChange.hotspotIndices =
            std::make_shared<const std::vector<std::uint64_t>>(
                std::initializer_list<std::uint64_t>{1});
        artifact.analysis.regionalChange.groundGrid = embeddingGrid;
        artifact.analysis.pca.components =
            std::make_shared<const std::vector<float>>(192, 0.0f);
        artifact.analysis.pca.scores =
            std::make_shared<const std::vector<float>>(6, 0.0f);
        artifact.analysis.pca.explainedVarianceRatios =
            std::make_shared<const std::vector<double>>(
                std::initializer_list<double>{0.6, 0.3, 0.1});
        artifact.analysis.clusters.assignments =
            std::make_shared<const std::vector<int>>(2, 1);
        artifact.analysis.clusters.centroids =
            std::make_shared<const std::vector<float>>(256, 0.0f);
        artifact.analysis.clusters.populations =
            std::make_shared<const std::vector<std::uint64_t>>(
                std::initializer_list<std::uint64_t>{1, 1, 0, 0});
        artifact.analysis.interpretation =
            std::make_shared<const std::vector<std::string>>(
                1, "change is localized");
        artifact.analysis.limitations =
            std::make_shared<const std::vector<std::string>>(
                1, "coverage is incomplete");

        const earthscience::ScienceArtifact copied = artifact;
        require(copied.embedding.years == artifact.embedding.years &&
                    copied.embedding.values == artifact.embedding.values &&
                    copied.embedding.mask == artifact.embedding.mask &&
                    copied.embedding.norms == artifact.embedding.norms &&
                    copied.embedding.processingSteps ==
                        artifact.embedding.processingSteps &&
                    copied.embedding.warnings ==
                        artifact.embedding.warnings &&
                    copied.embedding.groundGrid ==
                        artifact.embedding.groundGrid,
                "artifact copy duplicated immutable embedding backing");
        require(copied.analysis.metrics == artifact.analysis.metrics &&
                    copied.analysis.annualSeries ==
                        artifact.analysis.annualSeries &&
                    copied.analysis.annualSeries->at(0).years ==
                        artifact.analysis.annualSeries->at(0).years &&
                    copied.analysis.annualSeries->at(0).values ==
                        artifact.analysis.annualSeries->at(0).values &&
                    copied.analysis.annualSeries->at(0).validity ==
                        artifact.analysis.annualSeries->at(0).validity,
                "artifact copy duplicated immutable analysis records");
        require(copied.analysis.scalarChangeRaster.values ==
                    artifact.analysis.scalarChangeRaster.values &&
                    copied.analysis.scalarChangeRaster.mask ==
                        artifact.analysis.scalarChangeRaster.mask &&
                    copied.analysis.scalarChangeRaster.groundGrid ==
                        artifact.analysis.scalarChangeRaster.groundGrid &&
                    copied.analysis.regionalChange.quantiles ==
                        artifact.analysis.regionalChange.quantiles &&
                    copied.analysis.regionalChange.hotspotMask ==
                        artifact.analysis.regionalChange.hotspotMask &&
                    copied.analysis.regionalChange.hotspotIndices ==
                        artifact.analysis.regionalChange.hotspotIndices &&
                    copied.analysis.regionalChange.groundGrid ==
                        artifact.analysis.regionalChange.groundGrid &&
                    copied.analysis.pca.components ==
                        artifact.analysis.pca.components &&
                    copied.analysis.pca.scores == artifact.analysis.pca.scores &&
                    copied.analysis.pca.explainedVarianceRatios ==
                        artifact.analysis.pca.explainedVarianceRatios &&
                    copied.analysis.clusters.assignments ==
                        artifact.analysis.clusters.assignments &&
                    copied.analysis.clusters.centroids ==
                        artifact.analysis.clusters.centroids &&
                    copied.analysis.clusters.populations ==
                        artifact.analysis.clusters.populations &&
                    copied.analysis.interpretation ==
                        artifact.analysis.interpretation &&
                    copied.analysis.limitations ==
                        artifact.analysis.limitations,
                "artifact copy duplicated immutable analysis backing");
    }

    void testArtifactByteEstimateIncludesRegionalAnalysisBacking()
    {
        earthscience::ScienceArtifact artifact;
        const std::uint64_t before =
            earthscience::estimatedArtifactBytes(artifact);
        artifact.analysis.regionalChange.quantiles =
            std::make_shared<const std::vector<
                earthscience::ScienceQuantileResult>>(1024);
        artifact.analysis.regionalChange.hotspotMask =
            std::make_shared<const std::vector<unsigned char>>(2048, 1);
        artifact.analysis.regionalChange.hotspotIndices =
            std::make_shared<const std::vector<std::uint64_t>>(512, 1);
        const std::uint64_t after =
            earthscience::estimatedArtifactBytes(artifact);
        const std::uint64_t minimumAdded =
            1024 * sizeof(earthscience::ScienceQuantileResult) + 2048 +
            512 * sizeof(std::uint64_t);
        require(after >= before + minimumAdded,
                "artifact byte estimate omitted regional analysis backing");
    }

    void testArtifactByteEstimateRejectsOverflow()
    {
        earthscience::ScienceArtifact artifact;
        artifact.embedding.years =
            std::make_shared<const std::vector<int>>(2, 2025);
        artifact.embedding.width = std::numeric_limits<int>::max();
        artifact.embedding.height = std::numeric_limits<int>::max();

        bool rejected = false;
        try
        {
            static_cast<void>(earthscience::estimatedArtifactBytes(artifact));
        }
        catch (const std::overflow_error&)
        {
            rejected = true;
        }
        require(rejected, "artifact byte estimate accepted integer overflow");
    }

    void requireInvalidEmbedding(
        const earthscience::ScienceArtifact& artifact, const char* message)
    {
        bool rejected = false;
        try
        {
            static_cast<void>(earthscience::estimatedArtifactBytes(artifact));
        }
        catch (const std::invalid_argument&)
        {
            rejected = true;
        }
        require(rejected, message);
    }

    earthscience::ScienceArtifact makeOneCellEmbedding()
    {
        earthscience::ScienceArtifact artifact;
        artifact.embedding.years =
            std::make_shared<const std::vector<int>>(1, 2025);
        artifact.embedding.width = 1;
        artifact.embedding.height = 1;
        artifact.embedding.values =
            std::make_shared<const std::vector<float>>(64, 0.25f);
        return artifact;
    }

    void testEmbeddingShapeContractRejectsPartialPayloads()
    {
        earthscience::ScienceArtifact missingYears;
        missingYears.embedding.width = 1;
        missingYears.embedding.height = 1;
        missingYears.embedding.values =
            std::make_shared<const std::vector<float>>(64, 0.25f);
        requireInvalidEmbedding(
            missingYears, "embedding values without years were accepted");

        earthscience::ScienceArtifact zeroWidth = makeOneCellEmbedding();
        zeroWidth.embedding.width = 0;
        requireInvalidEmbedding(
            zeroWidth, "embedding payload with zero width was accepted");

        earthscience::ScienceArtifact missingValues = makeOneCellEmbedding();
        missingValues.embedding.values.reset();
        requireInvalidEmbedding(
            missingValues, "embedding dimensions without values were accepted");
    }

    void testEmbeddingShapeContractRejectsInconsistentBacking()
    {
        earthscience::ScienceArtifact wrongValues = makeOneCellEmbedding();
        wrongValues.embedding.values =
            std::make_shared<const std::vector<float>>(63, 0.25f);
        requireInvalidEmbedding(
            wrongValues, "embedding value shape did not enforce 64 components");

        earthscience::ScienceArtifact wrongMask = makeOneCellEmbedding();
        wrongMask.embedding.mask =
            std::make_shared<const std::vector<unsigned char>>(2, 1);
        requireInvalidEmbedding(
            wrongMask, "embedding mask shape was not validated");

        earthscience::ScienceArtifact wrongNorms = makeOneCellEmbedding();
        wrongNorms.embedding.norms =
            std::make_shared<const std::vector<float>>(2, 1.0f);
        requireInvalidEmbedding(
            wrongNorms, "embedding norm shape was not validated");
    }

    void testArtifactByteEstimateIncludesOwnedStrings()
    {
        earthscience::ScienceArtifact artifact;
        artifact.artifactId = std::string(4096, 'a');
        artifact.processingVersion = std::string(8192, 'p');
        artifact.warnings.push_back(std::string(16384, 'w'));
        artifact.embedding = makeOneCellEmbedding().embedding;
        artifact.embedding.processingSteps =
            std::make_shared<const std::vector<std::string>>(
                1, std::string(32768, 's'));
        artifact.analysis.limitations =
            std::make_shared<const std::vector<std::string>>(
                1, std::string(65536, 'l'));

        const std::uint64_t minimumOwnedCharacters =
            4096 + 8192 + 16384 + 32768 + 65536;
        require(earthscience::estimatedArtifactBytes(artifact) >=
                    minimumOwnedCharacters,
                "artifact byte estimate omitted owned string storage");
    }
}

int main()
{
    testStableStateNames();
    testGenericDefaultsAreUnavailableAndBounded();
    testStableAnalysisContractNames();
    testIncompleteLargeProgressNeverRoundsToComplete();
    testArtifactSharesImmutablePixelsAndExactGroundGrid();
    testArtifactSharesImmutableEmbeddingAndAnalysisBacking();
    testArtifactByteEstimateIncludesRegionalAnalysisBacking();
    testArtifactByteEstimateRejectsOverflow();
    testEmbeddingShapeContractRejectsPartialPayloads();
    testEmbeddingShapeContractRejectsInconsistentBacking();
    testArtifactByteEstimateIncludesOwnedStrings();
    std::cout << "[OK] ScienceEarth generic query type contract\n";
    return 0;
}
