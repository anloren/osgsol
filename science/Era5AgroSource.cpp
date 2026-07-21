#include "Era5AgroSource.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>

#include "picojson.h"

namespace earthscience
{
namespace
{
    constexpr std::size_t MAX_RESPONSE_BYTES = 8u * 1024u * 1024u;
    constexpr double MINIMUM_COMPLETENESS = 0.95;

    struct VariableDefinition
    {
        const char* id;
        const char* displayName;
        const char* unit;
        const char* meaning;
        const char* aggregation;
        bool sum;
    };

    const std::vector<VariableDefinition>& definitions(
        Era5AgroProduct product)
    {
        static const std::vector<VariableDefinition> LAND = {
            {"temperature_2m_mean", "Mean air temperature at 2 m", "°C",
             "Daily mean near-surface air temperature from ERA5-Land",
             "annual mean of valid daily means", false},
            {"relative_humidity_2m_mean", "Mean relative humidity at 2 m", "%",
             "Daily mean near-surface relative humidity from ERA5-Land",
             "annual mean of valid daily means", false},
            {"soil_moisture_0_to_100cm_mean",
             "Mean soil moisture at 0–100 cm", "m³/m³",
             "Daily mean volumetric soil-water content across the upper metre",
             "annual mean of valid daily means", false},
        };
        static const std::vector<VariableDefinition> CLIMATE = {
            {"temperature_2m_mean", "Mean air temperature at 2 m", "°C",
             "Daily mean near-surface air temperature from ERA5",
             "annual mean of valid daily means", false},
            {"precipitation_sum", "Total precipitation", "mm",
             "Daily liquid-equivalent precipitation sum from ERA5",
             "annual sum of valid daily totals", true},
            {"et0_fao_evapotranspiration", "FAO-56 reference ET₀", "mm",
             "Open-Meteo-derived reference evapotranspiration for a "
             "well-watered grass surface, calculated from ERA5 fields",
             "annual sum of valid daily totals", true},
            {"shortwave_radiation_sum", "Shortwave radiation", "MJ/m²",
             "Daily accumulated incoming shortwave radiation",
             "annual sum of valid daily totals", true},
            {"relative_humidity_2m_mean", "Mean relative humidity at 2 m", "%",
             "Daily mean near-surface relative humidity from ERA5",
             "annual mean of valid daily means", false},
            {"wind_speed_10m_mean", "Mean wind speed at 10 m", "km/h",
             "Daily mean wind speed ten metres above the model surface",
             "annual mean of valid daily means", false},
        };
        return product == Era5AgroProduct::LandSurface ? LAND : CLIMATE;
    }

    double resolution(Era5AgroProduct product)
    {
        return product == Era5AgroProduct::LandSurface ? 11100.0 : 27800.0;
    }

    int firstYear(Era5AgroProduct product)
    {
        return product == Era5AgroProduct::LandSurface ? 1950 : 1940;
    }

    int lastCompleteYear()
    {
        const std::time_t now = std::time(nullptr);
        std::tm utc = {};
        if (!gmtime_r(&now, &utc)) return 2025;
        return utc.tm_year + 1900 - 1;
    }

    bool leapYear(int year)
    {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }

    int daysInYear(int year)
    {
        return leapYear(year) ? 366 : 365;
    }

    const picojson::value* member(
        const picojson::object& object, const char* name)
    {
        const auto found = object.find(name);
        return found == object.end() ? nullptr : &found->second;
    }

    bool exactVariables(Era5AgroProduct product,
                        const std::vector<std::string>& variables)
    {
        return variables == era5AgroVariableIds(product);
    }

