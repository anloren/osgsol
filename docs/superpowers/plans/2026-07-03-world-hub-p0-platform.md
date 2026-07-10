# 世界信息枢纽 P0:平台化改造 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让"新增一个世界数据源"从 ~400 行降到 ~100 行配置:FeedLayer 配置驱动框架(含 AI 工具自动注册)+ 弧线/折线渲染原语 + 图层目录 UI(搜索+场景预设),并以 GDACS 灾害源与地震层迁移自证。

**Architecture:** 沿用已验证模式——libhv 轮询 worker → 主线程 SyncCallback 挂几何 → GL_POINTS/LINES 独立着色器(不碰 globe 管线)→ LayerManager 注册 → `get_<id>_summary` Tool 自动生成。spec 见 `docs/superpowers/specs/2026-07-03-world-info-hub-roadmap.md` P0 节。

**Tech Stack:** C++11/OSG/ImGui/libhv/picojson(全部仓库自带);测试沿用 osgVerse_Test_Ai_Chat 单测 + fixture E2E + EARTH_* 钩子。

**约定(同 AI Chat 计划):** 构建 `cmake --build build/verse_core --target install --config Release`;E2E 从 build/sdk_core/bin 跑,fixture 用绝对路径;提交前缀 `feat(earth-feed)`/`refactor(earth-feed)`;**红线:不碰 globe 着色器/太阳/海洋;所有既有 E2E(AI 7 组+quake/flight fixtures)全程保持绿。**

## 文件结构(职责锁定)

| 文件 | 职责 |
|---|---|
| Create `applications/earth_explorer/feed_layer.h/.cpp` | FeedSpec 配置结构 + FeedLayerRuntime(轮询/解析/点渲染/拾取/详情卡数据/summary)+ registerFeedLayer() 一站式接线 |
| Create `applications/earth_explorer/geo_primitives.h/.cpp` | buildArcGeometry(大圆弧 O-D)/ buildPolylineGeometry(球面折线)+ 专用着色器 |
| Create `applications/earth_explorer/feeds/gdacs_feed.cpp` | GDACS 源定义(FeedSpec 实例,≤100 行自证) |
| Modify `applications/earth_explorer/quake_data.*` | 迁移到 FeedLayer(保留详情卡/钩子语义) |
| Modify `applications/earth_explorer/EarthControlUI.h` | 图层目录:搜索框+场景预设行(注:该文件已较大,只加不改老段落) |
| Modify `applications/earth_explorer/LayerManager.h` | preset 支持(命名启用集合) |
| Modify `ai_setup.cpp`/`earth_main.cpp`/`CMakeLists.txt` | 接线 |
| Modify `tests/ai_chat_tests.cpp` | FeedSpec 解析/geo 原语顶点数学单测 |

---

### Task 1: FeedLayer 框架 + GDACS 首个用户

**Files:** Create feed_layer.h/.cpp、feeds/gdacs_feed.cpp、test fixture `test/gdacs_fixture.json`;Modify CMakeLists.txt、earth_main.cpp、ai_setup.cpp(或由 registerFeedLayer 内部完成 Tool 注册)。

- [x] **Step 1: 接口定义 + 单测先行**(tests/ai_chat_tests.cpp 追加,CHECK 宏)

```cpp
// feed_layer.h 核心(实现细节可调,接口锁定)
namespace earthfeed
{
    struct FeedPoint
    {
        double lat = 0, lon = 0;      // 度
        float sizePx = 8.0f;          // 点大小
        osg::Vec4 color{1,1,1,1};
        std::string title, detail;    // 详情卡标题/正文(已格式化)
        picojson::value raw;          // 原始记录(summary 用)
    };
    struct FeedSpec
    {
        std::string id, displayName, group, subtitle;   // LayerManager 语义一致
        std::string url;                                 // {无占位,整 URL}
        int refreshSeconds = 300;
        std::string fixtureEnv;                          // 如 "EARTH_GDACS_FILE"
        std::string forceEnv;                            // 如 "EARTH_GDACS"(=非0 启动即开)
        std::function<std::vector<FeedPoint>(const std::string& body)> parse;
        std::function<std::string(const std::vector<FeedPoint>&)> summaryJson;  // 缺省=count+按 title 前缀分桶
        std::string toolDescriptionCn;                   // get_<id>_summary 的中文描述
    };
    // 一站式:建层(点渲染+拾取+详情数据)+ LayerManager 注册 + env 钩子 + AI Tool 注册。
    // 返回挂 sceneCamera 的节点。内部复用 quake 的线程/同步/着色器模式。
    osg::Node* registerFeedLayer(const FeedSpec& spec, osgViewer::Viewer& viewer,
                                 LayerManager* layers, earthai::ToolRegistry* tools);
}
```
单测(纯逻辑,不建 viewer):GDACS parse 函数喂 canned JSON → 断言点数/经纬度/分级颜色映射;默认 summaryJson 的计数正确。

