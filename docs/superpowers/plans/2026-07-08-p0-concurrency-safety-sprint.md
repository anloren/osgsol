# P0 并发安全冲刺 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** 清掉 2026-07-08 项目 review 里的 P0 崩溃/数据竞争/UB 债,拿到一个"无已知崩溃/UB"的安全基线。

**Architecture:** 六个独立修复。核心是把本仓已在 `feed_layer.cpp` 确立的并发标准(跨线程标志 `std::atomic`、POST_DRAW 共享态 `OpenThreads::Mutex`)推广到更早的 flight/sat/ais/precip 手写管线与 AI 卡片面板;外加 `ai_query` worker 兜底、`check()` 迭代器守卫、一条假绿测试补强。

**Tech Stack:** C++14,OpenSceneGraph(osg::Object DataVariance、OpenThreads::Mutex/ScopedLock)、`std::atomic`、picojson。

## Global Constraints

- **线程模型事实(所有并发修复的依据)**:`viewer.setThreadingModel(SingleThreaded)` 已注释(`earth_main.cpp:924`)→ 默认多线程(DrawThreadPerContext)。ImGui 内容(含 `AICardPanel::registerCards`)跑在 **POST_DRAW 相机回调 = draw 线程**(`ui/ImGui.cpp:352-359` 的 RenderCallback 调 `v->runInternal`);`feed_layer.cpp:250` 注释已明载"帧 N draw 与帧 N+1 update 重叠,无锁 = use-after-free"。跨线程共享数据**必须**加锁或 atomic。
- **模板**:锁用 `OpenThreads::Mutex` + `OpenThreads::ScopedLock<OpenThreads::Mutex>`,对齐 `feed_layer.cpp:242/250`;原子用 `std::atomic`,对齐 `ais_data.cpp:496`/`precip_data.cpp:122`。
- 不碰 globe GLSL 着色器(铁律)。
- 所有跑 app 带 `EARTH_OFFSCREEN=1`;app 在 `build/sdk_core/bin/osgVerse_EarthExplorer`;构建 `cmake --build build/verse_core --target install -j4`,**禁止并发第二个构建**。
- 并发修复难有确定性竞态复现;每任务的验收 = ①构建通过 ②`osgVerse_Test_Feeds` 全绿(无回归)③离屏冒烟跑完不崩 ④代码符合上述模板。诚实标注这一点,不谎称"已复现竞态"。
- commit message 结尾:`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

---

## File Structure

- `applications/earth_explorer/ai_cards.h` / `ai_cards.cpp` — T1:AICardPanel 加锁 + lambda 按值捕获 + closeBySerial。
- `applications/earth_explorer/flight_data.cpp` / `sat_data.cpp` — T2:geode DYNAMIC;T3:atomic 化。
- `applications/earth_explorer/ais_data.cpp` / `precip_data.cpp` — T3:atomic 化。
- `applications/earth_explorer/ai_query.cpp` / `ai_query.h` — T4:worker try/catch + 始终清 inflight + `_done` atomic。
- `readerwriter/TileCallback.cpp` — T5:`check()` 迭代器守卫。
- `readerwriter/TileCallback.h` — T3:`_lastOverlayStretchFrame` atomic。
- `tests/feed_layer_tests.cpp` — T6:补强 testPerSpecLiftMeters 原始点断言。

---

### Task 1: AICardPanel `_cards` 数据竞争/UAF(唯一 high)

`_cards` 被主线程(pushChart/pushPhoto/pushJob/removeJob)与 draw 线程(registerCards 的 erase-remove + drawBody/onClose lambda)无锁并发访问;`registerCards` push/erase 触发 vector realloc 时,draw 线程持有的 `&c` 引用悬垂 → UAF/堆破坏。

**Files:**
- Modify: `applications/earth_explorer/ai_cards.h`
- Modify: `applications/earth_explorer/ai_cards.cpp`

**Interfaces:**
- Produces: `void AICardPanel::closeBySerial(int serial)`(私有,onClose 用)。

- [ ] **Step 1: ai_cards.h 加锁成员 + closeBySerial 声明**

顶部 include 区加:
```cpp
#include <OpenThreads/Mutex>
#include <OpenThreads/ScopedLock>
```
`AICardPanel` 的 `private:` 区(`_cards` 声明附近)加:
```cpp
    void closeBySerial(int serial);
    mutable OpenThreads::Mutex _mutex;   // _cards 跨线程(主线程 push/remove × draw 线程 registerCards/draw)
