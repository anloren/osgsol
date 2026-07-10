# osgVerse EarthExplorer — 工作交接（HANDOFF）

> 给**全新会话**的接手文档（你没有之前的上下文）。先读这份，再读 `tasks/lessons.md`（全部排坑细节）。
> 用户主语言中文，请用中文回复。macOS / Apple Silicon。

---
## ✅ 2026-07-06 会话(续10)— 卫星层接入 AI 完成:`get_satellites_summary` 工具 + 真做实 `summaryJson`,补上 AI 唯一数据盲区(最新,先读这个)

**背景/动机**:用户实测发现——手动飞到 ISS 后问 AI"这是什么/我在哪",AI 答不上来,因为卫星层是当时**唯一没有接入 AI 工具**的数据源(船舶有 `get_ships_summary`、航班/地震/火点等 P1 六源都有对应 feed 工具,`SatelliteLayerImpl::summaryJson()` 之前是硬编码桩 `"{\"count\":0}"`)。两任务方案(T1 纯函数 + T2 接线),本次完成 T2(收尾)。

**T1 回顾**(`6ee8aaee`,已完成):`sat_math.h/.cpp` 新增 `earthsat::SatSummaryEntry`(最小输入结构,`category` 用 int,0=站/1=导航/2=气象/3=Starlink,不依赖 `sat_data.h` 的 `SatCategory` 枚举以保持纯函数可独立单测)+ `earthsat::buildSatelliteSummaryJson(vector<SatSummaryEntry>)`——纯函数,输出 `{count, byCategory:{station,navigation,weather,starlink}, iss:{found,noradId,latDeg,lonDeg,altKm,speedKmS}, tiangong:{同上}, note}`。ISS=NORAD 25544、天宫=NORAD 48274,找不到则该对象只有 `{found:false}`。

**T2(本次)三处改动**:
1. **`applications/earth_explorer/sat_data.cpp`** `SatelliteLayerImpl::summaryJson()`:把桩实现换成真逻辑——把 `_allPrecise`(精选组:站/导航/气象)+ `_allStarlink` 两个内部快照逐条转成 `earthsat::SatSummaryEntry`(`category` 做 `(int)` 转换,`SatCategory` 枚举序与 `SatSummaryEntry.category` 约定的 0/1/2/3 天然一致),调 T1 的纯函数生成 JSON。线程约定沿用 `flight_data.cpp` 的先例:`_allPrecise`/`_allStarlink` 只在主线程 `syncIfDirty()` 写,`summaryJson()` 也只从主线程调用(AI 工具执行 + 诊断 handler 都在主线程 FRAME),不加锁。
2. **`applications/earth_explorer/earth_main.cpp`** 新增 `get_satellites_summary` AI 工具(注册于 `get_ships_summary` 块之后):查询卫星总数/分类分布/ISS+天宫经纬度高度速度。**关键设计**——`execute` 里若 `Station` 类目未开启会自动 `setCategoryEnabled(SatCategory::Station, true)`,这样用户没手动勾选卫星图层也能问"ISS 在哪"(Station 组小,自动开不影响性能;Nav/Weather/Starlink 仍需用户手动开,尤其 Starlink ~7000 点不该被一句查询拉起)。若开启后数据还在抓取中(`iss.found=false`),返回体里加 `loadingNote` 提示"稍后再查一次"而不是让 AI 误判"没有 ISS"。
3. **只读诊断钩子**:`SatFetchStatusHandler::handle()`(已有的每-FRAME handler,成员指针名 `_sat`)里加 `EARTH_SAT_SUMMARY_DEBUG` 环境变量门控——设置时每 ~120 帧 `std::cout` 打印一次 `[SatSummary] <json>`,未设置时零开销（`static const bool` 一次 `getenv` 判断）。这条钩子是本次离屏真机验证的关键——没有它就只能靠真机 AI 对话验证，而 AI 对话链路涉及 LLM key，不适合作为自动化回归的一部分。

**红线严格守住**:只做数据暴露（读 `_allPrecise`/`_allStarlink` 现成快照 + 调用已有 public `isCategoryEnabled`/`setCategoryEnabled`），完全未碰 TLE 抓取、SGP4 传播、渲染/着色器（含前半球剔除片段）、拾取、线程模型、缓存。

**离屏真实网络验证**(`EARTH_OFFSCREEN=1 EARTH_SATS=1 EARTH_SAT_SUMMARY_DEBUG=1 --goto 20 100 20000`):`[Sat] Parsed 214 satellites (precise, network, 6 groups)` 确认真实 CelesTrak 抓取成功后，`[SatSummary]` 连续多帧打印真实 JSON：`count=214`、`byCategory={navigation:117, starlink:0, station:23, weather:74}`（Starlink 本次未开启，0 符合预期）、`iss.found=true`（`latDeg≈34.6~35.8`、`lonDeg≈162.3~163.9`，随帧推进符合 ISS 轨道运动、`altKm≈426`、`speedKmS≈7.658`——LEO 轨道量级完全合理）、`tiangong.found=true`（`altKm≈403`、`speedKmS≈7.670`，同样合理）。证明真实抓取数据经 `summaryJson()` 正确流到 AI 工具可读的 JSON。

**回归**：5 个单测二进制(`osgVerse_Test_Satellite`/`osgVerse_Test_Feeds`/`osgVerse_Test_Ais`/`osgVerse_Test_Ai_Chat`/`osgVerse_Test_TileOverlay`)全部 `exit=0`；额外跑一次 `EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 EARTH_SATS=1` 确认卫星渲染截图正常产出（`summaryJson` 只读新增，不影响渲染路径）。

**打包**：`packaging/package_macos.sh` 成功产出 `dist/EarthExplorer.app`；`codesign -v` 通过（`SIGN_OK`）。打包后未运行 dist app（沿用既有教训，所有功能验证都在打包前的 `build/sdk_core/bin` 二进制完成）。

**真机待验清单**（用户在 AI 聊天里操作即可）：
1. 问"现在天上有多少卫星" —— AI 应调 `get_satellites_summary` 答出总数+分类。
2. 问"ISS 在哪" —— AI 应报出合理经纬度（且若空间站类目之前未开启，回答里应体现"已自动开启/数据加载中，请再问一次"这类措辞，而不是生硬报错）。
3. 问"带我飞到 ISS" —— AI 应先查 `get_satellites_summary` 拿到 `iss.latDeg/lonDeg`，再调用既有 `fly_to` 工具飞过去，验证两个工具的组合调用链路通畅。

**env 钩子新增**：`EARTH_SAT_SUMMARY_DEBUG`（只读诊断，未设置时零开销）。

---
## ✅ 2026-07-06 会话(续9)— 标记系统重设计(vision 加强版)完成:每类信息标记形状可辨,全量回归+打包通过,真机视觉待验

**背景/动机**:此前所有信息层的标记要么是圆点要么是三角,颜色不同但形状高度雷同,肉眼在缩略图/多层叠加场景下很难区分"这是火点还是船"。本次给每个信息层分配一个独一无二的几何形状,同时保证地球标记、图层目录 icon、详情卡 chip 三处徽章"同形同色"——不再各自为政。

**架构:单一真源** `earthmark::visualForLayer(layerId)`(`applications/earth_explorer/marker_style.h` / `.cpp`)——查表返回 `{MarkerShape, 代表色}`。三个消费方全部读它,改一处三处同步:
1. **地球标记着色器**:`markerShapeGLSL()` 返回一段共享 GLSL 代码字符串,拼进各点精灵 Fragment Shader,提供 `float markerCoverage(int shapeId, vec2 pointCoord, float headingRad)`——0..1 覆盖率(边缘 smoothstep 抗锯齿,<=0 discard)。`shapeId` 通过 `texcoord0.y` 传给 FS(`texcoord0.x` 仍是既有的 `gl_PointSize`)。可旋转形状(Arrow/Ship)按 `headingRad` 先转正再算 SDF。
2. **图层目录 mini-icon** + **详情卡 chip**:`drawMarkerIcon(ImDrawList*, shape, cx, cy, sizePx, colorU32)`——ImGui 侧手绘对应形状(多边形/圆环/矩形组合),视觉上与 GLSL 版本一一对应。

**形状分配表**(11 种 `MarkerShape` 枚举,`marker_style.h:11-12`):

| 图层(id) | 形状 | 说明 | 代表色(面板 icon) |
|---|---|---|---|
| ais/ships | Ship(长五边形) | 随航向 `headingRad` 旋转,五点艏尖艉平 | 青绿 `(0.208,0.878,0.816)` |
| flights | Arrow(三角箭头) | 随航向旋转 | 蓝 `(0.302,0.659,1.0)` |
| fires (FIRMS) | StarBurst(八角星芒) | 按 FRP 值配色(不变) | 橙红 `(1.0,0.353,0.188)` |
| quakes / nhc(飓风) | Ring(同心环) | 圆环+实心内点 | quakes 紫、nhc 青 |
| gdacs(灾害) | WarnTriangle(警告三角) | 不随向旋转 | 红 `(1.0,0.231,0.278)` |
| gdelt(新闻) | Diamond(菱形) | | 黄 `(1.0,0.761,0.227)` |
| gpsjam | Hexagon(六边形) | | 粉 `(1.0,0.353,0.784)` |
| eonet(自然事件) | Teardrop(水滴) | 圆头+尖尾 | 绿 `(0.294,0.847,0.420)` |
| unhcr(难民) | Circle(圆,不变) | 保持原形状,弧线视觉不变 | 蓝 `(0.239,0.482,1.0)` |
| satstations/satnav/satwx/starlink | SatBox(方框+十字准星) | | 金 `(1.0,0.824,0.227)` |
| bases/ports/nuclear/spaceports/datacenters(战略) | Square(实心方块) | 5 个战略源统一形状 | 灰蓝 `(0.604,0.643,0.698)` |

未登记的图层 id 回退 `{Circle, 中性灰 (0.7,0.7,0.72)}`,不会因为遗漏注册而崩或显示异常颜色。数值配色(FIRMS 按 FRP、quakes 按深度等既有渐变)完全不变,`visualForLayer` 只提供图层代表色(用于面板 icon),不覆盖逐点数值配色逻辑。

**关键 commit**(由旧到新,基线 `aa10c570` 计划提交):

| Commit | 内容 |
|---|---|
| `b3ecab35` | T1:`marker_style.h/.cpp` 新建——`MarkerShape` 枚举 + `visualForLayer` 单一真源登记表 + 共享 GLSL 形状库 + ImGui `drawMarkerIcon` |
| `924ff375` | T2:通用 feed 框架接入——`shapeId` 从 `visualForLayer(spec.id)` 取值,经 `texcoord0.y` 传给共享 FS,所有走 `registerFeedLayer` 的静态/半静态源自动获得形状 |
| `21dd8e31` | T3:三个自定义渲染层各自接入共享 GLSL 形状库——flight=Arrow(按航向)、ship=Ship 长五边形(按 COG 航向)、satellite=SatBox |
| `dcd9070c` | T4:面板联动——图层目录每项 mini-icon、详情卡 chip 形状,全部改读 `visualForLayer`,不再各写各的临时形状/颜色 |
| `2d42691b` | T4 修复:补线 flights 的 OverlayLayer 图层目录 icon(遗漏挂到注册表,此前是默认灰点,现在与地球标记/详情卡箭头一致) |

**红线守住**:本次改动的边界严格限定"只动外观"——四个自定义 shader 文件(`feed_layer.cpp`/`flight_data.cpp`/`ais_data.cpp`/`sat_data.cpp`)里,会话早前(2026-07 早些时候)加固的"前半球剔除"片段 `dot(Pv.xyz - Cv, Pv.xyz)` 逐字节保留,未被形状改动误删或误改:

```
applications/earth_explorer/feed_layer.cpp:1
applications/earth_explorer/flight_data.cpp:1
applications/earth_explorer/ais_data.cpp:1
applications/earth_explorer/sat_data.cpp:0   ← 卫星层从来没有这段剔除逻辑,0 是正确基线,非本次回归
```

数据获取、线程模型、拾取(picking)、图层注册、聚合聚类(cluster)、near/far 裁剪——全部未触碰,只改了"点精灵长什么样"和"面板 icon 画什么"。

**本次(Task 5)全量回归验证结果**:
1. 5 个单测二进制(`osgVerse_Test_Feeds`/`osgVerse_Test_Ais`/`osgVerse_Test_Ai_Chat`/`osgVerse_Test_TileOverlay`/`osgVerse_Test_Satellite`)全部 `exit=0`,日志确认非空跑通过(`Test_Feeds` 里新增的 `[OK] visualForLayer` 断言也过了)。
2. 渲染回归:全局默认视角 + 极点视角(`--goto 89 0 8000`)`classify_rb_swap.py` 均判 `CLEAN`,人工目检两张截图确认——全局视角海洋自然蓝色无异常,极点视角闭合星形扇面(85.05° 收敛几何固有形态)、格陵兰/加拿大/俄罗斯标注正常,无 R/B 通道互换痕迹。
3. 多层辨识度截图(`EARTH_FIRES=1 EARTH_SHIPS=1`,真实 FIRMS/AISStream key):`--goto 10 105 3000`(泰国湾/南海视角)拍到真实火点渲染为八角星芒(与旧版全圆点对比,形状鲜明);船舶因该视角/瞬时窗口内实时 AIS 流量稀疏未入镜——另用已知真机验证过船舶密集的新加坡海峡视角(`--goto 1.15 103.8 150`,同 P3 期真机验证坐标)复核,拍到 1 艘船渲染为随航向旋转的长五边形/箭头状,与火点星芒形状对比鲜明可辨,达成"一眼分清信息类型"的设计目标。
4. 打包:`packaging/package_macos.sh` 成功产出 `dist/EarthExplorer.app`;`codesign -v` 通过(`SIGN_OK`);桌面软链 `~/Desktop/EarthExplorer.app` 确认指向新产物。**打包后未运行 dist app**(遵守"运行会写 imgui.ini 破坏签名"的既有教训,所有功能验证都在打包前的 `build/sdk_core/bin` 二进制上完成)。

**诚实的局限声明(真机待验)**:图层目录 mini-icon 和详情卡 chip 的形状渲染**只做了代码走查 + 单测覆盖**(`visualForLayer` 返回值断言、`drawMarkerIcon` 分支存在性),**未做离屏视觉验证**——因为 `EARTH_OFFSCREEN` 的截图管线是 FBO 读回先于 ImGui 绘制(见 `earth_main.cpp` 里 `EARTH_AUTOCAP` 那段注释:"ImGui 面板不在截图里"),ImGui 面板类的可视内容天生无法被离屏截图捕获,只能靠真机双击验证。**真机待验清单**:
1. 图层目录每一项的 mini-icon 形状/颜色是否与地球上对应标记视觉一致(11 种形状逐一过一遍)。
2. 点击标记弹出的详情卡 chip 形状是否清晰、与图层目录 icon 是否同形同色。
3. 多层同时打开时(尤其战略层 5 个源全是 Square、卫星层 4 个源全是 SatBox)会不会因为同形状扎堆而重新变得难分——如果同类源本来就该视为一类(如"战略设施")这是设计预期,但值得用户过一遍确认观感。
4. 船舶 Ship 形状在真实密集航道(如新加坡海峡)大量船只同屏时的旋转朝向渲染是否稳定(本次回归只实拍到 1 艘,历史 P3 期验证过 6 船 fixture 规模,真实大流量未覆盖)。
5. 任何形状辨识度不满意,反馈后只需微调对应 GLSL SDF 函数(`marker_style.cpp` 里 `_sd*` 系列)或 `drawMarkerIcon` 对应 case 分支即可,架构上保证改一处、三个界面同步生效。

**env 钩子**:无新增(本次不引入新数据源,只复用 `feed_layer.cpp`/`flight_data.cpp`/`ais_data.cpp`/`sat_data.cpp` 既有的 `EARTH_FIRES`/`EARTH_SHIPS`/`EARTH_FLIGHTS`/卫星层开关)。

---
## ✅ 2026-07-06 会话(续8)— P3 一期:AIS 船舶 + FIRMS 火点接入完成,两层真机验证均通过,已打 v0.17

**AIS 真机验证(2026-07-06,用户提供 AISStream key)— 通过**:①独立 WS 探针核对真实字段形状——`MetaData.MMSI` 整数、`ShipName` 右侧空格填充(裁剪契约正好兜住)、`Message.PositionReport.{Latitude,Longitude,Sog,Cog}` 全部与实现假设一致,订阅 bbox `[lat,lon]` 角序确认正确(收到的船位全落在请求的新加坡海峡框内);②真流离屏运行(150km 新加坡海峡):箭头精灵正确渲染在主航道上、SOG 配色正确(青=巡航/蓝=慢速/灰=锚泊);③"视口跨度收窄→换订阅"路径在相机落位时被真实触发一次并在 10s 冷却下干净恢复(每轮两条 `[Ship] ws connected` 日志,均为设计内行为);④顺手补了网络模式的连接日志(commit `15637361`,此前网络模式零 stdout,只能截图盲验)。

**状态**:P3 一期(AIS 船舶层 + FIRMS 火点层)7 个任务全部完成,`superpowers:subagent-driven-development` 流程(实现子代理→规格审查子代理→质量审查子代理,独立派发独立复核),基线 `ab97aaf2`(计划提交)。已推送,**未打 tag**(等两层真机验证都齐再打)。

