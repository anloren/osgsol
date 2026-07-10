# EarthExplorer 现阶段 Review + 优化 Backlog + 下一步计划

日期:2026-07-08(v0.21 后)· 方法:10 路多维审计 → 48 条 finding 逐条对抗性核实 → opus 首席工程师综合(66 agent / ~5.2M token)

## 1. 总体健康度

工程状态:**能力面成熟、平台底盘扎实,但并发正确性欠了一笔集中债**。FeedLayer 已是配置驱动的成品平台(11 源、`registerFeedLayer` 一站式、summaryJson + AI 工具自动注册),AI 工具族、科学层、叠加层 LOD 丝滑化都真实落地并有真机验收,产品完成度高于绝大多数同类原型。

真正的短板是**多线程一致性**:项目自己在 `feed_layer.cpp` 里立了正确标准(跨线程标志全 `std::atomic`、POST_DRAW 共享态全上 `_mutex`,并写了注释解释 DrawThreadPerContext 下"帧 N draw 与帧 N+1 update 重叠"),但这套标准**只在 FeedLayer 一处贯彻**;flight/sat/ais/precip 四条更早的手写管线,以及 AI 卡片面板(`AICardPanel::_cards`),都还停在旧写法——其中 `_cards` 是一个真实的 **use-after-free/堆破坏**(唯一 high),`ai_cards.cpp:14-16` 的"同一线程无需加锁"注释对当前线程模型是**事实性错误**。

第二笔债是**测试盲区错配**:最该被守的东西没被守——v0.21 叠加层 LOD 的 swap/顶替/复位(终审刚抓过两个接缝 bug)零单测,而 `testPerSpecLiftMeters` 是同义反复的假覆盖。

第三是**失败静默**:实时层网络失败对用户和 AI 都基本无反馈(复选框保持勾选、地球空白、AI 永远回"抓取中")。

这些都不是"能力不够",而是"已有能力没焊牢"。**结论:先花 2-3 天把并发/崩溃债清成一个安全基线,再谈下一个大方向,ROI 远高于直接堆第 12 个数据源。**

## 2. 优化 Backlog(按 影响÷成本 排序)

> 48 条原始 finding 去重合并为 19 个可直接开工的任务。严重度为对抗性复核后的校准值。

