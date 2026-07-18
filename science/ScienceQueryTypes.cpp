#include "ScienceQueryTypes.h"

#include <cmath>
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
    const float fraction = static_cast<float>(
        static_cast<double>(completedUnits) / static_cast<double>(totalUnits));
    if (!std::isfinite(fraction) || fraction >= 1.0f)
        return std::nextafter(1.0f, 0.0f);
    return fraction;
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
        addVectorBytes(total, reference.fields);
        for (const ScienceEvidenceField& field : reference.fields)
        {
            addStringBytes(total, field.id);
            addStringBytes(total, field.displayName);
            addStringBytes(total, field.value);
            addStringBytes(total, field.unit);
        }
    }

    bool hasEmbeddingContent(const ScienceEmbeddingPayload& embedding)
    {
        const ScienceWgs84Bounds& bounds = embedding.bounds;
        return embedding.years || embedding.width != 0 ||
               embedding.height != 0 || embedding.values || embedding.mask ||
               bounds.west != 0.0 || bounds.south != 0.0 ||
               bounds.east != 0.0 || bounds.north != 0.0 ||
               embedding.groundGrid ||
               embedding.actualResolutionMeters != 0.0 ||
               embedding.processingSteps || embedding.validCellCount != 0 ||
               embedding.noDataCellCount != 0 ||
               embedding.coverageFraction != 0.0 || embedding.norms ||
               embedding.warnings;
    }

    bool vectorSizeMatches(std::size_t actual, std::uint64_t expected)
    {
        if (expected > std::numeric_limits<std::size_t>::max()) return false;
        return actual == static_cast<std::size_t>(expected);
    }

    std::uint64_t validatedEmbeddingValueBytes(
        const ScienceEmbeddingPayload& embedding)
    {
        if (!hasEmbeddingContent(embedding)) return 0;
        if (!embedding.years || embedding.years->empty())
            throw std::invalid_argument(
                "science embedding payload requires non-empty years");
        if (embedding.width <= 0 || embedding.height <= 0)
            throw std::invalid_argument(
                "science embedding payload requires positive dimensions");

        std::uint64_t cellCount = checkedMultiply(
            embedding.years->size(),
            static_cast<std::uint64_t>(embedding.width));
        cellCount = checkedMultiply(
            cellCount, static_cast<std::uint64_t>(embedding.height));
        const std::uint64_t valueCount = checkedMultiply(
            cellCount, ScienceEmbeddingPayload::componentCount);
        const std::uint64_t valueBytes =
            checkedMultiply(valueCount, sizeof(float));

        if (!embedding.values ||
            !vectorSizeMatches(embedding.values->size(), valueCount))
            throw std::invalid_argument(
                "science embedding values do not match the 64D shape");
        if (embedding.mask &&
            !vectorSizeMatches(embedding.mask->size(), cellCount))
            throw std::invalid_argument(
                "science embedding mask does not match the cell shape");
        if (embedding.norms &&
            !vectorSizeMatches(embedding.norms->size(), cellCount))
            throw std::invalid_argument(
                "science embedding norms do not match the cell shape");
        return valueBytes;
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

    const std::uint64_t logicalBytes =
        validatedEmbeddingValueBytes(embedding);
    const std::uint64_t actualBytes = embedding.values
        ? checkedMultiply(embedding.values->capacity(), sizeof(float)) : 0;
    if (logicalBytes > actualBytes)
        checkedAdd(total, logicalBytes - actualBytes);

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
            addVectorBytes(total, series.validity);
            addStringBytes(total, series.unit);
        }
    }
    addVectorBytes(total, analysis.scalarChangeRaster.values);
    addVectorBytes(total, analysis.scalarChangeRaster.mask);
    addGroundGridBytes(total, analysis.scalarChangeRaster.groundGrid);
    addVectorBytes(total, analysis.regionalChange.quantiles);
    addVectorBytes(total, analysis.regionalChange.hotspotMask);
    addVectorBytes(total, analysis.regionalChange.hotspotIndices);
    addGroundGridBytes(total, analysis.regionalChange.groundGrid);
    addVectorBytes(total, analysis.pca.components);
    addVectorBytes(total, analysis.pca.scores);
    addVectorBytes(total, analysis.pca.eigenvalues);
    addVectorBytes(total, analysis.pca.explainedVarianceRatios);
    addVectorBytes(total, analysis.clusters.assignments);
    addVectorBytes(total, analysis.clusters.centroids);
    addVectorBytes(total, analysis.clusters.populations);
    addVectorBytes(total, analysis.clusters.concentrations);
    addStringVectorBytes(total, analysis.interpretation);
    addStringVectorBytes(total, analysis.limitations);
    addVectorBytes(total, artifact.scalarSummaries);
    for (const ScienceScalarSummary& summary : artifact.scalarSummaries)
    {
        addStringBytes(total, summary.variableId);
        addStringBytes(total, summary.displayName);
        addStringBytes(total, summary.unit);
    }
    addStringBytes(total, artifact.visualizationId);
    addStringVectorBytes(total, artifact.warnings);
    addStringBytes(total, artifact.processingVersion);
    addStringBytes(total, artifact.createdAt);
    return total;
}
}
