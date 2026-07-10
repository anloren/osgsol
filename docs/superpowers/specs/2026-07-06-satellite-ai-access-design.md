# 卫星数据接入 AI 设计规格

日期:2026-07-06。状态:盘点结论 + 范围(仅卫星)已获用户批准("先把卫星接上")。待用户 review 本 spec 后转 plan。

## 0. 背景与盘点结论

用户飞到 ISS 时问 AI,AI 看不见。盘点全部图层 vs AI 工具后确认:**AI 唯一的数据盲区是卫星**。
- 航班/船舶 + 8 个 feed 层 + 5 个战略层都有 `get_<x>_summary` 工具,AI 可读。
- 栅格/瓦片层(卫星影像底图/路网标注/GIBS 云图/降水雷达/香港实景三维)无结构化数据可查,不算盲区(AI 已能用 `set_layer` 开关)。
- **卫星层**(空间站含 ISS 25544/天宫 48274、导航星座、气象、Starlink,4 个图层共用一个 `SatelliteLayer`):`sat_data.cpp` 的 `summaryJson()` 是占位桩恒返回 `{"count":0}`,且 `configureSatelliteLayer` 没接 AI tools → 完全对 AI 不可见。

## 1. 目标

让 AI 能回答关于卫星的问题,尤其:
- "现在天上有多少卫星"、按类别(空间站/导航/气象/Starlink)各多少。
- "ISS 现在在哪"、"天宫在哪"——返回当前经纬度/高度/速度。
- 配合已有的 `fly_to` 工具,"带我飞到 ISS"能用(AI 从 summary 拿到 ISS 经纬度 → fly_to)。

**红线:只做"数据暴露"**——做实 `summaryJson()` + 注册一个 AI 工具。不碰卫星的 TLE 抓取、SGP4 传播、渲染、拾取、线程、缓存。

## 2. 设计

### 2.1 做实 `SatelliteLayerImpl::summaryJson()`(sat_data.cpp)

现状 `virtual std::string summaryJson() const { return "{\"count\":0}"; }`。改为遍历 `_allPrecise`(空间站/导航/气象)+ `_allStarlink`,产出 picojson:
```json
{
  "count": <总数>,
  "byCategory": { "station": N1, "navigation": N2, "weather": N3, "starlink": N4 },
  "iss":     { "found": true, "name": "...", "latDeg": .., "lonDeg": .., "altKm": .., "speedKmS": .. },
  "tiangong":{ "found": true, "name": "...", "latDeg": .., "lonDeg": .., "altKm": .., "speedKmS": .. },
  "note": "各类别计数只反映已开启并抓取到的类目;Starlink 需单独开启"
}
```
- ISS(noradId==25544)/天宫(noradId==48274)在 `_allPrecise` 里查(它们属 Station 类目);找不到(类目未开启/未抓到)则 `{"found": false}`。
- 计数只统计已加载的卫星(未开启的类目自然为 0),与 flights/ships 的"当前已加载"语义一致。
- **线程约定**:`summaryJson()` 在主线程读(AI 的 `drainMainThread` 是主线程 FRAME handler),`_allPrecise`/`_allStarlink` 也只在主线程 `syncIfDirty()` 写——同 flight_data.cpp `summaryJson` 的既有约定,不需加锁,注释写明。

### 2.2 注册 `get_satellites_summary` 工具(earth_main.cpp)

