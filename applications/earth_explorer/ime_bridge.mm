// 中文 IME 直打桥实现(设计说明见 ime_bridge.h)。Objective-C++,仅 macOS 编译。
// 手动引用计数(项目未开 ARC);overlay/window 与进程同寿命,不做销毁路径。
#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>

#include <osg/Notify>
#include <osgViewer/Viewer>
#include <osgViewer/api/Cocoa/GraphicsWindowCocoa>
#include "3rdparty/imgui/imgui.h"
#include "ime_bridge.h"
#include <mutex>

@class EarthIMEView;

// ---------------- 共享状态 ----------------
namespace
{
    // 输入框矩形(ImGui 坐标,左上原点)。渲染线程写(ai_ui.cpp
    // 的 draw 在 ImGui 帧回调里跑),主线程读(interpretKeyEvents 期间 IME 询问
    // firstRectForCharacterRange)——加锁互斥。
    std::mutex s_rectMutex;
    bool s_hasRect = false;
    float s_rectX = 0.0f, s_rectY = 0.0f, s_rectW = 0.0f, s_rectH = 0.0f;

    NSWindow* s_win = nil;            // GraphicsWindowCocoaWindow(NSWindow 子类)
    NSView* s_glView = nil;           // OSG 的 GLView(contentView)
    EarthIMEView* s_overlay = nil;    // 隐藏的 NSTextInputClient overlay
    int s_installState = 0;           // 0=未试(可重试) / 1=已装 / -1=永久放弃
    earthime::ProductTextTarget s_productTarget;
    bool s_productActive = false;

    void commitTextToFocusedUi(const char* utf8)
    {
        if (s_productActive && s_productTarget.commitText)
            s_productTarget.commitText(s_productTarget.userData, utf8);
        else if (ImGui::GetCurrentContext() != NULL)
            ImGui::GetIO().AddInputCharactersUTF8(utf8);
    }

    void markTextForFocusedUi(const char* utf8)
    {
        if (s_productActive && s_productTarget.setMarkedText)
            s_productTarget.setMarkedText(s_productTarget.userData, utf8);
    }

    void cancelFocusedComposition()
    {
        if (s_productActive && s_productTarget.cancelComposition)
            s_productTarget.cancelComposition(s_productTarget.userData);
    }

    bool productKeyTap(int key)
    {
        if (key <= 0 || !s_productActive || !s_productTarget.keyTap) return false;
        s_productTarget.keyTap(s_productTarget.userData, key);
        return true;
    }

    // 打一组"按下+抬起":ImGui 1.87+ 输入走事件队列,同键一帧两次转换会被
    // trickle 自动拆到相邻两帧,down/up 同时入队是官方支持的注入方式。
    void imguiKeyTap(ImGuiKey key)
    {
        if (ImGui::GetCurrentContext() == NULL || key == ImGuiKey_None) return;
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(key, true);
        io.AddKeyEvent(key, false);
    }

    // 带修饰键的 tap(Shift+方向键选择、Cmd+V 粘贴等):mod 按下→键 tap→mod 抬起,
    // trickle 保证 mod 在键按下的那一帧仍处于按下态。
    void imguiKeyTapWithMods(ImGuiKey key, bool superMod, bool shiftMod, bool ctrlMod, bool altMod)
    {
        if (ImGui::GetCurrentContext() == NULL || key == ImGuiKey_None) return;
        ImGuiIO& io = ImGui::GetIO();
        if (superMod) io.AddKeyEvent(ImGuiMod_Super, true);
        if (shiftMod) io.AddKeyEvent(ImGuiMod_Shift, true);
        if (ctrlMod) io.AddKeyEvent(ImGuiMod_Ctrl, true);
        if (altMod) io.AddKeyEvent(ImGuiMod_Alt, true);
        io.AddKeyEvent(key, true);
        io.AddKeyEvent(key, false);
        if (altMod) io.AddKeyEvent(ImGuiMod_Alt, false);
        if (ctrlMod) io.AddKeyEvent(ImGuiMod_Ctrl, false);
        if (shiftMod) io.AddKeyEvent(ImGuiMod_Shift, false);
        if (superMod) io.AddKeyEvent(ImGuiMod_Super, false);
    }

