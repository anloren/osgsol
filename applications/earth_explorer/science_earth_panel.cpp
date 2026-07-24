#include "science_earth_panel.h"

#include "LayerManager.h"
#include "earth_control_layout.h"
#include "earth_ui_chart.h"
#include "earth_ui_tokens.h"
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

bool isEra5AgroSource(const std::string& sourceId)
{
    return sourceId == "era5-land-surface-history" ||
        sourceId == "era5-agricultural-climate";
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
    if (isEra5AgroSource(source.id))
        return makeScienceVariablePointSeriesQuery(
            source, latitude, longitude, state.firstYear, state.lastYear);
    if (state.mode == SciencePanelMode::Preview && visualization)
        return makeSciencePointQuery(
            source, *visualization, latitude, longitude,
            state.lastYear, requestedSpanMeters);
    if (state.mode == SciencePanelMode::PointSeries)
    {
        earthscience::GeoTemporalQuery query = makeSciencePointSeriesQuery(
            source, latitude, longitude, state.firstYear, state.lastYear);
        query.analysis.metrics = sciencePanelSelectedMetrics(state.mode, state);
        return query;
    }
    if (state.mode == SciencePanelMode::RegionalChange)
    {
        earthscience::ScienceAnalysisOptions options;
        options.metrics = sciencePanelSelectedMetrics(state.mode, state);
        options.hotspotQuantile = state.hotspotQuantile;
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
        return earthui::design::kCyan;
    case SciencePanelSeverity::Success:
        return earthui::design::kSuccess;
    case SciencePanelSeverity::Warning:
        return earthui::design::kMeasure;
    case SciencePanelSeverity::Error:
        return earthui::design::kDanger;
    case SciencePanelSeverity::Neutral:
        return earthui::design::kText;
    }
    return earthui::design::kText;
}

