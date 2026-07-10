#include PREPENDED_HEADER
#include <osg/io_utils>
#include <osg/CullFace>
#include <osg/Texture2D>
#include <osg/MatrixTransform>
#include <osgDB/Archive>
#include <osgDB/FileNameUtils>
#include <osgDB/ReadFile>
#include <osgDB/WriteFile>
#include <osgGA/StateSetManipulator>
#include <osgGA/TrackballManipulator>
#include <osgGA/EventVisitor>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <osg/DisplaySettings>

#include <modeling/Math.h>
#include <readerwriter/EarthManipulator.h>
#include <readerwriter/TileCallback.h>
#include <readerwriter/FileCache.h>
#include <pipeline/Pipeline.h>
#include <ui/ImGui.h>
#include <libhv/all/hlog.h>
#include "EarthControlUI.h"
#include "LayerManager.h"
#include "input_gate.h"
#if defined(__APPLE__)
#include "ime_bridge.h"
#endif
#include "precip_data.h"
#include "flight_data.h"
#include "sat_data.h"
#include "ais_data.h"
#include "tiles3d_data.h"
#include "ai_tools.h"
#include "ai_chat.h"
#include "ai_setup.h"
#include "ai_media.h"
#include "ai_world_tools.h"
#include "ai_query.h"
#include "feed_layer.h"
#include "marker_style.h"
#include "feeds/gdacs_feed.h"
#include "feeds/usgs_quakes_feed.h"
#include "feeds/eonet_feed.h"
#include "feeds/gdelt_feed.h"
#include "feeds/gpsjam_feed.h"
#include "feeds/unhcr_feed.h"
#include "feeds/nhc_feed.h"
#include "feeds/strategic_feed.h"
#include "feeds/firms_feed.h"
#include "geo_primitives.h"
#include <VerseCommon.h>
#if defined(__APPLE__)
#   include <OpenGL/OpenGL.h>   // EARTH_OFFSCREEN(测试基建)用:CGL 无头上下文,见下方 HeadlessCGLContext
#endif
#include <iostream>
#include <sstream>
#include <ctime>
#include <thread>
#include <atomic>
#include <vector>

#ifdef OSG_LIBRARY_STATIC
USE_OSG_PLUGINS()
USE_VERSE_PLUGINS()
USE_SERIALIZER_WRAPPER(DracoGeometry)
#endif

#define EARTH_INTERSECTION_MASK 0xf0000000
#define SIMPLE_VERSION 1

#if defined(__APPLE__)
// EARTH_OFFSCREEN 测试基建(macOS):纯 CGL 无头 GL 4.1 Core 上下文。
//
// 为什么不用 osgViewer 现成路径:
//  - traits->pbuffer=true → PixelBufferCocoa(NSOpenGLPixelBuffer 旧 API)只能拿到
//    GL 2.1 legacy 上下文 → 全部 #version 330 着色器编译失败("version '330' is not
//    supported"),截出来的是白噪声,回归截图假绿;
//  - 走 GraphicsWindowCocoa 建不可见窗口(远离屏幕 + ActivationPolicyProhibited +
//    orderOut)虽能拿到 GL 4.1,但实测只要 NSApplication 一注册,LaunchServices 就会
//    把本进程激活为 frontmost(macOS 的"由前台 app 启动即视为用户启动"协作激活),
//    用户正在用的 app 会被抢焦点——违反"自动化测试绝不打扰用户"的铁律;另外完全离屏
//    窗口的 surface 读回在 GL-on-Metal 上还有 ~1/4 概率整帧上下翻转(Apple shim 层)。
//
// 因此绕开 AppKit:CGLCreateContext 不需要窗口、不需要 NSApplication,进程保持纯
// console 进程身份,LaunchServices 全程无感知 → 构造性保证不弹窗、不抢焦、无 Dock。
// 无窗口即无系统默认帧缓冲,realize 时自建一个 FBO 并 setDefaultFboId(),OSG 的
// RenderStage 在 RTT pass 之后会自动绑回它,master 相机清屏/ImGui 等"画到屏幕"的
// 操作全部落到这个 FBO,不产生 GL 错误。抓帧(EARTH_AUTOCAP)走 finalCamera 的
// FBO attach 读回,见下方 AUTOCAP 分支。
class HeadlessCGLContext : public osg::GraphicsContext
{
public:
    HeadlessCGLContext(osg::GraphicsContext::Traits* traits)
        : _context(NULL), _fbo(0), _colorRB(0), _depthRB(0), _realized(false)
    {
        _traits = traits;
        setState(new osg::State);
        getState()->setGraphicsContext(this);
        getState()->setContextID(osg::GraphicsContext::createNewContextID());
    }

    virtual bool valid() const { return true; }
    virtual bool isRealizedImplementation() const { return _realized; }

    // 轻量探测:CGL 核心上下文能否创建(立即销毁,不影响任何状态)。用于 offscreen 分支
    // 决定走无头路径还是回退开窗。注意不能用"提前 gc->realize() 验证"——那会让
    // viewer.isRealized() 提前为真,viewer.realize() 全程被跳过(RealizeOperation/
    // 线程启动等初始化丢失,实测星空等依赖初始化的效果会消失)。
    static bool probe()
    {
        CGLPixelFormatAttribute attrs[] =
        {
            kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
            kCGLPFAAccelerated, (CGLPixelFormatAttribute)0
        };
        CGLPixelFormatObj pf = NULL; GLint n = 0;
        if (CGLChoosePixelFormat(attrs, &pf, &n) != kCGLNoError || !pf) return false;
        CGLContextObj ctx = NULL;
        CGLError err = CGLCreateContext(pf, NULL, &ctx);
        CGLDestroyPixelFormat(pf);
        if (err != kCGLNoError || !ctx) return false;
        CGLDestroyContext(ctx); return true;
    }

    virtual bool realizeImplementation()
    {
        CGLPixelFormatAttribute attrs[] =
        {
            kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
            kCGLPFAAccelerated,
            kCGLPFAColorSize, (CGLPixelFormatAttribute)24,
            kCGLPFAAlphaSize, (CGLPixelFormatAttribute)8,
            kCGLPFADepthSize, (CGLPixelFormatAttribute)24,
            kCGLPFAStencilSize, (CGLPixelFormatAttribute)8,
            (CGLPixelFormatAttribute)0
        };
        CGLPixelFormatObj pixelFormat = NULL; GLint numPixelFormats = 0;
        if (CGLChoosePixelFormat(attrs, &pixelFormat, &numPixelFormats) != kCGLNoError || !pixelFormat)
        { OSG_WARN << "[HeadlessCGLContext] CGLChoosePixelFormat failed" << std::endl; return false; }

        CGLError err = CGLCreateContext(pixelFormat, NULL, &_context);
        CGLDestroyPixelFormat(pixelFormat);
        if (err != kCGLNoError || !_context)
        { OSG_WARN << "[HeadlessCGLContext] CGLCreateContext failed: " << err << std::endl; return false; }

        // 自建"默认帧缓冲"FBO(color+depth/stencil renderbuffer,尺寸=traits)
        CGLSetCurrentContext(_context);
        glGenFramebuffers(1, &_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        glGenRenderbuffers(1, &_colorRB);
        glBindRenderbuffer(GL_RENDERBUFFER, _colorRB);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, _traits->width, _traits->height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, _colorRB);
        glGenRenderbuffers(1, &_depthRB);
        glBindRenderbuffer(GL_RENDERBUFFER, _depthRB);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, _traits->width, _traits->height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, _depthRB);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            OSG_WARN << "[HeadlessCGLContext] default FBO incomplete: 0x"
                     << std::hex << status << std::dec << std::endl;
        setDefaultFboId(_fbo);
        CGLSetCurrentContext(NULL);
        _realized = true; return true;
    }

    virtual void closeImplementation()
    {
        // _fbo/_colorRB/_depthRB 不必显式 delete:上下文不共享,CGLDestroyContext
        // 连带回收全部 GL 对象。
        if (_context) { CGLDestroyContext(_context); _context = NULL; }
        _realized = false;
    }

    virtual bool makeCurrentImplementation()
    { return CGLSetCurrentContext(_context) == kCGLNoError; }
    virtual bool makeContextCurrentImplementation(osg::GraphicsContext* /*readContext*/)
    { return makeCurrentImplementation(); }
    virtual bool releaseContextImplementation()
    { return CGLSetCurrentContext(NULL) == kCGLNoError; }
    virtual void bindPBufferToTextureImplementation(GLenum /*buffer*/) {}
    virtual void swapBuffersImplementation() { glFlush(); }   // 无 drawable,flush 即可

protected:
    virtual ~HeadlessCGLContext() { closeImplementation(); }
    CGLContextObj _context;
    GLuint _fbo, _colorRB, _depthRB;
    bool _realized;
};
#endif

extern std::vector<osg::Camera*> configureEarthRendering(
        osgViewer::View& viewer, osg::Group* root, osg::Node* earth, osgVerse::EarthAtmosphereOcean& eData,
        const std::string& mainFolder, unsigned int mask, int w, int h);
extern osg::Node* configureCityData(osgViewer::View& viewer, osg::Node* earthRoot,
                                    osgVerse::EarthAtmosphereOcean& earthRenderingUtils,
                                    const std::string& mainFolder, unsigned int mask, bool waitingMode);
extern osg::Camera* configureUI(osgViewer::View& viewer, osg::Group* root,
                                const std::string& mainFolder, int w, int h);

class EnvironmentHandler : public osgGA::GUIEventHandler
{
public:
    EnvironmentHandler(osgVerse::EarthAtmosphereOcean* eao, const std::string& folder)
    :   _earthData(eao), _mainFolder(folder), _pressingKey(0), _pathIndex(0), _sunAngle(0.0f)
    {
        _earthData->commonUniforms["OceanOpaque"]->set(0.0f);
        _earthData->commonUniforms["WorldSunDir"]->set(
            osg::Vec3(-1.0f, 0.0f, 0.0f) * osg::Matrix::rotate(_sunAngle, osg::Z_AXIS));
    }

    bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
    {
        osgViewer::View* view = static_cast<osgViewer::View*>(&aa);
        if (ea.getEventType() == osgGA::GUIEventAdapter::USER)
        {
            const osgDB::Options* ev = dynamic_cast<const osgDB::Options*>(ea.getUserData());
            std::string command = ev ? ev->getOptionString() : "";

            std::vector<std::string> commmandPair; osgDB::split(command, commmandPair, '/');
            handleCommand(view, commmandPair.front(), commmandPair.back());
        }
        else if (ea.getEventType() == osgGA::GUIEventAdapter::FRAME)
        {
            for (std::map<std::string, bool>::iterator itr = _toggles.begin(); itr != _toggles.end(); ++itr)
            {
                const std::string& cmd = itr->first; if (!itr->second) continue;
                if (cmd == "light")
                {
                    _sunAngle += 0.01f;
                    _earthData->commonUniforms["WorldSunDir"]->set(
                        osg::Vec3(-1.0f, 0.0f, 0.0f) * osg::Matrix::rotate(_sunAngle, osg::Z_AXIS));
                    view->getEventQueue()->userEvent(new osgDB::Options("value/" + std::to_string(_sunAngle)));
                }
                else if (cmd == "auto_rotate")
                {
                    // TODO
                }
            }
        }
        else if (ea.getEventType() == osgGA::GUIEventAdapter::KEYDOWN)
        {
            // 链首 GlobalKeyboardGate 打字时已 setHandled(见 earth_main 下方闸注释);
            // OSG 3.6.5 事件仍会广播到这里,必须自查,否则聊天键入穿透成快捷键。
            if (!ea.getHandled()) _pressingKey = ea.getKey();
        }
        else if (ea.getEventType() == osgGA::GUIEventAdapter::KEYUP)
        {
            // 先清按住态再判 handled:','/'.' 按住转太阳时点进聊天框再松开,KEYUP
            // 会被闸吞掉——若先 return,_pressingKey 残留、太阳永远转下去(复审抓的坑)。
            _pressingKey = 0;
            if (ea.getHandled()) return false;   // 同上:打字态按键对本处理器不可见
            osgVerse::EarthManipulator* earthMani =
                static_cast<osgVerse::EarthManipulator*>(view->getCameraManipulator());
            if (ea.getKey() == 'o')  // set an animation-path frame
            {
                earthMani->insertControlPointFromCurrentView((float)_pathIndex); _pathIndex += 60;
            }
            else if (ea.getKey() == 'i')  // save the animation-path
            {
                std::ofstream out("../city_path.txt", std::ios::out);
                const osgVerse::EarthManipulator::ControlPointSet& path = earthMani->getControlPoints();
                for (osgVerse::EarthManipulator::ControlPointSet::const_iterator it = path.begin();
                     it != path.end(); ++it)
                {
                    osgVerse::EarthManipulator::ControlPoint* cp = (*it).get();
                    out << cp->_time << "," << cp->_rotation << "," << cp->_tilt << "," << cp->_distance << "\n";
                }
            }
            else if (ea.getKey() == 'p')  // load and play the animation-path
            {
                std::ifstream in("../city_path.txt", std::ios::in); std::string line;
                if (in)
                {
                    earthMani->clearControlPoints();
                    while (std::getline(in, line))
                    {
                        if (line.empty()) continue; else if (line[0] == '#') continue;
                        std::vector<std::string> values; osgDB::split(line, values, ',');

                        if (values.size() < 4) continue;
                        osg::Quat q; std::stringstream ss(values[1]); ss >> q;
                        double time = atof(values[0].c_str()), tilt = atof(values[2].c_str());
                        double distance = atof(values[3].c_str());
                        earthMani->insertControlPoint(
                            new osgVerse::EarthManipulator::ControlPoint(time, q, distance, tilt));
                    }

                    if (earthMani->isAnimationRunning()) earthMani->stopAnimation();
                    earthMani->startAnimation();
                }
            }
        }

        if (_pressingKey > 0)
        {
            switch (_pressingKey)
            {
            case ',': case '<':
                _sunAngle -= 0.00005f;
                _earthData->commonUniforms["WorldSunDir"]->set(
                    osg::Vec3(-1.0f, 0.0f, 0.0f) * osg::Matrix::rotate(_sunAngle, osg::Z_AXIS));
                view->getEventQueue()->userEvent(new osgDB::Options("value/" + std::to_string(_sunAngle))); break;
            case '.': case '>':
                _sunAngle += 0.00005f;
                _earthData->commonUniforms["WorldSunDir"]->set(
                    osg::Vec3(-1.0f, 0.0f, 0.0f) * osg::Matrix::rotate(_sunAngle, osg::Z_AXIS));
                view->getEventQueue()->userEvent(new osgDB::Options("value/" + std::to_string(_sunAngle))); break;
            }
        }
        return false;
    }

    void handleCommand(osgViewer::View* view, const std::string& type, const std::string& cmd)
    {
        if (type == "button")
        {
            osgVerse::EarthManipulator* earthMani =
                static_cast<osgVerse::EarthManipulator*>(view->getCameraManipulator());
            if (cmd == "light") _toggles[cmd] = !_toggles[cmd];
            else if (cmd == "auto_rotate") _toggles[cmd] = !_toggles[cmd];
            else if (cmd == "go_home") earthMani->home(0.0);
            else if (cmd == "ocean")
            {
                bool v = !_toggles[cmd]; _toggles[cmd] = v;
                _earthData->commonUniforms["OceanOpaque"]->set(v ? 1.0f : 0.0f);
            }
            /*else if (cmd == "globe")
            {
                bool v = !_toggles[cmd]; _toggles[cmd] = v;
                _earthData->commonUniforms["GlobalOpaque"]->set(v ? 0.5f : 1.0f);
            }*/
            else if (cmd == "zoom_in") earthMani->performScale(osgGA::GUIEventAdapter::SCROLL_UP);
            else if (cmd == "zoom_out") earthMani->performScale(osgGA::GUIEventAdapter::SCROLL_DOWN);
        }
        else if (type == "item")
        {
            osgVerse::TileManager* mgr = osgVerse::TileManager::instance();
            if (cmd.find("seg") != std::string::npos)
                mgr->setLayerPath(osgVerse::TileCallback::USER, _mainFolder + "/Tiles/" + cmd + "/{z}/{x}/{y}.png");
        }
    }

protected:
    std::map<std::string, bool> _toggles;
    osgVerse::EarthAtmosphereOcean* _earthData;
    std::string _mainFolder;
    int _pressingKey, _pathIndex;
    float _sunAngle;
};

// 全局键盘闸(取代旧 ImGuiAwareKeyFilter 逐个包装方案,根治按键穿透):
// ImGui 占用键盘时(打字 WantTextInput / 控件激活 WantCaptureKeyboard)吞掉
// KEYDOWN/KEYUP——返回 true 让 OSG 把事件标记 handled。
//
// 本仓 OSG 3.6.5 的事件语义(读 Viewer::eventTraversal + GUIEventHandler::handle 证实):
//   1) 事件会**无条件广播**给 addEventHandler 列表里的每个 handler,再喂相机 manipulator;
//      handler 返回 true 只会 setHandled(true),并不中断广播。
//   2) 因此"闸拦住后面的人"依赖下游自查 ea.getHandled():osgViewer 自带的
//      StatsHandler('s')/WindowSizeHandler('f')/StateSetManipulator('w'等) 与本仓
//      EarthManipulator(空格=回 home,EarthManipulator.cpp 开头有 getHandled 检查)
//      都自查;应用自己的键位处理器(EnvironmentHandler / CreateCityHandler)已补上
//      同样的检查(见各自 handle)。
//   3) 闸必须**先于所有响应键位的 handler** 安装(addEventHandler 顺序=遍历顺序),
//      这样 handled 标记在它们看到事件之前就已置位;ui/ImGui.cpp 的 ImGuiHandler
//      不检查 getHandled(键盘注入无条件进行),所以闸不会断 ImGui 自己的输入路。
//
// 判定用 WantTextInput || WantCaptureKeyboard:前者=文本框激活,后者补上"控件按住/
// 弹窗激活"等 ImGui 明确占用键盘的态(本工程未开 NavEnableKeyboard,不会因窗口获得
// 焦点而常驻为真)。KEYUP 与打字态按下的 KEYDOWN 配对吞,细节见 input_gate.h。
// 已知边界:WantTextInput 由 ImGui NewFrame(渲染回调里)重算,点进输入框后的第一个
// 按键理论上有一帧窗口期——60fps 下 ~16ms,人手打字触不到。
class GlobalKeyboardGate : public osgGA::GUIEventHandler
{
public:
    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
    {
        osgGA::GUIEventAdapter::EventType t = ea.getEventType();
        if (t != osgGA::GUIEventAdapter::KEYDOWN && t != osgGA::GUIEventAdapter::KEYUP)
            return false;
        bool wants = false;
        if (ImGui::GetCurrentContext() != NULL)
        {
            ImGuiIO& io = ImGui::GetIO();
            wants = io.WantTextInput || io.WantCaptureKeyboard;
        }
        return _logic.filter(t == osgGA::GUIEventAdapter::KEYDOWN, ea.getKey(), wants);
    }

private:
    earthinput::KeyGateLogic _logic;
};

// 关窗即退出:setKeyEventSetsDone(0) 之后 Esc 不再退出,红色关闭按钮成了唯一日常
// 退出路径——但 OSG 关窗只发 CLOSE_WINDOW 事件并销毁窗口,没人 setDone 的话进程
// 会无窗空转,连 Cmd+Q 都失效(NSApp 事件泵随窗口一起没了)。这里补上映射;
// 顺带保证 ime_bridge 的 NSWindow 指针不会经历"窗亡进程活"的悬垂期。
class CloseWindowQuitHandler : public osgGA::GUIEventHandler
{
public:
    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
    {
        if (ea.getEventType() != osgGA::GUIEventAdapter::CLOSE_WINDOW) return false;
        osgViewer::View* view = dynamic_cast<osgViewer::View*>(&aa);
        if (view != NULL && view->getViewerBase() != NULL) view->getViewerBase()->setDone(true);
        return false;   // 不吞事件,窗口自身的关闭流程照走
    }
};

// TEMPORARY DIAGNOSTIC (investigation only, not a fix) — "全部" 预设深色瓦片补丁 bug 排查。
// EARTH_PAGER_DEBUG=1:每隔 EARTH_PAGER_DEBUG_INTERVAL 帧(默认 30,~0.5s@60fps)打印一次
// osgDB::DatabasePager 的队列深度(getFileRequestListSize=file+http 请求队列合计、
// getDataToCompileListSize、getDataToMergeListSize、getRequestsInProgress),用于比较
// "实时" vs "全部" 预设在同一相机位置下 pager 排队/延迟是否有可观测差异。零影响于未设置时。
class PagerDebugHandler : public osgGA::GUIEventHandler
{
public:
    PagerDebugHandler(osgDB::DatabasePager* pager)
        : _pager(pager), _frameCount(0),
          _interval(30), _startTick(osg::Timer::instance()->tick())
    {
        const char* iv = getenv("EARTH_PAGER_DEBUG_INTERVAL");
        if (iv && *iv) _interval = std::max(1, atoi(iv));
    }
    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&)
    {
        if (ea.getEventType() != osgGA::GUIEventAdapter::FRAME) return false;
        if ((_frameCount++ % _interval) != 0) return false;
        double t = osg::Timer::instance()->delta_s(_startTick, osg::Timer::instance()->tick());
        OSG_NOTICE << "[PagerDBG] t=" << t << "s frame=" << _frameCount
                   << " fileReqQueue=" << _pager->getFileRequestListSize()
                   << " dataToCompile=" << _pager->getDataToCompileListSize()
                   << " dataToMerge=" << _pager->getDataToMergeListSize()
                   << " requestsInProgress=" << (_pager->getRequestsInProgress() ? 1 : 0)
                   << std::endl;
        return false;
    }
protected:
    osg::observer_ptr<osgDB::DatabasePager> _pager;
    unsigned int _frameCount; int _interval;
    osg::Timer_t _startTick;
};

class LayerManagerDrainHandler : public osgGA::GUIEventHandler
{
public:
    explicit LayerManagerDrainHandler(LayerManager* layers) : _layers(layers) {}
    bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&) override
    {
        if (ea.getEventType() == osgGA::GUIEventAdapter::FRAME && _layers)
            _layers->drainPending();
        return false;
    }
private:
    LayerManager* _layers;
};

// 卫星拉取失败可见提示(2026-07-05 真机反馈:Starlink 被 CelesTrak 限流(403)时界面完全
// 没有任何提示,和早前 AI 生图静默失败是同一类问题)。图层目录 UI(EarthControlUI.h)本来
// 就每帧读一次 OverlayLayer.subtitle 现渲染(ImGui::TextDisabled),这里只需要把 subtitle
// 的值按 SatelliteLayer::fetchErrorText() 的结果每帧刷新即可,不需要新写任何 UI 绘制代码。
class SatFetchStatusHandler : public osgGA::GUIEventHandler
{
public:
    SatFetchStatusHandler(LayerManager* lm, SatelliteLayer* sat) : _lm(lm), _sat(sat) {}
    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&)
    {
        if (ea.getEventType() != osgGA::GUIEventAdapter::FRAME || !_lm || !_sat) return false;
        const std::vector<OverlayLayer> layers = _lm->layersSnapshot();
        updateOne(layers, "satstations", SatCategory::Station, "");
        updateOne(layers, "satnav", SatCategory::Navigation, "");
        updateOne(layers, "satwx", SatCategory::Weather, "");
        // Starlink 本来就有一句固定说明,拉取失败时追加而不是顶掉。
        updateOne(layers, "starlink", SatCategory::Starlink,
                  u8"约 7000 颗,纯视觉壳层,不可点选");
        // 只读诊断:EARTH_SAT_SUMMARY_DEBUG 设置时,每 ~120 帧打印一次卫星汇总 JSON——
        // 离屏 + 真实 CelesTrak 网络下用它验证 summaryJson 里的 ISS/天宫是真实抓取位置。
        static const bool s_sumDbg = (getenv("EARTH_SAT_SUMMARY_DEBUG") != nullptr);
        if (s_sumDbg)
        {
            static int s_fr = 0;
            if ((s_fr++ % 120) == 0)
                std::cout << "[SatSummary] " << _sat->summaryJson() << std::endl;
        }
        return false;
    }
