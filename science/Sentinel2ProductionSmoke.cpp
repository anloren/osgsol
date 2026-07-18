#include "ScienceQueryService.h"
#include "Sentinel2Provider.h"

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
            switch (byte)
            {
            case '"': stream << "\\\""; break;
            case '\\': stream << "\\\\"; break;
            case '\b': stream << "\\b"; break;
            case '\f': stream << "\\f"; break;
            case '\n': stream << "\\n"; break;
            case '\r': stream << "\\r"; break;
            case '\t': stream << "\\t"; break;
            default:
                if (byte < 0x20)
                    stream << "\\u" << std::hex << std::setw(4)
                           << std::setfill('0') << static_cast<int>(byte)
                           << std::dec;
                else
                    stream << static_cast<char>(byte);
            }
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
        for (const earthscience::ScienceEvidenceField& candidate :
             reference.fields)
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
    if (argc != 7)
    {
        std::cerr << "usage: " << argv[0]
                  << " LAT LON START_UTC END_UTC MAX_CLOUD_PERCENT SPAN_METERS\n";
        return 2;
    }
    double latitude = 0.0, longitude = 0.0, cloud = 0.0, span = 0.0;
    if (!number(argv[1], latitude) || !number(argv[2], longitude) ||
        !number(argv[5], cloud) || !number(argv[6], span))
    {
        std::cerr << "numeric arguments must be finite numbers\n";
        return 2;
    }

    earthscience::GeoTemporalQuery query;
    query.sourceId = "sentinel-2-l2a";
    query.geometry.point = {latitude, longitude};
    query.geometry.requestedSpanMeters = span;
    query.time.mode = earthscience::ScienceTimeMode::Interval;
    query.time.intervalStart = argv[3];
    query.time.intervalEnd = argv[4];
    query.variables = {"visual"};
    query.targetResolutionMeters = 10.0;
    query.outputKind = earthscience::ScienceOutputKind::RasterLayer;
    query.visualizationId = "natural-color-visual";
    query.sceneFilters.maximumCloudCoverPercent = cloud;
    query.sceneFilters.maximumScenes = 10;

    auto registry = std::make_unique<earthscience::ScienceSourceRegistry>();
    std::string registrationError;
    if (!registry->add(
            std::make_unique<earthscience::Sentinel2Provider>(),
            registrationError))
    {
        std::cerr << registrationError << '\n';
        return 2;
    }
    earthscience::ScienceQueryService service(std::move(registry));
    earthscience::ScienceQueryCost cost;
    try
    {
        cost = service.estimate(query);
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 2;
    }
    std::cout << "{\"event\":\"request\",\"latitude\":" << latitude
              << ",\"longitude\":" << longitude
              << ",\"interval_start\":" << json(query.time.intervalStart)
              << ",\"interval_end\":" << json(query.time.intervalEnd)
              << ",\"maximum_cloud_percent\":" << cloud
              << ",\"span_meters\":" << span
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
        std::chrono::seconds(50);
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
        std::cerr << "Sentinel-2 production smoke failed: "
                  << state.message << '\n';
        return 2;
    }

    const earthscience::ScienceArtifact& artifact =
        *state.lastSuccessfulPreviewArtifact;
    if (artifact.raster.width != 256 || artifact.raster.height != 256 ||
        !artifact.raster.rgba || artifact.raster.rgba->size() !=
            256u * 256u * 4u || !artifact.raster.groundGrid ||
        artifact.raster.groundGrid->points.size() != 33u * 33u ||
        artifact.sourceReferences.size() != 1)
    {
        std::cerr << "Sentinel-2 production artifact is incomplete\n";
        return 2;
    }
    const earthscience::ScienceSourceReference& reference =
        artifact.sourceReferences.front();
    const earthscience::ScienceEvidenceField* stacCount =
        field(reference, "stac_request_count");
    const earthscience::ScienceEvidenceField* assetCount =
        field(reference, "selected_asset_count");
    const earthscience::ScienceEvidenceField* accessMode =
        field(reference, "cog_access_mode");
    const earthscience::ScienceEvidenceField* fallback =
        field(reference, "full_object_fallback");
    const earthscience::ScienceEvidenceField* cloudEvidence =
        field(reference, "scene_cloud_cover");
    const earthscience::ScienceEvidenceField* stacEndpoint =
        field(reference, "stac_endpoint");
    if (!cloudEvidence || !stacEndpoint ||
        !stacCount || stacCount->value != "1" ||
        !assetCount || assetCount->value != "1" ||
        !accessMode || accessMode->value != "GDAL /vsicurl/ bounded window" ||
        !fallback || fallback->value != "none")
    {
        std::cerr << "Sentinel-2 production transport evidence is incomplete\n";
        return 2;
    }

    std::cout << std::fixed << std::setprecision(8)
              << "{\"event\":\"artifact\",\"artifact_id\":"
              << json(artifact.artifactId)
              << ",\"scene_id\":" << json(reference.datasetId)
              << ",\"acquisition_time\":"
              << json(reference.acquisitionTime)
              << ",\"scene_cloud_cover_percent\":"
              << json(cloudEvidence->value)
              << ",\"stac_endpoint\":" << json(stacEndpoint->value)
              << ",\"cog_url\":" << json(reference.originalUrl)
              << ",\"width\":" << artifact.raster.width
              << ",\"height\":" << artifact.raster.height
              << ",\"ground_grid_points\":"
              << artifact.raster.groundGrid->points.size()
              << ",\"source_resolution_meters\":"
              << artifact.raster.sourceResolutionMeters
              << ",\"display_resolution_meters\":"
              << artifact.raster.displayResolutionMeters
              << ",\"elapsed_seconds\":"
              << state.progress.elapsedSeconds
              << ",\"stac_request_count\":1"
              << ",\"selected_cog_asset_count\":1"
              << ",\"cog_access_mode\":" << json(accessMode->value)
              << ",\"full_object_fallback\":" << json(fallback->value)
              << ",\"west\":" << artifact.raster.bounds.west
              << ",\"south\":" << artifact.raster.bounds.south
              << ",\"east\":" << artifact.raster.bounds.east
              << ",\"north\":" << artifact.raster.bounds.north
              << ",\"warning\":"
              << json(artifact.warnings.empty() ? std::string() :
                      artifact.warnings.front()) << "}\n";
    return 0;
}
