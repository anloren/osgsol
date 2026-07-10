# v0.15 vision:视觉美化 设计文档

> 状态:brainstorming 已定稿,待用户复核后转入 writing-plans。

## 背景与目标

用户真机使用 EarthExplorer(v0.14)后反馈三类问题:

1. `fly_to` 相机默认高度太低,经常只看到贴地纹理;
2. 日照默认走"实时天文位置"模式,经常飞到地球背光面显得很暗;
3. 整套 ImGui UI"略显粗糙"——主题配色、信息卡片排版都缺乏统一设计感;
4. 渲染点(尤其航班三角图标)边缘锯齿明显,和已经做了平滑处理的 feed 数据点不一致。

用户明确这是"推进 P2(太空层)前的视觉美化工作",完成后打标 **v0.15-vision**。

## 现状盘点(brainstorming 阶段代码探查结论)

| 项 | 现状 | 文件:行 |
|---|---|---|
| ImGui 主题 | 硬编码 `StyleColorsDark()`,另有 4 套自定义主题(VisualStudio/SonicRiders/LightBlue/Transparent)写好但从未启用 | `ui/ImGui.cpp:120-130`、`ui/ImGui.Styles.h` |
| 信息卡片 | 三处独立手搓实现(`ai_cards.cpp` 图表/图片/任务卡、`event_ticker.h` 事件流、`EarthControlUI.h` 航班/要素详情卡),排布/表头逻辑各写一份 | 见上 |
| fly_to 默认高度 | 50km(远低于地球半径 6371km,视野贴地) | `ai_setup.cpp:178` |
| 日照 | 无"跟随镜头"的持续模式;仅有手动方位角/仰角滑块 + 实时天文位置开关 + 一次性"太阳面向相机"按钮(点一下瞬时对齐,之后不再更新) | `EarthControlUI.h:138-150` |
| 航班图标着色器 | 硬边二值 discard(`step` 函数三角测试),无边缘平滑 | `flight_data.cpp:111-112` |
| feed 数据点着色器 | 已有 smoothstep 边缘软化(`r2` 半径测试 0.16→0.25) | `feed_layer.cpp:53-55` |
| MSAA | 未在 `GraphicsContext::Traits` 设置 samples 字段,窗口无多重采样 | `earth_main.cpp:989-993` |

## 设计决策(逐节,均已获用户确认)

### 1. 三处小修正

- **fly_to 默认高度**:50km → **150km**(`ai_setup.cpp:178`)。同步检查 `EarthControlUI.h` "跳转 Go To" 手动输入面板的默认高度字段,保持一致。
- **日照默认改为「常昼 / Always-Day」持续跟随模式**:新增布尔状态(默认 `true`),每帧根据当前相机位置重算太阳方向(逻辑复用现有"太阳面向相机"按钮的计算公式,改成每帧调用而非仅按钮触发一次)。用户手动碰方位角/仰角滑块或勾选"实时天文"时,自动关闭常昼模式(三者互斥,类似现有 `_realTimeSun`/`_followClock` 的互斥关系)。
- **抗锯齿双管齐下**:
  - 窗口 `GraphicsContext::Traits` 加 `samples = 4`(MSAA),仅影响主窗口/onscreen 路径;P0 引入的 `HeadlessCGLContext`(离屏测试用)不改动,离屏截图基线保持可比。
  - 航班三角图标片元着色器(`flight_data.cpp`)的硬边 `discard` 三角测试改为 smoothstep 边缘软化,做法与 `feed_layer.cpp` 的 `edge = 1.0 - smoothstep(...)` 一致,视觉语言统一。

### 2. UI 主题系统:「任务控制台 / Mission Control」

新增 `StyleColorsMissionControl(ImGuiStyle*)`(仿现有 4 套主题写法,加入 `ui/ImGui.Styles.h`),关键数值(源自 brainstorming 阶段确认的可视化对比稿):

| 用途 | 颜色 |
|---|---|
| WindowBg / ChildBg / PopupBg | `rgba(16,24,35,0.92)` |
| Border | `rgba(90,180,255,0.25)`(细边框微光,ImGui 无法做真正模糊光晕,此为近似) |
| FrameBg(二级面板色,输入框/按钮闲置态) | `rgb(22,33,46)` |
| 强调色(CheckMark/SliderGrab/Button 系/Header 系/TitleBgActive) | `#3ba7ff` |
| Text | `#eaf2f8` |
| TextDisabled | `rgba(180,200,220,0.65)` |
| WindowRounding/FrameRounding/ChildRounding/PopupRounding | `6.0f` |
| WindowBorderSize | `1.0f` |

`ui/ImGui.cpp:120` 的硬编码 `style=1` 分支改为默认使用新主题;原 FIXME 数字选择器保留(可切回旧主题调试用)。

**覆盖范围**:这是 ImGui 全局样式,自动应用到左上操作面板、底部 AI 对话条、顶部状态带、以及下节所有信息卡片——一次切换统一整体观感。**不影响**地球渲染层的数据点专属配色(地震红/灾害橙/军事绿等),两套系统完全独立。

