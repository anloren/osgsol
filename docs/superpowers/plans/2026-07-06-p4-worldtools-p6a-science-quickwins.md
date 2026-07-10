# P4「AI 情报分析师」+ P6a「科学速赢包」Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> ## ⚠️ 执行前必读:v0.19 对账更新（2026-07-06 二次修订）
> 本计划初稿基线是"P3 收尾后 master";之后项目推进到 **v0.19**（commit `b82570ec`），做了三件计划没预料、恰好碰同批文件的事:**① 标记系统重设计**（新增 `marker_style.h/.cpp` 单一真源，重写了 feed_layer 点精灵着色器 + 给 registerFeedLayer 加 icon 接线）、**② 卫星接入 AI**（`get_satellites_summary`）、**③ key 磁盘持久化 + AIS 闸门放宽**。完整对账见 `docs/superpowers/2026-07-06-PROJECT-STATUS-for-P4-session.md`（**必读**）。本轮已把下列失效点直接改进各 Task:
> - **计划里的行号一律不可信**——feed_layer.cpp / earth_main.cpp 大改，插入点全变，实现者一律读当前文件现场定位（各 Task 已把"锚点符号"而非行号写清）。
> - **必修链接坑（Task 6）**:任何 `#include feed_layer.cpp` 的测试单元，必须照抄 `tests/feed_layer_tests.cpp` 的完整 include 清单（含 `marker_style.cpp` + `earth_config.cpp` + 9 个 `feeds/*.cpp`），否则 `earthmark::`/`earthcfg::` 符号链接失败。Task 6 Step 1 已给出确切清单。
> - **新增源文件后必须先 `cmake /Users/USER/osgverse/build/verse_core` 重配**再 build，否则新 .cpp 不进构建、静默用旧二进制（本项目踩过）。Task 2 Step 5 已含此步。
> - earth_main 的 world-tools 接线放在 `get_ships_summary`/`get_satellites_summary` **手动注册块附近**（现约 1059-1090 行区间，符号定位），与既有独立图层工具风格一致。Task 2 Step 5 已改。

**Goal:** 给 AI 补上"跨源情报合成"能力（异步查询基建 + 5 个免 key 世界数据工具 + get_region_brief），并用零新依赖的方式上 3 个科学栅格层（NDVI 植被 / 夜光 / GEBCO 海底地形）。

**Architecture:** 工具 execute 在主线程（ai_tools.h 线程契约），不能阻塞网络 → 新建 `AsyncJsonFetcher`（后台 worker + URL 缓存 + pending 状态机），工具首调返回 `{"pending":true}`、模型稍后重调命中缓存——与 FeedLayer「懒开启 + loadingNote」同一交互范式。区域简报走 feed_layer 新增的 `RegionBriefProvider` 注册表（registerFeedLayer 自动挂上，所有既有源免费获得）。科学栅格层全部复用 OVERLAY 单槽（互斥逻辑从 clouds/precip 两两手写泛化为组表）。

**Tech Stack:** C++14、OSG、libhv（requests，已在 osgVerseDependency）、picojson、OpenThreads。

**Spec:** `docs/superpowers/specs/2026-07-03-world-info-hub-roadmap.md` §P4 + `docs/superpowers/research/2026-07-06-alphaearth-earth2-rs-apis.md`（数据源接入条件，必读）。

**范围取舍（记录在案）:** roadmap §P4 里 WHO 疫情 / 旅行警告 / Yahoo 行情 / FRED 四个工具本期不做（WHO、旅行警告数据形态弱，Yahoo 非官方接口有失效风险，FRED 需 key）；每个都是 ~30 行套 Task 2 模板，后续按需补。底部快捷 chips（「今日简报」等，roadmap §3）也顺延——等 region brief 工具真机跑顺后再做 UI 入口，避免给未验证的能力先立门面。P6b（COG+AlphaEarth）/P6c（GRIB2+AIFS）见文末附录，各自另出 plan。

## Global Constraints

- **基线:v0.19（commit `b82570ec`）之后的 master**（P0-P3 + 标记系统 + 卫星 AI 均已合并，工作树干净）。执行前 `git log --oneline -3` 应见 v0.19 附近提交。
- **新增源文件后必须先 `cmake /Users/USER/osgverse/build/verse_core` 重配再构建**（否则新 .cpp 不进构建、静默用旧二进制——本项目已踩过）。
- **任何 `#include feed_layer.cpp` 的测试单元必须同时 include `marker_style.cpp` + `earth_config.cpp` + 全部 `feeds/*.cpp`**（照抄 `tests/feed_layer_tests.cpp` 现行 include 清单），否则链接失败。
- 不碰 globe 着色器;不碰 `osgDB::Options`/`osg::PagedLOD`/`DatabasePager`。
- 后台线程与主线程只经 mutex+dirty/缓存交接;工具 execute 一律主线程、绝不阻塞网络。
- 任何运行 `osgVerse_EarthExplorer` 的验证**必须**带 `EARTH_OFFSCREEN=1`（用户铁律，绝不弹真实窗口）。
- **单测零网络**：一律走 fixture 文件或注入 fake fetch 函数;真实 URL 只在「curl 冒烟」步骤里手动敲。
- 运行期对外请求失败必须优雅降级为 `{"error":...}` JSON，绝不 crash、绝不重试风暴（TTL 内不重复入队）。
- env 钩子名（不得改名）:`EARTH_WEATHER_FILE` / `EARTH_CRYPTO_FILE` / `EARTH_WB_FILE` / `EARTH_PORTWATCH_FILE` / `EARTH_POLYMARKET_FILE` / `EARTH_NDVI` / `EARTH_NIGHTLIGHTS` / `EARTH_GEBCO`。
- 构建:`cmake --build /Users/USER/osgverse/build/verse_core --target install`（**禁止**并发第二个构建）。测试二进制在 `/Users/USER/osgverse/build/verse_core/bin/`。
- OSG_WARN/OSG_NOTICE 是带 if 的宏，if/else 分支必须加大括号（一天踩过两次的坑）。
- 提交信息末尾加 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`。
- 中文注释风格与既有文件一致（解释 why，不复述 what）。
- 新图层署名（subtitle）必须带数据来源（CC-BY/使用条款要求）。

---

### Task 1: AsyncJsonFetcher 异步查询基建

**Files:**
- Create: `applications/earth_explorer/ai_query.h`
- Create: `applications/earth_explorer/ai_query.cpp`
- Create: `tests/world_tools_tests.cpp`
- Modify: `tests/CMakeLists.txt`（NEW_TEST 注册，加在 `NEW_TEST(osgVerse_Test_Feeds feed_layer_tests.cpp)` 之后）

**Interfaces:**
- Produces（Task 2-6 依赖，签名逐字）:
```cpp
namespace earthai
{
    enum class QueryState { Fetching, Ready, Failed };
    class AsyncJsonFetcher
    {
    public:
        typedef std::function<bool(const std::string& url, std::string& bodyOut,
                                   std::string& errOut)> FetchFn;
        explicit AsyncJsonFetcher(FetchFn fn = FetchFn());   // 空=缺省 libhv GET(15s 超时)
        ~AsyncJsonFetcher();
        QueryState query(const std::string& url, double ttlSeconds,
                         const std::string& fixturePath, std::string& bodyOut, std::string& errOut);
        void waitIdleForTest();
    };
}
```

- [ ] **Step 1: 写失败单测**（新建 `tests/world_tools_tests.cpp`）

```cpp
// P4 世界情报工具族离线单测:AsyncJsonFetcher 状态机 + 工具 dispatch(fixture 驱动,零网络)。
// 沿用 feed_layer_tests.cpp 先例:被测 .cpp 直接 #include 进本翻译单元。
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <cstdio>
#include "../applications/earth_explorer/ai_query.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)

#ifdef _WIN32
static void setEnvVar(const char* k, const char* v) { _putenv_s(k, v); }
static void unsetEnvVar(const char* k) { _putenv_s(k, ""); }
#else
static void setEnvVar(const char* k, const char* v) { setenv(k, v, 1); }
static void unsetEnvVar(const char* k) { unsetenv(k); }
#endif

// ===== Task 1:AsyncJsonFetcher 状态机 =====
static void testFetcherFixtureSync()
{
    // fixture 路径非空 → 同步读文件立即 Ready,不走网络
    const char* kPath = "aiq_fixture_tmp.json";
    { std::ofstream f(kPath); f << "{\"x\":1}"; }
    earthai::AsyncJsonFetcher fx;   // 缺省 fetch 不会被调用
    std::string body, err;
    CHECK(fx.query("http://ignored", 60.0, kPath, body, err) == earthai::QueryState::Ready);
    CHECK(body == "{\"x\":1}");
    // fixture 文件缺失 → Failed(err 说明路径)
    CHECK(fx.query("http://ignored", 60.0, "no_such_file.json", body, err)
          == earthai::QueryState::Failed);
    CHECK(err.find("no_such_file") != std::string::npos);
    std::remove(kPath);
    std::cout << "[OK] fetcher fixture sync\n";
}

static void testFetcherAsyncFlow()
{
    // 注入 fake fetch:首调 Fetching(入队),worker 抓完后再调命中 Ready
    earthai::AsyncJsonFetcher fx([](const std::string& url, std::string& b, std::string& e) {
        if (url.find("bad") != std::string::npos) { e = "boom"; return false; }
        b = "{\"ok\":true}"; return true;
    });
    std::string body, err;
    CHECK(fx.query("http://good", 3600.0, "", body, err) == earthai::QueryState::Fetching);
    fx.waitIdleForTest();
    CHECK(fx.query("http://good", 3600.0, "", body, err) == earthai::QueryState::Ready);
    CHECK(body == "{\"ok\":true}");
    // TTL 内重复调用不再入队(仍 Ready、body 相同)
    CHECK(fx.query("http://good", 3600.0, "", body, err) == earthai::QueryState::Ready);
    // 失败源:Fetching → Failed(消费一次即弃) → 下次重试回 Fetching
    CHECK(fx.query("http://bad", 3600.0, "", body, err) == earthai::QueryState::Fetching);
    fx.waitIdleForTest();
    CHECK(fx.query("http://bad", 3600.0, "", body, err) == earthai::QueryState::Failed);
    CHECK(err.find("boom") != std::string::npos);
    CHECK(fx.query("http://bad", 3600.0, "", body, err) == earthai::QueryState::Fetching);
    fx.waitIdleForTest();
    // TTL=0 → 命中即过期,重新入队
    CHECK(fx.query("http://good", 0.0, "", body, err) == earthai::QueryState::Fetching);
    fx.waitIdleForTest();
    std::cout << "[OK] fetcher async flow\n";
}

