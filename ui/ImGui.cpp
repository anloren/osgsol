// Prevent GLES2/gl2.h to redefine gl* functions
#define GL_GLES_PROTOTYPES 0

#include <GL/glew.h>
#include <osg/Version>
#include <osg/Camera>
#include <osgDB/FileNameUtils>
#include <osgDB/ReadFile>
#include <imgui/imgui.h>
#if defined(OSG_GLES1_AVAILABLE) || defined(OSG_GLES2_AVAILABLE) || defined(OSG_GLES3_AVAILABLE)
    // Specified GLES version of IMGUI backend?
#else
#   include <imgui/imgui_impl_opengl2.h>
#endif
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/ImGuizmo.h>
#include "ImGui.h"
#include "ImGui.Styles.h"
#include "pipeline/Utilities.h"
#include <cstdio>    // popen/pclose: macOS 剪贴板接线用
#include <cstring>
#include <string>
using namespace osgVerse;

extern void StyleColorsVisualStudio(ImGuiStyle* dst = (ImGuiStyle*)0);
extern void StyleColorsSonicRiders(ImGuiStyle* dst = (ImGuiStyle*)0);
extern void StyleColorsLightBlue(ImGuiStyle* dst = (ImGuiStyle*)0);
extern void StyleColorsTransparent(ImGuiStyle* dst = (ImGuiStyle*)0);
extern void StyleColorsMissionControl(ImGuiStyle* dst = (ImGuiStyle*)0);
static bool s_useImguiLoaderGL3 = true;

void newImGuiFrame(osg::RenderInfo& renderInfo, double& time, std::function<void(ImGuiIO&)> func)
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (!context) return; else if (time < 0.0f)
    {
        glewInit(); time = 0.0f;
        s_useImguiLoaderGL3 = glewIsSupported("GL_VERSION_3_0");
#if defined(OSG_GLES1_AVAILABLE) || defined(OSG_GLES2_AVAILABLE) || defined(OSG_GLES3_AVAILABLE)
        ImGui_ImplOpenGL3_Init();
#else
        if (s_useImguiLoaderGL3) ImGui_ImplOpenGL3_Init();
        else ImGui_ImplOpenGL2_Init();
#endif
    }

#if defined(OSG_GLES1_AVAILABLE) || defined(OSG_GLES2_AVAILABLE) || defined(OSG_GLES3_AVAILABLE)
    ImGui_ImplOpenGL3_NewFrame();
#else
    if (s_useImguiLoaderGL3) ImGui_ImplOpenGL3_NewFrame();
    else ImGui_ImplOpenGL2_NewFrame();
#endif

    ImGuiIO& io = ImGui::GetIO();
    if (renderInfo.getView() != NULL)
    {
        osg::Viewport* viewport = (renderInfo.getCurrentCamera() != NULL)
            ? renderInfo.getCurrentCamera()->getViewport() : NULL;
        if (!viewport) viewport = renderInfo.getView()->getCamera()->getViewport();
        if (!viewport) { OSG_FATAL << "[ImGuiManager] Empty viewport!\n"; return; }
        io.DisplaySize = ImVec2(viewport->width(), viewport->height());

        double currentTime = renderInfo.getView()->getFrameStamp()->getSimulationTime();
        io.DeltaTime = currentTime - time + 0.0000001;
        time = currentTime; func(io);
    }
    else
    {
        OSG_FATAL << "[ImGuiManager] No view provided!\n";
    }
    ImGui::NewFrame();

    ImGuizmo::SetDrawlist(ImGui::GetBackgroundDrawList());
    ImGuizmo::BeginFrame();
}

