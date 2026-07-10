# P3 动态流(一期):AIS 船舶层 + FIRMS 火点层 设计规格

日期:2026-07-06。作者:Claude(与用户共同确认)。状态:已获用户口头批准(A/B 两节均确认),范围与拆分方式经用户选定。

## 0. 背景与范围

「世界信息枢纽」路线图 P3 阶段(动态流)。原始范围是 AISStream 船舶 / FIRMS 火点 / ACLED 冲突三源;用户决定:**本期只做 AIS + FIRMS**(两者免费 key 自动发放,用户已答应注册后提供),**ACLED 顺延**(需注册审批 + OAuth 24h token 刷新,摩擦最大)。

用户已明确:设计两节(A=AIS,B=FIRMS)均已口头批准,执行期间不再逐步请示,自行推进;仅真机验证需要 key 时找用户。

### 前置调研结论(已核实)

- `feed_layer.h/cpp` 是 P0 抽出的配置驱动框架(FeedSpec 一份配置接入一个轮询 REST 源),P1 六源全部走它。FIRMS 属轮询 REST,应复用并扩展它;AIS 是 WS 长连接推送,范式不同,单独立模块(仿 `flight_data` 先例)。
- libhv 的 `hv::WebSocketClient`(`3rdparty/libhv/all/client/WebSocketClient.h`)**已编译进 `osgVerseDependency`**(3rdparty/CMakeLists.txt 240-241 行),`earth_explorer` 已链接——AIS 不需要新依赖。API:`open(url, headers)` / `close()` / `send(std::string)` / `onopen`/`onclose`/`onmessage` 回调 / `setPingInterval(ms)`。
- 仓库内**没有**任何点聚合/LOD 抽稀实现。Starlink ~7000 点全量渲染的先例说明 GL_POINTS 绘制吞吐不是瓶颈;FIRMS 的问题是局部高密度堆叠的**可读性与拾取歧义**,聚合是本期唯一需要新建的通用能力。
- FIRMS API:`https://firms.modaps.eosdis.nasa.gov/api/area/csv/<MAP_KEY>/<SOURCE>/world/<DAYS>`,CSV;MAP_KEY 免费注册,限额 5000 次/10 分钟(我们 30 分钟一拉,远碰不到)。
- AISStream API:`wss://stream.aisstream.io/v0/stream`,连接后发一条 JSON 订阅消息(APIKey + BoundingBoxes + 可选 FilterMessageTypes/FiltersShipMMSI);**bbox 是连接时一次性指定,更换须断开重连**。

## 1. A 节:AIS 船舶层(`ais_data.h/.cpp`,新模块)

### 1.1 架构

仿 `flight_data.cpp` 的"独立模块 + 抽象接口 + 后台线程 + 双缓冲快照 + 主线程 syncIfDirty"模式,但抓取侧从"定时轮询 REST"换成"WS 事件循环、消息驱动增量更新":

- 后台线程持有 `hv::WebSocketClient`,连 `wss://stream.aisstream.io/v0/stream`,`onopen` 里发订阅消息:`{"APIKey": <key>, "BoundingBoxes": [[[latSW,lonSW],[latNE,lonNE]]], "FilterMessageTypes": ["PositionReport"]}`(坐标序以 aisstream 官方文档为准,实现时用 fixture 双向验证)。
- `onmessage` 解析 `PositionReport`(MMSI/经纬度/COG 航向/SOG 航速/时间戳),写入 worker 侧 `std::map<MMSI, Ship>` 存量表,**不逐条投递主线程**;worker 每 ~1s 把整表快照 post 给主线程(复用卫星层验证过的 mutex+dirty 模式,吸取 61479fc5 竞态教训:done/status 标志只在主线程消费点置位)。
- 船舶过期:>10 分钟无更新的 MMSI 从存量表剔除(worker 侧做)。
- 存量表硬上限 5000 条(先到先得,防极端 bbox 下内存无界)。

### 1.2 视口跟随与重连策略