int main(int, char**)
{
    testFetcherFixtureSync();
    testFetcherAsyncFlow();
    std::cout << "ALL WORLD-TOOLS TESTS PASSED\n";
    return 0;
}
```

`tests/CMakeLists.txt` 在最后一个 earth 相关测试 `NEW_TEST(osgVerse_Test_Ais ais_tests.cpp)`（现 132 行，Satellite 之后）行后追加（现状:Ai_Chat/Feeds/TileOverlay/Satellite/Ais 五个，本行成为第六个）:

```cmake
NEW_TEST(osgVerse_Test_WorldTools world_tools_tests.cpp)  # P4 world-query tools: async fetcher / tool dispatch offline tests
```

- [ ] **Step 2: 重配 + 跑测试确认失败**

新增了 CMake target 与源文件 → 先重配让 `osgVerse_Test_WorldTools` target 出现，再构建:

Run: `cmake /Users/USER/osgverse/build/verse_core > /dev/null 2>&1 && cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools 2>&1 | tail -5`
Expected: 编译错误（world_tools_tests.cpp include 不到尚未创建的 `ai_query.cpp`）。

- [ ] **Step 3: 实现 ai_query.h / ai_query.cpp**

`ai_query.h`:

```cpp
#pragma once
// P4:AI 工具用的异步 JSON 抓取器。工具 execute 跑在主线程(ai_tools.h 线程契约),不能
// 阻塞网络 → 首次调用登记抓取任务立即返回 Fetching,后台 worker 抓完落缓存,模型稍后
// 重调同一工具命中 Ready——与 FeedLayer「懒开启+loadingNote」同一交互范式。
// 失败结果消费一次即弃(下次调用重试),TTL 内绝不重复入队(限流友好)。
#include <OpenThreads/Thread>
#include <OpenThreads/Mutex>
#include <OpenThreads/ScopedLock>
#include <atomic>
#include <deque>
#include <map>
#include <string>
#include <functional>

namespace earthai
{
    enum class QueryState { Fetching, Ready, Failed };

    class AsyncJsonFetcher
    {
    public:
        typedef std::function<bool(const std::string& url, std::string& bodyOut,
                                   std::string& errOut)> FetchFn;
        explicit AsyncJsonFetcher(FetchFn fn = FetchFn());   // 空=缺省 libhv 同步 GET(15s,worker 内调)
        ~AsyncJsonFetcher();

        // 主线程调用。fixturePath 非空 → 同步读本地文件(离线/单测),不走网络。
        // Ready:bodyOut 有效;Fetching:已入队或抓取中;Failed:errOut 有效。
        QueryState query(const std::string& url, double ttlSeconds,
                         const std::string& fixturePath, std::string& bodyOut, std::string& errOut);
        void waitIdleForTest();   // 单测辅助:轮询等队列清空,封顶 5s

    private:
        struct Entry { std::string body, err; double fetchedAt = 0.0; bool ok = false; bool inflight = false; };
        class Worker;
        FetchFn _fetch;
        OpenThreads::Mutex _mutex;
        std::map<std::string, Entry> _cache;
        std::deque<std::string> _queue;
        Worker* _worker;
        std::atomic<bool> _done;
        friend class Worker;
    };
}
```

`ai_query.cpp`:

```cpp
#include "ai_query.h"
#include "3rdparty/libhv/all/client/requests.h"
#include <fstream>
#include <sstream>
#include <time.h>

namespace earthai
{

class AsyncJsonFetcher::Worker : public OpenThreads::Thread
{
public:
    Worker(AsyncJsonFetcher* o) : _owner(o) {}
    virtual void run()
    {
        while (!_owner->_done)
        {
            std::string url;
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_owner->_mutex);
                if (!_owner->_queue.empty())
                { url = _owner->_queue.front(); _owner->_queue.pop_front(); }
            }
            if (url.empty()) { OpenThreads::Thread::microSleep(100 * 1000); continue; }
            std::string body, err;
            bool ok = _owner->_fetch(url, body, err);
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_owner->_mutex);
                Entry& e = _owner->_cache[url];
                e.ok = ok; e.body = ok ? body : std::string();
                e.err = ok ? std::string() : (err.empty() ? std::string("fetch failed") : err);
                e.fetchedAt = (double)time(NULL); e.inflight = false;
            }
        }
    }
    AsyncJsonFetcher* _owner;
};

static bool defaultHttpGet(const std::string& url, std::string& bodyOut, std::string& errOut)
{
    requests::Request req(new HttpRequest);
    req->method = HTTP_GET; req->url = url; req->timeout = 15;
    requests::Response resp = requests::request(req);
    if (resp && resp->status_code == 200) { bodyOut = resp->body; return true; }
    std::ostringstream oss; oss << "status=" << (resp ? (int)resp->status_code : -1);
    errOut = oss.str(); return false;
}

AsyncJsonFetcher::AsyncJsonFetcher(FetchFn fn)
    : _fetch(fn ? fn : FetchFn(defaultHttpGet)), _worker(NULL), _done(false)
{ _worker = new Worker(this); _worker->start(); }

AsyncJsonFetcher::~AsyncJsonFetcher()
{ _done = true; if (_worker) { _worker->join(); delete _worker; _worker = NULL; } }

QueryState AsyncJsonFetcher::query(const std::string& url, double ttlSeconds,
    const std::string& fixturePath, std::string& bodyOut, std::string& errOut)
{
    if (!fixturePath.empty())
    {
        std::ifstream fin(fixturePath.c_str());
        if (!fin) { errOut = "fixture not found: " + fixturePath; return QueryState::Failed; }
        std::stringstream ss; ss << fin.rdbuf(); bodyOut = ss.str();
        return QueryState::Ready;
    }
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
    std::map<std::string, Entry>::iterator it = _cache.find(url);
    if (it != _cache.end())
    {
        Entry& e = it->second;
        if (e.inflight) return QueryState::Fetching;
        if (!e.ok)   // 失败结果消费一次即弃 → 下次调用重试
        { errOut = e.err; _cache.erase(it); return QueryState::Failed; }
        double age = (double)time(NULL) - e.fetchedAt;
        if (age < ttlSeconds) { bodyOut = e.body; return QueryState::Ready; }
        // ok 但过期 → 走下方重新入队(旧 body 弃用,保持语义简单)
    }
    Entry& e = _cache[url]; e.inflight = true;
    _queue.push_back(url);
    return QueryState::Fetching;
}

void AsyncJsonFetcher::waitIdleForTest()
{
    for (int i = 0; i < 50; ++i)   // 100ms × 50 = 5s 封顶
    {
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
            bool busy = !_queue.empty();
            for (std::map<std::string, Entry>::iterator it = _cache.begin();
                 !busy && it != _cache.end(); ++it)
                if (it->second.inflight) busy = true;
            if (!busy) return;
        }
        OpenThreads::Thread::microSleep(100 * 1000);
    }
}

}
```

注:`requests.h` 的 include 路径以 `feed_layer.cpp` 现有写法为准（若它用的是别的相对路径，照抄它的）。

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools 2>&1 | tail -3 && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_WorldTools`
Expected: `ALL WORLD-TOOLS TESTS PASSED`

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/ai_query.h applications/earth_explorer/ai_query.cpp tests/world_tools_tests.cpp tests/CMakeLists.txt
git commit -m "feat(earth): AsyncJsonFetcher — AI 工具异步查询基建,pending/缓存/fixture 状态机 (P4 T1)"
```

---

### Task 2: get_weather_forecast 工具（Open-Meteo）+ 工具族骨架 + 主程序接线

**Files:**
- Create: `applications/earth_explorer/ai_world_tools.h`
- Create: `applications/earth_explorer/ai_world_tools.cpp`
- Modify: `applications/earth_explorer/earth_main.cpp`（get_ships_summary/get_satellites_summary 手动注册块附近接线，见 Step 5）
- Modify: `applications/earth_explorer/CMakeLists.txt`（EXECUTABLE_FILES 加 `ai_query.cpp ai_world_tools.cpp`;现状该列表已含 earth_config.cpp/marker_style.cpp/ais_math.cpp/ais_data.cpp，照现状追加）
- Modify: `tests/world_tools_tests.cpp`（追加 include 与用例）

**Interfaces:**
- Produces（Task 3/4/6 在同一文件内追加工具;earth_main 依赖，签名逐字）:
```cpp
// ai_world_tools.h
void registerWorldQueryTools(earthai::ToolRegistry* tools, LayerManager* layers,
                             earthai::AsyncJsonFetcher* fetcher);
```
- ai_world_tools.cpp 内部 helper（Task 3/4/6 复用，签名逐字）:
```cpp
static picojson::value toolFetchJson(earthai::AsyncJsonFetcher& f, const std::string& url,
                                     double ttlSeconds, const char* fixtureEnvName);
```

- [ ] **Step 1: 写失败单测**（`tests/world_tools_tests.cpp` 顶部追加 include，main 前追加用例，main 里调用）

include 区追加（`ai_query.cpp` include 之后）:

```cpp
#include "../applications/earth_explorer/ai_world_tools.cpp"
```

用例:

```cpp
// ===== Task 2:get_weather_forecast(fixture 驱动) =====
static void testWeatherTool()
{
    const char* kFix = "aiq_weather_tmp.json";
    { std::ofstream f(kFix);
      f << "{\"current\":{\"temperature_2m\":25.3,\"wind_speed_10m\":3.2},"
           "\"daily\":{\"temperature_2m_max\":[30,31,29,28,27]}}"; }
    setEnvVar("EARTH_WEATHER_FILE", kFix);
    earthai::AsyncJsonFetcher fx;
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{\"lat\":25.0,\"lon\":102.7}").empty());
    CHECK(reg.dispatch("get_weather_forecast", args, out));
    CHECK(out.is<picojson::object>());
    CHECK(out.get("current").get("temperature_2m").get<double>() == 25.3);
    // 参数缺失 → error
    picojson::value bad; CHECK(picojson::parse(bad, "{}").empty());
    reg.dispatch("get_weather_forecast", bad, out);
    CHECK(out.contains("error"));
    unsetEnvVar("EARTH_WEATHER_FILE");
    std::remove(kFix);
    std::cout << "[OK] weather tool\n";
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools 2>&1 | tail -5`
Expected: 编译错误 `ai_world_tools.cpp` 不存在。

- [ ] **Step 3: 实现 ai_world_tools.h / ai_world_tools.cpp**

`ai_world_tools.h`:

```cpp
#pragma once
// P4:世界情报查询工具族(不上球、纯 AI 工具)。全部经 AsyncJsonFetcher 异步抓取:
// 首调返回 {"pending":true},模型稍后重调命中缓存——描述文案里已教模型这么做。
#include "LayerManager.h"
#include "ai_tools.h"
namespace earthai { class AsyncJsonFetcher; }

