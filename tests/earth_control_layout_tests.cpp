#include "earth_control_layout.h"

#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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
    // Retina 13/14-inch fullscreen: 2048x1152 backing pixels are exposed to
    // ImGui as roughly 1024x576 logical points. The control panel must leave
    // most of the globe visible and reserve the bottom strip for AI controls.
    const earthui::EarthControlPanelLayout compact =
        earthui::computeEarthControlPanelLayout(1024.0f, 576.0f);
    CHECK(nearlyEqual(compact.defaultWidth, 337.92f));
    CHECK(compact.defaultWidth <= 1024.0f * 0.34f);
    CHECK(compact.maxWidth <= 1024.0f * 0.34f);
    CHECK(compact.defaultHeight <= 576.0f * 0.72f + 0.01f);
    CHECK(compact.maxHeight <= 480.0f);
    CHECK(compact.maxHeight <= 576.0f - 20.0f - 76.0f);

    const earthui::ScienceWorkspaceLayout scienceCompact =
        earthui::computeScienceWorkspaceLayout(1024.0f, 576.0f, true);
    CHECK(scienceCompact.leftWidth <= 1024.0f * 0.34f);
    CHECK(scienceCompact.resultWidth <= 1024.0f * 0.32f);
    CHECK(scienceCompact.centerMapWidth >= 1024.0f * 0.30f);
    CHECK(scienceCompact.resultHeight <= 576.0f - 20.0f - 76.0f);

    const earthui::ScienceWorkspaceLayout scienceCollapsed =
        earthui::computeScienceWorkspaceLayout(1024.0f, 576.0f, false);
    CHECK(scienceCollapsed.resultWidth <= 44.0f);
    CHECK(scienceCollapsed.centerMapWidth > scienceCompact.centerMapWidth);

    // A large desktop should not make the panel grow with the viewport.
    const earthui::EarthControlPanelLayout large =
        earthui::computeEarthControlPanelLayout(1920.0f, 1080.0f);
    CHECK(nearlyEqual(large.defaultWidth, 380.0f));
    CHECK(nearlyEqual(large.maxWidth, 460.0f));
    CHECK(nearlyEqual(large.defaultHeight, 720.0f));
    CHECK(nearlyEqual(large.maxHeight, 760.0f));

    // Small windows still need usable, ordered constraints.
    const earthui::EarthControlPanelLayout small =
        earthui::computeEarthControlPanelLayout(640.0f, 360.0f);
    CHECK(small.minWidth <= small.defaultWidth);
    CHECK(small.defaultWidth <= small.maxWidth);
    CHECK(small.minHeight <= small.defaultHeight);
    CHECK(small.defaultHeight <= small.maxHeight);
    CHECK(small.maxWidth < 640.0f * 0.5f);
    CHECK(small.maxHeight <= 360.0f - 20.0f - 76.0f);

    // Keep the production window wired to this policy. In particular, do not
    // reintroduce content-driven auto-resize or disable its scrollbar.
    std::ifstream input(std::string(OSGVERSE_SOURCE_DIR) +
                        "/applications/earth_explorer/EarthControlUI.h");
    std::ostringstream sourceBuffer;
    sourceBuffer << input.rdbuf();
    const std::string source = sourceBuffer.str();
    const size_t setupBegin = source.find("computeEarthControlPanelLayout");
    const size_t setupEnd = source.find("// ---- 相机读数 ----", setupBegin);
    CHECK(input.good() || input.eof());
    CHECK(setupBegin != std::string::npos);
    CHECK(setupEnd != std::string::npos);
    const std::string setup = source.substr(setupBegin, setupEnd - setupBegin);
    CHECK(setup.find("SetNextWindowSizeConstraints") != std::string::npos);
    CHECK(setup.find("ImGuiCond_FirstUseEver") != std::string::npos);
    CHECK(setup.find("AlwaysAutoResize") == std::string::npos);
    CHECK(setup.find("NoScrollbar") == std::string::npos);

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
    CHECK(source.find("ImGui::PushTextWrapPos(0.0f)") != std::string::npos);
    CHECK(source.find("ImGui::PopTextWrapPos()") != std::string::npos);

    std::ifstream panelInput(std::string(OSGVERSE_SOURCE_DIR) +
        "/applications/earth_explorer/science_earth_panel.cpp");
    std::ostringstream panelBuffer;
    panelBuffer << panelInput.rdbuf();
    const std::string panel = panelBuffer.str();
    CHECK(panelInput.good() || panelInput.eof());
    CHECK(panel.find("ImGui::TextWrapped") != std::string::npos);
    CHECK(panel.find("ImGui::PlotLines") != std::string::npos);
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

    std::cout << "[OK] Earth control panel responsive layout\n";
    return 0;
}
