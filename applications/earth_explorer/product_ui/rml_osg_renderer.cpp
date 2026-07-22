#include "rml_osg_renderer.h"

#include "RmlUi_Renderer_GL3.h"
#include <RmlUi/Core/Context.h>

class RmlOsgRenderer::Impl
{
public:
    std::unique_ptr<RenderInterface_GL3> renderer;
    bool glLoaderReady = false;
};

RmlOsgRenderer::RmlOsgRenderer() : _impl(new Impl) {}
RmlOsgRenderer::~RmlOsgRenderer() { shutdown(); }

bool RmlOsgRenderer::initialize(int width, int height, std::string& error)
{
    if (_impl->renderer) return true;
    Rml::String message;
    if (!RmlGL3::Initialize(&message))
    {
        error = message.empty() ? "RmlUi OpenGL 3 loader failed" : message;
        return false;
    }
    _impl->glLoaderReady = true;
    std::unique_ptr<RenderInterface_GL3> renderer(new RenderInterface_GL3);
    if (!static_cast<bool>(*renderer))
    {
        error = "RmlUi OpenGL 3 renderer failed to compile its shaders";
        RmlGL3::Shutdown();
        _impl->glLoaderReady = false;
        return false;
    }
    renderer->SetViewport(width, height);
    _impl->renderer = std::move(renderer);
    return true;
}

void RmlOsgRenderer::resize(int width, int height)
{
    if (_impl->renderer && width > 0 && height > 0)
        _impl->renderer->SetViewport(width, height);
}

void RmlOsgRenderer::render(Rml::Context& context)
{
    if (!_impl->renderer) return;
    _impl->renderer->BeginFrame();
    context.Render();
    _impl->renderer->EndFrame();
}

void RmlOsgRenderer::shutdown()
{
    _impl->renderer.reset();
    if (_impl->glLoaderReady)
    {
        RmlGL3::Shutdown();
        _impl->glLoaderReady = false;
    }
}

Rml::RenderInterface* RmlOsgRenderer::interface() const
{
    return _impl->renderer.get();
}

bool RmlOsgRenderer::initialized() const
{
    return _impl->renderer != nullptr;
}