void registerWorldQueryTools(earthai::ToolRegistry* tools, LayerManager* layers,
                             earthai::AsyncJsonFetcher* fetcher);
```

`ai_world_tools.cpp`（本 Task 先落 helper + weather 一个工具，后续 Task 在同文件追加）:

```cpp
#include "ai_world_tools.h"
#include "ai_query.h"
#include <osg/Notify>
#include <cstdio>
#include <cstdlib>

// 统一抓取→JSON 包装:Fetching → {"pending":true,note};Failed → {"error":...};
// Ready → 解析后的 JSON 原值(顶层 array 也原样返回,调用方自行判形)。
static picojson::value toolFetchJson(earthai::AsyncJsonFetcher& f, const std::string& url,
                                     double ttlSeconds, const char* fixtureEnvName)
{
    const char* fx = fixtureEnvName ? getenv(fixtureEnvName) : NULL;
    std::string body, err;
    earthai::QueryState st = f.query(url, ttlSeconds,
        (fx && *fx) ? std::string(fx) : std::string(), body, err);
    picojson::object r;
    if (st == earthai::QueryState::Fetching)
    {
        r["pending"] = picojson::value(true);
        r["note"] = picojson::value(std::string(u8"数据抓取中,请稍等片刻后再次调用本工具"));
        return picojson::value(r);
    }
    if (st == earthai::QueryState::Failed)
    { r["error"] = picojson::value("fetch failed: " + err); return picojson::value(r); }
    picojson::value v; std::string perr = picojson::parse(v, body);
    if (!perr.empty())
    { r["error"] = picojson::value("bad json: " + perr); return picojson::value(r); }
    return v;
}

static bool getNum(const picojson::value& a, const char* k, double& out)
{
    if (!a.is<picojson::object>() || !a.contains(k) || !a.get(k).is<double>()) return false;
    out = a.get(k).get<double>(); return true;
}

static picojson::value argError(const char* msg)
{ picojson::object e; e["error"] = picojson::value(std::string(msg)); return picojson::value(e); }

// ---- get_weather_forecast(Open-Meteo,免 key;经度纬度 2 位小数取整提高缓存命中) ----
static void registerWeatherTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_weather_forecast";
    t.description = u8"查询任意经纬度的实况天气与未来5天预报(Open-Meteo,含 ECMWF AI 模式)。"
        u8"返回 current(气温/风速/降水)与 daily(逐日最高最低温/降水量)。"
        u8"若返回 pending=true 表示数据抓取中,请稍候片刻后用相同参数再次调用本工具。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"lat\":{\"type\":\"number\"},\"lon\":{\"type\":\"number\"}},"
        "\"required\":[\"lat\",\"lon\"]}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        double lat = 0, lon = 0;
        if (!getNum(a, "lat", lat) || !getNum(a, "lon", lon))
            return argError("need number lat/lon");
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180)
            return argError("out of range: lat[-90,90] lon[-180,180]");
        char url[512];
        snprintf(url, sizeof(url),
            "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
            "&current=temperature_2m,wind_speed_10m,precipitation,weather_code"
            "&daily=temperature_2m_max,temperature_2m_min,precipitation_sum,weather_code"
            "&forecast_days=5&timezone=auto", lat, lon);
        OSG_NOTICE << "[AIChat] get_weather_forecast " << lat << "," << lon << std::endl;
        return toolFetchJson(*f, url, 900.0, "EARTH_WEATHER_FILE");
    };
    tools->add(t);
}

void registerWorldQueryTools(earthai::ToolRegistry* tools, LayerManager* layers,
                             earthai::AsyncJsonFetcher* fetcher)
{
    (void)layers;   // Task 6(get_region_brief)开始使用
    if (!tools || !fetcher) return;
    registerWeatherTool(tools, fetcher);
}
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools 2>&1 | tail -3 && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_WorldTools`
Expected: `ALL WORLD-TOOLS TESTS PASSED`

- [ ] **Step 5: 主程序接线 + 全量构建**

`applications/earth_explorer/CMakeLists.txt` 的 `SET(EXECUTABLE_FILES ...)` 列表加 `ai_query.cpp ai_world_tools.cpp`（现状:该列表把 `ai_chat.cpp ai_setup.cpp ai_ui.cpp ai_cards.cpp ai_media.cpp` 放同一行，把两新文件追加到这行末尾即可）。

`earth_main.cpp` include 区加 `#include "ai_world_tools.h"` 和 `#include "ai_query.h"`。接线点:**放在既有独立图层 AI 工具手动注册块附近**（现约 earth_main.cpp:1059-1090，即 `get_ships_summary`/`get_satellites_summary` 那种 `earthai::Tool t; ...; aiRuntime.tools->add(t);` 块之后；符号定位，别信行号），与那批"独立图层→手动注册工具"风格一致。此处 `aiRuntime.tools` 与 `layerMgr` 均已在作用域内:

```cpp
    // P4:世界情报查询工具族(纯 AI 工具,不上球)。fetcher 静态存活至进程退出,
    // 析构时 join worker;工具注册零成本(无 AI key 时不会被调用,同 registry 既有约定)。
    static earthai::AsyncJsonFetcher worldQueryFetcher;
    registerWorldQueryTools(aiRuntime.tools, &layerMgr, &worldQueryFetcher);
```

新增了 `ai_query.cpp`/`ai_world_tools.cpp` 两个源文件 → **先重配再构建**:

Run: `cmake /Users/USER/osgverse/build/verse_core > /dev/null 2>&1 && cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | tail -3`
Expected: 编译链接零错误（若跳过重配，新 .cpp 不进构建、静默沿用旧二进制）。

- [ ] **Step 6: Commit**

```bash
git add applications/earth_explorer/ai_world_tools.h applications/earth_explorer/ai_world_tools.cpp \
        applications/earth_explorer/earth_main.cpp applications/earth_explorer/CMakeLists.txt \
        tests/world_tools_tests.cpp
git commit -m "feat(earth): get_weather_forecast 工具(Open-Meteo)+世界工具族骨架接线 (P4 T2)"
```

---

### Task 3: get_crypto_prices + get_country_indicator 工具（CoinGecko / World Bank）

**Files:**
- Modify: `applications/earth_explorer/ai_world_tools.cpp`（追加两个 register 函数并在 registerWorldQueryTools 调用）
- Modify: `tests/world_tools_tests.cpp`（追加用例）

**Interfaces:**
- Consumes: Task 2 的 `toolFetchJson` / `argError` helper。
- Produces: 工具 `get_crypto_prices(ids?)`、`get_country_indicator(country, indicator)`。

- [ ] **Step 1: 写失败单测**（追加，main 里调用）

```cpp
// ===== Task 3:行情 + 宏观指标(fixture 驱动) =====
static void testCryptoAndWorldBankTools()
{
    earthai::AsyncJsonFetcher fx;
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    // CoinGecko simple/price 形状:{"bitcoin":{"usd":97000,"usd_24h_change":-1.2}}
    const char* kC = "aiq_crypto_tmp.json";
    { std::ofstream f(kC); f << "{\"bitcoin\":{\"usd\":97000.0,\"usd_24h_change\":-1.2}}"; }
    setEnvVar("EARTH_CRYPTO_FILE", kC);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{}").empty());   // ids 缺省 bitcoin,ethereum
    CHECK(reg.dispatch("get_crypto_prices", args, out));
    CHECK(out.get("bitcoin").get("usd").get<double>() == 97000.0);
    unsetEnvVar("EARTH_CRYPTO_FILE"); std::remove(kC);
    // World Bank 顶层是数组 [meta, rows] → 工具需转成 {country,indicator,points:[{date,value}]}
    const char* kW = "aiq_wb_tmp.json";
    { std::ofstream f(kW);
      f << "[{\"page\":1},[{\"date\":\"2024\",\"value\":18.53},{\"date\":\"2023\",\"value\":17.79}]]"; }
    setEnvVar("EARTH_WB_FILE", kW);
    CHECK(picojson::parse(args, "{\"country\":\"CN\",\"indicator\":\"NY.GDP.MKTP.CD\"}").empty());
    CHECK(reg.dispatch("get_country_indicator", args, out));
    CHECK(out.get("country").get<std::string>() == "CN");
    CHECK(out.get("points").get<picojson::array>().size() == 2);
    CHECK(out.get("points").get<picojson::array>()[0].get("date").get<std::string>() == "2024");
    // 缺参 → error
    picojson::value bad; CHECK(picojson::parse(bad, "{\"country\":\"CN\"}").empty());
    reg.dispatch("get_country_indicator", bad, out);
    CHECK(out.contains("error"));
    unsetEnvVar("EARTH_WB_FILE"); std::remove(kW);
    std::cout << "[OK] crypto + worldbank tools\n";
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools 2>&1 | tail -5`
Expected: dispatch 返回 false（工具未注册）→ CHECK 失败 abort（或编译期无错、运行期失败均算失败）。

- [ ] **Step 3: 实现两个工具**（ai_world_tools.cpp 追加;registerWorldQueryTools 里调用两个新 register）

