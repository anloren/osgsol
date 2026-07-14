#include "ScienceQueryService.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
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

    const char* outputName(ScienceOutputKind output)
    {
        switch (output)
        {
        case ScienceOutputKind::RasterLayer: return "raster-layer";
        case ScienceOutputKind::Table: return "table";
        case ScienceOutputKind::VectorFeatures: return "vector-features";
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
}

ScienceQueryService::ScienceQueryService(
    std::unique_ptr<ScienceSourceRegistry> registry)
    : _registry(std::move(registry))
{
    if (!_registry)
    {
        _state.state = ScienceJobState::Unavailable;
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

std::uint64_t ScienceQueryService::submit(const GeoTemporalQuery& query)
{
    std::lock_guard<std::mutex> lock(_mutex);
    const std::uint64_t jobId = ++_nextJobId;
    const std::shared_ptr<const ScienceArtifact> lastArtifact =
        _state.lastSuccessfulArtifact;

    ScienceJobSnapshot next;
    next.jobId = jobId;
    next.query = query;
    next.lastSuccessfulArtifact = lastArtifact;

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
        next.message = error;
        _state = std::move(next);
        return jobId;
    }

    const std::uint64_t providerGeneration = provider->submit(query);
    if (providerGeneration == 0)
    {
        next.state = ScienceJobState::Failed;
        next.message = "science provider rejected query";
        _state = std::move(next);
        return jobId;
    }

    _activeProvider = provider;
    _activeSourceId = query.sourceId;
    _activeProviderGeneration = providerGeneration;
    _providerActive = true;
    next.state = ScienceJobState::Queued;
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
    _state.progress = 0.0f;
    _state.message = "Cancelled";
}

void ScienceQueryService::clearArtifact()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _state.lastSuccessfulArtifact.reset();
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
    _state.progress = std::clamp(providerSnapshot.progress, 0.0f, 1.0f);
    _state.message = providerSnapshot.message;

    if (providerSnapshot.state == ScienceJobState::Ready)
    {
        if (!providerSnapshot.artifact)
        {
            _state.state = ScienceJobState::Failed;
            _state.progress = 0.0f;
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
            _state.lastSuccessfulArtifact = std::move(artifact);
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
    if (query.geometry.kind != ScienceGeometryKind::Point)
    {
        error = "unsupported science geometry: " +
            std::string(geometryName(query.geometry.kind));
        return false;
    }
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
    if (!std::isfinite(query.geometry.requestedSpanMeters) ||
        query.geometry.requestedSpanMeters <
            source.capabilities.minimumSpanMeters ||
        query.geometry.requestedSpanMeters >
            source.capabilities.maximumSpanMeters)
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
    if (query.time.explicitYears.size() != 1)
    {
        error = "science query requires exactly one explicit year";
        return false;
    }
    const int year = query.time.explicitYears.front();
    if (year < source.firstYear || year > source.lastYear)
    {
        error = "science year must be inside [" +
            std::to_string(source.firstYear) + ", " +
            std::to_string(source.lastYear) + ']';
        return false;
    }

    const auto visualization = std::find_if(
        source.visualizations.begin(), source.visualizations.end(),
        [&query](const ScienceVisualizationDescriptor& candidate)
        { return candidate.id == query.visualizationId; });
    if (visualization == source.visualizations.end())
    {
        error = "unknown science visualization: " + query.visualizationId;
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
    if (query.outputKind != ScienceOutputKind::RasterLayer)
    {
        error = "unsupported science output: " +
            std::string(outputName(query.outputKind));
        return false;
    }
    if (!source.capabilities.rasterLayerOutput)
    {
        error = "science source does not support raster-layer output";
        return false;
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
