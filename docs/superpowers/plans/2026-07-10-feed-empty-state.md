# Feed 空态可见 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给所有 FeedLayer 源加一个中性"抓取成功但当前 0 要素"图层行指示(state 3),让"空但正常"与"失败/加载中"一眼可分。

**Architecture:** 现有 fetchStatus 回调返回 0/1/2 三态;新增纯函数 `feedDisplayState(fetchState, recordCount)` 把"成功且 0 要素"细分为 state 3;回调改走它;`EarthControlUI.h` 加 state 3 的中性灰字分支。

**Tech Stack:** C++17 / OSG / ImGui / OpenThreads::Mutex。

## Global Constraints

- 不碰 globe GLSL / 任何着色器。
- 禁止并发构建(同时刻只跑一个 `cmake --build`)。
- 测试用 `CHECK` 宏,不用 `assert`。
- OSG 日志宏 `OSG_WARN/OSG_NOTICE` 带隐藏 if,分支加大括号。
- commit message 结尾精确一行:`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`
- 单测 target `osgVerse_Test_Feeds`;`tests/feed_layer_tests.cpp` 已 `#include feed_layer.cpp`(第 25 行),故 `earthfeed::feedDisplayState`(feed_layer.cpp 内 namespace earthfeed 的 static)可直测,无需改 CMake。

---

### Task 1: feed 空态 state 3 + 通用图层行指示

**Files:**
- Modify: `applications/earth_explorer/feed_layer.cpp`(加 `feedDisplayState` + `recordCount()` + `_mutex` 改 mutable + 回调接线)
- Modify: `applications/earth_explorer/EarthControlUI.h`(UI 加 `else if (fetchSt==3)` 分支)
- Modify: `applications/earth_explorer/LayerManager.h`(`fetchStatus` 注释更 0/1/2→0/1/2/3)
- Test: `tests/feed_layer_tests.cpp`(`feedDisplayState` 4 例)

**Interfaces:**
- Produces: `static int earthfeed::feedDisplayState(int fetchState, size_t recordCount)` —— `(fetchState==1 && recordCount==0)→3`,否则原样返回 `fetchState`。
- Produces: `size_t FeedLayerImpl::recordCount() const` —— 锁 `_mutex` 读 `_records.size()`。

- [ ] **Step 1: 写失败测试**

在 `tests/feed_layer_tests.cpp` 里,找到现有 gdeltSummaryJson 测试函数(约 :1003 附近,`earthfeed::gdeltSummaryJson` 那个测试块)所在的 test 函数之后 / 或任一被 main 调用的 test 函数体内,加入一段(并确保它被 main 调用——若新开函数,记得在 main 里调它):

```cpp
    // feedDisplayState:成功且 0 要素 → state 3(通用空态指示),其余原样透传
    CHECK(earthfeed::feedDisplayState(1, 0) == 3);   // 抓取成功但空
    CHECK(earthfeed::feedDisplayState(1, 5) == 1);   // 成功有数据
    CHECK(earthfeed::feedDisplayState(2, 0) == 2);   // 失败原样
    CHECK(earthfeed::feedDisplayState(0, 0) == 0);   // idle 原样
    std::cout << "[OK] feedDisplayState empty-state\n";
```

- [ ] **Step 2: 构建确认 FAIL**

Run: `cmake --build build/verse_core --target osgVerse_Test_Feeds -j4`
Expected: 编译 FAIL —— `earthfeed::feedDisplayState` 未声明(use of undeclared identifier)。

- [ ] **Step 3: 实现 feedDisplayState + recordCount + _mutex mutable**

在 `applications/earth_explorer/feed_layer.cpp`:

3a. 找到 `OpenThreads::Mutex _mutex;`(约 :680),改为:
```cpp
        mutable OpenThreads::Mutex _mutex;
```
(`_errMutex` 已是 mutable,同源约定;`mutable` 只放宽 const 锁定,不改行为。)

