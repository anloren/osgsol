# 卫星接入 AI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 AI 能查卫星——总数、按类别分布、ISS/天宫当前位置,并借已有 fly_to 实现"带我飞到 ISS"。

**Architecture:** summary 逻辑抽成 `earthsat::buildSatelliteSummaryJson()` 纯函数(satellite_tests 可直测);`SatelliteLayerImpl::summaryJson()` 把 `_allPrecise`+`_allStarlink` 转成纯函数的输入并调它;earth_main 注册 `get_satellites_summary` AI 工具(自动开启空间站类目)。

**Tech Stack:** C++14、picojson、earthai::ToolRegistry、OSG。

**Spec:** `docs/superpowers/specs/2026-07-06-satellite-ai-access-design.md`。

## Global Constraints

- **红线:只做数据暴露**——做实 summaryJson + 抽纯函数 + 注册 AI 工具 + 一个只读诊断钩子。禁止改动:卫星 TLE 抓取(FetchThread/fetchGroupTextNetwork)、SGP4 传播(propagateOne/repropagate)、渲染(buildSatGeode/shader/前半球剔除)、拾取(pickAt)、线程模型、缓存(satCacheDir)、类目开关的既有逻辑(只允许 AI 工具内调用已有的 setCategoryEnabled 公开接口)。
- category 整数约定(与 `SatCategory` 枚举序一致,逐字):0=空间站(Station)/1=导航(Navigation)/2=气象(Weather)/3=Starlink。
- 特殊 NORAD:ISS=25544、天宫=48274。
- 线程约定:`summaryJson()` 在主线程读(AI drainMainThread 是主线程 FRAME handler),`_allPrecise`/`_allStarlink` 只在主线程 `syncIfDirty()` 写——同 flight_data.cpp summaryJson 既有约定,不加锁,注释写明。
- 构建:`cmake --build /Users/USER/osgverse/build/verse_core --target install`(禁止并发第二个构建)。测试二进制在 `/Users/USER/osgverse/build/verse_core/bin/`。
- 任何运行 `osgVerse_EarthExplorer` **必须** `EARTH_OFFSCREEN=1`;交付打包后不再运行 dist app(破坏签名),功能验证用 `build/sdk_core/bin/` 二进制。
- 提交信息末尾:`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`。中文注释解释 why。

---

### Task 1: `earthsat::buildSatelliteSummaryJson` 纯函数 + 单测

**Files:**
- Modify: `applications/earth_explorer/sat_math.h`(加 SatSummaryEntry 结构 + 函数声明)
- Modify: `applications/earth_explorer/sat_math.cpp`(实现 + `#include <picojson.h>`)
- Modify: `tests/satellite_tests.cpp`(单测 + `#include <picojson.h>`)

**Interfaces:**
- Produces(Task 2 消费,逐字):
```cpp
namespace earthsat {
    struct SatSummaryEntry {
        int noradId = 0;
        int category = 0;   // 0=station,1=nav,2=weather,3=starlink(同 SatCategory 枚举序)
        double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
    };
    // 生成 get_satellites_summary 的 JSON:count / byCategory / iss(25544) / tiangong(48274) / note。
    std::string buildSatelliteSummaryJson(const std::vector<SatSummaryEntry>& sats);
}
```

- [ ] **Step 1: 写失败单测**(satellite_tests.cpp:include 区加 `#include <picojson.h>`;在 main() 末尾 `return 0;` 之前加测试块)

