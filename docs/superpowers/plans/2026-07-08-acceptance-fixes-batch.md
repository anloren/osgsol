# 真机验收修复批次 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development,逐任务执行。步骤用 `- [ ]`。

**Goal:** 修掉真机验收攒下的 5 类问题:相机穿地看背面、HTTP -1 连接失败无重试、工具循环上限过低、角标持续碍事、缺可视化设置入口。

**Architecture:** 先建一个运行时配置存储(earthcfg 参数注册表 + 默认值),让工具循环上限/HTTP 重试次数/角标时长等都从它读;再逐项修 AI/网络/角标;设置面板把配置项可视化;相机穿地做防御性硬修(隔离、需真机验)。

**Tech Stack:** C++14,OSG,ImGui,libhv(requests),picojson。

## Global Constraints

- 不碰 globe GLSL 着色器(铁律)。相机修复在 EarthManipulator(C++),非 GLSL。
- 所有跑 app 带 `EARTH_OFFSCREEN=1`;app 在 `build/sdk_core/bin/osgVerse_EarthExplorer`;构建 `cmake --build build/verse_core --target install -j4`,**禁止并发第二个构建**。
- **T6 相机穿地是"红线"区**(memory 记多次回归史):只做防御性堵源头,不重构;env 调试钩子齐全;离屏只能部分验,**最终必须用户真机确认不回归**(全球视角/低空/极点/倾角四件套)。
- 网络失败提示分类要有用(区分 无响应/超时/TLS/HTTP错误码),不得再只打 "HTTP -1"。
- 配置项全部带默认值,面板可"恢复默认"。
- commit 结尾:`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

## File Structure

- `applications/earth_explorer/earth_config.{h,cpp}` — T1:运行时配置注册表(getInt/getDouble/getBool/set + 参数元数据列表 + 可选磁盘持久化)。
- `applications/earth_explorer/ai_chat.cpp` / `ai_media.cpp` — T2:HTTP 连接层重试 helper + 提示分类;T3:工具循环上限读配置 + 超限优雅收尾。
- `applications/earth_explorer/ai_chat.h` — T3:kMaxRounds 去硬编码。
- `applications/earth_explorer/overlay_lod_badge.h` / `EarthControlUI.h` / `LayerManager.h` — T4:角标自消失 + 不透明层提示。
- `applications/earth_explorer/EarthControlUI.h` — T5:设置面板。
- `readerwriter/EarthManipulator.{h,cpp}` — T6:硬海平面地板 + 近/远平面兜底。
- `tests/feed_layer_tests.cpp` — T1/T4 纯函数单测。

---

### Task 1: 运行时配置存储(earthcfg 参数注册表)

**Files:** Modify `earth_config.h`/`earth_config.cpp`;Test `tests/feed_layer_tests.cpp`。

**Interfaces（Produces）:**
- `namespace earthcfg { enum ParamKind { PK_INT, PK_DOUBLE, PK_BOOL }; struct Param { std::string id, label, group; ParamKind kind; double value, defVal, minVal, maxVal; }; }`
- `std::vector<earthcfg::Param>& earthcfg::params();`(注册表,单例 vector)
- `int earthcfg::getInt(const std::string& id);` `double earthcfg::getDouble(const std::string& id);` `bool earthcfg::getBool(const std::string& id);`
- `void earthcfg::setValue(const std::string& id, double v);`(写入并按 min/max 钳制)
- `void earthcfg::resetDefault(const std::string& id);`

- [ ] **Step 1: 写失败测试**

`tests/feed_layer_tests.cpp` 顶部 `#include "../applications/earth_explorer/earth_config.h"`。main 里加:
```cpp
    {   // Task 1: 配置注册表
        using namespace earthcfg;
        CHECK(getInt("ai.maxRounds") == 30);        // 默认
        setValue("ai.maxRounds", 50); CHECK(getInt("ai.maxRounds") == 50);
        setValue("ai.maxRounds", 9999); CHECK(getInt("ai.maxRounds") == 200);  // 钳到 max
        setValue("ai.maxRounds", -5); CHECK(getInt("ai.maxRounds") == 1);        // 钳到 min
        resetDefault("ai.maxRounds"); CHECK(getInt("ai.maxRounds") == 30);
        CHECK(getInt("http.retries") == 3);
        CHECK((int)(getDouble("badge.seconds") + 0.5) == 5);
        std::cout << "[OK] earthcfg params" << std::endl;
    }
```

