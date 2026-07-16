#ifndef EARTH_SCIENCE_QUERY_BUILDER_H
#define EARTH_SCIENCE_QUERY_BUILDER_H

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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

inline std::vector<int> makeScienceUiYearRange(
    const earthscience::ScienceSourceDescriptor& source,
    int firstYear, int lastYear)
{
    const int first = std::clamp(
        std::min(firstYear, lastYear), source.firstYear, source.lastYear);
    const int last = std::clamp(
        std::max(firstYear, lastYear), source.firstYear, source.lastYear);
    std::vector<int> years;
    years.reserve(static_cast<std::size_t>(last - first + 1));
    for (int year = first; year <= last; ++year) years.push_back(year);
    return years;
}

inline earthscience::GeoTemporalQuery makeSciencePointSeriesQuery(
    const earthscience::ScienceSourceDescriptor& source,
    double latitude, double longitude, int firstYear, int lastYear)
{
    earthscience::GeoTemporalQuery query;
    query.sourceId = source.id;
    query.geometry.kind = earthscience::ScienceGeometryKind::Point;
    query.geometry.point.latitude = latitude;
    query.geometry.point.longitude = longitude;
    query.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
    query.time.explicitYears = makeScienceUiYearRange(
        source, firstYear, lastYear);
    query.variables = {"embedding64"};
    query.targetResolutionMeters = source.nativeResolutionMeters;
    query.aggregation = earthscience::ScienceAggregation::None;
    query.outputKind = earthscience::ScienceOutputKind::TimeSeries;
    query.purpose = "interactive ScienceEarth 64D point series";
    query.priority = earthscience::SciencePriority::InteractiveResearch;
    query.analysis.kind = earthscience::ScienceAnalysisKind::PointSeries;
    if (!query.time.explicitYears.empty())
    {
        query.analysis.baselineYear = query.time.explicitYears.front();
        query.analysis.comparisonYear = query.time.explicitYears.back();
    }
    return query;
}

inline earthscience::GeoTemporalQuery makeScienceRegionalAnalysisQuery(
    const earthscience::ScienceSourceDescriptor& source,
    double latitude, double longitude, double spanMeters,
    int baselineYear, int comparisonYear,
    const earthscience::ScienceAnalysisOptions& options)
{
    constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
    constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
    constexpr double PI = 3.14159265358979323846;
    const double minimumSpan = source.capabilities.minimumSpanMeters > 0.0
        ? source.capabilities.minimumSpanMeters : 2560.0;
    const double maximumSpan =
        source.capabilities.maximumSpanMeters >= minimumSpan
            ? source.capabilities.maximumSpanMeters : 81920.0;
    const double clampedSpan = std::clamp(
        spanMeters, minimumSpan, maximumSpan);
    const double centerLatitude = latitude;
    const double centerLongitude = longitude;
    const double latitudeHalfSpan =
        clampedSpan * 0.5 / METERS_PER_LATITUDE_DEGREE;
    const double longitudeScale = METERS_PER_LONGITUDE_DEGREE *
        std::max(0.01, std::cos(centerLatitude * PI / 180.0));
    const double longitudeHalfSpan = clampedSpan * 0.5 / longitudeScale;

    int first = std::clamp(
        std::min(baselineYear, comparisonYear),
        source.firstYear, source.lastYear);
    int second = std::clamp(
        std::max(baselineYear, comparisonYear),
        source.firstYear, source.lastYear);
    if (first == second && source.firstYear < source.lastYear)
    {
        if (second < source.lastYear) ++second;
        else --first;
    }

    earthscience::GeoTemporalQuery query;
    query.sourceId = source.id;
    query.geometry.kind = earthscience::ScienceGeometryKind::BoundingBox;
    query.geometry.bounds = {
        centerLongitude - longitudeHalfSpan,
        centerLatitude - latitudeHalfSpan,
        centerLongitude + longitudeHalfSpan,
        centerLatitude + latitudeHalfSpan};
    query.geometry.requestedSpanMeters = clampedSpan;
    query.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
    query.time.explicitYears = {first, second};
    query.variables = {"embedding64"};
    query.targetResolutionMeters = source.nativeResolutionMeters;
    query.aggregation = earthscience::ScienceAggregation::None;
    query.outputKind = earthscience::ScienceOutputKind::Analysis;
    query.purpose = "interactive ScienceEarth 64D regional change";
    query.priority = earthscience::SciencePriority::InteractiveResearch;
    query.analysis = options;
    query.analysis.kind = earthscience::ScienceAnalysisKind::RegionalChange;
    query.analysis.baselineYear = first;
    query.analysis.comparisonYear = second;
    if (query.analysis.gridSize <= 0) query.analysis.gridSize = 128;
    return query;
}

#endif