```cpp
    // ---- satellite-AI Task 1:buildSatelliteSummaryJson 纯函数 ----
    {
        using namespace earthsat;
        std::vector<SatSummaryEntry> sats;
        SatSummaryEntry a; a.noradId = 25544; a.category = 0;
        a.latDeg = 22.0; a.lonDeg = 114.0; a.altKm = 420.0; a.speedKmS = 7.66; sats.push_back(a);
        SatSummaryEntry b; b.noradId = 48274; b.category = 0;
        b.latDeg = -10.0; b.lonDeg = 50.0; b.altKm = 390.0; b.speedKmS = 7.68; sats.push_back(b);
        SatSummaryEntry c; c.noradId = 11111; c.category = 1; sats.push_back(c);   // nav
        SatSummaryEntry d; d.noradId = 22222; d.category = 3; sats.push_back(d);   // starlink
        std::string js = buildSatelliteSummaryJson(sats);
        picojson::value v; std::string err = picojson::parse(v, js); CHECK(err.empty());
        picojson::object& o = v.get<picojson::object>();
        CHECK(o["count"].get<double>() == 4.0);
        CHECK(o["byCategory"].get("station").get<double>() == 2.0);
        CHECK(o["byCategory"].get("navigation").get<double>() == 1.0);
        CHECK(o["byCategory"].get("weather").get<double>() == 0.0);
        CHECK(o["byCategory"].get("starlink").get<double>() == 1.0);
        CHECK(o["iss"].get("found").get<bool>() == true);
        CHECK(std::fabs(o["iss"].get("latDeg").get<double>() - 22.0) < 1e-9);
        CHECK(std::fabs(o["iss"].get("lonDeg").get<double>() - 114.0) < 1e-9);
        CHECK(o["iss"].get("noradId").get<double>() == 25544.0);
        CHECK(o["tiangong"].get("found").get<bool>() == true);
        CHECK(std::fabs(o["tiangong"].get("altKm").get<double>() - 390.0) < 1e-9);
        // 空输入 → count 0、iss/tiangong found=false
        std::string js2 = buildSatelliteSummaryJson(std::vector<SatSummaryEntry>());
        picojson::value v2; CHECK(picojson::parse(v2, js2).empty());
        CHECK(v2.get("count").get<double>() == 0.0);
        CHECK(v2.get("iss").get("found").get<bool>() == false);
        CHECK(v2.get("tiangong").get("found").get<bool>() == false);
        std::cout << "[OK] buildSatelliteSummaryJson\n";
    }
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Satellite 2>&1 | tail -5`
Expected: 编译错误 `buildSatelliteSummaryJson` / `SatSummaryEntry` 未声明。

- [ ] **Step 3: 实现**

`sat_math.h`,在文件末尾 `}` (namespace earthsat 结束) 之前、`buildFootprintVertices` 声明之后加:
```cpp
    // ===== 卫星汇总(供 AI get_satellites_summary)=====
    // 最小输入结构(不依赖 sat_data 的 SatCategory 枚举,category 用 int,枚举序一致:
    // 0=空间站 1=导航 2=气象 3=Starlink)。抽成纯函数便于单测。
    struct SatSummaryEntry
    {
        int noradId = 0;
        int category = 0;
        double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
    };
    // 生成汇总 JSON:{count, byCategory:{station,navigation,weather,starlink},
    // iss/tiangong:{found, [noradId,latDeg,lonDeg,altKm,speedKmS]}, note}。
    // iss=NORAD 25544、tiangong=NORAD 48274;找不到则该对象 {found:false}。
    std::string buildSatelliteSummaryJson(const std::vector<SatSummaryEntry>& sats);
```
(`sat_math.h` 顶部需要 `#include <string>` 与 `#include <vector>`——已有 `std::vector<osg::Vec3d>` 返回类型,vector/string 应已可用;若缺 string 则补。)

