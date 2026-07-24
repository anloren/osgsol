// Prevent GLES2/gl2.h to redefine gl* functions
#define GL_GLES_PROTOTYPES 0

#include <GL/glew.h>
#include <osg/Version>
#include <osg/Camera>
#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>
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
#include "ImGuiInputQueue.h"
#include "ImGuiScroll.h"
#include "ImGui.Styles.h"
#include "pipeline/Utilities.h"
#include <cstdio>    // popen/pclose: macOS 剪贴板接线用
#include <cstdlib>
#include <cstring>
#include <string>
using namespace osgVerse;

extern void StyleColorsVisualStudio(ImGuiStyle* dst = (ImGuiStyle*)0);
extern void StyleColorsSonicRiders(ImGuiStyle* dst = (ImGuiStyle*)0);
extern void StyleColorsLightBlue(ImGuiStyle* dst = (ImGuiStyle*)0);
extern void StyleColorsTransparent(ImGuiStyle* dst = (ImGuiStyle*)0);
extern void StyleColorsMissionControl(ImGuiStyle* dst = (ImGuiStyle*)0);
static bool s_useImguiLoaderGL3 = true;

void shutdownImGuiRendererBackend()
{
#if defined(OSG_GLES1_AVAILABLE) || defined(OSG_GLES2_AVAILABLE) || defined(OSG_GLES3_AVAILABLE)
    ImGui_ImplOpenGL3_Shutdown();
#else
    if (s_useImguiLoaderGL3) ImGui_ImplOpenGL3_Shutdown();
    else ImGui_ImplOpenGL2_Shutdown();
#endif
}

std::string osgVerse::defaultImGuiSettingsPath()
{
#if defined(_WIN32)
    const char* base = std::getenv("LOCALAPPDATA");
    return (base && base[0]) ? std::string(base) + "/osgVerse/imgui.ini" : std::string();
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return (home && home[0]) ? std::string(home) +
        "/Library/Application Support/osgVerse/imgui.ini" : std::string();
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) return std::string(xdg) + "/osgVerse/imgui.ini";
    const char* home = std::getenv("HOME");
    return (home && home[0]) ? std::string(home) + "/.config/osgVerse/imgui.ini" : std::string();
#endif
}

void osgVerse::configureImGuiProductInput(ImGuiIO& io)
{
    // The Earth shell is a keyboard-operable product surface, not a
    // mouse-only debug overlay.  Tab/Shift+Tab traverse controls and
    // Enter/Space activate the focused item.  Gamepad navigation remains off
    // because no product-level gamepad mapping is exposed.
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
}

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
    osgVerse::configureImGuiProductInput(io);
    static std::string s_iniFilename;
    s_iniFilename = defaultImGuiSettingsPath();
    if (!s_iniFilename.empty() && osgDB::makeDirectoryForFile(s_iniFilename))
        io.IniFilename = s_iniFilename.c_str();
    else
        io.IniFilename = NULL;
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

void applyImGuiInputEvents(ImGuiIO& io, const std::vector<osgVerse::ImGuiInputEvent>& events)
{
    for (std::vector<osgVerse::ImGuiInputEvent>::const_iterator it = events.begin();
         it != events.end(); ++it)
    {
        const osgVerse::ImGuiInputEvent& event = *it;
        if (event.type == osgVerse::ImGuiInputEvent::Key)
        {
            const unsigned int mod = event.modifiers;
            io.AddKeyEvent(ImGuiMod_Ctrl, (mod & osgGA::GUIEventAdapter::MODKEY_CTRL) != 0);
            io.AddKeyEvent(ImGuiMod_Shift, (mod & osgGA::GUIEventAdapter::MODKEY_SHIFT) != 0);
            io.AddKeyEvent(ImGuiMod_Alt, (mod & osgGA::GUIEventAdapter::MODKEY_ALT) != 0);
            io.AddKeyEvent(ImGuiMod_Super, (mod & osgGA::GUIEventAdapter::MODKEY_SUPER) != 0);

            const int specialKey = convertImGuiSpecialKey(event.key);
            if (specialKey > 0)
                io.AddKeyEvent((ImGuiKey)specialKey, event.down);
            else if (event.key > 0 && event.key < 0xFF)
            {
                io.AddKeyEvent((ImGuiKey)convertImGuiCharacterKey(event.key), event.down);
                if (event.down) io.AddInputCharacter((unsigned short)event.key);
            }
            else if (event.key >= 0x100 && event.key < 0xE000 &&
                     (event.key < 0xD800 || event.key > 0xDFFF) && event.down)
                io.AddInputCharacter((unsigned int)event.key);
        }
        else if (event.type == osgVerse::ImGuiInputEvent::MousePosition)
            io.AddMousePosEvent(event.x, io.DisplaySize.y - event.y);
        else if (event.type == osgVerse::ImGuiInputEvent::MouseButtons)
        {
            io.AddMousePosEvent(event.x, io.DisplaySize.y - event.y);
            io.AddMouseButtonEvent(0, (event.buttonMask & osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON) != 0);
            io.AddMouseButtonEvent(1, (event.buttonMask & osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON) != 0);
            io.AddMouseButtonEvent(2, (event.buttonMask & osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON) != 0);
        }
        else if (event.type == osgVerse::ImGuiInputEvent::MouseWheel)
            io.AddMouseWheelEvent(0.0f, event.wheel);
        else if (event.type == osgVerse::ImGuiInputEvent::VirtualMouse)
        {
            io.AddMousePosEvent(io.DisplaySize.x * event.x, io.DisplaySize.y * event.y);
            io.AddMouseButtonEvent(0, (event.buttonMask & osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON) != 0);
            io.AddMouseButtonEvent(1, (event.buttonMask & osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON) != 0);
            io.AddMouseButtonEvent(2, (event.buttonMask & osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON) != 0);
            io.AddMouseWheelEvent(0.0f, event.wheel);
        }
    }
}

