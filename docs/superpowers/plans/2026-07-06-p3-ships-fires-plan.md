# P3 一期(AIS 船舶 + FIRMS 火点)Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 接入两条动态流——AISStream WebSocket 实时船位(独立模块)与 NASA FIRMS 火点(FeedSpec 接入 + 框架级网格聚合 LOD 新能力)。

**Architecture:** FIRMS 走既有 `feed_layer` 框架(轮询 REST),框架新增可选 `FeedClusterSpec` 聚合配置(不设=既有源零改动);AIS 走独立模块 `ais_math`(纯函数)+`ais_data`(WS 线程+双缓冲快照),仿 `sat_math`/`sat_data` 与 `flight_data` 的既有拆分先例。

**Tech Stack:** C++14、OSG、libhv(`requests` + `hv::WebSocketClient`,均已在 `osgVerseDependency` 内)、picojson、OpenThreads。

**Spec:** `docs/superpowers/specs/2026-07-06-p3-ships-fires-design.md`(必读,含调研结论与红线)。

## Global Constraints

- 不碰 globe 着色器;不碰 `osgDB::Options`/`osg::PagedLOD`/`DatabasePager`。
- StateSet/Program 按实例持有,禁止跨实例/全局共享(commit 4ee4c74d 教训)。
- 后台线程与主线程只经 mutex+dirty 快照交接;done/status 标志在主线程消费点置位(commit 61479fc5 教训)。
- 任何运行 `osgVerse_EarthExplorer` 的测试**必须**带 `EARTH_OFFSCREEN=1`(用户铁律,绝不弹真实窗口)。
- env 钩子名(与 spec 一致,不得改名):`EARTH_FIRMS_KEY` / `EARTH_FIRES` / `EARTH_FIRES_FILE` / `EARTH_AISSTREAM_KEY` / `EARTH_SHIPS` / `EARTH_SHIPS_FILE`。
- AIS 高空闸门:相机高度 > 3,000,000 m 时断开且不订阅;订阅 bbox = 视口 bbox 外扩 1.5 倍;重连冷却 ≥10 s;断线退避封顶 60 s;船舶 >600 s 无更新剔除;存量表上限 5000。
- FIRMS 三级聚合:`{maxCameraAltKm=300, cellDeg=0(原始)}, {1500, 0.25}, {1e9, 1.0}`;`refreshSeconds=1800`。
- 构建:`cmake --build /Users/USER/osgverse/build/verse_core --target install`(并行度默认;**禁止**同时开第二个构建,并发构建毁实验是已踩过的坑)。
- 测试二进制产出在 `/Users/USER/osgverse/build/verse_core/bin/`。
- 提交信息末尾加 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`。
- 中文注释风格与既有文件一致(解释 why,不是复述 what)。

---

### Task 1: 网格聚合纯函数(feed_layer 框架层)

**Files:**
- Modify: `applications/earth_explorer/feed_layer.h`(FeedClusterLevel/FeedClusterSpec 结构 + clusterFeedPoints 声明,加在 FeedGeometry 之后、FeedSpec 之前;FeedSpec 加 `cluster` 字段)
- Modify: `applications/earth_explorer/feed_layer.cpp`(clusterFeedPoints 实现,放在 namespace earthfeed 顶层、buildFeedGeometryGroup 旁)
- Test: `tests/feed_layer_tests.cpp`(追加聚合单测)

**Interfaces:**
- Produces(Task 2/3 依赖,签名逐字):
```cpp
struct FeedClusterLevel { double maxCameraAltKm = 0.0; double cellDeg = 0.0; };
struct FeedClusterSpec
{
    std::vector<FeedClusterLevel> levels;   // 按 maxCameraAltKm 升序;cellDeg<=0 = 原始点级别
    std::function<FeedPoint(const std::vector<FeedPoint>& members,
                            double centroidLat, double centroidLon)> makeAggregate;   // 可空
    bool empty() const { return levels.empty(); }
};
std::vector<FeedPoint> clusterFeedPoints(const std::vector<FeedPoint>& pts, double cellDeg,
    const std::function<FeedPoint(const std::vector<FeedPoint>&, double, double)>& makeAggregate);
```
- FeedSpec 追加字段:`FeedClusterSpec cluster;`(紧跟 `toolDescriptionCn` 之后)。

- [ ] **Step 1: 写失败单测**(追加到 `tests/feed_layer_tests.cpp` 末尾 main() 之前,并在 main() 里调用 `testClusterFeedPoints();`)

```cpp
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
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds 2>&1 | tail -5`
Expected: 编译错误 `clusterFeedPoints` 未声明。

- [ ] **Step 3: 实现**

`feed_layer.h`,在 `buildFeedGeometryGroup` 两个重载声明之后、`struct FeedSpec` 之前插入:

```cpp
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
```

`FeedSpec` 内 `toolDescriptionCn` 之后追加:

```cpp
        // P3:可选聚合 LOD(不设 = 纯原始点,现有源零改动)。
        FeedClusterSpec cluster;
```

`feed_layer.cpp`,在 `buildFeedGeometryGroup`(一次性重载)之前插入:

```cpp
std::vector<FeedPoint> clusterFeedPoints(const std::vector<FeedPoint>& pts, double cellDeg,
    const std::function<FeedPoint(const std::vector<FeedPoint>&, double, double)>& makeAggregate)
{
    if (pts.empty() || cellDeg <= 0.0) return pts;
    std::map<std::pair<int, int>, std::vector<FeedPoint> > buckets;
    for (size_t i = 0; i < pts.size(); ++i)
    {
        double lon = pts[i].lon;
        while (lon >= 180.0) lon -= 360.0;
        while (lon < -180.0) lon += 360.0;
        double lat = pts[i].lat;
        if (lat > 90.0) lat = 90.0; if (lat < -90.0) lat = -90.0;
        int ix = (int)std::floor((lon + 180.0) / cellDeg);
        int iy = (int)std::floor((lat + 90.0) / cellDeg);
        buckets[std::make_pair(ix, iy)].push_back(pts[i]);
    }
    std::vector<FeedPoint> out; out.reserve(buckets.size());
    for (std::map<std::pair<int, int>, std::vector<FeedPoint> >::iterator it = buckets.begin();
         it != buckets.end(); ++it)
    {
        std::vector<FeedPoint>& ms = it->second;
        if (ms.size() == 1) { out.push_back(ms[0]); continue; }   // 单点桶原样保留
        double cLat = 0.0, cLon = 0.0, tMax = 0.0;
        for (size_t k = 0; k < ms.size(); ++k)
        { cLat += ms[k].lat; cLon += ms[k].lon; if (ms[k].unixTime > tMax) tMax = ms[k].unixTime; }
        cLat /= (double)ms.size(); cLon /= (double)ms.size();
        FeedPoint a;
        if (makeAggregate) a = makeAggregate(ms, cLat, cLon);
        else
        {
            // 缺省聚合:颜色/大小取"最大成员"(sizePx 为强度代理),大小随计数对数放大。
            size_t big = 0;
            for (size_t k = 1; k < ms.size(); ++k) if (ms[k].sizePx > ms[big].sizePx) big = k;
            a.color = ms[big].color;
            a.sizePx = std::min(26.0f, ms[big].sizePx + 4.0f * log10f((float)ms.size()));
            char buf[64]; snprintf(buf, sizeof(buf), u8"%d 项聚合", (int)ms.size());
            a.title = buf;
            snprintf(buf, sizeof(buf), u8"该区域共 %d 项", (int)ms.size());
            a.detail = buf;
        }
        a.lat = cLat; a.lon = cLon; a.unixTime = tMax;   // 位置/时间恒由框架决定
        out.push_back(a);
    }
    return out;
}
```

`feed_layer.cpp` 头部 include 区确认已有 `<map>`、`<cmath>` 语义可用(现有文件已 include `<map>`;`log10f/floor/snprintf` 需要 `<cmath>`/`<cstdio>`,若缺则补 `#include <cmath>` `#include <cstdio>`)。

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Feeds; echo exit=$?`
Expected: 输出含 `[OK] clusterFeedPoints`,`exit=0`。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/feed_layer.h applications/earth_explorer/feed_layer.cpp tests/feed_layer_tests.cpp
git commit -m "feat(earth): grid-cluster pure function for high-density feed points (P3 T1)"
```

