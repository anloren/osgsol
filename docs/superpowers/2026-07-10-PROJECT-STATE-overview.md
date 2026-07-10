# osgVerse 项目整体现状(客观说明)—— 2026-07-10

> 本文档为**下一个 session 的整体项目现状说明**,只陈述事实,不含建议/优先级/下一步推荐。
> 权威状态源:本文件 + `docs/superpowers/2026-07-09-SESSION-HANDOFF.md`(上批次交接)+ memory 索引。
> 用户主语言中文。平台 macOS / Apple Silicon。

---

## 1. 项目定位(客观摘录)

- `README.md:5`:"osgVerse, a complete 3D engine solution based on OpenSceneGraph."
- `AGENTS.md`:osgVerse 是基于 OpenSceneGraph(OSG)的完整 3D 引擎,提供现代渲染管线(PBR、延迟着色、实时阴影)+ 物理、动画、UI。C++14/C++17,CMake 3.10+。
- 仓库同时是**引擎本体**(核心库)与**下游应用**(`applications/`)的单体仓库。当前活跃开发集中在 `applications/earth_explorer`(EarthExplorer,一个 3D 地球实时数据可视化 app)。

---

## 2. 仓库结构 / 核心引擎模块(引擎本体)

顶层 `CMakeLists.txt` 构建顺序(`:750-784`):`3rdparty → readerwriter → pipeline → animation → modeling → ai → script → ui → wrappers → plugins`;条件子目录 `helpers/3dsmax_exporter / wasm / tests / applications / assets`。各模块经 `NEW_LIBRARY` 生成独立静态/动态库。

| 目录 | 库名 | 文件数 | LOC(cpp+h) | 职责(代表文件) |
|------|------|--------|------------|------------------|
| `readerwriter/` | osgVerseReaderWriter | 41 | ~14,627 | IO/场景加载/窗口/分页。FBX/GLTF/KTX 加载(`LoadSceneFBX/GLTF.cpp`)、`DatabasePager.cpp`、`TileCallback.cpp`、窗口后端(`GraphicsWindowGLFW/SDL/Win32NV.cpp`)、`EarthManipulator.cpp`、`MaterialGraph.cpp`、`DracoProcessor.cpp` |
| `pipeline/` | osgVersePipeline | 54 | ~18,195 | 渲染管线本体。`Pipeline.cpp`/`PipelineStandard.cpp`/`ShaderLibrary.cpp`/`ShadowModule.cpp`/`LightModule.cpp`/`SkyBox.cpp`/`Drawer2D.cpp`/`NISUpscaler.cpp`;`CudaUtils/`(.cu)、`MusaUtils/`(.mu)GPU 内核 |
| `modeling/` | osgVerseModeling | 24 | ~10,860 | 几何/网格处理。变形/拓扑/合并/FFD/高斯几何/纹理映射、`Math.cpp`、`Octree.h` |
| `plugins/` | osgdb_*(27 个) | 54 | ~19,436 | OSG ReaderWriter 插件集,27 个 `osgdb_*` 子目录(gltf/fbx/3dtiles/ktx/webp、leveldb/mbtiles/odbc、ffmpeg/nvcodec、osm/mvt/shp/geojson/tms/terrain/potree/vdb 等) |
| `ui/` | osgVerseUI | 30 | ~5,160 | ImGui 集成。`ImGui.cpp`/`ImGui3D.cpp`/`SceneHierarchy.cpp`/`SceneNavigation.cpp`、`serializers/`(13) |
| `wrappers/` | osgVerseWrappers | 141 | ~5,141 | OSG 对象序列化包装器(反射/持久化),`generic_osg/`(136 文件逐类 wrapper) |
| `animation/` | osgVerseAnimation | 15 | ~4,965 | 动画/仿真。ozz 骨骼动画、BlendShape、物理(`PhysicsEngine.cpp`)、粒子(Effekseer/U3D)、补间 |
| `ai/` | osgVerseAI | 11 | ~3,466 | ONNX 推理(`OnnxRuntimeEngine.cpp`)、Recast 寻路(`RecastManager*.cpp`)、MCP 服务端(`McpServer.cpp`) |
| `helpers/` | (工具/导出器,非运行时核心) | 360 | ~138,553 | `toolchain_builder/`(790 文件,含打包的第三方构建源码=LOC 主体)、DCC 导出器(3dsmax/unity/blender) |
| `assets/` | (运行期资源) | — | — | `shaders/`(54 `.glsl`)、`models/`、`textures/`、`skyboxes/`、`misc/` |