```cpp
// ---- get_crypto_prices(CoinGecko 免 key;免费层限速 → TTL 120s) ----
static void registerCryptoTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_crypto_prices";
    t.description = u8"查询加密货币现价与24小时涨跌(CoinGecko)。ids 为逗号分隔的 CoinGecko id,"
        u8"如 bitcoin,ethereum,solana;缺省 bitcoin,ethereum。"
        u8"若返回 pending=true,请稍候片刻后用相同参数再次调用。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"ids\":{\"type\":\"string\",\"description\":\"comma separated coingecko ids\"}}}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        std::string ids = "bitcoin,ethereum";
        if (a.is<picojson::object>() && a.contains("ids") && a.get("ids").is<std::string>()
            && !a.get("ids").get<std::string>().empty())
            ids = a.get("ids").get<std::string>();
        if (ids.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789,-") != std::string::npos)
            return argError("ids: lowercase coingecko ids, comma separated");
        std::string url = "https://api.coingecko.com/api/v3/simple/price?ids=" + ids +
                          "&vs_currencies=usd&include_24hr_change=true";
        OSG_NOTICE << "[AIChat] get_crypto_prices " << ids << std::endl;
        return toolFetchJson(*f, url, 120.0, "EARTH_CRYPTO_FILE");
    };
    tools->add(t);
}

// ---- get_country_indicator(World Bank v2 免 key;顶层数组 [meta, rows] → 转规整对象) ----
static void registerWorldBankTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_country_indicator";
    t.description = u8"查询某国宏观指标最近几年数值(世界银行开放数据,免key,年度)。"
        u8"country 为 ISO2 国家码(CN/US/JP...);indicator 为世行指标码,常用:"
        u8"NY.GDP.MKTP.CD=GDP现价美元, SP.POP.TOTL=总人口, FP.CPI.TOTL.ZG=CPI年通胀%, "
        u8"SL.UEM.TOTL.ZS=失业率%, NE.EXP.GNFS.ZS=出口占GDP%。"
        u8"若返回 pending=true,请稍候片刻后用相同参数再次调用。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"country\":{\"type\":\"string\"},\"indicator\":{\"type\":\"string\"}},"
        "\"required\":[\"country\",\"indicator\"]}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        if (!a.is<picojson::object>() || !a.contains("country") || !a.get("country").is<std::string>()
            || !a.contains("indicator") || !a.get("indicator").is<std::string>())
            return argError("need string country(ISO2) and indicator(code)");
        std::string cc = a.get("country").get<std::string>();
        std::string ind = a.get("indicator").get<std::string>();
        if (cc.size() < 2 || cc.size() > 3
            || cc.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz") != std::string::npos)
            return argError("country: ISO2/ISO3 letters only");
        if (ind.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.") != std::string::npos)
            return argError("indicator: worldbank code like NY.GDP.MKTP.CD");
        std::string url = "https://api.worldbank.org/v2/country/" + cc + "/indicator/" + ind +
                          "?format=json&mrnev=3&per_page=3";
        picojson::value v = toolFetchJson(*f, url, 86400.0, "EARTH_WB_FILE");
        if (v.is<picojson::object>()) return v;   // pending / error 原样透传
        picojson::object r;
        if (!v.is<picojson::array>() || v.get<picojson::array>().size() < 2
            || !v.get<picojson::array>()[1].is<picojson::array>())
        { r["error"] = picojson::value("unexpected worldbank payload"); return picojson::value(r); }
        const picojson::array& rows = v.get<picojson::array>()[1].get<picojson::array>();
        picojson::array pts;
        for (size_t i = 0; i < rows.size(); ++i)
        {
            if (!rows[i].is<picojson::object>()) continue;
            picojson::object p;
            p["date"] = picojson::value(rows[i].contains("date") ? rows[i].get("date").to_str()
                                                                 : std::string());
            p["value"] = (rows[i].contains("value") && rows[i].get("value").is<double>())
                ? picojson::value(rows[i].get("value").get<double>()) : picojson::value();
            pts.push_back(picojson::value(p));
        }
        r["country"] = picojson::value(cc); r["indicator"] = picojson::value(ind);
        r["points"] = picojson::value(pts);
        OSG_NOTICE << "[AIChat] get_country_indicator " << cc << " " << ind
                   << " n=" << pts.size() << std::endl;
        return picojson::value(r);
    };
    tools->add(t);
}
```

registerWorldQueryTools 末尾追加:

```cpp
    registerCryptoTool(tools, fetcher);
    registerWorldBankTool(tools, fetcher);
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools 2>&1 | tail -3 && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_WorldTools`
Expected: `ALL WORLD-TOOLS TESTS PASSED`

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/ai_world_tools.cpp tests/world_tools_tests.cpp
git commit -m "feat(earth): get_crypto_prices + get_country_indicator 工具(CoinGecko/世行) (P4 T3)"
```

---

### Task 4: get_chokepoint_traffic + get_prediction_markets 工具（IMF PortWatch / Polymarket）

**Files:**
- Modify: `applications/earth_explorer/ai_world_tools.cpp`
- Modify: `tests/world_tools_tests.cpp`

**Interfaces:**
- Consumes: Task 2 helper。
- Produces: 工具 `get_chokepoint_traffic()`、`get_prediction_markets(limit?)`。

- [ ] **Step 0: curl 冒烟确认真实端点与字段**（一次性，不进代码）

```bash
curl -s "https://services9.arcgis.com/weJ1QsnbMYJlCHdG/arcgis/rest/services?f=json" | head -c 2000
```
在返回的服务列表里找 PortWatch 的每日咽喉点服务名（调研证据指向 `Daily_Chokepoints_Data`，见 docs/superpowers/research/2026-07-03-worldmonitor-analysis.md 第 20 条）。再:

```bash
curl -s "https://services9.arcgis.com/weJ1QsnbMYJlCHdG/arcgis/rest/services/Daily_Chokepoints_Data/FeatureServer/0/query?where=1%3D1&outFields=*&orderByFields=date%20DESC&resultRecordCount=3&f=json" | head -c 2000
curl -s "https://gamma-api.polymarket.com/markets?closed=false&order=volumeNum&ascending=false&limit=2" | head -c 2000
```

记录真实字段名（PortWatch 预期含 portname/date/n_total 之类;Polymarket 预期含 question/outcomes/outcomePrices/volumeNum）。**若与下方代码假设不符，同步修正代码与 fixture;fixture 必须从真实响应截取录制**（存 `applications/earth_explorer/test/` 下，供手动离线复验;单测内联 fixture 保持自包含）。Polymarket 若被 Cloudflare 拦（非 200），记录结论——工具保留、运行期优雅降级为 error JSON。

- [ ] **Step 1: 写失败单测**（追加，main 里调用）

```cpp
// ===== Task 4:PortWatch 咽喉点 + Polymarket(fixture 驱动) =====
static void testChokepointAndPredictionTools()
{
    earthai::AsyncJsonFetcher fx;
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    // ArcGIS 形状:{"features":[{"attributes":{...}}]};工具取每个咽喉点最新一条
    const char* kP = "aiq_portwatch_tmp.json";
    { std::ofstream f(kP);
      f << "{\"features\":["
           "{\"attributes\":{\"portname\":\"Suez Canal\",\"date\":1751500800000,\"n_total\":52}},"
           "{\"attributes\":{\"portname\":\"Strait of Hormuz\",\"date\":1751500800000,\"n_total\":88}},"
           "{\"attributes\":{\"portname\":\"Suez Canal\",\"date\":1751414400000,\"n_total\":49}}]}"; }
    setEnvVar("EARTH_PORTWATCH_FILE", kP);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{}").empty());
    CHECK(reg.dispatch("get_chokepoint_traffic", args, out));
    const picojson::array& cps = out.get("chokepoints").get<picojson::array>();
    CHECK(cps.size() == 2);   // 每咽喉点只留最新一条
    unsetEnvVar("EARTH_PORTWATCH_FILE"); std::remove(kP);
    // Polymarket 形状:顶层数组;outcomes/outcomePrices 是「字符串化的 JSON 数组」需二次解析
    const char* kM = "aiq_poly_tmp.json";
    { std::ofstream f(kM);
      f << "[{\"question\":\"X happens in 2026?\",\"volumeNum\":123456.7,"
           "\"outcomes\":\"[\\\"Yes\\\",\\\"No\\\"]\","
           "\"outcomePrices\":\"[\\\"0.34\\\",\\\"0.66\\\"]\"}]"; }
    setEnvVar("EARTH_POLYMARKET_FILE", kM);
    CHECK(reg.dispatch("get_prediction_markets", args, out));
    const picojson::array& ms = out.get("markets").get<picojson::array>();
    CHECK(ms.size() == 1);
    CHECK(ms[0].get("question").get<std::string>() == "X happens in 2026?");
    CHECK(ms[0].get("outcomes").get<picojson::array>()[0].get("prob").get<double>() == 0.34);
    unsetEnvVar("EARTH_POLYMARKET_FILE"); std::remove(kM);
    std::cout << "[OK] chokepoint + prediction tools\n";
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools 2>&1 | tail -3 && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_WorldTools`
Expected: dispatch 返回 false → CHECK abort。

- [ ] **Step 3: 实现两个工具**（字段常量集中放顶部，便于按冒烟结果一处修正）

```cpp
// ---- get_chokepoint_traffic(IMF PortWatch,ArcGIS FeatureServer 免 key) ----
// 字段名/服务路径以 Task 4 Step 0 冒烟结果为准,集中在此处修正:
static const char* kPortWatchUrl =
    "https://services9.arcgis.com/weJ1QsnbMYJlCHdG/arcgis/rest/services/"
    "Daily_Chokepoints_Data/FeatureServer/0/query"
    "?where=1%3D1&outFields=*&orderByFields=date%20DESC&resultRecordCount=120&f=json";
static const char* kPwName = "portname";
static const char* kPwDate = "date";
static const char* kPwCount = "n_total";

static void registerChokepointTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_chokepoint_traffic";
    t.description = u8"查询全球海运咽喉点(苏伊士/霍尔木兹/巴拿马/马六甲等)最新每日船流量"
        u8"(IMF PortWatch)。返回每个咽喉点的最新一条记录。"
        u8"若返回 pending=true,请稍候片刻后再次调用。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value&) {
        picojson::value v = toolFetchJson(*f, kPortWatchUrl, 3600.0, "EARTH_PORTWATCH_FILE");
        if (!v.is<picojson::object>() || v.contains("pending") || v.contains("error")) return v;
        picojson::object r;
        if (!v.contains("features") || !v.get("features").is<picojson::array>())
        { r["error"] = picojson::value("unexpected portwatch payload"); return picojson::value(r); }
        const picojson::array& fs = v.get("features").get<picojson::array>();
        // 结果按 date DESC,首见即该咽喉点最新一条
        std::map<std::string, picojson::value> latest;
        picojson::array out;
        for (size_t i = 0; i < fs.size(); ++i)
        {
            if (!fs[i].is<picojson::object>() || !fs[i].contains("attributes")) continue;
            const picojson::value& at = fs[i].get("attributes");
            if (!at.is<picojson::object>() || !at.contains(kPwName)) continue;
            std::string name = at.get(kPwName).to_str();
            if (latest.count(name)) continue;
            picojson::object c;
            c["name"] = picojson::value(name);
            if (at.contains(kPwDate)) c["date"] = at.get(kPwDate);
            if (at.contains(kPwCount)) c["dailyTransits"] = at.get(kPwCount);
            latest[name] = picojson::value(c);
            out.push_back(picojson::value(c));
        }
        r["chokepoints"] = picojson::value(out);
        OSG_NOTICE << "[AIChat] get_chokepoint_traffic n=" << out.size() << std::endl;
        return picojson::value(r);
    };
    tools->add(t);
}