---

### Task 2: 聚合 LOD 接入 FeedLayerImpl(多级 Geode + 相机高度切级 + 分级拾取)

**Files:**
- Modify: `applications/earth_explorer/feed_layer.cpp`(FeedSnapshot/fetchOnce/syncIfDirty/pickAt/SyncCallback + registerFeedLayer 空 url 早退)
- Test: `tests/feed_layer_tests.cpp`

**Interfaces:**
- Consumes: Task 1 的 `FeedClusterSpec`/`clusterFeedPoints`。
- Produces(Task 3 依赖):`FeedSpec.cluster` 设置后自动生效的行为——抓取时逐级预聚合、每级一个 Geode、相机高度切 nodeMask、拾取只对当前级别;`FeedLayerImpl::updateActiveLevel(double camAltKm)` 与 `activeLevelIndex()`(单测抓手);registerFeedLayer 对"url 空且非静态源"不再起轮询线程。

**实现要点(全部在 feed_layer.cpp 内,行为开关是 `_spec.cluster.empty()`——空则走既有代码路径,现有源零变化):**

- [ ] **Step 1: 写失败单测**(追加到 feed_layer_tests.cpp,main() 里调用)

```cpp
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
```

- [ ] **Step 2: 跑测试确认失败**(`buildClusterLevels`/`levelGeodeCount` 等不存在 → 编译失败)

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds 2>&1 | tail -5`

- [ ] **Step 3: 实现**(feed_layer.cpp 内改动,逐处列出)

(a) `FeedSnapshot` 追加字段:

```cpp
    struct FeedSnapshot
    {
        std::vector<FeedRecord> records;
        FeedGeometry geometry;
        // P3 聚合 LOD:与 spec.cluster.levels 一一对应;cellDeg<=0 的级别恒为空
        // (显示时直接用 records,避免整份原始点重复存两遍)。抓取线程填、主线程消费。
        std::vector<std::vector<FeedRecord> > levels;
    };
```

(b) `FeedLayerImpl` 增加成员与方法(放在 `snapshotRecords()` 之后):

```cpp
        // ===== P3 聚合 LOD =====
        // 抓取线程侧:对 snap.records 逐级预聚合(单测直调入口)。位置 ecef 与
        // fetchOnce 同款椭球高抬升;聚合点不做地形矫正(只在高空显示,3km 抬升 vs
        // 地形误差在千公里视距下不可见,不值得为它跑求交)。
        void buildClusterLevels(FeedSnapshot& snap)
        {
            snap.levels.clear();
            if (_spec.cluster.empty()) return;
            std::vector<FeedPoint> rawPts; rawPts.reserve(snap.records.size());
            for (size_t i = 0; i < snap.records.size(); ++i) rawPts.push_back(snap.records[i].pt);
            for (size_t li = 0; li < _spec.cluster.levels.size(); ++li)
            {
                snap.levels.push_back(std::vector<FeedRecord>());
                double cell = _spec.cluster.levels[li].cellDeg;
                if (cell <= 0.0) continue;   // 原始点级别:空,显示时用 records
                std::vector<FeedPoint> agg = clusterFeedPoints(rawPts, cell, _spec.cluster.makeAggregate);
                std::vector<FeedRecord>& lvl = snap.levels.back(); lvl.reserve(agg.size());
                for (size_t k = 0; k < agg.size(); ++k)
                {
                    FeedRecord r; r.pt = agg[k];
                    r.ecef = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
                        osg::DegreesToRadians(agg[k].lat), osg::DegreesToRadians(agg[k].lon), kFeedLiftMeters));
                    lvl.push_back(r);
                }
            }
        }
        // 主线程:按相机高度(km)选择应显级别并切 nodeMask。级别表按 maxCameraAltKm
        // 升序,取第一个 maxCameraAltKm > camAltKm 的;都不满足取最后一级。
        void updateActiveLevel(double camAltKm)
        {
            if (_spec.cluster.empty() || _levelGeodes.empty()) return;
            int want = (int)_spec.cluster.levels.size() - 1;
            for (size_t i = 0; i < _spec.cluster.levels.size(); ++i)
                if (camAltKm < _spec.cluster.levels[i].maxCameraAltKm) { want = (int)i; break; }
            if (want == _activeLevel) return;
            _activeLevel = want;
            for (size_t i = 0; i < _levelGeodes.size(); ++i)
                if (_levelGeodes[i].valid())
                    _levelGeodes[i]->setNodeMask(((int)i == _activeLevel) ? ~0u : 0u);
        }
        int activeLevelIndex() const { return _activeLevel; }
        size_t levelGeodeCount() const { return _levelGeodes.size(); }
```

以及 protected 成员(`_geode` 声明旁):

```cpp
        // P3 聚合 LOD:cluster 非空时启用——每级一个 Geode(同一 _ss),_levelRecords
        // 与 spec.cluster.levels 对齐(原始级为空、显示/拾取用 _records)。
        std::vector<osg::ref_ptr<osg::Geode> > _levelGeodes;
        std::vector<std::vector<FeedRecord> > _levelRecords;
        int _activeLevel = -1;
```

(c) `fetchOnce()` 末尾(`return out;` 前、日志之后)加一行:`buildClusterLevels(out);`

(d) `syncIfDirty()`:在 `_records = std::move(snap.records);` 的锁块内同步接管级别数据:

```cpp
            { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
              _records = std::move(snap.records);
              _levelRecords = std::move(snap.levels); }
```

几何重建段,原 `_geode` 三行改为(cluster 空时原路径不变):

```cpp
            if (_spec.cluster.empty())
            {
                if (_geode.valid()) _root->removeChild(_geode.get());
                _geode = buildGeode();
                _root->addChild(_geode.get());
            }
            else
            {
                for (size_t i = 0; i < _levelGeodes.size(); ++i)
                    if (_levelGeodes[i].valid()) _root->removeChild(_levelGeodes[i].get());
                _levelGeodes.clear();
                for (size_t li = 0; li < _spec.cluster.levels.size(); ++li)
                {
                    const std::vector<FeedRecord>& recs =
                        (_spec.cluster.levels[li].cellDeg <= 0.0) ? _records : _levelRecords[li];
                    _levelGeodes.push_back(buildGeodeFrom(recs));
                    _root->addChild(_levelGeodes.back().get());
                }
                int cur = _activeLevel; _activeLevel = -1;           // 强制重刷 nodeMask
                updateActiveLevel(cur >= 0 && cur < (int)_spec.cluster.levels.size()
                                  ? _spec.cluster.levels[cur].maxCameraAltKm * 0.5 : 1.0e9);
            }
```

`buildGeode()` 抽出参数化版本 `buildGeodeFrom(const std::vector<FeedRecord>& recs)`(循环体一致,只把 `_records` 换成入参;原 `buildGeode()` 变成 `return buildGeodeFrom(_records);`)。

(e) `pickAt()`:遍历目标从 `_records` 换成当前级别:

```cpp
            const std::vector<FeedRecord>& recs =
                (_spec.cluster.empty() || _activeLevel < 0
                 || _spec.cluster.levels[_activeLevel].cellDeg <= 0.0)
                ? _records : _levelRecords[_activeLevel];
```

(循环内 `_records[i]` 全部改 `recs[i]`。)

(f) `SyncCallback::operator()` 增加逐帧高度检测(cluster 空时零开销):

```cpp
    void SyncCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        _owner->syncIfDirty();
        _owner->updateActiveLevelFromViewer();
        traverse(node, nv);
    }
```

`FeedLayerImpl` 加:

```cpp
        // 主线程逐帧:从 viewer 相机取眼点高度换算 km 喂给 updateActiveLevel。
        // cluster 空 / 无 viewer(单测)时直接返回,零开销。
        void updateActiveLevelFromViewer()
        {
            if (_spec.cluster.empty() || !_viewer || !_viewer->getCamera()) return;
            osg::Vec3d eye = _viewer->getCamera()->getInverseViewMatrix().getTrans();
            osg::Vec3d lla = osgVerse::Coordinate::convertECEFtoLLA(eye);   // (latRad, lonRad, altM)
            updateActiveLevel(lla[2] / 1000.0);
        }
