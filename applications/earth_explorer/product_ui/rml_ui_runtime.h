#pragma once

#include "rml_input_bridge.h"

#include <atomic>
#include <memory>
#include <string>

namespace osg { class Camera; class GraphicsContext; }
namespace osgGA { class GUIEventAdapter; }
namespace osgViewer { class Viewer; }
namespace Rml { class Context; }

class RmlUiRuntime : private RmlInputSink
{
public:
    RmlUiRuntime();
    ~RmlUiRuntime();

    bool initialize(osg::GraphicsContext& graphics, float logicalDpi,
                    std::string& error);
    bool attach(osgViewer::Viewer& viewer, osg::Camera& camera,
                const std::string& fallbackFont, float logicalDpi,
                std::string& error);
    void processEvent(const osgGA::GUIEventAdapter& event);
    void update(double monotonicSeconds);
    void render();
    void shutdown();
    Rml::Context* context() const;

    bool wantsPointer() const { return _wantsPointer.load(); }
    bool wantsKeyboard() const { return _wantsKeyboard.load(); }
    bool wantsTextInput() const { return _wantsText.load(); }

    // Thread-safe entry points used by the macOS NSTextInputClient overlay.
    void enqueueCommittedText(const std::string& utf8);
    void enqueueMarkedText(const std::string& utf8);
    void enqueueCancelComposition();
    void enqueueKeyTap(RmlInputKey key, int modifiers = 0);

private:
    bool processWheel(float x, float y, int modifiers) override;
    bool processPointerButton(int button, bool pressed,
                              int modifiers) override;
    bool processKey(RmlInputKey key, bool pressed, int modifiers) override;
    bool processText(const std::string& utf8) override;
    void focusChanged(bool focused) override;

    class Impl;
    std::unique_ptr<Impl> _impl;
    std::atomic<bool> _wantsPointer{false};
    std::atomic<bool> _wantsKeyboard{false};
    std::atomic<bool> _wantsText{false};
};