// ---- get_prediction_markets(Polymarket Gamma 免 key;Cloudflare JA3 可能拦 C++ TLS,
//      拦了就 error JSON 优雅降级——worldmonitor 证据:桌面原生 TLS 大概率可过) ----
static void registerPredictionTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_prediction_markets";
    t.description = u8"查询 Polymarket 预测市场当前成交量最高的开放合约(问题+各结果概率+成交量),"
        u8"多为地缘政治/宏观事件,适合回答『市场认为X概率多大』。limit 缺省 12。"
        u8"若返回 pending=true 请稍候重调;若返回 error 说明源被网络策略拦截,如实告知用户即可。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"limit\":{\"type\":\"number\",\"description\":\"1-25, default 12\"}}}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        int limit = 12; double lv = 0;
        if (getNum(a, "limit", lv) && lv >= 1 && lv <= 25) limit = (int)lv;
        char url[256];
        snprintf(url, sizeof(url),
            "https://gamma-api.polymarket.com/markets?closed=false"
            "&order=volumeNum&ascending=false&limit=%d", limit);
        picojson::value v = toolFetchJson(*f, url, 300.0, "EARTH_POLYMARKET_FILE");
        if (v.is<picojson::object>()) return v;   // pending / error
        picojson::object r;
        if (!v.is<picojson::array>())
        { r["error"] = picojson::value("unexpected polymarket payload"); return picojson::value(r); }
        const picojson::array& arr = v.get<picojson::array>();
        picojson::array ms;
        for (size_t i = 0; i < arr.size(); ++i)
        {
            if (!arr[i].is<picojson::object>()) continue;
            picojson::object m;
            m["question"] = picojson::value(arr[i].contains("question")
                ? arr[i].get("question").to_str() : std::string());
            if (arr[i].contains("volumeNum") && arr[i].get("volumeNum").is<double>())
                m["volumeUsd"] = arr[i].get("volumeNum");
            // outcomes/outcomePrices 是字符串化 JSON 数组 → 二次解析后配对
            picojson::value ov, pv2; picojson::array outs;
            std::string oe = arr[i].contains("outcomes")
                ? picojson::parse(ov, arr[i].get("outcomes").to_str()) : std::string("x");
            std::string pe = arr[i].contains("outcomePrices")
                ? picojson::parse(pv2, arr[i].get("outcomePrices").to_str()) : std::string("x");
            if (oe.empty() && pe.empty() && ov.is<picojson::array>() && pv2.is<picojson::array>())
            {
                const picojson::array& oa = ov.get<picojson::array>();
                const picojson::array& pa = pv2.get<picojson::array>();
                for (size_t k = 0; k < oa.size() && k < pa.size(); ++k)
                {
                    picojson::object o;
                    o["name"] = picojson::value(oa[k].to_str());
                    o["prob"] = picojson::value(atof(pa[k].to_str().c_str()));
                    outs.push_back(picojson::value(o));
                }
            }
            m["outcomes"] = picojson::value(outs);
            ms.push_back(picojson::value(m));
        }
        r["markets"] = picojson::value(ms);
        OSG_NOTICE << "[AIChat] get_prediction_markets n=" << ms.size() << std::endl;
        return picojson::value(r);
    };
    tools->add(t);
}
```

registerWorldQueryTools 末尾追加两行调用。注意 `#include <map>`、`#include <cstring>` 如缺补上。

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools 2>&1 | tail -3 && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_WorldTools`
Expected: `ALL WORLD-TOOLS TESTS PASSED`

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/ai_world_tools.cpp tests/world_tools_tests.cpp
git commit -m "feat(earth): get_chokepoint_traffic + get_prediction_markets 工具(PortWatch/Polymarket) (P4 T4)"
```

---

### Task 5: FeedLayer 区域查询基建（RegionBriefProvider 注册表）

> ⚠️ **v0.19 对账**:feed_layer.h/.cpp 自初稿以来被**标记系统 + cluster LOD 大改**:`FeedLayerImpl` 现有 `_shapeId`、cluster 多级 `_levelGeodes`/`_levelRecords`、着色器里有前半球剔除段（红线勿动）+ 形状库拼接。下方的所有"在 X 之后插入"一律**读当前文件现场定位**（`FeedSelection` 结构仍在，可作锚点，但其前后代码已变）。`regionSummaryJson` 遍历点集时用**原始全量点**（`_records` 或 `pointsOnly()`，与 `summaryJson` 同一来源），**不要**用 cluster 聚合后的 `_levelRecords`（区域查询要全量、不要按相机高度抽稀）。

**Files:**
- Modify: `applications/earth_explorer/feed_layer.h`（FeedSelection 之后追加 RegionBriefProvider 声明区）
- Modify: `applications/earth_explorer/feed_layer.cpp`（haversineKm、FeedLayerImpl::regionSummaryJson、registry、registerFeedLayer 挂接）
- Test: `tests/feed_layer_tests.cpp`（追加用例）

**Interfaces:**
- Produces（Task 6 依赖，签名逐字，加在 feed_layer.h 的 `FeedSelection` 相关声明之后）:
```cpp
    // ===== P4:区域简报(get_region_brief)基建 =====
    struct RegionBriefProvider
    {
        std::string id;                      // 与 LayerManager 图层 id 一致
        std::function<bool()> isEnabled;
        std::function<std::string(double lat, double lon, double radiusKm)> regionSummaryJson;
    };
    // 主线程 only(启动期注册、工具 execute=主线程查询),无锁。provider 持 FeedLayerImpl
    // 裸指针,生命周期=场景节点=会话期(与 get_<id>_summary 工具捕获 implPtr 同一先例)。
    std::vector<RegionBriefProvider>& regionBriefProviders();
    double haversineKm(double lat1, double lon1, double lat2, double lon2);
```
- FeedLayerImpl 追加公有方法（供 registerFeedLayer 包装）:
```cpp
    std::string regionSummaryJson(double lat, double lon, double radiusKm) const;
```

- [ ] **Step 1: 写失败单测**（`tests/feed_layer_tests.cpp` 追加，main 里调用）

```cpp
// ===== P4 Task 5:haversine + 区域摘要 =====
static void testHaversineAndRegionSummary()
{
    using namespace earthfeed;
    // 北京(39.9,116.4)—上海(31.2,121.5)大圆距约 1067km,容差 2%
    double d = haversineKm(39.9, 116.4, 31.2, 121.5);
    CHECK(d > 1045.0 && d < 1090.0);
    CHECK(haversineKm(10.0, 20.0, 10.0, 20.0) < 1e-6);
    // 区域摘要:半径内点入选、半径外排除、按距离升序、封顶 8 条
    FeedSpec spec; spec.id = "regiontest";
    std::vector<FeedPoint> pts;
    pts.push_back(mkPt(10.0, 20.0));  pts.back().title = "near0";
    pts.push_back(mkPt(10.5, 20.0));  pts.back().title = "near55";   // ~56km
    pts.push_back(mkPt(30.0, 120.0)); pts.back().title = "far";
    for (int i = 0; i < 10; ++i)      // 凑满溢出,验证 nearest 封顶 8
    { pts.push_back(mkPt(10.0 + 0.01 * (i + 1), 20.0)); pts.back().title = "pad"; }
    // 灌数据:静态源路径(不设 url + fixtureEnv 已设 → 只读一次),parse 忽略文件内容
    // 直接返回上面构造的 pts;随后按 SyncCallback 的既有驱动方式让 records 就位
    // (testStaticSourceLoad 已示范该驱动链,同步调用名以该用例现行写法为准)。
    const char* kRFix = "region_fixture_tmp.json";
    { std::ofstream f(kRFix); f << "{}"; }
    setEnvVar("EARTH_REGIONTEST_FILE", kRFix);
    spec.fixtureEnv = "EARTH_REGIONTEST_FILE";
    spec.parse = [pts](const std::string&) { return pts; };
    osg::ref_ptr<FeedLayerImpl> impl = new FeedLayerImpl(spec);
    // 灌入后:
    picojson::value v;
    CHECK(picojson::parse(v, impl->regionSummaryJson(10.0, 20.0, 100.0)).empty());
    CHECK(v.get("count").get<double>() == 12.0);            // near0+near55+pad×10
    const picojson::array& nearest = v.get("nearest").get<picojson::array>();
    CHECK(nearest.size() == 8);                             // 封顶
    CHECK(nearest[0].get("title").get<std::string>() == "near0");   // 距离升序
    CHECK(nearest[0].get("distanceKm").get<double>() < 1.0);
    // 半径外:far 不出现在 count 里(12 而非 13 已隐含);半径极小 → count=1
    picojson::value v2;
    CHECK(picojson::parse(v2, impl->regionSummaryJson(10.0, 20.0, 0.5)).empty());
    CHECK(v2.get("count").get<double>() == 1.0);
    unsetEnvVar("EARTH_REGIONTEST_FILE"); std::remove(kRFix);
    std::cout << "[OK] haversine + regionSummaryJson\n";
}
```

注:spec.parse 的确切签名（`std::function<std::vector<FeedPoint>(const std::string&)>` 或带 spec 参）以 feed_layer.h 现行定义为准，lambda 相应调整;驱动 records 就位的调用（syncIfDirty 或等价）照 `testStaticSourceLoad` 现行写法，断言保持不变。

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds 2>&1 | tail -5`
Expected: 编译错误 `haversineKm` / `regionSummaryJson` 未声明。

- [ ] **Step 3: 实现**

`feed_layer.cpp` namespace earthfeed 顶层（clusterFeedPoints 附近）:

```cpp
double haversineKm(double lat1, double lon1, double lat2, double lon2)
{
    const double kR = 6371.0, kD = osg::PI / 180.0;
    double dLat = (lat2 - lat1) * kD, dLon = (lon2 - lon1) * kD;
    double a = sin(dLat * 0.5) * sin(dLat * 0.5) +
               cos(lat1 * kD) * cos(lat2 * kD) * sin(dLon * 0.5) * sin(dLon * 0.5);
    return 2.0 * kR * atan2(sqrt(a), sqrt(1.0 - a));
}

