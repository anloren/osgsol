#include "science_workbench_query_plan.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void expect(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

earthscience::ScienceSourceDescriptor source(
    const std::string& id, int firstYear, int lastYear,
    double resolutionMeters)
{
    earthscience::ScienceSourceDescriptor value;
    value.id = id;
    value.name = id;
    value.firstYear = firstYear;
    value.lastYear = lastYear;
    value.nativeResolutionMeters = resolutionMeters;
    value.capabilities.pointQuery = true;
    value.capabilities.minimumSpanMeters = 2560.0;
    value.capabilities.maximumSpanMeters = 81920.0;
    return value;
}

earthscience::ScienceGeometry point(double latitude, double longitude,
                                    double spanMeters = 20000.0)
{
    earthscience::ScienceGeometry value;
    value.kind = earthscience::ScienceGeometryKind::Point;
    value.point.latitude = latitude;
    value.point.longitude = longitude;
    value.requestedSpanMeters = spanMeters;
    return value;
}

void testEveryRegisteredScienceSourceHasExecutableMethods()
{
    earthscience::ScienceSourceDescriptor alpha = source(
        "alphaearth-foundations", 2017, 2025, 10.0);
    alpha.capabilities.explicitYears = true;
    alpha.capabilities.timeSeriesOutput = true;
    alpha.capabilities.analysisOutput = true;
    alpha.capabilities.boundingBoxQuery = true;
    alpha.variables.push_back(
        {"embedding64", "Embedding A00-A63", "1", "embedding", 64});

    earthscience::ScienceSourceDescriptor era5Land = source(
        "era5-land-surface-history", 1950, 2025, 11100.0);
    era5Land.capabilities.explicitYears = true;
    era5Land.capabilities.timeSeriesOutput = true;
    era5Land.variables = {
        {"temperature_2m_mean", "Temperature", "°C"},
        {"relative_humidity_2m_mean", "Humidity", "%"},
        {"soil_moisture_0_to_100cm_mean", "Soil moisture", "m³/m³"},
    };

    earthscience::ScienceSourceDescriptor era5Climate = source(
        "era5-agricultural-climate", 1940, 2025, 27800.0);
    era5Climate.capabilities.explicitYears = true;
    era5Climate.capabilities.timeSeriesOutput = true;
    era5Climate.variables = {
        {"temperature_2m_mean", "Temperature", "°C"},
        {"precipitation_sum", "Precipitation", "mm"},
        {"et0_fao_evapotranspiration", "ET0", "mm"},
        {"shortwave_radiation_sum", "Radiation", "MJ/m²"},
        {"relative_humidity_2m_mean", "Humidity", "%"},
        {"wind_speed_10m_mean", "Wind", "km/h"},
    };

    earthscience::ScienceSourceDescriptor sentinel = source(
        "sentinel-2-l2a", 2015, 2026, 10.0);
    sentinel.capabilities.intervalTime = true;
    sentinel.capabilities.rasterLayerOutput = true;
    sentinel.variables.push_back({"visual", "True color", "display DN"});

    earthscience::ScienceSourceDescriptor dem = source(
        "copernicus-dem-glo-30", 2021, 2021, 30.0);
    dem.capabilities.instantTime = true;
    dem.capabilities.rasterLayerOutput = true;
    dem.variables.push_back(
        {"surface_elevation", "Surface elevation", "m"});

    const struct Expected
    {
        earthscience::ScienceSourceDescriptor value;
        std::vector<std::string> methods;
    } expected[] = {
        {alpha, {"annual-summary", "change-map", "direction-change"}},
        {era5Land, {"annual-summary"}},
        {era5Climate, {"annual-summary"}},
        {sentinel, {"satellite-preview"}},
        {dem, {"terrain-preview"}},
    };

    for (const Expected& item : expected)
    {
        const std::vector<ScienceWorkbenchMethod> methods =
            scienceWorkbenchMethodsForSource(item.value);
        expect(methods.size() == item.methods.size(),
               item.value.id + " exposes the wrong number of methods");
        for (std::size_t index = 0; index < methods.size(); ++index)
        {
            expect(methods[index].id == item.methods[index],
                   item.value.id + " exposes a wrong method");
            expect(!methods[index].label.empty() &&
                       !methods[index].summary.empty() &&
                       !methods[index].runLabel.empty(),
                   item.value.id + " method lacks user-facing copy");
        }
    }
}

