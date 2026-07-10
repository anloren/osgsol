#include "ai_world_tools.h"
#include "ai_query.h"
#include "feed_layer.h"
#include <osg/Notify>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <map>

// 统一抓取→JSON 包装:Fetching → {"pending":true,note};Failed → {"error":...};
// Ready → 解析后的 JSON 原值(顶层 array 也原样返回,调用方自行判形)。
static picojson::value toolFetchJson(earthai::AsyncJsonFetcher& f, const std::string& url,
                                     double ttlSeconds, const char* fixtureEnvName)
{
    const char* fx = fixtureEnvName ? getenv(fixtureEnvName) : NULL;
    std::string body, err;
    earthai::QueryState st = f.query(url, ttlSeconds,
        (fx && *fx) ? std::string(fx) : std::string(), body, err);
    picojson::object r;
    if (st == earthai::QueryState::Fetching)
    {
        r["pending"] = picojson::value(true);
        r["note"] = picojson::value(std::string(u8"数据抓取中,请稍等片刻后再次调用本工具"));
        return picojson::value(r);
    }
    if (st == earthai::QueryState::Failed)
    { r["error"] = picojson::value("fetch failed: " + err); return picojson::value(r); }
    picojson::value v; std::string perr = picojson::parse(v, body);
    if (!perr.empty())
    { r["error"] = picojson::value("bad json: " + perr); return picojson::value(r); }
    return v;
}

static bool getNum(const picojson::value& a, const char* k, double& out)
{
    if (!a.is<picojson::object>() || !a.contains(k) || !a.get(k).is<double>()) return false;
    out = a.get(k).get<double>(); return true;
}

static bool getStr(const picojson::value& a, const char* k, std::string& out)
{
    if (!a.is<picojson::object>() || !a.contains(k) || !a.get(k).is<std::string>()) return false;
    out = a.get(k).get<std::string>(); return true;
}

static picojson::value argError(const char* msg)
{ picojson::object e; e["error"] = picojson::value(std::string(msg)); return picojson::value(e); }

// 删除 s 中 [openLow..closeLow] 的整块(含起止标签,大小写不敏感),反复删除所有出现。
// openLow/closeLow 传小写;s 与其小写镜像同步删除,保持位置对齐。
static void removeHtmlBlock(std::string& s, const char* openLow, const char* closeLow)
{
    std::string low = s;
    for (size_t i = 0; i < low.size(); ++i) low[i] = (char)tolower((unsigned char)low[i]);
    std::string ol = openLow, cl = closeLow;
    size_t pos = 0;
    while ((pos = low.find(ol, pos)) != std::string::npos)
    {
        size_t end = low.find(cl, pos);
        if (end == std::string::npos) { s.erase(pos); low.erase(pos); break; }  // 无闭合 → 删到尾
        end += cl.size();
        s.erase(pos, end - pos); low.erase(pos, end - pos);
    }
}

// HTML → 纯正文(best-effort):删 script/style/注释块 → 删所有标签 → 解码常见实体 →
// 折叠空白 → 截断到 maxLen(UTF-8 边界安全)。
static std::string stripHtmlToText(const std::string& html, size_t maxLen)
{
    std::string s = html;
    removeHtmlBlock(s, "<script", "</script>");
    removeHtmlBlock(s, "<style", "</style>");
    removeHtmlBlock(s, "<!--", "-->");
    // 删所有 <...> 标签
    std::string noTags; noTags.reserve(s.size());
    bool inTag = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) noTags.push_back(c);
    }
    // 解码常见实体
    static const char* ents[][2] = {
        {"&amp;","&"},{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},
        {"&#39;","'"},{"&apos;","'"},{"&nbsp;"," "}
    };
    for (size_t k = 0; k < sizeof(ents)/sizeof(ents[0]); ++k)
    {
        std::string from = ents[k][0], to = ents[k][1]; size_t p = 0;
        while ((p = noTags.find(from, p)) != std::string::npos)
        { noTags.replace(p, from.size(), to); p += to.size(); }
    }
    // 折叠连续空白为单空格,吃掉前导空白
    std::string out; out.reserve(noTags.size());
    bool prevSpace = true;
    for (size_t i = 0; i < noTags.size(); ++i)
    {
        char c = noTags[i];
        bool sp = (c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v');
        if (sp) { if (!prevSpace) out.push_back(' '); prevSpace = true; }
        else { out.push_back(c); prevSpace = false; }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();   // 尾部 trim
    // 截断到 maxLen(UTF-8 边界安全):仅当切点落在多字节字符内部(被丢弃的首字节是续字节)
    // 才回退去掉半个字符,否则 maxLen 恰在字符边界,直接截断不误删完整字符。
    if (out.size() > maxLen)
    {
        bool splitsChar = ((unsigned char)out[maxLen] & 0xC0) == 0x80;
        out.resize(maxLen);
        if (splitsChar)
        {
            while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80) out.pop_back(); // 去半个字符的续字节
            if (!out.empty()) out.pop_back();   // 去该字符被切断的首字节
        }
        out += "\xE2\x80\xA6";   // …
    }
    return out;
}