**FIRMS 真机验证(2026-07-06,用户提供 MAP_KEY)— 通过**:真实网络拉到 45,485 个火点(与预估 1e4~1e5 吻合),三级聚合离屏目检全部正确——8000km 非洲草原火带 1° 聚合点阵清晰可读 / 800km 0.25° 网格密而不糊 / 150km 原始火点呈真实火线簇团;坏 key HTTP 400 优雅降级不崩(终审 C-1 修复在真网失败路径上兜住)。**顺带抓到并修了一个真机才现形的坑**(commit `0bce859d`):DAY_RANGE=1 是"最近 1 个 UTC 日历日",VIIRS NRT 有数小时入库延迟,每天 UTC 凌晨(北京时间上午)会拿到 HTTP 200+只有表头的空 CSV、图层零火点——已改 `world/2`(昨+今),subtitle 同步改"最近 48h"。AIS 真机验证仍待用户提供 AISStream key(https://aisstream.io/),第一件事核对真实字段形状(fixture 按文档假设,从未真网验证)。

**关键 commit**(由旧到新):

| Commit | 内容 |
|---|---|
| `eb320289` | 聚合纯函数(网格聚类),含计划自带代码里 lat=90 的 off-by-one 修正 |
| `94e02c49` | `FeedLayerImpl` 聚合 LOD 接入:每级独立 Geode + 按相机高度切级 + 分级拾取;`registerFeedLayer` 对空 url 且非静态源不再起轮询线程 |
| `93967eb3`+`ad6be49c` | FIRMS 源(VIIRS CSV,FRP 配色,3 级聚类 LOD)+ 可见性修复:计划自带的"3km 抬升够用"假设被云南高原实测证伪(该处地形本身就 >3km)→ 新增 `FeedSpec.liftMeters` 字段,默认 3000 对既有 5 个静态源零回归,FIRMS 专设 9000(超珠峰);修复后三档高度像素计数(1/4/10 聚类点数)全部符合预期 |
| `50bbeedd`+`4e36a549` | AIS 纯函数层(消息解析、bbox 判定、订阅 JSON)+ 船名右侧空格裁剪的补钉测试 |
| `e936c4b2`+`889bdd5b` | AIS 模块:libhv WebSocket 三线程模型(网络线程/主线程/控制线程);修复 `pthread_cancel` 跳过 WS 拆除逻辑的未定义行为窗口——根治手段是控制线程 `setCancelState(PTHREAD_CANCEL_DISABLE)`,同时析构函数兜底;闸门边界加 10s 重连冷却防抖 |
| `ec2880fb` | AIS 全链路接线:图层目录开关、状态字幕、详情卡、视口跟随 handler、AI 工具接入;E2E 6 船箭头像素级验证通过;冷却期内误显"连接失败"文案的措辞修正 |

**env 钩子**:`EARTH_FIRMS_KEY` / `EARTH_FIRES` / `EARTH_FIRES_FILE`(FIRMS 火点);`EARTH_AISSTREAM_KEY` / `EARTH_SHIPS` / `EARTH_SHIPS_FILE`(AIS 船舶)。

**key 注册地址**:FIRMS `https://firms.modaps.eosdis.nasa.gov/api/map_key/`;AISStream `https://aisstream.io/`(注册后台生成 key)。**用户已答应注册后提供 key**。

**诚实风险声明(同 P2 先例)**:AIS 的 WebSocket 真实连接**从未测过**——目前实现全部是 fixture-only,aisstream 真实字段形状(`MetaData.MMSI`/`MetaData.ShipName`、`Message.PositionReport.*` 等)、订阅消息的坐标序、真实限流行为,全部是按官方文档假设实现的,**真机带 key 验证时字段形状是第一个要核对的**(文档和真实返回体不一致是完全可能的)。FIRMS 真实全球量级(约 1e4~1e5 点)下的三级聚合观感/性能也未验证过——本次 fixture 只有 13 个点,聚合逻辑本身过了单测,但真实密度下的视觉效果(聚类粒度是否合适、是否卡顿)是未知数。

**真机待验清单(留给用户 / 下一会话)**:
1. AIS 真实 WS 连接:aisstream.io 实际推送的 JSON 字段形状与假设是否一致(第一优先核对项);视口平移触发的重新订阅/重连是否按预期工作(日志确认);断线后的退避重连策略(10s 冷却)在真实网络抖动下是否合理;真实消息速率下的帧率与内存表现(先前实现只验证过 6 船 fixture 规模)。
2. FIRMS 真实全球量级(1e4~1e5 点)下三级聚合的视觉观感——聚类阈值/图标密度是否需要调参,以及该量级下的加载/渲染性能。
3. 两个 key 都未配置时的降级提示文案(FIRMS `"未配置 key"`、AIS `kNoKey`+中文字幕"未配置 key(EARTH_AISSTREAM_KEY)")在真实 UI 里的呈现是否清楚,是否需要在图层目录里加更醒目的引导。
4. FIRMS 拉取失败路径(网络抖动/非200)已在终审修复(C-1),真机弱网下确认不崩即可。

**教训(本期新增)**:
1. **计划假设被 E2E 证伪 ×2**——这是本期最值得记住的一条模式,连续踩了两次"写计划时凭直觉估的数字,一到真实地理数据就露馅":① FIRMS 抬升量沿用既有 5 个静态源的默认 3000m(基于海平面假设),云南高原本身地形就超过 3km,火点标记直接被地形"吃掉";② (P2 卫星层先例,同类模式)"卫星过顶按 8000km 地平线粗筛"这类几何常数,凡是没有真实极端地理数据(高原、地平线边界)验证过的,都应该假设不准,计划阶段列的具体数字要在实现阶段用真实数据/边界条件复核,不能直接照抄。
2. **`pthread_cancel` 模板坑首次在本仓库暴露并根治**——AIS 控制线程原设计沿用了仓库里其它后台线程常见的"外部 `pthread_cancel` 强制终止"模式,但这次线程内部持有 WebSocket 连接需要有序关闭(析构/发送 close 帧),`pthread_cancel` 可能在任意 cancellation point 打断线程,导致 WS 连接对象在没有走完拆除逻辑的情况下被跳过——未定义行为窗口。根治手段是控制线程一开始就 `setCancelState(PTHREAD_CANCEL_DISABLE)`,配合析构函数兜底清理。**这是本仓库第一次系统性处理这个坑**,如果之后还有类似"线程内持有需要有序释放的外部资源(网络连接/文件句柄/锁)"的场景,应直接抄这次的模式,而不是继续用旧的 `pthread_cancel` 强杀套路。

**本次(Task 7)全量回归验证结果**:
1. 全量构建(`cmake --build build/verse_core --target install`)zero error,binary 与 HEAD `ec2880fb` 一致。
2. 5 个单测二进制(`osgVerse_Test_Feeds`/`osgVerse_Test_Ai_Chat`/`osgVerse_Test_TileOverlay`/`osgVerse_Test_Satellite`/`osgVerse_Test_Ais`,新增 `osgVerse_Test_Ais`)全部 `exit=0`,日志确认非空跑(`ALL AIS TESTS PASSED` 等)。
3. 全局默认视角 + 极点视角(`--goto 89 0 8000`)`classify_rb_swap.py` 均 `CLEAN`,人工目检截图确认:全局视角海洋自然蓝色、无橙色/暗瓦片;极点视角闭合星形扇面(85.05° 收敛几何的已知固有形态,非本次改动引入)、格陵兰冰盖正常白色、无 R/B 通道互换痕迹。
4. 无 key 降级路径:`EARTH_FIRES=1 EARTH_SHIPS=1` 且两个 key 都 unset,日志精确命中 `[Feed] fires no url & no fixture, fetch disabled`;AIS 侧代码确认(`ais_data.cpp` 构造函数)无 key 无 fixture 时静默置 `_status=kNoKey`、不起任何网络线程,无日志噪音属预期行为(非静默失败);exit=0,无崩溃。
5. 打包:`packaging/package_macos.sh` 成功产出 `dist/EarthExplorer.app`;`codesign -v` 通过;打包产物离屏冒烟测试 exit=0、截图正常(与开发版渲染一致)。

---
## 2026-07-05 会话(续7)— 卫星 TLE 缓存被打包脚本每次清空,已挪到 bundle 外

**用户的追问**:「续6」说明了缓存机制本身没问题(网络失败会退到本地缓存),但用户马上追问到了点子上——「我问你的就是,既然有缓存机制,我也拉取成功,我也看见了,为什么没有缓存」。这句话值得认真对待,不能用"续6"里已经答过的话敷衍过去,重新查了一遍。

**根因**:卫星层缓存目录当时是 `mainFolder + "/sat_cache"`,落在 `EarthExplorer.app/Contents/models/sat_cache`——**在 app bundle 内部**。而 `packaging/package_macos.sh` 第 9 行是 `rm -rf "$APP"`——**每次打包都把整个 `.app` 连根删掉重建**。用户当时真机成功拉取过一次(看到了数据),但这次会话后续为了修「续6」的问题又重新打包了两次,每次都把用户那次成功写下的缓存文件连同 bundle 一起删掉了——不是缓存机制坏了,是我自己后续的打包操作把它冲掉的。

**对照**:这个坑仓库里其实早就有人踩过并修好了——`readerwriter/UtilitiesEx.cpp` 的 `tileCacheDir()`(瓦片缓存)一开始就特意放在 bundle 外的 `$HOME/Library/Caches/EarthExplorer`,注释里就是为了不被重新打包冲掉。卫星层没有照抄这个先例,是我实现「续3」时的疏漏。

**修复**(commit `4baa2a3b`):新增 `satCacheDir()`,原样照搬 `tileCacheDir()` 的写法——`EARTH_SAT_CACHE` 环境变量可覆盖路径,设为 `0` 显式禁用,默认 `$HOME/Library/Caches/EarthExplorer/sat_cache`(bundle 外、打包重建不影响)。`SatelliteLayerImpl` 构造函数不再需要 `mainFolder` 参数。`fetchGroupTextNetwork()` 增加 `cacheEnabled` 判断,禁用时不再拼出 `/starlink.tle` 这种指向空字符串的假路径。清理了旧的 bundle 内残留(`build/sdk_core/models/sat_cache`、旧 `.app` 里的 `Contents/models/sat_cache`,均确认是 gitignored 构建产物,非用户数据)。

**验证**:离屏真实网络运行确认新路径 `/Users/USER/Library/Caches/EarthExplorer/sat_cache/` 下 `stations.tle`/`gps-ops.tle`/`beidou.tle` 及各自 `.ts` 时间戳文件都正确写入;`EARTH_SAT_CACHE=0` 离屏运行 exit=0 且确认缓存目录时间戳没被碰过(禁用时真的零磁盘 I/O,不是"禁用了但还是偷偷写了一下");重新打包后 `find .app -iname "*sat_cache*"` 确认 bundle 内**不再含任何缓存文件**,外部缓存目录不受影响独立留存;4 个单测 exit=0,全局回归 CLEAN。已重新打包,`/Users/USER/osgverse/dist/EarthExplorer.app`(2026-07-05 打包)含此次修复,**这次缓存不会再被后续打包清空**。

---
## ✅ 2026-07-05 会话(续6)— Starlink 被 CelesTrak 限流(403),补上失败可见提示

**背景**:「续5」加了 ISS/天宫高亮后,用户反馈"星链看不见了"。离屏复现 + 直接 curl 确认:CelesTrak 服务器对 `GROUP=starlink` 返回 403,响应正文明确说明"自上次成功下载(2026-07-05 14:30:06 UTC)以来数据还没更新,每 2 小时才更新一次"——这是 CelesTrak 自己的限流策略,不是代码 bug(同一时刻 `stations` 分组请求正常 200)。全盘搜索确认这台机器从未成功缓存过 starlink.tle,所以“上次成功下载”是共享出口 IP 的其它客户端触发的,不是本机记录。

**修复**(commit `f18d68ac`):`SatelliteLayer` 新增 `fetchErrorText(SatCategory)`,某类目开启且至少完整尝试过一次拉取但一颗都没拿到时返回提示文案,复用图层目录 UI 本来就每帧刷新的 `OverlayLayer.subtitle` 机制(不用新写 UI),`earth_main.cpp` 新增 `SatFetchStatusHandler` 逐帧同步。同时发现并修复一个"文案会说谎"的漏洞——原架构里任何类目只在首次开启时拉取一次,之后关关开开并不会真的重新连网络;现在 Starlink 专门加了"上次拉空、重新打开时真的再拉一次"的重试路径(`_starlinkRefetchRequested`),提示文案统一改成"本次未拉取到数据(可尝试关闭再打开重试)"这种对两边(有重试/没重试)都真实的措辞。`_preciseFetchDone`/`_starlinkFetchDone` 特意放在 `syncIfDirty()`(主线程消费快照的同一处)置位,不让后台线程直接写——避免重蹈「续4」那次 `lastUpdateRefTime` 跨线程时序竞态的覆辙。

**验证**:真实网络离屏运行确认 403 时 Starlink 开关下方文字变成"约 7000 颗,纯视觉壳层,不可点选 · 本次未拉取到数据(可尝试关闭再打开重试)";精选组正常拉取(214 颗)时不出现误报;4 个单测 exit=0;全局回归 CLEAN。**重试通路只做了 Starlink 一侧**(唯一实际观察到失败的类目),精选组三类目维持原有"只拉一次"语义不变——如果后续精选组也出现拉取失败且需要重试,需要照 Starlink 的模式补一遍,不要假设已经通用。

**追加(同一会话)**:用户追问"有缓存吗、没数据时能不能告诉我具体什么时候能重试"——磁盘缓存机制本来就对(`fetchGroupTextNetwork` 网络失败会先退到本地缓存,这次纯粹是 starlink 从未在本机成功缓存过,没有旧数据可退),但确认之前只显示自己写的通用提示、没利用 CelesTrak 403 正文里其实已经写明的"什么时候能重试"信息。commit `bf3fd558` 补上:把服务器原始文本(换行折成空格、合并多余空格)透传到 UI,现在实际显示"CelesTrak: GP data has not updated since your last successful download of GROUP=starlink at 2026-07-05 14:30:06 UTC. Data is updated once every 2 hours."——比自己编的话准确。已重新打包,`/Users/USER/osgverse/dist/EarthExplorer.app`(2026-07-05 23:32 打包)含此次改动。

---
## ✅ 2026-07-05 会话(续5)— 真机验收追问四件事,ISS/天宫加高亮,暗瓦片确认非卫星层问题

用户真机验收(续4 修复后)截图报了 4 个问题,逐一查实:

1. **南极附近暗色扇形瓦片**:关掉全部卫星图层、离屏复现用户同一视角,**同样的暗色图案依然存在**——这是纬度 >85° 极点几何收敛的固有特征(该纬度范围本来没有 Web 墨卡托瓦片数据,深色占位色填充三角扇),北极也有、只是海洋深蓝背景把它盖住了看不出来,南极因为是白色冰盖对比强烈才显眼。**确认与本次卫星层无关**,是仓库早就存在的已知限制,想解决需要单独立项(真实极地影像或更好看的占位处理),这次未动。
2. **「空间站」类目为何有 23 个物体**:直接查了真实 CelesTrak `GROUP=stations`,官方口径本来就不是"只含空间站"——混了对接飞船(Crew Dragon/Progress/Cygnus/Soyuz)、空间站舱段(POISK/NAUKA/WENTIAN/MENGTIAN/SZ-21)、火箭残骸(FREGAT DEB)、ISS 释放的学生立方星(HMU-SAT2 等 7 个,`98067Xx` 国际编号前缀就是 ISS 自己的)。不是代码分类错误。用户确认:23 个都保留,但 ISS(25544)/天宫(48274)改用更大+青白色的点单独高亮(commit `0f405cf2`),不再和其余 21 个同色小点混在一起认不出来。
3. **导航星座"有的能点有的不能"**:代码层面确认是拾取算法的固有性质——`pickAt()` 在 12px 容差内只选"屏幕距离最近的一个",~115 颗导航卫星密集时,被挡住的邻近点永远选不中直到精确点在它本体上。不是随机故障,是"容差内选最近"的设计取舍,这次未改(涉及全部图层的拾取行为,需要单独讨论)。
4. **Starlink 是真实位置还是纯视觉展示**:确认是真实位置——和其它卫星走同一条 TLE 拉取+SGP4 传播流水线,代码里没有任何伪造坐标的分支,只是渲染上刻意做成更小更暗、不可点选、无轨道线(减少 ~7000 点对精选组交互体验的干扰)。

**验证**:ISS/天宫高亮离屏截图确认(真实网络)大点在一堆小点里清晰可辨;4 个单测 exit=0;全局回归 CLEAN。已重新打包,`/Users/USER/osgverse/dist/EarthExplorer.app`(2026-07-05 22:51 打包)含此次高亮改动。

---
## ✅ 2026-07-05 会话(续4)— P2 卫星层真机验证发现并修复关键 bug:真实网络下开关点了但地球上什么都看不到

**背景**:「续3」交付时全部测试只跑过沙盒离屏 + fixture(本地文件,读取近乎零耗时)。用户真机测试第一次真正连上真实 CelesTrak 网络后报告:「所有卫星层点击,都没有任何变化,什么新内容都看不见」——UI 开关勾选状态正常变化,但地球画面没有任何新东西。

**根因(commit `61479fc5`)**:`FetchThread::run()` 里,首次抓取完成后**在同一轮循环、零延迟**紧接着就做了第一次"每 1s 重新传播"检查——真实网络抓取要花好几秒真实时间,`postPreciseSnapshot()` 把刚抓到的 214 颗卫星写进 `_pendingPrecise` 后,主线程根本还没来得及在下一次 `syncIfDirty()` 里把它消费进 `_allPrecise`,同一轮循环紧接着的重新传播步骤就已经调用 `currentPrecise()` 读到**仍是空的** `_allPrecise`,然后 `postPreciseSnapshot()` 把这个空快照**原地覆盖**回去——真实卫星数据在渲染层还没来得及看一眼就被自己冲掉了。Fixture 测试因为读本地文件几乎零耗时,主线程总能抢在这条竞态触发前消费掉真数据,所以之前所有测试都没暴露这条时序缝隙——这正是 HANDOFF 上一节"真机待验清单"第 1 条明确提醒过的风险类别(真实网络 vs fixture 的耗时差异),真被撞上了。

**修复**:①刚抓取完成那一刻把 `lastPreciseUpdate`/`lastStarlinkUpdate` 设为"此刻",让同一轮循环跳过重新传播检查,把消费真数据的时间窗口让给主线程;②双保险——重新传播读到的快照如果是空的,不再执行覆盖式的 `postPreciseSnapshot()`(无论空的原因是什么)。保留了一个 `EARTH_SAT_DEBUG=1` 诊断钩子(零影响于未设置时),同 `EARTH_PAGER_DEBUG` 先例。

**验证**:真实网络离屏截图(40000km 高空视角)确认金黄色空间站点位 + 青色 ISS/天宫轨道线正确显示在地球边缘(修复前同条件是光秃秃的地球,一个点都没有);fixture 路径回归无损;4 个单测二进制 exit=0;全局视角回归分类器 CLEAN。**已重新打包**,`/Users/USER/osgverse/dist/EarthExplorer.app`(2026-07-05 19:15 打包)是当前含此修复的最新可测试版本。

**教训**:这是 3 层审查(自查/任务审查/整体分支审查)全部没能抓到的第 3 类 bug——因为它只在"网络耗时 >> 主线程消费延迟"的真实条件下才触发,任何离屏 fixture 测试的耗时结构性地不可能撞见这条竞态窗口。HANDOFF 上一版已经诚实写明"从未接触真实网络"是未验证风险,这次验证到了、也修了——但也印证了：**subagent 密集审查 + 沙盒全绿,不能替代真实环境下至少跑一次**,尤其是任何涉及"背景线程+真实外部 I/O 耗时"的路径。

---
## ✅ 2026-07-05 会话(续3)— P2 卫星层(CelesTrak TLE + SGP4)实现完成,全量回归+打包通过,待真机验网

**状态**:7 个任务(设计规格→计划→vendor SGP4→纯数学层→数据/渲染/拾取/轨道线/足迹圆→UI 详情卡→图层注册/预设/全量回归打包)全部完成,全部本地 commit,**未 push、未打 tag**(按仓库既有约定,见下方 `release-requires-confirmation`,推送/打标时机由用户决定)。全程 `superpowers:subagent-driven-development`(实现子代理→规格审查子代理→质量审查子代理,独立派发独立复核)。

**关键 commit**(HEAD `e42cddd0`,由旧到新):

| Commit | 内容 |
|---|---|
| `900eda40` | P2 设计规格(CelesTrak TLE + SGP4) |
| `8db43a20` | P2 实现计划(7 任务拆分) |
| `7ea686dc` | vendor SGP4 轨道传播库(`dnwrnr/sgp4`,Apache-2.0,`3rdparty/sgp4/`) |
| `99c1dab5` | `sat_math.h/.cpp` 纯数学函数层(TLE 解析、SGP4 封装、轨道/足迹几何,`earthsat::` 命名空间) |
| `47ba6417` | `sat_data.h/.cpp` 卫星层骨架(TLE 抓取+缓存、分类开关、点云渲染) |
| `480f7488` | 卫星拾取、轨道线、足迹圆、ISS/天宫常显轨道线 |
| `10a2a8ba` | 修复:`lastUpdateRefTime` 只在拿到新快照时重置,不是每帧——否则逐帧平滑外推被自己代码抵消(质量审查独立抓到,写计划自查没发现) |
| `482b7d7b` | 卫星详情卡接入共享 CardStack |
| `e8206f2b` | 计划文档小修(`syncIfDirty` 代码片段与 Task4 修正后代码同步) |
| `e42cddd0` | 图层目录注册(精选组=空间站/导航/气象 3 开关 + Starlink 独立开关)、「全部」预设纳入精选组、`EARTH_SATS`/`EARTH_SATS_FILE`/`EARTH_STARLINK`/`EARTH_STARLINK_FILE` 测试钩子 |

**SGP4 数值正确性**:vendored 库用 Vallado「Revisiting Spacetrack Report #3」标准回归用例(卫星 #5,t=0/360 分钟)核对,参考向量用独立的官方 Python `sgp4` 包(Brandon Rhodes 维护,业界公认参考实现)于 2026-07-05 生成,与 vendored `libsgp4` 数值完全一致(位置误差 <1e-5 km,速度误差 <1e-5 km/s)。测试见 `tests/satellite_tests.cpp`。

**本次(Task 7)全量回归验证结果**:
1. 4 个单测二进制(`osgVerse_Test_Feeds`/`osgVerse_Test_Ai_Chat`/`osgVerse_Test_TileOverlay`/`osgVerse_Test_Satellite`)全部 `exit=0`,含新增 `osgVerse_Test_Satellite`(vendored SGP4 数值核验 + `sat_math` 封装层测试)。
2. Starlink 壳层 fixture 验证:`EARTH_STARLINK=1 EARTH_STARLINK_FILE=...sat_fixture_starlink.tle` 离屏运行,日志确认 `[Sat] Parsed 2 satellites (starlink, fixture)`,截图目检地球渲染正常(非黑屏/非纯色)。**两个 Starlink 点在该次截图里未能肉眼定位**——用官方 Python `sgp4` 包独立算出 fixture 里两颗卫星在渲染时刻的星下点约在 (10°N, 137-147°E)(太平洋菲律宾以东),而截图相机朝向是非洲/欧洲那一面(约 0-40°E),两点正好在地球背面被球体深度测试遮挡——这是正常的固定初始相机朝向+固定 fixture 轨道参数的巧合,不是渲染缺陷(点云走标准 GL_POINTS + depth test,背面遮挡是预期几何行为)。壳层机制本身(解析、开关、坐标传播、渲染管线不崩溃)已经过 Task 3/4/6 的其余单测/E2E 覆盖,本次截图只用于"跑起来不炸"的最终把关,符合计划书对此步骤的预期("数量太少不代表壳层机制有问题")。
3. 打包:`bash packaging/package_macos.sh` 成功产出 `dist/EarthExplorer.app`;`codesign -v` 通过(`SIG_OK`);额外做了打包产物的离屏冒烟测试(exit 0,截图正常)。
4. 顺带跑了 4 类历史回归里的 2 类(全局默认视角 + 极点视角)用 `classify_rb_swap.py`,结果均为 `CLEAN`,极点截图目检确认闭合星形无破洞、无橙色海洋、无暗瓦片。

**真机待验清单(留给用户,这整个卫星层功能从未接触过真实网络/真实 GPU 显示,只做过沙盒离屏 + fixture 测试)**:
1. 真实 CelesTrak 网络抓取(`https://celestrak.org/NORAD/elements/gp.php?GROUP=<g>&FORMAT=tle`)成功拿到数据,且 24 小时缓存 TTL 生效(短时间内重启 app 不应重新拉网络)。
2. Starlink 全量(~7000+ 颗)真实拉取后点云渲染性能可接受(v1 设计是一次性拉取全量渲染成点云,不做分块懒加载,需要真机确认帧率/内存观感)。
3. 精选组(空间站/导航/气象)+ Starlink 图层开关在真实 UI 里的交互观感;点选 ISS 或其它卫星后的轨道线、足迹圆、详情卡是否符合预期(位置、朝向、文字排版、颜色区分)。
4. 「全部」预设纳入精选组三个分类后,与既有图层(precip/hk3d/静态战略层等)同时开启时是否有新的性能或视觉冲突(本次只验证了功能不崩溃,未验证真机下的综合观感)。

**已知 v1 范围外限制**(计划书已明确,不要节外生枝):时间快进/历史回放、卫星过境预报、AI 工具接入(留给 P4)、Starlink 分块懒加载、非选中卫星的轨道线/足迹圆随时间持续刷新(ISS/天宫的常显轨道线会跟着每 1s 精选组重传播刷新,选中态的轨道线/足迹圆只在点选那一刻算一次)。

---
## ✅ 2026-07-05 会话(续2)— 「全部」预设暗瓦片根因找到并修复(不是 hk3d!最新,先读这个)

用户真机完成了关键对照测试:**同样操作(转镜头+降高度)只有「全部」触发暗瓦片,「实时」不触发**。据此继续排查,结果推翻了上一会话的 hk3d 假设:

- **真根因**(`readerwriter/TileCallback.cpp` createLayerImage):OVERLAY 层走 ImageRequestHandler 异步分支时,返回的 Texture2D **不带任何图像数据**就被 updateLayerData 绑到瓦片单元 3;GL 对 incomplete texture 采样返回不透明黑 `(0,0,0,1)`,`scattering_globe.frag` 按 overlay alpha 混合(precip 时 `Overlay2Opacity=0.85`)→ 整瓦片渲成黑色,直到 ImagePager 异步图像到达(取不到=永久黑)。
- **为什么只有「全部」**:precip(RainViewer)是唯一走这条 irh 路径的层(OVERLAY 重定向/DEFERRED 重试),且 `createCustomPath` 只对 **z≤10** 拉取——与"降到某个高度稳定触发"吻合。「实时」无 precip → 永不进此路径。**hk3d 降级为加重因子**(16 个 http 线程抢带宽,推高 overlay 拉取失败率),不是根因;上会话"排除 precip"只排除了 DatabasePager 队列机制,漏了纹理机制(教训:排除结论要写明排除的是哪个机制)。
- **修复**(`24e39c61`):irh 分支给占位纹理挂 1×1 全透明 RGBA 图像——纹理完整可采样、alpha=0 混合零影响,"加载中/失败"都表现为"暂无叠加层"。真实图像仍由 ImagePager `setImage` 正常替换。注释注明透明占位仅适用颜色叠加层(ELEVATION 若未来走 irh 需另行设计)。
- **验证**:沙盒离屏 E2E 修复前后对照(overlay 指向秒拒端口:修复前全屏黑地表→修复后正常);本地 HTTP 红瓦片服务确认正向替换路径;新增单测 `osgVerse_Test_TileOverlay`(RED→GREEN,规格审查还做了撤销修复→变红的反向验证);Feeds/AiChat 单测 0;global/pole 分类器 CLEAN+亲验截图正常。双审查 Ready to merge: Yes。
- **真机验收通过(2026-07-05)**:用户用重新打包的 `dist/EarthExplorer.app` 重复原复现操作,确认暗瓦片"确实消失了"。已打标 `v0.15.1` 并推送(4 个验收 bug 全部闭环:rpath / AI照片卡堆叠+静默失败 / 高原标记地形遮挡 / 「全部」暗瓦片)。
- **遗留/备忘**:
  1. `EARTH_PAGER_DEBUG` 诊断钩子(`ad60f77f`)保留未删——排查已结束可删,但若想继续看「全部」下瓦片加载偏慢(hk3d 抢带宽的残留影响)仍可用;上会话排队的"hk3d 优先级修复" chip(task_da0d50ec)已过时,不要再做。
  2. 审查发现两处**既有** Minor(非本次引入,不阻塞):异步 `requestImageFile` 未传 read options(绕过 getTileLoadingOptions 缓存);`TileCallback::_imageRequests` 按 URL 累积不清理(RainViewer 帧路径随时间变化,长跑会慢慢积累少量条目)。
  3. **环境坑(已修,防重踩)**:`build/sdk_core/bin/osgVerse_EarthExplorer` 曾签名失效(rpath 会话 install_name_tool 动过未重签),macOS 内核 exec 时**秒 SIGKILL 且零输出**;诊断法 `codesign -v <bin>`,修法 `codesign -s - -f <bin>`(本次已重签;后续重新构建会自动重签)。离屏截图验证前务必 `rm /tmp/earth_capture_0.png` 防拿陈旧文件得假结论(本次差点踩)。

---
## ✅ 2026-07-05 会话(续)— v0.15-vision 验收顺带发现的 4 个 bug 清理完成(3 修复+1 排查留证)

用户 v0.15-vision 真机验收时发现的 3 个新 bug + 早先排队的 rpath bug,本次逐一按 subagent-driven-development(实现→规格审查→质量审查,独立复核)处理完。**全部本地 commit,未 push**(下次 push 时机由用户决定)。

| Bug | 状态 | Commit | 说明 |
|---|---|---|---|
| 1. macOS rpath | ✅ 已修复 | `5f184103` | `CMakeLists.txt` 加 `IF(APPLE)` 分支,`$ORIGIN`→`@loader_path`;`build/sdk_core/bin/*` 现在不设 `DYLD_LIBRARY_PATH` 也能跑 |
| 2. AI 照片卡堆叠+静默失败 | ✅ 已修复 | `c3aeab89`+`a7e62c31` | 同类型(photo/video 靠 isVideo 区分)卡片只留最新一张;新增 `AIChatCore::addErrorNote()` 复用既有红字 `ChatEntry::ERR` 渲染,接进 `MediaManager` 全部 FAILED 分支;新增 `EARTH_AI_FAKE_IMG_FAIL` 失败注入测试钩子 |
| 3. 高原标记遮挡 | ✅ 已修复 | `963ab469`+`7f4f01ac` | 根因:`kFeedLiftMeters=3000` 是相对 WGS84 椭球面(≈海平面)非当地地形;改用 `EarthManipulator::terrainAltitudeAt()`(可见性从 protected 提到 public)在主线程查真实地形高度、`terrainAlt+3000` 重算 ecef,查询失败兜底原有行为;云南 Diqing(lat27.8,lon99.7)实测地形 5494m,验证修复前净空 -2494m(埋进地下)、修复后 +3000m |
| 4. 「全部」预设暗瓦片 | ⚠️ 排查未竟,留诊断工具 | `ad60f77f` | precip、5 个静态图层已排除(代码确认不碰分页机制);hk3d 是唯一剩余嫌疑(确认共享 DatabasePager 队列),但沙盒连不上其瓦片服务器测不了。留了 `EARTH_PAGER_DEBUG=1` 诊断钩子,已排队独立任务(chip `task_da0d50ec`)等真机确认后再修 |

**过程中一个值得记住的教训**:Bug 3 的质量审查独立发现"是否值得为复用引擎方法而放宽 `EarthManipulator::terrainAltitudeAt()` 的可见性(protected→public)"和"~18ms 一次性延迟是否可接受"这两个判断题,没有简单附和规格审查的结论,而是自己独立推理给出意见(认可两处都是合理判断)——这是"质量审查不能只是规格审查的回声"的一个好例子。Bug 3 完成后质量审查还指出 `syncIfDirty()` 因新逻辑膨胀到 111 行,建议抽取,已照做(`7f4f01ac`,纯重构无行为变化)。

**Bug 4 未竟的原因**:不是偷懒少做,是诚实止步于"证据不支持强行归因"——investigation 子代理已经很扎实地排除了两个候选(precip/静态层),对第三个候选(hk3d)因为沙盒网络策略连不上其真实瓦片服务器主机(直接 curl 能通,应用自己的 fetch 就是不完成),没法收集到"确认"所需的真实队列深度数据。这是环境限制,不是子代理偷懒——诊断工具已经就位,下一个能连上真实服务器的环境(用户真机)跑一次 `EARTH_PAGER_DEBUG=1` 对比「全部」vs「实时」就能确认或推翻。

---
## ✅ 2026-07-04/05 会话 — v0.15-vision 视觉美化全部完成,用户真机验收通过,已打标签推送

**用户真机验收结果(2026-07-05):** 真机验收清单全部通过("都验过了，可以接受")——UI 主题、卡片新排版、fly_to 150km 高度手感、日照常昼模式、事件流卡不可拖动的行为变化、昆明倾斜视角已知限制,均确认可接受。验收过程中额外发现 3 个既有问题(见下方待办 3/4/5),均确认与本次 Task 1-9 改动无关(未被本次任何 commit 触及的代码路径),已排队为独立后续任务,不阻塞本次发布。已执行 Task 9 Step 5:打标签 `v0.15-vision` 并推送 `origin master v0.15-vision`。

### 全部 9 个任务已完成(HEAD 见下,均已 commit,未 push;全部双审查 Ready to merge: Yes)

| Task | Commit | 内容 |
|---|---|---|
| 1 | `03cf3049` | fly_to 默认高度 50→150km + Go-To 面板同步 |
| 2 | `ff8e7f6f` | 日照「常昼 Always Day」默认开启,每帧太阳跟相机走,与实时天文/手动滑块互斥 |
| 3 | `791b189e` | MSAA 4x(仅窗口路径)+ 航班三角图标硬边→smoothstep 软化 |
| 4 | `a2b124ea` | UI 主题「任务控制台 Mission Control」(深空蓝黑 + `#3ba7ff` 青蓝强调色),`ui/ImGui.cpp` 默认启用(`style=20`) |
| 5 | `98ca2ea7` | `ui_card.h` 新增共享 Card/CardStyle/CardStack/computeCardLayout 组件(TDD,纯排布函数 5 组单测) |
| 6+7+8 | `d9cb06e7`(ai_cards/ai_ui)+ `64e72f09`(EarthControlUI/event_ticker) | 三处手搓卡片(AI 图表/照片/任务卡、事件流卡、航班/要素详情卡)全部迁移到共享 CardStack;三任务因中间态无法独立编译按计划合并实现,双审查按 Task6/7/8 全部要求逐条核对 |
| 9 | 本节 + `a027b63a`(计划文档小修) | 全量回归矩阵 A-F 自动化部分 + 重新打包;Step 2(本节)+ Step 4(真机验收)+ Step 5(打标签推送)见下 |

**Task 4-9 是本次(续2)会话新做的**,用 `superpowers:subagent-driven-development` 流程逐任务执行(实现子代理→规格审查子代理→质量审查子代理,全部独立派发、独立复核,不采信报告)。

### 本次审查抓到的三件事(均已处理,记录供参考)

1. **一次被驳回的误判**:Task 4 质量审查曾判定"MissionControl 主题未设置 `ImGuiCol_InputTextCursor`,导致 AI 对话框/图层搜索框光标不可见"为 Important 级问题。Controller 独立核实 ImGui 源码(`imgui.cpp:1482` `ImGuiStyle` 构造函数内无条件调用 `StyleColorsDark(this)`;`imgui_draw.cpp:222` 该函数内 `InputTextCursor = Text` 纯白;`imgui_internal.h:2190` `ImGuiContext::Style` 是值成员非指针,C++ 保证构造函数必然执行)后,证实未设置的槽位并非全透明,而是保留 context 构造期已写入的合理默认值(纯白,不透明)——**该问题不成立,已驳回,未改代码**。教训:审查意见也要验证,不能因为"审查子代理很严谨"就直接采信。
2. **一次被采纳的发现,记录待用户确认**:Task 6+7+8 质量审查发现事件流卡(`event_ticker.h`)迁移到 CardStack 后**不再可拖动定位**(原来用 `ImGuiCond_FirstUseEver`,注释明确写着"因为其它卡固定不可拖、故意让事件流卡可拖动避让";CardStack 统一用 `ImGuiCond_Always`)。核实为真实行为变化,但属于 Task 5(已批准)"统一定位系统"设计的必然结果——新方案靠自动换列消除重叠问题本身,不再需要人工拖动来避让。**未改代码**(改了等于重新引入"各卡各行为"的旧问题),已列入下方真机验收清单,请用户确认是否可接受。
3. **一处计划文档小 bug**:Task 8 Step 7 的"零影响断言"命令原文漏写了 `(static)` 过滤器(P1 会话就已确立的约定:5 个静态战略数据源启动即无条件打印,应排除),导致断言从"预期 0"变成实际"5"。核实确系计划文本过时(本计划 Task 9 Step 1-D 自己的措辞其实是对的),非实现缺陷,已直接修正提交(`a027b63a`,纯文档)。

### Task 9 自动化部分(A-F)全部完成,细节见下

- **A** 两测试目标 exit 0 ✓(`osgVerse_Test_Feeds`/`osgVerse_Test_Ai_Chat`,含 Task5 新增 `earthui::computeCardLayout` 单测)
- **B** AI 7 组 E2E 全绿(flyto/quakes-summary/chart/chart-multi/flights-summary/photo/video,含 `EARTH_AI_VIDEO_AUTOTEST=1` 全流程 A→B→confirm→done)
- **C** 七源 fixture 全载(quakes3/gdacs3/eonet5/gdelt7/gpsjam7/unhcr10/nhc3,计数精确匹配)
- **D** 零影响断言排除 `(static)` 后 = 0 ✓
- **E** 4 类历史回归:global/pole 分类器 CLEAN(**controller 亲验截图**,地球渲染干净、极点仍是闭合星形无破洞);kunming/hk 分类器 UNKNOWN 但目检确认是已知限制非新回归(kunming 离屏时序冲突拍不到地形只有渐变色块;hk 是真实港湾细节渲染,分类器不认近景陆地色系)
- **F** 航班图标截图 CLEAN(**controller 亲验**,可见法国上空两个小三角图标,MSAA+抗锯齿边缘观感正常);CardStack 激活截图 exit 0 无崩溃(此截图**看不到卡片本身**,ImGui 不入离屏 FBO,这是结构性限制,卡片视觉效果只能走下面真机验收)
- **打包**:`bash packaging/package_macos.sh` 成功,输出 `dist/EarthExplorer.app`(518MB)。**意外收获**:独立用 `otool -l` 核实打包后的 `.app` 二进制 rpath 已被打包脚本的 `install_name_tool` 步骤正确改写为 `@executable_path/../lib`,**不设 `DYLD_LIBRARY_PATH` 直接跑 `dist/EarthExplorer.app` 也能正常启动**——下方 rpath bug 待办的影响范围因此缩小为"仅影响 `build/sdk_core/bin/` 原始构建产物,不影响最终分发的 `.app`"。

### ⚠️ 待办 1:rpath bug(不阻塞 v0.15,范围已缩小,建议打标推送后单独修)

`build/sdk_core/bin/` 下的原始二进制用了 `$ORIGIN` 风格 rpath(macOS dyld 不认,只认 `@loader_path`/`@executable_path`),开发迭代时每个 E2E 命令都要手动加 `DYLD_LIBRARY_PATH=build/sdk_core/lib`。**本次核实:打包产物 `dist/*.app` 不受影响**(`packaging/package_macos.sh` 的 `install_name_tool` 步骤已经把 `.app` 内二进制的 rpath 正确改写成 `@executable_path/../lib`,双击可直接启动)。修复范围缩小为纯开发体验问题:去 CMake 配置(根 `CMakeLists.txt` 或 `NEW_EXECUTABLE`/`NEW_TEST` 宏)里把 `CMAKE_INSTALL_RPATH`/`INSTALL_RPATH` 在 macOS 下也设成 `@loader_path/../lib`,省得每次手动加环境变量。

### ⚠️ 待办 2:Kunming 离屏截图已知限制(勿重踩)

`EARTH_TILT` + `--goto` 低空斜视角离屏模式下时序冲突拍不到真实地形(本次 Task 9 又确认一次,截图是近乎无特征的渐变色块,非新回归)。**真机验收清单已包含此项**,不要再花时间调参数试图离屏复现。

### ⚠️ 待办 3(本次真机验收新发现):「全部」预设下偶发暗瓦片(不阻塞 v0.15,已排队独立任务)

用户真机验收时报告:选中图层预设「全部」(`earth_main.cpp` Preset 列表,`enabledIds` 含 precip/flights/hk3d/gdacs/quakes/eonet/gdelt/gpsjam/unhcr/nhc/bases/ports/nuclear/spaceports/datacenters)后,某区域会出现明显暗色瓦片色块,固定贴地(世界坐标,非屏幕坐标),等待数十秒或旋转镜头离开再回来会自行消失;切换到「实时」预设(不含 precip/hk3d/静态战略层)同一位置/高度不会出现。**排查结论(未改代码,只诊断到假设阶段)**:大概率不是 v0.15 回归——占位色机制是 `render_effects.cpp:93` 的既有深海蓝 `RGB(0.03,0.06,0.10)`(P0 阶段就有,配合"启动大面积橙色"系列历史记录一起看),怀疑「全部」同时打开 precip(复用瓦片 OVERLAY 槽,和底图影像抢同一套加载资源)+ hk3d(3D Tiles 建筑网格流式加载)+ 1090+ 静态战略点,叠加导致底图瓦片加载队列变慢、占位色显示时间变长到肉眼可见,不是永久性渲染错误。GPSJam 已排除(只画彩色点,从不渲染暗色)。已作为独立任务排队(chip `task_faaf4fee`),需按 systematic-debugging 流程实测确认根因(检查 TileManager/DatabasePager 资源竞争)后再修,不要凭猜测改。

### ⚠️ 待办 4(本次真机验收新发现):高原地区标记贴近地表被地形遮挡(不阻塞 v0.15,已排队独立任务)

用户真机验收时顺带发现:云南(云贵高原,实际地表海拔约 1900-2800+ 米)一次地震标记远处可见,放大镜头靠近后消失;其它地势较低地区的地震标记放大后正常,不会消失。**已定位可能根因(未改代码)**:`feed_layer.cpp:32` 的 `kFeedLiftMeters = 3000.0` 把全部 Feed 图层标记(地震/GDACS/EONET/GPSJam 等共用同一模板)统一抬高到"WGS84 椭球面(约等于海平面)+3000 米",不是"当地实际地形高度+3000 米"。云贵高原地表本身就有 1900-2800+ 米,局部山区更高,标记的椭球面+3000米绝对高度可能低于或贴近真实地形,远看深度精度不够看不出来,凑近后被地形前遮挡(其实是陷入地下)。此模板影响面较大(全部 Feed 图层共用),已排队独立任务(chip `task_84c98d2b`),需先用实际经纬度+地形高程数值核实假设,再决定修复方向(地形高程感知 lift,或简单增大常量到覆盖全球最高地形之上,两者观感/工作量权衡不同),不要跳过验证直接改常量。

### ⚠️ 待办 5(本次真机验收新发现):AI 照片卡无限堆叠 + 异步任务失败完全静默(不阻塞 v0.15,已排队独立任务)

用户真机验收 AI 生图功能时报告:连续生成两张照片(飞到不同地点各生成一张),旧照片面板一直在,分不清新的在哪——查证两张照片文件都真实生成成功(`~/Pictures/EarthExplorer/gen_*.png` 都在,大小正常),问题纯粹是 `ai_cards.cpp:pushPhoto()` 每次都 `push_back` 追加新卡、旧卡不自动关闭,且所有照片卡标题写死同一句"实景照片"(`ai_media.cpp:858` 等处调用),两张卡挤在一起标题相同容易被当成一张。**用户已确认修复方向**:同类型(PHOTO)卡片任意时刻只保留最新一张,新卡 push 前把同类型旧卡设 `open=false`(仿照现有"进度卡→结果卡"replaceJob 模式扩展)。

顺带验收时还真实复现了一次更严重的问题:生成旧金山照片后进度卡消失、无结果卡、无错误提示,`~/Pictures/EarthExplorer/` 下只有对应的 `snap_*.png`(快照/输入),没有 `gen_*.png`(生成结果)——查证 `ai_media.cpp` 全部 `AIJob::FAILED` 分支(十几处)后确认:失败时只写 `OSG_WARN` 控制台日志,不会呈现到聊天记录或卡片上,而双击运行的 `.app` 没有终端附着,用户完全看不到任何失败信息,体验上就是"什么都没发生"。这次具体失败原因未查(未能保留现场复现),但"异步任务失败无可见提示"这个更普遍的设计缺口已确认坐实。

两个问题都是 7月2日 AI Chat 原始实现就有的既有行为,非 Task 1-9 引入,已排队独立任务(chip `task_58e687fa`),修复时需按 systematic-debugging 流程先理清 AIChatCore 现有的同步工具错误 vs 异步 job 轮询失败两条呈现路径的差异,再决定怎么把异步失败也接进用户可见的地方。

### 真机验收清单(Task 9 Step 1-G + Step 4,呈递用户,通过后才能打标签推送)

1. UI 主题整体观感(Mission Control:深空蓝黑背景 + 青蓝 `#3ba7ff` 强调色,左上操作面板/底部 AI 对话条/顶部状态带/全部信息卡片统一风格)
2. 卡片新排版(表头胶囊标签、右上角统一堆叠、溢出自动换列)
3. fly_to 150km 高度手感(AI"飞到 X"或 Go-To 面板)
4. 日照常昼模式(飞到任意位置 + 手动拖转到地球背面,画面应始终保持白天,不再有暗背光面)
5. 事件流卡/详情卡(航班/要素)/AI 卡(图表/照片/任务)外观一致性
6. **【本次新变化,需明确确认】事件流卡不再可拖动定位**(此前可拖到任意位置避让其它固定卡片,现在和其它卡片一样固定在右上角堆叠序列中,靠自动换列避免遮挡)——如果依赖旧的可拖动习惯,请告知,可以考虑加一个"仅事件流卡可拖动"的例外
7. **【已知限制,非本次引入】昆明倾斜视角**(EARTH_TILT 低空斜视角看滇池岸/城区)——只需确认真机交互下地形正常显示,无需关心离屏截图的问题
8. （可选,不影响功能)AI 对话框/图层搜索框光标颜色:审查中一度怀疑不可见,已用源码核实实际是不透明白色,真机顺带留意一下和新主题协调度即可,不是必须验证项

