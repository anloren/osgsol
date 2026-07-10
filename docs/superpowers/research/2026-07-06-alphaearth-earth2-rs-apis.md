# AlphaEarth / NVIDIA Earth-2 / 遥感数据接口 接入条件调研

日期：2026-07-06 · 方法：3 个并行 web 调研 agent，关键事实均附来源 URL，区分已证实/推断。
用途：EarthExplorer「世界信息枢纽」路线图（../specs/2026-07-03-world-info-hub-roadmap.md）的下一阶段（科学数据层）依据。

---

## 1. Google AlphaEarth Foundations（Satellite Embedding）——✅ 可直接接入，且不必碰 Earth Engine

### 数据形态【已证实】
- 全球每个 10m 像素一个 **64 维单位 embedding 向量**（波段 A00–A63，分量 [-1,1]），编码该像素一整年的多传感器时序（Sentinel-2/Landsat 光学、Sentinel-1 雷达、LiDAR、气候）。
- 覆盖全球陆地+浅海，**年度一层，2017–2025 共 9 层**；UTM 分带投影（120 带）。
- 数据集版本 1.1（2025-11-17，AEF 模型 v2.1 重新生成）；无 "V2" 数据集 ID，collection 仍是 `GOOGLE/SATELLITE_EMBEDDING/V1/ANNUAL`。
- 模型权重**不开放**，只有预计算 embedding 数据集（论文 arxiv.org/abs/2507.22291）。

### 三条获取途径【已证实】
| 途径 | 细节 | 费用 |
|---|---|---|
| Earth Engine | `ee.ImageCollection("GOOGLE/SATELLITE_EMBEDDING/V1/ANNUAL")` | EE 配额制（2026-04-27 起非商业也限 150 EECU-h/月） |
| GCS 官方桶 | `gs://alphaearth_foundations`，COG 8192×8192×64 波段 int8 量化，索引 `aef_index.*` | Requester-Pays（egress 自付，需 GCP 项目） |
| **source.coop 镜像（推荐）** | `s3://us-west-2.opendata.source.coop/tge-labs/aef`，匿名免费，2017–2025 全量，含修正方向问题的 .vrt | **零门槛零费用**（非 Google 官方托管） |

- 反量化：`((v/127.5)^2) * sign(v)`（来源：developers.google.com/earth-engine/guides/aef_on_gcs_readme）。
- 许可 **CC-BY 4.0**，仅需署名 "produced by Google and Google DeepMind" → 下载/缓存/可视化/再分发均可，与 EARTH_TILE_CACHE 无冲突（对比：Google 3D Tiles 禁缓存）。
- EE REST `computePixels` 备选（单请求 ≤48MB），但需 GCP 项目且分发型桌面应用商业定性存疑，不推荐做主路。

### 工程路径【推断】
- COG HTTP Range 流式读取（`aef_index.csv` 定位文件 → Range 读 tile 块 → 3 波段 int8 → 反量化 → RGB 上屏），与 libhv/TileManager 模式契合。
- 依赖决策点：引 GDAL（1–2 周）vs 自写最小 COG reader（+1 周）。坑：裸 COG 有方向问题，官方建议经 .vrt 修正。
- RGB 可视化惯例：官方教程用 A01/A16/A09，min -0.3/max 0.3；更好用 PCA 前 3 主成分或 K-means 聚类着色。
- 四大用例：分类 / 回归 / **变化检测**（逐年向量点积）/ **相似度搜索**（点积=余弦相似度）。

核心来源：developers.google.com/earth-engine/datasets/catalog/GOOGLE_SATELLITE_EMBEDDING_V1_ANNUAL · registry.opendata.aws/aef-source/ · source.coop/tge-labs/aef · developers.google.com/earth-engine/guides/noncommercial_tiers

---

## 2. NVIDIA Earth-2 ——❌ 无自助托管 API，不能"直接接入"；平替路线现成

