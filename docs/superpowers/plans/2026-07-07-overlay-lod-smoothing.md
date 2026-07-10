# 叠加层 LOD 丝滑化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 EarthExplorer 的 OVERLAY 叠加层(texUnit 3,5 层互斥共用 clouds/precip/ndvi/nightlights/gebco)在异步加载、切 LOD、超原生最大缩放三种情况下都不再"漏出地面",改用父级瓦片纹理顶替,并在超缩放时右上角显示"已达最大细节"角标。

**Architecture:** 复用引擎现成的父级顶替机制(`findAndUseParentData` + `_uvRangesToSet`,底图早已在用)。三处改动:①`TileCallback::updateLayerData` 的 OVERLAY 分支——异步加载窗口期先绑父级纹理、真图到货后在 `operator()` 里无缝换本瓦片;超缩放 emptyPath 分支改为绑父级拉伸而非移除。②`TileManager` 加一个"本帧激活叠加层超缩放拉伸"帧戳标志。③app 侧 `EarthControlUI` 每帧读该标志,右上角画可关闭角标。

**Tech Stack:** C++14,OpenSceneGraph(osg::Texture2D/StateSet/NodeVisitor/FrameStamp),ImGui,CMake。

## Global Constraints

- **不碰 globe GLSL 着色器**(项目铁律,历史多次回归)。本改动纯在 C++ 纹理绑定/UV uniform 层。
- 引擎改动仅限 `readerwriter/TileCallback.cpp` + `readerwriter/TileCallback.h`(TileManager 标志);UI 改动仅限 app 侧 `applications/earth_explorer/`。
- 复用现成 `findAndUseParentData(LayerType, osg::Group*)` / `_uvRangesToSet` / `operator()` 逐帧钩子,不新造 LOD 机制。
- OVERLAY 5 层互斥,同时只有一层激活;角标只需考虑单一激活层。
- 所有跑 app 必须带 `EARTH_OFFSCREEN=1`(裸跑会抢前台打断用户);app 二进制在 `build/sdk_core/bin/osgVerse_EarthExplorer`。
- 构建:`cmake --build build/verse_core --target install -j4`,禁止并发第二个构建。
- 不做真·瓦片预取/下载提速(YAGNI,留作后续)。
- commit message 结尾:`Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>`

---

## File Structure

- `readerwriter/TileCallback.h` — 修改:TileManager 加 `_lastOverlayStretchFrame` 帧戳 + set/get;TileCallback 加 `_overlayPending` 成员。
- `readerwriter/TileCallback.cpp` — 修改:`updateLayerData` OVERLAY 超缩放/异步顶替;`operator()` 异步到货 swap。
- `applications/earth_explorer/overlay_lod_badge.h` — 新建:纯函数 `overlayMaxDetailBadgeVisible(...)`(角标可见性判定,可单测)。
- `applications/earth_explorer/LayerManager.h` — 修改:`OverlayLayer` 加 `std::string maxDetailNote;` 字段。
- `applications/earth_explorer/earth_main.cpp` — 修改:5 个 overlay 图层定义处填 `maxDetailNote`。
- `applications/earth_explorer/EarthControlUI.h` — 修改:每帧画右上角"已达最大细节"角标。
- `tests/feed_layer_tests.cpp` — 修改:加 `overlayMaxDetailBadgeVisible` 与 TileManager 帧戳的单测。

---

### Task 1: 超缩放父级拉伸 + TileManager 拉伸帧戳

叠加层超过原生最大缩放(`createCustomPath` 返回 `""` → `emptyPath`)时,不再移除 texUnit 3 纹理(漏地面),改为绑最近祖先叠加纹理拉伸顶住,并给 TileManager 打一个帧戳供 app 侧角标读取。

**Files:**
- Modify: `readerwriter/TileCallback.h`(TileManager 加帧戳成员 + 方法)
- Modify: `readerwriter/TileCallback.cpp:595-596`(updateLayerData,插入超缩放父级拉伸块)
- Modify: `readerwriter/TileCallback.cpp`(TileManager 构造函数初始化帧戳)
- Test: `tests/feed_layer_tests.cpp`(帧戳 round-trip 单测)