class ImGuiHandler : public osgGA::GUIEventHandler
{
public:
    std::map<std::string, ImFont*> _fonts;
    osgVerse::ImGuiInputQueue _input;

    ImGuiHandler() : _started(false) {}

    void start(ImGuiManager* manager)
    {
        if (!_started && !manager->isShutdownRequested())
        { startImGuiContext(manager, _fonts); _started = true; }
    }

    void drain(ImGuiIO& io)
    { applyImGuiInputEvents(io, _input.takeAll()); }

    void publishCapture()
    {
        if (!ImGui::GetCurrentContext()) return;
        ImGuiIO& io = ImGui::GetIO();
        _input.publishCapture(io.WantCaptureMouse || ImGuizmo::IsUsing(), io.WantCaptureKeyboard);
    }

    void releaseOnDrawThread()
    {
        if (!_started) return;
        shutdownImGuiRendererBackend();
        ImGui::DestroyContext(); _started = false;
    }

    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
    {
        const bool wantCaptureMouse = _input.wantsMouse();
        const bool wantCaptureKeyboard = _input.wantsKeyboard();

        switch (ea.getEventType())
        {
        case osgGA::GUIEventAdapter::KEYDOWN:
        case osgGA::GUIEventAdapter::KEYUP:
            //if (wantCaptureKeyboard)
            {
                const bool isKeyDown = ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN;
                _input.push(osgVerse::ImGuiInputEvent::keyEvent(
                    ea.getKey(), isKeyDown, ea.getModKeyMask()));
                return wantCaptureKeyboard;
            }
        case osgGA::GUIEventAdapter::DOUBLECLICK:
        case osgGA::GUIEventAdapter::RELEASE:
        case osgGA::GUIEventAdapter::PUSH:
            _input.push(osgVerse::ImGuiInputEvent::mouseButtonsEvent(
                ea.getX(), ea.getY(), ea.getButtonMask()));
            return wantCaptureMouse;
        case osgGA::GUIEventAdapter::DRAG:
        case osgGA::GUIEventAdapter::MOVE:
            _input.push(osgVerse::ImGuiInputEvent::mousePositionEvent(ea.getX(), ea.getY()));
            return wantCaptureMouse;
        case osgGA::GUIEventAdapter::SCROLL:
            _input.push(osgVerse::ImGuiInputEvent::mouseWheelEvent(
                osgVerse::resolveImGuiWheelAmount(ea)));
            return wantCaptureMouse;
        default: return false;
        }
        return false;
    }

protected:
    virtual ~ImGuiHandler()
    {
    }

private:
    bool _started;
};

struct ImGuiNewFrameCallback : public CameraDrawCallback
{
    ImGuiNewFrameCallback(ImGuiManager* m, osgGA::GUIEventHandler* h)
        : _handler(h), _manager(m), _time(-1.0f) {}
    osg::observer_ptr<osgGA::GUIEventHandler> _handler;
    ImGuiManager* _manager;
    mutable double _time;

    virtual void operator()(osg::RenderInfo& renderInfo) const override
    {
        ImGuiHandler* handler = static_cast<ImGuiHandler*>(_handler.get());
        if (handler) { handler->start(_manager); newImGuiFrame(renderInfo, _time,
            [&](ImGuiIO& io) { handler->drain(io); }); }
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
        ImGuiHandler* handler = static_cast<ImGuiHandler*>(_handler.get());
        if (handler)
        {
            handler->publishCapture();
            if (_manager->consumeReleaseRequest()) handler->releaseOnDrawThread();
        }
    }
};

////////////// ImGuiManager //////////////

ImGuiManager::ImGuiManager() : _releaseRequested(false), _shutdownRequested(false)
{}

ImGuiManager::~ImGuiManager()
{}

void ImGuiManager::initializeEventHandler2D()
{
    _imguiHandler = new ImGuiHandler;
}

void ImGuiManager::initialize(ImGuiContentHandler* cb, bool eventsFrom3D)
{
    _releaseRequested.store(false); _shutdownRequested.store(false);
    _contentHandler = cb;
    if (eventsFrom3D) initializeEventHandler3D();
    else initializeEventHandler2D();
}

void ImGuiManager::shutdown()
{
    _shutdownRequested.store(true);
    _releaseRequested.store(true);
}

void ImGuiManager::addToView(osgViewer::View* view, osg::Camera* specCam)
{
    osg::Camera* cam = (specCam != NULL) ? specCam : view->getCamera();
    osg::ref_ptr<ImGuiNewFrameCallback> nfcb = new ImGuiNewFrameCallback(this, _imguiHandler.get());
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