void endImGuiFrame(osg::RenderInfo& renderInfo, ImGuiManager* manager,
    std::map<std::string, ImTextureID>& textureIdList,
    std::function<void(ImGuiContentHandler*, ImGuiContext*)> func)
{
    ImGuiContext* context = ImGui::GetCurrentContext();
    if (manager && context)
    {
        ImGuiContentHandler* v = manager->getContentHandler();
        if (v)
        {
            std::map<std::string, osg::ref_ptr<osg::Texture2D>>& tList = manager->getTextures();
            for (std::map<std::string, osg::ref_ptr<osg::Texture2D>>::iterator itr = tList.begin();
                 itr != tList.end(); ++itr)
            {
                osg::Texture2D* tex2D = itr->second.get();
#if OSG_VERSION_GREATER_THAN(3, 4, 1)
                if (tex2D->isDirty(renderInfo.getContextID())) tex2D->apply(*renderInfo.getState());
#else
                if (tex2D->getTextureParameterDirty(renderInfo.getContextID()) > 0)
                    tex2D->apply(*renderInfo.getState());
#endif

                osg::Texture::TextureObject* tObj = tex2D->getTextureObject(renderInfo.getContextID());
                if (tObj) textureIdList[itr->first] = (ImTextureID)tObj->id();
            }
            func(v, context);
        }
    }
    else return;

    ImGui::Render();
#if defined(OSG_GLES1_AVAILABLE) || defined(OSG_GLES2_AVAILABLE) || defined(OSG_GLES3_AVAILABLE)
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#else
    if (s_useImguiLoaderGL3)
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    else
        ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
#endif
}

void startImGuiContext(ImGuiManager* manager, std::map<std::string, ImFont*>& fonts)
{
    ImGui::CreateContext();
    int style = 20;  // v0.15-vision:「任务控制台」主题默认启用(仍可改回 1/2/10-13 调试用)
    switch (style)
    {
    case 1: ImGui::StyleColorsDark(); break;
    case 2: ImGui::StyleColorsLight(); break;
    case 10: StyleColorsVisualStudio(); break;
    case 11: StyleColorsSonicRiders(); break;
    case 12: StyleColorsLightBlue(); break;
    case 13: StyleColorsTransparent(); break;
    case 20: StyleColorsMissionControl(); break;
    default: ImGui::StyleColorsClassic(); break;
    }

    ImGuiIO& io = ImGui::GetIO();
    fonts[""] = io.Fonts->AddFontDefault();

#if defined(__APPLE__)
    // macOS 系统剪贴板接线(pbcopy/pbpaste):imgui 的默认实现只有在定义了
    // IMGUI_ENABLE_OSX_DEFAULT_CLIPBOARD_FUNCTIONS 且链接 ApplicationServices 时才走系统
    // 剪贴板,否则是应用内私有缓冲——系统里复制的文本(尤其中文)粘不进 ImGui 输入框。
    // GraphicsWindowCocoa 无 NSTextInputClient,中文 IME 无法直打,"外部复制 + Cmd+V 粘贴"
    // 是当前唯一可靠的中文输入通道,因此用命令行工具接系统剪贴板(零新增链接依赖)。
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text)
    {
        FILE* p = popen("/usr/bin/pbcopy", "w");
        if (p) { if (text) fwrite(text, 1, strlen(text), p); pclose(p); }
    };
    pio.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char*
    {
        static std::string buffer; buffer.clear();
        FILE* p = popen("/usr/bin/pbpaste", "r");
        if (!p) return NULL;
        char tmp[4096]; size_t n = 0;
        while ((n = fread(tmp, 1, sizeof(tmp), p)) > 0) buffer.append(tmp, n);
        pclose(p);
        return buffer.c_str();
    };
#endif

    std::string fontData = manager->getChineseSimplifiedFont();
    if (!fontData.empty())
    {
        fonts[osgDB::getStrippedName(fontData)] = io.Fonts->AddFontFromFileTTF(
            fontData.c_str(), 20.0f, NULL, io.Fonts->GetGlyphRangesChineseFull());
    }
    //io.Fonts->Build();

    /*unsigned char* pixels; int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    osg::Image* img = new osg::Image;
    img->setImage(width, height, 1, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE, pixels, osg::Image::NO_DELETE);
    osgDB::writeImageFile(*img, "test.png");*/
}

