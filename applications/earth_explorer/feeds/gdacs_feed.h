#ifndef EARTH_FEEDS_GDACS_FEED_H
#define EARTH_FEEDS_GDACS_FEED_H

#include <osgViewer/Viewer>
#include "../feed_layer.h"
#include "../LayerManager.h"
#include "../ai_tools.h"

// 注册 GDACS 灾害预警源(worldmonitor 盘点 #2):无 key JSON,红/橙分级。
osg::Node* registerGdacsFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                             earthai::ToolRegistry* tools);

// 暴露给单测(tests/ai_chat_tests.cpp #include gdacs_feed.cpp,同 ai_chat.cpp 先例):
// 纯函数,喂 GDACS GeoJSON FeatureCollection 字符串 → 解析出的点。Green 级别被过滤掉。
namespace earthfeed { std::vector<FeedPoint> parseGdacs(const std::string& body); }

#endif