| 优先级 | 问题 | 位置 | 类别 | 严重度 | 工作量 | 修法要点 |
|---|---|---|---|---|---|---|
| **P0** | `AICardPanel::_cards` 主线程 mutator 与 POST_DRAW 绘制线程无锁并发,`registerCards` erase-remove + `drawBody` 捕获 `&c` → UAF/堆破坏/崩溃 | `ai_cards.cpp:14-16/66/95/103` | concurrency | **high** | M | mutator 全加同一 `std::mutex`;`drawBody` 按值捕获 AICard 字段而非 `&c`。照抄 `feed_layer.cpp:250` |
| **P0** | flight/sat 每帧改顶点但 Drawable 从不 `setDataVariance(DYNAMIC)` → draw/update 无屏障数据竞争、顶点撕裂 | `flight_data.cpp:139/322`、`sat_data.cpp:308/597` | concurrency | medium | **S** | geom+Vec3Array `setDataVariance(DYNAMIC)` |
| **P0** | 跨线程普通 `bool`/`int` 一批,违反本仓自己的 atomic 约定(UB) | `_done`/`_enabled`/`_refreshNow` 遍布 flight/sat/ais/precip;`TileManager::_lastOverlayStretchFrame` `TileCallback.h:198` | concurrency | low(UB) | **S** | 统一 `std::atomic`,一次性批量提交 |
| **P0** | `ai_query` 单 worker `_fetch` 无 try/catch(异常→`std::terminate` 整进程 abort)+ inflight 永不清 → 卡死/崩溃 | `ai_query.cpp:26` | robustness | low(后果重) | **S** | `_fetch` 包 try/catch;post-fetch 块**始终**清 inflight |
| **P0** | `check()` layer key 缺失且路径空时 else-if 解引用 `paths.end()`(UB,per-frame) | `TileCallback.cpp:779` | robustness | low(latent) | **S** | else-if 加 `it2!=paths.end() &&` 守卫(一行) |
| **P1** | `pickAt` 用陈旧快照 `_flights[i].ecef` 命中测试,标记却被外推(~3km)→ 低空点击快速目标静默不中 | `flight_data.cpp:218`、`sat_data.cpp:483` | correctness | medium | M | pickAt 用同一外推公式再投影 |
| **P1** | 实时叠加层拉取失败对用户完全静默(无 loading/lastError,只 `std::cout`) | `EarthControlUI.h:272`、`flight_data.cpp:79`、`precip_data.cpp:62`、`feed_layer.cpp:202` | robustness/ux | medium | M | `OverlayLayer` 加 loading/lastError,图层行内联转轮/红"!"+tooltip |
| **P1** | fetch 状态未三态化:summary 工具永远对 AI 报"抓取中",真失败无限打转;feedHealth 静态源 fixture 打不开也算绿 | `feed_layer.cpp:866/928`、`ai_setup.cpp:33` | robustness | low | M | `_lastFetchOk` 升三态 NEVER/OK/FAILED。**是下一步方向 A 的前置** |
| **P1** | `testPerSpecLiftMeters` 断言 `f(x)==f(x)` 同义反复(P3 T3 高原高程修复回退仍全绿) | `feed_layer_tests.cpp:216` | test | medium | **S** | 真驱动 `fetchOnce(liftMeters=9000)`,断言 ecef 与 3000 差 >1000m |
| **P2** | v0.21 叠加层 LOD swap/顶替/超缩放/入口复位零单测——终审刚抓的两个接缝 bug 无回归守卫 | `TileCallback.cpp:581/604-632/697-715/737-741` | test | medium | L | 2 级 PLOD stand-in + StubImageRequestHandler,三场景断言 |
| **P2** | 卫星失败文案承诺"关闭再打开重试",但精密组无重取路径,只 Starlink 兑现 | `sat_data.cpp:449` | ux | low | **S** | 精密组加 `_preciseRefetchRequested`,或拆文案去掉假承诺 |
| **P2** | marker_style 单一真源被绕过:strategic 5 色 vs 目录/chip 灰;三张详情卡硬编码 shape+color | `marker_style.cpp:31`、`EarthControlUI.h:344/366/389` | architecture | low | **S** | 三张卡改 `visualForLayer` 取源 |
| **P2** | fly_to/事件点击/goto 全走 `setByEye` 瞬时赋值,"飞"标签名不副实 | `EarthControlUI.h:296`、`event_ticker.h:92`、`ai_setup.cpp:206` | ux | low | M | 复用现成 `moveTo`/`startAnimation`(巡游已用)加平滑过渡 |
| **P2** | 4 个纯函数/边界单测缺口(合并) | `decodeTerrarium`/`findAndUseParentData` UV/`toolFetchJson`/`buildOrbitVertices` | test | low | S~M | 纯函数直接断言(orbit 别用 back≈front 闭合断言) |
| **P3** | earth_main.cpp 注册样板 DRY:OVERLAY apply lambda×3、env 覆盖×9、`kOverlaySlotIds` 平行表、卫星层 4 连拷贝 | `earth_main.cpp:950/1010-1213` | architecture | low | M | `registerRasterOverlay` helper + 表驱动 |
| **P3** | 巨型 TU/函数拆分:`main()` ~756 行、`ai_media.cpp` 1466 行多职责、HTTP 错误样板×4 | `earth_main.cpp:778`、`ai_media.cpp:257` | architecture | low | L | 抽 `registerAllLayers`/`wireAiTools`/`runHeadlessCapture` + `httpOk` helper |
| **P3** | 死代码:`ui.cpp` 遗留 HUD(~400 行恒不可达仍编入)、city_data 死分支+FIXME 簇、auto_rotate no-op | `ui.cpp:373`、`city_data.cpp:416`、`earth_main.cpp:236` | quality | low | S~M | 从 CMake 移除 ui.cpp;删死分支 |
| **P3** | TileCallback 微优化(后台线程,不卡帧):逐像素 getColor、每顶点弃用 ECEF、`_elevationRef` 深拷贝、重复遍历 | `TileCallback.cpp:99/252/282/571/702` | perf | low | S~M | 直读 data();先算 ecef;持 ref 不拷 |
| **P3** | AI/网络微优化:单 worker HOL、feed 线程 100ms 空转、TTL 弃可用 body、`_cache` 无上限、失败无退避 | `ai_query.cpp`、`feed_layer.cpp:669` | perf | low | S~M | 小线程池、条件变量、stale-while-revalidate、LRU cap、退避 |