protected:
    void updateOne(const std::vector<OverlayLayer>& layers, const char* id, SatCategory cat,
                   const std::string& baseSubtitle)
    {
        const OverlayLayer* layer = nullptr;
        for (size_t i = 0; i < layers.size(); ++i)
            if (layers[i].id == id) { layer = &layers[i]; break; }
        if (!layer) return;
        std::string err = _sat->fetchErrorText(cat);
        std::string want = err.empty() ? baseSubtitle
                          : (baseSubtitle.empty() ? err : (baseSubtitle + u8" · " + err));
        if (layer->subtitle != want && _lm->setSubtitle(id, want))
        {
            if (getenv("EARTH_SAT_DEBUG"))
                std::cout << "[SatDBG] " << id << " subtitle -> \"" << want << "\"" << std::endl;
        }
    }
    LayerManager* _lm; SatelliteLayer* _sat;
};

// P3:AIS 船舶层的视口跟随 + 状态 subtitle 刷新(FRAME 驱动,镜像 FlightBBoxHandler +
// SatFetchStatusHandler 两个先例;高空闸门的判定数据 camAltM 也在这里喂给 worker)。
class ShipViewStateHandler : public osgGA::GUIEventHandler
{
public:
    ShipViewStateHandler(LayerManager* lm, ShipLayer* ships) : _lm(lm), _ships(ships) {}
    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
    {
        if (ea.getEventType() != osgGA::GUIEventAdapter::FRAME || !_ships) return false;
        osgViewer::View* view = static_cast<osgViewer::View*>(&aa);
        osg::Vec3d eye = view->getCamera()->getInverseViewMatrix().getTrans();
        osg::Vec3d lla = osgVerse::Coordinate::convertECEFtoLLA(eye);
        double lat0 = osg::RadiansToDegrees(lla[0]), lon0 = osg::RadiansToDegrees(lla[1]);
        double h = lla[2]; const double R = 6371000.0;
        double cosv = R / (R + (h > 0.0 ? h : 0.0));
        double thetaDeg = (cosv < 1.0) ? osg::RadiansToDegrees(acos(cosv)) : 0.0;
        double latMin = lat0 - thetaDeg, latMax = lat0 + thetaDeg;
        double cosLat = cos(osg::DegreesToRadians(lat0)); if (cosLat < 0.2) cosLat = 0.2;
        double lonHalf = thetaDeg / cosLat; if (lonHalf > 180.0) lonHalf = 180.0;
        double lonMin = lon0 - lonHalf, lonMax = lon0 + lonHalf;
        if (latMin < -85.0) latMin = -85.0; if (latMax > 85.0) latMax = 85.0;
        if (lonMin < -180.0) lonMin = -180.0; if (lonMax > 180.0) lonMax = 180.0;
        _ships->setViewState(latMin, lonMin, latMax, lonMax, h);
        // kDisabled 空串不覆盖注册时的说明文案:statusText() 在图层关闭时返回 ""，
        // 若照旧每帧覆盖会把注册时写好的 u8"AISStream 实时船位" 说明文案抹掉。
        if (_lm)
        {
            std::string want = _ships->statusText();
            if (!want.empty())
            {
                const std::vector<OverlayLayer> layers = _lm->layersSnapshot();
                for (size_t i = 0; i < layers.size(); ++i)
                {
                    if (layers[i].id == "ships" && layers[i].subtitle != want)
                    { _lm->setSubtitle("ships", want); break; }
                }
            }
        }
        return false;
    }
protected:
    LayerManager* _lm; ShipLayer* _ships;
};

#if defined(__APPLE__)
// 中文 IME 直打(实现见 ime_bridge.mm):FRAME 事件在主线程,负责
//   1) 懒安装——viewer realize 后 Cocoa 窗口才存在,首个 FRAME 起逐帧尝试,
//      offscreen(HeadlessCGL,无 NSWindow)自动永久禁用,headless 测试零影响;
//   2) 键盘焦点驱动——WantTextInput 时 firstResponder 切到 IME overlay(可组字,
//      且 OSG GLView 收不到 keyDown,与 GlobalKeyboardGate 互为双保险),失焦切回。
class ImeFrameHandler : public osgGA::GUIEventHandler
{
public:
    ImeFrameHandler(osgViewer::Viewer* v) : _viewer(v) {}
    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
    {
        if (ea.getEventType() != osgGA::GUIEventAdapter::FRAME) return false;
        if (ImGui::GetCurrentContext() == NULL) return false;
        if (!earthime::ensureInstalled(_viewer)) return false;
        earthime::updateFocus(ImGui::GetIO().WantTextInput);
        return false;
    }

protected:
    osgViewer::Viewer* _viewer;
};
#endif

// AWS Terrarium 高程瓦片 URL 模板;earthURLs 与启动预热两处共用,保证预热与渲染同源、URL 永不漂移。
static const std::string kTerrariumUrl =
    "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/{z}/{x}/{y}.png";

// —— 香港高程运行时过滤(经 ElevationFilterFunction 注入 osgdb_tms)——
// 两个问题一并修:① Terrarium(SRTM 派生)是含楼高的表面模型,高楼区把楼高掺进地形
// (实测尖沙咀 p90≈37m,真实地面 3-8m);② 引擎统一 ×TileElevationScale(2.0) 夸张 →
// 香港地表处处高于实景三维网格(真实高度),底图贴图"污染"进 3D 城区(盖住网格地面)。
// 处方(全层级同一个连续函数 → 父子 LOD 与邻接由构造保证一致,不会再出"漏空壳"):
//   城区平原核心(九龙+港岛北岸):高度=0(海平面,低于网格地面 3-5m → 被网格盖住);
//   都会区(含太平山/狮子山/港岛南等):高度=原始×0.5-2m(抵消 ×2 夸张≈真实高度、略沉,
//   山形保留,网格贴着山面);区外:原始值。两条边界各带 ~1-2km smoothstep 渐变。
// f2 关闭时城区地面平整、山体回归真实高度,同样比"假土包"更对。EARTH_HK_FLATDEM=0 关闭。
static void hkElevationFilter(float* hts, int w, int h, int x, int y, int z)
{
    static const bool enabled = []() {
        const char* e = getenv("EARTH_HK_FLATDEM");
        return !(e && *e && atoi(e) == 0);
    }();
    if (!enabled || !hts || w <= 0 || h <= 0) return;

    // z>15 时图像内容是 z15 祖先瓦片(见 createCustomPath 同规则),按祖先坐标换算范围
    int tz = z, tx = x, tyTMS = y;
    if (z > 15) { int dz = z - 15; tx = x >> dz; tyTMS = y >> dz; tz = 15; }
    double n = (double)(1 << tz);
    int tyXYZ = (int)n - 1 - tyTMS;
    double lonMin = tx / n * 360.0 - 180.0, lonSpan = 360.0 / n;
    double latN = atan(sinh(osg::PI * (1.0 - 2.0 * tyXYZ / n))) * 180.0 / osg::PI;
    double latS = atan(sinh(osg::PI * (1.0 - 2.0 * (tyXYZ + 1) / n))) * 180.0 / osg::PI;
    // 快速剔除:与都会外包络(含渐变带)不相交的瓦片原样返回(全球其它地区零改动)
    if (lonMin > 114.37 || lonMin + lonSpan < 113.88 || latS > 22.44 || latN < 22.17) return;

    auto smooth01 = [](double t) {
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t); return t * t * (3.0 - 2.0 * t);
    };
    // 到矩形 [la0,la1]x[lo0,lo1] 的外距离经 f 宽渐变:核心=0 → 带外=1
    auto outerW = [&smooth01](double lat, double lon, double la0, double la1,
                              double lo0, double lo1, double f) {
        double dla = std::max(std::max(la0 - lat, lat - la1), 0.0);
        double dlo = std::max(std::max(lo0 - lon, lon - lo1), 0.0);
        return smooth01(sqrt(dla * dla + dlo * dlo) / f);
    };

    for (int row = 0; row < h; ++row)
    {
        // 解码图自底向上(row 0=南);行中心对应的 XYZ 行分数 → 墨卡托纬度
        double yf = (double)tyXYZ + 1.0 - ((double)row + 0.5) / (double)h;
        double lat = atan(sinh(osg::PI * (1.0 - 2.0 * yf / n))) * 180.0 / osg::PI;
        for (int col = 0; col < w; ++col)
        {
            double lon = lonMin + ((double)col + 0.5) / (double)w * lonSpan;
            double wMetro = outerW(lat, lon, 22.19, 22.42, 113.90, 114.35, 0.02);
            if (wMetro >= 1.0) continue;
            double wFlat = outerW(lat, lon, 22.276, 22.335, 114.115, 114.225, 0.012);
            float& hv = hts[row * w + col];
            double hMetro = (double)hv * 0.5 - 2.0;   // 抵消 ×2 夸张,整体略沉
            double hIn = wFlat * hMetro;              // 平原核心=0,向山地平滑过渡
            hv = (float)(wMetro * (double)hv + (1.0 - wMetro) * hIn);
        }
    }
}

// P6a:GIBS 科学图层瓦片模板。time 用 "default"(GIBS 解析为该层最新可用期,长跑不刷新
// 与云图 gibsDate 同一取舍)。LevelN 以上无数据 → 404 → 透明回退(同 VIIRS 极夜先例)。
static const char* kNdviTemplate =
    "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/MODIS_Terra_NDVI_8Day/"
    "default/default/GoogleMapsCompatible_Level9/{z}/{y}/{x}.png";
static const char* kNightTemplate =
    "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_Black_Marble/"
    "default/default/GoogleMapsCompatible_Level8/{z}/{y}/{x}.png";

