// USGS 实时地震源(T2:从 quake_data.cpp 迁移到 FeedLayer 框架)。
// 保留原实现的全部展示语义:深度→颜色、震级→点大小、详情卡字段、
// get_quakes_summary 的汇总统计(总数/最大震级/震级直方图/近24h数量)。
#include "usgs_quakes_feed.h"
#include <cstdio>
#include <ctime>

namespace earthfeed
{
    // 深度→颜色(USGS 习惯:浅红 → 中橙黄 → 深蓝),照搬 quake_data.cpp::depthColor。
    static osg::Vec4 depthColor(double depthKm)
    {
        if (depthKm < 70.0)  return osg::Vec4(0.90f, 0.18f, 0.15f, 1.0f);   // 浅:红
        if (depthKm < 300.0) return osg::Vec4(0.98f, 0.66f, 0.12f, 1.0f);   // 中:橙黄
        return osg::Vec4(0.20f, 0.45f, 0.95f, 1.0f);                        // 深:蓝
    }

    // 震级→屏幕点像素大小,照搬 quake_data.cpp::magSizePx。
    static float magSizePx(double mag)
    {
        float s = 4.0f + (float)(mag - 2.5) * 3.0f;
        return s < 4.0f ? 4.0f : (s > 28.0f ? 28.0f : s);
    }

    // 相对时间文案(详情卡用,照搬 EarthControlUI.h 原地震卡片的换算逻辑)。
    static std::string relativeTimeCn(long long timeMs)
    {
        long long nowMs = (long long)time(nullptr) * 1000LL;
        long long ageMin = (nowMs - timeMs) / 60000;
        char buf[64];
        if (ageMin < 60) snprintf(buf, sizeof(buf), u8"%lld 分钟前", ageMin);
        else if (ageMin < 1440) snprintf(buf, sizeof(buf), u8"%.1f 小时前", ageMin / 60.0);
        else snprintf(buf, sizeof(buf), u8"%.1f 天前", ageMin / 1440.0);
        return std::string(buf);
    }

    std::vector<FeedPoint> parseUsgsQuakes(const std::string& body)
    {
        std::vector<FeedPoint> out;
        picojson::value root;
        std::string err = picojson::parse(root, body);
        if (!err.empty() || !root.is<picojson::object>()) return out;
        const picojson::value& feats = root.get("features");
        if (!feats.is<picojson::array>()) return out;
        const picojson::array& arr = feats.get<picojson::array>();
        for (size_t i = 0; i < arr.size(); ++i)
        {
            const picojson::value& f = arr[i];
            if (!f.is<picojson::object>()) continue;
            const picojson::value& geom = f.get("geometry");
            const picojson::value& props = f.get("properties");
            if (!geom.is<picojson::object>() || !props.is<picojson::object>()) continue;
            const picojson::value& coords = geom.get("coordinates");
            if (!coords.is<picojson::array>()) continue;
            const picojson::array& c = coords.get<picojson::array>();
            if (c.size() < 3 || !c[0].is<double>() || !c[1].is<double>() || !c[2].is<double>()) continue;

            double lon = c[0].get<double>(), lat = c[1].get<double>(), depthKm = c[2].get<double>();
            double mag = props.get("mag").is<double>() ? props.get("mag").get<double>() : 0.0;
            // 注意:USGS properties.time 是毫秒——FeedPoint::unixTime(Unix 秒)必须 ÷1000。
            long long timeMs = props.get("time").is<double>() ? (long long)props.get("time").get<double>() : 0;
            std::string place = props.get("place").is<std::string>() ? props.get("place").get<std::string>() : "";
            std::string url = props.get("url").is<std::string>() ? props.get("url").get<std::string>() : "";

            FeedPoint p;
            p.lon = lon; p.lat = lat;
            p.color = depthColor(depthKm); p.sizePx = magSizePx(mag);
            char titleBuf[192];
            snprintf(titleBuf, sizeof(titleBuf), u8"地震 M %.1f · %s", mag,
                     place.empty() ? u8"未知位置" : place.c_str());
            p.title = titleBuf;
            char detailBuf[512];
            snprintf(detailBuf, sizeof(detailBuf),
                u8"震级 Mag: M %.1f\n深度 Depth: %.1f km\n位置 Place: %s\n经纬 LatLon: %.3f, %.3f\n时间 Time: %s",
                mag, depthKm, place.c_str(), lat, lon, relativeTimeCn(timeMs).c_str());
            p.detail = detailBuf;
            p.url = url;
            p.unixTime = timeMs / 1000.0;   // T8 事件流卡排序键(毫秒→秒)
            p.raw = f;
            out.push_back(p);
        }
        return out;
    }

