#include "rml_ui_runtime.h"

#include "rml_osg_renderer.h"

#include <RmlUi/Core.h>
#include <osg/Camera>
#include <osg/GraphicsContext>
#include <osg/RenderInfo>
#include <osg/Timer>
#include <osgGA/GUIEventAdapter>
#include <osgGA/GUIEventHandler>
#include <osgViewer/Viewer>
#include <pipeline/Global.h>

#include <algorithm>
#include <deque>
#include <mutex>
#include <utility>

namespace
{
enum class QueuedKind
{
    MouseMove,
    MouseButton,
    Wheel,
    Key,
    Text,
    MarkedText,
    CancelComposition
};

struct QueuedInput
{
    QueuedKind kind = QueuedKind::MouseMove;
    float x = 0.0f;
    float y = 0.0f;
    int value = 0;
    int modifiers = 0;
    bool pressed = false;
    RmlInputKey key = RmlInputKey::Unknown;
    std::string text;
};

int rmlModifiers(int osgModifiers)
{
    int result = 0;
    if (osgModifiers & osgGA::GUIEventAdapter::MODKEY_CTRL)
        result |= Rml::Input::KM_CTRL;
    if (osgModifiers & osgGA::GUIEventAdapter::MODKEY_SHIFT)
        result |= Rml::Input::KM_SHIFT;
    if (osgModifiers & osgGA::GUIEventAdapter::MODKEY_ALT)
        result |= Rml::Input::KM_ALT;
    if (osgModifiers & (osgGA::GUIEventAdapter::MODKEY_META |
                        osgGA::GUIEventAdapter::MODKEY_SUPER))
        result |= Rml::Input::KM_META;
    if (osgModifiers & osgGA::GUIEventAdapter::MODKEY_CAPS_LOCK)
        result |= Rml::Input::KM_CAPSLOCK;
    if (osgModifiers & osgGA::GUIEventAdapter::MODKEY_NUM_LOCK)
        result |= Rml::Input::KM_NUMLOCK;
    return result;
}

RmlInputKey normalizedKey(int key)
{
    switch (key)
    {
    case osgGA::GUIEventAdapter::KEY_BackSpace: return RmlInputKey::Backspace;
    case osgGA::GUIEventAdapter::KEY_Delete: return RmlInputKey::Delete;
    case osgGA::GUIEventAdapter::KEY_Escape: return RmlInputKey::Escape;
    case osgGA::GUIEventAdapter::KEY_Return:
    case osgGA::GUIEventAdapter::KEY_KP_Enter: return RmlInputKey::Return;
    case osgGA::GUIEventAdapter::KEY_Tab: return RmlInputKey::Tab;
    case osgGA::GUIEventAdapter::KEY_Left: return RmlInputKey::Left;
    case osgGA::GUIEventAdapter::KEY_Right: return RmlInputKey::Right;
    case osgGA::GUIEventAdapter::KEY_Up: return RmlInputKey::Up;
    case osgGA::GUIEventAdapter::KEY_Down: return RmlInputKey::Down;
    case osgGA::GUIEventAdapter::KEY_Home: return RmlInputKey::Home;
    case osgGA::GUIEventAdapter::KEY_End: return RmlInputKey::End;
    case osgGA::GUIEventAdapter::KEY_Page_Up: return RmlInputKey::PageUp;
    case osgGA::GUIEventAdapter::KEY_Page_Down: return RmlInputKey::PageDown;
    case 'a': case 'A': return RmlInputKey::A;
    case 'c': case 'C': return RmlInputKey::C;
    case 'v': case 'V': return RmlInputKey::V;
    case 'x': case 'X': return RmlInputKey::X;
    default: return RmlInputKey::Unknown;
    }
}

Rml::Input::KeyIdentifier rmlKey(RmlInputKey key)
{
    switch (key)
    {
    case RmlInputKey::Backspace: return Rml::Input::KI_BACK;
    case RmlInputKey::Delete: return Rml::Input::KI_DELETE;
    case RmlInputKey::Escape: return Rml::Input::KI_ESCAPE;
    case RmlInputKey::Return: return Rml::Input::KI_RETURN;
    case RmlInputKey::Tab: return Rml::Input::KI_TAB;
    case RmlInputKey::Left: return Rml::Input::KI_LEFT;
    case RmlInputKey::Right: return Rml::Input::KI_RIGHT;
    case RmlInputKey::Up: return Rml::Input::KI_UP;
    case RmlInputKey::Down: return Rml::Input::KI_DOWN;
    case RmlInputKey::Home: return Rml::Input::KI_HOME;
    case RmlInputKey::End: return Rml::Input::KI_END;
    case RmlInputKey::PageUp: return Rml::Input::KI_PRIOR;
    case RmlInputKey::PageDown: return Rml::Input::KI_NEXT;
    case RmlInputKey::A: return Rml::Input::KI_A;
    case RmlInputKey::C: return Rml::Input::KI_C;
    case RmlInputKey::V: return Rml::Input::KI_V;
    case RmlInputKey::X: return Rml::Input::KI_X;
    default: return Rml::Input::KI_UNKNOWN;
    }
}

int pointerButton(int osgButton)
{
    if (osgButton == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON) return 0;
    if (osgButton == osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON) return 1;
    if (osgButton == osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON) return 2;
    return -1;
}

bool isTextElement(Rml::Element* element)
{
    if (!element) return false;
    const Rml::String tag = element->GetTagName();
    if (tag == "textarea") return true;
    if (tag != "input") return false;
    const Rml::String type = element->GetAttribute<Rml::String>("type", "text");
    return type == "text" || type == "password" || type == "search";
}
}