// ---- get_weather_forecast(Open-Meteo,免 key;经度纬度 2 位小数取整提高缓存命中) ----
static void registerWeatherTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_weather_forecast";
    t.description = u8"查询任意经纬度的实况天气与未来5天预报(Open-Meteo,含 ECMWF AI 模式)。"
        u8"返回 current(气温/风速/降水)与 daily(逐日最高最低温/降水量)。"
        u8"若返回 pending=true 表示数据抓取中,请稍候片刻后用相同参数再次调用本工具。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"lat\":{\"type\":\"number\"},\"lon\":{\"type\":\"number\"}},"
        "\"required\":[\"lat\",\"lon\"]}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        double lat = 0, lon = 0;
        if (!getNum(a, "lat", lat) || !getNum(a, "lon", lon))
            return argError("need number lat/lon");
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180)
            return argError("out of range: lat[-90,90] lon[-180,180]");
        char url[512];
        snprintf(url, sizeof(url),
            "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
            "&current=temperature_2m,wind_speed_10m,precipitation,weather_code"
            "&daily=temperature_2m_max,temperature_2m_min,precipitation_sum,weather_code"
            "&forecast_days=5&timezone=auto", lat, lon);
        OSG_NOTICE << "[AIChat] get_weather_forecast " << lat << "," << lon << std::endl;
        return toolFetchJson(*f, url, 900.0, "EARTH_WEATHER_FILE");
    };
    tools->add(t);
}

// ---- get_crypto_prices(CoinGecko 免 key;免费层限速 → TTL 120s) ----
static void registerCryptoTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_crypto_prices";
    t.description = u8"查询加密货币现价与24小时涨跌(CoinGecko)。ids 为逗号分隔的 CoinGecko id,"
        u8"如 bitcoin,ethereum,solana;缺省 bitcoin,ethereum。"
        u8"若返回 pending=true,请稍候片刻后用相同参数再次调用。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"ids\":{\"type\":\"string\",\"description\":\"comma separated coingecko ids\"}}}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        std::string ids = "bitcoin,ethereum";
        if (a.is<picojson::object>() && a.contains("ids") && a.get("ids").is<std::string>()
            && !a.get("ids").get<std::string>().empty())
            ids = a.get("ids").get<std::string>();
        // 白名单字符校验:防止把任意字符串拼进 URL(纵深防御,非唯一防线)
        if (ids.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789,-") != std::string::npos)
            return argError("ids: lowercase coingecko ids, comma separated");
        std::string url = "https://api.coingecko.com/api/v3/simple/price?ids=" + ids +
                          "&vs_currencies=usd&include_24hr_change=true";
        OSG_NOTICE << "[AIChat] get_crypto_prices " << ids << std::endl;
        return toolFetchJson(*f, url, 120.0, "EARTH_CRYPTO_FILE");
    };
    tools->add(t);
}