int convertImGuiCharacterKey(int key)
{
    if (key >= 'a' && key <= 'z') return (int)ImGuiKey_A + (key - 'a');
    if (key >= 'A' && key <= 'Z') return (int)ImGuiKey_A + (key - 'A');
    if (key >= '0' && key <= '9') return (int)ImGuiKey_0 + (key - '0');
    switch (key)
    {
    case ' ': return ImGuiKey_Space; case ',': return ImGuiKey_Comma;
    case '-': return ImGuiKey_Minus; case '.': return ImGuiKey_Period;
    case '/': return ImGuiKey_Slash; case ';': return ImGuiKey_Semicolon;
    case '=': return ImGuiKey_Equal; case '[': return ImGuiKey_LeftBracket;
    case '\\': return ImGuiKey_Backslash; case ']': return ImGuiKey_RightBracket;
    case '`': return ImGuiKey_GraveAccent; case '\'': return ImGuiKey_Apostrophe;
    default: return ImGuiKey_None;
    }
}

int convertImGuiSpecialKey(int key)
{
    if (key >= osgGA::GUIEventAdapter::KEY_F1 && key <= osgGA::GUIEventAdapter::KEY_F24)
        return (int)ImGuiKey_F1 + (key - osgGA::GUIEventAdapter::KEY_F1);
    switch (key)
    {
    case osgGA::GUIEventAdapter::KEY_Tab: return ImGuiKey_Tab;
    case osgGA::GUIEventAdapter::KEY_Left: return ImGuiKey_LeftArrow;
    case osgGA::GUIEventAdapter::KEY_Right: return ImGuiKey_RightArrow;
    case osgGA::GUIEventAdapter::KEY_Up: return ImGuiKey_UpArrow;
    case osgGA::GUIEventAdapter::KEY_Down: return ImGuiKey_DownArrow;
    case osgGA::GUIEventAdapter::KEY_Page_Up: return ImGuiKey_PageUp;
    case osgGA::GUIEventAdapter::KEY_Page_Down: return ImGuiKey_PageDown;
    case osgGA::GUIEventAdapter::KEY_Home: return ImGuiKey_Home;
    case osgGA::GUIEventAdapter::KEY_End: return ImGuiKey_End;
    case osgGA::GUIEventAdapter::KEY_Delete: return ImGuiKey_Delete;
    case osgGA::GUIEventAdapter::KEY_Insert: return ImGuiKey_Insert;
    case osgGA::GUIEventAdapter::KEY_BackSpace: return ImGuiKey_Backspace;
    case osgGA::GUIEventAdapter::KEY_Return: return ImGuiKey_Enter;
    case osgGA::GUIEventAdapter::KEY_Escape: return ImGuiKey_Escape;
    case osgGA::GUIEventAdapter::KEY_Caps_Lock: return ImGuiKey_CapsLock;
    case osgGA::GUIEventAdapter::KEY_KP_Enter: return ImGuiKey_KeypadEnter;
    default: return -1;
    }
}

class ImGuiHandler : public osgGA::GUIEventHandler
{
public:
    std::map<std::string, ImFont*> _fonts;
    bool _mousePressed[3];
    float _mouseWheel;

    ImGuiHandler() : _mouseWheel(0.0f)
    {
        _mousePressed[0] = false;
        _mousePressed[1] = false;
        _mousePressed[2] = false;
    }

    void start(ImGuiManager* manager)
    { startImGuiContext(manager, _fonts); }

