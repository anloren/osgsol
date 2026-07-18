#include "Sentinel2Provider.h"

#include <cmath>
#include <ctime>
#include <utility>
#include <vector>

namespace earthscience
{
namespace
{
    int currentUtcYear()
    {
        const std::time_t now = std::time(nullptr);
        std::tm utc = {};
        return gmtime_r(&now, &utc) ? utc.tm_year + 1900 : 2026;
    }

    bool isNoMatchingScene(const ScienceProviderSnapshot& state)
    {
        return state.state == ScienceJobState::Failed &&
            state.message.rfind("No Sentinel-2 scene", 0) == 0;
    }
}

ScienceSourceDescriptor describeSentinel2()
{
    ScienceSourceDescriptor descriptor;
    descriptor.id = "sentinel-2-l2a";
    descriptor.name = "Sentinel-2 Level-2A";
    descriptor.category = "optical satellite natural-color scene";
    descriptor.providerVersion = "earth-search-v1";
    descriptor.attribution =
        "Copernicus Sentinel data · Element 84 Earth Search · AWS Open Data";
    descriptor.firstYear = 2015;
    descriptor.lastYear = currentUtcYear();
    descriptor.nativeResolutionMeters = 10.0;
    descriptor.componentCount = 3;
    descriptor.experimental = true;
    descriptor.health = ScienceSourceHealth::Ready;
    descriptor.healthMessage = "Ready on demand";
    descriptor.variables.push_back(
        {"visual", "True color image", "display DN", "display RGB", 3});

    ScienceVisualizationDescriptor visualization;
    visualization.id = "natural-color-visual";
    visualization.displayName = "Natural color scene";
    visualization.kind = ScienceVisualizationKind::NaturalColor;
    visualization.channelVariables = {"visual"};
    visualization.displayMinimum = 0.0;
    visualization.displayMaximum = 255.0;
    visualization.legend =
        "Rendered Sentinel-2 true-color display product; not raw reflectance "
        "or a cloud-free composite";
    descriptor.visualizations.push_back(std::move(visualization));

    descriptor.capabilities.pointQuery = true;
    descriptor.capabilities.intervalTime = true;
    descriptor.capabilities.rasterLayerOutput = true;
    descriptor.capabilities.minimumSpanMeters = 2560.0;
    descriptor.capabilities.maximumSpanMeters = 81920.0;
    return descriptor;
}

Sentinel2Provider::Sentinel2Provider()
    : _runtime(new Sentinel2Runtime())
{
}

Sentinel2Provider::Sentinel2Provider(std::unique_ptr<ISentinel2Io> io)
    : _runtime(new Sentinel2Runtime(std::move(io)))
{
}

Sentinel2Provider::~Sentinel2Provider() = default;

ScienceSourceDescriptor Sentinel2Provider::descriptor() const
{
    ScienceSourceDescriptor result = describeSentinel2();
    const ScienceProviderSnapshot state = _runtime->snapshot();
    if (state.state == ScienceJobState::Queued ||
        state.state == ScienceJobState::Fetching)
    {
        result.health = ScienceSourceHealth::Busy;
        result.healthMessage = state.message;
    }
    else if (state.state == ScienceJobState::Failed &&
             !isNoMatchingScene(state))
    {
        result.health = ScienceSourceHealth::Degraded;
        result.healthMessage = state.message;
    }
    return result;
}

bool Sentinel2Provider::validateQuery(
    const GeoTemporalQuery& query, std::string& error) const
{
    if (query.sourceId != "sentinel-2-l2a")
        error = "Sentinel-2 query source id changed";
    else if (query.geometry.kind != ScienceGeometryKind::Point)
        error = "Sentinel-2 preview requires point geometry";
    else if (query.time.mode != ScienceTimeMode::Interval)
        error = "Sentinel-2 preview requires an interval";
    else if (query.variables != std::vector<std::string>({"visual"}))
        error = "Sentinel-2 preview requires the visual display product";
    else if (query.outputKind != ScienceOutputKind::RasterLayer)
        error = "Sentinel-2 preview only emits a raster layer";
    else if (query.visualizationId != "natural-color-visual")
        error = "Sentinel-2 preview requires natural-color-visual";
    else if (query.aggregation != ScienceAggregation::None)
        error = "Sentinel-2 preview does not aggregate scenes";
    else if (query.analysis.kind != ScienceAnalysisKind::None)
        error = "Sentinel-2 preview does not run analysis";
    else if (!std::isfinite(query.targetResolutionMeters) ||
             std::abs(query.targetResolutionMeters - 10.0) > 1e-9)
        error = "Sentinel-2 preview requires the native 10 m target resolution";
    else if (!std::isfinite(
                 query.sceneFilters.maximumCloudCoverPercent) ||
             query.sceneFilters.maximumCloudCoverPercent < 0.0 ||
             query.sceneFilters.maximumCloudCoverPercent > 100.0)
        error = "Sentinel-2 cloud threshold must be inside [0, 100]";
    else if (query.sceneFilters.maximumScenes != 10)
        error = "Sentinel-2 preview requires exactly ten bounded candidates";
    else
    {
        ScienceWgs84Bounds bounds;
        if (!makeSentinel2PointSearchBounds(query.geometry, bounds, error))
            return false;
        std::string url;
        if (!buildSentinel2SearchUrl(
                bounds, query.time.intervalStart, query.time.intervalEnd,
                query.sceneFilters.maximumCloudCoverPercent,
                query.sceneFilters.maximumScenes, url, error))
            return false;
        error.clear();
        return true;
    }
    return false;
}

std::uint64_t Sentinel2Provider::submit(const GeoTemporalQuery& query)
{
    std::string error;
    return validateQuery(query, error) ? _runtime->submit(query) : 0;
}

ScienceProviderSnapshot Sentinel2Provider::snapshot() const
{
    return _runtime->snapshot();
}

void Sentinel2Provider::cancel(std::uint64_t generation)
{
    _runtime->cancel(generation);
}

void Sentinel2Provider::clear()
{
    _runtime->clear();
}
}
