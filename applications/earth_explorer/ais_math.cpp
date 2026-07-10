#include <picojson.h>
#include <algorithm>
#include "ais_math.h"

namespace earthais
{
    AisShip parseAisMessage(const std::string& jsonText)
    {
        AisShip s;
        picojson::value root; std::string err = picojson::parse(root, jsonText);
        if (!err.empty() || !root.is<picojson::object>()) return s;
        if (!root.get("MessageType").is<std::string>()
            || root.get("MessageType").get<std::string>() != "PositionReport") return s;
        const picojson::value& msg = root.get("Message");
        if (!msg.is<picojson::object>()) return s;
        const picojson::value& pr = msg.get("PositionReport");
        if (!pr.is<picojson::object>()) return s;
        if (!pr.get("Latitude").is<double>() || !pr.get("Longitude").is<double>()) return s;
        s.lat = pr.get("Latitude").get<double>();
        s.lon = pr.get("Longitude").get<double>();
        s.sogKn = pr.get("Sog").is<double>() ? pr.get("Sog").get<double>() : 0.0;
        s.cogDeg = pr.get("Cog").is<double>() ? pr.get("Cog").get<double>() : 0.0;
        const picojson::value& meta = root.get("MetaData");
        if (meta.is<picojson::object>())
        {
            if (meta.get("MMSI").is<double>()) s.mmsi = (long long)meta.get("MMSI").get<double>();
            if (meta.get("ShipName").is<std::string>())
            {
                s.name = meta.get("ShipName").get<std::string>();
                // aisstream 船名常带右侧空格填充,裁掉
                while (!s.name.empty() && s.name[s.name.size()-1] == ' ') s.name.erase(s.name.size()-1);
            }
        }
        if (s.mmsi <= 0) return s;   // 没有 MMSI 无法入存量表
        s.valid = true;
        return s;
    }

    ShipBBox inflateBBox(const ShipBBox& b, double factor)
    {
        double cLat = (b.latMin + b.latMax) * 0.5, cLon = (b.lonMin + b.lonMax) * 0.5;
        double hLat = (b.latMax - b.latMin) * 0.5 * factor, hLon = (b.lonMax - b.lonMin) * 0.5 * factor;
        ShipBBox o;
        o.latMin = std::max(-85.0, cLat - hLat); o.latMax = std::min(85.0, cLat + hLat);
        o.lonMin = std::max(-180.0, cLon - hLon); o.lonMax = std::min(180.0, cLon + hLon);
        return o;
    }

    bool bboxNeedsResubscribe(const ShipBBox& sub, const ShipBBox& view)
    {
        double cLat = (view.latMin + view.latMax) * 0.5, cLon = (view.lonMin + view.lonMax) * 0.5;
        if (cLat < sub.latMin || cLat > sub.latMax || cLon < sub.lonMin || cLon > sub.lonMax) return true;
        double vs = std::max(view.latMax - view.latMin, view.lonMax - view.lonMin);
        double ss = std::max(sub.latMax - sub.latMin, sub.lonMax - sub.lonMin);
        if (vs > ss) return true;          // 拉远超出订阅范围
        if (vs < ss * 0.25) return true;   // 推近太多,订阅过宽浪费流量
        return false;
    }

    std::string buildSubscriptionJson(const std::string& apiKey, const ShipBBox& b)
    {
        picojson::array sw, ne, box, boxes;
        sw.push_back(picojson::value(b.latMin)); sw.push_back(picojson::value(b.lonMin));
        ne.push_back(picojson::value(b.latMax)); ne.push_back(picojson::value(b.lonMax));
        box.push_back(picojson::value(sw)); box.push_back(picojson::value(ne));
        boxes.push_back(picojson::value(box));
        picojson::object o;
        o["APIKey"] = picojson::value(apiKey);
        o["BoundingBoxes"] = picojson::value(boxes);
        picojson::array types; types.push_back(picojson::value(std::string("PositionReport")));
        o["FilterMessageTypes"] = picojson::value(types);
        return picojson::value(o).serialize();
    }
}
