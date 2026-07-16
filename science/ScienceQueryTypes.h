#ifndef OSGSOL_SCIENCE_QUERY_TYPES_H
#define OSGSOL_SCIENCE_QUERY_TYPES_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "SciencePreviewSupport.h"

namespace earthscience
{
    enum class ScienceSourceHealth
    {
        Unavailable,
        Ready,
        Busy,
        Degraded,
    };

    enum class ScienceJobState
    {
        Unavailable,
        Idle,
        Queued,
        Fetching,
        Ready,
        Failed,
        Cancelled,
    };

    enum class ScienceProgressStage
    {
        Idle,
        Queued,
        Locating,
        Reading,
        Decoding,
        Validating,
        Aligning,
        Analyzing,
        Materializing,
        Cancelling,
        Ready,
        Failed,
        Cancelled,
    };

    enum class ScienceGeometryKind
    {
        Point,
        BoundingBox,
        CurrentView,
    };

    enum class ScienceTimeMode
    {
        Instant,
        Interval,
        ExplicitYears,
    };

    enum class ScienceAggregation
    {
        None,
        Mean,
        Minimum,
        Maximum,
    };

    enum class ScienceOutputKind
    {
        RasterLayer,
        Table,
        VectorFeatures,
        Embedding,
        TimeSeries,
        Analysis,
        Export,
    };

    enum class ScienceAnalysisKind
    {
        None,
        PointSeries,
        RegionalChange,
        PrincipalComponents,
        SphericalClusters,
    };

    enum class ScienceMetric
    {
        DotProduct,
        CosineSimilarity,
        CosineDistance,
        EuclideanDistance,
        AngularDistance,
    };

    enum class SciencePriority
    {
        Visible,
        InteractiveResearch,
        Background,
    };

    enum class ScienceVisualizationKind
    {
        FalseColor,
        NaturalColor,
        Continuous,
        Categorical,
    };

    const char* scienceSourceHealthName(ScienceSourceHealth health);
    const char* scienceJobStateName(ScienceJobState state);
    const char* scienceProgressStageName(ScienceProgressStage stage);
    const char* scienceOutputKindName(ScienceOutputKind kind);
    const char* scienceAnalysisKindName(ScienceAnalysisKind kind);
    const char* scienceMetricName(ScienceMetric metric);

    struct ScienceProgress
    {
        ScienceProgressStage stage = ScienceProgressStage::Idle;
        std::uint64_t completedUnits = 0, totalUnits = 0;
        bool determinate = false;
        std::string unit;
        double elapsedSeconds = 0.0;

        float legacyFraction() const;
        operator float() const { return legacyFraction(); }
    };

    struct ScienceAnalysisOptions
    {
        ScienceAnalysisKind kind = ScienceAnalysisKind::None;
        std::vector<ScienceMetric> metrics = {ScienceMetric::CosineSimilarity,
                                              ScienceMetric::CosineDistance};
        int baselineYear = 0, comparisonYear = 0, gridSize = 128;
        double hotspotQuantile = 0.90;
        bool enablePca = false, enableClustering = false;
        bool confirmedLargeRequest = false;
        int pcaComponents = 3, clusterCount = 4;
    };

    struct ScienceQueryCost
    {
        std::uint64_t sourceBytesUpperBound = 0;
        std::uint64_t residentBytesUpperBound = 0;
        std::uint64_t resultCells = 0;
        double estimatedDurationSeconds = 0.0;
        bool durationDeterminate = false;
        bool requiresConfirmation = false;
    };

    struct ScienceVariableDescriptor
    {
        std::string id;
        std::string displayName;
        std::string unit;
        std::string dataKind;
        int componentCount = 1;
    };

    struct ScienceVisualizationDescriptor
    {
        std::string id;
        std::string displayName;
        ScienceVisualizationKind kind = ScienceVisualizationKind::Continuous;
        std::vector<std::string> channelVariables;
        double displayMinimum = 0.0;
        double displayMaximum = 1.0;
        std::string legend;
    };

