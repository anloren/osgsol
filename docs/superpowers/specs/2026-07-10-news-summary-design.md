# 新闻摘要功能 设计文档 —— 2026-07-10

> 分支 `feat/acceptance-fixes`。用户已确认设计(交接文档 §4 + 本 session brainstorm)。
> 约束:不碰 globe GLSL;跑 app 带 `EARTH_OFFSCREEN=1`;禁止并发构建;commit 结尾
> `Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。

## 目标

点新闻热点标记(GDELT/GKG 源)当前只看到"地点 + 提及数",看不到内容。加"AI 摘要"
能力:让 AI 去源头抓文章正文并分析,**摘要出现在底部 AI 对话框**(经 AI 分析,不是详情卡里塞原文)。

## 三个组件

### 组件① 新 AI 工具 `get_news_content(url)` — `applications/earth_explorer/ai_world_tools.cpp`

给模型一个工具:给定新闻文章 URL,抓 HTML → 剥成正文文本 → 返回,供模型总结/分析。

**抓取路径(关键决策)**:**不复用 `toolFetchJson`**(`ai_world_tools.cpp:12-32`,其 `:28` 对 body 硬
`picojson::parse`,HTML 非 JSON 必失败返回 `bad json`)。直接调
`fetcher.query(url, ttlSeconds, fixturePath, body, err)`(`ai_query.h:29`)—— 该方法返回**原始 HTTP body
字符串**(写进 `bodyOut`),本身不解析 JSON,正是 HTML 需要的路径。底层缺省是 libhv 同步 GET(15s)。

**三态**(沿用全库"懒抓取 + pending 轮询"范式,与其它世界工具一致):
- `QueryState::Fetching` → 返回 `{"pending":true, "note":"数据抓取中,请稍候片刻后用相同参数再次调用本工具"}`
- `QueryState::Failed`   → 返回 `{"error":"fetch failed: <err>"}`
- `QueryState::Ready`    → 剥正文 → 返回 `{"content":"<stripped text>", "url":"<url>"}`

工具 handler **同步跑在主线程**(`ai_tools.h:17` 线程契约,由 `ai_chat.cpp:363-384` drainMainThread
逐个 dispatch),不能阻塞;`query()` cache miss 时立即入队返回 `Fetching`,真正抓取在后台 Worker 线程。
模型下一轮用相同 url 重调 → cache 命中 → `Ready` + body。dispatch 有集中 try/catch,工具自身不必 try/catch。

**HTML 剥正文**(新增 static 辅助 `stripHtmlToText(const std::string& html, size_t maxLen)`,纯 C++ string
操作,best-effort):
1. 整块删除 `<script>…</script>`、`<style>…</style>`、`<!--…-->`(大小写不敏感匹配起止标签)。
2. 删除所有剩余 `<…>` 标签。
3. 解码常见 HTML 实体:`&amp;` `&lt;` `&gt;` `&quot;` `&#39;` `&nbsp;`(其余原样保留,best-effort)。
4. 折叠连续空白(含换行/制表)为单空格,首尾 trim。
5. 截断到 `maxLen ≈ 4000` 字节(若截断,末尾追加 `…`)。

**输入校验**(url 来自模型/summary,不可信):
- 新增 static 辅助 `getStr(const picojson::value& a, const char* k, std::string& out)`(仿 `getNum` `:34`)。
- url 必须以 `http://` 或 `https://` 开头;长度 ≤ 2048;否则返回 `argError(...)`。
- 走 libhv GET(不进 shell),无 shell 逃逸风险,无需剥引号。

**已知坑 / best-effort 边界**(约定可接受):
- **gzip**:libhv 默认不解压(`gpsjam_feed.cpp` 注释踩过)。若 `body` 以 gzip 魔数 `0x1f 0x8b` 开头 →
  返回 `{"error":"内容为压缩格式,无法解析"}`,不尝试解压。
