#include "CopernicusDemProvider.h"
#include "ScienceQueryService.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

namespace
{
    std::string json(const std::string& value)
    {
        std::ostringstream stream;
        stream << '"';
        for (unsigned char byte : value)
        {
            if (byte == '"' || byte == '\\') stream << '\\' << byte;
            else if (byte == '\n') stream << "\\n";
            else if (byte >= 0x20) stream << static_cast<char>(byte);
        }
        stream << '"';
        return stream.str();
    }

    bool number(const char* text, double& value)
    {
        char* end = nullptr;
        value = std::strtod(text, &end);
        return end && end != text && *end == '\0' && std::isfinite(value);
    }

    const earthscience::ScienceEvidenceField* field(
        const earthscience::ScienceSourceReference& reference,
        const std::string& id)
    {
        for (const auto& candidate : reference.fields)
            if (candidate.id == id) return &candidate;
        return nullptr;
    }

    void stateRecord(const earthscience::ScienceJobSnapshot& state)
    {
        std::cout << "{\"event\":\"state\",\"job_id\":" << state.jobId
                  << ",\"state\":"
                  << json(earthscience::scienceJobStateName(state.state))
                  << ",\"stage\":"
                  << json(earthscience::scienceProgressStageName(
                         state.progress.stage))
                  << ",\"message\":" << json(state.message) << "}\n";
    }
}

int main(int argc, char** argv)
{
    if (argc != 4)
    {
        std::cerr << "usage: " << argv[0] << " LAT LON SPAN_METERS\n";
        return 2;
    }
    double latitude = 0.0, longitude = 0.0, span = 0.0;
    if (!number(argv[1], latitude) || !number(argv[2], longitude) ||
        !number(argv[3], span))
    {
        std::cerr << "numeric arguments must be finite numbers\n";
        return 2;
    }

    earthscience::GeoTemporalQuery query;
    query.sourceId = "copernicus-dem-glo-30";
    query.geometry.kind = earthscience::ScienceGeometryKind::Point;
    query.geometry.point = {latitude, longitude};
    query.geometry.requestedSpanMeters = span;
    query.time.mode = earthscience::ScienceTimeMode::Instant;
    query.time.instant = "2021";
    query.time.publicationTime = "2021";
    query.variables = {"surface_elevation"};
    query.targetResolutionMeters = 30.0;
    query.outputKind = earthscience::ScienceOutputKind::RasterLayer;
    query.visualizationId = "surface-elevation-hypsometric";

    auto registry = std::make_unique<earthscience::ScienceSourceRegistry>();
    std::string error;
    if (!registry->add(
            std::make_unique<earthscience::CopernicusDemProvider>(), error))
    {
        std::cerr << error << '\n';
        return 2;
    }
    earthscience::ScienceQueryService service(std::move(registry));
    const earthscience::ScienceQueryCost cost = service.estimate(query);
    std::cout << "{\"event\":\"request\",\"latitude\":" << latitude
              << ",\"longitude\":" << longitude
              << ",\"publication\":\"2021\",\"span_meters\":" << span
              << ",\"source_bytes_upper_bound\":"
              << cost.sourceBytesUpperBound
              << ",\"resident_bytes_upper_bound\":"
              << cost.residentBytesUpperBound << "}\n";

    const std::uint64_t jobId = service.submit(query);
    earthscience::ScienceJobSnapshot state = service.snapshot();
    stateRecord(state);
    earthscience::ScienceJobState previous = state.state;
    earthscience::ScienceProgressStage previousStage = state.progress.stage;
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(60);
    while (state.state != earthscience::ScienceJobState::Ready &&
           state.state != earthscience::ScienceJobState::Failed &&
           state.state != earthscience::ScienceJobState::Cancelled &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        state = service.snapshot();
        if (state.state != previous || state.progress.stage != previousStage)
        {
            stateRecord(state);
            previous = state.state;
            previousStage = state.progress.stage;
        }
    }
    if (state.state != previous || state.progress.stage != previousStage)
        stateRecord(state);
    if (state.jobId != jobId ||
        state.state != earthscience::ScienceJobState::Ready ||
        !state.lastSuccessfulPreviewArtifact)
    {
        std::cerr << "Copernicus DEM production smoke failed: "
                  << state.message << '\n';
        return 2;
    }

    const earthscience::ScienceArtifact& artifact =
        *state.lastSuccessfulPreviewArtifact;
    if (artifact.raster.width != 256 || artifact.raster.height != 256 ||
        !artifact.raster.rgba ||
        artifact.raster.rgba->size() != 256u * 256u * 4u ||
        !artifact.raster.groundGrid ||
        artifact.raster.groundGrid->points.size() != 33u * 33u ||
        artifact.scalarSummaries.size() != 1 ||
        artifact.sourceReferences.size() != 1)
    {
        std::cerr << "Copernicus DEM production artifact is incomplete\n";
        return 2;
    }
    const earthscience::ScienceScalarSummary& summary =
        artifact.scalarSummaries.front();
    const earthscience::ScienceSourceReference& reference =
        artifact.sourceReferences.front();
    const auto* verticalDatum = field(reference, "vertical_datum");
    const auto* surfaceModel = field(reference, "surface_model");
    const auto* accessMode = field(reference, "cog_access_mode");
    const auto* fallback = field(reference, "full_object_fallback");
    if (!summary.centerValid || !summary.minimumValid ||
        !summary.maximumValid || !summary.meanValid ||
        summary.validCellCount == 0 ||
        !verticalDatum || verticalDatum->value != "EGM2008" ||
        !surfaceModel || surfaceModel->value.find("DSM") == std::string::npos ||
        !accessMode || accessMode->value != "GDAL /vsicurl/ bounded window" ||
        !fallback || fallback->value != "none" ||
        reference.originalUrl.rfind(
            "https://copernicus-dem-30m.s3.amazonaws.com/", 0) != 0)
    {
        std::cerr << "Copernicus DEM numeric or provenance evidence is incomplete\n";
        return 2;
    }

    std::cout << std::fixed << std::setprecision(8)
              << "{\"event\":\"artifact\",\"artifact_id\":"
              << json(artifact.artifactId)
              << ",\"dataset_id\":" << json(reference.datasetId)
              << ",\"cog_url\":" << json(reference.originalUrl)
              << ",\"source_window_width\":"
              << artifact.raster.sourceWindowWidth
              << ",\"source_window_height\":"
              << artifact.raster.sourceWindowHeight
              << ",\"source_resolution_meters\":"
              << artifact.raster.sourceResolutionMeters
              << ",\"display_resolution_meters\":"
              << artifact.raster.displayResolutionMeters
              << ",\"center_meters\":" << summary.center
              << ",\"minimum_meters\":" << summary.minimum
              << ",\"maximum_meters\":" << summary.maximum
              << ",\"mean_meters\":" << summary.mean
              << ",\"valid_cells\":" << summary.validCellCount
              << ",\"nodata_cells\":" << summary.noDataCellCount
              << ",\"vertical_datum\":\"EGM2008\""
              << ",\"cog_access_mode\":" << json(accessMode->value)
              << ",\"full_object_fallback\":" << json(fallback->value)
              << ",\"elapsed_seconds\":" << state.progress.elapsedSeconds
              << ",\"west\":" << artifact.raster.bounds.west
              << ",\"south\":" << artifact.raster.bounds.south
              << ",\"east\":" << artifact.raster.bounds.east
              << ",\"north\":" << artifact.raster.bounds.north << "}\n";
    return 0;
}
