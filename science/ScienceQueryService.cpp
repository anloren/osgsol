#include "ScienceQueryService.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace earthscience
{
namespace
{
    const char* geometryName(ScienceGeometryKind kind)
    {
        switch (kind)
        {
        case ScienceGeometryKind::Point: return "point";
        case ScienceGeometryKind::BoundingBox: return "bounding-box";
        case ScienceGeometryKind::CurrentView: return "current-view";
        }
        return "unknown";
    }

    const char* timeModeName(ScienceTimeMode mode)
    {
        switch (mode)
        {
        case ScienceTimeMode::Instant: return "instant";
        case ScienceTimeMode::Interval: return "interval";
        case ScienceTimeMode::ExplicitYears: return "explicit-years";
        }
        return "unknown";
    }

    const char* aggregationName(ScienceAggregation aggregation)
    {
        switch (aggregation)
        {
        case ScienceAggregation::None: return "none";
        case ScienceAggregation::Mean: return "mean";
        case ScienceAggregation::Minimum: return "minimum";
        case ScienceAggregation::Maximum: return "maximum";
        }
        return "unknown";
    }

    std::string integerRange(double minimum, double maximum)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(0)
               << '[' << minimum << ", " << maximum << ']';
        return stream.str();
    }

    bool isLegacyPreviewQuery(const GeoTemporalQuery& query)
    {
        return query.geometry.kind == ScienceGeometryKind::Point &&
               query.time.mode == ScienceTimeMode::ExplicitYears &&
               query.time.explicitYears.size() == 1 &&
               query.variables ==
                   std::vector<std::string>({"A01", "A16", "A09"}) &&
               query.aggregation == ScienceAggregation::None &&
               query.outputKind == ScienceOutputKind::RasterLayer &&
               query.visualizationId == "false-color-a01-a16-a09" &&
               query.analysis.kind == ScienceAnalysisKind::None;
    }

    std::string throughputKey(const GeoTemporalQuery& query)
    {
        return query.sourceId +
            (isLegacyPreviewQuery(query) ? "#preview" : "#embedding64");
    }

    std::uint64_t checkedMultiply(std::uint64_t left, std::uint64_t right)
    {
        if (left != 0 &&
            right > std::numeric_limits<std::uint64_t>::max() / left)
            throw std::overflow_error("science query cost overflow");
        return left * right;
    }

    std::uint64_t checkedAdd(std::uint64_t left, std::uint64_t right)
    {
        if (right > std::numeric_limits<std::uint64_t>::max() - left)
            throw std::overflow_error("science query cost overflow");
        return left + right;
    }

    std::uint64_t estimatedSourceCells(
        const GeoTemporalQuery& query, double nativeResolutionMeters,
        double fallbackSpanMeters)
    {
        if (!std::isfinite(nativeResolutionMeters) ||
            nativeResolutionMeters <= 0.0)
            nativeResolutionMeters = 10.0;
        if (query.geometry.kind == ScienceGeometryKind::Point)
        {
            if (isLegacyPreviewQuery(query))
            {
                const double span = query.geometry.requestedSpanMeters > 0.0
                    ? query.geometry.requestedSpanMeters : fallbackSpanMeters;
                const double side =
                    std::ceil(span / nativeResolutionMeters) + 2.0;
                if (!std::isfinite(side) || side <= 0.0 ||
                    side > static_cast<double>(
                        std::numeric_limits<std::uint64_t>::max()))
                    throw std::overflow_error("science query cost overflow");
                const std::uint64_t sideCells =
                    static_cast<std::uint64_t>(side);
                return checkedMultiply(sideCells, sideCells);
            }
            return 1;
        }

        constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
        constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
        constexpr double PI = 3.14159265358979323846;
        const ScienceWgs84Bounds& bounds = query.geometry.bounds;
        const double middleLatitude = (bounds.south + bounds.north) * 0.5;
        const double widthMeters = (bounds.east - bounds.west) *
            METERS_PER_LONGITUDE_DEGREE *
            std::max(0.01, std::cos(middleLatitude * PI / 180.0));
        const double heightMeters =
            (bounds.north - bounds.south) * METERS_PER_LATITUDE_DEGREE;
        const double widthCellsValue =
            std::ceil(widthMeters / nativeResolutionMeters) + 2.0;
        const double heightCellsValue =
            std::ceil(heightMeters / nativeResolutionMeters) + 2.0;
        const double maximum = static_cast<double>(
            std::numeric_limits<std::uint64_t>::max());
        if (!std::isfinite(widthCellsValue) ||
            !std::isfinite(heightCellsValue) ||
            widthCellsValue <= 0.0 || heightCellsValue <= 0.0 ||
            widthCellsValue > maximum || heightCellsValue > maximum)
            throw std::overflow_error("science query cost overflow");
        return checkedMultiply(
            static_cast<std::uint64_t>(widthCellsValue),
            static_cast<std::uint64_t>(heightCellsValue));
    }
}

