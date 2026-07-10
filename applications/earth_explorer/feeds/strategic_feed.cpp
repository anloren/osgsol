// 静态战略数据集(P1 Task 7):五个 vendored JSON(applications/earth_explorer/data/strategic/,
// 构建时拷到 <install>/models/Earth/strategic/)各注册一个 FeedLayer 静态源图层。
//
// License 审计结论(逐数据集,详见各 JSON 顶层 _source/_license):
//   bases        HKU DataHub "Overseas Military Bases" doi:10.25442/hku.20438805.v1  CC BY-NC 4.0(非商用!)
//   ports        NGA World Port Index Pub.150(harborSize=L 的大型港)               公有领域(美国政府作品)
//   nuclear      WRI Global Power Plant Database v1.3(primary_fuel=Nuclear)          CC BY 4.0
//   spaceports   GCAT sites 表(J. McDowell, planet4589.org;仍活跃的发射场)         CC BY 4.0
//   datacenters  Epoch AI GPU Clusters(>1000 GPU 集群,含坐标的抽取)                CC BY 4.0
// worldmonitor 仓库自创的数据(如手写战略港口 62 条,AGPL 且无外部来源)一律未搬。
#include "strategic_feed.h"
#include <algorithm>
#include <map>

namespace earthfeed
{
    std::vector<FeedPoint> parseStrategic(const std::string& body, const StrategicStyle& style)
    {
        std::vector<FeedPoint> out;
        picojson::value root;
        std::string err = picojson::parse(root, body);
        if (!err.empty() || !root.is<picojson::object>()) return out;
        const picojson::value& items = root.get("items");
        if (!items.is<picojson::array>()) return out;

        // 来源署名行:从数据文件自身的 _source/_license 组装(detail 里逐点常显)
        std::string source = root.get("_source").is<std::string>() ? root.get("_source").get<std::string>() : "";
        std::string license = root.get("_license").is<std::string>() ? root.get("_license").get<std::string>() : "";
        std::string attribution;
        if (!source.empty())
        {
            attribution = std::string(u8"来源 Source: ") + source;
            if (!license.empty()) attribution += " [" + license + "]";
        }

        const picojson::array& arr = items.get<picojson::array>();
        for (size_t i = 0; i < arr.size(); ++i)
        {
            const picojson::value& it = arr[i];
            if (!it.is<picojson::object>()) continue;
            // name/lat/lon 三者缺一跳过(缺字段/畸形条目不崩、不掺假数据)
            if (!it.get("name").is<std::string>()) continue;
            if (!it.get("lat").is<double>() || !it.get("lon").is<double>()) continue;

            FeedPoint p;
            p.title = it.get("name").get<std::string>();
            p.lat = it.get("lat").get<double>(); p.lon = it.get("lon").get<double>();
            p.color = style.color; p.sizePx = style.sizePx;   // unixTime 恒 0:静态名录无事件时间
            if (it.get("country").is<std::string>() && !it.get("country").get<std::string>().empty())
                p.detail += std::string(u8"国家 Country: ") + it.get("country").get<std::string>() + "\n";
            if (it.get("info").is<std::string>() && !it.get("info").get<std::string>().empty())
                p.detail += std::string(u8"属性 Info: ") + it.get("info").get<std::string>() + "\n";
            p.detail += attribution;
            p.raw = it;
            out.push_back(p);
        }
        return out;
    }