- [x] **Step 2: 实现框架**——从 quake_data.cpp 提炼:FetchThread(libhv GET+fixture 分支)、SyncCallback(dirty→主线程重建 Geode)、点着色器(quake 的 VS/FS 参数化:统一顶色由 FeedPoint.color 顶点属性给)、拾取(复用 quake 的 pick handler 模式,详情卡走既有右上卡片——用 AICardPanel::pushChart? 不:详情卡沿用 quake 的独立右上详情窗模式,FeedLayer 持有 selected FeedPoint,EarthControlUI 通用绘制一个「要素详情」卡,所有 feed 共用一个卡位)。
- [x] **Step 3: GDACS 源**(feeds/gdacs_feed.cpp):url=`https://www.gdacs.org/gdacsapi/api/events/geteventlist/MAP`,parse:eventtype/alertlevel→图标色(红/橙/绿→绿过滤),title=类型+国家,detail=时间/等级/影响;summaryJson:总数+按 alertlevel、eventtype 分桶。**行数目标 ≤100(含注释),在 commit message 里报实际行数。**
- [x] **Step 4: fixture E2E**:抓一份真实响应存 `test/gdacs_fixture.json`(≥5 事件,含红橙);headless `EARTH_GDACS=1 EARTH_GDACS_FILE=<fixture>` → 日志断言 `[Feed] gdacs loaded N=...`;AI fixture 脚本调 `get_gdacs_summary` → 断言 count 日志。真网冒烟一次(无 key)。
- [x] **Step 5: 回归**:AI 全套 E2E + quake/flight fixture + 零影响(无钩子时 GDACS 层默认关、零网络)。
- [x] **Step 6: Commit** `feat(earth-feed): config-driven feed layer framework + GDACS source`

### Task 2: 地震层迁移到 FeedLayer(自证兼容)

**Files:** Modify quake_data.h/.cpp(大幅缩身)、earth_main.cpp。

- [x] Step 1: 用 FeedSpec 重写 USGS 源(parse/summary 从现实现平移);**保语义**:EARTH_QUAKES/EARTH_QUAKES_FILE 钩子、图层 id "quakes" 与显示名、详情卡字段(震级/深度/地点/时间/USGS 链接)、点色/大小随震级映射。
- [x] Step 2: QuakeLayer 对外接口(EarthControlUI/_quake 指针、get_quakes_summary)改由 FeedLayer 通用路径提供;删除 quake 专属线程/着色器代码(预计 -300 行)。
- [x] Step 3: 回归:quakes fixture E2E(count=3)+ AI chart E2E(依赖 get_quakes_summary)全绿;真网冒烟。
- [x] Step 4: Commit `refactor(earth-feed): migrate USGS quakes onto feed framework`
- **注**:flight 层**不迁移**(视口 bbox 抓取+每帧外推是 bespoke 能力,框架 v1 不吞,注释说明)。

### Task 3: geo_primitives 弧线/折线原语

**Files:** Create geo_primitives.h/.cpp;Modify CMakeLists.txt、tests(顶点数学单测)。