在 `get_ships_summary` 注册块附近(需要 `aiRuntime.tools` 与 `satelliteLayer`),照 ships 同款模式注册:
```cpp
if (aiRuntime.tools && satelliteLayer)
{
    earthai::Tool t; t.name = "get_satellites_summary";
    t.description = u8"查询当前卫星汇总:总数、按类别(空间站/导航星座/气象/Starlink)分布,"
        u8"以及 ISS(国际空间站)和天宫空间站的当前经纬度/高度/速度。"
        u8"若查不到 ISS/天宫位置,多为空间站类目尚未开启或数据加载中,本工具会自动开启该类目,稍后再查一次即可。"
        u8"要飞到 ISS/天宫,取本工具返回的经纬度再调用 fly_to。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
    SatelliteLayer* sat = satelliteLayer;
    t.execute = [sat](const picojson::value&) {
        // 自动开启空间站类目(ISS/天宫在此),使"飞到ISS"在未手动开图层时也能用;
        // 抓取是异步网络,首次可能 count/found 为 0/false,附 loadingNote(同 feed 工具惯例)。
        if (!sat->isCategoryEnabled(SatCategory::Station))
            sat->setCategoryEnabled(SatCategory::Station, true);
        picojson::value v; std::string err = picojson::parse(v, sat->summaryJson());
        if (!err.empty() || !v.is<picojson::object>())
        { picojson::object e; e["error"] = picojson::value("bad summary json"); return picojson::value(e); }
        picojson::object& obj = v.get<picojson::object>();
        bool issFound = obj.count("iss") && obj["iss"].is<picojson::object>()
                        && obj["iss"].get("found").evaluate_as_boolean();
        if (!issFound)
            obj["loadingNote"] = picojson::value(std::string(u8"空间站类目已自动开启,卫星数据抓取中,请稍后再查询一次"));
        return picojson::value(obj);
    };
    aiRuntime.tools->add(t);
}
```
- 自动开启 Station 类目(ISS/天宫所在,数据量小),让"飞到 ISS"在用户没手动开卫星图层时也能用;不强制开 Nav/Weather/Starlink(Starlink ~7000 点,不该被一句 AI 查询拉起来)。
- 首次查询数据未到 → `loadingNote` 提示稍后再查(同 feed 工具的 loadingNote 先例)。

## 3. 明确不做(范围外)

- 不做单星按名/编号查询、不做"离视野中心最近的卫星"(用户选了推荐档=汇总+ISS/天宫定位,更深的单星查询是下一档,本次不做)。
- 不碰卫星 TLE 抓取/SGP4/渲染/拾取/线程/缓存(红线)。
- 不给栅格层(降水/云图等)做 AI 读取(它们是瓦片图无结构化数据;盘点已确认不算盲区)。
- 不改 `configureSatelliteLayer` 签名(工具在 earth_main 注册,读已有的 `SatelliteLayer*`,同 ships 先例)。

## 4. 测试计划

1. **单测**:`SatelliteLayerImpl` 在匿名命名空间,tests/satellite_tests.cpp 已 #include sat_data 相关。给 summaryJson 加纯逻辑单测——构造几颗 Satellite(含 noradId 25544/48274)喂进 `_allPrecise`,断言 JSON 的 count/byCategory/iss.found/iss.latDeg 正确。(若 SatelliteLayerImpl 不便直接构造,退而测一个抽出的纯函数 `satellitesSummaryJson(const std::vector<Satellite>& precise, const std::vector<Satellite>& starlink)`,summaryJson() 调它——更可测,推荐此拆法。)
2. **离屏 E2E**:`EARTH_OFFSCREEN=1` + `EARTH_SATS=1` + 真实 CelesTrak 网络,跑一会儿让 stations 抓到,加临时诊断或用 AI fixture 触发 `get_satellites_summary`,确认返回真实 ISS 经纬度(与独立 curl CelesTrak + SGP4 或公开 ISS 位置源对比合理)。
3. **AI fixture(离线)**:若 AI 有 fake provider fixture 体系(EARTH_AI_FAKE),加一条脚本触发 get_satellites_summary + fly_to,验证工具链通(工具注册、返回 JSON、AI 能据此 fly_to)。
4. **回归**:5 个单测 exit 0;卫星层渲染/拾取不受影响(summaryJson 是只读新增)。
5. **真机**:用户问 AI"ISS 在哪"/"带我飞到 ISS",确认能答+能飞。
