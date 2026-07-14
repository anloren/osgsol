#include "science_ai_tools.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <ScienceQueryService.h>
#include <modeling/Math.h>
#include <readerwriter/EarthManipulator.h>

#include "LayerManager.h"
#include "ai_tools.h"
#include "science_preview_layer.h"
#include "science_query_builder.h"

namespace
{
    picojson::value errorJson(const std::string& message)
    {
        picojson::object error;
        error["error"] = picojson::value(message);
        return picojson::value(error);
    }

    bool optionalNumber(const picojson::value& args, const char* key,
                        double& value)
    {
        if (!args.is<picojson::object>() || !args.contains(key)) return true;
        if (!args.get(key).is<double>()) return false;
        value = args.get(key).get<double>();
        return true;
    }

    bool optionalString(const picojson::value& args, const char* key,
                        std::string& value)
    {
        if (!args.is<picojson::object>() || !args.contains(key)) return true;
        if (!args.get(key).is<std::string>()) return false;
        value = args.get(key).get<std::string>();
        return true;
    }

    picojson::value sourceJson(
        const earthscience::ScienceSourceDescriptor& source)
    {
        picojson::object item;
        item["id"] = picojson::value(source.id);
        item["name"] = picojson::value(source.name);
        item["category"] = picojson::value(source.category);
        item["provider_version"] = picojson::value(source.providerVersion);
        item["attribution"] = picojson::value(source.attribution);
        item["health"] = picojson::value(std::string(
            earthscience::scienceSourceHealthName(source.health)));
        item["health_message"] = picojson::value(source.healthMessage);
        item["first_year"] = picojson::value(
            static_cast<double>(source.firstYear));
        item["last_year"] = picojson::value(
            static_cast<double>(source.lastYear));
        item["resolution_m"] = picojson::value(
            source.nativeResolutionMeters);
        item["components"] = picojson::value(
            static_cast<double>(source.componentCount));
        item["experimental"] = picojson::value(source.experimental);

        picojson::array visualizations;
        for (const auto& visualization : source.visualizations)
        {
            picojson::object entry;
            entry["id"] = picojson::value(visualization.id);
            entry["name"] = picojson::value(visualization.displayName);
            entry["display_min"] = picojson::value(
                visualization.displayMinimum);
            entry["display_max"] = picojson::value(
                visualization.displayMaximum);
            entry["legend"] = picojson::value(visualization.legend);
            picojson::array channels;
            for (const std::string& channel : visualization.channelVariables)
                channels.push_back(picojson::value(channel));
            entry["channels"] = picojson::value(channels);
            visualizations.push_back(picojson::value(entry));
        }
        item["visualizations"] = picojson::value(visualizations);
        return picojson::value(item);
    }

