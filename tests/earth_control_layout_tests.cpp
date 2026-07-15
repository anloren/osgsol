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
    CHECK(compact.maxWidth <= 1024.0f * 0.42f);
    CHECK(compact.defaultHeight <= 576.0f * 0.72f + 0.01f);
    CHECK(compact.maxHeight <= 480.0f);
    CHECK(compact.maxHeight <= 576.0f - 20.0f - 76.0f);

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

    std::cout << "[OK] Earth control panel responsive layout\n";
    return 0;
}
