# 拍照视角、香港 3D Tiles 与地形连续性修复设计

**日期：** 2026-07-11
**状态：** 已确认方向，待规格复核后进入实施计划
**分支：** `codex/v0.2-runtime-safety`

## 目标

一次交付解决三个相关但边界独立的问题：

1. 拍照时先正常飞到目标；快门瞬间严格使用用户屏幕上已经看见的相机视角，不再被照片工具二次改成顶视角，同时不能复发“上一张香港照片/地点被下一张复用”。
2. 香港地政总署 F2 3D Tiles 改为可见区域优先的渐进加载，随视角和分辨率及时细化，改变高度时不反复卸载、重下或长期停留在粗纹理。
3. 减轻近地地形的豆腐块/平面块感，同时保持既有香港高度校正、父子瓦片连续、裙边防裂缝、z15 祖先高程采样，不复发空洞、比例不一致和扭曲底图。

## 非目标

- 不引入 Cesium Native，不替换 osgVerse 的场景图、材质或网络栈。
- 不改变香港 F2 数据源、署名、ECEF 变换、专用 3D Tiles 着色器或纹理内容。
- 不增加新的高程数据源，不改变全局 `TileElevationScale=2.0`、`TileSkirtRatio=0.05`、AWS Terrarium z15 上限。
- 不恢复会产生硬边界的香港 DEM 路径裁剪或预烘焙平地瓦片。
- 不在本轮实现 b3dm 原始字节磁盘缓存；先解决错误的全港预读和同会话 LOD 抖动。
- 不打 tag、不合并 master、不创建 PR；继续更新固定桌面 `/Users/USER/Desktop/osgSol Earth.app`。

## 调研证据

### 拍照链路

- `generate_photo` 为防止沿用上一次目标，在工具执行入口调用 `stopAnimation()` 和 `setByEye(request.lla...)`。
- `EarthManipulator::setByEye()` 明确把 `_tilt` 清零并构造正下方俯视，因此无论用户快门前看到什么角度，工具都会重置成顶视角。
- 当前图片隔离已经有独立 job id、快照路径和输出路径；缺少的是“目标状态”和“相机状态”分离，而不是必须重设相机。

### 香港 F2 冷缓存链路

- 官方根 `tileset.json` 当前包含 17 个外部子 tileset，根 `refine=ADD` 且自身无 content。
- 真实端点逐个请求 17 个顶层 JSON，单个耗时约 1.4–2.5 秒。
- 当前插件遇到“无 content + children”会同步递归 `readNodeFile()`；应用层虽然放到一个后台线程，仍在该单线程里继续读取外部 tileset、粗级 b3dm 和 KTX2。
- 真实冷缓存离屏运行 39.29 秒后仍未出现 `[Tiles3D] loaded`；日志已顺序解码多张 b3dm/KTX2，但整棵树尚未返回，图层没有挂入场景。
- EarthExplorer 已给 DatabasePager 配置 20 个文件线程和 16 个 HTTP 线程；问题不是线程数量太少，而是根加载没有把可见子树工作交给 pager。

### LOD/SSE

- 3D Tiles 规范要求客户端把 `geometricError` 与相机距离、屏幕尺寸和分辨率结合，得到以像素为单位的 SSE；SSE 超过阈值才细化。
- 当前插件默认使用固定 `height=1080` 和固定投影分母换算距离范围；窗口分辨率、投影和斜视情况下均不会更新。
- 插件已有 `PIXEL_SIZE_ON_SCREEN` 支持，但香港层未启用；默认 `MaxScreenSpaceError=16` 只可通过环境变量覆盖。
- OSG PagedLOD 默认允许精细子树立即过期；全局 PagedLOD 数量超过目标后，改变高度或视区可能造成精细内容卸载并重新下载。b3dm 因历史序列化崩溃被禁止写入普通 FileCache，所以这种抖动尤其昂贵。

### 地形连续性

