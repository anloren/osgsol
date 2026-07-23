#include "science_workbench_presenter.h"

#include "../science_plugin_runtime.h"
#include "../science_workbench_methods.h"
#include "../earth_control_layout.h"
#include "rml_science_chart.h"
#include "science_chart_model.h"
#include "science_report_window.h"
#include "map_context_capture.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <picojson.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>
#include <vector>

namespace
{
constexpr std::size_t MAX_QUEUED_ACTIONS = 64;

struct SourceView
{
    std::string id;
    std::string name;
    std::string category;
    std::string spatialSupport;
    std::string health;
    std::string healthMessage;
    int firstYear = 0;
    int lastYear = 0;
    bool supportsBounds = false;
    bool supportsTimeSeries = false;
    bool supportsAnalysis = false;
    bool supportsRaster = false;
};

struct SeriesView
{
    std::string id;
    std::string name;
    std::string unit;
    std::string aggregation;
    double nativeResolutionMeters = 0.0;
    std::vector<int> years;
    std::vector<double> values;
    std::vector<unsigned char> validity;
};

struct ArtifactView
{
    bool available = false;
    std::string artifactId;
    std::string sourceId;
    std::string processingVersion;
    std::string createdAt;
    std::vector<SeriesView> series;
};

struct CitationView
{
    std::string sourceId;
    std::string providerVersion;
    std::string datasetId;
    std::string originalUrl;
    std::string attribution;
    std::string publicationTime;
    std::vector<std::string> processingSteps;
};

struct EvidenceView
{
    std::string availability;
    bool hasSourceResolution = false;
    double sourceResolutionMeters = 0.0;
    std::string spatialSupport;
    std::string sourceLicense;
    std::string sourceDocumentationUrl;
    std::string sourceAttribution;
    std::string sourceQualityStatement;
    std::string limitationsAvailability;
    std::vector<std::string> aggregationMethods;
    std::vector<std::string> limitations;
    std::vector<std::string> warnings;
    std::vector<CitationView> citations;
    bool hasExecutionTiming = false;
    double executionTimingSeconds = 0.0;
    bool hasExportCapabilities = false;
    bool artifactExport = false;
    bool rasterOutput = false;
    bool tableOutput = false;
};

struct SnapshotView
{
    std::uint64_t revision = 0;
    std::string phase;
    std::string sourceId;
    std::string methodId;
    std::string errorCode;
    std::string errorMessage;
    std::string activeArtifactId;
    std::string selectedMetricId;
    int selectedYear = 0;
    int firstYear = 0;
    int lastYear = 0;
    bool locked = false;
    bool hasActual = false;
    ScienceOverlayGeometry requested;
    ScienceOverlayGeometry actual;
    double requestedLatitude = 0.0;
    double requestedLongitude = 0.0;
    double sourceBytes = 0.0;
    double resultCells = 0.0;
    double estimatedSeconds = 0.0;
    bool durationDeterminate = false;
    bool progressDeterminate = false;
    double completedUnits = 0.0;
    double totalUnits = 0.0;
    std::string progressStage;
    std::vector<SourceView> sources;
    ArtifactView artifact;
    EvidenceView evidence;
};

const picojson::value* field(const picojson::object& object, const char* key)
{
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &found->second;
}

std::string stringField(const picojson::object& object, const char* key)
{
    const picojson::value* value = field(object, key);
    return value && value->is<std::string>() ? value->get<std::string>() : "";
}

double numberField(const picojson::object& object, const char* key,
                   double fallback = 0.0)
{
    const picojson::value* value = field(object, key);
    return value && value->is<double>() &&
        std::isfinite(value->get<double>()) ? value->get<double>() : fallback;
}

bool boolField(const picojson::object& object, const char* key,
               bool fallback = false)
{
    const picojson::value* value = field(object, key);
    return value && value->is<bool>() ? value->get<bool>() : fallback;
}

std::vector<std::string> stringArrayField(
    const picojson::object& object, const char* key)
{
    std::vector<std::string> output;
    const picojson::value* value = field(object, key);
    if (!value || !value->is<picojson::array>()) return output;
    for (const picojson::value& item : value->get<picojson::array>())
        if (item.is<std::string>()) output.push_back(item.get<std::string>());
    return output;
}

bool geometryField(const picojson::object& object, const char* key,
                   ScienceOverlayGeometry& output)
{
    const picojson::value* value = field(object, key);
    if (!value || !value->is<picojson::object>()) return false;
    const picojson::object& geometry = value->get<picojson::object>();
    const std::string kind = stringField(geometry, "kind");
    if (kind == "point")
    {
        const picojson::value* pointValue = field(geometry, "point");
        if (!pointValue || !pointValue->is<picojson::object>()) return false;
        const picojson::object& point = pointValue->get<picojson::object>();
        output.kind = ScienceOverlayGeometryKind::Point;
        output.point.latitude = numberField(point, "latitude");
        output.point.longitude = numberField(point, "longitude");
        return std::isfinite(output.point.latitude) &&
            std::isfinite(output.point.longitude);
    }
    if (kind != "bounds") return false;
    const picojson::value* boundsValue = field(geometry, "bounds");
    if (!boundsValue || !boundsValue->is<picojson::object>()) return false;
    const picojson::object& bounds = boundsValue->get<picojson::object>();
    output.kind = ScienceOverlayGeometryKind::Bounds;
    output.bounds.west = numberField(bounds, "west");
    output.bounds.south = numberField(bounds, "south");
    output.bounds.east = numberField(bounds, "east");
    output.bounds.north = numberField(bounds, "north");
    return std::isfinite(output.bounds.west) &&
        std::isfinite(output.bounds.south) &&
        std::isfinite(output.bounds.east) &&
        std::isfinite(output.bounds.north);
}

bool parseSnapshot(const std::string& json, SnapshotView& output,
                   std::string& error)
{
    picojson::value parsed;
    error = picojson::parse(parsed, json);
    if (!error.empty() || !parsed.is<picojson::object>())
    {
        error = "科学工作台返回了无效状态";
        return false;
    }
    const picojson::object& root = parsed.get<picojson::object>();
    if (stringField(root, "schema") != "science-workbench-ui-v1")
    {
        error = "科学工作台状态版本不兼容";
        return false;
    }
    const double revision = numberField(root, "revision", -1.0);
    if (revision < 0.0 || revision >
        static_cast<double>(std::numeric_limits<std::uint64_t>::max()))
    {
        error = "科学工作台状态序号无效";
        return false;
    }
    output = SnapshotView();
    output.revision = static_cast<std::uint64_t>(revision);
    output.phase = stringField(root, "phase");
    output.methodId = stringField(root, "selectedMethodId");
    output.errorCode = stringField(root, "errorCode");
    output.errorMessage = stringField(root, "errorMessage");
    output.activeArtifactId = stringField(root, "activeArtifactId");
    output.selectedMetricId = stringField(root, "selectedMetricId");
    output.selectedYear = static_cast<int>(numberField(root, "selectedYear"));

    if (const picojson::value* draftValue = field(root, "draft");
        draftValue && draftValue->is<picojson::object>())
    {
        const picojson::object& draft = draftValue->get<picojson::object>();
        output.sourceId = stringField(draft, "sourceId");
        if (const picojson::value* yearsValue = field(draft, "years");
            yearsValue && yearsValue->is<picojson::array>())
        {
            const picojson::array& years = yearsValue->get<picojson::array>();
            if (!years.empty() && years.front().is<double>() &&
                years.back().is<double>())
            {
                output.firstYear = static_cast<int>(years.front().get<double>());
                output.lastYear = static_cast<int>(years.back().get<double>());
            }
        }
    }

    if (const picojson::value* targetValue = field(root, "target");
        targetValue && targetValue->is<picojson::object>())
    {
        const picojson::object& target = targetValue->get<picojson::object>();
        output.locked = boolField(target, "locked");
        output.hasActual = boolField(target, "hasActualCoverage");
        geometryField(target, "requested", output.requested);
        if (output.hasActual)
            geometryField(target, "actualCoverage", output.actual);
        if (const picojson::value* centerValue = field(target, "requestedCenter");
            centerValue && centerValue->is<picojson::object>())
        {
            const picojson::object& center = centerValue->get<picojson::object>();
            output.requestedLatitude = numberField(center, "latitude");
            output.requestedLongitude = numberField(center, "longitude");
        }
    }

    if (const picojson::value* costValue = field(root, "cost");
        costValue && costValue->is<picojson::object>())
    {
        const picojson::object& cost = costValue->get<picojson::object>();
        output.sourceBytes = numberField(cost, "sourceBytesUpperBound");
        output.resultCells = numberField(cost, "resultCells");
        output.estimatedSeconds = numberField(cost, "estimatedDurationSeconds");
        output.durationDeterminate = boolField(cost, "durationDeterminate");
    }

    if (const picojson::value* progressValue = field(root, "progress");
        progressValue && progressValue->is<picojson::object>())
    {
        const picojson::object& progress = progressValue->get<picojson::object>();
        output.progressStage = stringField(progress, "stage");
        output.completedUnits = numberField(progress, "completedUnits");
        output.totalUnits = numberField(progress, "totalUnits");
        output.progressDeterminate = boolField(progress, "determinate") &&
            output.totalUnits > 0.0;
    }

    if (const picojson::value* sourcesValue = field(root, "sources");
        sourcesValue && sourcesValue->is<picojson::array>())
    {
        for (const picojson::value& sourceValue :
             sourcesValue->get<picojson::array>())
        {
            if (!sourceValue.is<picojson::object>()) continue;
            const picojson::object& source = sourceValue.get<picojson::object>();
            SourceView item;
            item.id = stringField(source, "id");
            item.name = stringField(source, "name");
            item.category = stringField(source, "category");
            item.spatialSupport = stringField(source, "spatialSupport");
            item.health = stringField(source, "health");
            item.healthMessage = stringField(source, "healthMessage");
            item.firstYear = static_cast<int>(numberField(source, "firstYear"));
            item.lastYear = static_cast<int>(numberField(source, "lastYear"));
            if (const picojson::value* capabilitiesValue =
                    field(source, "capabilities");
                capabilitiesValue && capabilitiesValue->is<picojson::object>())
            {
                const picojson::object& capabilities =
                    capabilitiesValue->get<picojson::object>();
                item.supportsBounds = boolField(capabilities, "bounds");
                item.supportsTimeSeries =
                    boolField(capabilities, "timeSeries");
                item.supportsAnalysis = boolField(capabilities, "analysis");
                item.supportsRaster = boolField(capabilities, "raster");
            }
            if (!item.id.empty()) output.sources.push_back(std::move(item));
        }
    }

    if (const picojson::value* artifactValue = field(root, "activeArtifact");
        artifactValue && artifactValue->is<picojson::object>())
    {
        const picojson::object& artifact =
            artifactValue->get<picojson::object>();
        output.artifact.available = true;
        output.artifact.artifactId = stringField(artifact, "artifactId");
        output.artifact.sourceId = stringField(artifact, "sourceId");
        output.artifact.processingVersion = stringField(
            artifact, "processingVersion");
        output.artifact.createdAt = stringField(artifact, "createdAt");
        if (const picojson::value* seriesValue = field(artifact, "series");
            seriesValue && seriesValue->is<picojson::array>())
        {
            for (const picojson::value& itemValue :
                 seriesValue->get<picojson::array>())
            {
                if (!itemValue.is<picojson::object>()) continue;
                const picojson::object& item =
                    itemValue.get<picojson::object>();
                SeriesView series;
                series.id = stringField(item, "id");
                series.name = stringField(item, "name");
                series.unit = stringField(item, "unit");
                series.aggregation = stringField(item, "aggregation");
                series.nativeResolutionMeters = numberField(
                    item, "nativeResolutionMeters");
                if (const picojson::value* pointsValue = field(item, "points");
                    pointsValue && pointsValue->is<picojson::array>())
                {
                    for (const picojson::value& pointValue :
                         pointsValue->get<picojson::array>())
                    {
                        if (!pointValue.is<picojson::object>()) continue;
                        const picojson::object& point =
                            pointValue.get<picojson::object>();
                        const bool valid = boolField(point, "valid");
                        series.years.push_back(static_cast<int>(
                            numberField(point, "year")));
                        series.values.push_back(valid
                            ? numberField(point, "value") : 0.0);
                        series.validity.push_back(valid ? 1 : 0);
                    }
                }
                if (!series.id.empty())
                    output.artifact.series.push_back(std::move(series));
            }
        }
    }

    if (const picojson::value* evidenceValue = field(root, "reportEvidence");
        evidenceValue && evidenceValue->is<picojson::object>())
    {
        const picojson::object& evidence =
            evidenceValue->get<picojson::object>();
        output.evidence.availability = stringField(evidence, "availability");
        if (const picojson::value* resolution =
                field(evidence, "sourceNativeResolutionMeters");
            resolution && resolution->is<double>() &&
            std::isfinite(resolution->get<double>()))
        {
            output.evidence.hasSourceResolution = true;
            output.evidence.sourceResolutionMeters = resolution->get<double>();
        }
        output.evidence.spatialSupport = stringField(evidence, "spatialSupport");
        output.evidence.sourceLicense = stringField(evidence, "sourceLicense");
        output.evidence.sourceDocumentationUrl = stringField(
            evidence, "sourceDocumentationUrl");
        output.evidence.sourceAttribution = stringField(
            evidence, "sourceAttribution");
        output.evidence.sourceQualityStatement = stringField(
            evidence, "sourceQualityStatement");
        output.evidence.limitationsAvailability = stringField(
            evidence, "limitationsAvailability");
        output.evidence.aggregationMethods = stringArrayField(
            evidence, "aggregationMethods");
        output.evidence.limitations = stringArrayField(evidence, "limitations");
        output.evidence.warnings = stringArrayField(evidence, "warnings");
        if (const picojson::value* timing =
                field(evidence, "executionTimingSeconds");
            timing && timing->is<double>() &&
            std::isfinite(timing->get<double>()))
        {
            output.evidence.hasExecutionTiming = true;
            output.evidence.executionTimingSeconds = timing->get<double>();
        }
        if (const picojson::value* exportValue =
                field(evidence, "exportCapabilities");
            exportValue && exportValue->is<picojson::object>())
        {
            const picojson::object& exports =
                exportValue->get<picojson::object>();
            output.evidence.hasExportCapabilities = true;
            output.evidence.artifactExport = boolField(
                exports, "artifactExport");
            output.evidence.rasterOutput = boolField(exports, "rasterOutput");
            output.evidence.tableOutput = boolField(exports, "tableOutput");
        }
        if (const picojson::value* citationsValue = field(evidence, "citations");
            citationsValue && citationsValue->is<picojson::array>())
        {
            for (const picojson::value& citationValue :
                 citationsValue->get<picojson::array>())
            {
                if (!citationValue.is<picojson::object>()) continue;
                const picojson::object& item =
                    citationValue.get<picojson::object>();
                CitationView citation;
                citation.sourceId = stringField(item, "sourceId");
                citation.providerVersion = stringField(item, "providerVersion");
                citation.datasetId = stringField(item, "datasetId");
                citation.originalUrl = stringField(item, "originalUrl");
                citation.attribution = stringField(item, "attribution");
                citation.publicationTime = stringField(item, "publicationTime");
                citation.processingSteps = stringArrayField(
                    item, "processingSteps");
                output.evidence.citations.push_back(std::move(citation));
            }
        }
    }
    error.clear();
    return true;
}

std::string escapedRml(const std::string& input)
{
    std::string output;
    output.reserve(input.size());
    for (char value : input)
    {
        switch (value)
        {
        case '&': output += "&amp;"; break;
        case '<': output += "&lt;"; break;
        case '>': output += "&gt;"; break;
        case '"': output += "&quot;"; break;
        case '\'': output += "&#39;"; break;
        default: output.push_back(value); break;
        }
    }
    return output;
}

std::string phaseLabel(const std::string& phase)
{
    if (phase == "draft") return "先锁定分析位置";
    if (phase == "target-locked") return "位置已锁定，请检查年份";
    if (phase == "ready-to-run") return "设置完成，可以开始";
    if (phase == "queued") return "已加入分析队列";
    if (phase == "fetching") return "正在读取科学数据";
    if (phase == "analyzing") return "正在计算分析结果";
    if (phase == "ready") return "结果已生成";
    if (phase == "failed") return "本次分析失败";
    if (phase == "cancelled") return "分析已取消";
    return "准备研究";
}

std::string progressLabel(const std::string& stage)
{
    if (stage == "queued") return "等待开始";
    if (stage == "locating") return "定位数据范围";
    if (stage == "reading") return "读取源数据";
    if (stage == "decoding") return "解码数据";
    if (stage == "validating") return "检查科学元数据";
    if (stage == "aligning") return "对齐空间与时间";
    if (stage == "analyzing") return "执行分析";
    if (stage == "materializing") return "生成可视结果";
    if (stage == "cancelling") return "正在取消";
    return "分析进行中";
}

std::string errorLabel(const SnapshotView& view)
{
    if (!view.errorMessage.empty()) return view.errorMessage;
    if (view.errorCode == "workbench-target-not-locked")
        return "请先锁定地图中心或当前视野。";
    if (view.errorCode == "workbench-source-required")
        return "请选择要分析的科学数据。";
    if (view.errorCode == "workbench-time-required" ||
        view.errorCode == "workbench-year-range-invalid")
        return "年份范围无效，请检查开始和结束年份。";
    if (view.errorCode == "workbench-run-failed")
        return "科学数据处理失败；上一次成功结果仍保留。";
    return view.errorCode;
}

std::string formatBytes(double value)
{
    if (!(value > 0.0)) return "尚未估算";
    const char* units[] = {"B", "KiB", "MiB", "GiB"};
    int unit = 0;
    while (value >= 1024.0 && unit < 3) { value /= 1024.0; ++unit; }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(unit == 0 ? 0 : 1)
           << value << ' ' << units[unit];
    return stream.str();
}

std::string jsonString(const std::string& value)
{
    return picojson::value(value).serialize();
}

std::string joined(const std::vector<std::string>& values,
                   const std::string& separator)
{
    std::string output;
    for (const std::string& value : values)
    {
        if (value.empty()) continue;
        if (!output.empty()) output += separator;
        output += value;
    }
    return output;
}

std::string listMarkup(const std::vector<std::string>& values,
                       const std::string& unavailable)
{
    if (values.empty()) return "<p class=\"muted\">" +
        escapedRml(unavailable) + "</p>";
    std::string output = "<ul class=\"evidence-list\">";
    for (const std::string& value : values)
        output += "<li>" + escapedRml(value) + "</li>";
    return output + "</ul>";
}

std::string geometryDescription(const ScienceOverlayGeometry& geometry)
{
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(4);
    if (geometry.kind == ScienceOverlayGeometryKind::Point)
    {
        stream << "点位 " << geometry.point.latitude << "°, "
               << geometry.point.longitude << "°";
    }
    else
    {
        stream << "边界 W " << geometry.bounds.west << "° · S "
               << geometry.bounds.south << "° · E " << geometry.bounds.east
               << "° · N " << geometry.bounds.north << "°";
    }
    return stream.str();
}

std::string formatResolution(double meters)
{
    std::ostringstream stream;
    if (meters >= 1000.0)
        stream << std::fixed << std::setprecision(
            std::fmod(meters, 1000.0) == 0.0 ? 0 : 1)
               << meters / 1000.0 << " km";
    else
        stream << std::fixed << std::setprecision(meters >= 10.0 ? 0 : 1)
               << meters << " m";
    return stream.str();
}

bool overlayRectangle(const std::vector<osg::Vec2f>& points,
                      float& left, float& top, float& width, float& height)
{
    if (points.empty()) return false;
    float right = points.front().x(), bottom = points.front().y();
    left = right;
    top = bottom;
    for (const osg::Vec2f& point : points)
    {
        left = std::min(left, point.x());
        right = std::max(right, point.x());
        top = std::min(top, point.y());
        bottom = std::max(bottom, point.y());
    }
    width = std::max(0.012f, right - left);
    height = std::max(0.012f, bottom - top);
    left = std::clamp(left - 0.006f, 0.0f, 1.0f - width);
    top = std::clamp(top - 0.006f, 0.0f, 1.0f - height);
    return true;
}

bool running(const std::string& phase)
{
    return phase == "queued" || phase == "fetching" ||
           phase == "analyzing";
}

class ProgrammaticControlSync
{
public:
    explicit ProgrammaticControlSync(int& depth) : _depth(depth)
    {
        ++_depth;
    }

