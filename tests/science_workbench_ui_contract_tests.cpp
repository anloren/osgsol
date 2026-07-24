#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

std::string read(const std::string& relative)
{
    std::ifstream input(std::string(OSGVERSE_SOURCE_DIR) + "/" + relative);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    expect(input.good() || input.eof(), "workbench asset must be readable");
    return buffer.str();
}

std::size_t count(const std::string& text, const std::string& needle)
{
    std::size_t result = 0;
    for (std::size_t position = 0;
         (position = text.find(needle, position)) != std::string::npos;
         position += needle.size())
        ++result;
    return result;
}
}

int main()
{
    const std::string rml = read("assets/misc/ui/scienceearth/workbench.rml");
    const std::string report = read("assets/misc/ui/scienceearth/report.rml");
    const std::string tokens = read("assets/misc/ui/scienceearth/tokens.rcss");
    const std::string style = read("assets/misc/ui/scienceearth/scienceearth.rcss");
    const std::string presenter = read(
        "applications/earth_explorer/science_ui/"
        "science_workbench_presenter.cpp");
    const std::string nativeTokens = read(
        "applications/earth_explorer/earth_ui_tokens.h");

    const std::vector<std::string> requiredIds = {
        "analysis-template", "target-card", "time-range",
        "method-options", "cost-disclosure", "run-footer",
        "progress-footer", "focus-target"};
    for (const std::string& id : requiredIds)
        expect(rml.find("id=\"" + id + "\"") != std::string::npos,
               "required connected-workflow element ID is missing");

    expect(count(rml, "class=\"primary-action disabled\"") == 0 &&
               rml.find("id=\"run-action\" class=\"primary-action\"") !=
                   std::string::npos,
           "analysis CTA must remain actionable before an explicit target lock");
    expect(rml.find("开始分析时会自动锁定地图中心") != std::string::npos,
           "composer must explain the automatic target lock beside the CTA");
    expect(rml.find("data-action=\"lock-map-center\"") != std::string::npos,
           "target card must lock the map center");
    expect(rml.find("data-action=\"update-target\"") != std::string::npos,
           "target card must support an explicit target update");
    expect(rml.find("镜头移动不会改变已锁定范围") != std::string::npos,
           "immutable target explanation must be exact and visible");

    const std::size_t time = rml.find("id=\"time-range\"");
    const std::size_t first = rml.find("id=\"first-year\"", time);
    const std::size_t last = rml.find("id=\"last-year\"", time);
    const std::size_t timeEnd = rml.find("</section>", time);
    expect(time != std::string::npos && first > time && last > first &&
               timeEnd > last,
           "start and end years must be one grouped time-range region");
    expect(rml.find("id=\"year-range-summary\"") != std::string::npos &&
               rml.find("range-strip") == std::string::npos &&
               style.find(".range-strip") == std::string::npos,
           "time range must use an explicit year summary, never a decorative"
           " bar that looks like an inoperable slider");

    const std::size_t disclosure = rml.find("id=\"cost-disclosure\"");
    expect(disclosure != std::string::npos &&
               rml.find("id=\"cost-toggle\"", disclosure) != std::string::npos &&
               rml.find("id=\"cost-body\"", disclosure) != std::string::npos &&
               style.find(".cost-body { display: none;") != std::string::npos,
           "resource estimate must be collapsed by default");
    expect(rml.find("aria-label=\"查看科学分析帮助\"") != std::string::npos,
           "icon controls need accessible Chinese names");
    expect(rml.find(" / ") == std::string::npos,
           "normal mode must not duplicate every label in English");

    expect(tokens.find("--science-run") != std::string::npos &&
               tokens.find("--science-failure") != std::string::npos,
           "tokens must distinguish primary action and failures");
    expect(style.find("overflow-y: auto") != std::string::npos &&
               style.find("scrollbar") != std::string::npos,
           "independent composer scrolling must remain visible");
    expect(style.find("scrollbar-margin") == std::string::npos,
           "scrollbars must stay inside their docked panel without exposing a"
           " transparent map seam");
    expect(style.find("scrollbarvertical sliderarrowdec") !=
               std::string::npos &&
               style.find("scrollbarvertical sliderarrowinc") !=
               std::string::npos &&
               style.find("min-height: 0") != std::string::npos,
           "generated scrollbar arrows must be collapsed instead of forming"
           " bright vertical strips");
    expect(style.find("select selectbox option") != std::string::npos &&
               style.find("min-height: 34px") != std::string::npos,
           "native Rml select menus need explicit readable option rows");
    expect(rml.find("id=\"source-chevron\"") != std::string::npos &&
               rml.find(">▼</span>") != std::string::npos &&
               style.find(".select-chevron") != std::string::npos &&
               style.find("pointer-events: none") != std::string::npos,
           "analysis selects need a visible non-interactive dropdown chevron");
    const std::size_t mainEnd = rml.find("</main>");
    const std::size_t runFeedback = rml.find("id=\"run-feedback\"");
    const std::size_t runAction = rml.find("id=\"run-action\"");
    expect(mainEnd != std::string::npos && runFeedback > mainEnd &&
               runAction > runFeedback &&
               style.find(".run-feedback") != std::string::npos,
           "submission feedback must stay visible in the sticky Run footer");
    expect(rml.find("class=\"progress-header\"") != std::string::npos &&
               style.find(".progress-footer {") != std::string::npos &&
               style.find("height: 50px") != std::string::npos,
           "running state must use one compact footer instead of a hollow"
           " overflowing panel");
    expect(presenter.find("正在提交分析任务") != std::string::npos &&
               presenter.find(
                   "_impl->setDisabled(\"run-action\", true);") !=
                   std::string::npos,
           "Run clicks need immediate visible feedback before the next snapshot");
    expect(presenter.find(
               "if (!_impl->state.locked)\n"
               "            _impl->enqueue(\"lock-map-center\", \"\");") !=
               std::string::npos &&
               presenter.find(
               "const bool canRun = !view.sourceId.empty()") !=
               std::string::npos,
           "Run must auto-lock the live map center before submitting");
    expect(report.find("<science-chart id=\"science-chart\"") !=
               std::string::npos &&
               report.find("id=\"report-metric-select\"") !=
                   std::string::npos &&
               report.find("id=\"chart-selected-year\"") !=
                   std::string::npos,
           "report needs one interactive metric chart with year selection");
    const std::vector<std::string> chartAxisIds = {
        "chart-title", "chart-aggregation", "chart-unit",
        "chart-y-max", "chart-y-mid", "chart-y-min",
        "chart-x-first", "chart-x-mid", "chart-x-last",
        "chart-missing-note"};
    for (const std::string& id : chartAxisIds)
        expect(report.find("id=\"" + id + "\"") != std::string::npos,
               "scientific chart title, unit, axis or missing-data label is"
               " absent");
    expect(style.find(".science-chart { display: block; height: 300px;") !=
               std::string::npos,
           "scientific chart needs a stable readable plot height");
    expect(style.find(".chart-y-axis") != std::string::npos &&
               style.find(".chart-x-axis") != std::string::npos,
           "scientific chart needs visible X/Y axis label containers");

    const std::vector<std::string> unifiedTokens = {
        "#07090a", "#0d1011", "#141719", "#3b302e", "#e5e4df",
        "#a7aaa6", "#38c3df", "#d33123", "#380f0c", "#e1bd62",
        "#ef6a62"};
    for (const std::string& token : unifiedTokens)
        expect(tokens.find(token) != std::string::npos &&
                   nativeTokens.find(token) != std::string::npos,
               "ImGui and RmlUi tokens must match the unified Obsidian Survey"
               " palette");
    const std::vector<std::string> reportIds = {
        "overview", "trends", "spatial-range", "methods-evidence",
        "context-caption", "report-focus-target", "requested-spatial-fact",
        "actual-spatial-fact", "source-links", "limitations",
        "report-artifact-id", "copy-artifact-id", "execution-timing",
        "export-capabilities"};
    for (const std::string& id : reportIds)
        expect(report.find("id=\"" + id + "\"") != std::string::npos,
               "report evidence element ID is missing");
    expect(count(report, "id=\"report-focus-target\"") == 1,
           "report must expose one explicit map focus action");

    std::cout << "Science workbench UI contract tests passed" << std::endl;
    return 0;
}