static std::string createCustomPath(int type, const std::string& prefix, int x, int y, int z)
{
    // 内部瓦片是 TMS（OriginBottomLeft=1，原点左下）。在线服务（ArcGIS / Terrarium）
    // 都是 XYZ（原点左上），所以把 Y 翻转成 XYZ 行号。
    int yXYZ = (int)pow(2.0, (double)z) - 1 - y;
    if (type == osgVerse::TileCallback::ORTHOPHOTO)
    {
        // 卫星底图模板。URL 含 '='/'&',osgDB::Options 解析会把多 '=' 的值截断,
        // 所以在这里硬编码而非放进 earthURLs。EARTH_BASEMAP 可换源(启动时读一次):
        //   未设/google → Google lyrs=s(香港等高密度区掺倾斜航拍,楼体在地面影像里是斜的);
        //   esri        → Esri World Imagery(多数城市更接近真正射,可对比"地面扭曲"观感);
        //   其它        → 当作自定义模板(需含 {x}/{y}/{z} 占位符)。
        static const std::string basemap = []() {
            const char* e = getenv("EARTH_BASEMAP");
            std::string v = e ? e : "";
            if (v.empty() || v == "google")
                return std::string("https://mt1.google.com/vt/lyrs=s&x={x}&y={y}&z={z}");
            if (v == "esri")
                return std::string("https://server.arcgisonline.com/ArcGIS/rest/services/"
                                   "World_Imagery/MapServer/tile/{z}/{y}/{x}");
            return v;
        }();
        return osgVerse::TileCallback::createPath(basemap, x, yXYZ, z);
    }
    else if (type == osgVerse::TileCallback::ELEVATION)
    {
        // (香港城区 DSM 假土包/×2 夸张的修正不在这里做路径分流,而是经 ElevationFilterFunction
        //  钩子在解码后逐像素过滤——见上方 hkElevationFilter 注释。)
        // AWS Terrarium 高程最高约 z15。深瓦片(z>15)取 z15 **祖先瓦片**,createTile 用 elevScaleBias
        // 采子区一步烘焙正确高度 → 与 z15 父级连续、消除 LOD 边界落差/看穿孔洞;配套 EarthManipulator
        // 的相机地形地板(Step A)防穿模。一步烘焙按瓦片坐标确定性算出,无运行时继承的时序竞态。
        if (z > 15)
        {
            int dz = z - 15, ax = x >> dz, ay = y >> dz;
            int ayXYZ = (1 << 15) - 1 - ay;
            return osgVerse::TileCallback::createPath(prefix, ax, ayXYZ, 15);
        }
        return osgVerse::TileCallback::createPath(prefix, x, yXYZ, z);
    }
    else if (type == osgVerse::TileCallback::USER)
    {
        // Google 透明标注层（路网 + 地名），与底图同瓦片方案，叠在 ExtraLayer(unit2)
        static const std::string googleLabels = "https://mt1.google.com/vt/lyrs=h&x={x}&y={y}&z={z}";
        return osgVerse::TileCallback::createPath(googleLabels, x, yXYZ, z);
    }
    else if (type == osgVerse::TileCallback::OVERLAY)
    {
        // OVERLAY 槽按 prefix(= 该层路径,由 TileManager::setLayerPath 设)分流:
        //   "gibs"      → GIBS VIIRS 云图(默认);
        //   以 http 开头 → 当作完整瓦片模板(RainViewer 降水雷达);
        //   其它(空)    → 不加载(返回空)。
        if (prefix.rfind("http", 0) == 0)   // RainViewer 模板(已含 {z}/{x}/{y})
        {
            if (z > 10) return "";          // 雷达分辨率粗,z>10 不取
            return osgVerse::TileCallback::createPath(prefix, x, yXYZ, z);
        }
        if (prefix == "gebco")
        {
            // GEBCO 15″ 网格:z8 以上无信息增益;WMS GetMap 按瓦片 bbox 合成(EPSG:3857
            // 轴序 x,y)。URL 含 '='/'&' 与 Google 底图同先例(createCustomPath 直出不过
            // osgDB::Options,不受多 '=' 截断影响)。
            //
            // == 反经线白带根因 + 双重修复(2026-07-07)==
            // 现象:高空正对反经线时一条贯穿南北极的白色纺锤带 + 周边马赛克。
            // 根因:GEBCO mapserver 在 bbox 边缘坐标【恰好等于】mercator ±180 边界值
            //   (±20037508.342789244)时触发一个边界处理 quirk,把该侧约 25/256(≈10%)宽的
            //   像素列返回为 nodata。x=0 瓦片西缘 / x=末列瓦片东缘都恰贴反经线,又无相邻瓦片
            //   遮盖;WMS 默认 BGCOLOR 不透明白 → 这些 nodata 列渲成不透明白 (255,255,255,255)
            //   → overlay 按 alpha=1 混成纯白带。低 z 瓦片跨经度极大,10% 固定比例被放大成宽带
            //   (z1 一块 180° → ~18° 白带)。实测(curl):西缘 bbox=-kM → 左侧 26px 全透明/白;
            //   西缘 +1m → 0px,数据完好;各 z 级 nodata 恒为 ~25px(证明是"贴边界"而非真实数据缺失)。
            // 修复①(消除空隙,主):把恰贴 ±kM 的外缘向内偏移 1m —— 20037508m 里的 1m = 5e-8,
            //   亚微像素、数据无感,却让 mapserver 正常出图,GEBCO 数据一直覆盖到反经线,连空隙都没有。
            // 修复②(兜底,防白):加 TRANSPARENT=TRUE 让 WMS 输出 RGBA,任何残余 nodata/背景
            //   (如极区瓦片)变透明 (0,0,0,0) 露底图,而非不透明白 —— 即便①未覆盖到也绝不再现白带。
            // 附带:URL 变化(TRANSPARENT/微移)亦让缓存 key 变化,绕开旧版被写入的不透明白瓦片磁盘缓存。
            //
            // 末尾 &t=.png 是喂给 osgDB::getFileExtension 的:WMS query URL 本以 bbox 坐标
            // 结尾(如 ...,20037508.343),getFileExtension 会把 ".343" 误当扩展名 → FileCache
            // 按 ext 找不到 reader(getReaderWriterForExtension("343")=NULL)→ 无法缓存 →
            // 每次全量实时重下 + "Failed to find reader/writer" 刷屏(GIBS/.jpg 能缓存故同视角
            // 只剩极细 globe 接缝)。加 &t=.png 让扩展名解析为 png,FileCache 才按 png 缓存;WMS
            // 忽略未知参数 t(curl 实测加/不加返回同一 PNG)。
            if (z > 8) return "";
            osg::Vec4d bb = earthgeo::mercatorTileBBox(x, yXYZ, z);
            const double kM = 20037508.342789244;  // mercator 半宽(= mercatorTileBBox 内常数)
            if (bb[0] <= -kM + 1.0) bb[0] += 1.0;   // 西缘贴反经线 → 向东微移 1m
            if (bb[2] >=  kM - 1.0) bb[2] -= 1.0;   // 东缘贴反经线 → 向西微移 1m
            char buf[512];
            snprintf(buf, sizeof(buf),
                "https://wms.gebco.net/mapserv?request=getmap&service=wms&version=1.3.0"
                "&layers=GEBCO_LATEST&format=image/png&crs=EPSG:3857&width=256&height=256"
                "&TRANSPARENT=TRUE&bbox=%.3f,%.3f,%.3f,%.3f&t=.png", bb[0], bb[1], bb[2], bb[3]);
            return std::string(buf);
        }
        if (prefix != "gibs") return "";    // 空/未知 → 不加载叠加瓦片

        if (z > 9) return "";  // GIBS GoogleMapsCompatible_Level9 max zoom is 9; no data above
        // 用 VIIRS_SNPP 真彩而非 MODIS_Terra:MODIS 单星刈幅赤道留黑色刈幅空隙;VIIRS 无缝。
        // 取「今天-2 天」(UTC) 完整日期,只算一次(长跑不刷新;跨 UTC 午夜需重启)。
        // 南极极夜瓦片 VIIRS 返回 404 → 加载失败回退默认透明贴图(露底图)而非黑斑,无需特殊处理。
        static const std::string gibsDate = []() {
            time_t t = time(NULL) - 2 * 86400; struct tm g; gmtime_r(&t, &g);
            char buf[16]; strftime(buf, sizeof(buf), "%Y-%m-%d", &g); return std::string(buf);
        }();
        std::string gibs =
            "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/"
            "VIIRS_SNPP_CorrectedReflectance_TrueColor/default/" + gibsDate +
            "/GoogleMapsCompatible_Level9/{z}/{y}/{x}.jpg";
        return osgVerse::TileCallback::createPath(gibs, x, yXYZ, z);
    }
    // OCEAN_MASK：仍用本地 mbtiles（TMS，不翻转），深层级丢弃
    if (z > 3) return "";
    return osgVerse::TileCallback::createPath(prefix, x, y, z);
}

// 启动磁盘预热:把 z0..maxZ 全球低 LOD 瓦片的 底图+标注+高程 三层预拉进磁盘缓存,用户首次
// 平移/缩放到新区时粗瓦片立即出图。仅磁盘预热;4 worker 共享原子游标分摊;loadFileData 命中
// 缓存即秒回 → 天然去重、重跑零浪费。瓦片数 = Σ 4^z = (4^(maxZ+1)-1)/3。
static void prefetchLowLODGlobe(int maxZ)
{
    struct PT { int z, x, y; };
    std::vector<PT> tiles;
    for (int z = 0; z <= maxZ; ++z)
    {
        int n = 1 << z;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            { PT t; t.z = z; t.x = x; t.y = y; tiles.push_back(t); }
    }

    std::atomic<size_t> cursor(0);
    std::atomic<int> warmed(0);
    const int kWorkers = 4;
    std::vector<std::thread> pool;
    for (int w = 0; w < kWorkers; ++w)
    {
        pool.push_back(std::thread([&]() {
            for (;;)
            {
                size_t i = cursor.fetch_add(1);
                if (i >= tiles.size()) break;
                const PT& t = tiles[i];
                const int types[3] = { osgVerse::TileCallback::ORTHOPHOTO,
                                       osgVerse::TileCallback::USER,
                                       osgVerse::TileCallback::ELEVATION };
                for (int k = 0; k < 3; ++k)
                {
                    std::string prefix =
                        (types[k] == osgVerse::TileCallback::ELEVATION) ? kTerrariumUrl : std::string();
                    std::string path = createCustomPath(types[k], prefix, t.x, t.y, t.z);
                    if (!path.empty())
                    { std::string mime, enc; osgVerse::loadFileData(path, mime, enc); }
                }
                warmed.fetch_add(1);
            }
        }));
    }
    for (size_t i = 0; i < pool.size(); ++i) pool[i].join();
    OSG_NOTICE << "[prefetch] Warmed " << warmed.load() << " low-LOD globe tiles (z0-"
               << maxZ << ", base+labels+elevation)\n";
}