    ~ProgrammaticControlSync()
    {
        --_depth;
    }

    ProgrammaticControlSync(const ProgrammaticControlSync&) = delete;
    ProgrammaticControlSync& operator=(const ProgrammaticControlSync&) = delete;

private:
    int& _depth;
};
}

class ScienceWorkbenchPresenter::Impl
{
public:
    Impl(SciencePluginRuntime& runtimeValue, std::string documentPathValue,
         MapContextCapture* contextCaptureValue)
        : runtime(runtimeValue), documentPath(std::move(documentPathValue)),
          contextCapture(contextCaptureValue) {}

    Rml::Element* element(const char* id) const
    {
        return document ? document->GetElementById(id) : nullptr;
    }

    Rml::Element* reportElement(const char* id) const
    {
        return reportDocument ? reportDocument->GetElementById(id) : nullptr;
    }

    void setReportText(const char* id, const std::string& value)
    {
        if (Rml::Element* item = reportElement(id))
            item->SetInnerRML(escapedRml(value));
    }

    void setReportMarkup(const char* id, const std::string& value)
    {
        if (Rml::Element* item = reportElement(id)) item->SetInnerRML(value);
    }

    void setReportDisplay(const char* id, bool visible,
                          const char* display = "block")
    {
        if (Rml::Element* item = reportElement(id))
            item->SetProperty("display", visible ? display : "none");
    }