- 当前每块地形固定 `16×16` 顶点，即每边 15 段；15 不是二次幂，父子四叉树网格的采样点不构成嵌套序列。
- z15 附近一块瓦片在香港约为公里量级，15 段会明显降采样 256×256 Terrarium 高程；`TileElevationScale=2.0` 会进一步放大平面块观感。
- 历史空洞由“香港 bbox 内直接返回空高程路径”造成：平坦子瓦片与真实父/邻瓦片出现百米级落差。
- 当前最终版使用连续 `hkElevationFilter`，并在 z>15 取同一 z15 祖先纹理的确定性 UV 子区；该组合以及裙边必须保持。

## 方案选择

### 方案 A：只调 SSE 和线程数

优点是改动小；缺点是仍会全港串行预读，SSE 越低反而一次请求更多无关内容，不能解决根因。否决。

### 方案 B：可见区域优先的现有 OSG 渐进管线

保留现有引擎，把无内容容器改成有包围体的延迟外部引用；启用真实屏幕像素 LOD；保留粗级内容并给精细子树短期驻留；地形改为嵌套网格。能直接消除已测得的串行根因，同时把回归面限制在现有组件。采用。

### 方案 C：迁移到 Cesium Native/完整重写选择器

能提供完整 foveated/dynamic SSE 和资源缓存，但会同时更换坐标、材质、线程、缓存和生命周期体系，当前回归风险远超收益。否决。

## 设计一：拍照相机状态机

### 行为语义

1. 用户要求拍摄当前视角：不触发飞行，快门使用当前可见相机矩阵。
2. 用户要求拍摄另一个地点：AI 先调用现有 `fly_to` 完成地点切换；本批不改变 `fly_to` 的路径、速度或落点姿态。目标画面已经显示并至少完成一个新画面后，才允许 `generate_photo`。
3. `generate_photo` 只锁定当前相机状态并开始抓帧，绝不调用 `setByEye()`、`moveTo()`、`stopAnimation()` 或修改 tilt/distance/center。
4. 快门画面必须等于同一帧屏幕上可见的地球构图；HUD 仍按既有逻辑仅在抓帧时隐藏，不改变主场景相机。
5. 若飞行动画仍在运行，工具返回可恢复状态，要求等待，不偷偷瞬移，也不使用上一张任务状态。工具不按照片目标坐标推断或校正相机，因为 ISS 斜视等构图中，拍摄位置与地面注视点本来就不相同。

### 独立图片隔离

每个照片请求建立不可变 `PhotoCaptureRequest`：

```cpp
struct PhotoCaptureRequest
{
    osg::Vec3d targetLla;
    osg::Matrixd visibleCameraMatrix;
    std::string style;
    bool showCameraPlatform = false;
    long long requestId = 0;
};
```

- `targetLla` 只用于本次提示词中的地点事实，不参与相机移动、姿态重建或上一任务状态查找。
- `visibleCameraMatrix` 在实际预约抓帧的 FRAME owner 线程从主相机一次性快照，作为快门构图的权威状态。
- `requestId` 继续派生独立的 `snap_<epoch>_<job>.png` 和 `gen_<epoch>_<job>.png`。
- MediaManager 的 pending 状态持有请求值副本；不得从上一 job、上一卡片、旧截图或全局“最近地点”反查。

### 飞行与快门门控

- 直接复用 EarthManipulator 已有的只读接口 `bool isAnimationRunning() const`；不新增相机写入口。
- `generate_photo` 在动画运行时返回 `camera_flight_in_progress`；AI 系统提示明确要求等待下一轮再调。
- 动画结束后沿用 `WAITING_VIEW_RENDER`，至少让目标视角完成一帧可见渲染，再预约 SnapshotGrabber。
- 工具开始时不提前冻结旧矩阵；在真正预约抓帧的 FRAME owner 线程读取最新主相机矩阵并写入请求。若用户在等待期间主动拖动，以快门前最后可见矩阵为准，不能回退到旧矩阵。

## 设计二：香港 3D Tiles 可见区域优先加载

### 延迟外部 tileset

只对“当前 tile 没有 content、只有 children”的容器启用延迟外部 JSON：

- 每个外部 JSON child 先构造 `osg::ProxyNode`。
- Proxy 使用该 child 的 `boundingVolume` 作为 USER_DEFINED center/radius。
- Proxy 设置 `DEFER_LOADING_TO_DATABASE_PAGER`、数据库路径和克隆后的 Options。
- 根 JSON 解析只建立轻量代理树，不读取 17 个子树；返回后立即挂入 `Tiles3DLayer`。
- Cull traversal 只会访问视锥内 Proxy；可见外部子树由现有 16 个 HTTP pager 线程并行读取。

