# 项目进度对账说明 —— 给"P4 + AlphaEarth"新 session

> 写于 2026-07-06,给准备执行 `docs/superpowers/plans/2026-07-06-p4-worldtools-p6a-science-quickwins.md`(P4 情报分析师 + P6a 科学速赢包,含 P6b AlphaEarth 附录)的新 session。
> **用途**:那份 P4 计划写于今天 03:13,基线假设是"P3 收尾后的 master"。但之后项目又推进了一大截(现在在 **v0.19**),恰好改动了 P4 计划要碰的文件。**执行 P4 前必须先按本文档把计划与当前代码对账、做微调**,否则计划里的行号/上下文/测试链接假设会失效。
> 用户主语言中文,请用中文回复。macOS / Apple Silicon。

---

## 0. TL;DR 当前状态

- **HEAD = v0.19**(commit `b82570ec`),本地远程同步,工作树干净(除了 P4 计划本身 + AlphaEarth 调研两个**未跟踪**文档,见 §6)。
- 路线图(`docs/superpowers/specs/2026-07-03-world-info-hub-roadmap.md`)进度:**P0 平台化 / P1 零 key 铺量 / P2 卫星层 / P3 动态流(AIS+FIRMS)全部完成并打标**。P4(情报分析师)+ P6a(科学栅格)= 你要执行的计划,尚未开始。
- P3 之后**额外**做了三件计划没预料的大事,它们改了 P4 要碰的文件:
  1. **标记系统重设计(vision 加强版,v0.19)**——每类信息一个可辨形状 + 面板三面联动,核心是新增 `marker_style.h/.cpp` 单一真源,并**重写了 feed_layer 的点精灵着色器 + 给 registerFeedLayer 加了图层目录 icon 接线**。
  2. **卫星接入 AI**——新增 `get_satellites_summary` 工具(earth_main 里,读 `SatelliteLayer::summaryJson()`),AI 现在能定位 ISS/天宫。
  3. **API key 磁盘持久化 + AIS 闸门放宽 + 点标记防闪烁**(见 §5 硬规则)。

---

## 1. AI 现在能读的全部数据(P4 要在此基础上扩)

P4 的目标是给 AI 补"跨源情报合成"。先知道 AI **现在**已有哪些工具(避免重复/冲突):

**查询类**(读数据):`get_view_state` / `get_flights_summary` / `get_ships_summary` / `get_satellites_summary`(**本会话新增,计划不知道**) / `get_<8feed>_summary`(fires/quakes/gdacs/gdelt/gpsjam/eonet/unhcr/nhc) / `get_<5战略>_summary`(bases/ports/nuclear/spaceports/datacenters)。
**动作类**:`fly_to` / `set_layer` / `show_chart` / `generate_photo` / `generate_video`。

**两种已确立的"图层→AI 工具"范式,P4 可复用**:
- **feed 框架自动注册**:`registerFeedLayer` 里自动挂 `get_<id>_summary`(读 `FeedSpec.summaryJson` 或缺省实现)。P1/P3 的 feed 源都靠这个。
- **独立图层手动注册**(earth_main):`get_ships_summary`(ships) / `get_satellites_summary`(satellite)——`if (aiRuntime.tools && xxxLayer) { earthai::Tool t; ...; t.execute = [layer](...) { parse layer->summaryJson(); ... }; aiRuntime.tools->add(t); }`。P4 Task 2 的 world tools 接线应放在**这一堆手动注册块附近**(earth_main 约 1059-1090 行,get_ships_summary / get_satellites_summary 之后),保持风格一致。

P4 的 `AsyncJsonFetcher` + world tools 是**第三种范式**(工具直接打外部 API、异步 pending),与上面两种正交,可共存。

---

## 2. ⚠️ P4 计划逐任务对账(关键差异,必读)

计划的行号/上下文引用基于旧 master,以下是**必须现场重新定位**的点:

