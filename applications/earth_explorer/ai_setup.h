#ifndef EARTH_AI_SETUP_H
#define EARTH_AI_SETUP_H
#include <osg/Camera>
#include <osgViewer/Viewer>
#include "ai_tools.h"
#include "ai_chat.h"
#include "earth_context.h"
#include "LayerManager.h"
class FlightLayer;
class AIChatUI;
namespace osgVerse { class EarthManipulator; }
namespace earthai { class MediaManager; }

// Task 9 PART A 审查意见:configureAIChat 原先用一堆位置参数 + 一个出参指针(outMedia),
// 调用点稍不注意就会传错顺序/漏传出参。改成"入参结构体 + 返回结构体"——
// 入参一目了然(字段名即语义),返回值把 core/media 两个"进程期存活的裸指针"打包
// 一起给调用方,不再需要额外的出参指针。此签名为最终形态。
struct AIChatDeps
{
    osgViewer::Viewer* viewer = nullptr;
    // The fully composed output camera (Earth's cameras[3]), not viewer.getCamera().
    // Media capture installs its stable final-draw dispatcher here before viewer.run().
    osg::Camera* captureCamera = nullptr;
    osgVerse::EarthManipulator* mani = nullptr;
    LayerManager* layers = nullptr;
    // 地震(quakes)不再是本结构体的字段:get_quakes_summary 已随 T2 迁移到 FeedLayer 框架,
    // 由 registerFeedLayer 自动注册,不需要 configureAIChat 单独接一根 QuakeLayer* 指针。
    FlightLayer* flights = nullptr;
    AIChatUI* ui = nullptr;   // show_chart 等要回调它塞右上角卡片;可为 null(对应工具返回 error)
};

struct AIChatRuntime
{
    earthai::AIChatCore* core = nullptr;    // 无 EARTH_AI_KEY/EARTH_AI_FAKE 时为 null=零影响
    // MediaManager 始终存在以支持不带 key 的本地 360° 录制；付费/provider 路径仍要求 core。
    earthai::MediaManager* media = nullptr;
    // 统一工作区上下文总线。无 AI key 时也存在，便于各模块在固定启动顺序内注册；
    // 真正提交模型请求或调用 get_earth_context 时才读取各 section。
    std::shared_ptr<earthai::EarthContextHub> context;
    // registry 与 aiCore 是否创建无关(configureAIChat 内部无条件 new 一份),因此这里
    // 总是非空;FeedLayer(见 feed_layer.h::registerFeedLayer)用它注册 get_<id>_summary
    // 工具——即便用户没配 EARTH_AI_KEY/EARTH_AI_FAKE,工具注册本身也是零成本的(只有真的
    // 被 AI 核心调用才会执行,核心不存在时自然不会被调用)。
    earthai::ToolRegistry* tools = nullptr;
};

// 注册全部 AI 工具并按 EARTH_AI_KEY/EARTH_AI_FAKE 创建对话核心;同时挂 FRAME drain handler
// 与 EARTH_AI_AUTOSUBMIT。返回结构体里的指针进程期存活。
AIChatRuntime configureAIChat(const AIChatDeps& deps);
#endif
