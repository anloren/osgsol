// P4 世界情报工具族离线单测:AsyncJsonFetcher 状态机 + 工具 dispatch(fixture 驱动,零网络)。
// 沿用 feed_layer_tests.cpp 先例:被测 .cpp 直接 #include 进本翻译单元。
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include "../applications/earth_explorer/ai_query.cpp"
// feed_layer.cpp 依赖全家桶(照抄 feed_layer_tests.cpp 现行清单;缺一个就链接失败)
#include "../applications/earth_explorer/feed_layer.cpp"
#include "../applications/earth_explorer/feeds/gdacs_feed.cpp"
#include "../applications/earth_explorer/feeds/usgs_quakes_feed.cpp"
#include "../applications/earth_explorer/feeds/eonet_feed.cpp"
#include "../applications/earth_explorer/feeds/gdelt_feed.cpp"
#include "../applications/earth_explorer/feeds/gpsjam_feed.cpp"
#include "../applications/earth_explorer/feeds/unhcr_feed.cpp"
#include "../applications/earth_explorer/feeds/nhc_feed.cpp"
#include "../applications/earth_explorer/feeds/strategic_feed.cpp"
#include "../applications/earth_explorer/feeds/firms_feed.cpp"
#include "../applications/earth_explorer/earth_config.cpp"   // firms_feed.cpp 用 earthcfg::resolveKey
#include "../applications/earth_explorer/geo_primitives.cpp"
#include "../applications/earth_explorer/ui_card.h"
#include "../applications/earth_explorer/marker_style.cpp"
#include "../applications/earth_explorer/ai_world_tools.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)

#ifdef _WIN32
static void setEnvVar(const char* k, const char* v) { _putenv_s(k, v); }
static void unsetEnvVar(const char* k) { _putenv_s(k, ""); }
#else
static void setEnvVar(const char* k, const char* v) { setenv(k, v, 1); }
static void unsetEnvVar(const char* k) { unsetenv(k); }
#endif

// ===== Task 1:AsyncJsonFetcher 状态机 =====
static void testFetcherFixtureSync()
{
    // fixture 路径非空 → 同步读文件立即 Ready,不走网络
    const char* kPath = "aiq_fixture_tmp.json";
    { std::ofstream f(kPath); f << "{\"x\":1}"; }
    earthai::AsyncJsonFetcher fx;   // 缺省 fetch 不会被调用
    std::string body, err;
    CHECK(fx.query("http://ignored", 60.0, kPath, body, err) == earthai::QueryState::Ready);
    CHECK(body == "{\"x\":1}");
    // fixture 文件缺失 → Failed(err 说明路径)
    CHECK(fx.query("http://ignored", 60.0, "no_such_file.json", body, err)
          == earthai::QueryState::Failed);
    CHECK(err.find("no_such_file") != std::string::npos);
    std::remove(kPath);
    std::cout << "[OK] fetcher fixture sync\n";
}

static void testFetcherAsyncFlow()
{
    // 注入 fake fetch:首调 Fetching(入队),worker 抓完后再调命中 Ready
    earthai::AsyncJsonFetcher fx([](const std::string& url, std::string& b, std::string& e) {
        if (url.find("bad") != std::string::npos) { e = "boom"; return false; }
        b = "{\"ok\":true}"; return true;
    });
    std::string body, err;
    CHECK(fx.query("http://good", 3600.0, "", body, err) == earthai::QueryState::Fetching);
    fx.waitIdleForTest();
    CHECK(fx.query("http://good", 3600.0, "", body, err) == earthai::QueryState::Ready);
    CHECK(body == "{\"ok\":true}");
    // TTL 内重复调用不再入队(仍 Ready、body 相同)
    CHECK(fx.query("http://good", 3600.0, "", body, err) == earthai::QueryState::Ready);
    // 失败源:Fetching → Failed(消费一次即弃) → 下次重试回 Fetching
    CHECK(fx.query("http://bad", 3600.0, "", body, err) == earthai::QueryState::Fetching);
    fx.waitIdleForTest();
    CHECK(fx.query("http://bad", 3600.0, "", body, err) == earthai::QueryState::Failed);
    CHECK(err.find("boom") != std::string::npos);
    CHECK(fx.query("http://bad", 3600.0, "", body, err) == earthai::QueryState::Fetching);
    fx.waitIdleForTest();
    // TTL=0 → 命中即过期,重新入队
    CHECK(fx.query("http://good", 0.0, "", body, err) == earthai::QueryState::Fetching);
    fx.waitIdleForTest();
    std::cout << "[OK] fetcher async flow\n";
}

