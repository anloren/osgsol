// tests/feed_layer_tests.cpp — FeedLayer 框架 / geo_primitives / LayerManager 预设 /
// FeedSelection 单测(P1 Task 1:从 ai_chat_tests.cpp 整体迁出,治"杂烩";
// ai_chat_tests.cpp 只留 AI 块)。新增覆盖:FeedGeometry(线/弧)几何构建与
// sync 接线、静态源(空 url + fixtureEnv)一次性加载。
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <fstream>
#include <sstream>
#include <picojson.h>
#include "../applications/earth_explorer/earth_config.h"

// Release 构建带 -DNDEBUG 会吞掉 assert —— 用自定义 CHECK 保证断言永远生效
#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)

// 沿用 ai_chat_tests.cpp 的先例:NEW_TEST 宏只接受单个源文件,被测实现文件直接
// #include 进本测试的翻译单元(与 ai_chat_tests 是两个独立链接单元,不会重定义)。
// feed_layer.cpp:FeedLayerImpl/registerFeedLayer/buildFeedGeometryGroup;
// gdacs_feed.cpp / usgs_quakes_feed.cpp:parseGdacs/parseUsgsQuakes 等纯函数;
// geo_primitives.cpp:buildArcVertices/buildPolylineVertices 顶点数学。
#include "../applications/earth_explorer/feed_layer.cpp"
#include "../applications/earth_explorer/feeds/gdacs_feed.cpp"
#include "../applications/earth_explorer/feeds/usgs_quakes_feed.cpp"
#include "../applications/earth_explorer/feeds/eonet_feed.cpp"
#include "../applications/earth_explorer/feeds/gdelt_feed.cpp"
#include "../applications/earth_explorer/feeds/gpsjam_feed.cpp"
#include "../applications/earth_explorer/feeds/unhcr_feed.cpp"
#include "../applications/earth_explorer/feeds/nhc_feed.cpp"
#include "../applications/earth_explorer/feeds/strategic_feed.cpp"
#include "../applications/earth_explorer/feeds/firms_feed.cpp"
#include "../applications/earth_explorer/earth_config.cpp"   // firms_feed.cpp 用 earthcfg::resolveKey
#include "../applications/earth_explorer/geo_primitives.cpp"
#include "../applications/earth_explorer/ui_card.h"
#include "../applications/earth_explorer/marker_style.cpp"
#include "../applications/earth_explorer/overlay_lod_badge.h"
#include <readerwriter/TileCallback.h>

#ifdef _WIN32
static void setEnvVar(const char* k, const char* v) { _putenv_s(k, v); }
static void unsetEnvVar(const char* k) { _putenv_s(k, ""); }
#else
static void setEnvVar(const char* k, const char* v) { setenv(k, v, 1); }
static void unsetEnvVar(const char* k) { unsetenv(k); }
#endif

// ===== P3 Task 1:网格聚合纯函数 =====
static earthfeed::FeedPoint mkPt(double lat, double lon, float sizePx = 8.0f)
{
    earthfeed::FeedPoint p; p.lat = lat; p.lon = lon; p.sizePx = sizePx;
    p.title = "pt"; p.unixTime = 100.0; return p;
}
static void testClusterFeedPoints()
{
    using earthfeed::FeedPoint; using earthfeed::clusterFeedPoints;
    std::function<FeedPoint(const std::vector<FeedPoint>&, double, double)> noAgg;
    // 空输入 → 空输出
    CHECK(clusterFeedPoints(std::vector<FeedPoint>(), 1.0, noAgg).empty());
    // cellDeg<=0 → 原样返回(原始点级别)
    { std::vector<FeedPoint> in; in.push_back(mkPt(10, 20));
      CHECK(clusterFeedPoints(in, 0.0, noAgg).size() == 1); }
    // 单点桶:原点原样保留(title 不变,可拾取原 detail)
    { std::vector<FeedPoint> in; in.push_back(mkPt(10.2, 20.3));
      std::vector<FeedPoint> out = clusterFeedPoints(in, 1.0, noAgg);
      CHECK(out.size() == 1); CHECK(out[0].title == "pt");
      CHECK(std::fabs(out[0].lat - 10.2) < 1e-9); }
    // 同桶多点 → 合成一个聚合点,位置=质心,unixTime=成员最大
    { std::vector<FeedPoint> in;
      in.push_back(mkPt(10.1, 20.1)); in.push_back(mkPt(10.3, 20.3));
      in.back().unixTime = 200.0;
      std::vector<FeedPoint> out = clusterFeedPoints(in, 1.0, noAgg);
      CHECK(out.size() == 1);
      CHECK(std::fabs(out[0].lat - 10.2) < 1e-6);
      CHECK(std::fabs(out[0].lon - 20.2) < 1e-6);
      CHECK(out[0].unixTime == 200.0); }
    // 跨桶不合并:相邻两个 1° 桶各留一点
    { std::vector<FeedPoint> in;
      in.push_back(mkPt(10.5, 20.5)); in.push_back(mkPt(10.5, 21.5));
      CHECK(clusterFeedPoints(in, 1.0, noAgg).size() == 2); }
    // 反经线两侧(179.9E / 179.9W)在不同桶,各自保留(设计取舍:格网不环绕)
    { std::vector<FeedPoint> in;
      in.push_back(mkPt(0.0, 179.9)); in.push_back(mkPt(0.0, -179.9));
      CHECK(clusterFeedPoints(in, 1.0, noAgg).size() == 2); }
    // 极区纬度钳制不崩:lat=90 正常入桶
    { std::vector<FeedPoint> in; in.push_back(mkPt(90.0, 0.0)); in.push_back(mkPt(89.95, 0.0));
      CHECK(clusterFeedPoints(in, 1.0, noAgg).size() == 1); }
    // lon 越界输入(200°)归一化后与 -160° 同桶
    { std::vector<FeedPoint> in; in.push_back(mkPt(0.0, 200.0)); in.push_back(mkPt(0.0, -159.9));
      CHECK(clusterFeedPoints(in, 1.0, noAgg).size() == 1); }
    // 自定义 makeAggregate 被调用且收到全部成员
    { std::vector<FeedPoint> in;
      in.push_back(mkPt(10.1, 20.1)); in.push_back(mkPt(10.2, 20.2)); in.push_back(mkPt(10.3, 20.3));
      std::function<FeedPoint(const std::vector<FeedPoint>&, double, double)> agg =
          [](const std::vector<FeedPoint>& ms, double cLat, double cLon) {
              FeedPoint a; a.lat = cLat; a.lon = cLon;
              a.title = "agg-" + std::to_string(ms.size()); return a; };
      std::vector<FeedPoint> out = clusterFeedPoints(in, 1.0, agg);
      CHECK(out.size() == 1); CHECK(out[0].title == "agg-3"); }
    // 缺省聚合文案含成员数
    { std::vector<FeedPoint> in;
      in.push_back(mkPt(10.1, 20.1)); in.push_back(mkPt(10.2, 20.2));
      std::vector<FeedPoint> out = clusterFeedPoints(in, 1.0, noAgg);
      CHECK(out.size() == 1);
      CHECK(out[0].title.find("2") != std::string::npos); }
    std::cout << "[OK] clusterFeedPoints\n";
}

// ===== P3 Task 2:聚合 LOD 接入 FeedLayerImpl =====
static void testClusterLodIntegration()
{
    using namespace earthfeed;
    // 构造 12 个点的 fixture:10 个挤在 (10±0.3, 20±0.3) 的 1° 桶里,2 个远离
    FeedSpec spec; spec.id = "clustertest";
    FeedClusterLevel l1; l1.maxCameraAltKm = 300.0;  l1.cellDeg = 0.0;  spec.cluster.levels.push_back(l1);
    FeedClusterLevel l2; l2.maxCameraAltKm = 1500.0; l2.cellDeg = 0.25; spec.cluster.levels.push_back(l2);
    FeedClusterLevel l3; l3.maxCameraAltKm = 1.0e9;  l3.cellDeg = 1.0;  spec.cluster.levels.push_back(l3);
    osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec);
    osg::ref_ptr<osg::Group> root = impl->buildScene();
    FeedSnapshot snap;
    for (int i = 0; i < 10; ++i)
    {
        FeedRecord r; r.pt = mkPt(10.0 + 0.06 * i, 20.0 + 0.06 * i);
        r.ecef = osg::Vec3d(1, 0, 0); snap.records.push_back(r);
    }
    FeedRecord far1; far1.pt = mkPt(-30, 100); far1.ecef = osg::Vec3d(0, 1, 0); snap.records.push_back(far1);
    FeedRecord far2; far2.pt = mkPt(50, -60);  far2.ecef = osg::Vec3d(0, 0, 1); snap.records.push_back(far2);
    impl->buildClusterLevels(snap);                     // 抓取线程侧的预聚合入口(单测直调)
    CHECK(snap.levels.size() == 3);
    CHECK(snap.levels[0].empty());                      // cellDeg<=0 级别复用 records,不重复存
    CHECK(snap.levels[2].size() == 3);                  // 1° 级:10 点并 1 桶 + 2 远点 = 3
    impl->postSnapshot(snap);
    impl->syncIfDirty();
    CHECK(impl->levelGeodeCount() == 3);                // 每级一个 Geode
    impl->updateActiveLevel(100.0);  CHECK(impl->activeLevelIndex() == 0);
    impl->updateActiveLevel(800.0);  CHECK(impl->activeLevelIndex() == 1);
    impl->updateActiveLevel(20000.0); CHECK(impl->activeLevelIndex() == 2);
    std::cout << "[OK] cluster LOD integration\n";
}
static void testNoUrlNoFixtureFeedSkipsThread()
{
    using namespace earthfeed;
    unsetEnvVar("EARTH_NOURL_FILE");
    FeedSpec spec; spec.id = "nourl"; spec.fixtureEnv = "EARTH_NOURL_FILE";  // url 空、无 staticFile
    osgViewer::Viewer viewer;
    osg::ref_ptr<osg::Node> node = registerFeedLayer(spec, viewer, nullptr, nullptr);
    FeedLayerImpl* impl = static_cast<FeedLayerImpl*>(node->getUserData());
    CHECK(!impl->hasFetchThread());                     // 不起对空 url 干轮询的线程
    std::cout << "[OK] no-url feed skips fetch thread\n";
}

// ===== 任务A:抓取失败三态(fetchState()/lastErrorText())=====
// 场景(真机验收报的问题):GDELT 等上游死链 404 时,旧行为只是 std::cout 打一行、
// _lastFetchOk 悄悄置 false,UI 完全看不出来,用户以为 app 坏了。修法是给 FeedLayerImpl
// 加三态(0=未抓/idle,1=ok,2=失败),失败态附带可读错误文案,供 EarthControlUI 在图层
// 目录里画"加载中…/⚠ 抓取失败"。这里直接单测 fetchOnce() 的状态转移(不依赖网络):
// - 失败分支用"fixtureEnv 指向不存在的文件"触发(ifstream 打开失败,同旧有
//   fixture-open-failed 分支),文案应含"打开失败"字样、非空。
// - 成功分支用正常 fixture 文件,parse 被调用且状态回到 1、错误文案清空。
static void testFetchStateTriState()
{
    using namespace earthfeed;
    // ---- 失败:fixtureEnv 指向不存在的文件 ----
    {
        const char* kMissing = "fetchstate_missing_fixture_tmp.json";
        std::remove(kMissing);   // 确保真不存在(触发 ifstream 打开失败,非网络失败)
        setEnvVar("EARTH_FETCHSTATE_MISSING_FILE", kMissing);
        FeedSpec spec; spec.id = "fetchstatefail"; spec.fixtureEnv = "EARTH_FETCHSTATE_MISSING_FILE";
        osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec);
        CHECK(impl->fetchState() == 0);                  // 构造后、任何 fetchOnce 之前:idle
        CHECK(impl->lastErrorText().empty());
        FeedSnapshot snap = impl->fetchOnce();
        CHECK(!impl->lastFetchOk());                     // 既有两态字段仍照旧置 false(零回归)
        CHECK(impl->fetchState() == 2);                  // 新三态:确诊为"失败"而非"未抓"
        CHECK(impl->lastErrorText().find(u8"打开失败") != std::string::npos);
        CHECK(snap.records.empty());                      // 失败快照仍应无记录(既有行为)
        unsetEnvVar("EARTH_FETCHSTATE_MISSING_FILE");
    }
    // ---- 成功:正常 fixture 文件,parse 被调用 ----
    {
        const char* kFix = "fetchstate_ok_fixture_tmp.json";
        { std::ofstream f(kFix); f << "OK-BODY"; }
        setEnvVar("EARTH_FETCHSTATE_OK_FILE", kFix);
        int parseCalls = 0;
        FeedSpec spec; spec.id = "fetchstateok"; spec.fixtureEnv = "EARTH_FETCHSTATE_OK_FILE";
        spec.parse = [&](const std::string& body)
        { parseCalls++; CHECK(body == "OK-BODY"); return std::vector<FeedPoint>(); };
        osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec);
        impl->fetchOnce();
        CHECK(parseCalls == 1);
        CHECK(impl->lastFetchOk());
        CHECK(impl->fetchState() == 1);                  // 成功态
        CHECK(impl->lastErrorText().empty());            // 成功不残留错误文案
        unsetEnvVar("EARTH_FETCHSTATE_OK_FILE"); std::remove(kFix);
    }
    std::cout << "[OK] fetchState tri-state (task A)\n";
}

// ===== 任务A:registerFeedLayer 接线——OverlayLayer::fetchStatus 端到端 =====
// 只单测 fetchOnce() 状态机不够——真正的 bug 面是"UI 读的 OverlayLayer::fetchStatus
// 有没有正确接到 FeedLayerImpl"。这里走生产入口 registerFeedLayer(与 EarthControlUI
// 读同一个 OverlayLayer),验证 l.fetchStatus(err) 返回值与 lastErrorText 一致。
// fixtureEnv 一旦设置,isStaticSource()==true,registerFeedLayer 内部会同步跑一次
// fetchOnce()(静态源加载路径),不需要等轮询线程,状态确定性可测。
static void testFetchStatusWiring()
{
    using namespace earthfeed;
    const char* kMissing = "fetchwire_missing_fixture_tmp.json";
    std::remove(kMissing);
    setEnvVar("EARTH_FETCHWIRE_FILE", kMissing);
    FeedSpec spec; spec.id = "fetchwiretest"; spec.displayName = "FetchWireTest";
    spec.group = "test"; spec.fixtureEnv = "EARTH_FETCHWIRE_FILE";
    osgViewer::Viewer viewer;
    LayerManager lm;
    osg::ref_ptr<osg::Node> node = registerFeedLayer(spec, viewer, &lm, nullptr);
    OverlayLayer* l = lm.find("fetchwiretest");
    CHECK(l != nullptr);
    CHECK((bool)l->fetchStatus);              // registerFeedLayer 已接线,不是空回调
    std::string err;
    int st = l->fetchStatus(err);
    CHECK(st == 2);                           // 文件不存在 → 静态源加载时同步 fetchOnce 走失败分支
    CHECK(!err.empty());
    FeedLayerImpl* impl = static_cast<FeedLayerImpl*>(node->asGroup()->getUserData());
    CHECK(err == impl->lastErrorText());      // fetchStatus 出参与 impl 内部文案一致
    unsetEnvVar("EARTH_FETCHWIRE_FILE");
    std::cout << "[OK] fetchStatus wiring via registerFeedLayer (task A)\n";
}

// ===== P3 Task 3:FIRMS CSV 解析 =====
static void testParseFirmsCsv()
{
    std::ifstream in("../../applications/earth_explorer/test/firms_fixture.csv");
    // 测试运行目录不定,读不到就用仓库绝对路径再试(与其它 fixture 用例同款兜底)
    std::string body;
    if (in) { std::stringstream ss; ss << in.rdbuf(); body = ss.str(); }
    else
    {
        std::ifstream in2("/Users/USER/osgverse/applications/earth_explorer/test/firms_fixture.csv");
        CHECK(in2); std::stringstream ss; ss << in2.rdbuf(); body = ss.str();
    }
    std::vector<earthfeed::FeedPoint> pts = earthfeed::parseFirmsCsv(body);
    CHECK(pts.size() == 13);                     // 14 数据行 - 1 畸形行(缺 frp 的按 0 处理保留)
    CHECK(std::fabs(pts[0].lat - 25.05) < 1e-9);
    CHECK(std::fabs(pts[0].lon - 100.05) < 1e-9);
    CHECK(pts[0].unixTime > 0);                  // 2026-07-05 03:42 UTC
    // FRP 分档颜色:3.2 黄 / 8.5 橙 / 25.7 红
    CHECK(pts[0].color.g() > 0.8f);              // 黄
    CHECK(pts[2].color.g() < 0.5f);              // 红(g 分量低)
    CHECK(pts[1].sizePx > pts[0].sizePx);        // 橙档比黄档大
    CHECK(pts[2].sizePx > pts[1].sizePx);        // 红档最大
    CHECK(pts[0].title.find(u8"火点") != std::string::npos);
    std::cout << "[OK] parseFirmsCsv\n";
}

