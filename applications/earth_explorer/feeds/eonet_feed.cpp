// NASA EONET(Earth Observatory Natural Event Tracker)自然事件源。无 key JSON,
// 真实响应形状已用 curl 核实(见 test/eonet_fixture.json):events[] → 每事件取
// geometry[] 里日期最新的一条 Point(coordinates=[lon,lat],经度在前)。过滤:
// category "earthquakes" 整类排除(与 USGS quakes 层重复);"wildfires" 只保留最新
// geometry 在 48h 内的(worldmonitor 同款降噪:EONET 野火 open 状态拖得很长)。
#include "eonet_feed.h"
#include <cstdio>
#include <ctime>
#include <map>

namespace earthfeed
{
    // 13 类 category → 中文名/点色全表(earthquakes 之外的全部官方类别,行尾注色名):
    struct EonetCat { const char* id; const char* cn; float r, g, b; };
    static const EonetCat kEonetCats[] = {
        { "volcanoes", u8"火山", 0.95f, 0.25f, 0.15f },  { "severeStorms", u8"风暴", 0.25f, 0.55f, 1.00f },  // 红 / 蓝
        { "wildfires", u8"野火", 1.00f, 0.55f, 0.10f },  { "seaLakeIce", u8"海冰", 0.20f, 0.85f, 0.85f },    // 橙 / 青
        { "icebergs", u8"冰山", 0.65f, 0.90f, 0.95f },   { "floods", u8"洪水", 0.15f, 0.35f, 0.85f },        // 淡青 / 深蓝
        { "drought", u8"干旱", 0.75f, 0.65f, 0.30f },    { "dustHaze", u8"沙尘", 0.85f, 0.75f, 0.55f },      // 土黄 / 沙色
        { "landslides", u8"滑坡", 0.60f, 0.40f, 0.20f }, { "manmade", u8"人为事件", 0.60f, 0.60f, 0.60f },   // 棕 / 灰
        { "snow", u8"降雪", 0.95f, 0.95f, 0.95f },       { "tempExtremes", u8"极端温度", 0.90f, 0.20f, 0.80f },  // 白 / 品红
        { "waterColor", u8"水色", 0.25f, 0.80f, 0.35f } };                                                   // 绿

