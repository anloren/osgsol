#define GL_SILENCE_DEPRECATION
#include <GL/glew.h>
#include <OpenGL/OpenGL.h>

#include "EarthControlUI.h"
#include "ai_prompts.h"
#include "ui_evidence_io.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_internal.h>

#include <osg/Math>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// This target renders the production EarthControlUI entry point, but keeps all
// network and media owners absent. Narrow no-op definitions satisfy methods
// referenced by the UI translation unit without constructing workers, opening
// files, launching AppKit, or touching the packaged Desktop application.
namespace earthai
{
bool g_productUiBusy = false;
std::vector<std::string> g_productUiSubmissions;
std::vector<ChatEntry> g_productUiTranscript = {
    {ChatEntry::USER, u8"比较当前区域的农业气候与地表变化。"},
    {ChatEntry::ASSISTANT, u8"已准备数据源与空间范围，等待提交。"}};

AIChatCore::AIChatCore(LLMProvider* provider, ToolRegistry* registry)
    : _provider(provider), _registry(registry)
{
}

AIChatCore::~AIChatCore() {}

bool AIChatCore::busy() const
{
    return g_productUiBusy;
}

void AIChatCore::submit(const std::string& text)
{
    g_productUiSubmissions.push_back(text);
}

std::vector<ChatEntry> AIChatCore::transcript() const
{
    return g_productUiTranscript;
}

MediaManager::VideoUiSnapshot MediaManager::videoUiSnapshot() const
{
    return VideoUiSnapshot();
}
}

#if defined(__APPLE__)
namespace earthime
{
void setInputRect(float, float, float, float) {}
}
#endif

namespace earthfeed
{
namespace
{
FeedSelection g_selection;
std::vector<TickerEvent> g_events;
}

FeedSelection currentFeedSelection()
{
    return g_selection;
}

void clearFeedSelection()
{
    g_selection = FeedSelection();
}

std::vector<TickerEvent> collectRecentEvents(size_t)
{
    return g_events;
}

void feedHealth(int& okCount, int& enabledCount)
{
    okCount = 4;
    enabledCount = 5;
}

void setProductTestSelection(const FeedSelection& selection)
{
    g_selection = selection;
}

void setProductTestEvents(const std::vector<TickerEvent>& events)
{
    g_events = events;
}
}

namespace
{
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
        while (glGetError() != GL_NO_ERROR) {}

        glGenFramebuffers(1, &_framebuffer);
        glGenTextures(1, &_color);
        glGenRenderbuffers(1, &_depth);
        return true;
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

    bool resize(int width, int height)
    {
        _width = width;
        _height = height;
        CGLSetCurrentContext(_context);
        glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
        glBindTexture(GL_TEXTURE_2D, _color);
        glTexImage2D(
            GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, _color, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, _depth);
        glRenderbufferStorage(
            GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
            GL_RENDERBUFFER, _depth);
        return glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
            GL_FRAMEBUFFER_COMPLETE;
    }

    void bind() const
    {
        CGLSetCurrentContext(_context);
        glBindFramebuffer(GL_FRAMEBUFFER, _framebuffer);
        glViewport(0, 0, _width, _height);
    }

private:
    CGLContextObj _context = nullptr;
    GLuint _framebuffer = 0;
    GLuint _color = 0;
    GLuint _depth = 0;
    int _width = 1;
    int _height = 1;
};

class ProductFlightLayer : public FlightLayer
{
public:
    void setEnabled(bool on) override { _enabled = on; }
    bool isEnabled() const override { return _enabled; }
    void setViewBBox(double, double, double, double) override {}
    FlightInfo getSelected() const override { return selected; }
    void clearSelected() override { selected.valid = false; }
    std::string summaryJson() const override { return "{}"; }

    FlightInfo selected;
private:
    bool _enabled = true;
};

class ProductSatelliteLayer : public SatelliteLayer
{
public:
    void setCategoryEnabled(SatCategory, bool) override {}
    bool isCategoryEnabled(SatCategory) const override { return true; }
    void selectByNoradId(int) override {}
    void clearSelected() override { selected.valid = false; }
    SatelliteInfo getSelected() const override { return selected; }
    std::string summaryJson() const override { return "{}"; }
    std::string fetchErrorText(SatCategory) const override { return ""; }

    SatelliteInfo selected;
};

class ProductShipLayer : public ShipLayer
{
public:
    void setEnabled(bool on) override { _enabled = on; }
    bool isEnabled() const override { return _enabled; }
    void setViewState(double, double, double, double, double) override {}
    ShipInfo getSelected() const override { return selected; }
    void clearSelected() override { selected.valid = false; }
    std::string summaryJson() const override { return "{}"; }
    std::string statusText() const override { return u8"已连接 128 艘"; }

