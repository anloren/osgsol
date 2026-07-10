# 标记系统整体重设计(vision 加强版)设计规格

日期:2026-07-06。状态:形状方向 + 联动范围已获用户批准(AskUserQuestion:"方向对按这个细化"、"图层目录+详情卡 chip 都联动")。待用户 review 本 spec 后转 plan。

## 0. 背景与目标

**现状(已核实,file:line 见下)**:地球上所有信息点标记视觉无法分辨——
- 9 个 feed 源(火点/地震/灾害/自然事件/新闻热点/GPS干扰/难民/飓风/战略数据)**共用同一个圆点着色器**(`feed_layer.cpp` feedVertCode/feedFragCode),只有颜色和大小不同。
- 航班(`flight_data.cpp`)和船(`ais_data.cpp`)**共用同一个箭头三角形着色器**,连形状都一样。
- 卫星(`sat_data.cpp`)又是圆点。
- 形状**写死在各自的片元着色器**,无统一形状库。
- 面板侧:详情卡有 `CardStyle.accentColor` + 文字胶囊 `chipLabel`(`ui_card.h:25-36`),但图层目录项(`LayerManager.h` `OverlayLayer`)**无 icon、无颜色字段**;各层代表色**散落**在每个源的配色函数(`frpStyle`/`levelStyle`/`depthColor`/`nhcColor`/`kLvColor`…)。

**目标**:一套整体标记系统——
1. **形状语言**:每类信息一枚可辨识徽章(形状负责"是什么")。
2. **颜色策略**:保留各层现有"按值渐变"配色(火点按 FRP、地震按深度——颜色深浅负责"有多强"),同时为每层集中定义一个"代表色"供面板用。
3. **面板联动**:同一枚徽章出现在三处——地球标记、图层目录每项 icon、详情卡表头 chip,同形同色,集中定义一处、三处同步。

**红线(用户明确,贯穿全程)**:**只动外观**。不碰数据抓取、后台线程、拾取逻辑(pickAt)、图层注册流程、订阅/闸门、缓存、near/far、前半球剔除(本会话刚加的,别动)。改动仅限:片元/顶点着色器里画形状的代码、逐点/逐层的形状 id 与颜色属性、ImGui 画 icon/chip 的代码、一处集中的视觉定义。

## 1. 形状语言(每类信息一枚徽章)

全部用现有 GL_POINTS 点精灵在片元着色器里画:`gl_PointCoord ∈ [0,1]²`,中心 `(0.5,0.5)`,局部坐标 `p = gl_PointCoord - 0.5`(范围约 [-0.5,0.5])。每个形状给出可实现的判定法(SDF/边距/极坐标),plan 阶段写完整 GLSL。

| 图层 | 形状 | FS 判定法(可实现性已核) | 随航向旋转 | 代表色 |
|---|---|---|---|---|
| 船 AIS | 长五边形·船体(尖艏平艉) | 5 条边半平面交集:艏尖(上)、两舷、两艉角;或 `max` 组合的凸多边形 SDF | 是(COG) | 青 `#35e0d0` |
| 航班 flight | 箭头(机头三角) | 已有(flightFragCode 三边距 min+smoothstep) | 是(heading) | 天蓝 `#4da8ff` |
| 火点 fire | 星芒/火焰(4 芒) | 极坐标 `r(θ)=0.5*(0.55+0.45*|cos(2θ)|)`,`length(p) < r(atan(p.y,p.x))` | 否 | 橙红 `#ff5a30` |
| 地震 quake | 同心波纹环 | 环带:`abs(length(p)-0.34) < 0.09` 画环 + 中心 `length(p)<0.14` 实心点 | 否 | 紫 `#b06bff` |
| 灾害 GDACS | 警告三角(朝上) | 三角形三边半平面交(朝上等边);感叹号可省(纯三角+颜色已足够区分) | 否 | 红 `#ff3b47` |
| 新闻热点 GDELT | 菱形 | `abs(p.x)+abs(p.y) < 0.42` | 否 | 琥珀 `#ffc23a` |
| GPS 干扰 | 六边形 | 6 条边半平面交(正六边形,`max` over 6 法向投影) | 否 | 品红 `#ff5ac8` |
| 自然事件 EONET | 水滴(上尖下圆) | 下半 `length(p-(0,-0.05))<0.4` 圆 ∪ 上半三角尖;简化可用"圆 + 顶点凸起" | 否 | 绿 `#4bd86b` |
| 卫星 satellite | 方框 + 天线十字 | 方框边框 `max(abs(p.x),abs(p.y))∈[0.28,0.4]` + 十字臂 `min(abs(p.x),abs(p.y))<0.06 && length<0.5` | 否 | 金 `#ffd23a` |
| 难民流 UNHCR | 弧线(已有几何)+ 端点符号沿用现状 | 弧走 buildArcGeometry(不动);代表点若有则用小圆环 | — | 蓝 `#3d7bff` |
| 飓风 NHC | 路径折线(已有)+ 点用台风眼环 | 路径走 buildPolylineGeometry(不动);点标记用"环 + 偏心中心点"近似台风眼(与地震环靠颜色区分) | 否 | 青灰 `#7fd0d8` |
| 战略数据 | 方块(基地/港口/核/光缆/数据中心以颜色细分) | 圆角方块 `max(abs(p.x),abs(p.y))<0.36` | 否 | 石板灰 `#9aa4b2`(子类各自色) |

