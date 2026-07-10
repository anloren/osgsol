# worldmonitor 数据源全量盘点(为 osgVerse EarthExplorer 适配评估)

> 分析对象:https://github.com/koala73/worldmonitor(克隆于 scratchpad/worldmonitor,v2.8.0,AGPL-3.0)
> 分析日期:2026-07-03。所有结论以实际代码为证据(附文件路径),非仅凭文档。

---

## 1. 项目概览

- **是什么**:实时全球情报仪表盘("Real-time global intelligence dashboard")。聚合新闻、地缘政治、军事、金融、灾害、网络、气候、海运、航空等数十类数据,渲染为「地图 + 专业面板网格」的态势感知界面。一套代码 6 个站点变体(full/tech/finance/commodity/happy/energy),另有 Tauri 2 桌面版。
- **技术栈**(`package.json`, `README.md`, `ARCHITECTURE.md`):
  - 前端:**原生 TypeScript(无框架)** + Vite;双地图引擎 — globe.gl+Three.js(3D 球)与 deck.gl+MapLibre(WebGL 平面图);Web Worker 跑 ONNX(Transformers.js:嵌入/情感/摘要/NER)与新闻聚类。
  - 后端:**Vercel Edge Functions**(`api/` 目录,proto 契约生成的域网关 + 手写运维端点);**Railway 常驻 relay**(`scripts/ais-relay.cjs`,1.1 万行:AIS WebSocket 代理、RSS 代理、Telegram MTProto 轮询、OREF 轮询、CelesTrak 种子);**~150 个 seed 脚本**(`scripts/seed-*.mjs`,Railway cron 拉上游写 Redis);**Upstash Redis** 作全局缓存/数据中台;**Convex** 只管账号/支付/通知等业务态。
  - AI:Groq(主)/OpenRouter(备)/Ollama(本地),用于新闻简报合成、CII 情报评估、chat-analyst。
- **核心架构模式("gold standard")**:`Railway cron seed → Redis → Vercel RPC 只读` — 客户端一次 RPC 拿到聚合好的 JSON,上游全部在服务端收敛,浏览器几乎不直连第三方(Polymarket JA3 例外)。页面加载时 `/api/bootstrap` 两级注水(3s/5s 超时)让面板首屏即有数据。
- **UI 形态**:顶部地图(球/平面可切)+ 下方**可拖拽、可调行列跨度的面板网格**(104 个 Panel 类,`src/components/*Panel.ts`),56 种地图图层(`src/config/map-layer-definitions.ts`),多标签页(tab-store)、CMD+K 命令面板、TV 模式、移动端面板导航、数据新鲜度追踪器(35 个源的 fresh/stale/gap 状态,显式报告"情报盲区")。

---

## 2. 数据源全量清单(核心交付)

> 图例:🟢 无 key 免费;🟡 免费注册 key;🔴 付费/受限;⚙️ 静态数据集/人工整理。
> "接入方式" 指 worldmonitor 内部路径:seed=Railway cron 种子脚本写 Redis;relay=Railway 常驻进程;edge=Vercel edge 直调;browser=浏览器直连。

### 2.0 总表

