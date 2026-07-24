#define GL_SILENCE_DEPRECATION
#include <GL/glew.h>
#include <OpenGL/OpenGL.h>

#include "earth_ui_components.h"
#include "earth_ui_v2.h"
#include "ui_evidence_io.h"
#include "ui_card.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr int WIDTH = 1440;
constexpr int HEIGHT = 900;

void expect(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

class HeadlessCgl
{
public:
    bool initialize()
    {
        const CGLPixelFormatAttribute attributes[] = {
            kCGLPFAOpenGLProfile,
            static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_GL4_Core),
            kCGLPFAAccelerated,
            kCGLPFAColorSize, static_cast<CGLPixelFormatAttribute>(24),
            kCGLPFAAlphaSize, static_cast<CGLPixelFormatAttribute>(8),
            kCGLPFADepthSize, static_cast<CGLPixelFormatAttribute>(24),
            kCGLPFAStencilSize, static_cast<CGLPixelFormatAttribute>(8),
            static_cast<CGLPixelFormatAttribute>(0)};
        CGLPixelFormatObj format = nullptr;
        GLint formats = 0;
        if (CGLChoosePixelFormat(attributes, &format, &formats) != kCGLNoError ||
            !format)
            return false;
        const CGLError result = CGLCreateContext(format, nullptr, &_context);
        CGLDestroyPixelFormat(format);
        if (result != kCGLNoError || !_context) return false;
        if (CGLSetCurrentContext(_context) != kCGLNoError) return false;

        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) return false;
        // GLEW probes deprecated entry points in a core context and may leave
        // GL_INVALID_ENUM behind. Clear it before the actual UI assertions.
        while (glGetError() != GL_NO_ERROR) {}

        glGenFramebuffers(1, &_framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
        glGenTextures(1, &_color);
        glBindTexture(GL_TEXTURE_2D, _color);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, WIDTH, HEIGHT, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _color, 0);
        glGenRenderbuffers(1, &_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, _depth);
        glRenderbufferStorage(
            GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, WIDTH, HEIGHT);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
            GL_RENDERBUFFER, _depth);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
            GL_FRAMEBUFFER_COMPLETE;
    }

    ~HeadlessCgl()
    {
        if (_context)
        {
            CGLSetCurrentContext(_context);
            if (_depth) glDeleteRenderbuffers(1, &_depth);
            if (_color) glDeleteTextures(1, &_color);
            if (_framebuffer) glDeleteFramebuffers(1, &_framebuffer);
            CGLSetCurrentContext(nullptr);
            CGLDestroyContext(_context);
        }
    }

    void bind() const
    {
        CGLSetCurrentContext(_context);
        glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
        glViewport(0, 0, WIDTH, HEIGHT);
    }

private:
    CGLContextObj _context = nullptr;
    GLuint _framebuffer = 0;
    GLuint _color = 0;
    GLuint _depth = 0;
};

const char* moduleId(earthui::EarthUiModule module)
{
    switch (module)
    {
    case earthui::EarthUiModule::Explore: return "explore";
    case earthui::EarthUiModule::Layers: return "layers";
    case earthui::EarthUiModule::Science: return "science";
    case earthui::EarthUiModule::Live: return "live";
    case earthui::EarthUiModule::Satellites: return "satellites";
    case earthui::EarthUiModule::City3D: return "city3d";
    case earthui::EarthUiModule::Tasks: return "tasks";
    case earthui::EarthUiModule::Settings: return "settings";
    }
    return "unknown";
}

