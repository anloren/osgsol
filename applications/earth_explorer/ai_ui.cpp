#include "ai_ui.h"
#include "ai_media.h"
#include "ai_prompts.h"
#include "earth_control_layout.h"
#include "earth_ui_components.h"
#include "earth_ui_tokens.h"
#if defined(__APPLE__)
#include "ime_bridge.h"   // 中文 IME:上报输入框矩形给候选窗定位
#endif
#include <ui/ImGuiComponents.h>
#include <readerwriter/EarthManipulator.h>
#include <osg/Math>
#include <osg/Notify>
#include <algorithm>
#include <cfloat>
#include <cstring>

#if defined(OSGSOL_UI_AUDIT_HOOKS)
namespace
{
AIChatUI::AuditRect auditLastItemRect()
{
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    AIChatUI::AuditRect result;
    result.x = minimum.x;
    result.y = minimum.y;
    result.width = std::max(0.0f, maximum.x - minimum.x);
    result.height = std::max(0.0f, maximum.y - minimum.y);
    result.valid = result.width > 0.0f && result.height > 0.0f;
    return result;
}
}
#endif

AIChatUI::AIChatUI()
    : _historyCollapsed(true), _lastEntryCount(0)
{
    _inputBuf[0] = '\0';
    _cinematicPrompt[0] = '\0';
    _cinematicCustomEra[0] = '\0';
    _cinematicCustomTime[0] = '\0';
    _cinematicCustomStyle[0] = '\0';
}

// 转发给 AICardPanel(Task 8 PART A 从本文件抽出,见 ai_cards.h/.cpp)。
void AIChatUI::pushChart(const picojson::value& spec)
{
    _cards.pushChart(spec);
}

void AIChatUI::draw(earthai::AIChatCore* core, earthai::MediaManager* media,
                    osgVerse::EarthManipulator* mani,
                    const earthui::EarthUiShellLayout& shell,
                    earthui::CardStack& cardStack)
{
    ImGuiIO& io = ImGui::GetIO();
    const float winWidth = shell.commandWidth;
#if defined(OSGSOL_UI_AUDIT_HOOKS)
    _auditSnapshot = AuditSnapshot();
#endif

    // EarthUI v2 的 AI Command Deck 固定在地图下方中央；历史展开时向上生长，
    // 不侵占模块抽屉、右侧 Insight Lens 或底部上下文控件。
    ImGui::SetNextWindowPos(
        ImVec2(shell.commandX, shell.contextY - shell.outerGap),
        ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(winWidth, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(winWidth, 0.0f), ImVec2(winWidth, FLT_MAX));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 7.0f));
    ImGui::PushStyleColor(
        ImGuiCol_WindowBg, earthui::design::kCarbon);
    ImGui::PushStyleColor(
        ImGuiCol_Border, earthui::design::kBorder);
    ImGui::PushStyleColor(
        ImGuiCol_Button, earthui::design::kIron);
    ImGui::PushStyleColor(
        ImGuiCol_ButtonHovered, earthui::design::kOxblood);
    ImGui::PushStyleColor(
        ImGuiCol_ButtonActive, earthui::design::kVermilion);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                             ImGuiWindowFlags_NoSavedSettings;
    // 函数作用域(而非 Begin/End 块内)声明:确认 Modal 画在 AI 对话条窗口 End() 之后,
    // 需要跨过该窗口的 { } 作用域读到这个标志。
    bool openVideoModal = false;
    earthai::MediaManager::VideoUiSnapshot video = media
        ? media->videoUiSnapshot() : earthai::MediaManager::VideoUiSnapshot();
    earthai::MediaRouteCapabilities routeCapabilities = media
        ? media->routeCapabilities() : earthai::MediaRouteCapabilities();
    bool mediaControlsVisible = media != nullptr;
#if defined(OSGSOL_UI_AUDIT_HOOKS)
    if (_auditMediaControlsEnabled)
    {
        // Audit builds inject an explicit complete capability value. Production never derives
        // route eligibility from the audit flag or from pointer presence.
        routeCapabilities.hasRealMediaKey = true;
        routeCapabilities.hasProviderSession = true;
        routeCapabilities.videoProvider =
            earthai::CINEMATIC_VIDEO_PROVIDER_VEO;
        routeCapabilities.deterministicLocalEncoder = true;
        mediaControlsVisible = true;
    }
    if (_auditVideoState == AUDIT_VIDEO_WAIT_B)
    {
        video.phase = earthai::VIDEO_WAIT_B;
    }
    else if (_auditVideoState == AUDIT_VIDEO_CONFIRM)
    {
        video.phase = earthai::VIDEO_AWAIT_CONFIRM;
        video.pending.ready = true;
        video.pending.llaA.set(
            osg::DegreesToRadians(22.2950),
            osg::DegreesToRadians(114.1404), 1200.0);
        video.pending.llaB.set(
            osg::DegreesToRadians(22.3193),
            osg::DegreesToRadians(114.1694), 900.0);
        video.pending.motionPrompt =
            u8"从香港西九龙上空平滑推进至维多利亚港，保持地平线稳定并保留真实比例。";
    }
