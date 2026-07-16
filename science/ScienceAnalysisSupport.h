#ifndef OSGSOL_SCIENCE_ANALYSIS_SUPPORT_H
#define OSGSOL_SCIENCE_ANALYSIS_SUPPORT_H

#include "ScienceEmbedding.h"
#include "ScienceQueryTypes.h"

#include <cstddef>
#include <string>
#include <vector>

namespace earthscience
{
namespace analysisdetail
{
    constexpr std::size_t MAX_ANALYSIS_YEARS = 9;

    bool checkedCellCount(int width, int height, std::size_t& cellCount);
    bool equalBounds(const ScienceWgs84Bounds& left,
                     const ScienceWgs84Bounds& right);
    bool validGroundGrid(const ScienceGroundGrid& grid,
                         int width, int height);
    bool equalGroundGrid(const ScienceGroundGrid& left,
                         const ScienceGroundGrid& right);
    bool validateEmbeddingShape(
        const ScienceEmbeddingPayload& embedding, bool requireGrid,
        std::size_t& cellCount, std::string& error,
        const std::string& context);
    bool validateMetrics(const std::vector<ScienceMetric>& metrics,
                         std::string& error);
    double metricValue(const ScienceVectorMetrics& metrics,
                       ScienceMetric metric);
    const char* metricUnit(ScienceMetric metric);
    const float* vectorAt(const ScienceEmbeddingPayload& embedding,
                          std::size_t sampleIndex);
    bool compareVectors(const float* baseline, const float* comparison,
                        ScienceVectorMetrics& metrics, std::string& error,
                        const std::string& context);
    double linearQuantile(const std::vector<double>& sorted,
                          double probability);
    ScienceMetric regionalMetric(const std::vector<ScienceMetric>& metrics);
}
}

#endif
