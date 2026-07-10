// UNHCR 流离失所 O-D 弧线源(P1 Task 5,FeedArc 弧原语首个真实消费者)。
// 无 key JSON,年度统计(CC-BY 署名必须保留)。真实响应形状已 curl 核实(裁剪见
// test/unhcr_fixture.json):{"items":[{ "year":2025, "coo_iso":"SYR", "coa_iso":"TUR",
// "coo_name"/"coa_name", "refugees":2347756, ... }]}。实测坑:refugees >0 是数字、
// =0 是字符串 "0"(numField 两态兼容);UNHCR 特有非 ISO 代码(coo_iso = UNK 未知 /
// XXA 无国籍 / TIB)质心表(country_centroids.h)查不到 → 该条丢弃。
#include "unhcr_feed.h"
#include "country_centroids.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace earthfeed
{
    // 实测两态数值字段:数字直取,字符串 atof(非数字串 → 0)
    static double numField(const picojson::value& v)
    {
        return v.is<double>() ? v.get<double>()
             : (v.is<std::string>() ? std::atof(v.get<std::string>().c_str()) : 0.0);
    }

    struct UnhcrFlow   // latA/lonA=来源国质心,latB/lonB=收容国质心
    { std::string cooName, coaName; double refugees, year, latA, lonA, latB, lonB; };

    // 共享解析核心:parse(点)与 parseGeometry(弧)被框架各调一次、吃同一份 body,
    // 都走这里 → 弧数 == 点数 == 有效流数恒成立,不写两套解析逻辑。
    // 规则:refugees ≥ 10 万(小流丢弃降噪)、双端质心可查、按人数降序取 TOP 40。
    static std::vector<UnhcrFlow> parseUnhcrFlows(const std::string& body)
    {
        std::vector<UnhcrFlow> out; picojson::value root;
        if (!picojson::parse(root, body).empty() || !root.is<picojson::object>()) return out;
        if (!root.get("items").is<picojson::array>()) return out;
        const picojson::array& arr = root.get("items").get<picojson::array>();
        for (size_t i = 0; i < arr.size(); ++i)
        {
            const picojson::value& it = arr[i];
            if (!it.is<picojson::object>()) continue;
            if (!it.get("coo_iso").is<std::string>() || !it.get("coa_iso").is<std::string>()) continue;
            UnhcrFlow f;
            f.refugees = numField(it.get("refugees"));
            if (f.refugees < 100000.0) continue;   // <10 万的小流丢弃
            std::string coo = it.get("coo_iso").get<std::string>();
            std::string coa = it.get("coa_iso").get<std::string>();
            // 质心查不到(UNHCR 特有代码或未知国):无法落到球面,整条丢弃
            if (!countryCentroid(coo, f.latA, f.lonA)) continue;
            if (!countryCentroid(coa, f.latB, f.lonB)) continue;
            f.cooName = it.get("coo_name").is<std::string>() ? it.get("coo_name").get<std::string>() : coo;
            f.coaName = it.get("coa_name").is<std::string>() ? it.get("coa_name").get<std::string>() : coa;
            f.year = numField(it.get("year"));
            out.push_back(f);
        }
        // 人数降序;并列按来源国名字典序,输出确定(TOP 40 截断不受输入序影响)
        std::sort(out.begin(), out.end(), [](const UnhcrFlow& a, const UnhcrFlow& b) {
            return a.refugees != b.refugees ? a.refugees > b.refugees : a.cooName < b.cooName; });
        if (out.size() > 40) out.resize(40);
        return out;
    }

    // 弧/点共用配色:来源国蓝 → 收容国红
    static const osg::Vec4 kCooBlue(0.25f, 0.55f, 1.0f, 1.0f), kCoaRed(1.0f, 0.3f, 0.2f, 1.0f);

    // 点(收容国质心,拾取/详情载体;弧不可拾取的补偿)。同一收容国多条流各自一点,可重叠。
    std::vector<FeedPoint> parseUnhcrPoints(const std::string& body)
    {
        std::vector<UnhcrFlow> flows = parseUnhcrFlows(body); std::vector<FeedPoint> out;
        for (size_t i = 0; i < flows.size(); ++i)
        {
            const UnhcrFlow& f = flows[i];
            FeedPoint p;
            p.lat = f.latB; p.lon = f.lonB;   // 收容国质心
            // 大小:8px 起步,每 100 万人 +1px,封顶 +8px(8 + min(8, 人数/1e6))
            p.sizePx = 8.0f + std::min(8.0f, (float)(f.refugees / 1.0e6));
            p.color = kCoaRed; p.title = f.cooName + u8" → " + f.coaName;
            char buf[96]; snprintf(buf, sizeof(buf), u8"难民人数 Refugees: %.0f\n数据年份 Year: %.0f",
                                   f.refugees, f.year);
            p.detail = std::string(buf) + "\n" + u8"数据来源 UNHCR(CC-BY)";
            p.unixTime = 0;   // 年度存量数据,无事件时间
            picojson::object r;   // summaryJson 要用的字段(不回存整条原始记录,够用即省)
            r["from"] = picojson::value(f.cooName); r["to"] = picojson::value(f.coaName);
            r["refugees"] = picojson::value(f.refugees);
            p.raw = picojson::value(r); out.push_back(p);
        }
        return out;
    }

    // 弧(来源国 → 收容国大圆,渲染走框架 buildFeedGeometryGroup,不碰 globe 管线)
    FeedGeometry parseUnhcrGeometry(const std::string& body)
    {
        std::vector<UnhcrFlow> flows = parseUnhcrFlows(body); FeedGeometry g;
        for (size_t i = 0; i < flows.size(); ++i)
        {
            const UnhcrFlow& f = flows[i]; FeedArc a;
            a.llaA = osg::Vec3d(f.latA, f.lonA, 0.0); a.llaB = osg::Vec3d(f.latB, f.lonB, 0.0);
            a.colorA = kCooBlue; a.colorB = kCoaRed;
            // 弧高:基准 0.10 + 人数加成 0.06×min(1, 人数/500 万),500 万人及以上封顶 0.16
            a.heightScale = 0.10 + 0.06 * std::min(1.0, f.refugees / 5.0e6);
            g.arcs.push_back(a);
        }
        return g;
    }

    // 汇总:count + 总人数 + TOP5 流(points 与 flows 同序=人数降序,前 5 即 TOP5)
    static std::string unhcrSummaryJson(const std::vector<FeedPoint>& pts)
    {
        double total = 0.0; picojson::array top;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (!pts[i].raw.is<picojson::object>()) continue;   // raw 不保证 object(gdacs carry-forward)
            total += numField(pts[i].raw.get("refugees"));
            if (top.size() < 5) top.push_back(pts[i].raw);
        }
        picojson::object r; r["count"] = picojson::value((double)pts.size());
        r["totalRefugees"] = picojson::value(total); r["topFlows"] = picojson::value(top);
        return picojson::value(r).serialize();
    }
}

