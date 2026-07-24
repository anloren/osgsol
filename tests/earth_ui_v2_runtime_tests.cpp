#define IMGUI_DEFINE_MATH_OPERATORS
#include "earth_ui_v2.h"
#include "earth_ui_components.h"
#include "earth_ui_tokens.h"
#include "ui_card.h"

#include "3rdparty/imgui/imgui_internal.h"
#include <ui/ImGui.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void expect(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

bool near(float lhs, float rhs)
{
    return std::abs(lhs - rhs) <= 1.0f;
}

ImGuiWindow* window(const char* name)
{
    return ImGui::FindWindowByName(name);
}

void expectInside(const char* name, float width, float height)
{
    ImGuiWindow* item = window(name);
    expect(item != nullptr, std::string(name) + " was not produced");
    expect(item->Pos.x >= -0.5f && item->Pos.y >= -0.5f,
           std::string(name) + " starts outside the viewport");
    expect(item->Pos.x + item->Size.x <= width + 0.5f &&
               item->Pos.y + item->Size.y <= height + 0.5f,
           std::string(name) + " extends beyond the viewport");
}

void drawLongDrawer(const earthui::EarthUiShellLayout& layout,
                    const earthui::EarthUiShellState& state)
{
    if (!earthui::beginEarthUiModuleDrawer(layout, state)) return;
    for (int row = 0; row < 96; ++row)
        ImGui::TextWrapped(
            u8"第 %d 条真实内容：只有内容超过面板时才应出现滚动条。", row + 1);
    earthui::endEarthUiModuleDrawer();
}

