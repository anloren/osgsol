// GPSJam GPS 干扰源(P1 Task 4)。plan 假设的 /data/<日期>.geojson 已下线
// (2026-07-03 实测:任意日期都 404 "File not found")——现行真实结构(从站点
// JS bundle 挖出并 curl 验证):
//   https://gpsjam.org/data/<YYYY-MM-DD>-h3_4.csv    (manifest.csv 列可用日期)
//   列:hex,count_good_aircraft,count_bad_aircraft   (H3 res4 cell id,无坐标!)
//   服务器无视 Accept-Encoding 一律回 gzip(libhv 不解压)→ 本文件自行 gunzip。
// 干扰等级公式(站点图例原文):坏占比 = bad/(good+bad),低 0-2% / 中 2-10% / 高 >10%。
// cell id → 中心点解码:h3_lite.h(官方 uber/h3 移植,已对拍验证)。
#include "gpsjam_feed.h"
#include "h3_lite.h"
#include "3rdparty/libdeflate.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace earthfeed
{
    // gzip 魔数则解压,否则原样返回(fixture 明文)。仓库 vendored 的 libdeflate.c 是
    // 裁剪版(只有 raw DEFLATE 解压、无 gzip 包装函数)→ 自己剥 gzip 头(RFC 1952:
    // 10 字节定长 + FLG 可选段)再调 libdeflate_deflate_decompress。
    // gzip 尾 4 字节 ISIZE = 原始长度 mod 2^32,一次分配到位;异常(>64MB/损坏)返回空。
    static std::string gunzipIfNeeded(const std::string& body)
    {
        const unsigned char* b = (const unsigned char*)body.data();
        size_t n = body.size(), off = 10;
        if (n < 18 || b[0] != 0x1f || b[1] != 0x8b) return body;
        if (b[2] != 8) return "";                          // CM != DEFLATE,不认识
        unsigned flg = b[3];
        if (flg & 4) { if (off + 2 > n) return ""; off += 2 + (b[off] | (b[off + 1] << 8)); }   // FEXTRA
        if (flg & 8) { while (off < n && b[off]) off++; off++; }        // FNAME(NUL 结尾)
        if (flg & 16) { while (off < n && b[off]) off++; off++; }       // FCOMMENT
        if (flg & 2) off += 2;                                          // FHCRC
        if (off + 8 > n) return "";                        // 尾部 CRC32+ISIZE 都不够,必是截断
        uint32_t isize = (uint32_t)b[n - 4] | ((uint32_t)b[n - 3] << 8)
                       | ((uint32_t)b[n - 2] << 16) | ((uint32_t)b[n - 1] << 24);
        if (isize == 0 || isize > (64u << 20)) return "";
        libdeflate_decompressor* d = libdeflate_alloc_decompressor();
        if (!d) return "";
        std::string out(isize, '\0'); size_t actual = 0;
        libdeflate_result r = libdeflate_deflate_decompress(d, b + off, n - 8 - off,
                                                            &out[0], out.size(), &actual);
        libdeflate_free_decompressor(d);
        if (r != LIBDEFLATE_SUCCESS) return "";
        out.resize(actual); return out;
    }

    std::vector<FeedPoint> parseGpsjam(const std::string& rawBody,
                                       const std::string& dateStr, double dateUnix)
    {
        // 等级 → 名称/大小/颜色(黄/橙/红),阈值见文件头(站点图例)
        static const char* kLvName[3] = { u8"低 / Low", u8"中 / Medium", u8"高 / High" };
        static const float kLvSize[3] = { 8.0f, 10.0f, 12.0f };
        static const osg::Vec4 kLvColor[3] = { osg::Vec4(1.0f, 0.85f, 0.15f, 1.0f),
            osg::Vec4(1.0f, 0.55f, 0.1f, 1.0f), osg::Vec4(1.0f, 0.15f, 0.1f, 1.0f) };
        std::vector<FeedPoint> out;
        std::string body = gunzipIfNeeded(rawBody);
        for (size_t pos = 0; pos < body.size();)
        {
            size_t eol = body.find('\n', pos);
            std::string line = body.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
            pos = (eol == std::string::npos) ? body.size() : eol + 1;
            if (!line.empty() && line[line.size() - 1] == '\r') line.erase(line.size() - 1);
            size_t c1 = line.find(','), c2 = (c1 == std::string::npos) ? c1 : line.find(',', c1 + 1);
            if (c2 == std::string::npos) continue;   // 字段不足(空行等)安全跳过
            std::string hexStr = line.substr(0, c1);
            // 三个字段都要求"整段可解析":表头行("hex",...)/坏行在此被自然滤掉
            char* e1; unsigned long long cell = strtoull(hexStr.c_str(), &e1, 16);
            if (hexStr.empty() || *e1 != '\0') continue;
            char* e2; double good = strtod(line.c_str() + c1 + 1, &e2);
            if (e2 != line.c_str() + c2 || good < 0.0) continue;
            char* e3; double bad = strtod(line.c_str() + c2 + 1, &e3);
            if (e3 != line.c_str() + line.size() || bad < 1.0) continue;   // 无坏航迹的网格不上屏(降噪)
            double lat = 0.0, lon = 0.0;
            if (!h3lite::cellToLatLngDeg((uint64_t)cell, lat, lon)) continue;   // 非法 base cell
            double ratio = bad / (good + bad);
            int lv = (ratio > 0.10) ? 2 : ((ratio > 0.02) ? 1 : 0);
            FeedPoint p; p.lat = lat; p.lon = lon;
            p.sizePx = kLvSize[lv]; p.color = kLvColor[lv];
            p.title = std::string(u8"GPS 干扰 ") + kLvName[lv];
            char buf[224];
            snprintf(buf, sizeof(buf), u8"H3 网格 Cell: %s\n干扰等级 Level: %s\n"
                     u8"航迹 好/坏 Good/Bad: %.0f / %.0f(坏占 %.1f%%)\n日期 Date: %s(UTC)\n"
                     u8"数据源 gpsjam.org(源自 ADS-B)", hexStr.c_str(), kLvName[lv],
                     good, bad, ratio * 100.0, dateStr.c_str());
            p.detail = buf;
            snprintf(buf, sizeof(buf), "https://gpsjam.org/?lat=%.3f&lon=%.3f&z=6&date=%s",
                     lat, lon, dateStr.c_str());
            p.url = buf; p.unixTime = dateUnix;
            picojson::object o;   // raw 存解析后的干净字段,供 summaryJson 分桶/排序
            o["hex"] = picojson::value(hexStr); o["good"] = picojson::value(good);
            o["bad"] = picojson::value(bad); o["ratio"] = picojson::value(ratio);
            p.raw = picojson::value(o); out.push_back(p);
        }
        return out;
    }

    // 自定义汇总:count + 按等级分桶 + 高干扰 TOP3(坏占比降序,同比按坏数降序)。
    // hex id 来自网络,严禁字符串拼 JSON:全程 picojson 构建。
    static std::string gpsjamSummaryJson(const std::vector<FeedPoint>& pts)
    {
        int byLv[3] = { 0, 0, 0 };
        std::vector<std::pair<std::pair<double, double>, std::string> > v;   // ((-ratio,-bad), hex)
        for (size_t i = 0; i < pts.size(); ++i)
        {
            double ratio = 0.0, bad = 0.0; std::string hex = "?";
            if (pts[i].raw.is<picojson::object>())   // raw 不保证是 object(gdacs 复审 carry-forward)
            {
                const picojson::value& r = pts[i].raw;
                if (r.get("ratio").is<double>()) ratio = r.get("ratio").get<double>();
                if (r.get("bad").is<double>()) bad = r.get("bad").get<double>();
                if (r.get("hex").is<std::string>()) hex = r.get("hex").get<std::string>();
            }
            byLv[(ratio > 0.10) ? 2 : ((ratio > 0.02) ? 1 : 0)]++;
            v.push_back(std::make_pair(std::make_pair(-ratio, -bad), hex));
        }
        std::sort(v.begin(), v.end());
        picojson::array top;
        for (size_t i = 0; i < v.size() && i < 3; ++i)
        {
            picojson::object o;
            o["hex"] = picojson::value(v[i].second);
            o["badRatioPct"] = picojson::value(-v[i].first.first * 100.0);
            o["badAircraft"] = picojson::value(-v[i].first.second);
            top.push_back(picojson::value(o));
        }
        picojson::object lv, r;
        lv["low"] = picojson::value((double)byLv[0]); lv["medium"] = picojson::value((double)byLv[1]);
        lv["high"] = picojson::value((double)byLv[2]);
        r["count"] = picojson::value((double)pts.size());
        r["byLevel"] = picojson::value(lv); r["topCells"] = picojson::value(top);
        return picojson::value(r).serialize();
    }
}

