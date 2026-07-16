#include "ScienceAnalysisSupport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace earthscience
{
namespace analysisdetail
{
namespace
{
    constexpr std::size_t MAX_REGIONAL_CELLS = 256u * 256u;

    bool fail(std::string& error, const std::string& message)
    {
        error = message;
        return false;
    }

    bool finiteBounds(const ScienceWgs84Bounds& bounds)
    {
        return std::isfinite(bounds.west) && std::isfinite(bounds.south) &&
               std::isfinite(bounds.east) && std::isfinite(bounds.north) &&
               bounds.west <= bounds.east && bounds.south <= bounds.north;
    }
}

bool checkedCellCount(int width, int height, std::size_t& cellCount)
{
    if (width <= 0 || height <= 0) return false;
    const std::size_t unsignedWidth = static_cast<std::size_t>(width);
    const std::size_t unsignedHeight = static_cast<std::size_t>(height);
    if (unsignedWidth >
        std::numeric_limits<std::size_t>::max() / unsignedHeight)
        return false;
    cellCount = unsignedWidth * unsignedHeight;
    return true;
}

bool equalBounds(const ScienceWgs84Bounds& left,
                 const ScienceWgs84Bounds& right)
{
    return left.west == right.west && left.south == right.south &&
           left.east == right.east && left.north == right.north;
}

bool validGroundGrid(const ScienceGroundGrid& grid, int width, int height)
{
    std::size_t cellCount = 0;
    if (!checkedCellCount(width, height, cellCount) ||
        grid.columns != width || grid.rows != height ||
        grid.points.size() != cellCount)
        return false;
    for (const ScienceGroundPoint& point : grid.points)
    {
        if (!std::isfinite(point.longitude) ||
            !std::isfinite(point.latitude))
            return false;
    }
    return true;
}

bool equalGroundGrid(const ScienceGroundGrid& left,
                     const ScienceGroundGrid& right)
{
    if (left.columns != right.columns || left.rows != right.rows ||
        left.points.size() != right.points.size())
        return false;
    for (std::size_t i = 0; i < left.points.size(); ++i)
    {
        if (left.points[i].longitude != right.points[i].longitude ||
            left.points[i].latitude != right.points[i].latitude)
            return false;
    }
    return true;
}

bool validateEmbeddingShape(const ScienceEmbeddingPayload& embedding,
                            bool requireGrid, std::size_t& cellCount,
                            std::string& error, const std::string& context)
{
    if (!embedding.years || embedding.years->empty())
        return fail(error, context + " requires at least one year");
    if (embedding.years->size() > MAX_ANALYSIS_YEARS)
        return fail(error, context + " exceeds the nine-year bound");
    for (std::size_t i = 1; i < embedding.years->size(); ++i)
    {
        if (embedding.years->at(i - 1) >= embedding.years->at(i))
            return fail(error, context +
                        " requires strictly ascending unique years");
    }
    if (!checkedCellCount(embedding.width, embedding.height, cellCount))
        return fail(error, context + " requires positive finite dimensions");
    if (cellCount > MAX_REGIONAL_CELLS)
        return fail(error, context + " exceeds the 256 by 256 cell bound");
    if (embedding.years->size() >
        std::numeric_limits<std::size_t>::max() / cellCount)
        return fail(error, context + " cell shape overflowed");
    const std::size_t sampleCount = embedding.years->size() * cellCount;
    if (sampleCount > std::numeric_limits<std::size_t>::max() /
                          SCIENCE_EMBEDDING_COMPONENTS)
        return fail(error, context + " 64D value shape overflowed");
    const std::size_t valueCount =
        sampleCount * SCIENCE_EMBEDDING_COMPONENTS;
    if (!embedding.values || embedding.values->size() != valueCount)
        return fail(error, context +
                    " values do not match the dequantized 64D shape");
    if (!embedding.mask || embedding.mask->size() != sampleCount)
        return fail(error, context + " mask does not match the sample shape");
    if (embedding.norms && embedding.norms->size() != sampleCount)
        return fail(error, context + " norms do not match the sample shape");
    if (!finiteBounds(embedding.bounds))
        return fail(error, context + " requires finite ordered bounds");
    if (!std::isfinite(embedding.actualResolutionMeters) ||
        embedding.actualResolutionMeters < 0.0)
        return fail(error, context + " has an invalid actual resolution");
    if (requireGrid && (!embedding.groundGrid ||
                        !validGroundGrid(*embedding.groundGrid,
                                         embedding.width,
                                         embedding.height)))
        return fail(error, context +
                    " ground grid does not match the raster shape");
    return true;
}

bool validateMetrics(const std::vector<ScienceMetric>& metrics,
                     std::string& error)
{
    if (metrics.empty())
        return fail(error, "analysis requires at least one metric");
    if (metrics.size() > 5)
        return fail(error, "analysis metric list exceeds the bounded set");
    std::array<bool, 5> seen{};
    for (ScienceMetric metric : metrics)
    {
        const int index = static_cast<int>(metric);
        if (index < 0 || index >= static_cast<int>(seen.size()))
            return fail(error, "analysis contains an unknown metric");
        if (seen[static_cast<std::size_t>(index)])
            return fail(error, "analysis contains a duplicate metric");
        seen[static_cast<std::size_t>(index)] = true;
    }
    return true;
}

double metricValue(const ScienceVectorMetrics& metrics, ScienceMetric metric)
{
    switch (metric)
    {
    case ScienceMetric::DotProduct: return metrics.dotProduct;
    case ScienceMetric::CosineSimilarity: return metrics.cosineSimilarity;
    case ScienceMetric::CosineDistance: return metrics.cosineDistance;
    case ScienceMetric::EuclideanDistance: return metrics.euclideanDistance;
    case ScienceMetric::AngularDistance:
        return metrics.angularDistanceRadians;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

const char* metricUnit(ScienceMetric metric)
{
    switch (metric)
    {
    case ScienceMetric::AngularDistance: return "radians";
    case ScienceMetric::DotProduct:
    case ScienceMetric::EuclideanDistance:
        return "embedding-space units";
    case ScienceMetric::CosineSimilarity:
    case ScienceMetric::CosineDistance:
        return "unitless";
    }
    return "unknown";
}

const float* vectorAt(const ScienceEmbeddingPayload& embedding,
                      std::size_t sampleIndex)
{
    return embedding.values->data() +
        sampleIndex * SCIENCE_EMBEDDING_COMPONENTS;
}

bool compareVectors(const float* baseline, const float* comparison,
                    ScienceVectorMetrics& metrics, std::string& error,
                    const std::string& context)
{
    std::string metricError;
    if (!compareEmbeddingVectors(
            baseline, comparison, metrics, metricError))
        return fail(error, context + ": " + metricError);
    return true;
}

double linearQuantile(const std::vector<double>& sorted, double probability)
{
    if (sorted.size() == 1) return sorted.front();
    const double position = probability *
        static_cast<double>(sorted.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(position));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower);
    return sorted[lower] + (sorted[upper] - sorted[lower]) * fraction;
}

ScienceMetric regionalMetric(const std::vector<ScienceMetric>& metrics)
{
    const auto cosineDistance = std::find(
        metrics.begin(), metrics.end(), ScienceMetric::CosineDistance);
    return cosineDistance != metrics.end()
        ? ScienceMetric::CosineDistance : metrics.front();
}
}
}
