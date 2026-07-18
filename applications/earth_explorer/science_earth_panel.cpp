#include "science_earth_panel.h"

#include "LayerManager.h"
#include "earth_control_layout.h"
#include "science_preview_layer.h"
#include "science_query_builder.h"

#include <ScienceQueryService.h>
#include <osg/Math>
#include <readerwriter/EarthManipulator.h>
#include <ui/ImGuiComponents.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
std::string lowerAscii(std::string value)
{
    for (char& character : value)
        character = static_cast<char>(std::tolower(
            static_cast<unsigned char>(character)));
    return value;
}

bool containsAny(const std::string& value,
                 const std::vector<std::string>& needles)
{
    const std::string lower = lowerAscii(value);
    for (const std::string& needle : needles)
        if (lower.find(needle) != std::string::npos) return true;
    return false;
}

const char* stageLabel(earthscience::ScienceProgressStage stage)
{
    using Stage = earthscience::ScienceProgressStage;
    switch (stage)
    {
    case Stage::Idle: return u8"尚未开始 / Idle";
    case Stage::Queued: return u8"等待执行 / Queued";
    case Stage::Locating: return u8"定位数据 / Locating";
    case Stage::Reading: return u8"读取数据 / Reading";
    case Stage::Decoding: return u8"解码向量 / Decoding";
    case Stage::Validating: return u8"验证数据 / Validating";
    case Stage::Aligning: return u8"对齐网格 / Aligning";
    case Stage::Analyzing: return u8"计算分析 / Analyzing";
    case Stage::Materializing: return u8"生成结果 / Materializing";
    case Stage::Cancelling: return u8"正在取消 / Cancelling";
    case Stage::Ready: return u8"结果就绪 / Ready";
    case Stage::Failed: return u8"处理失败 / Failed";
    case Stage::Cancelled: return u8"已取消 / Cancelled";
    }
    return u8"未知阶段 / Unknown";
}

bool artifactMatchesMode(const earthscience::ScienceArtifact& artifact,
                         SciencePanelMode mode)
{
    using AnalysisKind = earthscience::ScienceAnalysisKind;
    using OutputKind = earthscience::ScienceOutputKind;
    switch (mode)
    {
    case SciencePanelMode::Preview:
        return artifact.query.outputKind == OutputKind::RasterLayer;
    case SciencePanelMode::PointSeries:
        return artifact.query.outputKind == OutputKind::TimeSeries &&
            artifact.analysis.kind == AnalysisKind::PointSeries;
    case SciencePanelMode::RegionalChange:
        return artifact.query.outputKind == OutputKind::Analysis &&
            artifact.analysis.kind == AnalysisKind::RegionalChange;
    }
    return false;
}

bool queryMatchesMode(const earthscience::GeoTemporalQuery& query,
                      SciencePanelMode mode)
{
    using AnalysisKind = earthscience::ScienceAnalysisKind;
    using OutputKind = earthscience::ScienceOutputKind;
    switch (mode)
    {
    case SciencePanelMode::Preview:
        return query.outputKind == OutputKind::RasterLayer;
    case SciencePanelMode::PointSeries:
        return query.outputKind == OutputKind::TimeSeries &&
            query.analysis.kind == AnalysisKind::PointSeries;
    case SciencePanelMode::RegionalChange:
        return query.outputKind == OutputKind::Analysis &&
            query.analysis.kind == AnalysisKind::RegionalChange;
    }
    return false;
}

earthscience::GeoTemporalQuery buildSciencePanelDraft(
    const earthscience::ScienceSourceDescriptor& source,
    const earthscience::ScienceVisualizationDescriptor* visualization,
    const SciencePanelState& state,
    double latitude,
    double longitude,
    double requestedSpanMeters)
{
    if (state.mode == SciencePanelMode::Preview && visualization)
        return makeSciencePointQuery(
            source, *visualization, latitude, longitude,
            state.lastYear, requestedSpanMeters);
    if (state.mode == SciencePanelMode::PointSeries)
        return makeSciencePointSeriesQuery(
            source, latitude, longitude, state.firstYear, state.lastYear);
    if (state.mode == SciencePanelMode::RegionalChange)
    {
        earthscience::ScienceAnalysisOptions options;
        options.gridSize = state.gridSize;
        options.enablePca = state.enablePca;
        options.enableClustering = state.enableClustering;
        options.clusterCount = state.clusterCount;
        return makeScienceRegionalAnalysisQuery(
            source, latitude, longitude, requestedSpanMeters,
            state.baselineYear, state.comparisonYear, options);
    }
    return earthscience::GeoTemporalQuery();
}

std::shared_ptr<const earthscience::ScienceArtifact> matchingArtifact(
    const std::shared_ptr<const earthscience::ScienceArtifact>& artifact,
    SciencePanelMode mode)
{
    return artifact && artifactMatchesMode(*artifact, mode) ? artifact : nullptr;
}

std::string formatBytes(std::uint64_t bytes)
{
    static const char* UNITS[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(UNITS) / sizeof(UNITS[0]))
    {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream text;
    text << std::fixed << std::setprecision(unit == 0 ? 0 : 1)
         << value << ' ' << UNITS[unit];
    return text.str();
}

std::string formatInteger(std::uint64_t value)
{
    const std::string digits = std::to_string(value);
    std::string result;
    result.reserve(digits.size() + digits.size() / 3);
    for (std::size_t index = 0; index < digits.size(); ++index)
    {
        if (index > 0 && (digits.size() - index) % 3 == 0)
            result.push_back(',');
        result.push_back(digits[index]);
    }
    return result;
}

bool hasGeographicExtent(const earthscience::ScienceWgs84Bounds& bounds)
{
    return std::isfinite(bounds.west) && std::isfinite(bounds.south) &&
        std::isfinite(bounds.east) && std::isfinite(bounds.north) &&
        bounds.east > bounds.west && bounds.north > bounds.south;
}

std::string formatBounds(const earthscience::ScienceWgs84Bounds& bounds)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(4)
         << "W " << bounds.west << " · S " << bounds.south
         << " · E " << bounds.east << " · N " << bounds.north;
    return text.str();
}

std::string formatResolution(double meters)
{
    std::ostringstream text;
    text << std::fixed << std::setprecision(1) << meters << " m";
    return text.str();
}