### Task 1(AsyncJsonFetcher,新建 ai_query.h/.cpp + world_tools_tests.cpp)
- 基本无冲突(全新文件)。但 `tests/CMakeLists.txt` 现在多了 `NEW_TEST(osgVerse_Test_Ais ais_tests.cpp)`(在 Satellite 之后)——计划说"加在 Feeds 之后",位置仍可,但确认现状再插。

### Task 2(ai_world_tools + earth_main 接线 + CMakeLists)
- `applications/earth_explorer/CMakeLists.txt` 的 `EXECUTABLE_FILES` 现在已含 `earth_config.cpp marker_style.cpp ais_math.cpp ais_data.cpp`(计划不知道)。加 `ai_query.cpp ai_world_tools.cpp` 时照现状追加即可。
- **新增源文件后必须 `cmake <build-dir>` 重新配置再构建**——否则新 .cpp 不进构建、静默用旧二进制(本会话踩过,排查半天)。
- earth_main 接线点:见 §1,放在 get_ships_summary / get_satellites_summary 手动注册块附近。

### Task 5(feed_layer 区域查询基建)—— **最大对账点**
`feed_layer.h/.cpp` 自计划以来大改,新增了:
- **标记形状管线**:`FeedLayerImpl` 有 `int _shapeId`(构造时 `= (int)earthmark::visualForLayer(spec.id).shape`);`buildGeodeFrom` 把 shapeId 写进 texcoord0.y;`feedVertCode` 有**前半球剔除段**(防闪烁,红线勿动) + `vShapeId` varying;`feedFragMain` 调 `markerCoverage`(共享形状库);`buildScene()` FS 拼 `earthmark::markerShapeGLSL()`。
- **cluster LOD**:`FeedSpec.cluster` 字段、`buildClusterLevels`、`_levelGeodes`/`_levelRecords`、`updateActiveLevel`(FIRMS 用)。
- **registerFeedLayer 里已加**:`OverlayLayer l` 现在还填 `l.shape/l.iconColor`(从 `visualForLayer(spec.id)`)。
- 计划 Task 5 要在 feed_layer 加 `RegionBriefProvider` 注册表 + `haversineKm` + `FeedLayerImpl::regionSummaryJson` + registerFeedLayer 挂接——这些**都能加,但插入点/上下文全变了**,实现者要读当前 feed_layer.cpp 现场定位,别信计划里的行号。FeedSelection 结构还在(计划的锚点),但其前后代码变了。