### 3. Card 组件系统

新增 `applications/earth_explorer/ui_card.h`(header-only,风格仿 `ai_cards.h`/`event_ticker.h`):

```cpp
namespace earthui
{
    struct CardStyle
    {
        osg::Vec4 accentColor;      // 强调色,同时用作左侧色条/胶囊底色(若后续想换方案 B 观感)
        std::string chipLabel;      // 数据来源胶囊文字,如 "USGS"/"GDACS"/"AI"
    };

    struct Card
    {
        std::string id;                       // 稳定 ImGui ID(不随重排变化)
        CardStyle style;
        std::string title;
        std::string subtitle;                 // 通常是"来源 · 相对时间"
        std::function<void()> drawBody;       // 正文内容,自由绘制(图表/图片/进度条/列表)
        std::function<void()> onClose;        // 可选,点击 [x] 时调用
        bool closable = true;
    };

    // 纯函数:给定卡片高度提示与屏幕尺寸,算出每张卡的位置(右上角堆叠+溢出换列)。
    // 不依赖 ImGui,可单元测试(仿 input_gate.h 的先例)。
    std::vector<osg::Vec2> computeCardLayout(const std::vector<float>& heightHints,
                                             float screenW, float screenH);

    class CardStack
    {
    public:
        void upsert(const Card& card);        // 按 id 新增或更新
        void remove(const std::string& id);
        void draw();                          // 每帧调用一次,内部走 computeCardLayout + ImGui::Begin/End
    private:
        std::vector<Card> _cards;
    };
}
```

**迁移**(行为对用户不变,代码合一):
- `ai_cards.cpp`(图表/图片/视频任务卡)→ 各类型卡片改为 `cardStack.upsert(...)`,`drawBody` 回调里保留原有 ImDrawList 绘图/图片显示/进度条逻辑;删除该文件里自己的堆叠列宽计算(`kCardGap`/`curY`/换列判断)。
- `event_ticker.h` → 整个事件流作为**一张卡片**注册(`drawBody` 回调里画可滚动的事件列表),删除自己的 `SetNextWindowPos/Size` 定位代码。
- `EarthControlUI.h` 的航班详情卡、要素详情卡 → 选中项变化时 `upsert`,取消选中时 `remove`。
- 顶部状态带**不是**卡片(常驻、不可关闭、顶部居中非右上),保持独立实现不纳入 `CardStack`。

## 测试与验收计划

**可自动化验证**:
- `computeCardLayout` 纯函数单测(仿 `input_gate.h` 先例):多卡堆叠不重叠、溢出换列、边界情况(0 张卡、超高卡片单独占列)。
- MSAA + 航班着色器改动:离屏截图 + `classify_rb_swap.py` 分类器回归(复用 P1 建立的自动分类器,注意其"低空陆地视图会误报"的已知限制,按 P1 收尾时定的约定处理)。
- fly_to 高度改动:日志断言实际请求高度。
- 全套既有回归不能破:两个测试目标 exit 0、七源 fixture、AI 7 组 E2E、零影响断言、4 类历史回归截图(全球远景/极点/昆明倾斜/香港 3D Tiles)——**MSAA 是渲染管线级改动,历史回归必须重新过一遍,不能假设"多重采样不影响着色器逻辑"就跳过**(项目有过"看似无关的改动引发大范围渲染回归"的教训)。

**只能真机验收**:整套 UI 主题观感、卡片视觉效果、日照常昼模式的实际体验——ImGui 不出现在离屏 FBO 截图里(P0/P1 已确认的既有限制),这部分必须等用户真机确认,和历次 UI 改动一样。

## 版本收尾

全量回归矩阵(沿用 P0/P1 的 A-J 套路,新增本次的 MSAA/主题/卡片专项)→ 用户真机验收 → 打标 **v0.15-vision** → push。

## 风险与边界(诚实列出,不留空泛表述)

- ImGui 做不出真正的模糊光晕(box-shadow 效果),"细边框微光"只能靠边框颜色+低透明度近似,视觉上不会和网页 mockup 完全一致,真机会有落差。
- Card 组件迁移触及三处现有工作系统(AI 卡片/事件流/详情卡),虽然设计上是纯提取共享逻辑、不改业务行为,但改动面比"只加新主题"大,回归测试要覆盖到这三处的既有功能(AI 图表生成、事件流点击飞行、航班/要素点击详情)全部不受影响。
- MSAA 4x 在集成显卡/低端设备上可能有性能代价;本项目目标平台是 Apple Silicon,预期无问题,但如实标注这是一个假设未做专门 benchmark。
- "常昼"模式与现有手动方位角/实时天文模式的互斥切换,需要仔细梳理状态机(哪个动作触发关闭常昼),做的时候要对照现有 `_realTimeSun`/`_followClock` 的既有写法保持一致的交互习惯。