    void setText(const char* id, const std::string& value)
    {
        if (Rml::Element* item = element(id)) item->SetInnerRML(escapedRml(value));
    }

    void setDisplay(const char* id, bool visible, const char* display = "block")
    {
        if (Rml::Element* item = element(id))
            item->SetProperty("display", visible ? display : "none");
    }

    void setDisabled(const char* id, bool disabled)
    {
        if (Rml::Element* item = element(id))
        {
            if (Rml::ElementFormControl* control =
                rmlui_dynamic_cast<Rml::ElementFormControl*>(item))
                control->SetDisabled(disabled);
            else if (disabled)
                item->SetAttribute("disabled", "");
            else
                item->RemoveAttribute("disabled");
            item->SetClass("disabled", disabled);
        }
    }

    std::string value(const char* id) const
    {
        Rml::Element* item = element(id);
        if (!item) return "";
        if (Rml::ElementFormControl* control =
            rmlui_dynamic_cast<Rml::ElementFormControl*>(item))
            return control->GetValue();
        return "";
    }

    std::string reportValue(const char* id) const
    {
        Rml::Element* item = reportElement(id);
        if (!item) return "";
        if (Rml::ElementFormControl* control =
            rmlui_dynamic_cast<Rml::ElementFormControl*>(item))
            return control->GetValue();
        return "";
    }

    void setControlValue(Rml::Element* item, const std::string& newValue)
    {
        Rml::ElementFormControl* control =
            item ? rmlui_dynamic_cast<Rml::ElementFormControl*>(item) : nullptr;
        if (!control || control->GetValue() == newValue) return;
        ProgrammaticControlSync sync(controlSyncDepth);
        control->SetValue(newValue);
    }

    void setControlValue(const char* id, const std::string& newValue)
    {
        setControlValue(element(id), newValue);
    }

    void updateSourceDescription(
        const SourceView* selected,
        const ScienceWorkbenchMethod* selectedMethod)
    {
        if (!selected) return;
        setText("analysis-name", selected->name);
        std::string summary = selectedMethod
            ? selectedMethod->summary
            : "该数据源当前没有可执行的分析方法。";
        if (selected->health != "ready" && !selected->healthMessage.empty())
            summary += " · " + selected->healthMessage;
        setText("analysis-summary", summary);
        setText("method-summary", summary);
        setText("provider-years",
            std::to_string(selected->firstYear) + "–" +
            std::to_string(selected->lastYear));
    }

    void enqueue(std::string name, std::string json)
    {
        std::lock_guard<std::mutex> guard(queueMutex);
        if (queue.size() >= MAX_QUEUED_ACTIONS) queue.pop_front();
        queue.push_back({std::move(name), std::move(json)});
    }

