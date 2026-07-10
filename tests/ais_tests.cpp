// tests/ais_tests.cpp — P3 AIS 船舶层纯函数单测(消息解析 / bbox 决策 / 订阅 JSON)。
// 沿用 NEW_TEST 单文件先例:被测实现直接 #include 进本翻译单元。
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>
#include <picojson.h>
#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)
#include "../applications/earth_explorer/ais_math.cpp"
#include "../applications/earth_explorer/earth_config.cpp"

static void testParseKeysEnv()
{
    using earthcfg::parseKeysEnv;
    std::string c = "# EarthExplorer keys\nEARTH_FIRMS_KEY=abc123\n\n"
                    "EARTH_AISSTREAM_KEY = def456 \n# trailing comment\n";
    CHECK(parseKeysEnv(c, "EARTH_FIRMS_KEY") == "abc123");
    CHECK(parseKeysEnv(c, "EARTH_AISSTREAM_KEY") == "def456");   // 裁剪 name/value 首尾空格
    CHECK(parseKeysEnv(c, "MISSING").empty());                  // 不存在 → 空串
    CHECK(parseKeysEnv("", "X").empty());                       // 空文本
    CHECK(parseKeysEnv("# only comment\n", "X").empty());       // 只有注释
    CHECK(parseKeysEnv("EARTH_X=a=b=c\n", "EARTH_X") == "a=b=c"); // value 内含 '='(按首个 '=' 切)
    CHECK(parseKeysEnv("EARTH_X=v\r\n", "EARTH_X") == "v");     // CRLF 行尾裁掉 \r
    CHECK(parseKeysEnv("noequalsign\n", "noequalsign").empty()); // 无 '=' 行跳过
    std::cout << "[OK] parseKeysEnv\n";
}

static std::string readAll(const char* p)
{ std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }

static void testParseAisMessage()
{
    using namespace earthais;
    const char* fx = "/Users/USER/osgverse/applications/earth_explorer/test/ais_fixture.jsonl";
    std::istringstream in(readAll(fx));
    std::string line; int nValid = 0; AisShip first; AisShip last;
    while (std::getline(in, line))
    {
        AisShip s = parseAisMessage(line);
        if (s.valid) { if (nValid == 0) first = s; last = s; nValid++; }
    }
    CHECK(nValid == 6);                                   // 其它类型/畸形行都被拒
    CHECK(first.mmsi == 477123456LL);
    CHECK(std::fabs(first.lat - 22.28) < 1e-9);
    CHECK(std::fabs(first.lon - 114.16) < 1e-9);
    CHECK(std::fabs(first.sogKn - 12.3) < 1e-9);
    CHECK(std::fabs(first.cogDeg - 45.5) < 1e-9);
    CHECK(first.name == "EVER GLORY");
    // Pin the right-trim contract: fixture has trailing spaces, parser trims them.
    CHECK(last.mmsi == 477678901LL);
    CHECK(last.name == "MAERSK HANOI");
    std::cout << "[OK] parseAisMessage\n";
}
static void testBBoxLogic()
{
    using namespace earthais;
    ShipBBox v; v.latMin = 20; v.latMax = 24; v.lonMin = 112; v.lonMax = 116;
    ShipBBox sub = inflateBBox(v, 1.5);
    CHECK(sub.latMin < v.latMin && sub.latMax > v.latMax);
    CHECK(sub.latMax - sub.latMin > (v.latMax - v.latMin) * 1.49);
    // 钳制:全球视口外扩不越界
    ShipBBox g; g.latMin = -85; g.latMax = 85; g.lonMin = -180; g.lonMax = 180;
    ShipBBox gi = inflateBBox(g, 1.5);
    CHECK(gi.latMin >= -85.0 && gi.latMax <= 85.0 && gi.lonMin >= -180.0 && gi.lonMax <= 180.0);
    // 视口在订阅范围内 → 不重连
    CHECK(!bboxNeedsResubscribe(sub, v));
    // 视口中心移出订阅范围 → 重连
    ShipBBox moved = v; moved.lonMin = 130; moved.lonMax = 134;
    CHECK(bboxNeedsResubscribe(sub, moved));
    // 拉远超过订阅范围 → 重连
    ShipBBox wide = v; wide.latMin = 0; wide.latMax = 44; wide.lonMin = 90; wide.lonMax = 140;
    CHECK(bboxNeedsResubscribe(sub, wide));
    // 推近太多(< 订阅跨度 1/4)→ 重连收窄,省流量
    ShipBBox tiny = v; tiny.latMin = 22.0; tiny.latMax = 22.4; tiny.lonMin = 114.0; tiny.lonMax = 114.4;
    CHECK(bboxNeedsResubscribe(sub, tiny));
    std::cout << "[OK] bbox logic\n";
}
static void testSubscriptionJson()
{
    using namespace earthais;
    ShipBBox b; b.latMin = 20; b.lonMin = 112; b.latMax = 24; b.lonMax = 116;
    std::string j = buildSubscriptionJson("test-key", b);
    picojson::value v; std::string err = picojson::parse(v, j);
    CHECK(err.empty());
    CHECK(v.get("APIKey").get<std::string>() == "test-key");
    const picojson::array& boxes = v.get("BoundingBoxes").get<picojson::array>();
    CHECK(boxes.size() == 1);
    const picojson::array& box = boxes[0].get<picojson::array>();
    CHECK(box.size() == 2);   // [SW, NE] 两角
    const picojson::array& sw = box[0].get<picojson::array>();
    CHECK(std::fabs(sw[0].get<double>() - 20.0) < 1e-9);   // [lat, lon] 序
    CHECK(std::fabs(sw[1].get<double>() - 112.0) < 1e-9);
    const picojson::array& types = v.get("FilterMessageTypes").get<picojson::array>();
    CHECK(types.size() == 1 && types[0].get<std::string>() == "PositionReport");
    std::cout << "[OK] subscription json\n";
}
int main()
{
    testParseAisMessage();
    testBBoxLogic();
    testSubscriptionJson();
    testParseKeysEnv();
    std::cout << "ALL AIS TESTS PASSED\n";
    return 0;
}
