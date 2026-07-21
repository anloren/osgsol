#include "earth_control_layout.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

namespace
{
bool nearlyEqual(float lhs, float rhs)
{
    return std::abs(lhs - rhs) < 0.01f;
}

size_t countOccurrences(const std::string& text, const std::string& needle)
{
    size_t count = 0;
    for (size_t pos = 0; (pos = text.find(needle, pos)) != std::string::npos;
         pos += needle.size())
        ++count;
    return count;
}
}

int main()
{
    // Retina 13/14-inch fullscreen: the stable shell must keep the map visible
    // between the module drawer and Insight Lens, with command/context regions
    // below both panels.
    const earthui::EarthUiShellLayout shell =
        earthui::computeEarthUiShellLayout(1024.0f, 576.0f, true);
    CHECK(nearlyEqual(shell.navigationWidth, 64.0f));
    CHECK(nearlyEqual(shell.drawerWidth, 310.0f));
    CHECK(nearlyEqual(shell.insightWidth, 330.0f));
    CHECK(shell.drawerX + shell.drawerWidth <
          1024.0f - shell.outerGap - shell.insightWidth);
    CHECK(shell.drawerY + shell.drawerHeight <= shell.commandY + 0.01f);
    CHECK(shell.insightTop + shell.insightHeight <= shell.commandY + 0.01f);
    CHECK(shell.commandY + shell.commandHeight < shell.contextY);
    CHECK(nearlyEqual(shell.contextY + shell.contextHeight +
                      shell.statusHeight, 576.0f));

    const earthui::EarthUiShellLayout shellClosed =
        earthui::computeEarthUiShellLayout(1024.0f, 576.0f, false);
    CHECK(nearlyEqual(shellClosed.drawerWidth, 0.0f));
    CHECK(nearlyEqual(shellClosed.commandX, shell.commandX));
    CHECK(nearlyEqual(shellClosed.contextY, shell.contextY));

    CHECK(earthui::contextKindForModule(earthui::EarthUiModule::Explore) ==
          earthui::EarthUiContextKind::View);
    CHECK(earthui::contextKindForModule(earthui::EarthUiModule::Science) ==
          earthui::EarthUiContextKind::DataSpecific);
    CHECK(earthui::contextKindForModule(earthui::EarthUiModule::Live) ==
          earthui::EarthUiContextKind::LiveWindow);
    CHECK(earthui::contextKindForModule(earthui::EarthUiModule::Satellites) ==
          earthui::EarthUiContextKind::Orbit);
    CHECK(earthui::contextKindForModule(earthui::EarthUiModule::City3D) ==
          earthui::EarthUiContextKind::Object);
    CHECK(earthui::contextKindForModule(earthui::EarthUiModule::Settings) ==
          earthui::EarthUiContextKind::None);

    const earthui::EarthControlPanelLayout compact =
        earthui::computeEarthControlPanelLayout(1024.0f, 576.0f);
    CHECK(nearlyEqual(compact.defaultWidth, shell.drawerWidth));
    CHECK(nearlyEqual(compact.defaultHeight, shell.drawerHeight));

    const earthui::ScienceWorkspaceLayout scienceCompact =
        earthui::computeScienceWorkspaceLayout(1024.0f, 576.0f, true);
    CHECK(nearlyEqual(scienceCompact.leftWidth, shell.drawerWidth));
    CHECK(scienceCompact.resultWidth <= 1024.0f * 0.32f);
    CHECK(scienceCompact.centerMapWidth >= 1024.0f * 0.30f);
    CHECK(nearlyEqual(scienceCompact.resultHeight, shell.insightHeight));
    CHECK(nearlyEqual(scienceCompact.resultTop, shell.insightTop));
    CHECK(nearlyEqual(scienceCompact.resultRight, shell.outerGap));

    const earthui::ScienceWorkspaceLayout scienceCollapsed =
        earthui::computeScienceWorkspaceLayout(1024.0f, 576.0f, false);
    CHECK(scienceCollapsed.resultWidth <= 44.0f);
    CHECK(scienceCollapsed.centerMapWidth > scienceCompact.centerMapWidth);

    std::vector<std::string> scienceFrameOrder;
    earthui::finishLeftThenDrawScienceResults(
        [&scienceFrameOrder]() { scienceFrameOrder.push_back("end-left"); },
        [&scienceFrameOrder]() { scienceFrameOrder.push_back("draw-results"); });
    CHECK(scienceFrameOrder.size() == 2);
    CHECK(scienceFrameOrder[0] == "end-left");
    CHECK(scienceFrameOrder[1] == "draw-results");

    // Large and small desktops remain ordered without covering the command deck.
    const earthui::EarthControlPanelLayout large =
        earthui::computeEarthControlPanelLayout(1920.0f, 1080.0f);
    CHECK(large.defaultWidth <= 372.0f);
    CHECK(large.defaultHeight < 1080.0f);

    // Small windows still need usable, ordered constraints.
    const earthui::EarthControlPanelLayout small =
        earthui::computeEarthControlPanelLayout(640.0f, 360.0f);
    CHECK(small.minWidth <= small.defaultWidth);
    CHECK(small.defaultWidth <= small.maxWidth);
    CHECK(small.minHeight <= small.defaultHeight);
    CHECK(small.defaultHeight <= small.maxHeight);
    const earthui::EarthUiShellLayout smallShell =
        earthui::computeEarthUiShellLayout(640.0f, 360.0f, true);
    CHECK(small.maxWidth <= 310.0f);
    CHECK(nearlyEqual(small.maxHeight, smallShell.drawerHeight));
    CHECK(smallShell.drawerY + smallShell.drawerHeight <=
          smallShell.commandY + 0.01f);

    // Keep the production UI wired to the v2 shell and its stable regions.
    std::ifstream input(std::string(OSGVERSE_SOURCE_DIR) +
                        "/applications/earth_explorer/EarthControlUI.h");
    std::ostringstream sourceBuffer;
    sourceBuffer << input.rdbuf();
    const std::string source = sourceBuffer.str();
    CHECK(input.good() || input.eof());
    CHECK(source.find("applyEarthUiV2Theme") != std::string::npos);
    CHECK(source.find("drawEarthUiTopBar") != std::string::npos);
    CHECK(source.find("drawEarthUiModuleRail") != std::string::npos);
    CHECK(source.find("beginEarthUiModuleDrawer") != std::string::npos);
    CHECK(source.find("drawEarthUiContextTray") != std::string::npos);
    CHECK(source.find("SetNextWindowPos(ImVec2(20.0f, 20.0f)") ==
          std::string::npos);

    // A constrained panel must not use ImGui's default "control then visible
    // label on the right" form. At compact widths that pattern clips the
    // label. Production fields use a wrapped label row followed by a
    // full-width control with a hidden ID instead.
    CHECK(source.find("ImGui::SliderFloat(u8\"") == std::string::npos);
    CHECK(source.find("ImGui::InputFloat(u8\"") == std::string::npos);
    CHECK(source.find("ImGui::InputInt(u8\"") == std::string::npos);
    CHECK(source.find("ImGui::Checkbox(u8\"") == std::string::npos);
    CHECK(source.find("ImGui::Checkbox(l.displayName.c_str()") == std::string::npos);
    CHECK(source.find("ImGui::Checkbox(p.label.c_str()") == std::string::npos);
    CHECK(source.find("SliderFloat((l.displayName") == std::string::npos);
    CHECK(source.find("ImGui::SliderInt(p.label.c_str()") == std::string::npos);
    CHECK(source.find("ImGui::SliderFloat(p.label.c_str()") == std::string::npos);
    CHECK(countOccurrences(source, "panelSliderFloat(") >= 8);
    CHECK(countOccurrences(source, "panelInputFloat(") >= 4);
    CHECK(countOccurrences(source, "panelInputInt(") >= 4);
    CHECK(countOccurrences(source, "panelCheckbox(") >= 8);
    std::ifstream shellInput(std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/earth_ui_v2.cpp");
    std::ostringstream shellBuffer;
    shellBuffer << shellInput.rdbuf();
    const std::string shellSource = shellBuffer.str();
    CHECK(shellInput.good() || shellInput.eof());
    CHECK(shellSource.find("ImGuiWindowFlags_AlwaysVerticalScrollbar") !=
          std::string::npos);
    CHECK(shellSource.find("PushTextWrapPos") != std::string::npos);
    CHECK(shellSource.find("PopTextWrapPos") != std::string::npos);
    CHECK(shellSource.find("kVermilion") != std::string::npos);
    CHECK(shellSource.find("kCyan") != std::string::npos);

    std::ifstream panelInput(std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/science_earth_panel.cpp");
    std::ostringstream panelBuffer;
    panelBuffer << panelInput.rdbuf();
    const std::string panel = panelBuffer.str();
    CHECK(panelInput.good() || panelInput.eof());
    CHECK(panel.find("ImGui::TextWrapped") != std::string::npos);
    CHECK(panel.find("drawAnnualSeriesChart") != std::string::npos);
    CHECK(panel.find("ImGui::PlotLines") == std::string::npos);
    CHECK(panel.find("AlwaysAutoResize") == std::string::npos);
    CHECK(panel.find("NoScrollbar") == std::string::npos);
    CHECK(panel.find("SliderInt") == std::string::npos);
    CHECK(panel.find("ProgressBar") == std::string::npos);
    CHECK(panel.find(u8"潜在嵌入关系") != std::string::npos);
    CHECK(panel.find(u8"不是物理量") != std::string::npos);
    CHECK(panel.find(u8"不是自然色") != std::string::npos);
    CHECK(panel.find("\"R = \"") != std::string::npos);
    CHECK(panel.find("\"G = \"") != std::string::npos);
    CHECK(panel.find("\"B = \"") != std::string::npos);
    CHECK(panel.find(u8"尚无可测时长") != std::string::npos);
    CHECK(panel.find("sciencePanelModeCapabilities(source, activeMode)") !=
          std::string::npos);
    CHECK(panel.find("sciencePanelPrimaryActionLabel(activeMode, source.id)") !=
          std::string::npos);
    CHECK(panel.find("resolveSciencePanelSource(sources, _state.sourceId)") !=
          std::string::npos);
    CHECK(panel.find("sciencePanelModesForSource(source)") !=
          std::string::npos);
    CHECK(panel.find("describeScienceArtifactUi") != std::string::npos);
    CHECK(panel.find("ImGui::OpenPopup") != std::string::npos);
    CHECK(panel.find("ImGui::BeginPopup") != std::string::npos);
    CHECK(panel.find(u8"本次未启用 PCA") == std::string::npos);
    CHECK(panel.find(u8"本次未启用聚类") == std::string::npos);
    CHECK(panel.find(u8"请在左侧设置本次研究并运行") ==
          std::string::npos);
    CHECK(panel.find("ImGuiWindowFlags_AlwaysVerticalScrollbar") !=
          std::string::npos);
    CHECK(panel.find("ImGuiStyleVar_ScrollbarSize") != std::string::npos);
    CHECK(panel.find("ImGuiCol_ScrollbarGrab") != std::string::npos);
    CHECK(panel.find("artifact->analysis.interpretation->front") ==
          std::string::npos);
    CHECK(panel.find("ImGuiTreeNodeFlags_DefaultOpen)) return") ==
          std::string::npos);
    const size_t draftRefresh = panel.find(
        "_currentDraft = buildSciencePanelDraft(");
    const size_t collapsedOperationsReturn = panel.find(
        "if (!operationsExpanded) return;");
    CHECK(draftRefresh != std::string::npos);
    CHECK(collapsedOperationsReturn != std::string::npos);
    CHECK(draftRefresh < collapsedOperationsReturn);

    const size_t step1 = panel.find(u8"1  定位区域 / Locate area");
    const size_t step2 = panel.find(
        u8"2  选择数据源与时间 / Source and time");
    const size_t step3 = panel.find(
        u8"3  选择分析方法 / Choose method");
    const size_t step4 = panel.find(
        u8"4  复核资源 / Review exact cost");
    const size_t step5 = panel.find(
        u8"5  开始或取消 / Start or cancel");
    const size_t step6 = panel.find(
        u8"6  查看地图、结果与证据 / View results");
    CHECK(step1 != std::string::npos && step2 != std::string::npos &&
          step3 != std::string::npos && step4 != std::string::npos &&
          step5 != std::string::npos && step6 != std::string::npos);
    CHECK(step1 < step2 && step2 < step3 && step3 < step4 &&
          step4 < step5 && step5 < step6);
    CHECK(panel.find(u8"当前状态 / Current state") != std::string::npos);
    CHECK(panel.find(u8"取消当前请求 / Cancel request") !=
          std::string::npos);
    CHECK(panel.find(u8"查看结果与证据 / View result and evidence") !=
          std::string::npos);
    CHECK(panel.find("configurationLocked") != std::string::npos);
    CHECK(panel.find("presentation.sourceText") != std::string::npos);
    CHECK(panel.find("presentation.retryText") != std::string::npos);
    CHECK(panel.find(u8"数据已载入并显示") != std::string::npos);
    CHECK(panel.find(u8"数据已载入，图层已隐藏") != std::string::npos);
    CHECK(panel.find(u8"分析结果已就绪；没有地图栅格") !=
          std::string::npos);
    CHECK(panel.find(u8"本次失败，保留上一次显示") !=
          std::string::npos);
    CHECK(panel.find(u8"渲染器无法显示") != std::string::npos);

    std::ifstream aiInput(std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/ai_ui.cpp");
    std::ostringstream aiBuffer;
    aiBuffer << aiInput.rdbuf();
    const std::string aiUi = aiBuffer.str();
    CHECK(aiInput.good() || aiInput.eof());
    CHECK(aiUi.find("_historyCollapsed(true)") != std::string::npos);
    CHECK(aiUi.find(u8"AI 地球助手") != std::string::npos);
    CHECK(aiUi.find(u8"发送") != std::string::npos);
    CHECK(aiUi.find(u8"已准备：") != std::string::npos);
    const size_t galleryBegin = aiUi.find(
        "// ---- ScienceEarth 分析模板：准备视角和参数，不自动提交 ----");
    const size_t galleryEnd = aiUi.find("// ---- 输入行 ----", galleryBegin);
    CHECK(galleryBegin != std::string::npos &&
          galleryEnd != std::string::npos && galleryBegin < galleryEnd);
    const std::string gallery = aiUi.substr(
        galleryBegin, galleryEnd - galleryBegin);
    CHECK(gallery.find("scienceEarthPromptExamples") != std::string::npos);
    CHECK(gallery.find("insertPromptSuggestion") != std::string::npos);
    CHECK(gallery.find("ImGui::BeginPopup") != std::string::npos);
    CHECK(gallery.find(u8"使用并定位") != std::string::npos);
    CHECK(gallery.find("navigateToLocation") != std::string::npos);
    CHECK(gallery.find("setByEye(") != std::string::npos);
    CHECK(gallery.find("DegreesToRadians") != std::string::npos);
    CHECK(gallery.find(u8"不会自动运行") != std::string::npos);
    CHECK(gallery.find("submit(") == std::string::npos);


    std::cout << "[OK] Earth control panel responsive layout\n";
    return 0;
}