### 用户验收通过后,执行(Task 9 Step 5,当前 HEAD 打标签):

```bash
cd /Users/USER/osgverse
git tag -a v0.15-vision -m "v0.15-vision: visual polish — Mission Control theme, unified Card component, camera/sun fixes, anti-aliasing"
git push origin master v0.15-vision
```

（打标签前记得把本节标题的 🔄 改成 ✅ 并补一句验收结果;若用户提出修改意见,先改代码走双审查,再重新打包,再重新走验收,不要跳步。）

---
## ✅ 2026-07-04 会话(续)— 输入体验修复:热键穿透根治 + 中文 IME 直打

用户真机反馈"打字仍触发热键"+要求中文直打支持,已解决并**用户真机验收通过**,打标 v0.14。

- **根因**(读 OSG 3.6.5 源码证实):`Viewer::eventTraversal` 无条件广播事件给全部 handler,`GUIEventHandler::handle` 返回 true 只置 `handled`、不中断广播;而处理键盘的 `ImGuiHandler` 装在链尾,它置的 handled 对前面的处理器毫无作用。真正漏点:数字键 `1`/`2` 触发 CreateCityHandler 误载北京城市数据;**Esc 会直接退出整个 app**(viewer `_keyEventSetsDone`,判定先于所有 handler,无法被闸拦)。
- **修复**(`641eb77d`):`GlobalKeyboardGate` 装在**第一个** `addEventHandler`,`WantTextInput||WantCaptureKeyboard` 时吞 KEYDOWN/KEYUP(带 DOWN/UP 配对,防松手时已失焦漏放);纯判定逻辑抽为 `input_gate.h`(可单测);`setKeyEventSetsDone(0)` 关掉 Esc 退出。
- **中文 IME 直打**(`fcb96ede`,app 侧方案,零 OSG 改动):`ime_bridge.{h,mm}` 给 Cocoa 窗口挂隐藏 1×1 `NSTextInputClient` overlay,`WantTextInput` 时切 firstResponder 过去,组字/上屏走 ImGui 注入;offscreen 自动禁用;`EARTH_IME=0` 逃生门。v1 限制:预编辑串不在输入框内联显示(ImGui 无 preedit),只在系统候选窗可见。
- **复审修复**(`bd52d072`):Retina 候选窗坐标错误(除了不该除的 backingScaleFactor)、**关窗按钮不真正退出进程**(CLOSE_WINDOW 未调 setDone,Esc 退出没了之后这是唯一退出路径却是坏的——新增 `CloseWindowQuitHandler` 补上)、按住太阳角度键切进输入框松手导致太阳永转(`_pressingKey` 清零时机错误)。
- **⚠️ 行为变化**:Esc 不再退出 app(只失焦),退出改走关窗按钮(已修复)或 Cmd+Q。
- 已打标 **v0.14** 并推送。