    int dateYear(const std::string& date)
    {
        if (date.size() != 10 || date[4] != '-' || date[7] != '-') return -1;
        for (std::size_t index = 0; index < date.size(); ++index)
        {
            if (index == 4 || index == 7) continue;
            if (date[index] < '0' || date[index] > '9') return -1;
        }
        const int year = std::stoi(date.substr(0, 4));
        const int month = std::stoi(date.substr(5, 2));
        const int day = std::stoi(date.substr(8, 2));
        static const int DAYS_PER_MONTH[] = {
            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month < 1 || month > 12) return -1;
        int maximumDay = DAYS_PER_MONTH[month - 1];
        if (month == 2 && leapYear(year)) maximumDay = 29;
        if (day < 1 || day > maximumDay) return -1;
        return year;
    }

    std::string number(double value)
    {
        std::ostringstream output;
        output << std::fixed << std::setprecision(6) << value;
        return output.str();
    }

    bool validateQuery(Era5AgroProduct product,
                       const GeoTemporalQuery& query, std::string& error)
    {
        if (query.sourceId != era5AgroSourceId(product))
            error = "ERA5 agricultural source id changed";
        else if (query.geometry.kind != ScienceGeometryKind::Point ||
                 !std::isfinite(query.geometry.point.latitude) ||
                 !std::isfinite(query.geometry.point.longitude) ||
                 query.geometry.point.latitude < -90.0 ||
                 query.geometry.point.latitude > 90.0 ||
                 query.geometry.point.longitude < -180.0 ||
                 query.geometry.point.longitude > 180.0)
            error = "ERA5 agricultural query requires a valid WGS84 point";
        else if (query.time.mode != ScienceTimeMode::ExplicitYears ||
                 query.time.explicitYears.empty() ||
                 query.time.explicitYears.size() > 9)
            error = "ERA5 agricultural query requires one to nine years";
        else if (!exactVariables(product, query.variables))
            error = "ERA5 agricultural query variables changed";
        else if (query.outputKind != ScienceOutputKind::TimeSeries ||
                 query.analysis.kind != ScienceAnalysisKind::PointSeries)
            error = "ERA5 agricultural query requires a point time series";
        else if (query.aggregation != ScienceAggregation::None)
            error = "ERA5 agricultural annual rules are variable-specific";
        else if (!std::isfinite(query.targetResolutionMeters) ||
                 std::abs(query.targetResolutionMeters - resolution(product)) >
                     1e-6)
            error = "ERA5 agricultural query changed the source grid scale";
        else
        {
            std::set<int> years;
            for (int year : query.time.explicitYears)
            {
                if (year < firstYear(product) || year > lastCompleteYear())
                {
                    error = "ERA5 agricultural year is outside complete coverage";
                    return false;
                }
                if (!years.insert(year).second)
                {
                    error = "ERA5 agricultural years must be unique";
                    return false;
                }
            }
            error.clear();
            return true;
        }
        return false;
    }
}

const char* era5AgroSourceId(Era5AgroProduct product)
{
    return product == Era5AgroProduct::LandSurface
        ? "era5-land-surface-history" : "era5-agricultural-climate";
}

std::vector<std::string> era5AgroVariableIds(Era5AgroProduct product)
{
    std::vector<std::string> result;
    for (const VariableDefinition& variable : definitions(product))
        result.emplace_back(variable.id);
    return result;
}

ScienceSourceDescriptor describeEra5Agro(Era5AgroProduct product)
{
    ScienceSourceDescriptor descriptor;
    descriptor.id = era5AgroSourceId(product);
    descriptor.name = product == Era5AgroProduct::LandSurface
        ? "ERA5-Land Surface & Soil History"
        : "ERA5 Agricultural Climate";
    descriptor.category = "historical agricultural weather";
    descriptor.providerVersion = product == Era5AgroProduct::LandSurface
        ? "ecmwf-era5-land-openmeteo-v1"
        : "ecmwf-era5-openmeteo-v1";
    descriptor.attribution =
        "ECMWF Copernicus Climate Change Service · Open-Meteo (CC BY 4.0)";
    descriptor.firstYear = firstYear(product);
    descriptor.lastYear = lastCompleteYear();
    descriptor.nativeResolutionMeters = resolution(product);
    descriptor.componentCount = static_cast<int>(definitions(product).size());
    for (const VariableDefinition& variable : definitions(product))
    {
        ScienceVariableDescriptor value;
        value.id = variable.id;
        value.displayName = variable.displayName;
        value.unit = variable.unit;
        value.dataKind = "scalar reanalysis field";
        value.componentCount = 1;
        value.bytesPerComponent = 8;
        value.nativeResolutionMeters = descriptor.nativeResolutionMeters;
        value.scientificMeaning = variable.meaning;
        value.aggregationMethod = variable.aggregation;
        value.uncertaintyStatement =
            "Source-grid reanalysis or derived value; not a farm, station, "
            "or parcel observation";
        descriptor.variables.push_back(std::move(value));
    }
    descriptor.capabilities.pointQuery = true;
    descriptor.capabilities.explicitYears = true;
    descriptor.capabilities.timeSeriesOutput = true;
    descriptor.health = ScienceSourceHealth::Ready;
    descriptor.healthMessage = "Ready on demand";
    descriptor.dataNature = "reanalysis";
    descriptor.temporalResolution = "daily to annual";
    descriptor.spatialSupport = product == Era5AgroProduct::LandSurface
        ? "Open-Meteo 0.1 degree output grid (about 11 km; ERA5-Land "
          "native resolution is about 9 km)"
        : "Open-Meteo 0.25 degree ERA5 output grid (about 25–28 km)";
    descriptor.updateLatency = "about five days; UI exposes complete years";
    descriptor.license = "CC BY 4.0 via Open-Meteo; ECMWF attribution required";
    descriptor.documentationUrl =
        "https://open-meteo.com/en/docs/historical-weather-api";
    descriptor.qualityStatement =
        "No elevation downscaling or land-cell relocation is applied; values "
        "represent the nearest source output-grid cell";
    descriptor.experimental = true;
    return descriptor;
}

bool buildEra5AgroRequestUrl(
    Era5AgroProduct product, const GeoTemporalQuery& query,
    std::string& url, std::string& error)
{
    url.clear();
    if (!validateQuery(product, query, error)) return false;
    const auto years = std::minmax_element(
        query.time.explicitYears.begin(), query.time.explicitYears.end());
    std::ostringstream output;
    output << "https://archive-api.open-meteo.com/v1/archive?latitude="
           << number(query.geometry.point.latitude)
           << "&longitude=" << number(query.geometry.point.longitude)
           << "&start_date=" << *years.first << "-01-01"
           << "&end_date=" << *years.second << "-12-31&daily=";
    const std::vector<std::string> variables = era5AgroVariableIds(product);
    for (std::size_t index = 0; index < variables.size(); ++index)
    {
        if (index) output << ',';
        output << variables[index];
    }
    output << "&models="
           << (product == Era5AgroProduct::LandSurface ? "era5_land" : "era5")
           << "&timezone=GMT&elevation=nan&cell_selection=nearest";
    url = output.str();
    error.clear();
    return true;
}

bool parseEra5AgroAnnualArtifact(
    Era5AgroProduct product, const GeoTemporalQuery& query,
    const std::string& requestUrl, const std::string& json,
    ScienceArtifact& artifact, std::string& error)
{
    artifact = ScienceArtifact();
    if (!validateQuery(product, query, error)) return false;
    if (requestUrl.rfind(
            "https://archive-api.open-meteo.com/v1/archive?", 0) != 0)
    {
        error = "ERA5 agricultural response URL is not allowlisted";
        return false;
    }
    if (json.empty() || json.size() > MAX_RESPONSE_BYTES)
    {
        error = "ERA5 agricultural response is empty or exceeds 8 MiB";
        return false;
    }
    picojson::value document;
    const std::string parseError = picojson::parse(document, json);
    if (!parseError.empty() || !document.is<picojson::object>())
    {
        error = "ERA5 agricultural response is invalid JSON";
        return false;
    }
    const picojson::object& root = document.get<picojson::object>();
    const picojson::value* latitudeValue = member(root, "latitude");
    const picojson::value* longitudeValue = member(root, "longitude");
    const picojson::value* timezone = member(root, "timezone");
    const picojson::value* unitsValue = member(root, "daily_units");
    const picojson::value* dailyValue = member(root, "daily");
    if (!latitudeValue || !latitudeValue->is<double>() ||
        !longitudeValue || !longitudeValue->is<double>() ||
        !timezone || !timezone->is<std::string>() ||
        timezone->get<std::string>() != "GMT" || !unitsValue ||
        !unitsValue->is<picojson::object>() || !dailyValue ||
        !dailyValue->is<picojson::object>())
    {
        error = "ERA5 agricultural response metadata is incomplete";
        return false;
    }
    const double latitude = latitudeValue->get<double>();
    const double longitude = longitudeValue->get<double>();
    if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
        latitude < -90.0 || latitude > 90.0 ||
        longitude < -180.0 || longitude > 180.0)
    {
        error = "ERA5 agricultural grid coordinate is invalid";
        return false;
    }
    const picojson::object& units = unitsValue->get<picojson::object>();
    const picojson::object& daily = dailyValue->get<picojson::object>();
    const picojson::value* timeValue = member(daily, "time");
    if (!timeValue || !timeValue->is<picojson::array>())
    {
        error = "ERA5 agricultural daily time array is missing";
        return false;
    }
    const picojson::array& times = timeValue->get<picojson::array>();
    if (times.empty())
    {
        error = "ERA5 agricultural daily time array is empty";
        return false;
    }