- 付费墙 / JS 渲染页(正文由 JS 注入)→ 剥出的正文可能很短或为骨架,best-effort,返回什么算什么。

**注册 & 参数**:
- 新增 `registerNewsContentTool(earthai::ToolRegistry*, earthai::AsyncJsonFetcher*)`,并在
  `registerWorldQueryTools`(`ai_world_tools.cpp:314-325`)追加一行调用。fetcher 已由 main 注入
  (`earth_main.cpp` 的 `worldQueryFetcher`)。
- `t.name = "get_news_content"`(全库工具名唯一,`add()` 无去重故名字别撞)。
- `t.parametersJson = "{\"type\":\"object\",\"properties\":{\"url\":{\"type\":\"string\"}},\"required\":[\"url\"]}"`。
- `t.description`:中文说明"给定新闻文章 URL,抓取其正文供总结/分析;pending=true 表示抓取中,请稍后用
  相同 url 再次调用"。
- fixture env = `EARTH_NEWS_FILE`(离线/单测本地 HTML 旁路);TTL = 1800s。

### 组件② 详情卡「AI 摘要」按钮 — `applications/earth_explorer/EarthControlUI.h`

**改动集中在一个 lambda**(通用"要素详情"卡的 `card.drawBody`,`EarthControlUI.h:479-501`)。

- 捕获列表 `[fs]` → `[fs, this]`(`this` = `EarthControlUI*`;lambda 在同帧 `_cardStack.draw()`
  `:517` 内同步执行、仍在 `runInternal` 栈帧,`this`/`_aiCore` 指针执行时保证有效)。
- 在现有「打开 Open」按钮同一 `if (!fs.url.empty())` 块内(`:485-500`),追加:
  ```cpp
  ImGui::SameLine();
  if (_aiCore && ImGui::Button(u8"AI 摘要"))
      _aiCore->submit(u8"请总结这条新闻热点(地点:" + fs.title +
          u8")的主要内容并分析其重要性。文章链接:" + fs.url +
          u8" —— 请先调用 get_news_content 抓取正文再总结。");
  ```
- `_aiCore`(`EarthControlUI.h:42`,`earthai::AIChatCore*`,由 main 注入)是同类成员,同作用域可直接访问。
- `AIChatCore::submit(const std::string& userText)`(`ai_chat.h:76`)—— 参数普通 `std::string`,主线程调,
  内部 busy 则忽略并提示,无 key 由既有逻辑处理。中文按 `u8"…"`(UTF-8)直接传。
- **按钮只在 `_aiCore != nullptr` 时出现**(无 AI 构建零干扰)。摘要落进**常驻底部 AI 对话条**历史
  (`EarthControlUI.h:513` 每帧画;历史面板默认展开、新条目自动滚到底 `ai_ui.h:36-37`)→ 用户即时可见。
  不需要额外"打开面板"逻辑。
- **不碰 `feed_layer.cpp` / `ai_chat.cpp`。**

**可用数据**:`FeedSelection`(`feed_layer.h:128-136`)只有 `title/detail/url/sourceId`(pick 时丢弃了
`FeedPoint.raw`)。GDELT 下 `fs.title`=地点名、`fs.detail`=含提及数文本、`fs.url`=allurls 首条。够拼 prompt。

### 组件③ `gdeltSummaryJson` 带 top 热点文章 URL — `applications/earth_explorer/feeds/gdelt_feed.cpp:101-132`

让 AI 光靠对话(不点按钮)也能拿到 top 热点的文章 URL 去分析。summary 由 AI 工具
`get_gdelt_summary`(`feed_layer.cpp:964-1001`)返回,**全量进 LLM 上下文无截断** → 必须控制体积。

- 排序结构从 `std::pair<double,std::string>`(`-cnt, name`)扩成同时携带 firstUrl(如
  `std::vector<std::tuple<double,std::string,std::string>>` 或等价 struct)。
