#ifndef OSGSOL_SCIENCE_WORKBENCH_QUERY_PLAN_H
#define OSGSOL_SCIENCE_WORKBENCH_QUERY_PLAN_H

#include "science_query_builder.h"
#include "science_workbench_methods.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

inline std::vector<ScienceWorkbenchMethod>
scienceWorkbenchMethodsForSource(
    const earthscience::ScienceSourceDescriptor& source)
{
    return scienceWorkbenchMethodsForSourceId(
        source.id, source.capabilities.timeSeriesOutput,
        source.capabilities.analysisOutput,
        source.capabilities.rasterLayerOutput);
}

inline const ScienceWorkbenchMethod* findScienceWorkbenchMethod(
    const earthscience::ScienceSourceDescriptor& source,
    const std::string& methodId)
{
    // Kept thread-local so callers can use the returned value while rendering
    // one frame without owning a second method list.
    thread_local std::vector<ScienceWorkbenchMethod> methods;
    methods = scienceWorkbenchMethodsForSource(source);
    for (const ScienceWorkbenchMethod& method : methods)
        if (method.id == methodId) return &method;
    return nullptr;
}

inline std::string scienceWorkbenchDefaultMethodId(
    const earthscience::ScienceSourceDescriptor& source)
{
    const std::vector<ScienceWorkbenchMethod> methods =
        scienceWorkbenchMethodsForSource(source);
    return methods.empty() ? std::string() : methods.front().id;
}

inline earthscience::ScienceWgs84Point scienceWorkbenchGeometryCenter(
    const earthscience::ScienceGeometry& geometry)
{
    if (geometry.kind == earthscience::ScienceGeometryKind::Point)
        return geometry.point;
    return {
        (geometry.bounds.south + geometry.bounds.north) * 0.5,
        (geometry.bounds.west + geometry.bounds.east) * 0.5,
    };
}

inline double scienceWorkbenchGeometrySpanMeters(
    const earthscience::ScienceGeometry& geometry)
{
    if (std::isfinite(geometry.requestedSpanMeters) &&
        geometry.requestedSpanMeters > 0.0)
        return geometry.requestedSpanMeters;
    if (geometry.kind == earthscience::ScienceGeometryKind::BoundingBox)
    {
        constexpr double METERS_PER_DEGREE = 111320.0;
        const double latitude = (geometry.bounds.south +
                                 geometry.bounds.north) * 0.5;
        const double northSouth =
            (geometry.bounds.north - geometry.bounds.south) *
            METERS_PER_DEGREE;
        const double eastWest =
            (geometry.bounds.east - geometry.bounds.west) *
            METERS_PER_DEGREE *
            std::max(0.01, std::cos(latitude * 3.14159265358979323846 / 180.0));
        return std::max(northSouth, eastWest);
    }
    return 20000.0;
}

inline std::string scienceWorkbenchUtcBoundary(
    int year, bool endOfYear)
{
    std::ostringstream value;
    value << std::setfill('0') << std::setw(4) << year
          << (endOfYear ? "-12-31T23:59:59Z" : "-01-01T00:00:00Z");
    return value.str();
}

inline bool buildScienceWorkbenchQuery(
    const earthscience::ScienceSourceDescriptor& source,
    const std::string& requestedMethodId,
    const earthscience::ScienceGeometry& target,
    int firstYear, int lastYear,
    earthscience::GeoTemporalQuery& query,
    std::string& error)
{
    query = earthscience::GeoTemporalQuery();
    const std::string methodId = requestedMethodId.empty()
        ? scienceWorkbenchDefaultMethodId(source) : requestedMethodId;
    if (!findScienceWorkbenchMethod(source, methodId))
    {
        error = "workbench-method-not-supported-for-source";
        return false;
    }
    const earthscience::ScienceWgs84Point center =
        scienceWorkbenchGeometryCenter(target);
    if (!std::isfinite(center.latitude) ||
        !std::isfinite(center.longitude) ||
        center.latitude < -90.0 || center.latitude > 90.0 ||
        center.longitude < -180.0 || center.longitude > 180.0)
    {
        error = "workbench-target-invalid";
        return false;
    }
    const double spanMeters = scienceWorkbenchGeometrySpanMeters(target);

    if (isScienceWorkbenchEra5Source(source.id))
    {
        query = makeScienceVariablePointSeriesQuery(
            source, center.latitude, center.longitude, firstYear, lastYear);
    }
    else if (source.id == "alphaearth-foundations" &&
             methodId == "change-map")
    {
        earthscience::ScienceAnalysisOptions options;
        options.metrics = {
            earthscience::ScienceMetric::CosineDistance,
            earthscience::ScienceMetric::EuclideanDistance,
        };
        query = makeScienceRegionalAnalysisQuery(
            source, center.latitude, center.longitude, spanMeters,
            firstYear, lastYear, options);
    }
    else if (source.id == "alphaearth-foundations")
    {
        query = makeSciencePointSeriesQuery(
            source, center.latitude, center.longitude, firstYear, lastYear);
        if (methodId == "direction-change")
        {
            query.analysis.metrics = {
                earthscience::ScienceMetric::DotProduct,
                earthscience::ScienceMetric::CosineSimilarity,
                earthscience::ScienceMetric::CosineDistance,
                earthscience::ScienceMetric::EuclideanDistance,
                earthscience::ScienceMetric::AngularDistance,
            };
        }
    }
    else if (source.id == "sentinel-2-l2a")
    {
        const int year = std::clamp(lastYear, source.firstYear, source.lastYear);
        query = makeSentinel2PreviewIntervalQuery(
            source, center.latitude, center.longitude,
            scienceWorkbenchUtcBoundary(year, false),
            scienceWorkbenchUtcBoundary(year, true),
            40.0, spanMeters);
    }
    else if (source.id == "copernicus-dem-glo-30")
    {
        query = makeCopernicusDemPreviewQuery(
            source, center.latitude, center.longitude, spanMeters);
    }
    else
    {
        error = "workbench-source-query-not-implemented";
        return false;
    }
    query.analysis.confirmedLargeRequest = true;
    error.clear();
    return true;
}

#endif