    void release(ImGuiManager* manager)
    {
#if defined(OSG_GLES1_AVAILABLE) || defined(OSG_GLES2_AVAILABLE) || defined(OSG_GLES3_AVAILABLE)
        ImGui_ImplOpenGL3_Shutdown();
#else
        if (s_useImguiLoaderGL3) ImGui_ImplOpenGL3_Shutdown();
        else ImGui_ImplOpenGL2_Shutdown();
#endif
    }

    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
    {
        ImGuiIO& io = ImGui::GetIO();
        bool wantCaptureMouse = io.WantCaptureMouse;
        bool wantCaptureKeyboard = io.WantCaptureKeyboard;
        wantCaptureMouse |= ImGuizmo::IsUsing();

        switch (ea.getEventType())
        {
        case osgGA::GUIEventAdapter::KEYDOWN:
        case osgGA::GUIEventAdapter::KEYUP:
            //if (wantCaptureKeyboard)
            {
                const bool isKeyDown = ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN;
                const int c = ea.getKey(); const int special_key = convertImGuiSpecialKey(c);
                // 修饰键必须走 AddKeyEvent(ImGuiMod_*):imgui 1.87+ 起 io.KeyCtrl 等由
                // NewFrame 从键事件流重算,直接赋值会被覆盖(等于从没设上),导致
                // Ctrl/Cmd+A/C/V 等文本框快捷键一律失效。对每个按键事件同步一次修饰位,
                // 且不能只在 special_key 分支里做——'v' 这类字符键也要带上 Cmd/Ctrl 状态。
                const unsigned int mod = ea.getModKeyMask();
                io.AddKeyEvent(ImGuiMod_Ctrl, (mod & osgGA::GUIEventAdapter::MODKEY_CTRL) != 0);
                io.AddKeyEvent(ImGuiMod_Shift, (mod & osgGA::GUIEventAdapter::MODKEY_SHIFT) != 0);
                io.AddKeyEvent(ImGuiMod_Alt, (mod & osgGA::GUIEventAdapter::MODKEY_ALT) != 0);
                io.AddKeyEvent(ImGuiMod_Super, (mod & osgGA::GUIEventAdapter::MODKEY_SUPER) != 0);
                if (special_key > 0)
                {
                    io.AddKeyEvent((ImGuiKey)special_key, isKeyDown);
                }
                else if (c > 0 && c < 0xFF)
                {
                    io.AddKeyEvent((ImGuiKey)convertImGuiCharacterKey(c), isKeyDown);
                    if (isKeyDown) io.AddInputCharacter((unsigned short)c);
                }
                else if (c >= 0x100 && c < 0xE000 && (c < 0xD800 || c > 0xDFFF))
                {
                    // 平台层送达的已组合非 ASCII 字符(如 Windows WM_CHAR 的中文):
                    // 无键位可映射,只喂字符流。上限取 0xE000:排除 PUA(苹果功能键
                    // 0xF700+)与 X11 功能/修饰键 keysym 区(0xFE00-0xFFFF),否则按
                    // Ctrl/方向键会往输入框塞乱码。CJK 基本区(0x4E00-0x9FFF)完整覆盖。
                    // macOS Cocoa 窗口无 NSTextInputClient,IME 不会组字,此分支在 mac
                    // 上不触发——中文输入见剪贴板粘贴通道。
                    // T2 复审carry-forward(cheap):额外排除 UTF-16 代理区 0xD800-0xDFFF——
                    // 这段本身不是合法码点(只在 UTF-16 里用一对高低代理凑一个增补平面字符),
                    // 单个代理值送进 AddInputCharacter 会被当成孤立码点,产生非法/乱码字符。
                    if (isKeyDown) io.AddInputCharacter((unsigned int)c);
                }
                return wantCaptureKeyboard;
            }
        case osgGA::GUIEventAdapter::DOUBLECLICK:
        case osgGA::GUIEventAdapter::RELEASE:
        case osgGA::GUIEventAdapter::PUSH:
            io.MousePos = ImVec2(ea.getX(), io.DisplaySize.y - ea.getY());
            _mousePressed[0] = ea.getButtonMask() & osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON;
            _mousePressed[1] = ea.getButtonMask() & osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON;
            _mousePressed[2] = ea.getButtonMask() & osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON;
            return wantCaptureMouse;
        case osgGA::GUIEventAdapter::DRAG:
        case osgGA::GUIEventAdapter::MOVE:
            io.MousePos = ImVec2(ea.getX(), io.DisplaySize.y - ea.getY());
            return wantCaptureMouse;
        case osgGA::GUIEventAdapter::SCROLL:
            if (wantCaptureMouse)
                _mouseWheel = (ea.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_UP ? 1.0f : -1.0f);
            return wantCaptureMouse;
        default: return false;
        }
        return false;
    }

protected:
    virtual ~ImGuiHandler()
    {
        //if (s_useImguiLoaderGL3) ImGui_ImplOpenGL3_Shutdown();  // FIXME
        //else ImGui_ImplOpenGL2_Shutdown();
        ImGui::DestroyContext();
    }
};

