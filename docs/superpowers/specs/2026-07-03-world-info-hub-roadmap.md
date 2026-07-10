# EarthExplorer「世界信息枢纽」路线图(spec/roadmap)

日期:2026-07-03 · 依据:[worldmonitor 全量数据源盘点](../research/2026-07-03-worldmonitor-analysis.md)(86 源,附代码证据)
目标:**汇集世界信息、并且可以用 AI 分析交互的地球**。

## 0. 现状底盘(已具备的能力)

- 3 条实时点要素层(USGS 地震/OpenSky 航班/RainViewer 降水)+ GIBS 云图 + 香港实景三维;
- AI Chat:ToolRegistry 架构(新数据源=注册一个 Tool)、8 工具、图表卡、实景照片、巡航视频;
- 成熟模式:libhv 轮询 worker → 主线程 FRAME 同步 → GL_POINTS 着色 → 点击详情卡 → env 测试钩子 → 离线 fixture E2E。

**结论:架构已经为"多源"做好准备,缺的是(a)让加源变得便宜的平台化、(b)新渲染原语、(c)承载几十个图层的 UI。**

## 1. worldmonitor 研究结论(浓缩)

- 它是 TS 实时全球情报仪表盘:**86 个外部源**,10 族(灾害/冲突/军航海运/电子战/网安/金融经济≈25 源/人道卫生/新闻/辐射/静态战略数据集);
- 架构金标准:`cron seed → Redis → 只读 RPC`,浏览器不直连上游;可 docker compose 全栈自托管;
- 对我们最有价值的三类:**T0 无 key JSON**(GDACS/EONET/GDELT/UNHCR/静态数据集…)、**T1 高价值小投入**(CelesTrak 卫星/AISStream 船舶/FIRMS/NHC)、**T2 纯 AI 工具**(PortWatch/Polymarket/FRED/Yahoo/WHO…不上球,只喂模型);
- UI 可借鉴:地图为索引+面板为正文的钻取链、数据新鲜度"情报盲区"显式化、场景预设(变体)、新闻源分级。

## 2. 分阶段方案

### P0 平台化改造(先做,~2 天)——让"加一个源"从 400 行降到 100 行 ✅ 已完成(2026-07-03,T1-T5 全闭环,见 plans/2026-07-03-world-hub-p0-platform.md 执行记录;验收达标:GDACS 108 行自证、quakes 迁移零回归、目录 UI+预设落地、弧线/折线原语带单测)

1. **FeedLayer 框架**:把 quake/flight 的共性(轮询线程+解析+GL_POINTS+拾取+详情卡+LayerManager 注册+`EARTH_*` 钩子+`get_*_summary` Tool)抽为配置驱动:
   `FeedSpec{ id, 显示名, url, 周期, 解析函数(JSON→vector<FeedPoint>), 点样式(色/大小映射), summaryJson 函数 }`。
   quake/flight 迁移为首批用户以自证(行为不变,回归守住)。
2. **渲染原语补齐**(独立模块,复用点层的着色思路):
   - **弧线层**(O-D 大圆弧,渐变+流动动画):UNHCR 流离失所、贸易流;
   - **线要素层**(折线,可虚线):海底光缆、飓风路径;
   - **面要素近似**(v1 先降级:H3/多边形 → 中心点+半径圈或点阵),避免过早做通用多边形填充。
3. **图层目录 UI**:左侧抽屉,分组树 + 搜索 + **场景预设**(灾害/军事/经济/太空/全部/干净地球),替代平铺复选框(将从 7 层涨到 20+)。
4. **AI 工具工厂**:FeedSpec 注册时自动生成 `get_<id>_summary` 工具(makeSummaryTool 已有,推广)。

验收:quake/flight 跑在新框架上零回归;新增一个演示源(GDACS)全流程 ≤100 行。

### P1 零门槛铺量(T0,~1 周)——图层数×3 ✅ 已完成(2026-07-04,T1-T9 全闭环,21 commit;6 新源(EONET/GDELT/GPSJam/UNHCR 弧/NHC 线+锥/静态战略×5 层)+事件流卡+状态带+预设扩军;验收 demo(灾害预设+AI 红色预警→图表+逐条 fly_to)通过;执行记录见 plans/2026-07-03-world-hub-p1-rollout.md)