| # | 数据源 | 类别 | 数据内容 | 协议/端点 | 数据形状 | key/费用 | 更新频率 | worldmonitor 展示 | 证据文件 |
|---|--------|------|----------|-----------|----------|----------|----------|------------------|----------|
| 1 | USGS Earthquakes | 灾害 | 全球 M4.5+ 地震 | REST GeoJSON `earthquake.usgs.gov/.../4.5_week.geojson` | 点 | 🟢 | 5 分钟 | 地图 natural 层+灾害面板 | `scripts/seed-earthquakes.mjs` |
| 2 | GDACS(UN) | 灾害 | 地震/洪水/气旋/火山/野火/干旱预警(红橙分级) | REST JSON `gdacs.org/gdacsapi/api/events/geteventlist/MAP` | 点+级别 | 🟢 | 实时 | natural 层(过滤绿色低级) | `scripts/seed-natural-events.mjs` |
| 3 | NASA EONET | 灾害 | 13 类自然事件(30 天开放事件) | REST JSON `eonet.gsfc.nasa.gov/api/v3/events` | 点/多点 | 🟢 | 实时 | natural 层(排除地震,野火限 48h) | `scripts/seed-natural-events.mjs` |
| 4 | NOAA NHC | 灾害 | 热带气旋路径/锥形预报 | ArcGIS MapServer `mapservices.weather.noaa.gov/tropical/...` | 线/面 | 🟢 | 实时 | natural 层飓风叠加 | `scripts/seed-natural-events.mjs` |
| 5 | NWS api.weather.gov | 天气 | 美国活跃天气警报 | REST JSON `api.weather.gov/alerts/active` | 面(区域) | 🟢 | 实时 | weather 层 | `scripts/seed-weather-alerts.mjs` |
| 6 | NASA FIRMS | 火情 | VIIRS 卫星热异常点(近实时) | REST CSV `firms.modaps.eosdis.nasa.gov/api/area/csv/{key}/{source}/{bbox}/1` | 点(海量) | 🟡 必需 | ~3h 轨道 | fires 层 | `scripts/seed-fire-detections.mjs` |
| 7 | Open-Meteo(ERA5) | 气候 | 15 个冲突/灾害带温度、降水距平(30 天基线) | REST JSON open-meteo.com | 时序→点异常 | 🟢 | 每日 | 气候异常面板+CII 放大器 | `scripts/seed-climate-anomalies.mjs`, `scripts/_open-meteo-archive.mjs` |
| 8 | NOAA GML | 气候 | 全球 CO2/CH4/N2O 浓度 | 纯文本 `gml.noaa.gov/webdata/ccgg/trends/...` | 时序 | 🟢 | 月度 | CO2 监测面板 | `scripts/seed-co2-monitoring.mjs` |
| 9 | NSIDC | 气候 | 海冰范围 | 文件 `noaadata.apps.nsidc.org` | 时序 | 🟢 | 每日 | 气候海冰面板 | `scripts/seed-climate-ocean-ice.mjs` |
| 10 | ReliefWeb(UN OCHA) | 灾害/人道 | 灾害报告与危机文档 | REST JSON `api.reliefweb.int`(需 appname) | 文本+国家 | 🟡 | 实时 | 气候灾害面板 | `scripts/seed-climate-disasters.mjs` |
| 11 | ACLED | 冲突 | 武装冲突/抗议/骚乱事件(带行为者、死亡数) | REST JSON,OAuth 24h 刷新 | 点(日粒度) | 🟡 免费(研究) | 每周官方更新;10min 缓存 | protests/conflicts 层+CII 冲突分量 | `.env.example`, `server/worldmonitor/unrest/`, docs/data-sources.mdx |
| 12 | UCDP GED | 冲突 | Uppsala 冲突事件库(编年,滞后) | REST JSON `ucdpapi.pcr.uu.se/api/gedevents/{ver}` | 点 | 🟡 token | 年度+候选月度 | ucdpEvents 层+CII 回退源 | `scripts/seed-ucdp-events.mjs` |
| 13 | GDELT DOC/GEO | 事件/新闻 | 全球事件、语调、地理新闻共现 | REST JSON `api.gdeltproject.org/api/v2/doc/doc`、`data.gdeltproject.org` | 点+文本 | 🟢 | 15 分钟 | 抗议双源之一、GDELT intel 面板、风险信号 | `scripts/seed-gdelt-intel.mjs`, `scripts/_gdelt-fetch.mjs`, `scripts/seed-unrest-events.mjs` |
| 14 | LiveUAMap(人工) | 冲突 | 伊朗冲突事件(打击/袭击)人工整理 | ⚙️ 手动 seed(无 API) | 点+严重度 | ⚙️ | 人工~每周 | iranAttacks 层 | `scripts/seed-iran-events.mjs`(`sourceVersion: 'liveuamap-manual-v1'`) |
| 15 | OREF(以色列国土前线司令部) | 冲突 | 火箭/导弹/无人机警报(希伯来语,1480 条地名翻译) | REST JSON `oref.org.il/WarningMessages/alert/alerts.json`,须 curl+以色列住宅代理绕 Akamai WAF | 点+波次 | 🟢(但反爬狠) | 5 分钟轮询 | OREF 警报面板+CII 以色列加分 | `scripts/ais-relay.cjs:111` |
| 16 | GPSJam.org | 电子战 | GPS/GNSS 干扰 H3 六边形网格(源自 ADS-B Exchange) | REST(每日 GeoJSON) | H3 hex 面 | 🟢 | 每日 | gpsJamming 层+CII 安全分量 | `api/gpsjam.js`, `server/worldmonitor/intelligence/v1/list-gps-interference.ts` |
| 17 | OpenSky Network | 航空 | ADS-B 航班状态向量(军机过滤) | REST JSON `opensky-network.org/api`,OAuth2 client credentials | 点(动) | 🟡 匿名限流 | 实时(秒级) | military 层军机追踪 | `scripts/seed-military-flights.mjs:510-512` |
| 18 | Wingbits | 航空 | ADS-B 航班+机主/运营人/机型 enrichment | REST `customer-api.wingbits.com/v1/flights` | 点(动) | 🔴 商务 key | 实时 | 军机层主源(OpenSky 备) | `scripts/seed-military-flights.mjs:511`, `src/services/wingbits.ts` |
| 19 | AISStream.io | 海运 | 全球 AIS 船位流 | **WebSocket** `wss://stream.aisstream.io/v0/stream` | 点流(动) | 🟡 免费 key | 实时流 | ais 层船舶+军舰监视,relay 扇出给浏览器 | `scripts/ais-relay.cjs:46` |
| 20 | IMF PortWatch | 海运 | 港口/咽喉点通行量(每日船流) | ArcGIS FeatureServer `services9.arcgis.com/.../Daily_Chokepoints_Data` | 点+时序 | 🟢 | 每日 | 咽喉点条带面板、Hormuz 追踪 | `scripts/seed-portwatch*.mjs` |
| 21 | WTO Hormuz Tracker | 海运 | 霍尔木兹贸易追踪(PowerBI 后端逆向) | REST `wabi-europe-north-b-api.analysis.windows.net`(WABI) | 时序 | 🟢(脆) | 每日 | Hormuz 面板 | `scripts/seed-hormuz.mjs` |
| 22 | Submarine Cable Map(TeleGeography) | 基建 | 全球海底光缆线+登陆点 | REST JSON `submarinecablemap.com/api/v3` | 线+点 | 🟢 | 静态月度 | cables 层 | `scripts/seed-submarine-cables.mjs` |
| 23 | NGA MSI | 基建 | 航海警告(光缆故障/维修船) | REST `msi.nga.mil` | 文本+点 | 🟢 | 实时 | 光缆健康 advisory | `src/services/cable-health.ts`(msi.nga.mil 引用) |
| 24 | Cloudflare Radar | 网络 | 互联网中断/异常注释 | REST `api.cloudflare.com/client/v4/radar/annotations/outages` | 国家/ASN+时段 | 🔴 需 Radar 权限 token | 实时 | outages 层+CII | `scripts/seed-internet-outages.mjs:7` |
| 25 | abuse.ch Feodo Tracker | 网安 | 僵尸网络 C2 服务器 IOC | REST JSON `feodotracker.abuse.ch` | IP→地理点 | 🟢 | 小时级 | cyberThreats 层 | `scripts/seed-cyber-threats.mjs` |
| 26 | abuse.ch URLhaus | 网安 | 恶意软件分发 URL | REST `urlhaus-api.abuse.ch` | URL/IP→点 | 🟢 | 小时级 | 同上 | 同上 |
| 27 | C2IntelFeeds | 网安 | 社区 C2 指标 | GitHub raw CSV | IP→点 | 🟢 | 每日 | 同上 | 同上 |
| 28 | AlienVault OTX | 网安 | 威胁情报 pulse IOC | REST `otx.alienvault.com` | 混合 | 🟡 | 实时 | 同上 | 同上 |
| 29 | AbuseIPDB | 网安 | 众包恶意 IP | REST `api.abuseipdb.com` | IP→点 | 🟡 配额 | 实时 | 同上 | 同上 |
| 30 | Ransomware.live | 网安 | 勒索团伙受害者/动态 | REST | 文本+组织 | 🟢 | 实时 | 同上+新闻 | 同上 |
| 31 | ipinfo.io / freeipapi.com | 网安辅助 | IP 地理定位(IOC 落点) | REST | IP→经纬度 | 🟡/🟢 | 24h 缓存 | IOC 上球前置 | `scripts/seed-cyber-threats.mjs` |
| 32 | MITRE ATT&CK | 网安 | APT 组织画像(159 处引用,静态) | ⚙️ 静态数据 | 文本 | 🟢 | 静态 | APT 归因面板 | `src/` 内 attack.mitre.org 链接 |
| 33 | CelesTrak | 太空 | NORAD TLE 轨道根数(情报卫星 ~80-120 颗) | REST `celestrak.org/NORAD/elements/gp.php?GROUP=...&FORMAT=tle` | TLE→SGP4 轨道 | 🟢 | 每日 TLE | satellites 层:球面实时轨道+足迹(客户端 satellite.js 传播) | `scripts/ais-relay.cjs:1779`, `src/services/satellites.ts` |
| 34 | EPA RadNet | 辐射 | 美国固定站伽马剂量率 | REST CSV `radnet.epa.gov/cdx-radnet-rest` | 站点时序 | 🟢 | 小时 | radiationWatch 层 | `scripts/seed-radiation-watch.mjs:382` |
| 35 | Safecast | 辐射 | 全球众包辐射测量 | REST `api.safecast.org/measurements.json` | 点 | 🟢 | 实时 | 同上 | `scripts/seed-radiation-watch.mjs:399` |
| 36 | UNHCR | 人道 | 难民/庇护/IDP 人口统计 | REST `api.unhcr.org/population/v1/population/` | 国家 O-D 矩阵 | 🟢 CC-BY | 年度+月度 | displacement 层(流向)+面板+CII | `scripts/seed-displacement-summary.mjs` |
| 37 | UN OCHA HAPI | 人道 | 人道指标(流离失所等) | REST `hapi.humdata.org` | 国家级 | 🟢 | 月度 | displacement 面板 | `server/worldmonitor/displacement/` |
| 38 | WHO Disease Outbreak News | 卫生 | 官方疫情通报 | REST `who.int/api/emergencies/diseaseoutbreaknews` | 国家+文本 | 🟢 | 实时 | diseaseOutbreaks 层 | `scripts/seed-disease-outbreaks.mjs` |
| 39 | CDC Travel Notices / outbreaknewstoday / thinkglobalhealth | 卫生 | 旅行健康告示/疫情新闻/追踪器 | RSS+JS bundle 抓取 | 文本+国家 | 🟢 | 实时 | 同上 | 同上 |
| 40 | OpenAQ v3 | 空气 | 全球空气质量站点 | REST `api.openaq.org/v3`(key 必需) | 点 | 🟡 | 小时 | 健康空气面板 | `scripts/seed-health-air-quality.mjs` |
| 41 | WAQI | 空气 | 城市 AQI 补充 | REST `api.waqi.info/map/bounds` | 点 | 🟡 | 实时 | 同上 | 同上 |
| 42 | FAA ASWS | 航空 | 美国 14 枢纽机场地面停飞/延误 | XML `nasstatus.faa.gov` | 机场点+状态 | 🟢 | 实时 | flights 层+航空面板 Ops | `server/worldmonitor/aviation/` |
| 43 | AviationStack | 航空 | 机场航班记录→延误率/取消率;航班状态 | REST(**付费预算制**,月配额 Redis 计数封顶) | 机场统计 | 🔴 免费层小 | cron+55min 守卫 | 航空面板 Ops/Flights/Airlines | `.env.example`(预算变量), `api/aviation/` |
| 44 | ICAO NOTAM API | 航空 | MENA 46 机场空域关闭 NOTAM | REST `dataservices.icao.int` | 机场+文本 | 🟡 | 实时 | 机场关闭覆盖 | `.env.example`, docs |
| 45 | Travelpayouts | 航空 | 机票价格搜索 | REST `api.travelpayouts.com` | 报价 | 🟡 联盟 | 请求时 | 航空面板 Prices + `fly` 命令 | README, `.env.example` |
| 46 | Yahoo Finance(非官方) | 金融 | 股票/指数/商品/外汇报价与 K 线 | REST `query1.finance.yahoo.com/v8/finance/chart`(21 处) | 时序 | 🟢 无 key(非官方) | 秒~分钟 | 市场面板、海湾经济、恐惧贪婪分量 | `scripts/_yahoo-fetch.mjs`, `scripts/seed-market-quotes.mjs` |
| 47 | Finnhub | 金融 | 股票实时报价(主源)、内幕交易 | REST `finnhub.io` | 报价 | 🟡 | 实时 | 市场面板 | `.env.example`, `server/worldmonitor/market/` |
| 48 | Alpha Vantage | 金融 | 报价备源 | REST `alphavantage.co` | 时序 | 🟡 | 分钟 | 备源 | server/market 引用 |
| 49 | CoinGecko(+Pro) | 加密 | 币价、稳定币锚定、板块 | REST `api.coingecko.com` / `pro-api` | 报价 | 🟢/🔴 | 实时 | 加密面板、稳定币监控 | `scripts/seed-crypto-quotes.mjs` 等 |
| 50 | CoinPaprika | 加密 | 币价备源 | REST `api.coinpaprika.com` | 报价 | 🟢 | 实时 | 备源 | server/market |
| 51 | Polymarket Gamma | 预测市场 | 地缘政治合约概率+成交量 | REST `gamma-api.polymarket.com`(**Cloudflare JA3 封锁**,4 级回退:bootstrap→RPC→浏览器直连→Tauri 原生 TLS) | 合约时序 | 🟢 | 5min 缓存 | 预测市场面板、国家简报、early-warning 信号 | `api/polymarket.js`, `src/services/prediction/index.ts` |
| 52 | CNN Fear&Greed + Barchart 抓取 + AAII | 金融情绪 | 恐惧贪婪指数分量(CPC put/call、S5TH、AAII 调查) | REST+HTML 抓取 | 指数时序 | 🟢(抓取脆) | 每日 | FearGreed 面板(自算 2.0 版) | `scripts/seed-fear-greed.mjs:51-112` |
| 53 | FRED(圣路易斯联储) | 经济 | 美/全球宏观时序 | REST `api.stlouisfed.org` | 时序 | 🟡 | 每日 | 经济面板、宏观信号 | `.env.example`, `scripts/seed-economy.mjs` |
| 54 | ECB Data Portal | 经济 | 欧元区汇率参考价、收益率曲线、ESTR、金融压力指数 | REST SDMX `data-api.ecb.europa.eu` | 时序 | 🟢 | 每日 | FSI/欧洲收益率面板 | `scripts/seed-ecb-*.mjs`, `seed-fsi-eu.mjs` |
| 55 | Eurostat | 经济 | 欧盟 CPI/GDP/失业/房价/工业产出 | REST JSON-stat `ec.europa.eu/eurostat` | 国家时序 | 🟢 | 月度 | 国家深潜面板 | `scripts/seed-eurostat-*.mjs` |
| 56 | World Bank Open Data | 经济 | 发展指标、外债 | REST `api.worldbank.org`(20 处) | 国家年度 | 🟢 | 年度 | 国家简报/韧性分量 | `scripts/seed-wb-*.mjs` |
| 57 | BIS Stats | 经济 | 央行政策利率、REER、信贷/GDP | SDMX `stats.bis.org` | 时序 | 🟢 | 月度 | 央行面板 | `scripts/seed-bis-*.mjs` |
| 58 | IMF SDMX(WEO/IRFCL) | 经济 | 增长、外部头寸、黄金储备、国债 | REST `api.imf.org`(间歇性 401,建议 key) | 国家时序 | 🟡 建议 | 半年 | 国债/宏观面板 | `scripts/seed-imf-*.mjs`, `.env.example` |
| 59 | US BLS | 经济 | 美国劳工统计 | REST | 时序 | 🟢 限额 | 月度 | 经济面板 | `scripts/seed-bls-series.mjs` |
| 60 | USAspending | 经济/军事 | 美国联邦支出与国防合同 | REST `api.usaspending.gov` | 合同记录 | 🟢 | 每日 | 国防开支信号 | `scripts/seed-usa-spending.mjs`, `src/services/usa-spending.ts` |
| 61 | US Treasury FiscalData | 经济 | 财政收入/债务 | REST `api.fiscaldata.treasury.gov` | 时序 | 🟢 | 每日 | 财政面板 | `scripts/seed-national-debt.mjs` |
| 62 | UN Comtrade | 贸易 | 双边商品贸易流(HS4) | REST `comtradeapi.un.org` | O-D 矩阵 | 🟡 限额 | 月度 | 贸易流/供应链面板 | `scripts/seed-comtrade-bilateral-hs4.mjs` |
| 63 | WTO API | 贸易 | 贸易限制、关税、SPS/TBT | REST `api.wto.org`, `stats.wto.org` | 国家记录 | 🟡 | 月度 | WTO 贸易政策情报 | `server/worldmonitor/trade/` |
| 64 | EIA | 能源 | 美国油价/产量/库存 | REST `api.eia.gov` | 时序 | 🟡 | 周度 | 石油库存/能源面板 | `scripts/seed-eia-petroleum.mjs` |
| 65 | GIE AGSI+ | 能源 | 欧盟天然气储气量 | REST `agsi.gie.eu` | 国家时序 | 🟡 免费 | 每日 | 储气面板 | `scripts/seed-gie-gas-storage.mjs` |
| 66 | JODI | 能源 | 全球油气产量/库存 | 文件 `jodidata.org` | 国家月度 | 🟢 | 月度 | 能源变体面板 | `scripts/seed-jodi-*.mjs` |
| 67 | Ember / OWID | 能源 | 电力结构、低碳份额 | CSV/GitHub | 国家年度 | 🟢 | 年度 | 能源结构面板 | `scripts/seed-ember-electricity.mjs`, `seed-owid-energy-mix.mjs` |
| 68 | OFAC / EU Sanctions Map / FATF | 制裁 | 制裁名单与灰名单 | REST/抓取 `sanctionslistservice.ofac.treas.gov`, `sanctionsmap.eu`, `fatf-gafi.org` | 国家/实体 | 🟢 | 不定期 | sanctions 层+制裁压力指数 | `scripts/seed-sanctions-pressure.mjs`, `seed-fatf-listing.mjs` |
| 69 | CFTC COT | 金融 | 期货持仓报告 | REST `cftc.gov` | 周度时序 | 🟢 | 周五 | COT 持仓面板 | `scripts/seed-cot.mjs` |
| 70 | FAO | 粮食 | 食品价格指数 | 文件 `fao.org` | 月度时序 | 🟢 | 月度 | FAO 面板 | `scripts/seed-fao-food-price-index.mjs` |
| 71 | RSS 生态(500+ 源,15 类) | 新闻 | BBC/Reuters/AP/Al Jazeera/政府源/智库/OSINT 博客…+大量 Google News 查询包装(379 处 news.google.com) | RSS/Atom,服务端聚合 `listFeedDigest`(20 并发、8s 超时、Redis 900s) | 文本流 | 🟢 | 15min 摘要 | 新闻面板矩阵、AI 简报、威胁分类、CII 新闻分量 | `server/worldmonitor/news/v1/_feeds.ts`, docs/data-sources.mdx |
| 72 | Telegram OSINT | 新闻/OSINT | 56 频道(官方/交战方/独立 OSINT 混编,tier 1-3) | **MTProto**(GramJS)relay 60s 轮询,200 条滚动缓冲 | 文本流 | 🟡 免费 api_id(需会话) | 60s | Telegram intel 面板 | `scripts/ais-relay.cjs:735+`, `data/telegram-channels.json` |
| 73 | Hacker News | 科技 | 头条/Show HN | RSS `hnrss.org` + Firebase API | 文本 | 🟢 | 实时 | tech 变体新闻 | feeds 配置 |
| 74 | Reddit(经 ScrapeCreators 或遗留 OAuth) | 社媒 | r/wallstreetbets 热帖、社交速度 | REST(公开端点已 403,须第三方 vendor) | 文本+热度 | 🔴 实质付费 | 轮询 | social velocity、WSB ticker | `.env.example`(详细吐槽), `src/services/social-velocity.ts` |
| 75 | PizzINT.watch | 另类信号 | 五角大楼披萨指数等另类监控+GPR 批量 | REST `pizzint.watch/api/dashboard-data` | 指数 | 🟢 | 实时 | PizzINT 信号(纳入 freshness 35 源) | `scripts/ais-relay.cjs:6357`, `src/services/pizzint.ts` |
| 76 | Windy Webcams | 影像 | 全球网络摄像头快照 | REST `api.windy.com/webcams/api/v3` | 图片+点 | 🟡 | 实时 | webcams 层 | `scripts/seed-webcams.mjs`, `server/worldmonitor/webcam/v1/get-webcam-image.ts` |
| 77 | YouTube Live | 影像 | 22 个热点城市直播流 | iframe 嵌入(懒加载) | 视频 | 🟢 | 直播 | 网络摄像头面板 4 宫格 | docs/data-sources.mdx, `src/services/webcams/` |
| 78 | CelesTrak 之外的静态战略数据集 | 军事/基建 | 226 军事基地、62 战略港口、19 贸易路线、核设施/伽马辐照器、航天发射场、关键矿产、313 AI 数据中心(Epoch AI)、DMZ 边界多边形 | ⚙️ 仓库内置 JSON(`data/`, `src/config/`) | 点/线/面 | 🟢 | 静态 | bases/nuclear/spaceports/minerals/datacenters/tradeRoutes 层 | `scripts/seed-military-bases.mjs`, `data/` |
| 79 | 消费者价格抓取(自建) | 民生 | 各国杂货篮子/油价(Playwright 抓超市) | 自营容器抓取→Redis | 国家指数 | ⚙️ 自建 | cron | 消费价格面板、BigMac | `consumer-prices-core/`, `scripts/seed-grocery-basket.mjs`(SerpAPI) |
| 80 | Groq / OpenRouter / Ollama | AI | LLM 推理(简报合成、CII 评估、chat-analyst、预报) | REST(OpenAI 兼容) | 文本 | 🟡/🟢 本地 | 请求时 | AI 简报/分析 | `.env.example`, `api/chat-analyst.ts` |
| 81 | Exa.ai / Brave Search / SerpAPI | AI 检索 | 股票新闻研究检索 | REST | 文本 | 🔴 | 请求时 | 股票研究面板 | `server/worldmonitor/market/v1/stock-news-search.ts` |
| 82 | 政府旅行警告(美国务院/澳 DFAT/英 FCDO)+13 使馆+CDC/ECDC/WHO | 安全 | 24 个 RSS 警告源,分级 0-4 | RSS/Atom(经 relay 代理) | 国家+级别 | 🟢 | 每小时 seed | 安全警告面板+CII 加分/地板 | `scripts/seed-security-advisories.mjs` |
| 83 | 服务状态页(GitHub/Docker/Netlify/Zoom/Notion/Replicate…) | 科技 | SaaS 宕机状态 | 状态页 RSS/JSON | 文本 | 🟢 | 实时 | tech 变体 outages 面板 | `scripts/seed-service-statuses.mjs` |
| 84 | 世界幸福报告 / Good News Network 等 | 正能量 | 幸福指数、善行、物种恢复、清洁能源装置 | RSS+静态数据 | 国家/点 | 🟢 | 年度/实时 | happy 变体图层与面板 | `src/services/happiness-data.ts`, `kindness-data.ts` |
| 85 | Nominatim(OSM) | 辅助 | 逆地理编码 | REST `nominatim.openstreetmap.org` | 点→地名 | 🟢 限速 | 请求时 | 点击详情 | `api/reverse-geocode.js` |
| 86 | 底图:OpenFreeMap / 自托管 PMTiles / CARTO basemap | 底图 | 矢量瓦片 | PMTiles/TileJSON | 瓦片 | 🟢 | 静态 | deck.gl 底图 | `.env.example`(VITE_PMTILES_URL) |

