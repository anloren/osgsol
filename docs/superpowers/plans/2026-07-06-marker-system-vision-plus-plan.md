# 标记系统重设计(vision 加强版)Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给地球上每类信息点一个可辨识的形状徽章(船=长五边形、火点=星芒、地震=同心环…),形状与信息面板的图层目录 icon、详情卡 chip 联动,颜色仍按值渐变。

**Architecture:** 单一真源 `marker_style.h`:一张 `visualForLayer(id)` 查表登记每个图层 id →(形状, 代表色),外加共享 GLSL 形状库字符串 + ImGui 形状绘制函数。地球标记着色器(feed 框架经 texcoord0.y 传 shapeId、其余用常量)、图层目录 icon、详情卡 chip 全部从这一张表取值 —— 改一处三处同步。

**Tech Stack:** C++14、OpenSceneGraph、osgVerse Pipeline 着色器(字符串拼接)、Dear ImGui(DrawList)、GL_POINTS 点精灵。

**Spec:** `docs/superpowers/specs/2026-07-06-marker-system-vision-plus-design.md`(必读)。

## Global Constraints

- **红线:只动外观**。禁止改动:数据抓取(fetchOnce/FetchThread/WS/订阅/闸门)、拾取(pickAt 及其数学)、图层注册流程(registerFeedLayer/configureXxxLayer 的接线逻辑本身)、聚合 LOD 机制、near/far、**本会话新加的 VS 前半球剔除**(各 VS 里 `dot(Pv.xyz - Cv, Pv.xyz)` 那段,保持原样,只在其后改颜色/形状输出)、缓存、key、线程模型。允许改:片元/顶点着色器里画形状与输出颜色的代码、逐点/逐层的形状 id 属性、ImGui 画 icon/chip 的代码、新增 marker_style 集中定义、OverlayLayer/CardStyle 的外观字段。
- **单一真源**:形状与代表色只在 `earthmark::visualForLayer(id)` 一处登记;标记着色器、图层目录、详情卡都从它取,不得在别处另设一份(避免地球与面板形状分叉)。
- 着色器是字符串拼接,经 `osgVerse::Pipeline::createShaderDefinitions(shader, 100, 130)` 处理;MRT 双输出格式(`gl_FragData[0]=颜色, gl_FragData[1]=掩码`;GLES3 分支 `fragColor/fragOrigin`)每个 FS 必须保留,照现有 feedFragCode/flightFragCode 原样。
- 点精灵局部坐标约定:`d = gl_PointCoord - vec2(0.5)`,范围约 `[-0.5,0.5]`;可旋转形状(船/航班)先按 `headingRad` 旋转再判形状(照现有 flight FS `r=vec2(c*d.x-s*d.y, s*d.x+c*d.y); r.y=-r.y;`)。
- 代表色(逐字,见下表)。
- 构建:`cmake --build /Users/USER/osgverse/build/verse_core --target install`(禁止并发第二个构建)。测试二进制在 `/Users/USER/osgverse/build/verse_core/bin/`;app 二进制在 `/Users/USER/osgverse/build/sdk_core/bin/`。
- 任何运行 `osgVerse_EarthExplorer` **必须** `EARTH_OFFSCREEN=1`(用户铁律);截图落 `/tmp/earth_capture_0.png`,截图前先 `rm -f` 防陈旧;交付打包后**绝不再运行 dist app**(会写 imgui.ini 破坏签名),功能验证一律用 `build/sdk_core/bin/` 二进制。
- 提交信息末尾:`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`。
- 中文注释解释 why,风格与既有文件一致。

## 形状-图层-代表色对照(全计划共用,逐字)

MarkerShape 枚举整数值(shapeId,GLSL/C++ 一致):`Circle=0, Arrow=1, Ship=2, StarBurst=3, Ring=4, WarnTriangle=5, Diamond=6, Hexagon=7, Teardrop=8, SatBox=9, Square=10`。

| 图层 id | MarkerShape | 代表色 RGB(0-1) | 随航向 | 来源 |
|---|---|---|---|---|
| ais / ships | Ship | (0.208, 0.878, 0.816) | 是 | ais_data 常量 |
| flights | Arrow | (0.302, 0.659, 1.0) | 是 | flight_data 常量 |
| fires | StarBurst | (1.0, 0.353, 0.188) | 否 | feed 经查表 |
| quakes | Ring | (0.690, 0.420, 1.0) | 否 | feed 经查表 |
| gdacs | WarnTriangle | (1.0, 0.231, 0.278) | 否 | feed 经查表 |
| gdelt | Diamond | (1.0, 0.761, 0.227) | 否 | feed 经查表 |
| gpsjam | Hexagon | (1.0, 0.353, 0.784) | 否 | feed 经查表 |
| eonet | Teardrop | (0.294, 0.847, 0.420) | 否 | feed 经查表 |
| unhcr | Circle | (0.239, 0.482, 1.0) | 否 | feed 经查表(弧线不变) |
| nhc | Ring | (0.498, 0.816, 0.847) | 否 | feed 经查表(路径不变) |
| bases/ports/nuclear/cables/datacenters(战略各源) | Square | (0.604, 0.643, 0.698) | 否 | feed 经查表 |
| satstations/satnav/satwx/starlink | SatBox | (1.0, 0.824, 0.227) | 否 | sat_data 常量 |