| 源 | 形态 | 备注 |
|---|---|---|
| GDACS 灾害预警 | 点(红/橙分级配色) | 无 key JSON,P0 演示源 |
| NASA EONET | 点(13 类事件图标色) | 无 key |
| NOAA NHC 飓风 | **线+锥**(线原语首用) | 无 key ArcGIS JSON |
| GDELT GEO 新闻热点 | 点(热度大小) | 无 key,15min 更新 |
| UNHCR 流离失所 | **O-D 弧线**(弧原语首用) | 无 key CC-BY |
| 静态战略数据集 | 点/线(军事基地 226/战略港口 62/海底光缆/核设施/航天发射场/AI 数据中心 313) | 直接搬 worldmonitor 仓库 JSON,一次性 |
| GPSJam GPS 干扰 | 点圈近似 H3 | 无 key,每日 |

每源同步注册 AI 工具。验收 demo:开「灾害」预设,问 AI"过去 24 小时全球有哪些红色预警?"→ 图表卡+逐条 fly_to。

### P2 杀手锏:太空层(~3-4 天)

- CelesTrak TLE(无 key 纯文本,每日)+ C++ SGP4(libsgp4 或自移植,≈600 行)→ **卫星实时点 + 轨道线 + 地面足迹圈**;
- 这是 worldmonitor 在平面图上做不好、我们 3D 球天然碾压的图层(它自己也是 globe 模式最出彩);
- AI 工具:`get_satellites_overhead(lat,lon)`(头顶有什么)、`query_satellite(name)`(轨道参数/下次过境)。

验收 demo:开太空层缩放到全球 → 星座环绕地球;问"现在国际空间站在哪"→ fly_to + 轨道高亮。

### P3 动态流升级(~1 周)

- **AISStream 船舶**:免费 key,WebSocket(libhv 支持 WS,新管道);军舰/货轮分色;与航班层并开;
- **FIRMS 火点**:免费 key,海量点(单日全球 1-10 万)→ 需要点聚合/按缩放 LOD 抽稀(平台能力顺带沉淀);
- **ACLED 冲突事件**:免费研究 key,OAuth 刷新;点+死亡数分级。

验收 demo:船舶+航班+卫星三层同开 = "活的地球"标志画面;问"霍尔木兹海峡现在有多少船"。

### P4 AI 情报分析师(纯工具族,~3-4 天)——"分析交互"的质变 ✅ 已完成(2026-07-06,T1-T6 subagent-driven 双审查全闭环,commit d9a482e9..95314794;AsyncJsonFetcher 异步基建(工具主线程不阻塞网络,pending/缓存/fixture 状态机)+5 免 key 工具(get_weather_forecast/get_crypto_prices/get_country_indicator/get_chokepoint_traffic/get_prediction_markets)+get_region_brief 跨源合成(FeedLayer regionBriefProviders external-linkage 注册表);WHO/旅行警告/Yahoo/FRED 4 工具本期未做(数据形态弱/非官方/需 key),后补即套 Task2 模板;快捷 chips UI 顺延。执行/对账见 plans/2026-07-06-p4-worldtools-p6a-science-quickwins.md。教训:一个 implementer 子代理擅自嵌套 spawn 野 agent,已记 memory)

新阶段 **P6a 科学速赢包 ✅ 已完成**(2026-07-06,T7-T8,commit 017aea87/b0b08da8;NDVI 植被+VIIRS 夜光(GIBS 现成瓦片,OVERLAY 互斥泛化为组表,审查确认与 clouds/precip 逐字等价)+GEBCO 海底地形(WMS bbox 合成,mercatorTileBBox 纯函数带单测)。离屏真机验收:NDVI 植被分布鲜明✅、GEBCO 洋中脊清晰✅、夜光工作但全球尺度灯光偏弱⚠️(待调优);预设回归 ndvi/nightlights/gebco 不入既有预设✅。**验收发现 2 待优化项**:①夜光 overlay 在夜面被昼夜着色压暗,视觉弱;②GEBCO z1 半球瓦片报"Failed to find reader/writer"(WMS query 式 URL 无 .png 扩展名,osgDB 选解码器失败;z2+ 细瓦片正常故整体可见,但个别低 zoom 瓦片缺失+日志噪声)。P6b(COG+AlphaEarth)/P6c(GRIB2+AIFS)各自另出 plan,见计划附录。)

不上球、只给模型的查询工具(每个 ~30 行,架构红利):
- PortWatch 咽喉点流量、Polymarket 预测市场(C++ 原生 TLS 大概率绕过其浏览器 JA3 封锁)、Yahoo/CoinGecko 行情、FRED/WorldBank/IMF 宏观、WHO 疫情、政府旅行警告;
- **`get_region_brief(lat,lon,radius_km)` 跨源合成工具**:并行拉取该区域全部相关源摘要 → 模型合成结构化简报 → 自动 show_chart + 逐事件 fly_to 链接。从"单源问答"升级为"跨源情报合成"。