#endif
    if (ImGui::Begin(u8"AI 对话条", NULL, flags))
    {
        bool busy = core && core->busy();

        ImGui::PushStyleColor(
            ImGuiCol_Text, earthui::design::kCyan);
        ImGui::TextUnformatted(u8"AI 地球助手");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled(busy ? u8"正在执行…" : u8"就绪");
        if (winWidth >= 360.0f) ImGui::SameLine();
        ImGui::TextColored(
            earthui::design::kSuccess, u8"上下文已连接");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                u8"AI 会读取当前模块、地图状态、图层、选中对象和科学报告的"
                u8"结构化数据；不读取屏幕像素，也不会把来源文字当作指令。");
        }

        // ---- 历史抽屉（默认折叠；只有用户主动打开时占用地图空间）----
        // 默认态把标题、历史和模板组织成一个响应式工具头：宽屏同一行，
        // 窄屏只把历史/模板换到第二行。旧布局无条件占三行，实际高度超过
        // Shell 预留并压到科学报告上。
        const bool inlineHeader = winWidth >= 420.0f;
        bool templateCanShareLine = inlineHeader;
        if (core)
        {
            std::vector<earthai::ChatEntry> transcript = core->transcript();   // 每帧一次快照
            if (!transcript.empty())
            {
                if (inlineHeader) ImGui::SameLine();
                if (ImGui::SmallButton(
                        _historyCollapsed ? u8"历史" : u8"收起历史"))
                    _historyCollapsed = !_historyCollapsed;
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                _auditSnapshot.historyButton = auditLastItemRect();
                _auditSnapshot.historyExpanded = !_historyCollapsed;
#endif
                ImGui::SameLine();
                ImGui::TextDisabled(u8"%d 条", (int)transcript.size());

                if (!_historyCollapsed)
                {
                    templateCanShareLine = false;
                    float maxH = std::min(360.0f, io.DisplaySize.y * 0.32f);
                    ImGui::BeginChild(
                        "##ai_history", ImVec2(winWidth - 28.0f, maxH), true);
                    for (size_t i = 0; i < transcript.size(); ++i)
                    {
                        const earthai::ChatEntry& e = transcript[i];
                        bool isLast = (i + 1 == transcript.size());
                        switch (e.kind)
                        {
                        case earthai::ChatEntry::USER:
                        {
                            // 右对齐：按可用宽度估算文本起始 x
                            float avail = ImGui::GetContentRegionAvail().x;
                            float textW = ImGui::CalcTextSize(e.text.c_str()).x;
                            float pad = avail - textW; if (pad < 0.0f) pad = 0.0f;
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + pad);
                            ImGui::PushStyleColor(
                                ImGuiCol_Text, earthui::design::kCyan);
                            ImGui::TextWrapped(u8"%s", e.text.c_str());
                            ImGui::PopStyleColor();
                            break;
                        }
                        case earthai::ChatEntry::ASSISTANT:
                            ImGui::PushStyleColor(
                                ImGuiCol_Text, earthui::design::kTextStrong);
                            ImGui::TextWrapped(u8"%s", e.text.c_str());
                            ImGui::PopStyleColor();
                            break;
                        case earthai::ChatEntry::TOOL_NOTE:
                            ImGui::PushStyleColor(
                                ImGuiCol_Text, earthui::design::kTextDim);
                            ImGui::TextWrapped(u8"%s", e.text.c_str());
                            ImGui::PopStyleColor();
                            break;
                        case earthai::ChatEntry::ERR:
                        default:
                            ImGui::PushStyleColor(
                                ImGuiCol_Text, earthui::design::kDanger);
                            ImGui::TextWrapped(u8"%s", e.text.c_str());
                            ImGui::PopStyleColor();
                            break;
                        }
                        // 忙碌态：最后一行追加动画小圆点/转轮，提示仍在处理
                        if (busy && isLast)
                        {
                            static const char spin[4] = { '|', '/', '-', '\\' };
                            int idx = (int)(ImGui::GetTime() * 8.0) % 4;
                            ImGui::SameLine();
                            ImGui::TextDisabled(" %c", spin[idx]);
                        }
                    }
                    // 新增条目时自动滚动到底部
                    if (transcript.size() != _lastEntryCount)
                    {
                        ImGui::SetScrollHereY(1.0f);
                        _lastEntryCount = transcript.size();
                    }
                    ImGui::EndChild();
                }
                else
                    templateCanShareLine = true;
            }
            else if (busy)
            {
                templateCanShareLine = false;
                static const char spin[4] = { '|', '/', '-', '\\' };
                int idx = (int)(ImGui::GetTime() * 8.0) % 4;
                ImGui::TextDisabled(u8"思考中 %c", spin[idx]);
            }
        }

        // ---- ScienceEarth 分析模板：准备视角和参数，不自动提交 ----
        if (core)
        {
            if (templateCanShareLine) ImGui::SameLine();
            if (ImGui::SmallButton(u8"分析模板"))
                ImGui::OpenPopup("##scienceearth_templates");
#if defined(OSGSOL_UI_AUDIT_HOOKS)
            _auditSnapshot.templateButton = auditLastItemRect();
#endif

            const float popupWidth = std::min(520.0f,
                std::max(320.0f, io.DisplaySize.x - 40.0f));
            const float popupHeight = std::min(540.0f,
                std::max(260.0f, io.DisplaySize.y * 0.62f));
            const float popupX = std::clamp(
                shell.commandX + winWidth - popupWidth,
                12.0f, std::max(12.0f, io.DisplaySize.x - popupWidth - 12.0f));
            const float popupY = std::max(
                shell.topBarHeight + shell.outerGap,
                shell.commandY - shell.outerGap - popupHeight);
            ImGui::SetNextWindowPos(
                ImVec2(popupX, popupY), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight),
                                     ImGuiCond_Appearing);
            if (ImGui::BeginPopup("##scienceearth_templates"))
            {
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                _auditSnapshot.templatePopupVisible = true;
#endif
                ImGui::TextUnformatted(u8"ScienceEarth 分析模板");
                ImGui::TextDisabled(
                    u8"带地点模板会先定位并填好参数；当前视野模板保留相机。"
                    u8"都不会自动运行，检查后点“发送”。");
                ImGui::Separator();
                ImGui::BeginChild(
                    "##scienceearth_prompt_gallery",
                    ImVec2(0.0f, 0.0f), false);
                const auto& examples = earthai::scienceEarthPromptExamples();
                for (std::size_t index = 0; index < examples.size(); ++index)
                {
                    const earthai::ScienceEarthPromptExample& example =
                        examples[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TextWrapped("%zu. %s", index + 1, example.title);
                    ImGui::TextDisabled(u8"数据源：%s", example.sources);
                    ImGui::TextDisabled(u8"参数：%s", example.parameters);
                    if (example.navigateToLocation)
                    {
                        ImGui::TextDisabled(u8"视角：%s · %.4f, %.4f · %.0f km",
                                            example.locationName,
                                            example.latitudeDeg,
                                            example.longitudeDeg,
                                            example.altitudeKm);
                    }
                    else
                    {
                        ImGui::TextDisabled(u8"视角：保留当前视野");
                    }
                    if (ImGui::TreeNodeEx(
                            "##prompt_text",
                            ImGuiTreeNodeFlags_SpanAvailWidth,
                            u8"查看完整提示词"))
                    {
                        ImGui::TextWrapped("%s", example.prompt);
                        ImGui::TreePop();
                    }
                    const char* actionLabel = example.navigateToLocation
                        ? u8"使用并定位" : u8"使用当前视野";
                    const bool needsManipulator =
                        example.navigateToLocation && !mani;
                    if (needsManipulator) ImGui::BeginDisabled();
                    if (ImGui::Button(actionLabel, ImVec2(-1.0f, 0.0f)))
                    {
                        if (earthai::insertPromptSuggestion(
                                example.prompt, _inputBuf, sizeof(_inputBuf)))
                        {
                            if (example.navigateToLocation && mani)
                            {
                                mani->setByEye(
                                    osg::DegreesToRadians(example.latitudeDeg),
                                    osg::DegreesToRadians(example.longitudeDeg),
                                    example.altitudeKm * 1000.0);
                            }
                            _preparedTemplateStatus = u8"已准备：";
                            _preparedTemplateStatus += example.locationName;
                            _preparedTemplateStatus += u8" · ";
                            _preparedTemplateStatus += example.parameters;
                            ImGui::CloseCurrentPopup();
                        }
                    }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                    if (index == 0)
                        _auditSnapshot.templatePrimaryAction =
                            auditLastItemRect();
#endif
                    if (needsManipulator) ImGui::EndDisabled();
                    if (needsManipulator &&
                        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                        ImGui::SetTooltip(u8"当前相机未就绪，暂时不能定位");
                    ImGui::Separator();
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::EndPopup();
            }
        }

        // ---- 输入行 ----
        bool submitted = false;
        std::string submitText;
        if (!_preparedTemplateStatus.empty())
            ImGui::TextDisabled("%s", _preparedTemplateStatus.c_str());
        if (!core) ImGui::BeginDisabled();
        else if (busy) ImGui::BeginDisabled();

        // Measure the real action group. WAIT_B adds a wider button and
        // Cancel; the old fixed reservation clipped the right edge.
        const earthai::VideoPhaseKindPublic vphase = video.phase;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float cancelWidth = ImGui::CalcTextSize(u8"取消").x +
            ImGui::GetStyle().FramePadding.x * 2.0f;
        const float contentWidth = ImGui::GetContentRegionAvail().x;
        const earthui::AiCommandRowLayout commandRow =
            earthui::computeAiCommandRowLayout(
                contentWidth, spacing, cancelWidth, core != nullptr,
                vphase == earthai::VIDEO_WAIT_B);
        const bool actionsOnNextLine = commandRow.actionsOnNextLine;
        ImGui::SetNextItemWidth(commandRow.inputWidth);
        const char* hint = core ? u8"问我：飞到纽约 / 打开航班层 / 统计全球地震…"
                                 : u8"设置 EARTH_AI_KEY 启用 AI 对话";
        if (ImGui::InputTextWithHint("##ai_input", hint, _inputBuf, sizeof(_inputBuf),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (_inputBuf[0] != '\0') { submitText = _inputBuf; submitted = true; }
            _inputBuf[0] = '\0';
            ImGui::SetKeyboardFocusHere(-1);
        }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
        _auditSnapshot.input = auditLastItemRect();
#endif
#if defined(__APPLE__)
        // 中文 IME 候选窗定位:输入框激活时把它的矩形(ImGui 坐标/左上原点)
        // 报给 ime_bridge(firstRectForCharacterRange 用)。每帧覆盖,开销可忽略。
        if (ImGui::IsItemActive())
        {
            ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
            earthime::setInputRect(mn.x, mn.y, mx.x - mn.x, mx.y - mn.y);
        }
#endif

        if (!core || busy) ImGui::EndDisabled();

        if (core)
        {
            if (!actionsOnNextLine) ImGui::SameLine();
            const bool canSubmit = !busy && _inputBuf[0] != '\0';
            if (!canSubmit) ImGui::BeginDisabled();
            if (ImGui::Button(u8"发送", ImVec2(52.0f, 0.0f)))
            {
                submitText = _inputBuf;
                submitted = true;
                _inputBuf[0] = '\0';
            }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
            _auditSnapshot.sendButton = auditLastItemRect();
#endif
            if (!canSubmit) ImGui::EndDisabled();
        }

        // openVideoModal 声明在外层函数作用域(见上方),本帧是否需要 OpenPopup
        // (A/B 都就绪、Modal 尚未打开时置真)在下面媒体控制块里赋值。
        if (mediaControlsVisible)
        {
            // 照片生成入口（Task 8 接线，见 ai_media.h/MediaManager）；🎬 视频（Task 9）三态按钮。
            // 内置中文字体（ChineseFull 范围）不含 emoji glyph，📷/🎬 会渲染成方块（tofu），
            // 因此用文字标签"照片"/"视频"代替。
            ImGui::SameLine();
            bool photoEnabled = !busy && earthai::cinematicSubmissionCanStart(
                earthai::defaultImageCinematicSettings(), routeCapabilities);
#if defined(OSGSOL_UI_AUDIT_HOOKS)
            _auditSnapshot.photoEnabled = photoEnabled;
#endif
            if (!photoEnabled) ImGui::BeginDisabled();
            if (ImGui::Button(u8"照片", ImVec2(52.0f, 0.0f)))
            {
                _cinematicMediaKind = earthai::CINEMATIC_IMAGE;
                _cinematicMotion = earthai::CINEMATIC_MOTION_STATIC;
                _cinematicStudioOpen = true;
                ImGui::OpenPopup(u8"时空影像工作台");
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                _auditActionMask |= AUDIT_ACTION_PHOTO;
#endif
            }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
            _auditSnapshot.photoButton = auditLastItemRect();
#endif
            if (!photoEnabled) ImGui::EndDisabled();
            // ImGui 1.92 起 IsItemHovered() 默认对禁用项返回 false，需显式加
            // ImGuiHoveredFlags_AllowWhenDisabled 才能在禁用按钮上弹出 tooltip。
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                    ImGui::SetTooltip(routeCapabilities.canGenerateImage()
                        ? u8"打开时空影像工作台：从当前视角生成当代、历史或深时重建图像"
                        : u8"生成照片需要已配置的 provider 会话和图像能力");
            }

            // 🎬 三态：空闲"视频" -> 已录 A"完成B点"(+取消) -> 两点都录完:自动弹确认 Modal。
            earthai::CinematicGenerationSettings pointToPoint =
                earthai::defaultVideoCinematicSettings();
            pointToPoint.motion = earthai::CINEMATIC_MOTION_POINT_TO_POINT;
            bool videoEnabled = mani && !busy &&
                (vphase == earthai::VIDEO_IDLE || vphase == earthai::VIDEO_WAIT_B) &&
                earthai::cinematicSubmissionCanStart(
                    pointToPoint, routeCapabilities);
            ImGui::SameLine();
            if (vphase == earthai::VIDEO_WAIT_B)
            {
                if (!videoEnabled) ImGui::BeginDisabled();
                if (ImGui::Button(u8"完成B点", ImVec2(80.0f, 0.0f)) && mani)
                {
                    osg::Vec3d llaB = mani->computeEyeLatLonHeight();
                    earthai::VideoUiRequest request;
                    request.kind = earthai::VideoUiRequest::CaptureEnd;
                    request.lla = llaB;
                    if (media) media->enqueueVideoRequest(request);
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                    else if (_auditMediaControlsEnabled)
                        _auditActionMask |= AUDIT_ACTION_VIDEO_END;
#endif
                }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                _auditSnapshot.videoButton = auditLastItemRect();
#endif
                if (!videoEnabled) ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"取消") && media)
                {
                    earthai::VideoUiRequest request;
                    request.kind = earthai::VideoUiRequest::Cancel;
                    media->enqueueVideoRequest(request);
                }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                else if (ImGui::IsItemActivated() &&
                         _auditMediaControlsEnabled)
                {
                    _auditActionMask |= AUDIT_ACTION_VIDEO_CANCEL;
                }
                _auditSnapshot.videoCancelButton = auditLastItemRect();
#endif
            }
            else
            {
                bool idleEnabled = mani && !busy && vphase == earthai::VIDEO_IDLE &&
                    earthai::cinematicSubmissionCanStart(
                        earthai::defaultVideoCinematicSettings(), routeCapabilities);
                if (!idleEnabled) ImGui::BeginDisabled();
                if (ImGui::Button(u8"视频", ImVec2(52.0f, 0.0f)) && mani)
                {
                    _cinematicMediaKind = earthai::CINEMATIC_VIDEO;
                    if (_cinematicMotion == earthai::CINEMATIC_MOTION_STATIC)
                        _cinematicMotion = earthai::CINEMATIC_MOTION_AERIAL_TOUR;
                    if (_cinematicVisualStyle == earthai::CINEMATIC_STYLE_SCIENTIFIC)
                        _cinematicVisualStyle = earthai::CINEMATIC_STYLE_ULTRA_REAL;
                    _cinematicStudioOpen = true;
                    ImGui::OpenPopup(u8"时空影像工作台");
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                    _auditActionMask |= AUDIT_ACTION_VIDEO_BEGIN;
#endif
                }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                _auditSnapshot.videoButton = auditLastItemRect();
#endif
                if (!idleEnabled) ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip(idleEnabled
                        ? u8"打开时空影像工作台：一镜到底航拍、360° 一镜到底环拍、"
                          u8"俯冲、横移或两点穿越"
                        : u8"生成视频（需设置 EARTH_AI_KEY / EARTH_AI_FAKE）");
                }

                // 没有 provider 视频能力时只暴露这条确定性、本地的单一入口；不能借由
                // MediaManager 的存在解锁普通图像或付费视频。
                if (!routeCapabilities.canGenerateProviderVideo() &&
                    routeCapabilities.canRenderLocalOrbit() && media && mani && !busy &&
                    vphase == earthai::VIDEO_IDLE)
                {
                    ImGui::SameLine();
                    if (ImGui::Button(u8"本地360", ImVec2(76.0f, 0.0f)))
                    {
                        const earthai::CinematicGenerationSettings localDefaults =
                            earthai::normalizedCinematicSubmissionSettings(
                                earthai::CinematicGenerationSettings{
                                    earthai::CINEMATIC_VIDEO,
                                    earthai::CINEMATIC_ERA_PRESENT,
                                    earthai::CINEMATIC_TIME_AUTO,
                                    earthai::CINEMATIC_STYLE_SCIENTIFIC,
                                    earthai::CINEMATIC_MOTION_ORBIT_360});
                        _cinematicMediaKind = localDefaults.mediaKind;
                        _cinematicMotion = localDefaults.motion;
                        _cinematicEra = localDefaults.era;
                        _cinematicLocalTime = localDefaults.localTime;
                        _cinematicVisualStyle = localDefaults.visualStyle;
                        _cinematicStudioOpen = true;
                        ImGui::OpenPopup(u8"时空影像工作台");
                    }
                }
            }

            // A/B 两点都就绪 -> 打开确认 Modal（本帧只判断是否需要 OpenPopup，实际弹窗内容
            // 在下面统一画,避免 OpenPopup 调用点分散）。
            if (vphase == earthai::VIDEO_AWAIT_CONFIRM) openVideoModal = true;
            if (vphase == earthai::VIDEO_RUNNING)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(u8"停止视频") && media)
                {
                    earthai::VideoUiRequest request;
                    request.kind = earthai::VideoUiRequest::Cancel;
                    media->enqueueVideoRequest(request);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(u8"立即停止本地工作或后续轮询；远端已提交的 provider 费用可能无法撤销。");
            }
        }

        // 📷 走对话代理循环（而不是直接调用 MediaManager）：保持"一切能力皆工具"的架构,
        // 且让用户在对话历史里看到这次操作的记录；FAKE 模式下 fixture 脚本需要自己调用
        // generate_photo(见 test/ai_fake_photo.json)。🎬 把值类型请求入队，不经对话循环——
        // 两点采集 + 确认 Modal 是强 UI 流程；FRAME owner 会在下一个 update() tick 执行请求。
        // generate_video 工具仍在 main-thread drain 里直调同一套 MediaManager 状态机。
        if (submitted && core)
        {
            core->submit(submitText);
            _preparedTemplateStatus.clear();
        }
    }
    ImGui::End();

    if (video.statusBanner.visible)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, earthui::design::kDanger);
        ImGui::TextWrapped(u8"视频状态：%s", video.statusBanner.message.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        if (ImGui::SmallButton(u8"关闭提示") && media)
        {
            earthai::VideoUiRequest request;
            request.kind = earthai::VideoUiRequest::DismissStatus;
            media->enqueueVideoRequest(request);
        }
    }

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);

    // ---- 时空影像工作台：图像与视频共享当前视角、时空、风格和科学边界。----
    // 入口只打开工作台，不再把一个隐藏默认提示直接提交给模型；用户能在产生费用前看到
    // 每个默认值。真正的媒体请求仍通过值类型队列交给 FRAME owner，draw traversal 不碰
    // 相机或 MediaManager 的 live 状态。
    if (_cinematicStudioOpen &&
        !ImGui::IsPopupOpen(u8"时空影像工作台"))
        ImGui::OpenPopup(u8"时空影像工作台");

    const earthui::BoundedWindowLayout studioLayout =
        earthui::computeCenteredModalLayout(
            io.DisplaySize.x, io.DisplaySize.y, 680.0f, 800.0f);
    ImGui::SetNextWindowPos(
        ImVec2(studioLayout.x, studioLayout.y), ImGuiCond_Always,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(
        ImVec2(studioLayout.width, studioLayout.maxHeight),
        ImGuiCond_Always);
    bool keepCinematicStudioOpen = _cinematicStudioOpen;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 16.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, earthui::design::kCarbon);
    ImGui::PushStyleColor(ImGuiCol_Border, earthui::design::kBorder);
    if (ImGui::BeginPopupModal(
            u8"时空影像工作台", &keepCinematicStudioOpen,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings))
    {
#if defined(OSGSOL_UI_AUDIT_HOOKS)
        _auditSnapshot.cinematicStudioVisible = true;
#endif
        ImGui::PushStyleColor(ImGuiCol_Text, earthui::design::kCyan);
        ImGui::TextUnformatted(u8"CINEMATIC EARTH / 时空影像");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled(u8"每次生成独立 · 当前画面是唯一几何锚点");

        const float tabGap = ImGui::GetStyle().ItemSpacing.x;
        const float tabWidth =
            (ImGui::GetContentRegionAvail().x - tabGap) * 0.5f;
        if (_cinematicMediaKind == earthai::CINEMATIC_IMAGE)
            ImGui::PushStyleColor(ImGuiCol_Button, earthui::design::kOxblood);
        if (ImGui::Button(u8"图像生成", ImVec2(tabWidth, 32.0f)))
        {
            _cinematicMediaKind = earthai::CINEMATIC_IMAGE;
            _cinematicMotion = earthai::CINEMATIC_MOTION_STATIC;
            if (_cinematicVisualStyle == earthai::CINEMATIC_STYLE_ULTRA_REAL)
                _cinematicVisualStyle = earthai::CINEMATIC_STYLE_SCIENTIFIC;
        }
        if (_cinematicMediaKind == earthai::CINEMATIC_IMAGE)
            ImGui::PopStyleColor();
        ImGui::SameLine();
        if (_cinematicMediaKind == earthai::CINEMATIC_VIDEO)
            ImGui::PushStyleColor(ImGuiCol_Button, earthui::design::kOxblood);
        if (ImGui::Button(u8"视频生成", ImVec2(tabWidth, 32.0f)))
        {
            _cinematicMediaKind = earthai::CINEMATIC_VIDEO;
            if (_cinematicMotion == earthai::CINEMATIC_MOTION_STATIC)
                _cinematicMotion = earthai::CINEMATIC_MOTION_AERIAL_TOUR;
            if (_cinematicVisualStyle == earthai::CINEMATIC_STYLE_SCIENTIFIC)
                _cinematicVisualStyle = earthai::CINEMATIC_STYLE_ULTRA_REAL;
        }
        if (_cinematicMediaKind == earthai::CINEMATIC_VIDEO)
            ImGui::PopStyleColor();
        const bool localDeterministicOrbit =
            _cinematicMediaKind == earthai::CINEMATIC_VIDEO &&
            earthai::cinematicMotionUsesDeterministicLocalRenderer(
                static_cast<earthai::CinematicCameraMotion>(_cinematicMotion));
        if (localDeterministicOrbit)
        {
            const earthai::CinematicGenerationSettings normalized =
                earthai::normalizedCinematicSubmissionSettings(
                    earthai::CinematicGenerationSettings{
                        earthai::CINEMATIC_VIDEO,
                        static_cast<earthai::CinematicEra>(_cinematicEra),
                        static_cast<earthai::CinematicLocalTime>(_cinematicLocalTime),
                        static_cast<earthai::CinematicVisualStyle>(_cinematicVisualStyle),
                        earthai::CINEMATIC_MOTION_ORBIT_360});
            _cinematicEra = normalized.era;
            _cinematicLocalTime = normalized.localTime;
            _cinematicVisualStyle = normalized.visualStyle;
            _cinematicPrompt[0] = '\0';
            _cinematicCustomEra[0] = '\0';
            _cinematicCustomTime[0] = '\0';
            _cinematicCustomStyle[0] = '\0';
        }
        ImGui::PushStyleColor(ImGuiCol_Text, earthui::design::kTextDim);
        ImGui::TextWrapped(
            _cinematicMediaKind == earthai::CINEMATIC_IMAGE
                ? u8"付费 provider：Nano Banana 2 · 2K · 预计数十秒 · 参考约 US$0.04/张（非实时保证；以 provider 当前账单为准）。"
                : (localDeterministicOrbit
                    ? u8"本地 360° 渲染当前 osgSol 场景：零 AI 费用、零网络请求；不调用 Nano Banana、Omni 或 Veo。录制时按 Esc 可立即停止。"
                    : u8"付费 provider 视频：Nano Banana 2 首帧 + 当前 Omni/Veo 模型 · 默认 8 秒 · 预计数十秒至数分钟 · 参考约 US$1–6（非实时保证；以 provider 当前账单为准）。确认前取消不会发请求；提交后费用可能无法撤销。"));
        ImGui::PopStyleColor();

        ImGui::BeginChild(
            "##cinematic_studio_scroll",
            ImVec2(0.0f, -52.0f), false);

        ImGui::SeparatorText(u8"当前视角");
        if (mani)
        {
            const earthai::PhotoCameraContext camera = media
                ? media->cinematicCameraContext() : earthai::PhotoCameraContext();
            const osg::Vec3d eye = camera.cameraEyeLla;
            ImGui::Text(u8"目标中心 %.4f°  %.4f° · 相机 %.4f°  %.4f° · 高度 %.2f km",
                        osg::RadiansToDegrees(camera.viewTargetLla[0]),
                        osg::RadiansToDegrees(camera.viewTargetLla[1]),
                        osg::RadiansToDegrees(eye[0]), osg::RadiansToDegrees(eye[1]),
                        eye[2] / 1000.0);
            ImGui::Text(u8"航向 %.1f° · 离天底 %.1f° · 垂直 FOV %.1f° · 比例 %.3f",
                        camera.headingDeg, camera.offNadirDeg, camera.verticalFovDeg,
                        camera.aspectRatio);
            ImGui::PushStyleColor(ImGuiCol_Text, earthui::design::kSuccess);
            ImGui::TextWrapped(
                u8"提交时冻结中心、方向、俯仰、视场与比例；不会移动地球。");
            ImGui::PopStyleColor();
        }
        else
            ImGui::TextColored(
                earthui::design::kDanger, u8"当前相机不可用，无法生成");

        if (ImGui::Button(u8"恢复此模式默认设置"))
        {
            const earthai::CinematicGenerationSettings defaults =
                _cinematicMediaKind == earthai::CINEMATIC_IMAGE
                    ? earthai::defaultImageCinematicSettings()
                    : earthai::defaultVideoCinematicSettings();
            const earthai::CinematicGenerationSettings appliedDefaults =
                localDeterministicOrbit
                    ? earthai::normalizedCinematicSubmissionSettings(
                        earthai::CinematicGenerationSettings{
                            earthai::CINEMATIC_VIDEO,
                            defaults.era, defaults.localTime,
                            defaults.visualStyle,
                            earthai::CINEMATIC_MOTION_ORBIT_360})
                    : defaults;
            _cinematicEra = appliedDefaults.era;
            _cinematicLocalTime = appliedDefaults.localTime;
            _cinematicVisualStyle = appliedDefaults.visualStyle;
            _cinematicMotion = localDeterministicOrbit
                ? earthai::CINEMATIC_MOTION_ORBIT_360 : appliedDefaults.motion;
            _cinematicPrompt[0] = '\0';
            _cinematicCustomEra[0] = '\0';
            _cinematicCustomTime[0] = '\0';
            _cinematicCustomStyle[0] = '\0';
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(localDeterministicOrbit
                ? u8"本地环拍锁定为当代当前光照、科学写实、8 秒和静音 MP4"
                : u8"图像默认科学写实；视频默认超写实航拍；时代和时间恢复为当前");

        if (localDeterministicOrbit) ImGui::BeginDisabled();
        ImGui::SeparatorText(u8"提示词");
        ImGui::PushStyleColor(ImGuiCol_Text, earthui::design::kTextDim);
        ImGui::TextWrapped(
            u8"补充季节、天气、叙事或禁止内容；地点和镜头仍以当前视角为准。");
        ImGui::PopStyleColor();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextMultiline(
            "##cinematic_prompt", _cinematicPrompt,
            sizeof(_cinematicPrompt), ImVec2(-1.0f, 50.0f));
#if defined(OSGSOL_UI_AUDIT_HOOKS)
        _auditSnapshot.cinematicPrompt = auditLastItemRect();
#endif

        const auto drawPresetGrid = [&](const char* id,
                                        const std::vector<const char*>& labels,
                                        int& selected,
                                        int auditSlot) {
            const float gap = ImGui::GetStyle().ItemSpacing.x;
            const float width = std::max(
                88.0f, (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f);
            ImGui::PushID(id);
            for (std::size_t index = 0; index < labels.size(); ++index)
            {
                if (index > 0 && index % 3 != 0) ImGui::SameLine();
                const bool active = selected == static_cast<int>(index);
                if (active)
                {
                    ImGui::PushStyleColor(ImGuiCol_Button, earthui::design::kOxblood);
                    ImGui::PushStyleColor(ImGuiCol_Text, earthui::design::kTextStrong);
                }
                if (ImGui::Button(labels[index], ImVec2(width, 30.0f)))
                    selected = static_cast<int>(index);
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                if (index == 0)
                {
                    if (auditSlot == 0)
                        _auditSnapshot.cinematicEraPreset = auditLastItemRect();
                    else if (auditSlot == 1)
                        _auditSnapshot.cinematicTimePreset = auditLastItemRect();
                    else if (auditSlot == 2)
                        _auditSnapshot.cinematicStylePreset = auditLastItemRect();
                    else if (auditSlot == 3)
                        _auditSnapshot.cinematicMotionPreset = auditLastItemRect();
                }
#else
                (void)auditSlot;
#endif
                if (active) ImGui::PopStyleColor(2);
            }
            ImGui::PopID();
        };

        ImGui::SeparatorText(u8"时代");
        const std::vector<const char*> eraLabels = {
            earthai::cinematicEraLabel(earthai::CINEMATIC_ERA_PRESENT),
            earthai::cinematicEraLabel(earthai::CINEMATIC_ERA_1920S),
            earthai::cinematicEraLabel(earthai::CINEMATIC_ERA_CAMBRIAN_CHENGJIANG),
            earthai::cinematicEraLabel(earthai::CINEMATIC_ERA_CUSTOM)};
        drawPresetGrid("era", eraLabels, _cinematicEra, 0);
        if (_cinematicEra == earthai::CINEMATIC_ERA_CUSTOM)
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##cinematic_custom_era", u8"例如：公元 1750 年 / 晚白垩世",
                _cinematicCustomEra, sizeof(_cinematicCustomEra));
        }

        ImGui::SeparatorText(u8"当地时间");
        const std::vector<const char*> timeLabels = {
            earthai::cinematicTimeLabel(earthai::CINEMATIC_TIME_AUTO),
            earthai::cinematicTimeLabel(earthai::CINEMATIC_TIME_DAWN),
            earthai::cinematicTimeLabel(earthai::CINEMATIC_TIME_NOON),
            earthai::cinematicTimeLabel(earthai::CINEMATIC_TIME_1900),
            earthai::cinematicTimeLabel(earthai::CINEMATIC_TIME_NIGHT),
            earthai::cinematicTimeLabel(earthai::CINEMATIC_TIME_CUSTOM)};
        drawPresetGrid("time", timeLabels, _cinematicLocalTime, 1);
        if (_cinematicLocalTime == earthai::CINEMATIC_TIME_CUSTOM)
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##cinematic_custom_time", u8"例如：当地 19:35 / 日出前 20 分钟",
                _cinematicCustomTime, sizeof(_cinematicCustomTime));
        }

        ImGui::SeparatorText(u8"表现风格");
        const std::vector<const char*> styleLabels = {
            earthai::cinematicStyleLabel(earthai::CINEMATIC_STYLE_SCIENTIFIC),
            earthai::cinematicStyleLabel(earthai::CINEMATIC_STYLE_ULTRA_REAL),
            earthai::cinematicStyleLabel(earthai::CINEMATIC_STYLE_ARCHIVAL_AMBER),
            earthai::cinematicStyleLabel(earthai::CINEMATIC_STYLE_DOCUMENTARY),
            earthai::cinematicStyleLabel(earthai::CINEMATIC_STYLE_CINEMATIC),
            earthai::cinematicStyleLabel(earthai::CINEMATIC_STYLE_ANIME),
            earthai::cinematicStyleLabel(earthai::CINEMATIC_STYLE_CUSTOM)};
        drawPresetGrid("style", styleLabels, _cinematicVisualStyle, 2);
        if (_cinematicVisualStyle == earthai::CINEMATIC_STYLE_CUSTOM)
        {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint(
                "##cinematic_custom_style", u8"描述媒介、色彩、镜头与质感；不能改动视角",
                _cinematicCustomStyle, sizeof(_cinematicCustomStyle));
        }
        if (localDeterministicOrbit) ImGui::EndDisabled();

        if (_cinematicMediaKind == earthai::CINEMATIC_VIDEO)
        {
            ImGui::SeparatorText(u8"运镜");
            const std::vector<const char*> motionLabels = {
                earthai::cinematicMotionLabel(earthai::CINEMATIC_MOTION_AERIAL_TOUR),
                earthai::cinematicMotionLabel(earthai::CINEMATIC_MOTION_ORBIT_360),
                earthai::cinematicMotionLabel(earthai::CINEMATIC_MOTION_DIVE),
                earthai::cinematicMotionLabel(earthai::CINEMATIC_MOTION_CRANE_REVEAL),
                earthai::cinematicMotionLabel(earthai::CINEMATIC_MOTION_TRUCK),
                earthai::cinematicMotionLabel(earthai::CINEMATIC_MOTION_POINT_TO_POINT)};
            int motionIndex = std::max(
                0, _cinematicMotion -
                    static_cast<int>(earthai::CINEMATIC_MOTION_AERIAL_TOUR));
            drawPresetGrid("motion", motionLabels, motionIndex, 3);
            _cinematicMotion = motionIndex +
                static_cast<int>(earthai::CINEMATIC_MOTION_AERIAL_TOUR);
            ImGui::PushStyleColor(ImGuiCol_Text, earthui::design::kTextDim);
            if (_cinematicMotion == earthai::CINEMATIC_MOTION_POINT_TO_POINT)
            {
                if (routeCapabilities.canGenerateProviderVideo() &&
                    !routeCapabilities.supportsLastFrameVideo())
                {
                    ImGui::TextWrapped(
                        u8"当前 Omni 模型不支持首尾帧穿越。请先配置 Veo 首尾帧模型，再记录 A/B 点。");
                }
                else
                {
                    ImGui::TextWrapped(
                        u8"先冻结当前起点；随后移动地球并点击“完成 B 点”，再确认生成。");
                }
            }
            else if (earthai::cinematicMotionRequiresClosureReview(
                         static_cast<earthai::CinematicCameraMotion>(_cinematicMotion)))
            {
                ImGui::TextWrapped(
                    u8"本地录制：不收取 AI 费用，不发起网络请求。"
                    u8"固定当前画面中心 · 8 秒 · 24 fps · 静音 MP4 · 同一条连续物理相机路径。"
                    u8"会保留当前可见的太阳、时间、图层与标注，只隐藏应用 UI；"
                    u8"不做历史、深时、动漫、风格化或生成音频处理。");
            }
            else
            {
                ImGui::TextWrapped(
                    _cinematicMotion == earthai::CINEMATIC_MOTION_AERIAL_TOUR
                        ? u8"从当前首帧开始，到最后一帧保持同一个连续镜头："
                          u8"不切镜、不跳高度、不换机位。"
                        : u8"单个当前首帧即可；确认后由视频模型执行连续运镜。");
            }
            ImGui::PopStyleColor();
        }

        if (_cinematicEra != earthai::CINEMATIC_ERA_PRESENT)
        {
            ImGui::SeparatorText(u8"科学边界");
            ImGui::PushStyleColor(ImGuiCol_Text, earthui::design::kMeasure);
            ImGui::TextWrapped(
                _cinematicEra == earthai::CINEMATIC_ERA_CAMBRIAN_CHENGJIANG
                    ? u8"深时科学重建：现代位置只作镜头锚点；古地理、生态与外观存在显著不确定性。"
                    : u8"历史重建：输出不是发现的档案照片或直接观测；"
                      u8"生成模型仍可能产生时代错置，"
                      u8"发布或分析前必须对照可靠史料核验。 ");
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();

        const bool customEraReady =
            _cinematicEra != earthai::CINEMATIC_ERA_CUSTOM ||
            _cinematicCustomEra[0] != '\0';
        const bool customTimeReady =
            _cinematicLocalTime != earthai::CINEMATIC_TIME_CUSTOM ||
            _cinematicCustomTime[0] != '\0';
        const bool customStyleReady =
            _cinematicVisualStyle != earthai::CINEMATIC_STYLE_CUSTOM ||
            _cinematicCustomStyle[0] != '\0';
        earthai::CinematicGenerationSettings availability =
            _cinematicMediaKind == earthai::CINEMATIC_IMAGE
                ? earthai::defaultImageCinematicSettings()
                : earthai::defaultVideoCinematicSettings();
        availability.mediaKind =
            static_cast<earthai::CinematicMediaKind>(_cinematicMediaKind);
        availability.motion = availability.mediaKind == earthai::CINEMATIC_IMAGE
            ? earthai::CINEMATIC_MOTION_STATIC
            : static_cast<earthai::CinematicCameraMotion>(_cinematicMotion);
        const bool canGenerate = mani && customEraReady && customTimeReady &&
            customStyleReady && earthai::cinematicSubmissionCanStart(
                availability, routeCapabilities);
        const float actionGap = ImGui::GetStyle().ItemSpacing.x;
        const float actionWidth =
            (ImGui::GetContentRegionAvail().x - actionGap) * 0.5f;
        if (!canGenerate) ImGui::BeginDisabled();
        const char* submitLabel =
            _cinematicMediaKind == earthai::CINEMATIC_IMAGE
                ? u8"付费提交 2K 图像（参考 US$0.04）"
                : (localDeterministicOrbit
                    ? u8"开始本地录制"
                    : (_cinematicMotion == earthai::CINEMATIC_MOTION_POINT_TO_POINT
                    ? u8"记录当前起点 A" : u8"从当前视角生成视频"));
        ImGui::PushStyleColor(ImGuiCol_Button, earthui::design::kCyan);
        ImGui::PushStyleColor(ImGuiCol_Text, earthui::design::kCarbon);
        if (ImGui::Button(submitLabel, ImVec2(actionWidth, 34.0f)))
        {
            earthai::CinematicUiRequest request;
            request.settings = _cinematicMediaKind == earthai::CINEMATIC_IMAGE
                ? earthai::defaultImageCinematicSettings()
                : earthai::defaultVideoCinematicSettings();
            request.settings.mediaKind =
                static_cast<earthai::CinematicMediaKind>(_cinematicMediaKind);
            request.settings.era =
                static_cast<earthai::CinematicEra>(_cinematicEra);
            request.settings.localTime =
                static_cast<earthai::CinematicLocalTime>(_cinematicLocalTime);
            request.settings.visualStyle =
                static_cast<earthai::CinematicVisualStyle>(_cinematicVisualStyle);
            request.settings.motion = _cinematicMediaKind == earthai::CINEMATIC_IMAGE
                ? earthai::CINEMATIC_MOTION_STATIC
                : static_cast<earthai::CinematicCameraMotion>(_cinematicMotion);
            request.settings.customEra = _cinematicCustomEra;
            request.settings.customLocalTime = _cinematicCustomTime;
            request.settings.customStyle = _cinematicCustomStyle;
            request.settings.userPrompt = _cinematicPrompt;
            request.settings = earthai::normalizedCinematicSubmissionSettings(
                request.settings);
            if (media) media->enqueueCinematicRequest(request);
#if defined(OSGSOL_UI_AUDIT_HOOKS)
            _auditActionMask |= AUDIT_ACTION_CINEMATIC_SUBMIT;
#endif
            _cinematicStudioOpen = false;
            keepCinematicStudioOpen = false;
            ImGui::CloseCurrentPopup();
        }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
        _auditSnapshot.cinematicSubmitButton = auditLastItemRect();
#endif
        ImGui::PopStyleColor(2);
        if (!canGenerate) ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button(u8"取消", ImVec2(actionWidth, 34.0f)))
        {
#if defined(OSGSOL_UI_AUDIT_HOOKS)
            _auditActionMask |= AUDIT_ACTION_CINEMATIC_CANCEL;
#endif
            _cinematicStudioOpen = false;
            keepCinematicStudioOpen = false;
            ImGui::CloseCurrentPopup();
        }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
        _auditSnapshot.cinematicCancelButton = auditLastItemRect();
