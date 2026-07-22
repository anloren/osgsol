#include "science_workbench_presenter.h"

#include "../science_plugin_runtime.h"
#include "science_report_window.h"
#include "map_context_capture.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <picojson.h>

#include <algorithm>
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
                item.supportsBounds = boolField(
                    capabilitiesValue->get<picojson::object>(), "bounds");
            if (!item.id.empty()) output.sources.push_back(std::move(item));
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
            if (Rml::ElementFormControl* control =
                rmlui_dynamic_cast<Rml::ElementFormControl*>(item))
                control->SetDisabled(disabled);
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

    SciencePluginRuntime& runtime;
    std::string documentPath;
    Rml::ElementDocument* document = nullptr;
    Rml::ElementDocument* reportDocument = nullptr;
    std::uint64_t renderedRevision = std::numeric_limits<std::uint64_t>::max();
    std::string renderedSourceFingerprint;
    bool costOpen = false;
    bool helpOpen = false;
    SnapshotView state;
    mutable std::mutex targetMutex;
    ScienceTargetOverlayInput target;
    mutable std::mutex queueMutex;
    std::deque<ScienceWorkbenchQueuedAction> queue;
    mutable std::mutex errorMutex;
    std::string actionError;
    ScienceReportWindowModel reportModel;
    MapContextCapture* contextCapture = nullptr;
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
        "report-focus-target", "shelf-0", "shelf-1", "shelf-2"};
    for (const char* id : reportClickIds)
        _impl->attachReport(*this, id, "click");
    const Rml::Vector2i dimensions = context.GetDimensions();
    _impl->reportModel.setViewport(
        static_cast<float>(dimensions.x), static_cast<float>(dimensions.y));
    _impl->reportDocument->Show(
        Rml::ModalFlag::None, Rml::FocusFlag::None);
    error.clear();
    return true;
}

void ScienceWorkbenchPresenter::onRmlFrame(Rml::Context&)
{
    _impl->updateLiveTargetOverlay();
    {
        std::lock_guard<std::mutex> guard(_impl->errorMutex);
        if (!_impl->actionError.empty())
            _impl->setText("workbench-error", _impl->actionError);
    }
    std::string json, error;
    if (!_impl->runtime.copyWorkbenchSnapshot(json, error))
    {
        _impl->setText("workbench-error", error);
        return;
    }
    SnapshotView view;
    if (!parseSnapshot(json, view, error))
    {
        _impl->setText("workbench-error", error);
        return;
    }
    if (const ScienceReportWindowState* active = _impl->reportModel.active())
        _impl->updateContextSnapshot(active->artifactId);
    if (view.revision == _impl->renderedRevision) return;
    _impl->state = view;
    _impl->renderedRevision = view.revision;

    std::string sourceFingerprint;
    for (const SourceView& source : view.sources)
        sourceFingerprint += source.id + "\n" + source.name + "\n";
    if (sourceFingerprint != _impl->renderedSourceFingerprint)
    {
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
    if (Rml::ElementFormControl* source =
        rmlui_dynamic_cast<Rml::ElementFormControl*>(
            _impl->element("source-select")))
        source->SetValue(view.sourceId);
    if (Rml::ElementFormControl* method =
        rmlui_dynamic_cast<Rml::ElementFormControl*>(
            _impl->element("method-select")))
        method->SetValue(view.methodId.empty() ? "annual-summary" : view.methodId);
    if (Rml::ElementFormControl* first =
        rmlui_dynamic_cast<Rml::ElementFormControl*>(
            _impl->element("first-year")))
        first->SetValue(std::to_string(view.firstYear));
    if (Rml::ElementFormControl* last =
        rmlui_dynamic_cast<Rml::ElementFormControl*>(
            _impl->element("last-year")))
        last->SetValue(std::to_string(view.lastYear));

    const SourceView* selected = nullptr;
    for (const SourceView& source : view.sources)
        if (source.id == view.sourceId) { selected = &source; break; }
    if (!selected && !view.sources.empty()) selected = &view.sources.front();
    if (selected)
    {
        _impl->setText("analysis-name", selected->name);
        std::string summary = selected->category.empty()
            ? "按时间和空间范围生成可复核的科学结果。"
            : selected->category + " · " + selected->spatialSupport;
        if (selected->health != "ready" && !selected->healthMessage.empty())
            summary += " · " + selected->healthMessage;
        _impl->setText("analysis-summary", summary);
        _impl->setText("provider-years",
            std::to_string(selected->firstYear) + "–" +
            std::to_string(selected->lastYear));
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
    const bool canRun = view.locked && !view.sourceId.empty() &&
        view.firstYear > 0 && view.lastYear >= view.firstYear && !active;
    _impl->setDisabled("run-action", !canRun);
    _impl->setDisplay("run-action", !active);
    _impl->setDisplay("progress-footer", active);
    _impl->setText("progress-label", progressLabel(view.progressStage));
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
            _impl->setText("workbench-error", errorLabel(view));
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
    Rml::Element* target = event.GetTargetElement();
    if (!target) return;
    const std::string id = target->GetId();
    const std::string schema =
        "{\"schema\":\"science-workbench-action-v1\",\"action\":";
    if (id == "source-select")
    {
        const std::string source = _impl->value("source-select");
        _impl->enqueue("select-source", schema + "\"select-source\",\"sourceId\":" +
            jsonString(source) + "}");
    }
    else if (id == "method-select")
    {
        const std::string method = _impl->value("method-select");
        _impl->enqueue("set-method", schema + "\"set-method\",\"methodId\":" +
            jsonString(method) + "}");
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
        _impl->enqueue("run", schema + "\"run\"}");
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
}