**注**:战略各源的确切 id 由 `feeds/strategic_feed.cpp` 的 `d.id` 决定;Task 1 需先 grep 出全部战略 id 并逐个登记为 Square(见 Task 1 Step 3 注)。

---

### Task 1: marker_style 单一真源(查表 + 共享 GLSL 形状库 + ImGui 绘制)

**Files:**
- Create: `applications/earth_explorer/marker_style.h`
- Create: `applications/earth_explorer/marker_style.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`(EXECUTABLE_FILES 加 `marker_style.cpp`)
- Modify: `tests/feed_layer_tests.cpp`(include marker_style.cpp + visualForLayer 单测)

**Interfaces:**
- Produces(后续任务逐字消费):
```cpp
namespace earthmark {
    enum class MarkerShape { Circle=0, Arrow=1, Ship=2, StarBurst=3, Ring=4,
                             WarnTriangle=5, Diamond=6, Hexagon=7, Teardrop=8, SatBox=9, Square=10 };
    struct MarkerVisual { MarkerShape shape; osg::Vec4 color; };
    MarkerVisual visualForLayer(const std::string& layerId);   // 未登记→{Circle,(0.7,0.7,0.72,1)}
    const char* markerShapeGLSL();   // float markerCoverage(int shapeId, vec2 pc, float headingRad)
    void drawMarkerIcon(void* imDrawList, MarkerShape shape, float cx, float cy, float sizePx, unsigned int colorU32);
}
```

- [ ] **Step 1: 写失败单测**(feed_layer_tests.cpp include 区加 `#include "../applications/earth_explorer/marker_style.cpp"`;追加测试并在 main() 调用)

```cpp
// ===== marker-system Task 1:visualForLayer 单一真源 =====
static void testVisualForLayer()
{
    using namespace earthmark;
    CHECK(visualForLayer("ais").shape == MarkerShape::Ship);
    CHECK(visualForLayer("ships").shape == MarkerShape::Ship);       // 面板 id 与标记 id 同视觉
    CHECK(visualForLayer("flights").shape == MarkerShape::Arrow);
    CHECK(visualForLayer("fires").shape == MarkerShape::StarBurst);
    CHECK(visualForLayer("quakes").shape == MarkerShape::Ring);
    CHECK(visualForLayer("gdacs").shape == MarkerShape::WarnTriangle);
    CHECK(visualForLayer("gdelt").shape == MarkerShape::Diamond);
    CHECK(visualForLayer("gpsjam").shape == MarkerShape::Hexagon);
    CHECK(visualForLayer("eonet").shape == MarkerShape::Teardrop);
    CHECK(visualForLayer("unhcr").shape == MarkerShape::Circle);
    CHECK(visualForLayer("nhc").shape == MarkerShape::Ring);
    CHECK(visualForLayer("satstations").shape == MarkerShape::SatBox);
    CHECK(visualForLayer("starlink").shape == MarkerShape::SatBox);
    MarkerVisual s = visualForLayer("ais");
    CHECK(std::fabs(s.color.x() - 0.208f) < 0.01f && std::fabs(s.color.y() - 0.878f) < 0.01f);
    CHECK(visualForLayer("nonexistent").shape == MarkerShape::Circle);   // 未登记回退
    CHECK(std::string(markerShapeGLSL()).find("markerCoverage") != std::string::npos);
    std::cout << "[OK] visualForLayer\n";
}
```

- [ ] **Step 2: 跑测试确认失败**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds 2>&1 | tail -5`
Expected: 编译错误 `marker_style.cpp` 不存在 / `earthmark` 未声明。

- [ ] **Step 3: 实现 marker_style.h**

```cpp
#ifndef EARTH_MARKER_STYLE_H
#define EARTH_MARKER_STYLE_H
#include <string>
#include <osg/Vec4>

// 标记系统单一真源(vision 加强版):形状枚举 + 每层代表色查表 + 共享 GLSL 形状库 +
// ImGui 形状绘制。地球标记着色器、图层目录 icon、详情卡 chip 都读这里 → 改一处三处同步。
// 红线:本模块只负责"长什么样",不碰任何数据/线程/拾取/注册逻辑。
namespace earthmark
{
    enum class MarkerShape { Circle=0, Arrow=1, Ship=2, StarBurst=3, Ring=4,
                             WarnTriangle=5, Diamond=6, Hexagon=7, Teardrop=8, SatBox=9, Square=10 };
    struct MarkerVisual { MarkerShape shape; osg::Vec4 color; };   // color=代表色
    // 按图层 id 查代表徽章;未登记回退 {Circle, 中性灰}。
    MarkerVisual visualForLayer(const std::string& layerId);
    // 共享 GLSL 形状库(拼进各点精灵 FS)。提供 float markerCoverage(int,vec2,float),返回
    // 覆盖率 0..1(0=形状外应 discard;边缘 smoothstep 抗锯齿)。pc=gl_PointCoord。
    const char* markerShapeGLSL();
    // ImGui 侧画对应形状(图层目录 icon / 详情卡 chip)。imDrawList 传 ImDrawList*(void* 免头依赖);
    // (cx,cy)=中心屏幕坐标,sizePx=外接尺寸,colorU32=ImU32。
    void drawMarkerIcon(void* imDrawList, MarkerShape shape, float cx, float cy, float sizePx,
                        unsigned int colorU32);
}
#endif
```

- [ ] **Step 4: 实现 marker_style.cpp**

**先 grep 战略各源 id**:`grep -n "d.id\s*=" applications/earth_explorer/feeds/strategic_feed.cpp` 或看 `feeds/strategic_feed.cpp` 里定义各战略源的静态表(id 如 bases/ports/nuclear/cables/datacenters),把实际 id 逐个登记为 Square。

```cpp
#include "marker_style.h"
#include <map>
#include <cmath>
#include <imgui.h>