void runViewport(float width, float height)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DeltaTime = 1.0f / 60.0f;

    earthui::applyEarthUiV2Theme();
    const earthui::EarthUiShellLayout layout =
        earthui::computeEarthUiShellLayout(width, height, true);
    earthui::EarthUiShellState state;
    state.activeModule = earthui::EarthUiModule::Science;
    state.drawerOpen = true;
    earthui::EarthUiTopBarData data;
    data.latitudeDeg = 24.9752;
    data.longitudeDeg = 102.0031;
    data.altitudeKm = 1336.7;
    data.scienceAvailable = true;

    ImGui::NewFrame();
    earthui::drawEarthUiTopBar(layout, state, data);
    earthui::drawEarthUiModuleRail(layout, state);
    if (earthui::beginEarthUiModuleDrawer(layout, state))
    {
        if (earthui::beginDrawerSection(
                "runtime", u8"科学参数",
                u8"用于边界测试的超长科学模块文字，不允许越过抽屉或遮挡操作。"))
        {
            ImGui::TextWrapped(
                "%s", u8"状态、时间、方法和执行动作全部留在同一任务流。");
            earthui::fullWidthButton(
                u8"开始分析", earthui::StatusTone::Active);
            earthui::endDrawerSection();
        }
        earthui::endEarthUiModuleDrawer();
    }
    earthui::drawEarthUiContextTray(layout, state);
    ImGui::Render();

    // Scrollbar visibility follows the previous frame's measured content.
    // Repeat the stable short state before asserting that no decorative track
    // remains from a prior long module.
    ImGui::NewFrame();
    earthui::drawEarthUiTopBar(layout, state, data);
    earthui::drawEarthUiModuleRail(layout, state);
    if (earthui::beginEarthUiModuleDrawer(layout, state))
    {
        if (earthui::beginDrawerSection(
                "runtime", u8"科学参数",
                u8"用于边界测试的超长科学模块文字，不允许越过抽屉或遮挡操作。"))
        {
            ImGui::TextWrapped(
                "%s", u8"状态、时间、方法和执行动作全部留在同一任务流。");
            earthui::fullWidthButton(
                u8"开始分析", earthui::StatusTone::Active);
            earthui::endDrawerSection();
        }
        earthui::endEarthUiModuleDrawer();
    }
    earthui::drawEarthUiContextTray(layout, state);
    ImGui::Render();

    expectInside("##earth_ui_v2_top", width, height);
    expectInside("##earth_ui_v2_modules", width, height);
    expectInside("##earth_ui_v2_drawer", width, height);
    expectInside("##earth_ui_v2_context", width, height);
    expect(near(window("##earth_ui_v2_drawer")->Pos.x,
                layout.navigationWidth),
           "module rail and drawer expose a horizontal map seam");
    expect(near(window("##earth_ui_v2_drawer")->Pos.y,
                layout.topBarHeight),
           "top bar and drawer expose a vertical map seam");
    expect(near(window("##earth_ui_v2_drawer")->Size.x,
                layout.drawerWidth),
           "drawer runtime width differs from the shell contract; runtime=" +
           std::to_string(window("##earth_ui_v2_drawer")->Size.x) +
           ", contract=" + std::to_string(layout.drawerWidth));
    expect(ImGui::GetStyleColorVec4(ImGuiCol_WindowBg).x ==
               earthui::design::kCarbon.x &&
               ImGui::GetStyleColorVec4(ImGuiCol_CheckMark).x ==
               earthui::design::kCyan.x,
           "native shell is not using the shared Obsidian Survey tokens");
    expect(!window("##earth_ui_v2_drawer")->ScrollbarX,
           "module drawer exposes a horizontal scrollbar");
    expect(window("##earth_ui_v2_drawer")->ScrollMax.x <= 0.5f,
           "module drawer content overflows horizontally");
    expect(!window("##earth_ui_v2_drawer")->ScrollbarY,
           "short module drawer shows a decorative scrollbar");
    expect(!window("##earth_ui_v2_context")->ScrollbarX,
           "context tray exposes a horizontal scrollbar");
    expect(window("##earth_ui_v2_context")->ScrollMax.x <= 0.5f,
           "context tray content overflows horizontally");

    ImDrawData* drawData = ImGui::GetDrawData();
    expect(drawData != nullptr && drawData->CmdListsCount > 0,
           "shell produced no runtime draw data");
    for (int listIndex = 0; listIndex < drawData->CmdListsCount; ++listIndex)
    {
        const ImDrawList* list = drawData->CmdLists[listIndex];
        for (const ImDrawCmd& command : list->CmdBuffer)
        {
            expect(command.ClipRect.x >= -0.5f &&
                       command.ClipRect.y >= -0.5f &&
                       command.ClipRect.z <= width + 0.5f &&
                       command.ClipRect.w <= height + 0.5f,
                   "shell draw command uses an out-of-viewport clip rectangle");
        }
    }

    // The native drawer exposes a continuous scrollbar only when its content
    // actually overflows.  It must reach the end and then return to the top.
    ImGui::NewFrame();
    drawLongDrawer(layout, state);
    ImGui::Render();
    // ImGui resolves scrollbar presence from the previous frame's measured
    // content size. Render the same state once more before auditing it.
    ImGui::NewFrame();
    drawLongDrawer(layout, state);
    ImGui::Render();
    ImGuiWindow* drawer = window("##earth_ui_v2_drawer");
    expect(drawer->ScrollbarY && drawer->ScrollMax.y > 1.0f,
           "long module drawer does not expose a vertical scrollbar");
    ImGui::SetScrollY(drawer, drawer->ScrollMax.y);
    ImGui::NewFrame();
    drawLongDrawer(layout, state);
    ImGui::Render();
    drawer = window("##earth_ui_v2_drawer");
    const float drawerScrolledDown = drawer->Scroll.y;
    expect(drawerScrolledDown > 1.0f,
           "module drawer cannot reach content below the fold");
    ImGui::SetScrollY(drawer, 0.0f);
    ImGui::NewFrame();
    drawLongDrawer(layout, state);
    ImGui::Render();
    drawer = window("##earth_ui_v2_drawer");
    expect(drawer->Scroll.y < drawerScrolledDown,
           "module drawer cannot return upward after scrolling down");

    // Every non-science detail uses one bounded Insight Lens. Multiple
    // information sources become tabs instead of independently overlapping
    // windows or running off the left edge.
    ImGui::NewFrame();
    earthui::CardStack cards;
    earthui::Card first;
    first.id = "runtime-flight";
    first.title = u8"航班详情";
    first.style.chipLabel = u8"航班";
    first.drawBody = []() {
        for (int row = 0; row < 96; ++row)
            ImGui::TextWrapped(
                u8"第 %d 行详情：长文本必须在洞察透镜内部滚动。", row + 1);
    };
    earthui::Card second;
    second.id = "runtime-event";
    second.title = u8"事件流";
    second.style.chipLabel = u8"事件";
    second.drawBody = []() {
        ImGui::TextWrapped(
            u8"第二个来源通过标签切换，不在地图上生成第二个重叠窗口。");
    };
    cards.upsert(first);
    cards.upsert(second);
    cards.draw();
    ImGui::Render();
    // Auto-resize constraints use the previous frame's measured content.
    // Render the same card set once more before auditing its final scrollbar
    // state, matching the steady state users interact with.
    ImGui::NewFrame();
    cards.upsert(first);
    cards.upsert(second);
    cards.draw();
    ImGui::Render();
    expectInside(u8"洞察透镜###earth_insight_lens", width, height);
    ImGuiWindow* insight =
        window(u8"洞察透镜###earth_insight_lens");
    expect(insight->Size.y <= layout.insightHeight + 1.0f,
           "Insight Lens grows below its reserved region");
    expect(!insight->ScrollbarX,
           "Insight Lens exposes a horizontal scrollbar");

    // Select the remaining long card by removing the short active tab, then
    // give the auto-sized window one frame to settle on its measured content.
    ImGui::NewFrame();
    cards.upsert(first);
    cards.draw();
    ImGui::Render();
    ImGui::NewFrame();
    cards.upsert(first);
    cards.draw();
    ImGui::Render();
    insight = window(u8"洞察透镜###earth_insight_lens");
    expectInside(u8"洞察透镜###earth_insight_lens", width, height);
    expect(insight->Size.y <= layout.insightHeight + 1.0f,
           "long Insight Lens grows below its reserved region");
    expect(insight->ScrollbarY && insight->ScrollMax.y > 1.0f,
           "long Insight Lens content is not visibly scrollable; height=" +
               std::to_string(insight->Size.y) + ", content=" +
               std::to_string(insight->ContentSize.y) + ", scrollMax=" +
               std::to_string(insight->ScrollMax.y) + ", scrollbar=" +
               std::to_string(static_cast<int>(insight->ScrollbarY)));
    ImGui::SetScrollY(insight, insight->ScrollMax.y);
    ImGui::NewFrame();
    cards.upsert(first);
    cards.draw();
    ImGui::Render();
    insight = window(u8"洞察透镜###earth_insight_lens");
    const float scrolledDown = insight->Scroll.y;
    expect(scrolledDown > 1.0f,
           "Insight Lens cannot reach content below the fold");
    ImGui::SetScrollY(insight, 0.0f);
    ImGui::NewFrame();
    cards.upsert(first);
    cards.draw();
    ImGui::Render();
    insight = window(u8"洞察透镜###earth_insight_lens");
    expect(insight->Scroll.y < scrolledDown,
           "Insight Lens cannot return upward after scrolling down");
}