int main(int argc, char** argv)
{
    const std::string settingsPath = osgVerse::defaultImGuiSettingsPath();
    const std::string userDataPath = osgDB::getFilePath(settingsPath);
    const std::string runtimeLogBase = userDataPath.empty() ? std::string()
        : userDataPath + "/EarthExplorer/libhv";
    if (!runtimeLogBase.empty() && osgDB::makeDirectoryForFile(runtimeLogBase))
        hlog_set_file(runtimeLogBase.c_str());
    else
        hlog_disable();

    osgViewer::Viewer viewer;
    osg::ArgumentParser arguments = osgVerse::globalInitialize(argc, argv, osgVerse::defaultInitParameters());
    osg::setNotifyHandler(new osgVerse::ConsoleHandler(false));
    osgVerse::updateOsgBinaryWrappers();
    osgDB::Registry::instance()->addFileExtensionAlias("tif", "verse_tiff");
    // 固定 jpg/png 图像解码/编码插件:主线程启动期先行加载 osgdb_verse_image(stb,输出 GL_RGB/RGBA),
    // 使其位于 Registry 插件表最前,进程内所有 jpg/png/mime 查询(瓦片解码、抓帧写 PNG)恒定命中它。
    // 背景:不预载时,"谁先触发首次 jpg/png 查询"决定 stb 还是 macOS osgdb_imageio(输出 GL_BGRA)
    // 接管——多线程分页下这是进程级竞态,同一命令两次运行可得不同解码器(EARTH_IMG_DEBUG=1 可观测)。
    // 曾出现瓦片级 R/B 对调花屏(海洋棕、撒哈拉蓝),内容取证证实为 BGRA/RGB 字节序按瓦片错配;
    // 两解码器各自单独使用均正常,故按证据消除唯一已证实的不确定性源,同时使离屏截图编码器恒定。
    if (osgDB::Registry::instance()->getReaderWriterForExtension("verse_image") == NULL)
    {
        // 插件缺失(打包破损)时静默回退到竞态旧行为——必须留痕,否则花屏复发无从下手。
        OSG_WARN << "[Earth] osgdb_verse_image plugin missing; jpg/png decoder "
                 << "nondeterminism is back (check packaging)" << std::endl;
    }

    std::string mainFolder = BASE_DIR + "/models"; arguments.read("--folder", mainFolder);
    std::string skirtRatio = "0.05"; arguments.read("--skirt", skirtRatio);
    int w = 1920, h = 1080; arguments.read("--resolution", w, h);
    bool cityWaitingTiles = true, manipulatorCanThrow = false;
    if (arguments.read("--no-wait")) cityWaitingTiles = false;
    if (arguments.read("--thrown")) manipulatorCanThrow = true;
    // 启动即跳转到指定经纬度+高度（度, 度, 千米），便于直接查看街道级瓦片
    double gotoLat = 1.0e9, gotoLon = 0.0, gotoAltKm = 0.0;
    arguments.read("--goto", gotoLat, gotoLon, gotoAltKm);

    // Create earth
    std::string earthURLs =
        // Orthophoto 值仅为占位符（非空即启用影像层）；真实 Google 混合瓦片 URL 在
        // createCustomPath 里拼装——Google URL 含多个 '='，放这里会被 Options 解析截断。
        " Orthophoto=google"          // non-empty placeholder; real lyrs=s URL built in createCustomPath
        " User=googleLabels"          // non-empty placeholder; real lyrs=h URL built in createCustomPath
        " Overlay=gibs"               // non-empty placeholder; real GIBS URL built in createCustomPath
        " Elevation=" + kTerrariumUrl +
        " OceanMask=mbtiles://" + mainFolder + "/Earth/Mask_lv3.mbtiles/{z}-{x}-{y}.tif"
        " ElevationEncoding=terrarium MaximumLevel=19 UseWebMercator=1 UseEarth3D=1 OriginBottomLeft=1"
        " TileElevationScale=2.0 TileSkirtRatio=" + skirtRatio;
    osg::ref_ptr<osgDB::Options> earthOptions = new osgDB::Options(earthURLs);
    earthOptions->setPluginData("UrlPathFunction", (void*)createCustomPath);
    earthOptions->setPluginData("ElevationFilterFunction", (void*)hkElevationFilter);

    osg::ref_ptr<osg::Node> earth = osgDB::readNodeFile("0-0-0.verse_tms", earthOptions.get());
    if (!earth) { OSG_FATAL << "Main earth scene is missing!\n"; return 1; }

    // 启动磁盘预热(后台 detach 线程,进程退出即回收)。EARTH_PREFETCH=最大 zoom(默认 4,0=关)。
    {
        const char* pfEnv = getenv("EARTH_PREFETCH");
        int prefetchZ = pfEnv ? atoi(pfEnv) : 4;
        if (prefetchZ > 0) std::thread(prefetchLowLODGlobe, prefetchZ).detach();
    }

    // 全局键盘闸:必须是 viewer 上**第一个** addEventHandler(事件遍历按安装顺序),
    // 保证聊天框打字时 handled 标记先于一切键位处理器置位(原理见类注释)。
    viewer.addEventHandler(new GlobalKeyboardGate);
    viewer.addEventHandler(new CloseWindowQuitHandler);   // 关窗=退出(见类注释)
    // Esc 不再整个退出程序:默认 _keyEventSetsDone=Escape 在 eventTraversal 里先于
    // 所有 handler 判定,闸拦不住——聊天框里按 Esc(ImGui 语义=撤销输入并失焦)会直接
    // 杀掉 app。退出改走窗口关闭按钮 / Cmd+Q,Esc 专职"输入框失焦/取消"。
    viewer.setKeyEventSetsDone(0);

    // Configure scene components
    osgVerse::EarthAtmosphereOcean earthRenderingUtils;
    osg::ref_ptr<osg::MatrixTransform> earthRoot = new osg::MatrixTransform;
    std::vector<osg::Camera*> cameras = configureEarthRendering(
        viewer, earthRoot.get(), earth.get(), earthRenderingUtils, mainFolder, EARTH_INTERSECTION_MASK, w, h);
    earthRoot->getOrCreateStateSet()->setMode(GL_LIGHTING, osg::StateAttribute::OFF);

    osg::Camera* sceneCamera = cameras[0];
    //osg::Camera* atmosphereCamera = cameras[1];
    //osg::Camera* oceanCamera = cameras[2];
    sceneCamera->setLODScale(0.8f);
    sceneCamera->setNearFarRatio(0.00001);
    sceneCamera->setSmallFeatureCullingPixelSize(10.0f);
    sceneCamera->addChild(configureCityData(
        viewer, earthRoot.get(), earthRenderingUtils, mainFolder, EARTH_INTERSECTION_MASK, cityWaitingTiles));

    // 地震层(T2 起)已迁移到 FeedLayer 框架,注册点挪到下面 registerGdacsFeed 旁边
    // (需要 aiRuntime.tools,晚于 configureAIChat)——见 registerUsgsQuakesFeed 调用处。

    FlightLayer* flightLayer = nullptr;
    sceneCamera->addChild(configureFlightLayer(viewer, earthRoot.get(), mainFolder, &flightLayer));
    SatelliteLayer* satelliteLayer = nullptr;
    sceneCamera->addChild(configureSatelliteLayer(viewer, earthRoot.get(), mainFolder, &satelliteLayer));
    ShipLayer* shipLayer = nullptr;
    sceneCamera->addChild(configureShipLayer(viewer, &shipLayer));   // P3:AIS 船舶(WS 流)

    Tiles3DLayer* tiles3dLayer = nullptr;
    sceneCamera->addChild(configure3DTilesLayer(viewer, earthRoot.get(), mainFolder, &tiles3dLayer));

    osg::ref_ptr<PrecipController> precip = configurePrecipLayer(viewer);

    osg::ref_ptr<osg::Group> root = new osg::Group;
    root->addChild(earthRoot.get());
#if !SIMPLE_VERSION
    root->addChild(configureUI(viewer, earthRoot.get(), mainFolder, w, h));
#endif

    // Configure the manipulator
    osg::ref_ptr<osgVerse::EarthManipulator> earthManipulator = new osgVerse::EarthManipulator;
    earthManipulator->setIntersectionMask(EARTH_INTERSECTION_MASK);
    earthManipulator->setWorldNode(earth.get());
    earthManipulator->setThrowAllowed(manipulatorCanThrow);

    //osg::Vec3d pos = osgVerse::Coordinate::convertLLAtoECEF(
    //    osg::Vec3d(osg::inDegrees(0.0), osg::inDegrees(120.0), 10000.0));
    //earthManipulator->moveTo(pos, 0.0, 120.0);

    // Start the viewer
    osg::ref_ptr<osgVerse::FileCache> fileCache = new osgVerse::FileCache("earth_cache");
    osgDB::Registry::instance()->setFileCache(fileCache.get());

    //osg::ref_ptr<osgVerse::EarthProjectionMatrixCallback> epmcb =
    //    new osgVerse::EarthProjectionMatrixCallback(sceneCamera, earth->getBound().center());
    //epmcb->setNearFirstModeThreshold(2000.0);
    //sceneCamera->setClampProjectionMatrixCallback(epmcb.get());

    osgDB::DatabasePager* pager = new osgDB::DatabasePager;
    pager->setDrawablePolicy(osgDB::DatabasePager::USE_VERTEX_BUFFER_OBJECTS);
    // Online satellite/elevation tiles are fetched synchronously over HTTPS on the
    // pager's HTTP threads. Each deeper LOD level needs ~4x as many tiles, so tile
    // streaming throughput is the bottleneck for drilling down to street level.
    // Give the pager many more HTTP threads so tiles load in parallel (combined with
    // keep-alive connection reuse in loadFileData) instead of trickling in one host
    // round-trip at a time.
    pager->setDoPreCompile(true); pager->setUpThreads(20, 16);
    // 默认 targetMaximumNumberOfPageLOD=300，低于整个地球 z1-4(=340 块)就开始 LRU 过期卸载：
    // 深 zoom 进某地后，其它区域的中低 LOD 瓦片被卸载，缩放外推回全球时它们正在重载 → 露出默认
    // 底图("半个地球橙色")。提高上限让覆盖全球的低/中 LOD 瓦片常驻，外推秒回、不再重载露馅。
    pager->setTargetMaximumNumberOfPageLOD(1500);

    // 调试快捷键处理器('s'=统计/'f'=全屏/'w'=线框/'o','i','p',',','.'=环境):
    // 打字防穿透由链首的 GlobalKeyboardGate 统一负责(旧 ImGuiAwareKeyFilter 逐个
    // 包装已删)——osgViewer 三件套自带 ea.getHandled() 检查,EnvironmentHandler
    // 在键位分支里自查;非打字状态行为与从前完全一致。
    viewer.addEventHandler(new EnvironmentHandler(&earthRenderingUtils, mainFolder));
    viewer.addEventHandler(new osgViewer::StatsHandler);
    viewer.addEventHandler(new osgViewer::WindowSizeHandler);
    viewer.addEventHandler(new osgGA::StateSetManipulator(viewer.getCamera()->getOrCreateStateSet()));
    viewer.setRealizeOperation(new osgVerse::RealizeOperation);
    viewer.setCameraManipulator(earthManipulator.get());
    viewer.setDatabasePager(pager);
    viewer.setSceneData(root.get());
    //viewer.setThreadingModel(osgViewer::Viewer::SingleThreaded);

    // TEMPORARY DIAGNOSTIC (investigation only) — see PagerDebugHandler def above.
    if (getenv("EARTH_PAGER_DEBUG")) viewer.addEventHandler(new PagerDebugHandler(pager));

    // 图层注册（P0：底图 + 标注）
    LayerManager layerMgr;
    viewer.addEventHandler(new LayerManagerDrainHandler(&layerMgr));
    {
        OverlayLayer base; base.id = "base"; base.displayName = u8"卫星影像";
        base.group = u8"底图 / 标注"; base.enabled = true; base.hasOpacity = false;
        layerMgr.add(base);   // 基础底图，常开、无开关动作

        OverlayLayer labels; labels.id = "labels"; labels.displayName = u8"路网·地名";
        labels.group = u8"底图 / 标注"; labels.enabled = true; labels.hasOpacity = true; labels.opacity = 1.0f;
        osgVerse::EarthAtmosphereOcean* eptr = &earthRenderingUtils;
        labels.apply = [eptr](const OverlayLayer& l) {
            float v = l.enabled ? l.opacity : 0.0f;
            if (eptr->commonUniforms.count("LabelOpacity"))
                eptr->commonUniforms["LabelOpacity"]->set(v);
        };
        layerMgr.add(labels);

        LayerManager* lmptr = &layerMgr;
        PrecipController* pcptr = precip.get();
        // OVERLAY 瓦片槽互斥组:物理上只有一个 OVERLAY 槽,组内任意一层开启须关闭其余。
        // P6a 从 clouds/precip 两两手写互斥泛化为组表;gebco 由 Task 8 启用。
        static const std::vector<std::string> kOverlaySlotIds =
            { "clouds", "precip", "ndvi", "nightlights", "gebco" };
        auto disableOtherOverlays = [lmptr, pcptr](const std::string& selfId) {
            lmptr->setExclusiveGroupEnabled(kOverlaySlotIds, selfId);
            if (selfId != "precip" && pcptr) pcptr->setEnabled(false);
        };
        // OVERLAY 槽可见度:组内任意一层开启,取该层透明度;都关→0。
        auto applyOverlayOpacity = [eptr, lmptr]() {   // eptr/lmptr 为裸指针,浅复制进 lambda(对象会话期存活)
            float op = lmptr->firstEnabledOpacity(kOverlaySlotIds);
            if (eptr->commonUniforms.count("Overlay2Opacity"))
                eptr->commonUniforms["Overlay2Opacity"]->set(op);
        };

        OverlayLayer clouds; clouds.id = "clouds"; clouds.displayName = u8"GIBS 影像/云图";
        clouds.group = u8"影像 / 天气"; clouds.enabled = false; clouds.hasOpacity = true; clouds.opacity = 0.7f;
        clouds.maxDetailNote = u8"~250 m";
        clouds.apply = [lmptr, disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)   // 互斥:开云图 → 关组内其余层 + OVERLAY 切回 gibs
            {
                disableOtherOverlays("clouds");
                osgVerse::TileManager::instance()->setLayerPath(osgVerse::TileCallback::OVERLAY, "gibs");
            }
            applyOverlayOpacity();
        };
        layerMgr.add(clouds);

        OverlayLayer precipL; precipL.id = "precip"; precipL.displayName = u8"降水雷达 RainViewer";
        precipL.group = u8"影像 / 天气"; precipL.enabled = false; precipL.hasOpacity = true; precipL.opacity = 0.85f;
        precipL.maxDetailNote = u8"~1 km";
        precipL.apply = [lmptr, pcptr, disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)   // 互斥:开降水 → 关组内其余层;先清 OVERLAY,待控制器拼好 RV 模板再上
            {
                disableOtherOverlays("precip");
                osgVerse::TileManager::instance()->setLayerPath(osgVerse::TileCallback::OVERLAY, "");
                if (pcptr) pcptr->setEnabled(true);   // 控制器抓帧后由主线程 FRAME handler setLayerPath(OVERLAY, RV模板)
            }
            else   // 关降水 → OVERLAY 回 gibs(云图开则可见,否则 opacity 0)
            {
                if (pcptr) pcptr->setEnabled(false);
                osgVerse::TileManager::instance()->setLayerPath(osgVerse::TileCallback::OVERLAY, "gibs");
            }
            applyOverlayOpacity();
        };
        layerMgr.add(precipL);

        // P6a:科学数据栅格层(GIBS,复用 OVERLAY http 模板分支,零新管线)
        OverlayLayer ndvi; ndvi.id = "ndvi"; ndvi.displayName = u8"植被指数 NDVI";
        ndvi.group = u8"科学数据 / Science"; ndvi.enabled = false;
        ndvi.hasOpacity = true; ndvi.opacity = 0.8f;
        ndvi.subtitle = u8"NASA GIBS · MODIS 8日合成";
        ndvi.maxDetailNote = u8"~250 m";
        ndvi.opaque = true;   // 不透明科学层,看不穿地形(Task 4)
        ndvi.apply = [disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)
            {
                disableOtherOverlays("ndvi");
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, kNdviTemplate);
            }
            else
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, "gibs");
            applyOverlayOpacity();
        };
        layerMgr.add(ndvi);

        OverlayLayer night; night.id = "nightlights"; night.displayName = u8"夜间灯光";
        night.group = u8"科学数据 / Science"; night.enabled = false;
        night.hasOpacity = true; night.opacity = 0.9f;
        night.subtitle = u8"NASA GIBS · VIIRS Black Marble";
        night.maxDetailNote = u8"~500 m";
        night.opaque = true;   // 不透明科学层,看不穿地形(Task 4)
        night.apply = [disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)
            {
                disableOtherOverlays("nightlights");
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, kNightTemplate);
            }
            else
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, "gibs");
            applyOverlayOpacity();
        };
        layerMgr.add(night);

        // P6a T8:GEBCO 海底地形(WMS GetMap 按瓦片 bbox 合成,唯一非"完整模板"OVERLAY
        // 分支——用 prefix=="gebco" 走 createCustomPath 里的专属分流,见上方注释)。
        OverlayLayer gebco; gebco.id = "gebco"; gebco.displayName = u8"海底地形 GEBCO";
        gebco.group = u8"科学数据 / Science"; gebco.enabled = false;
        gebco.hasOpacity = true; gebco.opacity = 0.85f;
        gebco.subtitle = u8"© GEBCO Compilation Group 2025";
        gebco.maxDetailNote = u8"~600 m";
        gebco.opaque = true;   // 不透明科学层,看不穿地形(Task 4)
        gebco.apply = [disableOtherOverlays, applyOverlayOpacity](const OverlayLayer& l) {
            if (l.enabled)
            {
                disableOtherOverlays("gebco");
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, "gebco");
            }
            else
                osgVerse::TileManager::instance()->setLayerPath(
                    osgVerse::TileCallback::OVERLAY, "gibs");
            applyOverlayOpacity();
        };
        layerMgr.add(gebco);

        // 地震层("quakes")不再在这里手动 add——registerUsgsQuakesFeed(见下方 registerGdacsFeed
        // 旁边的调用)内部通过 registerFeedLayer 自动完成 LayerManager 注册 + EARTH_QUAKES*
        // 钩子,语义(图层 id/显示名/分组/env 钩子)与迁移前完全一致。

        OverlayLayer flights; flights.id = "flights"; flights.displayName = u8"航班 (OpenSky)";
        flights.group = u8"实时数据 / Live"; flights.enabled = false; flights.hasOpacity = false;
        { earthmark::MarkerVisual mv = earthmark::visualForLayer("flights");
          flights.shape = mv.shape; flights.iconColor = mv.color; }
        FlightLayer* fptr = flightLayer;
        flights.apply = [fptr](const OverlayLayer& l) { if (fptr) fptr->setEnabled(l.enabled); };
        layerMgr.add(flights);

        // 香港实景三维(地政总署 3D Visualisation Map,Cesium 3D Tiles 流式)。
        // 懒加载:首次勾选才联网;官方使用条款要求署名 → subtitle 常显数据来源。
        OverlayLayer hk3d; hk3d.id = "hk3d"; hk3d.displayName = u8"香港实景三维 (LandsD)";
        hk3d.group = u8"三维城市 / 3D City"; hk3d.enabled = false; hk3d.hasOpacity = false;
        hk3d.subtitle = u8"© 香港特区政府地政总署 data.map.gov.hk";
        Tiles3DLayer* tptr = tiles3dLayer;
        hk3d.apply = [tptr](const OverlayLayer& l) { if (tptr) tptr->setEnabled(l.enabled); };
        layerMgr.add(hk3d);

        OverlayLayer satStations; satStations.id = "satstations"; satStations.displayName = u8"空间站 (CelesTrak)";
        satStations.group = u8"卫星 Satellites"; satStations.enabled = false; satStations.hasOpacity = false;
        { earthmark::MarkerVisual mv = earthmark::visualForLayer("satstations");
          satStations.shape = mv.shape; satStations.iconColor = mv.color; }
        SatelliteLayer* satptr = satelliteLayer;
        satStations.apply = [satptr](const OverlayLayer& l) { if (satptr) satptr->setCategoryEnabled(SatCategory::Station, l.enabled); };
        layerMgr.add(satStations);

        OverlayLayer satNav; satNav.id = "satnav"; satNav.displayName = u8"导航星座 (GPS/北斗/Galileo/GLONASS)";
        satNav.group = u8"卫星 Satellites"; satNav.enabled = false; satNav.hasOpacity = false;
        { earthmark::MarkerVisual mv = earthmark::visualForLayer("satnav");
          satNav.shape = mv.shape; satNav.iconColor = mv.color; }
        satNav.apply = [satptr](const OverlayLayer& l) { if (satptr) satptr->setCategoryEnabled(SatCategory::Navigation, l.enabled); };
        layerMgr.add(satNav);

        OverlayLayer satWx; satWx.id = "satwx"; satWx.displayName = u8"气象卫星 (CelesTrak)";
        satWx.group = u8"卫星 Satellites"; satWx.enabled = false; satWx.hasOpacity = false;
        { earthmark::MarkerVisual mv = earthmark::visualForLayer("satwx");
          satWx.shape = mv.shape; satWx.iconColor = mv.color; }
        satWx.apply = [satptr](const OverlayLayer& l) { if (satptr) satptr->setCategoryEnabled(SatCategory::Weather, l.enabled); };
        layerMgr.add(satWx);

        OverlayLayer starlink; starlink.id = "starlink"; starlink.displayName = u8"Starlink 星链 (全量点云)";
        starlink.group = u8"卫星 Satellites"; starlink.enabled = false; starlink.hasOpacity = false;
        { earthmark::MarkerVisual mv = earthmark::visualForLayer("starlink");
          starlink.shape = mv.shape; starlink.iconColor = mv.color; }
        starlink.subtitle = u8"约 7000 颗,纯视觉壳层,不可点选";
        starlink.apply = [satptr](const OverlayLayer& l) { if (satptr) satptr->setCategoryEnabled(SatCategory::Starlink, l.enabled); };
        layerMgr.add(starlink);
        viewer.addEventHandler(new SatFetchStatusHandler(&layerMgr, satptr));

        OverlayLayer ships; ships.id = "ships"; ships.displayName = u8"船舶 (AIS)";
        ships.group = u8"实时数据 / Live"; ships.subtitle = u8"AISStream 实时船位";
        ships.hasOpacity = false;
        { earthmark::MarkerVisual mv = earthmark::visualForLayer("ships");
          ships.shape = mv.shape; ships.iconColor = mv.color; }
        ShipLayer* shipsPtr = shipLayer;
        ships.apply = [shipsPtr](const OverlayLayer& l) { if (shipsPtr) shipsPtr->setEnabled(l.enabled); };
        OverlayLayer& shipsAdded = layerMgr.add(ships);
        const char* shipsEnv = getenv("EARTH_SHIPS");
        if (shipsEnv && *shipsEnv) shipsAdded.enabled = (std::string(shipsEnv) != "0");
        layerMgr.setEnabled("ships", shipsAdded.enabled);
        viewer.addEventHandler(new ShipViewStateHandler(&layerMgr, shipLayer));
    }
    // 同步初始状态到 uniform（apply 只在交互时触发，这里推一次初值）
    if (OverlayLayer* lbl = layerMgr.find("labels"))
        layerMgr.setEnabled("labels", lbl->enabled);
    if (OverlayLayer* cl = layerMgr.find("clouds"))
    {
        // 默认关 → Overlay2Opacity 保持 0。EARTH_CLOUDS=<不透明度> 可在 headless/脚本里强制
        // 开启 GIBS 影像/云图层(取值即不透明度，0=关)，供无界面验证用（同 EARTH_TILT 测试钩子）。
        const char* cloudsEnv = getenv("EARTH_CLOUDS");
        if (cloudsEnv && *cloudsEnv)
        {
            float op = (float)atof(cloudsEnv);
            cl->enabled = (op > 0.0f);
            cl->opacity = (op < 0.0f) ? 0.0f : (op > 1.0f ? 1.0f : op);
        }
        layerMgr.setEnabled("clouds", cl->enabled);  // default off → Overlay2Opacity stays 0
    }
    // 地震层("quakes")的 EARTH_QUAKES 强制开关钩子已内置于 registerFeedLayer(见
    // registerUsgsQuakesFeed 调用处),不再需要这里手动读 env 并 setEnabled。
    if (OverlayLayer* pp = layerMgr.find("precip"))
    {
        // EARTH_PRECIP=1 强制开启降水层(headless 验证用;与 EARTH_CLOUDS 互斥,后开者生效)。
        const char* pEnv = getenv("EARTH_PRECIP");
        if (pEnv && *pEnv) pp->enabled = (atoi(pEnv) != 0);
        layerMgr.setEnabled("precip", pp->enabled);
    }
    // EARTH_NDVI / EARTH_NIGHTLIGHTS=<不透明度>:headless 强制开启科学层(同 EARTH_CLOUDS 语义)。
    if (OverlayLayer* nv = layerMgr.find("ndvi"))
    {
        const char* e = getenv("EARTH_NDVI");
        if (e && *e)
        {
            float op = (float)atof(e);
            nv->enabled = (op > 0.0f);
            nv->opacity = (op < 0.0f) ? 0.0f : (op > 1.0f ? 1.0f : op);
            layerMgr.setEnabled("ndvi", nv->enabled);
        }
    }
    if (OverlayLayer* nl = layerMgr.find("nightlights"))
    {
        const char* e = getenv("EARTH_NIGHTLIGHTS");
        if (e && *e)
        {
            float op = (float)atof(e);
            nl->enabled = (op > 0.0f);
            nl->opacity = (op < 0.0f) ? 0.0f : (op > 1.0f ? 1.0f : op);
            layerMgr.setEnabled("nightlights", nl->enabled);
        }
    }
    // EARTH_GEBCO=<不透明度>:headless 强制开启海底地形层(同 EARTH_NIGHTLIGHTS 语义)。
    if (OverlayLayer* gb = layerMgr.find("gebco"))
    {
        const char* e = getenv("EARTH_GEBCO");
        if (e && *e)
        {
            float op = (float)atof(e);
            gb->enabled = (op > 0.0f);
            gb->opacity = (op < 0.0f) ? 0.0f : (op > 1.0f ? 1.0f : op);
            layerMgr.setEnabled("gebco", gb->enabled);
        }
    }
    if (OverlayLayer* fl = layerMgr.find("flights"))
    {
        // EARTH_FLIGHTS=<非0> 强制开启航班层(headless 验证用)。
        const char* flEnv = getenv("EARTH_FLIGHTS");
        if (flEnv && *flEnv) fl->enabled = (atoi(flEnv) != 0);
        layerMgr.setEnabled("flights", fl->enabled);
    }
    if (OverlayLayer* ss = layerMgr.find("satstations"))
    {
        const char* e = getenv("EARTH_SATS");
        if (e && *e) { ss->enabled = (atoi(e) != 0); layerMgr.setEnabled("satstations", ss->enabled); }
    }
    if (OverlayLayer* sl = layerMgr.find("starlink"))
    {
        const char* e = getenv("EARTH_STARLINK");
        if (e && *e) { sl->enabled = (atoi(e) != 0); layerMgr.setEnabled("starlink", sl->enabled); }
    }
    if (OverlayLayer* t3 = layerMgr.find("hk3d"))
    {
        // EARTH_3DTILES 已设置(=1 默认源 / =<url> 换源,见 tiles3d_data.cpp)→ 随启动开启;
        // 值为 0 视作显式关闭。未设置 → 默认关,由 UI 勾选触发懒加载。
        const char* tEnv = getenv("EARTH_3DTILES");
        if (tEnv && *tEnv) t3->enabled = (std::string(tEnv) != "0");
        layerMgr.setEnabled("hk3d", t3->enabled);
    }

    // AIChatUI 先于 configureAIChat 创建：show_chart 工具的 execute 需要拿到它的指针
    // 才能把图表 spec 推进右上角卡片队列（同一个实例后面又挂到 ctrlUI->_aiUI 供 draw() 用）。
    AIChatUI* aiUI = new AIChatUI;
    AIChatDeps aiDeps;
    aiDeps.viewer = &viewer; aiDeps.mani = earthManipulator.get(); aiDeps.layers = &layerMgr;
    aiDeps.flights = flightLayer; aiDeps.ui = aiUI;
    AIChatRuntime aiRuntime = configureAIChat(aiDeps);
    earthai::AIChatCore* aiCore = aiRuntime.core;
    earthai::MediaManager* aiMedia = aiRuntime.media;
    if (aiMedia) aiMedia->setEarthUniforms(&earthRenderingUtils);   // 快门补光需要 WorldSunDir
    if (aiMedia) aiMedia->setContentSize(w, h);   // 快照按渲染内容区裁剪(去掉整窗多余底色边条)

    if (aiRuntime.tools && shipLayer)
    {
        earthai::Tool t; t.name = "get_ships_summary";
        t.description = u8"查询当前视野内 AIS 船舶汇总:总数、按航速分桶(锚泊/慢速/巡航/高速)、"
            u8"最快船。数据来自 AISStream 实时流,覆盖范围为当前订阅视口;"
            u8"未配置 EARTH_AISSTREAM_KEY 或视野过大时 count 为 0。";
        t.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        ShipLayer* sp = shipLayer;
        t.execute = [sp](const picojson::value&) {
            picojson::value v; std::string err = picojson::parse(v, sp->summaryJson());
            if (!err.empty() || !v.is<picojson::object>())
            { picojson::object e; e["error"] = picojson::value("bad summary json"); return picojson::value(e); }
            return v;
        };
        aiRuntime.tools->add(t);
    }

    if (aiRuntime.tools && satelliteLayer)
    {
        earthai::Tool t; t.name = "get_satellites_summary";
        t.description = u8"查询卫星汇总:总数、按类别(空间站/导航星座/气象/Starlink)分布,"
            u8"以及 ISS(国际空间站,NORAD 25544)和天宫空间站(NORAD 48274)的当前经纬度/高度/速度。"
            u8"若 iss/tiangong 的 found 为 false,多为空间站类目尚未开启或数据加载中——本工具会自动开启该类目,"
            u8"看到 loadingNote 时稍后再查一次即可。要飞到 ISS/天宫,取返回的 latDeg/lonDeg 再调用 fly_to。";
        t.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        SatelliteLayer* sat = satelliteLayer;
        t.execute = [sat](const picojson::value&) {
            // 自动开启空间站类目(ISS/天宫在此,数据量小),让"飞到ISS"在未手动开图层时也能用;
            // 不强制开 Nav/Weather/Starlink(Starlink ~7000 点,不该被一句 AI 查询拉起)。
            if (!sat->isCategoryEnabled(SatCategory::Station))
                sat->setCategoryEnabled(SatCategory::Station, true);
            picojson::value v; std::string err = picojson::parse(v, sat->summaryJson());
            if (!err.empty() || !v.is<picojson::object>())
            { picojson::object e; e["error"] = picojson::value("bad summary json"); return picojson::value(e); }
            picojson::object& obj = v.get<picojson::object>();
            bool issFound = obj.count("iss") && obj["iss"].is<picojson::object>()
                            && obj["iss"].get("found").is<bool>() && obj["iss"].get("found").get<bool>();
            if (!issFound)
                obj["loadingNote"] = picojson::value(std::string(
                    u8"空间站类目已自动开启,卫星数据抓取中,请稍后再查询一次"));
            OSG_NOTICE << "[AIChat] get_satellites_summary count="
                       << (obj.count("count") ? (long long)obj["count"].get<double>() : 0) << std::endl;
            return picojson::value(obj);
        };
        aiRuntime.tools->add(t);
    }

    // P4:世界情报查询工具族(纯 AI 工具,不上球)。fetcher 静态存活至进程退出,
    // 析构时 join worker;工具注册零成本(无 AI key 时不会被调用,同 registry 既有约定)。
    static earthai::AsyncJsonFetcher worldQueryFetcher;
    registerWorldQueryTools(aiRuntime.tools, &layerMgr, &worldQueryFetcher);

    // FeedLayer 用户(需要 configureAIChat 之后才有 aiRuntime.tools):GDACS 灾害预警 +
    // USGS 实时地震(T2 从 quake_data.cpp 迁移过来,图层 id/显示名/env 钩子语义不变)。
    sceneCamera->addChild(registerGdacsFeed(viewer, &layerMgr, aiRuntime.tools));
    sceneCamera->addChild(registerUsgsQuakesFeed(viewer, &layerMgr, aiRuntime.tools));
    sceneCamera->addChild(registerEonetFeed(viewer, &layerMgr, aiRuntime.tools));   // P1 T2:NASA 自然事件
    sceneCamera->addChild(registerGdeltFeed(viewer, &layerMgr, aiRuntime.tools));   // P1 T3:GDELT 新闻热点
    sceneCamera->addChild(registerGpsjamFeed(viewer, &layerMgr, aiRuntime.tools));  // P1 T4:GPSJam GPS 干扰
    sceneCamera->addChild(registerUnhcrFeed(viewer, &layerMgr, aiRuntime.tools));   // P1 T5:UNHCR 流离失所弧线
    sceneCamera->addChild(registerNhcFeed(viewer, &layerMgr, aiRuntime.tools));     // P1 T6:NHC 飓风路径/预报锥
    sceneCamera->addChild(registerStrategicFeeds(viewer, &layerMgr, aiRuntime.tools, mainFolder));   // P1 T7:静态战略数据集×5
    sceneCamera->addChild(registerFirmsFeed(viewer, &layerMgr, aiRuntime.tools));   // P3:FIRMS 火点(三级聚合 LOD)

    // 场景预设(Task 4 初版,P1 T9 扩军):一键切换图层组合。必须在全部图层(含上面的
    // FeedLayer)注册完之后注册——此时 LayerManager 里的 id 才齐全。取舍:
    //  - "灾害" = 灾害事件类四源(GDACS 预警 / USGS 地震 / EONET 自然事件 / NHC 飓风);
    //  - "军事" = 静态战略层的军事相关子集(bases/ports/nuclear/spaceports);datacenters
    //    语义偏"经济/科技基建",留给未来"经济"预设,不入军事;
    //  - "实时" = 实时点/线事件源(quakes/gdacs/flights/eonet/gdelt/gpsjam/nhc)。unhcr 是
    //    UNHCR 年度统计快照(非实时流)不入实时;precip/clouds 是栅格天气叠加,语义偏
    //    "天气底图"而非事件流,同样不入(P0 既定取舍);
    //  - "全部" = 除底图(base)/标注(labels)外全开(纯视觉壳层 Starlink 与需 key 的
    //    流式源 ships/fires 除外),且 precip 与 clouds 共用 OVERLAY 瓦片槽互斥
    //    (后开覆盖先开),两个同时列出会互相打架 → 只开 precip(维持现状)。
    {
        Preset p;
        p.name = u8"干净"; p.enabledIds.clear();
        layerMgr.addPreset(p);
        p.name = u8"灾害"; p.enabledIds = { "gdacs", "quakes", "eonet", "nhc" };
        layerMgr.addPreset(p);
        p.name = u8"军事"; p.enabledIds = { "bases", "ports", "nuclear", "spaceports" };
        layerMgr.addPreset(p);
        p.name = u8"实时"; p.enabledIds = { "quakes", "gdacs", "flights", "eonet", "gdelt", "gpsjam", "nhc" };
        layerMgr.addPreset(p);
        p.name = u8"全部"; p.enabledIds = { "precip", "flights", "hk3d", "gdacs", "quakes",
                                            "eonet", "gdelt", "gpsjam", "unhcr", "nhc",
                                            "bases", "ports", "nuclear", "spaceports", "datacenters",
                                            "satstations", "satnav", "satwx" };
        layerMgr.addPreset(p);

        // EARTH_PRESET=<预设名>:启动即应用该预设并打印各层 enabled 状态。headless 逻辑
        // 验证钩子(日志断言图层组合,不依赖截图;历史上 EARTH_OFFSCREEN 曾是 GL2.1
        // pbuffer 截不出有效画面,现已换 CGL 无头上下文可截真帧,但日志断言仍保留),
        // 语义同其它 EARTH_* 钩子;未设置=零影响。注意:它在 EARTH_CLOUDS/EARTH_PRECIP/
        // EARTH_FLIGHTS/EARTH_QUAKES/EARTH_3DTILES 之后执行,预设的"先全关"会覆盖这些
        // 逐层钩子刚打开的层——同用时以 EARTH_PRESET 为准,别追"钩子失灵"的鬼。
        const char* presetEnv = getenv("EARTH_PRESET");
        if (presetEnv && *presetEnv)
        {
            // OSG_WARN 是带 if 的宏:THEN 分支必须加大括号,否则 else 被宏内 if 吞掉
            // (回归矩阵实测踩过:applied 行静默不可达)。
            if (!layerMgr.applyPreset(presetEnv))
            {
                OSG_WARN << "[Preset] unknown preset name '" << presetEnv << "', ignored" << std::endl;
            }
            else
            {
                std::ostringstream oss;
                const std::vector<OverlayLayer> lls = layerMgr.layersSnapshot();
                for (size_t i = 0; i < lls.size(); ++i)
                    oss << (i > 0 ? " " : "") << lls[i].id << "=" << (lls[i].enabled ? 1 : 0);
                OSG_NOTICE << "[Preset] applied '" << presetEnv << "': " << oss.str() << std::endl;
            }
        }
    }

    // T3:EARTH_ARC_DEMO=<非0> → 弧线/折线原语演示层(不进产品,headless 验收/回归
    // 复现用;语义同其它 EARTH_* 钩子,未设或 "0" = 关,关时零代码激活、零日志)。
    const char* arcDemoEnv = getenv("EARTH_ARC_DEMO");
    if (arcDemoEnv && *arcDemoEnv && std::string(arcDemoEnv) != "0")
        sceneCamera->addChild(earthgeo::createArcDemoNode());

    // ImGui 控制面板 — 挂到最终 HUD 相机（cameras[3]），确保在地球图像之上绘制
    osg::ref_ptr<osgVerse::ImGuiManager> imgui = new osgVerse::ImGuiManager;
    imgui->setChineseSimplifiedFont(MISC_DIR + std::string("LXGWFasmartGothic.otf"));
    EarthControlUI* ctrlUI = new EarthControlUI(earthManipulator.get(), &earthRenderingUtils, &viewer);
    ctrlUI->_layers = &layerMgr;
    ctrlUI->_flight = flightLayer;
    ctrlUI->_satellites = satelliteLayer;
    ctrlUI->_ships = shipLayer;
    ctrlUI->_aiUI = aiUI;
    ctrlUI->_aiCore = aiCore;
    ctrlUI->_aiMedia = aiMedia;
    imgui->initialize(ctrlUI, false);
    imgui->addToView(&viewer, cameras[3]);  // cameras[3] = finalCamera (HUD, renders to screen)