// ---- get_country_indicator(World Bank v2 免 key;顶层数组 [meta, rows] → 转规整对象) ----
static void registerWorldBankTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_country_indicator";
    t.description = u8"查询某国宏观指标最近几年数值(世界银行开放数据,免key,年度)。"
        u8"country 为 ISO2 国家码(CN/US/JP...);indicator 为世行指标码,常用:"
        u8"NY.GDP.MKTP.CD=GDP现价美元, SP.POP.TOTL=总人口, FP.CPI.TOTL.ZG=CPI年通胀%, "
        u8"SL.UEM.TOTL.ZS=失业率%, NE.EXP.GNFS.ZS=出口占GDP%。"
        u8"若返回 pending=true,请稍候片刻后用相同参数再次调用。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"country\":{\"type\":\"string\"},\"indicator\":{\"type\":\"string\"}},"
        "\"required\":[\"country\",\"indicator\"]}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        if (!a.is<picojson::object>() || !a.contains("country") || !a.get("country").is<std::string>()
            || !a.contains("indicator") || !a.get("indicator").is<std::string>())
            return argError("need string country(ISO2) and indicator(code)");
        std::string cc = a.get("country").get<std::string>();
        std::string ind = a.get("indicator").get<std::string>();
        if (cc.size() < 2 || cc.size() > 3
            || cc.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos)
            return argError("country: ISO2/ISO3 letters only");
        if (ind.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.") != std::string::npos)
            return argError("indicator: worldbank code like NY.GDP.MKTP.CD");
        std::string url = "https://api.worldbank.org/v2/country/" + cc + "/indicator/" + ind +
                          "?format=json&mrnev=3&per_page=3";
        picojson::value v = toolFetchJson(*f, url, 86400.0, "EARTH_WB_FILE");
        if (v.is<picojson::object>()) return v;   // pending / error 原样透传
        picojson::object r;
        // World Bank v2 顶层是 [meta, rows] 数组;形状不符(改版/坏数据)一律降级为 error,不 crash
        if (!v.is<picojson::array>() || v.get<picojson::array>().size() < 2
            || !v.get<picojson::array>()[1].is<picojson::array>())
        { r["error"] = picojson::value("unexpected worldbank payload"); return picojson::value(r); }
        const picojson::array& rows = v.get<picojson::array>()[1].get<picojson::array>();
        picojson::array pts;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (!rows[i].is<picojson::object>()) continue;
            picojson::object p;
            p["date"] = picojson::value(rows[i].contains("date") ? rows[i].get("date").to_str()
                                                                 : std::string());
            p["value"] = (rows[i].contains("value") && rows[i].get("value").is<double>())
                ? picojson::value(rows[i].get("value").get<double>()) : picojson::value();
            pts.push_back(picojson::value(p));
        }
        r["country"] = picojson::value(cc); r["indicator"] = picojson::value(ind);
        r["points"] = picojson::value(pts);
        OSG_NOTICE << "[AIChat] get_country_indicator " << cc << " " << ind
                   << " n=" << pts.size() << std::endl;
        return picojson::value(r);
    };
    tools->add(t);
}

// ---- get_chokepoint_traffic(IMF PortWatch,ArcGIS FeatureServer 免 key) ----
// 字段名/服务路径以 Task 4 Step 0 冒烟结果为准,集中在此处修正:
// 冒烟(2026-07-06)已用真实端点核对:features[].attributes 含 portname/date/n_total,与假设一致。
static const char* kPortWatchUrl =
    "https://services9.arcgis.com/weJ1QsnbMYJlCHdG/arcgis/rest/services/"
    "Daily_Chokepoints_Data/FeatureServer/0/query"
    "?where=1%3D1&outFields=*&orderByFields=date%20DESC&resultRecordCount=120&f=json";
static const char* kPwName = "portname";
static const char* kPwDate = "date";
static const char* kPwCount = "n_total";

static void registerChokepointTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_chokepoint_traffic";
    t.description = u8"查询全球海运咽喉点(苏伊士/霍尔木兹/巴拿马/马六甲等)最新每日船流量"
        u8"(IMF PortWatch)。返回每个咽喉点的最新一条记录。"
        u8"若返回 pending=true,请稍候片刻后再次调用本工具。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value&) {
        picojson::value v = toolFetchJson(*f, kPortWatchUrl, 3600.0, "EARTH_PORTWATCH_FILE");
        if (!v.is<picojson::object>() || v.contains("pending") || v.contains("error")) return v;
        picojson::object r;
        if (!v.contains("features") || !v.get("features").is<picojson::array>())
        { r["error"] = picojson::value("unexpected portwatch payload"); return picojson::value(r); }
        const picojson::array& fs = v.get("features").get<picojson::array>();
        // 结果按 date DESC,首见即该咽喉点最新一条
        std::map<std::string, picojson::value> latest;
        picojson::array out;
        for (size_t i = 0; i < fs.size(); ++i)
        {
            if (!fs[i].is<picojson::object>() || !fs[i].contains("attributes")) continue;
            const picojson::value& at = fs[i].get("attributes");
            if (!at.is<picojson::object>() || !at.contains(kPwName)) continue;
            std::string name = at.get(kPwName).to_str();
            if (latest.count(name)) continue;
            picojson::object c;
            c["name"] = picojson::value(name);
            if (at.contains(kPwDate)) c["date"] = at.get(kPwDate);
            if (at.contains(kPwCount)) c["dailyTransits"] = at.get(kPwCount);
            latest[name] = picojson::value(c);
            out.push_back(picojson::value(c));
        }
        r["chokepoints"] = picojson::value(out);
        OSG_NOTICE << "[AIChat] get_chokepoint_traffic n=" << out.size() << std::endl;
        return picojson::value(r);
    };
    tools->add(t);
}