void runKeyboardNavigation()
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(640.0f, 360.0f);
    io.DeltaTime = 1.0f / 60.0f;
    osgVerse::configureImGuiProductInput(io);
    expect((io.ConfigFlags & ImGuiConfigFlags_NavEnableKeyboard) != 0,
           "product ImGui keyboard navigation is disabled");

    ImGuiID firstId = 0;
    ImGuiID secondId = 0;
    char firstValue[16] = {};
    char secondValue[16] = {};
    ImGui::NewFrame();
    ImGui::Begin("##keyboard_navigation");
    ImGui::InputText("##first", firstValue, sizeof(firstValue));
    ImGui::InputText("##second", secondValue, sizeof(secondValue));
    ImGui::End();
    ImGui::Render();

    ImGui::NewFrame();
    ImGui::Begin("##keyboard_navigation");
    ImGui::SetWindowFocus();
    ImGui::SetKeyboardFocusHere();
    ImGui::InputText("##first", firstValue, sizeof(firstValue));
    firstId = ImGui::GetItemID();
    ImGui::InputText("##second", secondValue, sizeof(secondValue));
    secondId = ImGui::GetItemID();
    ImGui::End();
    ImGui::Render();
    ImGui::NewFrame();
    ImGui::Begin("##keyboard_navigation");
    ImGui::InputText("##first", firstValue, sizeof(firstValue));
    ImGui::InputText("##second", secondValue, sizeof(secondValue));
    ImGui::End();
    ImGui::Render();
    expect(GImGui->NavId == firstId,
           "keyboard focus was not established on the first control");

    io.AddKeyEvent(ImGuiKey_Tab, true);
    ImGui::NewFrame();
    ImGui::Begin("##keyboard_navigation");
    ImGui::InputText("##first", firstValue, sizeof(firstValue));
    ImGui::InputText("##second", secondValue, sizeof(secondValue));
    ImGui::End();
    ImGui::Render();
    io.AddKeyEvent(ImGuiKey_Tab, false);
    ImGui::NewFrame();
    ImGui::Begin("##keyboard_navigation");
    ImGui::InputText("##first", firstValue, sizeof(firstValue));
    ImGui::InputText("##second", secondValue, sizeof(secondValue));
    ImGui::End();
    ImGui::Render();
    expect(GImGui->NavId == secondId,
           "Tab did not move keyboard focus to the next control");
}