// ===== Task 2:get_weather_forecast(fixture 驱动) =====
static void testWeatherTool()
{
    const char* kFix = "aiq_weather_tmp.json";
    { std::ofstream f(kFix);
      f << "{\"current\":{\"temperature_2m\":25.3,\"wind_speed_10m\":3.2},"
           "\"daily\":{\"temperature_2m_max\":[30,31,29,28,27]}}"; }
    setEnvVar("EARTH_WEATHER_FILE", kFix);
    earthai::AsyncJsonFetcher fx;
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{\"lat\":25.0,\"lon\":102.7}").empty());
    CHECK(reg.dispatch("get_weather_forecast", args, out));
    CHECK(out.is<picojson::object>());
    CHECK(out.get("current").get("temperature_2m").get<double>() == 25.3);
    // 参数缺失 → error
    picojson::value bad; CHECK(picojson::parse(bad, "{}").empty());
    reg.dispatch("get_weather_forecast", bad, out);
    CHECK(out.contains("error"));
    unsetEnvVar("EARTH_WEATHER_FILE");
    std::remove(kFix);
    std::cout << "[OK] weather tool\n";
}

// ===== Task 3:行情 + 宏观指标(fixture 驱动) =====
static void testCryptoAndWorldBankTools()
{
    earthai::AsyncJsonFetcher fx;
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    // CoinGecko simple/price 形状:{"bitcoin":{"usd":97000,"usd_24h_change":-1.2}}
    const char* kC = "aiq_crypto_tmp.json";
    { std::ofstream f(kC); f << "{\"bitcoin\":{\"usd\":97000.0,\"usd_24h_change\":-1.2}}"; }
    setEnvVar("EARTH_CRYPTO_FILE", kC);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{}").empty());   // ids 缺省 bitcoin,ethereum
    CHECK(reg.dispatch("get_crypto_prices", args, out));
    CHECK(out.get("bitcoin").get("usd").get<double>() == 97000.0);
    unsetEnvVar("EARTH_CRYPTO_FILE"); std::remove(kC);
    // World Bank 顶层是数组 [meta, rows] → 工具需转成 {country,indicator,points:[{date,value}]}
    const char* kW = "aiq_wb_tmp.json";
    { std::ofstream f(kW);
      f << "[{\"page\":1},[{\"date\":\"2024\",\"value\":18.53},{\"date\":\"2023\",\"value\":17.79}]]"; }
    setEnvVar("EARTH_WB_FILE", kW);
    CHECK(picojson::parse(args, "{\"country\":\"CN\",\"indicator\":\"NY.GDP.MKTP.CD\"}").empty());
    CHECK(reg.dispatch("get_country_indicator", args, out));
    CHECK(out.get("country").get<std::string>() == "CN");
    CHECK(out.get("points").get<picojson::array>().size() == 2);
    CHECK(out.get("points").get<picojson::array>()[0].get("date").get<std::string>() == "2024");
    // 缺参 → error
    picojson::value bad; CHECK(picojson::parse(bad, "{\"country\":\"CN\"}").empty());
    reg.dispatch("get_country_indicator", bad, out);
    CHECK(out.contains("error"));
    unsetEnvVar("EARTH_WB_FILE"); std::remove(kW);
    std::cout << "[OK] crypto + worldbank tools\n";
}

// ===== Task 4:PortWatch 咽喉点 + Polymarket(fixture 驱动) =====
static void testChokepointAndPredictionTools()
{
    earthai::AsyncJsonFetcher fx;
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    // ArcGIS 形状:{"features":[{"attributes":{...}}]};工具取每个咽喉点最新一条
    const char* kP = "aiq_portwatch_tmp.json";
    { std::ofstream f(kP);
      f << "{\"features\":["
           "{\"attributes\":{\"portname\":\"Suez Canal\",\"date\":1751500800000,\"n_total\":52}},"
           "{\"attributes\":{\"portname\":\"Strait of Hormuz\",\"date\":1751500800000,\"n_total\":88}},"
           "{\"attributes\":{\"portname\":\"Suez Canal\",\"date\":1751414400000,\"n_total\":49}}]}"; }
    setEnvVar("EARTH_PORTWATCH_FILE", kP);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{}").empty());
    CHECK(reg.dispatch("get_chokepoint_traffic", args, out));
    const picojson::array& cps = out.get("chokepoints").get<picojson::array>();
    CHECK(cps.size() == 2);   // 每咽喉点只留最新一条
    unsetEnvVar("EARTH_PORTWATCH_FILE"); std::remove(kP);
    // Polymarket 形状:顶层数组;outcomes/outcomePrices 是「字符串化的 JSON 数组」需二次解析
    const char* kM = "aiq_poly_tmp.json";
    { std::ofstream f(kM);
      f << "[{\"question\":\"X happens in 2026?\",\"volumeNum\":123456.7,"
           "\"outcomes\":\"[\\\"Yes\\\",\\\"No\\\"]\","
           "\"outcomePrices\":\"[\\\"0.34\\\",\\\"0.66\\\"]\"}]"; }
    setEnvVar("EARTH_POLYMARKET_FILE", kM);
    CHECK(reg.dispatch("get_prediction_markets", args, out));
    const picojson::array& ms = out.get("markets").get<picojson::array>();
    CHECK(ms.size() == 1);
    CHECK(ms[0].get("question").get<std::string>() == "X happens in 2026?");
    CHECK(ms[0].get("outcomes").get<picojson::array>()[0].get("prob").get<double>() == 0.34);
    unsetEnvVar("EARTH_POLYMARKET_FILE"); std::remove(kM);
    std::cout << "[OK] chokepoint + prediction tools\n";
}

