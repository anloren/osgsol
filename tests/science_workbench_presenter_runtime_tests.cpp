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
class TestSystemInterface : public Rml::SystemInterface
{
public:
    void SetClipboardText(const Rml::String& text) override
    {
        clipboard = text;
    }

    void GetClipboardText(Rml::String& text) override
    {
        text = clipboard;
    }

    std::string clipboard;
};

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

Rml::ElementDocument* documentContaining(Rml::Context& context,
                                         const char* id)
{
    for (int index = 0; index < context.GetNumDocuments(); ++index)
    {
        Rml::ElementDocument* document = context.GetDocument(index);
        if (document && document->GetElementById(id)) return document;
    }
    return nullptr;
}

float height(Rml::ElementDocument* document, const char* id)
{
    Rml::Element* element = document ? document->GetElementById(id) : nullptr;
    return element ? element->GetOffsetHeight() : 0.0f;
}

bool visible(Rml::ElementDocument* document, const char* id)
{
    Rml::Element* element = document ? document->GetElementById(id) : nullptr;
    return element && element->IsVisible(true);
}

bool hasClass(Rml::ElementDocument* document, const char* id,
              const char* className)
{
    Rml::Element* element = document ? document->GetElementById(id) : nullptr;
    return element && element->IsClassSet(className);
}

void click(Rml::Context& context, Rml::ElementDocument* document,
           const char* id)
{
    Rml::Element* element = document ? document->GetElementById(id) : nullptr;
    expect(element != nullptr, std::string("click target is missing: ") + id);
    const Rml::Vector2f before = element->GetAbsoluteOffset();
    const Rml::Vector2i viewport = context.GetDimensions();
    Rml::Element* scroll = element->GetClosestScrollableContainer();
    const Rml::Vector2f scrollOffset =
        scroll ? scroll->GetAbsoluteOffset() : Rml::Vector2f();
    const float clipTop = scroll
        ? scrollOffset.y : 0.0f;
    const float clipBottom = scroll
        ? scrollOffset.y + scroll->GetClientHeight()
        : static_cast<float>(viewport.y);
    if (before.y < clipTop ||
        before.y + element->GetOffsetHeight() > clipBottom)
        element->ScrollIntoView(true);
    context.Update();
    const Rml::Vector2f center = element->GetAbsoluteOffset() +
        Rml::Vector2f(
            element->GetOffsetWidth() * 0.5f,
            element->GetOffsetHeight() * 0.5f);
    context.ProcessMouseMove(-1, -1, 0);
    context.ProcessMouseMove(
        static_cast<int>(center.x), static_cast<int>(center.y), 0);
    Rml::Element* hover = context.GetHoverElement();
    bool targetHovered = false;
    for (Rml::Element* node = hover; node; node = node->GetParentNode())
        if (node == element) { targetHovered = true; break; }
    if (!targetHovered)
    {
        std::string detail = std::string("click target is covered: ") + id;
        if (hover)
            detail += "; hover=" + std::string(hover->GetTagName()) +
                "#" + std::string(hover->GetId()) +
                "." + std::string(hover->GetClassNames()) +
                " at " + std::to_string(hover->GetAbsoluteOffset().x) +
                "," + std::to_string(hover->GetAbsoluteOffset().y) +
                " size " + std::to_string(hover->GetOffsetWidth()) +
                "x" + std::to_string(hover->GetOffsetHeight());
        detail += "; target at " +
            std::to_string(element->GetAbsoluteOffset().x) + "," +
            std::to_string(element->GetAbsoluteOffset().y) + " size " +
            std::to_string(element->GetOffsetWidth()) + "x" +
            std::to_string(element->GetOffsetHeight());
        if (Rml::Element* parent = element->GetParentNode())
            detail += "; parent=" + std::string(parent->GetTagName()) +
                "#" + std::string(parent->GetId()) + "." +
                std::string(parent->GetClassNames()) + " at " +
                std::to_string(parent->GetAbsoluteOffset().x) + "," +
                std::to_string(parent->GetAbsoluteOffset().y) + " size " +
                std::to_string(parent->GetOffsetWidth()) + "x" +
                std::to_string(parent->GetOffsetHeight());
        detail += "; center=" + std::to_string(center.x) + "," +
            std::to_string(center.y);
        expect(false, detail);
    }
    context.ProcessMouseButtonDown(0, 0);
    context.ProcessMouseButtonUp(0, 0);
    context.Update();
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

void expectSingleAction(
    ScienceWorkbenchPresenter& presenter, const std::string& name,
    const std::string& payload = std::string())
{
    const std::vector<ScienceWorkbenchQueuedAction> actions =
        drain(presenter);
    if (actions.size() != 1)
    {
        std::string detail = "expected exactly one action for " + name +
            ", got " + std::to_string(actions.size()) + ":";
        for (const ScienceWorkbenchQueuedAction& action : actions)
            detail += " " + action.name;
        expect(false, detail);
    }
    expect(actions.front().name == name,
           "expected action " + name + ", got " + actions.front().name);
    if (!payload.empty())
        expect(actions.front().json.find(payload) != std::string::npos,
               "action " + name + " is missing payload " + payload);
}
}

