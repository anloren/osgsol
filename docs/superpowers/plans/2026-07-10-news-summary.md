# 新闻摘要功能 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 EarthExplorer 加"新闻摘要"能力:AI 能抓新闻文章正文并分析,摘要出现在底部 AI 对话框。

**Architecture:** 三个独立组件。① 新 AI 工具 `get_news_content(url)` 直调 `AsyncJsonFetcher::query` 拿 HTML 原始 body(绕开会硬解析 JSON 的 `toolFetchJson`),纯 C++ 剥标签成正文返回给模型。② 详情卡「AI 摘要」按钮把 `_aiCore->submit(地点+url)` 交给 AI,AI 自行调 ①。③ `gdeltSummaryJson` 每个 top 热点带上代表文章 URL,让 AI 光靠对话也能拿到 URL。

**Tech Stack:** C++、picojson、libhv(经 `AsyncJsonFetcher` 封装)、ImGui、OSG。测试沿用现有 `#include 被测.cpp + CHECK 宏` 离线单测体系。

## Global Constraints

- 不碰 globe GLSL(任何 `.glsl` / 着色器字符串一律不动)。
- 跑 app 必带 `EARTH_OFFSCREEN=1`;app 在 `build/sdk_core/bin`。
- **禁止并发构建**:同一时刻只允许一个 `cmake --build`。
- 测试用 `CHECK` 宏,不用 `assert`(NDEBUG 吞 assert)。
- commit 结尾必须是:`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`。
- 分支 `feat/acceptance-fixes`,别碰 master。
- AI 工具结果要截断防 payload 过大;抓取走 `AsyncJsonFetcher` 异步别卡主线程。
- OSG 日志宏 `OSG_WARN/OSG_NOTICE` 带 if,if/else 分支里不加大括号会吞 else。

## File Structure