```

(g) `registerFeedLayer` 起线程分支改为(spec 2.1 的显式早退):

```cpp
    if (impl->isStaticSource()) { impl->postSnapshot(impl->fetchOnce()); }
    else if (!spec.url.empty()) { impl->startFetch(); }
    else std::cout << "[Feed] " << spec.id << " no url & no fixture, fetch disabled\n";
```

(h) 事件流卡/summaryJson/地形矫正语义**不变**:均继续只读 `_records`(原始点)——ticker 显示真实事件而非聚合桶,地形矫正只作用于近地可见的原始级(理由见 buildClusterLevels 注释)。

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Feeds; echo exit=$?`
Expected: `[OK] cluster LOD integration`、`[OK] no-url feed skips fetch thread`,exit=0,且既有全部用例仍过。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/feed_layer.cpp tests/feed_layer_tests.cpp
git commit -m "feat(earth): cluster-LOD wiring in FeedLayerImpl — per-level geodes, altitude switch, level-aware picking (P3 T2)"
```

---

### Task 3: FIRMS 火点源(CSV 解析 + 三级聚合 + key 处理 + 注册)

**Files:**
- Create: `applications/earth_explorer/feeds/firms_feed.h`
- Create: `applications/earth_explorer/feeds/firms_feed.cpp`
- Create: `applications/earth_explorer/test/firms_fixture.csv`
- Modify: `applications/earth_explorer/CMakeLists.txt`(EXECUTABLE_FILES 加 `feeds/firms_feed.cpp`)
- Modify: `applications/earth_explorer/earth_main.cpp`(registerStrategicFeeds 行之后追加注册)
- Test: `tests/feed_layer_tests.cpp`(include firms_feed.cpp + 解析单测)

**Interfaces:**
- Consumes: Task 1/2 的 cluster 机制。
- Produces: `osg::Node* registerFirmsFeed(osgViewer::Viewer& viewer, LayerManager* layers, earthai::ToolRegistry* tools);`(firms_feed.h);`earthfeed::parseFirmsCsv(const std::string&) -> std::vector<FeedPoint>`(单测直测)。

- [ ] **Step 1: 建 fixture**(`applications/earth_explorer/test/firms_fixture.csv`——10 个点挤在云南 (25±0.3, 100±0.3) 用于聚合观察 + 2 个远点 + 1 条畸形行 + 1 条缺 frp 行)

```csv
latitude,longitude,bright_ti4,scan,track,acq_date,acq_time,satellite,instrument,confidence,version,bright_ti5,frp,daynight
25.05,100.05,330.1,0.5,0.5,2026-07-05,0342,N,VIIRS,n,2.0NRT,290.0,3.2,N
25.10,100.10,335.2,0.5,0.5,2026-07-05,0342,N,VIIRS,n,2.0NRT,291.0,8.5,N
25.15,100.15,340.3,0.5,0.5,2026-07-05,0342,N,VIIRS,h,2.0NRT,292.0,25.7,N
25.20,100.20,332.0,0.5,0.5,2026-07-05,0342,N,VIIRS,n,2.0NRT,290.5,4.1,N
25.25,100.25,338.8,0.5,0.5,2026-07-05,0342,N,VIIRS,n,2.0NRT,291.5,12.3,N
25.02,100.28,331.4,0.5,0.5,2026-07-05,0342,N,VIIRS,l,2.0NRT,290.2,1.8,N
25.28,100.02,342.9,0.5,0.5,2026-07-05,0342,N,VIIRS,h,2.0NRT,293.0,45.6,N
25.08,100.22,333.7,0.5,0.5,2026-07-05,0342,N,VIIRS,n,2.0NRT,290.8,6.9,N
25.18,100.08,336.5,0.5,0.5,2026-07-05,0342,N,VIIRS,n,2.0NRT,291.2,9.4,N
25.22,100.18,339.1,0.5,0.5,2026-07-05,0342,N,VIIRS,n,2.0NRT,291.8,15.0,N
-15.50,28.30,345.0,0.5,0.5,2026-07-05,1130,N,VIIRS,h,2.0NRT,295.0,60.2,D
62.10,-145.20,328.5,0.5,0.5,2026-07-05,2215,N,VIIRS,l,2.0NRT,289.0,2.4,D
not,a,valid,row
30.00,110.00,330.0,0.5,0.5,2026-07-05,0500,N,VIIRS,n,2.0NRT,290.0,,N
```

- [ ] **Step 2: 写失败单测**(feed_layer_tests.cpp:include 区加 `#include "../applications/earth_explorer/feeds/firms_feed.cpp"`,追加用例并在 main() 调用)

```cpp
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
```

- [ ] **Step 3: 跑测试确认失败**(parseFirmsCsv 不存在)

- [ ] **Step 4: 实现**

`feeds/firms_feed.h`:

```cpp
#ifndef EARTH_FIRMS_FEED_H
#define EARTH_FIRMS_FEED_H
#include "../feed_layer.h"
// NASA FIRMS 活跃火点(VIIRS S-NPP 近实时,全球最近 24h)。
// key 注册(免费自动发放):https://firms.modaps.eosdis.nasa.gov/api/map_key/
// → 运行前 export EARTH_FIRMS_KEY=<key>;未配置时图层保留但不联网,subtitle 提示。
// 限额 5000 次/10 分钟,我们 30 分钟拉一次,远碰不到。
osg::Node* registerFirmsFeed(osgViewer::Viewer& viewer, LayerManager* layers,
                             earthai::ToolRegistry* tools);
#endif
```

`feeds/firms_feed.cpp`:

```cpp
// NASA FIRMS 活跃火点源(P3):全球 VIIRS 近实时 CSV,量级 1e4~1e5 点/日——
// 本仓库第一个启用 FeedClusterSpec 三级聚合 LOD 的源(高空 1° / 中空 0.25° / 近地原始点)。
#include "firms_feed.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <sstream>

namespace earthfeed
{
    // acq_date "2026-07-05" + acq_time "342"/"0342"(HHMM,可能不足 4 位)→ Unix 秒(UTC)
    static long long firmsParseTime(const std::string& d, const std::string& t)
    {
        struct tm tmv = tm();
        if (sscanf(d.c_str(), "%d-%d-%d", &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday) != 3) return 0;
        tmv.tm_year -= 1900; tmv.tm_mon -= 1;
        int hhmm = atoi(t.c_str());
        tmv.tm_hour = hhmm / 100; tmv.tm_min = hhmm % 100;
#ifdef _WIN32
        return (long long)_mkgmtime(&tmv);
#else
        return (long long)timegm(&tmv);
#endif
    }

    // FRP(MW)→ 颜色/大小三档:小黄 / 中橙 / 大红。
    static void frpStyle(double frp, osg::Vec4& color, float& sizePx)
    {
        if (frp < 5.0)       { color = osg::Vec4(1.00f, 0.85f, 0.30f, 1.0f); sizePx = 6.0f; }
        else if (frp < 20.0) { color = osg::Vec4(1.00f, 0.60f, 0.15f, 1.0f); sizePx = 9.0f; }
        else                 { color = osg::Vec4(1.00f, 0.30f, 0.12f, 1.0f); sizePx = 12.0f; }
    }

    std::vector<FeedPoint> parseFirmsCsv(const std::string& body)
    {
        std::vector<FeedPoint> out;
        std::istringstream in(body);
        std::string line;
        if (!std::getline(in, line)) return out;
        // 表头 → 列名索引(FIRMS 不同 SOURCE 列序有差异,不能按固定下标)
        std::map<std::string, int> col; { std::istringstream h(line); std::string c; int i = 0;
            while (std::getline(h, c, ',')) { if (!c.empty() && c[c.size()-1]=='\r') c.erase(c.size()-1); col[c] = i++; } }
        if (!col.count("latitude") || !col.count("longitude")) return out;
        int iLat = col["latitude"], iLon = col["longitude"];
        int iFrp = col.count("frp") ? col["frp"] : -1;
        int iConf = col.count("confidence") ? col["confidence"] : -1;
        int iDate = col.count("acq_date") ? col["acq_date"] : -1;
        int iTime = col.count("acq_time") ? col["acq_time"] : -1;
        int iDN = col.count("daynight") ? col["daynight"] : -1;
        while (std::getline(in, line))
        {
            if (line.empty()) continue;
            std::vector<std::string> f; { std::istringstream ls(line); std::string c;
                while (std::getline(ls, c, ',')) { if (!c.empty() && c[c.size()-1]=='\r') c.erase(c.size()-1); f.push_back(c); } }
            if ((int)f.size() <= iLon) continue;
            char* e1 = 0; char* e2 = 0;
            double lat = strtod(f[iLat].c_str(), &e1), lon = strtod(f[iLon].c_str(), &e2);
            if (e1 == f[iLat].c_str() || e2 == f[iLon].c_str()) continue;   // 畸形行
            double frp = (iFrp >= 0 && iFrp < (int)f.size()) ? atof(f[iFrp].c_str()) : 0.0;
            std::string conf = (iConf >= 0 && iConf < (int)f.size()) ? f[iConf] : "";
            std::string date = (iDate >= 0 && iDate < (int)f.size()) ? f[iDate] : "";
            std::string time = (iTime >= 0 && iTime < (int)f.size()) ? f[iTime] : "";
            std::string dn = (iDN >= 0 && iDN < (int)f.size()) ? f[iDN] : "";
            FeedPoint p; p.lat = lat; p.lon = lon;
            frpStyle(frp, p.color, p.sizePx);
            char buf[96]; snprintf(buf, sizeof(buf), u8"火点 Fire · FRP %.1f MW", frp);
            p.title = buf;
            p.unixTime = (double)firmsParseTime(date, time);
            snprintf(buf, sizeof(buf), u8"辐射功率 FRP: %.1f MW\n", frp); p.detail = buf;
            p.detail += u8"置信度 Confidence: " + (conf.empty() ? std::string("?") : conf) + "\n";
            p.detail += u8"观测 Observed: " + date + " " + time + " UTC\n";
            p.detail += u8"昼夜 Day/Night: " + (dn == "D" ? std::string(u8"昼") : std::string(u8"夜"));
            out.push_back(p);
        }
        return out;
    }

    // 聚合点样式:火点专属文案 + 最大 FRP 决定颜色档。
    static FeedPoint firmsAggregate(const std::vector<FeedPoint>& ms, double cLat, double cLon)
    {
        (void)cLat; (void)cLon;   // 位置由框架统一覆盖
        double maxFrp = 0.0; double tMax = 0.0;
        for (size_t i = 0; i < ms.size(); ++i)
        {
            double frp = 0.0; sscanf(ms[i].title.c_str(), u8"火点 Fire · FRP %lf", &frp);
            if (frp > maxFrp) maxFrp = frp;
            if (ms[i].unixTime > tMax) tMax = ms[i].unixTime;
        }
        FeedPoint a; float basePx = 0.0f;
        frpStyle(maxFrp, a.color, basePx);
        a.sizePx = std::min(26.0f, basePx + 5.0f * log10f((float)ms.size() + 1.0f));
        char buf[96];
        snprintf(buf, sizeof(buf), u8"火点聚合区 (%d 处)", (int)ms.size()); a.title = buf;
        snprintf(buf, sizeof(buf), u8"该区域 %d 个火点\n最大辐射功率 max FRP: %.1f MW\n推近查看单个火点",
                 (int)ms.size(), maxFrp);
        a.detail = buf;
        return a;
    }

    static std::string firmsSummaryJson(const std::vector<FeedPoint>& pts)
    {
        int nLow = 0, nMid = 0, nHigh = 0; double maxFrp = 0.0;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            double frp = 0.0; sscanf(pts[i].title.c_str(), u8"火点 Fire · FRP %lf", &frp);
            if (frp < 5.0) nLow++; else if (frp < 20.0) nMid++; else nHigh++;
            if (frp > maxFrp) maxFrp = frp;
        }
        picojson::object r; r["count"] = picojson::value((double)pts.size());
        picojson::object b;
        b["frp<5MW"] = picojson::value((double)nLow);
        b["frp5-20MW"] = picojson::value((double)nMid);
        b["frp>=20MW"] = picojson::value((double)nHigh);
        r["byIntensity"] = picojson::value(b);
        r["maxFrpMW"] = picojson::value(maxFrp);
        return picojson::value(r).serialize();
    }
}

osg::Node* registerFirmsFeed(osgViewer::Viewer& viewer, LayerManager* layers, earthai::ToolRegistry* tools)
{
    earthfeed::FeedSpec spec;
    spec.id = "fires"; spec.displayName = u8"火点 (FIRMS)";
    spec.group = u8"实时数据 / Live";
    const char* key = getenv("EARTH_FIRMS_KEY");
    if (key && *key)
    {
        spec.url = std::string("https://firms.modaps.eosdis.nasa.gov/api/area/csv/")
                 + key + "/VIIRS_SNPP_NRT/world/1";
        spec.subtitle = u8"NASA VIIRS 近实时 · 最近 24h";
    }
    else spec.subtitle = u8"未配置 key(EARTH_FIRMS_KEY)";
    // url 为空且无 fixture 时 registerFeedLayer 走"fetch disabled"早退(T2),不会干轮询。
    spec.refreshSeconds = 1800;
    spec.fixtureEnv = "EARTH_FIRES_FILE"; spec.forceEnv = "EARTH_FIRES";
    spec.parse = earthfeed::parseFirmsCsv;
    spec.summaryJson = earthfeed::firmsSummaryJson;
    // 三级聚合(spec §2.2):近地原始点 / 中空 0.25° / 高空 1°。
    earthfeed::FeedClusterLevel raw; raw.maxCameraAltKm = 300.0;   raw.cellDeg = 0.0;
    earthfeed::FeedClusterLevel mid; mid.maxCameraAltKm = 1500.0;  mid.cellDeg = 0.25;
    earthfeed::FeedClusterLevel top; top.maxCameraAltKm = 1.0e9;   top.cellDeg = 1.0;
    spec.cluster.levels.push_back(raw); spec.cluster.levels.push_back(mid); spec.cluster.levels.push_back(top);
    spec.cluster.makeAggregate = earthfeed::firmsAggregate;
    spec.toolDescriptionCn = u8"查询全球活跃火点汇总(NASA FIRMS VIIRS 近实时,最近 24h):"
        u8"总数、按辐射功率 FRP 强度分桶、最大 FRP。需要 EARTH_FIRMS_KEY;"
        u8"未配置 key 时 count 恒为 0。若图层尚未开启会自动开启。";
    return earthfeed::registerFeedLayer(spec, viewer, layers, tools);
}
```

`applications/earth_explorer/CMakeLists.txt`:`feeds/strategic_feed.cpp` 后追加 `feeds/firms_feed.cpp`。

`earth_main.cpp`:`registerStrategicFeeds` 行之后追加(include 区加 `#include "feeds/firms_feed.h"`):

```cpp
    sceneCamera->addChild(registerFirmsFeed(viewer, &layerMgr, aiRuntime.tools));   // P3:FIRMS 火点(三级聚合 LOD)
```

- [ ] **Step 5: 跑单测 + 离线 E2E**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Feeds; echo exit=$?`
Expected: `[OK] parseFirmsCsv`,exit=0。

Run(全量构建后离线 E2E,三个高度各截一张验证聚合切级——**必须 EARTH_OFFSCREEN=1**):
```bash
cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | tail -3
cd /Users/USER/osgverse/build/sdk_core/bin
rm -f capture_*.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 EARTH_FIRES=1 \
  EARTH_FIRES_FILE=/Users/USER/osgverse/applications/earth_explorer/test/firms_fixture.csv \
  ./osgVerse_EarthExplorer --goto 25 100 8000 2>&1 | grep -i "feed\|fires"
```
Expected: 日志含 `[Feed] fires loaded N=13 (static)`;截图(8000 km)可见云南方向一个聚合大点 + 赞比亚/阿拉斯加两个小点。再分别 `--goto 25 100 800`(0.25° 级,多个中点)与 `--goto 25 100 150`(原始级,10 个独立小点)各跑一次,目检三级切换。

- [ ] **Step 6: Commit**

```bash
git add applications/earth_explorer/feeds/firms_feed.h applications/earth_explorer/feeds/firms_feed.cpp \
        applications/earth_explorer/test/firms_fixture.csv applications/earth_explorer/CMakeLists.txt \
        applications/earth_explorer/earth_main.cpp tests/feed_layer_tests.cpp