`sat_math.cpp`:include 区加 `#include <picojson.h>`;在 namespace earthsat 内(文件末尾函数之后)加:
```cpp
    // 一颗特殊卫星(ISS/天宫)的位置对象;p 为空 → {found:false}。
    static picojson::value oneSat(const SatSummaryEntry* p)
    {
        picojson::object o;
        if (!p) { o["found"] = picojson::value(false); return picojson::value(o); }
        o["found"]    = picojson::value(true);
        o["noradId"]  = picojson::value((double)p->noradId);
        o["latDeg"]   = picojson::value(p->latDeg);
        o["lonDeg"]   = picojson::value(p->lonDeg);
        o["altKm"]    = picojson::value(p->altKm);
        o["speedKmS"] = picojson::value(p->speedKmS);
        return picojson::value(o);
    }

    std::string buildSatelliteSummaryJson(const std::vector<SatSummaryEntry>& sats)
    {
        int cat[4] = { 0, 0, 0, 0 };
        const SatSummaryEntry* iss = 0;
        const SatSummaryEntry* css = 0;
        for (size_t i = 0; i < sats.size(); ++i)
        {
            int c = sats[i].category;
            if (c >= 0 && c < 4) cat[c]++;
            if (sats[i].noradId == 25544) iss = &sats[i];
            else if (sats[i].noradId == 48274) css = &sats[i];
        }
        picojson::object r;
        r["count"] = picojson::value((double)sats.size());
        picojson::object bc;
        bc["station"]    = picojson::value((double)cat[0]);
        bc["navigation"] = picojson::value((double)cat[1]);
        bc["weather"]    = picojson::value((double)cat[2]);
        bc["starlink"]   = picojson::value((double)cat[3]);
        r["byCategory"] = picojson::value(bc);
        r["iss"]      = oneSat(iss);
        r["tiangong"] = oneSat(css);
        r["note"] = picojson::value(std::string(
            u8"各类别计数只反映已开启并抓取到的类目;Starlink 需单独开启"));
        return picojson::value(r).serialize();
    }
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Satellite && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Satellite; echo exit=$?`
Expected: 输出含 `[OK] buildSatelliteSummaryJson`,exit=0。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/sat_math.h applications/earth_explorer/sat_math.cpp tests/satellite_tests.cpp
git commit -m "feat(earth): earthsat::buildSatelliteSummaryJson pure function + unit test (sat-ai T1)"
```

---

### Task 2: summaryJson 接入 + 注册 get_satellites_summary + 验证/打包/HANDOFF

**Files:**
- Modify: `applications/earth_explorer/sat_data.cpp`(实现 summaryJson)
- Modify: `applications/earth_explorer/earth_main.cpp`(注册 AI 工具 + 只读诊断钩子)
- Modify: `HANDOFF.md`

**Interfaces:**
- Consumes: Task 1 的 `earthsat::SatSummaryEntry` / `earthsat::buildSatelliteSummaryJson`。

- [ ] **Step 1: 实现 sat_data.cpp summaryJson**

把 `virtual std::string summaryJson() const { return "{\"count\":0}"; }` 替换为:
```cpp
        // 主线程读(AI drainMainThread 是主线程 FRAME handler);_allPrecise/_allStarlink
        // 也只在主线程 syncIfDirty() 写——同 flight_data.cpp summaryJson 约定,不加锁。
        // 只做只读汇总,不碰抓取/传播/渲染(红线)。
        virtual std::string summaryJson() const
        {
            std::vector<earthsat::SatSummaryEntry> entries;
            entries.reserve(_allPrecise.size() + _allStarlink.size());
            for (size_t i = 0; i < _allPrecise.size(); ++i)
            {
                earthsat::SatSummaryEntry e;
                e.noradId = _allPrecise[i].noradId; e.category = (int)_allPrecise[i].category;
                e.latDeg = _allPrecise[i].latDeg;   e.lonDeg = _allPrecise[i].lonDeg;
                e.altKm = _allPrecise[i].altKm;     e.speedKmS = _allPrecise[i].speedKmS;
                entries.push_back(e);
            }
            for (size_t i = 0; i < _allStarlink.size(); ++i)
            {
                earthsat::SatSummaryEntry e;
                e.noradId = _allStarlink[i].noradId; e.category = (int)_allStarlink[i].category;
                e.latDeg = _allStarlink[i].latDeg;   e.lonDeg = _allStarlink[i].lonDeg;
                e.altKm = _allStarlink[i].altKm;     e.speedKmS = _allStarlink[i].speedKmS;
                entries.push_back(e);
            }
            return earthsat::buildSatelliteSummaryJson(entries);
        }
```
(sat_data.cpp 已 `#include "sat_math.h"`——它用 earthsat::propagateOne 等,无需新增 include。`SatCategory` 枚举序 Station/Navigation/Weather/Starlink = 0/1/2/3,与 SatSummaryEntry.category 约定一致,`(int)` 转换正确。)

- [ ] **Step 2: earth_main.cpp 注册 get_satellites_summary**

在 `get_ships_summary` 注册块(约 earth_main.cpp:1059-1074)之后插入:
```cpp
    if (aiRuntime.tools && satelliteLayer)
    {
        earthai::Tool t; t.name = "get_satellites_summary";
        t.description = u8"查询卫星汇总:总数、按类别(空间站/导航星座/气象/Starlink)分布,"
            u8"以及 ISS(国际空间站,NORAD 25544)和天宫空间站(NORAD 48274)的当前经纬度/高度/速度。"
            u8"若 iss/tiangong 的 found 为 false,多为空间站类目尚未开启或数据加载中——本工具会自动开启该类目,"
            u8"看到 loadingNote 时稍后再查一次即可。要飞到 ISS/天宫,取返回的 latDeg/lonDeg 再调用 fly_to。";
        t.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        SatelliteLayer* sat = satelliteLayer;
        t.execute = [sat](const picojson::value&) {
            // 自动开启空间站类目(ISS/天宫在此,数据量小),让"飞到ISS"在未手动开图层时也能用;
            // 不强制开 Nav/Weather/Starlink(Starlink ~7000 点,不该被一句 AI 查询拉起)。
            if (!sat->isCategoryEnabled(SatCategory::Station))
                sat->setCategoryEnabled(SatCategory::Station, true);
            picojson::value v; std::string err = picojson::parse(v, sat->summaryJson());
            if (!err.empty() || !v.is<picojson::object>())
            { picojson::object e; e["error"] = picojson::value("bad summary json"); return picojson::value(e); }
            picojson::object& obj = v.get<picojson::object>();
            bool issFound = obj.count("iss") && obj["iss"].is<picojson::object>()
                            && obj["iss"].get("found").is<bool>() && obj["iss"].get("found").get<bool>();
            if (!issFound)
                obj["loadingNote"] = picojson::value(std::string(
                    u8"空间站类目已自动开启,卫星数据抓取中,请稍后再查询一次"));
            OSG_NOTICE << "[AIChat] get_satellites_summary count="
                       << (obj.count("count") ? (long long)obj["count"].get<double>() : 0) << std::endl;
            return picojson::value(obj);
        };
        aiRuntime.tools->add(t);
    }
```
(SatCategory 来自 sat_data.h,earth_main 已 include;OSG_NOTICE 已用于本文件。)

