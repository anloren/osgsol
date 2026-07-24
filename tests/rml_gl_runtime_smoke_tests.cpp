#define IMGUI_DEFINE_MATH_OPERATORS
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>

#include "ai_media.h"
#include "ai_ui.h"
#include "earth_control_layout.h"
#include "earth_ui_tokens.h"
#include "earth_ui_v2.h"
#include "product_ui/rml_ui_runtime.h"
#include "science_ui/rml_science_chart.h"
#include "science_ui/science_chart_model.h"
#include "science_ui/science_report_window.h"
#include "ui_card.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_opengl3.h>
#include <imgui/imgui_internal.h>

#include <osg/GraphicsContext>
#include <osg/Group>
#include <osg/RenderInfo>
#include <osg/State>
#include <osgGA/GUIEventAdapter>
#include <osgViewer/Viewer>
#include <pipeline/Global.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

// The composite test executes the production AI command surface while keeping
// all network and media owners absent. These narrow definitions provide stable
// offline UI state without constructing workers, opening files, or touching
// the packaged Desktop application.
namespace earthai
{
AIChatCore::AIChatCore(LLMProvider* provider, ToolRegistry* registry)
    : _provider(provider), _registry(registry)
{
}

AIChatCore::~AIChatCore() {}

bool AIChatCore::busy() const
{
    return false;
}

void AIChatCore::submit(const std::string&) {}

std::vector<ChatEntry> AIChatCore::transcript() const
{
    return {
        {ChatEntry::USER, u8"比较当前区域的农业气候与地表变化。"},
        {ChatEntry::ASSISTANT, u8"已准备数据源与空间范围，等待提交。"}};
}

MediaManager::VideoUiSnapshot MediaManager::videoUiSnapshot() const
{
    return VideoUiSnapshot();
}
}

void AICardPanel::pushChart(const picojson::value&) {}

void AICardPanel::registerCards(earthui::CardStack&) {}

#if defined(__APPLE__)
namespace earthime
{
void setInputRect(float, float, float, float) {}
}
#endif

namespace
{
constexpr int WIDTH = 1440;
constexpr int HEIGHT = 900;
constexpr int MID_WIDTH = 1280;
constexpr int MID_HEIGHT = 720;

void expect(bool condition, const std::string& message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

std::string ancestry(Rml::Element* element)
{
    std::string output;
    for (int depth = 0; element && depth < 6; ++depth)
    {
        if (!output.empty()) output += " <- ";
        output += std::string(element->GetTagName()) + "#" +
                  std::string(element->GetId());
        element = element->GetParentNode();
    }
    return output.empty() ? std::string("<none>") : output;
}

class HeadlessCglContext : public osg::GraphicsContext
{
public:
    explicit HeadlessCglContext(osg::GraphicsContext::Traits* traits)
        : _context(nullptr), _fbo(0), _color(0), _depth(0), _realized(false)
    {
        _traits = traits;
        setState(new osg::State);
        getState()->setGraphicsContext(this);
        getState()->setContextID(osg::GraphicsContext::createNewContextID());
    }

    bool valid() const override { return true; }
    bool isRealizedImplementation() const override { return _realized; }

    bool realizeImplementation() override
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
        CGLPixelFormatObj pixelFormat = nullptr;
        GLint formats = 0;
        if (CGLChoosePixelFormat(attributes, &pixelFormat, &formats) !=
                kCGLNoError || !pixelFormat)
            return false;
        const CGLError error = CGLCreateContext(pixelFormat, nullptr, &_context);
        CGLDestroyPixelFormat(pixelFormat);
        if (error != kCGLNoError || !_context) return false;
        if (CGLSetCurrentContext(_context) != kCGLNoError) return false;

        glGenFramebuffers(1, &_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, _fbo);
        glGenRenderbuffers(1, &_color);
        glBindRenderbuffer(GL_RENDERBUFFER, _color);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8,
                              _traits->width, _traits->height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                  GL_RENDERBUFFER, _color);
        glGenRenderbuffers(1, &_depth);
        glBindRenderbuffer(GL_RENDERBUFFER, _depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
                              _traits->width, _traits->height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, _depth);
        const bool complete =
            glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        setDefaultFboId(_fbo);
        CGLSetCurrentContext(nullptr);
        _realized = complete;
        return complete;
    }

    void closeImplementation() override
    {
        if (_context) CGLDestroyContext(_context);
        _context = nullptr;
        _realized = false;
    }

    bool makeCurrentImplementation() override
    { return CGLSetCurrentContext(_context) == kCGLNoError; }
    bool makeContextCurrentImplementation(osg::GraphicsContext*) override
    { return makeCurrentImplementation(); }
    bool releaseContextImplementation() override
    { return CGLSetCurrentContext(nullptr) == kCGLNoError; }
    void bindPBufferToTextureImplementation(GLenum) override {}
    void swapBuffersImplementation() override { glFlush(); }

protected:
    ~HeadlessCglContext() override { closeImplementation(); }

private:
    CGLContextObj _context;
    GLuint _fbo;
    GLuint _color;
    GLuint _depth;
    bool _realized;
};