// ---- get_prediction_markets(Polymarket Gamma 免 key;Cloudflare JA3 可能拦 C++ TLS,
//      拦了就 error JSON 优雅降级——worldmonitor 证据:桌面原生 TLS 大概率可过) ----
static void registerPredictionTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_prediction_markets";
    t.description = u8"查询 Polymarket 预测市场当前成交量最高的开放合约(问题+各结果概率+成交量),"
        u8"多为地缘政治/宏观事件,适合回答『市场认为X概率多大』。limit 缺省 12。"
        u8"若返回 pending=true 请稍候重调;若返回 error 说明源被网络策略拦截,如实告知用户即可。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"limit\":{\"type\":\"number\",\"description\":\"1-25, default 12\"}}}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        int limit = 12; double lv = 0;
        if (getNum(a, "limit", lv) && lv >= 1 && lv <= 25) limit = (int)lv;
        char url[256];
        snprintf(url, sizeof(url),
            "https://gamma-api.polymarket.com/markets?closed=false"
            "&order=volumeNum&ascending=false&limit=%d", limit);
        picojson::value v = toolFetchJson(*f, url, 300.0, "EARTH_POLYMARKET_FILE");
        if (v.is<picojson::object>()) return v;   // pending / error
        picojson::object r;
        if (!v.is<picojson::array>())
        { r["error"] = picojson::value("unexpected polymarket payload"); return picojson::value(r); }
        const picojson::array& arr = v.get<picojson::array>();
        picojson::array ms;
        for (size_t i = 0; i < arr.size(); ++i)
        {
            if (!arr[i].is<picojson::object>()) continue;
            picojson::object m;
            m["question"] = picojson::value(arr[i].contains("question")
                ? arr[i].get("question").to_str() : std::string());
            if (arr[i].contains("volumeNum") && arr[i].get("volumeNum").is<double>())
                m["volumeUsd"] = arr[i].get("volumeNum");
            // outcomes/outcomePrices 是字符串化 JSON 数组 → 二次解析后配对
            picojson::value ov, pv2; picojson::array outs;
            std::string oe = arr[i].contains("outcomes")
                ? picojson::parse(ov, arr[i].get("outcomes").to_str()) : std::string("x");
            std::string pe = arr[i].contains("outcomePrices")
                ? picojson::parse(pv2, arr[i].get("outcomePrices").to_str()) : std::string("x");
            if (oe.empty() && pe.empty() && ov.is<picojson::array>() && pv2.is<picojson::array>())
            {
                const picojson::array& oa = ov.get<picojson::array>();
                const picojson::array& pa = pv2.get<picojson::array>();
                for (size_t k = 0; k < oa.size() && k < pa.size(); ++k)
                {
                    picojson::object o;
                    o["name"] = picojson::value(oa[k].to_str());
                    o["prob"] = picojson::value(atof(pa[k].to_str().c_str()));
                    outs.push_back(picojson::value(o));
                }
            }
            m["outcomes"] = picojson::value(outs);
            ms.push_back(picojson::value(m));
        }
        r["markets"] = picojson::value(ms);
        OSG_NOTICE << "[AIChat] get_prediction_markets n=" << ms.size() << std::endl;
        return picojson::value(r);
    };
    tools->add(t);
}