ScienceQueryService::ScienceQueryService(
    std::unique_ptr<ScienceSourceRegistry> registry)
    : _registry(std::move(registry))
{
    if (!_registry)
    {
        _state.state = ScienceJobState::Unavailable;
        _state.progress.stage = ScienceProgressStage::Failed;
        _state.message = "science source registry is missing";
    }
}

ScienceQueryService::~ScienceQueryService()
{
    std::lock_guard<std::mutex> lock(_mutex);
    cancelActiveProvider();
}

std::vector<ScienceSourceDescriptor> ScienceQueryService::listSources() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _registry ? _registry->listSources()
                     : std::vector<ScienceSourceDescriptor>();
}

ScienceQueryCost ScienceQueryService::estimate(
    const GeoTemporalQuery& query) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return estimateUnlocked(query);
}

ScienceQueryCost ScienceQueryService::estimateUnlocked(
    const GeoTemporalQuery& query) const
{
    ScienceQueryCost cost;
    double nativeResolutionMeters = 10.0;
    double maximumSpanMeters = 81920.0;
    if (_registry)
    {
        const IScienceProvider* provider = _registry->find(query.sourceId);
        if (provider)
        {
            const ScienceSourceDescriptor source = provider->descriptor();
            nativeResolutionMeters = source.nativeResolutionMeters;
            maximumSpanMeters = source.capabilities.maximumSpanMeters;
        }
    }
    const std::uint64_t sourceCells = estimatedSourceCells(
        query, nativeResolutionMeters, maximumSpanMeters);
    if (isLegacyPreviewQuery(query))
    {
        constexpr std::uint64_t PREVIEW_CELLS = 256u * 256u;
        cost.resultCells = PREVIEW_CELLS;
        cost.sourceBytesUpperBound = checkedMultiply(sourceCells, 3);
        cost.residentBytesUpperBound = checkedAdd(
            checkedMultiply(sourceCells, 6),
            checkedMultiply(PREVIEW_CELLS, 4));
    }
    else
    {
        const std::uint64_t yearCount = query.time.explicitYears.size();
        std::uint64_t readCells = 1;
        if (query.geometry.kind != ScienceGeometryKind::Point)
        {
            if (query.analysis.gridSize <= 0)
                throw std::invalid_argument(
                    "science analysis grid must be positive");
            const std::uint64_t grid =
                static_cast<std::uint64_t>(query.analysis.gridSize);
            readCells = checkedMultiply(grid, grid);
        }

        cost.sourceBytesUpperBound = checkedMultiply(
            checkedMultiply(sourceCells, 64), yearCount);
        if (query.aggregation == ScienceAggregation::Mean)
            readCells = sourceCells;
        cost.resultCells = query.aggregation == ScienceAggregation::Mean
            ? yearCount : checkedMultiply(readCells, yearCount);

        const std::uint64_t retainedYears =
            query.analysis.kind == ScienceAnalysisKind::RegionalChange
                ? std::min<std::uint64_t>(2, yearCount) : yearCount;
        const std::uint64_t combinedYears =
            query.analysis.kind == ScienceAnalysisKind::RegionalChange
                ? retainedYears : yearCount;
        constexpr std::uint64_t PERSISTENT_BYTES_PER_CELL =
            64 * sizeof(float) + sizeof(unsigned char) + sizeof(float);
        constexpr std::uint64_t READ_WORK_BYTES_PER_CELL =
            64 * sizeof(std::int8_t) + 8 * sizeof(std::int8_t);
        constexpr std::uint64_t ANALYSIS_WORK_BYTES_PER_CELL =
            64 * (sizeof(float) + sizeof(double));
        const std::uint64_t persistentSlice = checkedMultiply(
            readCells, PERSISTENT_BYTES_PER_CELL);
        const std::uint64_t retainedBytes = checkedMultiply(
            persistentSlice, retainedYears);
        const std::uint64_t combinedBytes = checkedMultiply(
            persistentSlice, combinedYears);
        const std::uint64_t readWorkBytes = checkedMultiply(
            readCells, READ_WORK_BYTES_PER_CELL);
        const std::uint64_t analysisBytes = checkedMultiply(
            checkedMultiply(readCells, combinedYears),
            ANALYSIS_WORK_BYTES_PER_CELL);
        cost.residentBytesUpperBound = checkedAdd(
            checkedAdd(retainedBytes, combinedBytes),
            checkedAdd(readWorkBytes, analysisBytes));

        if (query.analysis.kind == ScienceAnalysisKind::PrincipalComponents)
        {
            cost.residentBytesUpperBound = checkedAdd(
                cost.residentBytesUpperBound,
                64u * 64u * sizeof(double));
        }
        else if (query.analysis.kind ==
                 ScienceAnalysisKind::RegionalChange)
        {
            cost.residentBytesUpperBound = checkedAdd(
                cost.residentBytesUpperBound,
                checkedMultiply(readCells, 64));
        }
        cost.requiresConfirmation =
            (query.geometry.kind != ScienceGeometryKind::Point &&
             query.analysis.gridSize >= 256) ||
            (query.analysis.kind == ScienceAnalysisKind::RegionalChange &&
             yearCount > 2);
    }

    const auto throughput =
        _throughputCellsPerSecond.find(throughputKey(query));
    if (throughput != _throughputCellsPerSecond.end() &&
        std::isfinite(throughput->second) && throughput->second > 0.0)
    {
        cost.estimatedDurationSeconds =
            static_cast<double>(cost.resultCells) / throughput->second;
        cost.durationDeterminate =
            std::isfinite(cost.estimatedDurationSeconds);
        if (!cost.durationDeterminate)
            cost.estimatedDurationSeconds = 0.0;
    }
    return cost;
}

