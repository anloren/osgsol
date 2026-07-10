#ifndef EARTH_SAT_DATA_H
#define EARTH_SAT_DATA_H
#include <string>
#include <osg/Node>
#include <osgViewer/View>

enum class SatCategory { Station, Navigation, Weather, Starlink };

struct SatelliteInfo
{
    std::string name;
    int noradId = 0;
    SatCategory category = SatCategory::Station;
    double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
    bool valid = false;
};

class SatelliteLayer
{
public:
    virtual ~SatelliteLayer() {}
    // 空间站/导航星座/气象卫星/Starlink 四个独立开关(spec 要求,非二元"精选组+starlink")。
    virtual void setCategoryEnabled(SatCategory cat, bool on) = 0;
    virtual bool isCategoryEnabled(SatCategory cat) const = 0;
    // 点击拾取或程序化选中(Task 4 实现;本任务先占位签名)。
    virtual void selectByNoradId(int noradId) = 0;
    virtual void clearSelected() = 0;
    virtual SatelliteInfo getSelected() const = 0;
    // 供未来 P4 AI 工具复用的汇总统计,v1 先占位最小实现(同 flight_data.cpp 的先例)。
    virtual std::string summaryJson() const = 0;
    // 该类目已开启、且至少完整尝试过一次拉取,但一颗卫星都没拿到(网络失败/CelesTrak 限流/
    // 真的暂时没数据均可能)时返回一段用户可读的提示;正常情况(未开启/还在首次加载中/已有
    // 数据)返回空串。真机验收(2026-07-05)发现 Starlink 拉取被 CelesTrak 限流(403)时界面
    // 完全没有任何提示,和早前 AI 生图静默失败是同一类问题,这里补上可见反馈。
    virtual std::string fetchErrorText(SatCategory cat) const = 0;
};

extern osg::Node* configureSatelliteLayer(osgViewer::View& viewer, osg::Node* earthRoot,
                                          const std::string& mainFolder, SatelliteLayer** outLayer);
#endif