    void attach(ScienceWorkbenchPresenter& owner, const char* id,
                const char* event)
    {
        if (Rml::Element* item = element(id)) item->AddEventListener(event, &owner);
    }

    void attachReport(ScienceWorkbenchPresenter& owner, const char* id,
                      const char* event)
    {
        if (Rml::Element* item = reportElement(id))
            item->AddEventListener(event, &owner);
    }

    void updateReportShell(const SnapshotView& view)
    {
        if (!reportDocument) return;
        if (!view.activeArtifactId.empty() &&
            reportModel.openReady(view.activeArtifactId) && contextCapture)
        {
            ScienceTargetOverlayInput captureTarget;
            {
                std::lock_guard<std::mutex> guard(targetMutex);
                captureTarget = target;
            }
            contextCapture->request(view.activeArtifactId, captureTarget);
        }
        const ScienceReportWindowState* active = reportModel.active();
        setReportDisplay("science-report", active && active->visible);
        setReportDisplay("report-shelf", !reportModel.shelf().empty(), "flex");
        for (std::size_t index = 0; index < 3; ++index)
        {
            const std::string id = "shelf-" + std::to_string(index);
            const bool present = index < reportModel.shelf().size();
            setReportDisplay(id.c_str(), present, "inline-block");
            if (present)
            {
                setReportText(id.c_str(), "科学结果 " +
                    reportModel.shelf()[index].substr(0, 8));
                if (Rml::Element* item = reportElement(id.c_str()))
                    item->SetAttribute("data-artifact",
                        reportModel.shelf()[index]);
            }
        }
        if (!active || !active->visible) return;
        updateContextSnapshot(active->artifactId);
        if (Rml::Element* window = reportElement("science-report"))
        {
            window->SetProperty("left", std::to_string(active->position.x()) + "px");
            window->SetProperty("top", std::to_string(active->position.y()) + "px");
            window->SetProperty("width", std::to_string(active->size.x()) + "px");
            window->SetProperty("height", std::to_string(active->size.y()) + "px");
        }
        const SourceView* selected = nullptr;
        for (const SourceView& source : view.sources)
            if (source.id == view.sourceId) { selected = &source; break; }
        setReportText("report-title", selected ? selected->name : "科学分析结果");
        setReportText("report-artifact-id", active->artifactId);
        updateReportChart(view, active->artifactId);
        updateReportEvidence(view, selected);
        const char* sections[] = {
            "overview", "trends", "spatial-range", "methods-evidence"};
        for (const char* section : sections)
        {
            const bool selectedSection = active->activeSection == section;
            setReportDisplay(section, selectedSection);
            const std::string tabId = std::string("tab-") + section;
            if (Rml::Element* tab = reportElement(tabId.c_str()))
                tab->SetClass("selected", selectedSection);
        }
    }

    void updateReportEvidence(const SnapshotView& view,
                              const SourceView* selectedSource)
    {
        const std::string sourceName = selectedSource
            ? selectedSource->name : view.artifact.sourceId;
        setReportText("overview-time-range", view.firstYear > 0
            ? std::to_string(view.firstYear) + "–" +
                std::to_string(view.lastYear) : "未提供");
        setReportText("overview-spatial-support",
            view.evidence.spatialSupport.empty()
                ? "未提供" : view.evidence.spatialSupport);

        std::size_t totalPoints = 0, validPoints = 0;
        for (const SeriesView& series : view.artifact.series)
        {
            totalPoints += series.validity.size();
            validPoints += static_cast<std::size_t>(std::count(
                series.validity.begin(), series.validity.end(),
                static_cast<unsigned char>(1)));
        }
        setReportText("overview-completeness", totalPoints > 0
            ? std::to_string(validPoints) + "/" +
                std::to_string(totalPoints) + " 个年度值"
            : "未提供");
        std::ostringstream summary;
        summary << (sourceName.empty() ? "科学数据" : sourceName) << " · "
                << view.artifact.series.size() << " 项年度指标。"
                << "请求范围与数据实际覆盖分别记录，可在其他标签中复核。";
        setReportText("overview-summary", summary.str());

        const bool pointRequest =
            view.requested.kind == ScienceOverlayGeometryKind::Point;
        setReportText("requested-spatial-fact",
            geometryDescription(view.requested) +
            (pointRequest
                ? "。这是一个采样目标，不表示周边区域平均。"
                : "。这是本次固定的区域请求。"));
        setReportText("actual-spatial-fact", view.hasActual
            ? geometryDescription(view.actual) +
                "。它来自结果中的 actualCoverage，不等同于请求范围。"
            : "数据源没有提供可验证的实际覆盖范围。");
        setReportText("source-resolution",
            view.evidence.hasSourceResolution
                ? "数据源声明的原生分辨率：" +
                    formatResolution(view.evidence.sourceResolutionMeters)
                : "数据源没有提供可验证的原生分辨率。");

        std::vector<std::string> methodFacts;
        if (!view.evidence.aggregationMethods.empty())
            methodFacts.push_back("聚合方法：" + joined(
                view.evidence.aggregationMethods, "；"));
        methodFacts.push_back(
            "缺测规则：缺测年份保留为空缺，趋势线不跨越空缺连接。 ");
        for (const CitationView& citation : view.evidence.citations)
            if (!citation.processingSteps.empty())
                methodFacts.push_back("处理步骤：" + joined(
                    citation.processingSteps, " → "));
        setReportMarkup("method-facts", listMarkup(
            methodFacts, "未提供可验证的处理方法。"));
        setReportText("execution-timing",
            view.evidence.hasExecutionTiming
                ? formatScienceChartValue(
                    view.evidence.executionTimingSeconds, "秒")
                : "未提供（不会用估算值替代）");

        std::string citations;
        for (const CitationView& citation : view.evidence.citations)
        {
            citations += "<div class=\"citation\"><p><strong>" +
                escapedRml(citation.datasetId.empty()
                    ? citation.sourceId : citation.datasetId) + "</strong>";
            if (!citation.providerVersion.empty())
                citations += " · " + escapedRml(citation.providerVersion);
            citations += "</p>";
            if (!citation.attribution.empty())
                citations += "<p>" + escapedRml(citation.attribution) + "</p>";
            if (!citation.publicationTime.empty())
                citations += "<p>发布时间：" +
                    escapedRml(citation.publicationTime) + "</p>";
            if (!citation.originalUrl.empty())
                citations += "<p class=\"source-url\">" +
                    escapedRml(citation.originalUrl) + "</p>";
            citations += "</div>";
        }
        if (citations.empty() && !view.evidence.sourceDocumentationUrl.empty())
            citations = "<p class=\"source-url\">" +
                escapedRml(view.evidence.sourceDocumentationUrl) + "</p>";
        if (citations.empty())
            citations = "<p class=\"muted\">结果未提供来源引用。</p>";
        if (!view.evidence.sourceLicense.empty())
            citations += "<p>许可：" +
                escapedRml(view.evidence.sourceLicense) + "</p>";
        if (!view.evidence.sourceQualityStatement.empty())
            citations += "<p>质量说明：" +
                escapedRml(view.evidence.sourceQualityStatement) + "</p>";
        setReportMarkup("source-links", citations);

        setReportMarkup("limitations", listMarkup(
            view.evidence.limitations,
            view.evidence.limitationsAvailability == "provided"
                ? "数据源声明没有额外限制。"
                : "结果未提供限制说明；这不等于没有限制。"));
        setReportMarkup("report-warnings", listMarkup(
            view.evidence.warnings, "本次结果没有返回警告。"));
        setReportText("report-processing-version",
            view.artifact.processingVersion.empty()
                ? "未提供" : view.artifact.processingVersion);
        setReportText("report-created-at", view.artifact.createdAt.empty()
            ? "未提供" : view.artifact.createdAt);
        if (view.evidence.hasExportCapabilities)
        {
            std::vector<std::string> exports;
            if (view.evidence.artifactExport) exports.push_back("结果文件");
            if (view.evidence.rasterOutput) exports.push_back("栅格");
            if (view.evidence.tableOutput) exports.push_back("表格");
            setReportText("export-capabilities", exports.empty()
                ? "数据源明确未声明可用导出格式" : joined(exports, "、"));
        }
        else setReportText("export-capabilities", "未提供");
    }