std::uint64_t ScienceQueryService::submit(const GeoTemporalQuery& query)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const std::uint64_t jobId = ++_nextJobId;

    ScienceJobSnapshot next;
    next.jobId = jobId;
    next.query = query;
    next.lastSuccessfulArtifact = _state.lastSuccessfulArtifact;
    next.lastSuccessfulPreviewArtifact =
        _state.lastSuccessfulPreviewArtifact;
    next.lastSuccessfulAnalysisArtifact =
        _state.lastSuccessfulAnalysisArtifact;
    next.displayArtifact = _state.displayArtifact;

    IScienceProvider* provider =
        _registry ? _registry->find(query.sourceId) : nullptr;
    std::string error;
    if (!provider)
        error = "unknown science source: " + query.sourceId;
    else if (!validate(query, provider->descriptor(), error))
    {
        // The precise validation error was populated by validate().
    }

    cancelActiveProvider();
    _activeProvider = nullptr;
    _activeSourceId.clear();
    _activeProviderGeneration = 0;

    if (!error.empty())
    {
        next.state = ScienceJobState::Failed;
        next.progress.stage = ScienceProgressStage::Failed;
        next.message = error;
        _state = std::move(next);
        return jobId;
    }

    const std::uint64_t providerGeneration = provider->submit(query);
    if (providerGeneration == 0)
    {
        next.state = ScienceJobState::Failed;
        next.progress.stage = ScienceProgressStage::Failed;
        next.message = "science provider rejected query";
        _state = std::move(next);
        return jobId;
    }

    _activeProvider = provider;
    _activeSourceId = query.sourceId;
    _activeProviderGeneration = providerGeneration;
    _providerActive = true;
    next.state = ScienceJobState::Queued;
    next.progress.stage = ScienceProgressStage::Queued;
    next.message = "Queued";
    _state = std::move(next);
    return jobId;
}

void ScienceQueryService::cancel(std::uint64_t jobId)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_providerActive || jobId != _state.jobId) return;
    cancelActiveProvider();
    _state.state = ScienceJobState::Cancelled;
    _state.progress = ScienceProgress();
    _state.progress.stage = ScienceProgressStage::Cancelled;
    _state.message = "Cancelled";
}

void ScienceQueryService::clearArtifact()
{
    clearArtifacts();
}