    picojson::value snapshotJson(
        const earthscience::ScienceJobSnapshot& snapshot)
    {
        picojson::object result;
        result["job_id"] = picojson::value(
            static_cast<double>(snapshot.jobId));
        result["source_id"] = picojson::value(snapshot.query.sourceId);
        result["state"] = picojson::value(std::string(
            earthscience::scienceJobStateName(snapshot.state)));
        result["progress"] = picojson::value(
            static_cast<double>(snapshot.progress));
        result["message"] = picojson::value(snapshot.message);
        result["lat"] = picojson::value(
            snapshot.query.geometry.point.latitude);
        result["lon"] = picojson::value(
            snapshot.query.geometry.point.longitude);
        const int year = snapshot.query.time.explicitYears.empty()
            ? 0 : snapshot.query.time.explicitYears.front();
        result["year"] = picojson::value(static_cast<double>(year));

        if (snapshot.lastSuccessfulArtifact)
        {
            const earthscience::ScienceArtifact& artifact =
                *snapshot.lastSuccessfulArtifact;
            picojson::object artifactJson;
            artifactJson["artifact_id"] = picojson::value(artifact.artifactId);
            artifactJson["generation"] = picojson::value(
                static_cast<double>(artifact.generation));
            artifactJson["visualization_id"] = picojson::value(
                artifact.visualizationId);
            artifactJson["processing_version"] = picojson::value(
                artifact.processingVersion);
            artifactJson["west"] = picojson::value(artifact.raster.bounds.west);
            artifactJson["south"] = picojson::value(artifact.raster.bounds.south);
            artifactJson["east"] = picojson::value(artifact.raster.bounds.east);
            artifactJson["north"] = picojson::value(artifact.raster.bounds.north);
            artifactJson["source_resolution_m"] = picojson::value(
                artifact.raster.sourceResolutionMeters);
            artifactJson["display_resolution_m"] = picojson::value(
                artifact.raster.displayResolutionMeters);
            const int artifactYear = artifact.query.time.explicitYears.empty()
                ? 0 : artifact.query.time.explicitYears.front();
            artifactJson["year"] = picojson::value(
                static_cast<double>(artifactYear));
            if (!artifact.sourceReferences.empty())
            {
                const earthscience::ScienceSourceReference& reference =
                    artifact.sourceReferences.front();
                artifactJson["dataset_id"] = picojson::value(reference.datasetId);
                artifactJson["source_url"] = picojson::value(reference.originalUrl);
                artifactJson["source_version"] = picojson::value(
                    reference.providerVersion);
                artifactJson["attribution"] = picojson::value(
                    reference.attribution);
            }
            result["artifact"] = picojson::value(artifactJson);
        }
        return picojson::value(result);
    }
}

