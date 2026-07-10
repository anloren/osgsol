#ifndef EARTH_FEEDS_GDELT_FEED_H
#define EARTH_FEEDS_GDELT_FEED_H

#include <osgViewer/Viewer>
#include "../feed_layer.h"
#include "../LayerManager.h"
#include "../ai_tools.h"

// 注册 GDELT 新闻热点源(P1 Task 3,2026-07-09 切到 GKG GeoJSON 1.0,见 .cpp 文件头
// 切源记录):无 key GeoJSON,近 TIMESPAN 分钟全球新闻报道的地理聚合点,热度黄(低)→
// 红(高)渐变;提及数 <5 的低置信地点被过滤。
osg::Node* registerGdeltFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                             earthai::ToolRegistry* tools);

// 暴露给单测(tests/feed_layer_tests.cpp #include gdelt_feed.cpp,同 gdacs/eonet 先例):
// 纯函数,喂 GDELT GKG GeoJSON 字符串(可含裸控制字符,内部先清洗再解析)→ 解析出的
// 点(properties.sumtotalmentions ≥5 才收)。
namespace earthfeed { std::vector<FeedPoint> parseGdelt(const std::string& body); }

#endif
