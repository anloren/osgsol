#ifndef EARTH_FEEDS_USGS_QUAKES_FEED_H
#define EARTH_FEEDS_USGS_QUAKES_FEED_H

#include <osgViewer/Viewer>
#include "../feed_layer.h"
#include "../LayerManager.h"
#include "../ai_tools.h"

// 注册 USGS 实时地震源(从 quake_data.{h,cpp} 迁移到 FeedLayer 框架,T2 自证兼容):
// M2.5+、过去24h、无 key、HTTPS,约每分钟更新。图层 id/显示名/env 钩子语义与迁移前完全一致
// (EARTH_QUAKES 强制开、EARTH_QUAKES_FILE 离线 fixture)。
osg::Node* registerUsgsQuakesFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                                  earthai::ToolRegistry* tools);

// 暴露给单测(tests/ai_chat_tests.cpp #include usgs_quakes_feed.cpp,同 gdacs_feed.cpp 先例):
// 纯函数,喂 USGS GeoJSON FeatureCollection 字符串 → 解析出的点(含深度→颜色、震级→大小映射)。
namespace earthfeed { std::vector<FeedPoint> parseUsgsQuakes(const std::string& body); }

#endif