class RmlUiRuntime::Impl
{
public:
    explicit Impl(RmlUiRuntime& owner) : bridge(owner) {}

    RmlOsgRenderer renderer;
    Rml::Context* context = nullptr;
    RmlInputBridge bridge;
    std::mutex queueMutex;
    std::deque<QueuedInput> queue;
    std::string fontPath;
    float logicalDpi = 96.0f;
    int width = 1;
    int height = 1;
    bool initialized = false;
    bool attached = false;
    bool frameClientReady = false;
    RmlUiFrameClient* frameClient = nullptr;
    std::string deferredError;
};

namespace
{
class RmlRuntimeEventHandler : public osgGA::GUIEventHandler
{
public:
    explicit RmlRuntimeEventHandler(RmlUiRuntime& runtime) : _runtime(runtime) {}
    bool handle(const osgGA::GUIEventAdapter& event,
                osgGA::GUIActionAdapter&) override
    {
        _runtime.processEvent(event);
        switch (event.getEventType())
        {
        case osgGA::GUIEventAdapter::KEYDOWN:
        case osgGA::GUIEventAdapter::KEYUP:
            return _runtime.wantsKeyboard();
        case osgGA::GUIEventAdapter::PUSH:
        case osgGA::GUIEventAdapter::RELEASE:
        case osgGA::GUIEventAdapter::DOUBLECLICK:
        case osgGA::GUIEventAdapter::DRAG:
        case osgGA::GUIEventAdapter::MOVE:
        case osgGA::GUIEventAdapter::SCROLL:
            return _runtime.wantsPointer();
        default:
            return false;
        }
    }
private:
    RmlUiRuntime& _runtime;
};

class RmlRuntimeDrawCallback : public osgVerse::CameraDrawCallback
{
public:
    RmlRuntimeDrawCallback(RmlUiRuntime& runtime, float logicalDpi)
        : _runtime(runtime), _logicalDpi(logicalDpi) {}

    void operator()(osg::RenderInfo& renderInfo) const override
    {
        osg::State* state = renderInfo.getState();
        if (!_runtime.context() && !_runtime.failed() && state &&
            state->getGraphicsContext())
        {
            std::string error;
            if (!_runtime.initialize(*state->getGraphicsContext(), _logicalDpi,
                                     error) && !error.empty())
                OSG_WARN << "[RmlUi] " << error << std::endl;
        }
        if (_runtime.context())
        {
            _runtime.update(osg::Timer::instance()->time_s());
            _runtime.render();
        }
        if (getSubCallback()) getSubCallback()->run(renderInfo);
    }
private:
    RmlUiRuntime& _runtime;
    float _logicalDpi;
};
}

RmlUiRuntime::RmlUiRuntime() : _impl(new Impl(*this)) {}
RmlUiRuntime::~RmlUiRuntime() {}

bool RmlUiRuntime::initialize(osg::GraphicsContext& graphics, float logicalDpi,
                              std::string& error)
{
    if (_impl->initialized) return true;
    if (_failed.load())
    {
        error = "RmlUi initialization previously failed; legacy UI remains active";
        return false;
    }
    const osg::GraphicsContext::Traits* traits = graphics.getTraits();
    _impl->width = traits ? std::max(1, traits->width) : 1;
    _impl->height = traits ? std::max(1, traits->height) : 1;
    _impl->logicalDpi = logicalDpi > 0.0f ? logicalDpi : 96.0f;
    if (!_impl->renderer.initialize(_impl->width, _impl->height, error))
    {
        _failed.store(true);
        return false;
    }

    Rml::SetRenderInterface(_impl->renderer.interface());
    if (!Rml::Initialise())
    {
        error = "RmlUi core initialization failed";
        _impl->renderer.shutdown();
        _failed.store(true);
        return false;
    }
    _impl->context = Rml::CreateContext(
        "osgsol-product-ui", Rml::Vector2i(_impl->width, _impl->height));
    if (!_impl->context)
    {
        error = "RmlUi context creation failed";
        Rml::Shutdown();
        _impl->renderer.shutdown();
        _failed.store(true);
        return false;
    }
    _impl->context->SetDensityIndependentPixelRatio(_impl->logicalDpi / 96.0f);
    if (!_impl->fontPath.empty() &&
        !Rml::LoadFontFace(_impl->fontPath, true))
    {
        error = "RmlUi could not load the bundled fallback font";
        shutdown();
        _failed.store(true);
        return false;
    }
    _impl->initialized = true;
    if (_impl->frameClient)
    {
        if (!_impl->frameClient->onRmlContextReady(*_impl->context, error))
        {
            shutdown();
            _failed.store(true);
            return false;
        }
        _impl->frameClientReady = true;
    }
    _ready.store(!_impl->frameClient || _impl->frameClientReady);
    return true;
}