    ShipInfo selected;
private:
    bool _enabled = true;
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

picojson::value chartSpec(
    const char* type, const char* title, const char* unit,
    const char* description,
    const std::vector<std::string>& labels,
    const std::vector<double>& values)
{
    picojson::array labelValues;
    picojson::array numericValues;
    for (const std::string& label : labels)
        labelValues.push_back(picojson::value(label));
    for (double value : values)
        numericValues.push_back(picojson::value(value));

    picojson::object object;
    object["type"] = picojson::value(type);
    object["title"] = picojson::value(title);
    object["unit"] = picojson::value(unit);
    object["description"] = picojson::value(description);
    object["labels"] = picojson::value(labelValues);
    object["values"] = picojson::value(numericValues);
    return picojson::value(object);
}

void addProductLayers(LayerManager& manager)
{
    const std::array<const char*, 5> groups = {{
        u8"底图 / 标注",
        u8"实时数据 / Live",
        u8"卫星 Satellites",
        u8"三维城市 / 3D City",
        u8"科学数据 / Science"}};
    int serial = 0;
    for (const char* group : groups)
    {
        for (int index = 0; index < 8; ++index)
        {
            OverlayLayer layer;
            layer.id = "product-ui-" + std::to_string(serial);
            layer.displayName =
                u8"验收数据层 " + std::to_string(index + 1) +
                u8" · 超长来源名称与当前状态";
            layer.group = group;
            layer.subtitle =
                u8"来源、采集时间、空间分辨率与许可说明应完整换行，"
                u8"不能越过面板或滚动条边缘。";
            layer.enabled = index < 4;
            layer.hasOpacity = (index % 2) == 0;
            layer.opacity = 0.72f;
            if (serial == 0)
            {
                layer.maxDetailNote = u8"原始分辨率约 250 m";
                layer.opaque = true;
            }
            layer.apply = [](const OverlayLayer&) {};
            layer.fetchStatus = [index](std::string& error) {
                if (index % 4 == 0) return 0;
                if (index % 4 == 1) return 1;
                if (index % 4 == 2)
                {
                    error = u8"上游服务暂时不可用，稍后自动重试。";
                    return 2;
                }
                return 3;
            };
            manager.add(layer);
            ++serial;
        }
    }
    manager.addPreset({u8"干净", {"product-ui-0"}});
    manager.addPreset({u8"灾害与天气", {"product-ui-8", "product-ui-9"}});
    manager.addPreset({u8"科学研究", {"product-ui-32", "product-ui-33"}});
}

void configureSelection(earthui::EarthUiModule module,
                        EarthControlUI& ui,
                        ProductFlightLayer& flights,
                        ProductSatelliteLayer& satellites,
                        ProductShipLayer& ships)
{
    ui._flight = nullptr;
    ui._satellites = nullptr;
    ui._ships = nullptr;
    earthfeed::FeedSelection feed;
    switch (module)
    {
    case earthui::EarthUiModule::Explore:
        flights.selected.valid = true;
        ui._flight = &flights;
        break;
    case earthui::EarthUiModule::Live:
        ships.selected.valid = true;
        ui._ships = &ships;
        break;
    case earthui::EarthUiModule::Satellites:
        satellites.selected.valid = true;
        ui._satellites = &satellites;
        break;
    case earthui::EarthUiModule::Layers:
    case earthui::EarthUiModule::City3D:
    case earthui::EarthUiModule::Tasks:
    case earthui::EarthUiModule::Settings:
        feed.valid = true;
        feed.title =
            u8"跨区域长标题：农业、气候、灾害和基础设施联合观测结果";
        feed.detail =
            u8"该详情用于验证真实业务长文、来源、链接和操作在洞察透镜中"
            u8"完整换行，不遮挡地图，也不会把正文裁成一条标题。";
        feed.url = "https://example.invalid/evidence";
        feed.sourceId = "gdelt";
        break;
    case earthui::EarthUiModule::Science:
        break;
    }
    earthfeed::setProductTestSelection(feed);
}

bool overlaps(const ImGuiWindow* first, const ImGuiWindow* second)
{
    if (!first || !second) return false;
    const ImVec2 firstMax(
        first->Pos.x + first->Size.x, first->Pos.y + first->Size.y);
    const ImVec2 secondMax(
        second->Pos.x + second->Size.x, second->Pos.y + second->Size.y);
    return first->Pos.x < secondMax.x - 0.5f &&
        firstMax.x > second->Pos.x + 0.5f &&
        first->Pos.y < secondMax.y - 0.5f &&
        firstMax.y > second->Pos.y + 0.5f;
}

ImGuiWindow* activeNonModalPopup()
{
    ImGuiContext& context = *ImGui::GetCurrentContext();
    for (ImGuiWindow* window : context.Windows)
    {
        if (!window || !window->Active || window->Hidden)
            continue;
        if ((window->Flags & ImGuiWindowFlags_Popup) == 0 ||
            (window->Flags & ImGuiWindowFlags_Modal) != 0)
            continue;
        return window;
    }
    return nullptr;
}

void inspectProductionWindows(int width, int height,
                              earthui::EarthUiModule module)
{
    ImGuiContext& context = *ImGui::GetCurrentContext();
    for (ImGuiWindow* window : context.Windows)
    {
        if (!window || !window->Active || window->Hidden) continue;
        const std::string name = window->Name ? window->Name : "";
        expect(window->Pos.x >= -0.5f && window->Pos.y >= -0.5f,
               std::string(moduleId(module)) +
                   ": production window starts outside viewport: " + name);
        if (window->Pos.x + window->Size.x > width + 0.5f ||
            window->Pos.y + window->Size.y > height + 0.5f)
        {
            std::cerr << "viewport-overflow: viewport=" << width << "x"
                      << height << " window=" << name
                      << " pos=" << window->Pos.x << "," << window->Pos.y
                      << " size=" << window->Size.x << "x"
                      << window->Size.y << std::endl;
        }
        expect(window->Pos.x + window->Size.x <= width + 0.5f &&
                   window->Pos.y + window->Size.y <= height + 0.5f,
               std::string(moduleId(module)) +
                   ": production window ends outside viewport: " + name);
        // ImGui can report up to one vertical-scrollbar width of synthetic
        // horizontal range when the vertical bar appears after content
        // measurement. Reject a real horizontal bar or anything larger; do
        // not mistake that internal reflow allowance for clipped controls.
        const float allowedHorizontalRange = window->ScrollbarY
            ? ImGui::GetStyle().ScrollbarSize + 0.5f : 0.5f;
        if (window->ScrollbarX ||
            window->ScrollMax.x > allowedHorizontalRange)
        {
            std::cerr << "horizontal-overflow: viewport=" << width << "x"
                      << height << " window=" << name
                      << " size=" << window->Size.x << "x" << window->Size.y
                      << " content=" << window->ContentSize.x << "x"
                      << window->ContentSize.y
                      << " scrollbarX=" << window->ScrollbarX
                      << " scrollbarY=" << window->ScrollbarY
                      << " scrollMaxX=" << window->ScrollMax.x << std::endl;
        }
        expect(!window->ScrollbarX &&
                   window->ScrollMax.x <= allowedHorizontalRange,
               std::string(moduleId(module)) +
                   ": production window has horizontal overflow: " + name);
    }

    ImGuiWindow* drawer = ImGui::FindWindowByName("##earth_ui_v2_drawer");
    ImGuiWindow* command = ImGui::FindWindowByName(u8"AI 对话条");
    ImGuiWindow* contextTray =
        ImGui::FindWindowByName("##earth_ui_v2_context");
    ImGuiWindow* insight =
        ImGui::FindWindowByName(u8"洞察透镜###earth_insight_lens");
    expect(drawer != nullptr,
           std::string(moduleId(module)) + ": production drawer is absent");
    expect(command != nullptr,
           std::string(moduleId(module)) + ": production AI command is absent");
    if (overlaps(drawer, command))
    {
        std::cerr << "geometry: viewport=" << width << "x" << height
                  << " drawer=(" << drawer->Pos.x << "," << drawer->Pos.y
                  << " " << drawer->Size.x << "x" << drawer->Size.y << ")"
                  << " command=(" << command->Pos.x << "," << command->Pos.y
                  << " " << command->Size.x << "x" << command->Size.y << ")"
                  << std::endl;
    }
    expect(!overlaps(drawer, command),
           std::string(moduleId(module)) +
               ": production drawer overlaps the AI command bar");
    expect(!overlaps(command, contextTray),
           std::string(moduleId(module)) +
               ": AI command bar overlaps the context tray");
    expect(!overlaps(command, insight),
           std::string(moduleId(module)) +
               ": AI command bar overlaps the Insight Lens");
}

bool writePpm(const std::filesystem::path& path,
              const std::vector<unsigned char>& rgba,
              int width, int height)
{
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) return false;
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; --y)
    {
        for (int x = 0; x < width; ++x)
        {
            const std::size_t index =
                (static_cast<std::size_t>(y) * width + x) * 4;
            const unsigned char rgb[3] = {
                rgba[index], rgba[index + 1], rgba[index + 2]};
            std::fwrite(rgb, 1, sizeof(rgb), file);
        }
    }
    const bool ppmWritten = std::fclose(file) == 0;
    return ppmWritten &&
        ui_evidence::writePngSibling(path, rgba, width, height);
}

std::array<unsigned char, 3> screenPixel(
    const std::vector<unsigned char>& rgba,
    int width, int height, int x, int y)
{
    const int readY = height - 1 - y;
    const std::size_t index =
        (static_cast<std::size_t>(readY) * width + x) * 4;
    return {rgba[index], rgba[index + 1], rgba[index + 2]};
}

bool dark(const std::array<unsigned char, 3>& color)
{
    return color[0] < 90 && color[1] < 90 && color[2] < 90;
}

bool magenta(const std::array<unsigned char, 3>& color)
{
    return color[0] > 220 && color[1] < 30 && color[2] > 220;
}