void testEveryExposedMethodBuildsARealQuery()
{
    const earthscience::ScienceGeometry target = point(22.5431, 114.0579);
    std::string error;

    earthscience::ScienceSourceDescriptor alpha = source(
        "alphaearth-foundations", 2017, 2025, 10.0);
    alpha.capabilities.explicitYears = true;
    alpha.capabilities.timeSeriesOutput = true;
    alpha.capabilities.analysisOutput = true;
    alpha.capabilities.boundingBoxQuery = true;
    alpha.variables.push_back(
        {"embedding64", "Embedding A00-A63", "1", "embedding", 64});

    earthscience::GeoTemporalQuery alphaSeries;
    expect(buildScienceWorkbenchQuery(
               alpha, "annual-summary", target, 2017, 2025,
               alphaSeries, error),
           "AlphaEarth annual-summary must build: " + error);
    expect(alphaSeries.outputKind ==
               earthscience::ScienceOutputKind::TimeSeries &&
               alphaSeries.analysis.kind ==
                   earthscience::ScienceAnalysisKind::PointSeries,
           "AlphaEarth annual-summary must be a point time series");

    earthscience::GeoTemporalQuery alphaDirection;
    expect(buildScienceWorkbenchQuery(
               alpha, "direction-change", target, 2017, 2025,
               alphaDirection, error),
           "AlphaEarth direction-change must build: " + error);
    expect(alphaDirection.analysis.metrics.size() == 5,
           "AlphaEarth direction-change must calculate all five metrics");

    earthscience::GeoTemporalQuery alphaMap;
    expect(buildScienceWorkbenchQuery(
               alpha, "change-map", target, 2017, 2025, alphaMap, error),
           "AlphaEarth change-map must build: " + error);
    expect(alphaMap.geometry.kind ==
               earthscience::ScienceGeometryKind::BoundingBox &&
               alphaMap.outputKind ==
                   earthscience::ScienceOutputKind::Analysis &&
               alphaMap.analysis.kind ==
                   earthscience::ScienceAnalysisKind::RegionalChange,
           "AlphaEarth change-map must build a bounded regional analysis");

    earthscience::ScienceSourceDescriptor era5 = source(
        "era5-agricultural-climate", 1940, 2025, 27800.0);
    era5.capabilities.explicitYears = true;
    era5.capabilities.timeSeriesOutput = true;
    era5.variables = {
        {"temperature_2m_mean", "Temperature", "°C"},
        {"precipitation_sum", "Precipitation", "mm"},
    };
    earthscience::GeoTemporalQuery era5Query;
    expect(buildScienceWorkbenchQuery(
               era5, "annual-summary", target, 2017, 2025,
               era5Query, error),
           "ERA5 annual-summary must build: " + error);
    expect(era5Query.variables.size() == era5.variables.size() &&
               era5Query.outputKind ==
                   earthscience::ScienceOutputKind::TimeSeries,
           "ERA5 annual-summary must request every declared scalar variable");

    earthscience::ScienceSourceDescriptor sentinel = source(
        "sentinel-2-l2a", 2015, 2026, 10.0);
    sentinel.capabilities.intervalTime = true;
    sentinel.capabilities.rasterLayerOutput = true;
    sentinel.variables.push_back({"visual", "True color", "display DN"});
    earthscience::GeoTemporalQuery sentinelQuery;
    expect(buildScienceWorkbenchQuery(
               sentinel, "satellite-preview", target, 2025, 2025,
               sentinelQuery, error),
           "Sentinel-2 preview must build: " + error);
    expect(sentinelQuery.time.mode ==
               earthscience::ScienceTimeMode::Interval &&
               sentinelQuery.outputKind ==
                   earthscience::ScienceOutputKind::RasterLayer &&
               sentinelQuery.visualizationId == "natural-color-visual",
           "Sentinel-2 must build a bounded natural-color interval query");

    earthscience::ScienceSourceDescriptor dem = source(
        "copernicus-dem-glo-30", 2021, 2021, 30.0);
    dem.capabilities.instantTime = true;
    dem.capabilities.rasterLayerOutput = true;
    dem.variables.push_back(
        {"surface_elevation", "Surface elevation", "m"});
    earthscience::GeoTemporalQuery demQuery;
    expect(buildScienceWorkbenchQuery(
               dem, "terrain-preview", target, 2021, 2021,
               demQuery, error),
           "Copernicus DEM preview must build: " + error);
    expect(demQuery.time.mode == earthscience::ScienceTimeMode::Instant &&
               demQuery.outputKind ==
                   earthscience::ScienceOutputKind::RasterLayer &&
               demQuery.visualizationId ==
                   "surface-elevation-hypsometric",
           "Copernicus DEM must build a static elevation raster query");

    earthscience::GeoTemporalQuery rejected;
    expect(!buildScienceWorkbenchQuery(
               era5, "change-map", target, 2017, 2025, rejected, error) &&
               error == "workbench-method-not-supported-for-source",
           "the UI must not claim an unsupported source/method combination");
}
}

int main()
{
    testEveryRegisteredScienceSourceHasExecutableMethods();
    testEveryExposedMethodBuildsARealQuery();
    std::cout << "Science workbench query plan tests passed" << std::endl;
    return 0;
}