- 主线程每帧 `setViewBBox(...)`(与航班层同一手法、同一处调用点)。
- **高空闸门**:相机高度 > 3000 km(或 bbox 跨度 > 40°)时不订阅、断开已有连接,图层 subtitle 显示「推近后显示船舶」——全球 bbox 的 AIS 消息速率(数千条/秒)对桌面客户端没有意义,视口订阅天然限流。
- worker 侧对比"当前订阅 bbox"与"最新视口 bbox":视口中心移出订阅 bbox、或跨度变化 >2 倍时,断开重连换新订阅。订阅 bbox = 视口 bbox 按 1.5 倍外扩(减少平移触发的重连);重连冷却 ≥10 s(防抖)。
- 断线(网络/服务端)自动重连,指数退避封顶 60 s;连接失败/未配置 key 时 subtitle 显示原因文案(复用卫星层 `fetchErrorText` → subtitle 的既有通路手法)。

### 1.3 渲染与交互

- GL_POINTS 点精灵,按 COG 航向旋转的箭头(直接复用航班层 VS/FS 手法,独立 StateSet 实例——**红线:禁止跨实例共享 Program**,4ee4c74d 教训)。颜色按 SOG 航速分档(锚泊灰/慢速蓝/常速青/高速亮黄)。v1 不做逐帧外推(船速 ~10 m/s,可视距离下帧间位移可忽略;与航班层的差异点,注释写明缘由)。
- 点击拾取 + 右上角详情卡(MMSI/航速/航向/收到时间),复用 CardStack 模板与拾取手法。
- 抽象接口 `ShipLayer`(仿 `FlightLayer`):`setEnabled/isEnabled/setViewBBox/getSelected/clearSelected/summaryJson/statusText`。`summaryJson` 做真实实现(视口内船数、航速分桶)供 AI 工具 `get_ships_summary`。

### 1.4 配置与测试钩子

- `EARTH_AISSTREAM_KEY`:API key(未设置 → 图层可勾选但 subtitle 提示「未配置 key(EARTH_AISSTREAM_KEY)」,不联网)。
- `EARTH_SHIPS`:非 "0" 启动即开。
- `EARTH_SHIPS_FILE`:离线 fixture——一个 JSONL 文件(逐行 aisstream 消息原文),启动时经**同一条 onmessage 解析路径**灌入,不连网。这保证解析逻辑被 fixture 全覆盖。
- 图层目录注册进「动态流」组(group 名与 P1 各源一致的层级语义,实现时对齐现状)。

## 2. B 节:FIRMS 火点层(扩展 `feed_layer` + FeedSpec 接入)

### 2.1 数据源

- URL:`https://firms.modaps.eosdis.nasa.gov/api/area/csv/<KEY>/VIIRS_SNPP_NRT/world/1`(全球最近 24h,VIIRS S-NPP 近实时;VIIRS 选型与云图换 VIIRS 同理:无缝、分辨率高)。
- key 从 `EARTH_FIRMS_KEY` 读,注册时拼进 URL;未设置 → 注册时 url 置空(且不设 staticFile),框架现有语义下不起轮询线程,图层保留但 subtitle 固定为「未配置 key(EARTH_FIRMS_KEY)」。若实现时发现 registerFeedLayer 对"url 空且无 staticFile/fixture"路径有未定义行为,先补一个显式的早退分支再接入,不得依赖巧合。
- `refreshSeconds = 1800`(NRT 数据本身 ~3h 更新一次)。
- CSV 列:`latitude,longitude,bright_ti4,scan,track,acq_date,acq_time,satellite,instrument,confidence,version,bright_ti5,frp,daynight`。解析在 FeedSpec.parse 回调内(纯函数,单测直测)。
- 量级:全球日常 1 万~10 万点(季节波动)。

### 2.2 聚合/LOD(本期新建的通用能力,加在 feed_layer 框架层)

`FeedSpec` 新增可选 `cluster` 配置(不设 = 现有各源零改动):

```
struct FeedClusterLevel { double maxCameraAltKm; double cellDeg; };  // cellDeg<=0 表示原始点
struct FeedClusterSpec  { std::vector<FeedClusterLevel> levels; };   // 按 maxCameraAltKm 升序
```

