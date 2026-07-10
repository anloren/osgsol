# 叠加层 LOD 丝滑化 — 设计文档

日期：2026-07-07 · 范围：EarthExplorer 瓦片引擎 OVERLAY 槽的加载/LOD 体验优化

## 1. 问题

用户反馈(以 GEBCO 海底地形层为代表,但**所有科学/影像叠加层同病**):

- **①异步加载窗口期漏地面**:叠加层瓦片异步下载的那几十~几百毫秒里,先显示透明,露出底图/地面。
- **②切 LOD 时闪烁**:相机移动/缩放跨 LOD 级时,新一级瓦片未到 → 先漏地面 → 真图到货 → "啪"盖上。视觉上一会漏一会盖。
- **③超过原生最大缩放后叠加层消失**:每个叠加层有原生最大 zoom(GEBCO z8≈600m/px 等),超过后瓦片路径返回 `""` → 纹理被移除 → 永久漏出地面。

### 受益面:一处修复,5 层共用
"会闪"的图层只有一个代码路径——异步加载(`ImageRequestHandler`)只用于 **OVERLAY 槽(texUnit 3)**(`readerwriter/TileCallback.cpp:588`)。该槽由 5 层**互斥共用**(`kOverlaySlotIds`,`applications/earth_explorer/earth_main.cpp:950`):

| 图层 | 原生最大 zoom(超之则消失) |
|---|---|
| clouds(GIBS 影像/云图) | 9 |
| precip(降水雷达 RainViewer) | 10 |
| ndvi(植被指数) | 9 |
| nightlights(夜间灯光) | 8 |
| gebco(海底地形) | 8 |

改一段 OVERLAY 处理代码 → 5 层同时受益。其余图层无需改:底图(texUnit 0)/高程(ELEVATION)同步加载且已有父级顶替(故不闪);点/矢量层(地震/航班/船/火点/卫星/标记)是点精灵几何,不走此纹理路径。

## 2. 根因

底图(ORTHOPHOTO)之所以不闪:同步加载失败时走 `findAndUseParentData`(`TileCallback.cpp:523`),用最近祖先瓦片纹理 + UV 子区间(`_uvRangesToSet`)顶替。但 OVERLAY:

- **异步分支**:`createLayerImage(..., irh)` 返回一个**非空** tex2D(挂 1×1 透明占位 + 异步请求)。因 `tex` 非空,`updateLayerData:595` 的 `if (!tex && !emptyPath)` 父级顶替**被跳过** → 加载中显示透明 → 漏地面(症①②)。
- **emptyPath(超缩放)分支**:`updateLayerData:629` 直接 `removeTextureAttribute` 移除 texUnit 3 → 漏地面(症③)。

即:OVERLAY 唯独缺了底图早已有的"父级顶替"。

## 3. 设计

### 核心思路(一个机制统解三症)
叠加层任何时候"自己的瓦片没就绪"(加载中 或 超缩放永不存在),就**用最近祖先瓦片的叠加纹理顶替**——复用现成 `findAndUseParentData` + `_uvRangesToSet`,和底图一致。叠加层由此**永不消失、永不漏地面**,慢加载被"由粗到细渐清晰"掩盖。产品决策:超缩放时**拉伸顶住(逐渐糊)+ 右上角角标提示**(用户已选)。

### 组件 1 — 引擎侧(`readerwriter/TileCallback.cpp`,不碰 GLSL)

**A. 异步加载窗口期顶替(症①②)**
`updateLayerData` OVERLAY 分支:创建异步 tex2D 并交给 `irh->requestImageFile` 后(ImagePager 会在图像到货时对该 tex2D `setImage`,不论是否已绑定),**先把父级叠加纹理绑到 texUnit 3**(经 `findAndUseParentData` 设好父级 UV 子区间),并在本 `TileCallback` 实例记下待就绪的 tex2D(新成员 `osg::ref_ptr<osg::Texture2D> _overlayPending`)。
- 在 `operator()` 里(已逐帧逐瓦片执行)检测:若 `_overlayPending` 有效且其 `getImage()` 尺寸已从 1×1 变为真实(>1)→ 把 texUnit 3 换成本瓦片纹理、`UvOffset4` 复位为 `(0,0,1,1)`、清空 `_overlayPending`。一次性无缝切换,由粗到细。
- 找不到任何已加载祖先时,退回现状(挂透明占位)——只会在极早期根瓦片瞬间出现。