### 事实【已证实】
- FourCastNet/CorrDiff NIM 是**自托管容器**（docker pull → localhost:8000/v1/infer），文档无 build.nvidia.com 托管端点；官方"云 API"= DGX Cloud 企业合同（首客户 The Weather Company），无自助入口。
- 输入形态本身不适合公网 API：`(1,1,72,721,1440)` float32 ≈ 300MB/请求，输出 tar 包 NumPy。
- **2026-01-26（AMS）Earth-2 全家桶开源**：FourCastNet3 权重 Apache 2.0 挂 HF（`nvidia/fourcastnet3`），另有 Atlas（15 天中期）、StormScope（0-6h 临近，仅美国）、HealDA（同化，年内发布）、cBottle（km 级生成式气候）、StormCast；用开源 Earth2Studio（pip）跑——但要自己出 GPU（推理初始场用 GFS/ERA5）。
- 组件速览：FourCastNet3=0.25° 全球中期；CorrDiff=25km→2km 扩散降尺度（台湾/美国版）；StormCast=3km 对流尺度（美国中部）；cBottle=气候数字孪生非逐日预报。

### 平替三档（不自建 GPU 前提下的现实路径）【已证实+推断】
| 路线 | 工程量 | 得到什么 |
|---|---|---|
| **Open-Meteo API**（已含 ECMWF AIFS 模型端点） | 极轻：libhv+picojson 当天可上 | 任意点 AI 预报时序（"点击地球任意点看预报"） |
| **ECMWF AIFS/IFS 开放数据** | 中：GRIB2 下载 + ecCodes 解码 + 场渲染 | 与 Earth-2 同级 SOTA AI 全球预报可视化（0.25°，6h 步长到 15 天，每日 4 次，AWS/Azure/GCP 匿名镜像桶） |
| Earth-2 自托管 | 重：GPU+Python 栈+初始场管道 | km 级降尺度/集合/定制（将来有 GPU 预算再说） |

- 关键背景：**2025-10-01 ECMWF 完全开放数据**（CC-BY 4.0，实时零延迟 0.25°；2026 追加 9km 2 小时延迟免费）。AIFS ENS 2025-07 业务化，2026-05 升级 v2。
- 未证实点：Earth-2 NIM 具体 GPU 门槛（support-matrix 404）；Atlas/StormScope 确切 license 需接入前核对 HF repo。

核心来源：blogs.nvidia.com/blog/nvidia-earth-2-open-models/ · docs.nvidia.com/nim/earth-2/fourcastnet/latest/quickstart-guide.html · huggingface.co/nvidia/fourcastnet3 · www.ecmwf.int/en/forecasts/datasets/open-data · open-meteo.com/en/docs/ecmwf-api

---

## 3. 遥感/地球数据接口全景（C++/libhv 可直接接入优先）

### 梯队 1：零 key 零注册即接【已证实为主】
| 接口 | 形式 | 要点 |
|---|---|---|
| **Element84 Earth Search** | STAC REST（earth-search.aws.element84.com/v1），免注册 | Sentinel-2 L2A **COG 免费桶**（sentinel-cogs）HTTP Range 直读；另有 S1 GRD/Landsat/Copernicus DEM 目录；卫星影像首选 |
| **ECMWF open-data** | 匿名 HTTPS/S3 镜像桶，GRIB2 | AIFS+IFS，见上节；2025-10 全开放是本次调研最大新闻 |
| **GEBCO 2025 海洋测深** | **WMS**（可直接当 overlay 瓦片）+ netCDF/GeoTIFF 下载 | 免费开放，海底地形层最短路径 |
| **Overture Maps** | GeoParquet 开放桶 + **PMTiles**（HTTP range 单文件协议，C++ 实现简单） | 全球建筑/道路/POI，月度发布（最新 2026-06-17.0） |
| **GIBS 现成图层**（管线已在手） | WMTS | NDVI（MODIS/VIIRS 合成）、Black Marble 夜光等，零新基建 |
| Copernicus DEM GLO-30 | AWS 免费桶 COG（copernicus-dem-30m） | 1°×1° 瓦片，免认证 HTTPS 直读 |
| NOAA NOMADS/GFS | HTTP grib filter（按变量/区域裁剪） | 无 key，限 120 req/min；AWS 桶无限流备选 |
| NASA CMR / Worldview Snapshots | REST JSON 检索 / bbox 出静态图 | 查询免认证 |

