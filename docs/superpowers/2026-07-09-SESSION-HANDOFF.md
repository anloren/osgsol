# Session 交接文档 —— 2026-07-09(接续用)

> 上个 session 上下文将满,此文档 + memory 是下一个 session 的**唯一权威状态源**。先读 memory 索引,再读本文件。

## 1. 分支/提交状态(精确)

- **master** = `ea1640e1`(本地,比 origin 领先 1 = 那条 review+P0plan docs,未推)。origin/master = `bc51a749`(v0.21 LOD 丝滑化,已推)。
- **feat/p0-concurrency-safety** = `54f1d729` —— P0 并发安全冲刺 6 提交(叠在 master ea1640e1 上)。已过全部审查 + 终审 Yes。
- **feat/acceptance-fixes** = `a5243537` —— **当前工作分支**,叠在 p0 之上,共 **19 提交相对 master**。含:P0 冲刺 6 + 验收批次(plan + 6 任务 + 修复波)+ 轨道线修复 + 失败可见(A)+ GDELT 切换 + feed 超时重试。

**分支栈**:`master(ea1640e1) → feat/p0-concurrency-safety(6) → feat/acceptance-fixes(+13)`。合并时 ff `feat/acceptance-fixes` 进 master 即把全部带上。

## 2. 已完成(都在 feat/acceptance-fixes,未合 master,已打包进 dist/.app)

**P0 并发安全冲刺**(6):AICardPanel `_cards` UAF 加锁+按值捕获(唯一 high)/ flight-sat DYNAMIC / 跨线程 atomic 批 / ai_query try-catch+清inflight / check() 迭代器守卫 / testPerSpecLiftMeters 去假绿。终审 opus=Ready to merge。

**真机验收批次**:
- earthcfg 运行时配置注册表(deque + atomic value)+ 设置面板(控制台「设置 Settings」)
- HTTP 连接失败自动重试+退避+提示分类(ai_chat/ai_media);工具循环上限 6→配置默认30 + 超限优雅收尾(无工具最终回答);角标 ~5s 自消失 + 不透明层提示关闭
- 🔴 相机穿地防御性硬修:硬海平面地板(**探针未命中时才兜底**,门控见 9ad18f18)。终审确认 Step1 足够,Pipeline clamper 无需改。**必须真机四件套验收**(全球/低空/极点/倾角不回归)
- 「干净」预设残留 ISS/天宫轨道线 → `if(_catStation)` 门控清除
- 失败可见(A):FeedLayer 三态 fetchState + OverlayLayer.fetchStatus 回调 + 图层行「⚠ 抓取失败」红字+tooltip / 「加载中…」
- **GDELT 新闻热点切源**:GEO 2.0 上游 404 死透 → 切到 **GKG GeoJSON 1.0**(`api/v1/gkg_geojson?QUERY=&OUTPUTTYPE=2&TIMESPAN=60`,无 key,`sumtotalmentions`=强度)。坑:allurls 裸 TAB 字节让 picojson 整包丢 → 加 `sanitizeControlChars()`;离屏实测 N=99~119。
- feed 抓取加 per-source 超时(FeedSpec.timeoutSeconds,GDELT=40)+ 连接层重试(读 http.retries)。边缘:关闭时最坏 join ~163s(建议后续 retry 间查 `_done` 收敛)

## 3. 待办(下一个 session 按序)

### 3a. 先做:新闻摘要功能(用户已确认设计,见 §4)—— 这是下一步主任务

### 3b. 然后:收尾合并(需用户先真机验收)
1. 用户真机验收(尤其 **相机穿地** 四件套 + **新闻热点标记**是否出来)。
2. 跑**全分支终审**(feat/acceptance-fixes 相对 master,19 提交,量大——用 code-reviewer/opus)。
3. 终审修完 → ff 合 `feat/acceptance-fixes` → master → push origin → 打 tag(建议 **v0.22**,含 P0+验收批+新闻源切换+新闻摘要)。
4. 清理:删两个 feature 分支;`.superpowers/sdd/` 是 gitignore scratch。