- [ ] **Step 2: 跑测试确认失败** — `cmake --build build/verse_core --target osgVerse_Test_Feeds -j4`,预期编译失败(符号未定义)。

- [ ] **Step 3: 实现配置注册表**

`earth_config.h` 加类型与函数声明(见 Interfaces)。`earth_config.cpp` 实现:
- `params()`:静态局部 `std::vector<Param>`,首次调用时注册三个默认参数(可被后续任务追加):
  - `{"ai.maxRounds","AI 工具调用上限","AI",PK_INT, 30,30,1,200}`
  - `{"http.retries","网络失败重试次数","网络",PK_INT, 3,3,0,10}`
  - `{"badge.seconds","角标显示秒数","界面",PK_DOUBLE, 5,5,0,30}`
  - 每个参数首次注册时用 `earthcfg::resolveKey`/`getenv` 思路:若同名 `EARTH_*` env 存在则用 env 值 seed(可选,便于老钩子仍生效);否则用 defVal。
- `find(id)` 私有辅助返回 `Param*`;getInt/getDouble/getBool 读 `value`(找不到返回 def=0/false 并告警);`setValue` 钳到 `[minVal,maxVal]`;`resetDefault` 置回 `defVal`。
- (可选,若时间允许)持久化:`~/Library/Application Support/EarthExplorer/config.ini`,启动读、setValue 后写。**首版可不持久化**(留 TODO),先满足运行期可调 + 默认值。

- [ ] **Step 4: 跑测试确认通过** — 构建 + `EARTH_OFFSCREEN=1 .../osgVerse_Test_Feeds | grep 'earthcfg params'` → `[OK] earthcfg params`。

- [ ] **Step 5: Commit**（`feat(earth): 运行时配置注册表 earthcfg::params + get/set/reset`,带 trailer)

---

### Task 2: HTTP 连接层重试 + 提示分类

**Files:** Modify `ai_chat.cpp`(:472 附近)、`ai_media.cpp`(4 处 request 站点:~256/321/346/456,视频下载 ~411)。

- [ ] **Step 1: 加带重试的请求 helper**

在合适公共处(如 `ai_media.cpp`/`ai_chat.cpp` 各加一个静态 helper,或新建 `applications/earth_explorer/http_util.h` 内联)加:
```cpp
// 连接层失败(resp 为空:DNS/TLS 握手/连接拒绝/超时无响应)自动重试,带线性退避。
// 真实 HTTP 错误码(4xx/5xx)不重试(是服务端语义,重试无益)。retries 从配置读。
static requests::Response httpRequestRetry(requests::Request req)
{
    int retries = earthcfg::getInt("http.retries");   // 默认 3
    requests::Response resp;
    for (int attempt = 0; attempt <= retries; ++attempt)
    {
        resp = requests::request(req);
        if (resp) return resp;                          // 拿到任何响应(含 4xx/5xx)即返回
        if (attempt < retries)
            OpenThreads::Thread::microSleep((attempt + 1) * 300 * 1000);  // 300/600/900ms 退避
    }
    return resp;   // 仍为空 = 连接层彻底失败
}
```
把 `ai_chat.cpp:472` 的 `requests::request(req)` 与 `ai_media.cpp` 各 request 站点替换为 `httpRequestRetry(req)`。确保 include `<OpenThreads/Thread>` 与 `earth_config.h`。