    ImGuiKey charToImGuiKey(unichar c)
    {
        if (c >= 'a' && c <= 'z') return (ImGuiKey)(ImGuiKey_A + (c - 'a'));
        if (c >= 'A' && c <= 'Z') return (ImGuiKey)(ImGuiKey_A + (c - 'A'));
        if (c >= '0' && c <= '9') return (ImGuiKey)(ImGuiKey_0 + (c - '0'));
        return ImGuiKey_None;
    }

    const NSRange kEmptyRange = { NSNotFound, 0 };
}

// ---------------- NSTextInputClient overlay ----------------
@interface EarthIMEView : NSView <NSTextInputClient>
{
    NSString* _marked;   // 组字中的 marked text(仅记账,ImGui 输入框不显示预编辑串)
}
- (void)discardComposition;
@end

@implementation EarthIMEView

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView { return YES; }
// 1x1 隐藏 view,不参与鼠标命中(否则会挡 GLView 左上角一个像素的点击)
- (NSView*)hitTest:(NSPoint)point { return nil; }

- (void)discardComposition
{
    if (_marked) { [_marked release]; _marked = nil; }
    cancelFocusedComposition();
    [[self inputContext] discardMarkedText];
}

- (void)keyDown:(NSEvent*)theEvent
{
    // Cmd 组合键不进 IME:直接转发 ImGui(Cmd+V 粘贴/Cmd+A 全选/Cmd+C/X 复制剪切)。
    // ImGui 侧 io.ConfigMacOSXBehaviors 生效,文本框快捷键认 Super(=Cmd)。
    NSEventModifierFlags mods = [theEvent modifierFlags];
    if (mods & NSEventModifierFlagCommand)
    {
        NSString* s = [theEvent charactersIgnoringModifiers];
        if (s != nil && [s length] > 0)
        {
            const unichar c = [s characterAtIndex:0];
            if (s_productActive)
            {
                if (c == 'v' || c == 'V')
                {
                    NSString* paste = [[NSPasteboard generalPasteboard]
                        stringForType:NSPasteboardTypeString];
                    if (paste != nil) commitTextToFocusedUi([paste UTF8String]);
                    return;
                }
                if (c == 'a' || c == 'A') { productKeyTap(12); return; }
                if (c == 'c' || c == 'C') { productKeyTap(13); return; }
                if (c == 'x' || c == 'X') { productKeyTap(14); return; }
            }
            imguiKeyTapWithMods(charToImGuiKey([s characterAtIndex:0]),
                                true, (mods & NSEventModifierFlagShift) != 0,
                                (mods & NSEventModifierFlagControl) != 0,
                                (mods & NSEventModifierFlagOption) != 0);
        }
        return;
    }
    // 其余全部交给输入上下文:IME 组字走 setMarkedText/insertText,
    // 普通字符走 insertText,功能键走 doCommandBySelector。
    [self interpretKeyEvents:[NSArray arrayWithObject:theEvent]];
}

- (void)keyUp:(NSEvent*)theEvent
{
    // 字符注入(AddInputCharactersUTF8)与功能键 tap 都不依赖真实 keyUp,吞掉即可;
    // 特意不转发给 GLView——overlay 聚焦期间 OSG 不该看到任何键盘事件。
    (void)theEvent;
}

// ---- NSTextInputClient ----

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange
{
    (void)replacementRange;
    NSString* s = [string isKindOfClass:[NSAttributedString class]]
                ? [(NSAttributedString*)string string] : (NSString*)string;
    if (_marked) { [_marked release]; _marked = nil; }   // 上屏 = 组字结束
    if (s != nil && [s length] > 0) commitTextToFocusedUi([s UTF8String]);
}

- (void)setMarkedText:(id)string selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange
{
    (void)selectedRange; (void)replacementRange;
    NSString* s = [string isKindOfClass:[NSAttributedString class]]
                ? [(NSAttributedString*)string string] : (NSString*)string;
    [_marked release];
    _marked = (s != nil && [s length] > 0) ? [s copy] : nil;
    markTextForFocusedUi(_marked != nil ? [_marked UTF8String] : "");
    // 组字过程不向 ImGui 注入任何东西:预编辑串只显示在 IME 候选窗里,
    // 绝不会漏成热键或半截字符(overlay 聚焦期间 OSG 收不到 keyDown)。
}

- (void)unmarkText
{
    // IME 要求把组字串原样上屏(如按数字键直接选候选后的收尾、或点击别处)
    if (_marked != nil)
    {
        commitTextToFocusedUi([_marked UTF8String]);
        [_marked release]; _marked = nil;
    }
}