class DocumentClient : public RmlUiFrameClient, public Rml::EventListener
{
public:
    bool onRmlContextReady(Rml::Context& context, std::string& error) override
    {
        registerRmlScienceChartElement();
        _document = context.LoadDocument(
            std::string(OSGVERSE_SOURCE_DIR) +
            "/assets/misc/ui/scienceearth/workbench.rml");
        if (!_document)
        {
            error = "real ScienceEarth workbench document did not load";
            return false;
        }
        if (Rml::Element* source = _document->GetElementById("source-select"))
            source->SetInnerRML(
                "<option selected value='era5-agro'>"
                "ERA5 Agricultural Climate</option>"
                "<option value='alphaearth'>AlphaEarth Foundations</option>"
                "<option value='dem'>Copernicus DEM GLO-30</option>"
                "<option value='era5-land'>"
                "ERA5-Land Surface &amp; Soil History</option>"
                "<option value='sentinel2'>Sentinel-2 Level-2A</option>");
        if (Rml::Element* help =
                _document->GetElementById("science-help-copy"))
            help->SetProperty("display", "block");
        if (Rml::Element* helpButton =
                _document->GetElementById("science-help"))
            helpButton->AddEventListener("click", this);
        if (Rml::Element* runButton =
                _document->GetElementById("run-action"))
            runButton->AddEventListener("click", this);
        _report = context.LoadDocument(
            std::string(OSGVERSE_SOURCE_DIR) +
            "/assets/misc/ui/scienceearth/report.rml");
        if (!_report)
        {
            error = "real ScienceEarth report document did not load";
            return false;
        }
        _document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        _report->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
        return true;
    }

    void onRmlFrame(Rml::Context& context) override
    {
        const Rml::Vector2i dimensions = context.GetDimensions();
        if (dimensions == _lastDimensions) return;
        _lastDimensions = dimensions;
        const earthui::EarthUiShellLayout shell =
            earthui::computeEarthUiShellLayout(
                static_cast<float>(dimensions.x),
                static_cast<float>(dimensions.y), true);
        const earthui::ScienceWorkbenchLayout workbench =
            earthui::computeScienceWorkbenchLayout(
                static_cast<float>(dimensions.x),
                static_cast<float>(dimensions.y));
        if (_document)
        {
            Rml::Element* root =
                _document->GetElementById("science-workbench");
            auto setWorkbenchProperty =
                [this, root](const char* name, const std::string& value)
            {
                _document->SetProperty(name, value);
                if (root && root != _document)
                    root->SetProperty(name, value);
            };
            setWorkbenchProperty(
                "left", std::to_string(shell.drawerX) + "px");
            setWorkbenchProperty(
                "top", std::to_string(shell.drawerY) + "px");
            setWorkbenchProperty("bottom", "auto");
            setWorkbenchProperty(
                "width", std::to_string(workbench.composerWidth) + "px");
            setWorkbenchProperty(
                "height", std::to_string(shell.drawerHeight) + "px");
        }
        const float bottomInset = shell.statusHeight +
            shell.contextHeight + shell.commandHeight +
            shell.outerGap * 3.0f;
        _reportModel.setViewport(
            static_cast<float>(dimensions.x),
            static_cast<float>(dimensions.y),
            shell.drawerX + workbench.composerWidth + shell.outerGap,
            shell.topBarHeight, bottomInset);
        applyReportGeometry();
    }

    void ProcessEvent(Rml::Event& event) override
    {
        Rml::Element* target = event.GetTargetElement();
        if (target && target->GetId() == "science-help")
            ++_helpClicks;
        if (target && target->GetId() == "run-action")
            ++_runClicks;
    }

    float width(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        return element ? element->GetOffsetWidth() : 0.0f;
    }

    float height(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        return element ? element->GetOffsetHeight() : 0.0f;
    }

    float top(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        return element ? element->GetAbsoluteOffset().y : 0.0f;
    }

    float bottom(const char* id) const
    {
        return top(id) + height(id);
    }

    float left(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        return element ? element->GetAbsoluteOffset().x : 0.0f;
    }

    float horizontalOverflow(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        return element
            ? element->GetScrollWidth() - element->GetOffsetWidth() : 0.0f;
    }

    float scrollWidth(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        return element ? element->GetScrollWidth() : 0.0f;
    }

    float scrollTop(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        return element ? element->GetScrollTop() : 0.0f;
    }

    void setVisible(const char* id, bool visible)
    {
        if (Rml::Element* element = _document
                ? _document->GetElementById(id) : nullptr)
            element->SetProperty("display", visible ? "block" : "none");
    }

    Rml::Vector2f center(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        if (!element) return Rml::Vector2f(-1.0f, -1.0f);
        return element->GetAbsoluteOffset() +
            Rml::Vector2f(element->GetOffsetWidth() * 0.5f,
                          element->GetOffsetHeight() * 0.5f);
    }

    bool selectBoxVisible(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        Rml::ElementFormControlSelect* select =
            element
                ? rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(element)
                : nullptr;
        return select && select->IsSelectBoxVisible();
    }