namespace earthmark
{
    // ---- 1) 单一真源:每层代表徽章登记表 ----
    MarkerVisual visualForLayer(const std::string& id)
    {
        static const osg::Vec4 kShipCol(0.208f,0.878f,0.816f,1.f);
        static const osg::Vec4 kStratCol(0.604f,0.643f,0.698f,1.f);
        static const osg::Vec4 kSatCol(1.000f,0.824f,0.227f,1.f);
        static const std::map<std::string, MarkerVisual> kReg = {
            { "ais",         { MarkerShape::Ship,         kShipCol } },
            { "ships",       { MarkerShape::Ship,         kShipCol } },
            { "flights",     { MarkerShape::Arrow,        osg::Vec4(0.302f,0.659f,1.000f,1.f) } },
            { "fires",       { MarkerShape::StarBurst,    osg::Vec4(1.000f,0.353f,0.188f,1.f) } },
            { "quakes",      { MarkerShape::Ring,         osg::Vec4(0.690f,0.420f,1.000f,1.f) } },
            { "gdacs",       { MarkerShape::WarnTriangle, osg::Vec4(1.000f,0.231f,0.278f,1.f) } },
            { "gdelt",       { MarkerShape::Diamond,      osg::Vec4(1.000f,0.761f,0.227f,1.f) } },
            { "gpsjam",      { MarkerShape::Hexagon,      osg::Vec4(1.000f,0.353f,0.784f,1.f) } },
            { "eonet",       { MarkerShape::Teardrop,     osg::Vec4(0.294f,0.847f,0.420f,1.f) } },
            { "unhcr",       { MarkerShape::Circle,       osg::Vec4(0.239f,0.482f,1.000f,1.f) } },
            { "nhc",         { MarkerShape::Ring,         osg::Vec4(0.498f,0.816f,0.847f,1.f) } },
            { "satstations", { MarkerShape::SatBox,       kSatCol } },
            { "satnav",      { MarkerShape::SatBox,       kSatCol } },
            { "satwx",       { MarkerShape::SatBox,       kSatCol } },
            { "starlink",    { MarkerShape::SatBox,       kSatCol } },
            // 战略各源(id 以 strategic_feed.cpp 实际值为准,全部 Square):
            { "bases",       { MarkerShape::Square,       kStratCol } },
            { "ports",       { MarkerShape::Square,       kStratCol } },
            { "nuclear",     { MarkerShape::Square,       kStratCol } },
            { "cables",      { MarkerShape::Square,       kStratCol } },
            { "datacenters", { MarkerShape::Square,       kStratCol } },
        };
        std::map<std::string, MarkerVisual>::const_iterator it = kReg.find(id);
        if (it != kReg.end()) return it->second;
        return MarkerVisual{ MarkerShape::Circle, osg::Vec4(0.700f,0.700f,0.720f,1.f) };
    }

    // ---- 2) 共享 GLSL 形状库 ----
    // 约定:d=pc-0.5。可旋转形状先按 headingRad 旋转。每个形状返回 signed margin(>0 内、<0 外);
    // markerCoverage 统一 AA:cov=smoothstep(-AA,AA,margin),AA≈0.035;调用方对 cov<=0 discard。
    const char* markerShapeGLSL()
    {
        return
        "float _sdCircle(vec2 d){ return 0.45 - length(d); }\n"
        "float _sdDiamond(vec2 d){ return 0.44 - (abs(d.x)+abs(d.y)); }\n"
        "float _sdSquare(vec2 d){ return 0.38 - max(abs(d.x),abs(d.y)); }\n"
        "float _sdHex(vec2 d){ d=abs(d); return 0.44 - max(d.x*0.8660254+d.y*0.5, d.y); }\n"
        "float _sdTriUp(vec2 d){ float mb=d.y+0.34; float hw=0.5*(0.42-d.y); return min(mb, hw-abs(d.x)); }\n"
        "float _sdArrow(vec2 r){ float mb=r.y+0.32; float hw=0.30*(0.42-r.y)/0.74; return min(mb, hw-abs(r.x)); }\n"
        "float _sdShip(vec2 r){\n"
        "  float m = 1.0;\n"
        "  m = min(m, r.y + 0.40);                              // 艉底 y>-0.40\n"
        "  m = min(m, (0.45 - r.y)*0.28 - (r.x)*0.53 + 0.045);  // 右艏斜边\n"
        "  m = min(m, (0.45 - r.y)*0.28 + (r.x)*0.53 + 0.045);  // 左艏斜边\n"
        "  m = min(m, 0.24 - r.x);                              // 右舷\n"
        "  m = min(m, 0.24 + r.x);                              // 左舷\n"
        "  return m;\n"
        "}\n"
        "float _sdStar(vec2 d){ float a=atan(d.y,d.x); float rr=0.45*(0.52+0.48*abs(cos(2.0*a))); return rr-length(d); }\n"
        "float _sdRing(vec2 d){ float r=length(d); float band=0.085-abs(r-0.33); float dot=0.14-r; return max(band,dot); }\n"
        "float _sdDrop(vec2 d){ float circ=0.33-length(d-vec2(0.0,-0.08));\n"
        "  float hw=0.33*(0.46-d.y)/0.54; float tri=min(min(hw-abs(d.x), d.y+0.08), 0.46-d.y); return max(circ,tri); }\n"
        "float _sdSat(vec2 d){ float cheb=max(abs(d.x),abs(d.y));\n"
        "  float frame=min(cheb-0.26, 0.38-cheb); float arm=min(0.055-min(abs(d.x),abs(d.y)), 0.48-cheb); return max(frame,arm); }\n"
        "float markerCoverage(int shapeId, vec2 pc, float headingRad){\n"
        "  vec2 d = pc - vec2(0.5);\n"
        "  vec2 r = d;\n"
        "  if (shapeId==1 || shapeId==2){ float s=sin(headingRad),c=cos(headingRad);\n"
        "     r=vec2(c*d.x - s*d.y, s*d.x + c*d.y); r.y=-r.y; }\n"
        "  float m;\n"
        "  if      (shapeId==1) m=_sdArrow(r);\n"
        "  else if (shapeId==2) m=_sdShip(r);\n"
        "  else if (shapeId==3) m=_sdStar(d);\n"
        "  else if (shapeId==4) m=_sdRing(d);\n"
        "  else if (shapeId==5) m=_sdTriUp(d);\n"
        "  else if (shapeId==6) m=_sdDiamond(d);\n"
        "  else if (shapeId==7) m=_sdHex(d);\n"
        "  else if (shapeId==8) m=_sdDrop(d);\n"
        "  else if (shapeId==9) m=_sdSat(d);\n"
        "  else if (shapeId==10) m=_sdSquare(d);\n"
        "  else m=_sdCircle(d);\n"
        "  return smoothstep(-0.035, 0.035, m);\n"
        "}\n";
    }