    void updateReportChart(const SnapshotView& view,
                           const std::string& artifactId)
    {
        if (!view.artifact.available ||
            view.artifact.artifactId != artifactId)
        {
            setReportText("chart-statistics",
                "这个结果没有可绘制的年度指标序列。");
            return;
        }
        Rml::ElementFormControlSelect* select =
            rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(
                reportElement("report-metric-select"));
        if (!select) return;
        ProgrammaticControlSync sync(controlSyncDepth);

        std::string fingerprint = view.artifact.artifactId;
        for (const SeriesView& series : view.artifact.series)
            fingerprint += "\n" + series.id + "\n" + series.name +
                "\n" + series.unit;
        if (fingerprint != renderedArtifactFingerprint)
        {
            select->RemoveAll();
            for (const SeriesView& series : view.artifact.series)
            {
                const std::string label = normalizeScienceMetricTitle(
                    series.id, series.name) +
                    (series.unit.empty() ? "" : " · " + series.unit);
                select->Add(escapedRml(label), series.id);
            }
            renderedArtifactFingerprint = fingerprint;
        }

        std::string metricId = activeMetricId(view, artifactId);
        if (metricId.empty() && !view.artifact.series.empty())
            metricId = view.artifact.series.front().id;
        if (metricId.empty())
        {
            setReportText("chart-statistics",
                "这个结果没有可绘制的年度指标序列。");
            return;
        }
        if (select->GetValue() != metricId) select->SetValue(metricId);
        reportModel.selectMetric(artifactId, metricId);

        const SeriesView* selected = nullptr;
        for (const SeriesView& series : view.artifact.series)
            if (series.id == metricId) { selected = &series; break; }
        if (!selected) return;
        const ScienceChartModel chart = makeScienceChartModel(
            selected->id, selected->name, selected->unit, selected->years,
            selected->values, selected->validity);
        int selectedYear = activeYear(view, artifactId);
        if (RmlScienceChart* chartElement =
            rmlui_dynamic_cast<RmlScienceChart*>(reportElement("science-chart")))
            chartElement->setModel(chart, selectedYear);

        if (!chart.points.empty())
        {
            setReportText("chart-domain",
                std::to_string(chart.points.front().year) + "–" +
                std::to_string(chart.points.back().year) + " · " +
                chart.title);
        }
        std::ostringstream statistics;
        statistics << "均值 "
                   << formatScienceChartValue(chart.summary.mean, chart.unit)
                   << " · 首末变化 "
                   << formatScienceChartValue(
                          chart.summary.firstToLastChange, chart.unit)
                   << " · 线性趋势 "
                   << formatScienceChartValue(
                          chart.summary.linearTrendPerYear,
                          chart.unit.empty() ? "/年" : chart.unit + "/年")
                   << " · 有效年份 " << chart.summary.validCount << "/"
                   << chart.points.size();
        setReportText("chart-statistics", statistics.str());
        updateSelectedYearReadout(chart, selectedYear);
    }

    std::string activeMetricId(const SnapshotView& view,
                               const std::string& artifactId) const
    {
        const ScienceReportWindowState* report = reportModel.find(artifactId);
        if (report && !report->selectedMetric.empty())
            return report->selectedMetric;
        return view.selectedMetricId;
    }

    int activeYear(const SnapshotView& view,
                   const std::string& artifactId) const
    {
        const ScienceReportWindowState* report = reportModel.find(artifactId);
        if (report && report->selectedYear > 0) return report->selectedYear;
        return view.selectedYear;
    }

    void updateSelectedYearReadout(const ScienceChartModel& chart, int year)
    {
        if (year <= 0)
        {
            setReportText("chart-selected-year",
                "点击图中年份可固定对比时点");
            return;
        }
        for (const ScienceChartPoint& point : chart.points)
        {
            if (point.year != year) continue;
            setReportText("chart-selected-year", std::to_string(year) +
                " · " + (point.missing ? "数据缺测" :
                    formatScienceChartValue(point.value, chart.unit)));
            return;
        }
        setReportText("chart-selected-year",
            std::to_string(year) + " · 不在当前序列中");
    }

    void updateContextSnapshot(const std::string& artifactId)
    {
        if (!contextCapture || artifactId.empty() || !reportDocument) return;
        ScienceContextSnapshot snapshot;
        if (!contextCapture->copy(artifactId, snapshot) ||
            snapshot.imagePath.empty())
            return;
        if (Rml::Element* image = reportElement("context-image"))
        {
            const std::string current = image->GetAttribute<Rml::String>(
                "src", "");
            if (current != snapshot.imagePath)
                image->SetAttribute("src", snapshot.imagePath);
        }
        setReportDisplay("context-placeholder", false);
        setReportDisplay("context-image", true);
        auto applyOverlay = [this](const char* id,
                                   const std::vector<osg::Vec2f>& points)
        {
            float left = 0.0f, top = 0.0f, width = 0.0f, height = 0.0f;
            if (!overlayRectangle(points, left, top, width, height))
            {
                setReportDisplay(id, false);
                return;
            }
            Rml::Element* overlay = reportElement(id);
            if (!overlay) return;
            setReportDisplay(id, true);
            overlay->SetProperty("left", std::to_string(left * 100.0f) + "%");
            overlay->SetProperty("top", std::to_string(top * 100.0f) + "%");
            overlay->SetProperty("width", std::to_string(width * 100.0f) + "%");
            overlay->SetProperty("height", std::to_string(height * 100.0f) + "%");
        };
        applyOverlay("context-requested-overlay", snapshot.requestedOverlay);
        applyOverlay("context-actual-overlay", snapshot.actualOverlay);
        setReportText("context-caption",
            "固定分析范围 · 画面 " + std::to_string(snapshot.width) + " × " +
            std::to_string(snapshot.height) + " · 捕获帧 " +
            std::to_string(snapshot.capturedFrame));
    }

    void updateLiveTargetOverlay()
    {
        if (!contextCapture || !reportDocument) return;
        ScienceLiveTargetProjection projection;
        if (!contextCapture->copyLiveProjection(projection))
        {
            setReportDisplay("live-target-requested", false);
            setReportDisplay("live-target-actual", false);
            setReportDisplay("live-target-label", false);
            return;
        }
        auto apply = [this](const char* id,
                            const std::vector<osg::Vec2f>& points)
        {
            float left = 0.0f, top = 0.0f, width = 0.0f, height = 0.0f;
            if (!overlayRectangle(points, left, top, width, height))
            {
                setReportDisplay(id, false);
                return;
            }
            Rml::Element* element = reportElement(id);
            if (!element) return;
            setReportDisplay(id, true);
            element->SetProperty("left", std::to_string(left * 100.0f) + "%");
            element->SetProperty("top", std::to_string(top * 100.0f) + "%");
            element->SetProperty("width", std::to_string(width * 100.0f) + "%");
            element->SetProperty("height", std::to_string(height * 100.0f) + "%");
        };
        apply("live-target-requested", projection.requestedOverlay);
        apply("live-target-actual", projection.actualOverlay);
        if (Rml::Element* label = reportElement("live-target-label"))
        {
            setReportDisplay("live-target-label", true);
            label->SetProperty("left",
                std::to_string(projection.labelAnchor.x() * 100.0f) + "%");
            label->SetProperty("top",
                std::to_string(projection.labelAnchor.y() * 100.0f) + "%");
            setReportText("live-target-label", projection.label);
        }
    }