### Task 6(get_region_brief,world_tools_tests.cpp `#include feed_layer.cpp + geo_primitives.cpp`)—— **必修链接坑**
- feed_layer.cpp 现在 `#include "marker_style.h"` 且用 `earthmark::visualForLayer` / `markerShapeGLSL`。所以**任何 `#include feed_layer.cpp` 的测试单元,必须同时 `#include marker_style.cpp`**,否则 `earthmark::` 符号未定义、链接失败。
- 先例:`tests/feed_layer_tests.cpp` 已经这么做了(它 include 了 feed_layer.cpp + marker_style.cpp + earth_config.cpp)。**world_tools_tests.cpp 里 include feed_layer.cpp 时,照抄 feed_layer_tests.cpp 的 include 清单**(marker_style.cpp、earth_config.cpp、各 feeds/*.cpp、geo_primitives.cpp、ui_card.h 都要),否则编译/链接必挂。这是本会话反复踩过的同一类坑(被测 .cpp 加新依赖 → 所有 include 它的测试单元要补定义)。

### Task 7(NDVI/夜光,OVERLAY 槽互斥泛化)
- 标记系统**没碰**栅格 OVERLAY 逻辑(clouds/precip),Task 7 的泛化仍有效。但 earth_main 里 OverlayLayer 注册块因为加了 ships/satellite/marker 接线**行号全变**;`clouds`/`precip` 的注册块还在,现场定位。
- 注意 OverlayLayer 现在多了 `shape`/`iconColor` 两个外观字段(栅格层用不到,取默认 Circle+灰即可,不影响)。

### Task 8(GEBCO,geo_primitives + earth_main)
- `geo_primitives.h/.cpp` 本会话没大动(标记系统用的是 marker_style,不是 geo_primitives)。加 `mercatorTileBBox` 纯函数无冲突。earth_main createCustomPath 分支现场定位。

### Task 9(文档收尾)
- roadmap.md 标记 P4/P6a 完成时,注意 §P4 里 WHO/旅行警告/Yahoo/FRED 四个工具计划**本期不做**(计划已记录范围取舍)。

---

## 3. 计划可复用的新基建(计划不知道,能省事)

- **`earth_config.h` 的 `earthcfg::resolveKey(envName)`**:环境变量优先、回退磁盘 `keys.env`。P4 的 5 个 world 工具都免 key,暂用不上;但**若将来任何工具/图层要 key,一律走 resolveKey**(别再裸 getenv,双击 .app 读不到环境变量)。
- **`marker_style.h` 的 `visualForLayer(id)`**:若 P4/P6 新增**任何点标记图层**(feed 类),必须在 `marker_style.cpp` 的 `kReg` 登记 id→(形状,代表色),否则地球上是灰圆点、面板 icon 也是灰圆点。P4 的科学层是**栅格**(NDVI/夜光/GEBCO,无点标记),world 工具是纯 AI(无图层),所以**大概率用不到**——但 get_region_brief 或未来点源要知道。
- **cluster LOD**(feed_layer):高密度点源可设 `FeedSpec.cluster` 自动分级(FIRMS 先例)。P4 用不到,记录备查。

---

## 4. 怎么起步(构建/测试/离屏/key)

- 构建:`cmake --build /Users/USER/osgverse/build/verse_core --target install`(**禁止并发第二个构建**,会互毁)。新增源文件后先 `cmake /Users/USER/osgverse/build/verse_core` 重配。
- 测试二进制:`/Users/USER/osgverse/build/verse_core/bin/`,现有 5 个:`osgVerse_Test_Feeds / _Ais / _Ai_Chat / _TileOverlay / _Satellite`。全绿基线 = 全部 exit 0。
- 离屏运行:**任何 `osgVerse_EarthExplorer` 必须带 `EARTH_OFFSCREEN=1`**(用户铁律,裸跑会弹窗抢前台打断用户)。截图落 `/tmp/earth_capture_0.png`,截图前先 `rm -f` 防陈旧。
- key:已配在 `$HOME/Library/Application Support/EarthExplorer/keys.env`(FIRMS + AISSTREAM + AI 三个真 key,600 权限,仓库外)。离屏测真网时 `env` 里带对应 `EARTH_*_KEY`(或从 keys.env cut 出来),或靠 resolveKey 磁盘回退。
- 打包:`bash packaging/package_macos.sh && codesign -v dist/EarthExplorer.app`。桌面 `~/Desktop/EarthExplorer.app` 软链自动指向 dist 产物,用户双击它。

---

## 5. 注意事项(硬规则,违反必出事)

1. **前半球剔除段勿动**:`feed_layer.cpp`/`flight_data.cpp`/`ais_data.cpp` 的顶点着色器里有 `if (dot(Pv.xyz - Cv, Pv.xyz) >= 0.0) { gl_Position = vec4(2.0,2.0,2.0,1.0); return; }`——这是防"拉远转动地球标记闪烁"的修复(v0.18),改点着色器时必须逐字保留(grep 计数应 feed/flight/ais=1、sat=0)。P4 大概率不碰点着色器,但若碰要知道。
2. **打包后绝不再运行 dist app**:app 运行会往 bundle 内写 imgui.ini,破坏 codesign 封印(`codesign -v` 报 "sealed resource missing")。功能验证一律用 `build/sdk_core/bin/` 二进制;打包是最后一步,之后不跑 dist。
3. **面板 ImGui 内容离屏截不到图**:`EARTH_AUTOCAP` 的 FBO 回读发生在 ImGui POST_DRAW 之前,所以图层目录/详情卡/AI 聊天面板**离屏无法截图验证**,只能代码审查 + 用户真机。P4 的 world 工具是 AI 聊天里的,同理——工具链正确性靠单测(fixture)+ 用户真机问答。
4. **OSG_WARN/OSG_NOTICE 是带 if 的宏**:if/else 分支里不加大括号会吞掉 else(一天踩两次)。
5. **`--goto` 第三参数是 km**(高度),不是米。
6. **单测零网络**:一律 fixture / 注入 fake fetch;真实 URL 只在手动 curl 冒烟步骤敲。P4 的 AsyncJsonFetcher 设计里有 fixturePath 参数正是为此。
7. **提交信息末尾** `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`;中文注释解释 why;新图层 subtitle 带数据来源署名(CC-BY 要求)。
8. **仓库约定**:直接在 master 提交,不建 worktree/feature branch(用户明确)。发布 GitHub Release 需用户明确同意(打 tag + push commits 不算 Release,可自行做但打 tag 时机问用户)。

---

## 6. 待解决 / 待优化 / 待确认的问题

**真机待确认(需用户双击验证,非阻塞)**:
- **标记系统面板 icon**:图层目录每项前的迷你徽章 + 详情卡 chip,离屏截不到,待用户真机看形状/颜色/对齐是否正常(地球上的标记形状已离屏验过)。
- **卫星 AI**:用户在 AI 聊天问"ISS 在哪 / 带我飞到 ISS",确认能答能飞(真网离屏已验 summaryJson 返回真实 ISS 坐标,但完整 AI 问答链需真机 + AI key)。

**已知遗留 Minor(不阻塞,记录备查)**:
- **flight/ship/sat 三个遗留自定义着色器层的 shapeId 硬编码在 3 处**(FS 常量 + visualForLayer 查表 + 详情卡),当前逐位一致,但改查表不会带动它们——维护陷阱。若 P4 之外要收口,让这三层从查表派生 shapeId。9 个 feed 层无此问题(真运行时单源)。
- **两个规划文档未跟踪**:`docs/superpowers/plans/2026-07-06-p4-worldtools-p6a-science-quickwins.md` 和 `docs/superpowers/research/2026-07-06-alphaearth-earth2-rs-apis.md` 从未 `git add`(是那个规划 session 留下的)。P4 session 对账/微调后,应把(调整后的)计划 + 调研 + 本状态文档一并提交。

**更早的历史遗留(与 P4 无关,极低优先,仅备案)**:
- 偶发输入死锁(疑 ImGui 粘捕获),`EARTH_INPUT_DEBUG` 钩子待抓,未解。
- 昆明瓦片轻微错位(原始问题,历次会话未根治)。
- 高峰轻微穿模(用户接受、不修)。

**AlphaEarth 接入要点(P6b,来自调研文档)**:
- AlphaEarth 走 `source.coop` 免费镜像可直接接(CC-BY),不需 Google key。Earth-2 无托管 API,用 AIFS 平替。ECMWF 2025-10 已全开放。全文见 `docs/superpowers/research/2026-07-06-alphaearth-earth2-rs-apis.md`。P6b/P6c 计划文档里说"各自另出 plan"——即 AlphaEarth 的详细实现计划还没写,P4 session 若要推进到 AlphaEarth,需先 brainstorm→spec→plan(基于调研文档)。

---

## 7. 执行建议(给 P4 session)

1. 先读:本文档 → `docs/superpowers/plans/2026-07-06-p4-worldtools-p6a-science-quickwins.md` → `docs/superpowers/research/2026-07-06-alphaearth-earth2-rs-apis.md` → `HANDOFF.md` 顶部续10/续9/续8。
2. 按 §2 逐任务把计划与当前 feed_layer.cpp / earth_main.cpp / tests 现状对账,**改掉计划里失效的行号/上下文/测试 include 清单**(尤其 Task 5/6 的 feed_layer + Task 6 的 marker_style.cpp 链接坑)。
3. 对账后微调计划,再 subagent-driven 逐任务执行(实现子代理→审查子代理→修复,同本项目一贯节奏)。
4. AlphaEarth(P6b)是独立后续,需先出自己的 spec/plan,不在 P4 计划的 9 个 task 内。