- (BOOL)hasMarkedText { return _marked != nil && [_marked length] > 0; }

- (NSRange)markedRange
{
    if (_marked != nil && [_marked length] > 0) return NSMakeRange(0, [_marked length]);
    return kEmptyRange;
}

- (NSRange)selectedRange { return kEmptyRange; }

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText
{ return [NSArray array]; }

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:(NSRangePointer)actualRange
{ (void)range; (void)actualRange; return nil; }

- (NSUInteger)characterIndexForPoint:(NSPoint)point
{ (void)point; return 0; }

// IME 候选窗定位:把 ai_ui 上报的输入框矩形(ImGui 坐标/左上原点)换算成
// 屏幕坐标(点/左下原点)。没有矩形时退化到窗口底边中部(对话条常驻位置附近)。
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange
{
    (void)range; (void)actualRange;
    NSView* host = s_glView != nil ? s_glView : self;
    NSRect vb = [host bounds];
    // ImGui 坐标 → view 点的比例:OSG 3.6.5 GraphicsWindowCocoa 从不开启
    // wantsBestResolutionOpenGLSurface,ImGui DisplaySize 与 view bounds 是 1:1 的
    // "点"——之前除 backingScaleFactor 会在 Retina 上把坐标折半,候选窗漂到错误
    // 位置(复审抓的坑)。比例不写死 1.0 而用两者实测之比,对未来 OSG 行为变化稳健。
    CGFloat scale = 1.0;
    if (ImGui::GetCurrentContext() != NULL && vb.size.height > 0.0)
    {
        float dispH = ImGui::GetIO().DisplaySize.y;
        if (dispH > 0.0f) { scale = (CGFloat)dispH / vb.size.height; }
    }
    if (scale <= 0.0) scale = 1.0;

    NSRect local;
    bool hasRect = false;
    float rx = 0.0f, ry = 0.0f, rw = 0.0f, rh = 0.0f;
    {
        std::lock_guard<std::mutex> guard(s_rectMutex);
        hasRect = s_hasRect; rx = s_rectX; ry = s_rectY; rw = s_rectW; rh = s_rectH;
    }
    if (hasRect)
    {
        // ImGui 单位→点,y 从"顶起"翻成"底起"(取矩形下边缘,候选窗浮在输入框下方)
        local = NSMakeRect(rx / scale, vb.size.height - (ry + rh) / scale,
                           rw / scale, rh / scale);
    }
    else
    {
        local = NSMakeRect(vb.size.width * 0.3, 40.0, vb.size.width * 0.4, 24.0);
    }
    NSRect winRect = [host convertRect:local toView:nil];
    return s_win != nil ? [s_win convertRectToScreen:winRect] : winRect;
}

// 功能键:输入上下文吃剩的命令映射成 ImGui 键(组字期间 Enter/Esc/退格由 IME
// 优先消费,不会走到这里——正确语义)。不调 super,避免未映射命令触发系统蜂鸣。
- (void)doCommandBySelector:(SEL)selector
{
    int productKey = 0;
    if (selector == @selector(insertNewline:)) productKey = 1;
    else if (selector == @selector(insertTab:)) productKey = 2;
    else if (selector == @selector(deleteBackward:)) productKey = 3;
    else if (selector == @selector(deleteForward:)) productKey = 4;
    else if (selector == @selector(moveLeft:)) productKey = 5;
    else if (selector == @selector(moveRight:)) productKey = 6;
    else if (selector == @selector(moveUp:)) productKey = 7;
    else if (selector == @selector(moveDown:)) productKey = 8;
    else if (selector == @selector(moveToBeginningOfLine:)) productKey = 9;
    else if (selector == @selector(moveToEndOfLine:)) productKey = 10;
    else if (selector == @selector(cancelOperation:)) productKey = 11;
    if (productKeyTap(productKey)) return;

    if (selector == @selector(insertNewline:)) { imguiKeyTap(ImGuiKey_Enter); }
    else if (selector == @selector(insertTab:)) { imguiKeyTap(ImGuiKey_Tab); }
    else if (selector == @selector(deleteBackward:)) { imguiKeyTap(ImGuiKey_Backspace); }
    else if (selector == @selector(deleteForward:)) { imguiKeyTap(ImGuiKey_Delete); }
    else if (selector == @selector(moveLeft:)) { imguiKeyTap(ImGuiKey_LeftArrow); }
    else if (selector == @selector(moveRight:)) { imguiKeyTap(ImGuiKey_RightArrow); }
    else if (selector == @selector(moveUp:)) { imguiKeyTap(ImGuiKey_UpArrow); }
    else if (selector == @selector(moveDown:)) { imguiKeyTap(ImGuiKey_DownArrow); }
    else if (selector == @selector(moveToBeginningOfLine:)) { imguiKeyTap(ImGuiKey_Home); }
    else if (selector == @selector(moveToEndOfLine:)) { imguiKeyTap(ImGuiKey_End); }
    else if (selector == @selector(moveLeftAndModifySelection:))
    { imguiKeyTapWithMods(ImGuiKey_LeftArrow, false, true, false, false); }
    else if (selector == @selector(moveRightAndModifySelection:))
    { imguiKeyTapWithMods(ImGuiKey_RightArrow, false, true, false, false); }
    else if (selector == @selector(moveToBeginningOfLineAndModifySelection:))
    { imguiKeyTapWithMods(ImGuiKey_Home, false, true, false, false); }
    else if (selector == @selector(moveToEndOfLineAndModifySelection:))
    { imguiKeyTapWithMods(ImGuiKey_End, false, true, false, false); }
    else if (selector == @selector(cancelOperation:))
    {
        // Esc:ImGui InputText 原生语义=撤销本次编辑并失焦;失焦后 WantTextInput
        // 变假,下一帧 updateFocus 会把 firstResponder 还给 GLView。
        imguiKeyTap(ImGuiKey_Escape);
    }
    // 其余命令(如 deleteWordBackward:)v1 不映射,静默忽略
}

