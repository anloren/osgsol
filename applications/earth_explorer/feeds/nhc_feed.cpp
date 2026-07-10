// NOAA NHC 飓风路径 + 预报锥(P1 Task 6,FeedLine 折线原语首个真实消费者)。
// API 实测(2026-07-04,当日无活动风暴,结构取自图层 schema + 空响应实测):
//   服务 = tropical/NHC_tropical_weather_summary/MapServer(聚合全部活动风暴;
//   非 summary 版把每个风暴槽位拆成 AT1-5/EP1-5/CP1-5 独立层组,不可用)。
//   layer 5 Forecast Points(点):stormname / dvlbl(强度档 "D|S|H|M,Tropical
//     Cyclone",renderer uniqueValues 实测)/ tcdvlp(强度全名)/ maxwind|gust(kt)
//     / mslp(mb)/ tau(预报时效 h,0=当前位置)/ fldatelbl / idp_filedate(epoch
//     毫秒!)…;layer 6 Forecast Track(折线);layer 7 Forecast Cone(面)。
//   空响应 = {"type":"FeatureCollection","features":[]}(飓风季外常态,N=0 合法)。
// 多层拉取裁定:框架一 spec 一 URL(主 URL = 点层 5);MapServer 只支持逐层 query
// (identify 需给几何、find 是文本搜索,均不合用)→ Track/Cone 由 parseNhcGeometry
// 在抓取线程内 libhv 同步补拉(阻塞可接受),超时 10s,失败只出点层 = 优雅降级;
// fixture 模式(EARTH_NHC_FILE)从 <fixture>.track / <fixture>.cone 兄弟文件读。
#include "nhc_feed.h"
#include "3rdparty/libhv/all/client/requests.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

namespace earthfeed
{
    // ArcGIS GeoJSON 防御性取 features(eonet 标准:任何形态畸形 → NULL 不崩)
    static const picojson::array* nhcFeatures(picojson::value& root, const std::string& body)
    {
        if (!picojson::parse(root, body).empty() || !root.is<picojson::object>()) return NULL;
        const picojson::value& f = root.get("features");
        return f.is<picojson::array>() ? &f.get<picojson::array>() : NULL;
    }
    // 字段安全取值(调用方保证 o 是 object;缺失/类型不符 → 0 / 空串)
    static double nhcNum(const picojson::value& o, const char* k)
    { return o.get(k).is<double>() ? o.get(k).get<double>() : 0.0; }
    static std::string nhcStr(const picojson::value& o, const char* k)
    { return o.get(k).is<std::string>() ? o.get(k).get<std::string>() : std::string(); }

    // dvlbl 首字符 = 强度档(实测:D 热带低压 / S 热带风暴 / H 飓风 / M 大飓风)
    static osg::Vec4 nhcColor(const std::string& dvlbl)
    {
        char c = dvlbl.empty() ? '?' : dvlbl[0];
        if (c == 'H' || c == 'M') return osg::Vec4(1.0f, 0.25f, 0.2f, 1.0f);   // 飓风红
        if (c == 'S') return osg::Vec4(1.0f, 0.85f, 0.3f, 1.0f);               // 风暴黄
        if (c == 'D') return osg::Vec4(0.3f, 0.55f, 1.0f, 1.0f);               // 低压蓝
        return osg::Vec4(0.8f, 0.8f, 0.8f, 1.0f);                              // 未知灰
    }
    static const char* nhcBucket(const std::string& dvlbl)   // summaryJson 分桶键
    {
        char c = dvlbl.empty() ? '?' : dvlbl[0];
        return (c == 'M') ? "major" : (c == 'H') ? "hurricane" : (c == 'S') ? "storm"
             : (c == 'D') ? "depression" : "other";
    }