#if defined(__APPLE__)
    // 中文 IME 直打(见 ImeFrameHandler/ime_bridge.mm)。EARTH_IME=0 可整体关闭
    // (真机出问题时的一键回退,行为退回"仅 Cmd+V 粘贴中文"的旧状态)。
    {
        const char* imeEnv = getenv("EARTH_IME");
        bool imeOn = !(imeEnv && *imeEnv && atoi(imeEnv) == 0);
        if (imeOn) { viewer.addEventHandler(new ImeFrameHandler(&viewer)); }
    }
#endif

    int screenNo = 0; arguments.read("--screen", screenNo);
    // EARTH_OFFSCREEN=1:离屏渲染(不打扰用户的不可见上下文)。自动化测试/子代理跑 E2E 用,
    // 避免测试窗弹到前台打断用户(真机反馈)。macOS 用纯 CGL 无头 GL 4.1 Core 上下文
    // (HeadlessCGLContext,原理/取舍见类注释:不建窗、不碰 AppKit → 构造性保证不弹窗、
    // 不抢焦点);其它平台维持原 pbuffer 路径。EARTH_AUTOCAP 抓帧在 offscreen 下改从
    // finalCamera 的 FBO 读回(见 AUTOCAP 分支注释)。失败则回退开窗并 WARN。
    const char* offscreenEnv = getenv("EARTH_OFFSCREEN");
    bool offscreenOk = false;
    if (offscreenEnv && *offscreenEnv && std::string(offscreenEnv) != "0")
    {
        osg::ref_ptr<osg::GraphicsContext::Traits> traits = new osg::GraphicsContext::Traits;
        traits->x = 0; traits->y = 0; traits->width = w; traits->height = h;
        traits->windowDecoration = false; traits->doubleBuffer = false;
        traits->red = 8; traits->green = 8; traits->blue = 8;
        traits->alpha = 8; traits->depth = 24; traits->stencil = 8;
#if defined(__APPLE__)
        // 先 probe 验证 CGL 可用(失败回退开窗);真正的 realize 留给 viewer.realize()
        // 统一做,否则 RealizeOperation 等初始化会被跳过(见 HeadlessCGLContext::probe 注释)。
        osg::ref_ptr<osg::GraphicsContext> gc;
        if (HeadlessCGLContext::probe()) gc = new HeadlessCGLContext(traits.get());
#else
        traits->pbuffer = true;          // 非 macOS 平台维持原 pbuffer 路径
        osg::ref_ptr<osg::GraphicsContext> gc = osg::GraphicsContext::createGraphicsContext(traits.get());
#endif
        if (gc.valid())
        {
            viewer.getCamera()->setGraphicsContext(gc.get());
            viewer.getCamera()->setViewport(new osg::Viewport(0, 0, w, h));
            {   // 投影纵横比对齐视口 —— 与开窗路径(SingleWindow::configure)同款修正,
                // 否则主相机沿用 View 构造时的默认纵横比,地球盘面会被横向拉椭(实测 ~1.28×)。
                double fovy = 30.0, ar = 1.0, zn = 1.0, zf = 10000.0;
                if (viewer.getCamera()->getProjectionMatrixAsPerspective(fovy, ar, zn, zf))
                {
                    double change = ((double)w / (double)h) / ar;
                    if (change != 1.0)
                        viewer.getCamera()->getProjectionMatrix() *= osg::Matrix::scale(1.0 / change, 1.0, 1.0);
                }
            }
            offscreenOk = true;
            OSG_NOTICE << "[Earth] offscreen context " << w << "x" << h << std::endl;
        }
        else
            OSG_WARN << "[Earth] EARTH_OFFSCREEN requested but offscreen context creation failed, falling back to window" << std::endl;
    }
    if (!offscreenOk)
    {
        // MSAA(v0.15-vision):osg::GraphicsContext::Traits 的构造函数从这个全局单例读
        // samples 字段(见 OSG 源码 GraphicsContext.cpp `samples = ds->getNumMultiSamples()`)。
        // 仅影响本 if 分支下面的 setUpViewOnSingleScreen 窗口路径——上面 EARTH_OFFSCREEN
        // 分支的 HeadlessCGLContext 自建 FBO 不读这个全局设置,离屏截图基线不受影响。
        osg::DisplaySettings::instance()->setNumMultiSamples(4);
        viewer.setUpViewOnSingleScreen(screenNo);
    }

    if (gotoLat < 1.0e8)  // --goto 指定了起始视点
        earthManipulator->setByEye(osg::inDegrees(gotoLat), osg::inDegrees(gotoLon), gotoAltKm * 1000.0);

    // Headless verification aid only (no effect on the actual fix/rendering):
    // EARTH_TILT=<radians> applies a startup camera pitch after --goto, so oblique views
    // — where LOD-boundary cracks and terrain penetration appear — can be reproduced and
    // captured without a live mouse drag. makeDeltaTilt subtracts its arg from _tilt.
    const char* tiltEnv = getenv("EARTH_TILT");
    if (tiltEnv && tiltEnv[0] && gotoLat < 1.0e8)
        earthManipulator->makeDeltaTilt(-(float)atof(tiltEnv));

    // Headless auto-capture: render a fixed number of frames (letting the database
    // pager stream tiles) then grab the GL framebuffer to a PNG. Used to verify the
    // earth renders without relying on OS screen-capture permissions.
    const char* autoCap = getenv("EARTH_AUTOCAP");
    if (autoCap && autoCap[0])
    {
        // offscreen 模式不走 ScreenCaptureHandler(它读"当前默认帧缓冲"):曾试过不可见
        // 窗口方案,macOS GL-on-Metal 对完全离屏窗口 surface 的读回存在**不定期整帧上下
        // 翻转**(实测同命令 ~1/4 概率倒置,翻转在 Apple shim 层,GL 侧无法探测/纠正);
        // 现在的 CGL 无头上下文则根本没有窗口帧缓冲。统一改为把 finalCamera(cameras[3],
        // 地球+海洋合成的最终 HUD 面)重定向到 FBO 并 attach osg::Image 每帧读回——纯 GL
        // 纹理读回,方向确定。代价:ImGui 面板不在截图里(它是 finalCamera 的 POST_DRAW
        // 回调,晚于 RenderStage 的 image 读回)——对回归截图反而更干净(纯地球画面,无 UI
        // 噪声);UI 逻辑验证走日志断言(如 EARTH_PRESET)。
        // 非 offscreen(开窗)路径的 ScreenCaptureHandler 行为保持原样,零变化。
        osg::ref_ptr<osg::Image> capImage;
        osg::ref_ptr<osgViewer::ScreenCaptureHandler> capturer;
        if (offscreenOk)
        {
            capImage = new osg::Image;
            capImage->allocateImage(w, h, 1, GL_RGBA, GL_UNSIGNED_BYTE);
            cameras[3]->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT);
            cameras[3]->attach(osg::Camera::COLOR_BUFFER0, capImage.get());
        }
        else
        {
            osg::ref_ptr<osgViewer::ScreenCaptureHandler::WriteToFile> writer =
                new osgViewer::ScreenCaptureHandler::WriteToFile(
                    "/tmp/earth_capture", "png",
                    osgViewer::ScreenCaptureHandler::WriteToFile::OVERWRITE);
            capturer = new osgViewer::ScreenCaptureHandler(writer.get(), 1);
            viewer.addEventHandler(capturer.get());
        }

        int total = atoi(autoCap); if (total < 100) total = 600;
        // Optional: aim the sun at the camera-facing hemisphere so the day side
        // is visible in the capture. EARTH_SUN_TO_CAMERA=1 (or -1 to flip sign).
        const char* sunCam = getenv("EARTH_SUN_TO_CAMERA");
        float sunSign = (sunCam && atof(sunCam) < 0.0) ? -1.0f : 1.0f;
        // Optional per-frame delay (ms) so the database pager has real wall-clock
        // time to stream deep online tiles before the capture frame.
        const char* fsleep = getenv("EARTH_FRAME_SLEEP_MS");
        int frameSleepMs = fsleep ? atoi(fsleep) : 0;
        // Optional: pin the sun to an exact azimuth/elevation (UI 滑块 convention) to
        // reproduce a reported lighting condition headless. EARTH_SUN_AZEL=az,el (度)。
        const char* sunAzEl = getenv("EARTH_SUN_AZEL");
        bool useSunAzEl = false; osg::Vec3 sunAzElDir(-1.0f, 0.0f, 0.0f);
        if (sunAzEl && *sunAzEl)
        {
            float az = 0.0f, el = 0.0f;
            if (sscanf(sunAzEl, "%f,%f", &az, &el) >= 1)
            {
                float ar = osg::DegreesToRadians(az), er = osg::DegreesToRadians(el);
                sunAzElDir.set(cosf(er) * cosf(ar), cosf(er) * sinf(ar), sinf(er));
                useSunAzEl = true;
            }
        }
        if (!viewer.isRealized()) viewer.realize();
        for (int i = 0; i < total && !viewer.done(); ++i)
        {
            if (sunCam)
            {
                osg::Vec3d eye, center, up;
                viewer.getCamera()->getViewMatrixAsLookAt(eye, center, up);
                osg::Vec3 dir(eye); dir.normalize(); dir *= sunSign;
                earthRenderingUtils.commonUniforms["WorldSunDir"]->set(dir);
            }
            if (useSunAzEl)
                earthRenderingUtils.commonUniforms["WorldSunDir"]->set(sunAzElDir);
            viewer.frame();
            if (frameSleepMs > 0) OpenThreads::Thread::microSleep((unsigned int)frameSleepMs * 1000);
            if (i == total - 5 && capturer.valid()) capturer->captureNextFrame(viewer);
        }
        viewer.frame();  // flush the pending capture
        if (capImage.valid())  // offscreen:落盘 FBO 读回的最终合成帧(路径与旧约定一致)
        {
            // 假绿灯防线:probe() 过但 realize 失败时 viewer 未 realize、帧全空转,
            // capImage 是未初始化内存——宁可不落盘让下游断言"文件不存在"而失败。
            // 注意:OSG_WARN/OSG_NOTICE 是带 if 的宏,分支必须用大括号,否则 else 被吞。
            if (!viewer.isRealized())
            {
                OSG_WARN << "[Earth] offscreen viewer never realized, capture skipped" << std::endl;
            }
            else if (osgDB::writeImageFile(*capImage, "/tmp/earth_capture_0.png"))
            {
                OSG_NOTICE << "[Earth] offscreen capture saved to /tmp/earth_capture_0.png" << std::endl;
            }
            else
            {
                OSG_WARN << "[Earth] offscreen capture failed to write /tmp/earth_capture_0.png" << std::endl;
            }
        }
        return 0;
    }
    return viewer.run();
}