osg::Node* registerUnhcrFeed(osgViewer::Viewer& viewer, LayerManager* layers, earthai::ToolRegistry* tools)
{
    earthfeed::FeedSpec spec;
    spec.id = "unhcr"; spec.displayName = u8"流离失所 / Displacement";
    spec.group = u8"实时数据 / Live"; spec.subtitle = u8"数据来源 UNHCR(CC-BY)";
    // 年份取"去年"(年度数据按年发布,当年尚未完整;启动时算一次,86400s 一刷不追跨年)。
    // 实测:yearFrom 单用不生效(仍返回 1951 起全量),必须 yearFrom+yearTo 成对;
    // limit=10000 一页拿全该年 ~6300 条(实测 maxPages=1)——TOP40 需全量排序,首页 200 条不够。
    time_t now = time(NULL); struct tm* g = gmtime(&now); char url[192];
    snprintf(url, sizeof(url), "https://api.unhcr.org/population/v1/population/"
             "?limit=10000&yearFrom=%d&yearTo=%d&coo_all=true&coa_all=true",
             1899 + g->tm_year, 1899 + g->tm_year);   // 1900 + tm_year - 1 = 去年
    spec.url = url;
    spec.refreshSeconds = 86400;   // 年度数据,一天一刷足够
    spec.fixtureEnv = "EARTH_UNHCR_FILE"; spec.forceEnv = "EARTH_UNHCR";
    spec.parse = earthfeed::parseUnhcrPoints; spec.parseGeometry = earthfeed::parseUnhcrGeometry;
    spec.summaryJson = earthfeed::unhcrSummaryJson;
    spec.toolDescriptionCn = u8"查询全球流离失所(难民)流向汇总(UNHCR 年度统计,CC-BY):"
        u8"总流数、总人数、TOP5 来源国→收容国流(按人数降序)。仅含 ≥10 万人的大流(TOP 40);"
        u8"若图层尚未开启会自动开启并开始抓取,此时返回的 count 可能为 0,代表数据仍在加载中。";
    return earthfeed::registerFeedLayer(spec, viewer, layers, tools);
}