### 2.1 重点源逐段说明(与 EarthExplorer 相关性最高的)

**USGS / GDACS / EONET / NHC(灾害四件套)** — worldmonitor 把三源合并去重(0.1° 网格),再喂给"地理汇聚"检测(地震靠近管线→级联告警)。EarthExplorer 已有 USGS;GDACS 与 EONET 是零成本增量:同为无 key JSON,GDACS 自带红/橙分级(可直接映射点色),EONET 提供火山/风暴/冰山等 13 类。NHC 输出线/面(飓风路径+预报锥),是球面弧线/面要素的好素材。

**CelesTrak TLE + SGP4(轨道监视)** — relay 每日拉 `gp.php?GROUP=...&FORMAT=tle`(intelligence 相关组),客户端 satellite.js 每帧传播,画卫星点、15 分钟轨迹线、地面足迹圈。**这是 worldmonitor 里最"3D 球原生"的图层**,平面图反而委屈了它;EarthExplorer 的 OSG 球做轨道线+足迹会远比 globe.gl 好看。C++ 侧有现成 SGP4 库(如 libsgp4),TLE 是纯文本无 key。

**AISStream(船舶)** — 唯一的 WebSocket 流。worldmonitor 不让浏览器直连,而是 Railway relay 订阅后扇出,并维护军舰名单监视。EarthExplorer 用 libhv 的 WS 客户端可直连(免费 key),或按 bbox 订阅降载;与既有 flight 点要素模板同构(点+航向+速度)。