FIRMS 用三级:`{300, 0(原始点)}, {1500, 0.25}, {+inf, 1.0}`。

- **分桶在抓取线程做**:parse 产出原始 FeedPoints 后,框架按每个非原始级别做经纬网格分桶,每桶合成一个聚合 FeedPoint:位置=桶内质心,sizePx/颜色随桶内计数与最大 FRP 增长,title=「火点聚合区」,detail=「该区域 N 个火点\n最大 FRP …\n最新观测 …」。全部级别一次算好,主线程不重算。
- **主线程 sync 时每级别建一个 Geode**,挂同一 feed 根下;每帧回调按当前相机高度切 nodeMask 只显一个级别。拾取对"当前可见级别"生效,聚合点与原始点同走现有 FeedPoint 拾取/详情卡路径,零新交互代码。
- 网格分桶纯函数(输入点集+cellDeg,输出聚合点集)放 feed_layer 内部但单独可链接,单测直测边界(跨 180° 经线、极区、空桶、单点桶)。

### 2.3 渲染

- 暖色标:按 FRP 值黄→橙→红(FRP 缺失/非数值按最低档黄)。夜间观测(daynight=N)不单独区分,v1 不做。
- 走 feed_layer 现有 GL_POINTS 路径,无着色器改动;聚合级切换只动 nodeMask。

### 2.4 配置与测试钩子

- `EARTH_FIRMS_KEY` / `EARTH_FIRES`(强制开)/ `EARTH_FIRES_FILE`(CSV fixture,走 fixtureEnv 既有机制)。
- AI 工具 `get_fires_summary` 走 FeedSpec.summaryJson(总数、FRP TOP 区域、按大洲/网格分桶——用默认实现或轻定制,实现取简)。

## 3. 红线(两层共同)

1. 不碰 globe 着色器、不碰 osgDB::Options/PagedLOD/DatabasePager。
2. StateSet/Program 按实例持有,禁止跨实例/全局共享(4ee4c74d)。
3. 后台线程与主线程只经 mutex+dirty 快照交接;一切 done/status 标志在主线程消费点置位(61479fc5)。
4. 所有自测/回归一律 `EARTH_OFFSCREEN=1`,绝不弹真实窗口。
5. UI 信息呈现:右上角、独立、可关闭(既有偏好)。

## 4. 测试计划

1. **单测**(进既有测试二进制或新建,exit=0 为过):AIS 消息 JSON 解析(fixture 样本)、bbox 重连决策纯函数(中心移出/跨度突变/冷却期)、FIRMS CSV 解析(含畸形行)、网格聚合纯函数(跨 180°/极区/空输入/FRP 缺失)。
2. **离线 E2E**:`EARTH_OFFSCREEN=1 EARTH_SHIPS=1 EARTH_SHIPS_FILE=<jsonl>` 与 `EARTH_FIRES=1 EARTH_FIRES_FILE=<csv>` 截图,目检船舶箭头/火点暖色点上屏;FIRMS 用构造的密集 fixture 验证三级聚合切换(不同 `--goto` 高度各截一张)。
3. **回归**:既有全部单测二进制 exit=0;全局视角 + 极点 `classify_rb_swap.py` CLEAN(必须配合目检,该脚本有已知极区假阳性)。
4. **真机**(需用户 key):AISStream 真连接(视口跟随重连、断线重连、消息速率下的帧率)、FIRMS 真拉取(全球量级下三级聚合观感、拾取);真机验证前重打包+codesign -v。

## 5. 明确不做(v1 范围外)

- ACLED(顺延下一期)。
- 船舶静态信息(船名/船型需订阅 ShipStaticData 消息类型,v1 只有 PositionReport 的 MMSI/航速/航向)。
- 船舶逐帧外推、轨迹尾迹。
- FIRMS 历史日期查询(只拉最近 24h)、按置信度过滤 UI。
- 聚合能力对 P1 既有源的回填启用(能力做在框架层,但本期只有 FIRMS 用)。