void drawContractContent(earthui::EarthUiModule module)
{
    const char* description = earthui::moduleContextLabel(module);
    if (!earthui::beginDrawerSection(
            moduleId(module), earthui::moduleLabel(module), description))
        return;

    switch (module)
    {
    case earthui::EarthUiModule::Explore:
    {
        float latitude = 24.9752f;
        float longitude = 102.0031f;
        ImGui::TextWrapped(u8"当前视角与导航");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputFloat(u8"##latitude", &latitude, 0.0f, 0.0f, "%.4f°");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputFloat(u8"##longitude", &longitude, 0.0f, 0.0f, "%.4f°");
        earthui::fullWidthButton(
            u8"飞到此处", earthui::StatusTone::Active);
        break;
    }
    case earthui::EarthUiModule::Layers:
    {
        static char filter[64] = "";
        bool imagery = true;
        bool roads = true;
        ImGui::InputTextWithHint(
            u8"##layer_filter", u8"搜索图层…", filter, sizeof(filter));
        ImGui::Checkbox(u8"卫星影像", &imagery);
        ImGui::Checkbox(u8"路网与地名", &roads);
        break;
    }
    case earthui::EarthUiModule::Science:
        earthui::drawStatusLine(
            earthui::StatusTone::Active, u8"科学工作台已连接");
        ImGui::TextWrapped(
            u8"数据源、位置、年份、方法与运行状态使用同一任务流。");
        earthui::fullWidthButton(
            u8"打开科学工作台", earthui::StatusTone::Active);
        break;
    case earthui::EarthUiModule::Live:
        earthui::drawStatusLine(
            earthui::StatusTone::Success, u8"实时数据正常");
        ImGui::TextWrapped(u8"天气、航班与事件按来源显示刷新时间。");
        earthui::fullWidthButton(
            u8"立即刷新", earthui::StatusTone::Neutral);
        break;
    case earthui::EarthUiModule::Satellites:
    {
        bool track = true;
        bool footprint = true;
        ImGui::Checkbox(u8"轨迹", &track);
        ImGui::Checkbox(u8"地面覆盖范围", &footprint);
        ImGui::TextWrapped(u8"选中卫星后显示过境、轨道与覆盖。");
        break;
    }
    case earthui::EarthUiModule::City3D:
    {
        float lod = 0.65f;
        ImGui::TextWrapped(u8"城市对象与地形保持同一空间基准。");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::SliderFloat(u8"##city_lod", &lod, 0.0f, 1.0f, u8"细节 %.2f");
        break;
    }
    case earthui::EarthUiModule::Tasks:
        earthui::drawStatusLine(
            earthui::StatusTone::Neutral, u8"当前没有运行中的任务");
        ImGui::TextWrapped(
            u8"运行中、队列、失败和历史在这里统一呈现。");
        break;
    case earthui::EarthUiModule::Settings:
        ImGui::TextWrapped(u8"界面、渲染和运行参数");
        earthui::fullWidthButton(
            u8"恢复默认设置", earthui::StatusTone::Neutral);
        earthui::fullWidthButton(
            u8"正常退出程序", earthui::StatusTone::Danger);
        break;
    }
    earthui::endDrawerSection();
}

void drawInsight(earthui::EarthUiModule module, earthui::CardStack& cards)
{
    if (module == earthui::EarthUiModule::Settings) return;
    earthui::Card card;
    card.id = std::string("gl-") + moduleId(module);
    card.title = u8"洞察透镜";
    card.style.chipLabel = earthui::moduleShortLabel(module);
    card.closable = false;
    card.drawBody = [module]() {
        ImGui::TextWrapped(
            u8"当前模块：%s", earthui::moduleLabel(module));
        ImGui::Separator();
        ImGui::TextWrapped(
            u8"这里呈现与地图选择绑定的结果、来源和状态，不覆盖左侧操作流程。");
    };
    cards.upsert(card);
    cards.draw();
}

bool writePpm(const std::filesystem::path& path,
              const std::vector<unsigned char>& rgba)
{
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) return false;
    std::fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (int y = HEIGHT - 1; y >= 0; --y)
    {
        for (int x = 0; x < WIDTH; ++x)
        {
            const std::size_t index =
                (static_cast<std::size_t>(y) * WIDTH + x) * 4;
            const unsigned char rgb[3] = {
                rgba[index], rgba[index + 1], rgba[index + 2]};
            std::fwrite(rgb, 1, sizeof(rgb), file);
        }
    }
    const bool ppmWritten = std::fclose(file) == 0;
    return ppmWritten &&
        ui_evidence::writePngSibling(path, rgba, WIDTH, HEIGHT);
}

std::array<unsigned char, 3> screenPixel(
    const std::vector<unsigned char>& rgba, int x, int y)
{
    const int readY = HEIGHT - 1 - y;
    const std::size_t index =
        (static_cast<std::size_t>(readY) * WIDTH + x) * 4;
    return {rgba[index], rgba[index + 1], rgba[index + 2]};
}

bool dark(const std::array<unsigned char, 3>& color)
{
    return color[0] < 80 && color[1] < 80 && color[2] < 80;
}

bool magenta(const std::array<unsigned char, 3>& color)
{
    return color[0] > 220 && color[1] < 30 && color[2] > 220;
}