ImVec4 severityColor(SciencePanelSeverity severity)
{
    switch (severity)
    {
    case SciencePanelSeverity::Info:
        return ImVec4(0.35f, 0.78f, 1.0f, 1.0f);
    case SciencePanelSeverity::Success:
        return ImVec4(0.35f, 0.90f, 0.55f, 1.0f);
    case SciencePanelSeverity::Warning:
        return ImVec4(1.0f, 0.78f, 0.25f, 1.0f);
    case SciencePanelSeverity::Error:
        return ImVec4(1.0f, 0.42f, 0.35f, 1.0f);
    case SciencePanelSeverity::Neutral:
        return ImVec4(0.72f, 0.78f, 0.84f, 1.0f);
    }
    return ImVec4(0.72f, 0.78f, 0.84f, 1.0f);
}

void drawColoredWrapped(const ImVec4& color, const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

bool drawWrappedCheckbox(const char* id, const char* label, bool* value)
{
    bool changed = ImGui::Checkbox(id, value);
    ImGui::SameLine();
    ImGui::TextWrapped("%s", label);
    if (ImGui::IsItemClicked())
    {
        *value = !*value;
        changed = true;
    }
    return changed;
}

void drawHelpButton(const char* id, ScienceHelpTopic topic,
                    bool placeOnSameLine = true)
{
    if (placeOnSameLine) ImGui::SameLine();
    ImGui::PushID(id);
    if (ImGui::SmallButton("?")) ImGui::OpenPopup("##science_help_popup");
    if (ImGui::BeginPopup("##science_help_popup"))
    {
        ImGui::TextWrapped("%s", scienceHelpTopicTitle(topic));
        ImGui::Separator();
        ImGui::PushTextWrapPos(
            ImGui::GetCursorPosX() + std::min(360.0f,
                std::max(220.0f, ImGui::GetContentRegionAvail().x)));
        ImGui::TextWrapped("%s", scienceHelpTopicBody(topic));
        ImGui::PopTextWrapPos();
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

void pushScienceScrollbarStyle()
{
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 14.0f);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,
                          ImVec4(0.23f, 0.65f, 1.0f, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                          ImVec4(0.23f, 0.65f, 1.0f, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,
                          ImVec4(0.23f, 0.65f, 1.0f, 1.0f));
}

void popScienceScrollbarStyle()
{
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

bool drawDiscreteYear(const char* id, const char* label, int* year,
                      int firstYear, int lastYear)
{
    ImGui::TextWrapped("%s · %d–%d", label, firstYear, lastYear);
    ImGui::PushID(id);
    bool changed = false;
    if (*year <= firstYear) ImGui::BeginDisabled();
    if (ImGui::SmallButton("-"))
    {
        --*year;
        changed = true;
    }
    if (*year <= firstYear) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(72.0f, ImGui::GetContentRegionAvail().x - 34.0f));
    const std::string current = std::to_string(*year);
    if (ImGui::BeginCombo("##year", current.c_str()))
    {
        for (int candidate = firstYear; candidate <= lastYear; ++candidate)
        {
            const bool selected = candidate == *year;
            const std::string item = std::to_string(candidate);
            if (ImGui::Selectable(item.c_str(), selected))
            {
                *year = candidate;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (*year >= lastYear) ImGui::BeginDisabled();
    if (ImGui::SmallButton("+"))
    {
        ++*year;
        changed = true;
    }
    if (*year >= lastYear) ImGui::EndDisabled();
    ImGui::PopID();
    *year = std::clamp(*year, firstYear, lastYear);
    return changed;
}

void drawLegendLine(const char* id, const ImVec4& color, const char* label)
{
    const ImVec2 cursor = ImGui::GetCursorScreenPos();
    const float size = ImGui::GetTextLineHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(
        cursor, ImVec2(cursor.x + size, cursor.y + size),
        ImGui::ColorConvertFloat4ToU32(color), 2.0f);
    ImGui::Dummy(ImVec2(size + 4.0f, size));
    ImGui::SameLine();
    ImGui::PushID(id);
    ImGui::TextWrapped("%s", label);
    ImGui::PopID();
}

std::string queryEstimateKey(
    const earthscience::GeoTemporalQuery& query,
    const earthscience::ScienceQueryCost& cost)
{
    std::ostringstream key;
    key << query.sourceId << '|' << static_cast<int>(query.outputKind) << '|'
        << static_cast<int>(query.geometry.kind) << '|'
        << std::setprecision(12) << query.geometry.point.latitude << '|'
        << query.geometry.point.longitude << '|'
        << query.geometry.bounds.west << '|' << query.geometry.bounds.south << '|'
        << query.geometry.bounds.east << '|' << query.geometry.bounds.north << '|'
        << query.geometry.requestedSpanMeters << '|'
        << static_cast<int>(query.time.mode) << '|'
        << query.targetResolutionMeters << '|'
        << static_cast<int>(query.aggregation) << '|'
        << static_cast<int>(query.priority) << '|'
        << query.visualizationId << '|' << query.purpose << '|'
        << static_cast<int>(query.analysis.kind) << '|'
        << query.analysis.baselineYear << '|'
        << query.analysis.comparisonYear << '|'
        << query.analysis.gridSize << '|' << query.analysis.hotspotQuantile << '|'
        << query.analysis.enablePca << '|' << query.analysis.pcaComponents << '|'
        << query.analysis.enableClustering << '|' << query.analysis.clusterCount;
    for (const std::string& variable : query.variables)
        key << "|variable:" << variable;
    for (earthscience::ScienceMetric metric : query.analysis.metrics)
        key << "|metric:" << static_cast<int>(metric);
    for (int year : query.time.explicitYears) key << '|' << year;
    key << '|' << cost.sourceBytesUpperBound << '|'
        << cost.residentBytesUpperBound << '|' << cost.resultCells << '|'
        << cost.durationDeterminate << '|' << cost.estimatedDurationSeconds;
    return key.str();
}

const char* modeLabel(SciencePanelMode mode)
{
    switch (mode)
    {
    case SciencePanelMode::Preview: return u8"伪彩预览 / Preview";
    case SciencePanelMode::PointSeries: return u8"点位年度序列 / Point series";
    case SciencePanelMode::RegionalChange:
        return u8"区域年度变化 / Regional change";
    }
    return u8"未知 / Unknown";
}

void drawPrimaryMetrics(const earthscience::ScienceArtifact& artifact)
{
    int shown = 0;
    if (artifact.analysis.metrics)
    {
        for (const earthscience::ScienceMetricResult& metric :
             *artifact.analysis.metrics)
        {
            if (shown == 2) break;
            ImGui::TextWrapped("%s · %d→%d: %.5f %s",
                earthscience::scienceMetricName(metric.metric),
                metric.baselineYear, metric.comparisonYear, metric.value,
                metric.unit.c_str());
            ++shown;
        }
    }
    if (shown < 2 && artifact.embedding.width > 0)
    {
        ImGui::TextWrapped(u8"有效覆盖 / Valid coverage: %.1f%%",
                           artifact.embedding.coverageFraction * 100.0);
        ++shown;
    }
    if (shown < 2 && artifact.embedding.validCellCount > 0)
    {
        ImGui::TextWrapped(u8"有效样本 / Valid samples: %s",
            formatInteger(artifact.embedding.validCellCount).c_str());
        ++shown;
    }
    if (shown < 2 && artifact.raster.width > 0)
    {
        ImGui::TextWrapped(u8"显示网格 / Display grid: %d × %d",
                           artifact.raster.width, artifact.raster.height);
    }
}

bool drawMetricSeries(const earthscience::ScienceArtifact& artifact)
{
    if (!artifact.analysis.annualSeries ||
        artifact.analysis.annualSeries->empty()) return false;
    const earthscience::ScienceAnnualSeries& series =
        artifact.analysis.annualSeries->front();
    if (!series.values || series.values->empty()) return false;

    std::vector<float> plot;
    plot.reserve(series.values->size());
    float minimum = std::numeric_limits<float>::max();
    float maximum = std::numeric_limits<float>::lowest();
    for (std::size_t index = 0; index < series.values->size(); ++index)
    {
        const bool valid = !series.validity ||
            index >= series.validity->size() || (*series.validity)[index] != 0;
        const float value = valid
            ? static_cast<float>((*series.values)[index])
            : std::numeric_limits<float>::quiet_NaN();
        plot.push_back(value);
        if (valid && std::isfinite(value))
        {
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }
    if (minimum > maximum) return false;
    if (minimum == maximum)
    {
        minimum -= 0.01f;
        maximum += 0.01f;
    }
    ImGui::TextWrapped(u8"年度嵌入关系 / Annual embedding relationship");
    ImGui::PlotLines("##science_metric_series", plot.data(),
        static_cast<int>(plot.size()), 0,
        earthscience::scienceMetricName(series.metric), minimum, maximum,
        ImVec2(-1.0f, 92.0f));
    if (series.years && !series.years->empty())
        ImGui::TextWrapped("%d — %d", series.years->front(),
                           series.years->back());
    return true;
}

void drawEmbeddingLegend(const earthscience::ScienceArtifact& artifact)
{
    if (artifact.analysis.kind == earthscience::ScienceAnalysisKind::None &&
        artifact.query.variables.size() >= 3)
    {
        const std::string red = "R = " + artifact.query.variables[0];
        const std::string green = "G = " + artifact.query.variables[1];
        const std::string blue = "B = " + artifact.query.variables[2];
        drawLegendLine("red", ImVec4(0.95f, 0.30f, 0.30f, 1.0f),
                       red.c_str());
        drawLegendLine("green", ImVec4(0.30f, 0.90f, 0.45f, 1.0f),
                       green.c_str());
        drawLegendLine("blue", ImVec4(0.30f, 0.60f, 1.0f, 1.0f),
                       blue.c_str());
    }
    else if (artifact.analysis.clusters.clusterCount > 0)
    {
        static const ImVec4 COLORS[] = {
            ImVec4(0.27f, 0.72f, 0.96f, 1.0f),
            ImVec4(0.96f, 0.66f, 0.24f, 1.0f),
            ImVec4(0.47f, 0.84f, 0.48f, 1.0f),
            ImVec4(0.82f, 0.47f, 0.92f, 1.0f),
        };
        const int shown = std::min(artifact.analysis.clusters.clusterCount, 4);
        for (int cluster = 0; cluster < shown; ++cluster)
        {
            const std::string label = u8"无标签簇 / Unlabeled cluster " +
                std::to_string(cluster + 1);
            const std::string id = "cluster" + std::to_string(cluster);
            drawLegendLine(id.c_str(), COLORS[cluster], label.c_str());
        }
    }
    else
    {
        drawLegendLine("low", ImVec4(0.10f, 0.45f, 0.72f, 1.0f),
                       u8"较低的相对嵌入距离 / Lower relative distance");
        drawLegendLine("high", ImVec4(0.95f, 0.58f, 0.18f, 1.0f),
                       u8"较高的相对嵌入距离 / Higher relative distance");
    }
}

void drawTechnicalDetails(const earthscience::ScienceArtifact& artifact)
{
    const bool hasPca = artifact.analysis.pca.componentCount > 0;
    const bool hasClusters = artifact.analysis.clusters.clusterCount > 0;
    if (hasPca || hasClusters)
    {
        std::ostringstream structureLabel;
        structureLabel << u8"结构分析";
        if (hasPca)
            structureLabel << u8" · PCA "
                           << artifact.analysis.pca.componentCount;
        if (hasClusters)
            structureLabel << u8" · "
                           << artifact.analysis.clusters.clusterCount
                           << u8" 个聚类";
        const bool structureOpen = ImGui::CollapsingHeader(
            structureLabel.str().c_str());
        if (hasPca)
            drawHelpButton("pca_result", ScienceHelpTopic::Pca, false);
        if (hasClusters)
            drawHelpButton("cluster_result", ScienceHelpTopic::Clusters,
                           hasPca);
        if (structureOpen)
        {
            if (hasPca && artifact.analysis.pca.explainedVarianceRatios)
            {
                for (std::size_t index = 0;
                     index < artifact.analysis.pca.explainedVarianceRatios->size();
                     ++index)
                    ImGui::TextWrapped("PC%zu: %.1f%%", index + 1,
                        (*artifact.analysis.pca.explainedVarianceRatios)[index] * 100.0);
            }
            if (hasClusters)
                ImGui::TextWrapped(u8"聚类数：%d",
                    artifact.analysis.clusters.clusterCount);
        }
    }

    const bool detailsOpen = ImGui::CollapsingHeader(u8"数据详情与来源");
    drawHelpButton("provenance", ScienceHelpTopic::Provenance);
    if (detailsOpen)
    {
        ImGui::TextWrapped(u8"处理版本：%s",
                           artifact.processingVersion.c_str());
        for (const earthscience::ScienceSourceReference& source :
             artifact.sourceReferences)
        {
            ImGui::TextWrapped(u8"数据集：%s", source.datasetId.c_str());
            ImGui::TextWrapped(u8"提供方版本：%s",
                               source.providerVersion.c_str());
            ImGui::TextWrapped(u8"署名：%s",
                               source.attribution.c_str());
        }
    }
}
}

SciencePanelPresentation describeScienceSnapshot(
    const earthscience::ScienceJobSnapshot& snapshot)
{
    SciencePanelPresentation view;
    view.stageText = std::string(u8"阶段 / Stage: ") +
        stageLabel(snapshot.progress.stage);
    view.detail = snapshot.message;
    view.progressDeterminate = snapshot.progress.determinate &&
        snapshot.progress.totalUnits > 0;
    if (view.progressDeterminate)
    {
        const double fraction = std::clamp(
            static_cast<double>(snapshot.progress.completedUnits) /
                static_cast<double>(snapshot.progress.totalUnits),
            0.0, 1.0);
        std::ostringstream progress;
        progress << snapshot.progress.completedUnits << " / "
                 << snapshot.progress.totalUnits;
        if (!snapshot.progress.unit.empty())
            progress << ' ' << snapshot.progress.unit;
        progress << " · " << std::fixed << std::setprecision(0)
                 << fraction * 100.0 << '%';
        view.progressText = progress.str();
    }
    else if (snapshot.state == earthscience::ScienceJobState::Fetching ||
             snapshot.state == earthscience::ScienceJobState::Queued)
        view.progressText =
            u8"● 正在处理；此阶段没有可验证的总量，不显示百分比";

    const bool noCoverage = containsAny(snapshot.message,
        {"no coverage", "no alphaearth tile", "does not cover", "covers this point"});
    const bool stale = containsAny(snapshot.message,
        {"stale", "newer request", "newer generation"});
    if (stale)
    {
        view.kind = SciencePanelResultKind::Stale;
        view.severity = SciencePanelSeverity::Warning;
        view.title = u8"旧请求未采用 / Stale result discarded";
    }
    else if (noCoverage && snapshot.state == earthscience::ScienceJobState::Failed)
    {
        view.kind = SciencePanelResultKind::NoCoverage;
        view.severity = SciencePanelSeverity::Warning;
        view.title = u8"所选位置或年份无数据覆盖 / No coverage";
    }
    else
    {
        switch (snapshot.state)
        {
        case earthscience::ScienceJobState::Idle:
            view.kind = SciencePanelResultKind::Idle;
            view.title = u8"尚未运行分析 / No analysis yet";
            break;
        case earthscience::ScienceJobState::Queued:
            view.kind = SciencePanelResultKind::Queued;
            view.severity = SciencePanelSeverity::Info;
            view.title = u8"分析已排队 / Analysis queued";
            view.busy = true;
            break;
        case earthscience::ScienceJobState::Fetching:
            view.kind = SciencePanelResultKind::Active;
            view.severity = SciencePanelSeverity::Info;
            view.title = stageLabel(snapshot.progress.stage);
            view.busy = true;
            break;
        case earthscience::ScienceJobState::Ready:
            view.kind = SciencePanelResultKind::Ready;
            view.severity = SciencePanelSeverity::Success;
            view.title = u8"分析完成 / Analysis ready";
            break;
        case earthscience::ScienceJobState::Failed:
        case earthscience::ScienceJobState::Unavailable:
            view.kind = SciencePanelResultKind::Failed;
            view.severity = SciencePanelSeverity::Error;
            view.title = u8"本次请求失败 / Request failed";
            break;
        case earthscience::ScienceJobState::Cancelled:
            view.kind = SciencePanelResultKind::Cancelled;
            view.severity = SciencePanelSeverity::Warning;
            view.title = u8"本次请求已取消 / Request cancelled";
            break;
        }
    }

    return view;
}

SciencePanelModeCapabilities sciencePanelModeCapabilities(SciencePanelMode mode)
{
    SciencePanelModeCapabilities capabilities;
    switch (mode)
    {
    case SciencePanelMode::Preview:
        capabilities.showsSingleYear = true;
        break;
    case SciencePanelMode::PointSeries:
        capabilities.showsYearRange = true;
        break;
    case SciencePanelMode::RegionalChange:
        capabilities.showsYearPair = true;
        capabilities.supportsGrid = true;
        capabilities.supportsPca = true;
        capabilities.supportsClustering = true;
        break;
    }
    return capabilities;
}

const char* sciencePanelPrimaryActionLabel(SciencePanelMode mode)
{
    switch (mode)
    {
    case SciencePanelMode::Preview: return u8"加载伪彩预览";
    case SciencePanelMode::PointSeries: return u8"分析点位年度变化";
    case SciencePanelMode::RegionalChange: return u8"分析当前视野变化";
    }
    return u8"开始分析";
}

ScienceArtifactUiPresentation describeScienceArtifactUi(
    const earthscience::ScienceArtifact& artifact,
    const earthscience::GeoTemporalQuery& currentDraft)
{
    ScienceArtifactUiPresentation view;
    const earthscience::ScienceQueryCost emptyCost;
    view.matchesDraft = queryEstimateKey(artifact.query, emptyCost) ==
        queryEstimateKey(currentDraft, emptyCost);
    view.showPcaSummary = artifact.analysis.pca.componentCount > 0;
    view.showClusterSummary = artifact.analysis.clusters.clusterCount > 0;

    std::ostringstream scope;
    if (artifactMatchesMode(artifact, SciencePanelMode::Preview))
        scope << u8"伪彩预览";
    else if (artifactMatchesMode(artifact, SciencePanelMode::PointSeries))
        scope << u8"点位年度变化";
    else if (artifactMatchesMode(artifact, SciencePanelMode::RegionalChange))
        scope << u8"区域年度变化";
    else
        scope << u8"ScienceEarth 结果";
    if (!artifact.query.time.explicitYears.empty())
    {
        scope << u8" · ";
        scope << artifact.query.time.explicitYears.front();
        if (artifact.query.time.explicitYears.size() > 1)
            scope << u8"→" << artifact.query.time.explicitYears.back();
    }
    if (artifact.query.analysis.kind ==
            earthscience::ScienceAnalysisKind::RegionalChange &&
        artifact.query.analysis.gridSize > 0)
        scope << u8" · " << artifact.query.analysis.gridSize << u8"×"
              << artifact.query.analysis.gridSize;
    if (view.showPcaSummary) scope << u8" · PCA";
    if (view.showClusterSummary)
        scope << u8" · 聚类 " << artifact.analysis.clusters.clusterCount;
    view.scopeLabel = scope.str();
    if (!view.matchesDraft)
        view.pendingSettingsLabel =
            u8"当前设置尚未运行；下方仍是上一次结果。";
    return view;
}

const char* scienceHelpTopicTitle(ScienceHelpTopic topic)
{
    switch (topic)
    {
    case ScienceHelpTopic::DataMeaning: return u8"64 维数据是什么？";
    case ScienceHelpTopic::PreviewColors: return u8"伪彩颜色表示什么？";
    case ScienceHelpTopic::Pca: return u8"PCA 与右侧结果";
    case ScienceHelpTopic::Clusters: return u8"无标签聚类是什么？";
    case ScienceHelpTopic::ScientificLimits: return u8"科学解释边界";
    case ScienceHelpTopic::Provenance: return u8"方法、来源与导出";
    }
    return u8"ScienceEarth 帮助";
}

const char* scienceHelpTopicBody(ScienceHelpTopic topic)
{
    switch (topic)
    {
    case ScienceHelpTopic::DataMeaning:
        return u8"A00–A63 是 64 个潜在表征分量；单个分量没有获验证的"
               u8"植被、温度、城市或其他物理名称。";
    case ScienceHelpTopic::PreviewColors:
        return u8"伪彩把选定分量映射到红、绿、蓝通道，帮助定位"
               u8"潜在嵌入关系差异；它不是自然色，也不是物理量。";
    case ScienceHelpTopic::Pca:
        return u8"PCA 只适用于区域年度变化。勾选后重新运行，右侧才会显示"
               u8"本次结果内的局部数学方向；它不代表具体地物。";
    case ScienceHelpTopic::Clusters:
        return u8"聚类只适用于区域年度变化。它产生可复现的"
               u8"无标签数学分组，不是土地覆盖类别。";
    case ScienceHelpTopic::ScientificLimits:
        return u8"嵌入关系不能单独证明建设、砍伐、洪水、升温或其他物理"
               u8"原因；需要与可解释数据源联合验证。";
    case ScienceHelpTopic::Provenance:
        return u8"详情保留数据集、提供方版本、署名、处理版本、"
               u8"覆盖和分辨率；证据导出保留查询、算法与限制。";
    }
    return "";
}

SciencePanelPresentation describeScienceSnapshot(
    const earthscience::ScienceJobSnapshot& snapshot,
    SciencePanelMode mode)
{
    SciencePanelPresentation view = describeScienceSnapshot(snapshot);
    if (snapshot.jobId != 0 &&
        snapshot.state != earthscience::ScienceJobState::Idle &&
        !view.busy &&
        !queryMatchesMode(snapshot.query, mode))
    {
        view = SciencePanelPresentation();
        if (selectSciencePanelArtifact(snapshot, mode))
            view.title = u8"显示该模式上一次结果 / Previous result";
        else
            view.title = u8"此模式尚无结果 / No result for this mode";
        return view;
    }
    const bool replacementFailed =
        view.kind == SciencePanelResultKind::NoCoverage ||
        view.kind == SciencePanelResultKind::Failed ||
        view.kind == SciencePanelResultKind::Cancelled ||
        view.kind == SciencePanelResultKind::Stale;
    const std::shared_ptr<const earthscience::ScienceArtifact> retained =
        replacementFailed ? selectSciencePanelArtifact(snapshot, mode) : nullptr;
    if (!retained) return view;

    view.retention.present = true;
    view.retention.mode = mode;
    view.retention.severity = SciencePanelSeverity::Warning;
    switch (mode)
    {
    case SciencePanelMode::Preview:
        view.retention.title = u8"已保留预览 / Retained Preview";
        break;
    case SciencePanelMode::PointSeries:
        view.retention.title = u8"已保留点位序列 / Retained Point series";
        break;
    case SciencePanelMode::RegionalChange:
        view.retention.title =
            u8"已保留区域变化 / Retained Regional change";
        break;
    }

    switch (view.kind)
    {
    case SciencePanelResultKind::NoCoverage:
        view.retention.reason = std::string(
            u8"无覆盖未替换旧结果 / No coverage did not replace artifact: ") +
            retained->artifactId;
        break;
    case SciencePanelResultKind::Failed:
        view.retention.reason = std::string(
            u8"数据源失败未替换旧结果 / Provider failure did not replace artifact: ") +
            retained->artifactId;
        break;
    case SciencePanelResultKind::Cancelled:
        view.retention.reason = std::string(
            u8"取消未替换旧结果 / Cancelled request did not replace artifact: ") +
            retained->artifactId;
        break;
    case SciencePanelResultKind::Stale:
        view.retention.reason = std::string(
            u8"旧请求已丢弃，保留旧结果 / "
            u8"Stale request discarded; retained artifact: ") +
            retained->artifactId;
        break;
    default:
        break;
    }
    return view;
}

ScienceCostPresentation describeScienceCost(
    const earthscience::ScienceQueryCost& cost)
{
    ScienceCostPresentation view;
    view.sourceBytes = formatBytes(cost.sourceBytesUpperBound);
    view.residentMemory = formatBytes(cost.residentBytesUpperBound);
    view.resultCells = formatInteger(cost.resultCells);
    view.requiresConfirmation = cost.requiresConfirmation;
    if (cost.durationDeterminate)
    {
        std::ostringstream duration;
        duration << std::fixed << std::setprecision(1)
                 << cost.estimatedDurationSeconds << " s observed estimate";
        view.duration = duration.str();
    }
    else
        view.duration = u8"尚无可测时长 / duration not yet measurable";
    return view;
}

std::shared_ptr<const earthscience::ScienceArtifact> selectSciencePanelArtifact(
    const earthscience::ScienceJobSnapshot& snapshot,
    SciencePanelMode mode)
{
    if (mode == SciencePanelMode::Preview)
    {
        std::shared_ptr<const earthscience::ScienceArtifact> selected =
            matchingArtifact(snapshot.displayArtifact, mode);
        if (selected) return selected;
        selected = matchingArtifact(snapshot.lastSuccessfulPreviewArtifact, mode);
        if (selected) return selected;
        return matchingArtifact(snapshot.lastSuccessfulArtifact, mode);
    }
    return matchingArtifact(snapshot.lastSuccessfulAnalysisArtifact, mode);
}

bool sciencePanelEstimateRequiresConfirmation(
    const earthscience::GeoTemporalQuery& query,
    const earthscience::ScienceQueryCost& cost)
{
    const bool retainedMultiYearPointSeries =
        query.outputKind == earthscience::ScienceOutputKind::TimeSeries &&
        query.analysis.kind == earthscience::ScienceAnalysisKind::PointSeries &&
        query.time.explicitYears.size() > 1;
    const bool highResolutionRegional =
        query.outputKind == earthscience::ScienceOutputKind::Analysis &&
        query.analysis.kind == earthscience::ScienceAnalysisKind::RegionalChange &&
        query.analysis.gridSize >= 256;
    return cost.requiresConfirmation || retainedMultiYearPointSeries ||
        highResolutionRegional;
}

std::string sciencePanelEstimateBindingKey(
    const earthscience::GeoTemporalQuery& query,
    const earthscience::ScienceQueryCost& cost)
{
    return queryEstimateKey(query, cost);
}

bool sciencePanelEstimateConfirmationMatches(
    const earthscience::GeoTemporalQuery& query,
    const earthscience::ScienceQueryCost& cost,
    const std::string& confirmedBindingKey)
{
    if (!sciencePanelEstimateRequiresConfirmation(query, cost)) return true;
    return !confirmedBindingKey.empty() &&
        confirmedBindingKey == sciencePanelEstimateBindingKey(query, cost);
}

std::vector<std::string> describeScienceArtifactEvidence(
    const earthscience::ScienceArtifact& artifact)
{
    const std::string unavailable = u8"未记录 / not recorded";
    std::vector<std::string> lines;

    std::ostringstream years;
    years << u8"已载入结果年份 / Loaded artifact year(s): ";
    if (artifact.query.time.explicitYears.empty())
        years << unavailable;
    else
    {
        for (std::size_t index = 0;
             index < artifact.query.time.explicitYears.size(); ++index)
        {
            if (index > 0) years << ", ";
            years << artifact.query.time.explicitYears[index];
        }
    }
    lines.push_back(years.str());

    earthscience::ScienceWgs84Bounds payloadBounds;
    double actualResolutionMeters = 0.0;
    bool hasPayloadBounds = false;
    if (artifact.query.outputKind == earthscience::ScienceOutputKind::RasterLayer)
    {
        payloadBounds = artifact.raster.bounds;
        hasPayloadBounds = hasGeographicExtent(payloadBounds);
    }
    else if (artifact.query.outputKind ==
             earthscience::ScienceOutputKind::TimeSeries)
    {
        payloadBounds = artifact.embedding.bounds;
        hasPayloadBounds = hasGeographicExtent(payloadBounds);
        actualResolutionMeters = artifact.embedding.actualResolutionMeters;
    }
    else if (artifact.query.outputKind == earthscience::ScienceOutputKind::Analysis)
    {
        if (hasGeographicExtent(artifact.analysis.regionalChange.bounds))
        {
            payloadBounds = artifact.analysis.regionalChange.bounds;
            actualResolutionMeters =
                artifact.analysis.regionalChange.actualResolutionMeters;
            hasPayloadBounds = true;
        }
        else
        {
            payloadBounds = artifact.analysis.scalarChangeRaster.bounds;
            actualResolutionMeters =
                artifact.analysis.scalarChangeRaster.actualResolutionMeters;
            hasPayloadBounds = hasGeographicExtent(payloadBounds);
        }
    }

    lines.push_back(std::string(u8"结果范围 / Artifact bounds: ") +
        (hasPayloadBounds ? formatBounds(payloadBounds) : unavailable));

    const earthscience::ScienceWgs84Bounds* actualCoverage = nullptr;
    for (const earthscience::ScienceSourceReference& reference :
         artifact.sourceReferences)
    {
        if (hasGeographicExtent(reference.actualCoverage))
        {
            actualCoverage = &reference.actualCoverage;
            break;
        }
    }
    lines.push_back(std::string(u8"实际数据覆盖 / Actual source coverage: ") +
        (actualCoverage ? formatBounds(*actualCoverage) : unavailable));

    if (artifact.query.outputKind == earthscience::ScienceOutputKind::RasterLayer)
    {
        lines.push_back(std::string(u8"源数据分辨率 / Source resolution: ") +
            (artifact.raster.sourceResolutionMeters > 0.0
                ? formatResolution(artifact.raster.sourceResolutionMeters)
                : unavailable));
        lines.push_back(std::string(u8"显示分辨率 / Display resolution: ") +
            (artifact.raster.displayResolutionMeters > 0.0
                ? formatResolution(artifact.raster.displayResolutionMeters)
                : unavailable));
    }
    else
        lines.push_back(std::string(u8"实际分析分辨率 / Actual resolution: ") +
            (actualResolutionMeters > 0.0
                ? formatResolution(actualResolutionMeters) : unavailable));
    return lines;
}

void ScienceEarthPanel::drawOperations(
    earthscience::ScienceQueryService* service,
    SciencePreviewLayer* previewLayer,
    LayerManager* layers,
    osgVerse::EarthManipulator* manipulator)
{
    if (!service || !previewLayer || !manipulator) return;
    const bool operationsExpanded =
        ImGui::CollapsingHeader(u8"ScienceEarth 分析 / Analysis");

    const std::vector<earthscience::ScienceSourceDescriptor> sources =
        service->listSources();
    if (sources.empty())
    {
        if (operationsExpanded)
            ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.35f, 1.0f),
                               u8"没有注册科学数据源 / No science source");
        return;
    }
    const earthscience::ScienceSourceDescriptor& source = sources.front();
    const earthscience::ScienceVisualizationDescriptor* visualization =
        source.visualizations.empty() ? nullptr : &source.visualizations.front();
    const osg::Vec3d target = manipulator->computeViewPointLatLonHeight();
    const osg::Vec3d eye = manipulator->computeEyeLatLonHeight();
    const double latitude = osg::RadiansToDegrees(target[0]);
    const double longitude = osg::RadiansToDegrees(target[1]);
    const double requestedSpanMeters = std::clamp(
        eye[2] * 0.85, 2560.0, 81920.0);

    _state.firstYear = std::clamp(
        _state.firstYear, source.firstYear, source.lastYear);
    _state.lastYear = std::clamp(
        _state.lastYear, source.firstYear, source.lastYear);
    _state.baselineYear = std::clamp(
        _state.baselineYear, source.firstYear, source.lastYear);
    _state.comparisonYear = std::clamp(
        _state.comparisonYear, source.firstYear, source.lastYear);
    _state.locationMode = _state.mode == SciencePanelMode::RegionalChange
        ? SciencePanelLocationMode::CurrentViewFootprint
        : SciencePanelLocationMode::CurrentLocation;

    _currentDraft = buildSciencePanelDraft(
        source, visualization, _state, latitude, longitude,
        requestedSpanMeters);
    _hasCurrentDraft = !_currentDraft.sourceId.empty();
    if (!operationsExpanded) return;

    const earthscience::ScienceJobSnapshot snapshot = service->snapshot();

    drawColoredWrapped(ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
                       source.name.c_str());
    drawHelpButton("data_meaning", ScienceHelpTopic::DataMeaning, false);
    if (ImGui::CollapsingHeader(u8"数据源详情 / Source details"))
    {
        ImGui::TextWrapped(u8"原始分辨率：%.1f m",
                           source.nativeResolutionMeters);
        ImGui::TextWrapped(u8"潜在分量：%d", source.componentCount);
        ImGui::TextWrapped(u8"提供方版本：%s",
                           source.providerVersion.c_str());
        ImGui::TextWrapped(u8"署名：%s", source.attribution.c_str());
    }
    const bool sourceUnavailable =
        source.health == earthscience::ScienceSourceHealth::Unavailable;
    if (source.health != earthscience::ScienceSourceHealth::Ready)
    {
        const std::string health = std::string(u8"数据源状态 / Source: ") +
            earthscience::scienceSourceHealthName(source.health);
        drawColoredWrapped(
            sourceUnavailable
                ? ImVec4(1.0f, 0.42f, 0.35f, 1.0f)
                : ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
            health.c_str());
        if (!source.healthMessage.empty())
            ImGui::TextWrapped("%s", source.healthMessage.c_str());
    }
    if (_state.locationMode == SciencePanelLocationMode::CurrentLocation)
        ImGui::TextWrapped(u8"当前位置 / Current location: %.4f, %.4f",
                           latitude, longitude);
    else
        ImGui::TextWrapped(
            u8"当前视野范围 / Current footprint: 中心 %.4f, %.4f · 约 %.1f km",
            latitude, longitude, requestedSpanMeters / 1000.0);

    ImGui::TextWrapped(u8"研究类型 / Research mode");
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##science_mode", modeLabel(_state.mode)))
    {
        for (int index = 0; index < 3; ++index)
        {
            const SciencePanelMode candidate = static_cast<SciencePanelMode>(index);
            const bool selected = candidate == _state.mode;
            if (ImGui::Selectable(modeLabel(candidate), selected))
                _state.mode = candidate;
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const SciencePanelModeCapabilities capabilities =
        sciencePanelModeCapabilities(_state.mode);

    if (capabilities.showsSingleYear)
        drawDiscreteYear("preview", u8"年份 / Year", &_state.lastYear,
                         source.firstYear, source.lastYear);
    else if (capabilities.showsYearRange)
    {
        drawDiscreteYear("series_start", u8"起始年份 / First year",
                         &_state.firstYear, source.firstYear, source.lastYear);
        drawDiscreteYear("series_end", u8"结束年份 / Last year",
                         &_state.lastYear, source.firstYear, source.lastYear);
        if (_state.firstYear > _state.lastYear)
            std::swap(_state.firstYear, _state.lastYear);
    }
    else if (capabilities.showsYearPair)
    {
        drawDiscreteYear("baseline", u8"基准年份 / Baseline",
                         &_state.baselineYear, source.firstYear, source.lastYear);
        drawDiscreteYear("comparison", u8"对比年份 / Comparison",
                         &_state.comparisonYear, source.firstYear, source.lastYear);
    }

    if (capabilities.supportsGrid || capabilities.supportsPca ||
        capabilities.supportsClustering)
    {
        ImGui::SetNextItemOpen(_state.advancedOpen, ImGuiCond_Once);
        _state.advancedOpen = ImGui::CollapsingHeader(u8"高级设置");
        if (_state.advancedOpen)
        {
            if (capabilities.supportsGrid)
            {
                ImGui::TextWrapped(u8"分析网格");
                if (ImGui::RadioButton(
                        "128 × 128##grid", _state.gridSize == 128))
                    _state.gridSize = 128;
                ImGui::SameLine();
                if (ImGui::RadioButton(
                        "256 × 256##grid", _state.gridSize == 256))
                    _state.gridSize = 256;
            }
            if (capabilities.supportsPca)
            {
                drawWrappedCheckbox("##science_pca",
                    u8"附加 PCA 结构摘要", &_state.enablePca);
                drawHelpButton("pca_setting", ScienceHelpTopic::Pca);
            }
            if (capabilities.supportsClustering)
            {
                drawWrappedCheckbox("##science_clusters",
                    u8"附加无标签聚类", &_state.enableClustering);
                drawHelpButton(
                    "cluster_setting", ScienceHelpTopic::Clusters);
            }
            if (_state.enableClustering && capabilities.supportsClustering)
            {
                ImGui::TextWrapped(u8"聚类数量：%d（2–8）",
                                   _state.clusterCount);
                if (_state.clusterCount <= 2) ImGui::BeginDisabled();
                if (ImGui::SmallButton("-##clusters")) --_state.clusterCount;
                if (_state.clusterCount <= 2) ImGui::EndDisabled();
                ImGui::SameLine();
                if (_state.clusterCount >= 8) ImGui::BeginDisabled();
                if (ImGui::SmallButton("+##clusters")) ++_state.clusterCount;
                if (_state.clusterCount >= 8) ImGui::EndDisabled();
                _state.clusterCount = std::clamp(_state.clusterCount, 2, 8);
            }
            if ((_state.enablePca && capabilities.supportsPca) ||
                (_state.enableClustering &&
                 capabilities.supportsClustering))
                ImGui::TextDisabled(
                    u8"本次设置将在运行后显示于右侧“结构分析”。");
        }
    }

    earthscience::GeoTemporalQuery query = buildSciencePanelDraft(
        source, visualization, _state, latitude, longitude,
        requestedSpanMeters);
    _currentDraft = query;
    _hasCurrentDraft = !query.sourceId.empty();

    bool estimateFailed = false;
    std::string estimateError;
    earthscience::ScienceQueryCost cost;
    if (!query.sourceId.empty())
    {
        try
        {
            cost = service->estimate(query);
        }
        catch (const std::exception& error)
        {
            estimateFailed = true;
            estimateError = error.what();
        }
    }
    const std::string estimateKey = estimateFailed
        ? std::string() : sciencePanelEstimateBindingKey(query, cost);
    if (estimateKey != _displayedEstimateKey)
    {
        _displayedEstimateKey = estimateKey;
        _confirmedEstimateKey.clear();
    }
    _displayedCost = cost;
    _estimateVisible = !estimateFailed &&
        sciencePanelEstimateRequiresConfirmation(query, cost);
    bool estimateConfirmed = !estimateFailed &&
        sciencePanelEstimateConfirmationMatches(
            query, cost, _confirmedEstimateKey);
    if (_estimateVisible)
    {
        const ScienceCostPresentation estimate = describeScienceCost(cost);
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                           u8"提交前资源上限 / Cost before submit");
        ImGui::TextWrapped(u8"源数据上限 / Source bytes: %s",
                           estimate.sourceBytes.c_str());
        ImGui::TextWrapped(u8"驻留内存上限 / Resident memory: %s",
                           estimate.residentMemory.c_str());
        ImGui::TextWrapped(u8"结果单元 / Result cells: %s",
                           estimate.resultCells.c_str());
        ImGui::TextWrapped(u8"时长 / Duration: %s", estimate.duration.c_str());
        if (drawWrappedCheckbox("##confirm_science_cost",
            u8"我确认按以上估算提交 / Confirm this exact estimate",
            &estimateConfirmed))
            _confirmedEstimateKey = estimateConfirmed
                ? _displayedEstimateKey : std::string();
    }
    if (estimateFailed)
    {
        const std::string failure =
            std::string(u8"无法估算 / Estimate failed: ") + estimateError;
        drawColoredWrapped(ImVec4(1.0f, 0.42f, 0.35f, 1.0f),
                           failure.c_str());
    }

    const SciencePanelPresentation presentation =
        describeScienceSnapshot(snapshot, _state.mode);
    const bool invalidPreview =
        _state.mode == SciencePanelMode::Preview && !visualization;
    const bool blocked = sourceUnavailable || invalidPreview || estimateFailed ||
        presentation.busy || (_estimateVisible && !estimateConfirmed);
    if (blocked) ImGui::BeginDisabled();
    if (ImGui::Button(sciencePanelPrimaryActionLabel(_state.mode),
                      ImVec2(-1.0f, 0.0f)))
    {
        query.analysis.confirmedLargeRequest =
            _estimateVisible && estimateConfirmed;
        if (_state.mode == SciencePanelMode::Preview)
        {
            previewLayer->setVisible(true);
            if (layers) layers->setEnabled("alphaearth", true);
        }
        service->submit(query);
    }
    if (blocked) ImGui::EndDisabled();

    if (presentation.busy)
    {
        drawColoredWrapped(severityColor(presentation.severity),
                           presentation.stageText.c_str());
        ImGui::TextWrapped("%s", presentation.progressText.c_str());
        if (!presentation.detail.empty())
            ImGui::TextWrapped("%s", presentation.detail.c_str());
        if (ImGui::Button(u8"取消当前请求 / Cancel request",
                          ImVec2(-1.0f, 0.0f)))
            service->cancel(snapshot.jobId);
    }
}

void ScienceEarthPanel::drawResults(
    earthscience::ScienceQueryService* service,
    SciencePreviewLayer* previewLayer,
    LayerManager* layers)
{
    if (!service || !previewLayer) return;
    const ImGuiIO& io = ImGui::GetIO();
    const earthui::ScienceWorkspaceLayout layout =
        earthui::computeScienceWorkspaceLayout(
            io.DisplaySize.x, io.DisplaySize.y, _state.resultExpanded);
    const float height = _state.resultExpanded ? layout.resultHeight : 108.0f;
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x - 20.0f - layout.resultWidth, 20.0f),
        ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(layout.resultWidth, height), ImGuiCond_Always);
    pushScienceScrollbarStyle();
    if (!ImGui::Begin("ScienceEarth Results / 科学结果", nullptr,
                      ImGuiWindowFlags_NoTitleBar |
                      ImGuiWindowFlags_AlwaysVerticalScrollbar))
    {
        ImGui::End();
        popScienceScrollbarStyle();
        return;
    }

    if (!_state.resultExpanded)
    {
        if (ImGui::Button(">", ImVec2(-1.0f, 0.0f)))
            _state.resultExpanded = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"展开 ScienceEarth 结果");
        ImGui::End();
        popScienceScrollbarStyle();
        return;
    }

    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped(u8"ScienceEarth 结果 / Results");
    ImGui::SameLine();
    if (ImGui::SmallButton(u8"折叠 >##science_results"))
        _state.resultExpanded = false;
    ImGui::Separator();

    const earthscience::ScienceJobSnapshot snapshot = service->snapshot();
    const SciencePanelPresentation presentation =
        describeScienceSnapshot(snapshot, _state.mode);
    const std::shared_ptr<const earthscience::ScienceArtifact> artifact =
        selectSciencePanelArtifact(snapshot, _state.mode);
    const bool activeOrProblem = presentation.busy ||
        presentation.kind == SciencePanelResultKind::NoCoverage ||
        presentation.kind == SciencePanelResultKind::Failed ||
        presentation.kind == SciencePanelResultKind::Cancelled ||
        presentation.kind == SciencePanelResultKind::Stale;
    if (activeOrProblem || !artifact)
    {
        drawColoredWrapped(severityColor(presentation.severity),
                           presentation.title.c_str());
        if (!presentation.stageText.empty() &&
            presentation.kind != SciencePanelResultKind::Idle)
            ImGui::TextWrapped("%s", presentation.stageText.c_str());
        if (!presentation.detail.empty())
            ImGui::TextWrapped("%s", presentation.detail.c_str());
        if (!presentation.progressText.empty())
            ImGui::TextWrapped("%s", presentation.progressText.c_str());
    }
    if (presentation.retention.present)
    {
        drawColoredWrapped(
            severityColor(presentation.retention.severity),
            presentation.retention.title.c_str());
        ImGui::TextWrapped("%s", presentation.retention.reason.c_str());
    }

    if (artifact)
    {
        const earthscience::GeoTemporalQuery& draft =
            _hasCurrentDraft ? _currentDraft : artifact->query;
        const ScienceArtifactUiPresentation artifactUi =
            describeScienceArtifactUi(*artifact, draft);
        drawColoredWrapped(ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
                           artifactUi.scopeLabel.c_str());
        if (!artifactUi.pendingSettingsLabel.empty())
            drawColoredWrapped(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                               artifactUi.pendingSettingsLabel.c_str());

        const std::vector<std::string> evidence =
            describeScienceArtifactEvidence(*artifact);
        if (evidence.size() > 1)
            ImGui::TextWrapped("%s", evidence[1].c_str());

        drawPrimaryMetrics(*artifact);
        ImGui::SeparatorText(u8"图表或图例");
        if (!drawMetricSeries(*artifact)) drawEmbeddingLegend(*artifact);
        drawHelpButton("preview_colors", ScienceHelpTopic::PreviewColors,
                       false);

        ImGui::TextDisabled(u8"科学解释边界");
        drawHelpButton(
            "scientific_limits", ScienceHelpTopic::ScientificLimits);

        drawTechnicalDetails(*artifact);
        if (artifact->raster.width > 0)
        {
            if (ImGui::Button(previewLayer->isVisible()
                    ? u8"隐藏图层 / Hide layer"
                    : u8"显示图层 / Show layer",
                    ImVec2(-1.0f, 0.0f)))
            {
                const bool visible = !previewLayer->isVisible();
                previewLayer->setVisible(visible);
                if (layers) layers->setEnabled("alphaearth", visible);
            }
        }
        if (ImGui::Button(u8"移除 ScienceEarth 结果 / Remove results",
                          ImVec2(-1.0f, 0.0f)))
        {
            service->cancel(snapshot.jobId);
            service->clearArtifact();
            previewLayer->removeArtifact();
            previewLayer->setVisible(false);
            if (layers) layers->setEnabled("alphaearth", false);
        }
    }

    ImGui::PopTextWrapPos();
    ImGui::End();
    popScienceScrollbarStyle();
}
