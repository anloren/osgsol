// GDACS(Global Disaster Alert and Coordination System,UN 全球灾害预警)源。
// 无 key JSON;真实响应形状已用 curl 核实(见 test/gdacs_fixture.json,129 条 Feature,
// 因为每个事件的边界多边形/连线等几何都各占一条 Feature),按 FeatureCollection 解析:
// 先只保留 Class == "Point_Centroid"(每事件唯一的代表点,过滤掉同事件的重复几何),
// 再过滤掉 Green(非紧急)——fixture 里最终只剩 3 条(1 Red + 2 Orange)。
#include "gdacs_feed.h"
#include <cstdio>
#include <ctime>
#include <map>

namespace earthfeed
{
    // ISO8601(fromdate 如 "2026-06-29T11:35:33",无时区后缀但 GDACS 一律 UTC)→ Unix 秒
    // (FeedPoint.unixTime 约定是秒不是毫秒)。与 eonet_feed.cpp 同款各留一份 static
    // (T9 裁定:两处 12 行的纯函数,不值得为此立公共头,别过度抽象)。
    static long long gdacsParseIso8601(const std::string& s)
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

    static const char* kEventTypeCn(const std::string& t)
    {
        if (t == "EQ") return u8"地震"; if (t == "TC") return u8"热带气旋";
        if (t == "FL") return u8"洪水"; if (t == "VO") return u8"火山";
        if (t == "WF") return u8"野火"; if (t == "DR") return u8"干旱";
        return u8"灾害";
    }

    // eventtype/alertlevel → 颜色/大小;Red 最显眼,Orange 次之;Green 由调用方过滤掉。
    static bool levelStyle(const std::string& lvl, osg::Vec4& color, float& sizePx)
    {
        if (lvl == "Red")    { color = osg::Vec4(1.0f, 0.25f, 0.2f, 1.0f); sizePx = 16.0f; return true; }
        if (lvl == "Orange") { color = osg::Vec4(1.0f, 0.65f, 0.15f, 1.0f); sizePx = 12.0f; return true; }
        return false;   // Green / 未知级别:不显示
    }

    std::vector<FeedPoint> parseGdacs(const std::string& body)
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
            // 去重(T1 复审carry-forward):GDACS 每个事件在同一个 FeatureCollection 里会
            // 重复出现多份几何(影响区多边形的每个顶点、连线、圆圈……全部各自一条 Feature),
            // 其中只有 Class == "Point_Centroid" 是事件的代表点(每事件恰好一条),其余
            // Point_area/Point_Polygon_Point_N/Line_Line_N/Poly_* 都是同一事件的重复几何,
            // 全部纳入会把一次灾害算成几十条。只接受 Point_Centroid,其它 Class 一律跳过。
            std::string cls = props.get("Class").is<std::string>() ? props.get("Class").get<std::string>() : "";
            if (cls != "Point_Centroid") continue;
            const picojson::value& coords = geom.get("coordinates");
            if (!coords.is<picojson::array>()) continue;
            const picojson::array& c = coords.get<picojson::array>();
            if (c.size() < 2 || !c[0].is<double>() || !c[1].is<double>()) continue;

            std::string lvl = props.get("alertlevel").is<std::string>() ? props.get("alertlevel").get<std::string>() : "";
            osg::Vec4 color; float sizePx;
            if (!levelStyle(lvl, color, sizePx)) continue;   // Green/未知 → 过滤

            std::string etype = props.get("eventtype").is<std::string>() ? props.get("eventtype").get<std::string>() : "";
            std::string country = props.get("country").is<std::string>() ? props.get("country").get<std::string>() : u8"未知";
            std::string fromdate = props.get("fromdate").is<std::string>() ? props.get("fromdate").get<std::string>() : "";
            std::string sevText;
            const picojson::value& sev = props.get("severitydata");
            if (sev.is<picojson::object>() && sev.get("severitytext").is<std::string>())
                sevText = sev.get("severitytext").get<std::string>();

            FeedPoint p;
            p.lon = c[0].get<double>(); p.lat = c[1].get<double>();
            p.color = color; p.sizePx = sizePx;
            p.title = std::string(kEventTypeCn(etype)) + u8" · " + country;
            p.unixTime = (double)gdacsParseIso8601(fromdate);   // 事件流卡排序用(T9 补齐;解析失败=0 被 ticker 排除)
            p.detail = u8"等级 Level: " + lvl + "\n" + u8"时间 Time: " + fromdate;
            if (!sevText.empty()) p.detail += "\n" + std::string(u8"影响 Impact: ") + sevText;
            p.raw = f;
            out.push_back(p);
        }
        return out;
    }

    // 默认 summaryJson 只按 title 首词分桶;GDACS 需要更结构化的 alertlevel/eventtype 分桶,
    // 因此提供专属实现(FeedSpec.summaryJson 非空即覆盖 registerFeedLayer 的缺省逻辑)。
    static std::string gdacsSummaryJson(const std::vector<FeedPoint>& pts)
    {
        std::map<std::string, int> byLevel, byType;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            // T1 复审carry-forward:raw 不保证是 object(picojson::value 默认构造是 null),
            // .get("properties") 在非 object 上调用是未定义行为 —— 先判 is<object>() 再取。
            std::string lvl = "?", typ = "?";
            if (pts[i].raw.is<picojson::object>())
            {
                const picojson::value& props = pts[i].raw.get("properties");
                if (props.is<picojson::object>())
                {
                    if (props.get("alertlevel").is<std::string>()) lvl = props.get("alertlevel").get<std::string>();
                    if (props.get("eventtype").is<std::string>()) typ = props.get("eventtype").get<std::string>();
                }
            }
            byLevel[lvl]++; byType[typ]++;
        }
        picojson::object r; r["count"] = picojson::value((double)pts.size());
        picojson::object lvlObj, typObj;
        for (std::map<std::string, int>::iterator it = byLevel.begin(); it != byLevel.end(); ++it)
            lvlObj[it->first] = picojson::value((double)it->second);
        for (std::map<std::string, int>::iterator it = byType.begin(); it != byType.end(); ++it)
            typObj[it->first] = picojson::value((double)it->second);
        r["byAlertLevel"] = picojson::value(lvlObj); r["byEventType"] = picojson::value(typObj);
        return picojson::value(r).serialize();
    }
}

osg::Node* registerGdacsFeed(osgViewer::Viewer& viewer, LayerManager* layers, earthai::ToolRegistry* tools)
{
    earthfeed::FeedSpec spec;
    spec.id = "gdacs"; spec.displayName = u8"灾害预警 (GDACS)";
    spec.group = u8"实时数据 / Live"; spec.subtitle = u8"GDACS · UN 全球灾害预警";
    spec.url = "https://www.gdacs.org/gdacsapi/api/events/geteventlist/MAP";
    spec.refreshSeconds = 600;
    spec.fixtureEnv = "EARTH_GDACS_FILE"; spec.forceEnv = "EARTH_GDACS";
    spec.parse = earthfeed::parseGdacs;
    spec.summaryJson = earthfeed::gdacsSummaryJson;
    spec.toolDescriptionCn = u8"查询当前全球灾害预警汇总(地震/热带气旋/洪水/火山/野火/干旱等):"
        u8"总数、按预警等级(Red/Orange)分桶、按灾害类型分桶。"
        u8"数据来自 GDACS(联合国全球灾害预警协调系统),已过滤掉 Green(非紧急)级别;"
        u8"若图层尚未开启会自动开启并开始抓取,此时返回的 count 可能为 0,代表数据仍在加载中。";
    return earthfeed::registerFeedLayer(spec, viewer, layers, tools);
}
