#ifndef EARTH_FEED_LAYER_H
#define EARTH_FEED_LAYER_H

#include <functional>
#include <string>
#include <vector>
#include <osg/Node>
#include <osg/Group>
#include <osg/Vec2d>
#include <osg/Vec3d>
#include <osg/Vec4>
#include <osgViewer/Viewer>
#include <picojson.h>
#include "LayerManager.h"
#include "ai_tools.h"
#include "marker_style.h"

// FeedLayer:配置驱动的"世界数据源"框架。把 quake_data.cpp 里验证过的模式
// (抓取线程 + 主线程同步重建 GL_POINTS + LayerManager 注册 + AI 工具)抽成
// 一份 FeedSpec 配置,一次 registerFeedLayer() 调用即可接入一个新数据源。
namespace earthfeed
{
    // 单个数据点(地图上一个可点选的要素)。
    struct FeedPoint
    {
        double lat = 0, lon = 0;      // 度
        float sizePx = 8.0f;          // 点像素大小
        osg::Vec4 color = osg::Vec4(1, 1, 1, 1);
        std::string title, detail;    // 详情卡标题/正文(已格式化多行)
        std::string url;              // 可空:详情卡"打开"按钮的链接(如 USGS 事件页)
        double unixTime = 0;          // 可选:事件时间(Unix 秒),事件流卡(T8)排序用,0=无时间
        picojson::value raw;          // 原始记录(summaryJson 可用)
    };

    // O-D 大圆弧(UNHCR 等流向类数据)。顶点数学/渲染走 earthgeo::buildArcVertices
    // / buildArcGeometry(geo_primitives.h),不碰 globe 管线。
    struct FeedArc
    {
        osg::Vec3d llaA, llaB;        // (纬度°, 经度°, 高度m)——纬度在前,与 FeedPoint 一致
        osg::Vec4 colorA = osg::Vec4(1, 1, 1, 1), colorB = osg::Vec4(1, 1, 1, 1);
        double heightScale = 0.12;    // 传给 buildArcVertices(弧高 = scale × 大圆长)
    };

    // 球面折线(NHC 路径/锥边界、光缆等)。渲染走 earthgeo::buildPolylineGeometry。
    struct FeedLine
    {
        std::vector<osg::Vec2d> latLonDeg;   // (纬度°, 经度°) 路标序列
        osg::Vec4 color = osg::Vec4(1, 1, 1, 1);
        double liftMeters = 15000.0;  // 折线默认抬升(P0 结论:贴地线会被地形淹没)
    };

    // 一次抓取产出的全部线/弧几何(与 points 并列,主线程 sync 时整组重建)。
    struct FeedGeometry { std::vector<FeedArc> arcs; std::vector<FeedLine> lines; };

    // ===== P3:可选网格聚合 LOD(FIRMS 火点等高密度点源用)=====
    // 高密度点源(全球日常 1e4~1e5 点)绘制吞吐不是问题(Starlink ~7000 点全量渲染先例),
    // 问题是局部扎堆时的视觉可读性与拾取歧义。方案:经纬网格分桶,桶大小随相机高度分级,
    // 全部级别在抓取线程一次算好,主线程按高度切 nodeMask,不逐帧重算。
    struct FeedClusterLevel { double maxCameraAltKm = 0.0; double cellDeg = 0.0; };
    struct FeedClusterSpec
    {
        // 按 maxCameraAltKm 升序;cellDeg<=0 表示该级别显示原始点(不聚合)。
        std::vector<FeedClusterLevel> levels;
        // 可空:桶内成员 → 聚合点(位置字段由框架用质心覆盖,其余展示字段由回调决定);
        // 留空用缺省实现(标题含成员数,颜色/大小取最大成员并随计数放大)。
        std::function<FeedPoint(const std::vector<FeedPoint>& members,
                                double centroidLat, double centroidLon)> makeAggregate;
        bool empty() const { return levels.empty(); }
    };
    // 网格聚合纯函数(单测直测):cellDeg<=0 或空输入时原样返回;单点桶原样保留
    // (维持可拾取的原 detail);多点桶经 makeAggregate(空则缺省)合成一点,位置=桶内
    // 质心,unixTime=成员最大。经度先归一化到 [-180,180);格网不跨反经线环绕(反经线
    // 两侧相邻点各归各桶,不合并——全球尺度下 1° 的分缝可接受,是明确设计取舍)。
    std::vector<FeedPoint> clusterFeedPoints(const std::vector<FeedPoint>& pts, double cellDeg,
        const std::function<FeedPoint(const std::vector<FeedPoint>&, double, double)>& makeAggregate);