`VerseCommon.h` 是汇总头,仅 include 各模块公共头(modeling/pipeline/animation/readerwriter/wrappers)。

### Shader 位置(客观)
- `pipeline/` 目录内**无** `.glsl/.vert/.frag`;所有 shader 在 `assets/shaders/`(54 个 `.glsl`)。
- 三类:标准延迟管线 `std_*`(由 `standard_pipeline.json` 装配)/ 可复用模块 `*.module.glsl` / 地球天空海洋 POI。
- 约定"不碰 globe GLSL"对应文件:`assets/shaders/` 下的 `scattering_globe.*`、`global_ocean.*`、`atmosphere.*`、`scattering_sky.*`、`poi_symbols.*`。

### 3rdparty(主要库)
libhv(网络)、imgui、picojson/rapidjson/rapidxml、Eigen、sgp4(卫星轨道)、ozz(骨骼)、recastnavigation、meshoptimizer、tiny_gltf/ufbx、pmtiles、shapelib、laszip、sqlite3/leveldb、pybind11、marl(任务调度)、ktx/stb/tinyexr、VHACD/xatlas/quadriflow(网格)等。

---

## 3. EarthExplorer 应用(`applications/earth_explorer`,当前活跃开发)

主目录 ~13,626 LOC + `feeds/` ~2,380 LOC。其它 app(同 `applications/` 下):`viewer`、`osgearth_viewer`、`qml_viewer`、`qt_viewer`、`scene_editor`、`sdl_es_viewer`、`viewer_composite`。

### 主要文件(按 LOC 降序,前 13)
`earth_main.cpp`(1537,入口+离屏基建+图层/工具注册)、`ai_media.cpp`(1499,AI 生图/生视频)、`feed_layer.cpp`(1030,Feed 框架+LOD 聚合)、`sat_data.cpp`(856,卫星 TLE/SGP4)、`ais_data.cpp`(656,AIS 船舶)、`EarthControlUI.h`(605,主控 UI+详情卡)、`ai_chat.cpp`(524,AI 对话核心)、`city_data.cpp`(515)、`flight_data.cpp`(490,OpenSky 航班)、`ai_setup.cpp`(485,AI 工具注册)、`ai_world_tools.cpp`(447,世界查询工具)、`ai_cards.cpp`(433,卡片面板)、`ui.cpp`(405)。

### Feed 数据源(8 个,`feeds/`,spec.id + 上游端点)
| id | 名称 | 端点 |
|----|------|------|
| `gdelt` | 新闻热点 | `api.gdeltproject.org/api/v1/gkg_geojson`(GKG GeoJSON 1.0) |
| `quakes` | 地震 | `earthquake.usgs.gov/.../2.5_day.geojson` |
| `fires` | 火点 | `firms.modaps.eosdis.nasa.gov/api/area/csv/`(FIRMS) |
| `nhc` | 飓风 | `mapservices.weather.noaa.gov/tropical/rest/services/tropical/NHC_tropical_weather_summary/MapServer/{5,6,7}`(点/轨迹/锥) |
| `gdacs` | 灾害 | `gdacs.org/gdacsapi/api/events/geteventlist/MAP` |
| `eonet` | 自然事件 | `eonet.gsfc.nasa.gov/api/v3/events` |
| `gpsjam` | GPS 干扰 | `gpsjam.org/data/{date}-h3_4.csv` |
| `unhcr` | 难民 | `api.unhcr.org/population/v1/population/` |

Feed 框架:`registerFeedLayer`(feed_layer.cpp)统一注册;每源含 `parse`/`parseGeometry`/`summaryJson`;三态+空态 fetchState(0=idle/1=ok有数据/2=失败/3=成功但空);`FeedSpec.fixtureEnv` 离线 fixture 旁路。

