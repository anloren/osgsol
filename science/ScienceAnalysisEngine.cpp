#include "ScienceAnalysisEngine.h"

#include "ScienceAnalysisSupport.h"
#include "ScienceEmbedding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <utility>

namespace earthscience
{
namespace
{
    using analysisdetail::checkedCellCount;
    using analysisdetail::compareVectors;
    using analysisdetail::equalBounds;
    using analysisdetail::equalGroundGrid;
    using analysisdetail::linearQuantile;
    using analysisdetail::MAX_ANALYSIS_YEARS;
    using analysisdetail::metricUnit;
    using analysisdetail::metricValue;
    using analysisdetail::regionalMetric;
    using analysisdetail::validateEmbeddingShape;
    using analysisdetail::validateMetrics;
    using analysisdetail::validGroundGrid;
    using analysisdetail::vectorAt;

    bool isCancelled(const std::function<bool()>& cancelled)
    {
        return cancelled && cancelled();
    }

    bool fail(std::string& error, const std::string& message)
    {
        error = message;
        return false;
    }

    bool findYear(const std::vector<int>& years, int year,
                  std::size_t& index)
    {
        const auto found = std::lower_bound(years.begin(), years.end(), year);
        if (found == years.end() || *found != year) return false;
        index = static_cast<std::size_t>(found - years.begin());
        return true;
    }