    Rml::Vector2f optionCenter(const char* id, int index) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        Rml::ElementFormControlSelect* select =
            element
                ? rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(element)
                : nullptr;
        Rml::Element* option = select ? select->GetOption(index) : nullptr;
        if (!option) return Rml::Vector2f(-1.0f, -1.0f);
        return option->GetAbsoluteOffset() +
            Rml::Vector2f(option->GetOffsetWidth() * 0.5f,
                          option->GetOffsetHeight() * 0.5f);
    }

    float optionHeight(const char* id, int index) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        Rml::ElementFormControlSelect* select =
            element
                ? rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(element)
                : nullptr;
        Rml::Element* option = select ? select->GetOption(index) : nullptr;
        return option ? option->GetOffsetHeight() : 0.0f;
    }

    int helpClicks() const { return _helpClicks; }
    int runClicks() const { return _runClicks; }

    void showTrendReport()
    {
        Rml::Element* window = reportElement("science-report");
        if (!window) return;
        if (!_reportModel.active())
            _reportModel.openReady("composite-science-report");
        _reportModel.setSection("composite-science-report", "trends");
        applyReportGeometry();
        window->SetProperty("display", "block");
        if (Rml::Element* overview = reportElement("overview"))
            overview->SetProperty("display", "none");
        if (Rml::Element* trends = reportElement("trends"))
            trends->SetProperty("display", "block");
        setReportText("report-title", "ERA5 Agricultural Climate");
        setReportText("chart-title", "2 米平均气温");
        setReportText("chart-aggregation", "年度平均 · 每年一个结果值");
        setReportText("chart-unit", "°C");
        setReportText("chart-y-max", "28.1");
        setReportText("chart-y-mid", "27.2");
        setReportText("chart-y-min", "26.3");
        setReportText("chart-x-first", "2017");
        setReportText("chart-x-mid", "2021");
        setReportText("chart-x-last", "2025");
        setReportText(
            "chart-domain", "2017–2025 · 2 米平均气温");
        setReportText("chart-missing-note",
            "年度序列完整；点击曲线可固定一个年份。");
        setReportText(
            "chart-statistics",
            "均值 26.9 °C · 首末变化 -0.3 °C · 有效年份 9/9");
        if (Rml::Element* metric = reportElement("report-metric-select"))
            metric->SetInnerRML(
                "<option selected value='temperature'>2 米平均气温 · °C</option>");
        if (RmlScienceChart* chart = rmlui_dynamic_cast<RmlScienceChart*>(
                reportElement("science-chart")))
        {
            const std::vector<int> years =
                {2017, 2018, 2019, 2020, 2021, 2022, 2023, 2024, 2025};
            const std::vector<double> values =
                {26.7, 26.6, 26.5, 26.8, 27.0, 27.7, 27.3, 27.1, 26.4};
            chart->setModel(makeScienceChartModel(
                "mean_temperature_2m", "2 米平均气温", "°C",
                years, values), 2023);
        }
    }

    void showOverviewReport()
    {
        Rml::Element* window = reportElement("science-report");
        if (!window) return;
        if (!_reportModel.active())
            _reportModel.openReady("composite-science-report");
        _reportModel.setSection("composite-science-report", "overview");
        applyReportGeometry();
        window->SetProperty("display", "block");
        if (Rml::Element* overview = reportElement("overview"))
            overview->SetProperty("display", "block");
        if (Rml::Element* trends = reportElement("trends"))
            trends->SetProperty("display", "none");
        setReportText("report-title", "ERA5 Agricultural Climate");
        setReportText(
            "context-placeholder",
            "地图快照尚未就绪；不会用白底冒充分析范围。");
        setReportText(
            "context-caption",
            "固定分析位置 · 24.9752°, 102.0031°");
        setReportText(
            "overview-summary",
            "2017–2025 · 9 个年度 · 6 项独立单位指标");
        setReportText("overview-time-range", "2017–2025");
        setReportText(
            "overview-spatial-support", "ERA5 0.25° 数据网格");
        setReportText("overview-completeness", "54/54 个年度值");
    }

    void hideTrendReport()
    {
        if (Rml::Element* window = reportElement("science-report"))
            window->SetProperty("display", "none");
    }

    float reportWidth(const char* id) const
    {
        Rml::Element* element = reportElement(id);
        return element ? element->GetOffsetWidth() : 0.0f;
    }

    float reportHeight(const char* id) const
    {
        Rml::Element* element = reportElement(id);
        return element ? element->GetOffsetHeight() : 0.0f;
    }

    float reportRight(const char* id) const
    {
        Rml::Element* element = reportElement(id);
        return element
            ? element->GetAbsoluteOffset().x + element->GetOffsetWidth() : 0.0f;
    }

    float reportLeft(const char* id) const
    {
        Rml::Element* element = reportElement(id);
        return element ? element->GetAbsoluteOffset().x : 0.0f;
    }

    float reportTop(const char* id) const
    {
        Rml::Element* element = reportElement(id);
        return element ? element->GetAbsoluteOffset().y : 0.0f;
    }

    float reportBottom(const char* id) const
    {
        Rml::Element* element = reportElement(id);
        return element
            ? element->GetAbsoluteOffset().y + element->GetOffsetHeight() : 0.0f;
    }

    float reportHorizontalOverflow(const char* id) const
    {
        Rml::Element* element = reportElement(id);
        return element
            ? element->GetScrollWidth() - element->GetOffsetWidth() : 0.0f;
    }

    std::string value(const char* id) const
    {
        Rml::Element* element = _document
            ? _document->GetElementById(id) : nullptr;
        Rml::ElementFormControl* control =
            element ? rmlui_dynamic_cast<Rml::ElementFormControl*>(element)
                    : nullptr;
        return control ? std::string(control->GetValue()) : std::string();
    }

    void prepareCleanEvidenceState()
    {
        if (Rml::Element* year = _document
                ? _document->GetElementById("first-year") : nullptr)
            if (Rml::ElementFormControl* control =
                    rmlui_dynamic_cast<Rml::ElementFormControl*>(year))
                control->SetValue("2017");
        if (Rml::Element* source = _document
                ? _document->GetElementById("source-select") : nullptr)
            if (Rml::ElementFormControl* control =
                    rmlui_dynamic_cast<Rml::ElementFormControl*>(source))
                control->SetValue("era5-agro");
        if (Rml::Element* name = _document
                ? _document->GetElementById("analysis-name") : nullptr)
            name->SetInnerRML("ERA5 Agricultural Climate");
        if (Rml::Element* summary = _document
                ? _document->GetElementById("analysis-summary") : nullptr)
            summary->SetInnerRML(
                "历史农业气象年度分析 · 约 25–28 km 数据网格");
    }