```
(可顺手把 pushChart/pushPhoto 等声明处"无需加锁"的旧注释改为"加锁,见 .cpp"。)

- [ ] **Step 2: ai_cards.cpp 四个 mutator 加锁**

`pushChart`/`pushPhoto`/`pushJob`/`removeJob` 各自函数体最外层包一层:
```cpp
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
```
(放在函数第一行;pushPhoto 的整个"关旧卡 + push 新卡"都在锁内。)同时把文件头 14-16 与各函数上方"主线程同源,无需加锁"的**错误注释**改正为"viewer 多线程,`_cards` 由 draw 线程 registerCards 并发访问,必须加锁(见 review 2026-07-08)"。

- [ ] **Step 3: registerCards 全程持锁 + lambda 按值捕获 + onClose 走 serial**

把 `registerCards` 整个函数体包进锁(第一行 `OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);`,`erase` 与 for 循环都在锁内)。循环内把按引用捕获改为**按值快照**:
```cpp
        AICard snapshot = c;   // 按值快照:draw 线程执行 lambda 时不得再触碰 _cards(realloc 会使引用悬垂)
        card.drawBody = [this, snapshot]() {
            if (snapshot.type == AICard::CHART) drawChartCard(snapshot.spec, ImGui::GetContentRegionAvail().x);
            else if (snapshot.type == AICard::PHOTO) drawPhotoCard(snapshot);
            else if (snapshot.type == AICard::JOB) drawJobCard(snapshot);
            else ImGui::TextDisabled(u8"（暂未实现的卡片类型）");
        };
        int serial = c.serial;
        card.onClose = [this, serial]() { closeBySerial(serial); };
```
(drawJobCard 仍从 `snapshot.jobs->get(snapshot.jobId)` 实时读进度——jobs 指针/jobId 按值拷贝,进度仍每帧现读。)把 90-94 那段"按引用捕获安全"的旧注释替换为上面按值快照的理由说明。

- [ ] **Step 4: 新增 closeBySerial**

在 registerCards 之后加:
```cpp
// 由卡片 [x] 的 onClose(draw 线程)调用:按 serial 定位并标记关闭(下一帧 registerCards 在锁内 erase)。
// 不能像旧代码那样捕获 &c——_cards 可能已被主线程 push 触发 realloc,引用会悬垂。
void AICardPanel::closeBySerial(int serial)
{
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
    for (size_t i = 0; i < _cards.size(); ++i)
        if (_cards[i].serial == serial) { _cards[i].open = false; break; }
}
```

- [ ] **Step 5: 构建 + 单测 + 离屏冒烟**

Run:
```bash
cmake --build build/verse_core --target install -j4 2>&1 | tail -1
EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | tail -2
EARTH_OFFSCREEN=1 EARTH_GEBCO=0.9 EARTH_AUTOCAP=120 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_EarthExplorer --goto 20 0 12000 2>&1 | tail -3; echo "exit=$?"
```
Expected:构建成功;`feed_layer tests OK`;离屏跑完 `exit=0`(无崩溃)。说明:AICard 路径靠 AI 工具触发,离屏难确定性走到,故本步验的是"改动不破坏既有渲染/退出";正确性靠 Step 3/4 的按值+锁语义与代码审查保证。

- [ ] **Step 6: Commit**

```bash
git add applications/earth_explorer/ai_cards.h applications/earth_explorer/ai_cards.cpp
git commit -m "fix(earth): AICardPanel _cards 加锁 + lambda 按值捕获,修 draw×主线程 UAF

viewer 多线程(DrawThreadPerContext),registerCards/drawBody 在 POST_DRAW draw 线程、
push/removeJob 在主线程并发访问 _cards → realloc 使 &c 悬垂 UAF。全 mutator + registerCards
加 OpenThreads::Mutex;drawBody 按值快照、onClose 走 closeBySerial。照 feed_layer.cpp:250 标准。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: flight/sat 逐帧改顶点的 Drawable 标 DYNAMIC