void renderModule(HeadlessCgl& gl, earthui::EarthUiModule module,
                  const std::filesystem::path& evidenceDirectory)
{
    earthui::EarthUiShellState state;
    state.activeModule = module;
    state.drawerOpen = true;
    const earthui::EarthUiShellLayout layout =
        earthui::computeEarthUiShellLayout(
            static_cast<float>(WIDTH), static_cast<float>(HEIGHT), true);
    earthui::CardStack cards;
    // ImGui tab bars select newly-added tabs on the following frame. Render
    // two stable frames so the evidence captures the body, not a first-frame
    // empty shell that users never meaningfully interact with.
    for (int frame = 0; frame < 2; ++frame)
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();

        earthui::EarthUiTopBarData top;
        top.latitudeDeg = 24.9752;
        top.longitudeDeg = 102.0031;
        top.altitudeKm = 1336.7;
        top.scienceAvailable = true;
        earthui::drawEarthUiTopBar(layout, state, top);
        earthui::drawEarthUiModuleRail(layout, state);
        if (earthui::beginEarthUiModuleDrawer(layout, state))
        {
            drawContractContent(module);
            earthui::endEarthUiModuleDrawer();
        }
        earthui::drawEarthUiContextTray(layout, state);
        drawInsight(module, cards);

        ImGui::Render();
        gl.bind();
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glFinish();
    }

    std::vector<unsigned char> rgba(
        static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
    glReadPixels(
        0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    expect(dark(screenPixel(rgba, 20, 240)),
           std::string(moduleId(module)) +
               ": module rail is not a continuous dark surface");
    expect(dark(screenPixel(
               rgba, static_cast<int>(layout.navigationWidth) - 1, 240)) &&
               dark(screenPixel(
                   rgba, static_cast<int>(layout.navigationWidth) + 1, 240)),
           std::string(moduleId(module)) +
               ": rail and drawer contain a visible map-colored seam");
    expect(dark(screenPixel(rgba, 420, 20)),
           std::string(moduleId(module)) +
               ": top bar is missing or transparent");
    if (earthui::contextKindForModule(module) !=
        earthui::EarthUiContextKind::None)
    {
        expect(dark(screenPixel(
                   rgba, static_cast<int>(layout.contextX) + 20,
                   static_cast<int>(layout.contextY) + 20)),
               std::string(moduleId(module)) +
                   ": context tray is missing or transparent");
    }
    const int insightLeft = WIDTH - static_cast<int>(
        layout.outerGap + layout.insightWidth);
    const int mapCenterX = static_cast<int>(
        (layout.drawerX + layout.drawerWidth + insightLeft) * 0.5f);
    expect(magenta(screenPixel(rgba, mapCenterX, HEIGHT / 2)),
           std::string(moduleId(module)) +
               ": UI covers the reserved map canvas");

    std::size_t whitePixels = 0;
    std::size_t magentaPixels = 0;
    for (std::size_t i = 0; i < rgba.size(); i += 4)
    {
        if (rgba[i] > 245 && rgba[i + 1] > 245 && rgba[i + 2] > 245)
            ++whitePixels;
        if (rgba[i] > 220 && rgba[i + 1] < 30 && rgba[i + 2] > 220)
            ++magentaPixels;
    }
    expect(whitePixels < static_cast<std::size_t>(WIDTH * HEIGHT / 100),
           std::string(moduleId(module)) +
               ": a large white fallback surface entered the product shell");
    expect(magentaPixels > static_cast<std::size_t>(WIDTH * HEIGHT / 5),
           std::string(moduleId(module)) +
               ": the map no longer remains the primary canvas");

    const std::filesystem::path output =
        evidenceDirectory /
        (std::string("earth-ui-") + moduleId(module) + ".ppm");
    expect(writePpm(output, rgba),
           "could not write ImGui GL evidence: " + output.string());
}
}

int main()
{
    HeadlessCgl gl;
    expect(gl.initialize(), "headless CGL 4.1 context did not initialize");
    gl.bind();

    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<float>(WIDTH), static_cast<float>(HEIGHT));
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    const std::string fontPath =
        std::string(OSGVERSE_SOURCE_DIR) +
        "/assets/misc/LXGWFasmartGothic.otf";
    expect(io.Fonts->AddFontFromFileTTF(
               fontPath.c_str(), 20.0f, nullptr,
               io.Fonts->GetGlyphRangesChineseFull()) != nullptr,
           "product Chinese font did not load");
    earthui::applyEarthUiV2Theme();
    expect(ImGui_ImplOpenGL3_Init("#version 410 core"),
           "ImGui OpenGL3 backend did not initialize");

    const std::filesystem::path evidenceDirectory =
        std::filesystem::path(OSGSOL_UI_EVIDENCE_DIR) / "imgui-shell";
    std::filesystem::create_directories(evidenceDirectory);
    const std::array<earthui::EarthUiModule, 8> modules = {{
        earthui::EarthUiModule::Explore,
        earthui::EarthUiModule::Layers,
        earthui::EarthUiModule::Science,
        earthui::EarthUiModule::Live,
        earthui::EarthUiModule::Satellites,
        earthui::EarthUiModule::City3D,
        earthui::EarthUiModule::Tasks,
        earthui::EarthUiModule::Settings}};
    for (earthui::EarthUiModule module : modules)
        renderModule(gl, module, evidenceDirectory);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(context);
    std::cout << "[OK] Earth UI GL evidence rendered for 8 modules: "
              << evidenceDirectory << std::endl;
    return 0;
}