    std::string strategicSummaryJson(const std::vector<FeedPoint>& pts)
    {
        std::map<std::string, int> byCountry;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            std::string c = u8"未知";
            if (pts[i].raw.is<picojson::object>() && pts[i].raw.get("country").is<std::string>())
                c = pts[i].raw.get("country").get<std::string>();
            byCountry[c]++;
        }
        // 按计数降序取 TOP5(map 无序不可直接截断)
        std::vector<std::pair<std::string, int> > sorted(byCountry.begin(), byCountry.end());
        std::sort(sorted.begin(), sorted.end(),
                  [](const std::pair<std::string, int>& a, const std::pair<std::string, int>& b)
                  { return a.second != b.second ? a.second > b.second : a.first < b.first; });
        picojson::object r; r["count"] = picojson::value((double)pts.size());
        picojson::object top;
        for (size_t i = 0; i < sorted.size() && i < 5; ++i)
            top[sorted[i].first] = picojson::value((double)sorted[i].second);
        r["topCountries"] = picojson::value(top);
        return picojson::value(r).serialize();
    }

    namespace
    {
        // 每数据集一行配置:解析/摘要/注册逻辑全部共用,五层只差这里的字段。
        struct StrategicDataset
        {
            const char* id; const char* file;
            const char* displayName; const char* subtitle;
            const char* forceEnv; const char* fixtureEnv;
            float r, g, b; float sizePx;
            const char* toolDescCn;
        };
        static const StrategicDataset kDatasets[] =
        {
            { "bases", "bases.json", u8"军事基地 (HKU)",
              u8"HKU DataHub 海外军事基地 · CC BY-NC 4.0",
              "EARTH_BASES", "EARTH_BASES_FILE", 0.45f, 0.58f, 0.30f, 8.0f,   // 军绿
              u8"查询全球海外军事基地静态名录汇总(210 处,HKU DataHub 2022 发布、数据截至 2020):总数、按驻在国 TOP5 分桶。" },
            { "ports", "ports.json", u8"战略港口 (WPI)",
              u8"NGA World Port Index · 公有领域",
              "EARTH_PORTS", "EARTH_PORTS_FILE", 0.20f, 0.80f, 0.80f, 8.0f,   // 青
              u8"查询全球大型战略港口静态名录汇总(美国 NGA 世界港口索引中 harborSize=L 的 135 港):总数、按国家 TOP5 分桶。" },
            { "nuclear", "nuclear.json", u8"核电站 (WRI)",
              u8"WRI Global Power Plant DB · CC BY 4.0",
              "EARTH_NUCLEAR", "EARTH_NUCLEAR_FILE", 1.00f, 0.90f, 0.25f, 9.0f,   // 亮黄
              u8"查询全球核电站静态名录汇总(WRI 全球电厂数据库核燃料机组 195 座,含装机容量):总数、按国家 TOP5 分桶。" },
            { "spaceports", "spaceports.json", u8"航天发射场 (GCAT)",
              u8"GCAT planet4589.org · CC BY 4.0",
              "EARTH_SPACEPORTS", "EARTH_SPACEPORTS_FILE", 0.70f, 0.45f, 0.95f, 9.0f,   // 紫
              u8"查询全球仍活跃的航天发射场/火箭发射设施静态名录汇总(GCAT 在册 237 处):总数、按国家 TOP5 分桶。" },
            { "datacenters", "datacenters.json", u8"AI 数据中心 (Epoch)",
              u8"Epoch AI GPU Clusters · CC BY 4.0",
              "EARTH_DATACENTERS", "EARTH_DATACENTERS_FILE", 0.95f, 0.95f, 0.95f, 7.0f,   // 白
              u8"查询全球大型 AI GPU 数据中心静态名录汇总(Epoch AI,>1000 GPU 集群 313 处,含芯片型号/数量):总数、按国家 TOP5 分桶。" },
        };
    }
}

osg::Node* registerStrategicFeeds(osgViewer::Viewer& viewer, LayerManager* layers,
                                  earthai::ToolRegistry* tools, const std::string& mainFolder)
{
    osg::Group* group = new osg::Group;
    const size_t n = sizeof(earthfeed::kDatasets) / sizeof(earthfeed::kDatasets[0]);
    for (size_t i = 0; i < n; ++i)
    {
        const earthfeed::StrategicDataset& d = earthfeed::kDatasets[i];
        earthfeed::StrategicStyle style;
        style.color = osg::Vec4(d.r, d.g, d.b, 1.0f); style.sizePx = d.sizePx;

        earthfeed::FeedSpec spec;
        spec.id = d.id; spec.displayName = d.displayName;
        spec.group = u8"战略 / Strategic"; spec.subtitle = d.subtitle;
        spec.url = "";   // 静态源:无网络
        spec.staticFile = mainFolder + "/Earth/strategic/" + d.file;   // exe 锚定路径(globalInitialize 已 chdir 到 exe 目录),调用 cwd 无关
        spec.forceEnv = d.forceEnv; spec.fixtureEnv = d.fixtureEnv;    // fixtureEnv 优先于 staticFile
        spec.parse = [style](const std::string& body) { return earthfeed::parseStrategic(body, style); };
        spec.summaryJson = earthfeed::strategicSummaryJson;
        spec.toolDescriptionCn = d.toolDescCn;
        group->addChild(earthfeed::registerFeedLayer(spec, viewer, layers, tools));
    }
    return group;
}