3b. 在 `int fetchState() const { return _fetchState; }`(约 :596)附近、类内 public 区,加线程安全 const 访问器:
```cpp
        // 空态指示用:当前已解析要素数(加锁读,仿 lastErrorText 的 const 加锁读)。
        size_t recordCount() const { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); return _records.size(); }
```

3c. 在 namespace earthfeed 作用域内(与 gdeltSummaryJson 等纯函数同层,`registerFeedLayer` 定义之前的某处),加纯函数:
```cpp
    // 图层目录 UI 展示态:在三态抓取状态(0=idle/1=ok/2=失败)上,把"成功但 0 要素"
    // 细分为 state 3,让"抓取成功当前无数据(非故障)"与"失败/加载中"在面板上一眼可分。
    // 通用于所有 FeedLayer 源(都经 registerFeedLayer 接 fetchStatus)。
    static int feedDisplayState(int fetchState, size_t recordCount)
    {
        if (fetchState == 1 && recordCount == 0) return 3;   // 成功但空
        return fetchState;                                    // 0/1/2 原样透传
    }
```

- [ ] **Step 4: 构建确认 feedDisplayState 测试 PASS**

Run: `cmake --build build/verse_core --target osgVerse_Test_Feeds -j4 && ./build/verse_core/bin/osgVerse_Test_Feeds`
Expected: 编译通过;运行含 `[OK] feedDisplayState empty-state`,无 `CHECK failed`,退出码 0。

- [ ] **Step 5: 回调接线 —— 让 state 3 真正产出**

在 `applications/earth_explorer/feed_layer.cpp` 的 `l.fetchStatus` 回调(:948-953),整段替换。
当前:
```cpp
        l.fetchStatus = [implPtr](std::string& out) -> int
        {
            int s = implPtr->fetchState();
            if (s == 2) out = implPtr->lastErrorText();
            return s;
        };
```
改为:
```cpp
        l.fetchStatus = [implPtr](std::string& out) -> int
        {
            int s = earthfeed::feedDisplayState(implPtr->fetchState(), implPtr->recordCount());
            if (s == 2) out = implPtr->lastErrorText();
            return s;
        };
```
(`feedDisplayState` 对 2 原样透传,故 `if (s==2)` 填 errText 的失败分支语义不变;新 state 3 仅在
fetchState==1 时产生,永不与失败态冲突。同步把 :946-947 那句"返回 0/1/2"注释更新为 0/1/2/3。)

- [ ] **Step 6: UI 加 state 3 分支 + 注释更新**

6a. `applications/earth_explorer/EarthControlUI.h`,在 `else if (fetchSt == 0) { ... "加载中…" }`
块(约 :293-297)之后,加:
```cpp
                            else if (fetchSt == 3)
                            {
                                ImGui::SameLine();
                                ImGui::TextDisabled(u8"· 当前无数据");
                                if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"抓取成功,当前无数据(非故障)");
                            }
```
并把该块上方注释(约 :280-282,列举 0/1/2 语义处)补一行说明 `3=成功但 0 要素→"· 当前无数据"`。

6b. `applications/earth_explorer/LayerManager.h`:找到 `std::function<int(std::string&)> fetchStatus;`(约 :29)
的注释,把返回码说明从 `0/1/2` 更新为 `0/1/2/3`(3=成功但空)。

- [ ] **Step 7: 全量构建确认编译链接**

Run: `cmake --build build/verse_core --target install -j4`
Expected: 编译链接成功无 error。**不要运行 app**(会跳前台)。

- [ ] **Step 8: Commit**

```bash
git add applications/earth_explorer/feed_layer.cpp applications/earth_explorer/EarthControlUI.h applications/earth_explorer/LayerManager.h tests/feed_layer_tests.cpp
git commit -m "feat(earth-feed): 图层行加'当前无数据'空态指示(成功但0要素)

飓风等 feed 抓取成功(200)但当前 0 要素时,面板与失败/加载中不可分,
易被误判'不通'。新增纯函数 feedDisplayState 把'成功且0要素'细分为 state 3,
所有 FeedLayer 源经 fetchStatus 回调自动覆盖,UI 显中性灰字'· 当前无数据'+tooltip。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```