**Interfaces:**
- Produces: `void TileManager::markOverlayStretchedPastNative(unsigned int frame)`;`unsigned int TileManager::getLastOverlayStretchFrame() const`。Task 3 的角标读 `getLastOverlayStretchFrame()`。

- [ ] **Step 1: 写失败测试(TileManager 帧戳 round-trip)**

在 `tests/feed_layer_tests.cpp` 末尾、`main` 里现有测试块之后(紧邻 `mercatorTileBBox` 测试处),加:

```cpp
    // Task 1: TileManager 超缩放拉伸帧戳 round-trip
    {
        osgVerse::TileManager* tm = osgVerse::TileManager::instance();
        tm->markOverlayStretchedPastNative(12345u);
        assert(tm->getLastOverlayStretchFrame() == 12345u);
        tm->markOverlayStretchedPastNative(0u);
        assert(tm->getLastOverlayStretchFrame() == 0u);
        std::cout << "[OK] TileManager overlay stretch frame stamp" << std::endl;
    }
```

确认文件已 `#include "readerwriter/TileCallback.h"`(若无则加)。

- [ ] **Step 2: 跑测试确认失败(方法未定义)**

Run: `cmake --build build/verse_core --target osgVerse_Test_Feeds -j4`
Expected: 编译失败,`markOverlayStretchedPastNative` / `getLastOverlayStretchFrame` 未声明。

- [ ] **Step 3: TileManager 加帧戳成员 + 方法**

在 `readerwriter/TileCallback.h` 的 `TileManager` public 区(紧接 `getLayerPath` 方法后,约第 167 行后)加:

```cpp
        // 超缩放拉伸信号:updateLayerData 对 OVERLAY 做"超原生最大缩放→父级拉伸"兜底时,
        // 记下当前帧号;app 侧据此(带去抖)显示"已达最大细节"角标。
        void markOverlayStretchedPastNative(unsigned int frame) { _lastOverlayStretchFrame = frame; }
        unsigned int getLastOverlayStretchFrame() const { return _lastOverlayStretchFrame; }
```

在 `TileManager` protected 成员区(约第 189 行,`_layerPaths` 附近)加:

```cpp
        unsigned int _lastOverlayStretchFrame = 0;
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build/verse_core --target osgVerse_Test_Feeds -j4 && EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | grep "overlay stretch frame"`
Expected: `[OK] TileManager overlay stretch frame stamp`

- [ ] **Step 5: 实现超缩放父级拉伸(updateLayerData)**

在 `readerwriter/TileCallback.cpp`,`updateLayerData` 里现有父级回退行(595-596)之后、`if (tex.valid())`(597)之前,插入:

```cpp
    // 超缩放父级拉伸:OVERLAY 超过原生最大缩放时 createCustomPath 返回空(emptyPath),原逻辑会
    // 移除 texUnit 3 → 漏出地面。改为绑最近祖先叠加纹理拉伸顶住(渐糊但始终盖住),并打帧戳供角标。
    // findAndUseParentData 会顺带把父级 UV 子区间排进 _uvRangesToSet,由 operator() 应用。
    if (!tex.valid() && emptyPath && id == OVERLAY && node->getNumParents() > 0)
    {
        tex = findAndUseParentData(id, node->getParent(0));
        if (tex.valid())
        {
            emptyPath = false;  // 视为"有数据",走下方正常绑定路径而非移除分支
            unsigned int fr = (nv && nv->getFrameStamp()) ? nv->getFrameStamp()->getFrameNumber() : 0u;
            TileManager::instance()->markOverlayStretchedPastNative(fr);
        }
    }
```

- [ ] **Step 6: 构建 app + 离屏验证(超缩放不再漏地面)**

Run:
```bash
cmake --build build/verse_core --target install -j4 2>&1 | tail -1
APP=/Users/USER/osgverse/build/sdk_core/bin/osgVerse_EarthExplorer
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_GEBCO=0.9 EARTH_SUN_TO_CAMERA=1 EARTH_AUTOCAP=500 EARTH_FRAME_SLEEP_MS=50 \
  $APP --goto -2.068 -178.858 2000 2>&1 | grep -aiE 'TileCache] summary' | head -1
```
Expected:生成 `/tmp/earth_capture_0.png`。用 Read 工具查看:高度 2000km 已进入 z>8,GEBCO 海底地形应**拉伸顶住、仍覆盖海面**(略糊),而**非露出底图裸地面**。修复前此高度叠加层会消失。

