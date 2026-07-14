#include "science_ai_tools.h"

#include <cmath>
#include <SciencePreviewRuntime.h>
#include <modeling/Math.h>
#include <readerwriter/EarthManipulator.h>
#include "LayerManager.h"
#include "ai_tools.h"
#include "science_preview_layer.h"

namespace
{
    const char* stateName(earthscience::PreviewState state)
    {
        switch (state)
        {
        case earthscience::PreviewState::Unavailable: return "unavailable";
        case earthscience::PreviewState::Idle: return "idle";
        case earthscience::PreviewState::Queued: return "queued";
        case earthscience::PreviewState::Fetching: return "fetching";
        case earthscience::PreviewState::Ready: return "ready";
        case earthscience::PreviewState::Failed: return "failed";
        case earthscience::PreviewState::Cancelled: return "cancelled";
        }
        return "unknown";
    }

    picojson::value snapshotJson(
        const earthscience::SciencePreviewSnapshot& snapshot)
    {
        picojson::object result;
        result["job_id"] = picojson::value(
            static_cast<double>(snapshot.generation));
        result["state"] = picojson::value(std::string(stateName(snapshot.state)));
        result["progress"] = picojson::value(static_cast<double>(snapshot.progress));
        result["message"] = picojson::value(snapshot.message);
        result["lat"] = picojson::value(snapshot.latitude);
        result["lon"] = picojson::value(snapshot.longitude);
        result["year"] = picojson::value(static_cast<double>(snapshot.year));
        if (snapshot.state == earthscience::PreviewState::Ready)
        {
            picojson::object artifact;
            artifact["dataset_id"] = picojson::value(snapshot.artifact.datasetId);
            artifact["source_url"] = picojson::value(snapshot.artifact.sourceUrl);
            artifact["source_version"] = picojson::value(snapshot.artifact.sourceVersion);
            artifact["attribution"] = picojson::value(snapshot.artifact.attribution);
            artifact["west"] = picojson::value(snapshot.artifact.west);
            artifact["south"] = picojson::value(snapshot.artifact.south);
            artifact["east"] = picojson::value(snapshot.artifact.east);
            artifact["north"] = picojson::value(snapshot.artifact.north);
            result["artifact"] = picojson::value(artifact);
        }
        return picojson::value(result);
    }

    bool optionalNumber(const picojson::value& args, const char* key,
                        double& value)
    {
        if (!args.is<picojson::object>() || !args.contains(key)) return true;
        if (!args.get(key).is<double>()) return false;
        value = args.get(key).get<double>();
        return true;
    }
}