void drawColoredWrapped(const ImVec4& color, const char* text)
{
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void drawDisabledWrapped(const char* text)
{
    ImGui::PushStyleColor(
        ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
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

void drawLabeledHelpButton(const char* id, const char* label,
                           ScienceHelpTopic topic)
{
    ImGui::PushID(id);
    if (ImGui::SmallButton(label)) ImGui::OpenPopup("##science_help_popup");
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
                          earthui::design::kRaisedIron);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered,
                          earthui::design::kCyan);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,
                          earthui::design::kVermilion);
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

const char* simpleModeLabel(SciencePanelMode mode)
{
    switch (mode)
    {
    case SciencePanelMode::Preview:
        return u8"单个年份：查看空间特征图";
    case SciencePanelMode::PointSeries:
        return u8"固定位置：比较历年变化";
    case SciencePanelMode::RegionalChange:
        return u8"当前视野：比较两个年份";
    }
    return u8"未知 / Unknown";
}

const char* modeLabelForSource(
    SciencePanelMode mode, const std::string& sourceId)
{
    if (sourceId == "sentinel-2-l2a" && mode == SciencePanelMode::Preview)
        return u8"查看近期真彩卫星影像";
    if (sourceId == "copernicus-dem-glo-30" &&
        mode == SciencePanelMode::Preview)
        return u8"查看当前视野的地表高程";
    if (isEra5AgroSource(sourceId) &&
        mode == SciencePanelMode::PointSeries)
        return u8"固定位置：年度农业气候";
    return simpleModeLabel(mode);
}

const char* modeDescriptionForSource(
    SciencePanelMode mode, const std::string& sourceId)
{
    if (sourceId == "sentinel-2-l2a")
        return u8"在当前视野查找所选时间窗口内云量较低的 Sentinel-2 真彩场景。";
    if (sourceId == "copernicus-dem-glo-30")
        return u8"加载当前视野的 DSM 地表高程；数值包含建筑与树冠，并非裸土地形。";
    if (isEra5AgroSource(sourceId))
        return u8"固定屏幕中心，按完整年份汇总数据源网格的再分析数据；"
               u8"它用于区域气候背景，不是气象站或地块级实测。";
    switch (mode)
    {
    case SciencePanelMode::Preview:
        return u8"把所选年份的 64 维地表表征压成伪彩图，用于寻找空间结构；颜色不是自然色或单一物理指标。";
    case SciencePanelMode::PointSeries:
        return u8"固定屏幕中心同一位置，逐年比较 64 维地表表征；变化是高维相对变化，不等同于温度、植被等单一指标。";
    case SciencePanelMode::RegionalChange:
        return u8"比较当前视野在两个年份的 64 维表征，定位变化较强的区域；结果不自动给变化原因下结论。";
    }
    return "";
}

std::string selectionSummaryForState(
    SciencePanelMode mode, const SciencePanelState& state,
    const std::string& sourceId)
{
    std::ostringstream out;
    if (sourceId == "sentinel-2-l2a")
        out << u8"已选择：最近 " << state.sentinelWindowDays
            << u8" 天 · 当前视野 · 尚未开始";
    else if (sourceId == "copernicus-dem-glo-30")
        out << u8"已选择：2021 公共版高程 · 当前视野 · 尚未开始";
    else if (isEra5AgroSource(sourceId))
    {
        const int first = std::min(state.firstYear, state.lastYear);
        const int last = std::max(state.firstYear, state.lastYear);
        out << u8"已选择：当前位置 · " << first << "–" << last
            << u8" · 年度农业气候剖面 · 尚未开始";
    }
    else if (mode == SciencePanelMode::Preview)
        out << u8"已选择：" << state.lastYear
            << u8" 年 · 当前视野 · 尚未开始";
    else if (mode == SciencePanelMode::PointSeries)
    {
        const int first = std::min(state.firstYear, state.lastYear);
        const int last = std::max(state.firstYear, state.lastYear);
        out << u8"已选择：当前位置 · " << first << "–" << last
            << u8" · " << (last - first + 1) << u8" 个年度 · 尚未开始";
    }
    else
        out << u8"已选择：当前视野 · " << state.baselineYear << u8" 对比 "
            << state.comparisonYear << u8" · 尚未开始";
    return out.str();
}

void drawPrimaryMetrics(const earthscience::ScienceArtifact& artifact)
{
    if (!artifact.variableSeries.empty())
    {
        int shown = 0;
        for (const earthscience::ScienceVariableSeries& series :
             artifact.variableSeries)
        {
            if (shown == 3 || !series.years || !series.values ||
                series.years->size() != series.values->size())
                continue;
            for (std::size_t offset = 0; offset < series.values->size(); ++offset)
            {
                const std::size_t index = series.values->size() - 1 - offset;
                const bool valid = !series.validity ||
                    (series.validity->size() == series.values->size() &&
                     series.validity->at(index) != 0);
                if (!valid || !std::isfinite(series.values->at(index))) continue;
                ImGui::TextWrapped("%s · %d: %.3f %s",
                    series.displayName.c_str(), series.years->at(index),
                    series.values->at(index), series.unit.c_str());
                ++shown;
                break;
            }
        }
        return;
    }
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
    const std::vector<std::string> highlights =
        describeScienceAnalysisHighlights(artifact);
    if (!highlights.empty())
    {
        for (const std::string& highlight : highlights)
            ImGui::TextWrapped("%s", highlight.c_str());
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
    if (!artifact.variableSeries.empty())
    {
        bool drewAny = false;
        ImGui::TextWrapped(
            u8"再分析数据源网格的年度汇总 / Annual source-grid summaries");
        for (std::size_t seriesIndex = 0;
             seriesIndex < artifact.variableSeries.size(); ++seriesIndex)
        {
            const earthscience::ScienceVariableSeries& series =
                artifact.variableSeries[seriesIndex];
            if (!series.years || !series.values || series.values->empty())
                continue;
            const std::string id =
                "##science_variable_series_" + std::to_string(seriesIndex);
            drewAny = earthui::drawAnnualSeriesChart(
                id.c_str(), series.displayName.c_str(), series.years.get(),
                series.values.get(), series.validity.get(), series.unit) ||
                drewAny;
        }
        return drewAny;
    }
    if (!artifact.analysis.annualSeries ||
        artifact.analysis.annualSeries->empty()) return false;
    bool drewAny = false;
    ImGui::TextWrapped(
        u8"相对所选起始年的年度轨迹 / Annual trajectory from baseline");
    for (std::size_t seriesIndex = 0;
         seriesIndex < artifact.analysis.annualSeries->size(); ++seriesIndex)
    {
        const earthscience::ScienceAnnualSeries& series =
            artifact.analysis.annualSeries->at(seriesIndex);
        if (!series.values || series.values->empty()) continue;
        const std::string id =
            "##science_metric_series_" + std::to_string(seriesIndex);
        drewAny = earthui::drawAnnualSeriesChart(
            id.c_str(), sciencePanelMetricLabel(series.metric),
            series.years.get(), series.values.get(), series.validity.get(),
            series.unit) || drewAny;
    }
    return drewAny;
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
        drawDisabledWrapped(u8"固定色标；透明 = NoData");
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

const char* sciencePanelModeLabel(
    SciencePanelMode mode, const std::string& sourceId)
{
    return modeLabelForSource(mode, sourceId);
}

const char* sciencePanelModeDescription(
    SciencePanelMode mode, const std::string& sourceId)
{
    return modeDescriptionForSource(mode, sourceId);
}

std::vector<earthscience::ScienceMetric> sciencePanelSelectedMetrics(
    SciencePanelMode mode, const SciencePanelState& state)
{
    using Metric = earthscience::ScienceMetric;
    if (mode == SciencePanelMode::RegionalChange)
        return {state.regionalMetric};
    switch (state.pointMetricChoice)
    {
    case SciencePanelMetricChoice::DirectionChange:
        return {Metric::CosineDistance};
    case SciencePanelMetricChoice::VectorDisplacement:
        return {Metric::EuclideanDistance};
    case SciencePanelMetricChoice::DirectionAndDisplacement:
        return {Metric::CosineDistance, Metric::EuclideanDistance};
    }
    return {Metric::CosineDistance};
}

const char* sciencePanelMetricLabel(earthscience::ScienceMetric metric)
{
    using Metric = earthscience::ScienceMetric;
    switch (metric)
    {
    case Metric::CosineDistance:
        return u8"方向变化 / Cosine distance";
    case Metric::EuclideanDistance:
        return u8"向量位移 / Euclidean distance";
    case Metric::AngularDistance:
        return u8"方向夹角 / Angular distance";
    case Metric::CosineSimilarity:
        return u8"方向相似 / Cosine similarity";
    case Metric::DotProduct:
        return u8"点积（高级） / Dot product";
    }
    return u8"未知度量 / Unknown metric";
}

const char* sciencePanelMetricDescription(SciencePanelMetricChoice choice)
{
    switch (choice)
    {
    case SciencePanelMetricChoice::DirectionChange:
        return u8"比较归一化后的 64 维方向；值越大，嵌入关系变化越强。";
    case SciencePanelMetricChoice::VectorDisplacement:
        return u8"计算 64 维向量的 L2 位移；它是嵌入空间距离，不是米或物理量。";
    case SciencePanelMetricChoice::DirectionAndDisplacement:
        return u8"同时显示方向变化与 L2 位移。对接近单位长度的 AlphaEarth "
               u8"向量，两者是相关刻度，不应当作两份独立证据。";
    }
    return "";
}

std::string sciencePanelSelectionSummary(
    SciencePanelMode mode, const SciencePanelState& state,
    const std::string& sourceId)
{
    return selectionSummaryForState(mode, state, sourceId);
}

bool sciencePanelSnapshotMatchesDraft(
    const earthscience::ScienceJobSnapshot& snapshot,
    const earthscience::GeoTemporalQuery& currentDraft)
{
    if (snapshot.jobId == 0) return false;
    const earthscience::ScienceQueryCost emptyCost;
    return queryEstimateKey(snapshot.query, emptyCost) ==
        queryEstimateKey(currentDraft, emptyCost);
}

bool acknowledgeSciencePanelSubmission(
    std::uint64_t submittedJobId,
    const earthscience::ScienceJobSnapshot& submittedSnapshot,
    SciencePanelState* state)
{
    if (!state || submittedJobId == 0 ||
        submittedSnapshot.jobId != submittedJobId ||
        submittedSnapshot.state == earthscience::ScienceJobState::Idle)
        return false;
    state->resultExpanded = true;
    return true;
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

    if (!snapshot.query.sourceId.empty())
        view.sourceText = std::string(u8"数据源 / Source: ") +
            snapshot.query.sourceId;
    switch (view.kind)
    {
    case SciencePanelResultKind::NoCoverage:
        view.retryText =
            u8"可重试 / Retry: 调整位置、年份或筛选条件后重新运行";
        break;
    case SciencePanelResultKind::Failed:
        view.retryText =
            u8"可重试 / Retry: 数据源恢复后可重新运行；旧结果不会被覆盖";
        break;
    case SciencePanelResultKind::Cancelled:
        view.retryText = u8"可重试 / Retry: 可以重新开始同一设置";
        break;
    case SciencePanelResultKind::Stale:
        view.retryText =
            u8"无需重试 / Retry not needed: 已有更新的请求接替";
        break;
    default:
        break;
    }

    return view;
}

SciencePanelWorkflowStrip describeScienceWorkflowStrip(
    const earthscience::ScienceSourceDescriptor& source,
    SciencePanelMode mode,
    const SciencePanelState& state,
    const earthscience::GeoTemporalQuery& draft,
    const earthscience::ScienceJobSnapshot& snapshot,
    const std::string& visibleArtifactId)
{
    (void)mode;
    (void)state;
    SciencePanelWorkflowStrip strip;
    strip.source = source.name.empty() ? source.id : source.name;

    std::ostringstream area;
    area << std::fixed << std::setprecision(4);
    if (draft.geometry.kind == earthscience::ScienceGeometryKind::Point)
        area << draft.geometry.point.latitude << ", "
             << draft.geometry.point.longitude;
    else
        area << "W " << draft.geometry.bounds.west
             << " · S " << draft.geometry.bounds.south
             << " · E " << draft.geometry.bounds.east
             << " · N " << draft.geometry.bounds.north;
    strip.area = area.str();

    std::ostringstream time;
    if (draft.time.mode == earthscience::ScienceTimeMode::ExplicitYears)
    {
        if (!draft.time.explicitYears.empty())
        {
            time << draft.time.explicitYears.front();
            if (draft.time.explicitYears.size() > 1)
                time << u8"→" << draft.time.explicitYears.back()
                     << u8" · " << draft.time.explicitYears.size()
                     << u8" 年";
        }
    }
    else if (draft.time.mode == earthscience::ScienceTimeMode::Interval)
        time << draft.time.intervalStart << " — " << draft.time.intervalEnd;
    else
        time << (draft.time.publicationTime.empty()
            ? draft.time.instant : draft.time.publicationTime);
    strip.time = time.str().empty() ? u8"未选择 / not selected" : time.str();

    std::ostringstream method;
    if (isEra5AgroSource(draft.sourceId))
        method << u8"数据源网格日值 → 年均值/年总量";
    else if (draft.analysis.kind == earthscience::ScienceAnalysisKind::None)
    {
        if (draft.visualizationId == "natural-color-visual")
            method << u8"自然色影像 / Natural color";
        else if (draft.visualizationId == "surface-elevation-hypsometric")
            method << u8"地表高程设色 / Elevation";
        else
            method << u8"64 维定位伪彩 / False color";
    }
    else
    {
        for (std::size_t index = 0;
             index < draft.analysis.metrics.size(); ++index)
        {
            if (index > 0) method << " + ";
            method << sciencePanelMetricLabel(draft.analysis.metrics[index]);
        }
        if (draft.analysis.enablePca) method << " + PCA";
        if (draft.analysis.enableClustering)
            method << u8" + 聚类 " << draft.analysis.clusterCount;
    }
    strip.method = method.str();
    strip.stage = sciencePanelSnapshotMatchesDraft(snapshot, draft)
        ? stageLabel(snapshot.progress.stage)
        : u8"尚未提交 / Not submitted";
    strip.visibleArtifactId = visibleArtifactId.empty()
        ? u8"无 / none" : visibleArtifactId;
    return strip;
}

SciencePanelDisplayPresentation describeScienceDisplayState(
    const earthscience::ScienceJobSnapshot& snapshot,
    SciencePanelMode mode,
    bool layerVisible,
    bool publisherHasArtifact,
    bool rendererUnavailable,
    const std::string& rendererMessage)
{
    SciencePanelDisplayPresentation view;
    const std::shared_ptr<const earthscience::ScienceArtifact> artifact =
        selectSciencePanelArtifact(snapshot, mode);
    if (!artifact)
    {
        view.title = u8"尚无可显示结果 / No display artifact";
        return view;
    }

    const bool failedReplacement =
        snapshot.state == earthscience::ScienceJobState::Failed ||
        snapshot.state == earthscience::ScienceJobState::Unavailable ||
        snapshot.state == earthscience::ScienceJobState::Cancelled;
    if (failedReplacement)
    {
        view.kind = SciencePanelDisplayKind::FailureRetained;
        view.severity = SciencePanelSeverity::Warning;
        view.title = u8"本次失败，保留上一次显示 / Previous display retained";
        view.detail = std::string(u8"保留结果 / Artifact: ") +
            artifact->artifactId;
        return view;
    }
    if (artifact->raster.width <= 0 || artifact->raster.height <= 0 ||
        !artifact->raster.rgba)
    {
        view.kind = SciencePanelDisplayKind::AnalysisReadyWithoutRaster;
        view.severity = SciencePanelSeverity::Success;
        view.title = artifact->variableSeries.empty()
            ? u8"分析结果已就绪；没有地图栅格 / Analysis ready without raster"
            : u8"年度气候序列已就绪 / Annual climate series ready";
        view.detail = std::string(u8"结果 / Artifact: ") + artifact->artifactId;
        return view;
    }
    if (!layerVisible)
    {
        view.kind = SciencePanelDisplayKind::LoadedHidden;
        view.severity = SciencePanelSeverity::Warning;
        view.title = u8"数据已载入，图层已隐藏 / Loaded, layer hidden";
        view.detail = std::string(u8"结果 / Artifact: ") + artifact->artifactId;
        return view;
    }
    if (rendererUnavailable)
    {
        view.kind = SciencePanelDisplayKind::RendererUnavailable;
        view.severity = SciencePanelSeverity::Error;
        view.title = u8"数据已载入，但渲染器无法显示 / Renderer unavailable";
        view.detail = rendererMessage.empty()
            ? u8"地形栅格渲染能力不可用；未创建悬浮替代图层。"
            : rendererMessage;
        return view;
    }
    if (publisherHasArtifact)
    {
        view.kind = SciencePanelDisplayKind::LoadedVisible;
        view.severity = SciencePanelSeverity::Success;
        view.title = u8"数据已载入并显示 / Loaded and visible";
        view.detail = std::string(u8"显示结果 / Artifact: ") +
            artifact->artifactId;
        return view;
    }
    view.kind = SciencePanelDisplayKind::Publishing;
    view.severity = SciencePanelSeverity::Info;
    view.title = u8"数据已载入，正在交给地形渲染器 / Publishing to renderer";
    view.detail = std::string(u8"结果 / Artifact: ") + artifact->artifactId;
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

bool sciencePanelRequiresContextPreview(
    const earthscience::ScienceSourceDescriptor& source,
    SciencePanelMode mode)
{
    return source.id == "alphaearth-foundations" &&
        mode != SciencePanelMode::Preview;
}

const char* sciencePanelPrimaryActionLabel(SciencePanelMode mode)
{
    switch (mode)
    {
    case SciencePanelMode::Preview:
        return u8"加载所选年份的空间特征图";
    case SciencePanelMode::PointSeries:
        return u8"先显示定位伪彩，再分析历年变化";
    case SciencePanelMode::RegionalChange:
        return u8"先显示定位伪彩，再生成变化热图";
    }
    return u8"开始分析";
}

const char* sciencePanelPrimaryActionLabel(
    SciencePanelMode mode, const std::string& sourceId)
{
    if (sourceId == "sentinel-2-l2a" && mode == SciencePanelMode::Preview)
        return u8"加载近期 Sentinel-2 真彩影像";
    if (sourceId == "copernicus-dem-glo-30" &&
        mode == SciencePanelMode::Preview)
        return u8"加载当前视野的地表高程";
    if (isEra5AgroSource(sourceId) &&
        mode == SciencePanelMode::PointSeries)
        return u8"生成年度农业气候剖面";
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
    else if (isEra5AgroSource(artifact.query.sourceId))
        scope << u8"ERA5 · 年度农业气候剖面";
    else if (artifactMatchesMode(artifact, SciencePanelMode::Preview))
        scope << u8"AlphaEarth · 伪彩预览";
    else if (artifactMatchesMode(artifact, SciencePanelMode::PointSeries))
        scope << u8"当前位置的历年变化";
    else if (artifactMatchesMode(artifact, SciencePanelMode::RegionalChange))
        scope << u8"当前视野的年度变化";
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
    case ScienceHelpTopic::Era5Meaning:
        return u8"ERA5 农业气候数据是什么？";
    case ScienceHelpTopic::Era5Limits:
        return u8"ERA5 农业气候科学边界";
    case ScienceHelpTopic::Pca: return u8"PCA 与右侧结果";
    case ScienceHelpTopic::Clusters: return u8"无标签聚类是什么？";
    case ScienceHelpTopic::EmbeddingMetrics: return u8"变化方法有什么区别？";
    case ScienceHelpTopic::Hotspots: return u8"相对高值是什么？";
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
    case ScienceHelpTopic::Era5Meaning:
        return u8"ERA5 与 ERA5-Land 是把观测和数值模型结合起来的再分析数据。"
               u8"这里关闭额外的高程降尺度和陆地网格迁移，并从数据源输出网格"
               u8"的日值计算完整日历年的均值或总量。";
    case ScienceHelpTopic::Era5Limits:
        return u8"这些值代表约 0.1° 或 0.25° 的模型网格，不是气象站、"
               u8"农田传感器或地块级实测，不能直接解释为某一块田的微气候、"
               u8"产量、病虫害或灌溉需求。";
    case ScienceHelpTopic::Pca:
        return u8"PCA 只适用于区域年度变化。勾选后重新运行，右侧才会显示"
               u8"本次结果内的局部数学方向；它不代表具体地物。";
    case ScienceHelpTopic::Clusters:
        return u8"聚类只适用于区域年度变化。它产生可复现的"
               u8"无标签数学分组，不是土地覆盖类别。";
    case ScienceHelpTopic::EmbeddingMetrics:
        return u8"余弦距离 [0,2] 与方向夹角 [0,π] 越大，潜在方向变化越强；"
               u8"余弦相似度 [-1,1] 越大则越相似。L2 是向量位移，点积还受"
               u8"向量长度影响。它们都是 64 维表征空间的数学读数，不是米、"
               u8"温度、植被或土地覆盖类别；相关方法不能当成多份独立证据。";
    case ScienceHelpTopic::Hotspots:
        return u8"相对高值是在本次视野内按所选数学读数排序后的最高一部分，"
               u8"例如 P90 表示最高约 10%。选择相似度或点积时，高值不等于"
               u8"变化更强；它始终只是相对筛选，不是物理阈值，也不说明原因。";
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
    (void)query;
    // The provider/service owns the hard resource policy. Merely selecting more than one
    // year or a 256 grid is not itself a large request; the primary action click is already
    // explicit consent for ordinary bounded work.
    return cost.requiresConfirmation;
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
        if (!artifact.variableSeries.empty())
        {
            actualResolutionMeters =
                artifact.variableSeries.front().nativeResolutionMeters;
            for (const earthscience::ScienceSourceReference& reference :
                 artifact.sourceReferences)
            {
                if (!hasGeographicExtent(reference.actualCoverage)) continue;
                payloadBounds = reference.actualCoverage;
                hasPayloadBounds = true;
                break;
            }
        }
        else
        {
            payloadBounds = artifact.embedding.bounds;
            hasPayloadBounds = hasGeographicExtent(payloadBounds);
            actualResolutionMeters = artifact.embedding.actualResolutionMeters;
        }
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

std::vector<std::string> describeScienceAnalysisHighlights(
    const earthscience::ScienceArtifact& artifact)
{
    std::vector<std::string> lines;
    if (artifact.analysis.kind ==
            earthscience::ScienceAnalysisKind::PointSeries &&
        artifact.analysis.metrics)
    {
        const earthscience::ScienceMetric ordered[] = {
            earthscience::ScienceMetric::CosineDistance,
            earthscience::ScienceMetric::EuclideanDistance,
            earthscience::ScienceMetric::AngularDistance,
            earthscience::ScienceMetric::CosineSimilarity,
            earthscience::ScienceMetric::DotProduct,
        };
        for (earthscience::ScienceMetric metric : ordered)
        {
            const earthscience::ScienceMetricResult* largest = nullptr;
            for (const earthscience::ScienceMetricResult& result :
                 *artifact.analysis.metrics)
                if (result.metric == metric &&
                    (!largest || result.value > largest->value))
                    largest = &result;
            if (!largest) continue;
            std::ostringstream line;
            line << u8"最大相邻年度 / Largest adjacent · "
                 << sciencePanelMetricLabel(metric) << " · "
                 << largest->baselineYear << u8"→"
                 << largest->comparisonYear << ": "
                 << std::fixed << std::setprecision(5) << largest->value;
            if (!largest->unit.empty()) line << ' ' << largest->unit;
            lines.push_back(line.str());
        }
    }
    else if (artifact.analysis.kind ==
             earthscience::ScienceAnalysisKind::RegionalChange)
    {
        const earthscience::ScienceRegionalChangeSummary& summary =
            artifact.analysis.regionalChange;
        std::ostringstream metric;
        metric << sciencePanelMetricLabel(summary.metric) << u8" · 均值 / mean "
               << std::fixed << std::setprecision(5) << summary.mean
               << u8" · 中位 / median " << summary.median;
        lines.push_back(metric.str());
        std::ostringstream hotspot;
        const std::size_t hotspotCount = summary.hotspotIndices
            ? summary.hotspotIndices->size() : 0;
        hotspot << u8"相对高值 / High values · P"
                << static_cast<int>(summary.hotspotQuantile * 100.0 + 0.5)
                << u8" 阈值 " << std::fixed << std::setprecision(5)
                << summary.hotspotThreshold << u8" · " << hotspotCount
                << u8" 个网格";
        lines.push_back(hotspot.str());
    }
    return lines;
}

SciencePanelWorkflowDecision sciencePanelPendingAnalysisDecision(
    std::uint64_t contextPreviewJobId,
    const earthscience::ScienceJobSnapshot& snapshot)
{
    if (contextPreviewJobId == 0 || snapshot.jobId != contextPreviewJobId)
        return SciencePanelWorkflowDecision::Wait;
    if (snapshot.state == earthscience::ScienceJobState::Ready &&
        snapshot.query.outputKind ==
            earthscience::ScienceOutputKind::RasterLayer)
        return SciencePanelWorkflowDecision::SubmitAnalysis;
    if (snapshot.state == earthscience::ScienceJobState::Failed ||
        snapshot.state == earthscience::ScienceJobState::Cancelled ||
        snapshot.state == earthscience::ScienceJobState::Unavailable)
        return SciencePanelWorkflowDecision::Abort;
    return SciencePanelWorkflowDecision::Wait;
}

void ScienceEarthPanel::drawOperations(
    earthscience::ScienceQueryService* service,
    SciencePreviewLayer* previewLayer,
    LayerManager* layers,
    osgVerse::EarthManipulator* manipulator)
{
    if (!service || !previewLayer || !manipulator) return;
    const bool operationsExpanded = ImGui::CollapsingHeader(
        u8"ScienceEarth 研究工作台 / Research workspace",
        ImGuiTreeNodeFlags_DefaultOpen);

    const std::vector<earthscience::ScienceSourceDescriptor> sources =
        service->listSources();
    if (sources.empty())
    {
        if (operationsExpanded)
            ImGui::TextColored(earthui::design::kDanger,
                               u8"没有注册科学数据源 / No science source");
        return;
    }
    earthscience::ScienceJobSnapshot snapshot = service->snapshot();
    const bool configurationLocked = _hasPendingAnalysis ||
        snapshot.state == earthscience::ScienceJobState::Queued ||
        snapshot.state == earthscience::ScienceJobState::Fetching;
    const osg::Vec3d target = manipulator->computeViewPointLatLonHeight();
    const osg::Vec3d eye = manipulator->computeEyeLatLonHeight();
    const double latitude = osg::RadiansToDegrees(target[0]);
    const double longitude = osg::RadiansToDegrees(target[1]);
    const earthscience::ScienceSourceDescriptor* selectedSource =
        resolveSciencePanelSource(sources, _state.sourceId);
    if (!selectedSource) return;
    const double requestedSpanMeters = resolveAutomaticScienceSpanMeters(
        *selectedSource, eye[2]);
    if (operationsExpanded)
    {
        ImGui::SeparatorText(u8"1  定位区域 / Locate area");
        ImGui::TextWrapped(
            u8"屏幕中心 / Center: %.4f, %.4f · 自动分析范围 %.1f km",
            latitude, longitude, requestedSpanMeters / 1000.0);
        ImGui::SeparatorText(u8"2  选择数据源与时间 / Source and time");
        ImGui::TextWrapped(u8"数据源 / Source");
        if (configurationLocked) ImGui::BeginDisabled();
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
                    _hasPendingAnalysis = false;
                    _pendingContextJobId = 0;
                    _workflowFailed = false;
                    _workflowMessage.clear();
                    previewLayer->removeArtifact();
                    previewLayer->setVisible(false);
                    if (layers) layers->setEnabled("alphaearth", false);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (configurationLocked) ImGui::EndDisabled();
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
    if (_hasPendingAnalysis)
    {
        const SciencePanelWorkflowDecision decision =
            sciencePanelPendingAnalysisDecision(
                _pendingContextJobId, snapshot);
        if (decision == SciencePanelWorkflowDecision::SubmitAnalysis)
        {
            previewLayer->setVisible(true);
            if (layers) layers->setEnabled("alphaearth", true);
            const std::uint64_t submittedJobId =
                service->submit(_pendingAnalysisQuery);
            snapshot = service->snapshot();
            acknowledgeSciencePanelSubmission(
                submittedJobId, snapshot, &_state);
            _hasPendingAnalysis = false;
            _pendingContextJobId = 0;
            _workflowFailed = false;
            _workflowMessage =
                u8"定位伪彩已显示；第 2/2 步正在执行 64 维分析。";
        }
        else if (decision == SciencePanelWorkflowDecision::Abort)
        {
            _hasPendingAnalysis = false;
            _pendingContextJobId = 0;
            _workflowFailed = true;
            _workflowMessage = std::string(
                u8"定位伪彩失败，分析没有启动：") + snapshot.message;
        }
    }
    if (snapshot.state == earthscience::ScienceJobState::Ready &&
        snapshot.lastSuccessfulAnalysisArtifact &&
        snapshot.lastSuccessfulAnalysisArtifact->analysis.kind ==
            earthscience::ScienceAnalysisKind::RegionalChange &&
        snapshot.lastSuccessfulAnalysisArtifact->raster.width > 0 &&
        (!snapshot.displayArtifact ||
         snapshot.displayArtifact->artifactId !=
             snapshot.lastSuccessfulAnalysisArtifact->artifactId))
    {
        if (service->showArtifact(
                snapshot.lastSuccessfulAnalysisArtifact->artifactId))
        {
            previewLayer->setVisible(true);
            if (layers) layers->setEnabled("alphaearth", true);
            snapshot = service->snapshot();
        }
    }
    if (snapshot.state == earthscience::ScienceJobState::Ready &&
        queryMatchesMode(snapshot.query, activeMode))
    {
        _workflowFailed = false;
        _workflowMessage.clear();
    }
    if (!operationsExpanded) return;

    const SciencePreviewPublishStatus publishStatus =
        previewLayer->displayStatus();
    std::string visibleArtifactId;
    if (previewLayer->isVisible() &&
        publishStatus.state == SciencePreviewPublishState::Published &&
        snapshot.displayArtifact)
        visibleArtifactId = snapshot.displayArtifact->artifactId;
    const SciencePanelWorkflowStrip workflowStrip =
        describeScienceWorkflowStrip(
            source, activeMode, _state, _currentDraft, snapshot,
            visibleArtifactId);
    ImGui::SeparatorText(u8"当前状态 / Current state");
    ImGui::TextWrapped(u8"数据：%s · 时间：%s",
                       workflowStrip.source.c_str(),
                       workflowStrip.time.c_str());
    ImGui::TextWrapped(u8"范围：%s", workflowStrip.area.c_str());
    ImGui::TextWrapped(u8"方法：%s", workflowStrip.method.c_str());
    ImGui::TextWrapped(u8"阶段：%s · 可见结果：%s",
                       workflowStrip.stage.c_str(),
                       workflowStrip.visibleArtifactId.c_str());

    drawLabeledHelpButton("data_meaning", u8"? 这是什么数据 / About this data",
        source.id == "sentinel-2-l2a"
            ? ScienceHelpTopic::Sentinel2Meaning
            : source.id == "copernicus-dem-glo-30"
                ? ScienceHelpTopic::CopernicusDemMeaning
                : isEra5AgroSource(source.id)
                    ? ScienceHelpTopic::Era5Meaning
                    : ScienceHelpTopic::DataMeaning);
    if (ImGui::CollapsingHeader(u8"技术信息与来源 / Technical details"))
    {
        ImGui::TextWrapped(u8"原始分辨率：%.1f m",
                           source.nativeResolutionMeters);
        ImGui::TextWrapped(source.id == "sentinel-2-l2a"
                ? u8"显示通道：%d"
                : source.id == "copernicus-dem-glo-30"
                    ? u8"数值波段：%d"
                    : isEra5AgroSource(source.id)
                        ? u8"年度变量：%d" : u8"潜在分量：%d",
            source.componentCount);
        if (!source.dataNature.empty())
            ImGui::TextWrapped(u8"数据性质：%s", source.dataNature.c_str());
        if (!source.temporalResolution.empty())
            ImGui::TextWrapped(u8"时间尺度：%s",
                               source.temporalResolution.c_str());
        if (!source.spatialSupport.empty())
            ImGui::TextWrapped(u8"空间支持：%s",
                               source.spatialSupport.c_str());
        if (!source.qualityStatement.empty())
            ImGui::TextWrapped(u8"质量说明：%s",
                               source.qualityStatement.c_str());
        ImGui::TextWrapped(u8"提供方版本：%s",
                           source.providerVersion.c_str());
        ImGui::TextWrapped(u8"署名：%s", source.attribution.c_str());
    }
    const bool sourceUnavailable =
        source.health == earthscience::ScienceSourceHealth::Unavailable;
    if (source.health != earthscience::ScienceSourceHealth::Ready)
    {
        const std::string health = std::string(u8"数据源 / Source: ") +
            source.id + " · " +
            earthscience::scienceSourceHealthName(source.health);
        drawColoredWrapped(
            sourceUnavailable
                ? earthui::design::kDanger
                : earthui::design::kMeasure,
            health.c_str());
        if (!source.healthMessage.empty())
            ImGui::TextWrapped("%s", source.healthMessage.c_str());
        ImGui::TextWrapped("%s", sourceUnavailable
            ? u8"可重试：数据源恢复后重新运行；旧结果保持不变。"
            : u8"可重试：当前源可用但性能下降；可稍后重试，旧结果保持不变。");
    }
    ImGui::TextWrapped(u8"分析范围 / Analysis scope");
    const std::vector<SciencePanelMode> modes =
        sciencePanelModesForSource(source);
    if (modes.size() == 1)
        ImGui::TextWrapped("%s", sciencePanelModeLabel(modes.front(), source.id));
    else
    {
        if (configurationLocked) ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::BeginCombo(
                "##science_mode", sciencePanelModeLabel(activeMode, source.id)))
        {
            for (SciencePanelMode candidate : modes)
            {
                const bool selected = candidate == activeMode;
                if (ImGui::Selectable(
                        sciencePanelModeLabel(candidate, source.id), selected))
                {
                    if (candidate != activeMode)
                    {
                        if (_hasPendingAnalysis)
                        {
                            const earthscience::ScienceJobSnapshot active =
                                service->snapshot();
                            if (active.state ==
                                    earthscience::ScienceJobState::Queued ||
                                active.state ==
                                    earthscience::ScienceJobState::Fetching)
                                service->cancel(active.jobId);
                            _hasPendingAnalysis = false;
                            _pendingContextJobId = 0;
                        }
                        _workflowFailed = false;
                        _workflowMessage.clear();
                    }
                    _state.mode = candidate;
                    activeMode = candidate;
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (configurationLocked) ImGui::EndDisabled();
    }
    const SciencePanelModeCapabilities capabilities =
        sciencePanelModeCapabilities(source, activeMode);

    ImGui::TextWrapped(u8"时间 / Time");
    if (configurationLocked) ImGui::BeginDisabled();
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
    }
    else if (source.id != "copernicus-dem-glo-30" &&
             capabilities.showsSingleYear)
        drawDiscreteYear("preview", u8"年份 / Year", &_state.lastYear,
                         source.firstYear, source.lastYear);
    else if (capabilities.showsYearRange)
    {
        const bool firstChanged = drawDiscreteYear(
            "series_start", u8"从 / From", &_state.firstYear,
            source.firstYear, source.lastYear);
        const bool lastChanged = drawDiscreteYear(
            "series_end", u8"到 / To", &_state.lastYear,
            source.firstYear, source.lastYear);
        if (_state.firstYear > _state.lastYear)
            std::swap(_state.firstYear, _state.lastYear);
        if (isEra5AgroSource(source.id) &&
            _state.lastYear - _state.firstYear > 8)
        {
            if (firstChanged && !lastChanged)
                _state.lastYear = std::min(
                    source.lastYear, _state.firstYear + 8);
            else
                _state.firstYear = std::max(
                    source.firstYear, _state.lastYear - 8);
        }
        if (isEra5AgroSource(source.id))
            drawDisabledWrapped(
                u8"每次 1–9 个完整年份；年均值与年总量按变量定义计算。");
    }
    else if (capabilities.showsYearPair)
    {
        drawDiscreteYear("baseline", u8"之前 / Before",
                         &_state.baselineYear, source.firstYear, source.lastYear);
        drawDiscreteYear("comparison", u8"之后 / After",
                         &_state.comparisonYear, source.firstYear, source.lastYear);
    }
    if (configurationLocked) ImGui::EndDisabled();

    const bool embeddingAnalysis = source.id == "alphaearth-foundations" &&
        activeMode != SciencePanelMode::Preview;
    if (embeddingAnalysis)
    {
        ImGui::SeparatorText(u8"3  选择分析方法 / Choose method");
        drawHelpButton("embedding_metrics", ScienceHelpTopic::EmbeddingMetrics);
        if (configurationLocked) ImGui::BeginDisabled();
        if (activeMode == SciencePanelMode::PointSeries)
        {
            const auto choiceLabel = [](SciencePanelMetricChoice choice)
            {
                switch (choice)
                {
                case SciencePanelMetricChoice::DirectionChange:
                    return u8"方向变化（论文同类方法）";
                case SciencePanelMetricChoice::VectorDisplacement:
                    return u8"64 维向量位移（L2）";
                case SciencePanelMetricChoice::DirectionAndDisplacement:
                    return u8"方向变化 + 向量位移（推荐）";
                }
                return u8"方向变化";
            };
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo(
                    "##point_metric", choiceLabel(_state.pointMetricChoice)))
            {
                const SciencePanelMetricChoice choices[] = {
                    SciencePanelMetricChoice::DirectionAndDisplacement,
                    SciencePanelMetricChoice::DirectionChange,
                    SciencePanelMetricChoice::VectorDisplacement,
                };
                for (SciencePanelMetricChoice choice : choices)
                {
                    const bool selected = choice == _state.pointMetricChoice;
                    if (ImGui::Selectable(choiceLabel(choice), selected))
                        _state.pointMetricChoice = choice;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
        }
        else
        {
            ImGui::TextWrapped(u8"变化热图数值 / Change-map value");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo(
                    "##regional_metric",
                    sciencePanelMetricLabel(_state.regionalMetric)))
            {
                const earthscience::ScienceMetric metrics[] = {
                    earthscience::ScienceMetric::CosineDistance,
                    earthscience::ScienceMetric::AngularDistance,
                    earthscience::ScienceMetric::EuclideanDistance,
                    earthscience::ScienceMetric::CosineSimilarity,
                    earthscience::ScienceMetric::DotProduct,
                };
                for (earthscience::ScienceMetric metric : metrics)
                {
                    const bool selected = metric == _state.regionalMetric;
                    if (ImGui::Selectable(
                            sciencePanelMetricLabel(metric), selected))
                        _state.regionalMetric = metric;
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::TextWrapped(u8"相对高值 / Relative high values");
            drawHelpButton("hotspot_quantile", ScienceHelpTopic::Hotspots);
            ImGui::SameLine();
            const double quantiles[] = {0.90, 0.95, 0.99};
            for (std::size_t index = 0;
                 index < sizeof(quantiles) / sizeof(quantiles[0]); ++index)
            {
                if (index > 0) ImGui::SameLine();
                const std::string label = "P" + std::to_string(
                    static_cast<int>(quantiles[index] * 100.0 + 0.5));
                if (ImGui::RadioButton(
                        label.c_str(), _state.hotspotQuantile == quantiles[index]))
                    _state.hotspotQuantile = quantiles[index];
            }
        }
        if (configurationLocked) ImGui::EndDisabled();
    }

    if (capabilities.supportsGrid || capabilities.supportsPca ||
        capabilities.supportsClustering)
    {
        ImGui::SetNextItemOpen(_state.advancedOpen, ImGuiCond_Once);
        _state.advancedOpen = ImGui::CollapsingHeader(
            u8"更多工具 / More tools");
        if (_state.advancedOpen)
        {
            if (capabilities.supportsGrid)
            {
                ImGui::TextWrapped(u8"分析网格");
                if (configurationLocked) ImGui::BeginDisabled();
                if (ImGui::RadioButton(
                        "128 × 128##grid", _state.gridSize == 128))
                    _state.gridSize = 128;
                ImGui::SameLine();
                if (ImGui::RadioButton(
                        "256 × 256##grid", _state.gridSize == 256))
                    _state.gridSize = 256;
                if (configurationLocked) ImGui::EndDisabled();
            }
            if (capabilities.supportsPca)
            {
                if (configurationLocked) ImGui::BeginDisabled();
                drawWrappedCheckbox("##science_pca",
                    u8"附加 PCA 结构摘要", &_state.enablePca);
                if (configurationLocked) ImGui::EndDisabled();
                drawHelpButton("pca_setting", ScienceHelpTopic::Pca);
            }
            if (capabilities.supportsClustering)
            {
                if (configurationLocked) ImGui::BeginDisabled();
                drawWrappedCheckbox("##science_clusters",
                    u8"附加无标签聚类", &_state.enableClustering);
                if (configurationLocked) ImGui::EndDisabled();
                drawHelpButton(
                    "cluster_setting", ScienceHelpTopic::Clusters);
            }
            if (_state.enableClustering && capabilities.supportsClustering)
            {
                ImGui::TextWrapped(u8"聚类数量：%d（2–8）",
                                   _state.clusterCount);
                if (configurationLocked) ImGui::BeginDisabled();
                if (_state.clusterCount <= 2) ImGui::BeginDisabled();
                if (ImGui::SmallButton("-##clusters")) --_state.clusterCount;
                if (_state.clusterCount <= 2) ImGui::EndDisabled();
                ImGui::SameLine();
                if (_state.clusterCount >= 8) ImGui::BeginDisabled();
                if (ImGui::SmallButton("+##clusters")) ++_state.clusterCount;
                if (_state.clusterCount >= 8) ImGui::EndDisabled();
                _state.clusterCount = std::clamp(_state.clusterCount, 2, 8);
                if (configurationLocked) ImGui::EndDisabled();
            }
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
    ImGui::SeparatorText(u8"4  复核资源 / Review exact cost");
    if (!estimateFailed)
    {
        const ScienceCostPresentation estimate = describeScienceCost(cost);
        ImGui::TextWrapped(u8"源数据 ≤ %s · 驻留内存 ≤ %s",
                           estimate.sourceBytes.c_str(),
                           estimate.residentMemory.c_str());
        ImGui::TextWrapped(u8"结果单元：%s · 时长：%s",
                           estimate.resultCells.c_str(),
                           estimate.duration.c_str());
        if (_estimateVisible)
        {
            ImGui::TextColored(earthui::design::kMeasure,
                               u8"此任务超过数据源的普通资源上限");
            if (drawWrappedCheckbox("##confirm_science_cost",
                u8"我确认运行这项较大任务 / Confirm large request",
                &estimateConfirmed))
                _confirmedEstimateKey = estimateConfirmed
                    ? _displayedEstimateKey : std::string();
        }
    }
    if (estimateFailed)
    {
        const std::string failure =
            std::string(u8"无法估算 / Estimate failed: ") + estimateError;
        drawColoredWrapped(earthui::design::kDanger,
                           failure.c_str());
    }

    const SciencePanelPresentation presentation =
        describeScienceSnapshot(snapshot, activeMode);
    const bool currentSnapshotMatchesDraft =
        sciencePanelSnapshotMatchesDraft(snapshot, query);
    const bool currentDraftBusy =
        currentSnapshotMatchesDraft && presentation.busy;
    const bool currentDraftProblem = currentSnapshotMatchesDraft &&
        (presentation.kind == SciencePanelResultKind::NoCoverage ||
         presentation.kind == SciencePanelResultKind::Failed ||
         presentation.kind == SciencePanelResultKind::Cancelled ||
         presentation.kind == SciencePanelResultKind::Stale);
    const bool invalidPreview =
        activeMode == SciencePanelMode::Preview && !visualization;
    const bool blocked = sourceUnavailable || invalidPreview || estimateFailed ||
        currentDraftBusy || _hasPendingAnalysis ||
        (_estimateVisible && !estimateConfirmed);
    const std::shared_ptr<const earthscience::ScienceArtifact> currentArtifact =
        selectSciencePanelArtifact(snapshot, activeMode);
    const bool currentDraftAlreadyLoaded = currentArtifact &&
        describeScienceArtifactUi(*currentArtifact, query).matchesDraft;
    ImGui::SeparatorText(u8"5  开始或取消 / Start or cancel");
    if (_hasPendingAnalysis)
        drawColoredWrapped(earthui::design::kCyan,
            u8"第 1/2 步：正在加载对比年份的伪彩图；完成后会自动开始分析。"
            u8"相机和分析范围已固定。");
    else if (_workflowFailed)
        drawColoredWrapped(earthui::design::kDanger,
                           _workflowMessage.c_str());
    else if (currentDraftBusy)
        drawColoredWrapped(severityColor(presentation.severity),
            _workflowMessage.empty()
                ? u8"已提交，正在处理当前设置。"
                : _workflowMessage.c_str());
    else if (currentDraftAlreadyLoaded)
        drawColoredWrapped(earthui::design::kSuccess,
                           u8"当前设置已完成；结果显示在右侧科学结果面板。");
    else if (currentDraftProblem)
    {
        drawColoredWrapped(severityColor(presentation.severity),
                           presentation.title.c_str());
        if (!presentation.sourceText.empty())
            ImGui::TextWrapped("%s", presentation.sourceText.c_str());
        if (!presentation.retryText.empty())
            ImGui::TextWrapped("%s", presentation.retryText.c_str());
        if (!presentation.detail.empty())
            ImGui::TextWrapped("%s", presentation.detail.c_str());
    }
    else
    {
        const std::string selection = sciencePanelSelectionSummary(
            activeMode, _state, source.id);
        drawColoredWrapped(earthui::design::kCyan,
                           selection.c_str());
    }
    if (sourceUnavailable)
        drawColoredWrapped(earthui::design::kDanger,
                           u8"暂时无法开始：数据源不可用。");
    else if (invalidPreview)
        drawColoredWrapped(earthui::design::kDanger,
                           u8"暂时无法开始：缺少匹配的显示方式。");
    else if (_estimateVisible && !estimateConfirmed)
        drawColoredWrapped(earthui::design::kMeasure,
                           u8"勾选上方确认项后可以运行。");
    if (blocked) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,
                          earthui::design::kOxblood);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          earthui::design::kVermilion);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                          earthui::design::kCyan);
    if (ImGui::Button(
            sciencePanelPrimaryActionLabel(activeMode, source.id),
                      ImVec2(-1.0f, 0.0f)))
    {
        query.analysis.confirmedLargeRequest =
            _estimateVisible && estimateConfirmed;
        std::uint64_t submittedJobId = 0;
        if (activeMode == SciencePanelMode::Preview)
        {
            previewLayer->setVisible(true);
            if (layers) layers->setEnabled("alphaearth", true);
            submittedJobId = service->submit(query);
        }
        else if (!sciencePanelRequiresContextPreview(source, activeMode))
            submittedJobId = service->submit(query);
        else if (visualization)
        {
            const earthscience::GeoTemporalQuery contextQuery =
                makeScienceAnalysisContextPreviewQuery(
                    source, *visualization, query, requestedSpanMeters);
            const bool contextAlreadyVisible = snapshot.displayArtifact &&
                artifactMatchesMode(
                    *snapshot.displayArtifact, SciencePanelMode::Preview) &&
                describeScienceArtifactUi(
                    *snapshot.displayArtifact, contextQuery).matchesDraft;
            previewLayer->setVisible(true);
            if (layers) layers->setEnabled("alphaearth", true);
            if (contextAlreadyVisible)
                submittedJobId = service->submit(query);
            else
            {
                _pendingAnalysisQuery = query;
                _hasPendingAnalysis = true;
                _workflowFailed = false;
                _workflowMessage =
                    u8"第 1/2 步：正在加载对比年份的定位伪彩。";
                _pendingContextJobId = service->submit(contextQuery);
                _state.resultExpanded = true;
            }
        }
        if (submittedJobId != 0)
        {
            if (activeMode != SciencePanelMode::Preview)
            {
                _workflowFailed = false;
                _workflowMessage = isEra5AgroSource(source.id)
                    ? u8"已提交；正在读取日值并生成年度气候序列。"
                    : u8"定位伪彩已显示；第 2/2 步正在执行 64 维分析。";
            }
            const earthscience::ScienceJobSnapshot submittedSnapshot =
                service->snapshot();
            acknowledgeSciencePanelSubmission(
                submittedJobId, submittedSnapshot, &_state);
        }
    }
    ImGui::PopStyleColor(3);
    if (blocked) ImGui::EndDisabled();

    const earthscience::ScienceJobSnapshot snapshotAfterAction =
        service->snapshot();
    const SciencePanelPresentation presentationAfterAction =
        describeScienceSnapshot(snapshotAfterAction, activeMode);
    if (presentationAfterAction.busy || _hasPendingAnalysis)
    {
        drawColoredWrapped(severityColor(presentationAfterAction.severity),
                           presentationAfterAction.stageText.c_str());
        ImGui::TextWrapped("%s",
                           presentationAfterAction.progressText.c_str());
        if (!presentationAfterAction.detail.empty())
            ImGui::TextWrapped("%s",
                               presentationAfterAction.detail.c_str());
        if (ImGui::Button(u8"取消当前请求 / Cancel request",
                          ImVec2(-1.0f, 0.0f)))
            service->cancel(snapshotAfterAction.jobId);
    }

    ImGui::SeparatorText(u8"6  查看地图、结果与证据 / View results");
    const SciencePreviewPublishStatus statusAfterAction =
        previewLayer->displayStatus();
    const bool rendererUnavailable =
        statusAfterAction.state ==
            SciencePreviewPublishState::RendererUnavailable;
    const SciencePanelDisplayPresentation display =
        describeScienceDisplayState(
            snapshotAfterAction, activeMode, previewLayer->isVisible(),
            previewLayer->hasArtifact(), rendererUnavailable,
            statusAfterAction.message);
    drawColoredWrapped(severityColor(display.severity),
                       display.title.c_str());
    if (!display.detail.empty())
        ImGui::TextWrapped("%s", display.detail.c_str());
    if (ImGui::Button(u8"查看结果与证据 / View result and evidence",
                      ImVec2(-1.0f, 0.0f)))
        _state.resultExpanded = true;
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
    ImGui::SetNextWindowPos(ImVec2(
        io.DisplaySize.x - layout.resultRight - layout.resultWidth,
        layout.resultTop), ImGuiCond_Always);
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
    ImGui::PushStyleColor(ImGuiCol_Text,
                          earthui::design::kCyan);
    ImGui::TextUnformatted(u8"洞察透镜 / Insight Lens");
    ImGui::PopStyleColor();
    ImGui::TextDisabled(u8"ScienceEarth · 结果、图表与证据");
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
    const SciencePreviewPublishStatus publishStatus =
        previewLayer->displayStatus();
    const SciencePanelDisplayPresentation display =
        describeScienceDisplayState(
            snapshot, activeMode, previewLayer->isVisible(),
            previewLayer->hasArtifact(),
            publishStatus.state ==
                SciencePreviewPublishState::RendererUnavailable,
            publishStatus.message);
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
        if (!presentation.sourceText.empty())
            ImGui::TextWrapped("%s", presentation.sourceText.c_str());
        if (!presentation.retryText.empty())
            ImGui::TextWrapped("%s", presentation.retryText.c_str());
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
        drawColoredWrapped(earthui::design::kCyan,
                           artifactUi.scopeLabel.c_str());
        drawColoredWrapped(severityColor(display.severity),
                           display.title.c_str());
        if (!display.detail.empty())
            ImGui::TextWrapped("%s", display.detail.c_str());
        if (!artifactUi.pendingSettingsLabel.empty())
            drawColoredWrapped(earthui::design::kMeasure,
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
                    : isEra5AgroSource(artifact->query.sourceId)
                        ? ScienceHelpTopic::Era5Meaning
                        : ScienceHelpTopic::PreviewColors,
                       false);

        drawLabeledHelpButton(
            "scientific_limits", u8"? 科学解释边界 / Limits",
            artifact->query.sourceId == "sentinel-2-l2a"
                ? ScienceHelpTopic::Sentinel2Limits
                : artifact->query.sourceId == "copernicus-dem-glo-30"
                    ? ScienceHelpTopic::CopernicusDemLimits
                    : isEra5AgroSource(artifact->query.sourceId)
                        ? ScienceHelpTopic::Era5Limits
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
