#include "Era5AgroSource.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    earthscience::GeoTemporalQuery query(
        earthscience::Era5AgroProduct product,
        const std::vector<int>& years)
    {
        earthscience::GeoTemporalQuery value;
        value.sourceId = earthscience::era5AgroSourceId(product);
        value.geometry.kind = earthscience::ScienceGeometryKind::Point;
        value.geometry.point = {22.5431, 114.0579};
        value.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        value.time.explicitYears = years;
        value.variables = earthscience::era5AgroVariableIds(product);
        value.targetResolutionMeters = product ==
                earthscience::Era5AgroProduct::LandSurface
            ? 11100.0 : 27800.0;
        value.outputKind = earthscience::ScienceOutputKind::TimeSeries;
        value.analysis.kind = earthscience::ScienceAnalysisKind::PointSeries;
        value.purpose = "annual agricultural climate profile";
        return value;
    }

    std::string completeEra5Fixture(int year)
    {
        const bool leap = year % 4 == 0 &&
            (year % 100 != 0 || year % 400 == 0);
        const int dayCount = leap ? 366 : 365;
        std::ostringstream json;
        json << "{\"latitude\":22.5,\"longitude\":114.0,"
             << "\"generationtime_ms\":1.25,\"utc_offset_seconds\":0,"
             << "\"timezone\":\"GMT\",\"timezone_abbreviation\":\"GMT\","
             << "\"elevation\":55.0,\"daily_units\":{"
             << "\"time\":\"iso8601\","
             << "\"temperature_2m_mean\":\"°C\","
             << "\"precipitation_sum\":\"mm\","
             << "\"et0_fao_evapotranspiration\":\"mm\","
             << "\"shortwave_radiation_sum\":\"MJ/m²\","
             << "\"relative_humidity_2m_mean\":\"%\","
             << "\"wind_speed_10m_mean\":\"km/h\"},\"daily\":{"
             << "\"time\":[";
        static const int DAYS_PER_MONTH[] = {
            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        int emitted = 0;
        for (int month = 1; month <= 12; ++month)
        {
            int monthDays = DAYS_PER_MONTH[month - 1];
            if (month == 2 && leap) monthDays = 29;
            for (int day = 1; day <= monthDays; ++day)
            {
                if (emitted++) json << ',';
                json << '\"' << year << '-';
                if (month < 10) json << '0';
                json << month << '-';
                if (day < 10) json << '0';
                json << day << '\"';
            }
        }
        require(emitted == dayCount, "ERA5 fixture calendar is incomplete");
        json << ']';
        const auto values = [&json, dayCount](
            const char* id, double value)
        {
            json << ",\"" << id << "\":[";
            for (int day = 0; day < dayCount; ++day)
            {
                if (day) json << ',';
                json << value;
            }
            json << ']';
        };
        values("temperature_2m_mean", 20.0);
        values("precipitation_sum", 2.0);
        values("et0_fao_evapotranspiration", 0.5);
        values("shortwave_radiation_sum", 10.0);
        values("relative_humidity_2m_mean", 70.0);
        values("wind_speed_10m_mean", 5.0);
        json << "}}";
        return json.str();
    }

    const earthscience::ScienceVariableSeries* findSeries(
        const earthscience::ScienceArtifact& artifact,
        const std::string& id)
    {
        for (const auto& series : artifact.variableSeries)
            if (series.variableId == id) return &series;
        return nullptr;
    }

    void testDescriptorsExposeScientificMeaningAndScale()
    {
        const auto land = earthscience::describeEra5Agro(
            earthscience::Era5AgroProduct::LandSurface);
        require(land.id == "era5-land-surface-history" &&
                    land.firstYear == 1950 &&
                    land.nativeResolutionMeters == 11100.0 &&
                    land.dataNature == "reanalysis" &&
                    land.temporalResolution == "daily to annual" &&
                    land.spatialSupport.find("grid") != std::string::npos &&
                    land.license.find("CC BY 4.0") != std::string::npos &&
                    !land.documentationUrl.empty() &&
                    land.capabilities.pointQuery &&
                    land.capabilities.explicitYears &&
                    land.capabilities.timeSeriesOutput &&
                    !land.capabilities.rasterLayerOutput,
                "ERA5-Land descriptor hides its scientific scale or nature");
        require(land.variables.size() == 3 &&
                    land.variables.front().nativeResolutionMeters == 11100.0 &&
                    !land.variables.front().scientificMeaning.empty() &&
                    !land.variables.front().aggregationMethod.empty(),
                "ERA5-Land variables lack per-variable semantics");

        const auto climate = earthscience::describeEra5Agro(
            earthscience::Era5AgroProduct::AgriculturalClimate);
        require(climate.id == "era5-agricultural-climate" &&
                    climate.firstYear == 1940 &&
                    climate.nativeResolutionMeters == 27800.0 &&
                    climate.variables.size() == 6,
                "ERA5 agricultural climate descriptor is not truthful");
    }

    void testRequestUsesExactGridWithoutHiddenDownscaling()
    {
        std::string url, error;
        require(earthscience::buildEra5AgroRequestUrl(
                    earthscience::Era5AgroProduct::LandSurface,
                    query(earthscience::Era5AgroProduct::LandSurface,
                          {2020, 2021}), url, error),
                "valid ERA5-Land request was rejected");
        require(error.empty() &&
                    url.rfind(
                        "https://archive-api.open-meteo.com/v1/archive?", 0) == 0 &&
                    url.find("models=era5_land") != std::string::npos &&
                    url.find("start_date=2020-01-01") != std::string::npos &&
                    url.find("end_date=2021-12-31") != std::string::npos &&
                    url.find("timezone=GMT") != std::string::npos &&
                    url.find("elevation=nan") != std::string::npos &&
                    url.find("cell_selection=nearest") != std::string::npos,
                "ERA5-Land URL applies hidden elevation or land-cell downscaling");
    }

    void testParsesCompleteLeapYearWithVariableSpecificAggregation()
    {
        const auto request = query(
            earthscience::Era5AgroProduct::AgriculturalClimate, {2020});
        std::string url, error;
        require(earthscience::buildEra5AgroRequestUrl(
                    earthscience::Era5AgroProduct::AgriculturalClimate,
                    request, url, error),
                "ERA5 request URL was rejected");
        earthscience::ScienceArtifact artifact;
        require(earthscience::parseEra5AgroAnnualArtifact(
                    earthscience::Era5AgroProduct::AgriculturalClimate,
                    request, url, completeEra5Fixture(2020), artifact, error),
                "complete ERA5 annual fixture was rejected");
        const auto* temperature = findSeries(
            artifact, "temperature_2m_mean");
        const auto* precipitation = findSeries(
            artifact, "precipitation_sum");
        const auto* evapotranspiration = findSeries(
            artifact, "et0_fao_evapotranspiration");
        require(temperature && precipitation && evapotranspiration &&
                    temperature->years && temperature->values &&
                    temperature->validity &&
                    temperature->years->at(0) == 2020 &&
                    temperature->validity->at(0) == 1 &&
                    std::abs(temperature->values->at(0) - 20.0) < 1e-9 &&
                    std::abs(precipitation->values->at(0) - 732.0) < 1e-9 &&
                    std::abs(evapotranspiration->values->at(0) - 183.0) < 1e-9,
                "ERA5 annual mean/sum aggregation changed");
        require(artifact.sourceReferences.size() == 1 &&
                    artifact.sourceReferences.front().sourceId ==
                        "era5-agricultural-climate" &&
                    artifact.sourceReferences.front().actualCoverage.west ==
                        113.875 &&
                    artifact.sourceReferences.front().actualCoverage.south ==
                        22.375 &&
                    artifact.processingVersion == "era5-agro-annual-v1" &&
                    artifact.analysis.kind ==
                        earthscience::ScienceAnalysisKind::PointSeries,
                "ERA5 provenance or annual processing identity is missing");
    }

    void testRejectsIncompleteAnnualCoverage()
    {
        const auto request = query(
            earthscience::Era5AgroProduct::AgriculturalClimate, {2020});
        std::string url, error;
        earthscience::buildEra5AgroRequestUrl(
            earthscience::Era5AgroProduct::AgriculturalClimate,
            request, url, error);
        std::string incomplete = completeEra5Fixture(2020);
        const std::size_t time = incomplete.find("\"time\":[");
        const std::size_t next = incomplete.find("]", time);
        incomplete.replace(time, next - time + 1,
                           "\"time\":[\"2020-01-01\"]");
        earthscience::ScienceArtifact artifact;
        require(!earthscience::parseEra5AgroAnnualArtifact(
                    earthscience::Era5AgroProduct::AgriculturalClimate,
                    request, url, incomplete, artifact, error) &&
                    error.find("length") != std::string::npos,
                "truncated ERA5 response was accepted as a complete year");
    }

    void testRejectsDuplicateCalendarDates()
    {
        const auto request = query(
            earthscience::Era5AgroProduct::AgriculturalClimate, {2021});
        std::string url, error;
        earthscience::buildEra5AgroRequestUrl(
            earthscience::Era5AgroProduct::AgriculturalClimate,
            request, url, error);
        std::string duplicate = completeEra5Fixture(2021);
        const std::size_t secondDate = duplicate.find("\"2021-01-02\"");
        require(secondDate != std::string::npos,
                "ERA5 duplicate-date fixture is malformed");
        duplicate.replace(secondDate, 12, "\"2021-01-01\"");
        earthscience::ScienceArtifact artifact;
        require(!earthscience::parseEra5AgroAnnualArtifact(
                    earthscience::Era5AgroProduct::AgriculturalClimate,
                    request, url, duplicate, artifact, error) &&
                    error.find("duplicate") != std::string::npos,
                "duplicate ERA5 calendar dates were accepted");
    }
}

int main()
{
    testDescriptorsExposeScientificMeaningAndScale();
    testRequestUsesExactGridWithoutHiddenDownscaling();
    testParsesCompleteLeapYearWithVariableSpecificAggregation();
    testRejectsIncompleteAnnualCoverage();
    testRejectsDuplicateCalendarDates();
    std::cout << "[OK] ERA5 agricultural source contract\n";
    return 0;
}