该延迟规则不得用于“有粗级 content 的 REPLACE tile 的 refined children”。这些 group 仍在 pager 请求内部构造成完整可用子树后一次合入，避免父级刚退场、代理内容尚未到货时产生空洞。

### 粗级保底与驻留

- 每个 content-bearing PagedLOD 的粗级 child 设为不可过期的第一个 child。
- refined child 设置最短驻留时间 30 秒；环境变量仅作为诊断覆盖，不在 UI 暴露。
- 当 refined child 尚未完整合入时继续遍历粗级 child；REPLACE 只在 refined group 已可渲染时发生。
- 图层关闭仍用 NodeMask 停止 traversal/request；重新开启复用仍在内存中的已加载节点。

### 屏幕像素 SSE

- 香港层默认设置 `UsePixelsOnScreen=1`。
- 默认 `MaxScreenSpaceError=8`；`EARTH_3DTILES_SSE` 可覆盖，合法范围钳制到 `[2, 32]`。
- PagedLOD 的 switch pixel 由 bound radius、geometric error 和 SSE 得到，实际选择交给 OSG 当前 viewport/projection 的 pixel-size 计算。
- 保留 `EARTH_3DTILES_SSE` 诊断能力，但不把“更低一定更快”作为结论；更低只代表更清晰、请求更多。
- 不改变 tile transform、ECEF 落位、`gltfUpAxis` 或 F2 专用 shader。

### 失败与可观测性

- 根 JSON 成功解析后立即记录 `root_attached_ms`、lazy proxy 数量。
- pager 合入外部 JSON/b3dm 时，诊断模式记录 URL 类型、LOD 深度和等待时间；默认日志保持低噪声。
- 单个可见子树失败时保留粗级内容并允许下一次 traversal 重试，不把整层标为失败。
- 关闭图层后不得继续产生新的 F2 请求；已在网络中的请求允许自然结束，但结果只保存在节点/缓存中，不强制重新开启图层。

## 设计三：地形豆腐块与连续性

### 嵌套网格

按层级选择正方形网格：

```cpp
inline unsigned int terrainGridSizeForLevel(int z)
{
    return z >= 12 ? 33u : 17u;
}
```

- 17 和 33 分别为 `2^4+1`、`2^5+1`，采样段数为 16/32，可与四叉树二分关系对齐。
- 近地 z12+ 使用 33×33，显著减少 16×16 对高程的块状降采样。
- 远景使用 17×17，避免全球同时提高到 33×33 带来的无谓内存和三角形开销。
- 创建几何时把 rows/columns 写进 Geometry user value；更新高程和裙边不得继续依赖全局常量。

### 高度与接缝不变量

以下行为必须原样保持并转成回归测试：

- `TileElevationScale=2.0`。
- `TileSkirtRatio=0.05`。
- Terrarium 解码公式与 GL_FLOAT 米制数据。
- z>15 始终读取 z15 祖先 `(x >> dz, y >> dz)`。
- `elevScaleBias=(subX/subN, subY/subN, 1/subN, 1/subN)`。
- 香港核心平地、都会山体 `0.5*h-2m`、外部原高度以及两段 smoothstep 羽化。
- 同层相邻瓦片公共边使用同一 UV 端点；z16–19 相邻子区公共边从同一 z15 图像取同一采样坐标。
- 裙边只向局部法线下方延伸，不改变顶面高度，也不在透明场景中泄漏。

### 不触碰的老修复

- 不恢复香港 bbox 返回空 elevation path。
- 不恢复 6m 预烘瓦片。
- 不修改 globe GLSL、晨昏线、海洋、正射底图 URL 或 F2 shader。
- 不把香港局部高程比例改成与全局不同的 `TileElevationScale`；局部校正仍只在连续 filter 内进行。

## 测试设计

### 拍照 RED/GREEN