**B. 超缩放拉伸顶住(症③)**
`updateLayerData` 的 `emptyPath && texUnit==OVERLAY` 分支:不再 `removeTextureAttribute`,改为 `findAndUseParentData` 绑最深祖先叠加纹理 + UV 子区间拉伸顶住(永久状态,child 永不存在,无需换)。同时通知引擎"本帧激活叠加层走了超缩放拉伸"(见组件 2 检测)。

**不变量**:texUnit 3 在"叠加层已激活"时永远绑着某张纹理(本瓦片 或 祖先),绝不空绑 → 杜绝"漏地面"。

### 组件 2 — 提示角标(app/UI 侧,右上角)

当前激活叠加层正在"超原生分辨率拉伸"显示时,右上角淡入一个**可关闭小 chip**,如 `🌊 GEBCO · 已达最大细节 ~600m`。遵循既有偏好(信息面板一律右上角、独立、可关闭)。
- **检测(引擎侧信号)**:`TileManager` 维护一个标志,组件 1-B 的超缩放拉伸路径运行时置位;app 每帧读取以决定角标显隐。为避免角标本身闪烁(某些帧有超缩放瓦片更新、某些帧没有),读取端加**短去抖**(约 0.5s 内检测到即保持显示)。标志的精确置位/复位点、去抖窗口在实现 plan 中定死。
- 文案数据:每个叠加层的显示名 + 原生分辨率串(如 GEBCO "~600m")。原生 max-zoom 目前硬编码在 `createCustomPath` 的 `z>N` 截断里;角标所需的"层→原生分辨率文案"作为图层定义上的一个数据字段补充(与截断值保持一致,实现时二者取一处为准,避免重复)。

## 4. 不做(YAGNI,明确排除)

- **真·瓦片预取/下载提速**:本方案靠父级顶替让加载"感觉"瞬时(粗瓦片立即出图),不改实际下载管线。真预取(视口预测、并行预拉)是另一个更大的独立优化,留作后续。
- 不动 globe GLSL 着色器(项目铁律)。
- 不改点/矢量层、底图、高程的加载逻辑。

## 5. 验证

- **症①②(加载/切 LOD 不闪)**:反经线及常规视角,开 GEBCO,平移/缩放跨多个 LOD 级,离屏连拍——应始终"由粗到细渐清晰",无"漏地面→啪盖上"的闪。对 clouds/ndvi/nightlights/precip 各抽一层同验(共用路径)。
- **症③(超缩放不漏地面 + 角标)**:开 GEBCO 俯冲到 z>8(城市级),叠加层应拉伸顶住不消失;右上角出现"已达最大细节"角标;可关闭。
- **无回归**:底图/高程/点层在各视角正常;overlay 互斥切换正常;GIBS 云图无回归。
- **单测**:`osgVerse_Test_Feeds` 全绿;若引擎侧新增纯函数(如 UV 子区间计算/去抖),补单测。
- 所有跑 app 带 `EARTH_OFFSCREEN=1`;app 在 `build/sdk_core/bin`。

## 6. 约束

- 不碰 globe GLSL 着色器。
- 引擎改动限于 `readerwriter/TileCallback.cpp`(+ `TileManager` 一个标志的 header/impl);UI 改动限于 app 侧角标。
- 复用现成 `findAndUseParentData` / `_uvRangesToSet` / `operator()` 逐帧钩子,不新造 LOD 机制。
- OVERLAY 5 层互斥,角标只需考虑单一激活层。