    // ---- 3) ImGui 形状绘制(与 GLSL 形状对应) ----
    void drawMarkerIcon(void* imDrawList, MarkerShape shape, float cx, float cy, float sizePx,
                        unsigned int colorU32)
    {
        ImDrawList* dl = (ImDrawList*)imDrawList;
        ImU32 col = (ImU32)colorU32;
        float R = sizePx * 0.5f;
        ImVec2 c(cx, cy);
        #define MP(x,y) ImVec2(c.x + (x)*sizePx, c.y - (y)*sizePx)
        switch (shape)
        {
        case MarkerShape::Ship: {
            ImVec2 p[5] = { MP(0.0f,0.45f), MP(0.26f,0.12f), MP(0.20f,-0.40f), MP(-0.20f,-0.40f), MP(-0.26f,0.12f) };
            dl->AddConvexPolyFilled(p, 5, col); break; }
        case MarkerShape::Arrow: {
            ImVec2 p[3] = { MP(0.0f,0.45f), MP(0.30f,-0.35f), MP(-0.30f,-0.35f) };
            dl->AddConvexPolyFilled(p, 3, col); break; }
        case MarkerShape::StarBurst: {
            ImVec2 p[8]; for (int i=0;i<8;++i){ float a=(float)i*3.14159265f/4.0f;
                float rr=(i%2==0)?R:R*0.5f; p[i]=ImVec2(c.x+cosf(a)*rr, c.y-sinf(a)*rr); }
            dl->AddConvexPolyFilled(p, 8, col); break; }
        case MarkerShape::Ring:
            dl->AddCircle(c, R*0.82f, col, 24, R*0.20f); dl->AddCircleFilled(c, R*0.28f, col, 12); break;
        case MarkerShape::WarnTriangle: {
            ImVec2 p[3] = { MP(0.0f,0.42f), MP(0.42f,-0.38f), MP(-0.42f,-0.38f) };
            dl->AddConvexPolyFilled(p, 3, col); break; }
        case MarkerShape::Diamond: {
            ImVec2 p[4] = { MP(0.0f,0.45f), MP(0.45f,0.0f), MP(0.0f,-0.45f), MP(-0.45f,0.0f) };
            dl->AddConvexPolyFilled(p, 4, col); break; }
        case MarkerShape::Hexagon: {
            ImVec2 p[6]; for (int i=0;i<6;++i){ float a=3.14159265f/2.0f + (float)i*3.14159265f/3.0f;
                p[i]=ImVec2(c.x+cosf(a)*R, c.y-sinf(a)*R); }
            dl->AddConvexPolyFilled(p, 6, col); break; }
        case MarkerShape::Teardrop: {
            dl->AddCircleFilled(ImVec2(c.x, c.y + R*0.16f), R*0.66f, col, 16);
            ImVec2 p[3] = { MP(0.0f,0.46f), MP(0.30f,-0.05f), MP(-0.30f,-0.05f) };
            dl->AddConvexPolyFilled(p, 3, col); break; }
        case MarkerShape::SatBox:
            dl->AddRect(MP(-0.30f,0.30f), MP(0.30f,-0.30f), col, 1.5f, 0, R*0.18f);
            dl->AddLine(MP(0.0f,0.48f), MP(0.0f,-0.48f), col, R*0.12f);
            dl->AddLine(MP(-0.48f,0.0f), MP(0.48f,0.0f), col, R*0.12f); break;
        case MarkerShape::Square:
            dl->AddRectFilled(MP(-0.38f,0.38f), MP(0.38f,-0.38f), col, R*0.20f); break;
        case MarkerShape::Circle:
        default:
            dl->AddCircleFilled(c, R*0.9f, col, 16); break;
        }
        #undef MP
    }
}
```

CMakeLists.txt:EXECUTABLE_FILES 里 `earth_config.cpp` 后加 `marker_style.cpp`。若 `#include <imgui.h>` 找不到,改用 feed_layer.cpp 同款 `#include <ui/ImGuiComponents.h>`。

