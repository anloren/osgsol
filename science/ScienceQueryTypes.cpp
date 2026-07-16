#include "ScienceQueryTypes.h"

#include <limits>
#include <stdexcept>

namespace earthscience
{
const char* scienceSourceHealthName(ScienceSourceHealth health)
{
    switch (health)
    {
    case ScienceSourceHealth::Unavailable: return "unavailable";
    case ScienceSourceHealth::Ready: return "ready";
    case ScienceSourceHealth::Busy: return "busy";
    case ScienceSourceHealth::Degraded: return "degraded";
    }
    return "unknown";
}

const char* scienceJobStateName(ScienceJobState state)
{
    switch (state)
    {
    case ScienceJobState::Unavailable: return "unavailable";
    case ScienceJobState::Idle: return "idle";
    case ScienceJobState::Queued: return "queued";
    case ScienceJobState::Fetching: return "fetching";
    case ScienceJobState::Ready: return "ready";
    case ScienceJobState::Failed: return "failed";
    case ScienceJobState::Cancelled: return "cancelled";
    }
    return "unknown";
}

const char* scienceProgressStageName(ScienceProgressStage stage)
{
    switch (stage)
    {
    case ScienceProgressStage::Idle: return "idle";
    case ScienceProgressStage::Queued: return "queued";
    case ScienceProgressStage::Locating: return "locating";
    case ScienceProgressStage::Reading: return "reading";
    case ScienceProgressStage::Decoding: return "decoding";
    case ScienceProgressStage::Validating: return "validating";
    case ScienceProgressStage::Aligning: return "aligning";
    case ScienceProgressStage::Analyzing: return "analyzing";
    case ScienceProgressStage::Materializing: return "materializing";
    case ScienceProgressStage::Cancelling: return "cancelling";
    case ScienceProgressStage::Ready: return "ready";
    case ScienceProgressStage::Failed: return "failed";
    case ScienceProgressStage::Cancelled: return "cancelled";
    }
    return "unknown";
}

const char* scienceOutputKindName(ScienceOutputKind kind)
{
    switch (kind)
    {
    case ScienceOutputKind::RasterLayer: return "raster-layer";
    case ScienceOutputKind::Table: return "table";
    case ScienceOutputKind::VectorFeatures: return "vector-features";
    case ScienceOutputKind::Embedding: return "embedding";
    case ScienceOutputKind::TimeSeries: return "time-series";
    case ScienceOutputKind::Analysis: return "analysis";
    case ScienceOutputKind::Export: return "export";
    }
    return "unknown";
}

const char* scienceAnalysisKindName(ScienceAnalysisKind kind)
{
    switch (kind)
    {
    case ScienceAnalysisKind::None: return "none";
    case ScienceAnalysisKind::PointSeries: return "point-series";
    case ScienceAnalysisKind::RegionalChange: return "regional-change";
    case ScienceAnalysisKind::PrincipalComponents:
        return "principal-components";
    case ScienceAnalysisKind::SphericalClusters: return "spherical-clusters";
    }
    return "unknown";
}

const char* scienceMetricName(ScienceMetric metric)
{
    switch (metric)
    {
    case ScienceMetric::DotProduct: return "dot-product";
    case ScienceMetric::CosineSimilarity: return "cosine-similarity";
    case ScienceMetric::CosineDistance: return "cosine-distance";
    case ScienceMetric::EuclideanDistance: return "euclidean-distance";
    case ScienceMetric::AngularDistance: return "angular-distance";
    }
    return "unknown";
}

float ScienceProgress::legacyFraction() const
{
    if (!determinate || totalUnits == 0) return 0.0f;
    if (completedUnits >= totalUnits) return 1.0f;
    return static_cast<float>(
        static_cast<double>(completedUnits) / static_cast<double>(totalUnits));
}

namespace
{
    void checkedAdd(std::uint64_t& total, std::uint64_t value)
    {
        if (value > std::numeric_limits<std::uint64_t>::max() - total)
            throw std::overflow_error("science artifact byte estimate overflow");
        total += value;
    }

    std::uint64_t checkedMultiply(std::uint64_t left, std::uint64_t right)
    {
        if (left != 0 &&
            right > std::numeric_limits<std::uint64_t>::max() / left)
            throw std::overflow_error("science artifact byte estimate overflow");
        return left * right;
    }

    template<typename T>
    void addVectorBytes(
        std::uint64_t& total,
        const std::shared_ptr<const std::vector<T>>& values)
    {
        if (!values) return;
        checkedAdd(total, sizeof(std::vector<T>));
        checkedAdd(total, checkedMultiply(values->capacity(), sizeof(T)));
    }

    template<typename T>
    void addVectorBytes(std::uint64_t& total, const std::vector<T>& values)
    {
        checkedAdd(total, checkedMultiply(values.capacity(), sizeof(T)));
    }

    void addStringBytes(std::uint64_t& total, const std::string& value)
    {
        checkedAdd(total, value.capacity());
        checkedAdd(total, 1);
    }

    void addStringVectorBytes(
        std::uint64_t& total, const std::vector<std::string>& values)
    {
        addVectorBytes(total, values);
        for (const std::string& value : values) addStringBytes(total, value);
    }