void runModuleInteraction()
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1440.0f, 900.0f);
    io.DeltaTime = 1.0f / 60.0f;
    const earthui::EarthUiShellLayout layout =
        earthui::computeEarthUiShellLayout(1440.0f, 900.0f, true);
    earthui::EarthUiShellState state;
    state.drawerOpen = true;

    const auto drawRail = [&]() {
        ImGui::NewFrame();
        earthui::drawEarthUiModuleRail(layout, state);
        ImGui::Render();
    };
    drawRail();
    ImGuiWindow* rail = window("##earth_ui_v2_modules");
    expect(rail != nullptr, "module rail was not produced for interaction");
    const float firstButtonCenterY =
        rail->Pos.y + rail->WindowPadding.y +
        layout.topBarHeight * 0.18f + ImGui::GetStyle().ItemSpacing.y + 24.0f;
    const float moduleStep = 48.0f + ImGui::GetStyle().ItemSpacing.y;
    const float moduleCenterX = rail->Pos.x + rail->Size.x * 0.5f;
    const auto clickModule = [&](int index) {
        io.AddMousePosEvent(
            moduleCenterX, firstButtonCenterY + moduleStep * index);
        drawRail();
        io.AddMouseButtonEvent(0, true);
        drawRail();
        io.AddMouseButtonEvent(0, false);
        drawRail();
    };

    for (int index = 0;
         index <= static_cast<int>(earthui::EarthUiModule::Settings);
         ++index)
    {
        const earthui::EarthUiModule requested =
            static_cast<earthui::EarthUiModule>(index);
        state.activeModule = requested == earthui::EarthUiModule::Explore
            ? earthui::EarthUiModule::Settings
            : earthui::EarthUiModule::Explore;
        state.drawerOpen = true;
        clickModule(index);
        expect(static_cast<int>(state.activeModule) == index,
               "clicking the visible module button did not select index " +
                   std::to_string(index));
        expect(state.drawerOpen,
               "clicking a different module unexpectedly closed its drawer");
        expect(std::string(earthui::moduleLabel(state.activeModule)).size() > 0,
               "active module has no visible label");
    }
    clickModule(static_cast<int>(earthui::EarthUiModule::Settings));
    expect(!state.drawerOpen,
           "clicking the active module did not collapse its drawer");

    state.activeModule = earthui::EarthUiModule::Science;
    state.drawerOpen = true;
    expect(earthui::productScienceSurfaceVisible(state, true),
           "ready Science workbench is not visible with its drawer open");
    earthui::activateEarthUiModule(
        state, earthui::EarthUiModule::Science);
    expect(!earthui::productScienceSurfaceVisible(state, true),
           "collapsing Science leaves the product workbench visible");
    expect(!earthui::productScienceSurfaceVisible(state, false),
           "unready Science workbench is reported visible");

    expect(earthui::moduleAcceptsLayerGroup(
               earthui::EarthUiModule::Layers, "Strategic"),
           "Layers must accept every catalog group");
    expect(earthui::moduleAcceptsLayerGroup(
               earthui::EarthUiModule::Science, "ScienceEarth") &&
               earthui::moduleAcceptsLayerGroup(
                   earthui::EarthUiModule::Science, u8"科学数据") &&
               !earthui::moduleAcceptsLayerGroup(
                   earthui::EarthUiModule::Science, "Live Weather"),
           "Science must accept science groups without retaining live content");
    expect(earthui::moduleAcceptsLayerGroup(
               earthui::EarthUiModule::Live, "Live Weather") &&
               earthui::moduleAcceptsLayerGroup(
                   earthui::EarthUiModule::Live, u8"实时天气") &&
               !earthui::moduleAcceptsLayerGroup(
                   earthui::EarthUiModule::Live, "ScienceEarth"),
           "Live must accept only live/weather groups");
    expect(earthui::moduleAcceptsLayerGroup(
               earthui::EarthUiModule::Satellites, "Satellites") &&
               earthui::moduleAcceptsLayerGroup(
                   earthui::EarthUiModule::Satellites, u8"卫星") &&
               !earthui::moduleAcceptsLayerGroup(
                   earthui::EarthUiModule::Satellites, "3D City"),
           "Satellites must accept only satellite groups");
    expect(earthui::moduleAcceptsLayerGroup(
               earthui::EarthUiModule::City3D, "3D City") &&
               earthui::moduleAcceptsLayerGroup(
                   earthui::EarthUiModule::City3D, u8"三维城市") &&
               !earthui::moduleAcceptsLayerGroup(
                   earthui::EarthUiModule::City3D, "ScienceEarth"),
           "3D City must accept only city groups");
    const std::vector<earthui::EarthUiModule> modulesWithoutLayerGroups = {
        earthui::EarthUiModule::Explore,
        earthui::EarthUiModule::Tasks,
        earthui::EarthUiModule::Settings};
    for (earthui::EarthUiModule module : modulesWithoutLayerGroups)
        expect(!earthui::moduleAcceptsLayerGroup(module, "ScienceEarth"),
               "non-layer module unexpectedly accepts a catalog group");
}
}

int main()
{
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    runViewport(1024.0f, 576.0f);
    runViewport(1440.0f, 900.0f);
    runViewport(2048.0f, 1152.0f);
    runKeyboardNavigation();
    runModuleInteraction();
    ImGui::DestroyContext(context);
    std::cout << "[OK] Earth UI v2 runtime bounds at 3 viewports\n";
    return 0;
}