std::vector<RegionBriefProvider>& regionBriefProviders()
{ static std::vector<RegionBriefProvider> s; return s; }
```

`FeedLayerImpl::regionSummaryJson`（summaryJson 旁）:

```cpp
std::string regionSummaryJson(double lat, double lon, double radiusKm) const
{
    // 主线程调用;_records 的读取方式与 summaryJson/pickAt 同一约定(如它们有锁则同样加)。
    std::vector<std::pair<double, const FeedPoint*> > hits;
    for (size_t i = 0; i < _records.size(); ++i)
    {
        const FeedPoint& p = _records[i].pt;
        double d = haversineKm(lat, lon, p.lat, p.lon);
        if (d <= radiusKm) hits.push_back(std::make_pair(d, &p));
    }
    std::sort(hits.begin(), hits.end(),
        [](const std::pair<double, const FeedPoint*>& a,
           const std::pair<double, const FeedPoint*>& b) { return a.first < b.first; });
    picojson::object r; r["count"] = picojson::value((double)hits.size());
    picojson::array nearest;
    for (size_t i = 0; i < hits.size() && i < 8; ++i)
    {
        picojson::object n;
        n["title"] = picojson::value(hits[i].second->title);
        n["lat"] = picojson::value(hits[i].second->lat);
        n["lon"] = picojson::value(hits[i].second->lon);
        n["distanceKm"] = picojson::value(floor(hits[i].first * 10.0) * 0.1);
        nearest.push_back(picojson::value(n));
    }
    r["nearest"] = picojson::value(nearest);
    return picojson::value(r).serialize();
}
```

`registerFeedLayer` 里 get_`<id>`_summary 工具注册块之后追加:

```cpp
    // P4:注册区域简报 provider(get_region_brief 跨源合成用)。implPtr 生命周期同上注释。
    {
        RegionBriefProvider bp; bp.id = spec.id;
        FeedLayerImpl* rp = impl.get();
        bp.isEnabled = [rp]() { return rp->isEnabled(); };
        bp.regionSummaryJson = [rp](double la, double lo, double rk)
        { return rp->regionSummaryJson(la, lo, rk); };
        regionBriefProviders().push_back(bp);
    }
```

- [ ] **Step 4: 跑测试确认通过（含既有全量回归）**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds 2>&1 | tail -3 && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Feeds`
Expected: 既有全部用例 + 新用例全过。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/feed_layer.h applications/earth_explorer/feed_layer.cpp tests/feed_layer_tests.cpp
git commit -m "feat(earth): FeedLayer 区域查询基建 — haversine + regionSummaryJson + brief provider 注册表 (P4 T5)"
```

---

### Task 6: get_region_brief 跨源合成工具

**Files:**
- Modify: `applications/earth_explorer/ai_world_tools.cpp`（include feed_layer.h;新工具）
- Modify: `tests/world_tools_tests.cpp`（include feed_layer.cpp + geo_primitives.cpp;追加用例）

**Interfaces:**
- Consumes: Task 5 的 `earthfeed::regionBriefProviders()`;Task 2 的 toolFetchJson。
- Produces: 工具 `get_region_brief(lat, lon, radius_km?)`。

- [ ] **Step 1: 写失败单测**

`tests/world_tools_tests.cpp` 首次引入 `feed_layer.cpp`。**⚠️ 必修链接坑**:feed_layer.cpp 现 `#include "marker_style.h"` 并用 `earthmark::`；firms_feed.cpp 用 `earthcfg::resolveKey`——所以必须照抄 `tests/feed_layer_tests.cpp` 当前的**完整** include 清单，否则 `earthmark::`/`earthcfg::` 符号链接失败。在 `ai_query.cpp` include 之后、`ai_world_tools.cpp` include 之前插入这整块（顺序:实现定义先于使用它的用例）:

```cpp
// feed_layer.cpp 依赖全家桶(照抄 feed_layer_tests.cpp 现行清单;缺一个就链接失败)
#include "../applications/earth_explorer/feed_layer.cpp"
#include "../applications/earth_explorer/feeds/gdacs_feed.cpp"
#include "../applications/earth_explorer/feeds/usgs_quakes_feed.cpp"
#include "../applications/earth_explorer/feeds/eonet_feed.cpp"
#include "../applications/earth_explorer/feeds/gdelt_feed.cpp"
#include "../applications/earth_explorer/feeds/gpsjam_feed.cpp"
#include "../applications/earth_explorer/feeds/unhcr_feed.cpp"
#include "../applications/earth_explorer/feeds/nhc_feed.cpp"
#include "../applications/earth_explorer/feeds/strategic_feed.cpp"
#include "../applications/earth_explorer/feeds/firms_feed.cpp"
#include "../applications/earth_explorer/earth_config.cpp"
#include "../applications/earth_explorer/geo_primitives.cpp"
#include "../applications/earth_explorer/marker_style.cpp"
```

注:执行时以 `tests/feed_layer_tests.cpp` 的**现行** include 清单为准（本清单是 v0.19 快照，若届时又有增删照它抄）。若遇 `defaultHttpGet` 等 static 同名重定义（feed_layer.cpp 与 ai_query.cpp 同 TU），把 ai_query.cpp 的私有 helper 改成更独特的名字（如 `aiqDefaultHttpGet`）。

用例:

```cpp
// ===== Task 6:get_region_brief 跨源合成 =====
static void testRegionBriefTool()
{
    // 直接向注册表塞两个假 provider:一个启用一个禁用
    earthfeed::regionBriefProviders().clear();
    earthfeed::RegionBriefProvider on; on.id = "fakeon";
    on.isEnabled = []() { return true; };
    on.regionSummaryJson = [](double, double, double)
    { return std::string("{\"count\":2,\"nearest\":[{\"title\":\"A\"}]}"); };
    earthfeed::regionBriefProviders().push_back(on);
    earthfeed::RegionBriefProvider off; off.id = "fakeoff";
    off.isEnabled = []() { return false; };
    off.regionSummaryJson = [](double, double, double) { return std::string("{}"); };
    earthfeed::regionBriefProviders().push_back(off);

    const char* kW = "aiq_briefwx_tmp.json";
    { std::ofstream f(kW); f << "{\"current\":{\"temperature_2m\":18.0}}"; }
    setEnvVar("EARTH_WEATHER_FILE", kW);
    earthai::AsyncJsonFetcher fx;
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{\"lat\":10.0,\"lon\":20.0,\"radius_km\":300}").empty());
    CHECK(reg.dispatch("get_region_brief", args, out));
    CHECK(out.get("radius_km").get<double>() == 300.0);
    const picojson::array& srcs = out.get("sources").get<picojson::array>();
    CHECK(srcs.size() == 1);
    CHECK(srcs[0].get("id").get<std::string>() == "fakeon");
    CHECK(srcs[0].get("count").get<double>() == 2.0);
    const picojson::array& dis = out.get("disabled_sources").get<picojson::array>();
    CHECK(dis.size() == 1 && dis[0].get<std::string>() == "fakeoff");
    CHECK(out.get("weather").get("current").get("temperature_2m").get<double>() == 18.0);
    unsetEnvVar("EARTH_WEATHER_FILE"); std::remove(kW);
    earthfeed::regionBriefProviders().clear();
    std::cout << "[OK] region brief tool\n";
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools 2>&1 | tail -3 && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_WorldTools`
Expected: dispatch false → CHECK abort。

- [ ] **Step 3: 实现**（ai_world_tools.cpp;顶部 `#include "feed_layer.h"`）

```cpp
// ---- get_region_brief(P4 核心:跨源区域情报合成) ----
static void registerRegionBriefTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_region_brief";
    t.description = u8"跨源区域情报简报:汇总指定经纬度半径内所有『已开启』数据层的要素"
        u8"(地震/灾害/火点/新闻热点/飓风等,含最近8条与距离)+ 中心点实况天气。"
        u8"radius_km 缺省 500。disabled_sources 列出未开启因此未查询的层,可提示用户"
        u8"用 set_layer 开启后重查;weather.pending=true 时可稍后重调。"
        u8"拿到结果后请合成结构化简报,并善用 show_chart 画分布、fly_to 逐点导览。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"lat\":{\"type\":\"number\"},\"lon\":{\"type\":\"number\"},"
        "\"radius_km\":{\"type\":\"number\",\"description\":\"default 500\"}},"
        "\"required\":[\"lat\",\"lon\"]}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        double lat = 0, lon = 0, radius = 500.0, rv = 0;
        if (!getNum(a, "lat", lat) || !getNum(a, "lon", lon))
            return argError("need number lat/lon");
        if (getNum(a, "radius_km", rv) && rv > 0 && rv <= 5000) radius = rv;
        if (lat < -90 || lat > 90 || lon < -180 || lon > 180)
            return argError("out of range: lat[-90,90] lon[-180,180]");
        picojson::object r;
        r["lat"] = picojson::value(lat); r["lon"] = picojson::value(lon);
        r["radius_km"] = picojson::value(radius);
        picojson::array srcs, disabled;
        std::vector<earthfeed::RegionBriefProvider>& ps = earthfeed::regionBriefProviders();
        for (size_t i = 0; i < ps.size(); ++i)
        {
            if (!ps[i].isEnabled())
            { disabled.push_back(picojson::value(ps[i].id)); continue; }
            picojson::value v; std::string perr =
                picojson::parse(v, ps[i].regionSummaryJson(lat, lon, radius));
            if (!perr.empty() || !v.is<picojson::object>()) continue;
            v.get<picojson::object>()["id"] = picojson::value(ps[i].id);
            srcs.push_back(v);
        }
        r["sources"] = picojson::value(srcs);
        r["disabled_sources"] = picojson::value(disabled);
        char wurl[256];
        snprintf(wurl, sizeof(wurl),
            "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f"
            "&current=temperature_2m,wind_speed_10m,precipitation,weather_code&timezone=auto",
            lat, lon);
        r["weather"] = toolFetchJson(*f, wurl, 900.0, "EARTH_WEATHER_FILE");
        OSG_NOTICE << "[AIChat] get_region_brief " << lat << "," << lon << " r=" << radius
                   << " sources=" << srcs.size() << std::endl;
        return picojson::value(r);
    };
    tools->add(t);
}
```

