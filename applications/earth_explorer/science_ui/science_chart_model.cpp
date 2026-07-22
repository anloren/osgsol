#include "science_chart_model.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

std::string normalizeScienceMetricTitle(
    const std::string& metricId, const std::string& title)
{
    if (metricId.find("et0") != std::string::npos ||
        metricId.find("reference_evapotranspiration") != std::string::npos)
    {
        if (title.empty()) return "FAO-56 参考蒸散 ET₀";
        std::string output = title;
        const std::size_t plain = output.find("ET0");
        if (plain != std::string::npos) output.replace(plain, 3, "ET₀");
        return output;
    }
    return title.empty() ? metricId : title;
}

ScienceChartModel makeScienceChartModel(
    const std::string& metricId, const std::string& title,
    const std::string& unit, const std::vector<int>& years,
    const std::vector<double>& values,
    const std::vector<unsigned char>& validity)
{
    ScienceChartModel output;
    output.metricId = metricId;
    output.title = normalizeScienceMetricTitle(metricId, title);
    output.unit = unit;
    const std::size_t count = std::min(years.size(), values.size());
    output.points.reserve(count);
    double sum = 0.0;
    double firstValue = 0.0, lastValue = 0.0;
    double sumX = 0.0, sumY = 0.0, sumXX = 0.0, sumXY = 0.0;
    bool haveFirst = false;
    output.summary.minimum = std::numeric_limits<double>::infinity();
    output.summary.maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < count; ++index)
    {
        const bool valid = std::isfinite(values[index]) &&
            (validity.empty() || index >= validity.size() || validity[index]);
        output.points.push_back({years[index], valid ? values[index] : 0.0,
                                 !valid});
        if (!valid) continue;
        const double x = static_cast<double>(years[index]);
        const double y = values[index];
        if (!haveFirst) { firstValue = y; haveFirst = true; }
        lastValue = y;
        output.summary.minimum = std::min(output.summary.minimum, y);
        output.summary.maximum = std::max(output.summary.maximum, y);
        sum += y;
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
        ++output.summary.validCount;
    }
    if (output.summary.validCount == 0)
    {
        output.summary.minimum = 0.0;
        output.summary.maximum = 0.0;
        output.domainMinimum = -1.0;
        output.domainMaximum = 1.0;
        return output;
    }
    output.summary.mean = sum / output.summary.validCount;
    output.summary.firstToLastChange = lastValue - firstValue;
    const double n = static_cast<double>(output.summary.validCount);
    const double denominator = n * sumXX - sumX * sumX;
    if (std::abs(denominator) > 1.0e-12)
        output.summary.linearTrendPerYear =
            (n * sumXY - sumX * sumY) / denominator;
    const double range = output.summary.maximum - output.summary.minimum;
    const double padding = range > 0.0 ? range * 0.08 :
        std::max(1.0, std::abs(output.summary.maximum) * 0.08);
    output.domainMinimum = output.summary.minimum - padding;
    output.domainMaximum = output.summary.maximum + padding;
    if (!std::isfinite(output.domainMinimum) ||
        !std::isfinite(output.domainMaximum) ||
        output.domainMinimum >= output.domainMaximum)
    {
        output.domainMinimum = -1.0;
        output.domainMaximum = 1.0;
    }
    return output;
}

std::string formatScienceChartValue(
    double value, const std::string& unit)
{
    if (!std::isfinite(value)) return "不可用";
    int precision = 1;
    if (unit == "mm" || unit == "MJ/m²" || std::abs(value) >= 1000.0)
        precision = 0;
    else if (std::abs(value) < 1.0)
        precision = 2;
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    std::string number = stream.str();
    if (precision > 0)
    {
        while (!number.empty() && number.back() == '0') number.pop_back();
        if (!number.empty() && number.back() == '.') number.pop_back();
    }
    return unit.empty() ? number : number + " " + unit;
}