git commit -m "feat(earth): FIRMS active-fire layer — VIIRS CSV, FRP styling, 3-level cluster LOD (P3 T3)"
```

---

### Task 4: AIS 纯函数层(消息解析 + bbox 决策 + 订阅 JSON)

**Files:**
- Create: `applications/earth_explorer/ais_math.h`
- Create: `applications/earth_explorer/ais_math.cpp`
- Create: `applications/earth_explorer/test/ais_fixture.jsonl`
- Create: `tests/ais_tests.cpp`
- Modify: `tests/CMakeLists.txt`(`NEW_TEST(osgVerse_Test_Satellite ...)` 行后加 `NEW_TEST(osgVerse_Test_Ais ais_tests.cpp)  # P3 AIS ship layer: message parse / bbox decision unit tests`)
- Modify: `applications/earth_explorer/CMakeLists.txt`(EXECUTABLE_FILES 加 `ais_math.cpp`,与 `sat_math.cpp` 并排)

**Interfaces:**
- Produces(Task 5 依赖,签名逐字,`earthais::` 命名空间):
```cpp
struct AisShip { long long mmsi = 0; double lat = 0, lon = 0, sogKn = 0, cogDeg = 0;
                 std::string name; double lastSeenUnix = 0; bool valid = false; };
AisShip parseAisMessage(const std::string& jsonText);
struct ShipBBox { double latMin = -85, lonMin = -180, latMax = 85, lonMax = 180; };
ShipBBox inflateBBox(const ShipBBox& b, double factor);
bool bboxNeedsResubscribe(const ShipBBox& subscribed, const ShipBBox& view);
std::string buildSubscriptionJson(const std::string& apiKey, const ShipBBox& b);
```