**OpenSky / Wingbits(航班)** — EarthExplorer 已接 OpenSky。worldmonitor 的增量思路:①OAuth2 提升配额;②军机过滤(callsign/ICAO hex 前缀名单)做"军事活动"层;③Wingbits 做机型/运营人 enrichment(商务 key,可略)。

**GPSJam(GPS 干扰)** — 每日 H3 hex GeoJSON,无 key。上球即"干扰热力蜂窝"面要素;数据小、语义强(电子战),AI-chat 讲得出故事("黑海周边今日高干扰 hex 数 N")。worldmonitor 用它给 CII 安全分量加分的公式(`min(35, high×5+medium×2)`)可直接抄。

**Cloudflare Radar(断网)** — 数据是"国家/ASN+时间段"的事件注释,不是几何;上球适合做国家面高亮/标签。需要付费权限 token,是断网类里唯一现实可用的(NetBlocks 无公开 API — worldmonitor 也没接 NetBlocks,证实了这点)。

**网安 IOC 六源(abuse.ch 等)** — 本身只有 IP,worldmonitor 用 ipinfo/freeipapi 转经纬度再上球(24h 缓存、250 IP/轮、16 并发)。视觉上是"全球威胁点阵"很唬人,但地理意义弱(IP 定位≠攻击者位置);EarthExplorer 若接,建议只当装饰层+计数摘要。

