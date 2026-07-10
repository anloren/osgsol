// NASA FIRMS 活跃火点源(P3):全球 VIIRS 近实时 CSV,量级 1e4~1e5 点/日——
// 本仓库第一个启用 FeedClusterSpec 三级聚合 LOD 的源(高空 1° / 中空 0.25° / 近地原始点)。
#include "firms_feed.h"
#include "../earth_config.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>

namespace earthfeed
{
    // acq_date "2026-07-05" + acq_time "342"/"0342"(HHMM,可能不足 4 位)→ Unix 秒(UTC)
    static long long firmsParseTime(const std::string& d, const std::string& t)
    {
        struct tm tmv = tm();
        if (sscanf(d.c_str(), "%d-%d-%d", &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday) != 3) return 0;
        tmv.tm_year -= 1900; tmv.tm_mon -= 1;
        int hhmm = atoi(t.c_str());
        tmv.tm_hour = hhmm / 100; tmv.tm_min = hhmm % 100;
#ifdef _WIN32
        return (long long)_mkgmtime(&tmv);
#else
        return (long long)timegm(&tmv);
#endif
    }

    // FRP(MW)→ 颜色/大小三档:小黄 / 中橙 / 大红。
    static void frpStyle(double frp, osg::Vec4& color, float& sizePx)
    {
        if (frp < 5.0)       { color = osg::Vec4(1.00f, 0.85f, 0.30f, 1.0f); sizePx = 6.0f; }
        else if (frp < 20.0) { color = osg::Vec4(1.00f, 0.60f, 0.15f, 1.0f); sizePx = 9.0f; }
        else                 { color = osg::Vec4(1.00f, 0.30f, 0.12f, 1.0f); sizePx = 12.0f; }
    }

    std::vector<FeedPoint> parseFirmsCsv(const std::string& body)
    {
        std::vector<FeedPoint> out;
        std::istringstream in(body);
        std::string line;
        if (!std::getline(in, line)) return out;
        // 表头 → 列名索引(FIRMS 不同 SOURCE 列序有差异,不能按固定下标)
        std::map<std::string, int> col; { std::istringstream h(line); std::string c; int i = 0;
            while (std::getline(h, c, ',')) { if (!c.empty() && c[c.size()-1]=='\r') c.erase(c.size()-1); col[c] = i++; } }
        if (!col.count("latitude") || !col.count("longitude")) return out;
        int iLat = col["latitude"], iLon = col["longitude"];
        int iFrp = col.count("frp") ? col["frp"] : -1;
        int iConf = col.count("confidence") ? col["confidence"] : -1;
        int iDate = col.count("acq_date") ? col["acq_date"] : -1;
        int iTime = col.count("acq_time") ? col["acq_time"] : -1;
        int iDN = col.count("daynight") ? col["daynight"] : -1;
        while (std::getline(in, line))
        {
            if (line.empty()) continue;
            std::vector<std::string> f; { std::istringstream ls(line); std::string c;
                while (std::getline(ls, c, ',')) { if (!c.empty() && c[c.size()-1]=='\r') c.erase(c.size()-1); f.push_back(c); } }
            if ((int)f.size() <= iLon) continue;
            char* e1 = 0; char* e2 = 0;
            double lat = strtod(f[iLat].c_str(), &e1), lon = strtod(f[iLon].c_str(), &e2);
            if (e1 == f[iLat].c_str() || e2 == f[iLon].c_str()) continue;   // 畸形行
            double frp = (iFrp >= 0 && iFrp < (int)f.size()) ? atof(f[iFrp].c_str()) : 0.0;
            std::string conf = (iConf >= 0 && iConf < (int)f.size()) ? f[iConf] : "";
            std::string date = (iDate >= 0 && iDate < (int)f.size()) ? f[iDate] : "";
            std::string time = (iTime >= 0 && iTime < (int)f.size()) ? f[iTime] : "";
            std::string dn = (iDN >= 0 && iDN < (int)f.size()) ? f[iDN] : "";
            FeedPoint p; p.lat = lat; p.lon = lon;
            frpStyle(frp, p.color, p.sizePx);
            char buf[96]; snprintf(buf, sizeof(buf), u8"火点 Fire · FRP %.1f MW", frp);
            p.title = buf;
            p.unixTime = (double)firmsParseTime(date, time);
            snprintf(buf, sizeof(buf), u8"辐射功率 FRP: %.1f MW\n", frp); p.detail = buf;
            p.detail += u8"置信度 Confidence: " + (conf.empty() ? std::string("?") : conf) + "\n";
            p.detail += u8"观测 Observed: " + date + " " + time + " UTC\n";
            p.detail += u8"昼夜 Day/Night: " + (dn == "D" ? std::string(u8"昼") : std::string(u8"夜"));
            out.push_back(p);
        }
        return out;
    }

