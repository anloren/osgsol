// GDELT 新闻热点源(P1 Task 3,2026-07-09 切源修复)。无 key GeoJSON:features[] → 点,
// properties.sumtotalmentions = 该地点近 TIMESPAN 分钟的新闻提及数,源约每 15 分钟更新。
//
// 切源记录:旧 GEO 2.0 端点(api/v2/geo/geo?...&format=geojson)实测对所有请求恒返回
// 404(2026-07-04 排查,官方博客示例链接同样 404,判定为服务下线,非本机代理问题)。
// 已改用实测可用的 GKG(Global Knowledge Graph) GeoJSON 1.0:
//   https://api.gdeltproject.org/api/v1/gkg_geojson?QUERY=&OUTPUTTYPE=2&TIMESPAN=60
// 2026-07-09 实测:HTTP 200,~770KB,~366 features(QUERY 留空 = 不限关键词,全球全部
// 新闻;OUTPUTTYPE=2 = location+time 聚合;TIMESPAN 单位分钟)。
//
// 坑(实测确认,非猜测):响应体不是合法 JSON —— properties.allurls 把多条相关文章
// URL 用字面 TAB 字节(0x09)直接拼接,未转义、未加引号分隔,而 JSON 字符串内的原始
// 控制字符(<0x20,含 tab/换行)本身就是非法的,picojson(乃至任何合规 JSON 解析器)
// 遇到会直接解析失败。用 picojson 实测原始 body:parse 报 "syntax error"。另外地名
// 里还偶发非法 UTF-8 字节(如 "Valpara\xEDso" 写成 Latin-1 单字节 0xED 而非合法
// UTF-8 两字节),但这个不会让 picojson 失败 —— picojson 对 >=0x20 的字节完全不做
// UTF-8 校验,原样透传,只有 <0x20 的裸控制字符才会触发 _parse_string 的 `ch < ' '`
// 判错。因此清洗只需处理前者:sanitizeControlChars() 把整份 body 里 <0x20 的字节
// 全部替换成空格(含 tab/换行,不区分是否"空白"——它们混在字符串内部时同样非法,
// 替换后 tab 分隔的 allurls 变成空格分隔,取首条 URL 的逻辑改用空格切分即可)。
#include "gdelt_feed.h"
#include <algorithm>
#include <cstdio>

namespace earthfeed
{
    // 响应体不是合法 JSON 的根因(见文件头):properties.allurls 里裸 tab 字节
    // (以及偶发裸换行)未转义地混在 JSON 字符串内,picojson 对字符串内 <0x20 的字节
    // 一律判为语法错误。全量替换成空格 —— 结构位置(token 之间)本就是合法空白,
    // 字符串内部替换后只是把 allurls 的 tab 分隔变成空格分隔,不影响后续按分隔符
    // 取首条 URL。实测验证:替换前 picojson::parse 对真实响应体报 syntax error,
    // 替换后 366 个 feature 全部解析成功。
    static std::string sanitizeControlChars(const std::string& body)
    {
        std::string out; out.reserve(body.size());
        for (size_t i = 0; i < body.size(); ++i)
        {
            unsigned char c = (unsigned char)body[i];
            out.push_back(c < 0x20 ? ' ' : (char)c);
        }
        return out;
    }

    // allurls 是同一地点相关的多条文章 URL,原本以 tab 分隔(经 sanitizeControlChars
    // 清洗后变成空格分隔);取第一条作为详情卡"打开"按钮的链接。空串 → 返回空。
    static std::string firstUrl(const std::string& allurls)
    {
        size_t p = allurls.find(' ');
        return (p == std::string::npos) ? allurls : allurls.substr(0, p);
    }

    std::vector<FeedPoint> parseGdelt(const std::string& rawBody)
    {
        std::vector<FeedPoint> out;
        std::string body = sanitizeControlChars(rawBody);
        picojson::value root;
        if (!picojson::parse(root, body).empty() || !root.is<picojson::object>()) return out;
        if (!root.get("features").is<picojson::array>()) return out;
        const picojson::array& arr = root.get("features").get<picojson::array>();
        for (size_t i = 0; i < arr.size(); ++i)
        {
            const picojson::value& f = arr[i];
            if (!f.is<picojson::object>()) continue;
            const picojson::value& geom = f.get("geometry");
            const picojson::value& props = f.get("properties");
            if (!geom.is<picojson::object>() || !props.is<picojson::object>()) continue;
            if (!geom.get("coordinates").is<picojson::array>()) continue;
            const picojson::array& c = geom.get("coordinates").get<picojson::array>();
            if (c.size() < 2 || !c[0].is<double>() || !c[1].is<double>()) continue;
            // 降噪(worldmonitor 同款):提及数 <5 的地点多为孤立误匹配,直接丢弃;
            // 缺 sumtotalmentions 字段视作 0,一并过滤。
            double count = props.get("sumtotalmentions").is<double>()
                ? props.get("sumtotalmentions").get<double>() : 0.0;
            if (count < 5.0) continue;
            FeedPoint p;
            p.lon = c[0].get<double>(); p.lat = c[1].get<double>();
            // 大小:6px 起步,每 10 次提及 +1px,封顶 16px(6 + min(10, count/10))
            p.sizePx = 6.0f + std::min(10.0f, (float)(count / 10.0));
            // 热度渐变:count 5(黄 1,0.85,0.15)→ 50(红 1,0.2,0.1)线性插值,>50 保持纯红
            float t = (float)((count - 5.0) / 45.0); if (t > 1.0f) t = 1.0f;
            p.color = osg::Vec4(1.0f, 0.85f - 0.65f * t, 0.15f - 0.05f * t, 1.0f);
            std::string name = props.get("name").is<std::string>()
                ? props.get("name").get<std::string>() : std::string(u8"未知地点");
            p.title = name;
            char buf[32]; snprintf(buf, sizeof(buf), "%.0f", count);
            p.detail = std::string(u8"地点 Location: ") + name + "\n"
                + u8"新闻提及 Mentions: " + buf + u8"(近 60 分钟)";
            // allurls 首条 URL → 详情卡"打开"按钮(GKG 无 shareimage/html,GEO 那套
            // popup-html 抽取逻辑已随切源移除)
            if (props.get("allurls").is<std::string>())
                p.url = firstUrl(props.get("allurls").get<std::string>());
            // GKG 每要素带 urlpubtimedate,但本源用于"新闻热点"整体聚合展示,暂不接入
            // unixTime 排序(维持 GEO 时代行为:unixTime=0,天然不进入事件流 TOP N)
            p.raw = f; out.push_back(p);
        }
        return out;
    }

