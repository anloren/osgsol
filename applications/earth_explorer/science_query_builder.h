#ifndef EARTH_SCIENCE_QUERY_BUILDER_H
#define EARTH_SCIENCE_QUERY_BUILDER_H

#include <algorithm>
#include <string>

#include <ScienceQueryTypes.h>

inline const earthscience::ScienceVisualizationDescriptor*
findScienceVisualization(
    const earthscience::ScienceSourceDescriptor& source,
    const std::string& visualizationId)
{
    for (const auto& visualization : source.visualizations)
        if (visualization.id == visualizationId) return &visualization;
    return source.visualizations.empty() ? nullptr : &source.visualizations.front();
}

inline earthscience::GeoTemporalQuery makeSciencePointQuery(
    const earthscience::ScienceSourceDescriptor& source,
    const earthscience::ScienceVisualizationDescriptor& visualization,
    double latitude, double longitude, int year,
    double requestedSpanMeters)
{
    const double minimumSpan = source.capabilities.minimumSpanMeters > 0.0
        ? source.capabilities.minimumSpanMeters : 2560.0;
    const double maximumSpan = source.capabilities.maximumSpanMeters >= minimumSpan
        ? source.capabilities.maximumSpanMeters : 81920.0;

    earthscience::GeoTemporalQuery query;
    query.sourceId = source.id;
    query.geometry.kind = earthscience::ScienceGeometryKind::Point;
    query.geometry.point.latitude = latitude;
    query.geometry.point.longitude = longitude;
    query.geometry.requestedSpanMeters = std::clamp(
        requestedSpanMeters, minimumSpan, maximumSpan);
    query.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
    query.time.explicitYears = {
        std::clamp(year, source.firstYear, source.lastYear)};
    query.variables = visualization.channelVariables;
    query.targetResolutionMeters = source.nativeResolutionMeters;
    query.aggregation = earthscience::ScienceAggregation::None;
    query.outputKind = earthscience::ScienceOutputKind::RasterLayer;
    query.purpose = "visible ScienceEarth research layer";
    query.priority = earthscience::SciencePriority::Visible;
    query.visualizationId = visualization.id;
    return query;
}

#endif