**ACLED / UCDP / GDELT(冲突三源)** — ACLED 是黄金源但要注册+OAuth 刷新(24h token,worldmonitor 专门写了自动换发);UCDP 滞后严重(年度);GDELT 免费实时但噪声大(worldmonitor 用 mention≥5 过滤+双源 Haversine 去重)。EarthExplorer 想要"冲突点层"最省事的路径:GDELT GEO(无 key)起步,ACLED 进阶。

**OREF(火箭警报)** — 数据价值高但工程代价离谱:Akamai WAF 封 Node fetch(JA3),必须 curl+以色列住宅代理。除非专做中东态势,不建议 EarthExplorer 碰。

**Polymarket(预测市场)** — 无 key 但 Cloudflare JA3 封服务端;worldmonitor 搞了 4 级回退,其中"桌面原生 TLS(Rust reqwest)可过"这一条对 EarthExplorer 有利:**libhv/openssl 的 C++ TLS 指纹同样不是 Node,大概率直连可用**。数据无地理坐标,适合 AI-chat 工具(get_prediction_summary)而非上球。

**金融族(Yahoo/Finnhub/CoinGecko/FRED/ECB/WB/BIS/IMF…)** — 全是结构化时序 JSON,几乎不上球(worldmonitor 也是画在面板/迷你走势图里),但都是极好的 AI-chat 工具素材。Yahoo v8 chart 无 key 这一点(21 处使用)值得记下:EarthExplorer 的 AI 图表卡可以直接喂。