private:
    void applyReportGeometry()
    {
        const ScienceReportWindowState* state = _reportModel.active();
        Rml::Element* window = reportElement("science-report");
        if (!state || !window) return;
        window->SetProperty(
            "left", std::to_string(state->position.x()) + "px");
        window->SetProperty(
            "top", std::to_string(state->position.y()) + "px");
        window->SetProperty(
            "width", std::to_string(state->size.x()) + "px");
        window->SetProperty(
            "height", std::to_string(state->size.y()) + "px");
    }

    Rml::Element* reportElement(const char* id) const
    {
        return _report ? _report->GetElementById(id) : nullptr;
    }

    void setReportText(const char* id, const char* value)
    {
        if (Rml::Element* element = reportElement(id))
            element->SetInnerRML(value);
    }

    Rml::ElementDocument* _document = nullptr;
    Rml::ElementDocument* _report = nullptr;
    Rml::Vector2i _lastDimensions{-1, -1};
    ScienceReportWindowModel _reportModel;
    int _helpClicks = 0;
    int _runClicks = 0;
};

class ProductShellCallback : public osgVerse::CameraDrawCallback
{
public:
    explicit ProductShellCallback(RmlUiRuntime& runtime)
        : _runtime(runtime), _core(nullptr, nullptr)
    {
        _state.activeModule = earthui::EarthUiModule::Science;
        _state.drawerOpen = true;
    }

    bool initialize(const std::string& fontPath, std::string& error)
    {
        if (ImGui::GetCurrentContext())
        {
            error = "unexpected existing ImGui context";
            return false;
        }
        IMGUI_CHECKVERSION();
        _context = ImGui::CreateContext();
        ImGui::SetCurrentContext(_context);
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = 1.0f / 60.0f;
        io.IniFilename = nullptr;
        if (!io.Fonts->AddFontFromFileTTF(
                fontPath.c_str(), 20.0f, nullptr,
                io.Fonts->GetGlyphRangesChineseFull()))
        {
            error = "product shell font did not load";
            return false;
        }
        if (!ImGui_ImplOpenGL3_Init("#version 410 core"))
        {
            error = "product shell OpenGL backend did not initialize";
            return false;
        }
        _backendReady = true;
        return true;
    }

    void shutdown()
    {
        ImGui::SetCurrentContext(_context);
        if (_backendReady)
        {
            ImGui_ImplOpenGL3_Shutdown();
            _backendReady = false;
        }
        if (_context)
        {
            ImGui::DestroyContext(_context);
            _context = nullptr;
        }
    }

    void operator()(osg::RenderInfo&) const override
    {
        if (!_context || !_backendReady) return;
        ImGui::SetCurrentContext(_context);
        Rml::Context* rml = _runtime.context();
        const Rml::Vector2i dimensions =
            rml ? rml->GetDimensions() : Rml::Vector2i(WIDTH, HEIGHT);
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(
            static_cast<float>(dimensions.x),
            static_cast<float>(dimensions.y));
        ImGui_ImplOpenGL3_NewFrame();
        ImGui::NewFrame();
        earthui::applyEarthUiV2Theme();
        const earthui::EarthUiShellLayout shell =
            earthui::computeEarthUiShellLayout(
                io.DisplaySize.x, io.DisplaySize.y, true);
        earthui::EarthUiTopBarData top;
        top.latitudeDeg = 24.9752;
        top.longitudeDeg = 102.0031;
        top.altitudeKm = 1336.7;
        top.scienceAvailable = true;
        earthui::drawEarthUiTopBar(shell, _state, top);
        earthui::drawEarthUiModuleRail(shell, _state);
        earthui::drawEarthUiContextTray(shell, _state);
        _command.draw(&_core, nullptr, nullptr, shell, _cards);
        _cards.draw();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

private:
    RmlUiRuntime& _runtime;
    mutable earthui::EarthUiShellState _state;
    mutable earthai::AIChatCore _core;
    mutable AIChatUI _command;
    mutable earthui::CardStack _cards;
    ImGuiContext* _context = nullptr;
    bool _backendReady = false;
};

class CompositeClearCallback : public osgVerse::CameraDrawCallback
{
public:
    explicit CompositeClearCallback(RmlUiRuntime& runtime)
        : _runtime(runtime) {}

    void operator()(osg::RenderInfo& renderInfo) const override
    {
        const Rml::Context* context = _runtime.context();
        const Rml::Vector2i dimensions = context
            ? context->GetDimensions()
            : Rml::Vector2i(WIDTH, HEIGHT);
        osg::State* state = renderInfo.getState();
        osg::GraphicsContext* graphics =
            state ? state->getGraphicsContext() : nullptr;
        glBindFramebuffer(
            GL_FRAMEBUFFER, graphics ? graphics->getDefaultFboId() : 0);
        glViewport(0, 0, dimensions.x, dimensions.y);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                GL_STENCIL_BUFFER_BIT);
        if (getSubCallback()) getSubCallback()->run(renderInfo);
    }

private:
    RmlUiRuntime& _runtime;
};

void expectProductCommandFits(
    const earthui::EarthUiShellLayout& shell,
    float viewportWidth, float viewportHeight,
    float workbenchRight, const std::string& viewportLabel)
{
    ImGuiWindow* command = ImGui::FindWindowByName(u8"AI 对话条");
    expect(command != nullptr,
           viewportLabel + ": production AI command window is absent");
    expect(command->Pos.x >= workbenchRight + shell.outerGap - 1.0f,
           viewportLabel +
               ": production AI command overlaps the Science workbench; "
               "command_left=" + std::to_string(command->Pos.x) +
               ", workbench_right=" + std::to_string(workbenchRight));
    expect(command->Pos.x + command->Size.x <=
               viewportWidth - shell.insightWidth -
                   shell.outerGap * 2.0f + 1.0f,
           viewportLabel +
               ": production AI command enters the Insight Lens track");
    expect(command->Pos.y >= -0.5f &&
               command->Pos.y + command->Size.y <=
                   shell.contextY - shell.outerGap + 1.0f &&
               command->Pos.y + command->Size.y <= viewportHeight + 0.5f,
           viewportLabel +
               ": production AI command leaves its vertical safe region; "
               "top=" + std::to_string(command->Pos.y) +
               ", height=" + std::to_string(command->Size.y) +
               ", safe_bottom=" +
                   std::to_string(shell.contextY - shell.outerGap));
    expect(!command->ScrollbarX && !command->ScrollbarY &&
               command->ScrollMax.x <= 0.5f &&
               command->ScrollMax.y <= 0.5f,
           viewportLabel +
               ": production AI command clips controls behind a scrollbar");
}