- [ ] **Step 7: Commit**

```bash
git add readerwriter/TileCallback.h readerwriter/TileCallback.cpp tests/feed_layer_tests.cpp
git commit -m "feat(earth): OVERLAY 超缩放父级拉伸顶住 + TileManager 拉伸帧戳

超过原生最大缩放(gebco z>8 等)时不再移除叠加纹理漏地面,改绑最近祖先纹理拉伸
覆盖(渐糊但始终盖住),并打帧戳供 app 侧角标。5 层共用 OVERLAY 路径同时受益。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 2: 异步加载父级顶替 + 到货无缝换

叠加层瓦片异步下载窗口期(以及切 LOD 时新瓦片未到)不再显示透明占位(漏地面),先绑父级瓦片纹理顶替;本瓦片真图到货后在 `operator()` 里无缝换回并复位 UV。

**Files:**
- Modify: `readerwriter/TileCallback.h`(TileCallback 加 `_overlayPending` 成员)
- Modify: `readerwriter/TileCallback.cpp`(updateLayerData 异步顶替块;operator() 到货 swap)

**Interfaces:**
- Consumes: Task 1 已在 updateLayerData 中的位置。
- Produces:无对外接口;纯内部行为改进。

- [ ] **Step 1: TileCallback 加 `_overlayPending` 成员**

在 `readerwriter/TileCallback.h` 的 `TileCallback` protected 成员区(约第 149 行 `_elevationRef` 附近)加:

```cpp
        osg::ref_ptr<osg::Texture2D> _overlayPending;  // 异步加载中的 OVERLAY 本瓦片纹理(顶替期暂存,到货后 swap)
```

确认头部已 `#include <osg/Texture2D>`(TileCallback.cpp 已 include;头文件若无则加)。

- [ ] **Step 2: updateLayerData 加异步父级顶替块**

在 `readerwriter/TileCallback.cpp` 的 `updateLayerData` 里,`switch (id){...}` 结束(593)之后、现有父级回退行(595)之前,插入:

```cpp
    // 异步加载父级顶替:OVERLAY 走异步,createLayerImage 返回的 tex2D 初始是 1×1 透明占位,
    // 真图到货前直接绑上去会"漏地面"。若本瓦片图像尚未就绪且能找到已加载祖先,则先绑父级纹理
    // 顶替(带父级 UV 子区间),把本瓦片 tex 暂存 _overlayPending;operator() 检测到其真图到货后无缝换回。
    if (id == OVERLAY && tex.valid() && node->getNumParents() > 0)
    {
        osg::Texture2D* ownTex = dynamic_cast<osg::Texture2D*>(tex.get());
        osg::Image* ownImg = ownTex ? ownTex->getImage() : NULL;
        bool ownReady = (ownImg && ownImg->s() > 1);  // >1×1 = 真图(可能磁盘缓存同步命中);1×1 = 占位
        if (!ownReady)
        {
            osg::Texture* parentTex = findAndUseParentData(id, node->getParent(0));
            if (parentTex) { _overlayPending = ownTex; tex = parentTex; }  // 先显父级,到货再 swap
        }
    }
```

- [ ] **Step 3: operator() 加到货 swap**

在 `readerwriter/TileCallback.cpp` 的 `operator()` 里,`check()` 块结束(657)之后、`_uvRangesToSet` 应用块(660 `if (!_uvRangesToSet.empty())`)之前,插入:

```cpp
    // 异步到货 swap:顶替期结束——本瓦片真图已加载(尺寸从 1×1 变真),把 texUnit 3 从父级顶替纹理
    // 换回本瓦片纹理,并把 UV 复位为满铺(0,0,1,1)。UV 通过 _uvRangesToSet 排入,由紧接的应用块生效。
    if (_overlayPending.valid())
    {
        osg::Image* img = _overlayPending->getImage();
        if (img && img->s() > 1)
        {
            FindTileGeometry ftg; node->accept(ftg);
            osg::StateSet* ss = ftg.geometry.valid() ? ftg.geometry->getOrCreateStateSet() : NULL;
            if (ss)
            {
#if defined(OSG_GLES2_AVAILABLE) || defined(OSG_GLES3_AVAILABLE) || defined(OSG_GL3_AVAILABLE)
                ss->setTextureAttribute(3, _overlayPending.get());
#else
                ss->setTextureAttributeAndModes(3, _overlayPending.get());
#endif
                _uvRangesToSet["UvOffset4"] = osg::Vec4(0.0f, 0.0f, 1.0f, 1.0f);
            }
            _overlayPending = NULL;
        }
    }
```

- [ ] **Step 4: 构建 + 临时诊断验证 swap 时序**

在 Step 3 插入的 swap 块 `_overlayPending = NULL;` 之前,临时加一行诊断:

```cpp
                if (getenv("EARTH_LOD_DEBUG"))
                    OSG_NOTICE << "[SWAP] overlay z=" << _z << " x=" << _x << " y=" << _y
                               << " parent->own" << std::endl;
```

Run:
```bash
cmake --build build/verse_core --target install -j4 2>&1 | tail -1
APP=/Users/USER/osgverse/build/sdk_core/bin/osgVerse_EarthExplorer
EARTH_OFFSCREEN=1 EARTH_GEBCO=0.9 EARTH_LOD_DEBUG=1 EARTH_SUN_TO_CAMERA=1 EARTH_AUTOCAP=400 \
  EARTH_FRAME_SLEEP_MS=50 $APP --goto -2.068 -178.858 33106 2>&1 | grep -aE '\[SWAP\]' | head -10
```
Expected:出现若干 `[SWAP] overlay z=.. parent->own` 行,证明"先父级顶替→到货换回"时序真实发生。若一条都没有,说明 `_overlayPending` 从未命中(检查异步分支逻辑)。

- [ ] **Step 5: 移除临时诊断**

删掉 Step 4 加的 `if (getenv("EARTH_LOD_DEBUG")) OSG_NOTICE ... [SWAP] ...` 三行。重新构建:
Run: `cmake --build build/verse_core --target install -j4 2>&1 | tail -1`
Expected:构建成功。

- [ ] **Step 6: 离屏视觉验证(切 LOD 不漏地面)+ 单测无回归**

Run:
```bash
APP=/Users/USER/osgverse/build/sdk_core/bin/osgVerse_EarthExplorer
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_GEBCO=0.9 EARTH_SUN_TO_CAMERA=1 EARTH_AUTOCAP=300 EARTH_FRAME_SLEEP_MS=30 \
  $APP --goto 20 -160 8000 2>&1 | grep -aiE 'TileCache] summary'
EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | tail -2
```
Expected:`/tmp/earth_capture_0.png` 用 Read 查看——中高度(跨 LOD)GEBCO 连续覆盖、无裸地面斑块;`feed_layer tests OK`。

- [ ] **Step 7: Commit**

```bash
git add readerwriter/TileCallback.h readerwriter/TileCallback.cpp
git commit -m "feat(earth): OVERLAY 异步加载父级顶替 + 到货无缝换

异步加载/切 LOD 窗口期不再显示透明占位漏地面,先绑父级瓦片纹理顶替,本瓦片真图到货后
在 operator() 里换回并复位 UV。由粗到细渐清晰,消除闪烁。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

### Task 3: 右上角"已达最大细节"角标

激活叠加层正在超缩放拉伸时(读 Task 1 的帧戳,带去抖),右上角淡入一个可关闭小角标,如 `🌊 海底地形 GEBCO · 已达最大细节 (~600 m)`。

**Files:**
- Create: `applications/earth_explorer/overlay_lod_badge.h`(纯判定函数)
- Modify: `applications/earth_explorer/LayerManager.h`(OverlayLayer 加 `maxDetailNote`)
- Modify: `applications/earth_explorer/earth_main.cpp`(5 个 overlay 定义处填 `maxDetailNote`)
- Modify: `applications/earth_explorer/EarthControlUI.h`(每帧画角标)
- Test: `tests/feed_layer_tests.cpp`(角标可见性纯函数单测)

**Interfaces:**
- Consumes: `TileManager::getLastOverlayStretchFrame()`(Task 1)。
- Produces: `bool overlayMaxDetailBadgeVisible(unsigned int curFrame, unsigned int lastStretchFrame, unsigned int debounceFrames, bool hasActiveNote, bool dismissed)`。

- [ ] **Step 1: 写失败测试(角标可见性纯函数)**

在 `tests/feed_layer_tests.cpp` 顶部加 `#include "overlay_lod_badge.h"`。在 `main` 里加:

```cpp
    // Task 3: 角标可见性纯函数
    {
        using earthui::overlayMaxDetailBadgeVisible;
        // 近期有拉伸(帧差 < 去抖窗)+ 有激活层文案 + 未被关 → 显示
        assert(overlayMaxDetailBadgeVisible(100, 95, 30, true, false) == true);
        // 帧差超过去抖窗(已不再拉伸)→ 隐藏
        assert(overlayMaxDetailBadgeVisible(200, 95, 30, true, false) == false);
        // 无激活层文案 → 隐藏
        assert(overlayMaxDetailBadgeVisible(100, 95, 30, false, false) == false);
        // 用户已关闭 → 隐藏
        assert(overlayMaxDetailBadgeVisible(100, 95, 30, true, true) == false);
        // lastStretchFrame=0(从未拉伸)→ 隐藏(curFrame 远大于 0)
        assert(overlayMaxDetailBadgeVisible(100, 0, 30, true, false) == false);
        std::cout << "[OK] overlayMaxDetailBadgeVisible" << std::endl;
    }
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build build/verse_core --target osgVerse_Test_Feeds -j4`
Expected:编译失败,找不到 `overlay_lod_badge.h` / `overlayMaxDetailBadgeVisible`。

- [ ] **Step 3: 创建纯判定函数头**

新建 `applications/earth_explorer/overlay_lod_badge.h`:

```cpp
#ifndef EARTH_OVERLAY_LOD_BADGE_H
#define EARTH_OVERLAY_LOD_BADGE_H

namespace earthui
{
    // "已达最大细节"角标是否可见(纯函数,便于单测):
    // - lastStretchFrame:引擎最近一次做 OVERLAY 超缩放拉伸的帧号(0 = 从未);
    // - curFrame:当前帧号;debounceFrames:去抖窗(帧数),防止边界帧抖动导致角标闪烁;
    // - hasActiveNote:当前有激活叠加层且它带 maxDetailNote 文案;dismissed:用户已手动关闭。
    // 可见 ⟺ 近期(curFrame - lastStretchFrame < 去抖窗)确有拉伸 且 有文案 且 未被关。
    inline bool overlayMaxDetailBadgeVisible(unsigned int curFrame, unsigned int lastStretchFrame,
                                             unsigned int debounceFrames, bool hasActiveNote, bool dismissed)
    {
        if (dismissed || !hasActiveNote) return false;
        if (lastStretchFrame == 0u || curFrame < lastStretchFrame) return false;
        return (curFrame - lastStretchFrame) < debounceFrames;
    }
}

#endif
```

- [ ] **Step 4: 跑测试确认通过**

Run: `cmake --build build/verse_core --target osgVerse_Test_Feeds -j4 && EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | grep BadgeVisible`
Expected: `[OK] overlayMaxDetailBadgeVisible`

- [ ] **Step 5: OverlayLayer 加 maxDetailNote 字段**

在 `applications/earth_explorer/LayerManager.h` 的 `OverlayLayer` 结构里(`subtitle` 字段之后,约第 14 行后)加:

```cpp
    std::string maxDetailNote;   // 超原生最大缩放时角标里的分辨率提示(空=该层不显角标)
```

- [ ] **Step 6: 5 个 overlay 定义处填 maxDetailNote**

在 `applications/earth_explorer/earth_main.cpp` 各 overlay 定义处(clouds≈972 / precip≈984 / ndvi≈1003 / nightlights≈1021 / gebco≈1041,紧邻各自 `.subtitle =` 赋值处)加对应行:

```cpp
        clouds.maxDetailNote = u8"~250 m (z9)";
        precipL.maxDetailNote = u8"~1 km (z10)";
        ndvi.maxDetailNote = u8"~250 m (z9)";
        night.maxDetailNote = u8"~500 m (z8)";
        gebco.maxDetailNote = u8"~600 m (z8)";
```

(注:变量名以该处实际定义为准——见 grep 结果 `clouds`/`precipL`/`ndvi`/`night`/`gebco`。分辨率为近似值,传达"已达细节上限"即可。)

- [ ] **Step 7: EarthControlUI 每帧画角标**

在 `applications/earth_explorer/EarthControlUI.h`:

(a) 顶部加 include(约第 24 行 `#include "ai_ui.h"` 附近):
```cpp
#include "overlay_lod_badge.h"
#include <readerwriter/TileCallback.h>
```

(b) 结构成员区(约第 52 行 `_cardStack` 附近)加角标状态:
```cpp
    bool _detailBadgeDismissed = false;      // 用户关掉角标
    std::string _detailBadgeLastLayer;       // 上次角标对应的激活层 id(变了则重置 dismissed)
```

(c) 在 draw 方法里、主窗口 `ImGui::End()` 之后、方法结尾 `if (font) ImGui::PopFont();` 之前(即 `if (!hudHidden){...}` 块内、所有主面板绘制之后),加角标绘制:
```cpp
        // 超缩放"已达最大细节"角标:引擎在做 OVERLAY 超缩放拉伸时打帧戳,这里带去抖读取。
        if (_layers && _viewer && _viewer->getFrameStamp())
        {
            unsigned int curFrame = (unsigned int)_viewer->getFrameStamp()->getFrameNumber();
            unsigned int lastStretch = osgVerse::TileManager::instance()->getLastOverlayStretchFrame();
            // 找当前激活且带 maxDetailNote 的叠加层(5 层互斥,至多一个)
            OverlayLayer* active = nullptr;
            std::vector<OverlayLayer>& ls = _layers->layers();
            for (size_t i = 0; i < ls.size(); ++i)
                if (ls[i].enabled && !ls[i].maxDetailNote.empty()) { active = &ls[i]; break; }
            // 激活层变了 → 重置关闭态(新层的角标该重新出现)
            std::string activeId = active ? active->id : std::string();
            if (activeId != _detailBadgeLastLayer) { _detailBadgeDismissed = false; _detailBadgeLastLayer = activeId; }

            const unsigned int kDebounceFrames = 30;  // ~0.5s@60fps,防边界抖动
            if (earthui::overlayMaxDetailBadgeVisible(curFrame, lastStretch, kDebounceFrames,
                                                      active != nullptr, _detailBadgeDismissed))
            {
                ImGuiIO& io2 = ImGui::GetIO();
                ImGui::SetNextWindowPos(ImVec2(io2.DisplaySize.x - 12.0f, 12.0f),
                                        ImGuiCond_Always, ImVec2(1.0f, 0.0f));  // 右上角锚定
                ImGui::SetNextWindowBgAlpha(0.85f);
                bool open = true;
                if (ImGui::Begin(u8"##max_detail_badge", &open,
                                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing))
                {
                    ImGui::TextUnformatted((active->displayName + u8" · 已达最大细节 (" +
                                            active->maxDetailNote + u8")").c_str());
                }
                ImGui::End();
                if (!open) _detailBadgeDismissed = true;   // 用户点了 [x]
            }
        }
```

(d) 确认 draw 方法内 `if (!hudHidden){` 的闭合 `}` 在此角标块之后——角标属于 HUD 内容,MediaManager 抓帧时应一起隐藏(与主面板一致)。

- [ ] **Step 8: 构建 + 离屏验证角标出现**