验收 demo:"红海航运现在什么状况?" → AI 自主调船舶+咽喉点+新闻+预测市场 → 简报卡+图表+飞行导览。

### P5 数据中台决策(择机,量表触发)

触发条件任一:需 key 的源 >5 个 / 出现反爬源(OREF、Fear&Greed 抓取)/ 想要多端共享。
→ docker compose 自托管 worldmonitor 后端(4 容器,SELF_HOSTING.md 验证可行),EarthExplorer 改拉其 `/api/<domain>/v1/*` **一次接入换几十个预聚合源**;其 ~150 个 seed 脚本同时是"上游 API 反爬/限额/评分公式说明书"。
在此之前保持直连(P1-P4 全部无 key 或个人免费 key,零运维)。注意 AGPL:自用/自托管无碍。

## 3. UI 重新设计(围绕"信息枢纽",分区沿用既定偏好)

```
┌───────────────────────────────────────────────────────────┐
│ 顶部微状态带:UTC 时钟 · 数据源健康 n/m · 当前预设名        │
│ ┌──────────┐                            ┌───────────────┐ │
│ │左:操作区 │                            │右:信息卡流    │ │
│ │·紧凑工具条│         3D 地球            │·事件流 ticker │ │
│ │ (相机/太阳│      (永远全屏可见)       │ (时序,点击   │ │
│ │  /渲染折叠)│                           │  fly_to)      │ │
│ │·图层目录  │                            │·详情/图表/照片│ │
│ │ 抽屉:分组 │                            │ 卡(既有,可关)│ │
│ │ 树+搜索+  │                            │·新鲜度指示    │ │
│ │ 场景预设  │                            │ (情报盲区)   │ │
│ └──────────┘                            └───────────────┘ │
│        底部:AI 命令栏 + 快捷 chips(今日简报/视野内动态/📷/🎬) │
└───────────────────────────────────────────────────────────┘
```

- **球是索引、卡是正文**(借鉴 worldmonitor 钻取链):点球上要素→右上详情卡;点事件流条目→fly_to+高亮;
- **左=操作、右=信息、下=对话** 的既定分区不变,只做密度升级:Earth Control 折叠为紧凑工具条,腾出空间给图层目录;
- **场景预设**是驾驭 20+ 图层的关键(borrow 变体思想):一键切换灾害/军事/经济/太空视图;
- **数据新鲜度指示**(borrow 情报盲区):每源 fresh/stale 小圆点,stale 源在 AI 回答中自动附注"数据可能过期";
- v2 候选:底部迷你时间轴(24h 事件密度回放)、TV/演示模式(自动巡航+AI 播报)。
- 实现顺序:图层目录(P0)→ 事件流卡+状态带(P1 末)→ 快捷 chips(P4)→ 其余 v2。

## 4. AI 交互设计升级

- **每源三件套**(自动获得):统计问答(summary 工具)/ 可视化(show_chart)/ 定位(fly_to+set_layer);
- **跨源合成**:get_region_brief(P4)、get_world_brief(全球版,快捷 chip「今日简报」);
- **生成式媒体×情报**(已就绪能力的组合玩法,v2):"给我一段红海局势简报视频"= 区域简报文本 + 实景照片 + 巡航视频;
- **主动化**(v2):巡逻模式——AI 定时调 get_world_brief,变化超阈值时右上推简报卡。

## 5. 风险与成本

| 项 | 评估 |
|---|---|
| 免费源限额 | T0 全无 key;OpenSky/FIRMS/ACLED 免费层足够单机使用 |
| 反爬源 | OREF/Reddit/Cloudflare Radar 明确不接(worldmonitor 踩坑记录在案) |
| 点量性能 | FIRMS 数万点需聚合 LOD(P3 沉淀为平台能力) |
| AGPL | 只借鉴数据源清单/自托管后端,不拷贝其前端代码进本仓库 |
| AI 成本 | 查询类工具零生成成本;简报合成一次 ~千 token 级 |

## 6. 执行方式

每个 Phase 沿用本仓库已验证的流程:brainstorm 确认范围 → writing-plans 拆任务 → subagent-driven 实现+双审查 → 真机验收 → push。P0 是所有后续的地基,建议先行;P1-P4 可按兴趣调序(P2 太空层演示效果最惊艳,P4 对"AI 分析"目标价值最大)。
