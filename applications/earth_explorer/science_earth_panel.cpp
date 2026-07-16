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

std::shared_ptr<const earthscience::ScienceArtifact> retainedArtifact(
    const earthscience::ScienceJobSnapshot& snapshot)
{
    if (snapshot.lastSuccessfulAnalysisArtifact)
        return snapshot.lastSuccessfulAnalysisArtifact;
    if (snapshot.lastSuccessfulPreviewArtifact)
        return snapshot.lastSuccessfulPreviewArtifact;
    return snapshot.lastSuccessfulArtifact;
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
        << query.analysis.gridSize << '|' << query.analysis.enablePca << '|'
        << query.analysis.enableClustering << '|' << query.analysis.clusterCount;
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
    drawColoredWrapped(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
        u8"颜色表示潜在嵌入关系；不是物理量，也不是自然色。");
}

void drawTechnicalDetails(const earthscience::ScienceArtifact& artifact)
{
    if (ImGui::CollapsingHeader(u8"PCA 与聚类 / PCA & clusters"))
    {
        if (artifact.analysis.pca.componentCount > 0)
        {
            ImGui::TextWrapped(
                u8"PCA 轴只是此结果内的局部数学方向，"
                u8"不代表植被、温度、城市或其他物理变量。");
            if (artifact.analysis.pca.explainedVarianceRatios)
            {
                for (std::size_t index = 0;
                     index < artifact.analysis.pca.explainedVarianceRatios->size();
                     ++index)
                    ImGui::TextWrapped("PC%zu: %.1f%%", index + 1,
                        (*artifact.analysis.pca.explainedVarianceRatios)[index] * 100.0);
            }
        }
        else
            ImGui::TextWrapped(u8"本次未启用 PCA。");
        if (artifact.analysis.clusters.clusterCount > 0)
            ImGui::TextWrapped(
                u8"%d 个簇为可复现的无标签数学分组，不是土地覆盖类别。",
                artifact.analysis.clusters.clusterCount);
        else
            ImGui::TextWrapped(u8"本次未启用聚类。");
    }

    if (ImGui::CollapsingHeader(u8"方法与来源 / Method & provenance"))
    {
        ImGui::TextWrapped(u8"处理版本 / Processing: %s",
                           artifact.processingVersion.c_str());
        for (const earthscience::ScienceSourceReference& source :
             artifact.sourceReferences)
        {
            ImGui::TextWrapped(u8"数据集 / Dataset: %s", source.datasetId.c_str());
            ImGui::TextWrapped(u8"提供方版本 / Provider: %s",
                               source.providerVersion.c_str());
            ImGui::TextWrapped(u8"署名 / Attribution: %s",
                               source.attribution.c_str());
        }
    }

    if (ImGui::CollapsingHeader(u8"64 维与导出 / Raw dimensions & export"))
    {
        ImGui::TextWrapped(
            u8"64 个分量是潜在表征维度，"
            u8"单个 A01–A64 没有获验证的物理名称。");
        ImGui::TextWrapped(
            u8"证据导出使用 CSV/JSON，并保留查询、算法、覆盖、"
            u8"处理步骤与限制；"
            u8"原始维度仅在明确选择后导出。");
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

    const std::shared_ptr<const earthscience::ScienceArtifact> retained =
        retainedArtifact(snapshot);
    if (retained && (view.kind == SciencePanelResultKind::NoCoverage ||
                     view.kind == SciencePanelResultKind::Failed ||
                     view.kind == SciencePanelResultKind::Cancelled ||
                     view.kind == SciencePanelResultKind::Stale))
        view.retentionReason = std::string(
            u8"上一次成功结果仍保留 / Prior successful result retained: ") +
            retained->artifactId;
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
        if (snapshot.displayArtifact &&
            snapshot.displayArtifact->query.outputKind ==
                earthscience::ScienceOutputKind::RasterLayer)
            return snapshot.displayArtifact;
        if (snapshot.lastSuccessfulPreviewArtifact)
            return snapshot.lastSuccessfulPreviewArtifact;
    }
    else if (snapshot.lastSuccessfulAnalysisArtifact)
        return snapshot.lastSuccessfulAnalysisArtifact;
    return retainedArtifact(snapshot);
}