// ===== Task 6:get_region_brief 跨源合成 =====
static void testRegionBriefTool()
{
    // 直接向注册表塞两个假 provider:一个启用一个禁用
    earthfeed::regionBriefProviders().clear();
    earthfeed::RegionBriefProvider on; on.id = "fakeon";
    on.isEnabled = []() { return true; };
    on.regionSummaryJson = [](double, double, double)
    { return std::string("{\"count\":2,\"nearest\":[{\"title\":\"A\"}]}"); };
    earthfeed::regionBriefProviders().push_back(on);
    earthfeed::RegionBriefProvider off; off.id = "fakeoff";
    off.isEnabled = []() { return false; };
    off.regionSummaryJson = [](double, double, double) { return std::string("{}"); };
    earthfeed::regionBriefProviders().push_back(off);

    const char* kW = "aiq_briefwx_tmp.json";
    { std::ofstream f(kW); f << "{\"current\":{\"temperature_2m\":18.0}}"; }
    setEnvVar("EARTH_WEATHER_FILE", kW);
    earthai::AsyncJsonFetcher fx;
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{\"lat\":10.0,\"lon\":20.0,\"radius_km\":300}").empty());
    CHECK(reg.dispatch("get_region_brief", args, out));
    CHECK(out.get("radius_km").get<double>() == 300.0);
    const picojson::array& srcs = out.get("sources").get<picojson::array>();
    CHECK(srcs.size() == 1);
    CHECK(srcs[0].get("id").get<std::string>() == "fakeon");
    CHECK(srcs[0].get("count").get<double>() == 2.0);
    const picojson::array& dis = out.get("disabled_sources").get<picojson::array>();
    CHECK(dis.size() == 1 && dis[0].get<std::string>() == "fakeoff");
    CHECK(out.get("weather").get("current").get("temperature_2m").get<double>() == 18.0);
    unsetEnvVar("EARTH_WEATHER_FILE"); std::remove(kW);
    earthfeed::regionBriefProviders().clear();
    std::cout << "[OK] region brief tool\n";
}