    // get_quakes_summary 的汇总统计,照搬 quake_data.cpp::QuakeLayerImpl::summaryJson——
    // 字段名/直方图分桶边界/last24h 定义与迁移前完全一致(AI 聊天历史提示词、图表卡消费方
    // 都依赖这些字段名,不能改)。
    static std::string quakesSummaryJson(const std::vector<FeedPoint>& pts)
    {
        picojson::object r;
        r["count"] = picojson::value((double)pts.size());
        if (pts.empty())
        {
            r["maxMag"] = picojson::value(0.0);
            picojson::object hist;
            hist["<3"] = picojson::value(0.0); hist["3-4"] = picojson::value(0.0);
            hist["4-5"] = picojson::value(0.0); hist["5-6"] = picojson::value(0.0);
            hist[">=6"] = picojson::value(0.0);
            r["magHistogram"] = picojson::value(hist);
            r["last24h"] = picojson::value(0.0);
            return picojson::value(r).serialize();
        }

        int nLt3 = 0, n34 = 0, n45 = 0, n56 = 0, nGe6 = 0, nLast24h = 0;
        double maxMag = -1e18; long long maxTimeMs = 0; std::string maxPlace;
        long long nowMs = (long long)time(nullptr) * 1000LL;
        const long long kDayMs = 24LL * 3600LL * 1000LL;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            // mag/time/place 未在 FeedPoint 上保留专属字段,从 raw(原始 USGS Feature)取回——
            // parse 时已把整条 Feature 存进 raw,这里复原出统计需要的字段即可。
            const picojson::value& props = pts[i].raw.is<picojson::object>()
                ? pts[i].raw.get("properties") : picojson::value();
            double m = (props.is<picojson::object>() && props.get("mag").is<double>())
                ? props.get("mag").get<double>() : 0.0;
            long long t = (props.is<picojson::object>() && props.get("time").is<double>())
                ? (long long)props.get("time").get<double>() : 0;
            std::string place = (props.is<picojson::object>() && props.get("place").is<std::string>())
                ? props.get("place").get<std::string>() : "";

            if (m < 3.0) ++nLt3;
            else if (m < 4.0) ++n34;
            else if (m < 5.0) ++n45;
            else if (m < 6.0) ++n56;
            else ++nGe6;
            if (m > maxMag) { maxMag = m; maxTimeMs = t; maxPlace = place; }
            if (nowMs - t < kDayMs) ++nLast24h;
        }

        r["maxMag"] = picojson::value(maxMag);
        r["maxMagPlace"] = picojson::value(maxPlace);
        r["maxMagTimeMs"] = picojson::value((double)maxTimeMs);
        picojson::object hist;
        hist["<3"] = picojson::value((double)nLt3); hist["3-4"] = picojson::value((double)n34);
        hist["4-5"] = picojson::value((double)n45); hist["5-6"] = picojson::value((double)n56);
        hist[">=6"] = picojson::value((double)nGe6);
        r["magHistogram"] = picojson::value(hist);
        r["last24h"] = picojson::value((double)nLast24h);
        return picojson::value(r).serialize();
    }
}

osg::Node* registerUsgsQuakesFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                                  earthai::ToolRegistry* tools)
{
    earthfeed::FeedSpec spec;
    spec.id = "quakes"; spec.displayName = u8"地震 (USGS)";
    spec.group = u8"实时数据 / Live";
    spec.url = "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/2.5_day.geojson";
    spec.refreshSeconds = 60;   // 同迁移前:约每分钟刷新
    spec.fixtureEnv = "EARTH_QUAKES_FILE"; spec.forceEnv = "EARTH_QUAKES";
    spec.parse = earthfeed::parseUsgsQuakes;
    spec.summaryJson = earthfeed::quakesSummaryJson;
    spec.toolDescriptionCn = u8"查询当前地震数据汇总:总数、最大震级(及地点/时间)、"
        u8"震级直方图(<3/3-4/4-5/5-6/>=6)、过去24小时内数量。"
        u8"数据来自 USGS 实时地震流;若地震层尚未开启会自动开启并开始抓取,"
        u8"此时返回的 count 可能为 0,代表数据仍在加载中,可稍后再查询一次。";
    return earthfeed::registerFeedLayer(spec, viewer, layers, tools);
}