void renderProductionModule(
    HeadlessCgl& gl, EarthControlUI& ui,
    earthui::EarthUiModule module,
    int width, int height,
    const std::filesystem::path& evidenceDirectory)
{
    expect(gl.resize(width, height), "could not resize headless GL target");
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width),
                            static_cast<float>(height));
    ui._uiV2.activeModule = module;
    ui._uiV2.drawerOpen = true;

    const auto renderFrame = [&]() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        ui.runInternal(nullptr);
        ImGui::Render();
        gl.bind();
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glFinish();
    };

    // Auto-sized windows receive their pivoted final position after one
    // measured frame. Inspect the stable production geometry, not ImGui's
    // transient first-frame 60,60 placeholder.
    for (int frame = 0; frame < 3; ++frame) renderFrame();
    inspectProductionWindows(width, height, module);

    // The user's original panel regression was not only "can scroll down" but
    // "can return to the top". Exercise the real production drawer both ways
    // with its long layer/camera content. Geometry-only mocks do not cover
    // ImGui's retained scroll target and scrollbar reflow.
    ImGuiWindow* drawer = ImGui::FindWindowByName("##earth_ui_v2_drawer");
    expect(drawer != nullptr,
           std::string(moduleId(module)) + ": scroll test drawer is absent");
    if (module == earthui::EarthUiModule::Explore ||
        module == earthui::EarthUiModule::Layers)
    {
        expect(drawer->ScrollMax.y > 1.0f,
               std::string(moduleId(module)) +
                   ": production long drawer has no vertical scroll range");
    }
    if (drawer->ScrollMax.y > 1.0f)
    {
        const float scrollMaximum = drawer->ScrollMax.y;
        ImGui::SetScrollY(drawer, scrollMaximum);
        renderFrame();
        renderFrame();
        drawer = ImGui::FindWindowByName("##earth_ui_v2_drawer");
        expect(drawer && drawer->Scroll.y >= scrollMaximum - 1.0f,
               std::string(moduleId(module)) +
                   ": production drawer cannot scroll to the bottom");

        ImGui::SetScrollY(drawer, 0.0f);
        renderFrame();
        renderFrame();
        drawer = ImGui::FindWindowByName("##earth_ui_v2_drawer");
        expect(drawer && drawer->Scroll.y <= 1.0f,
               std::string(moduleId(module)) +
                   ": production drawer cannot scroll back to the top");
        inspectProductionWindows(width, height, module);
    }

    std::vector<unsigned char> rgba(
        static_cast<std::size_t>(width) * height * 4);
    glReadPixels(
        0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    const earthui::EarthUiShellLayout shell =
        earthui::computeEarthUiShellLayout(
            static_cast<float>(width), static_cast<float>(height), true);
    expect(dark(screenPixel(rgba, width, height, 20, 240)),
           std::string(moduleId(module)) +
               ": production rail is not a continuous dark surface");
    expect(dark(screenPixel(
               rgba, width, height,
               static_cast<int>(shell.navigationWidth) - 1, 240)) &&
               dark(screenPixel(
                   rgba, width, height,
                   static_cast<int>(shell.navigationWidth) + 1, 240)),
           std::string(moduleId(module)) +
               ": production rail and drawer contain a visible seam");

    const int insightLeft =
        width - static_cast<int>(shell.outerGap + shell.insightWidth);
    const int mapCenterX = std::max(
        static_cast<int>(shell.drawerX + shell.drawerWidth + 4.0f),
        static_cast<int>(
            (shell.drawerX + shell.drawerWidth + insightLeft) * 0.5f));
    expect(magenta(screenPixel(
               rgba, width, height, mapCenterX, height / 2)),
           std::string(moduleId(module)) +
               ": production UI covers the reserved map canvas");

    std::size_t whitePixels = 0;
    std::size_t magentaPixels = 0;
    for (std::size_t index = 0; index < rgba.size(); index += 4)
    {
        if (rgba[index] > 245 && rgba[index + 1] > 245 &&
            rgba[index + 2] > 245)
            ++whitePixels;
        if (rgba[index] > 220 && rgba[index + 1] < 30 &&
            rgba[index + 2] > 220)
            ++magentaPixels;
    }
    expect(whitePixels <
               static_cast<std::size_t>(width * height / 100),
           std::string(moduleId(module)) +
               ": a white fallback entered the production UI");
    expect(magentaPixels >
               static_cast<std::size_t>(width * height / 10),
           std::string(moduleId(module)) +
               ": production UI no longer leaves enough map visible");

    const std::filesystem::path output =
        evidenceDirectory /
        (std::to_string(width) + "x" + std::to_string(height) + "-" +
         moduleId(module) + ".ppm");
    expect(writePpm(output, rgba, width, height),
           "could not write production UI GL evidence: " + output.string());
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
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    const std::string fontPath =
        std::string(OSGVERSE_SOURCE_DIR) +
        "/assets/misc/LXGWFasmartGothic.otf";
    expect(io.Fonts->AddFontFromFileTTF(
               fontPath.c_str(), 20.0f, nullptr,
               io.Fonts->GetGlyphRangesChineseFull()) != nullptr,
           "product Chinese font did not load");
    expect(ImGui_ImplOpenGL3_Init("#version 410 core"),
           "ImGui OpenGL3 backend did not initialize");

    osg::ref_ptr<osgVerse::EarthManipulator> manipulator =
        new osgVerse::EarthManipulator;
    manipulator->setByEye(
        osg::DegreesToRadians(24.9752),
        osg::DegreesToRadians(102.0031),
        1336700.0);
    osgVerse::EarthAtmosphereOcean earth;
    earth.commonUniforms["WorldSunDir"] =
        new osg::Uniform(
            "WorldSunDir", osg::Vec3(-1.0f, 0.0f, 0.0f));
    earth.commonUniforms["GlobalOpaque"] =
        new osg::Uniform("GlobalOpaque", 1.0f);
    earth.commonUniforms["OceanOpaque"] =
        new osg::Uniform("OceanOpaque", 1.0f);
    earth.commonUniforms["HdrExposure"] =
        new osg::Uniform("HdrExposure", 0.25f);
    osgViewer::Viewer viewer;
    osg::ref_ptr<osg::FrameStamp> frameStamp = new osg::FrameStamp;
    frameStamp->setFrameNumber(500);
    viewer.setFrameStamp(frameStamp.get());
    EarthControlUI productUi(manipulator.get(), &earth, &viewer);
    productUi._alwaysDay = false;
    productUi._realTimeSun = false;
    productUi._exposureAuto = false;

    LayerManager layers;
    addProductLayers(layers);
    productUi._layers = &layers;

    earthai::AIChatCore core(nullptr, nullptr);
    AIChatUI command;
    productUi._aiCore = &core;
    productUi._aiUI = &command;

    ProductFlightLayer flights;
    flights.selected.callsign =
        u8"OSGSOL-长呼号-跨区域科学与实时联动验证";
    flights.selected.country =
        u8"国家或地区名称用于验证长文换行";
    flights.selected.lat = 24.9752;
    flights.selected.lon = 102.0031;
    flights.selected.altM = 10900.0;
    flights.selected.velMS = 241.0;
    flights.selected.headingDeg = 128.0;

    ProductSatelliteLayer satellites;
    satellites.selected.name =
        u8"Sentinel-2A / 科学与卫星轨迹联合验证";
    satellites.selected.noradId = 40697;
    satellites.selected.category = SatCategory::Weather;
    satellites.selected.latDeg = 24.9752;
    satellites.selected.lonDeg = 102.0031;
    satellites.selected.altKm = 786.0;
    satellites.selected.speedKmS = 7.46;

    ProductShipLayer ships;
    ships.selected.name =
        u8"长江农业气候与航运联合观测验证船";
    ships.selected.mmsi = 413000001;
    ships.selected.lat = 24.9752;
    ships.selected.lon = 102.0031;
    ships.selected.sogKn = 13.5;
    ships.selected.cogDeg = 205.0;
    ships.selected.ageSec = 18.0;

    const std::filesystem::path evidenceDirectory =
        std::filesystem::path(OSGSOL_UI_EVIDENCE_DIR) / "product-shell";
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
    const std::array<std::array<int, 2>, 3> viewports = {{
        {{1024, 576}},
        {{1440, 900}},
        {{2048, 1152}}}};
    for (const auto& viewport : viewports)
    {
        for (earthui::EarthUiModule module : modules)
        {
            configureSelection(
                module, productUi, flights, satellites, ships);
            std::snprintf(
                productUi._layerFilter,
                sizeof(productUi._layerFilter), "%s", u8"验收");
            renderProductionModule(
                gl, productUi, module,
                viewport[0], viewport[1], evidenceDirectory);
        }
    }

    // Hidden AI surfaces are part of the formal product, not optional mockups.
    // Exercise them through the real ImGui mouse path and keep their geometry
    // under the same viewport/overflow gates as the eight module shells.
    const int auditWidth = 1440;
    const int auditHeight = 900;
    expect(gl.resize(auditWidth, auditHeight),
           "could not resize GL target for AI surface audit");
    io.DisplaySize = ImVec2(
        static_cast<float>(auditWidth), static_cast<float>(auditHeight));
    productUi._uiV2.activeModule = earthui::EarthUiModule::Science;
    productUi._uiV2.drawerOpen = true;
    configureSelection(
        earthui::EarthUiModule::Science,
        productUi, flights, satellites, ships);
    earthai::g_productUiTranscript = {
        {earthai::ChatEntry::USER,
         u8"比较 2017–2025 年昆明周边农业气候、地表覆盖与高程约束。"},
        {earthai::ChatEntry::ASSISTANT,
         u8"已锁定当前研究位置，并把不同科学单位保留在独立图表中。"},
        {earthai::ChatEntry::TOOL_NOTE,
         u8"工具：准备 ERA5、AlphaEarth 与 Copernicus DEM 的来源和时间范围。"},
        {earthai::ChatEntry::ERR,
         u8"示例错误状态：上游暂不可用时必须保留重试说明，不能只显示空白。"}};

    std::function<void()> auditOverlay;
    const auto renderAuditFrame = [&]() {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        productUi.runInternal(nullptr);
        if (auditOverlay) auditOverlay();
        ImGui::Render();
        gl.bind();
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glFinish();
    };
    const auto clickAuditRect = [&](const AIChatUI::AuditRect& rect) {
        expect(rect.valid, "AI audit click target has no rendered rectangle");
        const float x = rect.x + rect.width * 0.5f;
        const float y = rect.y + rect.height * 0.5f;
        io.AddMousePosEvent(x, y);
        renderAuditFrame();
        io.AddMouseButtonEvent(0, true);
        renderAuditFrame();
        io.AddMouseButtonEvent(0, false);
        renderAuditFrame();
    };
    const auto clickShellRect = [&](earthui::EarthUiItemRect rect) {
        expect(rect.visible && rect.width > 1.0f && rect.height > 1.0f,
               "shell audit click target has no rendered rectangle");
        const float x = rect.x + rect.width * 0.5f;
        const float y = rect.y + rect.height * 0.5f;
        io.AddMousePosEvent(x, y);
        renderAuditFrame();
        expect(rect.id == 0 || GImGui->HoveredId == rect.id,
               "shell audit click target is covered; expected id=" +
                   std::to_string(rect.id) + ", hovered id=" +
                   std::to_string(GImGui->HoveredId) + ", point=" +
                   std::to_string(x) + "," + std::to_string(y));
        io.AddMouseButtonEvent(0, true);
        renderAuditFrame();
        io.AddMouseButtonEvent(0, false);
        renderAuditFrame();
    };
    const auto findVisibleEarthControl =
        [&](const std::string& key)
            -> const EarthControlUI::AuditItem* {
        ImGuiWindow* drawer =
            ImGui::FindWindowByName("##earth_ui_v2_drawer");
        for (int step = 0; step <= 21; ++step)
        {
            const EarthControlUI::AuditItem* item =
                productUi.auditControl(key);
            const bool insideDrawer =
                item && drawer &&
                item->x >= drawer->InnerClipRect.Min.x - 0.5f &&
                item->y >= drawer->InnerClipRect.Min.y - 0.5f &&
                item->x + item->width <=
                    drawer->InnerClipRect.Max.x + 0.5f &&
                item->y + item->height <=
                    drawer->InnerClipRect.Max.y + 0.5f;
            if (item && item->visible && insideDrawer &&
                item->width > 1.0f && item->height > 1.0f)
                return item;
            if (!drawer) break;
            ImGui::SetScrollY(
                drawer, drawer->ScrollMax.y *
                    static_cast<float>(std::min(step, 20)) / 20.0f);
            renderAuditFrame();
            renderAuditFrame();
            drawer = ImGui::FindWindowByName("##earth_ui_v2_drawer");
        }
        return nullptr;
    };
    const auto clickEarthControlAt =
        [&](const std::string& key, float relativeX) {
        const EarthControlUI::AuditItem* item =
            findVisibleEarthControl(key);
        expect(item != nullptr,
               "formal Earth control is not visibly reachable: " + key);
        const float x = item->x + item->width *
            std::clamp(relativeX, 0.05f, 0.95f);
        const float y = item->y + item->height * 0.5f;
        io.AddMousePosEvent(x, y);
        renderAuditFrame();
        io.AddMouseButtonEvent(0, true);
        renderAuditFrame();
        io.AddMouseButtonEvent(0, false);
        renderAuditFrame();
    };
    const auto clickEarthControl = [&](const std::string& key) {
        clickEarthControlAt(key, 0.5f);
    };
    const auto replaceFocusedInput = [&](const char* value) {
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_A, true);
        renderAuditFrame();
        io.AddKeyEvent(ImGuiKey_A, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false);
        renderAuditFrame();
        io.AddInputCharactersUTF8(value);
        renderAuditFrame();
        io.AddKeyEvent(ImGuiKey_Enter, true);
        renderAuditFrame();
        io.AddKeyEvent(ImGuiKey_Enter, false);
        renderAuditFrame();
    };
    const auto replaceEarthInput =
        [&](const std::string& key, const char* value) {
        clickEarthControl(key);
        replaceFocusedInput(value);
    };
    const auto saveAuditFrame = [&](const char* name) {
        std::vector<unsigned char> rgba(
            static_cast<std::size_t>(auditWidth) * auditHeight * 4);
        glReadPixels(
            0, 0, auditWidth, auditHeight,
            GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        const std::filesystem::path output = evidenceDirectory / name;
        expect(writePpm(output, rgba, auditWidth, auditHeight),
               "could not write AI surface evidence: " + output.string());
    };
    const auto expectRectInside = [&](const AIChatUI::AuditRect& rect,
                                      const ImGuiWindow* window,
                                      const char* label) {
        expect(rect.valid, std::string(label) + " is not rendered");
        expect(window != nullptr, "AI command window is absent");
        expect(rect.x >= window->Pos.x - 0.5f &&
                   rect.y >= window->Pos.y - 0.5f &&
                   rect.x + rect.width <=
                       window->Pos.x + window->Size.x + 0.5f &&
                   rect.y + rect.height <=
                       window->Pos.y + window->Size.y + 0.5f,
               std::string(label) + " is clipped by the AI command window");
    };

    // Main-drawer controls used to be counted as covered merely because the
    // module screenshot existed. Exercise the actual hit targets and assert
    // their product state changes. External actions such as opening a URL are
    // intentionally contract-tested elsewhere; this block never starts
    // another app or touches the host desktop.
    productUi._auditCaptureControls = true;
    productUi._uiV2.activeModule = earthui::EarthUiModule::Explore;
    productUi._uiV2.drawerOpen = true;
    productUi._alwaysDay = false;
    productUi._realTimeSun = false;
    productUi._followClock = true;
    for (int frame = 0; frame < 3; ++frame) renderAuditFrame();

    clickEarthControl("explore-always-day");
    expect(productUi._alwaysDay,
           "formal Always Day checkbox did not change product state");
    clickEarthControl("explore-always-day");
    expect(!productUi._alwaysDay,
           "formal Always Day checkbox could not be turned off");
    clickEarthControl("explore-realtime-sun");
    expect(productUi._realTimeSun && !productUi._alwaysDay,
           "formal real-time Sun checkbox did not enforce mutual exclusion");
    clickEarthControl("explore-realtime-sun");
    expect(!productUi._realTimeSun,
           "formal real-time Sun checkbox could not be turned off");

    const float sunAzimuthBefore = productUi._sunAz;
    clickEarthControlAt("explore-sun-azimuth", 0.82f);
    expect(std::fabs(productUi._sunAz - sunAzimuthBefore) > 1.0f,
           "formal Sun azimuth slider did not change product state");
    const float sunElevationBefore = productUi._sunEl;
    clickEarthControlAt("explore-sun-elevation", 0.72f);
    expect(std::fabs(productUi._sunEl - sunElevationBefore) > 1.0f,
           "formal Sun elevation slider did not change product state");
    manipulator->setByEye(
        osg::DegreesToRadians(12.3456),
        osg::DegreesToRadians(78.9012), 420000.0);
    renderAuditFrame();
    clickEarthControl("explore-sun-align");
    expect(std::fabs(productUi._sunAz - 78.9012f) < 0.05f &&
               std::fabs(productUi._sunEl - 12.3456f) < 0.05f,
           "formal align-Sun action did not use the current camera");

    clickEarthControl("explore-realtime-sun");
    expect(productUi._realTimeSun,
           "real-time Sun did not reopen its conditional controls");
    clickEarthControl("explore-follow-clock");
    expect(!productUi._followClock,
           "formal follow-clock checkbox did not expose manual time");
    expect(findVisibleEarthControl("explore-sun-year") != nullptr &&
               findVisibleEarthControl("explore-sun-month") != nullptr &&
               findVisibleEarthControl("explore-sun-day") != nullptr,
           "manual Sun date inputs are not visibly reachable");
    replaceEarthInput("explore-sun-year", "2025");
    replaceEarthInput("explore-sun-month", "9");
    replaceEarthInput("explore-sun-day", "17");
    expect(productUi._year == 2025 && productUi._month == 9 &&
               productUi._day == 17,
           "manual Sun date inputs did not accept keyboard edits");
    const float utcBefore = productUi._utcHour;
    clickEarthControlAt("explore-utc-hour", 0.22f);
    expect(std::fabs(productUi._utcHour - utcBefore) > 0.5f,
           "formal UTC-hour slider did not change product state");
    clickEarthControl("explore-realtime-sun");
    expect(!productUi._realTimeSun,
           "real-time Sun could not be closed after manual-time audit");

    const bool oceanBefore = productUi._ocean;
    clickEarthControl("explore-ocean");
    expect(productUi._ocean != oceanBefore,
           "formal Ocean checkbox did not change product state");
    const bool exposureAutoBefore = productUi._exposureAuto;
    clickEarthControl("explore-exposure-auto");
    expect(productUi._exposureAuto != exposureAutoBefore,
           "formal auto-exposure checkbox did not change product state");
    if (productUi._exposureAuto)
        clickEarthControl("explore-exposure-auto");
    const float exposureBefore = productUi._exposure;
    clickEarthControlAt("explore-exposure", 0.78f);
    expect(std::fabs(productUi._exposure - exposureBefore) > 0.05f,
           "formal Exposure slider did not change product state");
    const float atmosphereBefore = productUi._globalOpaque;
    clickEarthControlAt("explore-atmosphere", 0.25f);
    expect(std::fabs(productUi._globalOpaque - atmosphereBefore) > 0.05f,
           "formal Atmosphere slider did not change product state");

    replaceEarthInput("explore-goto-latitude", "31.2304");
    replaceEarthInput("explore-goto-longitude", "121.4737");
    replaceEarthInput("explore-goto-altitude", "125.0");
    expect(std::fabs(productUi._gotoLat - 31.2304f) < 0.001f &&
               std::fabs(productUi._gotoLon - 121.4737f) < 0.001f &&
               std::fabs(productUi._gotoAltKm - 125.0f) < 0.01f,
           "formal Go To inputs did not accept keyboard edits");
    clickEarthControl("explore-goto-submit");
    osg::Vec3d flownTo = manipulator->computeEyeLatLonHeight();
    expect(std::fabs(osg::RadiansToDegrees(flownTo[0]) - 31.2304) < 0.05 &&
               std::fabs(osg::RadiansToDegrees(flownTo[1]) - 121.4737) <
                   0.05 &&
               std::fabs(flownTo[2] / 1000.0 - 125.0) < 0.5,
           "formal Go To action did not update the Earth camera");

    ImGuiWindow* interactionDrawer =
        ImGui::FindWindowByName("##earth_ui_v2_drawer");
    expect(interactionDrawer != nullptr,
           "Explore drawer disappeared before bookmark interaction audit");
    interactionDrawer->StateStorage.SetInt(
        interactionDrawer->GetID(
            u8"书签与巡游###drawer_section_bookmarks"),
        1);
    renderAuditFrame();
    renderAuditFrame();
    manipulator->clearControlPoints();
    productUi._bookmarkTime = 0;
    clickEarthControl("explore-bookmark-record");
    expect(manipulator->getControlPoints().size() == 1,
           "formal bookmark action did not record the current view");
    clickEarthControl("explore-bookmark-play");
    expect(manipulator->isAnimationRunning(),
           "formal tour Play action did not start animation");
    clickEarthControl("explore-bookmark-stop");
    expect(!manipulator->isAnimationRunning(),
           "formal tour Stop action did not stop animation");
    clickEarthControl("explore-bookmark-clear");
    expect(manipulator->getControlPoints().empty() &&
               productUi._bookmarkTime == 0,
           "formal tour Clear action did not reset the tour");

    productUi._uiV2.activeModule = earthui::EarthUiModule::Layers;
    productUi._uiV2.drawerOpen = true;
    ImGui::SetScrollY(
        ImGui::FindWindowByName("##earth_ui_v2_drawer"), 0.0f);
    for (int frame = 0; frame < 3; ++frame) renderAuditFrame();
    clickEarthControl("layers-search");
    io.AddInputCharactersUTF8("x");
    renderAuditFrame();
    expect(std::string(productUi._layerFilter).find('x') !=
               std::string::npos,
           "formal layer-search field did not accept text input");
    for (const Preset& preset : layers.presetsSnapshot())
    {
        clickEarthControl("layers-preset:" + preset.name);
        layers.drainPending();
        expect(layers.lastAppliedPreset() == preset.name,
               "formal layer preset did not reach LayerManager: " +
                   preset.name);
    }
    ImGui::ClearActiveID();
    productUi._layerFilter[0] = '\0';
    renderAuditFrame();
    const float firstLayerOpacity =
        layers.layersSnapshot().front().opacity;
    renderAuditFrame();
    renderAuditFrame();
    clickEarthControlAt("layer-opacity:product-ui-0", 0.25f);
    layers.drainPending();
    expect(std::fabs(
               layers.layersSnapshot().front().opacity -
               firstLayerOpacity) > 0.05f,
           "formal layer-opacity slider did not reach LayerManager");
    const std::vector<OverlayLayer> beforeLayerToggle =
        layers.layersSnapshot();
    const bool firstLayerEnabled = beforeLayerToggle.front().enabled;
    clickEarthControl("layer-enabled:product-ui-0");
    layers.drainPending();
    const std::vector<OverlayLayer> afterLayerToggle =
        layers.layersSnapshot();
    expect(afterLayerToggle.front().enabled != firstLayerEnabled,
           "formal layer visibility checkbox did not reach LayerManager");

    const std::array<std::pair<earthui::EarthUiModule, std::string>, 3>
        routedLayerControls = {{
            {earthui::EarthUiModule::Live,
             "layer-enabled:product-ui-8"},
            {earthui::EarthUiModule::Satellites,
             "layer-enabled:product-ui-16"},
            {earthui::EarthUiModule::City3D,
             "layer-enabled:product-ui-24"}}};
    for (const auto& routed : routedLayerControls)
    {
        productUi._uiV2.activeModule = routed.first;
        productUi._uiV2.drawerOpen = true;
        for (int frame = 0; frame < 3; ++frame) renderAuditFrame();
        const EarthControlUI::AuditItem* routedControl =
            findVisibleEarthControl(routed.second);
        expect(routedControl != nullptr,
               std::string("module-routed layer control is absent: ") +
                   routed.second);
        const std::string layerId =
            routed.second.substr(std::string("layer-enabled:").size());
        bool routedBefore = false;
        for (const OverlayLayer& layer : layers.layersSnapshot())
            if (layer.id == layerId) routedBefore = layer.enabled;
        clickEarthControl(routed.second);
        layers.drainPending();
        bool routedAfter = routedBefore;
        for (const OverlayLayer& layer : layers.layersSnapshot())
            if (layer.id == layerId) routedAfter = layer.enabled;
        expect(routedAfter != routedBefore,
               std::string("module-routed layer control did not change ") +
                   "LayerManager state: " + routed.second);
    }

    productUi._uiV2.activeModule = earthui::EarthUiModule::Tasks;
    productUi._uiV2.drawerOpen = true;
    productUi._ticker.showTicker = false;
    productUi._ticker.showStatusBar = false;
    for (int frame = 0; frame < 3; ++frame) renderAuditFrame();
    clickEarthControl("tasks-toggle-event-stream");
    expect(productUi._ticker.showTicker,
           "formal event-stream action did not change product state");
    clickEarthControl("tasks-toggle-status-bar");
    expect(productUi._ticker.showStatusBar,
           "formal status-strip action did not change product state");
    productUi._ticker.showTicker = false;
    productUi._ticker.showStatusBar = false;

    productUi._uiV2.activeModule = earthui::EarthUiModule::Settings;
    productUi._uiV2.drawerOpen = true;
    for (int frame = 0; frame < 3; ++frame) renderAuditFrame();
    std::deque<earthcfg::Param>& auditSettings = earthcfg::params();
    expect(!auditSettings.empty(),
           "settings registry is empty during formal control audit");
    for (earthcfg::Param& setting : auditSettings)
    {
        const double settingBefore = setting.value.load();
        const double span = setting.maxVal - setting.minVal;
        const double normalized = span > 0.0
            ? (settingBefore - setting.minVal) / span : 0.0;
        if (setting.kind == earthcfg::PK_BOOL)
            clickEarthControl("settings-value:" + setting.id);
        else
            clickEarthControlAt(
                "settings-value:" + setting.id,
                normalized > 0.5 ? 0.2f : 0.8f);
        expect(std::fabs(setting.value.load() - settingBefore) > 0.0001,
               "formal setting did not update the registry: " +
                   setting.id);
        clickEarthControl("settings-reset:" + setting.id);
        expect(std::fabs(
                   setting.value.load() - setting.defVal) < 0.0001,
               "formal setting Reset did not restore the default: " +
                   setting.id);
    }
    earthexit::QuitRequest auditQuit;
    productUi._quitRequest = &auditQuit;
    clickEarthControl("settings-quit");
    expect(auditQuit.consume(),
           "formal Quit action did not publish the main-thread request");
    productUi._quitRequest = nullptr;
    saveAuditFrame("1440x900-drawer-controls.ppm");
    productUi._auditCaptureControls = false;
    layers.setEnabled("product-ui-0", true);
    layers.drainPending();

    for (int frame = 0; frame < 3; ++frame) renderAuditFrame();
    const AIChatUI::AuditSnapshot compactAi = command.auditSnapshot();
    ImGuiWindow* aiCommand = ImGui::FindWindowByName(u8"AI 对话条");
    expectRectInside(compactAi.historyButton, aiCommand, "history button");
    expectRectInside(compactAi.templateButton, aiCommand, "template button");
    expectRectInside(compactAi.input, aiCommand, "AI input");
    expectRectInside(compactAi.sendButton, aiCommand, "send button");
    expectRectInside(compactAi.photoButton, aiCommand, "photo button");
    expectRectInside(compactAi.videoButton, aiCommand, "video button");

    // Type into the real production input and submit through its actual
    // button. This proves keyboard input, enabled-state calculation and the
    // command handoff instead of treating a visible row as operable.
    clickAuditRect(compactAi.input);
    io.AddInputCharactersUTF8(
        u8"比较昆明 2017–2025 年农业气候与地表变化");
    renderAuditFrame();
    expect(command.auditSnapshot().sendButton.valid,
           "typing into the AI input did not keep the Send target visible");
    const std::size_t submittedBefore =
        earthai::g_productUiSubmissions.size();
    clickAuditRect(command.auditSnapshot().sendButton);
    expect(earthai::g_productUiSubmissions.size() == submittedBefore + 1 &&
               earthai::g_productUiSubmissions.back() ==
                   u8"比较昆明 2017–2025 年农业气候与地表变化",
           "real AI Send click did not submit the typed command");

    clickAuditRect(compactAi.historyButton);
    renderAuditFrame();
    renderAuditFrame();
    expect(command.auditSnapshot().historyExpanded,
           "real AI history button click did not expand the transcript");
    inspectProductionWindows(
        auditWidth, auditHeight, earthui::EarthUiModule::Science);
    saveAuditFrame("1440x900-ai-history.ppm");

    clickAuditRect(command.auditSnapshot().historyButton);
    renderAuditFrame();
    renderAuditFrame();
    expect(!command.auditSnapshot().historyExpanded,
           "real AI history button click did not collapse the transcript");

    clickAuditRect(command.auditSnapshot().templateButton);
    renderAuditFrame();
    renderAuditFrame();
    expect(command.auditSnapshot().templatePopupVisible,
           "real analysis-template button click did not open the gallery");
    inspectProductionWindows(
        auditWidth, auditHeight, earthui::EarthUiModule::Science);
    ImGuiWindow* templatePopup = activeNonModalPopup();
    aiCommand = ImGui::FindWindowByName(u8"AI 对话条");
    expect(templatePopup != nullptr,
           "analysis-template popup has no active product window");
    expect(!overlaps(templatePopup, aiCommand),
           "analysis-template popup covers the AI command controls");
    saveAuditFrame("1440x900-ai-templates.ppm");
    io.AddMousePosEvent(
        static_cast<float>(auditWidth) * 0.72f, 90.0f);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, true);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, false);
    renderAuditFrame();
    expect(!command.auditSnapshot().templatePopupVisible,
           "clicking outside did not close the analysis-template gallery");

    // Open the gallery again and choose its real first action. The production
    // handler must fill the prompt and relocate the camera before Send.
    clickAuditRect(command.auditSnapshot().templateButton);
    renderAuditFrame();
    renderAuditFrame();
    expect(command.auditSnapshot().templatePrimaryAction.valid,
           "analysis-template gallery has no reachable primary action");
    const earthai::ScienceEarthPromptExample& firstTemplate =
        earthai::scienceEarthPromptExamples().front();
    clickAuditRect(command.auditSnapshot().templatePrimaryAction);
    renderAuditFrame();
    expect(!command.auditSnapshot().templatePopupVisible,
           "using an analysis template did not close the gallery");
    const osg::Vec3d templateEye =
        manipulator->computeEyeLatLonHeight();
    expect(std::fabs(
               osg::RadiansToDegrees(templateEye[0]) -
               firstTemplate.latitudeDeg) < 0.001 &&
               std::fabs(
                   osg::RadiansToDegrees(templateEye[1]) -
                   firstTemplate.longitudeDeg) < 0.001,
           "analysis template did not move the camera to its declared place");
    const std::size_t templateSubmitBefore =
        earthai::g_productUiSubmissions.size();
    clickAuditRect(command.auditSnapshot().sendButton);
    expect(
        earthai::g_productUiSubmissions.size() ==
            templateSubmitBefore + 1 &&
        earthai::g_productUiSubmissions.back() == firstTemplate.prompt,
        "template primary action did not prepare its exact prompt for Send");

    // Media requests are intercepted only in this audit build. No network,
    // file viewer, camera capture or packaged Desktop app is touched; the
    // real button hit paths and request semantics are still exercised.
    command.auditSetMediaControlsEnabled(true);
    for (int frame = 0; frame < 4; ++frame) renderAuditFrame();
    expect(command.auditSnapshot().photoEnabled,
           "audit media mode did not enable the Photo action");
    saveAuditFrame("1440x900-ai-command-actions.ppm");
    const std::size_t photoSubmitBefore =
        earthai::g_productUiSubmissions.size();
    command.auditClearActions();
    clickAuditRect(command.auditSnapshot().photoButton);
    expect(command.auditActionMask() & AIChatUI::AUDIT_ACTION_PHOTO,
           "real Photo click did not activate the production button");
    expect(
        earthai::g_productUiSubmissions.size() == photoSubmitBefore + 1 &&
        earthai::g_productUiSubmissions.back() ==
            u8"生成一张当前视角的实景照片",
        "real Photo click did not submit the current-view request");
    command.auditClearActions();
    clickAuditRect(command.auditSnapshot().videoButton);
    expect(
        command.auditActionMask() & AIChatUI::AUDIT_ACTION_VIDEO_BEGIN,
        "real Video click did not publish the begin request");
    command.auditClearActions();
    command.auditSetVideoState(AIChatUI::AUDIT_VIDEO_WAIT_B);
    renderAuditFrame();
    renderAuditFrame();
    expectRectInside(
        command.auditSnapshot().videoButton,
        ImGui::FindWindowByName(u8"AI 对话条"), "finish-B button");
    expectRectInside(
        command.auditSnapshot().videoCancelButton,
        ImGui::FindWindowByName(u8"AI 对话条"), "video cancel button");
    clickAuditRect(command.auditSnapshot().videoButton);
    expect(
        command.auditActionMask() & AIChatUI::AUDIT_ACTION_VIDEO_END,
        "real Finish-B click did not publish the end request");
    command.auditClearActions();
    clickAuditRect(command.auditSnapshot().videoCancelButton);
    expect(
        command.auditActionMask() & AIChatUI::AUDIT_ACTION_VIDEO_CANCEL,
        "real inline video Cancel click did not publish cancel");
    command.auditSetVideoState(AIChatUI::AUDIT_VIDEO_IDLE);
    command.auditClearActions();

    // AI-generated chart cards are formal product surfaces. Render the real
    // AICardPanel implementation rather than a test stub so the shared
    // Insight Lens, axes, units, legends, tokens and long labels all enter
    // the same viewport/overflow/pixel gate as the shell.
    const auto renderChartCard = [&](const char* evidenceName,
                                     const picojson::value& spec) {
        AICardPanel panel;
        earthui::CardStack stack;
        panel.pushChart(spec);
        auditOverlay = [&]() {
            panel.registerCards(stack);
            stack.draw();
        };
        for (int frame = 0; frame < 4; ++frame) renderAuditFrame();
        inspectProductionWindows(
            auditWidth, auditHeight, earthui::EarthUiModule::Science);
        ImGuiWindow* insight =
            ImGui::FindWindowByName(u8"洞察透镜###earth_insight_lens");
        expect(insight != nullptr && insight->Active && !insight->Hidden,
               std::string(evidenceName) +
                   ": real AI chart did not enter the Insight Lens");
        expect(!insight->ScrollbarX && insight->ScrollMax.x <=
                   (insight->ScrollbarY
                        ? ImGui::GetStyle().ScrollbarSize + 0.5f : 0.5f),
               std::string(evidenceName) +
                   ": real AI chart has horizontal clipping");
        saveAuditFrame(evidenceName);
        auditOverlay = std::function<void()>();
        renderAuditFrame();
    };

    renderChartCard(
        "1440x900-ai-chart-bar.ppm",
        chartSpec(
            "bar", u8"农业指标比较", "mm",
            u8"不同变量保持自己的科学单位，长标签必须在洞察透镜内完整呈现。",
            {u8"年降水量", u8"参考蒸散量", u8"生长季水分盈亏"},
            {1286.4, 1042.8, 243.6}));
    renderChartCard(
        "1440x900-ai-chart-line.ppm",
        chartSpec(
            "line", u8"年度平均气温趋势", u8"°C",
            u8"横轴为年份，纵轴为温度；首年、中间年和末年必须可识别。",
            {"2017", "2018", "2019", "2020", "2021",
             "2022", "2023", "2024", "2025"},
            {17.2, 17.5, 17.1, 17.9, 18.0, 18.4, 18.1, 18.7, 18.2}));
    renderChartCard(
        "1440x900-ai-chart-donut.ppm",
        chartSpec(
            "donut", u8"地表覆盖构成", "%",
            u8"每个扇区必须拥有对应图例、标签和数值。",
            {u8"耕地", u8"林地", u8"建设用地", u8"水体"},
            {46.0, 31.0, 17.0, 6.0}));
    renderChartCard(
        "1440x900-ai-chart-stat.ppm",
        chartSpec(
            "stat", u8"区域平均高程", "m",
            u8"基于 Copernicus DEM 的区域统计，不把单位混入数值。",
            {u8"分析区域平均值"}, {1842.0}));

    const auto renderMediaCard = [&](
        const char* evidenceName,
        const std::function<void(AICardPanel&)>& prepare) {
        AICardPanel panel;
        earthui::CardStack stack;
        prepare(panel);
        auditOverlay = [&]() {
            panel.registerCards(stack);
            stack.draw();
        };
        for (int frame = 0; frame < 4; ++frame) renderAuditFrame();
        inspectProductionWindows(
            auditWidth, auditHeight, earthui::EarthUiModule::Science);
        ImGuiWindow* insight =
            ImGui::FindWindowByName(u8"洞察透镜###earth_insight_lens");
        expect(insight != nullptr && insight->Active && !insight->Hidden,
               std::string(evidenceName) +
                   ": real AI media card did not enter the Insight Lens");
        saveAuditFrame(evidenceName);
        auditOverlay = std::function<void()>();
        renderAuditFrame();
    };
    renderMediaCard(
        "1440x900-ai-photo-result.ppm",
        [](AICardPanel& panel) {
            panel.pushPhoto(
                "/tmp/osgsol-ui-audit/"
                u8"昆明农业气候与地表变化联合分析航拍结果.png",
                u8"科学航拍结果", false);
        });
    renderMediaCard(
        "1440x900-ai-video-result.ppm",
        [](AICardPanel& panel) {
            panel.pushPhoto(
                "/tmp/osgsol-ui-audit/"
                u8"香港维多利亚港科学巡航与地形融合验证视频.mp4",
                u8"巡航视频结果", true);
        });
    earthai::JobManager auditJobs;
    const int auditJobId =
        auditJobs.create("photo", u8"正在生成科学航拍");
    auditJobs.update(
        auditJobId, earthai::AIJob::RUNNING, 0.63f,
        std::string(), std::string());
    renderMediaCard(
        "1440x900-ai-generation-progress.ppm",
        [&](AICardPanel& panel) {
            panel.pushJob(
                &auditJobs, auditJobId,
                u8"生成昆明农业气候与地表变化联合航拍");
        });

    // Module help is a real hidden product surface. Click its production
    // hit target, then prove the popup remains bounded, wraps instead of
    // scrolling sideways, and accepts an outside click to close.
    productUi._uiV2.activeModule = earthui::EarthUiModule::Science;
    renderAuditFrame();
    clickShellRect(productUi._uiV2.drawerHelpButton);
    renderAuditFrame();
    renderAuditFrame();
    ImGuiWindow* moduleHelp = activeNonModalPopup();
    expect(moduleHelp != nullptr && moduleHelp->Active &&
               !moduleHelp->Hidden,
           "module help popup did not enter the product frame");
    expect(moduleHelp->Pos.x >= -0.5f && moduleHelp->Pos.y >= -0.5f &&
               moduleHelp->Pos.x + moduleHelp->Size.x <=
                   auditWidth + 0.5f &&
               moduleHelp->Pos.y + moduleHelp->Size.y <=
                   auditHeight + 0.5f,
           "module help popup extends beyond the viewport");
    expect(moduleHelp->ScrollMax.x <= 1.0f && !moduleHelp->ScrollbarX,
           "module help popup has horizontal overflow");
    ImGuiWindow* helpDrawer =
        ImGui::FindWindowByName("##earth_ui_v2_drawer");
    expect(helpDrawer != nullptr &&
               moduleHelp->Pos.x >= helpDrawer->Pos.x - 0.5f &&
               moduleHelp->Pos.x + moduleHelp->Size.x <=
                   helpDrawer->Pos.x + helpDrawer->Size.x + 0.5f,
           "drawer help popup drifted away from its trigger surface");
    saveAuditFrame("1440x900-module-help.ppm");
    io.AddMousePosEvent(
        static_cast<float>(auditWidth) * 0.72f, 90.0f);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, true);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, false);
    renderAuditFrame();
    expect(activeNonModalPopup() == nullptr,
           "outside click did not close the module help popup");
    clickShellRect(productUi._uiV2.contextHelpButton);
    renderAuditFrame();
    renderAuditFrame();
    expect(activeNonModalPopup() != nullptr,
           "context-tray module help does not receive a real pointer click");
    io.AddMousePosEvent(
        static_cast<float>(auditWidth) * 0.72f, 90.0f);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, true);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, false);
    renderAuditFrame();
    expect(activeNonModalPopup() == nullptr,
           "outside click did not close context-tray module help");

    // Event stream and the global status strip are optional but formal UI
    // states. Exercise their real product code together, including the long
    // bounded scroll region and the exact boundary with the context tray.
    std::vector<earthfeed::TickerEvent> auditEvents;
    const double now = static_cast<double>(std::time(nullptr));
    for (int index = 0; index < 18; ++index)
    {
        earthfeed::TickerEvent event;
        event.title =
            u8"农业气象、地表覆盖与基础设施联合观测事件 " +
            std::to_string(index + 1) +
            u8" · 长标题必须完整换行且可滚动返回";
        event.sourceId = (index % 2 == 0) ? "ERA5" : "GDELT";
        event.lat = 24.9752 + index * 0.01;
        event.lon = 102.0031 + index * 0.01;
        event.unixTime = now - index * 300.0;
        auditEvents.push_back(event);
    }
    earthfeed::setProductTestEvents(auditEvents);
    earthfeed::clearFeedSelection();
    productUi._uiV2.activeModule = earthui::EarthUiModule::Tasks;
    productUi._ticker.showTicker = true;
    productUi._ticker.showStatusBar = true;
    for (int frame = 0; frame < 4; ++frame) renderAuditFrame();
    inspectProductionWindows(
        auditWidth, auditHeight, earthui::EarthUiModule::Tasks);
    ImGuiWindow* statusBar = ImGui::FindWindowByName("##earth_statusbar");
    ImGuiWindow* contextTray =
        ImGui::FindWindowByName("##earth_ui_v2_context");
    ImGuiWindow* eventLens =
        ImGui::FindWindowByName(u8"洞察透镜###earth_insight_lens");
    expect(statusBar != nullptr && contextTray != nullptr &&
               eventLens != nullptr,
           "event/status product surfaces are not all visible");
    expect(contextTray->Pos.y + contextTray->Size.y <=
               statusBar->Pos.y + 0.5f,
           "global status strip overlaps the context tray");
    expect(eventLens->ScrollMax.y > 1.0f && eventLens->ScrollbarY,
           "long event stream has no visible vertical scroll range");
    const float eventScrollMaximum = eventLens->ScrollMax.y;
    ImGui::SetScrollY(eventLens, eventScrollMaximum);
    renderAuditFrame();
    renderAuditFrame();
    eventLens =
        ImGui::FindWindowByName(u8"洞察透镜###earth_insight_lens");
    expect(eventLens && eventLens->Scroll.y >= eventScrollMaximum - 1.0f,
           "event stream cannot scroll to the bottom");
    ImGui::SetScrollY(eventLens, 0.0f);
    renderAuditFrame();
    renderAuditFrame();
    eventLens =
        ImGui::FindWindowByName(u8"洞察透镜###earth_insight_lens");
    expect(eventLens && eventLens->Scroll.y <= 1.0f,
           "event stream cannot scroll back to the top");
    expect(productUi._ticker._auditFirstEvent.visible,
           "first event has no reachable product hit target");
    const EventTickerUI::AuditRect& firstEvent =
        productUi._ticker._auditFirstEvent;
    earthui::EarthUiItemRect firstEventRect = {
        firstEvent.x, firstEvent.y, firstEvent.width, firstEvent.height,
        firstEvent.visible};
    clickShellRect(firstEventRect);
    const osg::Vec3d eventEye =
        manipulator->computeEyeLatLonHeight();
    expect(std::fabs(
               osg::RadiansToDegrees(eventEye[0]) -
               auditEvents.front().lat) < 0.01 &&
               std::fabs(
                   osg::RadiansToDegrees(eventEye[1]) -
               auditEvents.front().lon) < 0.01,
           "clicking a formal event did not relocate the Earth camera");
    saveAuditFrame("1440x900-event-stream-status.ppm");
    eventLens =
        ImGui::FindWindowByName(u8"洞察透镜###earth_insight_lens");
    ImGuiTabBar* eventTabBar = eventLens
        ? GImGui->TabBars.GetByKey(
              eventLens->GetID("##insight_tabs"))
        : nullptr;
    expect(eventTabBar != nullptr,
           "event stream has no formal Insight Lens tab bar");
    const ImGuiTabItem* selectedEventTab = nullptr;
    for (const ImGuiTabItem& tab : eventTabBar->Tabs)
        if (tab.ID == eventTabBar->SelectedTabId)
            selectedEventTab = &tab;
    expect(selectedEventTab != nullptr,
           "event stream has no selected closable tab");
    const float closeX =
        eventTabBar->BarRect.Min.x + selectedEventTab->Offset +
        selectedEventTab->Width - eventTabBar->FramePadding.x -
        ImGui::GetFontSize() * 0.5f;
    const float closeY =
        eventTabBar->BarRect.Min.y + eventTabBar->FramePadding.y +
        ImGui::GetFontSize() * 0.5f;
    io.AddMousePosEvent(closeX, closeY);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, true);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, false);
    renderAuditFrame();
    expect(!productUi._ticker.showTicker,
           "real Insight Lens close control did not close the event stream");
    productUi._ticker.showStatusBar = false;
    earthfeed::setProductTestEvents({});
    productUi._uiV2.activeModule = earthui::EarthUiModule::Science;

    // The maximum-detail notice previously appeared as repeated map
    // watermarks. Force the production debounced signal and prove the single
    // bounded toast stays inside the map corridor and can coexist with the
    // shell without becoming a full-screen overlay.
    osgVerse::TileManager::instance()->markOverlayStretchedPastNative(500);
    productUi._uiV2.activeModule = earthui::EarthUiModule::Layers;
    for (int frame = 0; frame < 4; ++frame) renderAuditFrame();
    inspectProductionWindows(
        auditWidth, auditHeight, earthui::EarthUiModule::Layers);
    ImGuiWindow* detailToast =
        ImGui::FindWindowByName(
            u8"地图细节提示###max_detail_badge");
    expect(detailToast != nullptr && detailToast->Active &&
               !detailToast->Hidden,
           "maximum-detail notice did not render as a single product toast");
    ImGuiWindow* drawer =
        ImGui::FindWindowByName("##earth_ui_v2_drawer");
    ImGuiWindow* insight =
        ImGui::FindWindowByName(u8"洞察透镜###earth_insight_lens");
    expect(!overlaps(detailToast, drawer) &&
               !overlaps(detailToast, insight),
           "maximum-detail toast overlaps a docked product surface");
    saveAuditFrame("1440x900-max-detail-toast.ppm");
    const ImGuiStyle& toastStyle = ImGui::GetStyle();
    const float toastCloseButtonSize = ImGui::GetFontSize();
    io.AddMousePosEvent(
        detailToast->Pos.x + detailToast->Size.x -
            toastStyle.FramePadding.x - toastCloseButtonSize * 0.5f,
        detailToast->Pos.y + toastStyle.FramePadding.y +
            toastCloseButtonSize * 0.5f);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, true);
    renderAuditFrame();
    io.AddMouseButtonEvent(0, false);
    renderAuditFrame();
    expect(productUi._detailBadgeDismissed,
           "real close-button click did not dismiss maximum-detail toast");
    renderAuditFrame();
    detailToast = ImGui::FindWindowByName(
        u8"地图细节提示###max_detail_badge");
    expect(!detailToast || !detailToast->Active || detailToast->Hidden,
           "dismissed maximum-detail toast remained visible");
    osgVerse::TileManager::instance()->markOverlayStretchedPastNative(0);
    productUi._uiV2.activeModule = earthui::EarthUiModule::Science;

    command.auditSetVideoConfirm(true);
    renderAuditFrame();
    renderAuditFrame();
    expect(command.auditSnapshot().videoModalVisible,
           "video confirmation modal did not enter the formal product frame");
    inspectProductionWindows(
        auditWidth, auditHeight, earthui::EarthUiModule::Science);
    saveAuditFrame("1440x900-ai-video-confirm.ppm");
    expect(command.auditSnapshot().videoConfirmButton.valid &&
               command.auditSnapshot().videoModalCancelButton.valid,
           "video modal actions are not both reachable");
    clickAuditRect(command.auditSnapshot().videoConfirmButton);
    expect(
        command.auditActionMask() & AIChatUI::AUDIT_ACTION_VIDEO_CONFIRM,
        "real video Confirm click did not publish confirmation");
    command.auditClearActions();
    clickAuditRect(command.auditSnapshot().videoModalCancelButton);
    expect(
        command.auditActionMask() & AIChatUI::AUDIT_ACTION_VIDEO_CANCEL,
        "real video modal Cancel click did not publish cancel");
    command.auditSetVideoConfirm(false);
    command.auditSetMediaControlsEnabled(false);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext(context);
    std::cout << "[OK] production EarthControlUI rendered for 8 modules at "
              << "3 viewports: " << evidenceDirectory << std::endl;
    return 0;
}
