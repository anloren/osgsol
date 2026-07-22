#pragma once

#include <memory>
#include <string>

namespace Rml { class Context; class RenderInterface; }

// Thin ownership boundary around RmlUi's official OpenGL 3 renderer. The
// upstream renderer backs up and restores every GL state it mutates in
// BeginFrame/EndFrame, which is required when sharing OSG's context.
class RmlOsgRenderer
{
public:
    RmlOsgRenderer();
    ~RmlOsgRenderer();

    bool initialize(int width, int height, std::string& error);
    void resize(int width, int height);
    void render(Rml::Context& context);
    void shutdown();
    Rml::RenderInterface* interface() const;
    bool initialized() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};
