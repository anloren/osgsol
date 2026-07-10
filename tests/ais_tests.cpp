// tests/ais_tests.cpp — P3 AIS 船舶层纯函数单测(消息解析 / bbox 决策 / 订阅 JSON)。
// 沿用 NEW_TEST 单文件先例:被测实现直接 #include 进本翻译单元。
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <vector>
#include <picojson.h>
#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)
#include "../applications/earth_explorer/ais_math.cpp"
#include "../applications/earth_explorer/earth_config.cpp"

namespace
{
const double kEpsilon = 1e-9;

bool near(double actual, double expected)
{ return std::fabs(actual - expected) <= kEpsilon; }

earthais::ShipBBox bbox(double latMin, double lonMin, double latMax, double lonMax)
{
    earthais::ShipBBox value;
    value.latMin = latMin; value.lonMin = lonMin;
    value.latMax = latMax; value.lonMax = lonMax;
    return value;
}

std::string readSourceFile(const std::string& relative)
{
    std::ifstream input(std::string(OSGVERSE_SOURCE_DIR) + "/" + relative);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::string extractFunctionBody(const std::string& source,
                                const std::string& signature)
{
    size_t signaturePos = source.find(signature);
    if (signaturePos == std::string::npos) return std::string();
    size_t openingBrace = source.find('{', signaturePos + signature.size());
    if (openingBrace == std::string::npos) return std::string();

    int depth = 1;
    bool lineComment = false, blockComment = false;
    bool stringLiteral = false, charLiteral = false, escaped = false;
    for (size_t i = openingBrace + 1; i < source.size(); ++i)
    {
        char c = source[i];
        char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
        if (lineComment)
        {
            if (c == '\n') lineComment = false;
            continue;
        }
        if (blockComment)
        {
            if (c == '*' && next == '/') { blockComment = false; ++i; }
            continue;
        }
        if (stringLiteral || charLiteral)
        {
            if (escaped) { escaped = false; continue; }
            if (c == '\\') { escaped = true; continue; }
            if ((stringLiteral && c == '"') || (charLiteral && c == '\''))
            { stringLiteral = false; charLiteral = false; }
            continue;
        }
        if (c == '/' && next == '/') { lineComment = true; ++i; continue; }
        if (c == '/' && next == '*') { blockComment = true; ++i; continue; }
        if (c == '"') { stringLiteral = true; continue; }
        if (c == '\'') { charLiteral = true; continue; }
        if (c == '{') ++depth;
        else if (c == '}' && --depth == 0)
            return source.substr(openingBrace + 1, i - openingBrace - 1);
    }
    return std::string();
}

std::string normalizeCodeOnly(const std::string& source)
{
    std::string compact;
    compact.reserve(source.size());
    bool lineComment = false, blockComment = false;
    bool stringLiteral = false, charLiteral = false, escaped = false;
    for (size_t i = 0; i < source.size(); ++i)
    {
        char c = source[i];
        char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
        if (lineComment)
        {
            if (c == '\n') lineComment = false;
            continue;
        }
        if (blockComment)
        {
            if (c == '*' && next == '/') { blockComment = false; ++i; }
            continue;
        }
        if (stringLiteral || charLiteral)
        {
            if (escaped) { escaped = false; continue; }
            if (c == '\\') { escaped = true; continue; }
            if ((stringLiteral && c == '"') || (charLiteral && c == '\''))
            {
                compact.push_back(c);
                stringLiteral = false; charLiteral = false;
            }
            continue;
        }
        if (c == '/' && next == '/') { lineComment = true; ++i; continue; }
        if (c == '/' && next == '*') { blockComment = true; ++i; continue; }
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        compact.push_back(c);
        if (c == '"') stringLiteral = true;
        else if (c == '\'') charLiteral = true;
    }
    return compact;
}

size_t countOccurrences(const std::string& source, const std::string& needle)
{
    size_t count = 0, offset = 0;
    while ((offset = source.find(needle, offset)) != std::string::npos)
    {
        ++count;
        offset += needle.size();
    }
    return count;
}
}

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
    const std::string fixturePath = std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/test/ais_fixture.jsonl";
    const std::string fixture = readAll(fixturePath.c_str());
    CHECK(!fixture.empty());
    std::istringstream in(fixture);
    std::string line; int nValid = 0; AisShip first; AisShip last;
    std::string firstValidMessage;
    std::map<long long, AisShip> store;
    while (std::getline(in, line))
    {
        AisShip s = parseAisMessage(line);
        if (s.valid)
        {
            if (nValid == 0) { first = s; firstValidMessage = line; }
            last = s; nValid++;
            store[s.mmsi] = s;
        }
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
    // Replaying a duplicate fixture message still represents one MMSI entry.
    AisShip duplicate = parseAisMessage(firstValidMessage);
    CHECK(duplicate.valid && duplicate.mmsi == first.mmsi);
    store[duplicate.mmsi] = duplicate;
    CHECK(store.size() == 6);
    std::cout << "[OK] parseAisMessage\n";
}
static void testBBoxLogic()
{
    using namespace earthais;
    ShipBBox v = bbox(20.0, 112.0, 24.0, 116.0);
    ShipBBox sub = inflateBBox(v, 1.5);
    CHECK(sub.latMin < v.latMin && sub.latMax > v.latMax);
    CHECK(sub.latMax - sub.latMin > (v.latMax - v.latMin) * 1.49);
    // Latitude remains clamped, while longitude stays as an unwrapped interval.
    ShipBBox g = bbox(-85.0, -180.0, 85.0, 180.0);
    ShipBBox gi = inflateBBox(g, 1.5);
    CHECK(gi.latMin >= -85.0 && gi.latMax <= 85.0);
    CHECK(near(gi.lonMin, -270.0) && near(gi.lonMax, 270.0));
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

    ShipBBox dateLineView = bbox(10.0, 176.0, 14.0, 182.0);
    ShipBBox dateLineSub = inflateBBox(dateLineView, 1.5);
    CHECK(near(dateLineSub.lonMin, 174.5));
    CHECK(near(dateLineSub.lonMax, 183.5));
    ShipBBox equivalentWrappedView = bbox(10.0, -182.0, 14.0, -176.0);
    CHECK(!bboxNeedsResubscribe(dateLineSub, equivalentWrappedView));
    CHECK(bboxNeedsResubscribe(dateLineSub, bbox(10.0, -154.0, 14.0, -148.0)));
    CHECK(bboxNeedsResubscribe(dateLineSub, bbox(0.0, -190.0, 24.0, -168.0)));
    CHECK(bboxNeedsResubscribe(dateLineSub, bbox(11.8, -179.2, 12.2, -178.8)));
    std::cout << "[OK] bbox logic\n";
}
static void testSubscriptionJson()
{
    using namespace earthais;
    ShipBBox b = bbox(20.0, 112.0, 24.0, 116.0);
    std::vector<ShipBBox> normal = splitSubscriptionBoxes(b);
    CHECK(normal.size() == 1);
    CHECK(near(normal[0].lonMin, 112.0) && near(normal[0].lonMax, 116.0));

    std::string j = buildSubscriptionJson("test-key", normal);
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

    std::vector<ShipBBox> split = splitSubscriptionBoxes(
        bbox(20.0, 174.0, 24.0, 184.0));
    CHECK(split.size() == 2);
    CHECK(near(split[0].lonMin, 174.0) && near(split[0].lonMax, 180.0));
    CHECK(near(split[1].lonMin, -180.0) && near(split[1].lonMax, -176.0));
    CHECK(split[0].lonMin < split[0].lonMax);
    CHECK(split[1].lonMin < split[1].lonMax);

    picojson::value splitValue;
    err = picojson::parse(splitValue, buildSubscriptionJson("date-key", split));
    CHECK(err.empty());
    const picojson::array& splitJson =
        splitValue.get("BoundingBoxes").get<picojson::array>();
    CHECK(splitJson.size() == 2);
    const picojson::array& firstBox = splitJson[0].get<picojson::array>();
    const picojson::array& secondBox = splitJson[1].get<picojson::array>();
    const picojson::array& firstSw = firstBox[0].get<picojson::array>();
    const picojson::array& firstNe = firstBox[1].get<picojson::array>();
    const picojson::array& secondSw = secondBox[0].get<picojson::array>();
    const picojson::array& secondNe = secondBox[1].get<picojson::array>();
    CHECK(near(firstSw[0].get<double>(), 20.0));
    CHECK(near(firstSw[1].get<double>(), 174.0));
    CHECK(near(firstNe[0].get<double>(), 24.0));
    CHECK(near(firstNe[1].get<double>(), 180.0));
    CHECK(near(secondSw[0].get<double>(), 20.0));
    CHECK(near(secondSw[1].get<double>(), -180.0));
    CHECK(near(secondNe[0].get<double>(), 24.0));
    CHECK(near(secondNe[1].get<double>(), -176.0));
    std::cout << "[OK] subscription json\n";
}

static void testAisRuntimeSourceWiring()
{
    const std::string normalizedSnippet = normalizeCodeOnly(
        "liveCall(); // splitSubscriptionBoxes(_subscribedBBox); _store[s.mmsi] = s;\n"
        "const char* fake = \"buildSubscriptionJson(_apiKey, boxes)\"; nextCall();");
    CHECK(normalizedSnippet == "liveCall();constchar*fake=\"\";nextCall();");

    const std::string aisSource =
        readSourceFile("applications/earth_explorer/ais_data.cpp");
    const std::string mainSource =
        readSourceFile("applications/earth_explorer/earth_main.cpp");
    CHECK(!aisSource.empty());
    CHECK(!mainSource.empty());

    const std::string connectRaw = extractFunctionBody(aisSource, "bool connectWs(");
    const std::string messageRaw = extractFunctionBody(aisSource, "void onWsMessage(");
    const std::string fixtureRaw = extractFunctionBody(aisSource, "void loadFixture(");
    const std::string handlerRaw = extractFunctionBody(mainSource, "class ShipViewStateHandler");
    CHECK(!connectRaw.empty());
    CHECK(!messageRaw.empty());
    CHECK(!fixtureRaw.empty());
    CHECK(!handlerRaw.empty());

    const std::string connect = normalizeCodeOnly(connectRaw);
    const std::string message = normalizeCodeOnly(messageRaw);
    const std::string fixture = normalizeCodeOnly(fixtureRaw);
    const std::string handler = normalizeCodeOnly(handlerRaw);

    const size_t inflatePos = connect.find(
        "_subscribedBBox=earthais::inflateBBox(view,1.5);");
    const size_t splitPos = connect.find(
        "std::vector<earthais::ShipBBox>boxes="
        "earthais::splitSubscriptionBoxes(_subscribedBBox);");
    const size_t jsonPos = connect.find(
        "earthais::buildSubscriptionJson(_apiKey,boxes)");
    CHECK(inflatePos != std::string::npos);
    CHECK(splitPos != std::string::npos && inflatePos < splitPos);
    CHECK(jsonPos != std::string::npos && splitPos < jsonPos);
    CHECK(countOccurrences(connect, "earthais::splitSubscriptionBoxes(") == 1);
    CHECK(countOccurrences(connect, "earthais::buildSubscriptionJson(") == 1);

    CHECK(handler.find("lonMin<-180.0") == std::string::npos);
    CHECK(handler.find("lonMax>180.0") == std::string::npos);
    CHECK(countOccurrences(handler, "lonMin") == 2);
    CHECK(countOccurrences(handler, "lonMax") == 2);
    CHECK(handler.find("if(latMin<-85.0)latMin=-85.0;") != std::string::npos);
    CHECK(handler.find("if(latMax>85.0)latMax=85.0;") != std::string::npos);
    CHECK(handler.find("if(lonHalf>180.0)lonHalf=180.0;") != std::string::npos);
    CHECK(handler.find("_ships->setViewState(latMin,lonMin,latMax,lonMax,h);") !=
          std::string::npos);

    CHECK(message.find("_store[s.mmsi]=s;") != std::string::npos);
    CHECK(fixture.find("_store[s.mmsi]=s;") != std::string::npos);
    CHECK(countOccurrences(normalizeCodeOnly(aisSource),
        "std::map<longlong,earthais::AisShip>_store;") == 1);
    std::cout << "[OK] AIS runtime source wiring\n";
}
int main()
{
    testParseAisMessage();
    testBBoxLogic();
    testSubscriptionJson();
    testParseKeysEnv();
    testAisRuntimeSourceWiring();
    std::cout << "ALL AIS TESTS PASSED\n";
    return 0;
}
