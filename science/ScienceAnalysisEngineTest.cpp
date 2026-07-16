#include "ScienceAnalysisEngine.h"
#include "ScienceEmbedding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <set>
#include <string>
#include <vector>

namespace
{
    constexpr double PI = 3.14159265358979323846;

    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    bool nearlyEqual(double left, double right, double tolerance = 1.0e-6)
    {
        const double scale = std::max({1.0, std::abs(left), std::abs(right)});
        return std::abs(left - right) <= tolerance * scale;
    }

    std::shared_ptr<const earthscience::ScienceGroundGrid> makeGrid(
        int width, int height)
    {
        earthscience::ScienceGroundGrid grid;
        grid.columns = width;
        grid.rows = height;
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                grid.points.push_back({100.0 + 0.01 * x,
                                       20.0 + 0.01 * y});
            }
        }
        return std::make_shared<const earthscience::ScienceGroundGrid>(
            std::move(grid));
    }

    void setDirection(std::vector<float>& values, std::size_t vectorIndex,
                      double angle)
    {
        const std::size_t offset = vectorIndex *
            earthscience::SCIENCE_EMBEDDING_COMPONENTS;
        values[offset] = static_cast<float>(std::cos(angle));
        values[offset + 1] = static_cast<float>(std::sin(angle));
    }

    earthscience::ScienceEmbeddingPayload makePointFixture()
    {
        earthscience::ScienceEmbeddingPayload embedding;
        embedding.years = std::make_shared<const std::vector<int>>(
            std::initializer_list<int>{
                2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024, 2025});
        embedding.width = 1;
        embedding.height = 1;
        std::vector<float> values(
            9 * earthscience::SCIENCE_EMBEDDING_COMPONENTS, 0.0f);
        const double angles[] = {0.0, 0.1, 0.2, 0.0, 0.3,
                                 1.5, 1.6, 1.7, 1.8};
        for (std::size_t i = 0; i < 9; ++i)
            setDirection(values, i, angles[i]);
        embedding.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        embedding.mask = std::make_shared<const std::vector<unsigned char>>(
            std::initializer_list<unsigned char>{1, 1, 1, 0, 1, 1, 1, 1, 1});
        embedding.bounds = {100.0, 20.0, 100.0, 20.0};
        embedding.groundGrid = makeGrid(1, 1);
        embedding.actualResolutionMeters = 10.0;
        embedding.validCellCount = 8;
        embedding.noDataCellCount = 1;
        embedding.coverageFraction = 8.0 / 9.0;
        return embedding;
    }

    earthscience::ScienceEmbeddingPayload makeRegionalFixture()
    {
        earthscience::ScienceEmbeddingPayload embedding;
        embedding.years = std::make_shared<const std::vector<int>>(
            std::initializer_list<int>{2020, 2021});
        embedding.width = 4;
        embedding.height = 4;
        const std::size_t cells = 16;
        std::vector<float> values(
            2 * cells * earthscience::SCIENCE_EMBEDDING_COMPONENTS, 0.0f);
        for (std::size_t cell = 0; cell < cells; ++cell)
            setDirection(values, cell, 0.0);
        for (std::size_t cell = 0; cell < 12; ++cell)
        {
            const double score = static_cast<double>(cell) / 10.0;
            setDirection(values, cells + cell, std::acos(1.0 - score));
        }
        for (std::size_t cell = 12; cell < cells; ++cell)
            setDirection(values, cells + cell, 0.0);
        embedding.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        std::vector<unsigned char> mask(2 * cells, 1);
        for (std::size_t cell = 12; cell < cells; ++cell)
            mask[cells + cell] = 0;
        embedding.mask = std::make_shared<const std::vector<unsigned char>>(
            std::move(mask));
        embedding.bounds = {100.0, 20.0, 100.03, 20.03};
        embedding.groundGrid = makeGrid(4, 4);
        embedding.actualResolutionMeters = 30.0;
        embedding.validCellCount = 28;
        embedding.noDataCellCount = 4;
        embedding.coverageFraction = 28.0 / 32.0;
        return embedding;
    }

    const earthscience::ScienceQuantileResult* findQuantile(
        const earthscience::ScienceRegionalChangeSummary& summary,
        double probability)
    {
        if (!summary.quantiles) return nullptr;
        for (const earthscience::ScienceQuantileResult& quantile :
             *summary.quantiles)
        {
            if (nearlyEqual(quantile.probability, probability))
                return &quantile;
        }
        return nullptr;
    }

    bool containsText(
        const std::shared_ptr<const std::vector<std::string>>& values,
        const std::string& needle)
    {
        if (!values) return false;
        for (const std::string& value : *values)
        {
            if (value.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    earthscience::ScienceEmbeddingPayload makeLowRankFixture()
    {
        earthscience::ScienceEmbeddingPayload embedding;
        embedding.years =
            std::make_shared<const std::vector<int>>(1, 2024);
        embedding.width = 6;
        embedding.height = 1;
        const std::array<std::array<float, 2>, 6> samples = {{
            {{-3.0f, -1.0f}}, {{-2.0f, 1.0f}}, {{-1.0f, -1.0f}},
            {{1.0f, 1.0f}}, {{2.0f, -1.0f}}, {{3.0f, 1.0f}},
        }};
        std::vector<float> values(
            samples.size() * earthscience::SCIENCE_EMBEDDING_COMPONENTS,
            0.0f);
        for (std::size_t sample = 0; sample < samples.size(); ++sample)
        {
            const std::size_t offset =
                sample * earthscience::SCIENCE_EMBEDDING_COMPONENTS;
            values[offset] = samples[sample][0];
            values[offset + 1] = samples[sample][1];
        }
        embedding.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        embedding.mask =
            std::make_shared<const std::vector<unsigned char>>(6, 1);
        embedding.bounds = {100.0, 20.0, 100.05, 20.0};
        embedding.actualResolutionMeters = 10.0;
        embedding.validCellCount = 6;
        embedding.coverageFraction = 1.0;
        return embedding;
    }

    std::string serializeLatentResult(
        const earthscience::ScienceAnalysisPayload& result)
    {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(std::numeric_limits<double>::max_digits10)
               << static_cast<int>(result.kind) << '|';
        const auto append = [&stream](const auto& values) {
            if (!values)
            {
                stream << "null|";
                return;
            }
            for (const auto& value : *values) stream << value << ',';
            stream << '|';
        };
        append(result.pca.eigenvalues);
        append(result.pca.explainedVarianceRatios);
        append(result.pca.components);
        append(result.pca.scores);
        append(result.clusters.assignments);
        append(result.clusters.centroids);
        append(result.clusters.populations);
        append(result.clusters.concentrations);
        stream << result.clusters.converged << '|'
               << result.clusters.iterations;
        return stream.str();
    }

    std::string replayHash(const std::string& serialized)
    {
        std::uint64_t hash = 14695981039346656037ull;
        for (unsigned char byte : serialized)
        {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        std::ostringstream stream;
        stream << std::hex << std::setfill('0') << std::setw(16) << hash;
        return stream.str();
    }

    void testLocalPcaIsCenteredLowRankAndReplayStable()
    {
        const earthscience::ScienceEmbeddingPayload embedding =
            makeLowRankFixture();
        const std::vector<float> valuesBefore = *embedding.values;
        earthscience::ScienceAnalysisPayload first;
        earthscience::ScienceAnalysisPayload second;
        std::string error = "stale";

        require(earthscience::ScienceAnalysisEngine::computeLocalPca(
                    embedding, 2, first, [] { return false; }, error) &&
                    error.empty(),
                "low-rank PCA failed");
        require(first.kind ==
                    earthscience::ScienceAnalysisKind::PrincipalComponents &&
                    first.pca.inputComponentCount == 64 &&
                    first.pca.componentCount == 2 &&
                    first.pca.eigenvalues &&
                    first.pca.eigenvalues->size() == 2 &&
                    first.pca.explainedVarianceRatios &&
                    first.pca.explainedVarianceRatios->size() == 2 &&
                    first.pca.components &&
                    first.pca.components->size() == 128 &&
                    first.pca.scores && first.pca.scores->size() == 12,
                "PCA result shape is incomplete");
        require(first.pca.eigenvalues->at(0) >=
                    first.pca.eigenvalues->at(1) &&
                    nearlyEqual(first.pca.explainedVarianceRatios->at(0) +
                                    first.pca.explainedVarianceRatios->at(1),
                                1.0, 1.0e-5),
                "PCA eigenvalues or explained ratios are not ordered");
        for (int axis = 0; axis < 2; ++axis)
        {
            std::size_t largest = 0;
            for (std::size_t component = 1; component < 64; ++component)
            {
                if (std::abs(first.pca.components->at(
                                 static_cast<std::size_t>(axis) * 64 +
                                 component)) >
                    std::abs(first.pca.components->at(
                        static_cast<std::size_t>(axis) * 64 + largest)))
                    largest = component;
            }
            require(first.pca.components->at(
                        static_cast<std::size_t>(axis) * 64 + largest) > 0.0f,
                    "PCA loading sign was not canonicalized");
        }
        require(containsText(first.limitations, "physical") &&
                    containsText(first.interpretation, "local mathematical"),
                "PCA assigned physical semantics to mathematical axes");
        require(*embedding.values == valuesBefore,
                "PCA mutated immutable embedding input");

        require(earthscience::ScienceAnalysisEngine::computeLocalPca(
                    embedding, 2, second, [] { return false; }, error),
                "PCA deterministic replay failed");
        require(serializeLatentResult(first) == serializeLatentResult(second),
                "PCA deterministic replay serialization changed");
        std::cout << "[REPLAY] PCA FNV-1a "
                  << replayHash(serializeLatentResult(first)) << '\n';
    }

    earthscience::ScienceEmbeddingPayload makeDirectionalGroupsFixture()
    {
        earthscience::ScienceEmbeddingPayload embedding;
        embedding.years =
            std::make_shared<const std::vector<int>>(1, 2024);
        embedding.width = 8;
        embedding.height = 1;
        std::vector<float> values(
            8 * earthscience::SCIENCE_EMBEDDING_COMPONENTS, 0.0f);
        const std::array<double, 8> angles = {
            -0.12, -0.04, 0.04, 0.12,
            PI * 0.5 - 0.12, PI * 0.5 - 0.04,
            PI * 0.5 + 0.04, PI * 0.5 + 0.12};
        for (std::size_t sample = 0; sample < angles.size(); ++sample)
            setDirection(values, sample, angles[sample]);
        embedding.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        embedding.mask =
            std::make_shared<const std::vector<unsigned char>>(8, 1);
        embedding.bounds = {100.0, 20.0, 100.07, 20.0};
        embedding.actualResolutionMeters = 10.0;
        embedding.validCellCount = 8;
        embedding.coverageFraction = 1.0;
        return embedding;
    }

    bool centroidLexicographicallyLessOrEqual(
        const std::vector<float>& centroids, int left, int right)
    {
        for (int component = 0;
             component < earthscience::SCIENCE_EMBEDDING_COMPONENTS;
             ++component)
        {
            const float a = centroids[static_cast<std::size_t>(left) * 64 +
                                      static_cast<std::size_t>(component)];
            const float b = centroids[static_cast<std::size_t>(right) * 64 +
                                      static_cast<std::size_t>(component)];
            if (a < b) return true;
            if (a > b) return false;
        }
        return true;
    }

    void testSphericalClustersAreOrderedConvergedAndReplayStable()
    {
        const earthscience::ScienceEmbeddingPayload embedding =
            makeDirectionalGroupsFixture();
        const std::vector<float> valuesBefore = *embedding.values;
        earthscience::ScienceAnalysisPayload first;
        earthscience::ScienceAnalysisPayload second;
        std::string error;

        require(earthscience::ScienceAnalysisEngine::
                    computeSphericalClusters(
                        embedding, 2, first, [] { return false; }, error),
                "spherical clustering failed");
        require(error.empty() &&
                    first.kind ==
                        earthscience::ScienceAnalysisKind::SphericalClusters &&
                    first.clusters.metric ==
                        earthscience::ScienceMetric::CosineDistance &&
                    first.clusters.clusterCount == 2 &&
                    first.clusters.assignments &&
                    first.clusters.assignments->size() == 8 &&
                    first.clusters.centroids &&
                    first.clusters.centroids->size() == 128 &&
                    first.clusters.populations &&
                    first.clusters.populations->size() == 2 &&
                    first.clusters.concentrations &&
                    first.clusters.concentrations->size() == 2 &&
                    first.clusters.converged &&
                    first.clusters.iterations >= 1 &&
                    first.clusters.iterations <= 100,
                "cluster diagnostics are incomplete");
        require(first.clusters.populations->at(0) == 4 &&
                    first.clusters.populations->at(1) == 4,
                "directional groups did not form two non-empty clusters");
        require(centroidLexicographicallyLessOrEqual(
                    *first.clusters.centroids, 0, 1),
                "final centroids were not lexicographically ordered");
        for (double concentration : *first.clusters.concentrations)
            require(concentration > 0.99 && concentration <= 1.0,
                    "cluster concentration is outside its directional range");
        require(containsText(first.limitations, "physical") &&
                    containsText(first.interpretation, "direction"),
                "clusters were given unsupported semantic class names");
        require(*embedding.values == valuesBefore,
                "clustering mutated immutable embedding input");

        require(earthscience::ScienceAnalysisEngine::
                    computeSphericalClusters(
                        embedding, 2, second, [] { return false; }, error),
                "cluster deterministic replay failed");
        require(serializeLatentResult(first) == serializeLatentResult(second),
                "cluster ids or serialization changed on replay");
        std::cout << "[REPLAY] spherical clusters FNV-1a "
                  << replayHash(serializeLatentResult(first)) << '\n';
    }

    void testSphericalClustersRecoverEmptyGroupsDeterministically()
    {
        earthscience::ScienceEmbeddingPayload embedding =
            makeDirectionalGroupsFixture();
        std::vector<float> identical(
            embedding.values->size(), 0.0f);
        for (std::size_t sample = 0; sample < 8; ++sample)
            identical[sample * earthscience::SCIENCE_EMBEDDING_COMPONENTS] =
                1.0f;
        embedding.values = std::make_shared<const std::vector<float>>(
            std::move(identical));
        earthscience::ScienceAnalysisPayload first;
        earthscience::ScienceAnalysisPayload second;
        std::string error;

        require(earthscience::ScienceAnalysisEngine::
                    computeSphericalClusters(
                        embedding, 3, first, [] { return false; }, error) &&
                    earthscience::ScienceAnalysisEngine::
                    computeSphericalClusters(
                        embedding, 3, second, [] { return false; }, error),
                "deterministic empty-cluster recovery failed");
        require(first.clusters.populations &&
                    first.clusters.populations->size() == 3 &&
                    std::all_of(first.clusters.populations->begin(),
                                first.clusters.populations->end(),
                                [](std::uint64_t population) {
                                    return population > 0;
                                }) &&
                    first.clusters.converged,
                "empty-cluster recovery left an empty or unstable group");
        require(serializeLatentResult(first) == serializeLatentResult(second),
                "empty-cluster recovery changed ids on replay");
    }

    void testLatentAnalysisRejectsInvalidSamplesAndCancellationPrecisely()
    {
        earthscience::ScienceAnalysisPayload output;
        output.kind = earthscience::ScienceAnalysisKind::PointSeries;
        std::string error;
        earthscience::ScienceEmbeddingPayload one = makeLowRankFixture();
        one.mask = std::make_shared<const std::vector<unsigned char>>(
            std::initializer_list<unsigned char>{1, 0, 0, 0, 0, 0});
        require(!earthscience::ScienceAnalysisEngine::computeLocalPca(
                    one, 2, output, [] { return false; }, error) &&
                    error.find("at least two valid samples") !=
                        std::string::npos,
                "PCA accepted too few valid samples");

        earthscience::ScienceEmbeddingPayload underdetermined =
            makeLowRankFixture();
        underdetermined.mask =
            std::make_shared<const std::vector<unsigned char>>(
                std::initializer_list<unsigned char>{1, 1, 0, 0, 0, 0});
        require(!earthscience::ScienceAnalysisEngine::computeLocalPca(
                    underdetermined, 2, output,
                    [] { return false; }, error) &&
                    error.find("more valid samples") != std::string::npos,
                "PCA accepted more axes than centered samples support");

        earthscience::ScienceEmbeddingPayload constant = makeLowRankFixture();
        constant.values = std::make_shared<const std::vector<float>>(
            constant.values->size(), 0.25f);
        require(!earthscience::ScienceAnalysisEngine::computeLocalPca(
                    constant, 2, output, [] { return false; }, error) &&
                    error.find("zero variance") != std::string::npos,
                "PCA accepted zero-variance samples");

        earthscience::ScienceEmbeddingPayload nonFinite = makeLowRankFixture();
        std::vector<float> values = *nonFinite.values;
        values[2] = std::numeric_limits<float>::infinity();
        nonFinite.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        require(!earthscience::ScienceAnalysisEngine::computeLocalPca(
                    nonFinite, 2, output, [] { return false; }, error) &&
                    error.find("finite") != std::string::npos,
                "PCA accepted a non-finite valid sample");

        require(!earthscience::ScienceAnalysisEngine::
                    computeSphericalClusters(
                        makeDirectionalGroupsFixture(), 1, output,
                        [] { return false; }, error) &&
                    error.find("2 through 8") != std::string::npos,
                "spherical clustering accepted k outside 2 through 8");
        earthscience::ScienceEmbeddingPayload zeroNorm =
            makeDirectionalGroupsFixture();
        zeroNorm.values = std::make_shared<const std::vector<float>>(
            zeroNorm.values->size(), 0.0f);
        require(!earthscience::ScienceAnalysisEngine::
                    computeSphericalClusters(
                        zeroNorm, 2, output, [] { return false; }, error) &&
                    error.find("positive-norm") != std::string::npos,
                "spherical clustering accepted zero-norm directions");
        require(!earthscience::ScienceAnalysisEngine::computeLocalPca(
                    makeLowRankFixture(), 2, output,
                    [] { return true; }, error) &&
                    error.find("cancelled") != std::string::npos &&
                    output.kind ==
                        earthscience::ScienceAnalysisKind::PointSeries,
                "cancelled PCA published output or returned an imprecise error");
    }

    void testPointSeriesPreservesGapAndFindsLargestConsecutiveChange()
    {
        const earthscience::ScienceEmbeddingPayload embedding =
            makePointFixture();
        earthscience::ScienceAnalysisOptions options;
        options.kind = earthscience::ScienceAnalysisKind::PointSeries;
        options.baselineYear = 2018;
        options.metrics = {earthscience::ScienceMetric::CosineDistance,
                           earthscience::ScienceMetric::AngularDistance};
        std::string error = "stale";

        const auto result =
            earthscience::ScienceAnalysisEngine::analyzePointSeries(
                embedding, options, [] { return false; }, error);

        require(result && error.empty(), "point series analysis failed");
        require(result->kind == earthscience::ScienceAnalysisKind::PointSeries,
                "point series kind changed");
        require(result->annualSeries && result->annualSeries->size() == 2,
                "selected-baseline metric series are incomplete");
        for (const earthscience::ScienceAnnualSeries& series :
             *result->annualSeries)
        {
            require(series.years && series.values &&
                        series.validity && series.years->size() == 9 &&
                        series.values->size() == 9 &&
                        series.validity->size() == 9,
                    "point series dropped a requested year");
            require(series.years->at(3) == 2020 &&
                        series.validity->at(3) == 0 &&
                        std::isnan(series.values->at(3)),
                    "missing point year was interpolated or removed");
            require(series.validity->at(2) == 1 &&
                        series.validity->at(4) == 1,
                    "point gap validity changed adjacent years");
            require(nearlyEqual(series.values->at(1), 0.0),
                    "selected baseline did not compare to itself");
        }
        require(result->metrics && result->metrics->size() == 14,
                "consecutive-valid metric records are incomplete");

        const earthscience::ScienceMetricResult* largest = nullptr;
        for (const earthscience::ScienceMetricResult& metric : *result->metrics)
        {
            if (metric.metric != earthscience::ScienceMetric::AngularDistance)
                continue;
            if (!largest || metric.value > largest->value) largest = &metric;
        }
        require(largest && largest->baselineYear == 2021 &&
                    largest->comparisonYear == 2022 &&
                    nearlyEqual(largest->value, 1.2),
                "largest consecutive angular interval changed");
        require(containsText(result->interpretation, "embedding space"),
                "point result omitted its embedding-space scope");
        require(containsText(result->limitations, "No interpolation"),
                "point result did not state its missing-year policy");
    }

    void testRegionalChangeReportsExactOverlapDistributionAndHotspots()
    {
        const earthscience::ScienceEmbeddingPayload embedding =
            makeRegionalFixture();
        earthscience::ScienceAnalysisOptions options;
        options.kind = earthscience::ScienceAnalysisKind::RegionalChange;
        options.baselineYear = 2020;
        options.comparisonYear = 2021;
        std::string error;

        const auto result =
            earthscience::ScienceAnalysisEngine::analyzeRegionalChange(
                embedding, options, [] { return false; }, error);

        require(result && error.empty(), "regional change analysis failed");
        require(result->kind ==
                    earthscience::ScienceAnalysisKind::RegionalChange,
                "regional change kind changed");
        const earthscience::ScienceRegionalChangeSummary& summary =
            result->regionalChange;
        require(summary.metric == earthscience::ScienceMetric::CosineDistance,
                "regional default is not cosine distance");
        require(summary.baselineYear == 2020 &&
                    summary.comparisonYear == 2021,
                "regional comparison years changed");
        require(summary.totalCellCount == 16 &&
                    summary.validOverlapCount == 12 &&
                    summary.noDataCellCount == 4 &&
                    nearlyEqual(summary.coverageFraction, 0.75),
                "regional overlap accounting changed");
        require(nearlyEqual(summary.mean, 0.55) &&
                    nearlyEqual(summary.median, 0.55) &&
                    nearlyEqual(summary.standardDeviation,
                                std::sqrt(0.11916666666666667)) &&
                    nearlyEqual(summary.minimum, 0.0) &&
                    nearlyEqual(summary.maximum, 1.1),
                "regional distribution statistics changed");
        require(findQuantile(summary, 0.25) &&
                    nearlyEqual(findQuantile(summary, 0.25)->value, 0.275) &&
                    findQuantile(summary, 0.50) &&
                    nearlyEqual(findQuantile(summary, 0.50)->value, 0.55) &&
                    findQuantile(summary, 0.75) &&
                    nearlyEqual(findQuantile(summary, 0.75)->value, 0.825) &&
                    findQuantile(summary, 0.90) &&
                    nearlyEqual(findQuantile(summary, 0.90)->value, 0.99),
                "deterministic linear quantiles changed");
        require(nearlyEqual(summary.hotspotQuantile, 0.90) &&
                    nearlyEqual(summary.hotspotThreshold, 0.99),
                "default relative hotspot threshold changed");
        require(summary.hotspotMask && summary.hotspotMask->size() == 16 &&
                    summary.hotspotIndices &&
                    summary.hotspotIndices->size() == 2 &&
                    summary.hotspotIndices->at(0) == 10 &&
                    summary.hotspotIndices->at(1) == 11,
                "relative hotspot membership changed");
        require(summary.bounds.west == embedding.bounds.west &&
                    summary.bounds.north == embedding.bounds.north &&
                    summary.groundGrid == embedding.groundGrid &&
                    nearlyEqual(summary.actualResolutionMeters, 30.0),
                "regional footprint or exact ground grid changed");

        const earthscience::ScienceScalarChangeRaster& raster =
            result->scalarChangeRaster;
        require(raster.values && raster.values->size() == 16 &&
                    raster.mask && raster.mask->size() == 16 &&
                    raster.validCellCount == 12 &&
                    raster.noDataCellCount == 4 &&
                    nearlyEqual(raster.coverageFraction, 0.75) &&
                    raster.groundGrid == embedding.groundGrid,
                "regional scalar raster metadata changed");
        require(raster.mask->at(11) == 1 && raster.mask->at(12) == 0 &&
                    nearlyEqual(raster.values->at(11), 1.1) &&
                    std::isnan(raster.values->at(12)),
                "regional scalar raster ignored the both-valid mask");
        require(containsText(result->interpretation, "relative") &&
                    containsText(result->limitations,
                                 "not a physical-change threshold"),
                "relative hotspot semantics are not explicit");
    }

    void testRegionalValidationRejectsGridOverlapAndFiniteFailures()
    {
        earthscience::ScienceAnalysisOptions options;
        options.kind = earthscience::ScienceAnalysisKind::RegionalChange;
        options.baselineYear = 2020;
        options.comparisonYear = 2021;
        std::string error;

        earthscience::ScienceEmbeddingPayload wrongGrid =
            makeRegionalFixture();
        earthscience::ScienceGroundGrid grid = *wrongGrid.groundGrid;
        grid.columns = 3;
        wrongGrid.groundGrid =
            std::make_shared<const earthscience::ScienceGroundGrid>(grid);
        require(!earthscience::ScienceAnalysisEngine::analyzeRegionalChange(
                    wrongGrid, options, [] { return false; }, error) &&
                    error.find("ground grid") != std::string::npos,
                "inconsistent regional ground grid was accepted");

        earthscience::ScienceEmbeddingPayload noOverlap =
            makeRegionalFixture();
        noOverlap.mask =
            std::make_shared<const std::vector<unsigned char>>(32, 0);
        require(!earthscience::ScienceAnalysisEngine::analyzeRegionalChange(
                    noOverlap, options, [] { return false; }, error) &&
                    error.find("overlap") != std::string::npos,
                "zero valid regional overlap was accepted");

        earthscience::ScienceEmbeddingPayload nonFinite =
            makeRegionalFixture();
        std::vector<float> values = *nonFinite.values;
        values[16 * earthscience::SCIENCE_EMBEDDING_COMPONENTS] =
            std::numeric_limits<float>::infinity();
        nonFinite.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        require(!earthscience::ScienceAnalysisEngine::analyzeRegionalChange(
                    nonFinite, options, [] { return false; }, error) &&
                    error.find("finite") != std::string::npos,
                "non-finite regional change result was accepted");
    }

    void testRegionalHotspotQuantileIncludesAllExactTies()
    {
        earthscience::ScienceEmbeddingPayload embedding =
            makeRegionalFixture();
        std::vector<float> values = *embedding.values;
        const double tiedScore = 0.345678912;
        for (std::size_t cell = 0; cell < 16; ++cell)
        {
            setDirection(values, 16 + cell,
                         std::acos(1.0 - tiedScore));
        }
        embedding.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        embedding.mask =
            std::make_shared<const std::vector<unsigned char>>(32, 1);
        earthscience::ScienceAnalysisOptions options;
        options.kind = earthscience::ScienceAnalysisKind::RegionalChange;
        options.baselineYear = 2020;
        options.comparisonYear = 2021;
        std::string error;

        const auto result =
            earthscience::ScienceAnalysisEngine::analyzeRegionalChange(
                embedding, options, [] { return false; }, error);

        require(result && result->regionalChange.hotspotIndices &&
                    result->regionalChange.hotspotIndices->size() == 16,
                "quantile membership dropped exact threshold ties");
    }

    earthscience::ScienceEmbeddingPayload makeAnnualSlice(int year)
    {
        earthscience::ScienceEmbeddingPayload embedding;
        embedding.years =
            std::make_shared<const std::vector<int>>(1, year);
        embedding.width = 2;
        embedding.height = 2;
        std::vector<float> values(
            4 * earthscience::SCIENCE_EMBEDDING_COMPONENTS, 0.0f);
        const double angle = 0.1 * (year - 2017);
        for (std::size_t cell = 0; cell < 4; ++cell)
            setDirection(values, cell, angle);
        embedding.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        embedding.mask =
            std::make_shared<const std::vector<unsigned char>>(4, 1);
        embedding.bounds = {100.0, 20.0, 100.01, 20.01};
        embedding.groundGrid = makeGrid(2, 2);
        embedding.actualResolutionMeters = 30.0;
        embedding.validCellCount = 4;
        embedding.coverageFraction = 1.0;
        return embedding;
    }

    void testAnnualRegionalSummariesLoadOnceAscendingAndReleaseSlices()
    {
        std::vector<int> requested = {
            2025, 2017, 2024, 2018, 2023, 2019, 2022, 2020, 2021};
        std::vector<int> loads;
        std::weak_ptr<const earthscience::ScienceEmbeddingPayload> previous;
        bool retainedPrevious = false;
        auto loader = [&](int year, std::string&) {
            if (!previous.expired()) retainedPrevious = true;
            auto slice =
                std::make_shared<const earthscience::ScienceEmbeddingPayload>(
                    makeAnnualSlice(year));
            previous = slice;
            loads.push_back(year);
            return slice;
        };
        earthscience::ScienceAnalysisPayload output;
        std::string error;

        require(earthscience::ScienceAnalysisEngine::
                    analyzeRegionalAnnualSummaries(
                        requested, loader, output, [] { return false; }, error),
                "annual regional summaries failed");
        require(error.empty(), "annual summaries left a stale error");
        require(!retainedPrevious && previous.expired(),
                "annual loader retained a non-comparison grid across loads");
        require(loads == std::vector<int>({
                    2017, 2018, 2019, 2020, 2021,
                    2022, 2023, 2024, 2025}),
                "annual slices were not loaded once in ascending order");
        require(output.kind ==
                    earthscience::ScienceAnalysisKind::RegionalChange &&
                    output.annualSeries && output.annualSeries->size() == 1 &&
                    output.annualSeries->front().years &&
                    output.annualSeries->front().years->size() == 9 &&
                    output.annualSeries->front().values &&
                    output.annualSeries->front().values->size() == 9 &&
                    output.annualSeries->front().validity &&
                    output.annualSeries->front().validity->size() == 9,
                "annual summary did not retain one value per valid year");
        require(nearlyEqual(output.annualSeries->front().values->front(), 0.0) &&
                    nearlyEqual(output.annualSeries->front().values->back(),
                                1.0 - std::cos(0.8)),
                "annual aggregate comparison changed");
    }

    void testAnnualRegionalSummariesUseFirstRequestedValidYearAsBaseline()
    {
        auto loader = [](int year, std::string&) {
            earthscience::ScienceEmbeddingPayload slice = makeAnnualSlice(year);
            if (year < 2019)
            {
                slice.mask =
                    std::make_shared<const std::vector<unsigned char>>(4, 0);
                slice.validCellCount = 0;
                slice.noDataCellCount = 4;
                slice.coverageFraction = 0.0;
            }
            return std::make_shared<const earthscience::ScienceEmbeddingPayload>(
                std::move(slice));
        };
        earthscience::ScienceAnalysisPayload output;
        std::string error;

        require(earthscience::ScienceAnalysisEngine::
                    analyzeRegionalAnnualSummaries(
                        {2020, 2017, 2019, 2018}, loader, output,
                        [] { return false; }, error),
                "annual summaries rejected leading missing years");
        const earthscience::ScienceAnnualSeries& series =
            output.annualSeries->front();
        require(*series.years == std::vector<int>({2017, 2018, 2019, 2020}) &&
                    *series.validity ==
                        std::vector<unsigned char>({0, 0, 1, 1}) &&
                    std::isnan(series.values->at(0)) &&
                    std::isnan(series.values->at(1)) &&
                    nearlyEqual(series.values->at(2), 0.0) &&
                    nearlyEqual(series.values->at(3),
                                1.0 - std::cos(0.1)),
                "first requested valid annual slice was not the baseline");
        require(containsText(output.limitations,
                             "first requested valid year"),
                "annual baseline semantics were not documented");
    }

    void testAnnualCancellationStopsBeforeNextLoadAndPublishesNothing()
    {
        int loadCount = 0;
        std::weak_ptr<const earthscience::ScienceEmbeddingPayload> loaded;
        auto loader = [&](int year, std::string&) {
            ++loadCount;
            auto slice =
                std::make_shared<const earthscience::ScienceEmbeddingPayload>(
                    makeAnnualSlice(year));
            loaded = slice;
            return slice;
        };
        earthscience::ScienceAnalysisPayload output;
        output.kind = earthscience::ScienceAnalysisKind::PointSeries;
        std::string error;

        require(!earthscience::ScienceAnalysisEngine::
                    analyzeRegionalAnnualSummaries(
                        {2017, 2018, 2019}, loader, output,
                        [&] { return loadCount == 1; }, error),
                "cancelled annual summaries reported success");
        require(loadCount == 1 && loaded.expired(),
                "cancellation loaded another year or retained the active slice");
        require(output.kind == earthscience::ScienceAnalysisKind::PointSeries &&
                    !output.annualSeries,
                "cancellation published a partial annual result");
        require(error.find("cancelled") != std::string::npos,
                "annual cancellation returned an imprecise error");
    }

    void testMaterializedChangeRasterUsesOrderedColorsAndTransparentNoData()
    {
        const earthscience::ScienceEmbeddingPayload embedding =
            makeRegionalFixture();
        earthscience::ScienceAnalysisOptions options;
        options.kind = earthscience::ScienceAnalysisKind::RegionalChange;
        options.baselineYear = 2020;
        options.comparisonYear = 2021;
        std::string error;
        const auto analysis =
            earthscience::ScienceAnalysisEngine::analyzeRegionalChange(
                embedding, options, [] { return false; }, error);
        require(static_cast<bool>(analysis),
                "change raster setup analysis failed");

        const earthscience::ScienceRasterPayload raster =
            earthscience::ScienceAnalysisEngine::materializeChangeRaster(
                *analysis, embedding, error);
        require(error.empty() && raster.rgba && raster.rgba->size() == 64,
                "change raster materialization failed");
        require(raster.width == 4 && raster.height == 4 &&
                    raster.bounds.west == embedding.bounds.west &&
                    raster.groundGrid == embedding.groundGrid,
                "materialized raster lost the exact footprint or grid");
        for (std::size_t cell = 0; cell < 12; ++cell)
            require(raster.rgba->at(cell * 4 + 3) == 166,
                    "valid change raster alpha changed");
        for (std::size_t cell = 12; cell < 16; ++cell)
            require(raster.rgba->at(cell * 4 + 3) == 0,
                    "NoData change raster is not transparent");
        const int lowBrightness = raster.rgba->at(0) + raster.rgba->at(1) +
            raster.rgba->at(2);
        const int highBrightness = raster.rgba->at(44) + raster.rgba->at(45) +
            raster.rgba->at(46);
        require(highBrightness > lowBrightness,
                "change colors are not perceptually ordered low to high");
    }
}

int main()
{
    testLocalPcaIsCenteredLowRankAndReplayStable();
    testSphericalClustersAreOrderedConvergedAndReplayStable();
    testSphericalClustersRecoverEmptyGroupsDeterministically();
    testLatentAnalysisRejectsInvalidSamplesAndCancellationPrecisely();
    testPointSeriesPreservesGapAndFindsLargestConsecutiveChange();
    testRegionalChangeReportsExactOverlapDistributionAndHotspots();
    testRegionalValidationRejectsGridOverlapAndFiniteFailures();
    testRegionalHotspotQuantileIncludesAllExactTies();
    testAnnualRegionalSummariesLoadOnceAscendingAndReleaseSlices();
    testAnnualRegionalSummariesUseFirstRequestedValidYearAsBaseline();
    testAnnualCancellationStopsBeforeNextLoadAndPublishesNothing();
    testMaterializedChangeRasterUsesOrderedColorsAndTransparentNoData();
    std::cout << "Science analysis engine tests passed\n";
    return 0;
}