bool writePpm(const std::filesystem::path& path,
              const std::vector<unsigned char>& rgba,
              int width = WIDTH, int height = HEIGHT)
{
    FILE* file = std::fopen(path.string().c_str(), "wb");
    if (!file) return false;
    std::fprintf(file, "P6\n%d %d\n255\n", width, height);
    for (int y = height - 1; y >= 0; --y)
    {
        const unsigned char* row = rgba.data() +
            static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x)
            std::fwrite(row + x * 4, 1, 3, file);
    }
    return std::fclose(file) == 0;
}

void sendPointerEvent(RmlUiRuntime& runtime,
                      osgGA::GUIEventAdapter::EventType type,
                      const Rml::Vector2f& position, int button = 0)
{
    osg::ref_ptr<osgGA::GUIEventAdapter> event =
        new osgGA::GUIEventAdapter;
    event->setEventType(type);
    event->setX(position.x);
    event->setY(position.y);
    event->setMouseYOrientation(
        osgGA::GUIEventAdapter::Y_INCREASING_DOWNWARDS);
    if (button != 0) event->setButton(button);
    runtime.processEvent(*event);
}

void sendWheelEvent(RmlUiRuntime& runtime,
                    osgGA::GUIEventAdapter::ScrollingMotion motion)
{
    osg::ref_ptr<osgGA::GUIEventAdapter> event =
        new osgGA::GUIEventAdapter;
    event->setEventType(osgGA::GUIEventAdapter::SCROLL);
    event->setScrollingMotion(motion);
    runtime.processEvent(*event);
}

void sendResizeEvent(RmlUiRuntime& runtime, int width, int height)
{
    osg::ref_ptr<osgGA::GUIEventAdapter> event =
        new osgGA::GUIEventAdapter;
    event->setEventType(osgGA::GUIEventAdapter::RESIZE);
    event->setWindowRectangle(0, 0, width, height);
    runtime.processEvent(*event);
}
}