- [x] Step 1: 单测先行:`buildArcVertices(llaA, llaB, n, heightScale)` 纯函数(返回 ECEF 顶点数组):断言首尾点贴地(半径≈R)、中点隆起(半径>R)、大圆路径经度单调;`buildPolylineVertices(vector<lla>)` 贴地+分段。
- [x] Step 2: 实现几何构建(osg::Geometry LINE_STRIP,每弧独立 primitiveset)+ 弧线着色器(顶点色渐变 O→D + 可选流动动画 uniform)+ 折线着色器(纯色/虚线可后加)。深度:默认 depth test on、线宽 2;不接大气。
- [x] Step 3: demo 验证层(不进产品):`EARTH_ARC_DEMO=1` 画 3 条固定弧(京→巴黎 等)+ 1 条折线,headless 截图肉眼验收(弧形平滑、隆起自然、极区不破);验收后 demo 钩子保留(测试用)。
- [x] Step 4: Commit `feat(earth-feed): great-circle arc and polyline primitives`

### Task 4: 图层目录 UI(搜索 + 场景预设)

**Files:** Modify LayerManager.h(preset)、EarthControlUI.h(目录节)、earth_main.cpp(预设定义)。

- [x] Step 1: LayerManager 增 `struct Preset{ name; vector<string> enabledIds; }` + `applyPreset(name)`(先全关再开列表;"base/labels" 不动)+ presets() 注册。
- [x] Step 2: EarthControlUI 图层节升级:顶部一行预设按钮(干净|灾害|实时|全部——earth_main 注册,P1 后扩军事/经济/太空)+ 搜索框(InputTextWithHint,按 displayName/group 过滤树)+ 既有分组树保留;匹配高亮组自动展开。
- [x] Step 3: 视觉验收截图(预设点击切换层组合正确、搜索过滤即时);零影响(不动其它节)。
- [x] Step 4: Commit `feat(earth-feed): layer catalog with search and scene presets`

### Task 5: 收尾——回归/文档/打包

- [x] 全量回归矩阵(AI 7 组 E2E、quake/flight/GDACS fixtures、零影响、4 类地球历史回归截图);
- [x] HANDOFF 顶部新章节(FeedLayer 用法示例=GDACS 全文引用,钩子表增 EARTH_GDACS*、EARTH_ARC_DEMO);roadmap P0 打勾;
- [x] `bash packaging/package_macos.sh`(带 key 环境);
- [x] Commit + push(controller 统一 push)。

## Self-Review
- Spec P0 四项:FeedLayer(T1)✓ 原语(T3)✓ 目录 UI(T4)✓ 工具工厂(T1 内 registerFeedLayer 自动注册)✓;GDACS 演示源验收(T1)✓ quake 迁移自证(T2)✓。
- 占位符:详情卡"通用要素详情窗"的字段=FeedPoint.title/detail,quake 迁移时以其现有卡为验收基准——已明确。
- 类型一致性:FeedSpec/FeedPoint/registerFeedLayer 签名各任务引用一致。

---

## 执行记录(2026-07-03,subagent-driven 双审查)

- **T1** `ccf95fbc`(+审查修复):FeedLayer 框架 + GDACS(108 行自证)。
- **T2** `8a2d9fb4` + `00ed3f32`:quakes 迁移,净 -258 行,quake_data.{h,cpp} 删除;双审查过。
- **T3** `4b024e74` + `633875ea`(审查修复:折线逐点贴局部椭球面,原跨赤道段陷地 ~9km):
  geo_primitives(earthgeo),slerp 大圆 + sin(πt) 弧高,对跖安全;单测含独立路径(Bowring
  convertECEFtoLLA)红→绿验证。已知限制注释在头文件:macOS core 线宽钳 1px、alpha 未开混合。
- **T4** `ff5d4e7d` + `caa6aa56` + `9fd11858`:目录 UI(预设按钮行+搜索过滤+组归并修重复标题)
  + FeedSelection.sourceId 关层只清本源;EARTH_PRESET 钩子。⚠️两次踩 OSG_WARN/OSG_NOTICE
  宏 dangling-else(无大括号分支吞 else),全库已排查。
- **T5**:EARTH_OFFSCREEN 重做为 HeadlessCGLContext(`30584b3f` + `ec1caaba`,纯 CGL GL4.1
  Core + 自建 FBO,构造性不弹窗;旧 pbuffer=GL2.1 全着色器挂、截图假绿)+ 顺带修
  osgdb_verse_image 写出漏翻转(随机倒置根因)。回归矩阵 19/19(修 dangling-else 后)
  全绿;4 类历史回归截图人工验收通过。
- **不迁移**:flight 层(bespoke 视口 bbox+外推,框架 v1 不吞)。