    struct ScienceSourceCapabilities
    {
        bool pointQuery = false;
        bool boundingBoxQuery = false;
        bool currentViewQuery = false;
        bool instantTime = false;
        bool intervalTime = false;
        bool explicitYears = false;
        bool rasterLayerOutput = false;
        bool tableOutput = false;
        bool vectorOutput = false;
        bool aggregation = false;
        double minimumSpanMeters = 0.0;
        double maximumSpanMeters = 0.0;
    };

    struct ScienceSourceDescriptor
    {
        std::string id;
        std::string name;
        std::string category;
        std::string providerVersion;
        std::string attribution;
        int firstYear = 0;
        int lastYear = 0;
        double nativeResolutionMeters = 0.0;
        int componentCount = 0;
        std::vector<ScienceVariableDescriptor> variables;
        std::vector<ScienceVisualizationDescriptor> visualizations;
        ScienceSourceCapabilities capabilities;
        ScienceSourceHealth health = ScienceSourceHealth::Unavailable;
        std::string healthMessage;
        bool experimental = false;
    };

    struct ScienceWgs84Point
    {
        double latitude = 0.0;
        double longitude = 0.0;
    };

    struct ScienceWgs84Bounds
    {
        double west = 0.0;
        double south = 0.0;
        double east = 0.0;
        double north = 0.0;
    };

    struct ScienceGeometry
    {
        ScienceGeometryKind kind = ScienceGeometryKind::Point;
        ScienceWgs84Point point;
        ScienceWgs84Bounds bounds;
        double requestedSpanMeters = 0.0;
    };

    struct ScienceTimeSelection
    {
        ScienceTimeMode mode = ScienceTimeMode::ExplicitYears;
        std::string instant;
        std::string intervalStart;
        std::string intervalEnd;
        std::vector<int> explicitYears;
        std::string publicationTime;
        std::string forecastReferenceTime;
    };

    struct ScienceQueryLimits
    {
        double maximumAreaSquareMeters = 0.0;
        std::uint64_t maximumBytes = 0;
        std::uint64_t maximumMemoryBytes = 0;
        double maximumDurationSeconds = 0.0;
        std::uint64_t maximumResultCells = 0;
        bool allowUpsampling = false;
    };

    struct GeoTemporalQuery
    {
        std::string sourceId;
        ScienceGeometry geometry;
        ScienceTimeSelection time;
        std::vector<std::string> variables;
        double targetResolutionMeters = 0.0;
        ScienceAggregation aggregation = ScienceAggregation::None;
        ScienceOutputKind outputKind = ScienceOutputKind::RasterLayer;
        ScienceQueryLimits limits;
        std::string purpose;
        SciencePriority priority = SciencePriority::Visible;
        std::string visualizationId;
        ScienceAnalysisOptions analysis;
    };

    struct ScienceSourceReference
    {
        std::string sourceId;
        std::string providerVersion;
        std::string datasetId;
        std::string originalUrl;
        ScienceGeometry requestedCoverage;
        ScienceWgs84Bounds actualCoverage;
        std::vector<std::string> variables;
        std::vector<std::string> units;
        std::vector<std::string> processingSteps;
        std::string acquisitionTime;
        std::string publicationTime;
        std::string forecastReferenceTime;
        std::string attribution;
    };

    struct ScienceRasterPayload
    {
        ScienceWgs84Bounds bounds;
        int width = 0;
        int height = 0;
        int sourceWindowWidth = 0;
        int sourceWindowHeight = 0;
        double sourceResolutionMeters = 0.0;
        double displayResolutionMeters = 0.0;
        std::shared_ptr<const std::vector<unsigned char>> rgba;
        std::shared_ptr<const ScienceGroundGrid> groundGrid;
    };

    struct ScienceEmbeddingPayload
    {
        static constexpr int componentCount = 64;