    std::string yearContext(const char* prefix, int year)
    {
        std::ostringstream message;
        message << prefix << ' ' << year;
        return message.str();
    }
}

std::shared_ptr<const ScienceAnalysisPayload>
ScienceAnalysisEngine::analyzePointSeries(
    const ScienceEmbeddingPayload& embedding,
    const ScienceAnalysisOptions& options,
    const std::function<bool()>& cancelled, std::string& error)
{
    error.clear();
    if (isCancelled(cancelled))
    {
        error = "point-series analysis cancelled";
        return nullptr;
    }
    if (options.kind != ScienceAnalysisKind::None &&
        options.kind != ScienceAnalysisKind::PointSeries)
    {
        error = "point-series analysis received an incompatible analysis kind";
        return nullptr;
    }
    std::size_t cellCount = 0;
    if (!validateEmbeddingShape(
            embedding, false, cellCount, error, "point-series analysis"))
        return nullptr;
    if (embedding.width != 1 || embedding.height != 1)
    {
        error = "point-series analysis requires one spatial sample per year";
        return nullptr;
    }
    if (!validateMetrics(options.metrics, error)) return nullptr;

    const std::vector<int>& years = *embedding.years;
    const int baselineYear = options.baselineYear == 0
        ? years.front() : options.baselineYear;
    std::size_t baselineIndex = 0;
    if (!findYear(years, baselineYear, baselineIndex))
    {
        error = "point-series baseline year is absent from the payload";
        return nullptr;
    }
    if (!embedding.mask->at(baselineIndex))
    {
        error = "point-series baseline year is missing";
        return nullptr;
    }

    std::vector<ScienceAnnualSeries> annualSeries;
    annualSeries.reserve(options.metrics.size());
    for (ScienceMetric metric : options.metrics)
    {
        std::vector<double> values(
            years.size(), std::numeric_limits<double>::quiet_NaN());
        std::vector<unsigned char> validity(years.size(), 0);
        for (std::size_t i = 0; i < years.size(); ++i)
        {
            if (isCancelled(cancelled))
            {
                error = "point-series analysis cancelled";
                return nullptr;
            }
            if (!embedding.mask->at(i)) continue;
            ScienceVectorMetrics vectorMetrics;
            if (!compareVectors(
                    vectorAt(embedding, baselineIndex), vectorAt(embedding, i),
                    vectorMetrics, error,
                    yearContext("point-series comparison year", years[i])))
                return nullptr;
            const double value = metricValue(vectorMetrics, metric);
            if (!std::isfinite(value))
            {
                error = "point-series comparison produced a non-finite result";
                return nullptr;
            }
            values[i] = value;
            validity[i] = 1;
        }
        ScienceAnnualSeries series;
        series.metric = metric;
        series.years = embedding.years;
        series.values = std::make_shared<const std::vector<double>>(
            std::move(values));
        series.validity =
            std::make_shared<const std::vector<unsigned char>>(
                std::move(validity));
        series.unit = metricUnit(metric);
        annualSeries.push_back(std::move(series));
    }

    std::vector<ScienceMetricResult> consecutiveMetrics;
    std::size_t previousValid = years.size();
    double largestAngularDistance = -1.0;
    int largestBaselineYear = 0;
    int largestComparisonYear = 0;
    for (std::size_t i = 0; i < years.size(); ++i)
    {
        if (!embedding.mask->at(i)) continue;
        if (previousValid != years.size())
        {
            if (isCancelled(cancelled))
            {
                error = "point-series analysis cancelled";
                return nullptr;
            }
            ScienceVectorMetrics vectorMetrics;
            if (!compareVectors(
                    vectorAt(embedding, previousValid), vectorAt(embedding, i),
                    vectorMetrics, error,
                    yearContext("point-series consecutive year", years[i])))
                return nullptr;
            if (vectorMetrics.angularDistanceRadians > largestAngularDistance)
            {
                largestAngularDistance = vectorMetrics.angularDistanceRadians;
                largestBaselineYear = years[previousValid];
                largestComparisonYear = years[i];
            }
            for (ScienceMetric metric : options.metrics)
            {
                const double value = metricValue(vectorMetrics, metric);
                if (!std::isfinite(value))
                {
                    error = "point-series consecutive comparison produced a "
                            "non-finite result";
                    return nullptr;
                }
                ScienceMetricResult result;
                result.metric = metric;
                result.baselineYear = years[previousValid];
                result.comparisonYear = years[i];
                result.value = value;
                result.unit = metricUnit(metric);
                consecutiveMetrics.push_back(std::move(result));
            }
        }
        previousValid = i;
    }

    std::vector<std::string> interpretation = {
        "Selected-baseline and consecutive-valid metrics are embedding space "
        "comparisons only."};
    if (largestAngularDistance >= 0.0)
    {
        std::ostringstream largest;
        largest << "Largest consecutive valid angular-distance interval in "
                   "embedding space: "
                << largestBaselineYear << '-' << largestComparisonYear
                << " (radians=" << largestAngularDistance << ").";
        interpretation.push_back(largest.str());
    }
    std::vector<std::string> limitations = {
        "No interpolation was applied; missing years remain invalid in the "
        "annual validity mask.",
        "Embedding-space metrics do not identify physical or causal change."};

    if (isCancelled(cancelled))
    {
        error = "point-series analysis cancelled";
        return nullptr;
    }
    ScienceAnalysisPayload output;
    output.kind = ScienceAnalysisKind::PointSeries;
    output.metrics =
        std::make_shared<const std::vector<ScienceMetricResult>>(
            std::move(consecutiveMetrics));
    output.annualSeries =
        std::make_shared<const std::vector<ScienceAnnualSeries>>(
            std::move(annualSeries));
    output.interpretation =
        std::make_shared<const std::vector<std::string>>(
            std::move(interpretation));
    output.limitations =
        std::make_shared<const std::vector<std::string>>(
            std::move(limitations));
    return std::make_shared<const ScienceAnalysisPayload>(std::move(output));
}

std::shared_ptr<const ScienceAnalysisPayload>
ScienceAnalysisEngine::analyzeRegionalChange(
    const ScienceEmbeddingPayload& embedding,
    const ScienceAnalysisOptions& options,
    const std::function<bool()>& cancelled, std::string& error)
{
    error.clear();
    if (isCancelled(cancelled))
    {
        error = "regional change analysis cancelled";
        return nullptr;
    }
    if (options.kind != ScienceAnalysisKind::None &&
        options.kind != ScienceAnalysisKind::RegionalChange)
    {
        error = "regional change analysis received an incompatible analysis kind";
        return nullptr;
    }
    if (!std::isfinite(options.hotspotQuantile) ||
        options.hotspotQuantile < 0.0 || options.hotspotQuantile > 1.0)
    {
        error = "regional hotspot quantile must be finite and within [0, 1]";
        return nullptr;
    }
    if (!validateMetrics(options.metrics, error)) return nullptr;
    std::size_t cellCount = 0;
    if (!validateEmbeddingShape(
            embedding, true, cellCount, error, "regional change analysis"))
        return nullptr;
    if (embedding.years->size() < 2)
    {
        error = "regional change analysis requires at least two years";
        return nullptr;
    }

    const std::vector<int>& years = *embedding.years;
    const int baselineYear = options.baselineYear == 0
        ? years.front() : options.baselineYear;
    const int comparisonYear = options.comparisonYear == 0
        ? years[1] : options.comparisonYear;
    if (baselineYear == comparisonYear)
    {
        error = "regional change analysis requires two distinct years";
        return nullptr;
    }
    std::size_t baselineIndex = 0;
    std::size_t comparisonIndex = 0;
    if (!findYear(years, baselineYear, baselineIndex) ||
        !findYear(years, comparisonYear, comparisonIndex))
    {
        error = "regional comparison year is absent from the payload";
        return nullptr;
    }

    const ScienceMetric selectedMetric = regionalMetric(options.metrics);
    std::vector<float> rasterValues(
        cellCount, std::numeric_limits<float>::quiet_NaN());
    std::vector<unsigned char> rasterMask(cellCount, 0);
    std::vector<double> observed;
    observed.reserve(cellCount);
    const std::size_t baselineOffset = baselineIndex * cellCount;
    const std::size_t comparisonOffset = comparisonIndex * cellCount;
    for (std::size_t cell = 0; cell < cellCount; ++cell)
    {
        if (isCancelled(cancelled))
        {
            error = "regional change analysis cancelled";
            return nullptr;
        }
        if (!embedding.mask->at(baselineOffset + cell) ||
            !embedding.mask->at(comparisonOffset + cell))
            continue;
        ScienceVectorMetrics vectorMetrics;
        std::ostringstream context;
        context << "regional change cell " << cell;
        if (!compareVectors(
                vectorAt(embedding, baselineOffset + cell),
                vectorAt(embedding, comparisonOffset + cell),
                vectorMetrics, error, context.str()))
            return nullptr;
        const double score = metricValue(vectorMetrics, selectedMetric);
        if (!std::isfinite(score) ||
            std::abs(score) > std::numeric_limits<float>::max())
        {
            error = "regional change produced a non-finite result";
            return nullptr;
        }
        rasterValues[cell] = static_cast<float>(score);
        rasterMask[cell] = 1;
        observed.push_back(score);
    }
    if (observed.empty())
    {
        error = "regional change has zero both-valid overlap";
        return nullptr;
    }

    double sum = 0.0;
    for (double value : observed) sum += value;
    if (!std::isfinite(sum))
    {
        error = "regional change statistics overflowed";
        return nullptr;
    }
    const double mean = sum / static_cast<double>(observed.size());
    double squaredDeviationSum = 0.0;
    for (double value : observed)
    {
        const double deviation = value - mean;
        squaredDeviationSum += deviation * deviation;
    }
    const double standardDeviation = std::sqrt(
        squaredDeviationSum / static_cast<double>(observed.size()));
    if (!std::isfinite(mean) || !std::isfinite(standardDeviation))
    {
        error = "regional change produced non-finite statistics";
        return nullptr;
    }

    std::vector<double> sorted = observed;
    std::sort(sorted.begin(), sorted.end());
    std::vector<double> probabilities = {0.10, 0.25, 0.50, 0.75, 0.90};
    probabilities.push_back(options.hotspotQuantile);
    std::sort(probabilities.begin(), probabilities.end());
    probabilities.erase(std::unique(probabilities.begin(), probabilities.end()),
                        probabilities.end());
    std::vector<ScienceQuantileResult> quantiles;
    quantiles.reserve(probabilities.size());
    for (double probability : probabilities)
        quantiles.push_back({probability,
                             linearQuantile(sorted, probability)});
    const double median = linearQuantile(sorted, 0.50);
    const double hotspotThreshold =
        linearQuantile(sorted, options.hotspotQuantile);
    if (!std::isfinite(median) || !std::isfinite(hotspotThreshold))
    {
        error = "regional change produced a non-finite quantile";
        return nullptr;
    }

    std::vector<unsigned char> hotspotMask(cellCount, 0);
    std::vector<std::uint64_t> hotspotIndices;
    std::size_t observedIndex = 0;
    for (std::size_t cell = 0; cell < cellCount; ++cell)
    {
        if (!rasterMask[cell]) continue;
        if (observed[observedIndex] >= hotspotThreshold)
        {
            hotspotMask[cell] = 1;
            hotspotIndices.push_back(static_cast<std::uint64_t>(cell));
        }
        ++observedIndex;
    }

    ScienceScalarChangeRaster changeRaster;
    changeRaster.metric = selectedMetric;
    changeRaster.width = embedding.width;
    changeRaster.height = embedding.height;
    changeRaster.bounds = embedding.bounds;
    changeRaster.groundGrid = embedding.groundGrid;
    changeRaster.actualResolutionMeters = embedding.actualResolutionMeters;
    changeRaster.values = std::make_shared<const std::vector<float>>(
        std::move(rasterValues));
    changeRaster.mask =
        std::make_shared<const std::vector<unsigned char>>(
            std::move(rasterMask));
    changeRaster.validCellCount = observed.size();
    changeRaster.noDataCellCount = cellCount - observed.size();
    changeRaster.coverageFraction = static_cast<double>(observed.size()) /
        static_cast<double>(cellCount);

    ScienceRegionalChangeSummary summary;
    summary.metric = selectedMetric;
    summary.baselineYear = baselineYear;
    summary.comparisonYear = comparisonYear;
    summary.totalCellCount = cellCount;
    summary.validOverlapCount = observed.size();
    summary.noDataCellCount = cellCount - observed.size();
    summary.coverageFraction = changeRaster.coverageFraction;
    summary.mean = mean;
    summary.median = median;
    summary.standardDeviation = standardDeviation;
    summary.minimum = sorted.front();
    summary.maximum = sorted.back();
    summary.quantiles =
        std::make_shared<const std::vector<ScienceQuantileResult>>(
            std::move(quantiles));
    summary.hotspotQuantile = options.hotspotQuantile;
    summary.hotspotThreshold = hotspotThreshold;
    summary.hotspotMask =
        std::make_shared<const std::vector<unsigned char>>(
            std::move(hotspotMask));
    summary.hotspotIndices =
        std::make_shared<const std::vector<std::uint64_t>>(
            std::move(hotspotIndices));
    summary.bounds = embedding.bounds;
    summary.groundGrid = embedding.groundGrid;
    summary.actualResolutionMeters = embedding.actualResolutionMeters;

    ScienceMetricResult meanMetric;
    meanMetric.metric = selectedMetric;
    meanMetric.baselineYear = baselineYear;
    meanMetric.comparisonYear = comparisonYear;
    meanMetric.value = mean;
    meanMetric.unit = metricUnit(selectedMetric);
    std::vector<std::string> interpretation = {
        "Regional values summarize the both-valid overlap in embedding space.",
        "Hotspots are relative to the displayed embedding-space quantile."};
    std::vector<std::string> limitations = {
        "The hotspot threshold is relative and is not a physical-change "
        "threshold.",
        "Embedding-space differences do not identify a physical cause."};

    if (isCancelled(cancelled))
    {
        error = "regional change analysis cancelled";
        return nullptr;
    }
    ScienceAnalysisPayload output;
    output.kind = ScienceAnalysisKind::RegionalChange;
    output.metrics =
        std::make_shared<const std::vector<ScienceMetricResult>>(
            std::initializer_list<ScienceMetricResult>{meanMetric});
    output.scalarChangeRaster = std::move(changeRaster);
    output.regionalChange = std::move(summary);
    output.interpretation =
        std::make_shared<const std::vector<std::string>>(
            std::move(interpretation));
    output.limitations =
        std::make_shared<const std::vector<std::string>>(
            std::move(limitations));
    return std::make_shared<const ScienceAnalysisPayload>(std::move(output));
}

bool ScienceAnalysisEngine::analyzeRegionalAnnualSummaries(
    const std::vector<int>& years, const YearSliceLoader& loadYear,
    ScienceAnalysisPayload& output,
    const std::function<bool()>& cancelled, std::string& error)
{
    error.clear();
    if (years.empty())
        return fail(error, "annual regional summaries require years");
    if (years.size() > MAX_ANALYSIS_YEARS)
        return fail(error, "annual regional summaries exceed the nine-year bound");
    if (!loadYear)
        return fail(error, "annual regional summaries require a year loader");
    std::vector<int> orderedYears = years;
    std::sort(orderedYears.begin(), orderedYears.end());
    if (std::adjacent_find(orderedYears.begin(), orderedYears.end()) !=
        orderedYears.end())
        return fail(error, "annual regional summaries require unique years");

    std::vector<double> values(
        orderedYears.size(), std::numeric_limits<double>::quiet_NaN());
    std::vector<unsigned char> validity(orderedYears.size(), 0);
    std::array<float, SCIENCE_EMBEDDING_COMPONENTS> baselineDirection{};
    bool haveBaseline = false;
    int baselineYear = 0;
    std::shared_ptr<const ScienceGroundGrid> comparisonGrid;
    ScienceWgs84Bounds comparisonBounds;
    double comparisonResolution = 0.0;
    int comparisonWidth = 0;
    int comparisonHeight = 0;

    for (std::size_t yearIndex = 0;
         yearIndex < orderedYears.size(); ++yearIndex)
    {
        if (isCancelled(cancelled))
            return fail(error, "annual regional summaries cancelled");
        std::string loadError;
        std::shared_ptr<const ScienceEmbeddingPayload> slice =
            loadYear(orderedYears[yearIndex], loadError);
        if (!slice)
        {
            if (!loadError.empty())
                return fail(error, yearContext(
                    "annual regional loader failed for year",
                    orderedYears[yearIndex]) + ": " + loadError);
            return fail(error, yearContext(
                "annual regional loader returned no slice for year",
                orderedYears[yearIndex]));
        }
        if (isCancelled(cancelled))
        {
            slice.reset();
            return fail(error, "annual regional summaries cancelled");
        }

        std::size_t cellCount = 0;
        std::string validationError;
        if (!validateEmbeddingShape(
                *slice, true, cellCount, validationError,
                "annual regional slice"))
        {
            slice.reset();
            return fail(error, validationError);
        }
        if (slice->years->size() != 1 ||
            slice->years->front() != orderedYears[yearIndex])
        {
            slice.reset();
            return fail(error,
                        "annual regional slice year does not match the request");
        }

        if (!comparisonGrid)
        {
            comparisonGrid = slice->groundGrid;
            comparisonBounds = slice->bounds;
            comparisonResolution = slice->actualResolutionMeters;
            comparisonWidth = slice->width;
            comparisonHeight = slice->height;
        }
        else if (slice->width != comparisonWidth ||
                 slice->height != comparisonHeight ||
                 !equalBounds(slice->bounds, comparisonBounds) ||
                 slice->actualResolutionMeters != comparisonResolution ||
                 !equalGroundGrid(*slice->groundGrid, *comparisonGrid))
        {
            slice.reset();
            return fail(error,
                        "annual regional slices require the same exact ground grid");
        }

        const bool hasDeclaredValid = std::any_of(
            slice->mask->begin(), slice->mask->end(),
            [](unsigned char valid) { return valid != 0; });
        if (hasDeclaredValid)
        {
            std::array<float, SCIENCE_EMBEDDING_COMPONENTS> meanDirection{};
            double concentration = 0.0;
            std::string aggregateError;
            if (!aggregateEmbeddingVectors(
                    slice->values->data(), slice->mask->data(), cellCount,
                    meanDirection, concentration, aggregateError))
            {
                slice.reset();
                return fail(error, yearContext(
                    "annual regional summary failed for year",
                    orderedYears[yearIndex]) + ": " + aggregateError);
            }
            if (!haveBaseline)
            {
                baselineDirection = meanDirection;
                baselineYear = orderedYears[yearIndex];
                values[yearIndex] = 0.0;
                validity[yearIndex] = 1;
                haveBaseline = true;
            }
            else
            {
                ScienceVectorMetrics metrics;
                std::string compareError;
                if (!compareEmbeddingVectors(
                        baselineDirection.data(), meanDirection.data(),
                        metrics, compareError) ||
                    !std::isfinite(metrics.cosineDistance))
                {
                    slice.reset();
                    return fail(error, yearContext(
                        "annual regional comparison failed for year",
                        orderedYears[yearIndex]) + ": " + compareError);
                }
                values[yearIndex] = metrics.cosineDistance;
                validity[yearIndex] = 1;
            }
        }
        slice.reset();
    }
    if (!haveBaseline)
        return fail(error, "annual regional summaries have no valid year");
    if (isCancelled(cancelled))
        return fail(error, "annual regional summaries cancelled");

    ScienceAnnualSeries series;
    series.metric = ScienceMetric::CosineDistance;
    series.years =
        std::make_shared<const std::vector<int>>(std::move(orderedYears));
    series.values =
        std::make_shared<const std::vector<double>>(std::move(values));
    series.validity =
        std::make_shared<const std::vector<unsigned char>>(
            std::move(validity));
    series.unit = "unitless";
    ScienceAnalysisPayload candidate;
    candidate.kind = ScienceAnalysisKind::RegionalChange;
    candidate.annualSeries =
        std::make_shared<const std::vector<ScienceAnnualSeries>>(
            std::initializer_list<ScienceAnnualSeries>{series});
    std::ostringstream baselineStatement;
    baselineStatement << "The first requested valid year (" << baselineYear
                      << ") is the annual embedding-space baseline.";
    candidate.interpretation =
        std::make_shared<const std::vector<std::string>>(
            std::initializer_list<std::string>{
                "Annual summaries compare regional aggregate mean directions "
                "using cosine distance in embedding space."});
    candidate.limitations =
        std::make_shared<const std::vector<std::string>>(
            std::initializer_list<std::string>{baselineStatement.str(),
                "Missing annual slices remain invalid and are not interpolated."});
    output = std::move(candidate);
    return true;
}

ScienceRasterPayload ScienceAnalysisEngine::materializeChangeRaster(
    const ScienceAnalysisPayload& analysis,
    const ScienceEmbeddingPayload& embedding, std::string& error)
{
    error.clear();
    ScienceRasterPayload output;
    if (analysis.kind != ScienceAnalysisKind::RegionalChange)
    {
        error = "change raster materialization requires regional analysis";
        return output;
    }
    std::size_t embeddingCells = 0;
    if (!validateEmbeddingShape(
            embedding, true, embeddingCells, error,
            "change raster embedding"))
        return output;
    const ScienceScalarChangeRaster& change = analysis.scalarChangeRaster;
    std::size_t changeCells = 0;
    if (!checkedCellCount(change.width, change.height, changeCells) ||
        changeCells != embeddingCells || !change.values || !change.mask ||
        change.values->size() != changeCells ||
        change.mask->size() != changeCells)
    {
        error = "change raster values and mask do not match the raster shape";
        return output;
    }
    if (!change.groundGrid ||
        !validGroundGrid(*change.groundGrid, change.width, change.height) ||
        change.width != embedding.width || change.height != embedding.height ||
        !equalBounds(change.bounds, embedding.bounds) ||
        change.actualResolutionMeters != embedding.actualResolutionMeters ||
        !equalGroundGrid(*change.groundGrid, *embedding.groundGrid))
    {
        error = "change raster does not match the exact embedding ground grid";
        return output;
    }

    const double minimum = analysis.regionalChange.minimum;
    const double maximum = analysis.regionalChange.maximum;
    if (!std::isfinite(minimum) || !std::isfinite(maximum) ||
        minimum > maximum)
    {
        error = "change raster requires finite ordered display statistics";
        return output;
    }
    struct Color { double red, green, blue; };
    constexpr std::array<Color, 5> COLORS = {{
        {68.0, 1.0, 84.0},
        {59.0, 82.0, 139.0},
        {33.0, 145.0, 140.0},
        {94.0, 201.0, 98.0},
        {253.0, 231.0, 37.0},
    }};
    std::vector<unsigned char> rgba(changeCells * 4, 0);
    for (std::size_t cell = 0; cell < changeCells; ++cell)
    {
        if (!change.mask->at(cell)) continue;
        const double value = static_cast<double>(change.values->at(cell));
        if (!std::isfinite(value))
        {
            error = "change raster contains a non-finite valid value";
            return ScienceRasterPayload{};
        }
        const double normalized = maximum > minimum
            ? std::max(0.0, std::min(1.0,
                (value - minimum) / (maximum - minimum))) : 0.5;
        const double colorPosition = normalized * (COLORS.size() - 1);
        const std::size_t lower = static_cast<std::size_t>(
            std::floor(colorPosition));
        const std::size_t upper = std::min(lower + 1, COLORS.size() - 1);
        const double fraction = colorPosition - static_cast<double>(lower);
        const auto interpolate = [fraction](double low, double high) {
            return static_cast<unsigned char>(
                std::lround(low + (high - low) * fraction));
        };
        rgba[cell * 4] = interpolate(
            COLORS[lower].red, COLORS[upper].red);
        rgba[cell * 4 + 1] = interpolate(
            COLORS[lower].green, COLORS[upper].green);
        rgba[cell * 4 + 2] = interpolate(
            COLORS[lower].blue, COLORS[upper].blue);
        rgba[cell * 4 + 3] = 166;
    }

    output.bounds = change.bounds;
    output.width = change.width;
    output.height = change.height;
    output.sourceWindowWidth = change.width;
    output.sourceWindowHeight = change.height;
    output.sourceResolutionMeters = change.actualResolutionMeters;
    output.displayResolutionMeters = change.actualResolutionMeters;
    output.rgba =
        std::make_shared<const std::vector<unsigned char>>(std::move(rgba));
    output.groundGrid = change.groundGrid;
    return output;
}
}
