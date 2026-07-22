#include "science_ui/science_report_window.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

void testLifecycleAndBounds()
{
    ScienceReportWindowModel model;
    model.setViewport(1600.0f, 1000.0f);
    expect(model.openReady("a"), "new ready result must auto-open");
    expect(!model.openReady("a"), "same result must auto-open exactly once");
    const ScienceReportWindowState* state = model.find("a");
    expect(state && state->visible && !state->minimized,
           "opened report must be visible");
    expect(state->size.x() == 900.0f && state->size.y() == 680.0f,
           "wide viewport must use default report size");

    expect(model.drag("a", {-10000.0f, -10000.0f}),
           "visible report must drag");
    state = model.find("a");
    expect(state->position.x() >= 392.0f && state->position.y() >= 64.0f,
           "drag must clamp to map work area");
    expect(model.resize("a", ScienceReportResizeEdge::BottomRight,
                        {10000.0f, 10000.0f}),
           "corner resize must succeed");
    state = model.find("a");
    expect(state->size.x() <= 1200.0f && state->size.y() <= 900.0f,
           "resize must respect maximum size and viewport");
    model.resize("a", ScienceReportResizeEdge::Right, {-10000.0f, 0.0f});
    model.resize("a", ScienceReportResizeEdge::Bottom, {0.0f, -10000.0f});
    state = model.find("a");
    expect(state->size.x() >= 720.0f && state->size.y() >= 520.0f,
           "right and bottom resize must respect minimum size");

    expect(model.minimize("a"), "visible report must minimize");
    state = model.find("a");
    expect(state && !state->visible && state->minimized,
           "minimize must preserve report state");
    expect(model.reopen("a"), "minimized report must reopen");
    expect(model.close("a"), "report must close without delete");
    expect(model.find("a") != nullptr,
           "close must not delete report data");
    expect(model.reopen("a"), "closed report must reopen");
    expect(model.remove("a"), "explicit remove must delete report state");
    expect(model.find("a") == nullptr,
           "explicit remove must erase report state");
    expect(!model.openReady("a"),
           "deleted artifact must not be resurrected by stale snapshot");
}

void testCompactAndShelfLimit()
{
    ScienceReportWindowModel model;
    model.setViewport(1024.0f, 576.0f, 340.0f, 44.0f, 92.0f);
    expect(model.openReady("compact"), "compact report must open");
    const ScienceReportWindowState* compact = model.find("compact");
    expect(compact && compact->size.x() <= 660.0f &&
           compact->size.y() <= 416.0f,
           "compact report must fit above the AI composer");
    model.close("compact");

    for (int i = 1; i <= 4; ++i)
    {
        const std::string id = "r" + std::to_string(i);
        expect(model.openReady(id), "ready report must open");
        expect(model.minimize(id), "ready report must minimize");
    }
    expect(model.shelf().size() == 3,
           "report shelf must contain at most three entries");
    expect(model.shelf().front() == "r2" &&
           model.shelf().back() == "r4",
           "fourth minimize must evict oldest shelf entry");
    const ScienceReportWindowState* evicted = model.find("r1");
    expect(evicted && !evicted->visible && !evicted->minimized,
           "shelf eviction must keep result but remove its shelf chip");
}
}

int main()
{
    testLifecycleAndBounds();
    testCompactAndShelfLimit();
    std::cout << "ScienceReportWindow tests passed" << std::endl;
    return 0;
}
