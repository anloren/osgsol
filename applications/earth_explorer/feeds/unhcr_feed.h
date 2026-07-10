#ifndef EARTH_FEEDS_UNHCR_FEED_H
#define EARTH_FEEDS_UNHCR_FEED_H

#include <osgViewer/Viewer>
#include "../feed_layer.h"
#include "../LayerManager.h"
#include "../ai_tools.h"

// 注册 UNHCR 流离失所 O-D 弧线源(P1 Task 5,弧原语 FeedArc 的首个真实消费者):
// api.unhcr.org 年度难民统计(CC-BY),取去年 TOP 40 条 coo→coa 流画大圆弧
// (来源国蓝 → 收容国红),同时在收容国质心产出可拾取的点(弧不可拾取的补偿)。
// 国家 ISO3 → 质心查表见 country_centroids.h。
osg::Node* registerUnhcrFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                             earthai::ToolRegistry* tools);

// 暴露给单测(tests/feed_layer_tests.cpp #include unhcr_feed.cpp,同 gpsjam 先例):
// 两个纯函数被框架各调用一次、消费同一份 body,内部共享同一个解析核心
// (parseUnhcrFlows,static),保证 弧数 == 点数 == 有效流数。
namespace earthfeed
{
    std::vector<FeedPoint> parseUnhcrPoints(const std::string& body);
    FeedGeometry parseUnhcrGeometry(const std::string& body);
}

#endif