### 非 Feed 实时数据层
`sat_data`(CelesTrak TLE + SGP4 传播)、`flight_data`(OpenSky 航班)、`ais_data`(AIS 船舶 WS 流)、`precip_data`(RainViewer 降水雷达,复用 GIBS OVERLAY 槽)、`city_data`(城市)、`tiles3d_data`(3D Tiles,如香港实景三维)。叠加层(OverlayLayer):GIBS 影像/云图、GEBCO、NDVI、夜光等(互斥组)。

### AI 系统
- `ai_chat.cpp`(AIChatCore,Gemini,worker 线程 + 主线程 FRAME drain 执行工具)、`ai_ui.cpp`(底部对话条)、`ai_cards.cpp`(卡片面板)、`ai_media.cpp`(生图/生视频)、`ai_setup.cpp`/`ai_world_tools.cpp`(工具注册)、`ai_query.cpp`(AsyncJsonFetcher 异步抓取)。
- **AI 工具清单**:`get_weather_forecast`、`get_crypto_prices`、`get_country_indicator`、`get_chokepoint_traffic`、`get_prediction_markets`、`get_region_brief`、`get_satellites_summary`、`get_ships_summary`、`get_news_content`、`show_chart`,+ 每个 feed 源动态 `get_<id>_summary`(8 个),+ 生图/生视频/fly_to 等;`ai_tools.h` 定义 `Tool{name,description,parametersJson,execute}` + `ToolRegistry`。
- 离线测试:注入式 `FetchFn` + fixture 文件,零网络。

---

## 4. 构建 / 测试 / 打包(客观)

- **构建树**:`build/verse_core`。**install 前缀**:`build/sdk_core`。
- 构建单测:`cmake --build build/verse_core --target <TestTarget> -j4`
- 构建全量 app:`cmake --build build/verse_core --target install -j4`(装到 `build/sdk_core`)
- app 二进制:`build/sdk_core/bin/osgVerse_EarthExplorer`
- 打包:`bash packaging/package_macos.sh` → `dist/EarthExplorer.app`(修 rpath/写 Info.plist);之后 `codesign -v dist/EarthExplorer.app`。
- 运行 app 需 `EARTH_OFFSCREEN=1`(离屏,不抢前台);52 个 `EARTH_*` 环境钩子(测试/调试开关,如 `EARTH_OFFSCREEN/EARTH_AUTOCAP/EARTH_FRAME_SLEEP_MS/EARTH_*_FILE` fixture 等)。

### 测试(`tests/`,57 文件 ~14,006 LOC;NEW_TEST 目标 15+)
Earth 相关:`osgVerse_Test_Feeds`(feed_layer_tests.cpp,1929)、`osgVerse_Test_Ai_Chat`(683)、`osgVerse_Test_WorldTools`(308)、`osgVerse_Test_Satellite`(164)、`osgVerse_Test_Ais`、`osgVerse_Test_TileOverlay`、`earth_test.cpp`(429)。引擎相关:`csg_stencil_test`、`atmospheric_scattering`、`gaussian_splatting_test`、`paging_lod_test`、`volume_rendering_test`、`pipeline_test`、`reverse_depth_test`、`forward_pbr_test`、`physics_basic_test`、`shadow_test` 等。测试用 `CHECK` 宏(非 assert);Earth 测试沿用 `#include 被测.cpp` 先例(改 .cpp 自动重编进测试单元)。

---

## 5. 版本控制状态

- **master** = `6991f074` = **origin/master**(已同步,差异 0)。
- **最新 tag** = `v0.22`(annotated,`42d93b63`,已推)。tag 序列:v0.8/v0.9/v0.9.1/v0.16..v0.22。按 memory,仅 v0.8 是 GitHub Release,其余为内部里程碑 tag。
- 其它本地分支(未删,非本次范围):`feat/earth-hk-3dtiles`、`feature/earth-improvements`。
- 远程:`github.com/anloren/osgverse`。

### v0.22 内容(29 提交,已合 master)
P0 并发安全冲刺(6)+ 真机验收批次(earthcfg 配置注册表+设置面板 / HTTP 重试+提示分类 / 工具循环可配置+优雅收尾 / 角标自消失 / 相机穿地硬海平面地板 / 干净预设清轨道线 / 失败可见三态)+ GDELT 换源 GKG + feed 超时重试 + 新闻摘要(get_news_content + gdeltSummaryJson 带 URL + 详情卡 AI 摘要按钮)+ feed 空态可见(state3「· 当前无数据」)。全程 subagent-driven + 双层审查 + opus 全分支终审 = Ready to merge YES 无 Critical。