- firstUrl 从 `pts[i].raw.get("properties").get("allurls")`(`is<std::string>()` 时,空格分隔多条,已经
  `sanitizeControlChars` 清洗)取**首条**(复用 `gdelt_feed.cpp:44-50` 的 `firstUrl` 语义:首个空格前子串)。
- top5(`i < 5` 硬编码)每项对象加 `url` 字段:`{name, mentions, url}`。
- **每热点只 1 条 URL**(payload 最小化决策:一个 allurls 可达几十条长 URL × 5 热点会撑爆无截断的
  payload;取首条与 `FeedPoint.url` 约定一致)。若某热点无 allurls,`url` 置空串。
- 同步更新工具描述文案(`gdelt_feed.cpp:150-152`,`toolDescriptionCn`,提到现在含 URL)。

## 测试计划

**单测(离线 fixture 驱动,零网络;`CHECK` 宏不用 `assert`)**:
- `tests/world_tools_tests.cpp` 新增 `get_news_content`:
  - `EARTH_NEWS_FILE` 指向本地 HTML fixture → `dispatch` → 断言 `content` 含预期正文词、**不含** `<tag>`、
    脚本/样式块被删、实体解码、截断生效(超长 fixture)。
  - 非 http url(如 `ftp://…` 或裸字符串)→ 返回 `error`。
  - pending 路径:无 fixture、真实抓取器 → 首调返回 `pending`(参照现有 fetcher 状态机测试
    `world_tools_tests.cpp:58-77` 的注入式 FetchFn)。
- `tests/feed_layer_tests.cpp:1003-1012`:扩 `gdeltSummaryJson` 断言 —— `topHotspots[i]` 含 `url` 字段
  且等于该热点 allurls 首条;无 allurls 的热点 `url` 为空。
- **被测 .cpp 若新增依赖**:凡 include 该 .cpp 的测试单元都要补齐定义,否则链接失败中断打包(踩过)。

**真机验收(离屏截不到 ImGui 卡片/按钮)**:
- 组件②「AI 摘要」按钮:用户真机点按钮 → 看摘要是否出现在 AI 对话条。
- 组件①③:靠单测 + log 探针(工具 handler 加 `OSG_NOTICE << "[AIChat] get_news_content " << url` 探针,
  仿现有工具)。

**离屏跑 app**:必带 `EARTH_OFFSCREEN=1`;低 LOD 网络流式需 `EARTH_FRAME_SLEEP_MS` 给 wall-clock。

## 触碰文件清单(均不碰 GLSL)

| 文件 | 改动 |
|---|---|
| `applications/earth_explorer/ai_world_tools.cpp` | 新增 `registerNewsContentTool` + `stripHtmlToText` + `getStr`;`registerWorldQueryTools` 追加注册 |
| `applications/earth_explorer/EarthControlUI.h` | `card.drawBody` 捕获 `[fs]→[fs,this]` + 加「AI 摘要」按钮 |
| `applications/earth_explorer/feeds/gdelt_feed.cpp` | `gdeltSummaryJson` 加 `url` 字段 + 工具描述更新 |
| `tests/world_tools_tests.cpp` | 新工具单测 |
| `tests/feed_layer_tests.cpp` | summary url 字段断言 |

## 已定默认值(用户已确认)

- 每热点 **1 条 URL**(summary)
- HTML 正文截断 **~4000 字**
- 工具 TTL **1800s**
- 按钮 prompt 措辞见组件②

## 关键上下文(执行时别踩坑)

- 构建:`cmake --build build/verse_core --target install -j4`,**禁止并发第二个构建**。app 在 `build/sdk_core/bin`。
- 打包:`bash packaging/package_macos.sh` → `codesign -v dist/EarthExplorer.app` 确认(签名失效秒退)。
- GDELT/GKG 端点 TLS 偶发闪断,离屏验证多试几次;别高频 curl(限流 1/5s)。
- OSG 日志宏 `OSG_WARN/OSG_NOTICE` 带 if,if/else 里不加大括号会吞 else。