`interpolate`/`interpolateOne` 每 UPDATE 帧写 `(*va)[i]` 并 `va->dirty()`(`flight_data.cpp:326`、`sat_data.cpp:599`),但 `buildFlightGeode`/`buildSatGeode` 建的 Geometry 从不 `setDataVariance(DYNAMIC)` → 多线程绘制下 draw 与下一帧 update 无屏障,数据竞争 + 顶点撕裂。

**Files:**
- Modify: `applications/earth_explorer/flight_data.cpp`(`buildFlightGeode` ~141-155)
- Modify: `applications/earth_explorer/sat_data.cpp`(`buildSatGeode` ~310-325)

- [ ] **Step 1: flight geode 标 DYNAMIC**

`buildFlightGeode` 里 `geom`/`verts` 建好、在 `setUseDisplayList(false); setUseVertexBufferObjects(true);`(:155)附近加:
```cpp
        geom->setDataVariance(osg::Object::DYNAMIC);
        verts->setDataVariance(osg::Object::DYNAMIC);
```
(若颜色数组也逐帧改,同样标;仅顶点逐帧改则只标 verts+geom。先 Read 确认 interpolate 改哪些数组。)

- [ ] **Step 2: sat geode 标 DYNAMIC**

`buildSatGeode` 里对应位置(:325 附近)加同样两行(`geom`/`verts`)。

- [ ] **Step 3: 构建 + 单测 + 离屏冒烟(航班/卫星层)**

Run:
```bash
cmake --build build/verse_core --target install -j4 2>&1 | tail -1
EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | tail -1
EARTH_OFFSCREEN=1 EARTH_FLIGHTS_FILE=applications/earth_explorer/test/flights_fixture.json EARTH_AUTOCAP=120 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_EarthExplorer --goto 40 -100 8000 2>&1 | tail -2; echo "exit=$?"
```
Expected:构建成功;`feed_layer tests OK`;离屏 `exit=0`,航班点正常渲染(用 Read 看 `/tmp/earth_capture_0.png` 有航班箭头)。

- [ ] **Step 4: Commit**

```bash
git add applications/earth_explorer/flight_data.cpp applications/earth_explorer/sat_data.cpp
git commit -m "fix(earth): flight/sat 逐帧改顶点的 Drawable 标 DataVariance::DYNAMIC

interpolate/interpolateOne 每帧写顶点 + dirty(),但 geode 未标 DYNAMIC → 多线程绘制下
draw 与下帧 update 无屏障数据竞争/撕裂。给 geom+Vec3Array setDataVariance(DYNAMIC)。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: 跨线程 `bool`/`int` 标志批量 atomic 化

一批跨线程共享的普通 `bool`/`int`(worker 写/主读或反之)违反本仓 atomic 约定,是 UB。

**Files:**
- Modify: `flight_data.cpp`(`_done` :182;`_enabled` :361;`_refreshNow` :365 + `takeRefreshNow` :246)
- Modify: `sat_data.cpp`(`_done` :341;控制线程触发用的 trigger bool,先 grep `_refetch`/`_refresh`/trigger 类成员确认)
- Modify: `ais_data.cpp`(`_done` :160;`_enabled` :494)
- Modify: `precip_data.cpp`(`_done` :81;`_enabled`/`_refreshNow` 已 atomic,勿重复)
- Modify: `readerwriter/TileCallback.h`(`_lastOverlayStretchFrame` :198)

- [ ] **Step 1: 逐文件把跨线程标志改 `std::atomic`**

对每处:类型改 `std::atomic<bool>`/`std::atomic<int>`/`std::atomic<unsigned int>`;确保 `#include <atomic>`;构造/初始化列表里的赋值保持有效(atomic 支持 `=` 初值)。`takeRefreshNow()`(flight :246)的 `bool r=_refreshNow; _refreshNow=false;` 改为 `return _refreshNow.exchange(false);`(对齐 precip :90)。**只改真正跨线程的**(线程写另一线程读);纯单线程局部量不动——逐个核对注释/用法再改。TileManager `_lastOverlayStretchFrame`:改 `std::atomic<unsigned int>`(operator()/updateLayerData 在 update 线程写、EarthControlUI 在 draw 线程读)。

- [ ] **Step 2: 构建 + 单测**

Run:
```bash
cmake --build build/verse_core --target install -j4 2>&1 | tail -1
EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | tail -1
```
Expected:构建成功(atomic 无拷贝——若某处按值拷贝该结构体会编译失败,说明那里需改引用/指针,一并处理);`feed_layer tests OK`。

