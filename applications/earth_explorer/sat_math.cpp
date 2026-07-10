#include <cmath>
#include <sstream>
#include <picojson.h>
#include <modeling/Math.h>
#include "3rdparty/sgp4/Tle.h"
#include "3rdparty/sgp4/SGP4.h"
#include "3rdparty/sgp4/Eci.h"
#include "3rdparty/sgp4/SatelliteException.h"
#include "3rdparty/sgp4/DecayedException.h"
#include "sat_math.h"

namespace earthsat
{
namespace
{
    const double kEarthRadiusKm = 6371.0;   // 平均地球半径球近似,同 flight_data.cpp 的 6371000.0m
    const double kDeg2Rad = M_PI / 180.0, kRad2Deg = 180.0 / M_PI;
}

std::vector<TleEntry> parseTleBlock(const std::string& text)
{
    std::vector<TleEntry> out;
    std::istringstream in(text);
    std::string line;
    std::vector<std::string> buf;
    while (std::getline(in, line))
    {
        // 去掉行尾 \r(Windows 换行/网络响应常见)
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        buf.push_back(line);
        if (buf.size() == 3)
        {
            TleEntry e; e.name = buf[0]; e.line1 = buf[1]; e.line2 = buf[2];
            // 去掉名称行右侧补齐空格(CelesTrak 格式固定补到 24 字符)
            while (!e.name.empty() && e.name.back() == ' ') e.name.pop_back();
            out.push_back(e);
            buf.clear();
        }
    }
    return out;   // 末尾残余(buf.size()<3)丢弃,不报错——容忍截断的网络响应
}

PropagatedState propagateOne(const std::string& line1, const std::string& line2,
                             double minutesSinceEpoch)
{
    PropagatedState result;
    try
    {
        libsgp4::Tle tle(line1, line2);
        libsgp4::SGP4 sgp4(tle);
        libsgp4::Eci eci = sgp4.FindPosition(minutesSinceEpoch);
        libsgp4::CoordGeodetic geo = eci.ToGeodetic();
        libsgp4::Vector v = eci.Velocity();
        result.latDeg = geo.latitude * kRad2Deg;
        result.lonDeg = geo.longitude * kRad2Deg;
        result.altKm = geo.altitude;
        result.speedKmS = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        result.valid = true;
    }
    catch (const std::exception&)
    {
        // 轨道衰减/元素非法等——静默返回 invalid,调用方跳过该卫星(不崩溃)。
        // 真实 CelesTrak 数据里精选组(空间站/导航/气象)均为在轨维护卫星,预期
        // 极少触发;Starlink 全量偶有失效星体属正常现象。
    }
    return result;
}

double orbitalPeriodMinutes(const std::string& line1, const std::string& line2)
{
    try
    {
        libsgp4::Tle tle(line1, line2);
        double meanMotion = tle.MeanMotion();   // 圈/天
        if (meanMotion <= 0.0) return 0.0;
        return 1440.0 / meanMotion;
    }
    catch (const std::exception&) { return 0.0; }
}

double footprintRadiusKm(double altKm)
{
    if (altKm <= 0.0) return 0.0;
    double c = kEarthRadiusKm / (kEarthRadiusKm + altKm);
    if (c > 1.0) c = 1.0; if (c < -1.0) c = -1.0;
    return kEarthRadiusKm * std::acos(c);
}

void destinationPoint(double lat1Deg, double lon1Deg, double bearingDeg, double distanceKm,
                      double& outLatDeg, double& outLonDeg)
{
    double lat1 = lat1Deg * kDeg2Rad, lon1 = lon1Deg * kDeg2Rad, brng = bearingDeg * kDeg2Rad;
    double ang = distanceKm / kEarthRadiusKm;
    double lat2 = std::asin(std::sin(lat1) * std::cos(ang) +
                            std::cos(lat1) * std::sin(ang) * std::cos(brng));
    double lon2 = lon1 + std::atan2(std::sin(brng) * std::sin(ang) * std::cos(lat1),
                                    std::cos(ang) - std::sin(lat1) * std::sin(lat2));
    outLatDeg = lat2 * kRad2Deg;
    outLonDeg = lon2 * kRad2Deg;
}

std::vector<osg::Vec3d> buildOrbitVertices(const std::string& line1, const std::string& line2,
                                           double startMinutesSinceEpoch, int n)
{
    std::vector<osg::Vec3d> out;
    if (n < 2) n = 2;
    double period = orbitalPeriodMinutes(line1, line2);
    if (period <= 0.0) return out;   // 非法 TLE,返回空——调用方应据此跳过渲染
    double step = period / (double)(n - 1);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        double t = startMinutesSinceEpoch + step * (double)i;
        PropagatedState s = propagateOne(line1, line2, t);
        if (!s.valid) continue;   // 单点传播失败——跳过,不中断整条轨道线
        out.push_back(osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            s.latDeg * kDeg2Rad, s.lonDeg * kDeg2Rad, s.altKm * 1000.0)));
    }
    return out;
}

std::vector<osg::Vec3d> buildFootprintVertices(double centerLatDeg, double centerLonDeg,
                                               double radiusKm, int n)
{
    if (n < 3) n = 3;
    std::vector<osg::Vec3d> out; out.reserve(n + 1);
    for (int i = 0; i <= n; ++i)   // <=n:最后一点方位角=360°=0°,与首点重合,闭合环
    {
        double bearing = 360.0 * (double)i / (double)n;
        double lat, lon; destinationPoint(centerLatDeg, centerLonDeg, bearing, radiusKm, lat, lon);
        out.push_back(osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            lat * kDeg2Rad, lon * kDeg2Rad, 0.0)));
    }
    return out;
}

// 一颗特殊卫星(ISS/天宫)的位置对象;p 为空 → {found:false}。
static picojson::value oneSat(const SatSummaryEntry* p)
{
    picojson::object o;
    if (!p) { o["found"] = picojson::value(false); return picojson::value(o); }
    o["found"]    = picojson::value(true);
    o["noradId"]  = picojson::value((double)p->noradId);
    o["latDeg"]   = picojson::value(p->latDeg);
    o["lonDeg"]   = picojson::value(p->lonDeg);
    o["altKm"]    = picojson::value(p->altKm);
    o["speedKmS"] = picojson::value(p->speedKmS);
    return picojson::value(o);
}

std::string buildSatelliteSummaryJson(const std::vector<SatSummaryEntry>& sats)
{
    int cat[4] = { 0, 0, 0, 0 };
    const SatSummaryEntry* iss = 0;
    const SatSummaryEntry* css = 0;
    for (size_t i = 0; i < sats.size(); ++i)
    {
        int c = sats[i].category;
        if (c >= 0 && c < 4) cat[c]++;
        if (sats[i].noradId == 25544) iss = &sats[i];
        else if (sats[i].noradId == 48274) css = &sats[i];
    }
    picojson::object r;
    r["count"] = picojson::value((double)sats.size());
    picojson::object bc;
    bc["station"]    = picojson::value((double)cat[0]);
    bc["navigation"] = picojson::value((double)cat[1]);
    bc["weather"]    = picojson::value((double)cat[2]);
    bc["starlink"]   = picojson::value((double)cat[3]);
    r["byCategory"] = picojson::value(bc);
    r["iss"]      = oneSat(iss);
    r["tiangong"] = oneSat(css);
    r["note"] = picojson::value(std::string(
        u8"各类别计数只反映已开启并抓取到的类目;Starlink 需单独开启"));
    return picojson::value(r).serialize();
}
}
