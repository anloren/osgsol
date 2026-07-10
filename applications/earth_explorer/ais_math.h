#ifndef EARTH_AIS_MATH_H
#define EARTH_AIS_MATH_H
#include <string>
// AIS(aisstream.io)纯函数层:消息解析 / 订阅 bbox 决策 / 订阅消息构造。
// 不含任何网络与 OSG 依赖,tests/ais_tests.cpp 直测(仿 sat_math 先例)。
namespace earthais
{
    struct AisShip
    {
        long long mmsi = 0;
        double lat = 0, lon = 0, sogKn = 0, cogDeg = 0;   // SOG 节;COG 真航向角(度)
        std::string name;
        double lastSeenUnix = 0;   // 由调用方(收到消息那一刻)填,解析器不管时钟
        bool valid = false;
    };
    // 只认 MessageType=="PositionReport";其余类型/畸形 JSON → valid=false。
    // 字段形状按 aisstream 官方文档:MetaData.MMSI/ShipName,
    // Message.PositionReport.{Latitude,Longitude,Sog,Cog}。
    AisShip parseAisMessage(const std::string& jsonText);

    struct ShipBBox { double latMin = -85, lonMin = -180, latMax = 85, lonMax = 180; };
    // 中心不动、跨度乘 factor,钳到 [-85,85]/[-180,180](aisstream bbox 是订阅时一次性
    // 指定,外扩订阅可容忍视口小幅平移而不必立刻重连)。
    ShipBBox inflateBBox(const ShipBBox& b, double factor);
    // 需要断开重连换订阅的三种情形:视口中心移出订阅范围 / 拉远超出订阅范围 /
    // 推近到订阅跨度 1/4 以下(订阅范围过大浪费消息流量)。
    bool bboxNeedsResubscribe(const ShipBBox& subscribed, const ShipBBox& view);
    // aisstream 订阅消息:{"APIKey":..,"BoundingBoxes":[[[latMin,lonMin],[latMax,lonMax]]],
    //  "FilterMessageTypes":["PositionReport"]}(坐标序 [lat,lon],与官方示例一致)。
    std::string buildSubscriptionJson(const std::string& apiKey, const ShipBBox& b);
}
#endif
