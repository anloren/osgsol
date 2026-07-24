#include "earth_ui_v2.h"

#include "earth_ui_tokens.h"
#include "marker_style.h"

#include <ui/ImGuiComponents.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace earthui
{
namespace
{
using design::kBorder;
using design::kCarbon;
using design::kCyan;
using design::kIron;
using design::kOxblood;
using design::kRaisedIron;
using design::kText;
using design::kTextDim;
using design::kTextStrong;
using design::kVermilion;

struct ModuleVisual
{
    EarthUiModule module;
    earthmark::MarkerShape shape;
};

const std::array<ModuleVisual, 8> kModules = {{
    {EarthUiModule::Explore, earthmark::MarkerShape::Ring},
    {EarthUiModule::Layers, earthmark::MarkerShape::Square},
    {EarthUiModule::Science, earthmark::MarkerShape::Hexagon},
    {EarthUiModule::Live, earthmark::MarkerShape::StarBurst},
    {EarthUiModule::Satellites, earthmark::MarkerShape::SatBox},
    {EarthUiModule::City3D, earthmark::MarkerShape::Diamond},
    {EarthUiModule::Tasks, earthmark::MarkerShape::Teardrop},
    {EarthUiModule::Settings, earthmark::MarkerShape::Circle},
}};

bool containsAsciiInsensitive(const std::string& value, const char* needle)
{
    if (!needle || !needle[0]) return false;
    std::string folded = value;
    for (char& character : folded)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character + ('a' - 'A'));
    std::string target = needle;
    for (char& character : target)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character + ('a' - 'A'));
    return folded.find(target) != std::string::npos;
}

void drawContextItem(const char* label, const char* value, bool accent = false)
{
    ImGui::BeginGroup();
    ImGui::TextDisabled("%s", label);
    if (accent) ImGui::PushStyleColor(ImGuiCol_Text, kCyan);
    ImGui::TextUnformatted(value);
    if (accent) ImGui::PopStyleColor();
    ImGui::EndGroup();
}

const char* contextKindShortLabel(EarthUiContextKind kind)
{
    switch (kind)
    {
    case EarthUiContextKind::View: return u8"相机与视图";
    case EarthUiContextKind::Layer: return u8"图层显示";
    case EarthUiContextKind::DataSpecific: return u8"数据源参数";
    case EarthUiContextKind::LiveWindow: return u8"刷新时间窗";
    case EarthUiContextKind::Orbit: return u8"轨道与覆盖";
    case EarthUiContextKind::Object: return u8"对象与 LOD";
    case EarthUiContextKind::Task: return u8"任务状态";
    case EarthUiContextKind::None: return u8"无";
    }
    return u8"无";
}

void drawModuleHelp(EarthUiModule module)
{
    if (ImGui::SmallButton("?")) ImGui::OpenPopup("##module_context_help");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(u8"查看本模块的交互规则");
    const ImGuiIO& io = ImGui::GetIO();
    const float popupWidth = std::min(
        360.0f, std::max(220.0f, io.DisplaySize.x - 24.0f));
    const float popupHeight = std::min(
        260.0f, std::max(120.0f, io.DisplaySize.y - 24.0f));
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(std::min(220.0f, popupWidth), 0.0f),
        ImVec2(popupWidth, popupHeight));
    if (ImGui::BeginPopup("##module_context_help"))
    {
        ImGui::TextUnformatted(moduleLabel(module));
        ImGui::Separator();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", moduleContextLabel(module));
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
}
}

const char* moduleLabel(EarthUiModule module)
{
    switch (module)
    {
    case EarthUiModule::Explore: return u8"探索";
    case EarthUiModule::Layers: return u8"图层";
    case EarthUiModule::Science: return u8"科学";
    case EarthUiModule::Live: return u8"实时";
    case EarthUiModule::Satellites: return u8"卫星";
    case EarthUiModule::City3D: return u8"三维城市";
    case EarthUiModule::Tasks: return u8"任务";
    case EarthUiModule::Settings: return u8"设置";
    }
    return u8"探索";
}

const char* moduleShortLabel(EarthUiModule module)
{
    switch (module)
    {
    case EarthUiModule::Explore: return u8"探索";
    case EarthUiModule::Layers: return u8"图层";
    case EarthUiModule::Science: return u8"科学";
    case EarthUiModule::Live: return u8"实时";
    case EarthUiModule::Satellites: return u8"卫星";
    case EarthUiModule::City3D: return u8"三维";
    case EarthUiModule::Tasks: return u8"任务";
    case EarthUiModule::Settings: return u8"设置";
    }
    return u8"探索";
}

