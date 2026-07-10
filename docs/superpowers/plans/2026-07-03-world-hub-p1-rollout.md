# 世界信息枢纽 P1:零门槛铺量 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 图层数×3——新增 6 个无 key 数据源(EONET/GDELT/GPSJam/UNHCR/NHC/静态战略),其中 UNHCR 首用弧线原语、NHC 首用折线原语;FeedLayer 框架扩展线/弧几何;P1 末补事件流卡+顶部状态带。

**Architecture:** 全部沿用 P0 已验证模式——每源一个 `feeds/<name>_feed.cpp`(FeedSpec 配置,≤120 行)经 `registerFeedLayer` 一站接入(轮询/点渲染/拾取/详情/LayerManager/env 钩子/AI 工具全自动);线/弧几何经 T1 框架扩展挂到同一 sync 回调,渲染走 T3 已交付的 `earthgeo::buildArcGeometry/buildPolylineGeometry`(不碰 globe 管线)。spec 见 `docs/superpowers/specs/2026-07-03-world-info-hub-roadmap.md` P1 节 + `docs/superpowers/research/2026-07-03-worldmonitor-analysis.md`。

**Tech Stack:** C++11/OSG/ImGui/libhv/picojson(仓库自带);测试=单测(新 osgVerse_Test_Feeds)+ fixture E2E + EARTH_* 钩子 + EARTH_OFFSCREEN 真渲染截图。

**约定(同 P0):** 构建 `cmake --build build/verse_core --target install --config Release`;E2E 全部 `EARTH_OFFSCREEN=1` + `DYLD_LIBRARY_PATH=build/sdk_core/lib`,fixture 绝对路径;截图写 /tmp 需非沙箱 shell;提交前缀 `feat(earth-feed)`;**红线:不碰 globe 着色器/太阳/海洋;既有 E2E(AI 7 组+quakes/gdacs/flights fixtures+EARTH_PRESET)全程绿;OSG 日志宏 if/else 必加大括号**。每任务双审查(规格+质量)后才进下一任务。

## 文件结构(职责锁定)

| 文件 | 职责 |
|---|---|
| Modify `feed_layer.h/.cpp` | T1:FeedSpec 增可选 parseGeometry(线/弧);FeedPoint 增可选 unixTime;sync 时重建几何子图 |
| Create `tests/feed_layer_tests.cpp` | T1:feed/geo 相关单测从 ai_chat_tests.cpp 迁出(新 NEW_TEST 目标,治"杂烩") |
| Create `feeds/eonet_feed.cpp` / `gdelt_feed.cpp` / `gpsjam_feed.cpp` / `unhcr_feed.cpp` / `nhc_feed.cpp` | T2-T6:每源一文件,FeedSpec 实例 |
| Create `applications/earth_explorer/data/strategic/*.json` + `feeds/strategic_feed.cpp` | T7:静态战略数据集(vendored JSON+署名) |
| Create `applications/earth_explorer/test/{eonet,gdelt,gpsjam,unhcr,nhc}_fixture.*` | 各源真实响应 fixture |
| Modify `EarthControlUI.h` / 新增 `event_ticker.h` | T8:右上事件流卡(信息 UI 铁律:右上、独立、可关)+ 顶部微状态带 |
| Modify `earth_main.cpp` / `CMakeLists.txt` / `tests/CMakeLists.txt` | 各任务接线;T9 预设扩军 |

---

### Task 1: FeedLayer 线/弧几何扩展 + 测试拆分

**Files:** Modify feed_layer.h/.cpp;Create tests/feed_layer_tests.cpp;Modify tests/CMakeLists.txt、tests/ai_chat_tests.cpp(迁出)。

- [x] **Step 1: 接口锁定 + 失败单测先行**(写进新 tests/feed_layer_tests.cpp)

