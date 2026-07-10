#ifndef EARTH_FIRMS_FEED_H
#define EARTH_FIRMS_FEED_H
#include "../feed_layer.h"
// NASA FIRMS 活跃火点(VIIRS S-NPP 近实时,全球最近 24h)。
// key 注册(免费自动发放):https://firms.modaps.eosdis.nasa.gov/api/map_key/
// → 运行前 export EARTH_FIRMS_KEY=<key>;未配置时图层保留但不联网,subtitle 提示。
// 限额 5000 次/10 分钟,我们 30 分钟拉一次,远碰不到。
osg::Node* registerFirmsFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                             earthai::ToolRegistry* tools);

// 暴露给单测(tests/feed_layer_tests.cpp #include firms_feed.cpp,同 gdacs_feed.cpp 先例):
// 纯函数,喂 FIRMS CSV 字符串 → 解析出的点。
namespace earthfeed { std::vector<FeedPoint> parseFirmsCsv(const std::string& body); }
#endif