// ---- get_region_brief(P4 核心:跨源区域情报合成) ----
static void registerRegionBriefTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_region_brief";
    t.description = u8"跨源区域情报简报:汇总指定经纬度半径内所有『已开启』数据层的要素"
        u8"(地震/灾害/火点/新闻热点/飓风等,含最近8条与距离)+ 中心点实况天气。"
        u8"radius_km 缺省 500。disabled_sources 列出未开启因此未查询的层,可提示用户"
        u8"用 set_layer 开启后重查;weather.pending=true 时可稍后重调。"
        u8"拿到结果后请合成结构化简报,并善用 show_chart 画分布、fly_to 逐点导览。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"lat\":{\"type\":\"number\"},\"lon\":{\"type\":\"number\"},"
        "\"radius_km\":{\"type\":\"number\",\"description\":\"default 500\"}},"
        "\"required\":[\"lat\",\"lon\"]}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        double lat = 0, lon = 0, radius = 500.0, rv = 0;
        if (!getNum(a, "lat", lat) || !getNum(a, "lon", lon))
            return argError("need number lat/lon");
        if (getNum(a, "radius_km", rv) && rv > 0 && rv <= 5000) radius = rv;
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180)
            return argError("out of range: lat[-90,90] lon[-180,180]");
        picojson::object r;
        r["lat"] = picojson::value(lat); r["lon"] = picojson::value(lon);
        r["radius_km"] = picojson::value(radius);
        picojson::array srcs, disabled;
        std::vector<earthfeed::RegionBriefProvider>& ps = earthfeed::regionBriefProviders();
        for (size_t i = 0; i < ps.size(); ++i)
        {
            if (!ps[i].isEnabled())
            { disabled.push_back(picojson::value(ps[i].id)); continue; }
            picojson::value v; std::string perr =
                picojson::parse(v, ps[i].regionSummaryJson(lat, lon, radius));
            if (!perr.empty() || !v.is<picojson::object>()) continue;
            v.get<picojson::object>()["id"] = picojson::value(ps[i].id);
            srcs.push_back(v);
        }
        r["sources"] = picojson::value(srcs);
        r["disabled_sources"] = picojson::value(disabled);
        char wurl[256];
        snprintf(wurl, sizeof(wurl),
            "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
            "&current=temperature_2m,wind_speed_10m,precipitation,weather_code&timezone=auto",
            lat, lon);
        r["weather"] = toolFetchJson(*f, wurl, 900.0, "EARTH_WEATHER_FILE");
        OSG_NOTICE << "[AIChat] get_region_brief " << lat << "," << lon << " r=" << radius
                   << " sources=" << srcs.size() << std::endl;
        return picojson::value(r);
    };
    tools->add(t);
}

// ---- get_news_content(抓新闻文章正文供总结;直调 fetcher.query 拿 HTML 原始 body,不走 toolFetchJson) ----
static void registerNewsContentTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_news_content";
    t.description = u8"给定一条新闻文章的 URL,抓取其网页正文文本(已去除 HTML 标签)供总结与分析。"
        u8"典型用途:拿到新闻热点(get_gdelt_summary 返回的 topHotspots[].url)的 URL 后,用本工具读取正文。"
        u8"若返回 pending=true 表示正在抓取,请稍候片刻后用相同 url 再次调用本工具。"
        u8"付费墙或需 JS 渲染的页面可能只能取到部分正文(best-effort)。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"url\":{\"type\":\"string\"}},\"required\":[\"url\"]}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        std::string url;
        if (!getStr(a, "url", url)) return argError("need string url");
        bool okProto = url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
        if (!okProto) return argError("url must start with http:// or https://");
        if (url.size() > 2048) return argError("url too long (max 2048)");
        const char* fx = getenv("EARTH_NEWS_FILE");
        std::string body, err;
        earthai::QueryState st = f->query(url, 1800.0,
            (fx && *fx) ? std::string(fx) : std::string(), body, err);
        picojson::object r;
        if (st == earthai::QueryState::Fetching)
        {
            r["pending"] = picojson::value(true);
            r["note"] = picojson::value(std::string(u8"正文抓取中,请稍等片刻后用相同 url 再次调用本工具"));
            return picojson::value(r);
        }
        if (st == earthai::QueryState::Failed)
        { r["error"] = picojson::value("fetch failed: " + err); return picojson::value(r); }
        // libhv 默认不解压 gzip(gpsjam_feed.cpp 踩过):魔数 1f 8b → 直接报错,不尝试解压
        if (body.size() >= 2 && (unsigned char)body[0] == 0x1f && (unsigned char)body[1] == 0x8b)
        { r["error"] = picojson::value(std::string(u8"内容为压缩格式(gzip),无法解析")); return picojson::value(r); }
        OSG_NOTICE << "[AIChat] get_news_content " << url << " (" << body.size() << "B)" << std::endl;
        r["url"] = picojson::value(url);
        r["content"] = picojson::value(stripHtmlToText(body, 4000));
        return picojson::value(r);
    };
    tools->add(t);
}

void registerWorldQueryTools(earthai::ToolRegistry* tools, LayerManager* layers,
                             earthai::AsyncJsonFetcher* fetcher)
{
    (void)layers;   // Task 6(get_region_brief)开始使用
    if (!tools || !fetcher) return;
    registerWeatherTool(tools, fetcher);
    registerCryptoTool(tools, fetcher);
    registerWorldBankTool(tools, fetcher);
    registerChokepointTool(tools, fetcher);
    registerPredictionTool(tools, fetcher);
    registerRegionBriefTool(tools, fetcher);
    registerNewsContentTool(tools, fetcher);
}
