#ifndef EARTH_FEEDS_GPSJAM_FEED_H
#define EARTH_FEEDS_GPSJAM_FEED_H

#include <osgViewer/Viewer>
#include "../feed_layer.h"
#include "../LayerManager.h"
#include "../ai_tools.h"

// 注册 GPSJam GPS 干扰源(P1 Task 4):gpsjam.org 每日 H3 res4 网格 CSV
// (源自 ADS-B 航迹质量统计),坏航迹占比分级 低(黄)/中(橙)/高(红);
// H3 cell id → 中心点解码见 h3_lite.h(实测响应无坐标,只有 cell id)。
osg::Node* registerGpsjamFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                              earthai::ToolRegistry* tools);

// 暴露给单测(tests/feed_layer_tests.cpp #include gpsjam_feed.cpp,同 gdelt 先例):
// 纯函数,喂 gpsjam 每日 CSV(明文或 gzip 原样字节)→ 解析出的点(坏航迹 ≥1 才收)。
// dateStr/dateUnix 为数据日期(register 启动时算的 UTC 昨日),进 detail/unixTime。
namespace earthfeed
{
    std::vector<FeedPoint> parseGpsjam(const std::string& body,
                                       const std::string& dateStr, double dateUnix);
}

#endif