**RSS 聚合(listFeedDigest)** — worldmonitor 最重的自建资产:500+ feed 服务端并发抓取→regex 解析(边缘运行时无 DOM)→关键词分类→Redis 900s。EarthExplorer 不必自建这一套;若要新闻能力,自托管 worldmonitor 后端直接调 `/api/news/v1/...` 更划算(见 §4)。

**Telegram OSINT** — MTProto 有状态、要会话串、有 FLOOD_WAIT/AUTH_KEY_DUPLICATED 等坑(文档写满了教训)。价值高(快过通讯社)但运维重,C++ 端没有好客户端库(tdlib 很重)。不建议直接接;可经 worldmonitor relay 的 `/telegram/feed` 转 JSON。

---

## 3. 对 EarthExplorer 的适配评估

打分:形态=球面可视化形态;AI=chat 工具可分析性(结构化程度/统计价值);成本=接入成本(低=REST+JSON 无 key,可套 quake/flight 点要素模板;中=需 key 或解析复杂;高=需后端聚合/付费/反爬)。

| 数据源 | 球面形态 | AI-chat 可分析性 | 接入成本 | 备注 |
|--------|----------|------------------|----------|------|
| GDACS | 点(带红橙分级色)★★★ | ★★★(类型+级别+国家,量小) | **低**(无 key JSON,套 quake 模板) | 最推荐的下一个层 |
| NASA EONET | 点/多点 ★★★ | ★★★(13 类事件枚举) | **低** | 与 GDACS 合并去重后更好 |
| NOAA NHC 飓风 | **弧线+预报锥面** ★★★ | ★★(季节性) | 低-中(ArcGIS JSON,面要素需新着色) | 球上首个"线/面"天气要素 |
| NWS 天气警报 | 面(仅美国)★ | ★★ | 低 | 覆盖窄,优先级低 |
| NASA FIRMS | 海量点/热力 ★★★ | ★★★(按国家/日统计) | 中(必需 key,CSV 解析,点量大需抽稀) | 用户 memory 已有调研,与 GIBS 火点影像互补 |
| Open-Meteo 气候距平 | 区域标签/色斑 ★★ | ★★★(基线对比数值) | 低(无 key,但要自算 30 天基线) | AI 工具价值>图层价值 |
| NOAA GML CO2 | 不上球 | ★★★(单一时序,讲故事) | 低(纯文本) | 纯 AI 工具 |
| GPSJam | H3 hex 面/热力 ★★★ | ★★★(区域干扰计数) | **低**(每日 GeoJSON 无 key;hex 可退化为中心点用 GL_POINTS) | 强推荐:语义独特 |
| CelesTrak TLE | **轨道线+卫星点+足迹** ★★★★ | ★★(过顶查询有价值) | 中(TLE 免费,需引 SGP4 C++ 库+每帧传播) | 3D 球最出彩的层,worldmonitor 甩不开的平面局限我们没有 |
| AISStream 船舶 | 动点(航向/速度)★★★ | ★★(区域船数/军舰监视) | 中(免费 key,**WebSocket** 新管道;libhv 支持) | 复用 flight 外推平滑 |
| IMF PortWatch | 咽喉点标签+时序 ★★ | ★★★(霍尔木兹/苏伊士流量,好问答) | 低(ArcGIS JSON 无 key) | AI 工具优先 |
| Submarine Cable Map | 线要素 ★★★ | ★(静态) | 低(v3 JSON 无 key;需球面折线渲染) | 静态装饰层,一次拉取 |
| Cloudflare Radar 断网 | 国家高亮/标签 ★★ | ★★★(事件列表) | 高(付费 token) | 缓议 |
| abuse.ch 等 IOC | 点阵 ★★(语义弱) | ★★(计数/类型分布) | 中(免 key 但需 IP 地理二跳) | 装饰性>情报性 |
| ACLED | 点(冲突/抗议)★★★ | ★★★(行为者/死亡数/国家统计) | 中(注册+OAuth 24h 刷新) | 冲突层正源 |
| GDELT GEO | 点+新闻热度 ★★★ | ★★★(国家×主题矩阵) | **低**(无 key,噪声需过滤) | 冲突/抗议层的免费起步源 |
| UCDP | 点 ★★ | ★★(编年质量高但滞后) | 中(token) | 可跳过 |
| OREF 警报 | 点(以色列)★★ | ★★ | **极高**(WAF+住宅代理) | 不建议 |
| USGS 地震 | 已接入 ✅ | 已是 AI 工具 ✅ | — | worldmonitor 同款端点 |
| OpenSky 航班 | 已接入 ✅ | ✅ | —(可加 OAuth2 提配额+军机过滤) | 军机层=纯客户端过滤,零新数据源 |
| Polymarket | 不上球 | ★★★(概率+成交量,话题性强) | 中(无 key;C++ 原生 TLS 大概率绕过 JA3) | 纯 AI 工具候选 |
| Yahoo Finance | 不上球 | ★★★(任意 symbol 时序) | 低(无 key,非官方有失效风险) | AI 图表卡素材 |
| CoinGecko | 不上球 | ★★★ | 低(免费层限速) | 纯 AI 工具 |
| FRED/ECB/WB/BIS/Eurostat/IMF | 不上球(国家着色勉强) | ★★★(国家问答) | 低-中(FRED/IMF 要 key,其余无) | "国家简报"型 AI 工具 |
| UNHCR 流离失所 | **O-D 弧线** ★★★ | ★★★(来源国/收容国排行) | **低**(无 key CC-BY) | 球面弧线首选数据(EarthExplorer 尚无弧线层) |
| UN Comtrade 贸易流 | O-D 弧线 ★★ | ★★★ | 中(限额) | 同上但更重 |
| WHO 疫情通报 | 国家标签点 ★★ | ★★★(文本摘要) | 低(官方 JSON) | 轻量好接 |
| OpenAQ/WAQI | 点 ★★ | ★★★ | 中(key 必需) | 用户 memory 早有调研 |
| EPA RadNet+Safecast | 点 ★★ | ★★ | 低 | 冷门但零门槛 |
| 政府旅行警告 | 国家着色/标签 ★★ | ★★★(分级明确) | 中(24 个 RSS 解析) | AI 工具"这个国家安全吗" |
| RSS 新闻族 | 不上球(热点标签间接) | ★★★★(一切分析的底料) | 高(500 feed 聚合是大工程) | 走 §4 复用路线 |
| Telegram OSINT | 不上球 | ★★★ | 极高(MTProto 有状态) | 只可经 relay 转发 |
| FAA ASWS 机场延误 | 机场点+状态色 ★★ | ★★★ | 低(XML 但简单) | 补全既有 flight 层的"机场维度" |
| Windy webcams | 点+缩略图 ★★ | ★ | 中(key) | ImGui 弹图可行,优先级低 |
| 静态战略数据集(基地/港口/光缆/矿产/数据中心) | 点/线 ★★★ | ★★(静态问答) | **极低**(AGPL 仓库内 JSON 直接搬,注意许可) | 一次性导入即得 6+ 个图层 |
| 恐惧贪婪 2.0(自算) | 不上球 | ★★★ | 中(多源抓取脆) | 可简化为 CNN 端点单源 |
| PizzINT | 不上球 | ★★(话题性) | 低 | 彩蛋工具 |

