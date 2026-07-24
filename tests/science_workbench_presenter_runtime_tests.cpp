#include "../applications/earth_explorer/science_plugin_runtime.h"
#include "../applications/earth_explorer/science_ui/science_workbench_presenter.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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

void expect(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

std::string inner(Rml::ElementDocument* document, const char* id)
{
    Rml::Element* element = document ? document->GetElementById(id) : nullptr;
    return element ? std::string(element->GetInnerRML()) : std::string();
}

std::vector<ScienceWorkbenchQueuedAction> drain(
    ScienceWorkbenchPresenter& presenter)
{
    std::vector<ScienceWorkbenchQueuedAction> actions;
    ScienceWorkbenchQueuedAction action;
    while (presenter.takeQueuedAction(action))
        actions.push_back(std::move(action));
    return actions;
}
}

int main()
{
    SciencePluginRuntime runtime;
    expect(runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN_V4, "index.sqlite"),
           runtime.error());

    NullRenderer renderer;
    Rml::SetRenderInterface(&renderer);
    expect(Rml::Initialise(), "RmlUi core must initialize headlessly");
    expect(Rml::LoadFontFace(
               std::string(OSGVERSE_SOURCE_DIR) +
               "/assets/misc/LXGWFasmartGothic.otf"),
           "ScienceEarth test font must load");
    Rml::Context* context = Rml::CreateContext(
        "science-presenter-runtime", Rml::Vector2i(1440, 900));
    expect(context != nullptr, "headless Rml context must be created");

    const std::string root = std::string(OSGVERSE_SOURCE_DIR) +
        "/assets/misc/ui/scienceearth/";
    ScienceWorkbenchPresenter presenter(runtime, root + "workbench.rml");
    presenter.setVisible(true);
    std::string error;
    expect(presenter.onRmlContextReady(*context, error), error);
    presenter.onRmlFrame(*context);
    expect(drain(presenter).empty(),
           "rendering the snapshot must not dispatch user actions");

    Rml::ElementDocument* composer = context->GetDocument(0);
    expect(composer != nullptr, "composer document must remain open");
    context->Update();
    context->SetDimensions(Rml::Vector2i(1024, 576));
    expect(context->GetDimensions() == Rml::Vector2i(1024, 576),
           "Science presenter context must accept a compact viewport");
    presenter.onRmlFrame(*context);
    context->Update();
    Rml::Element* workbench =
        composer->GetElementById("science-workbench");
    expect(workbench != nullptr, "Science workbench root must exist");
    expect(workbench->GetAbsoluteOffset().y +
               workbench->GetOffsetHeight() <= 577.0f,
           "production Science workbench leaves a compact viewport");
    expect(workbench->GetOffsetWidth() <= 380.0f,
           "production Science workbench grows too wide in compact mode");
    context->SetDimensions(Rml::Vector2i(1440, 900));
    expect(context->GetDimensions() == Rml::Vector2i(1440, 900),
           "Science presenter context must restore its full viewport");
    presenter.onRmlFrame(*context);
    context->Update();
    expect(inner(composer, "analysis-name").find(
               "ERA5 Agricultural Climate") != std::string::npos,
           "initial source description must match the model source");

    Rml::ElementFormControl* source =
        rmlui_dynamic_cast<Rml::ElementFormControl*>(
            composer->GetElementById("source-select"));
    expect(source != nullptr, "source select must exist");
    source->SetValue("alphaearth-foundations");
    expect(inner(composer, "analysis-name").find(
               "AlphaEarth Foundations") != std::string::npos,
           "a user source selection must update its title immediately");
    expect(inner(composer, "analysis-summary").find("64") !=
               std::string::npos,
           "AlphaEarth selection must not retain the agriculture summary");

    const std::vector<ScienceWorkbenchQueuedAction> sourceActions =
        drain(presenter);
    if (sourceActions.size() != 3)
    {
        std::string detail = "one source selection queued " +
            std::to_string(sourceActions.size()) + " actions:";
        for (const ScienceWorkbenchQueuedAction& action : sourceActions)
            detail += " " + action.name;
        expect(false, detail);
    }
    expect(sourceActions[0].name == "select-source" &&
               sourceActions[0].json.find("alphaearth-foundations") !=
                   std::string::npos,
           "source action must carry the AlphaEarth source ID");
    expect(sourceActions[1].name == "set-method" &&
               sourceActions[1].json.find("annual-summary") !=
                   std::string::npos,
           "source action must choose AlphaEarth's default method");

    // Exercise every formal science source through the same real select
    // control. Each choice must update its own title immediately and queue
    // source/method/year actions; this guards against the previous failure
    // where choosing AlphaEarth left the ERA5 agriculture form underneath.
    const std::vector<std::pair<std::string, std::string>> sourceCases = {
        {"sentinel-2-l2a", "Sentinel-2"},
        {"copernicus-dem-glo-30", "Copernicus DEM"},
        {"era5-land-surface-history", "ERA5-Land"},
        {"era5-agricultural-climate", "ERA5 Agricultural Climate"},
        {"alphaearth-foundations", "AlphaEarth Foundations"},
    };
    for (const auto& sourceCase : sourceCases)
    {
        source->SetValue(sourceCase.first);
        const std::string renderedName = inner(composer, "analysis-name");
        expect(renderedName.find(sourceCase.second) !=
                   std::string::npos,
               "source selection retained the wrong form for " +
                   sourceCase.first + "; rendered=" + renderedName);
        const std::vector<ScienceWorkbenchQueuedAction> actions =
            drain(presenter);
        expect(actions.size() == 3,
               "source selection must queue source, method and year for " +
                   sourceCase.first);
        expect(actions.front().name == "select-source" &&
                   actions.front().json.find(sourceCase.first) !=
                       std::string::npos,
               "source selection action carries the wrong ID for " +
                   sourceCase.first);
    }

    Rml::ElementFormControl* method =
        rmlui_dynamic_cast<Rml::ElementFormControl*>(
            composer->GetElementById("method-select"));
    expect(method != nullptr, "method select must exist");
    method->SetValue("direction-change");
    expect(inner(composer, "method-summary").find("点积") !=
               std::string::npos,
           "a user method selection must update its explanation immediately");
    const std::vector<ScienceWorkbenchQueuedAction> methodActions =
        drain(presenter);
    expect(methodActions.size() == 1 &&
               methodActions.front().name == "set-method" &&
               methodActions.front().json.find("direction-change") !=
                   std::string::npos,
           "one method selection must dispatch exactly one method action");

    presenter.publishActionError("simulated action rejection");
    presenter.onRmlFrame(*context);
    expect(inner(composer, "analysis-name").find(
               "ERA5 Agricultural Climate") != std::string::npos,
           "a rejected source action must restore the confirmed model source");

    expect(runtime.dispatchWorkbenchAction(
               "{\"schema\":\"science-workbench-action-v1\","
               "\"action\":\"run\"}", error),
           error);
    presenter.onRmlFrame(*context);
    expect(drain(presenter).empty(),
           "opening an artifact must not dispatch a metric change recursively");

    Rml::ElementDocument* report = context->GetDocument(1);
    expect(report != nullptr, "report document must remain open");
    expect(inner(report, "report-title").find("AlphaEarth Foundations") !=
               std::string::npos,
           "the opened report must match the AlphaEarth artifact");
    expect(!inner(report, "chart-title").empty() &&
               !inner(report, "chart-unit").empty(),
           "ready report must explain the selected chart metric and unit");
    expect(!inner(report, "chart-y-max").empty() &&
               !inner(report, "chart-y-mid").empty() &&
               !inner(report, "chart-y-min").empty(),
           "ready report must label the scientific Y axis");
    expect(!inner(report, "chart-x-first").empty() &&
               !inner(report, "chart-x-mid").empty() &&
               !inner(report, "chart-x-last").empty(),
           "ready report must label first, middle and last chart years");

    Rml::RemoveContext(context->GetName());
    Rml::Shutdown();
    std::cout << "[OK] Science workbench presenter separates model sync from "
                 "user events" << std::endl;
    return 0;
}