说明:难画的火点星芒、水滴、台风眼在上表已给可行近似(极坐标/圆并三角/环+偏心点);plan 若某形状实现出来辨识度差,允许在 plan 内降级为更简单的可辨形状(如水滴→倒三角),但不改变"每类一形状"的原则。船/航班的形状随航向旋转(它们有 COG/heading);其余点无朝向,不旋转。

## 2. 颜色策略

- **保留按值渐变**:火点仍按 FRP(黄→橙→红)、地震按深度、灾害按预警等级、新闻按热度……这些是有信息量的,不丢。渐变作为徽章的**填充色**。
- **每层一个代表色**(上表末列):集中定义,用于面板 icon 与 chip、以及作为该层徽章的"基准色相"。当某层无按值渐变规则时(如船/航班),代表色即填充色。
- 徽章统一加**深色描边**(约 `#0c1418`,1px)保证任意底图上可读(现有 flight/feed FS 已有 `mix(rgb*0.7, rgb, edge)` 边缘处理,沿用同风格)。

## 3. 架构(实现方式,红线内)

### 3.1 集中视觉定义:`marker_style.h`(新文件)

```cpp
namespace earthmark {
    enum class MarkerShape {
        Circle = 0, Arrow, Ship, StarBurst, Ring, WarnTriangle,
        Diamond, Hexagon, Teardrop, SatBox, Square
    };
    struct MarkerVisual { MarkerShape shape; osg::Vec4 color; };   // color=代表色
    // 按图层 id 查代表徽章(图层目录 icon / 详情卡 chip 用);未登记的 id 回退 Circle+中性色。
    MarkerVisual visualForLayer(const std::string& layerId);
}
```
一处集中登记每个图层 id → (形状, 代表色)。标记着色器(经形状 id)、图层目录、详情卡、事件流都以此为准 → "改一处三处同步"。

### 3.2 共享 GLSL 形状库(字符串,各 FS 拼接)

osgVerse 着色器是字符串拼接。定义一个共享字符串 `kMarkerShapeGLSL`(放 marker_style.h 或新 `marker_shapes.glsl.h`),内含每个形状的判定函数 + 一个派发函数:
```glsl
// 返回覆盖率 alpha(0=在形状外 discard,>0=在内,边缘 smoothstep 抗锯齿)
float markerCoverage(int shapeId, vec2 pc, float headingRad);
```
各图层 FS 拼接此库,主体统一为:取 `pc = gl_PointCoord`、算 `cov = markerCoverage(shapeId, pc, headingRad)`、`if(cov<=0) discard`、`rgb = mix(color*0.7, color, cov)` 输出(与现有 MRT 双输出格式一致)。**形状定义只此一份**,消除现在每层各写 FS 的重复。

### 3.3 形状 id 如何传进着色器

- **feed 框架**(共享 shader,多源不同形状):`FeedSpec` 加字段 `earthmark::MarkerShape shape = Circle;`。`buildGeodeFrom` 建 geode 时,`texcoord0` 从现在的 `(sizePx, 0)` 改为 `(sizePx, shapeIdFloat)`——**texcoord0.y 在 feed 当前恒为 0、空闲**,正好当 shape id。VS 传成 varying,FS 传给 `markerCoverage`。feed 点无朝向,headingRad 传 0。
- **航班/船/卫星**(各自独立 shader):形状固定,shapeId 用常量(航班 Arrow、船 Ship、卫星 SatBox);船/航班的 `texcoord0.y` 继续是 headingRad(旋转用),shapeId 走常量不占通道。
- 聚合火点(FIRMS cluster)沿用 fire 形状(聚合点也是火点)。

### 3.4 ImGui 侧形状绘制(面板联动)

新增 `void drawMarkerIcon(ImDrawList*, MarkerShape, ImVec2 center, float size, ImU32 color);`——用 DrawList 的 `AddConvexPolyFilled`/`AddCircle`/`PathArcTo` 画与着色器**对应**的形状(五边形/箭头/星/环/三角/菱形/六边形/方框等)。ImGui 与 GLSL 两套画法读同一个 `MarkerShape` 枚举,视觉一致。