// ===== P3 T3 修复:FeedSpec::liftMeters 覆盖(高原地形可见性修复)=====
// 验证 fetchOnce()/buildClusterLevels() 两条 ecef 计算路径都读 _spec.liftMeters(而非
// 硬编码 kFeedLiftMeters):FIRMS 配置 liftMeters=9000 时,原始点与聚合点的 ecef 都应
// 等于 convertLLAtoECEF(lat, lon, 9000)——与生产代码同一函数,精确相等(非近似)。
// 同时钉死默认 spec(未设 liftMeters)的零回归契约:仍产出与旧 kFeedLiftMeters=3000
// 完全一致的 ecef,证明 P1 既有源(未触碰 liftMeters 字段)行为零变化。
static void testPerSpecLiftMeters()
{
    using namespace earthfeed;
    auto lla2ecef = [](double latDeg, double lonDeg, double altM) {
        return osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            osg::DegreesToRadians(latDeg), osg::DegreesToRadians(lonDeg), altM));
    };

    // ---- 自定义 liftMeters=9000(模拟 FIRMS 配置):原始点 + 聚合点 ecef 均按 9000 抬升 ----
    {
        FeedSpec spec; spec.id = "lifttest"; spec.liftMeters = 9000.0;
        FeedClusterLevel raw; raw.maxCameraAltKm = 300.0;  raw.cellDeg = 0.0;
        FeedClusterLevel top; top.maxCameraAltKm = 1.0e9;  top.cellDeg = 1.0;
        spec.cluster.levels.push_back(raw); spec.cluster.levels.push_back(top);

        // 云南高原纬经(与 firms_fixture.csv 同量级):两点同桶(1° 网格)触发聚合路径。
        std::vector<FeedPoint> pts;
        pts.push_back(mkPt(25.05, 100.05)); pts.push_back(mkPt(25.10, 100.10));

        // Step 1 核实结论:buildClusterLevels() 只读 snap.records[i].pt 生成聚合级别
        // (snap.levels),从不写 snap.records[i].ecef——"原始点" ecef 唯一的生产写入点
        // 是 fetchOnce() 里那句 `r.ecef = ...(_spec.liftMeters)`(见 feed_layer.cpp 221-224)。
        // fetchOnce() 并非只有网络路径:空 url + fixtureEnv 已设即走静态源分支读本地文件,
        // 无需 viewer——照 testRegionSummary(364 行起)现行驱动链,parse 忽略文件内容、
        // 直接返回上面构造的 pts,真正驱动生产的原始点 ecef 计算路径(非测试自行用
        // spec.liftMeters 重算的同义反复)。
        const char* kLiftFix = "lift_fixture_tmp.json";
        { std::ofstream f(kLiftFix); f << "{}"; }
        setEnvVar("EARTH_LIFTTEST_FILE", kLiftFix);
        spec.fixtureEnv = "EARTH_LIFTTEST_FILE";
        spec.parse = [pts](const std::string&) { return pts; };

        osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec);   // 无 viewer:地形矫正跳过
        impl->buildScene();
        FeedSnapshot snap = impl->fetchOnce();   // 生产入口:records[].ecef + levels 均生产产出
        unsetEnvVar("EARTH_LIFTTEST_FILE"); std::remove(kLiftFix);

        // 原始点级别(level0,cellDeg<=0):snap.levels[0] 为空(显示时复用 records),
        // 断言生产代码 fetchOnce() 实际算出的 records[].ecef(而非重算期望值的同义反复)。
        osg::Vec3d expectRaw0 = lla2ecef(25.05, 100.05, 9000.0);
        osg::Vec3d expectRaw1 = lla2ecef(25.10, 100.10, 9000.0);
        CHECK(snap.records.size() == 2);
        CHECK((snap.records[0].ecef - expectRaw0).length() < 1e-6);
        CHECK((snap.records[1].ecef - expectRaw1).length() < 1e-6);
        // 原始点确实用了 9000(而非硬编码 3000)反证:若 fetchOnce() 回退硬编码 3000,
        // 这条断言会失败(两者相差 6000m,远超浮点误差)。
        osg::Vec3d rawWrongIf3000 = lla2ecef(25.05, 100.05, 3000.0);
        CHECK((snap.records[0].ecef - rawWrongIf3000).length() > 1000.0);

        CHECK(snap.levels.size() == 2);
        CHECK(snap.levels[0].empty());          // level0(原始级)不重复存
        CHECK(snap.levels[1].size() == 1);      // level1(1° 聚合):两点同桶 → 1 个聚合点
        osg::Vec3d expectAgg = lla2ecef(snap.levels[1][0].pt.lat, snap.levels[1][0].pt.lon, 9000.0);
        CHECK((snap.levels[1][0].ecef - expectAgg).length() < 1e-6);
        // 聚合点确实用了 9000(而非默认 3000)——用一个明显更大的高度反证:若代码仍
        // 硬编码 3000,这条断言会失败(两者相差 6000m,远超浮点误差)。
        osg::Vec3d wrongIfHardcoded3000 = lla2ecef(snap.levels[1][0].pt.lat, snap.levels[1][0].pt.lon, 3000.0);
        CHECK((snap.levels[1][0].ecef - wrongIfHardcoded3000).length() > 1000.0);
    }

    // ---- 默认 spec(不设 liftMeters):零回归——仍等于旧 kFeedLiftMeters=3000 路径 ----
    {
        FeedSpec spec; spec.id = "defaultlifttest";   // liftMeters 用头文件默认值 3000.0
        CHECK(std::fabs(spec.liftMeters - 3000.0) < 1e-9);
        FeedClusterLevel top; top.maxCameraAltKm = 1.0e9; top.cellDeg = 1.0;
        spec.cluster.levels.push_back(top);

        osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec);
        impl->buildScene();
        std::vector<FeedPoint> pts;
        pts.push_back(mkPt(25.05, 100.05)); pts.push_back(mkPt(25.10, 100.10));
        FeedSnapshot snap;
        for (size_t i = 0; i < pts.size(); ++i)
        { FeedRecord r; r.pt = pts[i]; snap.records.push_back(r); }
        impl->buildClusterLevels(snap);
        CHECK(snap.levels.size() == 1 && snap.levels[0].size() == 1);
        osg::Vec3d expectAggDefault = lla2ecef(snap.levels[0][0].pt.lat, snap.levels[0][0].pt.lon, 3000.0);
        CHECK((snap.levels[0][0].ecef - expectAggDefault).length() < 1e-6);
    }
    std::cout << "[OK] per-spec liftMeters override (P3 T3 fix)\n";
}

// ===== 终审 C-1:失败快照的空 levels 不得让 syncIfDirty/pickAt 越界 =====
// 复现旧 bug 的构造方式:先走一轮正常快照(3 级聚类,让 _activeLevel 落在
// cellDeg>0 的级别、_levelGeodes 建好),再直接构造一个"旧失败路径"会产出的
// FeedSnapshot——records/levels 均为空、且不经过 buildClusterLevels()(模拟修复前
// fetchOnce() 的 `if (!ok) return out;` 早退)——喂给 syncIfDirty()。
// 断言:不崩 + syncIfDirty 的防线二把 levelGeodeCount() 重新对齐回 3(每级仍建一个
// Geode,只是内容为空),而不是让 for 循环在 _levelRecords[li] 处越界。
// 同时钉死根因修复本身:对空 records 调用 buildClusterLevels() 必须产出
// levels.size()==3、每级皆空(与 spec.cluster.levels 对齐)。
static void testClusterEmptyFailureSnapshotSafe()
{
    using namespace earthfeed;
    FeedSpec spec; spec.id = "clusterfailtest";
    FeedClusterLevel l1; l1.maxCameraAltKm = 300.0;  l1.cellDeg = 0.0;  spec.cluster.levels.push_back(l1);
    FeedClusterLevel l2; l2.maxCameraAltKm = 1500.0; l2.cellDeg = 0.25; spec.cluster.levels.push_back(l2);
    FeedClusterLevel l3; l3.maxCameraAltKm = 1.0e9;  l3.cellDeg = 1.0;  spec.cluster.levels.push_back(l3);
    osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec);
    osg::ref_ptr<osg::Group> root = impl->buildScene();

    // 第一步:正常快照(几个点),sync 后 _activeLevel 应落在某个 cellDeg>0 的级别
    // (200000km 远远超过 l2/l3 的 maxCameraAltKm,应选中最后一级 index=2)。
    FeedSnapshot good;
    for (int i = 0; i < 5; ++i)
    {
        FeedRecord r; r.pt = mkPt(10.0 + 0.1 * i, 20.0 + 0.1 * i);
        r.ecef = osg::Vec3d(1, 0, 0); good.records.push_back(r);
    }
    impl->buildClusterLevels(good);
    CHECK(good.levels.size() == 3);
    impl->postSnapshot(good);
    impl->syncIfDirty();
    CHECK(impl->levelGeodeCount() == 3);
    impl->updateActiveLevel(20000.0);
    CHECK(impl->activeLevelIndex() == 2);   // cellDeg=1.0 的聚合级别,非原始点级

    // 第二步:直接构造"旧失败路径"的空快照——不经过 buildClusterLevels(),
    // records/levels 都保持默认构造的空 vector,模拟修复前的 bug 现场。
    FeedSnapshot brokenEmpty;   // records.empty() && levels.empty()
    CHECK(brokenEmpty.records.empty());
    CHECK(brokenEmpty.levels.empty());
    impl->postSnapshot(brokenEmpty);
    impl->syncIfDirty();        // 修复前:_levelRecords[li] (li=1,2) 越界 → UB/崩溃
    // 防线二生效:levelGeodeCount 仍等于 spec.cluster.levels.size()(每级都建了
    // Geode,只是用了兜底空 vector 的内容),没有越界、没有崩溃。
    CHECK(impl->levelGeodeCount() == 3);

    // pickAt 同款越界场景:_activeLevel 还停留在上一份好快照选的 index=2,
    // 而本份快照的 _levelRecords 只有 0 个元素——pickAt 的范围检查必须让它
    // 安全地跳过聚合级、不解引用越界下标。这里只验证不崩(recs 为空时
    // pickAt 内部 `!_enabled` 或 recs.empty() 会提前返回)。
    osg::ref_ptr<osg::Camera> cam = new osg::Camera;
    cam->setViewport(0, 0, 800, 600);
    impl->setEnabled(true);
    impl->pickAt(cam.get(), 400.0f, 300.0f);   // 不崩即通过

    // 根因修复钉死:对空 records 直接调用 buildClusterLevels() 必须产出与
    // spec.cluster.levels 对齐的 levels(每级一个空 vector),这正是 fetchOnce()
    // 失败路径应该做的事——不再是早退返回一个空 out。
    FeedSnapshot pinned;
    CHECK(pinned.records.empty());
    impl->buildClusterLevels(pinned);
    CHECK(pinned.levels.size() == 3);
    CHECK(pinned.levels[0].empty());
    CHECK(pinned.levels[1].empty());
    CHECK(pinned.levels[2].empty());

    std::cout << "[OK] cluster empty-failure snapshot safe (P3 final-review C-1)\n";
}

// ===== marker-system Task 1:visualForLayer 单一真源 =====
static void testVisualForLayer()
{
    using namespace earthmark;
    CHECK(visualForLayer("ais").shape == MarkerShape::Ship);
    CHECK(visualForLayer("ships").shape == MarkerShape::Ship);       // 面板 id 与标记 id 同视觉
    CHECK(visualForLayer("flights").shape == MarkerShape::Arrow);
    CHECK(visualForLayer("fires").shape == MarkerShape::StarBurst);
    CHECK(visualForLayer("quakes").shape == MarkerShape::Ring);
    CHECK(visualForLayer("gdacs").shape == MarkerShape::WarnTriangle);
    CHECK(visualForLayer("gdelt").shape == MarkerShape::Diamond);
    CHECK(visualForLayer("gpsjam").shape == MarkerShape::Hexagon);
    CHECK(visualForLayer("eonet").shape == MarkerShape::Teardrop);
    CHECK(visualForLayer("unhcr").shape == MarkerShape::Circle);
    CHECK(visualForLayer("nhc").shape == MarkerShape::Ring);
    CHECK(visualForLayer("satstations").shape == MarkerShape::SatBox);
    CHECK(visualForLayer("starlink").shape == MarkerShape::SatBox);
    // 战略各源:真实 id 见 feeds/strategic_feed.cpp kDatasets[](bases/ports/nuclear/
    // spaceports/datacenters——brief 猜测的 "cables" 实际不存在,"spaceports" 是 brief 未列出的真实 id)。
    CHECK(visualForLayer("bases").shape == MarkerShape::Square);
    CHECK(visualForLayer("ports").shape == MarkerShape::Square);
    CHECK(visualForLayer("nuclear").shape == MarkerShape::Square);
    CHECK(visualForLayer("spaceports").shape == MarkerShape::Square);
    CHECK(visualForLayer("datacenters").shape == MarkerShape::Square);
    MarkerVisual s = visualForLayer("ais");
    CHECK(std::fabs(s.color.x() - 0.208f) < 0.01f && std::fabs(s.color.y() - 0.878f) < 0.01f);
    CHECK(visualForLayer("nonexistent").shape == MarkerShape::Circle);   // 未登记回退
    CHECK(std::string(markerShapeGLSL()).find("markerCoverage") != std::string::npos);
    std::cout << "[OK] visualForLayer\n";
}

// ===== P4 Task 5:haversine + 区域摘要 =====
static void testHaversineAndRegionSummary()
{
    using namespace earthfeed;
    // 北京(39.9,116.4)—上海(31.2,121.5)大圆距约 1067km,容差 2%
    double d = haversineKm(39.9, 116.4, 31.2, 121.5);
    CHECK(d > 1045.0 && d < 1090.0);
    CHECK(haversineKm(10.0, 20.0, 10.0, 20.0) < 1e-6);
    // 区域摘要:半径内点入选、半径外排除、按距离升序、封顶 8 条
    FeedSpec spec; spec.id = "regiontest";
    std::vector<FeedPoint> pts;
    pts.push_back(mkPt(10.0, 20.0));  pts.back().title = "near0";
    pts.push_back(mkPt(10.5, 20.0));  pts.back().title = "near55";   // ~56km
    pts.push_back(mkPt(30.0, 120.0)); pts.back().title = "far";
    for (int i = 0; i < 10; ++i)      // 凑满溢出,验证 nearest 封顶 8
    { pts.push_back(mkPt(10.0 + 0.01 * (i + 1), 20.0)); pts.back().title = "pad"; }
    // 灌数据:静态源路径(空 url + fixtureEnv 已设 → 一次性加载),parse 忽略文件内容,
    // 直接返回上面构造的 pts;照 testStaticSourceLoad 现行驱动链:fetchOnce()+postSnapshot()
    // 再 syncIfDirty() 模拟首帧 update,让 _records 就位。
    const char* kRFix = "region_fixture_tmp.json";
    { std::ofstream f(kRFix); f << "{}"; }
    setEnvVar("EARTH_REGIONTEST_FILE", kRFix);
    spec.fixtureEnv = "EARTH_REGIONTEST_FILE";
    spec.parse = [pts](const std::string&) { return pts; };
    osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec);
    impl->buildScene();   // syncIfDirty() 内部无条件操作 _root(见既有全部用例先例),必须先建场景图
    impl->postSnapshot(impl->fetchOnce());
    impl->syncIfDirty();
    // 灌入后:
    picojson::value v;
    CHECK(picojson::parse(v, impl->regionSummaryJson(10.0, 20.0, 100.0)).empty());
    CHECK(v.get("count").get<double>() == 12.0);            // near0+near55+pad×10
    const picojson::array& nearest = v.get("nearest").get<picojson::array>();
    CHECK(nearest.size() == 8);                             // 封顶
    CHECK(nearest[0].get("title").get<std::string>() == "near0");   // 距离升序
    CHECK(nearest[0].get("distanceKm").get<double>() < 1.0);
    // 半径外:far 不出现在 count 里(12 而非 13 已隐含);半径极小 → count=1
    picojson::value v2;
    CHECK(picojson::parse(v2, impl->regionSummaryJson(10.0, 20.0, 0.5)).empty());
    CHECK(v2.get("count").get<double>() == 1.0);
    unsetEnvVar("EARTH_REGIONTEST_FILE"); std::remove(kRFix);
    std::cout << "[OK] haversine + regionSummaryJson\n";
}

// ===== P6a Task 8:mercatorTileBBox =====
static void testMercatorTileBBox()
{
    const double kM = 20037508.342789244, kEps = 1.0;   // 米级容差
    osg::Vec4d b0 = earthgeo::mercatorTileBBox(0, 0, 0);   // z0 全球
    CHECK(std::fabs(b0[0] + kM) < kEps && std::fabs(b0[1] + kM) < kEps);
    CHECK(std::fabs(b0[2] - kM) < kEps && std::fabs(b0[3] - kM) < kEps);
    // z1:(0,0)=西北象限(x∈[-M,0], y∈[0,M]);(1,1)=东南象限
    osg::Vec4d nw = earthgeo::mercatorTileBBox(0, 0, 1);
    CHECK(std::fabs(nw[0] + kM) < kEps && std::fabs(nw[2]) < kEps);
    CHECK(std::fabs(nw[1]) < kEps && std::fabs(nw[3] - kM) < kEps);
    osg::Vec4d se = earthgeo::mercatorTileBBox(1, 1, 1);
    CHECK(std::fabs(se[0]) < kEps && std::fabs(se[2] - kM) < kEps);
    CHECK(std::fabs(se[1] + kM) < kEps && std::fabs(se[3]) < kEps);
    // 尺寸单调:z2 瓦片宽 = 全球/4
    osg::Vec4d q = earthgeo::mercatorTileBBox(2, 1, 2);
    CHECK(std::fabs((q[2] - q[0]) - kM * 2.0 / 4.0) < kEps);
    std::cout << "[OK] mercatorTileBBox\n";
}