    void updateViewportLayout(Rml::Context& context)
    {
        const Rml::Vector2i dimensions = context.GetDimensions();
        if (dimensions == lastContextDimensions) return;
        lastContextDimensions = dimensions;
        const earthui::EarthUiShellLayout shell =
            earthui::computeEarthUiShellLayout(
                static_cast<float>(dimensions.x),
                static_cast<float>(dimensions.y), true);
        const earthui::ScienceWorkbenchLayout workbench =
            earthui::computeScienceWorkbenchLayout(
                static_cast<float>(dimensions.x),
                static_cast<float>(dimensions.y));
        if (document)
        {
            document->SetProperty("left", std::to_string(shell.drawerX) + "px");
            document->SetProperty("top", std::to_string(shell.drawerY) + "px");
            document->SetProperty("width",
                std::to_string(workbench.composerWidth) + "px");
            document->SetProperty("height",
                std::to_string(shell.drawerHeight) + "px");
        }
        reportModel.setViewport(
            static_cast<float>(dimensions.x),
            static_cast<float>(dimensions.y),
            shell.drawerX + workbench.composerWidth + shell.outerGap,
            shell.topBarHeight, shell.statusHeight + shell.contextHeight +
                shell.commandHeight + shell.outerGap * 3.0f);
        if (Rml::Element* report = reportElement("science-report"))
        {
            const float leftInset = shell.drawerX +
                workbench.composerWidth + shell.outerGap;
            const float bottomInset = shell.statusHeight +
                shell.contextHeight + shell.commandHeight +
                shell.outerGap * 3.0f;
            const float availableWidth = std::max(
                1.0f, static_cast<float>(dimensions.x) - leftInset - 24.0f);
            const float availableHeight = std::max(
                1.0f, static_cast<float>(dimensions.y) -
                shell.topBarHeight - bottomInset - 24.0f);
            report->SetProperty("min-width",
                std::to_string(std::min(720.0f, availableWidth)) + "px");
            report->SetProperty("min-height",
                std::to_string(std::min(520.0f, availableHeight)) + "px");
        }
    }

    SciencePluginRuntime& runtime;
    std::string documentPath;
    Rml::ElementDocument* document = nullptr;
    Rml::ElementDocument* reportDocument = nullptr;
    std::uint64_t renderedRevision = std::numeric_limits<std::uint64_t>::max();
    std::string renderedSourceFingerprint;
    std::string renderedMethodFingerprint;
    std::string renderedArtifactFingerprint;
    bool costOpen = false;
    bool helpOpen = false;
    int controlSyncDepth = 0;
    std::string pendingSourceId;
    std::string pendingMethodId;
    std::string pendingYearRange;
    SnapshotView state;
    mutable std::mutex targetMutex;
    ScienceTargetOverlayInput target;
    mutable std::mutex queueMutex;
    std::deque<ScienceWorkbenchQueuedAction> queue;
    mutable std::mutex errorMutex;
    std::string actionError;
    ScienceReportWindowModel reportModel;
    MapContextCapture* contextCapture = nullptr;
    std::atomic<bool> desiredVisible{false};
    bool renderedVisible = false;
    Rml::Vector2i lastContextDimensions{-1, -1};
};

ScienceWorkbenchPresenter::ScienceWorkbenchPresenter(
    SciencePluginRuntime& runtime, std::string documentPath,
    MapContextCapture* contextCapture)
    : _impl(new Impl(runtime, std::move(documentPath), contextCapture))
{
}

ScienceWorkbenchPresenter::~ScienceWorkbenchPresenter()
{
    delete _impl;
}

bool ScienceWorkbenchPresenter::onRmlContextReady(
    Rml::Context& context, std::string& error)
{
    if (!_impl->runtime.supportsWorkbenchUi())
    {
        error = "ScienceEarth plugin does not expose workbench ABI v4";
        return false;
    }
    registerRmlScienceChartElement();
    _impl->document = context.LoadDocument(_impl->documentPath);
    if (!_impl->document)
    {
        error = "Could not load ScienceEarth workbench RML: " +
            _impl->documentPath;
        return false;
    }
    const char* clickIds[] = {
        "lock-map-center", "lock-current-view", "update-target",
        "focus-target", "run-action", "cancel-action", "cost-toggle",
        "science-help"};
    for (const char* id : clickIds) _impl->attach(*this, id, "click");
    const char* changeIds[] = {
        "source-select", "method-select", "first-year", "last-year"};
    for (const char* id : changeIds) _impl->attach(*this, id, "change");
    if (_impl->desiredVisible.load())
        _impl->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    const std::string reportPath = _impl->documentPath.substr(
        0, _impl->documentPath.find_last_of("/\\") + 1) + "report.rml";
    _impl->reportDocument = context.LoadDocument(reportPath);
    if (!_impl->reportDocument)
    {
        error = "Could not load ScienceEarth report RML: " + reportPath;
        return false;
    }
    const char* reportClickIds[] = {
        "report-minimize", "report-close", "report-overflow",
        "report-delete", "report-delete-cancel", "tab-overview",
        "tab-trends", "tab-spatial-range", "tab-methods-evidence",
        "report-focus-target", "copy-artifact-id",
        "shelf-0", "shelf-1", "shelf-2"};
    for (const char* id : reportClickIds)
        _impl->attachReport(*this, id, "click");
    _impl->attachReport(*this, "report-metric-select", "change");
    _impl->updateViewportLayout(context);
    if (_impl->desiredVisible.load())
        _impl->reportDocument->Show(
            Rml::ModalFlag::None, Rml::FocusFlag::None);
    _impl->renderedVisible = _impl->desiredVisible.load();
    error.clear();
    return true;
}

