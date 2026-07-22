#include "science_ui/science_chart_model.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace
{
void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

void testEra5MetricsAndStatistics()
{
    const std::vector<int> years = {2017, 2018, 2019, 2020, 2021};
    ScienceChartModel temperature = makeScienceChartModel(
        "temperature_2m_mean", "2 米平均气温", "°C", years,
        {17.5, 17.8, 18.1, 18.4, 18.7});
    expect(temperature.unit == "°C" && temperature.summary.validCount == 5,
           "temperature chart must retain unit and valid count");
    expect(std::abs(temperature.summary.mean - 18.1) < 1.0e-9 &&
           std::abs(temperature.summary.firstToLastChange - 1.2) < 1.0e-9,
           "temperature chart must calculate mean and change");
    expect(std::abs(temperature.summary.linearTrendPerYear - 0.3) < 1.0e-9,
           "temperature chart must calculate per-year linear trend");

    ScienceChartModel precipitation = makeScienceChartModel(
        "total_precipitation", "总降水量", "mm", years,
        {1470.0, 1310.0, 1425.0, 1090.0, 1980.0});
    expect(formatScienceChartValue(precipitation.summary.maximum,
                                   precipitation.unit) == "1980 mm",
           "precipitation values must avoid scientific notation");

    ScienceChartModel et0 = makeScienceChartModel(
        "reference_et0", "FAO-56 reference ET0", "mm", years,
        {1100.0, 1160.0, 1210.0, 1180.0, 1240.0});
    expect(et0.title.find("ET₀") != std::string::npos,
           "reference evapotranspiration title must render ET₀");

    ScienceChartModel radiation = makeScienceChartModel(
        "shortwave_radiation", "短波辐射", "MJ/m²", years,
        {5160.0, 5450.0, 5720.0, 5380.0, 5610.0});
    ScienceChartModel humidity = makeScienceChartModel(
        "relative_humidity_2m", "2 米平均相对湿度", "%", years,
        {71.0, 73.0, 70.0, 75.0, 74.0});
    expect(radiation.domainMinimum < radiation.summary.minimum &&
           humidity.domainMaximum > humidity.summary.maximum,
           "every metric must receive a padded finite domain");
}

void testMissingAndConstantSeries()
{
    const std::vector<int> years = {2020, 2021, 2022, 2023};
    ScienceChartModel missing = makeScienceChartModel(
        "temperature", "温度", "°C", years,
        {20.0, std::numeric_limits<double>::quiet_NaN(), 22.0, 23.0},
        {1, 1, 0, 1});
    expect(!missing.points[0].missing && missing.points[1].missing &&
           missing.points[2].missing && !missing.points[3].missing,
           "invalid and non-finite values must create visible chart gaps");
    expect(missing.summary.validCount == 2,
           "missing points must not enter statistics");

    ScienceChartModel constant = makeScienceChartModel(
        "humidity", "湿度", "%", years, {70.0, 70.0, 70.0, 70.0});
    expect(constant.domainMinimum < 70.0 && constant.domainMaximum > 70.0 &&
           std::isfinite(constant.domainMinimum) &&
           std::isfinite(constant.domainMaximum),
           "constant series must still receive a finite visible domain");
}
}

int main()
{
    testEra5MetricsAndStatistics();
    testMissingAndConstantSeries();
    std::cout << "ScienceChartModel tests passed" << std::endl;
    return 0;
}
