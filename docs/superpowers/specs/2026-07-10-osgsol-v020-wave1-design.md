# osgSol v0.2.0 Wave 1 Runtime Correctness Design

日期：2026-07-10

状态：用户已批准方向，进入实施

基线：`v0.1.0` = `3c22d5cbb7b47f87f0088cc756b9d990731e7634`

工作分支：`codex/v0.2-runtime-safety`

## 1. 目标

`v0.2.0` 不增加新的产品功能，集中关闭 Earth 应用默认运行路径中会导致竞态、错误定位、旧数据丢失、错误影像、输入冻结和 AI 工具越权的 High/Medium 问题。

本阶段保持 `v0.1.0` 已验收行为：

- 控制面板鼠标滚轮必须可以向下和向上滚动；
- 每次照片生成必须使用该次请求自己的目标坐标，不继承上一张照片或当前 3D 图层；
- 正式桌面交付始终更新 `/Users/USER/Desktop/osgSol Earth.app`，不再创建时间戳应用；
- 只修改并同步 `anloren/osgsol`，不覆盖原 `osgverse` 仓库，不直接修改 `master`。

## 2. 分批边界

Wave 1 拆成六个可独立验收的批次，每批独立计划、测试和提交：

1. **线程边界**：LayerManager、动态 subtitle、视频 UI 状态和 Veo 瞬时错误。
2. **地理正确性**：卫星/航班拾取外推、国际日期变更线 bbox 拆分、精选卫星重试。
3. **数据可靠性**：抓取失败保留上一轮有效数据，空态与失败态保持语义分离。
4. **渲染与输入**：动态瓦片同步、FRAME 事件不受修饰键闸门影响、叠加层关闭与超缩放分离。
5. **AI Web 安全**：阻止 loopback/LAN/link-local/metadata URL，并把网页正文作为不可信数据定界。
6. **交付验收**：全量离线测试、离屏回归、真机清单、固定桌面应用更新、签名核验和 `v0.2.0` 标签。

批次之间只通过已有公开接口衔接，不在 Wave 1 做 `earth_main.cpp`、`ai_media.cpp` 巨型文件拆分，不引入 STAC/COG，不启用当前依赖缺失的 ONNX Runtime 或 Bullet。

## 3. 线程模型

### 3.1 LayerManager

ImGui draw traversal 不直接调用图层 `apply` 回调，也不持有 `_layers` 中元素的裸引用。UI 只做两件事：读取值语义快照、把开关/透明度/预设命令写入加锁队列。

主线程 FRAME handler 每帧按 FIFO 顺序 drain 命令并执行 `apply`。动态 subtitle 由主线程通过 `setSubtitle()` 写入，draw traversal 通过 `layersSnapshot()` 读取。回调在 LayerManager 锁外执行，避免回调重入形成死锁。

### 3.2 MediaManager

视频按钮和确认 Modal 位于 draw traversal；视频状态机的 owner 是调用 `MediaManager::update()` 的 FRAME 线程。UI 不再直接调用 `beginVideoCapture()`、`captureVideoEnd()`、`confirmVideo()`、`cancelVideo()`，改为写入 `VideoUiRequest` 队列。

FRAME 线程消费请求并发布不可变 `VideoUiSnapshot`。UI 只读快照。HUD 隐藏计数使用原子整数，因为 draw traversal 读取、FRAME 线程修改。

照片工具仍由 AI 主线程 drain 调用 `startPhotoJob()`；`PhotoRequest` 的显式目标坐标规则不变，并保留双请求独立性回归测试。

### 3.3 Veo 轮询

连接失败、HTTP 429 和 5xx 是瞬时结果：本轮 `done=false`，保留任务并等待下一轮。其它 4xx、成功响应中的 operation error、不可解析的 200 响应是终态失败。10 分钟总超时仍是最终兜底。

## 4. 其它批次的架构决策

- 卫星和航班拾取使用与渲染相同的速度外推公式，不能命中陈旧快照。
- 跨国际日期变更线的视口拆成两个合法 bbox；结果按稳定业务键去重。
- feed 刷新失败只更新失败状态和错误文案，不提交空 snapshot；成功且零要素才进入空态。
- 动态瓦片默认标记 `DYNAMIC`，保留 `EARTH_TILE_DYNAMIC=0` 逃生开关并做 A/B 帧时验证。
- `EarthManipulator` 必须先处理 FRAME，再对交互事件应用 handled/modifier 闸门。
- OVERLAY 的“有意关闭”和“超过原生最大缩放”使用不同状态，关闭必须移除旧纹理且不刷新最大细节角标。
- AI Web 工具只允许公网 HTTP(S) 目标；网页文本在系统提示和工具结果两层标记为 untrusted data。

## 5. 测试与提交纪律

- 每个行为变更先写失败测试并确认按预期失败，再写最小实现。
- 每批先跑目标测试，再跑 `ctest -L offline --output-on-failure`。
- 任何 EarthExplorer 运行验证必须带 `EARTH_OFFSCREEN=1`；同时只运行一个 `cmake --build`。
- 每批形成一段独立、连续、可审查的提交序列：每个可验证任务单独提交并独立复核，
  批次末尾再提交全量验证记录；Wave 1 全部完成前不合并 `master`、不打 `v0.2.0`。
- 最终打包拒绝 `EARTH_AI_KEY`，更新固定桌面应用后运行 smoke test，再执行 `codesign --verify --deep --strict`。

## 6. 完成标准

1. Wave 1 范围内的线程写入只有一个 owner，draw traversal 仅入队或读快照。
2. 日期变更线、位置外推、失败保数据、叠加层置空和 modifier FRAME 均有确定性自动化回归。
3. `v0.1.0` 的滚轮双向滚动和照片目标独立测试持续通过。
4. 全量离线测试、离屏回归、真机清单和正式应用签名全部通过。
5. 固定应用版本升为 `0.2.0`，验收后创建并推送 annotated tag `v0.2.0`。