- [ ] **Step 5: 跑测试确认通过**

Run: `cmake --build /Users/USER/osgverse/build/verse_core --target osgVerse_Test_Feeds && /Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Feeds; echo exit=$?`
Expected: `[OK] visualForLayer`,exit=0。

- [ ] **Step 6: Commit**

```bash
git add applications/earth_explorer/marker_style.h applications/earth_explorer/marker_style.cpp \
        applications/earth_explorer/CMakeLists.txt tests/feed_layer_tests.cpp
git commit -m "feat(earth): marker_style single source — shape enum, per-layer visual registry, shared GLSL shape lib, ImGui icon drawer (marker T1)"
```

---

### Task 2: feed 框架接入形状(registerFeedLayer 从查表取 shapeId,共享 FS 画形状)

**Files:**
- Modify: `applications/earth_explorer/feed_layer.h`(include marker_style.h)
- Modify: `applications/earth_explorer/feed_layer.cpp`(FeedLayerImpl 缓存 shapeId、buildGeodeFrom 写入、FS 改用共享库)
- Modify: `tests/feed_layer_tests.cpp`(shapeId 缓存单测,可选)

**Interfaces:**
- Consumes: Task 1 的 `visualForLayer`、`markerShapeGLSL`、MarkerShape。
- 效果:所有 9 个 feed 源(+战略)按 id 自动获得形状,无需改各源 .cpp。

- [ ] **Step 1: 实现**

`feed_layer.h`:顶部 include 区加 `#include "marker_style.h"`。

`feed_layer.cpp`:
(a) `FeedLayerImpl` 加成员(protected 区)与构造时计算:
```cpp
        int _shapeId;   // 该层形状(从 visualForLayer(spec.id) 取,单一真源)
```
构造函数初始化列表(`FeedLayerImpl(const FeedSpec& spec, osgViewer::Viewer* viewer = nullptr)`)加 `, _shapeId((int)earthmark::visualForLayer(spec.id).shape)`。

(b) `buildGeodeFrom` 里 texcoord 写 shapeId(`0.0f` → `_shapeId`):
```cpp
                sizes->push_back(osg::Vec2(recs[i].pt.sizePx, (float)_shapeId));
```

(c) `feedVertCode` 加 vShapeId varying(**前半球剔除段保持不动**):
```cpp
    const char* feedVertCode = {
        "VERSE_VS_OUT vec4 pointColor;\n"
        "VERSE_VS_OUT float vShapeId;\n"
        "void main() {\n"
        "    pointColor = osg_Color;\n"
        "    gl_PointSize = osg_MultiTexCoord0.x;\n"
        "    vShapeId = osg_MultiTexCoord0.y;\n"
        "    vec4 Pv = VERSE_MATRIX_MV * osg_Vertex;\n"
        "    vec3 Cv = (VERSE_MATRIX_MV * vec4(0.0, 0.0, 0.0, 1.0)).xyz;\n"
        "    if (dot(Pv.xyz - Cv, Pv.xyz) >= 0.0) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); return; }\n"
        "    gl_Position = VERSE_MATRIX_MVP * osg_Vertex;\n"
        "}\n"
    };
```

(d) `feedFragCode` 整体替换为 `feedFragMain`(不再硬编码圆,改调 markerCoverage;renames the variable):
```cpp
    const char* feedFragMain = {
        "VERSE_FS_IN vec4 pointColor;\n"
        "VERSE_FS_IN float vShapeId;\n"
        "#ifdef VERSE_GLES3\n"
        "layout(location = 0) VERSE_FS_OUT vec4 fragColor;\n"
        "layout(location = 1) VERSE_FS_OUT vec4 fragOrigin;\n"
        "#endif\n"
        "void main() {\n"
        "    float cov = markerCoverage(int(vShapeId + 0.5), gl_PointCoord, 0.0);\n"
        "    if (cov <= 0.0) discard;\n"
        "    vec3 rgb = mix(pointColor.rgb * 0.7, pointColor.rgb, cov);\n"
        "#ifdef VERSE_GLES3\n"
        "    fragColor = vec4(rgb, 1.0); fragOrigin = vec4(1.0);\n"
        "#else\n"
        "    gl_FragData[0] = vec4(rgb, 1.0); gl_FragData[1] = vec4(1.0);\n"
        "#endif\n"
        "}\n"
    };
```

(e) `buildScene()` 里构造 FS 处(原 `new osg::Shader(osg::Shader::FRAGMENT, feedFragCode)`)改为拼接:
```cpp
            std::string fsSrc = std::string(earthmark::markerShapeGLSL()) + feedFragMain;
            osg::Shader* fs = new osg::Shader(osg::Shader::FRAGMENT, fsSrc);
```

- [ ] **Step 2: 全量构建 + 逐形状离屏目检**

