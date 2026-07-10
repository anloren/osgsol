#ifndef EARTH_FEEDS_EONET_FEED_H
#define EARTH_FEEDS_EONET_FEED_H

#include <osgViewer/Viewer>
#include "../feed_layer.h"
#include "../LayerManager.h"
#include "../ai_tools.h"

// 注册 NASA EONET 自然事件源(P1 Task 2):无 key JSON,13 类事件点(色表见 .cpp)。
osg::Node* registerEonetFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                             earthai::ToolRegistry* tools);

// 暴露给单测(tests/feed_layer_tests.cpp #include eonet_feed.cpp,同 gdacs 先例):
// 纯函数,喂 EONET v3 events JSON 字符串 → 解析出的点。earthquakes 类被排除
// (与 USGS quakes 层重复),wildfires 只保留最新 geometry 在 48h 内的。
namespace earthfeed { std::vector<FeedPoint> parseEonet(const std::string& body); }

#endif