- [ ] **Step 2: 提示分类**

`ai_chat.cpp:475` 与 `ai_media.cpp` 各 `"HTTP " + ... : -1` 分支,当 `!resp`(空响应)时改用更有用的文案,例如:
```cpp
// resp 为空 = 连接层失败(已重试)。区分于真实 HTTP 错误码。
turn.error = resp
    ? ("HTTP " + std::to_string((int)resp->status_code) + ": " + truncate200(resp->body))
    : u8"网络连接失败(多次重试无响应):可能网络中断 / 请求超时 / TLS 握手失败,请检查网络后重试";
```
(ai_media 的 `err = ...` 各处同理改。保持有 resp 时仍打真实状态码+body 摘要。)

- [ ] **Step 3: 构建 + 单测无回归** — 构建 install;`osgVerse_Test_Feeds` 与 `osgVerse_Test_WorldTools`/`Test_Ai_Chat` 全绿(离线 fixture 不走网络,验证不破坏既有路径)。

- [ ] **Step 4: Commit**（`fix(earth): HTTP 连接层失败自动重试+退避,提示分类(不再只报 HTTP -1)`,带 trailer)

---

### Task 3: 工具循环上限可配置 + 超限优雅收尾

**Files:** Modify `ai_chat.h`(kMaxRounds)、`ai_chat.cpp`(:298-320 上限处理 + :455-461 请求构造)。

- [ ] **Step 1: 上限读配置**

`ai_chat.h:104` 的 `static const int kMaxRounds = 6;` 删除;`ai_chat.cpp:302` 的 `_round > kMaxRounds` 改为 `_round > earthcfg::getInt("ai.maxRounds")`(默认 30)。include `earth_config.h`。

- [ ] **Step 2: 超限改为"无工具最终回答",不再硬报错**

先 Read `ai_chat.cpp` 理解 submit/round 循环与请求构造(:455-480 body 里 `tools` 字段仅当 declsJson 非空才带)。把 :302-319 的硬报错改为:
- 不再 push "工具循环超限" ERR 直接 return;而是**设一个 `_forceNoTools` 状态**(新成员,`std::atomic<bool>` 或在锁内的 bool),并给未执行的 functionCall 补合成 functionResponse(保持 Gemini call/response 配对合法,**这段保留**),同时给 transcript 加一条**温和提示**(如 `已达工具调用上限(N),基于已获取的数据作答`,kind=普通文本或一个不打断的 note)。
- 请求构造处(:458-460):当 `_forceNoTools` 为真时**不带 tools 字段**(等同 declsJson 视为空),强制模型用历史里已有的数据产出纯文本最终回答;该轮结束后清 `_forceNoTools`。
- 效果:撞上限 → 再走一轮"无工具"请求 → 用户拿到真答案 + 一句"已达上限"提示,而非 "工具循环超限" 错误。

- [ ] **Step 3: 构建 + 单测** — `Test_Ai_Chat`(含离线 fixture 的 chat 流程)全绿;至少不破坏既有配对/解析。若有 fixture 覆盖多轮,确认仍通过。

- [ ] **Step 4: Commit**（`feat(earth): 工具循环上限读配置(默认30)+ 超限优雅收尾(无工具最终回答)`,带 trailer）

---

### Task 4: 角标自消失 + 不透明科学层提示

**Files:** Modify `overlay_lod_badge.h`、`EarthControlUI.h`(:464-495 角标块 + 成员)、`LayerManager.h`(OverlayLayer 加 `opaque` 标识)、`earth_main.cpp`(给 gebco/ndvi/nightlights 标 opaque);Test `tests/feed_layer_tests.cpp`。

- [ ] **Step 1: OverlayLayer 加 opaque 标识**

`LayerManager.h` OverlayLayer 加 `bool opaque = false;`(不透明科学层置 true)。`earth_main.cpp` 给 `gebco`/`ndvi`/`night` 三层 `X.opaque = true;`(clouds/precip 半透,保持 false)。

