#ifndef OSGSOL_SCIENCE_EMBEDDING_H
#define OSGSOL_SCIENCE_EMBEDDING_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace earthscience
{
    constexpr std::size_t SCIENCE_EMBEDDING_COMPONENTS = 64;

    struct ScienceVectorMetrics
    {
        double dotProduct = 0.0;
        double cosineSimilarity = 0.0;
        double cosineDistance = 0.0;
        double euclideanDistance = 0.0;
        double angularDistanceRadians = 0.0;
    };

    struct ScienceNormValidationRule
    {
        std::string sourceId;
        std::string ruleId;
        double minimum = 0.0;
        double maximum = 0.0;
    };

    struct ScienceNormSummary
    {
        std::size_t validCount = 0;
        std::size_t invalidCount = 0;
        double minimum = 0.0;
        double maximum = 0.0;
        double mean = 0.0;
        std::string warning;
    };

    double dequantizeAlphaEarth(std::int8_t raw, bool& valid);

    bool compareEmbeddingVectors(const float* a, const float* b,
                                 ScienceVectorMetrics& output,
                                 std::string& error);

    bool summarizeEmbeddingNorms(const float* norms,
                                 const unsigned char* validMask,
                                 std::size_t normCount,
                                 const ScienceNormValidationRule* rule,
                                 ScienceNormSummary& output,
                                 std::string& error);

    bool aggregateEmbeddingVectors(
        const float* values, const unsigned char* validMask,
        std::size_t vectorCount,
        std::array<float, SCIENCE_EMBEDDING_COMPONENTS>& meanDirection,
        double& concentration, std::string& error);
}

#endif