### 3c. 遗留 backlog(docs/superpowers/2026-07-08-project-review-optimization-backlog.md)
review 出的 19 项优化尚有 P1-P3 未做(pickAt 陈旧快照命中 / DRY 巨型文件拆分 / 死代码清理 / 更多测试 / 终审发现的 syncIfDirty 结构改图×draw 竞态)。按需取用。

## 4. 新闻摘要功能规格(用户已确认:摘要出现在 AI 对话框,经 AI 分析)

**目标**:点新闻热点标记只看到"地点+提及数",看不到内容。加"AI 摘要"按钮去源头抓正文,让用户和 AI 都能读/分析。

**确认的设计**:摘要结果**出现在 AI 对话框**(经 AI 分析),不是详情卡里塞原文。

**三个组件**:
1. **新 AI 工具 `get_news_content(url)`**:抓取文章 HTML → 剥成正文文本 → 返回给模型。用 `AsyncJsonFetcher`(它的 query() 返回原始 body;HTML 不是 JSON,需确认有"取原始 body"的路径,或用 toolFetchJson 之外的抓取)。剥标签用简单 strip(去 `<...>`、collapse 空白、截断到合理长度如 ~4000 字防 payload 爆)。返回 picojson `{content, url}`。工具描述告诉模型"给定新闻文章 URL,返回其正文供总结分析"。参考现有工具:`ai_world_tools.cpp` 的 registerWeatherTool 等 + `toolFetchJson`(:12)。
2. **详情卡「AI 摘要」按钮**:先 **grep 找 feed 标记选中→详情卡渲染处**(pickAt 设 `_selected`/`s.url` 在 feed_layer.cpp:456/488-489;卡片渲染 + "打开"按钮先例要找到,可能在 ui_card / EarthControlUI / feed pick handler)。按钮点击 → `aiCore->submit(u8"总结这条新闻热点(" + 地点 + u8")的内容:" + url)`。需要卡片渲染处能拿到 `AIChatCore*`(EarthControlUI 有 `_aiCore`)+ 选中热点的 url。AI 收到消息 → 调 `get_news_content(url)` → 摘要 → 显示在对话。
3. **gdeltSummaryJson 带上 top 热点的文章 URL**(feeds/gdelt_feed.cpp:69-99;GKG allurls 已有),让 AI 光靠对话(不点按钮)也能分析新闻内容。

**约束**:不碰 GLSL;AI 工具结果要截断防 payload 过大;抓取走异步别卡主线程;HTML 剥正文是 best-effort(付费墙/JS 渲染页会剥不干净,可接受)。

**建议流程**:brainstorm 细化(尤其"按钮拿 AIChatCore 与选中 url 的接线"、"HTML 剥正文法")→ writing-plans → subagent-driven → 打包真机验。

## 5. 关键上下文(别踩坑)
- 跑 app 必带 `EARTH_OFFSCREEN=1`;app 在 `build/sdk_core/bin`;构建 `cmake --build build/verse_core --target install -j4`,**禁止并发第二个构建**。
- 离屏**截不到 ImGui 面板/卡片/角标** → UI 类验证靠单测 + log 探针 + 真机。
- 离屏低 LOD 网络流式加载需 `EARTH_FRAME_SLEEP_MS` 给 wall-clock(否则 300 帧 ~1.5s 跑完等不到抓取)。
- 测试用 `CHECK` 宏不用 `assert`(NDEBUG 吞 assert)。
- commit 结尾 `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。
- 打包:`bash packaging/package_macos.sh`;之后 `codesign -v dist/EarthExplorer.app` 确认(签名失效会秒退)。
- GDELT/GKG 端点 TLS 偶发闪断,离屏验证多试几次;别高频 curl 会被限流(1/5s)。