registerWorldQueryTools 末尾追加 `registerRegionBriefTool(tools, fetcher);`。

- [ ] **Step 4: 跑测试确认通过（两个测试目标都跑）**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_WorldTools --target osgVerse_Test_Feeds 2>&1 | tail -3 && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_WorldTools && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Feeds`
Expected: 双绿。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/ai_world_tools.cpp tests/world_tools_tests.cpp
git commit -m "feat(earth): get_region_brief 跨源区域情报合成工具 (P4 T6)"
```

---

### Task 7: NDVI 植被 + 夜光图层（GIBS，OVERLAY 槽互斥泛化）

> ⚠️ **v0.19 对账**:标记系统没碰栅格 OVERLAY 逻辑（clouds/precip 分支仍在，泛化仍有效），但 earth_main 图层注册块因加了 ships/satellite/marker 接线**行号全变**——`clouds`/`precip` 的 `OverlayLayer` 注册块用符号现场定位。另:`OverlayLayer` 现多了 `shape`/`iconColor` 两个外观字段（供点标记图层用），**栅格层不填、取默认即可**（Circle+灰，栅格无点标记，不影响）。

**Files:**
- Modify: `applications/earth_explorer/earth_main.cpp`（createCustomPath 旁加模板常量;图层注册块重构互斥 + 新增两层;env 钩子区新增两条）

**Interfaces:**
- Consumes: 既有 OVERLAY http 模板分支（earth_main.cpp:611-615，z>10 截断,404→透明回退,与极夜先例同）。
- Produces: 图层 id `"ndvi"` / `"nightlights"`（Task 8/9 的互斥表、预设、验收引用这两个 id）。

- [ ] **Step 0: curl 冒烟验证 GIBS 图层名与 "default" 时间**（一次性）

```bash
curl -s -o /dev/null -w '%{http_code} %{content_type}\n' "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/MODIS_Terra_NDVI_8Day/default/default/GoogleMapsCompatible_Level9/2/1/1.png"
curl -s -o /dev/null -w '%{http_code} %{content_type}\n' "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_Black_Marble/default/default/GoogleMapsCompatible_Level8/2/1/1.png"
```
Expected: 各 `200 image/png`。若 404:在 `https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/WMTSCapabilities.xml` 里搜 `NDVI` / `Black_Marble` 找正确的图层名与 TileMatrixSet 等级，同步修正下方模板常量与本步骤命令后重验。

- [ ] **Step 1: 加模板常量**（earth_main.cpp,createCustomPath 函数上方）

```cpp
// P6a:GIBS 科学图层瓦片模板。time 用 "default"(GIBS 解析为该层最新可用期,长跑不刷新
// 与云图 gibsDate 同一取舍)。LevelN 以上无数据 → 404 → 透明回退(同 VIIRS 极夜先例)。
static const char* kNdviTemplate =
    "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/MODIS_Terra_NDVI_8Day/"
    "default/default/GoogleMapsCompatible_Level9/{z}/{y}/{x}.png";
static const char* kNightTemplate =
    "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_Black_Marble/"
    "default/default/GoogleMapsCompatible_Level8/{z}/{y}/{x}.png";
```

- [ ] **Step 2: 重构 OVERLAY 互斥为组表 + 注册两层**

图层注册块里，用下面代码**替换**现有 `applyOverlayOpacity` lambda（earth_main.cpp:852-861），并改写 clouds/precip 的 apply（863-892）——对旧两层行为不变（回归由 Step 4 预设日志断言守住）:

```cpp
        // OVERLAY 瓦片槽互斥组:物理上只有一个 OVERLAY 槽,组内任意一层开启须关闭其余。
        // P6a 从 clouds/precip 两两手写互斥泛化为组表;gebco 由 Task 8 启用。
        static const char* kOverlaySlotIds[] = { "clouds", "precip", "ndvi", "nightlights", "gebco" };
        static const size_t kOverlaySlotN = sizeof(kOverlaySlotIds) / sizeof(kOverlaySlotIds[0]);
        auto disableOtherOverlays = [lmptr, pcptr](const std::string& selfId) {
            for (size_t i = 0; i < kOverlaySlotN; ++i)
            {
                if (selfId == kOverlaySlotIds[i]) continue;
                if (OverlayLayer* o = lmptr->find(kOverlaySlotIds[i])) o->enabled = false;
            }
            if (selfId != "precip" && pcptr) pcptr->setEnabled(false);
        };
        auto applyOverlayOpacity = [eptr, lmptr]() {
            float op = 0.0f;
            for (size_t i = 0; i < kOverlaySlotN; ++i)
            {
                OverlayLayer* o = lmptr->find(kOverlaySlotIds[i]);
                if (o && o->enabled) { op = o->opacity; break; }
            }
            if (eptr->commonUniforms.count("Overlay2Opacity"))
                eptr->commonUniforms["Overlay2Opacity"]->set(op);
        };
```

clouds.apply 改为:

```cpp
        clouds.apply = [lmptr, disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)
            {
                disableOtherOverlays("clouds");
                osgVerse::TileManager::instance()->setLayerPath(osgVerse::TileCallback::OVERLAY, "gibs");
            }
            applyOverlayOpacity();
        };
```

precip.apply 改为（语义保持:开=清槽等控制器,关=回 gibs）:

```cpp
        precipL.apply = [lmptr, pcptr, disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)
            {
                disableOtherOverlays("precip");
                osgVerse::TileManager::instance()->setLayerPath(osgVerse::TileCallback::OVERLAY, "");
                if (pcptr) pcptr->setEnabled(true);
            }
            else
            {
                if (pcptr) pcptr->setEnabled(false);
                osgVerse::TileManager::instance()->setLayerPath(osgVerse::TileCallback::OVERLAY, "gibs");
            }
            applyOverlayOpacity();
        };
```

precipL 注册之后新增两层:

```cpp
        // P6a:科学数据栅格层(GIBS,复用 OVERLAY http 模板分支,零新管线)
        OverlayLayer ndvi; ndvi.id = "ndvi"; ndvi.displayName = u8"植被指数 NDVI";
        ndvi.group = u8"科学数据 / Science"; ndvi.enabled = false;
        ndvi.hasOpacity = true; ndvi.opacity = 0.8f;
        ndvi.subtitle = u8"NASA GIBS · MODIS 8日合成";
        ndvi.apply = [disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)
            {
                disableOtherOverlays("ndvi");
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, kNdviTemplate);
            }
            else
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, "gibs");
            applyOverlayOpacity();
        };
        layerMgr.add(ndvi);

        OverlayLayer night; night.id = "nightlights"; night.displayName = u8"夜间灯光";
        night.group = u8"科学数据 / Science"; night.enabled = false;
        night.hasOpacity = true; night.opacity = 0.9f;
        night.subtitle = u8"NASA GIBS · VIIRS Black Marble";
        night.apply = [disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)
            {
                disableOtherOverlays("nightlights");
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, kNightTemplate);
            }
            else
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, "gibs");
            applyOverlayOpacity();
        };
        layerMgr.add(night);
```

- [ ] **Step 3: env 钩子**（EARTH_PRECIP 钩子块之后，语义同 EARTH_CLOUDS:值=不透明度,0=关）

```cpp
    // EARTH_NDVI / EARTH_NIGHTLIGHTS=<不透明度>:headless 强制开启科学层(同 EARTH_CLOUDS 语义)。
    if (OverlayLayer* nv = layerMgr.find("ndvi"))
    {
        const char* e = getenv("EARTH_NDVI");
        if (e && *e)
        {
            float op = (float)atof(e);
            nv->enabled = (op > 0.0f);
            nv->opacity = (op < 0.0f) ? 0.0f : (op > 1.0f ? 1.0f : op);
            layerMgr.setEnabled("ndvi", nv->enabled);
        }
    }
    if (OverlayLayer* nl = layerMgr.find("nightlights"))
    {
        const char* e = getenv("EARTH_NIGHTLIGHTS");
        if (e && *e)
        {
            float op = (float)atof(e);
            nl->enabled = (op > 0.0f);
            nl->opacity = (op < 0.0f) ? 0.0f : (op > 1.0f ? 1.0f : op);
            layerMgr.setEnabled("nightlights", nl->enabled);
        }
    }
```

- [ ] **Step 4: 构建 + 预设回归（日志断言）**

```bash
cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | tail -3
EARTH_OFFSCREEN=1 EARTH_PRESET=全部 EARTH_AUTOCAP=150 /Users/USER/osgverse/build/verse_core/bin/osgVerse_EarthExplorer 2>&1 | grep '\[Preset\]'
```
Expected: `[Preset] applied '全部': ...` 行存在;其中 `ndvi=0 nightlights=0`（新层不入既有预设）,其余各层 0/1 与 P3 收尾时基线一致。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/earth_main.cpp
git commit -m "feat(earth): NDVI 植被 + VIIRS 夜光科学图层(GIBS),OVERLAY 互斥泛化为组表 (P6a T7)"
```

---

### Task 8: GEBCO 海底地形层（WMS bbox 合成）

**Files:**
- Modify: `applications/earth_explorer/geo_primitives.h` / `geo_primitives.cpp`（mercatorTileBBox 纯函数）
- Modify: `applications/earth_explorer/earth_main.cpp`（createCustomPath "gebco" 分支;图层注册;EARTH_GEBCO 钩子）
- Test: `tests/feed_layer_tests.cpp`（bbox 数值单测;该测试已 include geo_primitives.cpp）

**Interfaces:**
- Produces（签名逐字，放 geo_primitives.h 既有声明风格处）:
```cpp
    // P6a:XYZ 瓦片坐标(yXYZ=原点左上) → EPSG:3857 米制 bbox(xmin,ymin,xmax,ymax)。
    // WMS GetMap 按瓦片金字塔合成请求用(GEBCO 首用)。
    osg::Vec4d mercatorTileBBox(int x, int yXYZ, int z);