- [ ] **Step 3: Commit**

```bash
git add applications/earth_explorer/flight_data.cpp applications/earth_explorer/sat_data.cpp applications/earth_explorer/ais_data.cpp applications/earth_explorer/precip_data.cpp readerwriter/TileCallback.h
git commit -m "fix(earth): 跨线程 bool/int 标志批量 atomic 化,对齐本仓并发约定

flight/sat/ais/precip 的 _done/_enabled/_refreshNow 及 TileManager::_lastOverlayStretchFrame
原为普通标志跨线程访问(UB)→ 统一 std::atomic,takeRefreshNow 用 exchange。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 4: `ai_query` worker try/catch + 始终清 inflight + `_done` atomic

`Worker::run` 里 `_fetch` 抛异常(`ai_query.cpp:26`)会跳过整个 post-fetch 块(27-33)→ ①异常跨出 run() → `std::terminate` 整进程 abort;②`inflight` 永不清零 → 该 url 永远 "Fetching",summary 工具永远回"抓取中"。

**Files:**
- Modify: `applications/earth_explorer/ai_query.cpp`(`Worker::run` 16-34)
- Modify: `applications/earth_explorer/ai_query.h`(`_done` 改 atomic)

- [ ] **Step 1: fetch 包 try/catch,post 块始终执行**

`ai_query.cpp` 的 `std::string body, err; bool ok = _owner->_fetch(url, body, err);`(25-26)改为:
```cpp
            std::string body, err; bool ok = false;
            try { ok = _owner->_fetch(url, body, err); }
            catch (const std::exception& ex) { ok = false; err = std::string("fetch exception: ") + ex.what(); }
            catch (...) { ok = false; err = "fetch exception (unknown)"; }
```
其后的 locked post 块(27-33,设 `e.inflight = false`)保持不变——现在无论 fetch 成功/失败/抛异常都会执行到,inflight 必被清。

- [ ] **Step 2: `_done` atomic**

`ai_query.h` 里 `_done`(worker 循环 `while(!_owner->_done)` 读、析构 `_done=true` 写,跨线程)改 `std::atomic<bool>`,补 `#include <atomic>`;构造初始化 `_done(false)` 仍有效。

- [ ] **Step 3: 构建 + 单测**

Run:
```bash
cmake --build build/verse_core --target install -j4 2>&1 | tail -1
EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | tail -1
```
Expected:构建成功;`feed_layer tests OK`(含 AsyncJsonFetcher 相关用例仍绿)。

- [ ] **Step 4: Commit**

```bash
git add applications/earth_explorer/ai_query.cpp applications/earth_explorer/ai_query.h
git commit -m "fix(earth): ai_query worker try/catch + 始终清 inflight + _done atomic

_fetch 抛异常会跳过 post 块 → 进程 abort + inflight 永不清(工具永久卡'抓取中')。
try/catch 兜底当 ok=false,inflight 必清;_done 跨线程改 atomic。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 5: `TileManager::check()` 迭代器越界守卫

`check()` 里当 `it2 == paths.end()` 且 `it->second.empty()` 时,首 `if` 为假 → 落入 `else if (it2->second...)` 解引用 `paths.end()`(UB),每帧走。

**Files:**
- Modify: `readerwriter/TileCallback.cpp`(`check()` ~779-782)

- [ ] **Step 1: else-if 加 end() 守卫**

把:
```cpp
        if (it2 == paths.end() && !it->second.empty()) updated.push_back(it->first);
        else if (it2->second.first != it->second) updated.push_back(it->first);
```
改为:
```cpp
        if (it2 == paths.end() && !it->second.empty()) updated.push_back(it->first);
        else if (it2 != paths.end() && it2->second.first != it->second) updated.push_back(it->first);
```

- [ ] **Step 2: 构建 + 单测**

Run:
```bash
cmake --build build/verse_core --target install -j4 2>&1 | tail -1
EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | tail -1
EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_TileOverlay 2>&1 | tail -1
```
Expected:构建成功;`feed_layer tests OK`;TileOverlay 测试通过。

- [ ] **Step 3: Commit**

```bash
git add readerwriter/TileCallback.cpp
git commit -m "fix(earth): TileManager::check() 加 paths.end() 守卫,防迭代器越界解引用

