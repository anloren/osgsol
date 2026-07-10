#ifndef EARTH_SAT_MATH_H
#define EARTH_SAT_MATH_H
// sat_math.h — 卫星层(P2)的纯函数底座:TLE 解析、SGP4 递推包装、轨道/足迹几何采样。
// 设计约定(与 geo_primitives.h 一致):纯函数,不建 viewer/GL 上下文即可调用,
// tests/satellite_tests.cpp 直接单测。场景图构建(点云/轨道线/足迹圆的 Geometry)
// 在 sat_data.cpp,不在这里。
// 单位约定:经纬度用**度**(与本仓库其余 feed 模块一致,如 FlightInfo/FeedPoint);
// 高度/半径用**千米**(TLE/SGP4 生态惯用单位,与 vendored libsgp4 API 直接对应,
// 减少转换出错概率);ECEF 输出统一用**米**(与 osgVerse::Coordinate::convertLLAtoECEF
// 的既有约定一致)。
#include <string>
#include <vector>
#include <osg/Vec3d>

namespace earthsat
{
    // 单颗卫星的原始 TLE 三行(名称+两行元素)。
    struct TleEntry { std::string name, line1, line2; };

    // 解析 CelesTrak gp.php?FORMAT=tle 的标准返回格式:每卫星 3 行(名称行右侧空格
    // 补齐到 24 字符,不含数字前缀;line1/line2 各 69 字符)。按 3 行一组切分,
    // 跳过空行;末尾不足 3 行的残余丢弃(容忍截断的网络响应)。
    std::vector<TleEntry> parseTleBlock(const std::string& text);

    // 单颗卫星在其自身 TLE 历元之后 minutesSinceEpoch 分钟时的地心状态(WGS72 球近似
    // 大地坐标,经 libsgp4::Eci::ToGeodetic 转换)。传播失败(轨道衰减/元素非法等
    // libsgp4 内部异常)时 valid=false,其余字段保持默认值,调用方应跳过该卫星
    // (不崩溃、不假装有效数据)。
    struct PropagatedState
    {
        double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
        bool valid = false;
    };
    PropagatedState propagateOne(const std::string& line1, const std::string& line2,
                                double minutesSinceEpoch);

    // 轨道周期(分钟),直接由 TLE 的平均运动(圈/天,line2 倒数第二个字段)反推:
    // 1440 / meanMotionRevPerDay。传播失败(TLE 非法)时返回 0.0。
    double orbitalPeriodMinutes(const std::string& line1, const std::string& line2);

    // 0° 仰角地面覆盖半径(km):Re*acos(Re/(Re+altKm)),Re 取平均地球半径 6371km
    // (与仓库既有 flight_data.cpp 的球近似一致,非 WGS84 椭球——够用于可视化,不用于
    // 精确覆盖预测)。altKm<=0 时返回 0。
    double footprintRadiusKm(double altKm);

    // 球面终点公式(方位角+距离,Re=6371km 球近似):从 (lat1,lon1) 出发,沿方位角
    // bearingDeg(0=正北,顺时针)走 distanceKm 后的终点经纬度。足迹圆/轨道采样的
    // 共用几何底座。
    void destinationPoint(double lat1Deg, double lon1Deg, double bearingDeg, double distanceKm,
                         double& outLatDeg, double& outLonDeg);

    // 未来一整圈轨道采样点(ECEF,米),从 tsince=startMinutesSinceEpoch 起按周期
    // 均分 n 个点(n<2 钳到 2)。传播失败的采样点会被跳过——返回数组长度可能 < n
    // (调用方应按实际返回长度渲染,不假设恰好 n 个)。
    std::vector<osg::Vec3d> buildOrbitVertices(const std::string& line1, const std::string& line2,
                                               double startMinutesSinceEpoch, int n);

    // 星下点为心的地面足迹圆(ECEF,米,贴地 altitude=0),按方位角均分 n 段闭合环:
    // 返回 n+1 个点,首尾重合(便于直接喂给 LINE_STRIP 渲染出闭合圆,不需要额外
    // 处理收尾)。n<3 钳到 3。
    std::vector<osg::Vec3d> buildFootprintVertices(double centerLatDeg, double centerLonDeg,
                                                   double radiusKm, int n);

    // ===== 卫星汇总(供 AI get_satellites_summary)=====
    // 最小输入结构(不依赖 sat_data 的 SatCategory 枚举,category 用 int,枚举序一致:
    // 0=空间站 1=导航 2=气象 3=Starlink)。抽成纯函数便于单测。
    struct SatSummaryEntry
    {
        int noradId = 0;
        int category = 0;
        double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
    };
    // 生成汇总 JSON:{count, byCategory:{station,navigation,weather,starlink},
    // iss/tiangong:{found, [noradId,latDeg,lonDeg,altKm,speedKmS]}, note}。
    // iss=NORAD 25544、tiangong=NORAD 48274;找不到则该对象 {found:false}。
    std::string buildSatelliteSummaryJson(const std::vector<SatSummaryEntry>& sats);
}
#endif
