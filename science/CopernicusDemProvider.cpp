#include "CopernicusDemProvider.h"

#include <cmath>
#include <utility>
#include <vector>

namespace earthscience
{
namespace
{
    constexpr double MINIMUM_SPAN_METERS = 2560.0;
    constexpr double MAXIMUM_SPAN_METERS = 81920.0;

    bool isMissingCoverage(const ScienceProviderSnapshot& state)
    {
        return state.state == ScienceJobState::Failed &&
            state.message.rfind("No Copernicus DEM", 0) == 0;
    }
}

ScienceSourceDescriptor describeCopernicusDem()
{
    ScienceSourceDescriptor descriptor;
    descriptor.id = "copernicus-dem-glo-30";
    descriptor.name = "Copernicus DEM GLO-30";
    descriptor.category = "static digital surface model elevation";
    descriptor.providerVersion = "aws-glo30-2021";
    descriptor.attribution =
        "Copernicus DEM GLO-30 · European Union and ESA · AWS Open Data";
    descriptor.firstYear = 2021;
    descriptor.lastYear = 2021;
    descriptor.nativeResolutionMeters = 30.0;
    descriptor.componentCount = 1;
    descriptor.experimental = true;
    descriptor.health = ScienceSourceHealth::Ready;
    descriptor.healthMessage = "Ready on demand";
    descriptor.variables.push_back({
        "surface_elevation", "DSM surface elevation", "m",
        "digital surface model height", 1, 4});

    ScienceVisualizationDescriptor visualization;
    visualization.id = "surface-elevation-hypsometric";
    visualization.displayName = "Surface elevation (hypsometric)";
    visualization.kind = ScienceVisualizationKind::Continuous;
    visualization.channelVariables = {"surface_elevation"};
    visualization.displayMinimum = -500.0;
    visualization.displayMaximum = 9000.0;
    visualization.legend =
        "Fixed hypsometric DSM display in metres relative to EGM2008; "
        "buildings, infrastructure, and vegetation may contribute to height";
    descriptor.visualizations.push_back(std::move(visualization));

    descriptor.capabilities.pointQuery = true;
    descriptor.capabilities.instantTime = true;
    descriptor.capabilities.rasterLayerOutput = true;
    descriptor.capabilities.minimumSpanMeters = MINIMUM_SPAN_METERS;
    descriptor.capabilities.maximumSpanMeters = MAXIMUM_SPAN_METERS;
    return descriptor;
}

CopernicusDemProvider::CopernicusDemProvider()
    : _runtime(new CopernicusDemRuntime())
{
}

CopernicusDemProvider::CopernicusDemProvider(
    std::unique_ptr<ICopernicusDemIo> io)
    : _runtime(new CopernicusDemRuntime(std::move(io)))
{
}

CopernicusDemProvider::~CopernicusDemProvider() = default;

ScienceSourceDescriptor CopernicusDemProvider::descriptor() const
{
    ScienceSourceDescriptor result = describeCopernicusDem();
    const ScienceProviderSnapshot state = _runtime->snapshot();
    if (state.state == ScienceJobState::Queued ||
        state.state == ScienceJobState::Fetching)
    {
        result.health = ScienceSourceHealth::Busy;
        result.healthMessage = state.message;
    }
    else if (state.state == ScienceJobState::Failed &&
             !isMissingCoverage(state))
    {
        result.health = ScienceSourceHealth::Degraded;
        result.healthMessage = state.message;
    }
    return result;
}

bool CopernicusDemProvider::validateQuery(
    const GeoTemporalQuery& query, std::string& error) const
{
    if (query.sourceId != "copernicus-dem-glo-30")
        error = "Copernicus DEM query source id changed";
    else if (query.geometry.kind != ScienceGeometryKind::Point)
        error = "Copernicus DEM preview requires point geometry";
    else if (query.time.mode != ScienceTimeMode::Instant)
        error = "Copernicus DEM is a static surface model, not a time series";
    else if (query.time.publicationTime != "2021")
        error = "Copernicus DEM preview requires the public 2021 release";
    else if (query.time.instant != "2021" ||
             !query.time.intervalStart.empty() ||
             !query.time.intervalEnd.empty() ||
             !query.time.explicitYears.empty())
        error = "Copernicus DEM query contains unsupported acquisition time";
    else if (query.variables !=
             std::vector<std::string>({"surface_elevation"}))
        error = "Copernicus DEM preview requires surface_elevation";
    else if (query.outputKind != ScienceOutputKind::RasterLayer)
        error = "Copernicus DEM preview only emits a raster layer";
    else if (query.visualizationId != "surface-elevation-hypsometric")
        error = "Copernicus DEM preview requires the hypsometric display";
    else if (query.aggregation != ScienceAggregation::None)
        error = "Copernicus DEM preview does not aggregate cells";
    else if (query.analysis.kind != ScienceAnalysisKind::None)
        error = "Copernicus DEM analysis is not implemented in this slice";
    else if (!std::isfinite(query.targetResolutionMeters) ||
             std::abs(query.targetResolutionMeters - 30.0) > 1e-9)
        error = "Copernicus DEM preview requires the nominal 30 m resolution";
    else if (!std::isfinite(query.geometry.requestedSpanMeters) ||
             query.geometry.requestedSpanMeters < MINIMUM_SPAN_METERS ||
             query.geometry.requestedSpanMeters > MAXIMUM_SPAN_METERS)
        error = "Copernicus DEM span must be inside [2560, 81920] metres";
    else
    {
        std::vector<CopernicusDemCell> cells;
        if (!deriveCopernicusDemCells(query.geometry, cells, error))
            return false;
        error.clear();
        return true;
    }
    return false;
}

std::uint64_t CopernicusDemProvider::submit(const GeoTemporalQuery& query)
{
    std::string error;
    return validateQuery(query, error) ? _runtime->submit(query) : 0;
}

ScienceProviderSnapshot CopernicusDemProvider::snapshot() const
{
    return _runtime->snapshot();
}

void CopernicusDemProvider::cancel(std::uint64_t generation)
{
    _runtime->cancel(generation);
}

void CopernicusDemProvider::clear()
{
    _runtime->clear();
}
}