// ===== 新闻正文抓取工具(注入式 fetch 驱动,零网络) =====
static void testNewsContentTool()
{
    // 注入 fake fetch:返回一段含 script/style/实体/多空白的 HTML;url 含 "bad" → 失败;
    // url 含 "long" → 返回 5000 字纯文本(测截断)。
    earthai::AsyncJsonFetcher fx([](const std::string& url, std::string& b, std::string& e) {
        if (url.find("bad") != std::string::npos) { e = "boom"; return false; }
        if (url.find("long") != std::string::npos) { b = std::string(5000, 'A'); return true; }
        if (url.find("utf8keep") != std::string::npos) {
            // 3997 'A' + 中(占字节 3997..3999)+ 填充 → 切点 out[4000]='A' 非续字节 → 中 完整保留
            b = std::string(3997, 'A') + "\xE4\xB8\xAD" + std::string(200, 'A'); return true;
        }
        if (url.find("utf8split") != std::string::npos) {
            // 3999 'A' + 中(占字节 3999..4001)+ 填充 → 切点 out[4000] 落在 中 中间 → 干净剔除半个字符
            b = std::string(3999, 'A') + "\xE4\xB8\xAD" + std::string(200, 'A'); return true;
        }
        b = "<html><head><style>.x{color:red}</style></head>"
            "<body><script>var a=1;</script><h1>Big News</h1>"
            "<p>Hello &amp; welcome to &lt;Earth&gt;.</p>"
            "<div>  Multiple    spaces   here </div></body></html>";
        return true;
    });
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{\"url\":\"https://news.example.com/a\"}").empty());
    // 首调:抓取中 → pending(三态透传)
    CHECK(reg.dispatch("get_news_content", args, out));
    CHECK(out.get("pending").get<bool>() == true);
    fx.waitIdleForTest();
    // 再调:Ready → content 已剥正文
    CHECK(reg.dispatch("get_news_content", args, out));
    CHECK(out.contains("content"));
    std::string c = out.get("content").get<std::string>();
    CHECK(c.find("Big News") != std::string::npos);                       // 正文保留
    CHECK(c.find("Hello & welcome to <Earth>.") != std::string::npos);    // 实体解码 &amp;&lt;&gt;
    CHECK(c.find("Multiple spaces here") != std::string::npos);           // 连续空白折叠
    CHECK(c.find("var a=1") == std::string::npos);                        // <script> 块整删
    CHECK(c.find("color:red") == std::string::npos);                      // <style> 块整删
    CHECK(c.find("<h1>") == std::string::npos);                           // 标签删除
    CHECK(out.get("url").get<std::string>() == "https://news.example.com/a");
    // 截断:5000 字 → ~4000 字 + 省略号
    picojson::value la, lo; CHECK(picojson::parse(la, "{\"url\":\"https://long.example.com\"}").empty());
    CHECK(reg.dispatch("get_news_content", la, lo)); fx.waitIdleForTest();
    CHECK(reg.dispatch("get_news_content", la, lo));
    std::string lc = lo.get("content").get<std::string>();
    CHECK(lc.size() <= 4100);                                             // 4000 + "…"(UTF-8 3字节)余量
    CHECK(lc.find("\xE2\x80\xA6") != std::string::npos);                  // 末尾 …
    // UTF-8 边界:完整多字节字符落在切点边界上 → 不得被误删
    picojson::value ka, ko; CHECK(picojson::parse(ka, "{\"url\":\"https://utf8keep.example.com\"}").empty());
    CHECK(reg.dispatch("get_news_content", ka, ko)); fx.waitIdleForTest();
    CHECK(reg.dispatch("get_news_content", ka, ko));
    std::string kc = ko.get("content").get<std::string>();
    CHECK(kc.size() == 4000 + 3);                                   // 4000字节正文(含完整"中")+ "…"(3字节)
    CHECK(kc.substr(kc.size() - 6) == "\xE4\xB8\xAD\xE2\x80\xA6");  // 结尾是完整"中…",未被误删
    // UTF-8 边界:多字节字符被切点劈开 → 干净剔除半个字符,无残留碎片字节
    picojson::value sa, so; CHECK(picojson::parse(sa, "{\"url\":\"https://utf8split.example.com\"}").empty());
    CHECK(reg.dispatch("get_news_content", sa, so)); fx.waitIdleForTest();
    CHECK(reg.dispatch("get_news_content", sa, so));
    std::string sc = so.get("content").get<std::string>();
    CHECK(sc.find('\xE4') == std::string::npos);                    // 半个"中"的首字节被剔除,无多字节碎片残留
    CHECK(sc.substr(sc.size() - 4) == "A\xE2\x80\xA6");             // 结尾是完整 ASCII 'A' + "…"
    // 超长 url(>2048)→ error
    picojson::value ua, uo;
    std::string longUrl = "https://x.example.com/" + std::string(2100, 'a');
    CHECK(picojson::parse(ua, ("{\"url\":\"" + longUrl + "\"}").c_str()).empty());
    reg.dispatch("get_news_content", ua, uo);
    CHECK(uo.contains("error"));
    // 非 http(s) url → error
    picojson::value bad, bout; CHECK(picojson::parse(bad, "{\"url\":\"ftp://x/y\"}").empty());
    reg.dispatch("get_news_content", bad, bout);
    CHECK(bout.contains("error"));
    // 缺 url 参数 → error
    picojson::value none, nout; CHECK(picojson::parse(none, "{}").empty());
    reg.dispatch("get_news_content", none, nout);
    CHECK(nout.contains("error"));
    // 抓取失败源 → error
    picojson::value ba, bo; CHECK(picojson::parse(ba, "{\"url\":\"https://bad.example.com\"}").empty());
    CHECK(reg.dispatch("get_news_content", ba, bo)); fx.waitIdleForTest();
    CHECK(reg.dispatch("get_news_content", ba, bo));
    CHECK(bo.contains("error"));
    std::cout << "[OK] news content tool\n";
}

int main(int, char**)
{
    testFetcherFixtureSync();
    testFetcherAsyncFlow();
    testWeatherTool();
    testCryptoAndWorldBankTools();
    testChokepointAndPredictionTools();
    testRegionBriefTool();
    testNewsContentTool();
    std::cout << "ALL WORLD-TOOLS TESTS PASSED\n";
    return 0;
}