void ScienceEarthPanel::drawOperations(
    earthscience::ScienceQueryService* service,
    SciencePreviewLayer* previewLayer,
    LayerManager* layers,
    osgVerse::EarthManipulator* manipulator)
{
    if (!service || !previewLayer || !manipulator) return;
    if (!ImGui::CollapsingHeader(u8"ScienceEarth 分析 / Analysis",
                                  ImGuiTreeNodeFlags_DefaultOpen)) return;

    const std::vector<earthscience::ScienceSourceDescriptor> sources =
        service->listSources();
    if (sources.empty())
    {
        ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.35f, 1.0f),
                           u8"没有注册科学数据源 / No science source");
        return;
    }
    const earthscience::ScienceSourceDescriptor& source = sources.front();
    const earthscience::ScienceVisualizationDescriptor* visualization =
        source.visualizations.empty() ? nullptr : &source.visualizations.front();
    const earthscience::ScienceJobSnapshot snapshot = service->snapshot();
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

    drawColoredWrapped(ImVec4(0.35f, 0.85f, 1.0f, 1.0f),
                       source.name.c_str());
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

    if (_state.mode == SciencePanelMode::Preview)
        drawDiscreteYear("preview", u8"年份 / Year", &_state.lastYear,
                         source.firstYear, source.lastYear);
    else if (_state.mode == SciencePanelMode::PointSeries)
    {
        drawDiscreteYear("series_start", u8"起始年份 / First year",
                         &_state.firstYear, source.firstYear, source.lastYear);
        drawDiscreteYear("series_end", u8"结束年份 / Last year",
                         &_state.lastYear, source.firstYear, source.lastYear);
        if (_state.firstYear > _state.lastYear)
            std::swap(_state.firstYear, _state.lastYear);
    }
    else
    {
        drawDiscreteYear("baseline", u8"基准年份 / Baseline",
                         &_state.baselineYear, source.firstYear, source.lastYear);
        drawDiscreteYear("comparison", u8"对比年份 / Comparison",
                         &_state.comparisonYear, source.firstYear, source.lastYear);
    }

    ImGui::SetNextItemOpen(_state.advancedOpen, ImGuiCond_Once);
    _state.advancedOpen = ImGui::CollapsingHeader(
        u8"高级设置 / Advanced settings");
    if (_state.advancedOpen)
    {
        if (_state.mode == SciencePanelMode::RegionalChange)
        {
            ImGui::TextWrapped(u8"分析网格 / Analysis grid");
            if (ImGui::RadioButton("128 × 128##grid", _state.gridSize == 128))
                _state.gridSize = 128;
            ImGui::SameLine();
            if (ImGui::RadioButton("256 × 256##grid", _state.gridSize == 256))
                _state.gridSize = 256;
        }
        ImGui::TextWrapped(
            u8"指标 / Metrics: cosine similarity + cosine distance");
        drawWrappedCheckbox("##science_pca", u8"局部 PCA / Local PCA",
                            &_state.enablePca);
        drawWrappedCheckbox("##science_clusters",
            u8"无标签球面聚类 / Unlabeled spherical clustering",
            &_state.enableClustering);
        if (_state.enableClustering)
        {
            ImGui::TextWrapped(u8"聚类数量 / Cluster count: %d（范围 2–8）",
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
        ImGui::TextWrapped(
            u8"导出 / Export: 结果就绪后可在右侧展开证据导出说明。");
        ImGui::SeparatorText(u8"数据源 / Source details");
        ImGui::TextWrapped(
            u8"%d–%d · %.0f m 原始分辨率 · %d 个分量 · provider v%s",
            source.firstYear, source.lastYear,
            source.nativeResolutionMeters, source.componentCount,
            source.providerVersion.c_str());
        ImGui::TextWrapped("%s", source.attribution.c_str());
    }

    earthscience::GeoTemporalQuery query;
    if (_state.mode == SciencePanelMode::Preview && visualization)
        query = makeSciencePointQuery(source, *visualization, latitude, longitude,
                                      _state.lastYear, requestedSpanMeters);
    else if (_state.mode == SciencePanelMode::PointSeries)
        query = makeSciencePointSeriesQuery(source, latitude, longitude,
                                            _state.firstYear, _state.lastYear);
    else if (_state.mode == SciencePanelMode::RegionalChange)
    {
        earthscience::ScienceAnalysisOptions options;
        options.gridSize = _state.gridSize;
        options.enablePca = _state.enablePca;
        options.enableClustering = _state.enableClustering;
        options.clusterCount = _state.clusterCount;
        query = makeScienceRegionalAnalysisQuery(
            source, latitude, longitude, requestedSpanMeters,
            _state.baselineYear, _state.comparisonYear, options);
    }

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
        ? std::string() : queryEstimateKey(query, cost);
    if (estimateKey != _displayedEstimateKey)
    {
        _displayedEstimateKey = estimateKey;
        _estimateConfirmed = false;
    }
    _displayedCost = cost;
    _estimateVisible = !estimateFailed && cost.requiresConfirmation;
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
        drawWrappedCheckbox("##confirm_science_cost",
            u8"我确认按以上估算提交 / Confirm this exact estimate",
            &_estimateConfirmed);
    }
    if (estimateFailed)
    {
        const std::string failure =
            std::string(u8"无法估算 / Estimate failed: ") + estimateError;
        drawColoredWrapped(ImVec4(1.0f, 0.42f, 0.35f, 1.0f),
                           failure.c_str());
    }

    const SciencePanelPresentation presentation =
        describeScienceSnapshot(snapshot);
    const bool invalidPreview =
        _state.mode == SciencePanelMode::Preview && !visualization;
    const bool blocked = sourceUnavailable || invalidPreview || estimateFailed ||
        presentation.busy || (_estimateVisible && !_estimateConfirmed);
    if (blocked) ImGui::BeginDisabled();
    if (ImGui::Button(u8"开始分析 / Start analysis", ImVec2(-1.0f, 0.0f)))
    {
        query.analysis.confirmedLargeRequest = _estimateConfirmed;
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
    if (!ImGui::Begin("ScienceEarth Results / 科学结果", nullptr,
                      ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::End();
        return;
    }

    if (!_state.resultExpanded)
    {
        if (ImGui::Button(">", ImVec2(-1.0f, 0.0f)))
            _state.resultExpanded = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(u8"展开 ScienceEarth 结果");
        ImGui::End();
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
        describeScienceSnapshot(snapshot);
    drawColoredWrapped(severityColor(presentation.severity),
                       presentation.title.c_str());
    ImGui::TextWrapped("%s", presentation.stageText.c_str());
    if (!presentation.detail.empty())
        ImGui::TextWrapped("%s", presentation.detail.c_str());
    if (!presentation.progressText.empty())
        ImGui::TextWrapped("%s", presentation.progressText.c_str());
    if (!presentation.retentionReason.empty())
        drawColoredWrapped(ImVec4(1.0f, 0.78f, 0.25f, 1.0f),
                           presentation.retentionReason.c_str());

    const std::shared_ptr<const earthscience::ScienceArtifact> artifact =
        selectSciencePanelArtifact(snapshot, _state.mode);
    if (artifact)
    {
        ImGui::SeparatorText(u8"摘要 / Summary");
        if (artifact->analysis.interpretation &&
            !artifact->analysis.interpretation->empty())
            ImGui::TextWrapped("%s",
                artifact->analysis.interpretation->front().c_str());
        else if (artifact->analysis.kind != earthscience::ScienceAnalysisKind::None)
            ImGui::TextWrapped(
                u8"结果描述 64 维嵌入空间中的可复现关系，"
                u8"不解释物理原因。");
        else
            ImGui::TextWrapped(
                u8"伪彩预览用于定位潜在嵌入差异，不是自然影像。");

        drawPrimaryMetrics(*artifact);
        ImGui::SeparatorText(u8"图表或图例 / Chart or legend");
        if (!drawMetricSeries(*artifact)) drawEmbeddingLegend(*artifact);

        ImGui::SeparatorText(u8"科学限制 / Scientific limitation");
        if (artifact->analysis.limitations &&
            !artifact->analysis.limitations->empty())
            ImGui::TextWrapped("%s", artifact->analysis.limitations->front().c_str());
        else
            ImGui::TextWrapped(
                u8"这些结果不能单独证明建设、砍伐、洪水、升温"
                u8"或其他物理原因。");

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
    else
        ImGui::TextWrapped(
            u8"在左侧选择位置、研究类型和离散年份，然后开始分析。"
            u8"相机不会移动。");

    ImGui::PopTextWrapPos();
    ImGui::End();
}