const char* moduleContextLabel(EarthUiModule module)
{
    switch (contextKindForModule(module))
    {
    case EarthUiContextKind::View: return u8"视图型控件 · 相机 / 光照 / 跳转";
    case EarthUiContextKind::Layer: return u8"图层型控件 · 可见性 / 图例 / 透明度";
    case EarthUiContextKind::DataSpecific:
        return u8"数据源上下文 · 时间、云量或静态高程由当前数据决定";
    case EarthUiContextKind::LiveWindow: return u8"实时型控件 · 时间窗 / 刷新 / 数据状态";
    case EarthUiContextKind::Orbit: return u8"轨道型控件 · 过境 / 轨迹 / 地面范围";
    case EarthUiContextKind::Object: return u8"对象型控件 · 选择 / LOD / 高度与材质";
    case EarthUiContextKind::Task: return u8"任务型控件 · 运行中 / 队列 / 历史";
    case EarthUiContextKind::None: return u8"当前模块没有底部上下文控件";
    }
    return "";
}

bool moduleAcceptsLayerGroup(EarthUiModule module, const std::string& group)
{
    switch (module)
    {
    case EarthUiModule::Layers:
        return true;
    case EarthUiModule::Science:
        return containsAsciiInsensitive(group, "science") ||
            group.find(u8"科学") != std::string::npos;
    case EarthUiModule::Live:
        return containsAsciiInsensitive(group, "live") ||
            containsAsciiInsensitive(group, "weather") ||
            group.find(u8"实时") != std::string::npos ||
            group.find(u8"天气") != std::string::npos;
    case EarthUiModule::Satellites:
        return containsAsciiInsensitive(group, "satellite") ||
            group.find(u8"卫星") != std::string::npos;
    case EarthUiModule::City3D:
        return containsAsciiInsensitive(group, "3d city") ||
            group.find(u8"三维城市") != std::string::npos;
    default:
        return false;
    }
}

void activateEarthUiModule(EarthUiShellState& state, EarthUiModule module)
{
    if (state.activeModule == module)
        state.drawerOpen = !state.drawerOpen;
    else
    {
        state.activeModule = module;
        state.drawerOpen = true;
    }
}

void applyEarthUiV2Theme()
{
    static ImGuiContext* themedContext = nullptr;
    ImGuiContext* current = ImGui::GetCurrentContext();
    if (!current || current == themedContext) return;
    themedContext = current;

    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = kText;
    colors[ImGuiCol_TextDisabled] = kTextDim;
    colors[ImGuiCol_WindowBg] = kCarbon;
    colors[ImGuiCol_ChildBg] = kIron;
    colors[ImGuiCol_PopupBg] = kIron;
    colors[ImGuiCol_Border] = kBorder;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_FrameBg] = kIron;
    colors[ImGuiCol_FrameBgHovered] = kRaisedIron;
    colors[ImGuiCol_FrameBgActive] = kOxblood;
    colors[ImGuiCol_TitleBg] = kCarbon;
    colors[ImGuiCol_TitleBgActive] = kOxblood;
    colors[ImGuiCol_TitleBgCollapsed] = kCarbon;
    colors[ImGuiCol_MenuBarBg] = kCarbon;
    colors[ImGuiCol_ScrollbarBg] = kCarbon;
    colors[ImGuiCol_ScrollbarGrab] = kRaisedIron;
    colors[ImGuiCol_ScrollbarGrabHovered] = kOxblood;
    colors[ImGuiCol_ScrollbarGrabActive] = kVermilion;
    colors[ImGuiCol_CheckMark] = kCyan;
    colors[ImGuiCol_SliderGrab] = kVermilion;
    colors[ImGuiCol_SliderGrabActive] = kCyan;
    colors[ImGuiCol_Button] = kIron;
    colors[ImGuiCol_ButtonHovered] = kOxblood;
    colors[ImGuiCol_ButtonActive] = kVermilion;
    colors[ImGuiCol_Header] = kIron;
    colors[ImGuiCol_HeaderHovered] = kOxblood;
    colors[ImGuiCol_HeaderActive] = kVermilion;
    colors[ImGuiCol_Separator] = kBorder;
    colors[ImGuiCol_SeparatorHovered] = kOxblood;
    colors[ImGuiCol_SeparatorActive] = kVermilion;
    colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = kOxblood;
    colors[ImGuiCol_ResizeGripActive] = kVermilion;
    colors[ImGuiCol_PlotLines] = kCyan;
    colors[ImGuiCol_PlotLinesHovered] = kVermilion;
    colors[ImGuiCol_PlotHistogram] = kVermilion;
    colors[ImGuiCol_PlotHistogramHovered] = kCyan;
    colors[ImGuiCol_Tab] = kCarbon;
    colors[ImGuiCol_TabHovered] = kOxblood;
    colors[ImGuiCol_TabActive] = kRaisedIron;
    colors[ImGuiCol_TabUnfocused] = kCarbon;
    colors[ImGuiCol_TabUnfocusedActive] = kIron;
    colors[ImGuiCol_TextSelectedBg] = ImVec4(
        kVermilion.x, kVermilion.y, kVermilion.z, 0.42f);
    colors[ImGuiCol_NavHighlight] = kCyan;

    style.WindowPadding = ImVec2(12.0f, 10.0f);
    style.FramePadding = ImVec2(9.0f, 6.0f);
    style.CellPadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 5.0f);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 11.0f;
    style.GrabMinSize = 10.0f;
    style.WindowRounding = 3.0f;
    style.ChildRounding = 2.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
}