    // FeedGeometry → 渲染子图:全部 arcs 合入一个 earthgeo::buildArcGeometry 调用
    // (共享顶点数组、每弧独立 LINE_STRIP,弧顶点数 = arcVertexCount),lines 逐条
    // buildPolylineGeometry。纯场景图构造、不需 GL 上下文,tests/feed_layer_tests.cpp
    // 直接单测;空几何返回无子节点的空 Group。
    osg::Group* buildFeedGeometryGroup(const FeedGeometry& g, int arcVertexCount = 65);

    // 同上,但支持 StateSet 实例内复用(6a 性能债修复):arcSS/lineSS 为 in-out——
    // 传入无效则本次构建自建 StateSet/Program 并回填(懒创建),传入有效则新几何直接
    // 挂旧 StateSet,不再触发着色器 compile/link(每 sync 原本 1+L 次)。同一次调用内
    // 全部折线共享同一个 lineSS(FlowPhase 等 uniform 随之共享;feed 线恒 0=静态,无碍)。
    // 红线:两个 StateSet 只能由**同一 feed 实例**持有复用,禁止跨实例/全局共享
    // (4ee4c74d 已实证全局共享 Program 会引发全球瓦片非确定性染色)。
    osg::Group* buildFeedGeometryGroup(const FeedGeometry& g, int arcVertexCount,
                                       osg::ref_ptr<osg::StateSet>& arcSS,
                                       osg::ref_ptr<osg::StateSet>& lineSS);

    // 一个数据源的完整配置。
    struct FeedSpec
    {
        std::string id, displayName, group, subtitle;   // LayerManager 语义一致
        std::string url;                                 // 整条 URL,无占位符
        int refreshSeconds = 300;
        // 单次抓取的连接超时秒数(默认 15,与既有源零改动兼容)。响应体大/源慢的
        // 源(如切到 GKG GeoJSON 1.0 后 ~846KB/~24s 的 GDELT)应显式调大,否则会在
        // 数据传完之前被硬超时打断——见 gdelt_feed.cpp registerGdeltFeed 的 40s 设置。
        int timeoutSeconds = 15;
        std::string fixtureEnv;   // 如 "EARTH_GDACS_FILE":设置时读本地文件,不联网
        std::string forceEnv;     // 如 "EARTH_GDACS":非 "0" → 启动即开启该图层
        // T7 静态源默认数据文件(打包内绝对路径):url 为空且 fixtureEnv 未设时读它;
        // fixtureEnv 仍优先(测试可覆盖打包数据)。url 非空时本字段无效。
        std::string staticFile;
        std::function<std::vector<FeedPoint>(const std::string& body)> parse;
        // 可选:线/弧几何解析(不设 = 纯点源,现有源零改动)。与 parse 在同一抓取
        // 线程调用同一份 body,产出在主线程 sync 时经 buildFeedGeometryGroup 整组重建。
        std::function<FeedGeometry(const std::string& body)> parseGeometry;
        // 可空:留空则用默认实现(count + 按 title 首词分桶)。
        std::function<std::string(const std::vector<FeedPoint>&)> summaryJson;
        std::string toolDescriptionCn;   // get_<id>_summary 的中文描述
        // P3:可选聚合 LOD(不设 = 纯原始点,现有源零改动)。
        FeedClusterSpec cluster;
        // P3:标记抬升高度(米,相对椭球;地形矫正命中后改为相对真地表)。默认与既有
        // kFeedLiftMeters 一致;FIRMS 这类"点会落在高原山区"的源应设 >8849(珠峰椭球高),
        // 让可见性不依赖地形矫正(矫正是一次性的且瓦片未加载时静默失效——命中时更贴地,
        // 未命中时兜底也不会沉进地形)。
        double liftMeters = 3000.0;
    };