    // 点:Forecast Points 层含"当前位置(tau=0)+ 预报点(tau=12/24/…)";每风暴取
    // tau 最小的一条 = 当前位置(v1 只画当前位置,未来路径由 Track 折线表达)。
    std::vector<FeedPoint> parseNhcPoints(const std::string& body)
    {
        typedef std::map<std::string, std::pair<size_t, double> > CurMap;   // 风暴名→(下标,tau)
        std::vector<FeedPoint> out; picojson::value root; CurMap cur;
        const picojson::array* feats = nhcFeatures(root, body); if (!feats) return out;
        for (size_t i = 0; i < feats->size(); ++i)
        {
            const picojson::value& f = (*feats)[i];
            if (!f.is<picojson::object>() || !f.get("properties").is<picojson::object>()
                || !f.get("geometry").is<picojson::object>()) continue;
            const picojson::value& c = f.get("geometry").get("coordinates");
            if (!c.is<picojson::array>() || c.get<picojson::array>().size() < 2
                || !c.get(0).is<double>() || !c.get(1).is<double>()) continue;
            std::string name = nhcStr(f.get("properties"), "stormname");
            if (name.empty()) continue;
            double tau = nhcNum(f.get("properties"), "tau");
            CurMap::iterator it = cur.find(name);
            if (it == cur.end() || tau < it->second.second) cur[name] = std::make_pair(i, tau);
        }
        for (CurMap::iterator it = cur.begin(); it != cur.end(); ++it)
        {
            const picojson::value& f = (*feats)[it->second.first];
            const picojson::value& pr = f.get("properties");
            const picojson::value& c = f.get("geometry").get("coordinates");
            std::string dvlbl = nhcStr(pr, "dvlbl"), inten = nhcStr(pr, "tcdvlp");
            if (inten.empty()) inten = dvlbl.empty() ? "?" : dvlbl;
            FeedPoint p;
            p.lon = c.get(0).get<double>(); p.lat = c.get(1).get<double>();
            p.sizePx = 12.0f; p.color = nhcColor(dvlbl);
            p.title = inten + " " + it->first;   // 强度全名 + 风暴名
            char buf[224];
            snprintf(buf, sizeof(buf),
                     u8"强度 Intensity: %s\n最大风速 Max Wind: %.0f kt(阵风 %.0f kt)\n"
                     u8"气压 Pressure: %.0f mb\n时间 Time: %s",
                     inten.c_str(), nhcNum(pr, "maxwind"), nhcNum(pr, "gust"),
                     nhcNum(pr, "mslp"), nhcStr(pr, "fldatelbl").c_str());
            p.detail = std::string(buf) + "\n" + u8"数据来源 NOAA NHC";
            p.url = "https://www.nhc.noaa.gov/";
            p.unixTime = nhcNum(pr, "idp_filedate") / 1000.0;   // ArcGIS Date=epoch 毫秒→秒
            picojson::object r; r["name"] = picojson::value(it->first);
            r["bucket"] = picojson::value(std::string(nhcBucket(dvlbl)));
            r["maxwindKt"] = picojson::value(nhcNum(pr, "maxwind"));   // summaryJson 用
            p.raw = picojson::value(r); out.push_back(p);
        }
        return out;
    }