- [ ] **Step 3: 只读诊断钩子**(离屏拿真实 ISS 位置验证用;env 未设时零开销)

`SatFetchStatusHandler`(earth_main.cpp,已每 FRAME 跑、已持 `_sat` 指针)的 `handle()` FRAME 分支里,在末尾 `return false;` 之前加:
```cpp
        // 只读诊断:EARTH_SAT_SUMMARY_DEBUG 设置时,每 ~120 帧打印一次卫星汇总 JSON——
        // 离屏 + 真实 CelesTrak 网络下用它验证 summaryJson 里的 ISS/天宫是真实抓取位置。
        static const bool s_sumDbg = (getenv("EARTH_SAT_SUMMARY_DEBUG") != nullptr);
        if (s_sumDbg)
        {
            static int s_fr = 0;
            if ((s_fr++ % 120) == 0)
                std::cout << "[SatSummary] " << _sat->summaryJson() << std::endl;
        }
```
(`SatFetchStatusHandler` 的成员指针名以实际代码为准——grep `class SatFetchStatusHandler` 确认字段名,brief 里推测是 `_sat`;若不同则用实际名。它已 include `<cstdlib>`/`<iostream>` 语义;若缺则补。)

- [ ] **Step 4: 全量构建 + 离屏真实网络验证**

```bash
cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | tail -3
cd /Users/USER/osgverse/build/sdk_core/bin
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=1500 EARTH_FRAME_SLEEP_MS=25 EARTH_SATS=1 EARTH_SAT_SUMMARY_DEBUG=1 \
  ./osgVerse_EarthExplorer --goto 20 100 20000 2>&1 | grep -iE "\[Sat\] Parsed|\[SatSummary\]" | tail -6
```
Expected:日志出现 `[Sat] Parsed N satellites (... stations ...)`(含 ISS 25544/天宫 48274)后,`[SatSummary]` 行打印真实 JSON:`count`>0、`byCategory.station`>0、`iss.found=true` 且 `iss.latDeg/lonDeg` 是合理的地球坐标(|lat|≤90、|lon|≤180),`tiangong.found=true`。把 ISS 经纬度与公开 ISS 追踪(如 wheretheiss.at 的粗略位置,或独立 curl CelesTrak stations TLE + `sat_math::propagateOne` 手算)对比同一时刻量级合理即通过。

- [ ] **Step 5: 单测 + 回归**

```bash
cd /Users/USER/osgverse/build/verse_core/bin
for t in osgVerse_Test_Satellite osgVerse_Test_Feeds osgVerse_Test_Ais osgVerse_Test_Ai_Chat osgVerse_Test_TileOverlay; do
  ./$t > /tmp/$t.log 2>&1; echo "$t exit=$?"; done
```
5 个全 exit=0。卫星渲染/拾取不受影响(summaryJson 只读新增)——不必额外截图,但可选跑一张 `EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 EARTH_SATS=1 --goto 20 100 20000` 确认卫星仍正常显示。

- [ ] **Step 6: 打包 + 签名**

```bash
bash /Users/USER/osgverse/packaging/package_macos.sh && codesign -v /Users/USER/osgverse/dist/EarthExplorer.app && echo SIGN_OK
```

- [ ] **Step 7: HANDOFF.md 顶部新增章节**(卫星接入 AI 完成;get_satellites_summary 工具+做实 summaryJson;抽 earthsat::buildSatelliteSummaryJson 纯函数;自动开 Station 类目使"飞到ISS"可用;EARTH_SAT_SUMMARY_DEBUG 诊断钩子;红线只做数据暴露;真机待验=用户问 AI"ISS在哪"/"带我飞到ISS";移除上一章节"最新"标记)

- [ ] **Step 8: Commit**

```bash
git add applications/earth_explorer/sat_data.cpp applications/earth_explorer/earth_main.cpp HANDOFF.md
git commit -m "feat(earth): get_satellites_summary AI tool + real summaryJson (ISS/Tiangong locate) — fills the AI satellite blind spot (sat-ai T2)"
```

---

## 真机验证(交付后用户执行)

用户在 AI 聊天里问"现在天上有多少卫星"、"ISS 在哪"、"带我飞到 ISS",确认:AI 调 get_satellites_summary 拿到真实数据作答,并能据 ISS 经纬度调 fly_to 飞过去。