    // 自定义汇总:总数 + TOP5 热点地名按提及数降序(比默认 title 首词分桶有用)。
    // 地名来自网络,严禁字符串拼 JSON:全程 picojson 对象构建 + serialize 转义。
    static std::string gdeltSummaryJson(const std::vector<FeedPoint>& pts)
    {
        // 排序键沿用原语义:(-mentions, name) 升序 = 热度降序、同热度地名字典序(输出确定)。
        // 额外携带 url(= FeedPoint.url,已是 allurls 首条且经 sanitize;无则空)。
        struct Row {
            double negCnt; std::string name, url;
            bool operator<(const Row& o) const
            { return negCnt != o.negCnt ? negCnt < o.negCnt : name < o.name; }
        };
        std::vector<Row> v;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            double cnt = 0.0; std::string name = "?";   // raw 不保证是 object(gdacs 复审 carry-forward)
            if (pts[i].raw.is<picojson::object>())
            {
                const picojson::value& props = pts[i].raw.get("properties");
                if (props.is<picojson::object>())
                {
                    if (props.get("sumtotalmentions").is<double>())
                        cnt = props.get("sumtotalmentions").get<double>();
                    if (props.get("name").is<std::string>()) name = props.get("name").get<std::string>();
                }
            }
            Row row; row.negCnt = -cnt; row.name = name; row.url = pts[i].url;
            v.push_back(row);
        }
        std::sort(v.begin(), v.end());
        picojson::array top;
        for (size_t i = 0; i < v.size() && i < 5; ++i)   // 每热点仅 1 条 URL,控制 payload 体积
        {
            picojson::object o;
            o["name"] = picojson::value(v[i].name);
            o["mentions"] = picojson::value(-v[i].negCnt);
            o["url"] = picojson::value(v[i].url);
            top.push_back(picojson::value(o));
        }
        picojson::object r;
        r["count"] = picojson::value((double)pts.size());
        r["topHotspots"] = picojson::value(top);
        return picojson::value(r).serialize();
    }
}

osg::Node* registerGdeltFeed(osgViewer::Viewer& viewer, LayerManager* layers, earthai::ToolRegistry* tools)
{
    earthfeed::FeedSpec spec;
    spec.id = "gdelt"; spec.displayName = u8"新闻热点 / News Hotspots";
    spec.group = u8"实时数据 / Live"; spec.subtitle = u8"数据来源 GDELT Project";
    // GKG GeoJSON 1.0(2026-07-09 切源实测可用,见文件头):QUERY 留空 = 全球全部新闻
    // (用户已定,不限关键词);OUTPUTTYPE=2 = location+time 聚合;TIMESPAN 单位分钟。
    spec.url = "https://api.gdeltproject.org/api/v1/gkg_geojson"
               "?QUERY=&OUTPUTTYPE=2&TIMESPAN=60";
    spec.refreshSeconds = 900;   // 源每 15 分钟更新,再快也只是重复数据
    // GKG GeoJSON 1.0 响应体 ~846KB、实测约 24s 才能传完(远超 FeedSpec::timeoutSeconds
    // 默认的 15s),默认超时下经常在数据传完前被硬切断;放宽到 40s 给足余量。
    spec.timeoutSeconds = 40;
    spec.fixtureEnv = "EARTH_GDELT_FILE"; spec.forceEnv = "EARTH_GDELT";
    spec.parse = earthfeed::parseGdelt; spec.summaryJson = earthfeed::gdeltSummaryJson;
    spec.toolDescriptionCn = u8"查询当前全球新闻热点汇总(GDELT:近 60 分钟全球新闻报道的地理聚合):"
        u8"总数、TOP5 热点地名(按新闻提及数降序)及每个热点的代表文章链接 url"
        u8"(可用 get_news_content 读取该 url 的正文再总结)。已过滤提及数 <5 的低置信地点;"
        u8"若图层尚未开启会自动开启并开始抓取,此时返回的 count 可能为 0,代表数据仍在加载中。";
    return earthfeed::registerFeedLayer(spec, viewer, layers, tools);
}
