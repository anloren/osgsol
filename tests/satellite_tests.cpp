// tests/satellite_tests.cpp — 卫星层单测。Task 1 只验证 vendored SGP4 库本身正确
// (直接调用 libsgp4:: API,不经封装),Task 2 会在本文件继续追加 sat_math.h 封装层的测试。
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iterator>
#include <string>
#include <picojson.h>
#include "3rdparty/sgp4/Tle.h"
#include "3rdparty/sgp4/SGP4.h"
#include "3rdparty/sgp4/Eci.h"
#include "../applications/earth_explorer/sat_math.h"
#include <modeling/Math.h>
#include "../applications/earth_explorer/sat_math.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)

static std::string readSourceFile(const std::string& relative)
{
    std::ifstream input(std::string(OSGVERSE_SOURCE_DIR) + "/" + relative);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

static std::string extractFunctionBody(const std::string& source,
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

static std::string removeWhitespace(const std::string& source)
{
    std::string compact;
    compact.reserve(source.size());
    for (size_t i = 0; i < source.size(); ++i)
        if (!std::isspace(static_cast<unsigned char>(source[i]))) compact.push_back(source[i]);
    return compact;
}

// 官方 SGP4 验证测试集(Vallado "Revisiting Spacetrack Report #3", CelesTrak SGP4-VER.TLE
// 附带的标准回归用例,卫星 #5)。参考向量用独立的 Python 官方 sgp4 包(Brandon Rhodes 维护,
// 业界公认参考实现)在 2026-07-05 生成,与本仓库 vendored libsgp4 数值核对完全一致
// (t=0/360 分钟,位置误差 < 1e-5 km,速度误差 < 1e-5 km/s)。
static const char* kTle5Line1 = "1 00005U 58002B   00179.78495062  .00000023  00000-0  28098-4 0  4753";
static const char* kTle5Line2 = "2 00005  34.2682 348.7242 1859667 331.7664  19.3264 10.82419157413667";

int main(int, char**)
{
    using namespace libsgp4;

    // ---- vendored 库本身的正确性(直接 API,独立于我们的封装)----
    {
        Tle tle(kTle5Line1, kTle5Line2);
        SGP4 sgp4(tle);

        Eci eci0 = sgp4.FindPosition(0.0);
        Vector p0 = eci0.Position(), v0 = eci0.Velocity();
        CHECK(std::fabs(p0.x - 7022.465293) < 1e-3);
        CHECK(std::fabs(p0.y - (-1400.082968)) < 1e-3);
        CHECK(std::fabs(p0.z - 0.039952) < 1e-3);
        CHECK(std::fabs(v0.x - 1.893841) < 1e-4);
        CHECK(std::fabs(v0.y - 6.405894) < 1e-4);
        CHECK(std::fabs(v0.z - 4.534807) < 1e-4);

        Eci eci360 = sgp4.FindPosition(360.0);
        Vector p360 = eci360.Position(), v360 = eci360.Velocity();
        CHECK(std::fabs(p360.x - (-7154.031202)) < 1e-3);
        CHECK(std::fabs(p360.y - (-3783.176825)) < 1e-3);
        CHECK(std::fabs(p360.z - (-3536.194123)) < 1e-3);
        CHECK(std::fabs(v360.x - 4.741887) < 1e-4);
        CHECK(std::fabs(v360.y - (-4.151818)) < 1e-4);
        CHECK(std::fabs(v360.z - (-2.093935)) < 1e-4);

        std::cout << "[satellite_tests] vendored SGP4 library numeric verification OK" << std::endl;
    }

    // ---- sat_math.h 封装层(Task 2)----
    {
        using namespace earthsat;

        // parseTleBlock:CelesTrak gp.php?FORMAT=tle 的标准 3 行一组格式
        std::string block =
            "ISS (ZARYA)             \n"
            "1 25544U 98067A   26185.47597407  .00009720  00000+0  18499-3 0  9996\n"
            "2 25544  51.6303 214.5154 0006744 255.1217 104.9025 15.48889796574435\n"
            "CSS (TIANHE)            \n"
            "1 48274U 21035A   26184.22810215  .00007882  00000+0  10678-3 0  9997\n"
            "2 48274  41.4672 224.3616 0002880 262.1202  97.9309 15.57904594296214\n";
        std::vector<TleEntry> entries = parseTleBlock(block);
        CHECK(entries.size() == 2);
        CHECK(entries[0].name == "ISS (ZARYA)");
        CHECK(entries[0].line1.substr(0, 7) == "1 25544");
        CHECK(entries[1].name == "CSS (TIANHE)");

        // propagateOne:与 Task 1 同一颗验证卫星 #5,同一组参考数值,但这次经我们自己的
        // 封装(含 ToGeodetic 转换),额外验证经纬度/高度/速度/单位换算没有在封装层引入误差。
        PropagatedState s0 = propagateOne(kTle5Line1, kTle5Line2, 0.0);
        CHECK(s0.valid);
        CHECK(std::fabs(s0.latDeg - 0.00032159) < 1e-4);
        CHECK(std::fabs(s0.lonDeg - 149.95573578) < 1e-4);
        CHECK(std::fabs(s0.altKm - 782.538928) < 1e-2);
        CHECK(std::fabs(s0.speedKmS - 8.073821) < 1e-3);

        PropagatedState s360 = propagateOne(kTle5Line1, kTle5Line2, 360.0);
        CHECK(s360.valid);
        CHECK(std::fabs(s360.latDeg - (-23.70534585)) < 1e-4);
        CHECK(std::fabs(s360.lonDeg - (-81.14467633)) < 1e-4);
        CHECK(std::fabs(s360.altKm - 2456.908168) < 1e-2);

        // orbitalPeriodMinutes:1440 / meanMotion(圈/天),卫星 #5 的 TLE 平均运动算出约 133.035 分钟
        double period = orbitalPeriodMinutes(kTle5Line1, kTle5Line2);
        CHECK(std::fabs(period - 133.035339) < 1e-3);

        // footprintRadiusKm:0° 仰角地面覆盖半径,Re=6371km 球近似(与 flight_data.cpp 一致)
        CHECK(std::fabs(footprintRadiusKm(400.0) - 2200.836319) < 1e-2);
        CHECK(std::fabs(footprintRadiusKm(20200.0) - 8464.922347) < 1e-1);
        CHECK(std::fabs(footprintRadiusKm(35786.0) - 9041.019336) < 1e-1);

        // destinationPoint:赤道原点、1000km、四个方位角(球面终点公式的解析验证)
        double lat, lon;
        destinationPoint(0.0, 0.0, 0.0, 1000.0, lat, lon);
        CHECK(std::fabs(lat - 8.99321606) < 1e-5 && std::fabs(lon - 0.0) < 1e-5);
        destinationPoint(0.0, 0.0, 90.0, 1000.0, lat, lon);
        CHECK(std::fabs(lat - 0.0) < 1e-5 && std::fabs(lon - 8.99321606) < 1e-5);
        destinationPoint(0.0, 0.0, 180.0, 1000.0, lat, lon);
        CHECK(std::fabs(lat - (-8.99321606)) < 1e-5 && std::fabs(lon - 0.0) < 1e-5);
        destinationPoint(0.0, 0.0, 270.0, 1000.0, lat, lon);
        CHECK(std::fabs(lat - 0.0) < 1e-5 && std::fabs(lon - (-8.99321606)) < 1e-5);

        // buildFootprintVertices:n+1 个点闭合环,首尾重合;每个点与 destinationPoint+
        // convertLLAtoECEF 直转结果一致(捕获"接线接错"类 bug,不需要额外的外部真值表)。
        std::vector<osg::Vec3d> ring = buildFootprintVertices(0.0, 0.0, 1000.0, 4);
        CHECK(ring.size() == 5);
        double lat0, lon0; destinationPoint(0.0, 0.0, 0.0, 1000.0, lat0, lon0);
        osg::Vec3d expect0 = osgVerse::Coordinate::convertLLAtoECEF(
            osg::Vec3d(osg::DegreesToRadians(lat0), osg::DegreesToRadians(lon0), 0.0));
        CHECK((ring[0] - expect0).length() < 1.0);          // 1 米内(浮点/球近似容差)
        CHECK((ring[4] - ring[0]).length() < 1e-6);          // 首尾重合

        // buildOrbitVertices:180 个采样点覆盖一整圈周期;首点应与 propagateOne(...,0.0)
        // 经 convertLLAtoECEF 直转结果一致(同样是自洽性检查,不依赖第三个独立真值源)。
        std::vector<osg::Vec3d> orbit = buildOrbitVertices(kTle5Line1, kTle5Line2, 0.0, 180);
        CHECK(orbit.size() == 180);
        osg::Vec3d expectFirst = osgVerse::Coordinate::convertLLAtoECEF(
            osg::Vec3d(osg::DegreesToRadians(s0.latDeg), osg::DegreesToRadians(s0.lonDeg), s0.altKm * 1000.0));
        CHECK((orbit[0] - expectFirst).length() < 1.0);

        std::cout << "[satellite_tests] sat_math wrapper OK" << std::endl;
    }

    // ---- satellite-AI Task 1:buildSatelliteSummaryJson 纯函数 ----
    {
        using namespace earthsat;
        std::vector<SatSummaryEntry> sats;
        SatSummaryEntry a; a.noradId = 25544; a.category = 0;
        a.latDeg = 22.0; a.lonDeg = 114.0; a.altKm = 420.0; a.speedKmS = 7.66; sats.push_back(a);
        SatSummaryEntry b; b.noradId = 48274; b.category = 0;
        b.latDeg = -10.0; b.lonDeg = 50.0; b.altKm = 390.0; b.speedKmS = 7.68; sats.push_back(b);
        SatSummaryEntry c; c.noradId = 11111; c.category = 1; sats.push_back(c);   // nav
        SatSummaryEntry d; d.noradId = 22222; d.category = 3; sats.push_back(d);   // starlink
        std::string js = buildSatelliteSummaryJson(sats);
        picojson::value v; std::string err = picojson::parse(v, js); CHECK(err.empty());
        picojson::object& o = v.get<picojson::object>();
        CHECK(o["count"].get<double>() == 4.0);
        CHECK(o["byCategory"].get("station").get<double>() == 2.0);
        CHECK(o["byCategory"].get("navigation").get<double>() == 1.0);
        CHECK(o["byCategory"].get("weather").get<double>() == 0.0);
        CHECK(o["byCategory"].get("starlink").get<double>() == 1.0);
        CHECK(o["iss"].get("found").get<bool>() == true);
        CHECK(std::fabs(o["iss"].get("latDeg").get<double>() - 22.0) < 1e-9);
        CHECK(std::fabs(o["iss"].get("lonDeg").get<double>() - 114.0) < 1e-9);
        CHECK(o["iss"].get("noradId").get<double>() == 25544.0);
        CHECK(o["tiangong"].get("found").get<bool>() == true);
        CHECK(std::fabs(o["tiangong"].get("altKm").get<double>() - 390.0) < 1e-9);
        // 空输入 → count 0、iss/tiangong found=false
        std::string js2 = buildSatelliteSummaryJson(std::vector<SatSummaryEntry>());
        picojson::value v2; CHECK(picojson::parse(v2, js2).empty());
        CHECK(v2.get("count").get<double>() == 0.0);
        CHECK(v2.get("iss").get("found").get<bool>() == false);
        CHECK(v2.get("tiangong").get("found").get<bool>() == false);
        std::cout << "[OK] buildSatelliteSummaryJson\n";
    }

    // ---- deterministic runtime safety seams ----
    {
        using namespace earthsat;
        osg::Vec3d ecef(1000.0, -2000.0, 3000.0);
        osg::Vec3d velocity(10.0, 20.0, -30.0);
        osg::Vec3d zero = extrapolateSatelliteEcef(ecef, velocity, 25.0, 25.0);
        CHECK((zero - ecef).length() < 1e-12);
        osg::Vec3d positive = extrapolateSatelliteEcef(ecef, velocity, 25.0, 30.0);
        CHECK((positive - osg::Vec3d(1050.0, -1900.0, 2850.0)).length() < 1e-12);
        osg::Vec3d negative = extrapolateSatelliteEcef(ecef, velocity, 25.0, 20.0);
        CHECK((negative - ecef).length() < 1e-12);

        for (int enabling = 0; enabling < 2; ++enabling)
            for (int fetchDone = 0; fetchDone < 2; ++fetchDone)
                for (int categoryHasData = 0; categoryHasData < 2; ++categoryHasData)
                    CHECK(shouldRequestPreciseRefetch(enabling != 0, fetchDone != 0,
                                                      categoryHasData != 0) ==
                          (enabling != 0 && fetchDone != 0 && categoryHasData == 0));
        std::cout << "[OK] satellite extrapolation and precise-refetch predicate\n";
    }

    // ---- runtime source wiring: render/pick position and explicit precise retry ----
    {
        const std::string source = readSourceFile("applications/earth_explorer/sat_data.cpp");
        CHECK(!source.empty());

        const std::string interpolate = removeWhitespace(
            extractFunctionBody(source, "void interpolateOne("));
        const std::string pick = removeWhitespace(
            extractFunctionBody(source, "void pickAt("));
        const std::string handler = removeWhitespace(
            extractFunctionBody(source, "virtual bool handle("));
        const std::string setCategory = removeWhitespace(
            extractFunctionBody(source, "virtual void setCategoryEnabled("));
        const std::string fetchRun = removeWhitespace(
            extractFunctionBody(source, "void FetchThread::run()"));
        CHECK(!interpolate.empty());
        CHECK(!pick.empty());
        CHECK(!handler.empty());
        CHECK(!setCategory.empty());
        CHECK(!fetchRun.empty());

        const std::string interpolateCall =
            "(*va)[i]=earthsat::extrapolateSatelliteEcef("
            "sats[i].ecef,sats[i].ecefVelocity,sats[i].lastUpdateRefTime,refTime);";
        CHECK(interpolate.find(interpolateCall) != std::string::npos);

        const std::string pickCall =
            "osg::Vec3dP=earthsat::extrapolateSatelliteEcef("
            "_visiblePrecise[i].ecef,_visiblePrecise[i].ecefVelocity,"
            "_visiblePrecise[i].lastUpdateRefTime,refTime);";
        const size_t pickCallPos = pick.find(pickCall);
        const size_t hemispherePos = pick.find("if((eye*P)<=(P*P))");
        const size_t projectionPos = pick.find("osg::Vec3dwin=P*VPW;");
        CHECK(pickCallPos != std::string::npos);
        CHECK(hemispherePos != std::string::npos && pickCallPos < hemispherePos);
        CHECK(projectionPos != std::string::npos && pickCallPos < projectionPos);
        CHECK(pick.find("_visiblePrecise[i].ecef*VPW") == std::string::npos);
        CHECK(pick.find("sat.ecef*VPW") == std::string::npos);

        CHECK(handler.find("view->getFrameStamp()") != std::string::npos);
        CHECK(handler.find("frameStamp?frameStamp->getReferenceTime():0.0") != std::string::npos);
        CHECK(handler.find("_owner->pickAt(cam,ea.getX(),my,refTime);") != std::string::npos);

        const size_t preciseCategories = setCategory.find(
            "cat==SatCategory::Station||cat==SatCategory::Navigation||"
            "cat==SatCategory::Weather");
        const size_t precisePredicate = setCategory.find(
            "earthsat::shouldRequestPreciseRefetch(on,_preciseFetchDone,categoryHasData)");
        CHECK(preciseCategories != std::string::npos);
        CHECK(precisePredicate != std::string::npos && preciseCategories < precisePredicate);

        const size_t takePrecise = fetchRun.find(
            "if(_owner->takePreciseRefetchRequest())preciseFetchedOnce=false;");
        const size_t preciseGuard = fetchRun.find(
            "if(_owner->preciseFetchTriggered()&&!preciseFetchedOnce)");
        CHECK(takePrecise != std::string::npos);
        CHECK(preciseGuard != std::string::npos && takePrecise < preciseGuard);
        std::cout << "[OK] satellite runtime source wiring\n";
    }

    std::cout << "[satellite_tests] all OK" << std::endl;
    return 0;
}
