#include "science_ui/rml_science_chart.h"

#include <RmlUi/Core.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
class NullRenderer : public Rml::RenderInterface
{
public:
    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override
    { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
                        Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override
    { return 0; }
    Rml::TextureHandle GenerateTexture(
        Rml::Span<const Rml::byte>, Rml::Vector2i) override
    { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}
}

int main()
{
    NullRenderer renderer;
    Rml::SetRenderInterface(&renderer);
    expect(Rml::Initialise(), "RmlUi core must initialize headlessly");
    registerRmlScienceChartElement();
    Rml::Context* context = Rml::CreateContext(
        "science-document-smoke", Rml::Vector2i(1440, 900));
    expect(context != nullptr, "headless Rml context must be created");

    const std::string root = std::string(OSGVERSE_SOURCE_DIR) +
        "/assets/misc/ui/scienceearth/";
    Rml::ElementDocument* composer = context->LoadDocument(
        root + "workbench.rml");
    expect(composer != nullptr, "science composer document must parse");
    expect(composer->GetElementById("run-action") != nullptr,
           "science composer primary action must instantiate");

    Rml::ElementDocument* report = context->LoadDocument(root + "report.rml");
    expect(report != nullptr, "science report document must parse");
    expect(rmlui_dynamic_cast<RmlScienceChart*>(
               report->GetElementById("science-chart")) != nullptr,
           "custom science chart element must instantiate");
    expect(report->GetElementById("methods-evidence") != nullptr,
           "report evidence section must instantiate");

    composer->Close();
    report->Close();
    Rml::RemoveContext(context->GetName());
    Rml::Shutdown();
    std::cout << "[OK] RmlUi science documents parse headlessly\n";
    return 0;
}
