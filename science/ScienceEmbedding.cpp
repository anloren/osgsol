#include "ScienceEmbedding.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace earthscience
{
namespace
{
    bool invalidNormSquared(double normSquared)
    {
        return !std::isfinite(normSquared) || normSquared <= 0.0;
    }
}

double dequantizeAlphaEarth(std::int8_t raw, bool& valid)
{
    if (raw == std::numeric_limits<std::int8_t>::min())
    {
        valid = false;
        return std::numeric_limits<double>::quiet_NaN();
    }

    valid = true;
    const int value = static_cast<int>(raw);
    const double sign = value < 0 ? -1.0 : (value > 0 ? 1.0 : 0.0);
    return sign * std::pow(std::abs(value) / 127.5, 2.0);
}

bool compareEmbeddingVectors(const float* a, const float* b,
                             ScienceVectorMetrics& output,
                             std::string& error)
{
    output = ScienceVectorMetrics{};
    error.clear();
    if (!a || !b)
    {
        error = "embedding comparison requires two vectors";
        return false;
    }

    double normSquaredA = 0.0, normSquaredB = 0.0;
    double squaredDistance = 0.0, dotProduct = 0.0;
    for (std::size_t i = 0; i < SCIENCE_EMBEDDING_COMPONENTS; ++i)
    {
        const double valueA = static_cast<double>(a[i]);
        const double valueB = static_cast<double>(b[i]);
        if (!std::isfinite(valueA) || !std::isfinite(valueB))
        {
            error = "embedding comparison requires finite components";
            return false;
        }

        const double difference = valueA - valueB;
        dotProduct += valueA * valueB;
        normSquaredA += valueA * valueA;
        normSquaredB += valueB * valueB;
        squaredDistance += difference * difference;
    }

    if (invalidNormSquared(normSquaredA) ||
        invalidNormSquared(normSquaredB))
    {
        error = "embedding comparison requires finite non-zero norms";
        return false;
    }
    if (!std::isfinite(dotProduct) || !std::isfinite(squaredDistance))
    {
        error = "embedding comparison overflowed";
        return false;
    }

    const double normA = std::sqrt(normSquaredA);
    const double normB = std::sqrt(normSquaredB);
    const double cosine = dotProduct / (normA * normB);
    if (!std::isfinite(normA) || !std::isfinite(normB) ||
        !std::isfinite(cosine))
    {
        error = "embedding comparison produced a non-finite norm";
        return false;
    }

    output.dotProduct = dotProduct;
    output.cosineSimilarity = cosine;
    output.cosineDistance = 1.0 - cosine;
    output.euclideanDistance = std::sqrt(squaredDistance);
    output.angularDistanceRadians = std::acos(std::max(-1.0,
                                                       std::min(1.0, cosine)));
    return true;
}

bool summarizeEmbeddingNorms(const float* norms,
                             const unsigned char* validMask,
                             std::size_t normCount,
                             const ScienceNormValidationRule* rule,
                             ScienceNormSummary& output,
                             std::string& error)
{
    output = ScienceNormSummary{};
    error.clear();
    if (!norms || !validMask || normCount == 0)
    {
        error = "norm summary requires samples and a validity mask";
        return false;
    }
    if (rule && (rule->sourceId.empty() || rule->ruleId.empty() ||
                 !std::isfinite(rule->minimum) ||
                 !std::isfinite(rule->maximum) ||
                 rule->minimum > rule->maximum))
    {
        error = "norm validation rule requires identity and finite ordered bounds";
        return false;
    }

    double sum = 0.0;
    std::size_t outOfRangeCount = 0;
    for (std::size_t i = 0; i < normCount; ++i)
    {
        const double norm = static_cast<double>(norms[i]);
        if (!validMask[i] || !std::isfinite(norm) || norm <= 0.0)
        {
            ++output.invalidCount;
            continue;
        }

        if (output.validCount == 0)
            output.minimum = output.maximum = norm;
        else
        {
            output.minimum = std::min(output.minimum, norm);
            output.maximum = std::max(output.maximum, norm);
        }
        sum += norm;
        ++output.validCount;
        if (rule && (norm < rule->minimum || norm > rule->maximum))
            ++outOfRangeCount;
    }

    if (output.validCount == 0)
    {
        error = "norm summary has no valid samples";
        return false;
    }
    if (!std::isfinite(sum))
    {
        error = "norm summary overflowed";
        return false;
    }

    output.mean = sum / static_cast<double>(output.validCount);
    if (rule && outOfRangeCount != 0)
    {
        std::ostringstream warning;
        warning << "warning: norm rule source=" << rule->sourceId
                << " rule=" << rule->ruleId
                << " observed " << outOfRangeCount
                << " finite sample(s) outside [" << rule->minimum
                << ", " << rule->maximum << "]"
                << " (min=" << output.minimum
                << ", mean=" << output.mean
                << ", max=" << output.maximum << ')';
        output.warning = warning.str();
    }
    return true;
}

bool aggregateEmbeddingVectors(
    const float* values, const unsigned char* validMask,
    std::size_t vectorCount,
    std::array<float, SCIENCE_EMBEDDING_COMPONENTS>& meanDirection,
    double& concentration, std::string& error)
{
    meanDirection.fill(0.0f);
    concentration = 0.0;
    error.clear();
    if (!values || !validMask || vectorCount == 0)
    {
        error = "embedding aggregation requires vectors and a validity mask";
        return false;
    }

    std::array<double, SCIENCE_EMBEDDING_COMPONENTS> componentSums{};
    std::size_t validCount = 0;
    for (std::size_t vectorIndex = 0;
         vectorIndex < vectorCount; ++vectorIndex)
    {
        if (!validMask[vectorIndex]) continue;

        const float* vector = values +
            vectorIndex * SCIENCE_EMBEDDING_COMPONENTS;
        double normSquared = 0.0;
        for (std::size_t component = 0;
             component < SCIENCE_EMBEDDING_COMPONENTS; ++component)
        {
            const double value = static_cast<double>(vector[component]);
            if (!std::isfinite(value))
            {
                normSquared = std::numeric_limits<double>::quiet_NaN();
                break;
            }
            normSquared += value * value;
        }
        if (invalidNormSquared(normSquared)) continue;

        for (std::size_t component = 0;
             component < SCIENCE_EMBEDDING_COMPONENTS; ++component)
        {
            componentSums[component] +=
                static_cast<double>(vector[component]);
        }
        ++validCount;
    }

    if (validCount == 0)
    {
        error = "embedding aggregation has no valid vectors";
        return false;
    }

    std::array<double, SCIENCE_EMBEDDING_COMPONENTS> mean{};
    double meanNormSquared = 0.0;
    for (std::size_t component = 0;
         component < SCIENCE_EMBEDDING_COMPONENTS; ++component)
    {
        mean[component] = componentSums[component] /
            static_cast<double>(validCount);
        if (!std::isfinite(mean[component]))
        {
            error = "embedding aggregation overflowed";
            return false;
        }
        meanNormSquared += mean[component] * mean[component];
    }

    if (invalidNormSquared(meanNormSquared))
    {
        error = "embedding aggregation produced no mean direction";
        return false;
    }

    concentration = std::sqrt(meanNormSquared);
    for (std::size_t component = 0;
         component < SCIENCE_EMBEDDING_COMPONENTS; ++component)
    {
        meanDirection[component] =
            static_cast<float>(mean[component] / concentration);
    }
    return true;
}
}