it2==end() 且 it->second 非空以外的分支会解引用 end()(UB,per-frame)。else-if 前置
it2!=end() 守卫。当前被 TMS reader 全量注册掩盖,一行防未来崩溃。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 6: 补强 testPerSpecLiftMeters 原始点断言(去同义反复)

`feed_layer_tests.cpp:213-221` 的"原始点"断言是 `f(9000)==f(9000)` 同义反复——它用 `spec.liftMeters` 直接重算期望值,并未驱动生产代码里"原始记录 ecef"的计算路径,故 fetchOnce 原始点若回退硬编码 3000 仍全绿。(注:同函数 222-230 的**聚合点**断言是真覆盖、能抓 3000 回归,勿动。)

**Files:**
- Modify: `tests/feed_layer_tests.cpp`(`testPerSpecLiftMeters` 213-221)

- [ ] **Step 1: 先核实原始记录 ecef 的生产来源**

Read `feed_layer.cpp` 确认:`buildClusterLevels(snap)`(或 fetchOnce)是否会写 `snap.records[i].ecef`(用 `_spec.liftMeters`)。若是 → 直接断言生产输出;若原始记录 ecef 仅在 fetchOnce 网络路径设、单测无法无 viewer 驱动 → 报告 DONE_WITH_CONCERNS 说明,并把断言至少改为对生产可达的量做真对比(不要保留 `f(x)==f(x)`)。

- [ ] **Step 2: 把同义反复替换为驱动生产输出的断言**

将 215-221 那段"复刻 fetchOnce 的 ecef 计算"替换为对**生产代码实际产出**的原始点 ecef 的断言,并加一条 3000 反证(对齐聚合点 230 的写法):
```cpp
        // 驱动生产路径产出的原始点 ecef(读 _spec.liftMeters=9000),而非用 spec.liftMeters 重算期望(那是同义反复)。
        CHECK(!snap.records.empty());
        const osg::Vec3d& prodRaw0 = snap.records[0].ecef;   // ← 若生产未在此写 ecef,按 Step 1 调整来源
        CHECK((prodRaw0 - expectRaw0).length() < 1e-6);
        osg::Vec3d rawWrongIf3000 = lla2ecef(pts[0].lat, pts[0].lon, 3000.0);
        CHECK((prodRaw0 - rawWrongIf3000).length() > 1000.0);   // 硬编码 3000 会失败
        (void)expectRaw1;
```
(实际变量名/来源以 Step 1 核实为准。)

- [ ] **Step 3: 跑测试确认它真的能抓回归(可选临时反证)**

Run:`cmake --build build/verse_core --target osgVerse_Test_Feeds -j4 && EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | tail -2`
Expected:`feed_layer tests OK`。(可临时把生产 liftMeters 读改成硬编码 3000 验证本测试转红、再改回——确认非假绿;改回后复跑绿。)

- [ ] **Step 4: Commit**

```bash
git add tests/feed_layer_tests.cpp
git commit -m "test(earth): testPerSpecLiftMeters 原始点断言去同义反复,驱动生产 ecef

原 f(9000)==f(9000) 未驱动生产原始点路径(回退硬编码 3000 仍绿)。改断言生产产出的
records[].ecef + 3000 反证。聚合点断言本已真覆盖,不动。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage:** review Top 5 + check() 守卫 → T1(AICardPanel UAF)、T2(DYNAMIC)、T3(atomic 批)、T4(ai_query 兜底)、T5(check 守卫)、T6(假绿测试)。全覆盖。✅
**2. Placeholder scan:** 无 TBD;每步给确切改动/命令/预期。T2/T3/T6 明确要求"先 Read/grep 核实真实成员/来源再改"(因这些是散落的现有代码,精确行号可能随文件微漂)。✅
**3. Type consistency:** `closeBySerial(int)` T1 声明+定义+onClose 调用一致;atomic 化保持初值语义;`exchange(false)` 与 precip 既有用法一致。✅
**已知边界:** T6 若原始点 ecef 生产路径在单测不可达,允许 DONE_WITH_CONCERNS(见 Step 1)。并发修复无确定性竞态复现,验收靠模板符合 + 无回归 + 无崩溃(见 Global Constraints)。
