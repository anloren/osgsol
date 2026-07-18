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
    double requestedSpanMeters,
    const std::string& sentinelIntervalEndUtc)
{
    if (source.id == "sentinel-2-l2a")
        return makeSentinel2PreviewQuery(
            source, latitude, longitude, sentinelIntervalEndUtc,
            state.sentinelWindowDays,
            state.sentinelMaximumCloudPercent, requestedSpanMeters);
    if (source.id == "copernicus-dem-glo-30")
        return makeCopernicusDemPreviewQuery(
            source, latitude, longitude, requestedSpanMeters);
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
        << query.time.instant << '|'
        << query.time.intervalStart << '|'
        << query.time.intervalEnd << '|'
        << query.targetResolutionMeters << '|'
        << static_cast<int>(query.aggregation) << '|'
        << static_cast<int>(query.priority) << '|'
        << query.visualizationId << '|' << query.purpose << '|'
        << static_cast<int>(query.analysis.kind) << '|'
        << query.analysis.baselineYear << '|'
        << query.analysis.comparisonYear << '|'
        << query.analysis.gridSize << '|' << query.analysis.hotspotQuantile << '|'
        << query.analysis.enablePca << '|' << query.analysis.pcaComponents << '|'
        << query.analysis.enableClustering << '|' << query.analysis.clusterCount
        << '|' << query.sceneFilters.maximumCloudCoverPercent
        << '|' << query.sceneFilters.maximumScenes;
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

const char* modeLabel(SciencePanelMode mode, const std::string& sourceId)
{
    if (sourceId == "sentinel-2-l2a" && mode == SciencePanelMode::Preview)
        return u8"真彩场景 / True-color scene";
    if (sourceId == "copernicus-dem-glo-30" &&
        mode == SciencePanelMode::Preview)
        return u8"地表高程 / Surface elevation";
    return modeLabel(mode);
}

void drawPrimaryMetrics(const earthscience::ScienceArtifact& artifact)
{
    if (artifact.query.sourceId == "sentinel-2-l2a")
    {
        if (!artifact.sourceReferences.empty())
        {
            const earthscience::ScienceSourceReference& reference =
                artifact.sourceReferences.front();
            if (!reference.acquisitionTime.empty())
                ImGui::TextWrapped(u8"实际采集 / Acquisition: %s",
                                   reference.acquisitionTime.c_str());
            for (const earthscience::ScienceEvidenceField& field :
                 reference.fields)
                if (field.id == "scene_cloud_cover")
                {
                    ImGui::TextWrapped(u8"场景云量 / Scene cloud: %s %s",
                        field.value.c_str(), field.unit.c_str());
                    break;
                }
        }
        if (artifact.raster.width > 0)
            ImGui::TextWrapped(u8"显示网格 / Display grid: %d × %d",
                               artifact.raster.width, artifact.raster.height);
        if (hasGeographicExtent(artifact.raster.bounds))
            ImGui::TextWrapped(u8"实际范围 / Footprint: %s",
                formatBounds(artifact.raster.bounds).c_str());
        if (artifact.raster.sourceResolutionMeters > 0.0 &&
            artifact.raster.displayResolutionMeters > 0.0)
            ImGui::TextWrapped(
                u8"分辨率 / Resolution: 源 %s · 显示 %s",
                formatResolution(
                    artifact.raster.sourceResolutionMeters).c_str(),
                formatResolution(
                    artifact.raster.displayResolutionMeters).c_str());
        return;
    }
    if (artifact.query.sourceId == "copernicus-dem-glo-30")
    {
        if (!artifact.scalarSummaries.empty())
        {
            const earthscience::ScienceScalarSummary& summary =
                artifact.scalarSummaries.front();
            if (summary.centerValid)
                ImGui::TextWrapped(
                    u8"中心 DSM 高程 / Center: %.1f %s",
                    summary.center, summary.unit.c_str());
            if (summary.minimumValid && summary.maximumValid &&
                summary.meanValid)
                ImGui::TextWrapped(
                    u8"范围与均值 / Min · mean · max: %.1f · %.1f · %.1f %s",
                    summary.minimum, summary.mean, summary.maximum,
                    summary.unit.c_str());
            ImGui::TextWrapped(
                u8"有效 / NoData: %s / %s cells",
                formatInteger(summary.validCellCount).c_str(),
                formatInteger(summary.noDataCellCount).c_str());
        }
        if (artifact.raster.sourceResolutionMeters > 0.0 &&
            artifact.raster.displayResolutionMeters > 0.0)
            ImGui::TextWrapped(
                u8"分辨率 / Resolution: 源 %s · 显示 %s",
                formatResolution(
                    artifact.raster.sourceResolutionMeters).c_str(),
                formatResolution(
                    artifact.raster.displayResolutionMeters).c_str());
        return;
    }
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
    if (artifact.query.sourceId == "sentinel-2-l2a")
    {
        ImGui::TextWrapped(
            u8"自然色通道 / Natural color: R = B4 · G = B3 · B = B2");
    }
    else if (artifact.query.sourceId == "copernicus-dem-glo-30")
    {
        drawLegendLine("dem_water", ImVec4(0.15f, 0.35f, 0.66f, 1.0f),
                       u8"-500–0 m · 蓝 / Blue");
        drawLegendLine("dem_low", ImVec4(0.38f, 0.65f, 0.36f, 1.0f),
                       u8"0–200 m · 绿 / Green");
        drawLegendLine("dem_mid", ImVec4(0.79f, 0.72f, 0.49f, 1.0f),
                       u8"200–3000 m · 黄褐 / Tan");
        drawLegendLine("dem_high", ImVec4(0.57f, 0.41f, 0.30f, 1.0f),
                       u8"3000–6000 m · 棕至白 / Brown to white");
        ImGui::TextDisabled(u8"固定色标；透明 = NoData");
    }
    else if (artifact.analysis.kind == earthscience::ScienceAnalysisKind::None &&
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
            if (!source.originalUrl.empty())
                ImGui::TextWrapped(u8"原始资产：%s",
                                   source.originalUrl.c_str());
            for (const earthscience::ScienceEvidenceField& field :
                 source.fields)
            {
                ImGui::TextWrapped("%s: %s%s%s",
                    field.displayName.c_str(), field.value.c_str(),
                    field.unit.empty() ? "" : " ", field.unit.c_str());
            }
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
        {"no coverage", "no alphaearth tile", "does not cover",
         "covers this point", "no sentinel-2 scene", "cloud threshold"});
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

SciencePanelModeCapabilities sciencePanelModeCapabilities(
    const earthscience::ScienceSourceDescriptor& source,
    SciencePanelMode mode)
{
    SciencePanelModeCapabilities capabilities =
        sciencePanelModeCapabilities(mode);
    if (mode == SciencePanelMode::Preview)
        capabilities.showsSingleYear = source.capabilities.explicitYears;
    return capabilities;
}

const earthscience::ScienceSourceDescriptor* resolveSciencePanelSource(
    const std::vector<earthscience::ScienceSourceDescriptor>& sources,
    const std::string& sourceId)
{
    for (const earthscience::ScienceSourceDescriptor& source : sources)
        if (source.id == sourceId) return &source;
    for (const earthscience::ScienceSourceDescriptor& source : sources)
        if (source.id == "alphaearth-foundations") return &source;
    return sources.empty() ? nullptr : &sources.front();
}

std::vector<SciencePanelMode> sciencePanelModesForSource(
    const earthscience::ScienceSourceDescriptor& source)
{
    std::vector<SciencePanelMode> modes;
    if (source.capabilities.rasterLayerOutput)
        modes.push_back(SciencePanelMode::Preview);
    if (source.capabilities.timeSeriesOutput)
        modes.push_back(SciencePanelMode::PointSeries);
    if (source.capabilities.analysisOutput)
        modes.push_back(SciencePanelMode::RegionalChange);
    return modes;
}

SciencePanelMode activeSciencePanelMode(
    const earthscience::ScienceSourceDescriptor& source,
    SciencePanelMode requested)
{
    const std::vector<SciencePanelMode> modes =
        sciencePanelModesForSource(source);
    return std::find(modes.begin(), modes.end(), requested) != modes.end()
        ? requested
        : (modes.empty() ? SciencePanelMode::Preview : modes.front());
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

const char* sciencePanelPrimaryActionLabel(
    SciencePanelMode mode, const std::string& sourceId)
{
    if (sourceId == "sentinel-2-l2a" && mode == SciencePanelMode::Preview)
        return u8"加载 Sentinel-2 真彩场景";
    if (sourceId == "copernicus-dem-glo-30" &&
        mode == SciencePanelMode::Preview)
        return u8"加载 Copernicus DEM 高程";
    return sciencePanelPrimaryActionLabel(mode);
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
    if (artifact.query.sourceId == "sentinel-2-l2a")
        scope << u8"Sentinel-2 · 真彩场景";
    else if (artifact.query.sourceId == "copernicus-dem-glo-30")
        scope << u8"Copernicus DEM · DSM 地表高程";
    else if (artifactMatchesMode(artifact, SciencePanelMode::Preview))
        scope << u8"AlphaEarth · 伪彩预览";
    else if (artifactMatchesMode(artifact, SciencePanelMode::PointSeries))
        scope << u8"点位年度变化";
    else if (artifactMatchesMode(artifact, SciencePanelMode::RegionalChange))
        scope << u8"区域年度变化";
    else
        scope << u8"ScienceEarth 结果";
    if (artifact.query.sourceId == "sentinel-2-l2a")
    {
        for (const earthscience::ScienceSourceReference& reference :
             artifact.sourceReferences)
            if (!reference.acquisitionTime.empty())
            {
                scope << u8" · 采集 " << reference.acquisitionTime;
                break;
            }
    }
    else if (artifact.query.sourceId == "copernicus-dem-glo-30" &&
             !artifact.query.time.publicationTime.empty())
        scope << u8" · " << artifact.query.time.publicationTime << u8" 发布";
    else if (!artifact.query.time.explicitYears.empty())
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
    case ScienceHelpTopic::Sentinel2Meaning:
        return u8"Sentinel-2 L2A 是什么？";
    case ScienceHelpTopic::Sentinel2NaturalColor:
        return u8"真彩场景表示什么？";
    case ScienceHelpTopic::Sentinel2Cloud:
        return u8"最大场景云量是什么？";
    case ScienceHelpTopic::Sentinel2Limits:
        return u8"Sentinel-2 科学边界";
    case ScienceHelpTopic::CopernicusDemMeaning:
        return u8"Copernicus DEM 表示什么？";
    case ScienceHelpTopic::CopernicusDemColors:
        return u8"高程颜色表示什么？";
    case ScienceHelpTopic::CopernicusDemLimits:
        return u8"Copernicus DEM 科学边界";
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
    case ScienceHelpTopic::Sentinel2Meaning:
        return u8"Sentinel-2 Level-2A 是经大气校正的地表反射率产品。"
               u8"本功能先搜索一个时间窗口，再从符合云量阈值的候选中"
               u8"确定性选择单个场景。";
    case ScienceHelpTopic::Sentinel2NaturalColor:
        return u8"这里显示官方 visual/TCI 真彩显示产品：红、绿、蓝来自"
               u8"可见光波段。它便于目视判读，但不是原始反射率数值，"
               u8"也不是无云合成图。";
    case ScienceHelpTopic::Sentinel2Cloud:
        return u8"云量是整个卫星场景的 eo:cloud_cover 元数据，不是当前"
               u8"256×256 范围内逐像素的云掩膜。阈值越低，可用场景可能越少。";
    case ScienceHelpTopic::Sentinel2Limits:
        return u8"当前切片只选择一个日期、一个场景和一个 COG，不做"
               u8"多景镶嵌、逐像素云检测、光谱指数或变化归因。";
    case ScienceHelpTopic::CopernicusDemMeaning:
        return u8"Copernicus DEM GLO-30 是约 30 m 的数字表面模型（DSM）。"
               u8"数值单位为米，垂直基准为 EGM2008；表面可包含建筑、"
               u8"基础设施和植被，不等同于裸地高程。";
    case ScienceHelpTopic::CopernicusDemColors:
        return u8"固定分层设色把 DSM 高程映射为蓝、绿、黄褐、棕、白；"
               u8"它不是自然色影像，透明表示 NoData。数值解释以结果统计为准。";
    case ScienceHelpTopic::CopernicusDemLimits:
        return u8"这是静态 DSM，不是裸地 DTM，也不是逐年变化产品。"
               u8"当前切片不推断地物类型、建成年份或变化原因。";
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

    std::ostringstream time;
    if (artifact.query.time.mode == earthscience::ScienceTimeMode::Interval)
    {
        time << u8"请求时间 / Requested interval: ";
        if (artifact.query.time.intervalStart.empty() ||
            artifact.query.time.intervalEnd.empty())
            time << unavailable;
        else
            time << artifact.query.time.intervalStart << " — "
                 << artifact.query.time.intervalEnd;
    }
    else if (artifact.query.time.mode == earthscience::ScienceTimeMode::Instant)
    {
        time << u8"产品发布 / Product release: ";
        time << (artifact.query.time.publicationTime.empty()
            ? unavailable : artifact.query.time.publicationTime);
    }
    else
    {
        time << u8"已载入结果年份 / Loaded artifact year(s): ";
        if (artifact.query.time.explicitYears.empty())
            time << unavailable;
        else for (std::size_t index = 0;
                  index < artifact.query.time.explicitYears.size(); ++index)
        {
            if (index > 0) time << ", ";
            time << artifact.query.time.explicitYears[index];
        }
    }
    lines.push_back(time.str());

    for (const earthscience::ScienceSourceReference& reference :
         artifact.sourceReferences)
    {
        if (!reference.acquisitionTime.empty())
            lines.push_back(std::string(
                u8"实际采集 / Acquisition: ") + reference.acquisitionTime);
        for (const earthscience::ScienceEvidenceField& field : reference.fields)
            if (field.id == "scene_id" ||
                field.id == "scene_cloud_cover")
            {
                std::string value = field.displayName + ": " + field.value;
                if (!field.unit.empty()) value += ' ' + field.unit;
                lines.push_back(std::move(value));
            }
    }

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
    const earthscience::ScienceSourceDescriptor* selectedSource =
        resolveSciencePanelSource(sources, _state.sourceId);
    if (!selectedSource) return;
    if (operationsExpanded)
    {
        ImGui::TextWrapped(u8"数据源 / Source");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(
                "##science_source", selectedSource->name.c_str()))
        {
            for (const earthscience::ScienceSourceDescriptor& candidate :
                 sources)
            {
                const bool selected = candidate.id == selectedSource->id;
                if (ImGui::Selectable(candidate.name.c_str(), selected))
                {
                    const earthscience::ScienceJobSnapshot active =
                        service->snapshot();
                    if (active.state == earthscience::ScienceJobState::Queued ||
                        active.state == earthscience::ScienceJobState::Fetching)
                        service->cancel(active.jobId);
                    selectedSource = &candidate;
                    _state.sourceId = candidate.id;
                    _displayedEstimateKey.clear();
                    _confirmedEstimateKey.clear();
                    _hasCurrentDraft = false;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }
    const earthscience::ScienceSourceDescriptor& source = *selectedSource;
    _state.sourceId = source.id;
    SciencePanelMode activeMode = activeSciencePanelMode(source, _state.mode);
    const std::string expectedVisualizationId =
        source.id == "sentinel-2-l2a" ? "natural-color-visual" :
        source.id == "copernicus-dem-glo-30"
            ? "surface-elevation-hypsometric"
            : "false-color-a01-a16-a09";
    const earthscience::ScienceVisualizationDescriptor* visualization =
        findScienceVisualization(source, expectedVisualizationId);
    if (visualization && visualization->id != expectedVisualizationId)
        visualization = nullptr;
    const osg::Vec3d target = manipulator->computeViewPointLatLonHeight();
    const osg::Vec3d eye = manipulator->computeEyeLatLonHeight();
    const double latitude = osg::RadiansToDegrees(target[0]);
    const double longitude = osg::RadiansToDegrees(target[1]);
    const double requestedSpanMeters = std::clamp(
        eye[2] * 0.85, 2560.0, 81920.0);

    if (source.capabilities.explicitYears)
    {
        _state.firstYear = std::clamp(
            _state.firstYear, source.firstYear, source.lastYear);
        _state.lastYear = std::clamp(
            _state.lastYear, source.firstYear, source.lastYear);
        _state.baselineYear = std::clamp(
            _state.baselineYear, source.firstYear, source.lastYear);
        _state.comparisonYear = std::clamp(
            _state.comparisonYear, source.firstYear, source.lastYear);
    }
    _state.locationMode = activeMode == SciencePanelMode::RegionalChange
        ? SciencePanelLocationMode::CurrentViewFootprint
        : SciencePanelLocationMode::CurrentLocation;

    SciencePanelState draftState = _state;
    draftState.mode = activeMode;
    const std::string sentinelIntervalEndUtc = scienceCurrentUtcDayEnd();
    _currentDraft = buildSciencePanelDraft(
        source, visualization, draftState, latitude, longitude,
        requestedSpanMeters, sentinelIntervalEndUtc);
    _hasCurrentDraft = !_currentDraft.sourceId.empty();
    if (!operationsExpanded) return;

    const earthscience::ScienceJobSnapshot snapshot = service->snapshot();

    drawColoredWrapped(ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
                       source.name.c_str());
    drawHelpButton("data_meaning",
        source.id == "sentinel-2-l2a"
            ? ScienceHelpTopic::Sentinel2Meaning
            : source.id == "copernicus-dem-glo-30"
                ? ScienceHelpTopic::CopernicusDemMeaning
                : ScienceHelpTopic::DataMeaning,
        false);
    if (ImGui::CollapsingHeader(u8"数据源详情 / Source details"))
    {
        ImGui::TextWrapped(u8"原始分辨率：%.1f m",
                           source.nativeResolutionMeters);
        ImGui::TextWrapped(source.id == "sentinel-2-l2a"
                ? u8"显示通道：%d"
                : source.id == "copernicus-dem-glo-30"
                    ? u8"数值波段：%d" : u8"潜在分量：%d",
            source.componentCount);
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
    const std::vector<SciencePanelMode> modes =
        sciencePanelModesForSource(source);
    if (modes.size() == 1)
        ImGui::TextWrapped("%s", modeLabel(modes.front(), source.id));
    else
    {
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(
                "##science_mode", modeLabel(activeMode, source.id)))
        {
            for (SciencePanelMode candidate : modes)
            {
                const bool selected = candidate == activeMode;
                if (ImGui::Selectable(
                        modeLabel(candidate, source.id), selected))
                {
                    _state.mode = candidate;
                    activeMode = candidate;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    const SciencePanelModeCapabilities capabilities =
        sciencePanelModeCapabilities(source, activeMode);

    if (source.id == "sentinel-2-l2a")
    {
        static const int WINDOWS[] = {7, 30, 90};
        ImGui::TextWrapped(u8"时间窗口 / Time window");
        const std::string windowPreview =
            std::to_string(_state.sentinelWindowDays) + u8" 天 / days";
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo("##sentinel_window", windowPreview.c_str()))
        {
            for (int window : WINDOWS)
            {
                const bool selected = _state.sentinelWindowDays == window;
                const std::string label =
                    std::to_string(window) + u8" 天 / days";
                if (ImGui::Selectable(label.c_str(), selected))
                    _state.sentinelWindowDays = window;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextWrapped(u8"最大场景云量 / Maximum scene cloud");
        drawHelpButton(
            "sentinel_cloud", ScienceHelpTopic::Sentinel2Cloud);
        static const double CLOUDS[] = {10.0, 20.0, 40.0, 100.0};
        const std::string cloudPreview =
            _state.sentinelMaximumCloudPercent >= 100.0
                ? std::string(u8"不限 / Any (100%)")
                : std::to_string(static_cast<int>(
                      _state.sentinelMaximumCloudPercent)) + "%";
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(
                "##sentinel_cloud", cloudPreview.c_str()))
        {
            for (double cloud : CLOUDS)
            {
                const bool selected =
                    _state.sentinelMaximumCloudPercent == cloud;
                const std::string label = cloud >= 100.0
                    ? std::string(u8"不限 / Any (100%)")
                    : std::to_string(static_cast<int>(cloud)) + "%";
                if (ImGui::Selectable(label.c_str(), selected))
                    _state.sentinelMaximumCloudPercent = cloud;
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled(u8"截至 UTC 今日；选择不会移动相机");
    }
    else if (source.id == "copernicus-dem-glo-30")
        ImGui::TextDisabled(
            u8"静态 2021 公共发布 · 无年份滑块 · 选择不会移动相机");
    else if (capabilities.showsSingleYear)
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

    draftState = _state;
    draftState.mode = activeMode;
    earthscience::GeoTemporalQuery query = buildSciencePanelDraft(
        source, visualization, draftState, latitude, longitude,
        requestedSpanMeters, sentinelIntervalEndUtc);
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
        describeScienceSnapshot(snapshot, activeMode);
    const bool invalidPreview =
        activeMode == SciencePanelMode::Preview && !visualization;
    const bool blocked = sourceUnavailable || invalidPreview || estimateFailed ||
        presentation.busy || (_estimateVisible && !estimateConfirmed);
    if (blocked) ImGui::BeginDisabled();
    if (ImGui::Button(
            sciencePanelPrimaryActionLabel(activeMode, source.id),
                      ImVec2(-1.0f, 0.0f)))
    {
        query.analysis.confirmedLargeRequest =
            _estimateVisible && estimateConfirmed;
        if (activeMode == SciencePanelMode::Preview)
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
    const std::vector<earthscience::ScienceSourceDescriptor> sources =
        service->listSources();
    const earthscience::ScienceSourceDescriptor* source =
        resolveSciencePanelSource(sources, _state.sourceId);
    const SciencePanelMode activeMode = source
        ? activeSciencePanelMode(*source, _state.mode)
        : SciencePanelMode::Preview;
    const SciencePanelPresentation presentation =
        describeScienceSnapshot(snapshot, activeMode);
    const std::shared_ptr<const earthscience::ScienceArtifact> artifact =
        selectSciencePanelArtifact(snapshot, activeMode);
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
        if (artifact->query.sourceId != "sentinel-2-l2a" &&
            evidence.size() > 1)
            ImGui::TextWrapped("%s", evidence[1].c_str());

        drawPrimaryMetrics(*artifact);
        ImGui::SeparatorText(u8"图表或图例");
        if (!drawMetricSeries(*artifact)) drawEmbeddingLegend(*artifact);
        drawHelpButton("preview_colors",
            artifact->query.sourceId == "sentinel-2-l2a"
                ? ScienceHelpTopic::Sentinel2NaturalColor
                : artifact->query.sourceId == "copernicus-dem-glo-30"
                    ? ScienceHelpTopic::CopernicusDemColors
                    : ScienceHelpTopic::PreviewColors,
                       false);

        ImGui::TextDisabled(u8"科学解释边界");
        drawHelpButton(
            "scientific_limits",
            artifact->query.sourceId == "sentinel-2-l2a"
                ? ScienceHelpTopic::Sentinel2Limits
                : artifact->query.sourceId == "copernicus-dem-glo-30"
                    ? ScienceHelpTopic::CopernicusDemLimits
                    : ScienceHelpTopic::ScientificLimits);

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
