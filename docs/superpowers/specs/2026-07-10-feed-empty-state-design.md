# Feed 空态可见 设计文档 —— 2026-07-10

> 分支 `feat/acceptance-fixes`。起因:真机验收时「飓风数据似乎不通」。
> 根因调查(systematic-debugging)结论:**NHC feed 没坏**——端点 HTTP 200 返回
> `{"features":[]}`,独立源 NHC CurrentStorms.json = `{"activeStorms":[]}` 证实当前全球
> 无活跃热带气旋。app 把 200 记为 `_fetchState=1`(成功),故不画「⚠ 抓取失败」。用户看到
> 0 标记 → 误判"不通"。**真实缺口 = 成功但空 与 坏掉 在面板上不可分。**
> 约束:不碰 GLSL;禁止并发构建;CHECK 宏;commit 结尾 `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。

## 目标

给**所有** FeedLayer 数据源(不止飓风)加一个中性的"抓取成功但当前 0 要素"图层行指示,
让"空但正常"与"抓取失败/加载中"一眼可分。

## 设计:图层行状态 3 态 → 4 态

现有三态(`feed_layer.cpp` 三态 fetchState + `EarthControlUI.h:283-298` 消费):
`0=未抓/idle→"加载中…"`;`1=成功→(不提示)`;`2=失败→"⚠ 抓取失败"+tooltip`。

新增 **state 3 = 抓取成功但 0 要素**。

### 判据:纯函数 helper(可 TDD)
`feed_layer.cpp` 新增(namespace earthfeed,static,测试 #include 该 .cpp 可直测):
```cpp
// 图层目录 UI 展示态:在三态抓取状态上把"成功但 0 要素"细分为 state 3,
// 让"抓取成功当前无数据(非故障)"与"失败/加载中"在面板上一眼可分。通用于所有 feed。
static int feedDisplayState(int fetchState, size_t recordCount)
{
    if (fetchState == 1 && recordCount == 0) return 3;   // 成功但空
    return fetchState;                                    // 0/1/2 原样透传
}
```

### 访问器:`recordCount()`
feed impl 加线程安全 const 访问器(仿 `lastErrorText()`;`_mutex` 改 `mutable` —— `_errMutex`
已是 mutable,同源约定):
```cpp
size_t recordCount() const { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); return _records.size(); }
```

### 回调接线
`feed_layer.cpp:948` 的 `l.fetchStatus` 回调返回值从 `implPtr->fetchState()` 改为
`earthfeed::feedDisplayState(implPtr->fetchState(), implPtr->recordCount())`。

### UI 分支
`EarthControlUI.h` 在 `else if (fetchSt == 0)` 之后加:
```cpp
else if (fetchSt == 3)
{
    ImGui::SameLine();
    ImGui::TextDisabled(u8"· 当前无数据");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"抓取成功,当前无数据(非故障)");
}
```
样式:`TextDisabled`(灰色柔和,同"加载中…"),非告警色(区别于失败的红字)。

## 覆盖面

所有 FeedLayer 源(nhc/gdelt/quakes/eonet/gpsjam/gdacs/unhcr/strategic/firms)都经同一
`registerFeedLayer` 接 `fetchStatus` → **自动全覆盖**。瓦片叠加层(OverlayLayer)不设
fetchStatus → 天然不受影响。

## 文案

统一「· 当前无数据」(诚实+普适)。NHC 那种"无活跃风暴"定制文案属可选后续,先不做(YAGNI)。

## 测试

- **单测**(TDD,`feed_layer_tests.cpp`,target `osgVerse_Test_Feeds`):`earthfeed::feedDisplayState`
  4 例 —— `(1,0)→3`、`(1,5)→1`、`(2,0)→2`、`(0,0)→0`。用 `CHECK` 宏。
- `recordCount()` 访问器 + UI 分支:编译验证 + 真机(离屏截不到 ImGui 图层行)。

## 触碰文件(均不碰 GLSL)

| 文件 | 改动 |
|---|---|
| `applications/earth_explorer/feed_layer.cpp` | `feedDisplayState` helper + `recordCount()` + `_mutex` 改 mutable + 回调接线 |
| `applications/earth_explorer/EarthControlUI.h` | UI 加 `else if (fetchSt==3)` 分支 |
| `applications/earth_explorer/LayerManager.h` | `fetchStatus` 注释更 0/1/2 → 0/1/2/3 |
| `tests/feed_layer_tests.cpp` | `feedDisplayState` 单测 4 例 |

## 关键上下文

- 构建:`cmake --build build/verse_core --target osgVerse_Test_Feeds -j4`(单测)+ install 全量(UI)。
- NOAA 端点 TLS 偶发闪断(调查中 layer6 曾 SSL_ERROR_SYSCALL),app 有 feed 重试自愈,与本改进无关。