std::shared_ptr<const ScienceArtifact> ScienceQueryService::findArtifact(
    const std::string& id) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _artifacts.find(id);
}

bool ScienceQueryService::showArtifact(const std::string& id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const std::shared_ptr<const ScienceArtifact> artifact =
        _artifacts.find(id);
    if (!artifact) return false;
    _state.displayArtifact = artifact;
    _state.lastSuccessfulArtifact = artifact;
    return true;
}

void ScienceQueryService::clearArtifacts()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _artifacts.clear();
    _state.lastSuccessfulArtifact.reset();
    _state.lastSuccessfulPreviewArtifact.reset();
    _state.lastSuccessfulAnalysisArtifact.reset();
    _state.displayArtifact.reset();
}

ScienceJobSnapshot ScienceQueryService::snapshot()
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (!_providerActive || !_activeProvider) return _state;

    const ScienceProviderSnapshot providerSnapshot =
        _activeProvider->snapshot();
    if (providerSnapshot.generation != _activeProviderGeneration)
        return _state;

    _state.state = providerSnapshot.state;
    _state.progress = providerSnapshot.progress;
    _state.message = providerSnapshot.message;

    if (providerSnapshot.state == ScienceJobState::Ready)
    {
        if (!providerSnapshot.artifact)
        {
            _state.state = ScienceJobState::Failed;
            _state.progress = ScienceProgress();
            _state.progress.stage = ScienceProgressStage::Failed;
            _state.message =
                "science provider reported ready without an artifact";
        }
        else
        {
            auto artifact = std::make_shared<ScienceArtifact>(
                *providerSnapshot.artifact);
            artifact->query = _state.query;
            artifact->generation = _state.jobId;
            if (artifact->artifactId.empty())
                artifact->artifactId = _activeSourceId + "-" +
                    std::to_string(_state.jobId);

            std::vector<std::string> pinnedIds;
            const auto pin = [&pinnedIds](
                const std::shared_ptr<const ScienceArtifact>& retained)
            {
                if (retained &&
                    std::find(pinnedIds.begin(), pinnedIds.end(),
                              retained->artifactId) == pinnedIds.end())
                    pinnedIds.push_back(retained->artifactId);
            };
            pin(_state.displayArtifact);
            pin(_state.lastSuccessfulPreviewArtifact);
            pin(_state.lastSuccessfulAnalysisArtifact);
            std::string storeError;
            if (!_artifacts.put(artifact, pinnedIds, storeError))
            {
                _state.state = ScienceJobState::Failed;
                _state.progress = ScienceProgress();
                _state.progress.stage = ScienceProgressStage::Failed;
                _state.message = storeError;
            }
            else
            {
                const std::shared_ptr<const ScienceArtifact> retained =
                    std::move(artifact);
                if (isLegacyPreviewQuery(_state.query))
                {
                    _state.lastSuccessfulPreviewArtifact = retained;
                    _state.displayArtifact = retained;
                    _state.lastSuccessfulArtifact = retained;
                }
                else
                {
                    _state.lastSuccessfulAnalysisArtifact = retained;
                }

                const ScienceQueryCost cost = estimateUnlocked(_state.query);
                if (providerSnapshot.progress.elapsedSeconds > 0.0 &&
                    std::isfinite(providerSnapshot.progress.elapsedSeconds) &&
                    cost.resultCells > 0)
                {
                    _throughputCellsPerSecond[
                        throughputKey(_state.query)] =
                        static_cast<double>(cost.resultCells) /
                        providerSnapshot.progress.elapsedSeconds;
                }
            }
        }
        _providerActive = false;
    }
    else if (providerSnapshot.state == ScienceJobState::Failed ||
             providerSnapshot.state == ScienceJobState::Cancelled ||
             providerSnapshot.state == ScienceJobState::Unavailable ||
             providerSnapshot.state == ScienceJobState::Idle)
    {
        _providerActive = false;
    }
    return _state;
}