```cpp
// feed_layer.h 追加(接口锁定,实现细节可调):
namespace earthfeed
{
    struct FeedArc      // O-D 大圆弧(UNHCR 等流向类数据)
    {
        osg::Vec3d llaA, llaB;            // (lat°, lon°, altM)
        osg::Vec4 colorA{1,1,1,1}, colorB{1,1,1,1};
        double heightScale = 0.12;        // 传给 buildArcVertices
    };
    struct FeedLine     // 球面折线(NHC 路径/锥边界、光缆等)
    {
        std::vector<osg::Vec2d> latLonDeg;
        osg::Vec4 color{1,1,1,1};
        double liftMeters = 15000.0;      // 折线默认抬升(P0 结论:贴地线会被地形淹没)
    };
    struct FeedGeometry { std::vector<FeedArc> arcs; std::vector<FeedLine> lines; };
    // FeedSpec 增(可选,不设=纯点源,现有 5 个源零改动):
    //   std::function<FeedGeometry(const std::string& body)> parseGeometry;
    // FeedPoint 增(可选):double unixTime = 0;  // T8 事件流卡排序用,0=无时间
}
```

单测:①喂 2 弧+1 线的 FeedGeometry → 断言 buildArcVertices/buildPolylineVertices 被正确参数化(顶点数、首尾 ECEF 与 convertLLAtoECEF 一致);②parseGeometry 未设时行为与现状完全一致(现有 gdacs/usgs parse 单测迁过来后原样全绿)。

- [x] **Step 2: 实现**:FeedLayerRuntime 的 sync 回调里,points Geode 旁挂一个 geometry Group(每次刷新整组替换;arcs 合一个 buildArcGeometry 调用、lines 逐条 buildPolylineGeometry);拾取仍只对点(线/弧 v1 不可拾取,注释说明)。
- [x] **Step 3: 测试迁移**:ai_chat_tests.cpp 中 earthfeed::/earthgeo::/LayerManager/FeedSelection 五个块整体迁入 feed_layer_tests.cpp(`NEW_TEST(osgVerse_Test_Feeds feed_layer_tests.cpp)`);ai_chat_tests.cpp 只留 AI 块。两个测试目标都 exit 0。
- [x] **Step 4: 回归**(quakes/gdacs fixture E2E + 零影响)后 **Commit** `feat(earth-feed): feed geometry extension (arcs/lines) + test split`

### Task 2: NASA EONET 事件点源

**Files:** Create feeds/eonet_feed.cpp、test/eonet_fixture.json;Modify CMakeLists.txt、earth_main.cpp(注册行)。

- [x] URL `https://eonet.gsfc.nasa.gov/api/v3/events?status=open&days=30`;parse:`events[]` → 每 event 取 `geometry[]` 最新一条 Point(coords=[lon,lat]);**排除 category id "earthquakes"**(与 USGS 重复)、"wildfires" 只留 48h 内(worldmonitor 同款过滤);13 类 category→点色映射(火山红/风暴蓝/海冰青/野火橙…全表写在源文件注释);title=类别+标题,detail=类别/日期/坐标,url=event.link;unixTime=geometry.date。
- [x] 单测(fixture 喂 parse,断言点数/过滤/颜色);fixture E2E `EARTH_EONET=1 EARTH_EONET_FILE=<abs>` → `[Feed] eonet loaded N=...`;真网冒烟 1 次;AI 工具自动注册验证(`get_eonet_summary`)。
- [x] **Commit** `feat(earth-feed): NASA EONET natural-events layer`(报实际行数,目标 ≤120)

### Task 3: GDELT GEO 新闻热点源

- [x] URL `https://api.gdeltproject.org/api/v2/geo/geo?query=(conflict OR protest OR military)&format=geojson&timespan=60m`(query 写在 spec 里可后调);parse:features[] → 点,`properties.count`(mention 数)**≥5 才收**(worldmonitor 降噪同款),sizePx=6+min(10, count/10),色=热度黄→红渐变;detail=name+count+shareimage 链接;refreshSeconds=900(源 15min 更新)。
- [x] 同 Task 2 流程(单测/fixture `EARTH_GDELT*`/真网冒烟/commit `feat(earth-feed): GDELT GEO news-hotspot layer`)。