    // [[lon,lat],...] → FeedLine(有效点 <2 整条丢弃;畸形元素逐点跳过)
    static void nhcCoordsToLine(const picojson::value& coords, const osg::Vec4& color,
                                FeedGeometry& g)
    {
        if (!coords.is<picojson::array>()) return;
        const picojson::array& a = coords.get<picojson::array>();
        FeedLine ln; ln.color = color;   // liftMeters 用默认 15000(贴地线会被地形淹没)
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (!a[i].is<picojson::array>() || a[i].get<picojson::array>().size() < 2
                || !a[i].get(0).is<double>() || !a[i].get(1).is<double>()) continue;
            ln.latLonDeg.push_back(osg::Vec2d(a[i].get(1).get<double>(), a[i].get(0).get<double>()));
        }
        if (ln.latLonDeg.size() >= 2) g.lines.push_back(ln);
    }
    // Track(LineString 全段 / MultiLineString 逐段)与 Cone(Polygon/MultiPolygon
    // **只取外环** rings[0],内环=洞忽略;v1 不做面填充,P0 既定降级;GeoJSON 环首尾
    // 同点,天然闭合)共用一个遍历器:coneMode 只改"几何类型名 + 是否剥一层外环"。
    static void parseNhcLines(const std::string& body, bool coneMode,
                              const osg::Vec4& color, FeedGeometry& g)
    {
        picojson::value root;
        const picojson::array* feats = nhcFeatures(root, body); if (!feats) return;
        for (size_t i = 0; i < feats->size(); ++i)
        {
            const picojson::value& f = (*feats)[i];
            if (!f.is<picojson::object>() || !f.get("geometry").is<picojson::object>()) continue;
            const picojson::value& geom = f.get("geometry");
            std::string t = nhcStr(geom, "type");
            if (!geom.get("coordinates").is<picojson::array>()) continue;
            const picojson::array& co = geom.get("coordinates").get<picojson::array>();
            if (t == (coneMode ? "Polygon" : "LineString"))
            {
                if (!coneMode) { nhcCoordsToLine(geom.get("coordinates"), color, g); }
                else if (!co.empty()) { nhcCoordsToLine(co[0], color, g); }
            }
            else if (t == (coneMode ? "MultiPolygon" : "MultiLineString"))
            {
                for (size_t s = 0; s < co.size(); ++s)
                {
                    if (!coneMode) { nhcCoordsToLine(co[s], color, g); }
                    else if (co[s].is<picojson::array>() && !co[s].get<picojson::array>().empty())
                    { nhcCoordsToLine(co[s].get(0), color, g); }
                }
            }
        }
    }
    static void parseNhcTrack(const std::string& body, FeedGeometry& g)   // 预报路径:白
    { parseNhcLines(body, false, osg::Vec4(0.95f, 0.95f, 0.95f, 1.0f), g); }
    static void parseNhcCone(const std::string& body, FeedGeometry& g)    // 锥外环:浅黄
    { parseNhcLines(body, true, osg::Vec4(1.0f, 0.9f, 0.4f, 1.0f), g); }

    static const char* kNhcBase = "https://mapservices.weather.noaa.gov/tropical/rest/"
                                  "services/tropical/NHC_tropical_weather_summary/MapServer/";
    // fixture env 名唯一出处(nhcAuxBody 与 FeedSpec.fixtureEnv 共用,防改名劈叉)
    static const char* kNhcFixtureEnv = "EARTH_NHC_FILE";
    // Track/Cone 层补拉(文件头"多层拉取裁定"):fixture 兄弟文件优先,否则同步 GET
    static std::string nhcAuxBody(const char* layerId, const char* siblingExt)
    {
        const char* fx = getenv(kNhcFixtureEnv);
        if (fx && *fx)
        {
            std::ifstream in((std::string(fx) + siblingExt).c_str());
            if (!in) return std::string();   // 缺兄弟文件 → 本层降级为空(约定见 fixture)
            std::stringstream ss; ss << in.rdbuf(); return ss.str();
        }
        requests::Request req(new HttpRequest);
        req->method = HTTP_GET; req->timeout = 10;
        req->url = std::string(kNhcBase) + layerId + "/query?where=1%3D1&outFields=*&f=geojson";
        requests::Response resp = requests::request(req);
        if (resp && resp->status_code == 200) return resp->body;
        std::cout << "[Feed] nhc aux layer " << layerId << " fetch failed, status="
                  << (resp ? (int)resp->status_code : -1) << "\n";
        return std::string();
    }
    FeedGeometry parseNhcGeometry(const std::string&)   // 入参=点层 body,线/锥不在其中
    {
        FeedGeometry g; parseNhcTrack(nhcAuxBody("6", ".track"), g);
        parseNhcCone(nhcAuxBody("7", ".cone"), g); return g;
    }

    // 汇总:count + 按强度分桶(五桶恒在,空输入也是完整结构)+ 风暴名列表
    static std::string nhcSummaryJson(const std::vector<FeedPoint>& pts)
    {
        static const char* kBuckets[5] = { "major", "hurricane", "storm", "depression", "other" };
        std::map<std::string, int> cnt; picojson::array names;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (!pts[i].raw.is<picojson::object>()) continue;   // raw 不保证 object(gdacs carry-forward)
            cnt[pts[i].raw.get("bucket").to_str()]++;
            names.push_back(pts[i].raw.get("name"));
        }
        picojson::object by;
        for (int b = 0; b < 5; ++b) by[kBuckets[b]] = picojson::value((double)cnt[kBuckets[b]]);
        picojson::object r; r["count"] = picojson::value((double)pts.size());
        r["byIntensity"] = picojson::value(by); r["storms"] = picojson::value(names);
        return picojson::value(r).serialize();
    }
}

osg::Node* registerNhcFeed(osgViewer::Viewer& viewer, LayerManager* layers, earthai::ToolRegistry* tools)
{
    earthfeed::FeedSpec spec;
    spec.id = "nhc"; spec.displayName = u8"飓风 / Hurricanes";
    spec.group = u8"实时数据 / Live"; spec.subtitle = u8"数据来源 NOAA NHC";
    spec.url = std::string(earthfeed::kNhcBase) + "5/query?where=1%3D1&outFields=*&f=geojson";
    spec.refreshSeconds = 1800;   // 官方公告 6h 一发、中间有更新,30min 足够
    spec.fixtureEnv = earthfeed::kNhcFixtureEnv; spec.forceEnv = "EARTH_NHC";
    spec.parse = earthfeed::parseNhcPoints; spec.parseGeometry = earthfeed::parseNhcGeometry;
    spec.summaryJson = earthfeed::nhcSummaryJson;
    spec.toolDescriptionCn = u8"查询当前活动的热带气旋/飓风汇总(NOAA NHC,大西洋/东太平洋/"
        u8"中太平洋):总数、按强度分桶(大飓风/飓风/热带风暴/热带低压)、风暴名列表。"
        u8"飓风季外常为 0(代表当前无活动风暴,不是故障);若图层尚未开启会自动开启并开始"
        u8"抓取,此时返回的 count 可能为 0,代表数据仍在加载中。";
    return earthfeed::registerFeedLayer(spec, viewer, layers, tools);
}