void registerScienceResearchTools(
    earthai::ToolRegistry* tools,
    earthscience::SciencePreviewRuntime* runtime,
    SciencePreviewLayer* layer,
    LayerManager* layers,
    osgVerse::EarthManipulator* manipulator)
{
    if (!tools || !runtime || !layer || !layers || !manipulator) return;

    earthai::Tool search;
    search.name = "search_science_sources";
    search.description = u8"查询当前可用的科学数据源。当前返回 AlphaEarth Foundations "
        u8"64 维 10 米年度嵌入数据及其年份范围、版本和署名。";
    search.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
    search.execute = [runtime](const picojson::value&)
    {
        const earthscience::ScienceSourceDescriptor& source = runtime->source();
        picojson::object item;
        item["id"] = picojson::value(source.id);
        item["name"] = picojson::value(source.name);
        item["version"] = picojson::value(source.version);
        item["attribution"] = picojson::value(source.attribution);
        item["first_year"] = picojson::value(static_cast<double>(source.firstYear));
        item["last_year"] = picojson::value(static_cast<double>(source.lastYear));
        item["resolution_m"] = picojson::value(
            static_cast<double>(source.nativeResolutionMeters));
        item["bands"] = picojson::value(static_cast<double>(source.bandCount));
        item["experimental"] = picojson::value(source.experimental);
        item["available"] = picojson::value(runtime->available());
        picojson::array sources; sources.push_back(picojson::value(item));
        picojson::object result; result["sources"] = picojson::value(sources);
        return picojson::value(result);
    };
    tools->add(search);

    earthai::Tool start;
    start.name = "start_science_research";
    start.description = u8"异步载入指定经纬度与年份的 AlphaEarth 科学图层预览。"
        u8"lat/lon 省略时使用当前视野中心；本工具不会改变相机视角。"
        u8"返回 job_id，随后调用 get_research_job 查询进度。";
    start.parametersJson = "{\"type\":\"object\",\"properties\":{" 
        "\"lat\":{\"type\":\"number\"},\"lon\":{\"type\":\"number\"},"
        "\"year\":{\"type\":\"integer\",\"minimum\":2017,\"maximum\":2025}}}";
    start.execute = [runtime, layer, layers, manipulator](const picojson::value& args)
    {
        const osg::Vec3d lla = manipulator->computeEyeLatLonHeight();
        double lat = osg::RadiansToDegrees(lla[0]);
        double lon = osg::RadiansToDegrees(lla[1]);
        double yearValue = runtime->source().lastYear;
        if (!optionalNumber(args, "lat", lat) ||
            !optionalNumber(args, "lon", lon) ||
            !optionalNumber(args, "year", yearValue))
        {
            picojson::object error;
            error["error"] = picojson::value("lat, lon, and year must be numbers");
            return picojson::value(error);
        }
        if (std::floor(yearValue) != yearValue)
        {
            picojson::object error;
            error["error"] = picojson::value("year must be an integer");
            return picojson::value(error);
        }
        const int year = static_cast<int>(yearValue);
        layer->setVisible(true);
        layers->setEnabled("alphaearth", true);
        runtime->queryPoint(lat, lon, year);
        return snapshotJson(runtime->snapshot());
    };
    tools->add(start);

    earthai::Tool get;
    get.name = "get_research_job";
    get.description = u8"查询当前 AlphaEarth 科学研究任务的状态、进度、来源证据和图层范围。";
    get.parametersJson = "{\"type\":\"object\",\"properties\":{" 
        "\"job_id\":{\"type\":\"integer\"}}}";
    get.execute = [runtime](const picojson::value& args)
    {
        const earthscience::SciencePreviewSnapshot snapshot = runtime->snapshot();
        double requested = static_cast<double>(snapshot.generation);
        if (!optionalNumber(args, "job_id", requested))
        {
            picojson::object error;
            error["error"] = picojson::value("job_id must be a number");
            return picojson::value(error);
        }
        if (static_cast<std::uint64_t>(requested) != snapshot.generation)
        {
            picojson::object error;
            error["error"] = picojson::value("science job is not current");
            error["current_job_id"] = picojson::value(
                static_cast<double>(snapshot.generation));
            return picojson::value(error);
        }
        return snapshotJson(snapshot);
    };
    tools->add(get);

    earthai::Tool show;
    show.name = "show_science_artifact";
    show.description = u8"把已经完成的 AlphaEarth 研究结果显示为独立地球图层。"
        u8"只切换图层可见性，不移动或重置相机。";
    show.parametersJson = "{\"type\":\"object\",\"properties\":{" 
        "\"job_id\":{\"type\":\"integer\"}}}";
    show.execute = [runtime, layer, layers](const picojson::value& args)
    {
        const earthscience::SciencePreviewSnapshot snapshot = runtime->snapshot();
        double requested = static_cast<double>(snapshot.generation);
        if (!optionalNumber(args, "job_id", requested) ||
            static_cast<std::uint64_t>(requested) != snapshot.generation)
        {
            picojson::object error;
            error["error"] = picojson::value("science artifact job_id is not current");
            return picojson::value(error);
        }
        if (snapshot.state != earthscience::PreviewState::Ready)
        {
            picojson::object error;
            error["error"] = picojson::value(
                "science artifact is not ready; call get_research_job first");
            return picojson::value(error);
        }
        layer->setVisible(true);
        layers->setEnabled("alphaearth", true);
        picojson::object result;
        result["ok"] = picojson::value(true);
        result["visible"] = picojson::value(true);
        result["camera_changed"] = picojson::value(false);
        result["job_id"] = picojson::value(
            static_cast<double>(snapshot.generation));
        return picojson::value(result);
    };
    tools->add(show);
}