- [ ] **Step 1: 建 fixture**(`applications/earth_explorer/test/ais_fixture.jsonl`——aisstream 真实消息形状:6 条有效 PositionReport(香港附近海域,不同航速航向)+ 1 条其它类型 + 1 条畸形。**实现者注意**:字段形状(MetaData.MMSI/ShipName、Message.PositionReport.{Latitude,Longitude,Sog,Cog})以 https://aisstream.io/documentation 为准,如与下面样本有出入,以官方为准同步改 fixture 与解析器,并在提交信息里注明)

```jsonl
{"Message":{"PositionReport":{"Cog":45.5,"Latitude":22.28,"Longitude":114.16,"Sog":12.3,"TrueHeading":46}},"MessageType":"PositionReport","MetaData":{"MMSI":477123456,"ShipName":"EVER GLORY","latitude":22.28,"longitude":114.16,"time_utc":"2026-07-06 01:23:45.678 +0000 UTC"}}
{"Message":{"PositionReport":{"Cog":180.0,"Latitude":22.35,"Longitude":114.05,"Sog":0.2,"TrueHeading":181}},"MessageType":"PositionReport","MetaData":{"MMSI":477234567,"ShipName":"OOCL HARBOUR","latitude":22.35,"longitude":114.05,"time_utc":"2026-07-06 01:23:46.000 +0000 UTC"}}
{"Message":{"PositionReport":{"Cog":270.3,"Latitude":22.20,"Longitude":114.30,"Sog":6.8,"TrueHeading":271}},"MessageType":"PositionReport","MetaData":{"MMSI":563345678,"ShipName":"PACIFIC DAWN","latitude":22.20,"longitude":114.30,"time_utc":"2026-07-06 01:23:47.000 +0000 UTC"}}
{"Message":{"PositionReport":{"Cog":90.0,"Latitude":22.42,"Longitude":114.22,"Sog":22.5,"TrueHeading":90}},"MessageType":"PositionReport","MetaData":{"MMSI":563456789,"ShipName":"FAST CAT 9","latitude":22.42,"longitude":114.22,"time_utc":"2026-07-06 01:23:48.000 +0000 UTC"}}
{"Message":{"PositionReport":{"Cog":315.0,"Latitude":22.15,"Longitude":113.95,"Sog":9.1,"TrueHeading":316}},"MessageType":"PositionReport","MetaData":{"MMSI":413567890,"ShipName":"ZHU JIANG 88","latitude":22.15,"longitude":113.95,"time_utc":"2026-07-06 01:23:49.000 +0000 UTC"}}
{"Message":{"PositionReport":{"Cog":135.7,"Latitude":22.31,"Longitude":114.12,"Sog":15.4,"TrueHeading":136}},"MessageType":"PositionReport","MetaData":{"MMSI":477678901,"ShipName":"MAERSK HANOI","latitude":22.31,"longitude":114.12,"time_utc":"2026-07-06 01:23:50.000 +0000 UTC"}}
{"Message":{"ShipStaticData":{"Name":"SOMETHING ELSE"}},"MessageType":"ShipStaticData","MetaData":{"MMSI":999999999}}
{this is not valid json at all
```

- [ ] **Step 2: 写失败单测**(`tests/ais_tests.cpp` 全新文件)

```cpp
// tests/ais_tests.cpp — P3 AIS 船舶层纯函数单测(消息解析 / bbox 决策 / 订阅 JSON)。
// 沿用 NEW_TEST 单文件先例:被测实现直接 #include 进本翻译单元。
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <sstream>
#include <picojson.h>
#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)
#include "../applications/earth_explorer/ais_math.cpp"

static std::string readAll(const char* p)
{ std::ifstream in(p); std::stringstream ss; ss << in.rdbuf(); return ss.str(); }

static void testParseAisMessage()
{
    using namespace earthais;
    const char* fx = "/Users/USER/osgverse/applications/earth_explorer/test/ais_fixture.jsonl";
    std::istringstream in(readAll(fx));
    std::string line; int nValid = 0; AisShip first;
    while (std::getline(in, line))
    {
        AisShip s = parseAisMessage(line);
        if (s.valid) { if (nValid == 0) first = s; nValid++; }
    }
    CHECK(nValid == 6);                                   // 其它类型/畸形行都被拒
    CHECK(first.mmsi == 477123456LL);
    CHECK(std::fabs(first.lat - 22.28) < 1e-9);
    CHECK(std::fabs(first.lon - 114.16) < 1e-9);
    CHECK(std::fabs(first.sogKn - 12.3) < 1e-9);
    CHECK(std::fabs(first.cogDeg - 45.5) < 1e-9);
    CHECK(first.name == "EVER GLORY");
    std::cout << "[OK] parseAisMessage\n";
}
static void testBBoxLogic()
{
    using namespace earthais;
    ShipBBox v; v.latMin = 20; v.latMax = 24; v.lonMin = 112; v.lonMax = 116;
    ShipBBox sub = inflateBBox(v, 1.5);
    CHECK(sub.latMin < v.latMin && sub.latMax > v.latMax);
    CHECK(sub.latMax - sub.latMin > (v.latMax - v.latMin) * 1.49);
    // 钳制:全球视口外扩不越界
    ShipBBox g; g.latMin = -85; g.latMax = 85; g.lonMin = -180; g.lonMax = 180;
    ShipBBox gi = inflateBBox(g, 1.5);
    CHECK(gi.latMin >= -85.0 && gi.latMax <= 85.0 && gi.lonMin >= -180.0 && gi.lonMax <= 180.0);
    // 视口在订阅范围内 → 不重连
    CHECK(!bboxNeedsResubscribe(sub, v));
    // 视口中心移出订阅范围 → 重连
    ShipBBox moved = v; moved.lonMin = 130; moved.lonMax = 134;
    CHECK(bboxNeedsResubscribe(sub, moved));
    // 拉远超过订阅范围 → 重连
    ShipBBox wide = v; wide.latMin = 0; wide.latMax = 44; wide.lonMin = 90; wide.lonMax = 140;
    CHECK(bboxNeedsResubscribe(sub, wide));
    // 推近太多(< 订阅跨度 1/4)→ 重连收窄,省流量
    ShipBBox tiny = v; tiny.latMin = 22.0; tiny.latMax = 22.4; tiny.lonMin = 114.0; tiny.lonMax = 114.4;
    CHECK(bboxNeedsResubscribe(sub, tiny));
    std::cout << "[OK] bbox logic\n";
}
static void testSubscriptionJson()
{
    using namespace earthais;
    ShipBBox b; b.latMin = 20; b.lonMin = 112; b.latMax = 24; b.lonMax = 116;
    std::string j = buildSubscriptionJson("test-key", b);
    picojson::value v; std::string err = picojson::parse(v, j);
    CHECK(err.empty());
    CHECK(v.get("APIKey").get<std::string>() == "test-key");
    const picojson::array& boxes = v.get("BoundingBoxes").get<picojson::array>();
    CHECK(boxes.size() == 1);
    const picojson::array& box = boxes[0].get<picojson::array>();
    CHECK(box.size() == 2);   // [SW, NE] 两角
    const picojson::array& sw = box[0].get<picojson::array>();
    CHECK(std::fabs(sw[0].get<double>() - 20.0) < 1e-9);   // [lat, lon] 序
    CHECK(std::fabs(sw[1].get<double>() - 112.0) < 1e-9);
    const picojson::array& types = v.get("FilterMessageTypes").get<picojson::array>();
    CHECK(types.size() == 1 && types[0].get<std::string>() == "PositionReport");
    std::cout << "[OK] subscription json\n";
}
int main()
{
    testParseAisMessage();
    testBBoxLogic();
    testSubscriptionJson();
    std::cout << "ALL AIS TESTS PASSED\n";
    return 0;
}
```

- [ ] **Step 3: 跑测试确认失败**(目标不存在/编译失败)

Run: `cmake /Users/USER/osgverse/build/verse_core 2>&1 | tail -2 && cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Ais 2>&1 | tail -5`

- [ ] **Step 4: 实现**

`ais_math.h`:

```cpp
#ifndef EARTH_AIS_MATH_H
#define EARTH_AIS_MATH_H
#include <string>
// AIS(aisstream.io)纯函数层:消息解析 / 订阅 bbox 决策 / 订阅消息构造。
// 不含任何网络与 OSG 依赖,tests/ais_tests.cpp 直测(仿 sat_math 先例)。
namespace earthais
{
    struct AisShip
    {
        long long mmsi = 0;
        double lat = 0, lon = 0, sogKn = 0, cogDeg = 0;   // SOG 节;COG 真航向角(度)
        std::string name;
        double lastSeenUnix = 0;   // 由调用方(收到消息那一刻)填,解析器不管时钟
        bool valid = false;
    };
    // 只认 MessageType=="PositionReport";其余类型/畸形 JSON → valid=false。
    // 字段形状按 aisstream 官方文档:MetaData.MMSI/ShipName,
    // Message.PositionReport.{Latitude,Longitude,Sog,Cog}。
    AisShip parseAisMessage(const std::string& jsonText);

    struct ShipBBox { double latMin = -85, lonMin = -180, latMax = 85, lonMax = 180; };
    // 中心不动、跨度乘 factor,钳到 [-85,85]/[-180,180](aisstream bbox 是订阅时一次性
    // 指定,外扩订阅可容忍视口小幅平移而不必立刻重连)。
    ShipBBox inflateBBox(const ShipBBox& b, double factor);
    // 需要断开重连换订阅的三种情形:视口中心移出订阅范围 / 拉远超出订阅范围 /
    // 推近到订阅跨度 1/4 以下(订阅范围过大浪费消息流量)。
    bool bboxNeedsResubscribe(const ShipBBox& subscribed, const ShipBBox& view);
    // aisstream 订阅消息:{"APIKey":..,"BoundingBoxes":[[[latMin,lonMin],[latMax,lonMax]]],
    //  "FilterMessageTypes":["PositionReport"]}(坐标序 [lat,lon],与官方示例一致)。
    std::string buildSubscriptionJson(const std::string& apiKey, const ShipBBox& b);
}
#endif
```

`ais_math.cpp`:

```cpp
#include <picojson.h>
#include <algorithm>
#include "ais_math.h"

namespace earthais
{
    AisShip parseAisMessage(const std::string& jsonText)
    {
        AisShip s;
        picojson::value root; std::string err = picojson::parse(root, jsonText);
        if (!err.empty() || !root.is<picojson::object>()) return s;
        if (!root.get("MessageType").is<std::string>()
            || root.get("MessageType").get<std::string>() != "PositionReport") return s;
        const picojson::value& msg = root.get("Message");
        if (!msg.is<picojson::object>()) return s;
        const picojson::value& pr = msg.get("PositionReport");
        if (!pr.is<picojson::object>()) return s;
        if (!pr.get("Latitude").is<double>() || !pr.get("Longitude").is<double>()) return s;
        s.lat = pr.get("Latitude").get<double>();
        s.lon = pr.get("Longitude").get<double>();
        s.sogKn = pr.get("Sog").is<double>() ? pr.get("Sog").get<double>() : 0.0;
        s.cogDeg = pr.get("Cog").is<double>() ? pr.get("Cog").get<double>() : 0.0;
        const picojson::value& meta = root.get("MetaData");
        if (meta.is<picojson::object>())
        {
            if (meta.get("MMSI").is<double>()) s.mmsi = (long long)meta.get("MMSI").get<double>();
            if (meta.get("ShipName").is<std::string>())
            {
                s.name = meta.get("ShipName").get<std::string>();
                // aisstream 船名常带右侧空格填充,裁掉
                while (!s.name.empty() && s.name[s.name.size()-1] == ' ') s.name.erase(s.name.size()-1);
            }
        }
        if (s.mmsi <= 0) return s;   // 没有 MMSI 无法入存量表
        s.valid = true;
        return s;
    }

    ShipBBox inflateBBox(const ShipBBox& b, double factor)
    {
        double cLat = (b.latMin + b.latMax) * 0.5, cLon = (b.lonMin + b.lonMax) * 0.5;
        double hLat = (b.latMax - b.latMin) * 0.5 * factor, hLon = (b.lonMax - b.lonMin) * 0.5 * factor;
        ShipBBox o;
        o.latMin = std::max(-85.0, cLat - hLat); o.latMax = std::min(85.0, cLat + hLat);
        o.lonMin = std::max(-180.0, cLon - hLon); o.lonMax = std::min(180.0, cLon + hLon);
        return o;
    }

    bool bboxNeedsResubscribe(const ShipBBox& sub, const ShipBBox& view)
    {
        double cLat = (view.latMin + view.latMax) * 0.5, cLon = (view.lonMin + view.lonMax) * 0.5;
        if (cLat < sub.latMin || cLat > sub.latMax || cLon < sub.lonMin || cLon > sub.lonMax) return true;
        double vs = std::max(view.latMax - view.latMin, view.lonMax - view.lonMin);
        double ss = std::max(sub.latMax - sub.latMin, sub.lonMax - sub.lonMin);
        if (vs > ss) return true;          // 拉远超出订阅范围
        if (vs < ss * 0.25) return true;   // 推近太多,订阅过宽浪费流量
        return false;
    }

    std::string buildSubscriptionJson(const std::string& apiKey, const ShipBBox& b)
    {
        picojson::array sw, ne, box, boxes;
        sw.push_back(picojson::value(b.latMin)); sw.push_back(picojson::value(b.lonMin));
        ne.push_back(picojson::value(b.latMax)); ne.push_back(picojson::value(b.lonMax));
        box.push_back(picojson::value(sw)); box.push_back(picojson::value(ne));
        boxes.push_back(picojson::value(box));
        picojson::object o;
        o["APIKey"] = picojson::value(apiKey);
        o["BoundingBoxes"] = picojson::value(boxes);
        picojson::array types; types.push_back(picojson::value(std::string("PositionReport")));
        o["FilterMessageTypes"] = picojson::value(types);
        return picojson::value(o).serialize();
    }
}
```

CMake 两处修改见 Files 清单。

- [ ] **Step 5: 跑测试确认通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Ais && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Ais; echo exit=$?`
Expected: `ALL AIS TESTS PASSED`,exit=0。

- [ ] **Step 6: Commit**

```bash
git add applications/earth_explorer/ais_math.h applications/earth_explorer/ais_math.cpp \
        applications/earth_explorer/test/ais_fixture.jsonl tests/ais_tests.cpp \
        tests/CMakeLists.txt applications/earth_explorer/CMakeLists.txt
git commit -m "feat(earth): AIS pure-function layer — message parse, bbox decisions, subscription JSON (P3 T4)"
```

---

### Task 5: AIS 船舶层模块(WS 线程 + 存量表 + 渲染 + 拾取)

**Files:**
- Create: `applications/earth_explorer/ais_data.h`
- Create: `applications/earth_explorer/ais_data.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`(EXECUTABLE_FILES 加 `ais_data.cpp`)

**Interfaces:**
- Consumes: Task 4 的 `earthais::` 纯函数。
- Produces(Task 6 依赖,`ais_data.h` 逐字):

```cpp
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
```

**实现要求(`ais_data.cpp`,结构仿 flight_data.cpp + sat_data.cpp 的既有骨架,以下为关键差异点的完整规格;箭头着色器/拾取/SyncCallback 逐字复用 flight_data.cpp 同名代码,仅换命名前缀 Ship_):**

- [ ] **Step 1: 实现模块**(本任务无独立单测——纯函数已在 T4 测过,线程/渲染走 T6 的 fixture E2E;这是 flight/sat 层同款的既定测试策略)

核心结构:

```cpp
// 线程模型(三方职责,红线:done/status 只在消费线程置位,61479fc5 教训):
// - libhv loop 线程(WebSocketClient 内部):onmessage 里 parseAisMessage → 持 _storeMutex
//   写 _store(map<mmsi,AisShip>),容量到 5000 时丢弃新 MMSI(已有的仍更新)。
// - 控制线程(OpenThreads,100ms tick):读 enabled/viewState → 高空闸门/连接/重连/退避
//   决策(全部 WS open/close 都只在这条线程发起,回调线程绝不自己重连);每 10 tick(1s)
//   剔除 >600s 未更新的船、持 _storeMutex 拷贝快照 → postSnapshot 给主线程。
// - 主线程:SyncCallback → syncIfDirty 重建 Geode(箭头点精灵,COG 旋转,SOG 配色)。
//
// 状态机(_status,std::atomic<int>,控制线程写、主线程 statusText() 读):
enum ShipStatus { kNoKey, kDisabled, kZoomedOut, kConnecting, kConnected, kRetrying, kFixture };
```

- SOG 配色(节):`<0.5` 锚泊灰 `(0.62,0.62,0.62)`;`<8` 慢速蓝 `(0.35,0.65,1.0)`;`<20` 常速青 `(0.30,0.95,0.90)`;`>=20` 高速亮黄 `(1.0,0.95,0.40)`。点大小 16px。船位 ECEF 抬升 `kShipLiftMeters = 3000.0`(海面标记防 z-fight,同 feed 先例)。
- **不做逐帧外推**(与航班层的刻意差异,注释写明:船速 ~10m/s,快照 1s 一发,帧间位移远小于 1px)。
- 连接管理(全部在控制线程):
  - `EARTH_AISSTREAM_KEY` 未设置 → 恒 kNoKey,不联网。
  - `EARTH_SHIPS_FILE` 设置 → 启动时逐行读文件过 `parseAisMessage` 灌 `_store`,post 一次快照,状态 kFixture,**不建 WS**(fixture 优先于 key,离线确定性)。
  - 闸门:`camAltM > 3.0e6` 或 bbox 跨度 >40°(经度)→ 确保断开,状态 kZoomedOut。
  - 连接:`ws->onopen` 里 `ws->send(buildSubscriptionJson(key, inflateBBox(view, 1.5)))` 并记录 `_subscribedBBox`;`setPingInterval(25000)`。
  - 重连决策:已连接且 `bboxNeedsResubscribe(_subscribedBBox, view)` 且距上次连接 >10s → close + 重开。
  - 失败退避:连接失败/onclose → 下次重试间隔 ×2(起步 5s,封顶 60s),成功后复位。
  - 析构:`cancel()` 置 done → join;控制线程退出前 `ws->close()`。
- `summaryJson()`:主线程读 `_ships`(syncIfDirty 后的主线程副本,同 flight 先例):`{"count":N,"note":"覆盖范围为当前订阅 bbox","bySpeed":{"anchored":..,"slow":..,"cruise":..,"fast":..},"fastestName":..,"fastestSogKn":..}`。
- `statusText()` 文案(逐字):kNoKey=`u8"未配置 key(EARTH_AISSTREAM_KEY)"`;kZoomedOut=`u8"推近后显示船舶(当前视野过大)"`;kConnecting=`u8"连接中…"`;kConnected=`u8"已连接 · <N> 艘"`(N=最近快照船数,atomic int);kRetrying=`u8"连接失败,自动重试中"`;kFixture=`u8"离线 fixture 数据"`;kDisabled=空串。
- `configureShipLayer`:仿 `configureFlightLayer`——建 impl、buildScene、UserData 持有、startThread、挂 `ShipPickHandler`(镜像 FlightPickHandler,tol 14px)。**不挂 bbox handler**(Task 6 在 earth_main 挂,因为要喂 camAlt)。
- include `"3rdparty/libhv/all/client/WebSocketClient.h"`(HV_STATICLIB 已在应用 CMake 定义)。

- [ ] **Step 2: 编译通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | grep -E "error|warning: unused" | head; echo done`
Expected: 无 error。

- [ ] **Step 3: Commit**

```bash
git add applications/earth_explorer/ais_data.h applications/earth_explorer/ais_data.cpp \
        applications/earth_explorer/CMakeLists.txt
git commit -m "feat(earth): AIS ship layer module — hv WebSocket thread, ship store, arrow sprites, picking (P3 T5)"
```

---

### Task 6: AIS 接线(图层目录 + 状态 subtitle + 详情卡 + 视口 handler)+ 离线 E2E

**Files:**
- Modify: `applications/earth_explorer/earth_main.cpp`
- Modify: `applications/earth_explorer/EarthControlUI.h`

**Interfaces:**
- Consumes: Task 5 的 `ShipLayer`/`configureShipLayer`/`statusText`。

- [ ] **Step 1: earth_main.cpp 接线**

(a) include 区加 `#include "ais_data.h"`。

(b) `configureSatelliteLayer` 调用行之后:

```cpp
    ShipLayer* shipLayer = nullptr;
    sceneCamera->addChild(configureShipLayer(viewer, &shipLayer));   // P3:AIS 船舶(WS 流)
```

(c) `SatFetchStatusHandler` 类定义之后,镜像加(FRAME 里同时喂视口与刷 subtitle,一个 handler 做两件事——bbox 数学逐字复用 FlightBBoxHandler,额外把 `lla[2]` 作为 camAltM 传入):

```cpp
// P3:AIS 船舶层的视口跟随 + 状态 subtitle 刷新(FRAME 驱动,镜像 FlightBBoxHandler +
// SatFetchStatusHandler 两个先例;高空闸门的判定数据 camAltM 也在这里喂给 worker)。
class ShipViewStateHandler : public osgGA::GUIEventHandler
{
public:
    ShipViewStateHandler(LayerManager* lm, ShipLayer* ships) : _lm(lm), _ships(ships) {}
    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
    {
        if (ea.getEventType() != osgGA::GUIEventAdapter::FRAME || !_ships) return false;
        osgViewer::View* view = static_cast<osgViewer::View*>(&aa);
        osg::Vec3d eye = view->getCamera()->getInverseViewMatrix().getTrans();
        osg::Vec3d lla = osgVerse::Coordinate::convertECEFtoLLA(eye);
        double lat0 = osg::RadiansToDegrees(lla[0]), lon0 = osg::RadiansToDegrees(lla[1]);
        double h = lla[2]; const double R = 6371000.0;
        double cosv = R / (R + (h > 0.0 ? h : 0.0));
        double thetaDeg = (cosv < 1.0) ? osg::RadiansToDegrees(acos(cosv)) : 0.0;
        double latMin = lat0 - thetaDeg, latMax = lat0 + thetaDeg;
        double cosLat = cos(osg::DegreesToRadians(lat0)); if (cosLat < 0.2) cosLat = 0.2;
        double lonHalf = thetaDeg / cosLat; if (lonHalf > 180.0) lonHalf = 180.0;
        double lonMin = lon0 - lonHalf, lonMax = lon0 + lonHalf;
        if (latMin < -85.0) latMin = -85.0; if (latMax > 85.0) latMax = 85.0;
        if (lonMin < -180.0) lonMin = -180.0; if (lonMax > 180.0) lonMax = 180.0;
        _ships->setViewState(latMin, lonMin, latMax, lonMax, h);
        OverlayLayer* l = _lm ? _lm->find("ships") : nullptr;
        if (l) { std::string want = _ships->statusText(); if (l->subtitle != want) l->subtitle = want; }
        return false;
    }
protected:
    LayerManager* _lm; ShipLayer* _ships;
};
```

(d) OverlayLayer 注册块(`layerMgr.add(starlink);` 附近的同一作用域)追加:

```cpp
        OverlayLayer ships; ships.id = "ships"; ships.displayName = u8"船舶 (AIS)";
        ships.group = u8"实时数据 / Live"; ships.subtitle = u8"AISStream 实时船位";
        ships.hasOpacity = false;
        ShipLayer* shipsPtr = shipLayer;
        ships.apply = [shipsPtr](const OverlayLayer& l) { if (shipsPtr) shipsPtr->setEnabled(l.enabled); };
        OverlayLayer& shipsAdded = layerMgr.add(ships);
        const char* shipsEnv = getenv("EARTH_SHIPS");
        if (shipsEnv && *shipsEnv) shipsAdded.enabled = (std::string(shipsEnv) != "0");
        layerMgr.setEnabled("ships", shipsAdded.enabled);
        viewer.addEventHandler(new ShipViewStateHandler(&layerMgr, shipLayer));
```

(e) AI 工具注册(`aiRuntime.tools` 可用的注册区,与 feed 工具注册同一位置风格):

```cpp
    if (aiRuntime.tools && shipLayer)
    {
        earthai::Tool t; t.name = "get_ships_summary";
        t.description = u8"查询当前视野内 AIS 船舶汇总:总数、按航速分桶(锚泊/慢速/巡航/高速)、"
            u8"最快船。数据来自 AISStream 实时流,覆盖范围为当前订阅视口;"
            u8"未配置 EARTH_AISSTREAM_KEY 或视野过大时 count 为 0。";
        t.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        ShipLayer* sp = shipLayer;
        t.execute = [sp](const picojson::value&) {
            picojson::value v; std::string err = picojson::parse(v, sp->summaryJson());
            if (!err.empty() || !v.is<picojson::object>())
            { picojson::object e; e["error"] = picojson::value("bad summary json"); return picojson::value(e); }
            return v;
        };
        aiRuntime.tools->add(t);
    }
```

(f) `ctrlUI->_satellites = ...` 旁边加 `ctrlUI->_ships = shipLayer;`。

- [ ] **Step 2: EarthControlUI.h 详情卡**(include 区加 `#include "ais_data.h"`,成员区加 `ShipLayer* _ships = nullptr;`,`_flight` 卡块之后镜像加)

```cpp
        if (_ships)
        {
            ShipInfo si = _ships->getSelected();
            if (si.valid)
            {
                earthui::Card card;
                card.id = "ship_detail";
                card.style.chipLabel = u8"船舶";
                card.title = u8"船舶详情 Ship";
                card.drawBody = [si]() {
                    ImGui::Text(u8"船名 Name: %s", si.name.empty() ? "?" : si.name.c_str());
                    ImGui::Text("MMSI: %lld", si.mmsi);
                    ImGui::Text(u8"航速 SOG: %.1f kn", si.sogKn);
                    ImGui::Text(u8"航向 COG: %.0f°", si.cogDeg);
                    ImGui::Text(u8"经纬 LatLon: %.3f, %.3f", si.lat, si.lon);
                    ImGui::Text(u8"数据龄 Age: %.0f s", si.ageSec);
                };
                card.onClose = [this]() { _ships->clearSelected(); };
                _cardStack.upsert(card);
            }
        }
```

- [ ] **Step 3: 离线 E2E(fixture,必须 EARTH_OFFSCREEN=1)**

```bash
cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | tail -3
cd /Users/USER/osgverse/build/sdk_core/bin
rm -f capture_*.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 EARTH_SHIPS=1 \
  EARTH_SHIPS_FILE=/Users/USER/osgverse/applications/earth_explorer/test/ais_fixture.jsonl \
  ./osgVerse_EarthExplorer --goto 22.3 114.15 200 2>&1 | grep -i "ship\|ais"
```
Expected: 日志含 `[Ship] fixture loaded 6 ships`(实现时打这行);截图香港海域可见 6 个不同颜色/朝向的箭头(0.2kn 那艘灰色、22.5kn 那艘亮黄)。目检箭头朝向与 fixture COG 一致(45°/180°/270°/90°/315°/135°)。

- [ ] **Step 4: Commit**

```bash
git add applications/earth_explorer/earth_main.cpp applications/earth_explorer/EarthControlUI.h
git commit -m "feat(earth): AIS ship layer wiring — layer directory, status subtitle, detail card, viewport handler, AI tool (P3 T6)"
```

---

### Task 7: 全量回归 + 打包 + HANDOFF

**Files:**
- Modify: `HANDOFF.md`(顶部新增本期章节,沿用「续N」格式)

- [ ] **Step 1: 全量单测**(5 个二进制全部 exit=0)

```bash
cd /Users/USER/osgverse/build/verse_core/bin
for t in osgVerse_Test_Feeds osgVerse_Test_Ai_Chat osgVerse_Test_TileOverlay osgVerse_Test_Satellite osgVerse_Test_Ais; do
  ./$t > /tmp/$t.log 2>&1; echo "$t exit=$?"
done
```

- [ ] **Step 2: 渲染回归**(全局默认视角 + 极点视角,classify_rb_swap.py 均 CLEAN + 人工目检截图——该脚本极区有已知假阳性,目检不可省)

```bash
cd /Users/USER/osgverse/build/sdk_core/bin
rm -f capture_*.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 ./osgVerse_EarthExplorer 2>&1 | tail -2
python3 /Users/USER/osgverse/applications/earth_explorer/test/classify_rb_swap.py capture_*.png
rm -f capture_*.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 ./osgVerse_EarthExplorer --goto 89 0 8000 2>&1 | tail -2
python3 /Users/USER/osgverse/applications/earth_explorer/test/classify_rb_swap.py capture_*.png
```

- [ ] **Step 3: 无 key 降级路径确认**(两层都未配置 key 时不联网、subtitle 提示正确、不崩)

```bash
cd /Users/USER/osgverse/build/sdk_core/bin
env -u EARTH_FIRMS_KEY -u EARTH_AISSTREAM_KEY EARTH_OFFSCREEN=1 EARTH_AUTOCAP=100 \
  EARTH_FIRES=1 EARTH_SHIPS=1 ./osgVerse_EarthExplorer 2>&1 | grep -iE "feed|ship|fires" | head
```
Expected: `[Feed] fires no url & no fixture, fetch disabled`;无网络请求日志;exit 0。

- [ ] **Step 4: 打包 + 签名验证**

```bash
bash /Users/USER/osgverse/packaging/package_macos.sh && codesign -v /Users/USER/osgverse/dist/EarthExplorer.app && echo SIGN_OK
```

- [ ] **Step 5: HANDOFF.md 顶部新增章节**(内容:P3 一期完成;两图层的 env 钩子/注册入口;真机待验清单——AISStream 真连接的视口跟随重连/断线重连/消息速率帧率、FIRMS 真实全球量级下三级聚合观感、两个 key 的注册地址;明确"WS 真连接从未测过"的诚实风险声明,同 P2 先例)

- [ ] **Step 6: Commit**

```bash
git add HANDOFF.md
git commit -m "docs(earth): HANDOFF — P3 phase-1 (AIS ships + FIRMS fires) complete, real-network verification pending user keys"
```

---

## 真机验证(计划外,需用户提供 key 后由主会话执行)

1. 用户注册两个 key:FIRMS `https://firms.modaps.eosdis.nasa.gov/api/map_key/`、AISStream `https://aisstream.io/`(注册后台生成)。
2. `EARTH_OFFSCREEN=1` + 真 key 各跑一轮:FIRMS 看全球真实量级下三级聚合与日志 N 值;AIS 在香港/新加坡海域视角看真实船流、视口平移触发重连(日志)、断网重连退避。
3. 通过后交用户真机交互验收(真实窗口由用户自己开)。