    std::map<int, std::vector<std::size_t>> indicesByYear;
    std::set<std::string> uniqueDates;
    for (std::size_t index = 0; index < times.size(); ++index)
    {
        if (!times[index].is<std::string>())
        {
            error = "ERA5 agricultural daily time is not an ISO date";
            return false;
        }
        const std::string& date = times[index].get<std::string>();
        const int year = dateYear(date);
        if (year < 0)
        {
            error = "ERA5 agricultural daily time is malformed";
            return false;
        }
        if (!uniqueDates.insert(date).second)
        {
            error = "ERA5 agricultural daily time contains duplicate dates";
            return false;
        }
        indicesByYear[year].push_back(index);
    }

    std::vector<int> orderedYears = query.time.explicitYears;
    std::sort(orderedYears.begin(), orderedYears.end());
    const auto sharedYears =
        std::make_shared<const std::vector<int>>(orderedYears);
    const double halfCellDegrees =
        product == Era5AgroProduct::LandSurface ? 0.05 : 0.125;

    for (const VariableDefinition& definition : definitions(product))
    {
        const picojson::value* valuesValue = member(daily, definition.id);
        const picojson::value* unitValue = member(units, definition.id);
        if (!valuesValue || !valuesValue->is<picojson::array>() ||
            !unitValue || !unitValue->is<std::string>())
        {
            error = std::string("ERA5 agricultural variable is missing: ") +
                definition.id;
            return false;
        }
        const picojson::array& dailyValues =
            valuesValue->get<picojson::array>();
        if (dailyValues.size() != times.size())
        {
            error = std::string("ERA5 agricultural array length mismatch: ") +
                definition.id;
            return false;
        }
        if (unitValue->get<std::string>() != definition.unit)
        {
            error = std::string("ERA5 agricultural unit changed: ") +
                definition.id;
            return false;
        }

        std::vector<double> annualValues;
        std::vector<unsigned char> validity;
        annualValues.reserve(orderedYears.size());
        validity.reserve(orderedYears.size());
        for (int year : orderedYears)
        {
            const auto found = indicesByYear.find(year);
            if (found == indicesByYear.end() ||
                found->second.size() != static_cast<std::size_t>(daysInYear(year)))
            {
                error = "ERA5 agricultural response does not cover complete year " +
                    std::to_string(year);
                return false;
            }
            double aggregate = 0.0;
            std::size_t validCount = 0;
            for (std::size_t index : found->second)
            {
                const picojson::value& value = dailyValues[index];
                if (!value.is<double>()) continue;
                const double numberValue = value.get<double>();
                if (!std::isfinite(numberValue)) continue;
                aggregate += numberValue;
                ++validCount;
            }
            const double completeness = static_cast<double>(validCount) /
                static_cast<double>(found->second.size());
            const bool complete = completeness >= MINIMUM_COMPLETENESS;
            annualValues.push_back(complete
                ? (definition.sum ? aggregate
                                  : aggregate / static_cast<double>(validCount))
                : std::numeric_limits<double>::quiet_NaN());
            validity.push_back(complete ? 1 : 0);
            if (!complete)
                artifact.warnings.push_back(
                    std::string(definition.displayName) + " " +
                    std::to_string(year) +
                    " has less than 95% valid daily coverage");
        }

        ScienceVariableSeries series;
        series.variableId = definition.id;
        series.displayName = definition.displayName;
        series.unit = definition.unit;
        series.aggregationMethod = definition.aggregation;
        series.nativeResolutionMeters = resolution(product);
        series.years = sharedYears;
        series.values = std::make_shared<const std::vector<double>>(
            std::move(annualValues));
        series.validity =
            std::make_shared<const std::vector<unsigned char>>(
                std::move(validity));
        artifact.variableSeries.push_back(std::move(series));
    }

