#define IMGUI_DEFINE_MATH_OPERATORS
#include "earth_ui_v2.h"
#include "earth_ui_components.h"
#include "earth_ui_tokens.h"
#include "ui_card.h"

#include "3rdparty/imgui/imgui_internal.h"

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

void runModuleInteraction()
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1440.0f, 900.0f);
    io.DeltaTime = 1.0f / 60.0f;
    const earthui::EarthUiShellLayout layout =
        earthui::computeEarthUiShellLayout(1440.0f, 900.0f, true);
    earthui::EarthUiShellState state;
    state.drawerOpen = true;
    for (int index = 1;
         index <= static_cast<int>(earthui::EarthUiModule::Settings);
         ++index)
    {
        earthui::activateEarthUiModule(
            state, static_cast<earthui::EarthUiModule>(index));
        ImGui::NewFrame();
        earthui::drawEarthUiModuleRail(layout, state);
        ImGui::Render();
        expect(static_cast<int>(state.activeModule) == index,
               "module activation did not select index " +
                   std::to_string(index));
        expect(state.drawerOpen,
               "switching module unexpectedly closed its drawer");
        expect(std::string(earthui::moduleLabel(state.activeModule)).size() > 0,
               "active module has no visible label");
    }
    earthui::activateEarthUiModule(
        state, earthui::EarthUiModule::Settings);
    expect(!state.drawerOpen,
           "activating the current module did not collapse its drawer");
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
    runModuleInteraction();
    ImGui::DestroyContext(context);
    std::cout << "[OK] Earth UI v2 runtime bounds at 3 viewports\n";
    return 0;
}
