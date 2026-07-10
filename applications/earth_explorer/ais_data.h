#ifndef EARTH_AIS_DATA_H
#define EARTH_AIS_DATA_H
#include <string>
#include <osg/Node>
#include <osgViewer/View>

struct ShipInfo
{
    std::string name;
    long long mmsi = 0;
    double lat = 0, lon = 0, sogKn = 0, cogDeg = 0;
    double ageSec = 0;   // 距最近一次收到该船位置的秒数
    bool valid = false;
};

class ShipLayer
{
public:
    virtual ~ShipLayer() {}
    virtual void setEnabled(bool on) = 0;
    virtual bool isEnabled() const = 0;
    // 主线程每帧:视口 bbox + 相机高度(高空闸门/订阅重连决策都在 worker 侧消费)。
    virtual void setViewState(double latMin, double lonMin,
                              double latMax, double lonMax, double camAltM) = 0;
    virtual ShipInfo getSelected() const = 0;
    virtual void clearSelected() = 0;
    virtual std::string summaryJson() const = 0;
    // 图层目录 subtitle 用的状态文案(未配置 key / 视野过大 / 连接中 / 已连接 N 艘 /
    // 连接失败重试中);主线程读,内部原子状态,不加锁。
    virtual std::string statusText() const = 0;
};

extern osg::Node* configureShipLayer(osgViewer::View& viewer, ShipLayer** outLayer);
#endif
