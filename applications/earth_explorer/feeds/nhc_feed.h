#ifndef EARTH_FEEDS_NHC_FEED_H
#define EARTH_FEEDS_NHC_FEED_H

#include <osgViewer/Viewer>
#include "../feed_layer.h"
#include "../LayerManager.h"
#include "../ai_tools.h"

// 注册 NOAA NHC 飓风源(P1 Task 6,FeedLine 折线原语的首个真实消费者):
// 大西洋/东太平洋/中太平洋活动风暴的 当前位置点(按强度配色,可拾取)+
// 预报路径折线(白)+ 预报锥外环边界线(浅黄;v1 不做面填充,P0 既定降级)。
// 飓风季外常态 0 要素(N=0 合法);API 实测细节见 nhc_feed.cpp 头注释。
osg::Node* registerNhcFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                           earthai::ToolRegistry* tools);

// 暴露给单测(tests/feed_layer_tests.cpp #include nhc_feed.cpp,同 unhcr 先例):
// parseNhcPoints 吃主 URL(Forecast Points 层)的 body;parseNhcGeometry 不消费
// 入参 body(路径/锥在另外两层),内部经 EARTH_NHC_FILE 兄弟文件或补拉两层取数。
namespace earthfeed
{
    std::vector<FeedPoint> parseNhcPoints(const std::string& body);
    FeedGeometry parseNhcGeometry(const std::string& body);
}

#endif