### Task 4: GPSJam GPS 干扰源

- [x] URL `https://gpsjam.org/data/<YYYY-MM-DD>.geojson`(UTC 昨日,启动时算;404 回退再前一天);parse:H3 hex 面 → **退化为中心点**(research 认可):properties 里干扰等级 low/med/high → 黄/橙/红,sizePx 8/10/12;detail=hex id+等级+"数据源 gpsjam.org(源自 ADS-B)";refreshSeconds=3600(每日源,勤刷没意义)。
- [x] 同流程;fixture 注意存一份含三档等级的真实响应;commit `feat(earth-feed): GPSJam GPS-interference layer`。

### Task 5: UNHCR 流离失所 O-D 弧线源(弧原语首用)

- [x] URL `https://api.unhcr.org/population/v1/population/?limit=200&yearFrom=<去年>&coo_all=true&coa_all=true`;parse **用 parseGeometry**:取 refugees 字段 TOP 40 条 coo→coa 流(<10 万人的丢弃),国家 ISO3→质心经纬度用**内置静态表**(新 header `feeds/country_centroids.h`,~200 国 constexpr 表,一次生成写死);FeedArc:colorA 蓝→colorB 红、heightScale=0.10+人数缩放;**同时**产出 points(coa 收容国质心点,sizePx 按人数)供拾取/详情(弧不可拾取的补偿);detail=来源国→收容国+人数+年份+"UNHCR CC-BY"。subtitle 署名必须有。
- [x] 单测重点:ISO3 查表命中/缺失丢弃不崩;TOP-N 排序;弧参数正确。E2E:`EARTH_UNHCR=1 EARTH_UNHCR_FILE=<abs>` → 日志 + **离屏截图亲验弧线在球上可见**(controller 肉眼验收,首个弧线消费者)。
- [x] commit `feat(earth-feed): UNHCR displacement O-D arcs (first arc consumer)`。

### Task 6: NOAA NHC 飓风路径+预报锥(折线原语首用)

> ⚠️ **T1 遗留性能债 + 已知雷区(动手前必读)**:每次 sync 整组重建几何会触发 1+L 次着色器 compile/link(NHC 十余条线可感帧抖)。**禁止用"全局 static map 共享 osg::Program"方案**——已实证(4ee4c74d 回退记录)它会引发全球瓦片非确定性染色(疑与 osgVerse createShaderDefinitions/ShaderLibrary 全局状态交互)。安全思路:FeedLayerRuntime **实例内**复用上一轮几何 Group 的 StateSet/Program(替换 Geometry 的顶点数据或仅新建 Geometry 挂旧 StateSet),不跨实例、不全局。改后必须跑 EARTH_ARC_DEMO 离屏截图 ×3 确认无染色。

- [x] **先探测**(实现前 curl):`https://mapservices.weather.noaa.gov/tropical/rest/services/tropical/NHC_tropical_weather/MapServer?f=json` 列 layers,找 Forecast Track(线)/Forecast Cone(面)/Past Track 的 layer id;每层 `…/<id>/query?where=1%3D1&outFields=*&f=geojson`。**飓风季外可能 0 要素:fixture 用历史响应存档,真网冒烟允许 N=0(日志照常)。**
- [x] parse 用 parseGeometry:Track LineString→FeedLine(白色路径+按强度分色段可后加,v1 单色);Cone Polygon→**取外环退化为 FeedLine 边界线**(v1 不做面填充,P0 既定降级);风暴当前位置→FeedPoint(name/风速/气压 detail)。
- [x] 同流程;commit `feat(earth-feed): NOAA NHC hurricane track and cone outline`。

### Task 7: 静态战略数据集