---

## 6. 文档结构

- `docs/superpowers/`:`specs/`(设计文档)、`plans/`(实现计划)、`research/`(调研)、`*-SESSION-HANDOFF.md` / `*-PROJECT-STATUS-*.md`(交接)、`2026-07-08-project-review-optimization-backlog.md`(review 优化 backlog)。
- 顶层 `HANDOFF.md`(~124KB,面向新会话的 earth_explorer 逐会话变更日志)、`tasks/lessons.md`(排坑细节)、`AGENTS.md`(代理指南)、`CODE_STYLE.md`(风格:4 空格/100 列/`{` 换行,改编自 Google Filament)。
- memory 索引:`~/.claude/projects/-Users-USER-osgverse/memory/MEMORY.md`(每条一行指针 + 独立 .md)。

---

## 7. 已记录的开放项(客观清单,非建议)

以下项在文档/审查中被记录为"存在但未处理",客观列举:

**review 优化 backlog**(`docs/superpowers/2026-07-08-project-review-optimization-backlog.md`,P1-P3 未做):pickAt 陈旧快照命中 / DRY 巨型文件拆分 / 死代码清理 / 更多测试覆盖 / syncIfDirty 结构改图×draw 竞态。

**v0.22 opus 全分支终审记录的项**(非硬阻断):
- `feed_layer.cpp:215-224` feed 重试循环退出时最坏阻塞 ~163s(`requests::request` + 退避 `microSleep` 不查 `_done`;GDELT `timeoutSeconds=40 × retries`)。交接文档已记录/接受。
- `EarthManipulator.h:518-551` 近/远平面防御分支是死代码(所在 callback 在 `earth_main.cpp:894-896` 注释掉);穿地防护实际生效的是 `EarthManipulator.cpp` 的 `updateTerrainFloor()` 硬海平面地板。
- `ai_chat.cpp:719-751` 撞工具上限后强制无工具轮的理论性无界循环(Gemini 生产不可达)。
- `feed_layer.cpp` `fetchOnce()` 成功时不清 `_lastErrorText`(UI 仅在 state2 读,无可见影响)。

**新闻摘要 / feed 空态已知 Minor**:`stripHtmlToText` 主线程解析大 HTML 单帧 hitch / `&amp;` 先解致双重解码 / `removeHtmlBlock` 裸子串匹配 `<script` / AI 摘要 prompt 拼未清洗 title+url(功能固有:本就喂不可信正文)/ feed 空态 1 帧级自愈竞态(`_fetchState=1` 先于 `_records` syncIfDirty)/ utf8split 测试对该 bug 无判别力。

**验证门(非缺陷)**:相机穿地硬海平面地板需真机四件套(全球/低空/极点/倾角不回归)验收;离屏截不到 ImGui 面板/卡片/图层行。

---

## 8. 约定与已知坑(客观)

- 不碰 globe GLSL(`assets/shaders/scattering_globe.* / global_ocean.* / atmosphere.* / scattering_sky.* / poi_symbols.*`)。
- 禁止并发构建(同时刻只跑一个 `cmake --build`)。
- 运行 app 必带 `EARTH_OFFSCREEN=1`;离屏低 LOD 网络流式需 `EARTH_FRAME_SLEEP_MS` 给 wall-clock。
- 打包后 `codesign -v`(签名失效→秒 SIGKILL);macOS rpath 用 `@loader_path`(APPLE 分支)。
- commit message 结尾:`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。
- `OSG_WARN/OSG_NOTICE` 是带 if 的宏,if/else 分支不加大括号会吞 else。
- 测试 `CHECK` 宏(NDEBUG 吞 assert);被测 .cpp 加新依赖后,所有 `#include` 它的测试单元都要补定义否则链接失败。
- GDELT/GKG、NOAA 端点 TLS 偶发闪断,离屏验证多试几次;别高频 curl(限流)。
- 更多排坑细节:`tasks/lessons.md` + memory 各条。
