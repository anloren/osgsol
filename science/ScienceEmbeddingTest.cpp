#include "ScienceEmbedding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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

    bool nearlyEqual(double left, double right, double tolerance = 1.0e-6)
    {
        const double scale = std::max({1.0, std::abs(left), std::abs(right)});
        return std::abs(left - right) <= tolerance * scale;
    }

    using Embedding = std::array<float,
        earthscience::SCIENCE_EMBEDDING_COMPONENTS>;

    Embedding basisVector(std::size_t component, float magnitude = 1.0f)
    {
        Embedding vector{};
        vector[component] = magnitude;
        return vector;
    }

    void testAlphaEarthDequantizationIsExactAndPreservesNoData()
    {
        bool valid = false;
        require(nearlyEqual(earthscience::dequantizeAlphaEarth(0, valid), 0.0) && valid,
                "zero is valid");
        require(nearlyEqual(earthscience::dequantizeAlphaEarth(127, valid),
                            std::pow(127.0 / 127.5, 2.0)) && valid,
                "positive dequantization changed");
        require(nearlyEqual(earthscience::dequantizeAlphaEarth(-127, valid),
                            -std::pow(127.0 / 127.5, 2.0)) && valid,
                "negative dequantization changed");

        const double noData = earthscience::dequantizeAlphaEarth(-128, valid);
        require(!valid, "-128 must remain NoData");
        require(!std::isfinite(noData), "NoData must never become numeric zero");
    }

    void testEmbeddingMetricsForCanonicalDirections()
    {
        const Embedding identicalA = basisVector(0);
        const Embedding identicalB = basisVector(0);
        const Embedding orthogonal = basisVector(1);
        const Embedding opposite = basisVector(0, -1.0f);
        earthscience::ScienceVectorMetrics metrics;
        std::string error;

        require(earthscience::compareEmbeddingVectors(
                    identicalA.data(), identicalB.data(), metrics, error),
                "identical unit vectors were rejected");
        require(error.empty(), "successful comparison left an error");
        require(nearlyEqual(metrics.dotProduct, 1.0) &&
                    nearlyEqual(metrics.cosineSimilarity, 1.0) &&
                    nearlyEqual(metrics.cosineDistance, 0.0) &&
                    nearlyEqual(metrics.euclideanDistance, 0.0) &&
                    nearlyEqual(metrics.angularDistanceRadians, 0.0),
                "identical vector metrics changed");

        require(earthscience::compareEmbeddingVectors(
                    identicalA.data(), orthogonal.data(), metrics, error),
                "orthogonal unit vectors were rejected");
        require(error.empty(), "orthogonal comparison left an error");
        require(nearlyEqual(metrics.dotProduct, 0.0) &&
                    nearlyEqual(metrics.cosineSimilarity, 0.0) &&
                    nearlyEqual(metrics.cosineDistance, 1.0) &&
                    nearlyEqual(metrics.euclideanDistance, std::sqrt(2.0)) &&
                    nearlyEqual(metrics.angularDistanceRadians,
                                std::acos(-1.0) / 2.0),
                "orthogonal vector metrics changed");

        require(earthscience::compareEmbeddingVectors(
                    identicalA.data(), opposite.data(), metrics, error),
                "opposite unit vectors were rejected");
        require(error.empty(), "opposite comparison left an error");
        require(nearlyEqual(metrics.dotProduct, -1.0) &&
                    nearlyEqual(metrics.cosineSimilarity, -1.0) &&
                    nearlyEqual(metrics.cosineDistance, 2.0) &&
                    nearlyEqual(metrics.euclideanDistance, 2.0) &&
                    nearlyEqual(metrics.angularDistanceRadians, std::acos(-1.0)),
                "opposite vector metrics changed");
    }

    void testScaledMetricsUseObservedNormsWithoutImplicitValidation()
    {
        const Embedding unit = basisVector(7);
        const Embedding scaled = basisVector(7, 2.0f);
        const Embedding unitBefore = unit;
        const Embedding scaledBefore = scaled;
        earthscience::ScienceVectorMetrics metrics;
        std::string error = "stale";

        require(earthscience::compareEmbeddingVectors(
                    unit.data(), scaled.data(), metrics, error),
                "finite scaled vectors were rejected");
        require(error.empty(), "scaled comparison invented a validation rule");
        require(nearlyEqual(metrics.dotProduct, 2.0) &&
                    nearlyEqual(metrics.cosineSimilarity, 1.0) &&
                    nearlyEqual(metrics.cosineDistance, 0.0) &&
                    nearlyEqual(metrics.euclideanDistance, 1.0) &&
                    nearlyEqual(metrics.angularDistanceRadians, 0.0),
                "scaled metrics silently normalized their inputs");
        require(unit == unitBefore && scaled == scaledBefore,
                "metric comparison mutated an input vector");
    }

    void testEmbeddingMetricsRejectInvalidNorms()
    {
        const Embedding unit = basisVector(0);
        const Embedding zero{};
        Embedding nonFinite = basisVector(0);
        nonFinite[5] = std::numeric_limits<float>::infinity();
        earthscience::ScienceVectorMetrics metrics;
        std::string error;

        require(!earthscience::compareEmbeddingVectors(
                    zero.data(), unit.data(), metrics, error) && !error.empty(),
                "zero-norm vectors must be invalid");
        require(!earthscience::compareEmbeddingVectors(
                    nonFinite.data(), unit.data(), metrics, error) && !error.empty(),
                "non-finite vectors must be invalid");
    }

    void testNormSummaryWithoutRuleNeverInventsAWarning()
    {
        const float norms[] = {
            0.9f, 1.1f, 0.0f,
            std::numeric_limits<float>::quiet_NaN(), 99.0f,
        };
        const unsigned char validMask[] = {1, 1, 1, 1, 0};
        earthscience::ScienceNormSummary summary;
        std::string error = "stale";

        require(earthscience::summarizeEmbeddingNorms(
                    norms, validMask, 5, nullptr, summary, error),
                "finite valid norms could not be summarized");
        require(error.empty(), "successful norm summary left an error");
        require(summary.warning.empty(),
                "norm summary invented a warning without a validation rule");
        require(summary.validCount == 2 && summary.invalidCount == 3 &&
                    nearlyEqual(summary.minimum, 0.9) &&
                    nearlyEqual(summary.maximum, 1.1) &&
                    nearlyEqual(summary.mean, 1.0),
                "norm summary counts or finite statistics changed");
    }

    void testExplicitNormRuleWarnsWithoutRejectingFiniteSamples()
    {
        const float norms[] = {0.8f, 1.0f, 1.2f};
        const unsigned char validMask[] = {1, 1, 1};
        earthscience::ScienceNormValidationRule rule;
        rule.sourceId = "alphaearth-example";
        rule.ruleId = "documented-norm-range";
        rule.minimum = 0.95;
        rule.maximum = 1.05;
        earthscience::ScienceNormSummary summary;
        std::string error;

        require(earthscience::summarizeEmbeddingNorms(
                    norms, validMask, 3, &rule, summary, error),
                "unexpected finite norm distribution was rejected");
        require(error.empty(), "warning was overloaded into the error channel");
        require(summary.validCount == 3 && summary.invalidCount == 0,
                "out-of-rule finite norms were treated as invalid");
        require(summary.warning.find(rule.sourceId) != std::string::npos &&
                    summary.warning.find(rule.ruleId) != std::string::npos &&
                    summary.warning.find("warning:") != std::string::npos,
                "explicit norm warning omitted its source or rule identity");
    }

    void testAggregationMeansValidVectorsBeforeNormalizingDirection()
    {
        std::vector<float> values(
            2 * earthscience::SCIENCE_EMBEDDING_COMPONENTS, 0.0f);
        values[0] = 1.0f;
        values[earthscience::SCIENCE_EMBEDDING_COMPONENTS + 1] = 1.0f;
        const unsigned char validMask[] = {1, 1};
        Embedding meanDirection{};
        double concentration = 0.0;
        std::string error;

        require(earthscience::aggregateEmbeddingVectors(
                    values.data(), validMask, 2, meanDirection,
                    concentration, error),
                "valid orthogonal vectors could not be aggregated");
        require(error.empty(), "successful aggregation left an error");
        require(nearlyEqual(concentration, std::sqrt(0.5)) &&
                    nearlyEqual(meanDirection[0], std::sqrt(0.5)) &&
                    nearlyEqual(meanDirection[1], std::sqrt(0.5)),
                "aggregation did not report the mean-vector concentration");
    }

    void testAggregationSkipsInvalidVectorsWithoutNormalizingInputs()
    {
        std::vector<float> values(
            4 * earthscience::SCIENCE_EMBEDDING_COMPONENTS, 0.0f);
        values[0] = 2.0f;
        values[earthscience::SCIENCE_EMBEDDING_COMPONENTS] = 4.0f;
        values[2 * earthscience::SCIENCE_EMBEDDING_COMPONENTS + 1] = 100.0f;
        const std::vector<float> valuesBefore = values;
        const unsigned char validMask[] = {1, 1, 0, 1};
        Embedding meanDirection{};
        double concentration = 0.0;
        std::string error = "stale";

        require(earthscience::aggregateEmbeddingVectors(
                    values.data(), validMask, 4, meanDirection,
                    concentration, error),
                "valid vectors were lost while skipping invalid vectors");
        require(error.empty(), "successful scaled aggregation left an error");
        require(nearlyEqual(concentration, 3.0) &&
                    nearlyEqual(meanDirection[0], 1.0) &&
                    nearlyEqual(meanDirection[1], 0.0),
                "aggregation normalized vectors before computing their mean");
        require(values == valuesBefore, "aggregation mutated its input vectors");
    }

    void testAggregationFailsWhenNoValidVectorRemains()
    {
        std::vector<float> values(
            2 * earthscience::SCIENCE_EMBEDDING_COMPONENTS, 0.0f);
        values[earthscience::SCIENCE_EMBEDDING_COMPONENTS] =
            std::numeric_limits<float>::quiet_NaN();
        const unsigned char validMask[] = {1, 1};
        Embedding meanDirection{};
        double concentration = 0.0;
        std::string error;

        require(!earthscience::aggregateEmbeddingVectors(
                    values.data(), validMask, 2, meanDirection,
                    concentration, error) && !error.empty(),
                "aggregation must fail when no valid vector remains");
    }
}

int main()
{
    static_assert(earthscience::SCIENCE_EMBEDDING_COMPONENTS == 64,
                  "embedding primitives must remain 64-dimensional");
    testAlphaEarthDequantizationIsExactAndPreservesNoData();
    testEmbeddingMetricsForCanonicalDirections();
    testScaledMetricsUseObservedNormsWithoutImplicitValidation();
    testEmbeddingMetricsRejectInvalidNorms();
    testNormSummaryWithoutRuleNeverInventsAWarning();
    testExplicitNormRuleWarnsWithoutRejectingFiniteSamples();
    testAggregationMeansValidVectorsBeforeNormalizingDirection();
    testAggregationSkipsInvalidVectorsWithoutNormalizingInputs();
    testAggregationFailsWhenNoValidVectorRemains();
    std::cout << "Science embedding tests passed\n";
    return 0;
}