        std::shared_ptr<const std::vector<int>> years;
        int width = 0;
        int height = 0;
        std::shared_ptr<const std::vector<float>> values;
        std::shared_ptr<const std::vector<unsigned char>> mask;
        ScienceWgs84Bounds bounds;
        std::shared_ptr<const ScienceGroundGrid> groundGrid;
        double actualResolutionMeters = 0.0;
        std::shared_ptr<const std::vector<std::string>> processingSteps;
        std::uint64_t validCellCount = 0;
        std::uint64_t noDataCellCount = 0;
        double coverageFraction = 0.0;
        std::shared_ptr<const std::vector<float>> norms;
        std::shared_ptr<const std::vector<std::string>> warnings;
    };

    struct ScienceMetricResult
    {
        ScienceMetric metric = ScienceMetric::CosineSimilarity;
        int baselineYear = 0;
        int comparisonYear = 0;
        double value = 0.0;
        std::string unit;
    };

    struct ScienceAnnualSeries
    {
        ScienceMetric metric = ScienceMetric::CosineSimilarity;
        std::shared_ptr<const std::vector<int>> years;
        std::shared_ptr<const std::vector<double>> values;
        std::string unit;
    };

    struct ScienceScalarChangeRaster
    {
        ScienceMetric metric = ScienceMetric::CosineDistance;
        int width = 0;
        int height = 0;
        ScienceWgs84Bounds bounds;
        std::shared_ptr<const ScienceGroundGrid> groundGrid;
        double actualResolutionMeters = 0.0;
        std::shared_ptr<const std::vector<float>> values;
        std::shared_ptr<const std::vector<unsigned char>> mask;
        std::uint64_t validCellCount = 0;
        std::uint64_t noDataCellCount = 0;
        double coverageFraction = 0.0;
    };

    struct SciencePcaResult
    {
        int inputComponentCount = 64;
        int componentCount = 0;
        std::shared_ptr<const std::vector<float>> components;
        std::shared_ptr<const std::vector<float>> scores;
        std::shared_ptr<const std::vector<double>> explainedVarianceRatios;
    };

    struct ScienceClusterResult
    {
        ScienceMetric metric = ScienceMetric::CosineDistance;
        int clusterCount = 0;
        std::shared_ptr<const std::vector<int>> assignments;
        std::shared_ptr<const std::vector<float>> centroids;
        std::shared_ptr<const std::vector<std::uint64_t>> populations;
    };

    struct ScienceAnalysisPayload
    {
        ScienceAnalysisKind kind = ScienceAnalysisKind::None;
        std::shared_ptr<const std::vector<ScienceMetricResult>> metrics;
        std::shared_ptr<const std::vector<ScienceAnnualSeries>> annualSeries;
        ScienceScalarChangeRaster scalarChangeRaster;
        SciencePcaResult pca;
        ScienceClusterResult clusters;
        std::shared_ptr<const std::vector<std::string>> interpretation;
        std::shared_ptr<const std::vector<std::string>> limitations;
    };

    struct ScienceArtifact
    {
        std::string artifactId;
        GeoTemporalQuery query;
        std::uint64_t generation = 0;
        std::vector<ScienceSourceReference> sourceReferences;
        ScienceRasterPayload raster;
        ScienceEmbeddingPayload embedding;
        ScienceAnalysisPayload analysis;
        std::string visualizationId;
        std::vector<std::string> warnings;
        std::string processingVersion;
        std::string createdAt;
    };

    struct ScienceJobSnapshot
    {
        std::uint64_t jobId = 0;
        ScienceJobState state = ScienceJobState::Idle;
        GeoTemporalQuery query;
        ScienceProgress progress;
        std::string message;
        std::shared_ptr<const ScienceArtifact> lastSuccessfulArtifact;
    };

    std::uint64_t estimatedArtifactBytes(const ScienceArtifact& artifact);
}

#endif