- [ ] **Step 2: 角标可见性纯函数加"自消失"**

`overlay_lod_badge.h` 改 `overlayMaxDetailBadgeVisible`:新增入参 `shownFrames`(角标已连续可见的帧数)与 `autoDismissFrames`(自消失阈值);逻辑:原条件成立 **且** `shownFrames < autoDismissFrames` 才可见(超过即自动隐藏)。更新单测(feed_layer_tests):可见→到阈值→隐藏。

- [ ] **Step 3: EarthControlUI 角标状态机 + 不透明提示**

`EarthControlUI.h` 角标块(:464-495):
- 加成员 `unsigned int _detailBadgeShownFrame = 0;`(角标本轮首次出现的帧号)与"离开超缩放后重置"逻辑:当 stretch 条件为真且上一帧不可见 → 记 `_detailBadgeShownFrame = curFrame`;当 stretch 条件转假(离开超缩放)一段时间 → 清零允许下次重新提示。
- 传 `shownFrames = curFrame - _detailBadgeShownFrame`、`autoDismissFrames = (unsigned)(earthcfg::getDouble("badge.seconds") * 60)` 给纯函数。
- 文案:对 `active->opaque == true` 的层,追加 ` · 关闭图层可看地形`(不透明层看不穿,提示用户关闭)。

- [ ] **Step 4: 构建 + 单测 + 离屏探针** — `Test_Feeds` 含新纯函数单测全绿;离屏在超缩放高度用临时 log 探针确认"出现~5s(300帧)后 visible 转 false",验完删探针。

- [ ] **Step 5: Commit**（`feat(earth): 角标进入超缩放提示~5s自消失 + 不透明科学层提示关闭图层`,带 trailer）

---

### Task 5: 设置面板

**Files:** Modify `EarthControlUI.h`(主面板加「设置」CollapsingHeader)。

- [ ] **Step 1: 面板渲染配置注册表**

`EarthControlUI.h` draw 方法主窗口内(相机/太阳等 header 之后)加:
```cpp
            if (ImGui::CollapsingHeader(u8"设置 Settings"))
            {
                std::vector<earthcfg::Param>& ps = earthcfg::params();
                std::string curGroup;
                for (size_t i = 0; i < ps.size(); ++i)
                {
                    earthcfg::Param& p = ps[i];
                    if (p.group != curGroup) { curGroup = p.group; ImGui::SeparatorText(p.group.c_str()); }
                    if (p.kind == earthcfg::PK_INT) {
                        int v = (int)p.value;
                        if (ImGui::SliderInt(p.label.c_str(), &v, (int)p.minVal, (int)p.maxVal))
                            earthcfg::setValue(p.id, (double)v);
                    } else if (p.kind == earthcfg::PK_DOUBLE) {
                        float v = (float)p.value;
                        if (ImGui::SliderFloat(p.label.c_str(), &v, (float)p.minVal, (float)p.maxVal, "%.1f"))
                            earthcfg::setValue(p.id, (double)v);
                    } else {
                        bool v = p.value != 0.0;
                        if (ImGui::Checkbox(p.label.c_str(), &v)) earthcfg::setValue(p.id, v ? 1.0 : 0.0);
                    }
                    ImGui::SameLine();
                    ImGui::PushID((int)i);
                    if (ImGui::SmallButton(u8"默认")) earthcfg::resetDefault(p.id);
                    ImGui::PopID();
                }
            }
```
include `earth_config.h`。若 `SeparatorText` 在本 ImGui 版本不存在,用 `ImGui::Separator(); ImGui::TextDisabled(group)` 替代(先确认版本)。

- [ ] **Step 2: 构建 + 离屏冒烟** — 构建 install;离屏跑 `exit=0`(面板离屏不可截,冒烟只验不崩不破坏渲染;真机看面板)。

