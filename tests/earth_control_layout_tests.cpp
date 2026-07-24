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
    CHECK(nearlyEqual(shell.drawerX, shell.navigationWidth));
    CHECK(nearlyEqual(shell.drawerY, shell.topBarHeight));
    CHECK(shell.drawerX + shell.drawerWidth <
          1024.0f - shell.outerGap - shell.insightWidth);
    CHECK(shell.drawerY + shell.drawerHeight <= shell.commandY + 0.01f);
    CHECK(shell.insightTop + shell.insightHeight <= shell.commandY + 0.01f);
    CHECK(shell.commandY + shell.commandHeight < shell.contextY);
    CHECK(nearlyEqual(shell.contextY + shell.contextHeight +
                      shell.statusHeight, 576.0f));
    CHECK(shell.contextX > shell.navigationWidth);
    CHECK(shell.contextWidth < 1024.0f - shell.navigationWidth);
    const float insightLeft =
        1024.0f - shell.outerGap - shell.insightWidth;
    CHECK(shell.commandX >=
          shell.drawerX + shell.drawerWidth + shell.outerGap);
    CHECK(shell.commandX + shell.commandWidth <=
          insightLeft - shell.outerGap);

    const earthui::EarthUiShellLayout shellClosed =
        earthui::computeEarthUiShellLayout(1024.0f, 576.0f, false);
    CHECK(nearlyEqual(shellClosed.drawerWidth, 0.0f));
    CHECK(shellClosed.commandX < shell.commandX);
    CHECK(shellClosed.commandWidth > shell.commandWidth);
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

    const earthui::ScienceWorkbenchLayout scienceCompact =
        earthui::computeScienceWorkbenchLayout(1024.0f, 576.0f);
    CHECK(scienceCompact.composerWidth >= 340.0f);
    CHECK(scienceCompact.composerWidth <= 380.0f);
    CHECK(scienceCompact.mapWidth >= 480.0f);
    CHECK(scienceCompact.compact);
    CHECK(scienceCompact.reportWidth <= 680.0f);
    CHECK(scienceCompact.reportHeight <= 500.0f);
    CHECK(scienceCompact.reportX >= 16.0f);
    CHECK(scienceCompact.reportY >= shell.topBarHeight);
    CHECK(scienceCompact.reportY + scienceCompact.reportHeight <=
          scienceCompact.aiComposerY - 16.0f);

    const earthui::ScienceWorkbenchLayout scienceLarge =
        earthui::computeScienceWorkbenchLayout(2048.0f, 1152.0f);
    CHECK(!scienceLarge.compact);
    CHECK(nearlyEqual(scienceLarge.reportWidth, 900.0f));
    CHECK(nearlyEqual(scienceLarge.reportHeight, 680.0f));
    CHECK(scienceLarge.mapWidth >= 480.0f);

    const earthui::AiCommandRowLayout normalCommand =
        earthui::computeAiCommandRowLayout(
            720.0f, 8.0f, 42.0f, true, false);
    CHECK(!normalCommand.actionsOnNextLine);
    CHECK(normalCommand.actionWidth > 140.0f);
    CHECK(normalCommand.inputWidth + normalCommand.actionWidth <= 720.01f);
    const earthui::AiCommandRowLayout videoCommand =
        earthui::computeAiCommandRowLayout(
            330.0f, 8.0f, 42.0f, true, true);
    CHECK(videoCommand.actionsOnNextLine);
    CHECK(nearlyEqual(videoCommand.inputWidth, 330.0f));
    CHECK(videoCommand.actionWidth > normalCommand.actionWidth);

    const earthui::BoundedWindowLayout compactModal =
        earthui::computeCenteredModalLayout(
            640.0f, 360.0f, 480.0f, 560.0f);
    CHECK(compactModal.width <= 640.0f - 24.0f);
    CHECK(compactModal.maxHeight <= 360.0f - 24.0f);
    CHECK(nearlyEqual(compactModal.x, 320.0f));
    CHECK(nearlyEqual(compactModal.y, 180.0f));

    const earthui::MapToastLayout compactToast =
        earthui::computeMapToastLayout(1024.0f, 576.0f, true);
    CHECK(compactToast.x >=
          shell.navigationWidth + std::max(shell.drawerWidth, 340.0f) +
              shell.outerGap);
    CHECK(compactToast.x + compactToast.width <=
          1024.0f - shell.insightWidth - shell.outerGap);
    CHECK(compactToast.y >= shell.topBarHeight);

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
    const std::vector<std::string> drawerSections = {
        "\"camera\", u8\"相机\"", "\"sun\", u8\"太阳与光照\"",
        "\"render\", u8\"渲染\"", "\"layer_catalog\", u8\"图层目录\"",
        "\"go_to\", u8\"跳转\"", "\"bookmarks\", u8\"书签与巡游\"",
        "\"tasks\", u8\"任务与状态\"", "\"settings\", u8\"设置\""};
    for (const std::string& section : drawerSections)
        CHECK(source.find(section) != std::string::npos);
    CHECK(source.find("computeMapToastLayout") != std::string::npos);
    CHECK(source.find("osg::Vec4(0.302f") == std::string::npos);
    CHECK(source.find("osg::Vec4(0.208f") == std::string::npos);
    CHECK(source.find("osg::Vec4(1.0f, 0.824f") == std::string::npos);

    // RmlUi must own the outer post-draw callback so it can initialize on the
    // first rendered frame and then invoke the legacy ImGui shell as its
    // chained callback. ImGui's renderer does not forward callbacks attached
    // after it, which otherwise leaves the product workbench permanently in
    // the legacy fallback without an initialization error.
    std::ifstream mainInput(std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/earth_main.cpp");
    std::ostringstream mainBuffer;
    mainBuffer << mainInput.rdbuf();
    const std::string mainSource = mainBuffer.str();
    CHECK(mainInput.good() || mainInput.eof());
    const size_t rmlAttach = mainSource.find("productUiRuntime.attach(");
    const size_t imguiAttach = mainSource.find("imgui->addToView(");
    CHECK(rmlAttach != std::string::npos);
    CHECK(imguiAttach != std::string::npos);
    CHECK(rmlAttach < imguiAttach);
    CHECK(mainSource.find(
        "contextCaptureCallback->setup(cameras[3], 2);") !=
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
    // Compact/Retina top bars keep actions on the first row at an explicit
    // vertical position and clip telemetry before the reserved action area.
    CHECK(shellSource.find("ImGui::PushClipRect") != std::string::npos);
    CHECK(shellSource.find(
        "ImGui::SetCursorPos(ImVec2(actionX, actionY))") !=
        std::string::npos);
    CHECK(shellSource.find(
        "if (ImGui::GetCursorPosX() < cursorRight)") ==
        std::string::npos);

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
    CHECK(aiUi.find(
        "const float contentWidth = ImGui::GetContentRegionAvail().x;") !=
          std::string::npos);
    CHECK(aiUi.find("actionsOnNextLine") != std::string::npos);
    CHECK(aiUi.find("computeAiCommandRowLayout") != std::string::npos);
    CHECK(aiUi.find(
        "const earthui::EarthUiShellLayout& shell") !=
          std::string::npos);
    CHECK(aiUi.find("computeEarthUiShellLayout(") ==
          std::string::npos);
    CHECK(aiUi.find("computeCenteredModalLayout") != std::string::npos);
    CHECK(aiUi.find("SetNextWindowSize(ImVec2(420.0f") ==
          std::string::npos);
    CHECK(aiUi.find("const float actionWidth = core ? 184.0f") ==
          std::string::npos);

    std::ifstream cardInput(std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/ui_card.h");
    std::ostringstream cardBuffer;
    cardBuffer << cardInput.rdbuf();
    const std::string cards = cardBuffer.str();
    CHECK(cardInput.good() || cardInput.eof());
    CHECK(cards.find(u8"洞察透镜###earth_insight_lens") !=
          std::string::npos);
    CHECK(cards.find("ImGui::BeginTabBar") != std::string::npos);
    CHECK(cards.find("ImGuiWindowFlags_HorizontalScrollbar") ==
          std::string::npos);
    CHECK(cards.find("ImGui::TextWrapped(") != std::string::npos);

    // Compact action groups must wrap according to available width instead of
    // assuming every translated label can remain on one row.
    const size_t bookmarksBegin = source.find(
        "\"bookmarks\", u8\"书签与巡游\"");
    const size_t settingsBegin = source.find(
        "// ---- 设置", bookmarksBegin);
    CHECK(bookmarksBegin != std::string::npos &&
          settingsBegin != std::string::npos &&
          bookmarksBegin < settingsBegin);
    const std::string bookmarks = source.substr(
        bookmarksBegin, settingsBegin - bookmarksBegin);
    CHECK(countOccurrences(bookmarks, "continueRowIfFits(") >= 3);
    const size_t feedBegin = source.find("card.id = \"feed_detail\"");
    const size_t tickerBegin = source.find(
        "// T8:右上角事件流卡", feedBegin);
    CHECK(feedBegin != std::string::npos &&
          tickerBegin != std::string::npos && feedBegin < tickerBegin);
    CHECK(source.substr(feedBegin, tickerBegin - feedBegin).find(
              "continueRowIfFits(") != std::string::npos);

    std::ifstream tokenInput(std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/earth_ui_tokens.h");
    std::ostringstream tokenBuffer;
    tokenBuffer << tokenInput.rdbuf();
    const std::string tokens = tokenBuffer.str();
    CHECK(tokenInput.good() || tokenInput.eof());
    CHECK(tokens.find("colorU32") != std::string::npos);
    std::ifstream chartInput(std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/earth_ui_chart.h");
    std::ostringstream chartBuffer;
    chartBuffer << chartInput.rdbuf();
    const std::string chart = chartBuffer.str();
    CHECK(chartInput.good() || chartInput.eof());
    CHECK(chart.find("IM_COL32(") == std::string::npos);

    std::ifstream tickerInput(std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/event_ticker.h");
    std::ostringstream tickerBuffer;
    tickerBuffer << tickerInput.rdbuf();
    const std::string ticker = tickerBuffer.str();
    CHECK(tickerInput.good() || tickerInput.eof());
    CHECK(ticker.find("ImGuiStyleVar_WindowRounding, 0.0f") !=
          std::string::npos);
    CHECK(ticker.find("ImGui::TextWrapped(\"%s\", e.title.c_str())") !=
          std::string::npos);

    std::cout << "[OK] Earth control panel responsive layout\n";
    return 0;
}