    const ScienceSourceDescriptor source = describeEra5Agro(product);
    ScienceSourceReference reference;
    reference.sourceId = source.id;
    reference.providerVersion = source.providerVersion;
    reference.datasetId = product == Era5AgroProduct::LandSurface
        ? "ECMWF ERA5-Land hourly data aggregated to daily by Open-Meteo"
        : "ECMWF ERA5 hourly data aggregated to daily by Open-Meteo";
    reference.originalUrl = requestUrl;
    reference.requestedCoverage = query.geometry;
    reference.actualCoverage = {
        longitude - halfCellDegrees, latitude - halfCellDegrees,
        longitude + halfCellDegrees, latitude + halfCellDegrees};
    reference.variables = query.variables;
    for (const VariableDefinition& definition : definitions(product))
        reference.units.emplace_back(definition.unit);
    reference.processingSteps = {
        "selected nearest source output-grid cell",
        "disabled elevation downscaling and land-cell relocation",
        "aggregated daily values into variable-specific annual mean or sum",
        "required at least 95 percent valid daily coverage per year",
    };
    reference.acquisitionTime = std::to_string(orderedYears.front()) +
        "-01-01/" + std::to_string(orderedYears.back()) + "-12-31";
    reference.attribution = source.attribution;
    reference.fields.push_back({
        "returned_grid_center", "Returned source-grid center",
        number(latitude) + ", " + number(longitude), "WGS84"});
    reference.fields.push_back({
        "spatial_support", "Native spatial support", source.spatialSupport, ""});

    artifact.query = query;
    artifact.sourceReferences.push_back(std::move(reference));
    artifact.analysis.kind = ScienceAnalysisKind::PointSeries;
    artifact.analysis.interpretation =
        std::make_shared<const std::vector<std::string>>(
            std::vector<std::string>{
                "Annual values summarize the selected reanalysis source-grid cell"});
    artifact.analysis.limitations =
        std::make_shared<const std::vector<std::string>>(
            std::vector<std::string>{
                "Reanalysis is model-assisted and is not a field observation",
                "Grid-cell values must not be interpreted as parcel-scale truth"});
    artifact.processingVersion = "era5-agro-annual-v1";
    artifact.createdAt = "derived from complete UTC calendar years";
    error.clear();
    return true;
}
}