bool ScienceQueryService::validate(
    const GeoTemporalQuery& query, const ScienceSourceDescriptor& source,
    std::string& error) const
{
    if (source.health == ScienceSourceHealth::Unavailable)
    {
        error = "science source unavailable: " + source.healthMessage;
        return false;
    }
    const bool rasterRequest =
        query.outputKind == ScienceOutputKind::RasterLayer;
    if (rasterRequest && query.geometry.kind != ScienceGeometryKind::Point)
    {
        error = "unsupported science geometry: " +
            std::string(geometryName(query.geometry.kind));
        return false;
    }
    if (query.geometry.kind == ScienceGeometryKind::Point)
    {
        if (!source.capabilities.pointQuery)
        {
            error = "science source does not support point queries";
            return false;
        }
        if (!std::isfinite(query.geometry.point.latitude) ||
            query.geometry.point.latitude < -90.0 ||
            query.geometry.point.latitude > 90.0)
        {
            error = "latitude must be finite and inside [-90, 90]";
            return false;
        }
        if (!std::isfinite(query.geometry.point.longitude) ||
            query.geometry.point.longitude < -180.0 ||
            query.geometry.point.longitude > 180.0)
        {
            error = "longitude must be finite and inside [-180, 180]";
            return false;
        }
    }
    else
    {
        const bool supported = query.geometry.kind ==
                ScienceGeometryKind::BoundingBox
            ? source.capabilities.boundingBoxQuery
            : source.capabilities.currentViewQuery;
        if (!supported)
        {
            error = "unsupported science geometry: " +
                std::string(geometryName(query.geometry.kind));
            return false;
        }
        const ScienceWgs84Bounds& bounds = query.geometry.bounds;
        if (!std::isfinite(bounds.west) || !std::isfinite(bounds.south) ||
            !std::isfinite(bounds.east) || !std::isfinite(bounds.north) ||
            bounds.west < -180.0 || bounds.east > 180.0 ||
            bounds.south < -90.0 || bounds.north > 90.0 ||
            bounds.west >= bounds.east || bounds.south >= bounds.north)
        {
            error = "science query bounds are invalid";
            return false;
        }
    }
    if (rasterRequest &&
        (!std::isfinite(query.geometry.requestedSpanMeters) ||
         query.geometry.requestedSpanMeters <
             source.capabilities.minimumSpanMeters ||
         query.geometry.requestedSpanMeters >
             source.capabilities.maximumSpanMeters))
    {
        error = "requested span must be inside " + integerRange(
            source.capabilities.minimumSpanMeters,
            source.capabilities.maximumSpanMeters) + " meters";
        return false;
    }
    if (query.time.mode != ScienceTimeMode::ExplicitYears)
    {
        error = "unsupported science time selection: " +
            std::string(timeModeName(query.time.mode));
        return false;
    }
    if (!source.capabilities.explicitYears)
    {
        error = "science source does not support explicit years";
        return false;
    }
    if (rasterRequest && query.time.explicitYears.size() != 1)
    {
        error = "science query requires exactly one explicit year";
        return false;
    }
    if (!rasterRequest && (query.time.explicitYears.empty() ||
                           query.time.explicitYears.size() > 9))
    {
        error = "science query requires one to nine explicit years";
        return false;
    }
    std::vector<int> years = query.time.explicitYears;
    std::sort(years.begin(), years.end());
    if (std::adjacent_find(years.begin(), years.end()) != years.end())
    {
        error = "science explicit years must be unique";
        return false;
    }
    const auto invalidYear = std::find_if(
        years.begin(), years.end(), [&source](int year)
        {
            return year < source.firstYear || year > source.lastYear;
        });
    if (invalidYear != years.end())
    {
        error = "science year must be inside [" +
            std::to_string(source.firstYear) + ", " +
            std::to_string(source.lastYear) + ']';
        return false;
    }

    if (rasterRequest)
    {
        const auto visualization = std::find_if(
            source.visualizations.begin(), source.visualizations.end(),
            [&query](const ScienceVisualizationDescriptor& candidate)
            { return candidate.id == query.visualizationId; });
        if (visualization == source.visualizations.end())
        {
            error = "unknown science visualization: " +
                query.visualizationId;
            return false;
        }
        if (query.variables != visualization->channelVariables)
        {
            error = "science variables do not match visualization: " +
                query.visualizationId;
            return false;
        }
        if (query.aggregation != ScienceAggregation::None)
        {
            error = "unsupported science aggregation: " +
                std::string(aggregationName(query.aggregation));
            return false;
        }
        if (!source.capabilities.rasterLayerOutput)
        {
            error = "science source does not support raster-layer output";
            return false;
        }
        if (!isLegacyPreviewQuery(query))
        {
            error = "raster-layer output requires the exact preview signature";
            return false;
        }
    }
    else
    {
        bool outputSupported = false;
        switch (query.outputKind)
        {
        case ScienceOutputKind::Embedding:
            outputSupported = source.capabilities.embeddingOutput;
            break;
        case ScienceOutputKind::TimeSeries:
            outputSupported = source.capabilities.timeSeriesOutput;
            break;
        case ScienceOutputKind::Analysis:
            outputSupported = source.capabilities.analysisOutput;
            break;
        case ScienceOutputKind::Export:
            outputSupported = source.capabilities.exportOutput;
            break;
        default: break;
        }
        if (!outputSupported)
        {
            error = "unsupported science output: " +
                std::string(scienceOutputKindName(query.outputKind));
            return false;
        }
        if (query.variables != std::vector<std::string>({"embedding64"}))
        {
            error = "science 64D query requires embedding64";
            return false;
        }
        if (query.aggregation != ScienceAggregation::None &&
            query.aggregation != ScienceAggregation::Mean)
        {
            error = "unsupported science aggregation: " +
                std::string(aggregationName(query.aggregation));
            return false;
        }
        if (query.analysis.kind == ScienceAnalysisKind::RegionalChange &&
            query.geometry.kind == ScienceGeometryKind::Point)
        {
            error = "regional change requires a bounding-box grid";
            return false;
        }
        if (query.analysis.kind == ScienceAnalysisKind::PointSeries &&
            query.geometry.kind != ScienceGeometryKind::Point)
        {
            error = "point series requires point geometry";
            return false;
        }
        if (query.geometry.kind != ScienceGeometryKind::Point &&
            (query.analysis.gridSize < 1 || query.analysis.gridSize > 256))
        {
            error = "analysis grid size must be inside [1, 256]";
            return false;
        }
        if (query.geometry.kind != ScienceGeometryKind::Point &&
            query.analysis.gridSize == 256 &&
            !query.analysis.confirmedLargeRequest)
        {
            error = "256x256 analysis requires explicit confirmation";
            return false;
        }
        if (query.analysis.kind == ScienceAnalysisKind::RegionalChange &&
            years.size() > 2 && !query.analysis.confirmedLargeRequest)
        {
            error =
                "multi-year regional analysis requires explicit confirmation";
            return false;
        }
    }
    if (!std::isfinite(query.targetResolutionMeters) ||
        query.targetResolutionMeters < 0.0)
    {
        error = "target resolution must be finite and non-negative";
        return false;
    }
    if (query.targetResolutionMeters > 0.0 &&
        query.targetResolutionMeters < source.nativeResolutionMeters &&
        !query.limits.allowUpsampling)
    {
        error = "target resolution requires unsupported upsampling";
        return false;
    }
    if (!std::isfinite(query.limits.maximumAreaSquareMeters) ||
        query.limits.maximumAreaSquareMeters < 0.0 ||
        !std::isfinite(query.limits.maximumDurationSeconds) ||
        query.limits.maximumDurationSeconds < 0.0)
    {
        error = "science query limits must be finite and non-negative";
        return false;
    }
    ScienceQueryCost cost;
    try
    {
        cost = estimateUnlocked(query);
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
    if (query.limits.maximumBytes != 0 &&
        cost.sourceBytesUpperBound > query.limits.maximumBytes)
    {
        error = "analysis exceeds maximum source bytes";
        return false;
    }
    if (query.limits.maximumMemoryBytes != 0 &&
        cost.residentBytesUpperBound > query.limits.maximumMemoryBytes)
    {
        error = "analysis exceeds maximum memory";
        return false;
    }
    if (query.limits.maximumResultCells != 0 &&
        cost.resultCells > query.limits.maximumResultCells)
    {
        error = "analysis exceeds maximum result cells";
        return false;
    }
    if (query.limits.maximumDurationSeconds != 0.0 &&
        cost.durationDeterminate &&
        cost.estimatedDurationSeconds >
            query.limits.maximumDurationSeconds)
    {
        error = "analysis exceeds maximum duration";
        return false;
    }
    error.clear();
    return true;
}

void ScienceQueryService::cancelActiveProvider()
{
    if (_providerActive && _activeProvider)
        _activeProvider->cancel(_activeProviderGeneration);
    _providerActive = false;
}
}
