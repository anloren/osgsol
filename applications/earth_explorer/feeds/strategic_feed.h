#ifndef EARTH_FEEDS_STRATEGIC_FEED_H
#define EARTH_FEEDS_STRATEGIC_FEED_H

#include <osgViewer/Viewer>
#include "../feed_layer.h"
#include "../LayerManager.h"
#include "../ai_tools.h"

// 注册静态战略数据集五层(P1 Task 7):军事基地/战略港口/核电站/航天发射场/AI 数据中心。
// 全部走 FeedLayer 静态源模式(url 空 + staticFile 指向打包内数据):注册时一次性读
// <mainFolder>/Earth/strategic/<name>.json,不联网、不轮询。数据 license 逐文件审计过
// (见各 JSON 顶层 _source/_license 字段),subtitle 常显来源署名。
// 返回挂 sceneCamera 的组节点(内含 5 个 feed 子节点)。
osg::Node* registerStrategicFeeds(osgViewer::Viewer& viewer, LayerManager* layers,
                                  earthai::ToolRegistry* tools, const std::string& mainFolder);

// 暴露给单测(tests/feed_layer_tests.cpp #include strategic_feed.cpp,同 gdacs 先例):
namespace earthfeed
{
    // 每个数据集只差样式(点色/大小),解析逻辑完全共用(统一 schema:
    // { _source, _license, _fetched, items:[{name,lat,lon,country,info}] })。
    struct StrategicStyle { osg::Vec4 color; float sizePx; };
    std::vector<FeedPoint> parseStrategic(const std::string& body, const StrategicStyle& style);
    // count + 按国家 TOP5 分桶(静态名录类数据比默认"title 首词分桶"更有意义)。
    std::string strategicSummaryJson(const std::vector<FeedPoint>& pts);
}

#endif