int main(int, char**)
{
    // 端点直转 ECEF 的辅助(与实现共用同一 convertLLAtoECEF/WGS84):
    // 各几何断言都用"与直转结果米级一致"表达,不依赖实现内部近似。
    auto lla2ecefT = [](double latDeg, double lonDeg, double altM) {
        return osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            osg::DegreesToRadians(latDeg), osg::DegreesToRadians(lonDeg), altM));
    };

    // ---- buildFeedGeometryGroup:2 弧 + 1 线的 FeedGeometry → 渲染子图(T1 Step 1 ①)----
    // 断言 buildArcVertices/buildPolylineVertices 被正确参数化:顶点数 = arcVertexCount、
    // 首尾顶点与 convertLLAtoECEF 直转 1m 内一致(顶点数组是 float,ECEF 量级下量化 ~0.5m)。
    {
        using namespace earthfeed;
        FeedGeometry g;
        FeedArc a1; a1.llaA = osg::Vec3d(39.9042, 116.4074, 10000.0);   // 北京
        a1.llaB = osg::Vec3d(48.8566, 2.3522, 10000.0);                 // 巴黎
        a1.colorA = osg::Vec4(1.0f, 0.8f, 0.2f, 1.0f);
        a1.colorB = osg::Vec4(0.2f, 0.9f, 1.0f, 1.0f);
        a1.heightScale = 0.12;
        FeedArc a2; a2.llaA = osg::Vec3d(31.2304, 121.4737, 10000.0);   // 上海
        a2.llaB = osg::Vec3d(-33.8688, 151.2093, 10000.0);              // 悉尼
        g.arcs.push_back(a1); g.arcs.push_back(a2);
        FeedLine l1;
        l1.latLonDeg.push_back(osg::Vec2d(35.6762, 139.7649));   // 东京
        l1.latLonDeg.push_back(osg::Vec2d(22.3027, 114.1772));   // 香港
        l1.latLonDeg.push_back(osg::Vec2d(1.3521, 103.8198));    // 新加坡
        l1.color = osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f);
        l1.liftMeters = 15000.0;
        g.lines.push_back(l1);

        const int kN = 65;
        osg::ref_ptr<osg::Group> grp = earthfeed::buildFeedGeometryGroup(g, kN);
        CHECK(grp.valid());
        CHECK(grp->getNumChildren() == 1);
        osg::Geode* geode = grp->getChild(0)->asGeode();
        CHECK(geode != nullptr);
        CHECK(geode->getNumDrawables() == 2);   // drawable0 = 全部弧合一个 Geometry,drawable1 = 折线

        // 弧 Geometry:共享顶点数组(2 × kN)、每弧独立 LINE_STRIP primitiveset
        osg::Geometry* arcGeom = geode->getDrawable(0)->asGeometry();
        CHECK(arcGeom != nullptr);
        osg::Vec3Array* av = static_cast<osg::Vec3Array*>(arcGeom->getVertexArray());
        CHECK(av != nullptr);
        CHECK((int)av->size() == 2 * kN);
        CHECK(arcGeom->getNumPrimitiveSets() == 2);
        for (int s = 0; s < 2; ++s)
        {
            osg::DrawArrays* da = dynamic_cast<osg::DrawArrays*>(arcGeom->getPrimitiveSet(s));
            CHECK(da != nullptr);
            CHECK(da->getMode() == GL_LINE_STRIP);
            CHECK(da->getFirst() == s * kN);
            CHECK(da->getCount() == kN);
        }
        // 首尾 ECEF 与 convertLLAtoECEF 直转一致(<1m)
        CHECK((osg::Vec3d((*av)[0]) - lla2ecefT(39.9042, 116.4074, 10000.0)).length() < 1.0);
        CHECK((osg::Vec3d((*av)[kN - 1]) - lla2ecefT(48.8566, 2.3522, 10000.0)).length() < 1.0);
        CHECK((osg::Vec3d((*av)[kN]) - lla2ecefT(31.2304, 121.4737, 10000.0)).length() < 1.0);
        CHECK((osg::Vec3d((*av)[2 * kN - 1]) - lla2ecefT(-33.8688, 151.2093, 10000.0)).length() < 1.0);
        // 弧 1 中点隆起:heightScale=0.12 × 大圆长(京→巴黎 ~8200km)≈ 1e6m,留余量断言
        CHECK(osg::Vec3d((*av)[kN / 2]).length()
              > lla2ecefT(39.9042, 116.4074, 10000.0).length() + 5.0e5);
        // 顶点色:O→D 渐变的两端等于 colorA/colorB
        osg::Vec4Array* ac = static_cast<osg::Vec4Array*>(arcGeom->getColorArray());
        CHECK(ac != nullptr && ac->size() == av->size());
        CHECK(((*ac)[0] - a1.colorA).length() < 1e-4f);
        CHECK(((*ac)[kN - 1] - a1.colorB).length() < 1e-4f);

        // 折线 Geometry:单独顶点数组(细分 > 路标数)、1 个 LINE_STRIP、抬升 15km 生效
        osg::Geometry* lineGeom = geode->getDrawable(1)->asGeometry();
        CHECK(lineGeom != nullptr);
        osg::Vec3Array* lv = static_cast<osg::Vec3Array*>(lineGeom->getVertexArray());
        CHECK(lv != nullptr);
        CHECK(lv->size() > l1.latLonDeg.size());   // 每段沿大圆细分过
        CHECK(lineGeom->getNumPrimitiveSets() == 1);
        osg::DrawArrays* lda = dynamic_cast<osg::DrawArrays*>(lineGeom->getPrimitiveSet(0));
        CHECK(lda != nullptr && lda->getMode() == GL_LINE_STRIP
              && lda->getCount() == (GLsizei)lv->size());
        CHECK((osg::Vec3d(lv->front()) - lla2ecefT(35.6762, 139.7649, 15000.0)).length() < 1.0);
        CHECK((osg::Vec3d(lv->back()) - lla2ecefT(1.3521, 103.8198, 15000.0)).length() < 1.0);

        // 默认 arcVertexCount=65(评审补测):不带 count 参数调用,弧 Geometry 的共享
        // 顶点数组 = 2 弧 × 65(折线是独立 Geometry/独立数组,不掺在弧数组里)。
        osg::ref_ptr<osg::Group> defGrp = earthfeed::buildFeedGeometryGroup(g);
        CHECK(defGrp.valid() && defGrp->getNumChildren() == 1);
        osg::Geode* defGeode = defGrp->getChild(0)->asGeode();
        CHECK(defGeode != nullptr && defGeode->getNumDrawables() == 2);
        osg::Geometry* defArcGeom = defGeode->getDrawable(0)->asGeometry();
        CHECK(defArcGeom != nullptr);
        osg::Vec3Array* dav = static_cast<osg::Vec3Array*>(defArcGeom->getVertexArray());
        CHECK(dav != nullptr && (int)dav->size() == 2 * 65);

        // 空几何:返回无子节点的空 Group(sync 侧据此不挂空壳)
        osg::ref_ptr<osg::Group> empty = earthfeed::buildFeedGeometryGroup(earthfeed::FeedGeometry());
        CHECK(empty.valid() && empty->getNumChildren() == 0);
        std::cout << "earthfeed::buildFeedGeometryGroup tests OK\n";
    }

    // ---- FeedLayerImpl sync 接线:parseGeometry 设/不设 两条路径(T1 Step 1 ①②)----
    // 本翻译单元已 #include feed_layer.cpp,可直接构造匿名命名空间里的 FeedLayerImpl
    // (buildScene 只建场景图/着色器对象,不需 GL 上下文,不起抓取线程)。
    {
        using namespace earthfeed;
        // 临时 fixture 文件(内容任意,parse/parseGeometry 由测试 lambda 决定产出)
        const char* kFixPath = "feed_layer_tests_fixture.tmp";
        { std::ofstream out(kFixPath); out << "BODY"; }
        setEnvVar("FEED_TEST_GEOM_FILE", kFixPath);

        // a) parseGeometry 已设:sync 后 root = points Geode + 几何 Group,整组替换不累积
        FeedSpec spec; spec.id = "geomtest"; spec.fixtureEnv = "FEED_TEST_GEOM_FILE";
        int parseCalls = 0, geomCalls = 0;
        bool emitEmptyGeometry = false;   // 置 true 后 parseGeometry 产出空几何(非空→空过渡用)
        spec.parse = [&](const std::string& body) {
            parseCalls++;
            CHECK(body == "BODY");
            std::vector<FeedPoint> pts(1);
            pts[0].lat = 22.3; pts[0].lon = 114.2; pts[0].title = "P";
            return pts;
        };
        spec.parseGeometry = [&](const std::string& body) {
            geomCalls++;
            CHECK(body == "BODY");
            FeedGeometry fg;
            if (emitEmptyGeometry) return fg;
            // 弧终点随调用次数变化(第 2 轮起纬度 10°→15°):供 6a 断言"StateSet 复用
            // 的同时,顶点数据确实按新快照更新了"。
            FeedArc a; a.llaA = osg::Vec3d(0.0, 0.0, 0.0);
            a.llaB = osg::Vec3d(geomCalls > 1 ? 15.0 : 10.0, 10.0, 0.0);
            fg.arcs.push_back(a); fg.arcs.push_back(a);
            FeedLine l; l.latLonDeg.push_back(osg::Vec2d(0.0, 0.0));
            l.latLonDeg.push_back(osg::Vec2d(5.0, 5.0));
            fg.lines.push_back(l);
            return fg;
        };
        osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec);
        osg::Group* root = impl->buildScene();
        CHECK(root->getNumChildren() == 0);
        impl->postSnapshot(impl->fetchOnce());
        impl->syncIfDirty();
        CHECK(parseCalls == 1 && geomCalls == 1);
        CHECK(impl->pointsOnly().size() == 1);
        CHECK(root->getNumChildren() == 2);   // [0] points Geode,[1] 几何 Group
        osg::Group* geomGrp = root->getChild(1)->asGroup();
        CHECK(geomGrp != nullptr);
        CHECK(geomGrp->getNumChildren() == 1);
        osg::Geode* gd = geomGrp->getChild(0)->asGeode();
        // 2 条弧合入 1 个 Geometry,1 条线 1 个 Geometry,共 2 个 drawable
        CHECK(gd != nullptr && gd->getNumDrawables() == 2);
        // 6a:记录首轮几何的 StateSet(弧=drawable0、线=drawable1,懒创建于本轮)
        osg::StateSet* arcSS1 = gd->getDrawable(0)->getStateSet();
        osg::StateSet* lineSS1 = gd->getDrawable(1)->getStateSet();
        CHECK(arcSS1 != nullptr && lineSS1 != nullptr && arcSS1 != lineSS1);
        // 持引用防 ABA:不然旧 Geode 释放后新 Geode 可能落在同一地址,gd2 != gd 失去意义
        osg::ref_ptr<osg::Geode> gdHold = gd;
        // 再 sync 一轮:整组替换,子节点数不累积
        impl->postSnapshot(impl->fetchOnce());
        impl->syncIfDirty();
        CHECK(root->getNumChildren() == 2);
        // 6a:StateSet 实例内复用——新一轮重建出的是**新 Geometry**(整组替换),但其
        // StateSet 指针与上一轮相同(不再触发着色器 compile/link);顶点数据已按第 2 轮
        // 快照更新(弧终点纬度 10°→15°)。
        {
            osg::Geode* gd2 = root->getChild(1)->asGroup()->getChild(0)->asGeode();
            CHECK(gd2 != nullptr && gd2 != gd);   // 几何确实整组重建了
            osg::Geometry* arcGeom2 = gd2->getDrawable(0)->asGeometry();
            CHECK(arcGeom2->getStateSet() == arcSS1);            // 弧 StateSet 复用
            CHECK(gd2->getDrawable(1)->getStateSet() == lineSS1); // 线 StateSet 复用
            osg::Vec3Array* av2 = static_cast<osg::Vec3Array*>(arcGeom2->getVertexArray());
            CHECK(av2 != nullptr && av2->size() == 2 * 65);
            CHECK((osg::Vec3d((*av2)[64]) - lla2ecefT(15.0, 10.0, 0.0)).length() < 1.0);
        }

        // 非空→空几何过渡(评审补测):后续快照几何为空时,几何 Group 必须被摘掉,
        // root 回到 points-only(而不是留着上一轮的旧线/弧,也不崩)。
        emitEmptyGeometry = true;
        impl->postSnapshot(impl->fetchOnce());
        impl->syncIfDirty();
        CHECK(root->getNumChildren() == 1);   // 只剩 points Geode
        CHECK(root->getChild(0)->asGeode() != nullptr);
        CHECK(impl->pointsOnly().size() == 1);   // 点不受几何清空影响
        emitEmptyGeometry = false;
        // 6a:空几何过渡后再来一轮非空快照——StateSet 仍是最初那两个(成员持有,
        // 不随几何 Group 摘除而丢失)。
        impl->postSnapshot(impl->fetchOnce());
        impl->syncIfDirty();
        {
            osg::Geode* gd3 = root->getChild(1)->asGroup()->getChild(0)->asGeode();
            CHECK(gd3 != nullptr);
            CHECK(gd3->getDrawable(0)->getStateSet() == arcSS1);
            CHECK(gd3->getDrawable(1)->getStateSet() == lineSS1);
        }

        // 6a:同一次构建内多条折线共享同一个 line StateSet(1+L 编译债里的 L 消掉);
        // 无弧时 arcSS 保持无效(不白造);二次调用同一对 in-out StateSet → 直接挂用。
        {
            FeedGeometry gl;
            FeedLine lw; lw.latLonDeg.push_back(osg::Vec2d(0.0, 0.0));
            lw.latLonDeg.push_back(osg::Vec2d(3.0, 3.0));
            gl.lines.push_back(lw); gl.lines.push_back(lw); gl.lines.push_back(lw);
            osg::ref_ptr<osg::StateSet> aSS, lSS;
            osg::ref_ptr<osg::Group> g1 = buildFeedGeometryGroup(gl, 65, aSS, lSS);
            CHECK(!aSS.valid());   // 没有弧 → 不懒创建弧 StateSet
            CHECK(lSS.valid());
            osg::Geode* lg = g1->getChild(0)->asGeode();
            CHECK(lg != nullptr && lg->getNumDrawables() == 3);
            for (unsigned int d = 0; d < 3; ++d)
                CHECK(lg->getDrawable(d)->getStateSet() == lSS.get());
            osg::ref_ptr<osg::Group> g2 = buildFeedGeometryGroup(gl, 65, aSS, lSS);
            CHECK(g2->getChild(0)->asGeode()->getDrawable(0)->getStateSet() == lSS.get());
        }

        // b) parseGeometry 未设:行为与现状完全一致——root 只有 points Geode,无几何 Group
        FeedSpec plain; plain.id = "plaintest"; plain.fixtureEnv = "FEED_TEST_GEOM_FILE";
        plain.parse = [&](const std::string&) {
            std::vector<FeedPoint> pts(2);
            pts[0].lat = 1.0; pts[0].lon = 2.0; pts[1].lat = 3.0; pts[1].lon = 4.0;
            return pts;
        };
        osg::ref_ptr<FeedLayerImpl> plainImpl = new FeedLayerImpl(plain);
        osg::Group* plainRoot = plainImpl->buildScene();
        plainImpl->postSnapshot(plainImpl->fetchOnce());
        plainImpl->syncIfDirty();
        CHECK(plainImpl->pointsOnly().size() == 2);
        CHECK(plainRoot->getNumChildren() == 1);   // 只有 points Geode

        unsetEnvVar("FEED_TEST_GEOM_FILE");
        std::remove(kFixPath);
        std::cout << "FeedLayerImpl geometry sync tests OK\n";
    }

    // ---- 静态源(T7 预留):url 空 + fixtureEnv 已设 → 注册时一次性加载,不起轮询线程 ----
    {
        using namespace earthfeed;
        const char* kFixPath = "feed_layer_tests_static_fixture.tmp";
        { std::ofstream out(kFixPath); out << "STATIC"; }
        setEnvVar("FEED_TEST_STATIC_FILE", kFixPath);

        int parseCalls = 0;
        FeedSpec spec; spec.id = "statictest";
        spec.url = "";                            // 空 url
        spec.fixtureEnv = "FEED_TEST_STATIC_FILE";
        spec.refreshSeconds = 1;                  // 静态源应忽略之(不轮询)
        spec.parse = [&](const std::string& body) {
            parseCalls++;
            CHECK(body == "STATIC");
            std::vector<FeedPoint> pts(3);
            for (int i = 0; i < 3; ++i) { pts[i].lat = i; pts[i].lon = i; }
            return pts;
        };
        osgViewer::Viewer viewer;   // 不 realize,无 GL;registerFeedLayer 只挂 handler
        osg::ref_ptr<osg::Node> node = registerFeedLayer(spec, viewer, nullptr, nullptr);
        CHECK(node.valid());
        osg::Group* root = node->asGroup();
        CHECK(root != nullptr);
        FeedLayerImpl* impl = static_cast<FeedLayerImpl*>(root->getUserData());
        CHECK(impl != nullptr);
        CHECK(!impl->hasFetchThread());          // 静态源:没有轮询线程
        CHECK(parseCalls == 1);                  // 注册时已一次性读入 fixture
        impl->syncIfDirty();                     // 模拟首帧 update
        CHECK(impl->pointsOnly().size() == 3);

        // 对照:url 非空的普通源照旧起轮询线程(行为零变化;默认关闭 → 线程空转不联网)
        FeedSpec net; net.id = "nettest"; net.url = "http://127.0.0.1:1/never";
        net.parse = [](const std::string&) { return std::vector<FeedPoint>(); };
        osg::ref_ptr<osg::Node> netNode = registerFeedLayer(net, viewer, nullptr, nullptr);
        FeedLayerImpl* netImpl = static_cast<FeedLayerImpl*>(netNode->asGroup()->getUserData());
        CHECK(netImpl->hasFetchThread());

        unsetEnvVar("FEED_TEST_STATIC_FILE");
        std::remove(kFixPath);
        std::cout << "static source (empty url + fixtureEnv) tests OK\n";
    }

    // ---- earthfeed::parseGdacs(Task 1,纯函数,#include gdacs_feed.cpp)----
    {
        // 3 个 Feature,对应 2 个"事件":
        //   1) Red EQ(美国)的 Point_Centroid —— 应保留
        //   2) 同一条 Red EQ 事件的 Point_area 重复几何(T2 复审carry-forward新增)—— 应被
        //      Class 过滤丢弃,验证不会把同一事件算两次
        //   3) Green FL(美国)的 Point_Centroid —— 应被 alertlevel 过滤掉(非紧急)
        const char* canned =
            "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[114.17,22.28]},"
            "\"properties\":{\"eventtype\":\"EQ\",\"alertlevel\":\"Red\",\"country\":\"United States\","
            "\"Class\":\"Point_Centroid\",\"fromdate\":\"2026-06-29T11:35:33\","
            "\"severitydata\":{\"severitytext\":\"Magnitude 6.5M, Depth:10km\"}}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[114.20,22.30]},"
            "\"properties\":{\"eventtype\":\"EQ\",\"alertlevel\":\"Red\",\"country\":\"United States\","
            "\"Class\":\"Point_area\",\"fromdate\":\"2026-06-29T11:35:33\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[-84.5,38.05]},"
            "\"properties\":{\"eventtype\":\"FL\",\"alertlevel\":\"Green\",\"country\":\"United States\","
            "\"Class\":\"Point_Centroid\",\"fromdate\":\"2026-05-19T01:00:00\"}}"
            "]}";
        std::vector<earthfeed::FeedPoint> pts = earthfeed::parseGdacs(canned);
        CHECK(pts.size() == 1);   // Point_area 重复几何 + Green 都被过滤,只剩 1 条 Red Point_Centroid
        CHECK(std::abs(pts[0].lon - 114.17) < 1e-6);
        CHECK(std::abs(pts[0].lat - 22.28) < 1e-6);
        CHECK(std::abs(pts[0].color.r() - 1.0f) < 1e-6);   // Red -> (1, 0.25, 0.2, 1)
        CHECK(std::abs(pts[0].color.g() - 0.25f) < 1e-6);
        CHECK(pts[0].title.find("United States") != std::string::npos);   // title 含国家
        // fromdate "2026-06-29T11:35:33"(UTC)→ Unix 秒(T9 补齐,事件流卡排序用;
        // 注意是秒不是毫秒——1782732933 是 python calendar.timegm 钉死的值)
        CHECK(pts[0].unixTime == 1782732933.0);

        // 默认/专属 summaryJson:count 正确 + byAlertLevel/byEventType 分桶
        std::string sj = earthfeed::gdacsSummaryJson(pts);
        picojson::value sv; std::string serr = picojson::parse(sv, sj);
        CHECK(serr.empty());
        CHECK(sv.get("count").get<double>() == 1.0);
        CHECK(sv.get("byAlertLevel").get("Red").get<double>() == 1.0);
        CHECK(sv.get("byEventType").get("EQ").get<double>() == 1.0);
        CHECK(earthfeed::parseGdacs("not json at all").empty());
    CHECK(earthfeed::parseUsgsQuakes("garbage {{{").empty());
    std::cout << "earthfeed::parseGdacs tests OK\n";
    }

    // ---- earthfeed::parseUsgsQuakes / quakesSummaryJson(T2,纯函数,#include usgs_quakes_feed.cpp)----
    {
        // 3 条地震:M2.5(浅源,<70km) / M4.2(中源,70-300km) / M6.1(深源,>300km),
        // 覆盖震级→大小、深度→颜色的三个分段。
        const char* canned =
            "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[-119.8,39.5,5.0]},"
            "\"properties\":{\"mag\":2.5,\"place\":\"Small Test near Reno\",\"time\":1718900000000,"
            "\"url\":\"https://earthquake.usgs.gov/sq1\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[121.56,25.03,120.0]},"
            "\"properties\":{\"mag\":4.2,\"place\":\"Mid Test near Taipei\",\"time\":1718890000000,"
            "\"url\":\"https://earthquake.usgs.gov/sq2\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[120.98,14.6,400.0]},"
            "\"properties\":{\"mag\":6.1,\"place\":\"Large Test near Manila\",\"time\":1718880000000,"
            "\"url\":\"https://earthquake.usgs.gov/sq3\"}}"
            "]}";
        std::vector<earthfeed::FeedPoint> pts = earthfeed::parseUsgsQuakes(canned);
        CHECK(pts.size() == 3);
        CHECK(std::abs(pts[0].lon - (-119.8)) < 1e-6);
        CHECK(std::abs(pts[0].lat - 39.5) < 1e-6);
        CHECK(pts[0].url == "https://earthquake.usgs.gov/sq1");
        CHECK(pts[0].title.find("Reno") != std::string::npos);
        CHECK(pts[0].detail.find(u8"深度") != std::string::npos);

        // 深度→颜色:浅源(5km)红、中源(120km)橙黄、深源(400km)蓝(照搬 depthColor 三分段)
        CHECK(pts[0].color.r() > 0.8f && pts[0].color.g() < 0.3f);          // 浅:红
        CHECK(pts[1].color.r() > 0.9f && pts[1].color.g() > 0.5f);          // 中:橙黄
        CHECK(pts[2].color.b() > 0.8f && pts[2].color.r() < 0.3f);          // 深:蓝

        // 震级→大小:严格单调递增(mag 2.5 < 4.2 < 6.1)
        CHECK(pts[0].sizePx < pts[1].sizePx);
        CHECK(pts[1].sizePx < pts[2].sizePx);

        // 汇总统计:count/maxMag(+place)/震级直方图/字段名与迁移前(quake_data.cpp)完全一致
        std::string sj = earthfeed::quakesSummaryJson(pts);
        picojson::value sv; std::string serr = picojson::parse(sv, sj);
        CHECK(serr.empty());
        CHECK(sv.get("count").get<double>() == 3.0);
        CHECK(std::abs(sv.get("maxMag").get<double>() - 6.1) < 1e-6);
        CHECK(sv.get("maxMagPlace").to_str().find("Manila") != std::string::npos);
        picojson::value hist = sv.get("magHistogram");
        CHECK(hist.get("<3").get<double>() == 1.0);    // M2.5
        CHECK(hist.get("3-4").get<double>() == 0.0);
        CHECK(hist.get("4-5").get<double>() == 1.0);    // M4.2
        CHECK(hist.get("5-6").get<double>() == 0.0);
        CHECK(hist.get(">=6").get<double>() == 1.0);    // M6.1

        // 空输入:count=0 且直方图全零(不是缺字段/崩溃)
        std::string sjEmpty = earthfeed::quakesSummaryJson(std::vector<earthfeed::FeedPoint>());
        picojson::value svEmpty; CHECK(picojson::parse(svEmpty, sjEmpty).empty());
        CHECK(svEmpty.get("count").get<double>() == 0.0);
        CHECK(svEmpty.get("maxMag").get<double>() == 0.0);
        std::cout << "earthfeed::parseUsgsQuakes tests OK\n";
    }

    // ---- earthfeed::parseEonet / eonetSummaryJson(P1 Task 2,纯函数,#include eonet_feed.cpp)----
    {
        // 5 个事件覆盖全部过滤/映射规则:
        //   1) volcanoes,2 条 geometry 且"最新"在前 —— 保留,须按日期取最新(而非取数组末尾)
        //   2) earthquakes —— 整类排除(与 USGS quakes 层重复)
        //   3) wildfires,日期 2020 年 —— 超 48h 排除(worldmonitor 同款降噪)
        //   4) wildfires,日期 = 现在-1h(动态生成)—— 保留,橙色
        //   5) severeStorms,单 geometry —— 保留,蓝色
        char recentIso[32];
        time_t recentT = time(nullptr) - 3600;
        strftime(recentIso, sizeof(recentIso), "%Y-%m-%dT%H:%M:%SZ", gmtime(&recentT));
        char canned[4096];
        snprintf(canned, sizeof(canned),
            "{\"events\":["
            "{\"id\":\"EONET_1\",\"title\":\"Test Volcano\","
            "\"link\":\"https://eonet.gsfc.nasa.gov/api/v3/events/EONET_1\","
            "\"categories\":[{\"id\":\"volcanoes\",\"title\":\"Volcanoes\"}],\"geometry\":["
            "{\"date\":\"2026-01-02T00:00:00Z\",\"type\":\"Point\",\"coordinates\":[-71.4,-36.9]},"
            "{\"date\":\"2026-01-01T00:00:00Z\",\"type\":\"Point\",\"coordinates\":[-70.0,-30.0]}]},"
            "{\"id\":\"EONET_2\",\"title\":\"Test Quake\",\"link\":\"https://x/2\","
            "\"categories\":[{\"id\":\"earthquakes\",\"title\":\"Earthquakes\"}],\"geometry\":["
            "{\"date\":\"2026-01-01T00:00:00Z\",\"type\":\"Point\",\"coordinates\":[100.0,30.0]}]},"
            "{\"id\":\"EONET_3\",\"title\":\"Stale Fire\",\"link\":\"https://x/3\","
            "\"categories\":[{\"id\":\"wildfires\",\"title\":\"Wildfires\"}],\"geometry\":["
            "{\"date\":\"2020-01-01T00:00:00Z\",\"type\":\"Point\",\"coordinates\":[-114.7,42.8]}]},"
            "{\"id\":\"EONET_4\",\"title\":\"Fresh Fire\",\"link\":\"https://x/4\","
            "\"categories\":[{\"id\":\"wildfires\",\"title\":\"Wildfires\"}],\"geometry\":["
            "{\"date\":\"%s\",\"type\":\"Point\",\"coordinates\":[-120.5,38.2]}]},"
            "{\"id\":\"EONET_5\",\"title\":\"Test Storm\",\"link\":\"https://x/5\","
            "\"categories\":[{\"id\":\"severeStorms\",\"title\":\"Severe Storms\"}],\"geometry\":["
            "{\"date\":\"2026-01-03T12:00:00Z\",\"type\":\"Point\",\"coordinates\":[-128.0,19.1]}]}"
            "]}", recentIso);
        std::vector<earthfeed::FeedPoint> pts = earthfeed::parseEonet(canned);
        CHECK(pts.size() == 3);   // earthquakes + 超 48h wildfire 被过滤
        // 事件1:取"最新"geometry(故意放在数组第一位,验证按日期比较而非取末尾)
        CHECK(std::abs(pts[0].lon - (-71.4)) < 1e-6 && std::abs(pts[0].lat - (-36.9)) < 1e-6);
        CHECK(pts[0].unixTime == 1767312000.0);   // 2026-01-02T00:00:00Z(Unix 秒,非毫秒)
        CHECK(pts[0].color.r() > 0.9f && pts[0].color.g() < 0.3f);   // volcanoes 红
        CHECK(pts[0].title.find(u8"火山") == 0);   // title = 类别中文名 + 事件 title
        CHECK(pts[0].title.find("Test Volcano") != std::string::npos);
        CHECK(pts[0].url == "https://eonet.gsfc.nasa.gov/api/v3/events/EONET_1");
        CHECK(pts[0].detail.find("2026-01-02") != std::string::npos);   // detail 含最新日期
        // 事件4:48h 内 wildfire 保留,橙色,unixTime 秒级 ≈ now-1h
        CHECK(pts[1].color.r() > 0.9f && pts[1].color.g() > 0.4f && pts[1].color.b() < 0.2f);
        CHECK(std::abs(pts[1].unixTime - (double)recentT) < 2.0);
        // 事件5:severeStorms 蓝
        CHECK(pts[2].color.b() > 0.9f && pts[2].color.r() < 0.4f);
        // 汇总:count=3,按类别分桶
        std::string sj = earthfeed::eonetSummaryJson(pts);
        picojson::value sv; CHECK(picojson::parse(sv, sj).empty());
        CHECK(sv.get("count").get<double>() == 3.0);
        CHECK(sv.get("byCategory").get("volcanoes").get<double>() == 1.0);
        CHECK(sv.get("byCategory").get("wildfires").get<double>() == 1.0);
        CHECK(sv.get("byCategory").get("severeStorms").get<double>() == 1.0);
        CHECK(earthfeed::parseEonet("not json").empty());
        std::cout << "earthfeed::parseEonet tests OK\n";
    }

    // ---- earthfeed::parseGdelt / gdeltSummaryJson(P1 Task 3,纯函数,#include gdelt_feed.cpp)----
    // 2026-07-09 切源(GEO 2.0 404 死透 → 实测可用的 GKG GeoJSON 1.0,见 gdelt_feed.cpp
    // 文件头):字段从 count/name/html/shareimage 换成 sumtotalmentions/name/allurls;
    // allurls 是同地点多条文章 URL,真网用裸 TAB 字节(未转义)拼接 —— JSON 字符串内的
    // 裸控制字符本身非法,picojson 对真实响应体实测直接 syntax error;下面第 8 个
    // feature 的 allurls 里嵌入真实 tab 字节(C 字符串 "\t" 即 0x09),复现并验证
    // parseGdelt 内置的 sanitizeControlChars 清洗生效(替换成空格后可解析、可正确
    // 取到 tab 分隔的首条 URL)。
    {
        // 8 个 feature 覆盖全部过滤/映射规则 + 缺字段安全跳过:
        //   1) Kyiv sumtotalmentions=50 —— 保留:热度 t=(50-5)/45=1 纯红、
        //      sizePx=6+min(10,5)=11、allurls 裸 tab 分隔两条 URL → 清洗后取第一条、
        //      detail 含地名/提及数
        //   2) sumtotalmentions=4 —— <5 降噪过滤(worldmonitor 同款)
        //   3) Paris sumtotalmentions=5 —— 边界保留:t=0 黄色、sizePx=6.5、无 allurls → url 空
        //   4) 元素是数字 42(非对象)—— 结构畸形,安全跳过(EONET 质量审查加强项)
        //   5) coordinates 是字符串 —— 结构畸形,安全跳过
        //   6) properties 缺 sumtotalmentions —— 视作 0,被降噪过滤
        //   7) Delhi sumtotalmentions=300 —— 保留:sizePx 钳到 16、纯红
        //   8) Osaka sumtotalmentions=9,allurls 单条无分隔符 —— url 原样整条返回
        const char* canned =
            "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[30.52,50.45]},"
            "\"properties\":{\"name\":\"Kyiv, Ukraine\",\"sumtotalmentions\":50,"
            "\"allurls\":\"https://news.example.com/kyiv\thttps://second.example.com/other\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[13.4,52.5]},"
            "\"properties\":{\"name\":\"Berlin, Germany\",\"sumtotalmentions\":4}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[2.35,48.86]},"
            "\"properties\":{\"name\":\"Paris, France\",\"sumtotalmentions\":5}},"
            "42,"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":\"oops\"},"
            "\"properties\":{\"name\":\"Broken, Nowhere\",\"sumtotalmentions\":99}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[100.5,13.75]},"
            "\"properties\":{\"name\":\"Bangkok, Thailand\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[77.21,28.61]},"
            "\"properties\":{\"name\":\"Delhi, India\",\"sumtotalmentions\":300}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[135.5,34.7]},"
            "\"properties\":{\"name\":\"Osaka, Japan\",\"sumtotalmentions\":9,"
            "\"allurls\":\"https://only.example.com/osaka\"}}"
            "]}";
        std::vector<earthfeed::FeedPoint> pts = earthfeed::parseGdelt(canned);
        CHECK(pts.size() == 4);   // count<5 / 非对象元素 / 坏 coordinates / 缺字段全被滤掉
        // [0] Kyiv:坐标、纯红、大小、url(裸 tab 分隔 allurls 清洗后取第一条)、detail、
        // unixTime=0(GKG 有 urlpubtimedate 但本源不接入排序,维持 GEO 时代行为)
        CHECK(std::abs(pts[0].lon - 30.52) < 1e-6 && std::abs(pts[0].lat - 50.45) < 1e-6);
        CHECK(pts[0].title == "Kyiv, Ukraine");
        CHECK(pts[0].color.r() > 0.9f && pts[0].color.g() < 0.3f);       // count 50 → 纯红
        CHECK(std::abs(pts[0].color.g() - 0.2f) < 1e-4f);                // 端点色精确钉死(防系数漂移)
        CHECK(std::abs(pts[0].sizePx - 11.0f) < 1e-4f);
        CHECK(pts[0].url == "https://news.example.com/kyiv");            // 裸 tab 被清洗成空格后按首个分隔取到
        CHECK(pts[0].detail.find("Kyiv, Ukraine") != std::string::npos);
        CHECK(pts[0].detail.find("50") != std::string::npos);
        CHECK(pts[0].unixTime == 0.0);
        // [1] Paris:边界 count=5 → 黄色、sizePx=6.5、无 allurls → url 空
        CHECK(pts[1].color.r() > 0.9f && pts[1].color.g() > 0.7f);       // count 5 → 黄
        CHECK(std::abs(pts[1].color.g() - 0.85f) < 1e-4f);               // 端点色精确钉死
        CHECK(std::abs(pts[1].sizePx - 6.5f) < 1e-4f);
        CHECK(pts[1].url.empty());
        // [2] Delhi:count 300 越界 → sizePx 钳到 16、颜色仍纯红(t 钳到 1)
        CHECK(std::abs(pts[2].sizePx - 16.0f) < 1e-4f);
        CHECK(pts[2].color.g() < 0.3f);
        // [3] Osaka:allurls 单条无分隔符 → url 原样整条返回(无 tab 可切分)
        CHECK(pts[3].title == "Osaka, Japan");
        CHECK(pts[3].url == "https://only.example.com/osaka");
        // 汇总:count + TOP5 地名按热度降序(picojson 构建,非字符串拼接)
        std::string sj = earthfeed::gdeltSummaryJson(pts);
        picojson::value sv; CHECK(picojson::parse(sv, sj).empty());
        CHECK(sv.get("count").get<double>() == 4.0);
        const picojson::array& top = sv.get("topHotspots").get<picojson::array>();
        CHECK(top.size() == 4);   // 不足 5 条时全量输出
        CHECK(top[0].get("name").to_str() == "Delhi, India");            // 热度降序
        CHECK(top[0].get("mentions").get<double>() == 300.0);
        CHECK(top[1].get("name").to_str() == "Kyiv, Ukraine");
        CHECK(top[2].get("name").to_str() == "Osaka, Japan");
        CHECK(top[3].get("name").to_str() == "Paris, France");
        // 新增:每个 top 热点带代表文章 URL(= FeedPoint.url = allurls 首条;无 allurls 则空串)
        CHECK(top[0].get("url").to_str() == "");                             // Delhi 无 allurls
        CHECK(top[1].get("url").to_str() == "https://news.example.com/kyiv"); // Kyiv 裸 tab 清洗后首条
        CHECK(top[2].get("url").to_str() == "https://only.example.com/osaka");// Osaka 单条
        CHECK(top[3].get("url").to_str() == "");                             // Paris 无 allurls
        // 结构畸形:features 非数组 / 根非对象 / 非 JSON —— 不崩、返回空
        CHECK(earthfeed::parseGdelt("{\"type\":\"FeatureCollection\",\"features\":\"boom\"}").empty());
        CHECK(earthfeed::parseGdelt("[1,2,3]").empty());
        CHECK(earthfeed::parseGdelt("not json").empty());
        // 真实响应体的核心坑:allurls 裸 tab 字节若不清洗,picojson 会直接语法错误
        // 拒绝整份 body(等价于"全部丢失",而非仅丢一个 feature)。这里直接验证:不做
        // 清洗时 picojson::parse 对同样内容判错(证明清洗不是可有可无的防御式动作)。
        {
            const char* noclean =
                "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
                "\"geometry\":{\"type\":\"Point\",\"coordinates\":[1.0,2.0]},"
                "\"properties\":{\"name\":\"X\",\"sumtotalmentions\":9,"
                "\"allurls\":\"http://a\thttp://b\"}}]}";
            picojson::value raw; CHECK(!picojson::parse(raw, noclean).empty());   // 未清洗:判错
            // 而 parseGdelt(内置清洗)对完全相同的字节能正常解析出这一条
            std::vector<earthfeed::FeedPoint> ok = earthfeed::parseGdelt(noclean);
            CHECK(ok.size() == 1 && ok[0].url == "http://a");
        }
        // firstUrl 边界:allurls 缺失 / 空串 —— 均安全返回空 url,不崩
        {
            const char* tmpl =
                "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
                "\"geometry\":{\"type\":\"Point\",\"coordinates\":[1.0,2.0]},"
                "\"properties\":{\"name\":\"X\",\"sumtotalmentions\":9%s}}]}";
            const char* variants[] = { "", ",\"allurls\":\"\"" };
            for (int e = 0; e < 2; ++e)
            {
                char buf[512]; snprintf(buf, sizeof(buf), tmpl, variants[e]);
                std::vector<earthfeed::FeedPoint> ep = earthfeed::parseGdelt(buf);
                CHECK(ep.size() == 1 && ep[0].url.empty());
            }
        }
        std::cout << "earthfeed::parseGdelt tests OK\n";
    }

    // ---- earthfeed::feedDisplayState(Task 1,纯函数,图层行"当前无数据"空态)----
    {
        // feedDisplayState:成功且 0 要素 → state 3(通用空态指示),其余原样透传
        CHECK(earthfeed::feedDisplayState(1, 0) == 3);   // 抓取成功但空
        CHECK(earthfeed::feedDisplayState(1, 5) == 1);   // 成功有数据
        CHECK(earthfeed::feedDisplayState(2, 0) == 2);   // 失败原样
        CHECK(earthfeed::feedDisplayState(0, 0) == 0);   // idle 原样
        std::cout << "[OK] feedDisplayState empty-state\n";
    }

    // ---- earthfeed::parseGpsjam / gpsjamSummaryJson(P1 Task 4,纯函数,#include gpsjam_feed.cpp)----
    {
        // 真实 CSV 结构(实测 2026-07-02 数据的真实行):hex,count_good_aircraft,count_bad_aircraft。
        // 覆盖:三档等级映射、H3 res4 中心点解码(期望值 = python h3 官方绑定 ground truth)、
        // bad=0 降噪过滤、表头/畸形行安全跳过。
        const char* canned =
            "hex,count_good_aircraft,count_bad_aircraft\n"
            "841ee97ffffffff,130,36\n"      // 21.7% → 高(红,12px);黑海西岸
            "841e50bffffffff,29,2\n"        // 6.5%  → 中(橙,10px)
            "841ea49ffffffff,667,1\n"       // 0.15% → 低(黄,8px)
            "8408001ffffffff,10,9\n"        // res-4 五边形 cell(挪威海)→ 高;钉死 pentagon 解码路径
            "84391a7ffffffff,141,0\n"       // bad=0 → 过滤(不上屏)
            "not-a-hex,10,5\n"              // hex 段非十六进制 → 跳过
            "841ee97ffffffff,abc,5\n"       // good 段非数字 → 跳过
            "841ee97ffffffff,10\n"          // 字段不足 → 跳过
            "\n";                           // 空行 → 跳过
        std::vector<earthfeed::FeedPoint> pts =
            earthfeed::parseGpsjam(canned, "2026-07-02", 1782950400.0);
        CHECK(pts.size() == 4);
        // 五边形 cell(h3_lite 最险路径:pentLeading4 旋转+overage 循环):
        // 期望值 = python h3 官方绑定 cell_to_latlng("8408001ffffffff")
        {
            bool sawPentagon = false;
            for (size_t i = 0; i < pts.size(); ++i)
                if (pts[i].detail.find("8408001ffffffff") != std::string::npos)
                {
                    sawPentagon = true;
                    CHECK(std::abs(pts[i].lat - 64.700000) < 1e-4 && std::abs(pts[i].lon - 10.536199) < 1e-4);
                }
            CHECK(sawPentagon);
        }
        // [0] 高:H3 中心点数值断言(python h3: 43.443272, 28.630075),红、12px
        CHECK(std::abs(pts[0].lat - 43.443272) < 1e-4 && std::abs(pts[0].lon - 28.630075) < 1e-4);
        CHECK(std::abs(pts[0].sizePx - 12.0f) < 1e-4f);
        CHECK(pts[0].color.r() > 0.9f && pts[0].color.g() < 0.3f);           // 红
        CHECK(pts[0].title.find(u8"高 / High") != std::string::npos);
        CHECK(pts[0].detail.find("841ee97ffffffff") != std::string::npos);   // detail 含 hex id
        CHECK(pts[0].detail.find("130 / 36") != std::string::npos);          // 好/坏计数
        CHECK(pts[0].detail.find("2026-07-02") != std::string::npos);        // 日期
        CHECK(pts[0].detail.find("gpsjam.org") != std::string::npos);        // 来源署名
        CHECK(pts[0].url.find("https://gpsjam.org/?lat=43.443") == 0);       // 打开 = 官网定位链接
        CHECK(pts[0].unixTime == 1782950400.0);                              // 数据日期 UTC 0 点
        // [1] 中:橙、10px(python h3: 47.030031, 26.976374)
        CHECK(std::abs(pts[1].lat - 47.030031) < 1e-4 && std::abs(pts[1].lon - 26.976374) < 1e-4);
        CHECK(std::abs(pts[1].sizePx - 10.0f) < 1e-4f);
        CHECK(pts[1].color.g() > 0.4f && pts[1].color.g() < 0.7f);           // 橙
        CHECK(pts[1].title.find(u8"中 / Medium") != std::string::npos);
        // [2] 低:黄、8px
        CHECK(std::abs(pts[2].sizePx - 8.0f) < 1e-4f);
        CHECK(pts[2].color.g() > 0.7f);                                      // 黄
        CHECK(pts[2].title.find(u8"低 / Low") != std::string::npos);
        // 汇总:count + 按等级分桶 + 高干扰 TOP3 按坏占比降序(picojson 构建,非字符串拼接)
        std::string sj = earthfeed::gpsjamSummaryJson(pts);
        picojson::value sv; CHECK(picojson::parse(sv, sj).empty());
        CHECK(sv.get("count").get<double>() == 4.0);
        CHECK(sv.get("byLevel").get("high").get<double>() == 2.0);           // 黑海 + 五边形
        CHECK(sv.get("byLevel").get("medium").get<double>() == 1.0);
        CHECK(sv.get("byLevel").get("low").get<double>() == 1.0);
        const picojson::array& top = sv.get("topCells").get<picojson::array>();
        CHECK(top.size() == 3);
        CHECK(top[0].get("hex").to_str() == "8408001ffffffff");              // 9/19=47.4% 居首
        CHECK(std::abs(top[0].get("badRatioPct").get<double>() - 47.368421052631575) < 1e-6);
        CHECK(top[1].get("hex").to_str() == "841ee97ffffffff");              // 21.7% 次之
        CHECK(std::abs(top[1].get("badRatioPct").get<double>() - 21.686746987951807) < 1e-6);
        CHECK(top[1].get("badAircraft").get<double>() == 36.0);
        // gzip 路径(真网:服务器无视 Accept-Encoding 一律回 gzip):压缩的单行 CSV 照常解析
        {
            static const unsigned char gz[] = { 0x1f, 0x8b, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x02, 0xff, 0xcb, 0x48, 0xad, 0xd0, 0x49, 0xce, 0x2f, 0xcd, 0x2b, 0x89, 0x4f,
                0xcf, 0xcf, 0x4f, 0x89, 0x4f, 0xcc, 0x2c, 0x4a, 0x2e, 0x4a, 0x4c, 0x2b, 0x81,
                0x8a, 0x25, 0x25, 0x22, 0x84, 0xb8, 0x2c, 0x4c, 0x0c, 0x53, 0x53, 0x2d, 0xcd,
                0xd3, 0xa0, 0x40, 0xc7, 0xd0, 0xd8, 0x40, 0xc7, 0xd8, 0x8c, 0x0b, 0x00, 0x28,
                0xc2, 0x2d, 0x2e, 0x42, 0x00, 0x00, 0x00 };
            std::string gzBody((const char*)gz, sizeof(gz));
            std::vector<earthfeed::FeedPoint> gp =
                earthfeed::parseGpsjam(gzBody, "2026-07-02", 0.0);
            CHECK(gp.size() == 1);
            CHECK(std::abs(gp[0].lat - 43.443272) < 1e-4);
            // 损坏的 gzip(魔数对、内容截断):不崩、返回空
            std::string broken = gzBody.substr(0, 20);
            CHECK(earthfeed::parseGpsjam(broken, "2026-07-02", 0.0).empty());
        }
        // 结构畸形(gdelt 同标准):非 CSV 文本 / 二进制垃圾 / 空串 —— 不崩、返回空
        CHECK(earthfeed::parseGpsjam("not csv at all", "2026-07-02", 0.0).empty());
        CHECK(earthfeed::parseGpsjam(std::string("\x00\x01\x02\xff", 4), "2026-07-02", 0.0).empty());
        CHECK(earthfeed::parseGpsjam("", "2026-07-02", 0.0).empty());
        // 空输入汇总:count=0 且分桶全零(不是缺字段/崩溃)
        std::string sjE = earthfeed::gpsjamSummaryJson(std::vector<earthfeed::FeedPoint>());
        picojson::value svE; CHECK(picojson::parse(svE, sjE).empty());
        CHECK(svE.get("count").get<double>() == 0.0);
        CHECK(svE.get("byLevel").get("high").get<double>() == 0.0);
        std::cout << "earthfeed::parseGpsjam tests OK\n";
    }

    // ---- earthfeed::parseUnhcrPoints / parseUnhcrGeometry(P1 Task 5,弧原语首个消费者)----
    {
        // countryCentroid 查表(country_centroids.h):命中取值、UNHCR 特有代码/空串 miss 不崩
        {
            double la = 0, lo = 0;
            CHECK(earthfeed::countryCentroid("CHN", la, lo) && la == 35.0 && lo == 105.0);
            CHECK(earthfeed::countryCentroid("SYR", la, lo) && la == 35.0 && lo == 38.0);
            CHECK(!earthfeed::countryCentroid("UNK", la, lo));   // UNHCR 未知国
            CHECK(!earthfeed::countryCentroid("XXA", la, lo));   // UNHCR 无国籍
            CHECK(!earthfeed::countryCentroid("TIB", la, lo));   // UNHCR 特有
            CHECK(!earthfeed::countryCentroid("", la, lo));
        }
        // 手工构造钉死全部规则(fixture=真实响应裁剪,见 test/unhcr_fixture.json,E2E 用;
        // 这里用可控数值断公式)。故意乱序输入(AFG 在前)验证按人数降序排序:
        //  1) AFG→PAK 100,000 —— 阈值边界:恰好 10 万,保留(排最后)
        //  2) SYR→TUR 9,000,000 —— 保留 TOP1;heightScale(≥500 万)/sizePx(≥800 万)双封顶路径
        //  3) UKR→DEU 2,500,000 —— 保留;公式中段数值
        //  4) COD→UGA 99,999 —— <10 万,过滤
        //  5) UNK→DEU 800,000 —— coo_iso=UNK 质心查不到,丢弃
        //  6) SYR→XXA 800,000 —— coa_iso=XXA 质心查不到,丢弃
        //  7) refugees="0"(实测零值是字符串形态)—— numField 视作 0,过滤
        //  8) 元素是数字 42 / coo_iso 缺失 —— 结构畸形,安全跳过
        const char* canned =
            "{\"page\":1,\"maxPages\":1,\"total\":[],\"items\":["
            "{\"year\":2025,\"coo_iso\":\"AFG\",\"coo_name\":\"Afghanistan\","
            "\"coa_iso\":\"PAK\",\"coa_name\":\"Pakistan\",\"refugees\":100000},"
            "{\"year\":2025,\"coo_iso\":\"SYR\",\"coo_name\":\"Syria\","
            "\"coa_iso\":\"TUR\",\"coa_name\":\"Turkiye\",\"refugees\":9000000},"
            "{\"year\":2025,\"coo_iso\":\"UKR\",\"coo_name\":\"Ukraine\","
            "\"coa_iso\":\"DEU\",\"coa_name\":\"Germany\",\"refugees\":2500000},"
            "{\"year\":2025,\"coo_iso\":\"COD\",\"coo_name\":\"DR Congo\","
            "\"coa_iso\":\"UGA\",\"coa_name\":\"Uganda\",\"refugees\":99999},"
            "{\"year\":2025,\"coo_iso\":\"UNK\",\"coo_name\":\"Unknown\","
            "\"coa_iso\":\"DEU\",\"coa_name\":\"Germany\",\"refugees\":800000},"
            "{\"year\":2025,\"coo_iso\":\"SYR\",\"coo_name\":\"Syria\","
            "\"coa_iso\":\"XXA\",\"coa_name\":\"Stateless\",\"refugees\":800000},"
            "{\"year\":2025,\"coo_iso\":\"MMR\",\"coo_name\":\"Myanmar\","
            "\"coa_iso\":\"BGD\",\"coa_name\":\"Bangladesh\",\"refugees\":\"0\"},"
            "42,"
            "{\"year\":2025,\"coa_iso\":\"DEU\",\"refugees\":500000}"
            "]}";
        std::vector<earthfeed::FeedPoint> pts = earthfeed::parseUnhcrPoints(canned);
        earthfeed::FeedGeometry geo = earthfeed::parseUnhcrGeometry(canned);
        // 双产出一致性:同一 body → 弧数 == 点数 == 有效流数(3),lines 恒空
        CHECK(pts.size() == 3 && geo.arcs.size() == 3 && geo.lines.empty());
        // 人数降序:SYR(900 万) > UKR(250 万) > AFG(10 万边界保留)
        CHECK(pts[0].title.find("Syria") == 0 && pts[0].title.find("Turkiye") != std::string::npos);
        CHECK(pts[1].title.find("Ukraine") == 0);
        CHECK(pts[2].title.find("Afghanistan") == 0);
        // [0] 点 = 收容国 TUR 质心 (39,35);sizePx = 8+min(8, 900万/100万) 封顶 = 16;红色系
        CHECK(pts[0].lat == 39.0 && pts[0].lon == 35.0);
        CHECK(std::abs(pts[0].sizePx - 16.0f) < 1e-4f);
        CHECK(std::abs(pts[0].color.r() - 1.0f) < 1e-4f && std::abs(pts[0].color.g() - 0.3f) < 1e-4f);
        CHECK(pts[0].detail.find("9000000") != std::string::npos);       // 人数
        CHECK(pts[0].detail.find("2025") != std::string::npos);          // 年份
        CHECK(pts[0].detail.find("UNHCR") != std::string::npos);         // CC-BY 署名
        CHECK(pts[0].detail.find("CC-BY") != std::string::npos);
        CHECK(pts[0].unixTime == 0.0);
        // [1] UKR→DEU:点在 DEU 质心 (51,9);sizePx = 8+2.5 = 10.5
        CHECK(pts[1].lat == 51.0 && pts[1].lon == 9.0);
        CHECK(std::abs(pts[1].sizePx - 10.5f) < 1e-4f);
        // [2] AFG→PAK 阈值边界:sizePx = 8+0.1 = 8.1
        CHECK(std::abs(pts[2].sizePx - 8.1f) < 1e-4f);
        // 弧参数:[0] SYR(35,38)→TUR(39,35),端点 alt=0,蓝→红,heightScale 公式数值
        CHECK(geo.arcs[0].llaA == osg::Vec3d(35.0, 38.0, 0.0));
        CHECK(geo.arcs[0].llaB == osg::Vec3d(39.0, 35.0, 0.0));
        CHECK((geo.arcs[0].colorA - osg::Vec4(0.25f, 0.55f, 1.0f, 1.0f)).length() < 1e-4f);
        CHECK((geo.arcs[0].colorB - osg::Vec4(1.0f, 0.3f, 0.2f, 1.0f)).length() < 1e-4f);
        // heightScale = 0.10 + 0.06×min(1, 人数/500万):900 万封顶 0.16;250 万=0.13;10 万=0.1012
        CHECK(std::abs(geo.arcs[0].heightScale - 0.16) < 1e-9);
        CHECK(std::abs(geo.arcs[1].heightScale - 0.13) < 1e-9);
        CHECK(std::abs(geo.arcs[2].heightScale - 0.1012) < 1e-9);
        // 双产出逐条对应:每条弧的收容端 == 对应点的经纬度
        for (size_t i = 0; i < pts.size(); ++i)
            CHECK(geo.arcs[i].llaB.x() == pts[i].lat && geo.arcs[i].llaB.y() == pts[i].lon);
        // 汇总:count + 总人数 + TOP5 流(picojson 构建)
        std::string sj = earthfeed::unhcrSummaryJson(pts);
        picojson::value sv; CHECK(picojson::parse(sv, sj).empty());
        CHECK(sv.get("count").get<double>() == 3.0);
        CHECK(sv.get("totalRefugees").get<double>() == 11600000.0);
        const picojson::array& top = sv.get("topFlows").get<picojson::array>();
        CHECK(top.size() == 3);   // 不足 5 条时全量输出
        CHECK(top[0].get("from").to_str() == "Syria" && top[0].get("to").to_str() == "Turkiye");
        CHECK(top[0].get("refugees").get<double>() == 9000000.0);
        CHECK(top[2].get("from").to_str() == "Afghanistan");
        // TOP 40 截断:45 条有效流(人数 10 万+i×1 万,i=0..44)→ 只留人数最高的 40 条,
        // 降序;第 40 名 = 100000+5×10000 = 150000
        {
            std::string body = "{\"items\":[";
            for (int i = 0; i < 45; ++i)
            {
                char item[256];
                snprintf(item, sizeof(item), "%s{\"year\":2025,\"coo_iso\":\"SYR\",\"coo_name\":\"Syria\","
                         "\"coa_iso\":\"TUR\",\"coa_name\":\"Turkiye\",\"refugees\":%d}",
                         i ? "," : "", 100000 + i * 10000);
                body += item;
            }
            body += "]}";
            std::vector<earthfeed::FeedPoint> tp = earthfeed::parseUnhcrPoints(body);
            earthfeed::FeedGeometry tg = earthfeed::parseUnhcrGeometry(body);
            CHECK(tp.size() == 40 && tg.arcs.size() == 40);
            CHECK(tp[0].detail.find("540000") != std::string::npos);     // TOP1 = 最大人数
            CHECK(tp[39].detail.find("150000") != std::string::npos);    // 第 40 名
            for (size_t i = 1; i < tg.arcs.size(); ++i)                  // 严格降序(人数各不同)
                CHECK(tg.arcs[i].heightScale < tg.arcs[i - 1].heightScale);
        }
        // 结构畸形(eonet/gdelt 同标准):非 JSON / items 非数组 / 根非对象 —— 双产出都不崩、返回空
        const char* bad[] = { "not json at all", "{\"items\":\"boom\"}", "[1,2,3]" };
        for (int b = 0; b < 3; ++b)
        {
            CHECK(earthfeed::parseUnhcrPoints(bad[b]).empty());
            CHECK(earthfeed::parseUnhcrGeometry(bad[b]).arcs.empty());
        }
        // 空输入汇总:count=0、totalRefugees=0、topFlows=[](不是缺字段/崩溃)
        std::string sjE = earthfeed::unhcrSummaryJson(std::vector<earthfeed::FeedPoint>());
        picojson::value svE; CHECK(picojson::parse(svE, sjE).empty());
        CHECK(svE.get("count").get<double>() == 0.0);
        CHECK(svE.get("totalRefugees").get<double>() == 0.0);
        CHECK(svE.get("topFlows").get<picojson::array>().empty());
        // 质心表有序性(二分查找前提;乱序插入会静默丢流)+ 坐标范围合法
        {
            const int n = (int)(sizeof(earthfeed::kCountryCentroids) / sizeof(earthfeed::kCountryCentroids[0]));
            for (int i = 1; i < n; ++i)
                CHECK(std::strcmp(earthfeed::kCountryCentroids[i - 1].iso3,
                                  earthfeed::kCountryCentroids[i].iso3) < 0);
            for (int i = 0; i < n; ++i)
                CHECK(std::abs(earthfeed::kCountryCentroids[i].lat) <= 90.0
                      && std::abs(earthfeed::kCountryCentroids[i].lon) <= 180.0);
        }
        std::cout << "earthfeed::parseUnhcrPoints/parseUnhcrGeometry tests OK\n";
    }

    // ---- earthfeed::parseNhcPoints / parseNhcTrack / parseNhcCone / parseNhcGeometry
    //      (P1 Task 6,FeedLine 折线原语首个真实消费者)----
    {
        using namespace earthfeed;
        // 点层(Forecast Points):Omar 故意 tau=12 在前、tau=0 在后 → 每风暴取 min-tau
        // 为当前位置;三档强度 D/S/H 配色;idp_filedate 是 epoch 毫秒 → unixTime 秒;
        // 畸形 3 例:元素是数字 / coordinates 是字符串 / 缺 stormname —— 全部安全跳过。
        const char* pbody =
            "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[-77.0,27.1]},"
            "\"properties\":{\"stormname\":\"Omar\",\"dvlbl\":\"H,Tropical Cyclone\","
            "\"tcdvlp\":\"Hurricane\",\"maxwind\":90,\"gust\":110,\"mslp\":962,\"tau\":12,"
            "\"fldatelbl\":\"2026-07-05 12:00 AM AST\",\"idp_filedate\":1783202400000}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[-75.2,25.6]},"
            "\"properties\":{\"stormname\":\"Omar\",\"dvlbl\":\"H,Tropical Cyclone\","
            "\"tcdvlp\":\"Hurricane\",\"maxwind\":85,\"gust\":105,\"mslp\":968,\"tau\":0,"
            "\"fldatelbl\":\"2026-07-04 11:00 AM AST\",\"idp_filedate\":1783202400000}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[-45.0,14.8]},"
            "\"properties\":{\"stormname\":\"Paloma\",\"dvlbl\":\"S,Tropical Cyclone\","
            "\"tcdvlp\":\"Tropical Storm\",\"maxwind\":50,\"mslp\":998,\"tau\":0,"
            "\"idp_filedate\":1783202400000}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[-108.5,13.2]},"
            "\"properties\":{\"stormname\":\"Six\",\"dvlbl\":\"D,Tropical Cyclone\","
            "\"tcdvlp\":\"Tropical Depression\",\"maxwind\":30,\"mslp\":1006,\"tau\":0,"
            "\"idp_filedate\":1783202400000}},"
            "42,"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":\"oops\"},"
            "\"properties\":{\"stormname\":\"Bad\",\"tau\":0}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[-60.0,20.0]},"
            "\"properties\":{\"tau\":0}}"
            "]}";
        std::vector<earthfeed::FeedPoint> pts = earthfeed::parseNhcPoints(pbody);
        CHECK(pts.size() == 3);   // Omar/Paloma/Six;畸形 3 条被滤
        // 输出按风暴名字典序(std::map):Omar < Paloma < Six
        CHECK(pts[0].title == "Hurricane Omar");
        CHECK(pts[0].lat == 25.6 && pts[0].lon == -75.2);   // min-tau(=0)的坐标,非数组首条
        CHECK(pts[0].color.r() > 0.9f && pts[0].color.g() < 0.3f);    // 飓风红
        CHECK(std::abs(pts[0].sizePx - 12.0f) < 1e-4f);
        CHECK(pts[0].unixTime == 1783202400.0);   // ArcGIS Date epoch 毫秒 ÷ 1000
        CHECK(pts[0].detail.find("85") != std::string::npos);    // 当前位置的风速(非 tau12 的 90)
        CHECK(pts[0].detail.find("968") != std::string::npos);   // 气压
        CHECK(pts[0].detail.find("2026-07-04") != std::string::npos);
        CHECK(pts[0].detail.find("NOAA NHC") != std::string::npos);
        CHECK(pts[0].url == "https://www.nhc.noaa.gov/");
        CHECK(pts[1].title == "Tropical Storm Paloma");
        CHECK(pts[1].color.r() > 0.9f && pts[1].color.g() > 0.7f);    // 风暴黄
        CHECK(pts[2].title == "Tropical Depression Six");
        CHECK(pts[2].color.b() > 0.9f && pts[2].color.r() < 0.4f);    // 低压蓝

        // Track 层:LineString + MultiLineString(2 段)= 3 条白线;单点线整条丢弃、
        // 畸形坐标元素逐点跳过;[lon,lat] → FeedLine 的 (lat,lon);liftMeters 用默认。
        const char* tbody =
            "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\","
            "\"coordinates\":[[-75.2,25.6],[-77.0,27.1],\"junk\",[-78.8,28.9]]},"
            "\"properties\":{\"stormname\":\"Omar\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"MultiLineString\","
            "\"coordinates\":[[[-45.0,14.8],[-47.5,15.6]],[[-47.5,15.6],[-50.1,16.5]]]},"
            "\"properties\":{\"stormname\":\"Paloma\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"LineString\","
            "\"coordinates\":[[-60.0,20.0]]},\"properties\":{\"stormname\":\"Solo\"}}"
            "]}";
        earthfeed::FeedGeometry tg; earthfeed::parseNhcTrack(tbody, tg);
        CHECK(tg.lines.size() == 3 && tg.arcs.empty());
        CHECK(tg.lines[0].latLonDeg.size() == 3);   // "junk" 元素被跳过,3 个有效点
        CHECK(tg.lines[0].latLonDeg[0] == osg::Vec2d(25.6, -75.2));   // 纬度在前
        CHECK((tg.lines[0].color - osg::Vec4(0.95f, 0.95f, 0.95f, 1.0f)).length() < 1e-4f);
        CHECK(tg.lines[0].liftMeters == 15000.0);
        CHECK(tg.lines[1].latLonDeg.size() == 2 && tg.lines[2].latLonDeg.size() == 2);

        // Cone 层:Polygon 只取外环(内环=洞,不混入);MultiPolygon 每面取外环;浅黄。
        const char* cbody =
            "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Polygon\",\"coordinates\":["
            "[[-75.2,25.0],[-73.9,26.0],[-79.0,33.8],[-81.6,30.4],[-75.2,25.0]],"
            "[[-77.0,28.0],[-76.5,28.5],[-77.5,28.7],[-77.0,28.0]]]},"
            "\"properties\":{\"stormname\":\"Omar\"}},"
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"MultiPolygon\",\"coordinates\":["
            "[[[-108.5,12.6],[-107.4,13.3],[-112.4,14.3],[-108.5,12.6]]]]},"
            "\"properties\":{\"stormname\":\"Six\"}}"
            "]}";
        earthfeed::FeedGeometry cg; earthfeed::parseNhcCone(cbody, cg);
        CHECK(cg.lines.size() == 2 && cg.arcs.empty());
        CHECK(cg.lines[0].latLonDeg.size() == 5);   // 外环 5 点(内环 4 点没混进来)
        CHECK(cg.lines[0].latLonDeg[0] == osg::Vec2d(25.0, -75.2));
        CHECK((cg.lines[0].color - osg::Vec4(1.0f, 0.9f, 0.4f, 1.0f)).length() < 1e-4f);
        CHECK(cg.lines[1].latLonDeg.size() == 4);

        // parseNhcGeometry:EARTH_NHC_FILE 兄弟文件约定(<fixture>.track / .cone)+
        // 缺层降级(track 文件删掉 → 只剩锥;全删 → 空几何,均不崩)。
        const char* kNhcFix = "nhc_tests_fixture.tmp";
        { std::ofstream o1(kNhcFix); o1 << "{}"; }
        { std::ofstream o2("nhc_tests_fixture.tmp.track"); o2 << tbody; }
        { std::ofstream o3("nhc_tests_fixture.tmp.cone"); o3 << cbody; }
        setEnvVar("EARTH_NHC_FILE", kNhcFix);
        earthfeed::FeedGeometry g1 = earthfeed::parseNhcGeometry("");
        CHECK(g1.lines.size() == 5 && g1.arcs.empty());   // 3 track + 2 cone
        std::remove("nhc_tests_fixture.tmp.track");
        earthfeed::FeedGeometry g2 = earthfeed::parseNhcGeometry("");
        CHECK(g2.lines.size() == 2);   // track 缺层 → 只剩锥
        std::remove("nhc_tests_fixture.tmp.cone");
        CHECK(earthfeed::parseNhcGeometry("").lines.empty());
        unsetEnvVar("EARTH_NHC_FILE"); std::remove(kNhcFix);

        // 汇总:count + 五桶恒在 + 风暴名列表;空输入也是完整结构
        std::string sj = earthfeed::nhcSummaryJson(pts);
        picojson::value sv; CHECK(picojson::parse(sv, sj).empty());
        CHECK(sv.get("count").get<double>() == 3.0);
        CHECK(sv.get("byIntensity").get("hurricane").get<double>() == 1.0);
        CHECK(sv.get("byIntensity").get("storm").get<double>() == 1.0);
        CHECK(sv.get("byIntensity").get("depression").get<double>() == 1.0);
        CHECK(sv.get("byIntensity").get("major").get<double>() == 0.0);
        CHECK(sv.get("byIntensity").get("other").get<double>() == 0.0);
        const picojson::array& storms = sv.get("storms").get<picojson::array>();
        CHECK(storms.size() == 3 && storms[0].to_str() == "Omar");
        std::string sjE = earthfeed::nhcSummaryJson(std::vector<earthfeed::FeedPoint>());
        picojson::value svE; CHECK(picojson::parse(svE, sjE).empty());
        CHECK(svE.get("count").get<double>() == 0.0);
        CHECK(svE.get("byIntensity").get("hurricane").get<double>() == 0.0);
        CHECK(svE.get("storms").get<picojson::array>().empty());

        // 结构畸形(eonet/gdelt 同标准):非 JSON / features 非数组 / 根非对象——双产出不崩
        CHECK(earthfeed::parseNhcPoints("not json at all").empty());
        CHECK(earthfeed::parseNhcPoints("{\"features\":\"boom\"}").empty());
        CHECK(earthfeed::parseNhcPoints("[1,2,3]").empty());
        earthfeed::FeedGeometry mg;
        earthfeed::parseNhcTrack("not json", mg);
        earthfeed::parseNhcCone("{\"features\":42}", mg);
        CHECK(mg.lines.empty());
        std::cout << "earthfeed::parseNhcPoints/parseNhcTrack/parseNhcCone tests OK\n";
    }

    // ---- earthgeo::buildArcVertices / buildPolylineVertices(T3,纯函数,#include geo_primitives.cpp)----
    {
        // 端点直转 ECEF 的辅助:与实现共用同一 convertLLAtoECEF(WGS84),断言"贴地"就是
        // "与直转结果米级一致",不依赖实现内部的球面近似。
        auto lla2ecef = [](double latDeg, double lonDeg, double altM) {
            return osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
                osg::DegreesToRadians(latDeg), osg::DegreesToRadians(lonDeg), altM));
        };
        auto lonOf = [](const osg::Vec3d& v) { return std::atan2(v.y(), v.x()); };

        const osg::Vec3d beijing(39.9042, 116.4074, 0.0), paris(48.8566, 2.3522, 0.0);
        osg::Vec3d eBJ = lla2ecef(39.9042, 116.4074, 0.0), ePA = lla2ecef(48.8566, 2.3522, 0.0);

        // 1) 京→巴黎:顶点数=n、首尾点贴地(与直转 ECEF 米级一致)
        std::vector<osg::Vec3d> arc = earthgeo::buildArcVertices(beijing, paris, 65, 0.15);
        CHECK(arc.size() == 65);
        CHECK((arc.front() - eBJ).length() < 1.0);
        CHECK((arc.back() - ePA).length() < 1.0);

        // 2) 中点隆起:heightScale=0.15 × 大圆长(~8200km)≈ 1.2e6m,留一半余量断言
        double rMid = arc[32].length();
        CHECK(rMid > eBJ.length() + 6.0e5);
        // 全程有限值(NaN 会毒化后续一切比较)
        for (size_t i = 0; i < arc.size(); ++i)
            CHECK(std::isfinite(arc[i].x()) && std::isfinite(arc[i].y()) && std::isfinite(arc[i].z()));

        // 3) 大圆路径经度单调:京 116.4E → 巴黎 2.35E 向西走,经度严格递减(此航线不跨日界线)
        for (size_t i = 1; i < arc.size(); ++i)
            CHECK(lonOf(arc[i]) < lonOf(arc[i - 1]));

        // 4) heightScale=0 → 全程贴地(半径在 WGS84 极半径与赤道半径+微量之间)
        std::vector<osg::Vec3d> flat = earthgeo::buildArcVertices(beijing, paris, 33, 0.0);
        for (size_t i = 0; i < flat.size(); ++i)
        { double r = flat[i].length(); CHECK(r > 6.35e6 && r < 6.385e6); }

        // 5) n<2 钳到 2(最短仍是一条可画的线段)
        CHECK(earthgeo::buildArcVertices(beijing, paris, 1, 0.1).size() == 2);
        CHECK(earthgeo::buildArcVertices(beijing, paris, -5, 0.1).size() == 2);

        // 6) 跨日界线:东京→洛杉矶向东,展开后的经度增量恒为正(wrap 到 (-π,π] 再比)
        std::vector<osg::Vec3d> arc2 = earthgeo::buildArcVertices(
            osg::Vec3d(35.6762, 139.7649, 0.0), osg::Vec3d(34.0522, -118.2437, 0.0), 65, 0.1);
        for (size_t i = 1; i < arc2.size(); ++i)
        {
            double d = lonOf(arc2[i]) - lonOf(arc2[i - 1]);
            if (d <= -osg::PI) d += 2.0 * osg::PI;
            if (d > osg::PI) d -= 2.0 * osg::PI;
            CHECK(d > 0.0);
        }

        // 7) 正对跖点(大圆方向数学上不唯一):不 NaN、首尾仍精确、中点仍隆起
        std::vector<osg::Vec3d> anti = earthgeo::buildArcVertices(
            osg::Vec3d(0.0, 0.0, 0.0), osg::Vec3d(0.0, 180.0, 0.0), 33, 0.1);
        CHECK(anti.size() == 33);
        for (size_t i = 0; i < anti.size(); ++i)
            CHECK(std::isfinite(anti[i].x()) && std::isfinite(anti[i].y()) && std::isfinite(anti[i].z()));
        CHECK((anti.front() - lla2ecef(0.0, 0.0, 0.0)).length() < 1.0);
        CHECK((anti.back() - lla2ecef(0.0, 180.0, 0.0)).length() < 1.0);
        CHECK(anti[16].length() > 6.4e6);

        // ---- buildPolylineVertices:东京→香港→新加坡 ----
        std::vector<osg::Vec2d> wps;
        wps.push_back(osg::Vec2d(35.6762, 139.7649));   // 东京
        wps.push_back(osg::Vec2d(22.3027, 114.1772));   // 香港
        wps.push_back(osg::Vec2d(1.3521, 103.8198));    // 新加坡
        std::vector<osg::Vec3d> line = earthgeo::buildPolylineVertices(wps);
        CHECK(line.size() > wps.size());   // 每段沿大圆细分过
        CHECK((line.front() - lla2ecef(35.6762, 139.7649, 0.0)).length() < 1.0);
        CHECK((line.back() - lla2ecef(1.3521, 103.8198, 0.0)).length() < 1.0);
        // 贴地:所有顶点半径落在 WGS84 表面范围内
        for (size_t i = 0; i < line.size(); ++i)
        { double r = line[i].length(); CHECK(r > 6.35e6 && r < 6.385e6); }
        // 途经中间路标(香港顶点精确出现在折线上)
        osg::Vec3d eHK = lla2ecef(22.3027, 114.1772, 0.0);
        bool hasHK = false;
        for (size_t i = 0; i < line.size(); ++i)
            if ((line[i] - eHK).length() < 1.0) { hasHK = true; break; }
        CHECK(hasHK);
        // 退化输入:空→空,单点→单点
        CHECK(earthgeo::buildPolylineVertices(std::vector<osg::Vec2d>()).empty());
        CHECK(earthgeo::buildPolylineVertices(
            std::vector<osg::Vec2d>(1, osg::Vec2d(10.0, 20.0))).size() == 1);
        // liftMeters:抬升量精确反映在半径上
        std::vector<osg::Vec3d> lifted = earthgeo::buildPolylineVertices(wps, 15000.0);
        CHECK(std::abs(lifted.front().length() - (line.front().length() + 15000.0)) < 1.0);

        // 跨赤道贴地(评审发现的回归):40°N→40°S 同经度,端点半径线性插值会让赤道处
        // 细分点掉到椭球面下 ~9km——正确实现必须让每个细分点都贴住"该点纬度处"的
        // WGS84 表面。断言用独立代码路径(convertECEFtoLLA 反解大地高)≈0,不与实现
        // 内部的射线-椭球闭式解共用公式。
        std::vector<osg::Vec2d> eqWps;
        eqWps.push_back(osg::Vec2d(40.0, 100.0));
        eqWps.push_back(osg::Vec2d(-40.0, 100.0));
        std::vector<osg::Vec3d> eqLine = earthgeo::buildPolylineVertices(eqWps, 0.0);
        CHECK(eqLine.size() > 2);
        for (size_t i = 0; i < eqLine.size(); ++i)
        {
            osg::Vec3d lla = osgVerse::Coordinate::convertECEFtoLLA(eqLine[i]);
            CHECK(std::abs(lla.z()) < 1.0);   // 大地高 |h| < 1m:全程贴地
        }

        std::cout << "earthgeo::buildArcVertices/buildPolylineVertices tests OK\n";
    }

    // ---- LayerManager 场景预设(Task 4):开关组合 + 底图/不可交互层豁免 + 未知名安全 ----
    // LayerManager.h 是纯头文件(不依赖 OSG viewer),经 feed_layer.h 已在本翻译单元可用。
    {
        LayerManager lm;
        int applyCount[3] = { 0, 0, 0 };   // labels / a / b 的 apply 触发计数
        OverlayLayer base; base.id = "base"; base.displayName = u8"卫星影像";
        base.group = u8"底图 / 标注"; base.enabled = true;
        lm.add(base);   // 无 apply 回调 → 双重豁免(组名 + 不可交互)
        OverlayLayer labels; labels.id = "labels"; labels.displayName = u8"路网·地名";
        labels.group = u8"底图 / 标注"; labels.enabled = true;
        labels.apply = [&](const OverlayLayer&) { applyCount[0]++; };
        lm.add(labels);
        OverlayLayer a; a.id = "a"; a.displayName = "Layer A"; a.group = "G1";
        a.apply = [&](const OverlayLayer&) { applyCount[1]++; };
        lm.add(a);
        OverlayLayer b; b.id = "b"; b.displayName = "Layer B"; b.group = "G2"; b.enabled = true;
        b.apply = [&](const OverlayLayer&) { applyCount[2]++; };
        lm.add(b);

        Preset onlyA; onlyA.name = "onlyA"; onlyA.enabledIds.push_back("a");
        lm.addPreset(onlyA);
        Preset clean; clean.name = "clean";   // 空列表 = 全关
        lm.addPreset(clean);
        CHECK(lm.presets().size() == 2);

        CHECK(lm.applyPreset("onlyA") == true);      // 命中预设 → 返回 true
        CHECK(lm.find("a")->enabled == true);
        CHECK(lm.find("b")->enabled == false);
        CHECK(lm.find("base")->enabled == true);     // 底图/标注组不动
        CHECK(lm.find("labels")->enabled == true);
        CHECK(applyCount[0] == 0);                   // 豁免层 apply 不触发
        CHECK(applyCount[1] == 2);                   // a:先关(第一步)再开(第二步)
        CHECK(applyCount[2] == 1);                   // b:只关

        CHECK(lm.applyPreset("no_such_preset") == false);   // 未知预设名:安全无操作,返回 false
        CHECK(lm.find("a")->enabled == true);
        CHECK(lm.find("b")->enabled == false);
        CHECK(applyCount[1] == 2 && applyCount[2] == 1);

        lm.applyPreset("clean");
        CHECK(lm.find("a")->enabled == false);
        CHECK(lm.find("b")->enabled == false);
        CHECK(lm.find("labels")->enabled == true);   // 豁免层依旧不动

        // 预设列表含豁免层 id / 未知 id:都被安全跳过,不打开豁免层、不崩
        Preset weird; weird.name = "weird";
        weird.enabledIds.push_back("base"); weird.enabledIds.push_back("ghost");
        lm.addPreset(weird);
        lm.applyPreset("weird");   // base 是豁免层,applyPreset 全程不碰它(维持注册时的 true)
        CHECK(lm.find("base")->enabled == true && lm.find("a")->enabled == false);
        std::cout << "LayerManager preset tests OK\n";
    }

    // ---- FeedSelection 关层清理(Task 4 必修 B):只清"来源==本 feed"的选中 ----
    // FeedLayerImpl/setGlobalSelection 在 feed_layer.cpp 匿名命名空间,本翻译单元
    // 已 #include 其实现,可直接构造(不建场景图、不起抓取线程,纯逻辑可测)。
    {
        using namespace earthfeed;
        FeedSelection sel; sel.valid = true; sel.title = "X"; sel.sourceId = "gdacs";
        setGlobalSelection(sel);
        FeedSpec otherSpec; otherSpec.id = "quakes";
        osg::ref_ptr<FeedLayerImpl> other = new FeedLayerImpl(otherSpec);
        other->setEnabled(false);                    // 关别的 feed → 选中保留
        CHECK(currentFeedSelection().valid);
        CHECK(currentFeedSelection().sourceId == "gdacs");
        FeedSpec sameSpec; sameSpec.id = "gdacs";
        osg::ref_ptr<FeedLayerImpl> same = new FeedLayerImpl(sameSpec);
        same->setEnabled(true);                      // 开层不清
        CHECK(currentFeedSelection().valid);
        same->setEnabled(false);                     // 关来源 feed → 清
        CHECK(!currentFeedSelection().valid);
        std::cout << "FeedSelection source-scoped clear tests OK\n";
    }

    // ---- 静态源 staticFile(P1 Task 7):url 空 + fixtureEnv 未设 → 读 spec.staticFile ----
    // (产品内默认加载路径;fixtureEnv 一旦设置仍优先,测试可覆盖打包数据)
    {
        using namespace earthfeed;
        const char* kDefPath = "feed_layer_tests_staticfile_default.tmp";
        const char* kEnvPath = "feed_layer_tests_staticfile_env.tmp";
        { std::ofstream out(kDefPath); out << "DEFAULT"; }
        { std::ofstream out(kEnvPath); out << "FROMENV"; }
        unsetEnvVar("FEED_TEST_STATICFILE_FILE");

        std::string lastBody; int parseCalls = 0;
        FeedSpec spec; spec.id = "staticfiletest";
        spec.url = "";
        spec.fixtureEnv = "FEED_TEST_STATICFILE_FILE";   // 未设置
        spec.staticFile = kDefPath;                      // 打包内默认数据文件
        spec.parse = [&](const std::string& body) {
            parseCalls++; lastBody = body;
            std::vector<FeedPoint> pts(1); return pts;
        };
        osgViewer::Viewer viewer;
        osg::ref_ptr<osg::Node> node = registerFeedLayer(spec, viewer, nullptr, nullptr);
        FeedLayerImpl* impl = static_cast<FeedLayerImpl*>(node->asGroup()->getUserData());
        CHECK(!impl->hasFetchThread());          // staticFile 也是静态源:不起轮询线程
        CHECK(parseCalls == 1 && lastBody == "DEFAULT");

        // fixtureEnv 已设 → 优先于 staticFile
        setEnvVar("FEED_TEST_STATICFILE_FILE", kEnvPath);
        FeedSpec spec2 = spec; spec2.id = "staticfiletest2";
        osg::ref_ptr<osg::Node> node2 = registerFeedLayer(spec2, viewer, nullptr, nullptr);
        CHECK(parseCalls == 2 && lastBody == "FROMENV");
        unsetEnvVar("FEED_TEST_STATICFILE_FILE");

        // url 非空时 staticFile 不改变行为(仍是轮询源)
        FeedSpec net; net.id = "staticfilenet"; net.url = "http://127.0.0.1:1/never";
        net.staticFile = kDefPath;
        net.parse = [](const std::string&) { return std::vector<FeedPoint>(); };
        osg::ref_ptr<osg::Node> netNode = registerFeedLayer(net, viewer, nullptr, nullptr);
        FeedLayerImpl* netImpl = static_cast<FeedLayerImpl*>(netNode->asGroup()->getUserData());
        CHECK(netImpl->hasFetchThread());

        std::remove(kDefPath); std::remove(kEnvPath);
        std::cout << "static source (staticFile default path) tests OK\n";
    }

    // ---- earthfeed::parseStrategic / strategicSummaryJson(P1 Task 7,统一 schema 解析器)----
    {
        using namespace earthfeed;
        StrategicStyle style; style.color = osg::Vec4(0.45f, 0.58f, 0.30f, 1.0f); style.sizePx = 8.0f;

        // 正常:_source/_license + 2 条 items(一条带 info,一条缺 info/country)
        std::string body =
            "{\"_source\":\"Test Dataset (example.org)\",\"_license\":\"CC BY 4.0\","
            "\"_fetched\":\"2026-07-04\",\"items\":["
            "{\"name\":\"Alpha Base\",\"lat\":10.5,\"lon\":103.6,\"country\":\"Cambodia\",\"info\":\"Navy | active\"},"
            "{\"name\":\"Beta Site\",\"lat\":-33.9,\"lon\":151.2}"
            "]}";
        std::vector<FeedPoint> pts = parseStrategic(body, style);
        CHECK(pts.size() == 2);
        CHECK(pts[0].title == "Alpha Base");
        CHECK(std::fabs(pts[0].lat - 10.5) < 1e-9 && std::fabs(pts[0].lon - 103.6) < 1e-9);
        CHECK(pts[0].color == style.color && pts[0].sizePx == style.sizePx);
        CHECK(pts[0].detail.find("Cambodia") != std::string::npos);            // 国家
        CHECK(pts[0].detail.find("Navy | active") != std::string::npos);       // 属性
        CHECK(pts[0].detail.find("Test Dataset (example.org)") != std::string::npos);   // 来源署名
        CHECK(pts[0].detail.find("CC BY 4.0") != std::string::npos);           // license
        CHECK(pts[0].unixTime == 0);                                           // 静态数据无时间
        CHECK(pts[1].title == "Beta Site");                                    // 缺 info/country 不崩
        CHECK(pts[1].detail.find("Test Dataset") != std::string::npos);

        // 缺字段:缺 name / 缺 lat / lat 非数字 → 该条跳过,其余照常
        std::string missing =
            "{\"_source\":\"S\",\"_license\":\"L\",\"items\":["
            "{\"lat\":1,\"lon\":2},"
            "{\"name\":\"NoLat\",\"lon\":2},"
            "{\"name\":\"BadLat\",\"lat\":\"oops\",\"lon\":2},"
            "{\"name\":\"Good\",\"lat\":1,\"lon\":2}"
            "]}";
        std::vector<FeedPoint> pts2 = parseStrategic(missing, style);
        CHECK(pts2.size() == 1 && pts2[0].title == "Good");

        // 畸形 3 例:非 JSON / 顶层非对象 / items 非数组 → 全部安全返回空
        CHECK(parseStrategic("not json at all", style).empty());
        CHECK(parseStrategic("[1,2,3]", style).empty());
        CHECK(parseStrategic("{\"items\":{\"a\":1}}", style).empty());

        // 分桶:按国家 TOP5(6 国 → 只留计数最高的 5 国;count 是全量)
        std::string bucket = "{\"_source\":\"S\",\"_license\":\"L\",\"items\":[";
        const char* cs[] = { "CN", "CN", "CN", "US", "US", "RU", "RU", "FR", "IN", "JP" };
        for (int i = 0; i < 10; ++i)
        {
            if (i > 0) bucket += ",";
            bucket += "{\"name\":\"p\",\"lat\":1,\"lon\":2,\"country\":\"" + std::string(cs[i]) + "\"}";
        }
        bucket += "]}";
        std::vector<FeedPoint> pts3 = parseStrategic(bucket, style);
        CHECK(pts3.size() == 10);
        picojson::value sv; CHECK(picojson::parse(sv, strategicSummaryJson(pts3)).empty());
        CHECK(sv.get("count").get<double>() == 10.0);
        const picojson::object& top = sv.get("topCountries").get<picojson::object>();
        CHECK(top.size() == 5);                       // 6 国只留 TOP5
        CHECK(top.count("CN") && top.at("CN").get<double>() == 3.0);
        CHECK(top.count("US") && top.at("US").get<double>() == 2.0);
        CHECK(top.count("RU") && top.at("RU").get<double>() == 2.0);
        // FR/IN/JP 各 1,TOP5 只装得下其中两个 → 总和 = 3+2+2+1+1 = 9(≠ 全量 10)
        double sum = 0;
        for (picojson::object::const_iterator it = top.begin(); it != top.end(); ++it)
            sum += it->second.get<double>();
        CHECK(sum == 9.0);
        std::cout << "strategic dataset parse/summary tests OK\n";
    }

    // ---- earthfeed::collectRecentEvents / feedHealth(T8 事件流卡+状态带数据面)----
    // 经 registerFeedLayer 走真实注册路径(全局注册表登记/析构自摘),两个静态 fixture
    // feed 验证:unixTime 降序跨 feed 混排、TOP N 截断、unixTime==0 排除、关层退出事件流、
    // 健康统计(静态源恒 ok、轮询源未成功不计入 n)。
    {
        using namespace earthfeed;
        const char* kFixA = "ticker_tests_a.tmp"; { std::ofstream o(kFixA); o << "A"; }
        const char* kFixB = "ticker_tests_b.tmp"; { std::ofstream o(kFixB); o << "B"; }
        setEnvVar("FEED_TEST_TICKER_A", kFixA);
        setEnvVar("FEED_TEST_TICKER_B", kFixB);

        FeedSpec sa; sa.id = "ticka"; sa.fixtureEnv = "FEED_TEST_TICKER_A";
        sa.parse = [](const std::string&) {
            std::vector<FeedPoint> pts(3);
            pts[0].title = "A-old"; pts[0].unixTime = 1000; pts[0].lat = 1.0; pts[0].lon = 2.0;
            pts[1].title = "A-new"; pts[1].unixTime = 5000;
            pts[2].title = "A-notime"; pts[2].unixTime = 0;   // 无时间 → 事件流排除
            return pts;
        };
        FeedSpec sb; sb.id = "tickb"; sb.fixtureEnv = "FEED_TEST_TICKER_B";
        sb.parse = [](const std::string&) {
            std::vector<FeedPoint> pts(2);
            pts[0].title = "B-mid"; pts[0].unixTime = 3000;
            pts[1].title = "B-newest"; pts[1].unixTime = 9000;
            return pts;
        };
        osgViewer::Viewer viewer;
        osg::ref_ptr<osg::Node> na = registerFeedLayer(sa, viewer, nullptr, nullptr);
        osg::ref_ptr<osg::Node> nb = registerFeedLayer(sb, viewer, nullptr, nullptr);
        FeedLayerImpl* ia = static_cast<FeedLayerImpl*>(na->asGroup()->getUserData());
        FeedLayerImpl* ib = static_cast<FeedLayerImpl*>(nb->asGroup()->getUserData());
        ia->syncIfDirty(); ib->syncIfDirty();   // 模拟首帧 update(静态源注册时已 postSnapshot)

        // 未开启:不进事件流、不计入健康统计
        CHECK(collectRecentEvents(20).empty());
        int okN = -1, enN = -1; feedHealth(okN, enN);
        CHECK(okN == 0 && enN == 0);

        // 开启后:跨 feed 混排、unixTime 严格降序、无时间点排除、字段完整
        ia->setEnabled(true); ib->setEnabled(true);
        std::vector<TickerEvent> evs = collectRecentEvents(20);
        CHECK(evs.size() == 4);   // 5 个点里 A-notime(unixTime=0)被排除
        CHECK(evs[0].title == "B-newest" && evs[0].sourceId == "tickb");
        CHECK(evs[1].title == "A-new" && evs[1].sourceId == "ticka");
        CHECK(evs[2].title == "B-mid");
        CHECK(evs[3].title == "A-old" && evs[3].lat == 1.0 && evs[3].lon == 2.0);
        CHECK(evs[3].unixTime == 1000.0);

        // TOP N 截断:n=2 只留最新两条(截断发生在全量排序之后,不是逐 feed 截)
        std::vector<TickerEvent> top2 = collectRecentEvents(2);
        CHECK(top2.size() == 2);
        CHECK(top2[0].title == "B-newest" && top2[1].title == "A-new");

        // 关一个 feed → 它的事件退出事件流(与球面显示一致)
        ib->setEnabled(false);
        evs = collectRecentEvents(20);
        CHECK(evs.size() == 2 && evs[0].title == "A-new" && evs[1].title == "A-old");

        // 健康统计:静态源恒 ok;轮询源(url 打不通、从未成功抓取)计入 m 不计入 n
        feedHealth(okN, enN);
        CHECK(okN == 1 && enN == 1);   // 只剩 ticka(静态,恒 ok)
        FeedSpec nf; nf.id = "ticknet"; nf.url = "http://127.0.0.1:1/never";
        nf.parse = [](const std::string&) { return std::vector<FeedPoint>(); };
        osg::ref_ptr<osg::Node> nn = registerFeedLayer(nf, viewer, nullptr, nullptr);
        FeedLayerImpl* in = static_cast<FeedLayerImpl*>(nn->asGroup()->getUserData());
        in->setEnabled(true);   // 抓取必失败(连接拒绝)→ lastFetchOk 恒 false
        feedHealth(okN, enN);
        CHECK(okN == 1 && enN == 2);

        // 析构自摘:释放 tickb 节点后,注册表不残留悬垂指针(collectRecentEvents 不崩)
        ib->setEnabled(true); nb = nullptr; ib = nullptr;
        evs = collectRecentEvents(20);
        CHECK(evs.size() == 2 && evs[0].title == "A-new");

        unsetEnvVar("FEED_TEST_TICKER_A"); unsetEnvVar("FEED_TEST_TICKER_B");
        std::remove(kFixA); std::remove(kFixB);
        std::cout << "collectRecentEvents/feedHealth tests OK\n";
    }

    // ---- 地震点 unixTime(T8 补齐:USGS time 毫秒 → FeedPoint::unixTime 秒)----
    {
        const char* canned =
            "{\"type\":\"FeatureCollection\",\"features\":["
            "{\"type\":\"Feature\",\"geometry\":{\"type\":\"Point\",\"coordinates\":[-119.8,39.5,5.0]},"
            "\"properties\":{\"mag\":2.5,\"place\":\"T\",\"time\":1718900000000,\"url\":\"https://x\"}}"
            "]}";
        std::vector<earthfeed::FeedPoint> pts = earthfeed::parseUsgsQuakes(canned);
        CHECK(pts.size() == 1 && pts[0].unixTime == 1718900000.0);
        std::cout << "usgs quakes unixTime tests OK\n";
    }

    // ---- LayerManager::lastAppliedPreset(T8 状态带"当前预设名")----
    {
        LayerManager lm;
        OverlayLayer a; a.id = "a"; a.displayName = "A"; a.group = "G";
        a.apply = [](const OverlayLayer&) {};
        lm.add(a);
        Preset p1; p1.name = u8"灾害"; p1.enabledIds.push_back("a"); lm.addPreset(p1);
        Preset p2; p2.name = "clean"; lm.addPreset(p2);
        CHECK(lm.lastAppliedPreset().empty());            // 未应用过 = 空串
        CHECK(lm.applyPreset(u8"灾害"));
        CHECK(lm.lastAppliedPreset() == u8"灾害");        // 成功应用 → 记录
        CHECK(!lm.applyPreset("no_such_preset"));
        CHECK(lm.lastAppliedPreset() == u8"灾害");        // 未知名失败 → 记录不变
        CHECK(lm.applyPreset("clean"));
        CHECK(lm.lastAppliedPreset() == "clean");         // 再次应用 → 覆盖
        std::cout << "LayerManager lastAppliedPreset tests OK\n";
    }

    // ---- earthui::computeCardLayout(v0.15-vision Task 5,纯函数,不依赖 ImGui)----
    {
        using namespace earthui;
        // 场景 1:单卡,首位置 = (screenW - rightMargin, topY)
        {
            std::vector<float> h; h.push_back(100.0f);
            std::vector<osg::Vec2> pos = computeCardLayout(h, 1280.0f, 800.0f, 20.0f, 340.0f, 12.0f, 20.0f, 160.0f, 200.0f);
            CHECK(pos.size() == 1);
            CHECK(std::abs(pos[0].x() - 1260.0f) < 1e-3f);
            CHECK(std::abs(pos[0].y() - 20.0f) < 1e-3f);
        }
        // 场景 2:三卡同列堆叠,不重叠(y 依次 20, 132, 244;gap=12)
        {
            std::vector<float> h; h.push_back(100.0f); h.push_back(100.0f); h.push_back(100.0f);
            std::vector<osg::Vec2> pos = computeCardLayout(h, 1280.0f, 800.0f, 20.0f, 340.0f, 12.0f, 20.0f, 160.0f, 200.0f);
            CHECK(pos.size() == 3);
            CHECK(std::abs(pos[0].y() - 20.0f) < 1e-3f);
            CHECK(std::abs(pos[1].y() - 132.0f) < 1e-3f);
            CHECK(std::abs(pos[2].y() - 244.0f) < 1e-3f);
            CHECK(std::abs(pos[0].x() - pos[1].x()) < 1e-3f);   // 同列 x 相同
        }
        // 场景 3:溢出换列——第二张卡高 700,加上第一张卡(50)会超过可用高度(800-160=640),
        // 触发向左开新列,新列 y 回到 topY
        {
            std::vector<float> h; h.push_back(50.0f); h.push_back(700.0f);
            std::vector<osg::Vec2> pos = computeCardLayout(h, 1280.0f, 800.0f, 20.0f, 340.0f, 12.0f, 20.0f, 160.0f, 200.0f);
            CHECK(pos.size() == 2);
            CHECK(std::abs(pos[1].y() - 20.0f) < 1e-3f);                       // 新列回到顶部
            CHECK(std::abs(pos[1].x() - (pos[0].x() - 340.0f - 12.0f)) < 1e-3f); // 新列在左边(宽+间隙)
        }
        // 场景 4:0 张卡,不崩溃,返回空
        {
            std::vector<float> h;
            std::vector<osg::Vec2> pos = computeCardLayout(h, 1280.0f, 800.0f, 20.0f, 340.0f, 12.0f, 20.0f, 160.0f, 200.0f);
            CHECK(pos.empty());
        }
        // 场景 5:heightHint=0(该卡从未画过,首帧未知高度)按 estimateHeight(200)预判溢出,
        // 而不是按 0 预判(0 永远不会触发溢出,会导致首帧卡片重叠)
        {
            std::vector<float> h; h.push_back(0.0f); h.push_back(0.0f); h.push_back(0.0f);
            // topY=20, bottomReserve=700(刻意设很大,让 200 高的估计值必然触发溢出)
            std::vector<osg::Vec2> pos = computeCardLayout(h, 1280.0f, 800.0f, 20.0f, 340.0f, 12.0f, 20.0f, 700.0f, 200.0f);
            CHECK(pos.size() == 3);
            CHECK(std::abs(pos[1].y() - 20.0f) < 1e-3f);   // 第二张卡因 estimateHeight=200 触发换列,回到顶部
        }
        std::cout << "earthui::computeCardLayout tests OK\n";
    }

    testClusterFeedPoints();
    testClusterLodIntegration();
    testNoUrlNoFixtureFeedSkipsThread();
    testFetchStateTriState();
    testFetchStatusWiring();
    testParseFirmsCsv();
    testPerSpecLiftMeters();
    testClusterEmptyFailureSnapshotSafe();
    testVisualForLayer();
    testHaversineAndRegionSummary();
    testMercatorTileBBox();

    // Task 1: TileManager 超缩放拉伸帧戳 round-trip
    // (用 CHECK 而非 brief 原文的 assert:本文件顶部已注明 -DNDEBUG 会吞掉 assert,
    //  见文件头注释与 CHECK 宏定义;沿用文件既有约定,保证断言在 Release 构建下仍生效)
    {
        osgVerse::TileManager* tm = osgVerse::TileManager::instance();
        tm->markOverlayStretchedPastNative(12345u);
        CHECK(tm->getLastOverlayStretchFrame() == 12345u);
        tm->markOverlayStretchedPastNative(0u);
        CHECK(tm->getLastOverlayStretchFrame() == 0u);
        std::cout << "[OK] TileManager overlay stretch frame stamp" << std::endl;
    }

    // Task 3: 角标可见性纯函数
    // (用 CHECK 而非 brief 原文的 assert:本文件顶部已注明 -DNDEBUG 会吞掉 assert,
    //  见文件头注释与 CHECK 宏定义;沿用文件既有约定,保证断言在 Release 构建下仍生效)
    {
        using earthui::overlayMaxDetailBadgeVisible;
        // 近期有拉伸(帧差 < 去抖窗)+ 有激活层文案 + 未被关 + 未到自消失阈值(shownFrames=0<300) → 显示
        CHECK(overlayMaxDetailBadgeVisible(100, 95, 30, true, false, 0, 300) == true);
        // 帧差超过去抖窗(已不再拉伸)→ 隐藏
        CHECK(overlayMaxDetailBadgeVisible(200, 95, 30, true, false, 0, 300) == false);
        // 无激活层文案 → 隐藏
        CHECK(overlayMaxDetailBadgeVisible(100, 95, 30, false, false, 0, 300) == false);
        // 用户已关闭 → 隐藏
        CHECK(overlayMaxDetailBadgeVisible(100, 95, 30, true, true, 0, 300) == false);
        // lastStretchFrame=0(从未拉伸)→ 隐藏(curFrame 远大于 0)
        CHECK(overlayMaxDetailBadgeVisible(100, 0, 30, true, false, 0, 300) == false);
        // Task 4: 自消失——shownFrames 未到阈值(299 < 300) → 仍可见
        CHECK(overlayMaxDetailBadgeVisible(100, 95, 30, true, false, 299, 300) == true);
        // Task 4: shownFrames 恰好达到阈值(300 == 300) → 自动隐藏
        CHECK(overlayMaxDetailBadgeVisible(100, 95, 30, true, false, 300, 300) == false);
        // Task 4: shownFrames 远超阈值 → 隐藏
        CHECK(overlayMaxDetailBadgeVisible(100, 95, 30, true, false, 301, 300) == false);
        std::cout << "[OK] overlayMaxDetailBadgeVisible" << std::endl;
    }

    {   // Task 1: 配置注册表
        using namespace earthcfg;
        CHECK(getInt("ai.maxRounds") == 30);        // 默认
        setValue("ai.maxRounds", 50); CHECK(getInt("ai.maxRounds") == 50);
        setValue("ai.maxRounds", 9999); CHECK(getInt("ai.maxRounds") == 200);  // 钳到 max
        setValue("ai.maxRounds", -5); CHECK(getInt("ai.maxRounds") == 1);        // 钳到 min
        resetDefault("ai.maxRounds"); CHECK(getInt("ai.maxRounds") == 30);
        CHECK(getInt("http.retries") == 3);
        CHECK((int)(getDouble("badge.seconds") + 0.5) == 5);
        std::cout << "[OK] earthcfg params" << std::endl;
    }

    std::cout << "feed_layer tests OK\n";
    return 0;
}