bool drawEarthUiTopBar(const EarthUiShellLayout& layout,
                       EarthUiShellState& state,
                       const EarthUiTopBarData& data)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(layout.navigationWidth, 0.0f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(std::max(1.0f, io.DisplaySize.x - layout.navigationWidth),
               layout.topBarHeight), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    bool requestHome = false;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##earth_ui_v2_top", nullptr, flags))
    {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 min = ImGui::GetWindowPos();
        draw->AddRectFilled(min,
            ImVec2(min.x + 3.0f, min.y + layout.topBarHeight),
            ImGui::ColorConvertFloat4ToU32(kVermilion));

        const ImGuiStyle& style = ImGui::GetStyle();
        const auto controlWidth = [&style](const char* label) {
            return ImGui::CalcTextSize(label).x +
                style.FramePadding.x * 2.0f;
        };
        const char* drawerLabel =
            state.drawerOpen ? u8"收起面板" : u8"打开面板";
        const char* statusLabel =
            data.aiBusy ? u8"AI 运行中" : u8"系统就绪";
        const float homeWidth = controlWidth(u8"回到全球");
        const float drawerWidth = controlWidth(drawerLabel);
        const float statusWidth = ImGui::CalcTextSize(statusLabel).x;
        const float pluginWidth = ImGui::CalcTextSize(
            u8"科学插件未载入").x;
        const float baseActionsWidth =
            homeWidth + drawerWidth + statusWidth +
            style.ItemSpacing.x * 2.0f;
        const bool showPluginStatus = !data.scienceAvailable &&
            ImGui::GetWindowWidth() >= 920.0f;
        const float actionsWidth = baseActionsWidth +
            (showPluginStatus ? style.ItemSpacing.x + pluginWidth : 0.0f);
        const float actionX = std::max(
            style.WindowPadding.x,
            ImGui::GetWindowWidth() - style.WindowPadding.x - actionsWidth);
        const float actionY = std::max(
            0.0f, (layout.topBarHeight - ImGui::GetFrameHeight()) * 0.5f);
        const float leftClipRight = std::max(
            min.x + ImGui::GetStyle().WindowPadding.x,
            min.x + actionX - 12.0f);
        ImGui::PushClipRect(
            min, ImVec2(leftClipRight, min.y + layout.topBarHeight), true);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextStrong);
        ImGui::TextUnformatted("osgSol Earth / ScienceEarth");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();
        ImGui::TextUnformatted(moduleLabel(state.activeModule));
        if (actionX >= 720.0f)
        {
            ImGui::SameLine(0.0f, 18.0f);
            ImGui::TextDisabled(u8"%.4f°, %.4f° · %.1f km",
                data.latitudeDeg, data.longitudeDeg, data.altitudeKm);
        }
        ImGui::PopClipRect();

        ImGui::SetCursorPos(ImVec2(actionX, actionY));
        if (ImGui::Button(u8"回到全球")) requestHome = true;
        ImGui::SameLine();
        if (ImGui::Button(drawerLabel))
            state.drawerOpen = !state.drawerOpen;
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text,
            data.aiBusy ? kVermilion : design::kSuccess);
        ImGui::TextUnformatted(data.aiBusy ? u8"AI 运行中" : u8"系统就绪");
        ImGui::PopStyleColor();
        if (showPluginStatus)
        {
            ImGui::SameLine();
            ImGui::TextDisabled(u8"科学插件未载入");
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return requestHome;
}