void registerScienceResearchTools(
    earthai::ToolRegistry* tools,
    earthscience::ScienceQueryService* service,
    SciencePreviewLayer* layer,
    LayerManager* layers,
    osgVerse::EarthManipulator* manipulator)
{
    if (!tools || !service || !layer || !layers || !manipulator) return;

    earthai::Tool search;
    search.name = "search_science_sources";
    search.description = u8"查询所有已注册科学数据源的健康状态、时空范围、"
        u8"分辨率、可视化语义、版本与署名。";
    search.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
    search.execute = [service](const picojson::value&)
    {
        picojson::array sourcesJson;
        const std::vector<earthscience::ScienceSourceDescriptor> sources =
            service->listSources();
        for (const auto& source : sources)
            sourcesJson.push_back(sourceJson(source));
        picojson::object result;
        result["sources"] = picojson::value(sourcesJson);
        return picojson::value(result);
    };
    tools->add(search);

    earthai::Tool start;
    start.name = "start_science_research";
    start.description = u8"异步载入指定科学数据源、可视化、经纬度与年份。"
        u8"lat/lon 省略时使用当前视野中心；本工具不会改变相机。";
    start.parametersJson = "{\"type\":\"object\",\"properties\":{" 
        "\"source_id\":{\"type\":\"string\"},"
        "\"visualization_id\":{\"type\":\"string\"},"
        "\"lat\":{\"type\":\"number\"},"
        "\"lon\":{\"type\":\"number\"},"
        "\"year\":{\"type\":\"integer\"}}}";
    start.execute = [service, layer, layers, manipulator](
        const picojson::value& args)
    {
        const std::vector<earthscience::ScienceSourceDescriptor> sources =
            service->listSources();
        if (sources.empty()) return errorJson("no science sources are registered");

        std::string sourceId = sources.front().id;
        if (!optionalString(args, "source_id", sourceId))
            return errorJson("source_id must be a string");
        const auto sourceIterator = std::find_if(
            sources.begin(), sources.end(), [&sourceId](const auto& source)
            { return source.id == sourceId; });
        if (sourceIterator == sources.end())
            return errorJson("unknown science source: " + sourceId);
        const earthscience::ScienceSourceDescriptor& source = *sourceIterator;

        std::string visualizationId = source.visualizations.empty()
            ? std::string() : source.visualizations.front().id;
        if (!optionalString(args, "visualization_id", visualizationId))
            return errorJson("visualization_id must be a string");
        const earthscience::ScienceVisualizationDescriptor* visualization =
            findScienceVisualization(source, visualizationId);
        if (!visualization || visualization->id != visualizationId)
            return errorJson("unknown science visualization: " + visualizationId);

        const osg::Vec3d targetLla =
            manipulator->computeViewPointLatLonHeight();
        const osg::Vec3d eyeLla = manipulator->computeEyeLatLonHeight();
        double latitude = osg::RadiansToDegrees(targetLla[0]);
        double longitude = osg::RadiansToDegrees(targetLla[1]);
        double yearValue = source.lastYear;
        if (!optionalNumber(args, "lat", latitude) ||
            !optionalNumber(args, "lon", longitude) ||
            !optionalNumber(args, "year", yearValue))
            return errorJson("lat, lon, and year must be numbers");
        if (std::floor(yearValue) != yearValue)
            return errorJson("year must be an integer");

        const earthscience::GeoTemporalQuery query = makeSciencePointQuery(
            source, *visualization, latitude, longitude,
            static_cast<int>(yearValue), eyeLla[2] * 0.85);
        service->submit(query);
        const earthscience::ScienceJobSnapshot snapshot = service->snapshot();
        if (snapshot.state != earthscience::ScienceJobState::Failed ||
            snapshot.lastSuccessfulArtifact)
        {
            layer->setVisible(true);
            layers->setEnabled("alphaearth", true);
        }
        return snapshotJson(snapshot);
    };
    tools->add(start);

    earthai::Tool get;
    get.name = "get_research_job";
    get.description = u8"查询当前科学研究任务的状态、进度、来源证据和结果范围。";
    get.parametersJson = "{\"type\":\"object\",\"properties\":{" 
        "\"job_id\":{\"type\":\"integer\"}}}";
    get.execute = [service](const picojson::value& args)
    {
        const earthscience::ScienceJobSnapshot snapshot = service->snapshot();
        double requested = static_cast<double>(snapshot.jobId);
        if (!optionalNumber(args, "job_id", requested) ||
            std::floor(requested) != requested)
            return errorJson("job_id must be an integer");
        if (static_cast<std::uint64_t>(requested) != snapshot.jobId)
            return errorJson("science job is not current");
        return snapshotJson(snapshot);
    };
    tools->add(get);

    earthai::Tool show;
    show.name = "show_science_artifact";
    show.description = u8"显示最后一次成功的科学结果。只切换图层可见性，"
        u8"不会移动或重置相机。";
    show.parametersJson = "{\"type\":\"object\",\"properties\":{" 
        "\"job_id\":{\"type\":\"integer\"}}}";
    show.execute = [service, layer, layers](const picojson::value& args)
    {
        const earthscience::ScienceJobSnapshot snapshot = service->snapshot();
        const std::shared_ptr<const earthscience::ScienceArtifact>& artifact =
            snapshot.lastSuccessfulArtifact;
        if (!artifact)
            return errorJson("science artifact is not ready; call get_research_job first");

        double requested = static_cast<double>(snapshot.jobId);
        if (!optionalNumber(args, "job_id", requested) ||
            std::floor(requested) != requested)
            return errorJson("job_id must be an integer");
        const std::uint64_t requestedId =
            static_cast<std::uint64_t>(requested);
        if (requestedId != snapshot.jobId &&
            requestedId != artifact->generation)
            return errorJson("science artifact job_id is not current or retained");

        layer->setVisible(true);
        layers->setEnabled("alphaearth", true);
        picojson::object result;
        result["ok"] = picojson::value(true);
        result["visible"] = picojson::value(true);
        result["camera_changed"] = picojson::value(false);
        result["current_job_id"] = picojson::value(
            static_cast<double>(snapshot.jobId));
        result["current_job_state"] = picojson::value(std::string(
            earthscience::scienceJobStateName(snapshot.state)));
        result["artifact_generation"] = picojson::value(
            static_cast<double>(artifact->generation));
        return picojson::value(result);
    };
    tools->add(show);
}