bool RmlUiRuntime::attach(osgViewer::Viewer& viewer, osg::Camera& camera,
                          const std::string& fallbackFont, float logicalDpi,
                          std::string& error)
{
    if (_impl->attached) return true;
    if (logicalDpi <= 0.0f)
    {
        error = "logical DPI must be positive";
        _failed.store(true);
        return false;
    }
    _impl->fontPath = fallbackFont;
    _impl->logicalDpi = logicalDpi;
    viewer.addEventHandler(new RmlRuntimeEventHandler(*this));
    osg::ref_ptr<RmlRuntimeDrawCallback> callback =
        new RmlRuntimeDrawCallback(*this, logicalDpi);
    callback->setup(&camera, 2); // POST_DRAW: after the composed world, before swap.
    _impl->attached = true;
    _ready.store(false);
    return true;
}

void RmlUiRuntime::processEvent(const osgGA::GUIEventAdapter& event)
{
    QueuedInput input;
    input.modifiers = rmlModifiers(event.getModKeyMask());
    switch (event.getEventType())
    {
    case osgGA::GUIEventAdapter::MOVE:
    case osgGA::GUIEventAdapter::DRAG:
        input.kind = QueuedKind::MouseMove;
        input.x = event.getX();
        input.y = event.getY();
        break;
    case osgGA::GUIEventAdapter::PUSH:
    case osgGA::GUIEventAdapter::RELEASE:
        input.kind = QueuedKind::MouseButton;
        input.value = pointerButton(event.getButton());
        input.pressed = event.getEventType() == osgGA::GUIEventAdapter::PUSH;
        if (input.value < 0) return;
        break;
    case osgGA::GUIEventAdapter::SCROLL:
        input.kind = QueuedKind::Wheel;
        if (event.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_2D)
        {
            input.x = -event.getScrollingDeltaX();
            input.y = -event.getScrollingDeltaY();
        }
        else if (event.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_UP)
            input.y = -1.0f;
        else if (event.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_DOWN)
            input.y = 1.0f;
        else if (event.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_LEFT)
            input.x = -1.0f;
        else if (event.getScrollingMotion() == osgGA::GUIEventAdapter::SCROLL_RIGHT)
            input.x = 1.0f;
        break;
    case osgGA::GUIEventAdapter::KEYDOWN:
    case osgGA::GUIEventAdapter::KEYUP:
        input.kind = QueuedKind::Key;
        input.key = normalizedKey(event.getKey());
        input.pressed = event.getEventType() == osgGA::GUIEventAdapter::KEYDOWN;
        if (input.key == RmlInputKey::Unknown) return;
        break;
    default:
        return;
    }
    std::lock_guard<std::mutex> guard(_impl->queueMutex);
    if (_impl->queue.size() < 4096) _impl->queue.push_back(std::move(input));
}

void RmlUiRuntime::update(double)
{
    if (!_impl->context) return;
    std::deque<QueuedInput> queue;
    {
        std::lock_guard<std::mutex> guard(_impl->queueMutex);
        queue.swap(_impl->queue);
    }
    for (const QueuedInput& input : queue)
    {
        switch (input.kind)
        {
        case QueuedKind::MouseMove:
        {
            const bool pass = _impl->context->ProcessMouseMove(
                static_cast<int>(input.x),
                _impl->height - static_cast<int>(input.y), input.modifiers);
            _wantsPointer.store(!pass);
            _impl->bridge.setUiFocused(!pass || _wantsText.load());
            break;
        }
        case QueuedKind::MouseButton:
            _impl->bridge.processPointerButton(
                input.value, input.pressed, input.modifiers);
            break;
        case QueuedKind::Wheel:
            _impl->bridge.processWheel(input.x, input.y, input.modifiers);
            break;
        case QueuedKind::Key:
            _impl->bridge.processKey(input.key, input.pressed, input.modifiers);
            break;
        case QueuedKind::Text:
            _impl->bridge.commitText(input.text);
            break;
        case QueuedKind::MarkedText:
            _impl->bridge.setMarkedText(input.text);
            break;
        case QueuedKind::CancelComposition:
            _impl->bridge.cancelMarkedText();
            break;
        }
    }
    if (_impl->frameClient && _impl->frameClientReady)
        _impl->frameClient->onRmlFrame(*_impl->context);
    _impl->context->Update();
    const bool text = isTextElement(_impl->context->GetFocusElement());
    _wantsText.store(text);
    _wantsKeyboard.store(text || _wantsPointer.load());
    _impl->bridge.setUiFocused(_wantsPointer.load() || text);
}