```bash
cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | tail -3
cd /Users/USER/osgverse/build/sdk_core/bin
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 EARTH_FIRES=1 \
  EARTH_FIRES_FILE=/Users/USER/osgverse/applications/earth_explorer/test/firms_fixture.csv \
  ./osgVerse_EarthExplorer --goto 25 100 300 2>&1 | grep -i fires
```
读 `/tmp/earth_capture_0.png`:火点为**星芒**(非圆点);颜色黄/橙/红分档仍在。再用各源 fixture/env(EARTH_QUAKES/EARTH_GDACS/EARTH_GDELT/EARTH_GPSJAM/EARTH_EONET 及 *_FILE;fixture 见 test/)分别单层截图,逐张目检:地震=同心环、灾害=警告三角、新闻=菱形、GPS=六边形、自然事件=水滴、战略=方块。某形状辨识度差则本任务内微调对应 `_sdXxx` GLSL(spec §1 允许降级)。

- [ ] **Step 3: 单测回归**

Run: `/Users/USER/osgverse/build/verse_core/bin/osgVerse_Test_Feeds; echo exit=$?`
Expected: exit=0(既有用例 + Task 1 用例全过)。

- [ ] **Step 4: Commit**

```bash
git add applications/earth_explorer/feed_layer.h applications/earth_explorer/feed_layer.cpp tests/feed_layer_tests.cpp
git commit -m "feat(earth): feed markers get per-layer shape from registry via shared FS (marker T2)"
```

---

### Task 3: 航班箭头 / 船五边形 / 卫星方框(独立着色器改用共享库)

**Files:**
- Modify: `applications/earth_explorer/flight_data.cpp`(FS 共享库,Arrow=1)
- Modify: `applications/earth_explorer/ais_data.cpp`(FS 共享库,Ship=2)
- Modify: `applications/earth_explorer/sat_data.cpp`(FS 共享库,SatBox=9)

**Interfaces:**
- Consumes: Task 1 的 `markerShapeGLSL`。

各文件顶部加 `#include "marker_style.h"`。每层 VS **保持前半球剔除段不动**(flight/ais 已有 headingRad varying;sat 无朝向不需要);FS 改为拼接共享库 + 调 markerCoverage(shapeId 常量),旋转形状用现有 headingRad。

- [ ] **Step 1: flight_data.cpp**——箭头 FS 换共享库(shapeId=1):
```cpp
    const char* flightFragMain = {
        "VERSE_FS_IN vec4 pointColor;\n"
        "VERSE_FS_IN float headingRad;\n"
        "#ifdef VERSE_GLES3\n"
        "layout(location=0) VERSE_FS_OUT vec4 fragColor;\n"
        "layout(location=1) VERSE_FS_OUT vec4 fragOrigin;\n"
        "#endif\n"
        "void main() {\n"
        "    float cov = markerCoverage(1, gl_PointCoord, headingRad);\n"
        "    if (cov <= 0.0) discard;\n"
        "    vec3 rgb = mix(pointColor.rgb * 0.7, pointColor.rgb, cov);\n"
        "#ifdef VERSE_GLES3\n"
        "    fragColor = vec4(rgb, 1.0); fragOrigin = vec4(1.0);\n"
        "#else\n"
        "    gl_FragData[0] = vec4(rgb, 1.0); gl_FragData[1] = vec4(1.0);\n"
        "#endif\n"
        "}\n"
    };
```
buildScene() 构造 FS 处改 `std::string fsSrc = std::string(earthmark::markerShapeGLSL()) + flightFragMain;`。

- [ ] **Step 2: ais_data.cpp**——同上,`markerCoverage(2, gl_PointCoord, headingRad)`(Ship 五边形随 COG 旋转)。

- [ ] **Step 3: sat_data.cpp**——`markerCoverage(9, gl_PointCoord, 0.0)`(SatBox 不旋转);ISS/天宫高亮的颜色/大小逻辑不变(只换形状绘制)。

- [ ] **Step 4: 全量构建 + 离屏目检**

```bash
cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | tail -3
cd /Users/USER/osgverse/build/sdk_core/bin
rm -f /tmp/earth_capture_0.png
KEYAIS=$(grep AISSTREAM "$HOME/Library/Application Support/EarthExplorer/keys.env" | cut -d= -f2)
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=1000 EARTH_FRAME_SLEEP_MS=25 EARTH_SHIPS=1 EARTH_AISSTREAM_KEY=$KEYAIS \
  ./osgVerse_EarthExplorer --goto 1.25 103.8 120 2>&1 | grep -i ship
```
读 PNG:船为**长五边形船体**、随航向朝向。再开航班(EARTH_FLIGHTS + flights fixture)验箭头、卫星(EARTH_SATS/EARTH_STARLINK)验方框,逐张读 PNG。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/flight_data.cpp applications/earth_explorer/ais_data.cpp applications/earth_explorer/sat_data.cpp
git commit -m "feat(earth): flight arrow / ship pentagon / satellite box via shared shape lib (marker T3)"
```

---

### Task 4: 面板联动(图层目录 icon + 详情卡 chip,全从查表)

**Files:**
- Modify: `applications/earth_explorer/LayerManager.h`(OverlayLayer 加 shape/iconColor 外观字段)
- Modify: `applications/earth_explorer/feed_layer.cpp`(registerFeedLayer 填 OverlayLayer.shape/iconColor)
- Modify: `applications/earth_explorer/earth_main.cpp`(卫星/船/航班 OverlayLayer 注册处填 shape/iconColor)
- Modify: `applications/earth_explorer/ui_card.h`(CardStyle 加 shape;drawChip 画 icon+文字)
- Modify: `applications/earth_explorer/EarthControlUI.h`(图层目录每项前 drawMarkerIcon;各详情卡设 shape)

**Interfaces:**
- Consumes: Task 1 的 `visualForLayer`、`drawMarkerIcon`、MarkerShape。

- [ ] **Step 1: OverlayLayer 加外观字段**(`LayerManager.h` OverlayLayer 内,`needsKey` 之后):
```cpp
    earthmark::MarkerShape shape = earthmark::MarkerShape::Circle;   // 图层目录 icon 形状(外观)
    osg::Vec4 iconColor = osg::Vec4(0.7f, 0.7f, 0.72f, 1.0f);        // 图层目录 icon 颜色(外观)
