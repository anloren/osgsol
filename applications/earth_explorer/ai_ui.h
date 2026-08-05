#ifndef EARTH_AI_UI_H
#define EARTH_AI_UI_H
#include "ai_chat.h"
#include "ai_cards.h"
#include <picojson.h>
#include <string>

namespace earthai { class MediaManager; }
namespace osgVerse { class EarthManipulator; }
namespace earthui { class CardStack; struct EarthUiShellLayout; }

// 底部悬浮 AI 操作条：默认只显示紧凑输入与明确发送按钮；历史按需展开，ScienceEarth
// 模板使用独立弹层，避免二者把地图可视区向上遮住。右上角卡片存储/绘制委托给
// AICardPanel（见 ai_cards.h）。
// core 为 null 时画禁用态输入框（提示设置 EARTH_AI_KEY），不画历史/按钮——对无 AI 场景零干扰。
class AIChatUI
{
public:
#if defined(OSGSOL_UI_AUDIT_HOOKS)
    struct AuditRect
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        bool valid = false;
    };
    struct AuditSnapshot
    {
        AuditRect historyButton;
        AuditRect templateButton;
        AuditRect templatePrimaryAction;
        AuditRect input;
        AuditRect sendButton;
        AuditRect photoButton;
        AuditRect videoButton;
        AuditRect local360Button;
        AuditRect videoStopButton;
        AuditRect videoStatusDismissButton;
        AuditRect videoCancelButton;
        AuditRect videoConfirmButton;
        AuditRect videoModalCancelButton;
        AuditRect cinematicPrompt;
        AuditRect cinematicSubmitButton;
        AuditRect cinematicCancelButton;
        AuditRect cinematicEraPreset;
        AuditRect cinematicTimePreset;
        AuditRect cinematicStylePreset;
        AuditRect cinematicMotionPreset;
        bool historyExpanded = false;
        bool templatePopupVisible = false;
        bool videoModalVisible = false;
        bool cinematicStudioVisible = false;
        bool photoEnabled = false;
    };
    enum AuditVideoState
    {
        AUDIT_VIDEO_IDLE = 0,
        AUDIT_VIDEO_WAIT_B,
        AUDIT_VIDEO_CONFIRM,
        AUDIT_VIDEO_RUNNING,
        AUDIT_VIDEO_FAILURE
    };
    enum AuditAction
    {
        AUDIT_ACTION_PHOTO = 1u << 0,
        AUDIT_ACTION_VIDEO_BEGIN = 1u << 1,
        AUDIT_ACTION_VIDEO_END = 1u << 2,
        AUDIT_ACTION_VIDEO_CONFIRM = 1u << 3,
        AUDIT_ACTION_VIDEO_CANCEL = 1u << 4,
        AUDIT_ACTION_CINEMATIC_SUBMIT = 1u << 5,
        AUDIT_ACTION_CINEMATIC_CANCEL = 1u << 6,
        AUDIT_ACTION_VIDEO_STOP = 1u << 7,
        AUDIT_ACTION_VIDEO_STATUS_DISMISS = 1u << 8,
        AUDIT_ACTION_LOCAL_360_START = 1u << 9
    };
#endif

    AIChatUI();
    // media 为空(未配置 key/EARTH_AI_FAKE_IMG)时📷按钮保持禁用态,其余不受影响。
    // mani(Task 9):🎬 按钮记录 A/B 两点位姿需要读当前相机经纬度/高度
    // (EarthManipulator::computeEyeLatLonHeight());为空时🎬按钮保持禁用态。
    // v0.15-vision:cardStack 用于把 AI 图表/照片/任务卡登记进共享 Card 组件
    // (EarthControlUI 帧末统一 draw() 绘制,见 ui_card.h)。
    void draw(earthai::AIChatCore* core, earthai::MediaManager* media,
              osgVerse::EarthManipulator* mani,
              const earthui::EarthUiShellLayout& shell,
              earthui::CardStack& cardStack);

    // 主线程调用(工具 execute 在 drain 中,与 draw() 同线程,无需加锁——见 ai_cards.cpp 头注释)。
    // 转发给 _cards,保留原有调用方(ai_setup.cpp 的 show_chart 工具)不必改动。
    void pushChart(const picojson::value& spec);

    // 供其它模块(如 Task 8 的 MediaManager)直接推卡片,不必都经 AIChatUI 转发方法。
    AICardPanel* cards() { return &_cards; }

#if defined(OSGSOL_UI_AUDIT_HOOKS)
    const AuditSnapshot& auditSnapshot() const { return _auditSnapshot; }
    void auditSetVideoConfirm(bool enabled)
    {
        _auditVideoState = enabled
            ? AUDIT_VIDEO_CONFIRM : AUDIT_VIDEO_IDLE;
    }
    void auditSetVideoState(AuditVideoState state)
    {
        _auditVideoState = state;
    }
    void auditSetMediaControlsEnabled(bool enabled)
    {
        _auditMediaControlsEnabled = enabled;
    }
    void auditSetLocalOnly(bool enabled) { _auditLocalOnly = enabled; }
    unsigned int auditActionMask() const { return _auditActionMask; }
    void auditClearActions() { _auditActionMask = 0u; }
#endif

private:
    char _inputBuf[1024];      // InputText 缓冲区，提交时转 std::string 再清空
    bool _historyCollapsed;    // 历史面板折叠状态（默认折叠，地图优先）
    size_t _lastEntryCount;    // 上次绘制时的历史条数，用于检测新增条目并自动滚动到底部
    std::string _preparedTemplateStatus; // 已准备的地点/参数摘要，发送前保持可见
    bool _cinematicStudioOpen = false;
    int _cinematicMediaKind = 0;
    int _cinematicEra = 0;
    int _cinematicLocalTime = 0;
    int _cinematicVisualStyle = 0;
    int _cinematicMotion = 1;
    char _cinematicPrompt[1024];
    char _cinematicCustomEra[128];
    char _cinematicCustomTime[64];
    char _cinematicCustomStyle[128];
    AICardPanel _cards;
#if defined(OSGSOL_UI_AUDIT_HOOKS)
    AuditSnapshot _auditSnapshot;
    AuditVideoState _auditVideoState = AUDIT_VIDEO_IDLE;
    bool _auditMediaControlsEnabled = false;
    bool _auditLocalOnly = false;
    unsigned int _auditActionMask = 0u;
#endif
};
#endif