    // 聚合点样式:火点专属文案 + 最大 FRP 决定颜色档。
    static FeedPoint firmsAggregate(const std::vector<FeedPoint>& ms, double cLat, double cLon)
    {
        (void)cLat; (void)cLon;   // 位置由框架统一覆盖
        double maxFrp = 0.0; double tMax = 0.0;
        for (size_t i = 0; i < ms.size(); ++i)
        {
            double frp = 0.0; sscanf(ms[i].title.c_str(), u8"火点 Fire · FRP %lf", &frp);
            if (frp > maxFrp) maxFrp = frp;
            if (ms[i].unixTime > tMax) tMax = ms[i].unixTime;
        }
        FeedPoint a; float basePx = 0.0f;
        frpStyle(maxFrp, a.color, basePx);
        a.sizePx = std::min(26.0f, basePx + 5.0f * log10f((float)ms.size() + 1.0f));
        char buf[96];
        snprintf(buf, sizeof(buf), u8"火点聚合区 (%d 处)", (int)ms.size()); a.title = buf;
        snprintf(buf, sizeof(buf), u8"该区域 %d 个火点\n最大辐射功率 max FRP: %.1f MW\n推近查看单个火点",
                 (int)ms.size(), maxFrp);
        a.detail = buf;
        return a;
    }

    static std::string firmsSummaryJson(const std::vector<FeedPoint>& pts)
    {
        int nLow = 0, nMid = 0, nHigh = 0; double maxFrp = 0.0;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            double frp = 0.0; sscanf(pts[i].title.c_str(), u8"火点 Fire · FRP %lf", &frp);
            if (frp < 5.0) nLow++; else if (frp < 20.0) nMid++; else nHigh++;
            if (frp > maxFrp) maxFrp = frp;
        }
        picojson::object r; r["count"] = picojson::value((double)pts.size());
        picojson::object b;
        b["frp<5MW"] = picojson::value((double)nLow);
        b["frp5-20MW"] = picojson::value((double)nMid);
        b["frp>=20MW"] = picojson::value((double)nHigh);
        r["byIntensity"] = picojson::value(b);
        r["maxFrpMW"] = picojson::value(maxFrp);
        return picojson::value(r).serialize();
    }
}

osg::Node* registerFirmsFeed(osgViewer::Viewer& viewer, LayerManager* layers, earthai::ToolRegistry* tools)
{
    earthfeed::FeedSpec spec;
    spec.id = "fires"; spec.displayName = u8"火点 (FIRMS)";
    spec.group = u8"实时数据 / Live";
    // 环境变量优先,回退磁盘 keys.env(双击 .app 启动读不到环境变量,见 earth_config.h)。
    std::string key = earthcfg::resolveKey("EARTH_FIRMS_KEY");
    if (!key.empty())
    {
        // 日范围用 2 不用 1:DAY_RANGE=1 是"最近 1 个日历日(UTC)",而 VIIRS NRT 产品
        // 有数小时处理延迟——每天 UTC 凌晨(北京时间上午)当日数据尚未入库,range=1 会
        // 拿到 HTTP 200 + 只有表头的空 CSV(真机验证 2026-07-06 实测踩中:可用性接口显示
        // SNPP 最新只到前一天)。range=2 恒覆盖"昨天+今天",最坏多显示一天旧火点,可接受。
        spec.url = std::string("https://firms.modaps.eosdis.nasa.gov/api/area/csv/")
                 + key + "/VIIRS_SNPP_NRT/world/2";
        spec.subtitle = u8"NASA VIIRS 近实时 · 最近 48h";
    }
    else spec.subtitle = u8"未配置 key(EARTH_FIRMS_KEY)";
    // url 为空且无 fixture 时 registerFeedLayer 走"fetch disabled"早退(T2),不会干轮询。
    spec.refreshSeconds = 1800;
    spec.fixtureEnv = "EARTH_FIRES_FILE"; spec.forceEnv = "EARTH_FIRES";
    spec.parse = earthfeed::parseFirmsCsv;
    spec.summaryJson = earthfeed::firmsSummaryJson;
    // 三级聚合(spec §2.2):近地原始点 / 中空 0.25° / 高空 1°。
    earthfeed::FeedClusterLevel raw; raw.maxCameraAltKm = 300.0;   raw.cellDeg = 0.0;
    earthfeed::FeedClusterLevel mid; mid.maxCameraAltKm = 1500.0;  mid.cellDeg = 0.25;
    earthfeed::FeedClusterLevel top; top.maxCameraAltKm = 1.0e9;   top.cellDeg = 1.0;
    spec.cluster.levels.push_back(raw); spec.cluster.levels.push_back(mid); spec.cluster.levels.push_back(top);
    spec.cluster.makeAggregate = earthfeed::firmsAggregate;
    // P3 修复:火点常年落在高原/山区(云南 2000-4000m+ 椭球高),既有 3000m 默认抬升
    // 会被地形矫正之前(或矫正因瓦片未加载而静默失效)的一次性椭球高兜底沉没——聚合
    // 层级更是完全不做地形矫正(见 feed_layer.cpp buildClusterLevels 注释)。抬到
    // 9000m(高于全球最高地表——珠峰椭球高约 8849m),让可见性不依赖地形矫正是否命中;
    // FIRMS 展示的视角高度都在 ≥150km 量级,9km 的视觉误差可忽略不计。
    spec.liftMeters = 9000.0;
    spec.toolDescriptionCn = u8"查询全球活跃火点汇总(NASA FIRMS VIIRS 近实时,最近 48h):"
        u8"总数、按辐射功率 FRP 强度分桶、最大 FRP。需要 EARTH_FIRMS_KEY;"
        u8"未配置 key 时 count 恒为 0。若图层尚未开启会自动开启。";
    return earthfeed::registerFeedLayer(spec, viewer, layers, tools);
}