    // ISO8601(如 "2026-07-02T18:00:00Z")→ Unix 秒(FeedPoint.unixTime 约定是秒不是毫秒)
    static long long parseIso8601(const std::string& s)
    {
        struct tm t = tm();
        if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday,
                   &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) { return 0; }
        t.tm_year -= 1900; t.tm_mon -= 1;
#ifdef _WIN32
        return (long long)_mkgmtime(&t);
#else
        return (long long)timegm(&t);
#endif
    }

    std::vector<FeedPoint> parseEonet(const std::string& body)
    {
        std::vector<FeedPoint> out; picojson::value root;
        if (!picojson::parse(root, body).empty() || !root.is<picojson::object>()) return out;
        if (!root.get("events").is<picojson::array>()) return out;
        const picojson::array& arr = root.get("events").get<picojson::array>();
        for (size_t i = 0; i < arr.size(); ++i)
        {
            const picojson::value& ev = arr[i];
            if (!ev.is<picojson::object>() || !ev.get("categories").is<picojson::array>()
                || !ev.get("geometry").is<picojson::array>()) continue;
            const picojson::array& cats = ev.get("categories").get<picojson::array>();
            // 只按 categories[0] 归类(EONET 事件实际均为单类别)
            std::string catId = (!cats.empty() && cats[0].is<picojson::object>()
                && cats[0].get("id").is<std::string>()) ? cats[0].get("id").get<std::string>() : "";
            const EonetCat* cat = nullptr;   // earthquakes 不在表里 → 顺带排除;未知新类同样跳过
            for (size_t k = 0; k < sizeof(kEonetCats) / sizeof(kEonetCats[0]); ++k)
                if (catId == kEonetCats[k].id) { cat = &kEonetCats[k]; break; }
            if (cat == nullptr) continue;
            // geometry[] 取日期最新的一条 Point(ISO8601 字符串字典序 == 时间序)
            const picojson::array& gs = ev.get("geometry").get<picojson::array>();
            std::string bestDate; double lon = 0, lat = 0; bool found = false;
            for (size_t k = 0; k < gs.size(); ++k)
            {
                const picojson::value& g = gs[k];
                if (!g.is<picojson::object>() || g.get("type").to_str() != "Point"
                    || !g.get("coordinates").is<picojson::array>()
                    || !g.get("date").is<std::string>()) continue;   // 缺 date 跳过:to_str() 的 "null" 会字典序压过真实日期
                const picojson::array& c = g.get("coordinates").get<picojson::array>();
                std::string d = g.get("date").to_str();
                if (c.size() < 2 || !c[0].is<double>() || !c[1].is<double>() || d < bestDate) continue;
                bestDate = d; lon = c[0].get<double>(); lat = c[1].get<double>(); found = true;
            }
            if (!found) continue;
            long long ts = parseIso8601(bestDate);
            if (catId == "wildfires" && (long long)time(nullptr) - ts > 48LL * 3600LL) continue;
            FeedPoint p;
            p.lon = lon; p.lat = lat; p.sizePx = 9.0f; p.color = osg::Vec4(cat->r, cat->g, cat->b, 1.0f);
            p.title = std::string(cat->cn) + u8" · " + (ev.get("title").is<std::string>()
                ? ev.get("title").get<std::string>() : std::string(u8"未命名事件"));
            p.url = ev.get("link").is<std::string>() ? ev.get("link").get<std::string>() : "";
            char llbuf[64]; snprintf(llbuf, sizeof(llbuf), "%.3f, %.3f", lat, lon);
            p.detail = std::string(u8"类别 Category: ") + cat->cn + " (" + catId + ")\n"
                + u8"时间 Time: " + bestDate + "\n" + u8"经纬 LatLon: " + llbuf;
            p.unixTime = (double)ts; p.raw = ev; out.push_back(p);
        }
        return out;
    }

    // 自定义汇总:总数 + 按类别计数(比默认的 title 首词分桶更有用,格式仿 gdacs_feed.cpp)
    static std::string eonetSummaryJson(const std::vector<FeedPoint>& pts)
    {
        std::map<std::string, int> byCat;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            std::string id = "?";   // raw 不保证是 object(gdacs 复审 carry-forward):先判再取
            if (pts[i].raw.is<picojson::object>() && pts[i].raw.get("categories").is<picojson::array>())
            {
                const picojson::array& cs = pts[i].raw.get("categories").get<picojson::array>();
                if (!cs.empty() && cs[0].is<picojson::object>() && cs[0].get("id").is<std::string>())
                    id = cs[0].get("id").get<std::string>();
            }
            byCat[id]++;
        }
        picojson::object r, catObj;
        r["count"] = picojson::value((double)pts.size());
        for (std::map<std::string, int>::iterator it = byCat.begin(); it != byCat.end(); ++it)
            catObj[it->first] = picojson::value((double)it->second);
        r["byCategory"] = picojson::value(catObj); return picojson::value(r).serialize();
    }
}

osg::Node* registerEonetFeed(osgViewer::Viewer& viewer, LayerManager* layers, earthai::ToolRegistry* tools)
{
    earthfeed::FeedSpec spec;
    spec.id = "eonet"; spec.displayName = u8"EONET 事件 / EONET Events";
    spec.group = u8"实时数据 / Live"; spec.subtitle = u8"数据来源 NASA EONET";   // 署名(EONET 要求)
    spec.url = "https://eonet.gsfc.nasa.gov/api/v3/events?status=open&days=30";
    spec.refreshSeconds = 600; spec.fixtureEnv = "EARTH_EONET_FILE"; spec.forceEnv = "EARTH_EONET";
    spec.parse = earthfeed::parseEonet; spec.summaryJson = earthfeed::eonetSummaryJson;
    spec.toolDescriptionCn = u8"查询当前全球自然事件汇总(NASA EONET:火山/风暴/野火/海冰/洪水等):总数、"
        u8"按类别计数。已排除地震类(由地震层单独提供),野火只保留 48 小时内活跃的;若图层尚未开启"
        u8"会自动开启并开始抓取,此时返回的 count 可能为 0,代表数据仍在加载中。";
    return earthfeed::registerFeedLayer(spec, viewer, layers, tools);
}