- [x] **Step 1 调查**:定位 worldmonitor 仓库(research 文档引用 `scripts/seed-military-bases.mjs`、`data/`;GitHub 搜 worldmonitor,克隆到 scratchpad)→ 找到军事基地 226/战略港口 62/核设施/航天发射场/AI 数据中心 313 的 JSON;**确认 LICENSE 允许搬运**(worldmonitor 是 AGPL,数据文件本身多为公开数据集汇编——逐文件看头注释/来源,不明确的跳过并在报告里列明)。找不到仓库或 license 不明 → 只做能从原始公开源直接拿的(如 Epoch AI 数据中心 CC-BY),其余 BLOCKED 报告。
- [x] **Step 2**:选定文件转成统一 schema 存 `applications/earth_explorer/data/strategic/<name>.json`(带 `_source`/`_license` 字段);`feeds/strategic_feed.cpp` 一个 FeedSpec **注册多个 LayerManager 图层**(军事基地/港口/核设施/发射场/数据中心各一层,同组"战略 / Strategic",fixtureEnv 各自独立如 EARTH_BASES_FILE——若框架一 spec 一层限制太死,注册 5 个 spec,每个 ≤40 行);静态源 refreshSeconds=0(只读本地文件,零网络——registerFeedLayer 需支持 file:// 或空 url+fixture 常载,T1 顺带留好)。
- [x] 单测+E2E+commit `feat(earth-feed): static strategic datasets (bases/ports/nuclear/spaceports/datacenters)`。

### Task 8: 事件流卡 + 顶部微状态带(P1 末 UI)

- [x] **事件流卡**(信息 UI 铁律:右上、独立 Begin、[x] 可关):新 `event_ticker.h`(header-only 仿 ai_cards 模式);数据源=各 FeedLayer 最新 points 按 unixTime 降序 TOP 20(feed_layer.cpp 加一个只读全局访问 `collectRecentEvents(n)`,主线程调用);每行=时间+源名+title,点击 → `EarthManipulator::setByEye` 飞到该点(复用 fly_to 逻辑)。默认关,图层目录旁加开关。
- [x] **顶部微状态带**:屏幕顶部居中一条细 ImGui 窗(NoTitleBar/NoInputs 除关闭):UTC 时钟 + 数据源健康 `n/m`(m=启用的 feed 数,n=最近一次抓取成功的;FeedLayerRuntime 记 lastFetchOk/lastFetchTime,暴露只读)+ 当前预设名(LayerManager 记 lastAppliedPreset)。
- [x] 视觉验收:离屏截图(状态带+事件流卡开启,EARTH_PRESET=灾害 造数据)controller 肉眼验收;零影响(两者默认关/不挡拾取)。
- [x] commit `feat(earth): event ticker card and top status strip`。

### Task 9: P1 收尾

- [x] 预设扩军:灾害={gdacs,quakes,eonet,nhc};新增 军事/Military={bases,…静态层}+实时含 gdelt;"全部"补全新层(仍避开 clouds/precip 冲突);
- [x] 全量回归矩阵(P0 的 19 项 + 新源 fixtures + 预设新组合日志断言 + 4 类历史回归截图);
- [x] 验收 demo(roadmap 定义):开「灾害」预设 → AI 问"过去 24 小时全球有哪些红色预警?"(EARTH_AI_FAKE 脚本化)→ 图表卡+逐条 fly_to 日志断言;
- [x] HANDOFF 顶部新章节 + roadmap P1 打勾;`bash packaging/package_macos.sh`;commit+push(controller 统一)。

## Self-Review

- Spec 覆盖:roadmap P1 表 7 行——GDACS(P0 已有)✓ EONET(T2)✓ NHC(T6)✓ GDELT(T3)✓ UNHCR(T5)✓ 静态战略(T7)✓ GPSJam(T4)✓;"每源 AI 工具"=registerFeedLayer 自动 ✓;验收 demo(T9)✓;"P1 末事件流卡+状态带"(T8)✓;线/弧首用依赖的框架扩展(T1)✓。
- 类型一致性:FeedArc/FeedLine/FeedGeometry/parseGeometry/unixTime 各任务引用一致;T5/T6 都走 parseGeometry;T7 依赖 T1 的"空 url 静态源"支持(已在 T1 Step 备注)。
- 已知取舍:线/弧不可拾取(v1)、NHC 锥不做面填充、GPSJam hex 退化中心点、线宽 1px(macOS core 限制,P1 不做屏幕空间扩线——观感不行再立项)、GDELT query 关键词后调。

---

## 执行记录(2026-07-04,subagent-driven 双审查,21 个 commit)

- **T1** `3867305d`+`d0434f0d`+`4ee4c74d`:几何扩展+测试拆分(osgVerse_Test_Feeds);审查修复中的"全局 Program 缓存"后被证实与瓦片染色无因果但仍回退(见下),move 快照交接等 5 项保留。
- **T2 EONET** `8337bc76`+`c296bd64`(120 行);**T3 GDELT** `3cb45c27`+`6c854673`(116 行;⚠️端点当时全网 404,fixture 按官方文档手工构造,**服务恢复后需真网核对字段名**——T9 复核仍 404);**T4 GPSJam** `6b89f8af`+`df029933`(实测偏差:CSV+H3 cell 无坐标 → 新增 feeds/h3_lite.h 667 行解码移植(uber/h3 v4.1.0 Apache-2.0,4.7 万 cell 对拍官方 Python 绑定零失配)+gzip 自剥);**T5 UNHCR** `38c4b904`(弧首用,140 行+251 国质心表);**T6 NHC** `b3ced7b7`(6a StateSet 实例内复用还性能债)+`695a4324`+`e988de59`(216 行;多层=parse 内补拉 6/7 层;飓风季外 N=0 合法,**首个真实风暴出现时值得复验**);**T7 静态战略** `2ea840f9`+`3dab390a`(license 逐源审计:弃 worldmonitor AGPL 自建表,全部改用原始公开源;⚠️bases=CC BY-NC 非商用,三处标注);**T8 事件流卡+状态带** `813960ea`+`87c7ca4e`(审查抓真数据竞争:ImGui=draw 线程 vs update 线程 _records move,已锁+快照修复);**T9** `32cd2f2e`(预设扩军+gdacs unixTime+验收 demo fixture)。
- **重大排查(独立提交)**:`56b4014e` 瓦片非确定性 R/B 染色 → 真凶=jpg/png 解码插件注册竞态(stb=RGB vs macOS imageio=BGRA,同二进制两次运行解码器可不同),已预载 verse_image 确定化;EARTH_IMG_DEBUG=1 诊断钩子;test/classify_rb_swap.py 自动分类器(海洋像素 R/B 判定;**低空陆地视图会误报,只断言 reds==0 或目检**)。GL 层最后一环未闭环(0/45 无法复现;损坏窗口与并发构建二进制混装重合,取证已不可恢复)——复发时先跑 EARTH_IMG_DEBUG=1。
- **T9 回归矩阵 A-J 全绿**;零影响断言标准(T9 起):无功能钩子运行,排除 `" (static)"` 行后 [Feed]/[Ticker]/[Status]/[GeoPrim]/[Preset]/[AIChat] 共 0 条。昆明截图需 EARTH_FRAME_SLEEP_MS=30 才流到深瓦片。
- **教训**:①绝不在代理构建期间并行跑实验/构建(二进制混装伪造了"回退修好了"的假象,耗数小时);②commit 前 build 输出 grep "error:" 必须为 0(NHC 润色曾拿旧二进制假绿提交,已 amend);③代理自称"视觉验证通过"不可信,controller 必须亲验截图(本轮两次靠这个拦住)。
- **遗留(P2 前)**:GDELT 真网字段核对(服务恢复时);NHC 真实风暴复验;线宽 1px 屏幕空间扩线(观感不行再立项);flight/precip 未迁框架(bespoke,维持);低空 classify 约定入矩阵文档。