```

- [ ] **Step 0: curl 冒烟验证 GEBCO WMS**

```bash
curl -s -o /tmp/gebco_probe.png -w '%{http_code} %{content_type}\n' "https://wms.gebco.net/mapserv?request=getmap&service=wms&version=1.3.0&layers=GEBCO_LATEST&format=image/png&crs=EPSG:3857&width=256&height=256&bbox=-20037508.34,-20037508.34,20037508.34,20037508.34"
```
Expected: `200 image/png` 且 /tmp/gebco_probe.png 打开是全球海底地形。若参数被拒（XML 报错），按报错调整（常见:layers 名或 crs 轴序），同步修正下方 URL 模板。

- [ ] **Step 1: 写失败单测**（tests/feed_layer_tests.cpp 追加，main 里调用）

```cpp
// ===== P6a Task 8:mercatorTileBBox =====
static void testMercatorTileBBox()
{
    const double kM = 20037508.342789244, kEps = 1.0;   // 米级容差
    osg::Vec4d b0 = earthgeo::mercatorTileBBox(0, 0, 0);   // z0 全球
    CHECK(std::fabs(b0[0] + kM) < kEps && std::fabs(b0[1] + kM) < kEps);
    CHECK(std::fabs(b0[2] - kM) < kEps && std::fabs(b0[3] - kM) < kEps);
    // z1:(0,0)=西北象限(x∈[-M,0], y∈[0,M]);(1,1)=东南象限
    osg::Vec4d nw = earthgeo::mercatorTileBBox(0, 0, 1);
    CHECK(std::fabs(nw[0] + kM) < kEps && std::fabs(nw[2]) < kEps);
    CHECK(std::fabs(nw[1]) < kEps && std::fabs(nw[3] - kM) < kEps);
    osg::Vec4d se = earthgeo::mercatorTileBBox(1, 1, 1);
    CHECK(std::fabs(se[0]) < kEps && std::fabs(se[2] - kM) < kEps);
    CHECK(std::fabs(se[1] + kM) < kEps && std::fabs(se[3]) < kEps);
    // 尺寸单调:z2 瓦片宽 = 全球/4
    osg::Vec4d q = earthgeo::mercatorTileBBox(2, 1, 2);
    CHECK(std::fabs((q[2] - q[0]) - kM * 2.0 / 4.0) < kEps);
    std::cout << "[OK] mercatorTileBBox\n";
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds 2>&1 | tail -5`
Expected: 编译错误 `mercatorTileBBox` 未声明。

- [ ] **Step 3: 实现**

`geo_primitives.cpp`（namespace earthgeo）:

```cpp
osg::Vec4d mercatorTileBBox(int x, int yXYZ, int z)
{
    const double kM = 20037508.342789244;
    double n = (double)(1 << z), size = 2.0 * kM / n;
    double xmin = -kM + x * size, ymax = kM - yXYZ * size;
    return osg::Vec4d(xmin, ymax - size, xmin + size, ymax);
}
```

`earth_main.cpp` createCustomPath 的 OVERLAY 分支，`if (prefix != "gibs") return "";` 之前插入:

```cpp
        if (prefix == "gebco")
        {
            // GEBCO 15″ 网格:z8 以上无信息增益;WMS GetMap 按瓦片 bbox 合成(EPSG:3857
            // 轴序 x,y)。URL 含 '='/'&' 与 Google 底图同先例(createCustomPath 直出不过
            // osgDB::Options,不受多 '=' 截断影响)。
            if (z > 8) return "";
            osg::Vec4d bb = earthgeo::mercatorTileBBox(x, yXYZ, z);
            char buf[512];
            snprintf(buf, sizeof(buf),
                "https://wms.gebco.net/mapserv?request=getmap&service=wms&version=1.3.0"
                "&layers=GEBCO_LATEST&format=image/png&crs=EPSG:3857&width=256&height=256"
                "&bbox=%.3f,%.3f,%.3f,%.3f", bb[0], bb[1], bb[2], bb[3]);
            return std::string(buf);
        }
```

（earth_main.cpp 需 include geo_primitives.h，如未含则补。）

图层注册（Task 7 的 night 层之后;互斥表已含 "gebco"）:

```cpp
        OverlayLayer gebco; gebco.id = "gebco"; gebco.displayName = u8"海底地形 GEBCO";
        gebco.group = u8"科学数据 / Science"; gebco.enabled = false;
        gebco.hasOpacity = true; gebco.opacity = 0.85f;
        gebco.subtitle = u8"© GEBCO Compilation Group 2025";
        gebco.apply = [disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)
            {
                disableOtherOverlays("gebco");
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, "gebco");
            }
            else
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, "gibs");
            applyOverlayOpacity();
        };
        layerMgr.add(gebco);
```

env 钩子（Task 7 两条之后，同语义）:

```cpp
    if (OverlayLayer* gb = layerMgr.find("gebco"))
    {
        const char* e = getenv("EARTH_GEBCO");
        if (e && *e)
        {
            float op = (float)atof(e);
            gb->enabled = (op > 0.0f);
            gb->opacity = (op < 0.0f) ? 0.0f : (op > 1.0f ? 1.0f : op);
            layerMgr.setEnabled("gebco", gb->enabled);
        }
    }
```

- [ ] **Step 4: 跑测试 + 构建**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | tail -3 && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Feeds`
Expected: 全绿 + 应用构建零错误。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/geo_primitives.h applications/earth_explorer/geo_primitives.cpp \
        applications/earth_explorer/earth_main.cpp tests/feed_layer_tests.cpp
git commit -m "feat(earth): GEBCO 海底地形层 — 瓦片 bbox 合成 WMS GetMap,mercatorTileBBox 纯函数 (P6a T8)"
```

---

### Task 9: 真机验收 + 文档收尾

**Files:**
- Modify: `docs/superpowers/specs/2026-07-03-world-info-hub-roadmap.md`（P4 标记完成 + P6a 记录）
- 无代码改动（发现 bug 则按 systematic-debugging 修并追加 commit）

- [ ] **Step 1: 单测全量回归**

```bash
/Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Feeds && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_WorldTools
```
Expected: 双绿。

- [ ] **Step 2: 三个科学层离屏视觉验收**（逐个跑;先删旧图防陈旧假象——既有教训）

```bash
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=600 EARTH_FRAME_SLEEP_MS=30 EARTH_SUN_TO_CAMERA=1 EARTH_NDVI=0.9 /Users/USER/osgverse/build/verse_core/bin/osgVerse_EarthExplorer 2>&1 | tail -2
```
用 Read 工具查看 /tmp/earth_capture_0.png:全球视角应见植被绿/褐分布叠加。随后同法分别验 `EARTH_NIGHTLIGHTS=0.9`（城市灯光网络清晰）与 `EARTH_GEBCO=0.9`（洋中脊/海沟纹理可辨）。任一层黑屏/花屏 → 先 curl 该层实际瓦片 URL（教训:先证到达目标代码路径再定位渲染）。

- [ ] **Step 3: 应用启动冒烟**（工具注册路径不 crash;真实 AI key 冒烟由用户择机做，既往惯例）

```bash
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 /Users/USER/osgverse/build/verse_core/bin/osgVerse_EarthExplorer > /tmp/earth_p4_smoke.log 2>&1; echo "exit=$?"
grep -iE 'error|crash|abort' /tmp/earth_p4_smoke.log | grep -v 'ErrorHandler' | head -5
```
Expected: `exit=0`,且 grep 无新增报错行（与 P3 收尾基线日志对比,允许既有噪声）。

- [ ] **Step 4: 预设/既有层回归**

```bash
EARTH_OFFSCREEN=1 EARTH_PRESET=灾害 EARTH_AUTOCAP=150 /Users/USER/osgverse/build/verse_core/bin/osgVerse_EarthExplorer 2>&1 | grep '\[Preset\]'
```
Expected: 与 P3 收尾基线一致（灾害四源开、其余关、新科学层全 0）。

- [ ] **Step 5: 文档 + Commit + push**

roadmap spec §P4 标注「✅ 已完成(2026-07-XX,含 5 工具+region brief;WHO/旅行警告/Yahoo/FRED 后补)」，并追加一行「P6a 科学速赢包 ✅(NDVI/夜光/GEBCO 三层)」。

```bash
git add docs/superpowers/specs/2026-07-03-world-info-hub-roadmap.md
git commit -m "docs(earth): P4+P6a 完成标注 — 世界工具族+region brief+三科学层 (P4/P6a T9)"
git push
```

---

## 附录:P6b / P6c（本计划不含，各自另出 plan 的 spec 级输入）

**P6b COG 底座 + AlphaEarth（~1-2 周）**
- 前置决策:GDAL 依赖 vs 自写最小 COG reader（TIFF 目录解析 + tile 索引 + deflate + HTTP Range;libhv 加 `req->headers["Range"]` 即可,osgdb_tiff 已有 libtiff/deflate 可借力）。建议先做 1 天 spike:Range 读 source.coop 一块 AEF COG 并解出 3 波段。
- 数据:`s3://us-west-2.opendata.source.coop/tge-labs/aef`（匿名 HTTPS,CC-BY 4.0,须署名 Google/Google DeepMind）;先下 `aef_index.csv` 做经纬度→文件映射;int8 反量化 `((v/127.5)^2)*sign(v)`;**方向坑:裸 COG 需按 .vrt 修正翻转**。
- 交付:AlphaEarth RGB 可视化层（惯例波段 A01/A16/A09,min -0.3/max 0.3）+ Sentinel-2 真彩浏览层（Earth Search STAC,免 key）+ AI 工具 `get_embedding_change(lat,lon)`（逐年向量点积=变化检测）/`find_similar_regions`（余弦相似度）。
- 验收愿景:点某块地问"这里 2017→2025 变化大吗" → AI 调工具出变化分数 + show_chart 逐年曲线 + fly_to。

**P6c GRIB2 底座 + AIFS 预报层（~1 周）**
- 依赖:ecCodes（Apache 2.0 C 库,CMake 成熟）;数据:ECMWF open-data 匿名镜像桶（AWS `s3://ecmwf-forecasts`,AIFS 0.25° GRIB2,每日 4 次,CC-BY）。
- 交付:全球温度/风/降水预报叠加层（GRIB2 解码 → 重投影贴 OVERLAY 或点阵渲染,时间步可切）+ AI 工具 `get_forecast_grid(var,lead_h)`;与 NHC 飓风层联动是杀手场景。
- 风险:GRIB2 变量重投影到瓦片的渲染路径需 spike;若 OVERLAY 槽不够用,届时评估多 overlay 槽扩展（引擎侧改动,单独评审）。