int main()
{
    osg::ref_ptr<osg::GraphicsContext::Traits> traits =
        new osg::GraphicsContext::Traits;
    traits->width = WIDTH;
    traits->height = HEIGHT;
    traits->red = traits->green = traits->blue = traits->alpha = 8;
    traits->depth = 24;
    traits->stencil = 8;
    traits->doubleBuffer = false;
    osg::ref_ptr<HeadlessCglContext> graphics = new HeadlessCglContext(traits);

    osgViewer::Viewer viewer;
    osg::Camera* camera = viewer.getCamera();
    camera->setGraphicsContext(graphics);
    camera->setViewport(0, 0, WIDTH, HEIGHT);
    camera->setClearColor(osg::Vec4(1.0f, 0.0f, 1.0f, 1.0f));
    camera->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                         GL_STENCIL_BUFFER_BIT);
    viewer.setSceneData(new osg::Group);

    DocumentClient client;
    RmlUiRuntime runtime;
    runtime.setFrameClient(&client);
    std::string error;
    expect(runtime.attach(
        viewer, *camera,
        std::string(OSGVERSE_SOURCE_DIR) + "/assets/misc/LXGWFasmartGothic.otf",
        96.0f, error), "RmlUi runtime attach failed: " + error);
    expect(graphics->realize(), "headless CGL 4.1 context did not realize");
    expect(graphics->makeCurrent(), "headless context could not become current");
    osg::ref_ptr<ProductShellCallback> productShell =
        new ProductShellCallback(runtime);
    expect(productShell->initialize(
        std::string(OSGVERSE_SOURCE_DIR) +
            "/assets/misc/LXGWFasmartGothic.otf",
        error), "product shell initialization failed: " + error);
    productShell->setup(camera, 2);
    osgVerse::CameraDrawCallback* rmlCallback =
        dynamic_cast<osgVerse::CameraDrawCallback*>(
            camera->getPostDrawCallback());
    expect(rmlCallback != nullptr,
           "RmlUi callback is not composable with the product shell");
    osg::ref_ptr<CompositeClearCallback> clearFrame =
        new CompositeClearCallback(runtime);
    clearFrame->setSubCallback(rmlCallback);
    camera->setPostDrawCallback(clearFrame);
    const std::filesystem::path evidenceDirectory =
        std::filesystem::path(OSGSOL_UI_EVIDENCE_DIR) /
        "science-composite";
    std::filesystem::create_directories(evidenceDirectory);
    glBindFramebuffer(GL_FRAMEBUFFER, graphics->getDefaultFboId());
    glViewport(0, 0, WIDTH, HEIGHT);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    osg::RenderInfo renderInfo(graphics->getState(), &viewer);
    renderInfo.pushCamera(camera);
    expect(camera->getPostDrawCallback() != nullptr,
           "RmlUi attach did not install the camera draw callback");
    (*camera->getPostDrawCallback())(renderInfo);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(!runtime.failed(), "RmlUi runtime entered failed state");
    expect(runtime.ready(),
           "RmlUi callback did not initialize the real workbench on a frame");
    sendResizeEvent(runtime, 1024, 576);
    (*camera->getPostDrawCallback())(renderInfo);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(runtime.context()->GetDimensions() == Rml::Vector2i(1024, 576),
           "RmlUi context ignores a real window resize event");
    expect(client.width("science-workbench") <= 380.0f &&
               client.bottom("science-workbench") <= 576.0f + 1.0f,
           "compact Science workbench leaves the resized viewport; width=" +
               std::to_string(client.width("science-workbench")) +
               ", bottom=" +
               std::to_string(client.bottom("science-workbench")));
    {
        const earthui::EarthUiShellLayout compactShell =
            earthui::computeEarthUiShellLayout(1024.0f, 576.0f, true);
        expect(std::abs(
                   client.left("science-workbench") -
                   compactShell.drawerX) <= 1.0f,
               "compact Science workbench is detached from the module rail");
        expect(std::abs(
                   client.top("science-workbench") -
                   compactShell.drawerY) <= 1.0f,
               "compact Science workbench is detached from the top bar");
        expect(client.bottom("science-workbench") <=
                   compactShell.commandY - compactShell.outerGap + 1.0f,
               "compact Science workbench overlaps the AI command deck");
        expectProductCommandFits(
            compactShell, 1024.0f, 576.0f,
            client.left("science-workbench") +
                client.width("science-workbench"),
            "1024x576");
        glBindFramebuffer(GL_FRAMEBUFFER, graphics->getDefaultFboId());
        glFinish();
        std::vector<unsigned char> compactRgba(
            static_cast<std::size_t>(1024) * 576 * 4);
        glReadPixels(
            0, 0, 1024, 576, GL_RGBA, GL_UNSIGNED_BYTE,
            compactRgba.data());
        expect(writePpm(
                   evidenceDirectory / "workbench-shell-1024x576.ppm",
                   compactRgba, 1024, 576),
               "could not write compact composite UI evidence");
    }
    expect(client.horizontalOverflow("composer-scroll") <= 1.0f,
           "compact Science workbench overflows horizontally after resize");
    sendResizeEvent(runtime, MID_WIDTH, MID_HEIGHT);
    (*camera->getPostDrawCallback())(renderInfo);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(runtime.context()->GetDimensions() ==
               Rml::Vector2i(MID_WIDTH, MID_HEIGHT),
           "RmlUi context ignores the medium product viewport");
    {
        const earthui::EarthUiShellLayout mediumShell =
            earthui::computeEarthUiShellLayout(
                static_cast<float>(MID_WIDTH),
                static_cast<float>(MID_HEIGHT), true);
        expect(std::abs(
                   client.left("science-workbench") -
                   mediumShell.drawerX) <= 1.0f,
               "medium Science workbench is detached from the module rail");
        expect(client.bottom("science-workbench") <=
                   mediumShell.commandY - mediumShell.outerGap + 1.0f,
               "medium Science workbench overlaps the AI command deck");
        expectProductCommandFits(
            mediumShell,
            static_cast<float>(MID_WIDTH),
            static_cast<float>(MID_HEIGHT),
            client.left("science-workbench") +
                client.width("science-workbench"),
            "1280x720");
        glBindFramebuffer(GL_FRAMEBUFFER, graphics->getDefaultFboId());
        glFinish();
        std::vector<unsigned char> mediumRgba(
            static_cast<std::size_t>(MID_WIDTH) * MID_HEIGHT * 4);
        glReadPixels(
            0, 0, MID_WIDTH, MID_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
            mediumRgba.data());
        expect(writePpm(
                   evidenceDirectory / "workbench-shell-1280x720.ppm",
                   mediumRgba, MID_WIDTH, MID_HEIGHT),
               "could not write medium composite UI evidence");
    }
    sendResizeEvent(runtime, WIDTH, HEIGHT);
    (*camera->getPostDrawCallback())(renderInfo);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(runtime.context()->GetDimensions() ==
               Rml::Vector2i(WIDTH, HEIGHT),
           "RmlUi context cannot return to the full viewport after resize");
    expect(client.width("source-select") > 260.0f,
           "analysis selector is visually collapsed; width=" +
               std::to_string(client.width("source-select")) +
               ", workbench=" +
               std::to_string(client.width("science-workbench")));
    {
        const earthui::EarthUiShellLayout fullShell =
            earthui::computeEarthUiShellLayout(
                static_cast<float>(WIDTH),
                static_cast<float>(HEIGHT), true);
        expectProductCommandFits(
            fullShell,
            static_cast<float>(WIDTH),
            static_cast<float>(HEIGHT),
            client.left("science-workbench") +
                client.width("science-workbench"),
            "1440x900");
    }
    expect(client.width("source-chevron") >= 24.0f &&
               client.height("source-chevron") >= 24.0f,
           "analysis selector has no visible dropdown chevron");
    expect(client.width("unlocked-target-action") > 260.0f &&
               client.height("unlocked-target-action") >= 30.0f,
           "target action menu is visually collapsed");
    expect(client.width("first-year") > 56.0f,
           "year inputs are visually collapsed");
    expect(client.height("analysis-summary") > 28.0f,
           "long Chinese analysis summary is clipped instead of wrapped");
    const Rml::Vector2f helpCenter = client.center("science-help");
    expect(helpCenter.x >= 0.0f && helpCenter.y >= 0.0f,
           "help button has no usable screen position");
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::MOVE, helpCenter);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::PUSH, helpCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::RELEASE, helpCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(client.helpClicks() == 1,
           "downward-origin macOS pointer click did not reach RmlUi control");
    expect(client.horizontalOverflow("composer-scroll") <= 1.0f,
           "workbench content exceeds the panel width; panel=" +
           std::to_string(client.width("composer-scroll")) + ", scroll=" +
           std::to_string(client.scrollWidth("composer-scroll")));
    expect(client.horizontalOverflow("science-help-copy") <= 1.0f,
           "Chinese help copy overflows horizontally instead of wrapping");
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::PUSH, helpCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::RELEASE, helpCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    client.setVisible("science-help-copy", false);
    (*camera->getPostDrawCallback())(renderInfo);

    const Rml::Vector2f sourceCenter = client.center("source-select");
    const std::string sourceBefore = client.value("source-select");
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::MOVE, sourceCenter);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::PUSH, sourceCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::RELEASE, sourceCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(client.selectBoxVisible("source-select"),
           "clicking the analysis selector did not open its menu");
    Rml::Vector2f secondOption;
    Rml::Vector2f previousOption;
    float previousHeight = 0.0f;
    for (int index = 0; index < 5; ++index)
    {
        const Rml::Vector2f option =
            client.optionCenter("source-select", index);
        const float optionHeight =
            client.optionHeight("source-select", index);
        expect(optionHeight >= 30.0f,
               "analysis menu option has no readable row height at index " +
               std::to_string(index));
        if (index > 0)
            expect(option.y - previousOption.y >=
                       (previousHeight + optionHeight) * 0.45f,
                   "analysis menu option labels overlap vertically at index " +
                   std::to_string(index));
        if (index == 1) secondOption = option;
        previousOption = option;
        previousHeight = optionHeight;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, graphics->getDefaultFboId());
    expect(glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
               GL_FRAMEBUFFER_COMPLETE,
           "composite framebuffer is incomplete");
    glFinish();
    std::vector<unsigned char> menuRgba(
        static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
    glReadPixels(
        0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, menuRgba.data());
    expect(writePpm(evidenceDirectory / "menu-open.ppm", menuRgba),
           "could not write opened analysis menu evidence image");
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::MOVE, secondOption);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::PUSH, secondOption,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::RELEASE, secondOption,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(client.value("source-select") != sourceBefore,
           "clicking a visible analysis menu row cannot choose it");

    const Rml::Vector2f yearCenter = client.center("first-year");
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::MOVE, yearCenter);
    (*camera->getPostDrawCallback())(renderInfo);
    Rml::Element* yearHover = runtime.context()->GetHoverElement();
    const std::string yearHoverTag = yearHover
        ? std::string(yearHover->GetTagName()) : std::string("<none>");
    const std::string yearHoverId = yearHover
        ? std::string(yearHover->GetId()) : std::string("<none>");
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::PUSH, yearCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::RELEASE, yearCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    const std::string yearBeforeText = client.value("first-year");
    runtime.enqueueCommittedText("9");
    (*camera->getPostDrawCallback())(renderInfo);
    expect(client.value("first-year") != yearBeforeText,
           "clicking a visible year input does not enable text editing; "
           "hover=" + yearHoverTag + "#" + yearHoverId +
           " ancestry=" + ancestry(yearHover) +
           ", center=" + std::to_string(yearCenter.x) + "," +
           std::to_string(yearCenter.y));
    Rml::Element* focusBeforeTab = runtime.context()->GetFocusElement();
    expect(focusBeforeTab != nullptr,
           "clicked year field did not enter the document focus order");
    runtime.enqueueKeyTap(RmlInputKey::Tab);
    (*camera->getPostDrawCallback())(renderInfo);
    Rml::Element* focusAfterTab = runtime.context()->GetFocusElement();
    expect(focusAfterTab != nullptr && focusAfterTab != focusBeforeTab,
           "Tab does not advance focus through the visible Science workflow;"
           " before=" + ancestry(focusBeforeTab) +
           ", after=" + ancestry(focusAfterTab));
    const float scrollBefore = client.scrollTop("composer-scroll");
    sendWheelEvent(runtime, osgGA::GUIEventAdapter::SCROLL_DOWN);
    (*camera->getPostDrawCallback())(renderInfo);
    const float scrollAfterDown = client.scrollTop("composer-scroll");
    expect(scrollAfterDown > scrollBefore,
           "visible workbench scrollbar does not respond to wheel down");
    sendWheelEvent(runtime, osgGA::GUIEventAdapter::SCROLL_UP);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(client.scrollTop("composer-scroll") < scrollAfterDown,
           "workbench cannot scroll upward after scrolling down");
    const Rml::Vector2f runCenter = client.center("run-action");
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::MOVE, runCenter);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::PUSH, runCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    sendPointerEvent(runtime, osgGA::GUIEventAdapter::RELEASE, runCenter,
                     osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(client.runClicks() == 1,
           "visible Start Analysis CTA does not receive a real pointer click");
    client.setVisible("run-action", false);
    client.setVisible("run-feedback", false);
    client.setVisible("progress-footer", true);
    (*camera->getPostDrawCallback())(renderInfo);
    expect(client.height("run-footer") <= 74.0f,
           "running footer is too tall and hollows out the panel");
    expect(client.height("progress-footer") <= 54.0f,
           "running status controls overflow into a second footer");
    expect(client.bottom("cancel-action") <=
               client.bottom("run-footer") + 1.0f,
           "cancel action overflows below the running footer");
    expect(client.horizontalOverflow("progress-footer") <= 1.0f,
           "running status controls overflow horizontally");
    client.prepareCleanEvidenceState();
    (*camera->getPostDrawCallback())(renderInfo);
    glBindFramebuffer(GL_FRAMEBUFFER, graphics->getDefaultFboId());
    glFinish();
    std::vector<unsigned char> runningRgba(
        static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
                 runningRgba.data());
    expect(writePpm(evidenceDirectory / "running.ppm", runningRgba),
           "could not write compact running-state evidence image");

    client.showOverviewReport();
    (*camera->getPostDrawCallback())(renderInfo);
    glBindFramebuffer(GL_FRAMEBUFFER, graphics->getDefaultFboId());
    glFinish();
    std::vector<unsigned char> overviewRgba(
        static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
                 overviewRgba.data());
    const int overviewSampleX = static_cast<int>(
        client.reportLeft("context-thumbnail") + 20.0f);
    const int overviewSampleY = HEIGHT - 1 - static_cast<int>(
        client.reportTop("context-thumbnail") + 20.0f);
    expect(overviewSampleX >= 0 && overviewSampleX < WIDTH &&
               overviewSampleY >= 0 && overviewSampleY < HEIGHT,
           "report overview thumbnail sample lies outside the framebuffer");
    const std::size_t overviewSample =
        (static_cast<std::size_t>(overviewSampleY) * WIDTH +
         overviewSampleX) * 4;
    expect(overviewRgba[overviewSample] < 80 &&
               overviewRgba[overviewSample + 1] < 80 &&
               overviewRgba[overviewSample + 2] < 80,
           "report overview falls back to a blank white canvas");
    expect(writePpm(evidenceDirectory / "overview.ppm", overviewRgba),
           "could not write scientific overview evidence image");

    client.showTrendReport();
    (*camera->getPostDrawCallback())(renderInfo);
    expect(client.reportWidth("science-report") >= 780.0f &&
               client.reportHeight("science-report") >= 620.0f,
           "scientific report collapsed below its readable test size; " +
               std::to_string(client.reportWidth("science-report")) + "x" +
               std::to_string(client.reportHeight("science-report")));
    expect(client.reportRight("science-report") <= WIDTH + 1.0f &&
               client.reportBottom("science-report") <= HEIGHT + 1.0f,
           "scientific report extends beyond the viewport");
    {
        const earthui::EarthUiShellLayout shell =
            earthui::computeEarthUiShellLayout(
                static_cast<float>(WIDTH),
                static_cast<float>(HEIGHT), true);
        expect(client.reportLeft("science-report") >=
                   shell.drawerX +
                   earthui::computeScienceWorkbenchLayout(
                       static_cast<float>(WIDTH),
                       static_cast<float>(HEIGHT)).composerWidth +
                   shell.outerGap - 1.0f,
               "scientific report covers the Science composer");
        expect(client.reportTop("science-report") >=
                   shell.topBarHeight + shell.outerGap - 1.0f,
               "scientific report covers the product top bar");
        expect(client.reportBottom("science-report") <=
                   shell.commandY - shell.outerGap + 1.0f,
               "scientific report covers the AI command deck");
        ImGuiWindow* command = ImGui::FindWindowByName(u8"AI 对话条");
        expect(command != nullptr,
               "production AI command disappeared behind the report");
        expect(client.reportBottom("science-report") <=
                   command->Pos.y - shell.outerGap + 1.0f,
               "scientific report overlaps the actual production AI command; "
               "report_bottom=" +
                   std::to_string(client.reportBottom("science-report")) +
               ", command_top=" + std::to_string(command->Pos.y));
    }
    expect(client.reportRight("report-close") <=
               client.reportRight("science-report") - 8.0f,
           "report window actions are clipped at the right edge; close=" +
           std::to_string(client.reportRight("report-close")) +
           ", window=" +
           std::to_string(client.reportRight("science-report")));
    expect(client.reportHorizontalOverflow("report-content") <= 1.0f,
           "scientific report content overflows horizontally");
    expect(client.reportWidth("chart-y-max") >= 18.0f &&
               client.reportWidth("chart-x-first") >= 24.0f,
           "scientific chart axes are not visibly laid out");
    glBindFramebuffer(GL_FRAMEBUFFER, graphics->getDefaultFboId());
    glFinish();
    std::vector<unsigned char> reportRgba(
        static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE,
                 reportRgba.data());
    expect(writePpm(evidenceDirectory / "trend-report.ppm", reportRgba),
           "could not write scientific report evidence image");
    client.hideTrendReport();
    glBindFramebuffer(GL_FRAMEBUFFER, graphics->getDefaultFboId());
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
            GL_STENCIL_BUFFER_BIT);
    (*camera->getPostDrawCallback())(renderInfo);

    glBindFramebuffer(GL_FRAMEBUFFER, graphics->getDefaultFboId());
    glFinish();
    std::vector<unsigned char> rgba(
        static_cast<std::size_t>(WIDTH) * HEIGHT * 4);
    glReadPixels(0, 0, WIDTH, HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    graphics->releaseContext();

    std::size_t changed = 0;
    std::size_t dark = 0;
    for (std::size_t i = 0; i < rgba.size(); i += 4)
    {
        const bool isChanged =
            rgba[i] < 245 || rgba[i + 1] > 10 || rgba[i + 2] < 245;
        if (isChanged)
            ++changed;
        if (rgba[i] < 80 && rgba[i + 1] < 80 && rgba[i + 2] < 80)
            ++dark;
    }
    expect(changed > static_cast<std::size_t>(WIDTH * HEIGHT / 20),
           "RmlUi reported ready but did not draw visible workbench pixels");
    expect(changed < static_cast<std::size_t>(WIDTH * HEIGHT * 3 / 4),
           "framebuffer evidence is uniformly changed instead of a UI panel");
    expect(dark > static_cast<std::size_t>(WIDTH * HEIGHT / 20),
           "workbench dark surface was not present in the rendered frame");
    const earthui::EarthUiShellLayout shell =
        earthui::computeEarthUiShellLayout(
            static_cast<float>(WIDTH), static_cast<float>(HEIGHT), true);
    expect(std::abs(
               client.left("science-workbench") - shell.drawerX) <= 1.0f,
           "Science workbench and module rail contain a transparent seam");
    expect(client.bottom("science-workbench") <=
               shell.commandY - shell.outerGap + 1.0f,
           "Science workbench covers the AI command deck");
    expect(writePpm(evidenceDirectory / "workbench-shell.ppm", rgba),
           "could not write RmlUi GL evidence image");

    productShell->shutdown();
    runtime.shutdown();
    runtime.setFrameClient(nullptr);
    graphics->releaseContext();
    std::cout << "[OK] RmlUi real GL workbench rendered; changed_pixels="
              << changed << "; evidence=" << evidenceDirectory.string()
              << "\n";
    return 0;
}