### 梯队 2：免费注册一次即用【已证实】
| 接口 | 形式 | 配额/要点 |
|---|---|---|
| **CDSE（Copernicus Data Space）** | STAC+OData+S3+**Sentinel Hub WMTS/Process** | 下载 12TB/月；Sentinel Hub 10k requests+10k PU/月——**S5P 污染层（NO2/CH4）与真彩影像的最快路径，复用现有 WMTS 管线**；OAuth token 仅 10 分钟有效需自动续签；旧 STAC 端点 2025-11-17 废弃 |
| **Open-Meteo** | REST JSON 无 key | 非商用 10k 次/天；变量极广（气压层/海浪/空气质量/花粉/集合） |
| NASA AppEEARS | REST 异步任务 | Earthdata Login；点/面时序提取（SMAP 土壤湿度、NDVI、LST），适合"点击出时序" |
| WorldPop | REST JSON→GeoTIFF | 无认证，CC-BY 4.0，~100m 人口栅格 |
| Global Forest Watch | REST JSON/GeoJSON | 毁林告警四合一（GLAD/RADD/DIST-ALERT），key 免费需注册 |
| VIIRS Black Marble 数值版 | LAADS DAAC HDF5 | Earthdata token；视觉版走 GIBS 即可 |
| OSM Overpass | QL POST | 免 key，slot 限流；ODbL 许可注意混用约束 |
| OpenTopography | REST 裁剪 GeoTIFF | 免费 key；**非学术仅 50 次/天**，只适合按需局部取 DEM |

### 梯队 3：谨慎/暂缓【已证实】
- **Google Photorealistic 3D Tiles**：Enterprise SKU，1,000 root tiles 免费/月（$6/千次），ToS 禁缓存（与 EARTH_TILE_CACHE 冲突，与既有记忆一致）。
- **Cesium ion**：Community 免费档 5GB+15GB 流量/月**仅限个人非商用**；含 World Terrain/OSM Buildings；osgdb_3dtiles 接 ion 资产只差 token header。
- Landsat 全分辨率：usgs-landsat 桶 requester-pays；零成本只有 M2M（需申请 MACHINE 权限）或 LandsatLook 浏览图。
- ARCO-ERA5（GCS Zarr 匿名）：数据免费但 **Zarr 对纯 C++ 不友好**（chunk 寻址+blosc），滞后 ~3 月。
- Microsoft Planetary Computer：Hub 已退役（2024-06），STAC API 保留匿名可查+SAS 签名免账号；但 Pro 商业化后公共版长期存续存疑 → 做成与 Earth Search 可互换的 STAC 后端。
- ESA DestinE：Phase 3（2026-06 至 2028-06）确认，免费注册即用（Polytope/Earth Data Hub API），非匿名。

### 公共技术底座（一次投入多源复用）【推断】
1. **STAC 客户端** = REST+JSON = libhv+picojson，与 USGS GeoJSON 管线同构，零新依赖。
2. **COG HTTP Range reader**（GDAL 或自写）→ 同时解锁 Sentinel-2 影像、AlphaEarth、Copernicus DEM。
3. **GRIB2 解码（ecCodes，Apache 2.0 C 库）** → 同时解锁 AIFS/IFS/GFS 全球预报场。
4. **PMTiles reader**（协议简单）→ Overture 建筑/路网。
- 公开 STAC 目录汇总：stacindex.org。

核心来源：documentation.dataspace.copernicus.eu/Quotas.html · element84.com/earth-search · registry.opendata.aws/sentinel-2-l2a-cogs/ · planetarycomputer.microsoft.com/docs/reference/stac/ · cesium.com/platform/cesium-ion/pricing/ · docs.overturemaps.org/getting-data/ · www.gebco.net/data-products/gebco-web-services · portal.opentopography.org/apidocs/ · platform.destine.eu