    void addStringVectorBytes(
        std::uint64_t& total,
        const std::shared_ptr<const std::vector<std::string>>& values)
    {
        addVectorBytes(total, values);
        if (!values) return;
        for (const std::string& value : *values) addStringBytes(total, value);
    }

    void addGroundGridBytes(
        std::uint64_t& total,
        const std::shared_ptr<const ScienceGroundGrid>& grid)
    {
        if (!grid) return;
        checkedAdd(total, sizeof(ScienceGroundGrid));
        checkedAdd(total, checkedMultiply(
            grid->points.capacity(), sizeof(ScienceGroundPoint)));
    }

    void addQueryBytes(std::uint64_t& total, const GeoTemporalQuery& query)
    {
        addStringBytes(total, query.sourceId);
        addStringBytes(total, query.time.instant);
        addStringBytes(total, query.time.intervalStart);
        addStringBytes(total, query.time.intervalEnd);
        addVectorBytes(total, query.time.explicitYears);
        addStringBytes(total, query.time.publicationTime);
        addStringBytes(total, query.time.forecastReferenceTime);
        addStringVectorBytes(total, query.variables);
        addStringBytes(total, query.purpose);
        addStringBytes(total, query.visualizationId);
        addVectorBytes(total, query.analysis.metrics);
    }

    void addSourceReferenceBytes(
        std::uint64_t& total, const ScienceSourceReference& reference)
    {
        addStringBytes(total, reference.sourceId);
        addStringBytes(total, reference.providerVersion);
        addStringBytes(total, reference.datasetId);
        addStringBytes(total, reference.originalUrl);
        addStringVectorBytes(total, reference.variables);
        addStringVectorBytes(total, reference.units);
        addStringVectorBytes(total, reference.processingSteps);
        addStringBytes(total, reference.acquisitionTime);
        addStringBytes(total, reference.publicationTime);
        addStringBytes(total, reference.forecastReferenceTime);
        addStringBytes(total, reference.attribution);
    }
}

std::uint64_t estimatedArtifactBytes(const ScienceArtifact& artifact)
{
    std::uint64_t total = sizeof(ScienceArtifact);
    addStringBytes(total, artifact.artifactId);
    addQueryBytes(total, artifact.query);
    addVectorBytes(total, artifact.sourceReferences);
    for (const ScienceSourceReference& reference : artifact.sourceReferences)
        addSourceReferenceBytes(total, reference);
    addVectorBytes(total, artifact.raster.rgba);
    addGroundGridBytes(total, artifact.raster.groundGrid);

    const ScienceEmbeddingPayload& embedding = artifact.embedding;
    addVectorBytes(total, embedding.years);
    addVectorBytes(total, embedding.values);
    addVectorBytes(total, embedding.mask);
    addGroundGridBytes(total, embedding.groundGrid);
    addStringVectorBytes(total, embedding.processingSteps);
    addVectorBytes(total, embedding.norms);
    addStringVectorBytes(total, embedding.warnings);

    if (embedding.years && embedding.width > 0 && embedding.height > 0)
    {
        if (embedding.componentCount != 64)
            throw std::invalid_argument(
                "science embedding payload must have exactly 64 components");
        std::uint64_t logicalValues = checkedMultiply(
            embedding.years->size(), static_cast<std::uint64_t>(embedding.width));
        logicalValues = checkedMultiply(
            logicalValues, static_cast<std::uint64_t>(embedding.height));
        logicalValues = checkedMultiply(logicalValues, 64);
        const std::uint64_t logicalBytes =
            checkedMultiply(logicalValues, sizeof(float));
        const std::uint64_t actualBytes = embedding.values
            ? checkedMultiply(embedding.values->capacity(), sizeof(float)) : 0;
        if (logicalBytes > actualBytes)
            checkedAdd(total, logicalBytes - actualBytes);
    }

    const ScienceAnalysisPayload& analysis = artifact.analysis;
    addVectorBytes(total, analysis.metrics);
    if (analysis.metrics)
    {
        for (const ScienceMetricResult& metric : *analysis.metrics)
            addStringBytes(total, metric.unit);
    }
    addVectorBytes(total, analysis.annualSeries);
    if (analysis.annualSeries)
    {
        for (const ScienceAnnualSeries& series : *analysis.annualSeries)
        {
            addVectorBytes(total, series.years);
            addVectorBytes(total, series.values);
            addStringBytes(total, series.unit);
        }
    }
    addVectorBytes(total, analysis.scalarChangeRaster.values);
    addVectorBytes(total, analysis.scalarChangeRaster.mask);
    addGroundGridBytes(total, analysis.scalarChangeRaster.groundGrid);
    addVectorBytes(total, analysis.pca.components);
    addVectorBytes(total, analysis.pca.scores);
    addVectorBytes(total, analysis.pca.explainedVarianceRatios);
    addVectorBytes(total, analysis.clusters.assignments);
    addVectorBytes(total, analysis.clusters.centroids);
    addVectorBytes(total, analysis.clusters.populations);
    addStringVectorBytes(total, analysis.interpretation);
    addStringVectorBytes(total, analysis.limitations);
    addStringBytes(total, artifact.visualizationId);
    addStringVectorBytes(total, artifact.warnings);
    addStringBytes(total, artifact.processingVersion);
    addStringBytes(total, artifact.createdAt);
    return total;
}
}