```
`LayerManager.h` 顶部加 `#include "marker_style.h"`。

- [ ] **Step 2: 注册处填字段**(外观赋值,不改注册流程)

`feed_layer.cpp` `registerFeedLayer` 里 `OverlayLayer l;` 各字段赋值后、`layers->add(l)` 前加:
```cpp
        earthmark::MarkerVisual mv = earthmark::visualForLayer(spec.id);
        l.shape = mv.shape; l.iconColor = mv.color;
```
`earth_main.cpp` 非 feed 图层的 `OverlayLayer` 注册块(船 ships、卫星 satstations/satnav/satwx/starlink;航班若也在图层目录注册则同办):**先 grep 定位**`grep -n "OverlayLayer .*;.*id\|\.id = \"ships\"\|\.id = \"satstations\"\|layerMgr.add" earth_main.cpp`,对每个 OverlayLayer 局部变量(如 `ships`、`satStations` 等,以实际代码为准)在其 `.id`/`.displayName` 赋值之后加:
```cpp
        { earthmark::MarkerVisual mv = earthmark::visualForLayer(<该 OverlayLayer 的 .id 字符串>);
          <该变量>.shape = mv.shape; <该变量>.iconColor = mv.color; }
```
`earth_main.cpp` 顶部加 `#include "marker_style.h"`。(所有这些 id——ships/satstations/satnav/satwx/starlink——已在 Task 1 查表登记,取得非回退值。)

- [ ] **Step 3: 图层目录每项前画 icon**(`EarthControlUI.h`,`ImGui::Checkbox(l.displayName...)` 之前):
```cpp
                        {
                            ImVec2 cur = ImGui::GetCursorScreenPos();
                            float ic = ImGui::GetTextLineHeight();
                            ImU32 icol = ImGui::ColorConvertFloat4ToU32(
                                ImVec4(l.iconColor.x(), l.iconColor.y(), l.iconColor.z(), 1.0f));
                            earthmark::drawMarkerIcon(ImGui::GetWindowDrawList(), l.shape,
                                cur.x + ic*0.5f, cur.y + ic*0.5f, ic, icol);
                            ImGui::Dummy(ImVec2(ic + 4.0f, ic)); ImGui::SameLine();
                        }
```
`EarthControlUI.h` 顶部加 `#include "marker_style.h"`。

- [ ] **Step 4: CardStyle 加 shape + drawChip 画 icon**(`ui_card.h`):

CardStyle 加 `earthmark::MarkerShape shape = earthmark::MarkerShape::Circle;`;`ui_card.h` 顶部加 `#include "marker_style.h"`。drawChip 改为:
```cpp
    inline void drawChip(const char* label, const osg::Vec4& color,
                         earthmark::MarkerShape shape = earthmark::MarkerShape::Circle)
    {
        if (!label || !label[0]) return;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float ih = ImGui::GetTextLineHeight();
        ImU32 icol = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x(), color.y(), color.z(), 1.0f));
        earthmark::drawMarkerIcon(dl, shape, p.x + ih*0.5f, p.y + ih*0.5f, ih, icol);
        float iconAdvance = ih + 4.0f;
        ImVec2 textSize = ImGui::CalcTextSize(label);
        ImVec2 padding(7.0f, 1.0f);
        ImVec2 chipSize(iconAdvance + textSize.x + padding.x * 2.0f, textSize.y + padding.y * 2.0f);
        ImU32 bg = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x(), color.y(), color.z(), 0.18f));
        ImU32 fg = ImGui::ColorConvertFloat4ToU32(ImVec4(color.x(), color.y(), color.z(), 1.0f));
        dl->AddRectFilled(p, ImVec2(p.x + chipSize.x, p.y + chipSize.y), bg, chipSize.y * 0.5f);
        dl->AddText(ImVec2(p.x + iconAdvance + padding.x, p.y + padding.y), fg, label);
        ImGui::Dummy(chipSize);
    }
```
CardPanel 内调用 `drawChip(c.style.chipLabel.c_str(), c.style.accentColor)` 处改为 `drawChip(c.style.chipLabel.c_str(), c.style.accentColor, c.style.shape)`。

- [ ] **Step 5: 各详情卡设 shape + accentColor**(`EarthControlUI.h` 各卡设置块,在设 `chipLabel` 附近):
- 航班卡:`card.style.shape = earthmark::MarkerShape::Arrow; card.style.accentColor = osg::Vec4(0.302f,0.659f,1.0f,1.f);`
- 船舶卡:`card.style.shape = earthmark::MarkerShape::Ship; card.style.accentColor = osg::Vec4(0.208f,0.878f,0.816f,1.f);`
- 卫星卡:`card.style.shape = earthmark::MarkerShape::SatBox; card.style.accentColor = osg::Vec4(1.0f,0.824f,0.227f,1.f);`
- 通用要素卡(`chipLabel=u8"要素"`,来自 FeedSelection):用 `earthmark::MarkerVisual mv = earthmark::visualForLayer(<FeedSelection 的 sourceId>); card.style.shape = mv.shape; card.style.accentColor = mv.color;`(FeedSelection 已有 sourceId 字段,见 feed_layer.h)。