- **图层目录**(`EarthControlUI.h` 画图层列表处):每项 `displayName` 前用 `drawMarkerIcon` 画迷你徽章(代表色),`OverlayLayer` 加 `MarkerShape shape` + `osg::Vec4 iconColor` 两个**外观字段**(注册时从 `visualForLayer` 填;不改注册流程本身,只加字段赋值)。
- **详情卡 chip**(`ui_card.h` `drawChip`):从"纯色文字胶囊"改为"形状 icon + 文字",`CardStyle` 加 `MarkerShape shape` 字段(默认 Circle),`drawChip` 先 `drawMarkerIcon` 再画文字,颜色用 `accentColor`。各详情卡(flight/ship/satellite/feed 通用要素卡)设置对应 shape。
- **事件流卡/ticker**(可选,同一版顺带):`collectRecentEvents` 每行源标签前用 `drawMarkerIcon` 画该源徽章(读 `visualForLayer(sourceId)`)。若实现复杂度高可留下一版,不阻塞主目标。

## 4. 逐图层改动清单(仅外观)

| 文件 | 改动 |
|---|---|
| `marker_style.h`/`.cpp`(新) | MarkerShape 枚举、MarkerVisual、visualForLayer 登记表、kMarkerShapeGLSL 共享形状库、drawMarkerIcon(ImGui) |
| `feed_layer.h` | FeedSpec 加 `MarkerShape shape`;OverlayLayer 无关(在 LayerManager.h) |
| `feed_layer.cpp` | feedFragCode 改为拼接 kMarkerShapeGLSL + 按 texcoord0.y 的 shapeId 派发;buildGeodeFrom 写 shapeId 进 texcoord0.y |
| `feeds/*.cpp`(9 源) | 各 registerXxxFeed 里 `spec.shape = MarkerShape::Xxx`(一行);代表色登记进 visualForLayer |
| `flight_data.cpp` | flightFragCode 改用共享库(Arrow),或保留现箭头(已达标)——统一走库以便未来一致 |
| `ais_data.cpp` | 箭头 FS 改画 Ship 五边形(随 COG 旋转) |
| `sat_data.cpp` | 圆点 FS 改 SatBox(或经库统一) |
| `LayerManager.h` | OverlayLayer 加 `MarkerShape shape` + `osg::Vec4 iconColor`(外观字段) |
| `EarthControlUI.h` | 图层目录每项前 drawMarkerIcon;各详情卡设 CardStyle.shape |
| `ui_card.h` | CardStyle 加 shape 字段;drawChip 画 icon+文字 |

## 5. 明确不碰(红线清单)

数据抓取(fetchOnce/FetchThread/WS/订阅/闸门)、拾取(pickAt 及其数学)、图层注册流程(registerFeedLayer/configureXxxLayer 的接线逻辑)、聚合 LOD 机制、near/far、本会话新加的前半球剔除(VS 里 dot(Pv-Cv,Pv) 那段,保持不动,只在其后改颜色/形状输出)、缓存、key、线程模型。**本版只改"点长什么样"和"面板 icon 长什么样",不改"点从哪来、点选了怎样、图层怎么接"。**

## 6. 测试计划

1. **离屏逐层形状可辨**(EARTH_OFFSCREEN=1):用各层 fixture/env 钩子(EARTH_FIRES/EARTH_SHIPS/EARTH_FLIGHTS/EARTH_QUAKES…)开单层,截图目检徽章形状正确、可辨、随航向(船/航班)朝向正确、颜色按值渐变仍在。
2. **多层同开辨识度**:同一视角开火点+地震+灾害+船+航班,截图确认五种形状肉眼可区分(对比重设计前的"全圆点/全三角")。
3. **面板 icon**:离屏截图图层目录(需要 UI 可见的离屏帧)确认每项前徽章形状+色正确、与地球标记一致;详情卡 chip 形状正确。
4. **联动一致性**:抽查 2-3 层,确认地球徽章、目录 icon、卡 chip 三处同形同色(改 visualForLayer 一处,三处应同步)。
5. **回归**:5 个单测二进制 exit=0(形状是纯着色器/UI 改动,不影响解析/线程单测,但确认无编译破坏);全局+极点 classify_rb_swap CLEAN;前半球剔除不受影响(远视角转动不闪——本会话已修,确认没被形状改动破坏)。
6. **真机**:用户双击确认整套徽章观感 + 面板联动。

## 7. 范围外(本版不做)

- 标记动画(火点闪烁/船拖尾/脉冲)。
- 图例(legend)面板。
- 形状随缩放层级变化(除现有 FIRMS 聚合外)。
- 事件流 ticker 徽章若复杂可留下一版(§3.4 已标可选)。
- 颜色主题切换 / 用户自定义配色。
- 触碰任何非外观逻辑(见 §5)。