void drawEarthUiModuleRail(const EarthUiShellLayout& layout,
                           EarthUiShellState& state)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(layout.navigationWidth, io.DisplaySize.y), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##earth_ui_v2_modules", nullptr, flags))
    {
        ImGui::Dummy(ImVec2(0.0f, layout.topBarHeight * 0.18f));
        for (const ModuleVisual& visual : kModules)
        {
            ImGui::PushID(static_cast<int>(visual.module));
            const bool active = state.activeModule == visual.module;
            if (active)
            {
                const ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    ImVec2(ImGui::GetWindowPos().x, p.y - 3.0f),
                    ImVec2(ImGui::GetWindowPos().x + 3.0f,
                           p.y + 47.0f),
                    ImGui::ColorConvertFloat4ToU32(kCyan));
                ImGui::PushStyleColor(ImGuiCol_Button, kOxblood);
            }
            const ImVec2 buttonSize(layout.navigationWidth - 18.0f, 48.0f);
            if (ImGui::Button("##module", buttonSize))
                activateEarthUiModule(state, visual.module);
            if (active) ImGui::PopStyleColor();

            const ImVec2 itemMin = ImGui::GetItemRectMin();
            const ImVec2 itemMax = ImGui::GetItemRectMax();
            const ImVec4 iconColor = active ? kCyan : kTextDim;
            const float iconSize = 15.0f;
            earthmark::drawMarkerIcon(ImGui::GetWindowDrawList(), visual.shape,
                (itemMin.x + itemMax.x) * 0.5f, itemMin.y + 14.0f,
                iconSize, ImGui::ColorConvertFloat4ToU32(iconColor));
            const char* label = moduleShortLabel(visual.module);
            const ImVec2 textSize = ImGui::CalcTextSize(label);
            ImGui::GetWindowDrawList()->AddText(
                ImVec2((itemMin.x + itemMax.x - textSize.x) * 0.5f,
                       itemMin.y + 27.0f),
                ImGui::ColorConvertFloat4ToU32(active ? kTextStrong : kTextDim),
                label);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s · %s", moduleLabel(visual.module),
                                  moduleContextLabel(visual.module));
            ImGui::PopID();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

bool beginEarthUiModuleDrawer(const EarthUiShellLayout& layout,
                              const EarthUiShellState& state)
{
    if (!state.drawerOpen || layout.drawerWidth <= 0.0f) return false;
    ImGui::SetNextWindowPos(ImVec2(layout.drawerX, layout.drawerY),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(layout.drawerWidth, layout.drawerHeight),
                             ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysVerticalScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (!ImGui::Begin("##earth_ui_v2_drawer", nullptr, flags))
    {
        ImGui::End();
        ImGui::PopStyleVar();
        return false;
    }
    ImGui::PushTextWrapPos(0.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, kTextStrong);
    ImGui::TextUnformatted(moduleLabel(state.activeModule));
    ImGui::PopStyleColor();
    ImGui::SameLine();
    drawModuleHelp(state.activeModule);
    ImGui::Separator();
    return true;
}

void endEarthUiModuleDrawer()
{
    ImGui::PopTextWrapPos();
    ImGui::End();
    ImGui::PopStyleVar();
}

void drawEarthUiContextTray(const EarthUiShellLayout& layout,
                            const EarthUiShellState& state)
{
    if (contextKindForModule(state.activeModule) == EarthUiContextKind::None)
        return;
    ImGui::SetNextWindowPos(ImVec2(layout.contextX, layout.contextY),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(layout.contextWidth, layout.contextHeight), ImGuiCond_Always);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##earth_ui_v2_context", nullptr, flags))
    {
        drawContextItem(u8"当前模块", moduleLabel(state.activeModule));
        ImGui::SameLine(0.0f, 30.0f);
        drawContextItem(u8"上下文",
            contextKindShortLabel(contextKindForModule(state.activeModule)),
            true);
        ImGui::SameLine(0.0f, 18.0f);
        drawModuleHelp(state.activeModule);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

}