- [ ] **Step 6: 全量构建 + 离屏面板目检**

```bash
cmake --build /Users/USER/osgverse/build/verse_core --target install 2>&1 | tail -3
cd /Users/USER/osgverse/build/sdk_core/bin && rm -f /tmp/earth_capture_0.png
KEYF=$(grep FIRMS "$HOME/Library/Application Support/EarthExplorer/keys.env" | cut -d= -f2)
KEYA=$(grep AISSTREAM "$HOME/Library/Application Support/EarthExplorer/keys.env" | cut -d= -f2)
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=200 EARTH_FIRES=1 EARTH_SHIPS=1 EARTH_FIRMS_KEY=$KEYF EARTH_AISSTREAM_KEY=$KEYA \
  ./osgVerse_EarthExplorer --goto 1.25 103.8 800 2>&1 | tail -3
```
读 `/tmp/earth_capture_0.png`:图层目录"火点"前星芒 icon、"船舶"前五边形 icon、"地震"前环 icon 等,颜色与地球标记一致;抽查 2-3 层三处(地球/目录/卡)同形同色。

- [ ] **Step 7: Commit**

```bash
git add applications/earth_explorer/LayerManager.h applications/earth_explorer/feed_layer.cpp \
        applications/earth_explorer/earth_main.cpp applications/earth_explorer/ui_card.h \
        applications/earth_explorer/EarthControlUI.h
git commit -m "feat(earth): panel linkage — layer-directory icons + detail-card chip shapes from central registry (marker T4)"
```

---

### Task 5: 全量回归 + 打包 + HANDOFF

**Files:**
- Modify: `HANDOFF.md`(顶部新增本版章节)

- [ ] **Step 1: 全量单测**(5 个二进制 exit=0)

```bash
cd /Users/USER/osgverse/build/verse_core/bin
for t in osgVerse_Test_Feeds osgVerse_Test_Ais osgVerse_Test_Ai_Chat osgVerse_Test_TileOverlay osgVerse_Test_Satellite; do
  ./$t > /tmp/$t.log 2>&1; echo "$t exit=$?"; done
```

- [ ] **Step 2: 渲染回归 + 闪烁未回归**

```bash
cd /Users/USER/osgverse/build/sdk_core/bin
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 ./osgVerse_EarthExplorer 2>&1 | tail -2
python3 /Users/USER/osgverse/applications/earth_explorer/test/classify_rb_swap.py /tmp/earth_capture_0.png
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=150 ./osgVerse_EarthExplorer --goto 89 0 8000 2>&1 | tail -2
python3 /Users/USER/osgverse/applications/earth_explorer/test/classify_rb_swap.py /tmp/earth_capture_0.png
```
两张 CLEAN + 目检。`grep -c "dot(Pv.xyz - Cv, Pv.xyz)" applications/earth_explorer/feed_layer.cpp applications/earth_explorer/flight_data.cpp applications/earth_explorer/ais_data.cpp applications/earth_explorer/sat_data.cpp` 确认前半球剔除段各层仍在(未被形状改动误删)。

- [ ] **Step 3: 多层辨识度总览截图**

```bash
cd /Users/USER/osgverse/build/sdk_core/bin && rm -f /tmp/earth_capture_0.png
KEYF=$(grep FIRMS "$HOME/Library/Application Support/EarthExplorer/keys.env" | cut -d= -f2)
KEYA=$(grep AISSTREAM "$HOME/Library/Application Support/EarthExplorer/keys.env" | cut -d= -f2)
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=200 EARTH_FIRES=1 EARTH_SHIPS=1 EARTH_FIRMS_KEY=$KEYF EARTH_AISSTREAM_KEY=$KEYA \
  ./osgVerse_EarthExplorer --goto 10 105 3000 2>&1 | tail -2
```
读 PNG:火点星芒 vs 船五边形肉眼可分(对比重设计前"全圆点/全三角")。

- [ ] **Step 4: 打包 + 签名**(打包后不再运行 dist app)

```bash
bash /Users/USER/osgverse/packaging/package_macos.sh && codesign -v /Users/USER/osgverse/dist/EarthExplorer.app && echo SIGN_OK
```

- [ ] **Step 5: HANDOFF.md 顶部新增章节**(内容:vision 加强版标记系统完成;marker_style 单一真源架构;形状分配表;面板联动;红线"只动外观"守住;前半球剔除未回归;真机待验=用户双击看整套徽章观感 + 面板联动;桌面软链已指向最新)

- [ ] **Step 6: Commit**

```bash
git add HANDOFF.md
git commit -m "docs(earth): HANDOFF — marker system redesign (vision+) complete, real-machine visual pending"
```

---

## 真机验证(计划外,交付后用户执行)

用户双击桌面 EarthExplorer,开多层转动地球,确认:每类信息形状可辨、船是五边形随航向、图层目录每项 icon 与地球标记同形同色、详情卡 chip 带形状 icon。任何形状辨识度不满意反馈后微调对应 GLSL/ImGui 绘制(仅外观)。