- 构造倾斜 EarthManipulator，相机已位于 NVIDIA 总部；执行照片工具不得写 center、tilt 或 matrix；SnapshotGrabber 预约时锁定当时主相机的完整 matrix。
- 动画运行时照片工具返回 `camera_flight_in_progress`，不得创建 job 或截图。
- 动画结束并完成一帧后开始截图，锁定矩阵等于该快门帧的主相机可见矩阵。
- 连续香港→NVIDIA 两次请求的 target、matrix、snap/gen path 和 job id 均不同；第二次提示词和快照不可含第一任务状态。

### 3D Tiles RED/GREEN

- 根 fixture 含 17 个 external JSON children；解析根时 fake reader 的 child network read count 必须为 0，返回 17 个带独立 bound 的 deferred Proxy。
- Cull 指向一个 proxy 时只请求对应 external JSON；相邻可见 proxy 可由 pager 并行请求。
- 有粗 content 的 REPLACE fixture：refined group 未完整可用时粗 content 仍存在；完成后才替换。
- PagedLOD 使用 `PIXEL_SIZE_ON_SCREEN`，SSE=8；不同 viewport/projection 下以实际像素大小触发细化。
- 粗 child 不过期；refined child 30 秒内不因短暂改变高度被移除。
- 真实 F2 冷缓存：根 attached 时间从当前超过 39 秒降到目标 5 秒内；首个可见粗级目标 12 秒内。在公网明显异常时报告原始时间，不伪造百分比。

### 地形 RED/GREEN

- `terrainGridSizeForLevel(11)==17`、`terrainGridSizeForLevel(12)==33`，且 `(size-1)` 为二次幂。
- 合成线性 z15 高程图：z16/z17 相邻子瓦片公共边的高度和 ECEF 坐标在容差内一致。
- 创建后更新高程与生成裙边均使用 Geometry 记录的动态 rows/columns，顶点/索引不越界。
- 香港 filter 数值：核心输出 0、都会区输出 `0.5*h-2`、包络外不变、羽化边界连续。
- 源码/行为守门确认 `TileElevationScale=2.0`、z15 ancestor 和 skirt ratio 未改变。

### 集成与视觉回归

顺序执行：

1. 聚焦单测与 3D Tiles fixture。
2. 完整 offline CTest。
3. 全球高空：大气、海洋、晨昏线正常。
4. 北极：闭合与纹理不扭曲。
5. 昆明低空倾斜：不穿模、不露地壳背面。
6. 香港地面、F2 关闭：无豆腐块、无亮色假土包、山海比例正常。
7. 香港 F2 开启：粗级快速出现，静止后细化，改变高度后不长期退回粗纹理；无空洞、无地图拉伸。
8. NVIDIA 总部：飞行后手动保持倾斜视角拍照，快门画面与屏幕构图一致。
9. 香港照片后再拍 NVIDIA：确认没有香港旧图、坐标或平台元素复用。

## 性能与安全边界

- 延迟代理减少无关网络和解码，不增加全局 pager 线程数。
- 33×33 只用于 z12+；远景维持 17×17，避免所有全球瓦片四倍增长。
- 30 秒 refined 驻留是同会话防抖，不是无限缓存；仍受全局 pager 生命周期管理。
- 所有网络 URI 继续走现有 `verse_web`、gzip 上限、格式 reader 和 HTTPS 处理。
- 不记录 API key、完整 query secret 或用户照片内容到新日志。

## 交付

- 每个子问题独立 RED→GREEN 提交并复审。
- 最终源码分支推送 `codex/v0.2-runtime-safety`。
- 更新固定 `/Users/USER/Desktop/osgSol Earth.app`，版本仍为 `0.2.0`、channel 为 `manual-test`，记录 source commit。
- 不生成时间戳 App，不覆盖旧仓库，不打 tag、不合并、不建 PR。

## 参考

- 3D Tiles 规范：<https://github.com/CesiumGS/3d-tiles/blob/main/specification/README.adoc>
- OGC 3D Tiles 1.1：<https://docs.ogc.org/cs/22-025r4/22-025r4.pdf>
- 香港地政总署 3D Visualisation Map 数据集：<https://data.gov.hk/en-data/dataset/hk-landsd-openmap-3d-visualisation-map-tile-based-models/resource/17166706-0627-4db0-ad59-fc680fe63658>