struct ImGuiNewFrameCallback : public CameraDrawCallback
{
    ImGuiNewFrameCallback(osgGA::GUIEventHandler* h) : _handler(h), _time(-1.0f) {}
    osg::observer_ptr<osgGA::GUIEventHandler> _handler;
    mutable double _time;

    virtual void operator()(osg::RenderInfo& renderInfo) const override
    {
        newImGuiFrame(renderInfo, _time, [&](ImGuiIO& io) {
            ImGuiHandler* handler = static_cast<ImGuiHandler*>(_handler.get());
            if (handler)
            {
                io.MouseDown[0] = handler->_mousePressed[0];
                io.MouseDown[1] = handler->_mousePressed[1];
                io.MouseDown[2] = handler->_mousePressed[2];
                io.MouseWheel = handler->_mouseWheel; handler->_mouseWheel = 0.0f;
            }
        });
    }
};

struct ImGuiRenderCallback : public CameraDrawCallback
{
    ImGuiRenderCallback(ImGuiManager* m, osgGA::GUIEventHandler* h) : _handler(h), _manager(m) {}
    mutable std::map<std::string, ImTextureID> _textureIdList;
    osg::observer_ptr<osgGA::GUIEventHandler> _handler;
    ImGuiManager* _manager;

    virtual void operator()(osg::RenderInfo& renderInfo) const override
    {
        endImGuiFrame(renderInfo, _manager, _textureIdList,
                      [&](ImGuiContentHandler* v, ImGuiContext* context) {
            ImGuiHandler* handler = static_cast<ImGuiHandler*>(_handler.get());
            if (handler) v->ImGuiFonts = handler->_fonts;
            v->ImGuiTextures = _textureIdList; v->context = context;
            v->runInternal(_manager);
        });
    }
};

////////////// ImGuiManager //////////////

ImGuiManager::ImGuiManager()
{}

ImGuiManager::~ImGuiManager()
{}

void ImGuiManager::initializeEventHandler2D()
{
    _imguiHandler = new ImGuiHandler;
    static_cast<ImGuiHandler*>(_imguiHandler.get())->start(this);
}

void ImGuiManager::initialize(ImGuiContentHandler* cb, bool eventsFrom3D)
{
    _contentHandler = cb;
    if (eventsFrom3D) initializeEventHandler3D();
    else initializeEventHandler2D();
}

void ImGuiManager::shutdown()
{
    if (_imguiHandler.valid())
        static_cast<ImGuiHandler*>(_imguiHandler.get())->release(this);
}

void ImGuiManager::addToView(osgViewer::View* view, osg::Camera* specCam)
{
    osg::Camera* cam = (specCam != NULL) ? specCam : view->getCamera();
    osg::ref_ptr<ImGuiNewFrameCallback> nfcb = new ImGuiNewFrameCallback(_imguiHandler.get());
    osg::ref_ptr<ImGuiRenderCallback> rcb = new ImGuiRenderCallback(this, _imguiHandler.get());
    nfcb->setup(cam, PRE_DRAW); rcb->setup(cam, POST_DRAW);
    if (view) view->addEventHandler(_imguiHandler.get());
}

void ImGuiManager::setGuiTexture(const std::string& name, const std::string& file)
{
    osg::ref_ptr<osg::Image> image = osgDB::readImageFile(file);
    setGuiTexture(name, new osg::Texture2D(image.get()));
}

void ImGuiManager::setGuiTexture(const std::string& name, osg::Texture2D* tex2D)
{ _textures[name] = tex2D; }

void ImGuiManager::removeGuiTexture(const std::string& name)
{
    std::map<std::string, osg::ref_ptr<osg::Texture2D>>::iterator itr = _textures.find(name);
    if (itr != _textures.end()) _textures.erase(itr);
}