void ScienceWorkbenchPresenter::onRmlFrame(Rml::Context& context)
{
    _impl->updateViewportLayout(context);
    const bool visible = _impl->desiredVisible.load();
    if (visible != _impl->renderedVisible)
    {
        if (visible)
        {
            if (_impl->document)
                _impl->document->Show(
                    Rml::ModalFlag::None, Rml::FocusFlag::None);
            if (_impl->reportDocument)
                _impl->reportDocument->Show(
                    Rml::ModalFlag::None, Rml::FocusFlag::None);
        }
        else
        {
            if (_impl->document) _impl->document->Hide();
            if (_impl->reportDocument) _impl->reportDocument->Hide();
        }
        _impl->renderedVisible = visible;
    }
    if (!visible) return;
    _impl->updateLiveTargetOverlay();
    {
        std::lock_guard<std::mutex> guard(_impl->errorMutex);
        if (!_impl->actionError.empty())
        {
            _impl->setText("workbench-error", _impl->actionError);
            _impl->setText("run-feedback", _impl->actionError);
            _impl->setDisabled("run-action", false);
        }
    }
    std::string json, error;
    if (!_impl->runtime.copyWorkbenchSnapshot(json, error))
    {
        _impl->setText("workbench-error", error);
        _impl->setText("run-feedback", error);
        _impl->setDisabled("run-action", false);
        return;
    }
    SnapshotView view;
    if (!parseSnapshot(json, view, error))
    {
        _impl->setText("workbench-error", error);
        _impl->setText("run-feedback", error);
        _impl->setDisabled("run-action", false);
        return;
    }
    if (const ScienceReportWindowState* active = _impl->reportModel.active())
    {
        _impl->updateContextSnapshot(active->artifactId);
        if (RmlScienceChart* chart = rmlui_dynamic_cast<RmlScienceChart*>(
                _impl->reportElement("science-chart")))
        {
            int selectedYear = 0;
            if (chart->consumeSelectedYear(selectedYear) &&
                _impl->reportModel.selectYear(active->artifactId, selectedYear))
            {
                _impl->enqueue("select-year",
                    "{\"schema\":\"science-workbench-action-v1\","
                    "\"action\":\"select-year\",\"year\":" +
                    std::to_string(selectedYear) + "}");
                _impl->updateReportShell(view);
            }
        }
    }
    if (view.revision == _impl->renderedRevision) return;
    _impl->state = view;
    _impl->renderedRevision = view.revision;
    if (_impl->pendingSourceId == view.sourceId)
        _impl->pendingSourceId.clear();
    if (_impl->pendingMethodId == view.methodId)
        _impl->pendingMethodId.clear();
    const std::string confirmedYearRange =
        std::to_string(view.firstYear) + ":" + std::to_string(view.lastYear);
    if (_impl->pendingYearRange == confirmedYearRange)
        _impl->pendingYearRange.clear();

    std::string sourceFingerprint;
    for (const SourceView& source : view.sources)
        sourceFingerprint += source.id + "\n" + source.name + "\n";
    if (sourceFingerprint != _impl->renderedSourceFingerprint)
    {
        ProgrammaticControlSync sync(_impl->controlSyncDepth);
        if (Rml::ElementFormControlSelect* select =
            rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(
                _impl->element("source-select")))
        {
            select->RemoveAll();
            for (const SourceView& source : view.sources)
            {
                std::string label = source.name;
                if (source.health != "ready") label += " · 暂不可用";
                select->Add(escapedRml(label), source.id,
                            -1, source.health == "ready");
            }
        }
        _impl->renderedSourceFingerprint = sourceFingerprint;
    }
    _impl->setControlValue("source-select", view.sourceId);

    const SourceView* selected = nullptr;
    for (const SourceView& source : view.sources)
        if (source.id == view.sourceId) { selected = &source; break; }
    if (!selected && !view.sources.empty()) selected = &view.sources.front();
    std::vector<ScienceWorkbenchMethod> methods;
    if (selected)
        methods = scienceWorkbenchMethodsForSourceId(
            selected->id, selected->supportsTimeSeries,
            selected->supportsAnalysis, selected->supportsRaster);
    std::string methodFingerprint = selected ? selected->id : "";
    for (const ScienceWorkbenchMethod& method : methods)
        methodFingerprint += "\n" + method.id + "\n" + method.label;
    if (methodFingerprint != _impl->renderedMethodFingerprint)
    {
        ProgrammaticControlSync sync(_impl->controlSyncDepth);
        if (Rml::ElementFormControlSelect* select =
            rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(
                _impl->element("method-select")))
        {
            select->RemoveAll();
            for (const ScienceWorkbenchMethod& method : methods)
                select->Add(
                    escapedRml(method.label), method.id, -1, true);
        }
        _impl->renderedMethodFingerprint = methodFingerprint;
    }
    const ScienceWorkbenchMethod* selectedMethod = nullptr;
    for (const ScienceWorkbenchMethod& method : methods)
    {
        if (method.id == view.methodId)
        {
            selectedMethod = &method;
            break;
        }
    }
    if (!selectedMethod && !methods.empty()) selectedMethod = &methods.front();
    _impl->setControlValue(
        "method-select", selectedMethod ? selectedMethod->id : "");

    const int displayedFirstYear = view.firstYear > 0 ? view.firstYear :
        (selected ? selected->firstYear : 0);
    const int displayedLastYear = view.lastYear > 0 ? view.lastYear :
        (selected ? selected->lastYear : 0);
    _impl->setControlValue(
        "first-year", std::to_string(displayedFirstYear));
    _impl->setControlValue(
        "last-year", std::to_string(displayedLastYear));

    std::string runLabel = "开始分析";
    if (selected)
    {
        _impl->updateSourceDescription(selected, selectedMethod);
        if (selectedMethod) runLabel = selectedMethod->runLabel;
    }

    _impl->setText("workbench-state", phaseLabel(view.phase));
    _impl->setText("target-status", view.locked ? "已锁定" : "未锁定");
    if (view.locked)
    {
        _impl->setText("target-title",
            view.requested.kind == ScienceOverlayGeometryKind::Bounds
                ? "固定区域" : "固定位置（单个数据网格）");
        std::ostringstream coordinates;
        coordinates << std::fixed << std::setprecision(4)
                    << view.requestedLatitude << "°, "
                    << view.requestedLongitude << "°";
        _impl->setText("target-coordinates", coordinates.str());
        _impl->setText("target-support", selected
            ? "数据实际空间单位：" + selected->spatialSupport : "");
    }
    else
    {
        _impl->setText("target-title", "尚未锁定分析位置");
        _impl->setText("target-coordinates", "在地图上定位后锁定范围");
        _impl->setText("target-support", "");
    }
    _impl->setDisplay("target-lock-note", view.locked);
    _impl->setDisplay("lock-map-center", !view.locked, "inline-block");
    _impl->setDisplay("lock-current-view",
        !view.locked && selected && selected->supportsBounds, "inline-block");
    _impl->setDisplay("update-target", view.locked, "inline-block");
    _impl->setDisplay("focus-target", view.locked, "inline-block");

    _impl->setText("source-bytes", formatBytes(view.sourceBytes));
    _impl->setText("result-cells", view.resultCells > 0.0
        ? std::to_string(static_cast<std::uint64_t>(view.resultCells))
        : "尚未估算");
    _impl->setText("estimated-duration",
        view.durationDeterminate && view.estimatedSeconds > 0.0
            ? std::to_string(static_cast<int>(std::ceil(view.estimatedSeconds))) + " 秒"
            : "运行后计算");

    const bool active = running(view.phase);
    const bool canRun = !view.sourceId.empty() &&
        displayedFirstYear > 0 &&
        displayedLastYear >= displayedFirstYear &&
        selectedMethod && !active;
    _impl->setDisabled("run-action", !canRun);
    _impl->setDisplay("run-action", !active);
    _impl->setText("run-action", view.locked
        ? runLabel : "锁定地图中心并" + runLabel);
    _impl->setDisplay("progress-footer", active);
    _impl->setDisplay("run-feedback", !active);
    _impl->setText("progress-label", progressLabel(view.progressStage));
    if (active) _impl->setText("run-feedback", "");
    if (Rml::Element* progress = _impl->element("run-progress"))
    {
        const double fraction = view.progressDeterminate
            ? std::clamp(view.completedUnits / view.totalUnits, 0.0, 1.0)
            : 0.0;
        progress->SetAttribute("value", static_cast<int>(fraction * 100.0));
    }
    {
        std::lock_guard<std::mutex> guard(_impl->errorMutex);
        if (_impl->actionError.empty())
        {
            _impl->setText("workbench-error", errorLabel(view));
            _impl->setText("run-feedback",
                view.phase == "failed"
                    ? errorLabel(view) : "");
        }
    }

    ScienceTargetOverlayInput target;
    target.requested = view.requested;
    target.actual = view.actual;
    target.requestedVisible = view.locked;
    target.actualVisible = view.locked && view.hasActual;
    target.label = selected ? selected->name : "科学分析范围";
    if (view.firstYear > 0)
        target.label += " · " + std::to_string(view.firstYear) + "–" +
            std::to_string(view.lastYear);
    {
        std::lock_guard<std::mutex> guard(_impl->targetMutex);
        _impl->target = std::move(target);
        if (_impl->contextCapture)
            _impl->contextCapture->setLiveTarget(_impl->target);
    }
    _impl->updateReportShell(view);
}

