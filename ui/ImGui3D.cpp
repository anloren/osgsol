#include <imgui/imgui.h>
#include <osg/Version>
#include <osg/Camera>
#include <osgDB/FileNameUtils>
#include <osgDB/ReadFile>
#include "ImGui.h"
#include "ImGuiInputQueue.h"
#include "pipeline/Pipeline.h"
#include "pipeline/Utilities.h"
using namespace osgVerse;

extern void newImGuiFrame(osg::RenderInfo& renderInfo, double& time, std::function<void(ImGuiIO&)> func);
extern void endImGuiFrame(osg::RenderInfo& renderInfo, ImGuiManager* manager,
                          std::map<std::string, ImTextureID>& textureIdList,
                          std::function<void(ImGuiContentHandler*, ImGuiContext*)> func);
extern void startImGuiContext(ImGuiManager* manager, std::map<std::string, ImFont*>& fonts);
extern int convertImGuiCharacterKey(int key);
extern int convertImGuiSpecialKey(int key);
extern void applyImGuiInputEvents(ImGuiIO& io, const std::vector<ImGuiInputEvent>& events);
extern void shutdownImGuiRendererBackend();

class ImGuiHandler3D : public osgGA::GUIEventHandler
{
public:
    std::map<std::string, ImFont*> _fonts;
    ImGuiInputQueue _input;

    ImGuiHandler3D() : _started(false) {}

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
        _input.publishCapture(io.WantCaptureMouse, io.WantCaptureKeyboard);
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
                _input.push(ImGuiInputEvent::keyEvent(
                    ea.getKey(), isKeyDown, ea.getModKeyMask()));
                return wantCaptureKeyboard;
            }
        case osgGA::GUIEventAdapter::SCROLL:
            _input.push(ImGuiInputEvent::mouseWheelEvent(
                ea.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_UP ? 1.0f : -1.0f));
            return wantCaptureMouse;
        default: return false;
        }
        return false;
    }

protected:
    virtual ~ImGuiHandler3D()
    {}

private:
    bool _started;
};

struct ImGuiDrawableCallback : public virtual osg::Drawable::DrawCallback
{
    ImGuiDrawableCallback(ImGuiManager* m, osgGA::GUIEventHandler* h)
        : _manager(m), _handler(h), _time(-1.0f) {}

    mutable std::map<std::string, ImTextureID> _textureIdList;
    osg::observer_ptr<osgGA::GUIEventHandler> _handler;
    ImGuiManager* _manager;
    mutable double _time;

    virtual void drawImplementation(osg::RenderInfo& renderInfo, const osg::Drawable* d) const
    {
        ImGuiHandler3D* handler = static_cast<ImGuiHandler3D*>(_handler.get());
        if (handler) { handler->start(_manager); newImGuiFrame(renderInfo, _time,
            [&](ImGuiIO& io) { handler->drain(io); }); }

        //d->drawImplementation(renderInfo);
        endImGuiFrame(renderInfo, _manager, _textureIdList,
                      [&](ImGuiContentHandler* v, ImGuiContext* context) {
            ImGuiHandler3D* handler = static_cast<ImGuiHandler3D*>(_handler.get());
            if (handler) v->ImGuiFonts = handler->_fonts;
            v->ImGuiTextures = _textureIdList; v->context = context;
            v->runInternal(_manager);
        });
        if (handler)
        {
            handler->publishCapture();
            if (_manager->consumeReleaseRequest()) handler->releaseOnDrawThread();
        }
    }
};

osg::Texture* ImGuiManager::addToTexture(osg::Group* parentOfRtt, int w, int h)
{
    osg::Texture* rttTex = Pipeline::createTexture(Pipeline::RGBA_INT8, w, h);
    osg::ref_ptr<osg::Camera> rttCamera = createRTTCamera(
        osg::Camera::COLOR_BUFFER0, rttTex, NULL, false);
    rttCamera->setReferenceFrame(osg::Transform::ABSOLUTE_RF);
    rttCamera->setProjectionMatrix(osg::Matrix::ortho2D(0.0, 1.0, 0.0, 1.0));
    rttCamera->setViewMatrix(osg::Matrix::identity());

    osg::Geode* geode = createScreenQuad(osg::Vec3(), 1.0f, 1.0f, osg::Vec4(0.0f, 0.0f, 1.0f, 1.0f));
    geode->getDrawable(0)->setUseDisplayList(false);
    geode->getDrawable(0)->setDrawCallback(new ImGuiDrawableCallback(this, _imguiHandler.get()));
    rttCamera->addChild(geode);
    if (parentOfRtt) parentOfRtt->addChild(rttCamera.get());
    return rttTex;
}

void ImGuiManager::initializeEventHandler3D()
{
    _imguiHandler = new ImGuiHandler3D;
}

void ImGuiManager::setMouseInput(const osg::Vec2& pos, int button, float wheel)
{
    ImGuiHandler3D* handler = dynamic_cast<ImGuiHandler3D*>(_imguiHandler.get());
    if (!handler)
    {
        OSG_NOTICE << "[ImGuiManager] setMouseInput() is unsupported in 2D GUI mode\n";
    }
    else
    {
        handler->_input.push(ImGuiInputEvent::virtualMouseEvent(pos[0], pos[1], button, wheel));
    }
}