---
## ✅ 2026-07-04 会话 — P1 零门槛铺量全部完成(T1-T9,21 commit)

**图层数×3 达成**。计划+执行记录:`docs/superpowers/plans/2026-07-03-world-hub-p1-rollout.md`(含逐任务 commit 表、教训、遗留);roadmap P1 已打勾。全程 subagent-driven 双审查,回归矩阵 A-J 十组全绿,4 类历史回归截图亲验。

### 新能力速查
- **6 个新数据源**(全走 FeedLayer,每源一文件 feeds/*.cpp):EONET 事件点(13 类色)/GDELT 新闻热点(热度渐变;⚠️端点暂 404,fixture 未经真网核实)/GPSJam GPS 干扰(gzip CSV+H3 解码,新增 feeds/h3_lite.h 移植)/UNHCR 流离失所 O-D 弧(弧原语首用,251 国质心表)/NHC 飓风路径+锥外环(折线首用,多层 parse 内补拉)/静态战略×5 层(bases/ports/nuclear/spaceports/datacenters,原始公开源 vendored,**bases=CC BY-NC 非商用**)。
- **框架扩展**:FeedSpec.parseGeometry(线/弧)、FeedPoint.unixTime、staticFile 静态源、实例内 StateSet 复用(刷新不再重编译着色器)、collectRecentEvents/feedHealth(draw 线程安全,锁+快照)。
- **UI**:右上事件流卡(unixTime 降序 TOP20,点击 fly_to;EARTH_TICKER=1)+ 顶部状态带(UTC/源健康 n/m/预设名;EARTH_STATUSBAR=1),默认均关,开关在图层节预设行旁。
- **预设**:干净|灾害{gdacs,quakes,eonet,nhc}|军事{bases,ports,nuclear,spaceports}|实时{quakes,gdacs,flights,eonet,gdelt,gpsjam,nhc}|全部。
- **测试**:新目标 osgVerse_Test_Feeds(feed/geo 全部单测迁入);七源 fixtures+静态五层+AI 7 组+验收 demo(test/ai_fake_disaster_brief.json);零影响断言标准=排除 " (static)" 行后六前缀日志 0 条。

### 🔥 重大排查:瓦片非确定性 R/B 染色(棕海洋/泛白撒哈拉)
真凶=**jpg/png 解码插件注册竞态**(osgdb_verse_image=RGB vs macOS osgdb_imageio=BGRA,谁先注册谁接管,同二进制两次运行可不同)。修复 `56b4014e`:启动预载 verse_image 确定化 + EARTH_IMG_DEBUG=1 诊断钩子 + test/classify_rb_swap.py 自动分类器(**低空陆地视图会误报,目检为准**)。GL 最后一环未闭环(修复后 0/45 无法复现)——**若复发:先 EARTH_IMG_DEBUG=1,出现 "Mac OS X ImageIO" 即确定化失效,否则查纹理对象/GL 状态方向**。注意:曾误判"全局 Program 缓存"为凶手(4ee4c74d 回退,保留不恢复),真相是当时并发构建导致二进制混装伪造了实验结果。

### ⚠️ 本次教训(勿重蹈)
1. **绝不在子代理构建期间并行跑实验/构建**——二进制混装伪造实验结论,耗数小时;
2. **commit 前 build 输出 grep "error:" 必须=0**——曾拿旧二进制假绿提交(已 amend);
3. **代理自称"视觉验证通过"不可信**,controller 必须亲验截图(本轮两次拦截靠此)。

### 遗留(P2 前顺带)
GDELT 服务恢复后真网核对字段;NHC 首个真实风暴复验;GDACS/NHC 外其余源 unixTime 视需要补(GDELT/静态层无时间语义);线宽 1px 扩线(观感不行再立项);真机交互验收(事件流卡/状态带/新图层点击详情)待用户。

---
## ✅ 2026-07-03 会话(续)— P0 平台化全部完成(T1-T5)+ 离屏测试基建重做(最新,先读这个)

**P0 五任务全闭环**(计划+执行记录:`docs/superpowers/plans/2026-07-03-world-hub-p0-platform.md`;roadmap P0 已打勾),全程 subagent-driven 双审查。回归矩阵 19/19 全绿,4 类历史回归截图人工验收过,dist 已重打包。**用户真机验收通过,已打标 v0.13 并推送**(含 Task#16/#17 关闭);下一阶段 P1 铺量进行中。

### 本次新增能力速查

- **T3 geo_primitives**(`applications/earth_explorer/geo_primitives.{h,cpp}`,namespace `earthgeo`):`buildArcVertices`(大圆 slerp+sin(πt) 弧高,对跖安全)/`buildPolylineVertices`(逐细分点贴局部 WGS84 椭球面+可选 lift)纯函数带单测;`buildArcGeometry`(每弧独立 LINE_STRIP、顶点色 O→D、FlowPhase 流动 uniform 桩)/`buildPolylineGeometry`;着色器自成一体不碰 globe。**已知限制(头文件有注释)**:macOS core profile 线宽钳 1px(真 2px 需 P1 屏幕空间扩线)、alpha 未开混合被忽略、±85.05° 外路标点被既有 Web-Mercator 极点钳制。`EARTH_ARC_DEMO=1` 演示层(3 弧+1 折线,日志 `[GeoPrim] arc demo enabled`)。
- **T4 图层目录**:LayerManager 增 `Preset/applyPreset(name)->bool`(先全关再开;豁免=底图/标注组+needsKey+无 apply);EarthControlUI 图层节=预设按钮行(干净|灾害|实时|全部)+搜索框(displayName/group 过滤+匹配组自动展开)+**分组按 group 归并渲染**(修了"实时数据/Live"重复标题);`FeedSelection.sourceId`:关 feed 层只清本源选中,[x] 仍全清。`EARTH_PRESET=<名>` 启动即应用+打印组合(覆盖其它 EARTH_* 逐层钩子,未知名仅 WARN)。"全部"不含 clouds(与 precip 互斥 OVERLAY 槽,注释在 earth_main)。
- **T5 离屏测试基建重做(重要,所有人都会用到)**:`EARTH_OFFSCREEN=1` 从"GL2.1 pbuffer 白帧假绿"重做为 **HeadlessCGLContext**(earth_main.cpp:纯 CGL GL4.1 Core,无 AppKit/NSApplication,构造性保证不弹窗不抢焦;自建 FBO 当默认帧缓冲)。EARTH_AUTOCAP 在 offscreen 下从 finalCamera FBO attach 读回(**截图不含 ImGui 面板**,UI 逻辑用日志断言如 EARTH_PRESET);写 /tmp 需非沙箱 shell。顺带修了 `osgdb_verse_image` 写 PNG/JPG 漏翻转(与 osgdb_imageio 注册竞态→历史截图 ~1/4 概率随机倒置的真根因)。**现在离屏截图是真渲染,可以直接做视觉回归。**

### ⚠️ 本次踩坑(勿重蹈)

- **OSG_WARN/OSG_NOTICE 是带 if 的宏**:if/else 链里分支不加大括号,else 会被宏内层 if 吞掉、代码静默不可达——同 session 踩了两次(AUTOCAP 落盘链、EARTH_PRESET applied 日志),回归矩阵才抓到第二处。**所有 OSG 日志宏出现在 if/else 里必须加大括号。**
- 验证修复时要覆盖**所有**分支:第二处 dangling-else 就是只复验了 unknown-name 路径、漏了 applied 路径漏网的。
- zsh 里 `echo ===` 会炸(=扩展);grep `" error"` 会误匹配 gl3.h 警告文本里的 "errors"。

### 提交清单(本段,未 push 前)

`4b024e74`/`633875ea`(T3)、`ff5d4e7d`/`caa6aa56`/`9fd11858`(T4+润色+日志修复)、`30584b3f`/`ec1caaba`(离屏基建)、文档+打包提交。

### Task#17 持久磁盘瓦片缓存 ✓ 完成(`3d3dbc3b`+`5af9807e`,审查过)——并翻案了下方 🔥 节结论

**HANDOFF 原🔥节"app 历来无持久瓦片缓存"不成立**(git 历史证实):`loadFileData`(readerwriter/UtilitiesEx.cpp)自 06-18 `38a4c93d` 就有持久磁盘缓存(原始 HTTP 字节,旧默认 `~/.osgverse_earth_cache`,本机在盘 81,590 块);simdb FileCache 只是其上的**解码结果**内存缓存,确实不落盘但不是重启重拉网的原因。**07-03 用户真机"启动大面积橙色"的更可能真因:用户双击的 .app 打包早于 06-18(不含该缓存)**,或旧 EARTH_TILE_CACHE 语义 bug(旧代码把 `0` 当目录名)。本次改动(仅 UtilitiesEx.cpp):缓存根迁至 `~/Library/Caches/EarthExplorer`(HOME 运行时展开,cwd 无关;旧目录首启原子 rename 迁移,81,590 块不作废,失败留 WARN);`EARTH_TILE_CACHE` 语义修正:未设=默认开 / `0`=彻底关(旧 bug 会建 `./0`)/ `<path>`=自定义;哈希前 2 字符扇出子目录(旧扁平文件兜底可读);diskHits/netFetches 计数(每 200 次 NOTICE + atexit fprintf 汇总——**析构期 osg::notify 互斥锁已销毁会拉崩,退出日志只能 fprintf**);关闭态零输出零目录。验证:热启动 netFetches=0;=0 与现状一致;单测/quakes fixture/离屏截图全绿。边界:v1 无淘汰上限;404 不缓存;若将来接 Google Photorealistic 3D Tiles 需在 loadFileData 加排除(ToS 禁缓存)。

### Task#16 占位色橙→深海蓝 ✓ 关闭(无需改动,实证充分)

按铁律先定位后动手,结论:**目标修复早已存在**——`render_effects.cpp:93` 的占位纹理 06-19(`7a6d1635`)就从白色改成了深海蓝 RGB(0.03,0.06,0.10),注释原文即"白色经大气散射在向阳侧会被染成刺眼橙色"。当前 master 上冷缓存早帧、1.5-2s/瓦片限流代理模拟、洋红诊断色(跑遍场景从未入画)均**复现不出橙色**;整球纯色瓦片实验证实现值过散射管线渲染为干净深海蓝、旧白色则过曝刺眼(机理闭环)。**07-03 用户所见橙色 = 旧 .app(打包早于 06-18/19,双缺磁盘缓存与深蓝占位)× Google 软限流**;今日重打包的 dist 已含全部修复。若用户用新 .app 冷启动仍见橙:按 earth-debug-verify-at-exact-condition 同条件取证再查(那是新线索,不是这个常量)。诊断截图在 session scratchpad t16/。未提交任何改动,零 shader 触碰。

---
## ✅ 2026-07-03 会话(P0 进行中)— 世界信息枢纽平台化 + 关键修复

**背景**:v0.12 已打标(AI Chat 全量里程碑)。路线图 `docs/superpowers/specs/2026-07-03-world-info-hub-roadmap.md`(P0-P5 六阶段,源自 worldmonitor 86 源盘点 `docs/superpowers/research/`)。当前执行 P0 计划 `docs/superpowers/plans/2026-07-03-world-hub-p0-platform.md`(5 任务)。

### P0 进度
- **T1 ✓ 完结**(`ccf95fbc`,双审查过):FeedLayer 配置驱动框架(feed_layer.h/.cpp:FeedSpec/FeedPoint/registerFeedLayer=轮询线程+GL_POINTS+拾取+详情+LayerManager+env 钩子+AI 工具自动注册)+ GDACS 灾害源(feeds/gdacs_feed.cpp,108 行自证)。审查要点:GDACS 的 129 feature=21 事件,只取 Class=="Point_Centroid"(T2 已修去重,N=3)。
- **T2 ✓ 实现完成+规格审查已过**(`8a2d9fb4`;审查硬核对:depthColor/magSizePx/summaryJson 与旧版逐字等价、6 组 E2E+真网 N=38+截图复现全过;PICKDBG 下线裁定可接受;质量审查也已过并修复其 fix-first 项:feed URL 剥引号+限 http(s) 才 system(),00ed3f32——**T2 完全闭环**,dist 已重打包含 T2):USGS 地震迁移 FeedLayer,quake_data.{h,cpp} 已删(净-258行),6 个审查遗留全清(atomic bool/死存储/质心去重/is<object>守卫/拾取不穿透 ImGui/代理项排除);FeedSelection.url + 通用详情卡「打开」按钮;get_quakes_summary 改由框架自动注册(名称/内容不变)。新 session:若质量审查结论缺失则重派(范围 0a24e54c..8a2d9fb4),approve 后重打包 dist 并进 T3。
- **T3-T5 待做**:弧线/折线原语 → 图层目录(搜索+预设;顺带修重复"实时数据/Live"分组标题+关层清选中)→ 收尾。
- **紧急修复(已 push)**:`0a24e54c` 聊天按键穿透调试快捷键(f=全屏/w=线框,ImGuiAwareKeyFilter 包装 4 个处理器)+ thought_signature 原样回传(gemini-3.5-flash 必需,rawPartJson)+ 中文 Cmd+V 粘贴(ui/ImGui.cpp:修饰键 AddKeyEvent+pbcopy/pbpaste 剪贴板+CJK AddInputCharacter;直打中文需 OSG Cocoa NSTextInputClient,未做)。`cbf6f30c` EARTH_OFFSCREEN=1 离屏 pbuffer 测试模式(**所有自动化测试必须带此钩子,勿弹窗打扰用户**)。

### 🔥 "启动大面积橙色"根因(2026-07-03 用户真机)——⚠️本节结论已被 Task#17 部分翻案,以上方 Task#17 节为准
FileCache 用 simdb=共享内存 DB **从不落盘**——app 历来无持久瓦片缓存,每次启动全量重拉;平时网络快(0.1-0.3s/瓦片)橙色一闪即过,当 Google 软限流(实测 1-1.5s/瓦片,因当日海量自动化测试触发)时,全球低层级 ~1000 张瓦片要数分钟,橙色赖着不走。**非渲染回归**(v0.12 以来渲染管线零改动,已 diff 证明)。已排队 Task#17 持久磁盘缓存(绝对路径 ~/Library/Caches/...,双击启动 cwd=/)治根;限流会随测试停止自行消退;临时缓解 EARTH_BASEMAP=esri 换源。

### ⚠️ 已排队高危任务:瓦片加载态默认色 橙→深海蓝
用户批准;**橙色海洋问题曾引发整 session 大回滚**(memory: earth-2026-06-session-reverted)。铁律:只改默认/占位颜色一处,严禁碰 scattering_globe/sunL/晨昏线;改前定位颜色确切来源,改后 4 类历史回归+加载态对比截图,独立提交不混改。

### 测试口令速查(新约定)
- 一切 headless 测试加 `EARTH_OFFSCREEN=1`(不弹窗)。
- 回归六件套见 P0 计划 Task 2 验证节;fixture 一律绝对路径(app cwd 会切走)。
- 真机 key 在 ~/.zshrc(EARTH_AI_KEY)与打包注入,scratchpad ai.env 每 session 需重建。

## ✅ 2026-07-02 会话(续)— AI Chat 已全量落地(最新,先读这个)

自然语言驱动 EarthExplorer 的 AI 对话功能(Gemini)已完整实现、10 个任务全部完成,详见 `docs/superpowers/plans/2026-07-02-earth-ai-chat.md`(全部勾选 + 末尾"执行记录"章节)与 `docs/superpowers/specs/2026-07-02-earth-ai-chat-design.md`。**代码已 commit(本地,未 push)**,commit 范围 `6b512db4..7f5286ea`(20 个 commit)。

### 功能清单

- **自然语言飞行**:"飞到纽约"→ `fly_to` 工具解析经纬度/高度,相机平滑飞过去。
- **图层控制**:自然语言开关地震/航班/降水/云图等既有图层(`set_layer` 工具)。
- **数据统计 + 图表**:`get_quakes_summary`/`get_flights_summary`(总数/分桶/最新)驱动对话回答,`show_chart` 工具在右上角手绘卡片(bar/donut/stat/line 四种,ImDrawList 直绘,不额外引入绘图库)。
- **📷 实景照片**:截取当前三维地球视角 → Gemini 图像模型生成同地点同视角写实照片,异步 Job,完成后右上角照片卡展示,点开大图。
- **🎬 首尾帧巡航视频(费用确认门)**:记录当前相机为起点 A → 移动相机记录终点 B → 自动算大圆方位角/距离/升降 → 生成英文运动提示词 → 弹确认框(**约 $2-6,必须用户点确认才会真正调用 Veo**,自然语言路径与 UI 按钮共用同一状态机,工具本身永不跳过确认框)→ 提交 `predictLongRunning` → 轮询 → 完成后视频卡片。

### 架构

- **ToolRegistry(一切能力皆工具)**:`ai_tools.h`,`Tool{name, description, parametersJson, execute}`,`AIChatCore` 是通用 Gemini function-calling 代理循环,**不认识任何具体工具**——加新能力只需注册一个 Tool,不改核心循环。
- **AIChatCore 通用代理循环**:`ai_chat.h/.cpp`,网络请求(libhv 同步 API)放后台线程,工具执行经队列回主线程(`AIFrameHandler` 的 FRAME handler 里 `drainMainThread()`),避免长网络调用卡住渲染帧。`LLMProvider` 接口下有 `GeminiProvider`(真实 REST v1beta)与 `FakeProvider`(`EARTH_AI_FAKE=<script.json>`,脚本化多轮 function-call,离线确定性 E2E 用)。
- **MediaManager Job 状态机**:`ai_media.h/.cpp`,照片走 `PendingState{IDLE,WAITING_SNAPSHOT,GENERATING,DONE_HANDLED}`,视频走独立的 `VideoJob::Phase{IDLE,WAIT_A,WAIT_B,CAPTURING_B,AWAIT_CONFIRM,SUBMITTING,POLLING,DOWNLOADING_FAKE}`(两套状态机字段差异大,拆两个结构体比硬凑通用类型更清楚)。`JobManager`(ai_tools.h)统一管理异步任务的 进度/状态/结果路径,右上角卡片轮询它渲染。
- **ai_cards 卡片系统**:`ai_cards.h/.cpp`,图表/照片/视频/Job 进度统一堆叠到右上角、支持溢出换列、`[x]` 各自独立关闭——从 ai_ui.cpp 抽出的独立子系统。
- **ai_motion.h**:header-only 纯函数 `buildMotionPrompt(llaA, llaB)`(大圆方位角 8 方位罗盘 + haversine 距离 + 升降描述 → 英文 Veo 提示词),不依赖 osgViewer/libhv,tests 直接 include,不必拖入整个 ai_media.cpp 编译单元。

### 模块文件表

| 文件 | 职责 |
|---|---|
| `applications/earth_explorer/ai_tools.h` | Tool/ToolRegistry/AIJob/JobManager 纯头文件抽象(无 OSG 依赖,单测覆盖) |
| `applications/earth_explorer/ai_chat.h/.cpp` | LLMProvider 接口、GeminiProvider、FakeProvider、AIChatCore 代理循环 |
| `applications/earth_explorer/ai_media.h/.cpp` | SnapshotGrabber 快照抓帧、GeminiMediaProvider(生图)、VeoVideoProvider(视频)、MediaManager 状态机 |
| `applications/earth_explorer/ai_motion.h` | buildMotionPrompt 纯函数(A/B 两点 → Veo 运动提示词) |
| `applications/earth_explorer/ai_cards.h/.cpp` | 右上角卡片系统(图表/照片/视频/Job 进度堆叠) |
| `applications/earth_explorer/ai_ui.h/.cpp` | 底部输入条 + 对话历史 + 📷/🎬 按钮 + 视频确认 Modal |
| `applications/earth_explorer/ai_setup.h/.cpp` | `configureAIChat()`:注册全部工具、接线 MediaManager/AIChatCore/AIFrameHandler |
| `applications/earth_explorer/earth_main.cpp` | 调用 `configureAIChat()`、AIChatUI 绘制挂进 EarthControlUI |
| `applications/earth_explorer/quake_data.h/.cpp`、`flight_data.h/.cpp` | 增加 `summaryJson()` 只读统计接口供 AI 工具调用 |
| `tests/ai_chat_tests.cpp` | 单测(`osgVerse_Test_Ai_Chat`):ToolRegistry/JobManager/AIChatCore 代理循环各分支/parseGeminiResponse 防御解析/buildMotionPrompt |
| `applications/earth_explorer/test/ai_fake_*.json` | 7 组 FakeProvider fixture 脚本,离线驱动 E2E |

### 环境钩子表

| 变量 | 作用 |
|---|---|
| `EARTH_AI_KEY` | Gemini API key,设置后 AI 对话真正可用(未设置且无 `EARTH_AI_FAKE` 时,AI 相关代码路径完全不激活,行为与无此功能时一致) |
| `EARTH_AI_MODEL` | 对话模型覆盖,默认 `gemini-3.5-flash` |
| `EARTH_AI_VIDEO_MODEL` | 视频模型覆盖,默认 `gemini-omni-flash-preview`(Interactions API 同步生成;**不支持首尾帧插值**,B 点只进提示词);严格首尾帧穿越设 `veo-3.1-fast-generate-preview`/`veo-3.1-generate-preview` |
| `EARTH_AI_IMAGE_MODEL` | 生图模型覆盖,默认 `nano-banana-pro-preview`(可切回 `gemini-2.5-flash-image`) |
| `EARTH_AI_FAKE=<script.json>` | 离线 E2E:脚本化多轮 function-call 对话,不联网、不需要 key(见 `test/ai_fake_*.json`) |
| `EARTH_AI_FAKE_IMG=<png路径>` | 离线 E2E:`generate_photo` 的生图步骤替换成拷贝该文件字节 |
| `EARTH_AI_FAKE_MP4=<mp4路径>` | 离线 E2E:`confirmVideo` 的提交+轮询+下载整段替换成拷贝该文件字节 |
| `EARTH_AI_OUTDIR=<dir>` | 快照/生成结果输出目录覆盖,默认 `$HOME/Pictures/EarthExplorer` |
| `EARTH_AI_AUTOSUBMIT=<text>` | 测试专用:启动后自动 `core->submit(text)`,headless E2E 用 |
| `EARTH_AI_AUTOSUBMIT_DELAY_FRAMES=<N>` | 配合上面:延迟 N 帧再提交,给地震/航班等异步数据抓取线程留出同步到主线程的时间(默认 0 立即提交) |
| `EARTH_AI_VIDEO_AUTOTEST=1` | 测试专用:headless 无法点 UI 确认 Modal,固定帧数程序化走一遍 beginVideoCapture→captureVideoEnd→confirmVideo,复用真实状态机 |

### 测试体系

- **单测** `osgVerse_Test_Ai_Chat`(`tests/ai_chat_tests.cpp`):ToolRegistry/JobManager 基础行为、AIChatCore 代理循环(基础/忙碌态拒绝提交/多轮/错误呈现/循环上限配对/工具异常隔离)、`parseGeminiResponse` 防御性解析(畸形响应不崩)、`buildMotionPrompt`(西南向下降/持平/上升/北向 wrap 跨 337.5°边界)。纯逻辑、零窗口、零网络,exit code 断言。
- **7 组 fixture E2E**(`EARTH_AI_FAKE=test/ai_fake_*.json` + `EARTH_AI_AUTOSUBMIT`):flyto / quakes-summary / chart(bar) / chart-multi / flights-summary / photo(started→done) / video(两种:工具路径 phase=A、`EARTH_AI_VIDEO_AUTOTEST` 全流程 done→tour_*.mp4)。全部断言 `[AIChat]` 日志行,离线确定性,不需要真实 key。
- **零影响不变式**:不设任何 `EARTH_AI_*` 环境变量跑 600 帧,`grep -c "\[AIChat\]"` 必须为 0、exit code 必须为 0——这是红线,任何改动都不能破坏"没配置 AI 就完全没有 AI 代码路径被激活"。

### 已知边界

- **真机 Gemini/Veo 未冒烟**:本次实现全程没有可用的真实 `EARTH_AI_KEY`,对话/生图/视频三条真网络路径只验证到"请求构造正确、响应解析防御性够强(畸形/缺字段不崩)、HTTP 错误分支不崩溃"这一层,**没有验证过真实响应内容的观感质量**。Veo 视频生成需要付费层(约 $2-6/条),额外需要用户自己的计费账号。
- **快照/生成文件不清理**:`EARTH_AI_OUTDIR` 下的截图/生图/生成视频文件会持续累积,没有做过期清理或大小上限,长期使用需要用户自己清理(默认 `$HOME/Pictures/EarthExplorer`)。
- **ai_media.cpp 接近拆分阈值**:当前约 1090 行(照片状态机 + 视频状态机 + 两个 Provider 都在一个文件里),未来如果再加生成式媒体功能(比如更多视频模型/多图生成),建议先把视频那部分拆到独立的 `ai_video.cpp`,不要继续往这个文件里堆。
- **香港 f2 3D Tiles 建筑体未渲染(与本功能无关的既有问题)**:回归验证时发现 3D Tiles 图层只加载根 tileset.json,子瓦片(建筑体 mesh)从未流式渲染出来(地面本身平整、无 DSM 鼓包,之前的回归修复仍然有效)。已确认不是本次 AI Chat 改动引入,已 flag 给用户作为独立任务跟进。终审复核补充:本次 headless 签名(0 次子瓦片请求、0 次 KTX 转码)与旧的"headless 可见性之谜"(1413 次 KTX 转码但截图无差异)不同,排查时勿混为一谈;真机交互曾亲眼确认建筑可见,按既有结论应真机判断。

### 待用户验收清单

1. `export EARTH_AI_KEY=<你的 Gemini key>` 后跑 `dist/EarthExplorer.app`(或 `build/sdk_core/bin/osgVerse_EarthExplorer`)。
2. 底部对话条输入"飞到纽约上空" → 确认相机平滑飞过去、对话历史显示问答。
3. 输入"统计一下全球地震情况" → 确认地震层自动开启(若未开)、回答里有总数/分桶信息。
4. 点「照片」按钮 → 确认右上角出现生成中 Job 卡 → 完成后可点开查看生成的写实照片,判断图质是否可接受。
5. 点「视频」记录起点 A → 移动相机 → 点「完成B点」→ 确认框里核对 A/B 坐标与运动提示词是否合理 → 点「确认生成」(**注意:这一步真会花钱,约 $2-6**)→ 等待 Job 完成 → 查看生成的 8 秒巡航视频质量。
6. 若 Veo 账号没有付费层权限,确认收到的是清晰的 403/权限错误提示(灰显 + 聊天里的错误文案),而不是卡死或崩溃。

---
## ✅ 2026-07-02 会话(续2)— f2 正式接入为图层 + EARTH_BASEMAP 底图钩子

用户拍板"都做",已完成并验证。详见 `docs/superpowers/plans/2026-07-02-earth-hk-f2-layer.md`。

- **f2 正式图层**:`tiles3d_data.{h,cpp}` 重构为 `Tiles3DLayer` 控制器(仿 QuakeLayer);默认源=f2(免 key,实测),**懒加载**(勾选才后台线程拉根 tileset → update 回调主线程挂树;NodeMask 开关,关=零流量)。UI 新组「三维城市 / 3D City」+ 署名小字(OverlayLayer 新增通用 subtitle 字段)。`EARTH_3DTILES`:`1`=默认源随启动开 / `<url>`=换源 / `0`=关 / 未设=UI 控制。
- **EARTH_BASEMAP 钩子**:`google`(默认)/`esri`/自定义模板,createCustomPath ORTHOPHOTO 分支,预热同源。
- **"地面扭曲"真根因(用户指认"扭曲层更亮≠建筑"后三度修正,最终版)**:**Terrarium 高程是含楼高的 DSM**(尖沙咀 z15 瓦片 p90≈37m/最高 78m,真实地面 3-8m)× `TileElevationScale=2.0` → 城区 80-120m 假土包,亮色底图绷上面拉扯变形,与 f2(真实高度、偏暗着色)错位。**也是更早会话"地图扭曲"悬案的真根因。**次要因素:Google/Esri 正射掺倾斜航拍(楼烘焙进地面)。
- **修复(`e66e4f96`)**:九龙+港岛北岸平原 bbox 内 z>=12 高程返回空路径(平坦几何),假土包消失,太平山/狮子山在 bbox 外保留;`EARTH_HK_FLATDEM=0` 可关。效果图=f2 建筑站平整地面、维港干净、背景群山自然。不动共享插件。
- **验证**:f2 尖沙咀街道级细化 ✓、默认零流量 ✓、全球远景+极点回归 ✓、Esri 切源 ✓、flatdem 三连跑稳定 ✓(一次 teardown SIGBUS 偶发,复现 0/3,不阻塞)。真机交互验证待用户。
- 未闭环小事项(不阻塞)见 plan 文档:粗 LOD 观感无 SSE 调节钩子、EARTH_TILT 时序、dist 未重打包。

---
## ✅ 2026-07-02 会话(续)— f2 决策已定 + 已 push + f2 实测:数据全免 key 直达,但插件缺"外链 tileset 矩阵继承"(结论后被修正,见续2)

**用户把 f2 调研与否的决定权交给本会话,已按计划执行完毕**:9 个 commit 已 push `origin/master`(HEAD=`1484f3f7`),f2 实测完成,**结论:接入 f2 不是换个 URL 就行,插件有一个明确的缺口要修**。

### f2(3D Visualisation Map API)实测结果 —— 数据侧全绿,渲染侧一个明确缺口

- **完全不需要 key(实测)**:`https://data.map.gov.hk/api/3d-data/3dtiles/f2/tileset.json` 及其下所有子 tileset/b3dm,**不带 key 全部 HTTP 200 直接下载**。官方文档说的 key(同一邮箱 `3dmap@landsd.gov.hk` 免费申请)当前未强制。署名要求与 3dsd 相同。
- **纹理是真 photorealistic**:抽查叶子 b3dm,嵌入 **KTX2**(Basis)压缩纹理,单瓦片 74KB 真实航拍纹理。**引擎的 KTX2 转码链路实测工作正常**(`LoaderGLTF`→`loadKtx2`→`[LoaderKTX] Transcoded format ... 83f0`=DXT1,一次 headless 跑出数千次成功转码)。
- **结构(ContextCapture 风味,与 3dsd 不同)**:root tileset(asset 1.1,refine=ADD,box 为绝对 ECEF)→ 17 个外链子 tileset(如 `1/tileset.json`,**root.transform=纯 ECEF 平移**,box 为局部坐标)→ 再外链叶子 tileset(如 `1/Data/temp0/Tile_5_15_L4.json`,**自己没有 transform**,坐标沿用父级局部系)→ b3dm(gltfUpAxis=Y)。
- **f2 网格实际能渲染(用户亲眼确认)**:我一度根据"叶子 tileset 无自身 transform + headless A/B diff 0.02%"判断插件缺外链矩阵继承 → **这个结论是错的,已撤回**。真相:`ReaderWriter3dTiles.cpp::createTile` 先构建 content/children 子树、**最后**才用本文件 root 的 transform 包 `MatrixTransform`(约 L281-293),外链 tileset 作为子节点天然嵌套在引用方的矩阵之下,场景图自动完成累积——矩阵链路是通的。用户在测试窗口交互拖动时**亲眼看到 f2 带真实纹理的建筑**。
- **仍未解释的小谜(下轮如果正式接入 f2 时再查)**:headless `--goto 22.296 114.156 2.0` 俯视、开/关 f2 的截图逐像素 diff 只有 0.02%,但同次运行有 1413 次 KTX 成功转码——"瓦片已加载已挂树"与"该视角截图无差异"的矛盾没有闭环(怀疑与 LOD 挂接时机/相机视角有关,交互拖动时确认可见)。诊断时注意:headless 截图需 `dangerouslyDisableSandbox`(沙箱拦应用写 /tmp);`EARTH_TILT` 与 goto 动画时序冲突,低空斜视角构图 headless 复现不出来,别再浪费时间,直接真机交互看。
- **"地面地图扭曲"已定性 = Google 源影像自带(非 bug)**:直接 curl 原始瓦片(`mt1.google.com/vt/lyrs=s` 半山 z17)可见楼体大面积倾斜——香港高密度区 Google 正射拼接掺了倾斜航拍,楼的侧立面被烘焙进地面影像。与昆明瓦片错位是两回事,改渲染代码修不了。开 3D 建筑层后视觉冲突更明显(立体楼直立 vs 地面"画着"倒伏楼)。缓解方向:换更接近真正射的底图(如 Esri World Imagery,可做 EARTH_BASEMAP 钩子对比)或接受现状(f2 网格自带真实地面纹理,近距离会盖住底图)。
- **下一步(待用户点头)**:正式接入 f2 为可开关图层(URL 默认值/UI 开关/数据来源署名),顺带查上面那个 headless 可见性小谜;3dsd 路线继续可用,不冲突。

---
## ✅ 2026-07-02 会话 — 香港真实 3D Tiles：破碎 bug 已修复 + 纹理"问题"查明是官方预期行为

**本会话代码工作已全部完成、已提交(8 个 commit,`ed6257c3`..`ecb6e3b2`)、未 push**。真机已用真实 key 验证过。剩一个**用户尚未回答的决策**,新会话开场就该先问。

### 破碎 bug —— 已修复,已被用户真机确认

- **决定性隔离实验**:单独一个真实 b3dm 瓦片(脱离瓦片树、走与真实场景相同网络路径)依然破碎 → bug 在单文件渲染层,不在瓦片树组合。
- **真根因(出乎意料,不是数据解析问题)**:3D Tiles 图层挂在 `sceneCamera` 下,默认**继承**了地球的 `scattering_globe` 大气散射着色器(自己没挂程序)→ 被地表 clarity/大气混合逻辑渲染成惨白破碎面片。`LoadSceneGLTF.cpp` 的顶点/索引/纹理解码经运行时插桩验证**完全正确**,不是它的锅。
- **修复**(`c0dd09ff`+`dcc6ad28`,仅 `applications/earth_explorer/tiles3d_data.cpp` +62行):给这层挂一个自成一体的最小网格着色器(采样 DiffuseMap + 简单半兰伯特光照),`OVERRIDE` 覆盖继承的 globe 程序。仿照 `city_data.cpp` 建筑图层的既有模式。**`LoadSceneGLTF.cpp`/globe 着色器零改动**。
- 顺带修复 `ReaderWriter3dTiles.cpp` 的 box→包围球计算 bug(`ed6257c3`,独立成立,不是破碎问题主因)。
- **验证链条**:本地 fixture 隔离测试肉眼前后对比 → 独立子代理二审(合规性+代码质量,均过,读了 OSG 源码真核实,不是走过场)→ 4 类历史回归 headless 复测 + 关钩子零影响 + 与地震/航班共存 → **用户用真实 key 在真机跑了交互版**,九龙密集区截图确认**建筑形状已规整**(不再是破碎多边形),shape 层面的 bug 确认修复生效。

### 纹理"没贴图/发灰" —— 查明是官方数据集设计如此,不是 bug

用户真机截图显示建筑规整但**大片是纯灰色无贴图**,一度怀疑修复不彻底。深挖后:

- **数值铁证**:同一片密集区抽查两栋楼,一栋纹理 512×512(真实照片细节),另一栋只有 **8×8 像素**(纯色占位)——同一份数据里精度天差地别。
- **官方文档逐字确认(非推测)**:地政总署 `hkmapmeta.gov.hk` 原文——建筑分 **Level 1/2/3**,Level 1(footprint>4㎡ 的绝大多数普通建筑)"**do not apply photorealistic texture**",只有 Level 2/3(**"prominent buildings with landmark importance"**)才 "photorealistic texture applied"。8×8 占位 = Level 1 普通楼,512×512 真贴图 = Level 2/3 地标楼,**完全吻合官方设计**,不是我们代码的锅。
- 官方还有一个**独立产品"3D Visualisation Map"**(不同端点 `3dtiles/f2`,标榜"highly photorealistic"),`3d.map.gov.hk` 门户默认展示的很可能是这个,不是我们申请到的 `3dsd`(3D Spatial Data,偏 GIS 分析用途)——之前拿门户效果对比,是在比两个不同档次的数据产品。
- 顺带排查过"地图扭曲"的其他可能原因,均排除:`city_data.cpp` 城市列表不含香港(无图层重叠)、GIBS 云图 404 确认无关且早于本次工作存在、Google 卫星底图深层缩放 404 确实存在但集中在市区以南海域(不在建筑聚集区)。
- 已核实"API 迁移截止 2026-05-09"的说法是过期缓存信息,当前 API 文档页无此通知,我们用的端点结构和文档一致,无需改动。

### ✅ 悬而未决 → 已解决(2026-07-02 续会话)

用户把决定权交给了续会话,执行结果:**已 push**(9 个 commit 到 `origin/master`)+ **f2 已实测**(见最顶部章节:数据免 key 直达、KTX2 转码正常,但插件缺外链 tileset 的 transform 继承,待用户决定是否修)。

详见 `docs/superpowers/plans/2026-07-01-earth-hk-3dtiles-render-fix.md`(完整排查过程+判定分支)、`docs/superpowers/plans/2026-06-24-earth-hk-3dtiles.md`(累积实现结果)。

---
## ✅ 2026-06-24 会话 — 香港官方 3D Tiles 接入 spike（最新,先读这个）

香港地政总署官方 3D 城市模型接入的**最小验证 spike**,已合并 master(`a43e984f`,**未 push,待用户决定 push 时机/真机看一眼**)。核心落位链路**数值验证通过**。详见记忆 [[earth-hk-3dtiles-spike-verified]]、`docs/superpowers/specs|plans/2026-06-24-earth-hk-3dtiles*`。

- **数据**:香港 **3D Spatial Data API = Cesium 3D Tiles + WGS84**(`https://data.map.gov.hk/api/3d-data/3dsd/WGS84/{building,infrastructure}/tileset.json?key=KEY`)。**免费 key 已发邮件 `3dmap@landsd.gov.hk` 申请、待回**;授权可商用但**需署名 attribution**。下载版另有 OBJ/OSGB/Cesium3DTiles(3d.map.gov.hk 交互选区,非批量;**不下载全港**——流式才对)。
- **引擎已自带**:`plugins/osgdb_3dtiles`(tileset.json 递归 + root transform + PagedLOD + HTTP)+ `osgdb_gltf`(b3dm/glb),直吃,无需造轮子。
- **实现**:新模块 `applications/earth_explorer/tiles3d_data.{h,cpp}` + `EARTH_3DTILES=<url>` 钩子,挂 `sceneCamera`(同地震/航班 ECEF 系)。http→`readNodeFile(url+".verse_web", Options("Extension=verse_tiles"))`/local→`url+".verse_tiles"`。**不碰 globe 着色器/太阳/海洋 → 4 类回归之外**;默认不设钩子零影响。
- **验证 = 数值铁证**:公开样本(earthsdk 大雁塔、ArcGIS Stuttgart)**都 404** → 合成本地 fixture `applications/earth_explorer/test/gen_hk_3dtiles_fixture.py`(WGS84 ENU→ECEF 烘焙在香港坐标,引用自带 `girl.glb`)。加载节点 ECEF bound center 与香港中环理论值 `(-2416519,5387607,2403296)` **三轴吻合几十米** = 落位正确。矩阵约定:Cesium 列主序数组 = 插件 `osg::Matrix(m)` 行主序 + v*M,**直接填不转置**。
- **待真数据(key 到了)**:换香港 building URL → 处理 `?key=` 查询串解析、验**多层流式 LOD** + 真机视觉、做正式可开关图层 + UI 数据来源署名。spike 局限:单 tile fixture 验不了多层流式;合成 PBR 人物 headless 难肉眼辨(真数据带纹理建筑自然可见)。

---
## ✅ 2026-06-23 会话(续2)— 实时航班层 OpenSky

第三个实时数据流:**全球航班**。v0.10 已打标(地震+降水里程碑)。航班层 6 个功能任务子代理驱动 + 每任务审查,**已全部 push `origin/master`(HEAD=`93894f60`)**。`dist/EarthExplorer.app` 已重打包。用户已接受现状(含下述覆盖限制)。

- 新模块 `applications/earth_explorer/flight_data.{h,cpp}`(仿地震点要素模板)。**独立点图层 + 独立着色器,不碰 globe 着色器/太阳/海洋 → 4 类回归之外。**
- 数据:**OpenSky `states/all` 视口包围盒查询**(无 key;主线程每帧由相机算可见区 bbox→mutex→worker 抓;实测欧洲 900km 视口 3648 架)。libhv 后台 GET + picojson。
- 渲染(震撼+快):单 `GL_POINTS` + **按航向旋转的发光箭头**(片元旋转 gl_PointCoord 画三角)+ **高度配色**(低暖橙/中黄白/高冷青蓝);**每帧 CPU 外推位置→平滑滑行**(velMS×heading×elapsed,数千架仍廉价)。
- 交互:点击航班→**右上角「航班详情」面板**(呼号/国家/高度/速度/航向,叠在地震卡下方,遵循信息UI右上角偏好)。
- UI:「实时数据 / Live」组加「航班 (OpenSky)」开关(默认关)。钩子 `EARTH_FLIGHTS=1`、`EARTH_FLIGHTS_FILE=<states.json>`。
- 设计/计划:`docs/superpowers/specs/2026-06-23-earth-flight-layer-design.md`、`docs/superpowers/plans/2026-06-23-earth-flight-layer.md`。详见 [[earth-flight-layer-done]]。
- 注:首次抓取偶尔 0 架(启动 bbox 未稳),~12s 后正常;3648 架在 900km 视口偏密(低空更稀),如嫌密可调 bbox/尺寸。
- **数据覆盖(用户已问"中国为何少"、已确认非 bug)**:OpenSky 众包 ADS-B,覆盖随接收站。实测美国 7195/欧洲 3517/中国 338/印度 258/日韩 42——东亚稀疏=接收站少+中国管制,**改代码补不全**(注册账号只提频率不增覆盖;真·全球需 FR24/Aireon 商业付费源)。用户接受现状。详见 [[earth-flight-layer-done]]。
- **下一步候选(未定,均结合"无 key 优先")**:火灾 FIRMS(需免费 key、视觉震撼、套点模板)、空气质量 OpenAQ(无 key、最快)、极光 SWPC(无 key、需新极区渲染);或打磨现有(航班拖尾/密度/UI)。

---
## ✅ 2026-06-23 会话(续)— 消除"补丁状瓦片" + 降水两处修复

**已全部 push `origin/master`(HEAD=`f48e61af`,含降水层+性能/切换/补丁修复共 14 提交)。** 用户已确认观感 OK(瞬态加载补丁=在线流式固有,选择维持现状)。`dist/EarthExplorer.app` 已重打包。

**① 补丁状瓦片修复(`7495299e`)** —— 用户报"全球操作时地球花得像补丁、各瓦片阴影程度不同"。
- **根因(决定性诊断,非源影像)**:把 globe frag 临时改成纯 `originalGroundColor` 对比 → 补丁消失 → 证明是**渲染着色**:大气散射+HDR 把影像洗灰发雾,且 z3 粗 OCEAN_MASK 派生的太阳着色法线逐瓦片块状跳变。
- **修法**(`scattering_globe.frag.glsl` + `render_effects.cpp`):`finalColor = mix(spaceColor, clearColor, groundClarity)` —— `spaceColor`=现有大气观感(高空保留)、`clearColor`=原始影像×平滑昼夜(低空清晰、无雾)、`groundClarity`=随相机高度 smoothstep。法线改平滑 `normalize(P)`(删粗 mask 法线扰动=块状根源;真实起伏仍由高程几何体现)。band env 可调 `EARTH_CLARITY_ALTLO/ALTHI`(km,默认 2000/8000)。uniform 加在应用层 globe stateset,**不碰共享管线**。
- **验证**:低空清晰无补丁、中空平滑过渡、高空太空观感保留、晨昏线平滑、海洋不橙、低空倾斜不穿模、地形起伏在。详见 [[earth-tile-patchwork-fix]]。

**② 降水性能修复(`fd198070`)** —— 开降水后任何操作卡顿。根因:OVERLAY 路径切换触发**主线程同步逐瓦片拉网络**(最多1500个)。修法:`updateLayerData` 的 OVERLAY 改用 `nv->getImageRequestHandler()`(场景自带 ImagePager)异步加载。详见 [[earth-precip-layer-done]]。

**③ 降水切换 bug 修复(`c78b1f59`)** —— 云图↔降水来回切后降水不再出现。根因:`_lastTemplate` 去重跨开关失效。修法:再开启时 `force` 绕过去重。

---
## ✅ 2026-06-23 会话 — 降水雷达图层 RainViewer

接入**第二个实时数据流**:RainViewer 降水雷达(原计划"地震+降水"的 Step 2,补齐)。**复用 GIBS 云图的 OVERLAY 槽、与云图二选一、零着色器改动**。4 任务提交(子代理驱动 + 每任务双审,含一处线程安全修复),**待用户真机交互确认后 push**。`dist/EarthExplorer.app` 已重打包(打包二进制验证通过)。

- **OVERLAY 槽按 prefix 分流**(`earth_main.cpp` createCustomPath):`"gibs"`→GIBS / 以 `http` 开头→RainViewer 模板(z>10 不取)/ 空→不加载。默认仍 `"gibs"`,行为不变。
- 数据:`weather-maps.json`(无 key)→ `host`+`radar.past` 末项 `path` → 模板 `{host}{path}/256/{z}/{x}/{y}/4/1_1.png`(256px **RGBA**,无降水处透明)。已 curl 验证。
- 新模块 `precip_data.{h,cpp}`:`PrecipController`(osg::Referenced)+ 后台 `FetchThread`(~8min,仅开启时联网,15s 超时)。**worker 不直接动 TileManager**(`_layerPaths` 无锁、主/cull 线程每帧 `check()` 读)——worker 经 mutex 交模板,**主线程 FRAME handler `applyPending` 调 `TileManager::setLayerPath(OVERLAY, 模板)`**;per-tile `check()` 自动重载(city_data 切 USER 层同款)。`_enabled`/`_refreshNow` 用 `std::atomic<bool>`。
- 互斥 + UI:「影像 / 天气」组两个互斥复选框「GIBS 影像/云图」+「降水雷达 RainViewer」。apply 用**直接写对方 `enabled` 字段**(不调 `setEnabled`)避免递归;`Overlay2Opacity` 由 `applyOverlayOpacity` 统一算(降水>云图>0)。
- 钩子:`EARTH_PRECIP=1`(强制开)、`EARTH_PRECIP_FILE=<weather-maps.json>`(离线样本)。
- 关键坑(已记):①`<readerwriter/TileCallback.h>` 独立编译需先 `#include <osgDB/Options>`(有 `ref_ptr<osgDB::Options>` 成员);② worker→TileManager 数据竞争(已用主线程 handler 修)。详见 [[earth-precip-layer-done]]。
- headless 验证:欧洲实时降水雷达可见、晨昏线/海洋无橙、默认关零抓取。**真机待确认:开关互斥、~8min 刷新**。
- 计划/设计:`docs/superpowers/plans/2026-06-23-earth-precip-layer.md`、spec 的 Step 2 段。

---
## ✅ 2026-06-22 会话 — 实时地震图层接入

接入**第一个真·实时全球数据流**：USGS 地震叠加层。**5 个任务提交**(子代理驱动 + 每任务双重审查 + 整体终审,均 headless 验证),**待用户真机交互确认后 push**。`dist/EarthExplorer.app` 已用最新二进制重打包(打包二进制 headless 跑通)。

- 新模块 `applications/earth_explorer/quake_data.{h,cpp}`(仿 `city_data.cpp`),对外只暴露 `QuakeLayer` 抽象 + `configureQuakeData`。**完全不碰 globe 着色器/太阳/海洋 → 4 类回归零风险**(终审确认 diff 只动 6 个文件,无 `scattering_globe.frag.glsl`/sun/OceanOpaque)。
- 数据:**USGS `2.5_day.geojson`**(M2.5+、过去24h、无 key、HTTPS),`libhv`(已编进 osgVerseDependency,CMake 加 `-DHV_STATICLIB`+include)后台线程 GET + `picojson` 解析。headless 实测联网拉到 **49 条**真实地震。
- 渲染:`GL_POINTS` 圆点精灵(独立 shader,MRT 双输出 gl_FragData[0/1],非 globe 着色器),**大小∝震级、颜色∝深度**(浅红/中橙/深蓝),ECEF 抬升 3km、深度测试遮挡背面。
- 实时:后台 `FetchThread` **每 ~60s** 刷新,**仅图层开启时联网**(关闭空转);`requests` 设 15s 超时界定退出 join。主线程 update 回调重建几何(worker 不碰 GL)。
- 交互:**点击地震点**(屏幕拾取 + 前半球过滤)→ ImGui「地震详情」卡片(震级/深度/位置/经纬/相对时间/USGS 链接)。
- **拾取 bug 已修(用户报告点击无卡片,系统化调试找到根因)**:`pickAt` 原有 `win.z∈[0,1]` 深度闸门把所有点都剔除了——事件阶段读到的相机投影近/远≠渲染时那套(osgViewer 在 cull 才按场景包围盒重算近/远,地球在 RTT 子相机渲染),地表点 z 饱和到 ≈1.0001。前半球测试已负责剔背面,故删掉脆弱的 z 闸门(只用 win.x/y),拾取容差 6→10px。新增 `EARTH_QUAKE_PICKDBG` 自测钩子(投影全表+自拾取)。提交 `24222220`。
- UI:面板新增「实时数据 / Live」分组 +「地震 (USGS)」开关(默认关)。测试钩子 `EARTH_QUAKES=1`(强制开)、`EARTH_QUAKES_FILE=<path>`(本地样本离线验证)、`EARTH_QUAKE_PICKDBG=1`(拾取自测)。
- 设计/计划文档:`docs/superpowers/specs/2026-06-22-earth-quake-precip-overlays-design.md`、`docs/superpowers/plans/2026-06-22-earth-quake-layer.md`。
- **下一步 = Step 2 降水**(RainViewer 雷达,复用云图 OVERLAY 槽、二选一、零着色器改动),设计已记入上面 spec。
- 终审 1 条 minor 备注(非阻塞):`QuakePickHandler`/`EarthControlUI` 持 `QuakeLayerImpl` 裸指针,当前"全随进程退出销毁"生命周期下无害(city_data 同款风格);若日后模块复用需改 observer_ptr。

---
## ✅ 2026-06-21 会话 — 已完成与现状

本会话在干净 master 上做完一批，**全部已 push `origin/master`（HEAD=`b2377bda`）**。**`dist/EarthExplorer.app` 已重打包为最新**（双击即最新）。

**已完成（均 headless 验证 + push）：**
- `56a8729b` fix：**GIBS 影像换 `VIIRS_SNPP`**。原 `MODIS_Terra` 单星日产真彩在赤道留黑色刈幅空隙（梳齿黑楔）；VIIRS 无缝。新增 `EARTH_CLOUDS` 测试钩子。详见记忆 [[earth-gibs-swath-gaps-use-viirs]]。
- `74706d8a`+`db694f27` feat：**启动磁盘预热**（回退功能重做）`prefetchLowLODGlobe`+`EARTH_PREFETCH`（默认 4，0 关）+ `loadFileData` 原子 `tmp+rename`。
- `c9d15a94` perf：**地形地板每帧全场景求交节流**。`updateTerrainFloor` 从每帧 → 移动>330m 或每 15 帧才求交（静止低空降 95%），消低空卡顿，**可能也缓解 #8**。
- `873e2761`+`6c041827` perf：**瓦片 5 层并行加载**（回退功能重做）`LayerLoadPool`+`EARTH_TILE_POOL`（默认 8，0 关）+ reader-writer 缓存加锁。冷缓存吞吐 **~4×**（92→364）。**回归守卫过**（昆明深瓦片+tilt，pool=8≈pool=0，无孔洞/穿模/橙）——但 headless 够不到 z16，真机街道级深下钻+高 tilt 仍建议留意那 3 类。
- `b2377bda` feat：**自适应曝光**（修低空发暗"像黑夜"）。随高度设 `HdrExposure`（低空 1.0、高空 0.25，80–4000km smoothstep 过渡），**纯 C++、着色器不动 → 4 类回归零风险**。`自适应曝光 Auto` 复选框（默认开）+ env `EARTH_EXP_LO/HI/ALTLO/ALTHI` 可调。详见 [[earth-low-altitude-dark-atmosphere]]。

**近平面 Step1 = 查清"不必做"**：`_minDistance` 是死代码（钳眼到地心、永不触发）；防穿靠地形地板 `_terrainLift`；高峰轻微穿模用户接受不修。详见 [[earth-near-plane-mindistance-dead-and-input-freeze]]。

**测试钩子（均 env，默认无影响）**：`EARTH_CLOUDS`、`EARTH_SUN_AZEL=az,el`、`EARTH_TILT`、`EARTH_INPUT_DEBUG`（写 `/tmp/earth_input.log`）、`EARTH_TERRAIN_DEBUG`、`EARTH_TILE_POOL`、`EARTH_PREFETCH`、`EARTH_EXP_*` + 既有 `EARTH_AUTOCAP`/`EARTH_FRAME_SLEEP_MS`/`EARTH_SUN_TO_CAMERA`/`EARTH_TILE_CACHE`。

**仍开放（详见下方 backlog #7/#8/#9 + 记忆）**：
- **#8 偶发输入失灵死锁**：低空探一阵后鼠标对地球失灵（Go To/ImGui 仍可用）、macOS 彩球、Home 救不回、**重启恢复**。未解；疑 ImGui 粘住捕获鼠标；`EARTH_INPUT_DEBUG` 待抓；地形节流或已缓解。
- **#9 掠射太阳橙块**：疑程序化海洋（`OceanOpaque`），**未证实**。决定性测试 = 在出橙时取消「海洋 Ocean」看橙是否消失（本会话用户只对**发暗**做过取消海洋测试=无变化；**橙的取消海洋测试还没做**）。
- **剩余回退功能**：高程感知裙边（只能交互验，易触发 #8）、海洋遮罩深瓦片继承（可选/低优先，建议用祖先烘焙）、完整异步层加载（高风险）。

---
## ⚠️ 2026-06-20 会话已整体回退 —— 必读，别再踩

上一会话试图修"昆明深瓦片错位"，结果**越改越多**，被用户要求**整体回退到会话前版本 `0f902837`**。
当时引入、现已删除的一系列回归 bug —— **下一步迭代严禁再次引入**：

1. **整片海洋变橙红**（全球俯视 33000–43000km，连夜侧也橙）。根因：把默认太阳方向翻成照亮正面后，
   `scattering_globe.frag.glsl` 的**太阳直射辐射 sunL**（低太阳角=落日橙）叠在近黑的海面上 → 全橙；亮地面(撒哈拉)过曝发白。
   **不是瓦片、不是缓存**（Google 海洋瓦片本来就是蓝的，已 curl 验证）。
2. **晨昏线被删**：曾用"直接显示影像(fullbright)"去压橙色，结果把昼夜分界整个干掉了。**用户要保留晨昏线。**
3. **倾斜穿模 / "高耸面片"扇形**：低空(如昆明 2.5km)倾斜时相机钻到地下、看穿地球看见另一侧。
   根因：相机地形地板只**正下方**射线，漏掉**旁边更高的地形**(西山 2.5–2.8km)，倾斜看向它就穿。
4. **桔红程序化水面 / OceanOpaque 默认乱动**：用户明确"程序化光滑水面原来从不显示"，别默认开它。

**下一步迭代铁律**：① 一次只改一个、当场用真实复现条件验证、确认不引入上面 4 类再继续；
② 在**用户报告的确切条件**下复现(高度段/开关/太阳)，别乱缩放截图；③ 改渲染前先 curl 真实瓦片确认不是数据问题；
④ 不要碰默认太阳方向/OceanOpaque 去"修"颜色。详见记忆 [[earth-orange-ocean-rootcause]]、[[earth-debug-verify-at-exact-condition]]。

**✅ 原始 bug 已修复并推送(2026-06-20 后续会话)**：昆明/滇池岸"**看穿孔洞**"(z>15 平瓦片 0m vs z15 高程 ~1890m 的 LOD 边界看穿到地球背面)**已修，用户交互确认大孔洞消失**。详见记忆 [[earth-seethrough-hole-fix]]（含根因、诊断法、窗口真相）。本会话提交(均已 push `origin/master`)：
- `b34ce064` test：`EARTH_TILT` 测试钩子(headless 斜视复现用)。
- `b5976ac7` fix：**相机地形地板**(防穿模，用户确认"始终为正")—— `EarthManipulator` 每帧按**眼睛自身经纬度**竖直射线测地形、把眼睛抬到地形+150m；倾斜可用、far/全球视图跳过。
- `48e38efd` fix：**深瓦片高程一步烘焙**(消看穿孔洞)—— `createCustomPath` 对 z>15 返回 **z15 祖先瓦片** URL；`createTile` 算 `elevScaleBias=((x&(subN-1))/subN,(y&(subN-1))/subN,1/subN,1/subN)`；`createTileGeometry` 用 `euv=uv*scale+bias` 采祖先子区一步烘焙真实高度(影像仍各自全分辨率)。**确定性、无运行时继承竞态**(重做被回退的 `fabf2747`/`e4c177e2`，这次单独做+验证)。配相机地板防穿模；不碰太阳/海洋/着色器→无 4 类回归。
- **全球性已验**(纯瓦片坐标、无地点假设)：珠峰深瓦片 ~8662m、昆明 ~1900-4000m、上海读到东海海床 bathymetry 负值(Terrarium 数据本身、非回归)。
- `dist/EarthExplorer.app` 已用最新二进制**重打包**(全屏，`NSHighResolutionCapable=false`)。

**残留(用户接受、待办、卫星图可接受)**：陡坡瓦片高程范围大→包围球被撑大→**过度细分到 z>15**→偶有极小缝/小范围 LOD 跳变。根治是独立 perf 项(修包围球/`PIXEL_SIZE_ON_SCREEN` 估算)。沿海城市真实街道级观感待人工扫一眼(headless 够不到 z16)。

**窗口"左下 1/4"别再乱修**：是 `.app` 的 Info.plist `NSHighResolutionCapable=false` 修的、**裸二进制 1/4 是正常的**、`getScreenResolution` 返回点数。详见 [[earth-seethrough-hole-fix]] + `tasks/lessons.md:126`。

**更早的校正会话**(同日早些)：补齐漏回退的 `ReaderWriterWeb.cpp`(代码树曾 = `0f902837`)+ 校正本 HANDOFF 陈旧描述。
---

## 这是什么
osgVerse（基于 OpenSceneGraph 的 3D 引擎，`github.com/anloren/osgverse`）的 **EarthExplorer**：一个在线流式高清地球应用，已移植到 macOS（OpenGL 4.1 Core），有 ImGui 控制面板、可双击 `.app`。

## 关键路径
- 仓库：`/Users/USER/osgverse`（远端 `origin` = github.com/anloren/osgverse，已用账号 `anloren` 登录 gh）
- OSG 源码（预克隆，含本地补丁）：`/Users/USER/OpenSceneGraph`
- CORE 构建产物：`/Users/USER/osgverse/build/sdk_core/`；增量构建目录：`/Users/USER/osgverse/build/verse_core/`
- 可执行：`build/sdk_core/bin/osgVerse_EarthExplorer`；桌面应用：`dist/EarthExplorer.app`
- 实现计划：`docs/superpowers/plans/`；排坑记录：`tasks/lessons.md`；用户文档：`docs/EarthExplorer.md`

## 必须知道的硬约束（否则白干）
1. **macOS 必须用 CORE 模式**（`Setup.sh CORE` → sdk_core/verse_core）。兼容档只有 GL2.1/GLSL120，osgVerse 着色器要 ≥130；CORE 才有 GL4.1/GLSL410。
2. 直接跑二进制要设 `DYLD_LIBRARY_PATH=build/sdk_core/lib`（rpath 用了 Linux 的 `$ORIGIN`，macOS 无效）。
3. **增量构建**：`cmake --build /Users/USER/osgverse/build/verse_core --target install --config Release`（秒级到分钟级，别重跑 Setup.sh）。
4. **无头截图验证**（OS `screencapture` 缺权限，用应用内 `ScreenCaptureHandler`）：
   ```bash
   cd /Users/USER/osgverse/build/sdk_core/bin && rm -f /tmp/earth_capture_0.png && \
   DYLD_LIBRARY_PATH=/Users/USER/osgverse/build/sdk_core/lib \
   EARTH_AUTOCAP=1500 EARTH_SUN_TO_CAMERA=1 \
   ./osgVerse_EarthExplorer --no-wait --resolution 1280 800 > /tmp/run.log 2>&1
   # 然后用 Read 工具看 /tmp/earth_capture_0.png
   ```
   - `EARTH_AUTOCAP=N`：渲染 N 帧后截图到 `/tmp/earth_capture_0.png` 退出。**注意**：本机约 ~700 帧后 ImGui 后端会 shutdown，N 设太大反而抓不到；定点深瓦片用 `EARTH_FRAME_SLEEP_MS=30` 给网络时间，`EARTH_AUTOCAP=2500` 左右。
   - `EARTH_SUN_TO_CAMERA=1`：太阳对准相机，便于看清（会每帧覆盖 WorldSunDir）。
   - `--goto <纬> <经> <高度km>`：启动直飞某地（俯视）。如 `--goto 37.77 -122.42 6` 看旧金山街道级。
   - `EARTH_FRAME_SLEEP_MS=30`：每帧延时，给在线瓦片流式加载真实时间。
5. **深层瓦片是延迟受限**：拉近后要等几秒瓦片流式加载（z 逐级 3→6→14…到街道级），不是瞬时。已加 TCP/TLS 长连接复用 + 20 HTTP 线程加速。

## Git 现状（已校正 2026-06-20 —— 回退后真实状态，旧正文作废）
- **HEAD = `878eb044`，代码等价于 `0f902837`**。本会话补齐了当初漏回退的半残留：`git checkout 0f902837 -- plugins/osgdb_web/ReaderWriterWeb.cpp`（那个文件的 `_cachedRWMutex` 锁是 #3 并行池的遗留，#3 已回退故一并清掉）。现**整棵代码树与 `0f902837` 逐字节一致**（`git diff 0f902837 -- '*.cpp' '*.h' '*.glsl'` 为空）。
- **真正在 master（活着的）**：P0 标注层、P1 GIBS 影像层、解析 ray-椭球求交(99d3f223)、**elevation z>15 截断(dfd8fd09 —— 正是下面开放 bug 的成因)**、深海蓝未加载默认+常驻 LOD(7a6d1635)、晨昏线调淡(0f902837)。详见下方 P0/P1/性能小节（那几节描述的代码确实在 master）。
- **⚠️ 已被回退、master 里没有（下个 session 别假设存在！）**：启动预热 #1(ba191b19)、5 层并行池 #3(da251b96)、深瓦片高程继承(f99b591e/e4c177e2)、海洋遮罩继承(87b9d155)、最小距离 150m(747b67a5)、相机地形地板/地形感知钳制(e4c177e2/dbeaafa4)、高程感知裙边(837ff999)、太阳翻面+OceanOpaque+fullbright 着色器(4b6f5333/34041206/21dc5777)。**回退一直退到 0f902837，把整条 6-19/6-20 深瓦片线全带走了。** 本文件**旧正文**曾把这些写成"已推送 master / ✅ 已做"——那是回退前描述，**全部作废**（下方 backlog #1/#3 同此）。
- **开放 bug —— 本会话已在现行代码核实根因（昆明/滇池岸深瓦片错位）**：
  - `applications/earth_explorer/earth_main.cpp:204` `if (z > 15) return ""`：z>15 高程路径为空（terrarium 上限，避免 404 主线程阻塞——这是有意保留的 perf 修复）。
  - `readerwriter/TileCallback.cpp:570` `if (!tex && !emptyPath && ...)`：父级高程继承被 `!emptyPath` 门挡住，而截断使 `emptyPath=true`，故继承**永不触发** → 深瓦片停在 0m 海平面、比 z15(~1890m)邻居低 ~1890m → LOD 边界裂缝/看穿/视差错位深块。
  - **为何难修（耦合，上次栽点）**：修法 = 让 z>15 继承 z15 高程（把地形抬到 ~1890m）；但 `readerwriter/EarthManipulator.h:76` `clampDistanceValue=max(dist,minDist)` 是按**椭球基准(0m)**钳制相机、不知地形（解析求交也忽略地形）→ 抬高地形后任何近地缩放都穿模。所以**高程继承与相机地形地板必须配套**，而地形地板（只向正下方射线、漏掉旁边更高地形）正是上次倾斜穿模回归的根。**且 headless 复现不出那个斜视角（本会话已实测），只能交互验证。** 留待用户有空时，按"一次一步、每步交互验证"推进。
- **.app 状态**：`dist/EarthExplorer.app` 很可能是回退前的旧打包（含已回退代码）；要用双击版须从**当前 master 重新** `packaging/package_macos.sh` 打包。Tag v0.8/v0.9/v0.9.1 都在，但**只有 v0.8 是 GitHub Release**——release 须用户确认，否则只同步仓库（见记忆 `release-requires-confirmation`）。
- 设计 spec：`docs/superpowers/specs/2026-06-18-earth-overlay-layers-design.md`（整体路线）。计划：`plans/2026-06-18-...p0-labels.md`、`...p1-gibs-clouds.md`、`2026-06-19-earth-perf.md`。
- **P0 标注层**：底图 `lyrs=s`；标注 `lyrs=h`(USER,texUnit2/UvOffset3)；`LabelOpacity` 门控；`LayerManager`(`applications/earth_explorer/LayerManager.h`)+「图层」UI(方案B 分类折叠)。
- **P1 GIBS 影像层**：GIBS MODIS 真彩(OVERLAY,texUnit3/UvOffset4,`GoogleMapsCompatible_Level9`,**z>9截断**,日期=`UTC今天-2天`)；`Overlay2Opacity` 门控(**大气采样器起始单元 3→4**,applyToGlobe)；面板「影像/天气」组、默认关、透明度0.7。**取舍**：earthURLs 常驻加载→默认关时仍下载(P1.1 改按需)。
- **性能/视觉优化**(plan `2026-06-19-earth-perf.md`)：
  - 求交(99d3f223)：`EarthManipulator::calcIntersectPoint` 改解析 ray-椭球，去掉每次鼠标的全场景 `IntersectionVisitor` 遍历。
  - elevation z>15 截断(dfd8fd09)：AWS terrarium 最高 ~z15，深 zoom 的 z16-19 必 404→主线程阻塞(近地卡死主因)+刷屏，已消除；深瓦片复用父级高程。
  - 橙块(7a6d1635)：未加载底图默认 白→深海蓝`Vec4(0.03,0.06,0.10)`(Scene 单独，Mask 仍白=陆地，见 `render_effects.cpp:89`)；`pager->setTargetMaximumNumberOfPageLOD(1500)`(默认300太小→低/中LOD过期重载露白底)。**用户实测：半个地球橙块已消失** ✅。
  - 晨昏线(0f902837)：橙带=昼夜分界大气 inscatter+饱和过强。`scattering_globe.frag.glsl` inscatter `*0.5`、饱和 `1.18→1.08`(仅夜/晨昏侧 cTheta→0 用到，不动白天影像)。**调淡/调浓就改这两个数。**
  - **缓存查明本就好**：真缓存在 `loadFileData`(`UtilitiesEx.cpp:496`)，per-tile `.tile`、`$HOME/.osgverse_earth_cache`(绝对、env `EARTH_TILE_CACHE` 可覆盖)、无限大小；实测 8537 块/重访 0 新增。registry 的 simdb `FileCache` 不用于 HTTP 瓦片（别再去改它，曾误改致崩溃）。

## 待用户交互验证（headless 无法验）
鼠标旋转/缩放手感、近地流畅度、晨昏线观感（太重→inscatter 再降到 0.3；太淡→回 0.7）。

## 下一步 backlog（下个 session 逐步做，按优先）
1. ⏪ **启动预载整个地球低 LOD 瓦片**（**已被回退、不在 master**；以下为当初实现细节，作重做参考）：`earth_main.cpp` 新增 `prefetchLowLODGlobe(maxZ)` + main 里 detach 后台线程；4 worker 拉 z0..maxZ 全球 **底图+标注+高程** 三层进磁盘缓存。`EARTH_PREFETCH` 控制最大 zoom（默认 4，0 关闭）。配套把 `loadFileData` 缓存写改成 **temp+rename 原子替换**（`UtilitiesEx.cpp`，并发安全）。细节见 `tasks/lessons.md`「启动预热」节。
2. **完整异步层加载**（消除首访顿挫）：`TileCallback::operator()`(update=主线程) 在 `_layersDone=false`/`TileManager::check()` 时同步 `readImage` 阻塞(`TileCallback.cpp` updateLayerData，异步 `requestImageFile` 被注释)。改后台 thread-pool 加载、就绪帧应用。**高风险(并发)、headless 无法验 UX**——留交互会话谨慎做。（注：#3 的 `LayerLoadPool` 已回退，不存在；#2 若做需自带线程池。）
3. ⏪ **瓦片吞吐并行**（**已被回退、不在 master**；以下为重做参考）：`ReaderWriterTMS::createTile` 的 5 层从串行改成**常驻 keep-alive 线程池** `LayerLoadPool` 并行加载（`EARTH_TILE_POOL`，默认 8，0=关闭=原串行）。**不能用临时线程**（会丢 thread_local TLS keep-alive→更慢）。配套加锁两处既存无锁 reader-writer 缓存 race（`TileManager::getReaderWriter` + `ReaderWriterWeb::getReaderWriter`）、`_layerPaths` operator[]→find()。细节见 `tasks/lessons.md`「瓦片 5 层并行加载」节。
4. **中国 GCJ-02 偏移**(已知限制)：中国区 `lyrs=h` 标注是 GCJ-02、卫星 `lyrs=s` 是 WGS-84，偏 ~50-700m。可在标注层 UV 做顶点级 GCJ-02→WGS-84 校正(仅中国、近似)。用户说"实在不行就算了"。
5. 之后按 spec：P2 RainViewer 雷达 → USGS 地震 / OpenSky 航班(点要素=独立 billboard 子图+后台拉取线程)。
6. **✅ 低空画面发暗/发蓝(2026-06-21,已修)**：高度下降时低空发暗"像黑夜"。**真因=曝光**(用户隔离测试:取消海洋无变化、拉曝光会变亮、real-time 太阳关着)——高空靠 inscatter 撑亮、低空 inscatter 消失只剩偏低 `HdrExposure=0.25`。单一曝光顾不了两头(低空要≥1、高空 1 就过爆)→ **自适应曝光(`b2377bda`)**:随相机高度设 `HdrExposure`(低空 1.0/高空 0.25/80-4000km 过渡),纯 C++、着色器不动。env `EARTH_EXP_LO/HI/ALTLO/ALTHI` 可调、`自适应曝光 Auto` 复选框。**残留**:掠射(El=0)低空仍"灰灰的"=暮色本质,曝光只提亮压不掉,用户暂可接受(想更鲜需调高 `EARTH_EXP_LO` 或太阳抬高)。**早先把它归成"散射管线雷区/sun-overhead 仍暗"是误判**(sun-overhead 其实不暗,是斜射+低曝光)。详见 [[earth-low-altitude-dark-atmosphere]]。
7. **~~近平面最小距离~~(已查清=不必做)**：实测 `_minDistance`/`clampDistance` 是**死代码**(钳的是眼到地心、永不触发),`--goto`/缩放都不受它限制;spec/lessons 说"50→150 修近平面"**是错的**,那个无效 commit 已 `git reset` 回退。真正防穿靠**相机地形地板** `_terrainLift`(b5976ac7),已生效。**残留=昆明/旧金山最高峰贴最近时"很轻微"穿模,用户决定先不修。** 详见记忆 [[earth-near-plane-mindistance-dead-and-input-freeze]]。
8. **偶发输入失灵死锁(未解)**：低空(尤其飞到冷僻新区如太平洋)+ 拖拽/倾斜探一阵后,**鼠标对地球操作全失灵**(Go To/ImGui 仍可用、能 home 到几万公里),macOS 彩球但菜单可动,**Home 救不回、重启可恢复**(偶发)。已证伪:非 `_minDistance`/非 NaN 顶点(无尖刺)/非地形地板每帧求交性能(高空仍失灵)/非纯同步加载。**最大嫌疑=ImGui 粘住捕获鼠标(WantCaptureMouse),未证实。** 已加诊断钩子 `EARTH_INPUT_DEBUG=1`(写 `/tmp/earth_input.log`)待下次复现定性。附:`updateTerrainFloor` 在 <30km **每帧全场景求交**是真实性能隐患(日后优化)。详见记忆 [[earth-near-plane-mindistance-dead-and-input-freeze]]。
9. **掠射太阳橙块(2026-06-21,用户暂搁置)**：太阳掠射(El≈0)时**海洋上出现大片橙红、边缘呈直线(瓦片形状)、随视角转动出现/消失**。决定性排除:`EARTH_SUN_TO_CAMERA=1` 全亮下橙**消失** → 是**掠射着色**,非纹理/影像 bug(那个红棕≈Terrarium 高程色是巧合,不是它)。**headless 海洋默认关(`OceanOpaque=0`)永远复现不出** → **强烈怀疑=程序化海洋(`OceanOpaque`,用户「海洋」勾着)掠射反射橙天空,直边=海洋按瓦片渲染**;未最终证实。**决定性测试(下次)**:取消「海洋 Ocean」看橙是否消失;若是→修海洋掠射反射或让它默认关。**教训**:本次错往散射/纹理找、反复 headless 复现失败,根因是漏了"用户海洋开、headless 海洋关"这个差异;我那些散射去饱和尝试**全已撤回原版**(`.app`/build/源 三处 = git HEAD)。见记忆 [[earth-orange-ocean-rootcause]]。
- **其它已知限制**：headless 截图 `EARTH_AUTOCAP`≤~280（交互不受影响）；`gmtime_r` 仅 POSIX（Windows 需适配，既有模式）。

## 极地处理：v0.9 去截断造成空洞 → v0.9.1 撤回，保留星状（2026-06-18）
**最终结论**：全球/中纬贴图本来就正确（早先以为不正确是误诊）。极点 v0.9 试过"去截断"反而留出敞开极冠盘/空洞、像球被整体变形，用户否决；**v0.9.1 已恢复截断**，回到星状极点（更顺眼、轮廓正常），星芒作为已知限制保留。

**早先误诊（别再犯）**：commit `50f73d4c` 以为"瓦片内部顶点按纬度线性插值、要在 `computeTileExtent` 做墨卡托反投影"，照此改反而把整张贴图搞扭，已回退（`47e0072e`）。

**重新定位的真相（证据驱动）**：
1. **顶点级墨卡托反投影早就正确**，藏在 `TileCallback::adjustLatitudeLongitudeAltitude`（TileCallback.cpp ~160）：extent‑Y 本身线性于墨卡托 Y（`inDegrees(extentY)=mercY/2`），adjust 里 `atan(sinh(mercY)) ≡ 2·atan(eᵐ)−π/2`（Gudermannian），所以每顶点纬度已是精确反投影。45°N 低 zoom（全欧洲）海岸线比例正确即证。**不需要改 computeTileExtent，重做是空操作。**
2. **极点星芒真因 = `convertLLAtoECEF`（Math.cpp:430）的 85.05° 硬截断**：最顶瓦片顶边 `atan(sinh(π))=85.0511°` 刚好略超阈值 → 整顶行 16 顶点 snap 到同一极点 → 扇形坍缩成星。
   - **v0.9 试过删截断**：星芒消失但极冠敞开成盘/空洞（裙边/海洋面填充、轮廓鼓出），用户认为比星状更差。
   - **v0.9.1 撤回，恢复截断**：保留星状极点（收敛点，轮廓正常）。<85° 区域三种状态都无回归（截断只在 >85.05° 触发）。
3. **遗留（已知限制）**：Web Mercator 无 >85.05° 瓦片数据，极冠无法贴图。当前取"星状收敛点"。真正干净做法 = 主动在 >85° 填平极冠（冰白 disc），属新增几何、风险高，留作后续单独立项。

**验证铁律**：极地/投影类改动必须看**低 zoom 中纬度陆地**（`--goto 45 12 6000` 全欧洲，海岸线比例）+ 极点正上方（`--goto 89.5 0 5000`）；别只看高 zoom（单瓦片跨度极小，gd 非线性可忽略，看不出问题）。

## 下一步建议（给新会话）
1. ✅ 回退已确认正常（全球/北京 40°/欧洲 45° 低 zoom/斯瓦尔巴 78° 全部无扭曲）。
2. ✅ 极点：v0.9 去截断的空洞已撤回，v0.9.1 恢复星状（用户偏好）。极冠 >85° 作为已知限制。
3. ✅ 已合并 master、打了 v0.8/v0.9 并推送。v0.9.1 = 撤回极地去截断的小修版（需重打包 .app + 推送 tag）。
4. 用户**单独立项、本轮未做**的需求：**真实感 3D 瓦片**（Google Photorealistic 3D Tiles / OGC 3D Tiles 网格，需 Google Cloud API key，另一条管线）——单独 brainstorm + 计划。

## 已完成的全部能力（截至 v0.8 + P0/P1/perf，均在 master；6-19/6-20 深瓦片线已回退）
- 在线 Google 混合影像(lyrs=y) + AWS Terrarium 高程，多级流式到街道级(z19)
- ImGui 面板：经纬度/海拔读数、太阳方位/高度、**真实时间太阳**、海洋、曝光、**大气强度**、跳转、书签、退出
- 瓦片磁盘缓存(`~/.osgverse_earth_cache`，env `EARTH_TILE_CACHE`)
- 相机最底点 50m（不穿地表）
- 滚轮缩放(macOS SCROLL_2D)、Go To 俯视、`--goto` 命令行
- 可双击 `dist/EarthExplorer.app`（`packaging/package_macos.sh` 打包，`packaging/run.sh` 命令行）

全部移植/实现细节见 `tasks/lessons.md`。