| 文件 | 职责 | 本计划改动 |
|---|---|---|
| `applications/earth_explorer/ai_world_tools.cpp` | AI 世界工具族注册 | Task 1:加 `getStr` + `stripHtmlToText` + `registerNewsContentTool`,并 wire 进 `registerWorldQueryTools` |
| `tests/world_tools_tests.cpp` | 世界工具离线单测(#include ai_world_tools.cpp) | Task 1:加 `testNewsContentTool` |
| `applications/earth_explorer/feeds/gdelt_feed.cpp` | GDELT/GKG feed + summary | Task 2:`gdeltSummaryJson` 每 top 热点加 `url` 字段 + 工具描述更新 |
| `tests/feed_layer_tests.cpp` | feed 离线单测(#include gdelt_feed.cpp) | Task 2:扩 summary 断言 `url` |
| `applications/earth_explorer/EarthControlUI.h` | 右上角控制 UI + 详情卡渲染 | Task 3:详情卡 lambda 捕获 `[fs]→[fs,this]` + 加「AI 摘要」按钮 |

**任务顺序**:Task 1 → Task 2 →(两者可测,独立)→ Task 3(纯 ImGui,无单测,靠编译 + 真机)。

**测试构建/运行速查**(每个 test #include 了被测 .cpp,改被测 .cpp 会触发 test 重编):
- WorldTools:`cmake --build build/verse_core --target osgVerse_Test_WorldTools -j4` → `./build/verse_core/bin/osgVerse_Test_WorldTools`(成功打印 `ALL WORLD-TOOLS TESTS PASSED`)
- Feeds:`cmake --build build/verse_core --target osgVerse_Test_Feeds -j4` → `./build/verse_core/bin/osgVerse_Test_Feeds`(成功:无 `CHECK failed`、退出码 0)

---

## Task 1: AI 工具 `get_news_content(url)`

**Files:**
- Modify: `applications/earth_explorer/ai_world_tools.cpp`(顶部加 `#include <cctype>`;加 `getStr`/`stripHtmlToText`/`removeHtmlBlock`/`registerNewsContentTool`;`registerWorldQueryTools:324` 后加一行)
- Test: `tests/world_tools_tests.cpp`(加 `testNewsContentTool`,并在 `main` 调用)

**Interfaces:**
- Consumes:`earthai::AsyncJsonFetcher::query(url, ttl, fixturePath, bodyOut&, errOut&) -> QueryState`(`ai_query.h:29`);`earthai::ToolRegistry::add(Tool)` / `dispatch(name, args, out&)`;现有 `argError(const char*)`(`ai_world_tools.cpp:40`)。
- Produces:注册名为 `get_news_content` 的工具;参数 `{url:string}`;返回 `{content:string, url:string}` 或 `{pending:true,note}` 或 `{error:string}`。供 Task 3 的按钮间接触发(AI 自调)。

- [ ] **Step 1: 写失败测试 `testNewsContentTool`**

在 `tests/world_tools_tests.cpp` 的 `int main` 之前(约 :211 后)加入下面的函数:

```cpp
// ===== 新闻正文抓取工具(注入式 fetch 驱动,零网络) =====
static void testNewsContentTool()
{
    // 注入 fake fetch:返回一段含 script/style/实体/多空白的 HTML;url 含 "bad" → 失败;
    // url 含 "long" → 返回 5000 字纯文本(测截断)。
    earthai::AsyncJsonFetcher fx([](const std::string& url, std::string& b, std::string& e) {
        if (url.find("bad") != std::string::npos) { e = "boom"; return false; }
        if (url.find("long") != std::string::npos) { b = std::string(5000, 'A'); return true; }
        b = "<html><head><style>.x{color:red}</style></head>"
            "<body><script>var a=1;</script><h1>Big News</h1>"
            "<p>Hello &amp; welcome to &lt;Earth&gt;.</p>"
            "<div>  Multiple    spaces   here </div></body></html>";
        return true;
    });
    earthai::ToolRegistry reg;
    registerWorldQueryTools(&reg, NULL, &fx);
    picojson::value args, out;
    CHECK(picojson::parse(args, "{\"url\":\"https://news.example.com/a\"}").empty());
    // 首调:抓取中 → pending(三态透传)
    CHECK(reg.dispatch("get_news_content", args, out));
    CHECK(out.get("pending").get<bool>() == true);
    fx.waitIdleForTest();
    // 再调:Ready → content 已剥正文
    CHECK(reg.dispatch("get_news_content", args, out));
    CHECK(out.contains("content"));
    std::string c = out.get("content").get<std::string>();
    CHECK(c.find("Big News") != std::string::npos);                       // 正文保留
    CHECK(c.find("Hello & welcome to <Earth>.") != std::string::npos);    // 实体解码 &amp;&lt;&gt;
    CHECK(c.find("Multiple spaces here") != std::string::npos);           // 连续空白折叠
    CHECK(c.find("var a=1") == std::string::npos);                        // <script> 块整删
    CHECK(c.find("color:red") == std::string::npos);                      // <style> 块整删
    CHECK(c.find("<h1>") == std::string::npos);                           // 标签删除
    CHECK(out.get("url").get<std::string>() == "https://news.example.com/a");
    // 截断:5000 字 → ~4000 字 + 省略号
    picojson::value la, lo; CHECK(picojson::parse(la, "{\"url\":\"https://long.example.com\"}").empty());
    CHECK(reg.dispatch("get_news_content", la, lo)); fx.waitIdleForTest();
    CHECK(reg.dispatch("get_news_content", la, lo));
    std::string lc = lo.get("content").get<std::string>();
    CHECK(lc.size() <= 4100);                                             // 4000 + "…"(UTF-8 3字节)余量
    CHECK(lc.find("\xE2\x80\xA6") != std::string::npos);                  // 末尾 …
    // 非 http(s) url → error
    picojson::value bad, bout; CHECK(picojson::parse(bad, "{\"url\":\"ftp://x/y\"}").empty());
    reg.dispatch("get_news_content", bad, bout);
    CHECK(bout.contains("error"));
    // 缺 url 参数 → error
    picojson::value none, nout; CHECK(picojson::parse(none, "{}").empty());
    reg.dispatch("get_news_content", none, nout);
    CHECK(nout.contains("error"));
    // 抓取失败源 → error
    picojson::value ba, bo; CHECK(picojson::parse(ba, "{\"url\":\"https://bad.example.com\"}").empty());
    CHECK(reg.dispatch("get_news_content", ba, bo)); fx.waitIdleForTest();
    CHECK(reg.dispatch("get_news_content", ba, bo));
    CHECK(bo.contains("error"));
    std::cout << "[OK] news content tool\n";
}
```

并在 `int main` 里 `testRegionBriefTool();`(:220)后加一行:

```cpp
    testNewsContentTool();
```

- [ ] **Step 2: 构建测试并确认 FAIL**

Run: `cmake --build build/verse_core --target osgVerse_Test_WorldTools -j4 && ./build/verse_core/bin/osgVerse_Test_WorldTools`
Expected: 编译通过(测试只用了已存在的符号),运行时 **FAIL** —— `dispatch("get_news_content", ...)` 因工具未注册返回 false,第一个 `CHECK(reg.dispatch(...))` 处打印 `CHECK failed at .../world_tools_tests.cpp:NNN`。

- [ ] **Step 3: 实现工具(在 `ai_world_tools.cpp`)**

3a. 文件顶部 include 区(`:1-8`)加一行:

```cpp
#include <cctype>
```

3b. 在 `getNum`(`:34-38`)之后加字符串取值器:

```cpp
static bool getStr(const picojson::value& a, const char* k, std::string& out)
{
    if (!a.is<picojson::object>() || !a.contains(k) || !a.get(k).is<std::string>()) return false;
    out = a.get(k).get<std::string>(); return true;
}
```

3c. 在 `argError`(`:40-41`)之后加 HTML 剥正文辅助:

```cpp
// 删除 s 中 [openLow..closeLow] 的整块(含起止标签,大小写不敏感),反复删除所有出现。
// openLow/closeLow 传小写;s 与其小写镜像同步删除,保持位置对齐。
static void removeHtmlBlock(std::string& s, const char* openLow, const char* closeLow)
{
    std::string low = s;
    for (size_t i = 0; i < low.size(); ++i) low[i] = (char)tolower((unsigned char)low[i]);
    std::string ol = openLow, cl = closeLow;
    size_t pos = 0;
    while ((pos = low.find(ol, pos)) != std::string::npos)
    {
        size_t end = low.find(cl, pos);
        if (end == std::string::npos) { s.erase(pos); low.erase(pos); break; }  // 无闭合 → 删到尾
        end += cl.size();
        s.erase(pos, end - pos); low.erase(pos, end - pos);
    }
}

// HTML → 纯正文(best-effort):删 script/style/注释块 → 删所有标签 → 解码常见实体 →
// 折叠空白 → 截断到 maxLen(UTF-8 边界安全)。
static std::string stripHtmlToText(const std::string& html, size_t maxLen)
{
    std::string s = html;
    removeHtmlBlock(s, "<script", "</script>");
    removeHtmlBlock(s, "<style", "</style>");
    removeHtmlBlock(s, "<!--", "-->");
    // 删所有 <...> 标签
    std::string noTags; noTags.reserve(s.size());
    bool inTag = false;
    for (size_t i = 0; i < s.size(); ++i)
    {
        char c = s[i];
        if (c == '<') inTag = true;
        else if (c == '>') inTag = false;
        else if (!inTag) noTags.push_back(c);
    }
    // 解码常见实体
    static const char* ents[][2] = {
        {"&amp;","&"},{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},
        {"&#39;","'"},{"&apos;","'"},{"&nbsp;"," "}
    };
    for (size_t k = 0; k < sizeof(ents)/sizeof(ents[0]); ++k)
    {
        std::string from = ents[k][0], to = ents[k][1]; size_t p = 0;
        while ((p = noTags.find(from, p)) != std::string::npos)
        { noTags.replace(p, from.size(), to); p += to.size(); }
    }
    // 折叠连续空白为单空格,吃掉前导空白
    std::string out; out.reserve(noTags.size());
    bool prevSpace = true;
    for (size_t i = 0; i < noTags.size(); ++i)
    {
        char c = noTags[i];
        bool sp = (c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'||c=='\v');
        if (sp) { if (!prevSpace) out.push_back(' '); prevSpace = true; }
        else { out.push_back(c); prevSpace = false; }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();   // 尾部 trim
    // 截断到 maxLen(UTF-8 边界安全:去掉被切断的多字节序列尾巴)
    if (out.size() > maxLen)
    {
        out.resize(maxLen);
        while (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0x80) out.pop_back(); // 续字节
        if (!out.empty() && ((unsigned char)out.back() & 0xC0) == 0xC0) out.pop_back();     // 被切的首字节
        out += "\xE2\x80\xA6";   // …
    }
    return out;
}
```

3d. 在 `registerRegionBriefTool` 与 `registerWorldQueryTools` 之间(约 `:312` 后)加工具注册函数:

```cpp
// ---- get_news_content(抓新闻文章正文供总结;直调 fetcher.query 拿 HTML 原始 body,不走 toolFetchJson) ----
static void registerNewsContentTool(earthai::ToolRegistry* tools, earthai::AsyncJsonFetcher* fetcher)
{
    earthai::Tool t; t.name = "get_news_content";
    t.description = u8"给定一条新闻文章的 URL,抓取其网页正文文本(已去除 HTML 标签)供总结与分析。"
        u8"典型用途:拿到新闻热点(get_gdelt_summary 返回的 topHotspots[].url)的 URL 后,用本工具读取正文。"
        u8"若返回 pending=true 表示正在抓取,请稍候片刻后用相同 url 再次调用本工具。"
        u8"付费墙或需 JS 渲染的页面可能只能取到部分正文(best-effort)。";
    t.parametersJson = "{\"type\":\"object\",\"properties\":{"
        "\"url\":{\"type\":\"string\"}},\"required\":[\"url\"]}";
    earthai::AsyncJsonFetcher* f = fetcher;
    t.execute = [f](const picojson::value& a) {
        std::string url;
        if (!getStr(a, "url", url)) return argError("need string url");
        bool okProto = url.compare(0, 7, "http://") == 0 || url.compare(0, 8, "https://") == 0;
        if (!okProto) return argError("url must start with http:// or https://");
        if (url.size() > 2048) return argError("url too long (max 2048)");
        const char* fx = getenv("EARTH_NEWS_FILE");
        std::string body, err;
        earthai::QueryState st = f->query(url, 1800.0,
            (fx && *fx) ? std::string(fx) : std::string(), body, err);
        picojson::object r;
        if (st == earthai::QueryState::Fetching)
        {
            r["pending"] = picojson::value(true);
            r["note"] = picojson::value(std::string(u8"正文抓取中,请稍等片刻后用相同 url 再次调用本工具"));
            return picojson::value(r);
        }
        if (st == earthai::QueryState::Failed)
        { r["error"] = picojson::value("fetch failed: " + err); return picojson::value(r); }
        // libhv 默认不解压 gzip(gpsjam_feed.cpp 踩过):魔数 1f 8b → 直接报错,不尝试解压
        if (body.size() >= 2 && (unsigned char)body[0] == 0x1f && (unsigned char)body[1] == 0x8b)
        { r["error"] = picojson::value(std::string(u8"内容为压缩格式(gzip),无法解析")); return picojson::value(r); }
        OSG_NOTICE << "[AIChat] get_news_content " << url << " (" << body.size() << "B)" << std::endl;
        r["url"] = picojson::value(url);
        r["content"] = picojson::value(stripHtmlToText(body, 4000));
        return picojson::value(r);
    };
    tools->add(t);
}
```

3e. 在 `registerWorldQueryTools` 里 `registerRegionBriefTool(tools, fetcher);`(`:324`)之后加一行:

```cpp
    registerNewsContentTool(tools, fetcher);
```

- [ ] **Step 4: 构建测试并确认 PASS**

Run: `cmake --build build/verse_core --target osgVerse_Test_WorldTools -j4 && ./build/verse_core/bin/osgVerse_Test_WorldTools`
Expected: PASS —— 打印 `[OK] news content tool` 和末行 `ALL WORLD-TOOLS TESTS PASSED`,退出码 0。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/ai_world_tools.cpp tests/world_tools_tests.cpp
git commit -m "feat(earth): 新增 AI 工具 get_news_content(url) 抓文章剥正文

直调 AsyncJsonFetcher::query 拿 HTML 原始 body(绕开硬解析 JSON 的
toolFetchJson),stripHtmlToText 删 script/style/标签+解码实体+折叠空白+
UTF-8 边界安全截断到 4000 字。三态 pending 轮询、http(s) 校验、gzip 魔数报错。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 2: `gdeltSummaryJson` 每个 top 热点带文章 URL

**Files:**
- Modify: `applications/earth_explorer/feeds/gdelt_feed.cpp`(`gdeltSummaryJson` `:101-132` + 工具描述 `:150-152`)
- Test: `tests/feed_layer_tests.cpp`(现有 gdelt summary 断言块 `:1003-1012` 之后扩断言)

**Interfaces:**
- Consumes:`FeedPoint.url`(`feed_layer.h`,已是 `firstUrl(allurls)` 的结果,parseGdelt 时算好并 sanitize);`FeedPoint.raw`(读 name/mentions,保持原逻辑)。
- Produces:summary JSON 的 `topHotspots[i]` 从 `{name,mentions}` 变为 `{name,mentions,url}`;`url` 空串表示该热点无 allurls。被 AI 工具 `get_gdelt_summary` 消费。

- [ ] **Step 1: 写失败测试(扩现有断言)**

在 `tests/feed_layer_tests.cpp` 现有断言 `CHECK(top[3].get("name").to_str() == "Paris, France");`(`:1012`)之后,插入(fixture 里 Delhi/Paris 无 allurls → url 空,Kyiv/Osaka 有):

```cpp
        // 新增:每个 top 热点带代表文章 URL(= FeedPoint.url = allurls 首条;无 allurls 则空串)
        CHECK(top[0].get("url").to_str() == "");                             // Delhi 无 allurls
        CHECK(top[1].get("url").to_str() == "https://news.example.com/kyiv"); // Kyiv 裸 tab 清洗后首条
        CHECK(top[2].get("url").to_str() == "https://only.example.com/osaka");// Osaka 单条
        CHECK(top[3].get("url").to_str() == "");                             // Paris 无 allurls
```

- [ ] **Step 2: 构建测试并确认 FAIL**

Run: `cmake --build build/verse_core --target osgVerse_Test_Feeds -j4 && ./build/verse_core/bin/osgVerse_Test_Feeds`
Expected: 编译通过,运行时 **FAIL** —— `top[0].get("url")` 字段不存在,`to_str()` 对不存在字段返回空但 `top[1]` 断言期望 `https://news.example.com/kyiv` 会失败,打印 `CHECK failed`。

- [ ] **Step 3: 实现(改 `gdeltSummaryJson`)**

把 `gdeltSummaryJson`(`:101-132`)整体替换为(排序结构从 `pair` 换成携带 url 的局部 struct,排序语义不变):

```cpp
    static std::string gdeltSummaryJson(const std::vector<FeedPoint>& pts)
    {
        // 排序键沿用原语义:(-mentions, name) 升序 = 热度降序、同热度地名字典序(输出确定)。
        // 额外携带 url(= FeedPoint.url,已是 allurls 首条且经 sanitize;无则空)。
        struct Row {
            double negCnt; std::string name, url;
            bool operator<(const Row& o) const
            { return negCnt != o.negCnt ? negCnt < o.negCnt : name < o.name; }
        };
        std::vector<Row> v;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            double cnt = 0.0; std::string name = "?";   // raw 不保证是 object(gdacs 复审 carry-forward)
            if (pts[i].raw.is<picojson::object>())
            {
                const picojson::value& props = pts[i].raw.get("properties");
                if (props.is<picojson::object>())
                {
                    if (props.get("sumtotalmentions").is<double>())
                        cnt = props.get("sumtotalmentions").get<double>();
                    if (props.get("name").is<std::string>()) name = props.get("name").get<std::string>();
                }
            }
            Row row; row.negCnt = -cnt; row.name = name; row.url = pts[i].url;
            v.push_back(row);
        }
        std::sort(v.begin(), v.end());
        picojson::array top;
        for (size_t i = 0; i < v.size() && i < 5; ++i)   // 每热点仅 1 条 URL,控制 payload 体积
        {
            picojson::object o;
            o["name"] = picojson::value(v[i].name);
            o["mentions"] = picojson::value(-v[i].negCnt);
            o["url"] = picojson::value(v[i].url);
            top.push_back(picojson::value(o));
        }
        picojson::object r;
        r["count"] = picojson::value((double)pts.size());
        r["topHotspots"] = picojson::value(top);
        return picojson::value(r).serialize();
    }
```

- [ ] **Step 4: 更新工具描述**

把工具描述(`:150-152`)里 `TOP5 热点地名(按新闻提及数降序)` 改为带 URL 说明。整段替换为:

```cpp
    spec.toolDescriptionCn = u8"查询当前全球新闻热点汇总(GDELT:近 60 分钟全球新闻报道的地理聚合):"
        u8"总数、TOP5 热点地名(按新闻提及数降序)及每个热点的代表文章链接 url"
        u8"(可用 get_news_content 读取该 url 的正文再总结)。已过滤提及数 <5 的低置信地点;"
        u8"若图层尚未开启会自动开启并开始抓取,此时返回的 count 可能为 0,代表数据仍在加载中。";
```

- [ ] **Step 5: 构建测试并确认 PASS**

Run: `cmake --build build/verse_core --target osgVerse_Test_Feeds -j4 && ./build/verse_core/bin/osgVerse_Test_Feeds`
Expected: PASS —— 无 `CHECK failed`,退出码 0。

- [ ] **Step 6: Commit**

```bash
git add applications/earth_explorer/feeds/gdelt_feed.cpp tests/feed_layer_tests.cpp
git commit -m "feat(earth): gdeltSummaryJson 每个 top 热点带代表文章 URL

topHotspots[] 从 {name,mentions} 扩为 {name,mentions,url}(= FeedPoint.url,
allurls 首条,每热点仅1条防 payload 爆)。AI 光靠对话即可拿到热点文章 URL
交给 get_news_content 读正文。工具描述同步更新。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Task 3: 详情卡「AI 摘要」按钮

**Files:**
- Modify: `applications/earth_explorer/EarthControlUI.h`(通用"要素详情"卡 `card.drawBody` `:479-501`)

**Interfaces:**
- Consumes:`this->_aiCore`(`EarthControlUI.h:42`,`earthai::AIChatCore*`,main 注入);`AIChatCore::submit(const std::string&)`(`ai_chat.h:76`);`earthfeed::FeedSelection fs` 的 `title`/`url`。Task 1 的 `get_news_content` 工具(AI 收到 submit 后自调)。
- Produces:详情卡在 `fs.url` 非空且 `_aiCore` 非空时多一个「AI 摘要」按钮;点击 → `_aiCore->submit(中文 prompt)`。无自动化测试,靠编译 + 真机验收。

> **说明**:这是 ImGui UI,离屏截不到卡片/按钮 → 无单测。验证 = 编译通过(捕获与成员解析正确)+ 用户真机点按钮。

- [ ] **Step 1: 改捕获列表 + 加按钮**

把 `card.drawBody = [fs]() {`(`:479`)改为 `card.drawBody = [fs, this]() {`。
然后在现有 `if (!fs.url.empty()) { ... "打开 Open" 按钮 ... }` 块(`:485-500`)**内部、`打开 Open` 按钮的 `}` 之后、`if` 块的 `}` 之前**,加「AI 摘要」按钮:

```cpp
                        // 新闻摘要:交给 AI 去抓正文并分析,摘要出现在底部对话条。
                        // _aiCore 可能为 null(无 AI 构建/未注入)→ 按钮不出现,零干扰。
                        if (_aiCore)
                        {
                            ImGui::SameLine();
                            if (ImGui::Button(u8"AI 摘要"))
                                _aiCore->submit(u8"请总结这条新闻热点(地点:" + fs.title +
                                    u8")的主要内容并分析其重要性。文章链接:" + fs.url +
                                    u8" —— 请先调用 get_news_content 抓取正文再总结。");
                        }
```

改动后该块结构应为:
```
if (!fs.url.empty())
{
    ImGui::Separator();
    if (ImGui::Button(u8"打开 Open")) { ...现有... }
    if (_aiCore) { ImGui::SameLine(); if (ImGui::Button(u8"AI 摘要")) _aiCore->submit(...); }
}
```

- [ ] **Step 2: 全量构建 app 确认编译通过**

Run: `cmake --build build/verse_core --target install -j4`
Expected: 编译链接成功,无 error。重点验证 `[fs, this]` 捕获下 `_aiCore->submit(...)` 解析正确(`this` 在同帧 `_cardStack.draw()` 内同步执行,指针有效)。

> ⚠️ 此步是**全量 install 构建**(比前两步的单 test target 慢)。确认前两个 test target 构建已结束,禁止并发。

- [ ] **Step 3: Commit**

```bash
git add applications/earth_explorer/EarthControlUI.h
git commit -m "feat(earth): 新闻热点详情卡加「AI 摘要」按钮

card.drawBody 捕获 [fs]→[fs,this],在「打开 Open」旁加「AI 摘要」按钮:
点击 → _aiCore->submit(地点+url),AI 自调 get_news_content 抓正文后在
底部对话条给出摘要。仅 _aiCore 非空时出现;不碰 feed_layer/ai_chat。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## 收尾(全部任务后,不属于单个 task,真机验收阶段)

1. 打包:`bash packaging/package_macos.sh` → `codesign -v dist/EarthExplorer.app`(签名失效秒退)。
2. 离屏冒烟(可选,验工具注册无崩):`EARTH_OFFSCREEN=1 ./build/sdk_core/bin/EarthExplorer`(带 `EARTH_FRAME_SLEEP_MS` 给 wall-clock)看 `[AIChat] get_news_content` 探针 / 无崩溃。
3. 用户真机验收:点新闻热点标记 → 详情卡「AI 摘要」按钮 → 看摘要是否出现在底部 AI 对话条。
4. 进入交接文档 §3b 收尾(全分支终审 → ff 合 master → tag v0.22),**等用户确认再做**。

## Self-Review 记录

- **Spec 覆盖**:组件①=Task 1、组件②=Task 3、组件③=Task 2,单测覆盖①③、②靠真机 —— 全覆盖。
- **占位符**:无 TBD/TODO,所有代码块完整。
- **类型一致**:`stripHtmlToText(const std::string&, size_t)`、`getStr(const picojson::value&, const char*, std::string&)`、`Row{negCnt,name,url}`、`submit(const std::string&)` 前后一致。
- **简化记录**:组件③ 用 `pts[i].url`(已是 allurls 首条)替代 spec 里"重解析 raw.properties.allurls",功能等价且 DRY。
