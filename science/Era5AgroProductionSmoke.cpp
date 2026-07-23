#include "Era5AgroProvider.h"
#include "Era5AgroSource.h"
#include "ScienceQueryService.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

namespace
{
bool number(const char* text, double& value)
{
    char* end = nullptr;
    value = std::strtod(text, &end);
    return end && end != text && *end == '\0' && std::isfinite(value);
}

bool year(const char* text, int& value)
{
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (!end || end == text || *end != '\0' ||
        parsed < 1900 || parsed > 2200)
        return false;
    value = static_cast<int>(parsed);
    return true;
}
}

int main(int argc, char** argv)
{
    if (argc != 6)
    {
        std::cerr << "usage: " << argv[0]
                  << " land|agriculture LAT LON FIRST_YEAR LAST_YEAR\n";
        return 2;
    }
    const std::string productName = argv[1];
    const earthscience::Era5AgroProduct product =
        productName == "land"
            ? earthscience::Era5AgroProduct::LandSurface
            : earthscience::Era5AgroProduct::AgriculturalClimate;
    if (productName != "land" && productName != "agriculture")
    {
        std::cerr << "product must be land or agriculture\n";
        return 2;
    }
    double latitude = 0.0, longitude = 0.0;
    int firstYear = 0, lastYear = 0;
    if (!number(argv[2], latitude) || !number(argv[3], longitude) ||
        !year(argv[4], firstYear) || !year(argv[5], lastYear) ||
        firstYear > lastYear || lastYear - firstYear > 20)
    {
        std::cerr << "invalid point or year range\n";
        return 2;
    }

    const earthscience::ScienceSourceDescriptor source =
        earthscience::describeEra5Agro(product);
    earthscience::GeoTemporalQuery query;
    query.sourceId = source.id;
    query.geometry.kind = earthscience::ScienceGeometryKind::Point;
    query.geometry.point = {latitude, longitude};
    query.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
    for (int selectedYear = firstYear;
         selectedYear <= lastYear; ++selectedYear)
        query.time.explicitYears.push_back(selectedYear);
    query.variables = earthscience::era5AgroVariableIds(product);
    query.targetResolutionMeters = source.nativeResolutionMeters;
    query.outputKind = earthscience::ScienceOutputKind::TimeSeries;
    query.analysis.kind = earthscience::ScienceAnalysisKind::PointSeries;
    query.analysis.baselineYear = firstYear;
    query.analysis.comparisonYear = lastYear;

    auto registry =
        std::make_unique<earthscience::ScienceSourceRegistry>();
    std::string error;
    if (!registry->add(
            std::make_unique<earthscience::Era5AgroProvider>(product), error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    earthscience::ScienceQueryService service(std::move(registry));
    if (!service.validateQuery(query, error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    const earthscience::ScienceQueryCost cost = service.estimate(query);
    std::cout << "{\"event\":\"request\",\"source\":\""
              << source.id << "\",\"first_year\":" << firstYear
              << ",\"last_year\":" << lastYear
              << ",\"variables\":" << query.variables.size()
              << ",\"source_bytes_upper_bound\":"
              << cost.sourceBytesUpperBound << "}\n";

    const std::uint64_t jobId = service.submit(query);
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(30);
    earthscience::ScienceJobSnapshot state = service.snapshot();
    while (state.state != earthscience::ScienceJobState::Ready &&
           state.state != earthscience::ScienceJobState::Failed &&
           state.state != earthscience::ScienceJobState::Cancelled &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        state = service.snapshot();
    }
    if (state.jobId != jobId ||
        state.state != earthscience::ScienceJobState::Ready ||
        !state.lastSuccessfulAnalysisArtifact)
    {
        std::cerr << source.id << " production smoke failed: "
                  << state.message << '\n';
        return 2;
    }
    const earthscience::ScienceArtifact& artifact =
        *state.lastSuccessfulAnalysisArtifact;
    if (artifact.variableSeries.size() != query.variables.size() ||
        artifact.sourceReferences.size() != 1)
    {
        std::cerr << source.id
                  << " production artifact is incomplete\n";
        return 2;
    }
    for (const earthscience::ScienceVariableSeries& series :
         artifact.variableSeries)
    {
        if (!series.years || !series.values || !series.validity ||
            series.years->size() !=
                static_cast<std::size_t>(lastYear - firstYear + 1) ||
            series.values->size() != series.years->size() ||
            series.validity->size() != series.years->size())
        {
            std::cerr << source.id
                      << " production series is incomplete\n";
            return 2;
        }
    }
    std::cout << "{\"event\":\"artifact\",\"artifact_id\":\""
              << artifact.artifactId << "\",\"series\":"
              << artifact.variableSeries.size() << ",\"years\":"
              << lastYear - firstYear + 1 << ",\"elapsed_seconds\":"
              << state.progress.elapsedSeconds << "}\n";
    return 0;
}