    // 当前跨全部 feed 的选中要素(全局单例式访问,file-scope static,仅主线程读写)。
    // EarthControlUI 用它画通用的"要素详情"右上角卡片,不必逐个 feed 加专属指针。
    // 语义:最后一次点击命中的点为准(last-click-wins);任一 feed 未命中不清空其它 feed
    // 已选中的点,由 EarthControlUI 的 [x] 关闭按钮显式调用 clearFeedSelection() 清除。
    struct FeedSelection
    {
        bool valid = false;
        std::string title, detail;
        std::string url;   // 可空:非空时通用详情卡多画一个"打开"按钮(system("open '<url>'"))
        // 来源 feed id(= FeedSpec.id,拾取时填):关闭一个 feed 图层时只清"来源==该层"
        // 的选中,别的 feed 的选中不受影响(Task 4 必修 B)。展示端(EarthControlUI)不读它。
        std::string sourceId;
    };
    FeedSelection currentFeedSelection();
    void clearFeedSelection();

    // ===== P4:区域简报(get_region_brief)基建 =====
    struct RegionBriefProvider
    {
        std::string id;                      // 与 LayerManager 图层 id 一致
        std::function<bool()> isEnabled;
        std::function<std::string(double lat, double lon, double radiusKm)> regionSummaryJson;
    };
    // 主线程 only(启动期注册、工具 execute=主线程查询),无锁。provider 持 FeedLayerImpl
    // 裸指针,生命周期=场景节点=会话期(与 get_<id>_summary 工具捕获 implPtr 同一先例)。
    std::vector<RegionBriefProvider>& regionBriefProviders();
    double haversineKm(double lat1, double lon1, double lat2, double lon2);

    // ===== T8 事件流卡 / 顶部状态带的只读框架接口 =====
    // 事件流卡的一行:某 FeedLayer 一个 FeedPoint 的展示快照(点击行 fly-to 用 lat/lon)。
    struct TickerEvent
    {
        std::string title, sourceId;   // sourceId = FeedSpec.id(行首源名标签)
        double lat = 0, lon = 0;       // 度
        double unixTime = 0;           // Unix 秒(排序键,恒 > 0)
    };
    // 跨全部已注册 FeedLayer 收集"最新事件":各 enabled 层的点按 unixTime 降序取 TOP n。
    // 线程契约(T8 评审修正):调用方是 ImGui 绘制回调(finalCamera POST_DRAW)——
    // viewer 默认 DrawThreadPerContext,即 **draw 线程**,与 UPDATE 线程的 syncIfDirty
    // 并发;内部经 snapshotRecords() 对每 feed 持 _mutex 整份拷贝记录,与其写入互斥。
    // unixTime==0 的点排除——无时间无法排序;GDELT/UNHCR/静态战略层等因此天然不出现
    // 在事件流,这是 unixTime 契约的自然结果,不是 bug。开销:~7 源 × ≤400 条的
    // 加锁拷贝+排序,毫秒内,每帧调用可忽略;真要省可加脏标记缓存,当前不值得复杂化。
    std::vector<TickerEvent> collectRecentEvents(size_t n);
    // 数据源健康统计(状态带 "n/m"):enabledCount=开启的 feed 层数,okCount=其中数据
    // 就绪的(静态源加载即恒 ok;轮询源以最近一轮抓取成败为准,尚未抓过 = 未成功)。
    // 只读两个 atomic 标志,draw 线程(同上)调用安全。
    void feedHealth(int& okCount, int& enabledCount);

    // 一站式接线:建层(点渲染 + 线/弧几何 + 拾取 + 详情数据)+ LayerManager 注册 +
    // env 钩子(fixtureEnv/forceEnv)+ AI 工具 get_<id>_summary 注册(tools 为空则跳过)。
    // 返回挂 sceneCamera 的节点,内部持有抓取线程(随返回节点析构 join)。
    // 静态源(T7):url 为空且(staticFile 非空 或 fixtureEnv 对应环境变量已设置)→
    // 注册时一次性读入本地文件(fixtureEnv 优先于 staticFile),不起轮询线程
    // (refreshSeconds 被忽略);数据在图层开启后首帧上屏。
    osg::Node* registerFeedLayer(const FeedSpec& spec, osgViewer::Viewer& viewer,
                                 LayerManager* layers, earthai::ToolRegistry* tools);
}

#endif
