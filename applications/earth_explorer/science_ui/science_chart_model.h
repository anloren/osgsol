#ifndef EARTH_SCIENCE_UI_CHART_MODEL_H
#define EARTH_SCIENCE_UI_CHART_MODEL_H

#include <cstddef>
#include <string>
#include <vector>

struct ScienceChartPoint
{
    int year = 0;
    double value = 0.0;
    bool missing = true;
};

struct ScienceChartSummary
{
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double firstToLastChange = 0.0;
    double linearTrendPerYear = 0.0;
    std::size_t validCount = 0;
};

struct ScienceChartModel
{
    std::string metricId;
    std::string title;
    std::string unit;
    std::vector<ScienceChartPoint> points;
    ScienceChartSummary summary;
    double domainMinimum = 0.0;
    double domainMaximum = 1.0;
};

ScienceChartModel makeScienceChartModel(
    const std::string& metricId,
    const std::string& title,
    const std::string& unit,
    const std::vector<int>& years,
    const std::vector<double>& values,
    const std::vector<unsigned char>& validity = {});

std::string formatScienceChartValue(double value,
                                    const std::string& unit);
std::string normalizeScienceMetricTitle(const std::string& metricId,
                                        const std::string& title);

#endif