- [ ] **Step 3: Commit**（`feat(earth): 设置面板 —— 可视化调节 earthcfg 配置项 + 恢复默认`,带 trailer）

---

### Task 6: 🔴 相机穿地防御性硬修(近/远平面兜底 + 硬海平面地板)

根因:眼睛沉到地球半径以下时,`EarthManipulator.h:489` 的近/远平面钳制被跳过 → 远平面不收敛 → 看穿到对面地壳背面。触发:低空快速俯冲时 `_terrainLift` 地形地板失守。

**Files:** Modify `readerwriter/EarthManipulator.cpp`(updateTerrainFloor / 相机眼位钳制)、`readerwriter/EarthManipulator.h`(:489 近/远平面块)。

- [ ] **Step 1: 硬海平面地板(堵源头)**

在 `updateTerrainFloor()`(:728)或眼位求取处加一道**与地形探针无关**的硬下限:眼睛离地心距离永不小于 `getEarthRadius() + kHardFloorMargin`(如 5m)。即便探针失败/未命中,也保证 `distance > rp`。实现:在算出 `hEye` 后,若 `hEye < kHardSeaFloor`(如 2m),把 `desiredLift` 至少提到 `kHardSeaFloor - hEye`(与地形 lift 取 max)。**先 Read updateTerrainFloor 全貌**确认接入点,保证与既有平滑/缓存逻辑不冲突。加 `EARTH_HARDFLOOR_DEBUG` 打印触发。

- [ ] **Step 2: 近/远平面兜底(即便万一沉下去也不暴露背面)**

`EarthManipulator.h:489` 的 `if (distance > rp)`:当 `distance <= rp`(理论上 Step 1 后不该发生,双保险)时,**不要直接 `return false` 放开远平面**,而是给一个收敛的兜底 znear/zfar(如 znear=1、zfar=一个不足以照到对面地壳的小值),避免看穿背面。先 Read :460-517 完整近/远平面逻辑再改,保持既有 `distance > rp` 分支**逐字不变**(只补 else 兜底)。

- [ ] **Step 3: 离屏部分验证 + 探针**

离屏飞到福建等低空点连续下探,用 `EARTH_HARDFLOOR_DEBUG` 确认硬地板触发、`EARTH_TERRAIN_DEBUG` 看 hEye 不再低于海平面;截图确认不再出现"背面地壳/糊成一片"。**注明离屏无法完全复现交互式俯冲**,最终由用户真机四件套验收(全球/低空/极点/倾角,不回归)。

- [ ] **Step 4: 构建 + 单测无回归** — `Test_Feeds` + `Test_Earth`(若含相机相关)全绿;既有 `distance > rp` 路径行为不变。

- [ ] **Step 5: Commit**（`fix(earth): 相机穿地防御性硬修 —— 硬海平面地板 + 近/远平面兜底(距地心<=半径不放开远平面)`,带 trailer）

---

## Self-Review

**1. 覆盖:** A穿地→T6;B HTTP-1→T2;C角标→T4;D工具循环→T3;E设置面板→T5;共享配置→T1(T3/T2/T4 都读它)。✅
**2. 占位:** 无 TBD;T3/T6 明确要求"先 Read 相关区再改"(涉及既有敏感逻辑,避免凭行号盲改);持久化在 T1 标为可选/首版可省。✅
**3. 类型一致:** earthcfg get/set/params/Param 在 T1 定义,T2/T3/T4/T5 使用一致;OverlayLayer.opaque T4 定义+使用;overlayMaxDetailBadgeVisible 新签名 T4 内自洽。✅
**已知取舍:** T6 是红线区,离屏只能部分验,**必须**用户真机四件套确认;若离屏发现硬地板与倾角/巡游动画冲突,报 DONE_WITH_CONCERNS 交人工判。T1 持久化首版可省(运行期可调 + 默认值已满足核心诉求)。