int main()
{
    SciencePluginRuntime runtime;
    expect(runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN_V4, "index.sqlite"),
           runtime.error());

    NullRenderer renderer;
    TestSystemInterface system;
    Rml::SetRenderInterface(&renderer);
    Rml::SetSystemInterface(&system);
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
    expect(inner(composer, "requested-time").find("2017") !=
               std::string::npos &&
               inner(composer, "available-time").find("1940") !=
               std::string::npos,
           "initial workbench must separate requested and available time");
    expect(inner(composer, "loading-time").find("未在读取") !=
               std::string::npos &&
               inner(composer, "applied-time").find("尚无结果") !=
               std::string::npos,
           "a selected draft must not masquerade as loading or applied");
    expect(!visible(composer, "science-help-copy"),
           "science help must start collapsed");
    click(*context, composer, "science-help");
    expect(visible(composer, "science-help-copy"),
           "science help button does not reveal its explanation");
    click(*context, composer, "science-help");
    expect(!visible(composer, "science-help-copy"),
           "science help button does not collapse its explanation");
    expect(!visible(composer, "cost-body"),
           "preflight cost details must start collapsed");
    click(*context, composer, "cost-toggle");
    expect(visible(composer, "cost-body"),
           "preflight cost disclosure does not open");
    click(*context, composer, "cost-toggle");
    expect(!visible(composer, "cost-body"),
           "preflight cost disclosure does not close");
    Rml::ElementFormControl* unlockedTargetAction =
        rmlui_dynamic_cast<Rml::ElementFormControl*>(
            composer->GetElementById("unlocked-target-action"));
    expect(unlockedTargetAction != nullptr,
           "unlocked target action menu must exist");
    unlockedTargetAction->SetValue("lock-map-center");
    expectSingleAction(presenter, "lock-map-center");
    presenter.onRmlFrame(*context);
    context->Update();

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
    unlockedTargetAction->SetValue("lock-current-view");
    expectSingleAction(presenter, "lock-current-view");

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
    expect(!visible(composer, "method-help-copy"),
           "method help must be collapsed before user interaction");
    click(*context, composer, "method-help");
    expect(visible(composer, "method-help-copy"),
           "method help button does not reveal its explanation");
    click(*context, composer, "method-help");
    expect(!visible(composer, "method-help-copy"),
           "method help button does not collapse its explanation");

    click(*context, composer, "run-action");
    const std::vector<ScienceWorkbenchQueuedAction> runActions =
        drain(presenter);
    if (runActions.size() != 2)
    {
        std::string detail = "an unlocked run queued " +
            std::to_string(runActions.size()) + " actions:";
        for (const ScienceWorkbenchQueuedAction& action : runActions)
            detail += " " + action.name;
        expect(false, detail);
    }
    expect(runActions[0].name == "lock-map-center" &&
               runActions[1].name == "run",
           "an unlocked run queued actions in the wrong order");

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
    context->Update();
    expect(inner(composer, "applied-time").find("2017") !=
               std::string::npos,
           "a completed artifact must publish its applied time");
    expect(drain(presenter).empty(),
           "opening an artifact must not dispatch a metric change recursively");

    Rml::ElementDocument* report =
        documentContaining(*context, "science-report");
    expect(report != nullptr, "report document must remain open");
    const std::string reportTitle = inner(report, "report-title");
    expect(reportTitle.find("AlphaEarth Foundations") != std::string::npos,
           "the opened report must match the AlphaEarth artifact; rendered=" +
               reportTitle);
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
    if (visible(composer, "unlocked-target-actions") ||
        !visible(composer, "locked-target-actions"))
    {
        Rml::Element* unlocked =
            composer->GetElementById("unlocked-target-actions");
        Rml::Element* locked =
            composer->GetElementById("locked-target-actions");
        std::string detail =
            "a locked result must expose target update and focus controls";
        detail += "; target-status=" + inner(composer, "target-status");
        detail += "; unlocked-visible=" +
            std::to_string(unlocked && unlocked->IsVisible(true));
        detail += "; locked-visible=" +
            std::to_string(locked && locked->IsVisible(true));
        expect(false, detail);
    }
    Rml::ElementFormControl* lockedTargetAction =
        rmlui_dynamic_cast<Rml::ElementFormControl*>(
            composer->GetElementById("locked-target-action"));
    expect(lockedTargetAction != nullptr,
           "locked target action menu must exist");
    lockedTargetAction->SetValue("lock-map-center");
    expectSingleAction(presenter, "lock-map-center");
    presenter.onRmlFrame(*context);
    context->Update();
    lockedTargetAction->SetValue("focus-target");
    expectSingleAction(presenter, "focus-target");

    click(*context, report, "report-focus-target");
    expectSingleAction(presenter, "focus-target");
    click(*context, report, "tab-trends");
    expect(visible(report, "trends") && !visible(report, "overview"),
           "real report tab click does not switch to time trends");
    expect(hasClass(report, "tab-trends", "selected") &&
               !hasClass(report, "tab-overview", "selected"),
           "time-trend content and selected report tab disagree");
    Rml::ElementFormControl* metric =
        rmlui_dynamic_cast<Rml::ElementFormControl*>(
            report->GetElementById("report-metric-select"));
    expect(metric != nullptr, "report metric select must exist");
    metric->SetValue("embedding-similarity");
    expectSingleAction(
        presenter, "select-metric", "embedding-similarity");
    expect(inner(report, "chart-title").find("similarity") !=
               std::string::npos,
           "metric selection does not update the plotted metric title");
    click(*context, report, "tab-spatial-range");
    expect(visible(report, "spatial-range") &&
               !visible(report, "trends"),
           "real report tab click does not switch to spatial evidence");
    expect(hasClass(report, "tab-spatial-range", "selected") &&
               !hasClass(report, "tab-trends", "selected"),
           "spatial content and selected report tab disagree");
    click(*context, report, "tab-methods-evidence");
    expect(visible(report, "methods-evidence") &&
               !visible(report, "spatial-range"),
           "real report tab click does not switch to methods and evidence");
    expect(hasClass(report, "tab-methods-evidence", "selected") &&
               !hasClass(report, "tab-spatial-range", "selected"),
           "method content and selected report tab disagree");
    click(*context, report, "tab-overview");
    expect(visible(report, "overview") &&
               !visible(report, "methods-evidence"),
           "real report tab click does not return to overview");
    expect(hasClass(report, "tab-overview", "selected") &&
               !hasClass(report, "tab-methods-evidence", "selected"),
           "overview content and selected report tab disagree");

    click(*context, report, "tab-methods-evidence");
    const std::string firstArtifactId =
        inner(report, "report-artifact-id");
    expect(firstArtifactId == "alphaearth-test-artifact-1",
           "first generated artifact must have a stable unique ID");
    click(*context, report, "copy-artifact-id");
    expect(system.clipboard == firstArtifactId,
           "copy result ID does not reach the Rml system clipboard");
    expect(inner(report, "copy-artifact-status").find("已复制") !=
               std::string::npos,
           "copy result ID does not provide visible feedback");

    click(*context, report, "report-overflow");
    expect(visible(report, "delete-confirmation"),
           "report more menu does not reveal its destructive confirmation");
    click(*context, report, "report-delete-cancel");
    expect(!visible(report, "delete-confirmation"),
           "report delete cancellation does not close the confirmation");

    click(*context, report, "report-minimize");
    expect(!visible(report, "science-report") &&
               visible(report, "report-shelf") &&
               visible(report, "shelf-0"),
           "report minimize does not move the result to the shelf");
    expectSingleAction(presenter, "minimize-report");
    click(*context, report, "shelf-0");
    expect(visible(report, "science-report"),
           "shelf result does not reopen the minimized report");
    expectSingleAction(presenter, "open-report", firstArtifactId);

    click(*context, report, "report-close");
    expect(!visible(report, "science-report"),
           "report close leaves the report visible");
    expectSingleAction(presenter, "close-report");

    expect(runtime.dispatchWorkbenchAction(
               "{\"schema\":\"science-workbench-action-v1\","
               "\"action\":\"run\"}", error),
           error);
    presenter.onRmlFrame(*context);
    context->Update();
    expect(drain(presenter).empty(),
           "opening a second artifact must not dispatch recursively");
    const std::string secondArtifactId =
        inner(report, "report-artifact-id");
    expect(secondArtifactId == "alphaearth-test-artifact-2" &&
               secondArtifactId != firstArtifactId,
           "a second run must open a distinct formal report");
    expect(visible(report, "science-report"),
           "a newly completed run must open its report");
    click(*context, report, "report-minimize");
    expectSingleAction(presenter, "minimize-report");

    expect(runtime.dispatchWorkbenchAction(
               "{\"schema\":\"science-workbench-action-v1\","
               "\"action\":\"run\"}", error),
           error);
    presenter.onRmlFrame(*context);
    context->Update();
    const std::string thirdArtifactId =
        inner(report, "report-artifact-id");
    expect(thirdArtifactId == "alphaearth-test-artifact-3",
           "a third run must open a distinct formal report");
    click(*context, report, "report-minimize");
    expectSingleAction(presenter, "minimize-report");

    expect(runtime.dispatchWorkbenchAction(
               "{\"schema\":\"science-workbench-action-v1\","
               "\"action\":\"run\"}", error),
           error);
    presenter.onRmlFrame(*context);
    context->Update();
    const std::string fourthArtifactId =
        inner(report, "report-artifact-id");
    expect(fourthArtifactId == "alphaearth-test-artifact-4",
           "a fourth run must open a distinct formal report");
    click(*context, report, "report-minimize");
    expectSingleAction(presenter, "minimize-report");
    expect(visible(report, "shelf-0") &&
               visible(report, "shelf-1") &&
               visible(report, "shelf-2"),
           "three minimized reports do not expose all formal shelf slots");

    click(*context, report, "shelf-2");
    expect(inner(report, "report-artifact-id") == fourthArtifactId,
           "third shelf slot reopens the wrong report");
    expectSingleAction(presenter, "open-report", fourthArtifactId);
    click(*context, report, "report-minimize");
    expectSingleAction(presenter, "minimize-report");
    click(*context, report, "shelf-1");
    expect(inner(report, "report-artifact-id") == thirdArtifactId,
           "second shelf slot reopens the wrong report");
    expectSingleAction(presenter, "open-report", thirdArtifactId);

    click(*context, report, "report-overflow");
    expect(visible(report, "delete-confirmation"),
           "reopened report does not expose the delete confirmation");
    click(*context, report, "report-delete");
    expect(!visible(report, "science-report"),
           "permanent deletion leaves the removed report visible");
    expectSingleAction(presenter, "remove-artifact", thirdArtifactId);

    Rml::RemoveContext(context->GetName());
    Rml::Shutdown();
    std::cout << "[OK] Science workbench presenter separates model sync from "
                 "user events" << std::endl;
    return 0;
}