Run:
```bash
cmake --build build/verse_core --target install -j4 2>&1 | tail -1
APP=/Users/USER/osgverse/build/sdk_core/bin/osgVerse_EarthExplorer
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_GEBCO=0.9 EARTH_SUN_TO_CAMERA=1 EARTH_AUTOCAP=500 EARTH_FRAME_SLEEP_MS=50 \
  $APP --goto -2.068 -178.858 1500 2>&1 | grep -aiE 'TileCache] summary'
```
Expected:`/tmp/earth_capture_0.png` 用 Read 查看——高度 1500km(z>8 超缩放)时右上角出现 `海底地形 GEBCO · 已达最大细节 (~600 m)` 角标;海底地形拉伸覆盖不漏地面。

- [ ] **Step 9: 离屏验证角标在高空不显(无拉伸)+ 单测**

Run:
```bash
APP=/Users/USER/osgverse/build/sdk_core/bin/osgVerse_EarthExplorer
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_GEBCO=0.9 EARTH_SUN_TO_CAMERA=1 EARTH_AUTOCAP=500 EARTH_FRAME_SLEEP_MS=50 \
  $APP --goto -2.068 -178.858 33106 2>&1 | grep -aiE 'TileCache] summary'
EARTH_OFFSCREEN=1 /Users/USER/osgverse/build/sdk_core/bin/osgVerse_Test_Feeds 2>&1 | tail -2
```
Expected:33106km(z≤8,未超缩放)截图**无**角标;`feed_layer tests OK`。

- [ ] **Step 10: Commit**

```bash
git add applications/earth_explorer/overlay_lod_badge.h applications/earth_explorer/LayerManager.h applications/earth_explorer/earth_main.cpp applications/earth_explorer/EarthControlUI.h tests/feed_layer_tests.cpp
git commit -m "feat(earth): 超缩放'已达最大细节'右上角可关闭角标

激活叠加层超缩放拉伸时(读 TileManager 帧戳,带去抖),右上角淡入可关闭角标标明
原生分辨率上限。纯判定函数 overlayMaxDetailBadgeVisible 附单测。

Co-Authored-By: Claude Opus 4.8 <noreply@anthropic.com>"
```

---

## Self-Review

**1. Spec coverage:**
- 设计症①②(异步加载/切 LOD 漏地面闪) → Task 2(异步父级顶替 + 到货 swap)。✅
- 设计症③(超缩放消失) → Task 1(超缩放父级拉伸)。✅
- 组件 2 检测(引擎帧戳) → Task 1(markOverlayStretchedPastNative / getLastOverlayStretchFrame)。✅
- 组件 2 角标(右上角可关闭) → Task 3。✅
- "找不到祖先才退透明占位"兜底 → Task 2 Step 2:`if (parentTex)` 才顶替,否则 tex 仍是占位(现状)。✅
- 5 层共用一处修复 → Task 1/2 改的是 `id == OVERLAY` 通用分支,非某层专属。✅
- 不做真预取 → 计划无预取任务。✅

**2. Placeholder scan:** 无 TBD/TODO;每个代码步给出完整代码;验证步给出确切命令与预期。maxDetailNote 分辨率标注为近似值已在 Task 3 Step 6 说明。✅

**3. Type consistency:**
- `markOverlayStretchedPastNative(unsigned int)` / `getLastOverlayStretchFrame() -> unsigned int`:Task 1 定义、Task 3(c)使用,一致。✅
- `overlayMaxDetailBadgeVisible(unsigned, unsigned, unsigned, bool, bool) -> bool`:Task 3 Step 3 定义、Step 1 测试、Step 7(c)使用,签名一致。✅
- `_overlayPending`(`osg::ref_ptr<osg::Texture2D>`):Task 2 Step 1 声明、Step 2/3 使用,一致。✅
- `OverlayLayer::maxDetailNote`(`std::string`):Task 3 Step 5 加、Step 6 赋值、Step 7 读,一致。✅
- `findAndUseParentData(LayerType, osg::Group*) -> osg::Texture*`:与 TileCallback.h:62 现有签名一致。✅

**已知次要项(留待真机确认,非阻塞):** 切换叠加层(如 gebco→clouds)瞬间,异步顶替可能短暂显示上一层的父级粗瓦片再换新层;因 5 层互斥且切换为用户主动、通常可接受。若真机觉突兀,后续可在 check() 触发路径抑制父级顶替。
