#ifndef EARTH_EXPLORER_IME_BRIDGE_H
#define EARTH_EXPLORER_IME_BRIDGE_H

#if defined(__APPLE__)

namespace osgViewer { class Viewer; }

// 中文 IME 直打桥(macOS 专用,实现在 ime_bridge.mm,纯 app 侧方案、零 OSG 改动):
// OSG 的 GraphicsWindowCocoa GLView 只实现 keyDown:(逐键直送 OSG 事件队列),没有
// NSTextInputClient,系统输入法永远无法进入组字状态——中文直打在 OSG 层面就是断的。
// 本桥往 Cocoa 窗口里挂一个 1x1 隐藏 overlay NSView(实现 NSTextInputClient):
//   - ImGui 文本框激活(WantTextInput)时把 firstResponder 切到 overlay:
//       * IME 可正常组字(拼音候选窗),上屏结果经 AddInputCharactersUTF8 注入 ImGui;
//       * ASCII 直输、Enter/Esc/退格/方向键经 doCommandBySelector 映射成 ImGui 键;
//       * Cmd+V/C/X/A 等快捷键转发进 ImGui(剪贴板通道保持可用);
//       * 期间 GLView 收不到任何 keyDown → OSG 热键被**物理隔离**(与 GlobalKeyboardGate
//         互为双保险,组字中的按键连 OSG 事件队列都进不去)。
//   - 失焦时切回 GLView,一切行为与从前完全一致。
namespace earthime
{
    struct ProductTextTarget
    {
        void* userData = nullptr;
        void (*commitText)(void*, const char*) = nullptr;
        void (*setMarkedText)(void*, const char*) = nullptr;
        void (*cancelComposition)(void*) = nullptr;
        void (*keyTap)(void*, int) = nullptr;
    };

    // 尝试安装(幂等,失败一次即放弃):找 viewer 的 GraphicsWindowCocoa 挂 overlay。
    // 未 realize 时返回 false 且下帧重试;offscreen(无 Cocoa 窗口)永久禁用。
    bool ensureInstalled(osgViewer::Viewer* viewer);

    // 每帧(主线程 FRAME 事件)驱动键盘焦点,wantTextInput 取 ImGui::GetIO().WantTextInput。
    void updateFocus(bool wantTextInput);

    // Product-UI overload. Only one NSTextInputClient overlay is installed in
    // the Cocoa window; it routes composition to the focused UI backend.
    void updateFocus(bool wantImGuiTextInput, bool wantProductTextInput);
    void setProductTextTarget(const ProductTextTarget& target);
    void clearProductTextTarget();

    // ai_ui 每帧上报激活输入框的矩形(ImGui 坐标、左上原点;OSG Cocoa 窗口非
    // best-resolution,与 view"点"1:1,换算见 .mm 的 firstRectForCharacterRange),
    // 用于 IME 候选窗定位。线程安全(渲染线程写/主线程读)。
    void setInputRect(float x, float y, float w, float h);
}

#endif

#endif