**推荐梯队**(纯按上表汇总,非设计决定):
- **T0 立即可做(套现有模板,无 key)**:GDACS、EONET、GDELT GEO、GPSJam、UNHCR 弧线、Submarine Cable 静态线、静态战略数据集。
- **T1 值得投入(小新管道)**:CelesTrak+SGP4 轨道层(3D 球杀手锏)、AISStream WS 船舶、FIRMS(key)、ACLED(key)、NHC 飓风锥。
- **T2 AI-chat 工具优先(不上球)**:PortWatch、Polymarket、Yahoo/CoinGecko、FRED/WB 国家简报、WHO 疫情、旅行警告。
- **T3 缓议/不做**:OREF、Telegram 直连、Reddit、Cloudflare Radar(付费)、AviationStack(预算制)、消费者价格自建抓取。

---

## 4. worldmonitor 自身的聚合层(可否当 EarthExplorer 的数据中台)

**有,而且是它最有复用价值的部分。** 三层结构:

1. **Seed 层**(`scripts/seed-*.mjs`,~150 个 + `seed-bundle-*` 合集):每个脚本 = "拉一个上游 → 规范化 → 写 Redis 固定 key(带 envelope:recordCount/sourceVersion/TTL)"。Railway cron 驱动,`scripts/run-seeders.sh` 一键全跑,`SEED_TIMEOUT` 防挂。**上游的所有反爬/限额/重试知识都沉淀在这里**(OREF curl 代理、AviationStack 月度预算 Redis 计数器、ACLED OAuth 自动换发、Polymarket JA3 回退)。
2. **Relay 层**(`scripts/ais-relay.cjs`):有状态长连接专用 — AIS WebSocket 扇出、Telegram MTProto、OREF 轮询、RSS 域名白名单代理(`api/_rss-allowed-domains.js`)、CelesTrak 种子。鉴权靠 `RELAY_SHARED_SECRET` 头。
3. **RPC 层**(`server/worldmonitor/<domain>/v1/*.ts` → `api/<domain>/v1/[rpc].ts`):proto 契约(`proto/`)生成的域网关(`server/gateway.ts` 的 `createDomainGateway`:origin 校验→CORS→key→限流→路由),**只读 Redis、不打上游**,响应是规范化 JSON。34 个域(aviation/climate/conflict/cyber/displacement/economic/infrastructure/intelligence/maritime/market/military/natural/news/prediction/radiation/sanctions/seismology/trade/unrest/webcam/wildfire…)。另有 `api/mcp.ts` — **它本身就带 MCP server**(docs/mcp-tools-reference.mdx),工具化暴露同一批数据。

