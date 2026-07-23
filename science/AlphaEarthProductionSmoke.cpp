#include "AlphaEarthProvider.h"
#include "ScienceQueryService.h"
#include "science_workbench_query_plan.h"

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
}

int main(int argc, char** argv)
{
    if (argc != 7)
    {
        std::cerr << "usage: " << argv[0]
                  << " INDEX METHOD LAT LON FIRST_YEAR LAST_YEAR\n";
        return 2;
    }
    const std::string method = argv[2];
    double latitude = 0.0, longitude = 0.0;
    const int firstYear = std::atoi(argv[5]);
    const int lastYear = std::atoi(argv[6]);
    if (!number(argv[3], latitude) || !number(argv[4], longitude) ||
        firstYear < 2017 || lastYear > 2025 || firstYear > lastYear)
    {
        std::cerr << "invalid point or year range\n";
        return 2;
    }

    auto provider =
        std::make_unique<earthscience::AlphaEarthProvider>(argv[1]);
    const earthscience::ScienceSourceDescriptor source =
        provider->descriptor();
    earthscience::ScienceGeometry target;
    target.kind = earthscience::ScienceGeometryKind::Point;
    target.point = {latitude, longitude};
    target.requestedSpanMeters = 20000.0;
    earthscience::GeoTemporalQuery query;
    std::string error;
    if (!buildScienceWorkbenchQuery(
            source, method, target, firstYear, lastYear, query, error))
    {
        std::cerr << error << '\n';
        return 2;
    }

    auto registry =
        std::make_unique<earthscience::ScienceSourceRegistry>();
    if (!registry->add(std::move(provider), error))
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
    std::cout << "{\"event\":\"request\",\"method\":\""
              << method << "\",\"first_year\":" << firstYear
              << ",\"last_year\":" << lastYear << "}\n";

    const std::uint64_t jobId = service.submit(query);
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(60);
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
        std::cerr << "AlphaEarth " << method
                  << " production smoke failed: " << state.message << '\n';
        return 2;
    }
    const earthscience::ScienceArtifact& artifact =
        *state.lastSuccessfulAnalysisArtifact;
    if (artifact.sourceReferences.empty())
    {
        std::cerr << "AlphaEarth production artifact has no provenance\n";
        return 2;
    }
    const std::size_t metrics = artifact.analysis.metrics
        ? artifact.analysis.metrics->size() : 0;
    const std::size_t annualSeries = artifact.analysis.annualSeries
        ? artifact.analysis.annualSeries->size() : 0;
    std::cout << "{\"event\":\"artifact\",\"artifact_id\":\""
              << artifact.artifactId << "\",\"metrics\":" << metrics
              << ",\"annual_series\":" << annualSeries
              << ",\"raster_width\":"
              << artifact.analysis.scalarChangeRaster.width
              << ",\"raster_height\":"
              << artifact.analysis.scalarChangeRaster.height
              << ",\"elapsed_seconds\":"
              << state.progress.elapsedSeconds << "}\n";
    return 0;
}
