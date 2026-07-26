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
#if defined(OSGSOL_UI_AUDIT_HOOKS)
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
        ImGui::SameLine();
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

        bool photoSubmit = false;
        // openVideoModal 声明在外层函数作用域(见上方),本帧是否需要 OpenPopup
        // (A/B 都就绪、Modal 尚未打开时置真)在下面 if(core) 块里赋值。
        if (core)
        {
            // 照片生成入口（Task 8 接线，见 ai_media.h/MediaManager）；🎬 视频（Task 9）三态按钮。
            // 内置中文字体（ChineseFull 范围）不含 emoji glyph，📷/🎬 会渲染成方块（tofu），
            // 因此用文字标签"照片"/"视频"代替。
            ImGui::SameLine();
            bool photoEnabled = (core && media && !busy);
#if defined(OSGSOL_UI_AUDIT_HOOKS)
            photoEnabled = photoEnabled ||
                (core && _auditMediaControlsEnabled && !busy);
            _auditSnapshot.photoEnabled = photoEnabled;
#endif
            if (!photoEnabled) ImGui::BeginDisabled();
            if (ImGui::Button(u8"照片", ImVec2(52.0f, 0.0f)))
            {
                photoSubmit = true;
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
                ImGui::SetTooltip(media ? u8"生成当前视角实景照片，约 $0.04/张"
                                        : u8"生成照片（需设置 EARTH_AI_KEY）");
            }

            // 🎬 三态：空闲"视频" -> 已录 A"完成B点"(+取消) -> 两点都录完:自动弹确认 Modal。
            bool videoEnabled = (core && media && mani && !busy
                                 && (vphase == earthai::VIDEO_IDLE || vphase == earthai::VIDEO_WAIT_B));
#if defined(OSGSOL_UI_AUDIT_HOOKS)
            videoEnabled = videoEnabled ||
                (core && _auditMediaControlsEnabled && mani && !busy &&
                 (vphase == earthai::VIDEO_IDLE ||
                  vphase == earthai::VIDEO_WAIT_B));
#endif
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
                bool idleEnabled = (core && media && mani && !busy && vphase == earthai::VIDEO_IDLE);
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                idleEnabled = idleEnabled ||
                    (core && _auditMediaControlsEnabled && mani && !busy &&
                     vphase == earthai::VIDEO_IDLE);
#endif
                if (!idleEnabled) ImGui::BeginDisabled();
                if (ImGui::Button(u8"视频", ImVec2(52.0f, 0.0f)) && mani)
                {
                    osg::Vec3d llaA = mani->computeEyeLatLonHeight();
                    earthai::VideoUiRequest request;
                    request.kind = earthai::VideoUiRequest::Begin;
                    request.lla = llaA;
                    if (media) media->enqueueVideoRequest(request);
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                    else if (_auditMediaControlsEnabled)
                        _auditActionMask |= AUDIT_ACTION_VIDEO_BEGIN;
#endif
                }
#if defined(OSGSOL_UI_AUDIT_HOOKS)
                _auditSnapshot.videoButton = auditLastItemRect();
#endif
                if (!idleEnabled) ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                {
                    ImGui::SetTooltip(media && mani
                        ? u8"记录起点 A，移动相机后点「完成B点」生成 8 秒巡航视频（约 $2-6）"
                        : u8"生成视频（需设置 EARTH_AI_KEY / EARTH_AI_FAKE_MP4）");
                }
            }

            // A/B 两点都就绪 -> 打开确认 Modal（本帧只判断是否需要 OpenPopup，实际弹窗内容
            // 在下面统一画,避免 OpenPopup 调用点分散）。
            if (vphase == earthai::VIDEO_AWAIT_CONFIRM) openVideoModal = true;
        }

        // 📷 走对话代理循环（而不是直接调用 MediaManager）：保持"一切能力皆工具"的架构,
        // 且让用户在对话历史里看到这次操作的记录；FAKE 模式下 fixture 脚本需要自己调用
        // generate_photo(见 test/ai_fake_photo.json)。🎬 把值类型请求入队，不经对话循环——
        // 两点采集 + 确认 Modal 是强 UI 流程；FRAME owner 会在下一个 update() tick 执行请求。
        // generate_video 工具仍在 main-thread drain 里直调同一套 MediaManager 状态机。
        if (photoSubmit && core) core->submit(u8"生成一张当前视角的实景照片");
        if (submitted && core)
        {
            core->submit(submitText);
            _preparedTemplateStatus.clear();
        }
    }
    ImGui::End();

    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(4);

    // ---- 视频确认 Modal(Task 9):居中弹窗,展示 A/B 坐标 + 运动提示词预览 + 费用提示。----
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
                ImGui::TextWrapped(
                    u8"起点 A：纬度 %.4f° 经度 %.4f° 高度 %.1fm",
                    osg::RadiansToDegrees(info.llaA[0]),
                    osg::RadiansToDegrees(info.llaA[1]), info.llaA[2]);
                ImGui::TextWrapped(
                    u8"终点 B：纬度 %.4f° 经度 %.4f° 高度 %.1fm",
                    osg::RadiansToDegrees(info.llaB[0]),
                    osg::RadiansToDegrees(info.llaB[1]), info.llaB[2]);
                ImGui::Separator();
                ImGui::TextWrapped("%s", info.motionPrompt.c_str());
                ImGui::Separator();
                ImGui::PushStyleColor(
                    ImGuiCol_Text, earthui::design::kMeasure);
                ImGui::TextWrapped(u8"将提交视频生成(默认 Omni Flash,同步、较快;"
                                   u8"EARTH_AI_VIDEO_MODEL=veo-3.1-* 可换首尾帧穿越模式,约 $1-6/条)。"
                                   u8"此操作计费,确认?");
                ImGui::PopStyleColor();

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
                if (ImGui::Button(u8"确认生成", ImVec2(actionWidth, 0.0f)))
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