**SELF_HOSTING.md 结论**:docker compose 全栈自托管完全可行(Redis+REST 代理+relay+SPA 四容器,仅需 3 个自生成密钥),seed 在宿主机 cron 跑,"开箱即用公开数据源,key 解锁增量"。对 EarthExplorer 有两条现实路线:
- **路线 A(自托管中台)**:跑一套 worldmonitor 后端(compose + 选择性 seeders),EarthExplorer 的 libhv 只需轮询 `http://<host>/api/<domain>/v1/<rpc>` 拿规范化 JSON — 一次接入换来几十个源,且把反爬/key 管理全部外包给它。代价:多维护一个 Node/Redis 栈;AGPL-3.0 对自用无碍(不分发其后端就不触发传染)。
- **路线 B(抄配方不抄栈)**:把 seed 脚本当"上游 API 使用说明书"逐个移植成 C++ 模块(端点、参数、过滤、去重、评分公式全都是现成答案),保持 EarthExplorer 零 Node 依赖。适合只要 T0/T1 少数源的场景。

值得单独借鉴的机制(与栈无关):**seed→cache→只读 RPC 的分层**(EarthExplorer 现在每个 worker 直连上游,源多了以后可引入本地磁盘/内存缓存层);**数据新鲜度追踪器 + "情报盲区"显式呈现**(35 源 fresh/stale/no_data,防止假自信);**bootstrap 一次注水**(启动时批量取全部缓存,替代 N 个首轮请求);**上游预算守卫**(Redis 计数器封顶付费调用)。

---

## 5. UI 布局速写(供"世界信息枢纽"重设计参考,仅观察)

- **地图 + 面板网格双主体**:上半是地图(3D 球/WebGL 平面一键切换,56 图层),下半是**自适应多列面板网格**;104 个面板全部继承同一 `Panel` 基类(统一标题栏/刷新徽章/折叠/行列跨度拖拽,布局持久化 localStorage)。信息组织是"**每个数据域一张卡**",而非塞进地图弹窗。
- **变体即预设**:同一代码按 hostname 切 6 套"面板+图层+刷新率+主题"组合(full/tech/finance/…)。对 EarthExplorer 的启发:图层/面板组合可以做成命名预设(mission preset,它也有 `mission-presets.ts`)。
- **多标签页 + 命令面板**:面板可分组到用户自建 tab(`PanelTabBar`/`tab-store`);CMD+K 全局调度(开层、跳面板、`fly LON DXB` 这类命令动词)。
- **信息可信度前置**:每条新闻带 Tier 1-4 源分级与国家附属标记;每层有 LayerExplanation(用途/来源/新鲜度/置信度/局限);顶部"情报盲区"徽章报数据缺口 — **"告诉用户你看不见什么"是它 UI 的核心理念**。
- **热点→详情的钻取链**:地图点击 → 右侧国家简报(CountryBriefPanel:CII 分数、相关新闻、预测市场、基建暴露)→ 深潜面板。地图是索引,面板是正文。
- **轻量渲染纪律**:面板 `setContent` 150ms 防抖;webcam iframe 懒加载+隐藏即销毁;bootstrap 注水保首屏。与 EarthExplorer 的 ImGui "右上角信息面板独立可关"偏好一致的点:它的面板也全部独立、可关、可重排,操作(图层开关)与信息(面板)分离。
- **AI 融入方式**:AI 不是独立聊天窗而已 — 每日简报面板(latest-brief)、ChatAnalystPanel(带全域数据上下文 `chat-analyst-context.ts`)、每层/每信号的 LLM 解释,即"AI 消化所有面板数据后再呈现",与 EarthExplorer 的 ToolRegistry 思路同构,但它把工具上下文做成了**服务端预聚合的 context 文档**而非逐工具调用,值得对照。

---

## 附:证据速查

- 数据源官方文档:`docs/data-sources.mdx`(542 行,与代码同步,含 Provider Credits 全表)
- feed 全集:`server/worldmonitor/news/v1/_feeds.ts`;Telegram 频道:`data/telegram-channels.json`
- 图层注册表:`src/config/map-layer-definitions.ts`(56 层)
- 上游端点最密集处:`scripts/seed-*.mjs`(每个文件头部 const URL)与 `scripts/ais-relay.cjs`
- 自托管:`SELF_HOSTING.md`;架构:`ARCHITECTURE.md`;环境变量总表:`.env.example`(~400 行,每个 key 有注释)