@end

// ---------------- C++ 接口 ----------------
namespace earthime
{

bool ensureInstalled(osgViewer::Viewer* viewer)
{
    if (s_installState != 0) return s_installState == 1;
    if (viewer == NULL || !viewer->isRealized()) return false;   // 未 realize:下帧再试

    osgViewer::Viewer::Windows windows;
    viewer->getWindows(windows);
    osgViewer::GraphicsWindowCocoa* cocoaWin = NULL;
    for (size_t i = 0; i < windows.size(); ++i)
    {
        cocoaWin = dynamic_cast<osgViewer::GraphicsWindowCocoa*>(windows[i]);
        if (cocoaWin != NULL) break;
    }
    if (cocoaWin == NULL || cocoaWin->getWindow() == nil)
    {
        // offscreen(HeadlessCGL)/非 Cocoa 窗口:本进程内永久禁用,零影响
        s_installState = -1;
        return false;
    }

    s_win = (NSWindow*)cocoaWin->getWindow();
    s_glView = [s_win contentView];
    s_overlay = [[EarthIMEView alloc] initWithFrame:NSMakeRect(0.0, 0.0, 1.0, 1.0)];
    [s_glView addSubview:s_overlay];
    s_installState = 1;
    OSG_NOTICE << "[IME] Chinese IME bridge installed (overlay NSTextInputClient)" << std::endl;
    return true;
}

void updateFocus(bool wantTextInput)
{
    updateFocus(wantTextInput, false);
}

void updateFocus(bool wantImGuiTextInput, bool wantProductTextInput)
{
    if (s_installState != 1 || s_win == nil || s_overlay == nil) return;
    const bool nextProductActive = wantProductTextInput &&
                                   s_productTarget.userData != nullptr;
    if (s_productActive != nextProductActive &&
        [s_win firstResponder] == (NSResponder*)s_overlay)
        [s_overlay discardComposition];
    s_productActive = nextProductActive;
    const bool wantTextInput = wantProductTextInput || wantImGuiTextInput;
    NSResponder* fr = [s_win firstResponder];
    if (wantTextInput)
    {
        if (fr != (NSResponder*)s_overlay) { [s_win makeFirstResponder:s_overlay]; }
    }
    else if (fr == (NSResponder*)s_overlay)
    {
        [s_overlay discardComposition];   // 丢弃未上屏的组字,防半截字符残留
        [s_win makeFirstResponder:s_glView];
    }
}

void setProductTextTarget(const ProductTextTarget& target)
{
    s_productTarget = target;
}

void clearProductTextTarget()
{
    s_productTarget = ProductTextTarget();
    s_productActive = false;
}

void setInputRect(float x, float y, float w, float h)
{
    std::lock_guard<std::mutex> guard(s_rectMutex);
    s_hasRect = true; s_rectX = x; s_rectY = y; s_rectW = w; s_rectH = h;
}

}

#endif