#endif
        ImGui::EndPopup();
    }
    if (!keepCinematicStudioOpen) _cinematicStudioOpen = false;
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    // ---- 视频确认 Modal:展示冻结视角、运镜/时空设置与显式计费边界。----
    // 放在 AI 对话条窗口 Begin/End 之外(Modal 是独立的顶层窗口,不依赖对话条是否展开)。
    bool videoUiAvailable = media != nullptr;
#if defined(OSGSOL_UI_AUDIT_HOOKS)
    videoUiAvailable = videoUiAvailable ||
        _auditVideoState == AUDIT_VIDEO_CONFIRM;
#endif
    if (videoUiAvailable)
    {
        if (openVideoModal && !ImGui::IsPopupOpen(u8"确认生成巡航视频"))
        {
            ImGui::OpenPopup(u8"确认生成巡航视频");
        }

        ImGuiIO& ioModal = ImGui::GetIO();
        const earthui::BoundedWindowLayout modal =
            earthui::computeCenteredModalLayout(
                ioModal.DisplaySize.x, ioModal.DisplaySize.y,
                480.0f, 560.0f);
        ImGui::SetNextWindowPos(
            ImVec2(modal.x, modal.y), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(modal.width, 0.0f), ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(modal.width, 0.0f),
            ImVec2(modal.width, modal.maxHeight));
        if (ImGui::BeginPopupModal(u8"确认生成巡航视频", NULL,
                                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
        {
#if defined(OSGSOL_UI_AUDIT_HOOKS)
            _auditSnapshot.videoModalVisible = true;
#endif
            earthai::MediaManager::PendingVideoInfo info = video.pending;
            if (info.ready)
            {
                const bool singleAnchor = info.cinematic && info.singleAnchor;
                const bool localDeterministicOrbit = info.cinematic &&
                    earthai::cinematicMotionUsesDeterministicLocalRenderer(
                        info.settings.motion);
                ImGui::PushStyleColor(
                    ImGuiCol_Text, earthui::design::kCyan);
                ImGui::TextUnformatted(
                    localDeterministicOrbit ? u8"确认本地录制" :
                    (singleAnchor ? u8"当前视角运镜" : u8"两点穿越"));
                ImGui::PopStyleColor();
                ImGui::TextWrapped(
                    singleAnchor
                        ? u8"冻结首帧：纬度 %.4f° 经度 %.4f° 高度 %.1fm"
                        : u8"起点 A：纬度 %.4f° 经度 %.4f° 高度 %.1fm",
                    osg::RadiansToDegrees(info.llaA[0]),
                    osg::RadiansToDegrees(info.llaA[1]), info.llaA[2]);
                if (!singleAnchor)
                {
                    ImGui::TextWrapped(
                        u8"终点 B：纬度 %.4f° 经度 %.4f° 高度 %.1fm",
                        osg::RadiansToDegrees(info.llaB[0]),
                        osg::RadiansToDegrees(info.llaB[1]), info.llaB[2]);
                }
                if (info.cinematic)
                {
                    ImGui::Spacing();
                    const earthai::PhotoCameraContext& anchorCamera =
                        info.anchorCapture.camera;
                    ImGui::TextWrapped(
                        u8"冻结 A 几何：中心 %.4f° %.4f° · 眼点 %.4f° %.4f° %.1fm · 航向 %.1f° · 离天底 %.1f° · FOV %.1f° · 比例 %.3f",
                        osg::RadiansToDegrees(info.anchorCapture.targetLla[0]),
                        osg::RadiansToDegrees(info.anchorCapture.targetLla[1]),
                        osg::RadiansToDegrees(anchorCamera.cameraEyeLla[0]),
                        osg::RadiansToDegrees(anchorCamera.cameraEyeLla[1]),
                        anchorCamera.cameraEyeLla[2], anchorCamera.headingDeg,
                        anchorCamera.offNadirDeg, anchorCamera.verticalFovDeg,
                        anchorCamera.aspectRatio);
                    if (!singleAnchor)
                    {
                        const earthai::PhotoCameraContext& endCamera =
                            info.endCapture.camera;
                        ImGui::TextWrapped(
                            u8"冻结 B 几何：中心 %.4f° %.4f° · 眼点 %.4f° %.4f° %.1fm · 航向 %.1f° · 离天底 %.1f° · FOV %.1f° · 比例 %.3f",
                            osg::RadiansToDegrees(info.endCapture.targetLla[0]),
                            osg::RadiansToDegrees(info.endCapture.targetLla[1]),
                            osg::RadiansToDegrees(endCamera.cameraEyeLla[0]),
                            osg::RadiansToDegrees(endCamera.cameraEyeLla[1]),
                            endCamera.cameraEyeLla[2], endCamera.headingDeg,
                            endCamera.offNadirDeg, endCamera.verticalFovDeg,
                            endCamera.aspectRatio);
                    }
                    ImGui::TextWrapped(
                        u8"运镜：%s    时代：%s    时间：%s    风格：%s",
                        earthai::cinematicMotionLabel(info.settings.motion),
                        earthai::cinematicEraLabel(info.settings.era),
                        earthai::cinematicTimeLabel(info.settings.localTime),
                        earthai::cinematicStyleLabel(info.settings.visualStyle));
                    if (info.settings.era != earthai::CINEMATIC_ERA_PRESENT)
                    {
                        ImGui::PushStyleColor(
                            ImGuiCol_Text, earthui::design::kMeasure);
                        ImGui::TextWrapped(
                            info.settings.era ==
                                earthai::CINEMATIC_ERA_CAMBRIAN_CHENGJIANG
                                ? u8"科学重建，不是直接观测；现代地理仅作为本次镜头锚点。"
                                : u8"历史重建，不是发现的档案影像；提示词会约束时代排除项，发布或分析前仍需专家对照史料核验。 ");
                        ImGui::PopStyleColor();
                    }
                }
                ImGui::Separator();
                if (ImGui::TreeNodeEx(
                        u8"查看完整生成合同",
                        ImGuiTreeNodeFlags_SpanAvailWidth))
                {
                    ImGui::TextWrapped("%s", info.motionPrompt.c_str());
                    ImGui::TreePop();
                }
                ImGui::Separator();
                ImGui::PushStyleColor(
                    ImGuiCol_Text, earthui::design::kMeasure);
                if (localDeterministicOrbit)
                {
                    ImGui::TextWrapped(
                        u8"本地录制：不收取 AI 费用，不发起网络请求。"
                        u8"固定当前画面中心 · 8 秒 · 24 fps · 静音 MP4 · "
                        u8"同一条连续物理相机路径。"
                        u8"录制的是当前可见 osgSol 场景，不生成历史、深时、动漫、"
                        u8"风格化画面或音频。");
                }
                else
                {
                    ImGui::TextWrapped(
                        u8"付费提交：Nano Banana 2 首帧 + 当前视频 provider/model（%s）· 默认 8 秒 · 预计数十秒至数分钟 · 参考约 US$1–6（非实时保证；provider 当前账单为准）。确认前取消不会发请求；远端提交后取消可能无法逆转费用。",
                        media ? media->videoModelLabel().c_str() : "provider model");
                }
                ImGui::PopStyleColor();

                const bool canConfirmVideo = localDeterministicOrbit
                    ? routeCapabilities.canRenderLocalOrbit()
                    : (singleAnchor
                        ? routeCapabilities.canGenerateProviderVideo()
                        : routeCapabilities.canGeneratePointToPoint());
                if (!canConfirmVideo)
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text, earthui::design::kDanger);
                    if (!singleAnchor &&
                        routeCapabilities.videoProvider ==
                            earthai::CINEMATIC_VIDEO_PROVIDER_OMNI)
                    {
                        ImGui::TextWrapped(
                            u8"当前 Omni 模型不支持首尾帧穿越；请切换到受支持的 Veo 模型后重新记录 A/B 点。");
                    }
                    else if (localDeterministicOrbit)
                    {
                        ImGui::TextWrapped(
                            u8"本地 360° 录制编码器不可用，无法开始录制。");
                    }
                    else if (!singleAnchor)
                    {
                        ImGui::TextWrapped(
                            u8"两点穿越需要受支持的 Veo 首尾帧模型，当前路由不可用。");
                    }
                    else
                    {
                        ImGui::TextWrapped(
                            u8"当前视频 provider 路由不可用，无法提交生成。");
                    }
                    ImGui::PopStyleColor();
                }

                const float actionGap = ImGui::GetStyle().ItemSpacing.x;
                const float actionWidth = std::max(
                    96.0f,
                    (ImGui::GetContentRegionAvail().x - actionGap) * 0.5f);
                ImGui::PushStyleColor(
                    ImGuiCol_Button, earthui::design::kCyan);
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered, earthui::design::kTextStrong);
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonActive, earthui::design::kMeasure);
                ImGui::PushStyleColor(
                    ImGuiCol_Text, earthui::design::kCarbon);
                if (!canConfirmVideo) ImGui::BeginDisabled();
                if (ImGui::Button(
                        localDeterministicOrbit ? u8"开始本地录制" : u8"付费确认生成",
                        ImVec2(actionWidth, 0.0f)))
                {
                    if (media)
                    {
                        earthai::VideoUiRequest request;
                        request.kind = earthai::VideoUiRequest::Confirm;
                        media->enqueueVideoRequest(request);
                    }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                    else if (_auditMediaControlsEnabled)
                        _auditActionMask |= AUDIT_ACTION_VIDEO_CONFIRM;
#endif
                }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                _auditSnapshot.videoConfirmButton = auditLastItemRect();
#endif
                if (!canConfirmVideo) ImGui::EndDisabled();
                ImGui::PopStyleColor(4);
                ImGui::SameLine();
                if (ImGui::Button(
                        u8"取消", ImVec2(actionWidth, 0.0f)) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    if (media)
                    {
                        earthai::VideoUiRequest request;
                        request.kind = earthai::VideoUiRequest::Cancel;
                        media->enqueueVideoRequest(request);
                    }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                    else if (_auditMediaControlsEnabled)
                        _auditActionMask |= AUDIT_ACTION_VIDEO_CANCEL;
#endif
                    ImGui::CloseCurrentPopup();
                }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                _auditSnapshot.videoModalCancelButton = auditLastItemRect();
#endif
                if (!video.commandError.empty())
                {
                    ImGui::PushStyleColor(
                        ImGuiCol_Text, earthui::design::kDanger);
                    ImGui::TextWrapped(u8"错误：%s", video.commandError.c_str());
                    ImGui::PopStyleColor();
                }
            }
            else
            {
                // 理论上不会发生(openVideoModal 只在 AWAIT_CONFIRM 时置真),防御一下:
                // 状态已经被别的路径清掉(如按钮的「取消」在同一帧发生)就直接关掉弹窗。
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    _cards.registerCards(cardStack);
}