void RmlUiRuntime::render()
{
    if (_impl->context) _impl->renderer.render(*_impl->context);
}

void RmlUiRuntime::shutdown()
{
    if (!_impl->initialized && !_impl->renderer.initialized()) return;
    if (_impl->context)
    {
        Rml::RemoveContext(_impl->context->GetName());
        _impl->context = nullptr;
    }
    if (_impl->initialized) Rml::Shutdown();
    _impl->renderer.shutdown();
    _impl->initialized = false;
    _impl->frameClientReady = false;
    _ready.store(false);
    _wantsPointer.store(false);
    _wantsKeyboard.store(false);
    _wantsText.store(false);
}

Rml::Context* RmlUiRuntime::context() const { return _impl->context; }

void RmlUiRuntime::setFrameClient(RmlUiFrameClient* client)
{
    _impl->frameClient = client;
    _impl->frameClientReady = false;
    _ready.store(false);
    if (_impl->context && client)
    {
        std::string error;
        if (client->onRmlContextReady(*_impl->context, error))
        {
            _impl->frameClientReady = true;
            _ready.store(true);
        }
        else if (!error.empty())
        {
            _failed.store(true);
            _ready.store(false);
            OSG_WARN << "[RmlUi] frame client initialization failed: "
                     << error << std::endl;
        }
    }
}

void RmlUiRuntime::enqueueCommittedText(const std::string& utf8)
{
    QueuedInput input; input.kind = QueuedKind::Text; input.text = utf8;
    std::lock_guard<std::mutex> guard(_impl->queueMutex);
    if (_impl->queue.size() < 4096) _impl->queue.push_back(std::move(input));
}

void RmlUiRuntime::enqueueMarkedText(const std::string& utf8)
{
    QueuedInput input; input.kind = QueuedKind::MarkedText; input.text = utf8;
    std::lock_guard<std::mutex> guard(_impl->queueMutex);
    if (_impl->queue.size() < 4096) _impl->queue.push_back(std::move(input));
}

void RmlUiRuntime::enqueueCancelComposition()
{
    QueuedInput input; input.kind = QueuedKind::CancelComposition;
    std::lock_guard<std::mutex> guard(_impl->queueMutex);
    if (_impl->queue.size() < 4096) _impl->queue.push_back(std::move(input));
}

void RmlUiRuntime::enqueueKeyTap(RmlInputKey key, int modifiers)
{
    QueuedInput down; down.kind = QueuedKind::Key; down.key = key;
    down.pressed = true; down.modifiers = modifiers;
    QueuedInput up = down; up.pressed = false;
    std::lock_guard<std::mutex> guard(_impl->queueMutex);
    if (_impl->queue.size() + 2 <= 4096)
    {
        _impl->queue.push_back(std::move(down));
        _impl->queue.push_back(std::move(up));
    }
}

bool RmlUiRuntime::processWheel(float x, float y, int modifiers)
{
    if (!_impl->context) return false;
    return !_impl->context->ProcessMouseWheel(Rml::Vector2f(x, y), modifiers);
}

bool RmlUiRuntime::processPointerButton(int button, bool pressed,
                                        int modifiers)
{
    if (!_impl->context) return false;
    const bool pass = pressed
        ? _impl->context->ProcessMouseButtonDown(button, modifiers)
        : _impl->context->ProcessMouseButtonUp(button, modifiers);
    _wantsPointer.store(!pass);
    return !pass;
}

bool RmlUiRuntime::processKey(RmlInputKey key, bool pressed, int modifiers)
{
    if (!_impl->context) return false;
    const Rml::Input::KeyIdentifier id = rmlKey(key);
    if (id == Rml::Input::KI_UNKNOWN) return false;
    const bool pass = pressed
        ? _impl->context->ProcessKeyDown(id, modifiers)
        : _impl->context->ProcessKeyUp(id, modifiers);
    return !pass;
}

bool RmlUiRuntime::processText(const std::string& utf8)
{
    return _impl->context && !_impl->context->ProcessTextInput(utf8);
}

void RmlUiRuntime::focusChanged(bool focused)
{
    if (!focused) _wantsKeyboard.store(false);
}