osg::Node* registerGpsjamFeed(osgViewer::Viewer& viewer, LayerManager* layers, earthai::ToolRegistry* tools)
{
    // 数据按日发布,URL 用 UTC 昨日、启动时算一次(FeedSpec.url 是静态字符串,框架
    // 不支持动态重算)。取舍:①跨 UTC 午夜的长跑进程会一直用启动日的"昨日"——数据
    // 本来就是日更,可接受;②昨日文件尚未发布时 404 → 框架打 "fetch failed" 日志优雅
    // 降级,refreshSeconds 到点自动重试,不做"再前一天"回退(register 是同步的,不能联网探测)。
    time_t y = time(NULL) - 86400;
    char dateBuf[16]; strftime(dateBuf, sizeof(dateBuf), "%Y-%m-%d", gmtime(&y));
    std::string dateStr = dateBuf;
    double dateUnix = (double)((y / 86400) * 86400);   // 该日 UTC 0 点(Unix 秒)
    earthfeed::FeedSpec spec;
    spec.id = "gpsjam"; spec.displayName = u8"GPS 干扰 / GPS Jamming";
    spec.group = u8"实时数据 / Live"; spec.subtitle = u8"数据来源 gpsjam.org(ADS-B)";
    spec.url = "https://gpsjam.org/data/" + dateStr + "-h3_4.csv";
    spec.refreshSeconds = 3600;   // 每日源,勤刷没意义;404(未发布)时兼作重试间隔
    spec.fixtureEnv = "EARTH_GPSJAM_FILE"; spec.forceEnv = "EARTH_GPSJAM";
    spec.parse = [dateStr, dateUnix](const std::string& b)
    { return earthfeed::parseGpsjam(b, dateStr, dateUnix); };
    spec.summaryJson = earthfeed::gpsjamSummaryJson;
    spec.toolDescriptionCn = u8"查询当前全球 GPS/GNSS 干扰汇总(gpsjam.org,源自 ADS-B 航迹质量,"
        u8"UTC 昨日全天):受干扰网格总数、按等级分桶(低/中/高,按坏航迹占比 2%/10% 分档)、"
        u8"坏占比最高的 TOP3 网格。只统计出现过坏航迹的 H3 网格;若图层尚未开启会自动开启并"
        u8"开始抓取,此时返回的 count 可能为 0,代表数据仍在加载中。";
    return earthfeed::registerFeedLayer(spec, viewer, layers, tools);
}