## 3. 建议立刻做的 Top 5(一个"P0 并发安全冲刺"批次,~2-3 天)

1. **`AICardPanel::_cards` 加锁 + 按值捕获**(唯一 high)——真实 UAF/堆破坏,AI 每次 show_chart/生图/生视频都在制造 main×draw 重叠窗口;注释是错的。模板现成(`feed_layer.cpp:250`)。
2. **flight/sat `setDataVariance(DYNAMIC)`**——S 成本消除逐帧顶点竞争,让渲染器自动 gate。
3. **跨线程 bool/int 批量 atomic 化**——近零成本,一次拉回本仓已确立的标准,消掉一整类 UB。
4. **`ai_query` worker try/catch + 始终清 inflight**——S 成本堵死"一次异常→整进程 abort/AI 永久卡死"。
5. **`testPerSpecLiftMeters` 改真驱动**——S 成本把"假绿"变真,恢复高原标记高程回归保护。
> 顺带把 P0 的 `check()` 迭代器守卫(一行)塞进同批次;B-P1 fetch 三态化紧随其后(是方向 A 的共同前置)。

## 4. 下一步计划(方向选择)

**方向 0(闸门)· P0 并发安全冲刺** — Top 5 + `check()` 守卫,拿到"无已知崩溃/UB"的安全基线。~2-3 天,极低风险。**非可选,是前置。**

**方向 A(首选)· AI 分析质变** — `get_world_brief` 全球简报 + 按时间的每源新鲜度(stale 自动附注)+ 快捷 chips。直补落后的"分析"半边,让 20+ 层一次性升值;依赖 100% 就位(≈`get_region_brief` 换全球尺度),近零新基建;B-P1 三态化正是前置。~3-5 天,低风险。

**方向 B(高天花板,排 A 之后)· STAC + COG Range reader** — Sentinel-2 真彩 + AlphaEarth 相似度/变化检测。一次投入多源复用的影像底座。多周级,中-高风险(**GDAL 会再引爆 macOS 打包/签名血泪**),应作独立 spike。

**方向 C · 质量加固冲刺** — 消化本 backlog P1-P3(LOD 测试/pickAt/失败可见性/DRY/死代码)。~1-1.5 周,可增量穿插;其中与 A 强相关项顺路收进 A。

**首选:方向 0(2-3 天)→ 方向 A(3-5 天)**,C 里与 A 强相关项(失败可见性 B7、三态化 B8、LOD 测试 B10)顺路收进 A;B 作独立 spike 排 A 之后。**明确不推荐现在做 P5 自托管中台**:触发条件(key 源 >5 / 反爬 / 多端)经核实未满足,过早优化。

## 5. 覆盖盲区(本次审计未触及、值得后续单独看)

1. **真机专属 UI**:离屏截不到 ImGui,所有 overlap/角标/卡片渲染、B1 崩溃窗口、B7 loading 态、B12 镜头过渡只能真机验。
2. **macOS 打包/签名链**:本次未看 CMake 打包脚本;方向 B 引 GDAL 前必须先评估这层。
3. **编译警告**:未跑 `-Wall`;历史有 `OSG_NOTIFY` 宏 dangling-else 隐患,值得专扫。
4. **偶发输入死锁**:MEMORY 记录的 input freeze 未解,纯运行期/真机专属。
5. **第三方依赖运行期**:libhv TLS/超时、picojson 畸形 JSON、免费 API 限流(CoinGecko/Open-Meteo 429、FIRMS UTC 日界),长会话才现形。
6. **globe GLSL 铁律区**:按要求未深入;低空曝光/暮色灰、极点闭合等历史项未纳入,要动需单独立项 + 真机回归四件套。
7. **长跑内存**:`_cache` 无上限、纹理缓存长跑增长,建议数小时压测观察 RSS。
8. **数据源 schema/URL 漂移**:建议给关键源加轻量健康探针(与 B7/B8 可见性改造共生)。