void ScienceWorkbenchPresenter::ProcessEvent(Rml::Event& event)
{
    if (_impl->controlSyncDepth > 0) return;
    Rml::Element* target = event.GetTargetElement();
    if (!target) return;
    if (target->IsClassSet("disabled") || target->HasAttribute("disabled"))
        return;
    const std::string id = target->GetId();
    const std::string schema =
        "{\"schema\":\"science-workbench-action-v1\",\"action\":";
    if (id == "source-select")
    {
        const std::string source = _impl->value("source-select");
        if (source.empty() || source == _impl->pendingSourceId ||
            (_impl->pendingSourceId.empty() &&
             source == _impl->state.sourceId))
            return;
        _impl->pendingSourceId = source;
        _impl->enqueue("select-source", schema + "\"select-source\",\"sourceId\":" +
            jsonString(source) + "}");
        for (const SourceView& item : _impl->state.sources)
        {
            if (item.id != source) continue;
            const std::vector<ScienceWorkbenchMethod> methods =
                scienceWorkbenchMethodsForSourceId(
                    item.id, item.supportsTimeSeries,
                    item.supportsAnalysis, item.supportsRaster);
            const ScienceWorkbenchMethod* selectedMethod =
                methods.empty() ? nullptr : &methods.front();
            {
                ProgrammaticControlSync sync(_impl->controlSyncDepth);
                if (Rml::ElementFormControlSelect* select =
                    rmlui_dynamic_cast<Rml::ElementFormControlSelect*>(
                        _impl->element("method-select")))
                {
                    select->RemoveAll();
                    for (const ScienceWorkbenchMethod& method : methods)
                        select->Add(
                            escapedRml(method.label), method.id, -1, true);
                    if (selectedMethod)
                        select->SetValue(selectedMethod->id);
                }
            }
            _impl->renderedMethodFingerprint = item.id;
            for (const ScienceWorkbenchMethod& method : methods)
                _impl->renderedMethodFingerprint +=
                    "\n" + method.id + "\n" + method.label;
            _impl->updateSourceDescription(&item, selectedMethod);
            if (selectedMethod)
                _impl->setText("run-action", _impl->state.locked
                    ? selectedMethod->runLabel
                    : "锁定地图中心并" + selectedMethod->runLabel);
            if (!methods.empty())
            {
                _impl->pendingMethodId = methods.front().id;
                _impl->enqueue("set-method", schema +
                    "\"set-method\",\"methodId\":" +
                    jsonString(methods.front().id) + "}");
            }
            const int first = std::clamp(
                _impl->state.firstYear, item.firstYear, item.lastYear);
            const int last = std::clamp(
                _impl->state.lastYear, first, item.lastYear);
            _impl->setControlValue("first-year", std::to_string(first));
            _impl->setControlValue("last-year", std::to_string(last));
            _impl->pendingYearRange =
                std::to_string(first) + ":" + std::to_string(last);
            _impl->enqueue("set-year-range", schema +
                "\"set-year-range\",\"firstYear\":" +
                std::to_string(first) + ",\"lastYear\":" +
                std::to_string(last) + "}");
            break;
        }
    }
    else if (id == "method-select")
    {
        const std::string method = _impl->value("method-select");
        if (method.empty() || method == _impl->pendingMethodId ||
            (_impl->pendingMethodId.empty() &&
             method == _impl->state.methodId))
            return;
        _impl->pendingMethodId = method;
        const std::string sourceId = _impl->pendingSourceId.empty()
            ? _impl->state.sourceId : _impl->pendingSourceId;
        for (const SourceView& item : _impl->state.sources)
        {
            if (item.id != sourceId) continue;
            const std::vector<ScienceWorkbenchMethod> methods =
                scienceWorkbenchMethodsForSourceId(
                    item.id, item.supportsTimeSeries,
                    item.supportsAnalysis, item.supportsRaster);
            for (const ScienceWorkbenchMethod& candidate : methods)
            {
                if (candidate.id != method) continue;
                _impl->updateSourceDescription(&item, &candidate);
                _impl->setText("run-action", _impl->state.locked
                    ? candidate.runLabel
                    : "锁定地图中心并" + candidate.runLabel);
                break;
            }
            break;
        }
        _impl->enqueue("set-method", schema + "\"set-method\",\"methodId\":" +
            jsonString(method) + "}");
    }
    else if (id == "report-metric-select")
    {
        const ScienceReportWindowState* active = _impl->reportModel.active();
        const std::string metric = _impl->reportValue("report-metric-select");
        if (active && !metric.empty() &&
            _impl->reportModel.selectMetric(active->artifactId, metric))
        {
            _impl->enqueue("select-metric", schema +
                "\"select-metric\",\"metricId\":" +
                jsonString(metric) + "}");
            _impl->updateReportShell(_impl->state);
        }
    }
    else if (id == "first-year" || id == "last-year")
    {
        try
        {
            const int first = std::stoi(_impl->value("first-year"));
            const int last = std::stoi(_impl->value("last-year"));
            if (first <= 0 || last < first)
            {
                _impl->setText("time-error", "结束年份必须不早于开始年份。");
                return;
            }
            _impl->setText("time-error", "");
            const std::string range =
                std::to_string(first) + ":" + std::to_string(last);
            if (range == _impl->pendingYearRange ||
                (_impl->pendingYearRange.empty() &&
                 first == _impl->state.firstYear &&
                 last == _impl->state.lastYear))
                return;
            _impl->pendingYearRange = range;
            _impl->enqueue("set-year-range", schema +
                "\"set-year-range\",\"firstYear\":" + std::to_string(first) +
                ",\"lastYear\":" + std::to_string(last) + "}");
        }
        catch (...)
        {
            _impl->setText("time-error", "请输入四位年份。");
        }
    }
    else if (id == "cost-toggle")
    {
        _impl->costOpen = !_impl->costOpen;
        _impl->setDisplay("cost-body", _impl->costOpen);
        _impl->setText("cost-toggle", _impl->costOpen
            ? "收起提交前检查" : "查看提交前检查");
    }
    else if (id == "science-help")
    {
        _impl->helpOpen = !_impl->helpOpen;
        _impl->setDisplay("science-help-copy", _impl->helpOpen);
    }
    else if (id == "lock-map-center" || id == "update-target")
        _impl->enqueue("lock-map-center", "");
    else if (id == "lock-current-view")
        _impl->enqueue("lock-current-view", "");
    else if (id == "focus-target")
        _impl->enqueue("focus-target", schema + "\"focus-target\"}");
    else if (id == "run-action")
    {
        {
            std::lock_guard<std::mutex> guard(_impl->errorMutex);
            _impl->actionError.clear();
        }
        _impl->setText("workbench-error", "");
        _impl->setText("run-feedback", "正在提交分析任务…");
        _impl->setDisabled("run-action", true);
        if (!_impl->state.locked)
            _impl->enqueue("lock-map-center", "");
        _impl->enqueue("run", schema + "\"run\"}");
    }
    else if (id == "cancel-action")
        _impl->enqueue("cancel", schema + "\"cancel\"}");
    else if (id == "report-minimize")
    {
        const ScienceReportWindowState* active = _impl->reportModel.active();
        if (active)
        {
            _impl->reportModel.minimize(active->artifactId);
            _impl->enqueue("minimize-report",
                schema + "\"minimize-report\"}");
            _impl->updateReportShell(_impl->state);
        }
    }
    else if (id == "report-close")
    {
        const ScienceReportWindowState* active = _impl->reportModel.active();
        if (active)
        {
            _impl->reportModel.close(active->artifactId);
            _impl->enqueue("close-report", schema + "\"close-report\"}");
            _impl->updateReportShell(_impl->state);
        }
    }
    else if (id == "report-overflow")
        _impl->setReportDisplay("delete-confirmation", true);
    else if (id == "report-delete-cancel")
        _impl->setReportDisplay("delete-confirmation", false);
    else if (id == "report-delete")
    {
        const ScienceReportWindowState* active = _impl->reportModel.active();
        if (active)
        {
            const std::string artifactId = active->artifactId;
            _impl->reportModel.remove(artifactId);
            _impl->enqueue("remove-artifact", schema +
                "\"remove-artifact\",\"artifactId\":" +
                jsonString(artifactId) + "}");
            _impl->setReportDisplay("delete-confirmation", false);
            _impl->updateReportShell(_impl->state);
        }
    }
    else if (id.rfind("tab-", 0) == 0)
    {
        const ScienceReportWindowState* active = _impl->reportModel.active();
        if (active)
        {
            _impl->reportModel.setSection(active->artifactId, id.substr(4));
            _impl->updateReportShell(_impl->state);
        }
    }
    else if (id == "report-focus-target")
        _impl->enqueue("focus-target", schema + "\"focus-target\"}");
    else if (id == "copy-artifact-id")
    {
        const ScienceReportWindowState* active = _impl->reportModel.active();
        if (active)
        {
            Rml::GetSystemInterface()->SetClipboardText(active->artifactId);
            _impl->setReportText("copy-artifact-status", "已复制");
        }
    }
    else if (id.rfind("shelf-", 0) == 0)
    {
        const std::string artifactId = target->GetAttribute<Rml::String>(
            "data-artifact", "");
        if (!artifactId.empty() && _impl->reportModel.reopen(artifactId))
        {
            _impl->enqueue("open-report", schema +
                "\"open-report\",\"artifactId\":" +
                jsonString(artifactId) + "}");
            _impl->updateReportShell(_impl->state);
        }
    }
}

bool ScienceWorkbenchPresenter::takeQueuedAction(
    ScienceWorkbenchQueuedAction& action)
{
    std::lock_guard<std::mutex> guard(_impl->queueMutex);
    if (_impl->queue.empty()) return false;
    action = std::move(_impl->queue.front());
    _impl->queue.pop_front();
    return true;
}

bool ScienceWorkbenchPresenter::latestTarget(
    ScienceTargetOverlayInput& target) const
{
    std::lock_guard<std::mutex> guard(_impl->targetMutex);
    target = _impl->target;
    return target.requestedVisible || target.actualVisible;
}

void ScienceWorkbenchPresenter::publishActionError(const std::string& error)
{
    std::lock_guard<std::mutex> guard(_impl->errorMutex);
    _impl->actionError = error;
    if (!error.empty())
    {
        _impl->pendingSourceId.clear();
        _impl->pendingMethodId.clear();
        _impl->pendingYearRange.clear();
        _impl->renderedRevision =
            std::numeric_limits<std::uint64_t>::max();
    }
}

void ScienceWorkbenchPresenter::setVisible(bool visible)
{
    _impl->desiredVisible.store(visible);
}
