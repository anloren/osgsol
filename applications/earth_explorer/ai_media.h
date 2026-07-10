#ifndef EARTH_AI_MEDIA_H
#define EARTH_AI_MEDIA_H
// 生成式媒体管线(Task 8 生图 + Task 9 首尾帧巡航视频):抓当前渲染帧 → 喂给 Gemini
// 图像模型生成实拍照片,或抓两帧(A/B 两点位姿)喂给 Veo 生成运镜视频。结果推到右上角
// 卡片(AICardPanel)。Job 驱动,主线程每帧 update() 轮询;耗时的 HTTP 调用放独立 worker
// 线程,绝不阻塞渲染主线程(与 AIChatCore 的 worker 模式一致)。
#include "ai_tools.h"
#include <pipeline/Utilities.h>   // EarthAtmosphereOcean(快门补光)
#include "ai_motion.h"
#include "ai_prompts.h"
#include <osg/Vec3d>
#include <osgViewer/Viewer>
#include <osgViewer/ViewerEventHandlers>
#include <picojson.h>
#include <atomic>
#include <deque>
#include <ios>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class AICardPanel;

namespace earthai
{
    class AIChatCore;   // 前置声明:MediaManager 只持有裸指针(setChatCore 注入),不需要完整定义

    // 抓当前帧到 PNG 文件。基于 osgViewer::ScreenCaptureHandler(EARTH_AUTOCAP 同款),
    // 挂到 viewer 上按需触发单帧捕获;写盘由捕获回调在渲染后完成(异步:调用 grab() 之后
    // 要过几帧文件才会出现,ready() 供轮询)。
    // handler 只在构造时创建并 addEventHandler 一次,grab() 复用同一个 handler(只换写盘目标),
    // 避免每次 grab() 都新增一个 handler 导致 viewer 上 handler 无限累积(见 .cpp 注释)。
    class SnapshotGrabber
    {
    public:
        explicit SnapshotGrabber(osgViewer::Viewer* viewer);
        void grab(const std::string& pngPath);   // 触发一次抓帧(覆盖写);同时重置跨帧稳定性状态

        // 真正的跨帧稳定性判断:调用方(MediaManager::update())每帧调一次 ready()。
        // 本次看到的文件大小与"上一次调用 ready() 时"记录的大小相比——只有连续两次不同的
        // update() tick 都测到同一个 >0 大小,才认为写盘已完成。单次调用内部不再做"读两次
        // 比较"那种伪稳定性判断(两次 stat 间隔太短,几乎总能读到同一个值,写到一半也会
        // 误判为稳定)。_lastSize 在 grab() 时清零,避免复用上一次抓帧遗留的大小造成
        // 首次 ready() 就误判稳定。
        bool ready(const std::string& pngPath);
        // 快照就绪后按当前相机视口裁剪(抓帧抓的是整个窗口帧缓冲,可能比渲染视口大,
        // 多出的边缘是未渲染的底色,会污染构图参考)。主线程调用(与 ready 同处),
        // 读图-裁剪-回写同一路径;失败静默保留原图(提示词有 no-borders 兜底)。
        void cropToViewport(const std::string& pngPath);
        // 内容区尺寸(= 应用 --resolution):窗口帧缓冲可能大于渲染内容区(全屏窗口 vs RTT
        // 分辨率),多出部分是未渲染底色。裁剪矩形优先用这里设置的值(左下原点),未设置
        // 则退回相机 viewport。
        void setContentSize(int w, int h) { _contentW = w; _contentH = h; }

    private:
        osgViewer::Viewer* _viewer;
        osg::ref_ptr<osgViewer::ScreenCaptureHandler> _capturer;  // 唯一实例,构造时创建并挂一次
        std::streamsize _lastSize;   // 上一次 ready() 调用时测到的文件大小,0=尚未测到/已重置
        int _contentW = 0, _contentH = 0;   // 裁剪矩形(左下原点),0=未设置
    };

    // Gemini 图像生成 Provider:输入渲染帧 PNG 字节 + 提示词,同步阻塞调用(供 worker 线程用)。
    class GeminiMediaProvider
    {
    public:
        explicit GeminiMediaProvider(const std::string& apiKey);
        // 成功返回生成图 PNG 字节(非空);失败返回空字符串,err 写入原因。key 不进日志。
        std::string generateImage(const std::string& pngBytes, const std::string& prompt,
                                  std::string& err);

    private:
        std::string _apiKey;
    };

    enum VideoPollDisposition
    {
        VIDEO_POLL_RETRY,
        VIDEO_POLL_TERMINAL_ERROR,
        VIDEO_POLL_PARSE_BODY
    };

    inline VideoPollDisposition classifyVideoPollHttp(bool hasResponse, int status)
    {
        if (!hasResponse || status == 429 || (status >= 500 && status < 600))
            return VIDEO_POLL_RETRY;
        if (status != 200) return VIDEO_POLL_TERMINAL_ERROR;
        return VIDEO_POLL_PARSE_BODY;
    }

    // Veo 首尾帧视频 Provider:输入 A/B 两帧 PNG 字节 + 运动提示词,提交长任务拿到
    // operation 名字;轮询该 operation 直到完成,取回 mp4 字节。两步都同步阻塞,
    // 供独立的轮询 worker 线程调用(不得在主线程调,轮询要跨越数十秒到数分钟)。
    class VeoVideoProvider
    {
    public:
        VeoVideoProvider(const std::string& apiKey, const std::string& model);

        // 提交生成请求(predictLongRunning),成功返回 operation 名字(如
        // "models/veo-3.1-generate-001/operations/xxxx"),失败返回空串并填 err。
        std::string submit(const std::string& pngBytesA, const std::string& pngBytesB,
                           const std::string& motionPrompt, std::string& err);

        // 轮询一次 operation 状态。done=true 且 mp4Bytes 非空 → 已拿到视频字节;
        // done=true 但 mp4Bytes 为空且 err 非空 → 服务端报错,视为终态失败;
        // done=false → 仍在跑,调用方稍后重试。uri 形式的结果本函数内部会再发一次
        // GET 把字节拉下来(uri 可能需要拼 ?key=,见 .cpp)。
        void poll(const std::string& operationName, bool& done, std::string& mp4Bytes, std::string& err);

    private:
        std::string _apiKey, _model;
    };

    // Omni(Interactions API,/v1beta/interactions)单图+提示词**同步**生视频。
    // 模型名含 "omni" 时 MediaManager::confirmVideo 走此路径而非 Veo predictLongRunning。
    // 官方明确:Omni 不支持首尾帧插值(video interpolation)——B 点画面不上传,
    // 只通过运动提示词参与;要严格首尾帧穿越请 EARTH_AI_VIDEO_MODEL 切回 veo-3.1-*。
    class OmniVideoProvider
    {
    public:
        OmniVideoProvider(const std::string& apiKey, const std::string& model);
        // 同步调用(worker 线程,可能耗时 30-180s):成功返回 true 并填 mp4Bytes;
        // 失败返回 false 并填 err。响应里视频可能是 steps[].content[] 的 base64,
        // 也可能是 output_video.uri(需再 GET 下载),两种形状都处理。
        bool generate(const std::string& firstPngBytes, const std::string& motionPrompt,
                      std::string& mp4Bytes, std::string& err);
    private:
        std::string _apiKey, _model;
    };

    // videoPhase() 的返回类型,给 UI 判断三态按钮/是否弹 Modal 用。放 MediaManager 类外
    // 是因为 ai_ui.cpp 需要在头文件之外看到这个类型名,且 MediaManager 类体内(下方)
    // 就要用到它作为 videoPhase() 的返回类型,必须先声明。
    enum VideoPhaseKindPublic
    {
        VIDEO_IDLE = 0,          // 空闲:按钮显示"视频"
        VIDEO_WAIT_A,            // 已点按钮,A 点快照抓取中(还没稳定)
        VIDEO_WAIT_B,            // A 点已就绪,等待用户移动相机后再次触发 B 点采集
        VIDEO_CAPTURING_B,       // 已触发 B 点采集,B 点快照抓取中
        VIDEO_AWAIT_CONFIRM,     // A/B 都就绪,等待用户在确认 Modal 里点「确认」
        VIDEO_RUNNING            // 已确认,Job 在跑(提交/轮询/下载)
    };

    struct VideoUiRequest
    {
        enum Kind { Begin, CaptureEnd, Confirm, Cancel } kind = Begin;
        osg::Vec3d lla;
        std::string style;
    };

    class VideoUiRequestQueue
    {
    public:
        void push(const VideoUiRequest& request)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _requests.push_back(request);
        }
        std::vector<VideoUiRequest> drain()
        {
            std::lock_guard<std::mutex> lock(_mutex);
            std::vector<VideoUiRequest> result(_requests.begin(), _requests.end());
            _requests.clear();
            return result;
        }
    private:
        std::mutex _mutex;
        std::deque<VideoUiRequest> _requests;
    };

    struct VideoUiDispatchResult
    {
        VideoUiRequest::Kind kind = VideoUiRequest::Begin;
        bool dispatched = false;
        bool succeeded = false;
        std::string error;
    };

    inline std::string reduceVideoCommandError(const std::string& previous,
                                               const VideoUiDispatchResult& result)
    {
        if (!result.dispatched) return previous;
        if (result.kind == VideoUiRequest::Cancel || result.succeeded)
            return std::string();
        return result.error;
    }

    inline std::string reduceVideoOwnerCommandError(const std::string& previous,
                                                    bool succeeded)
    {
        return succeeded ? std::string() : previous;
    }

    // 生成式媒体管线总控:Job 驱动,每帧 update() 由 AIFrameHandler 调用(主线程)。
    // 照片与视频各自只支持"单个 pending 任务"——与真实使用场景(用户点一次等一次)相符,
    // 并发第二个请求会被 startPhotoJob/beginVideoCapture 拒绝,避免状态机复杂化。
    class MediaManager
    {
    public:
        // apiKeyOrEmpty 为空 → 仅在 EARTH_AI_FAKE_IMG/EARTH_AI_FAKE_MP4 设置时可用(离线 E2E);
        // 两者都没有的话 startPhotoJob/confirmVideo 会返回 error(由调用方/工具层处理)。
        //
        // 用户反馈 1(HUD 干净截图)的实现踩坑记录:最初尝试把 finalCamera(render_effects.cpp
        // 里 cameras[3],ImGui 也挂在它上面)的 NodeMask 清零来"隐藏 HUD",结果适得其反——
        // finalCamera 并不是一个纯粹叠在最终画面之上的 HUD 层,它本身就是把 earth+ocean 两个
        // RTT 结果合成为屏幕最终画面的关键相机(见 render_effects.cpp 的 finalVertCode/
        // finalFragCode)。把它的 NodeMask 清零会让 OSG 剔除掉整个合成阶段(该相机整个子树,
        // 包括 ImGui 的 PostDrawCallback,都不会被 cull/draw 遍历到),同时
        // ScreenCaptureHandler(默认 END_FRAME 位置)按"最后一个 NodeMask!=0 的相机"选取
        // 抓帧目标——finalCamera 被剔除后它会退而求其次选中链路更靠前的 RTT 相机,拿到的是
        // 尚未与 ImGui 合成、也尚未做最终合成着色的中间缓冲区,画面明显发暗且构图不对;
        // 更糟的是这个"选中哪个相机"高度依赖调用 captureNextFrame() 那一刻各相机 NodeMask
        // 的瞬时状态,复现时观察到两点巡航视频的 B 点快照偶发仍然带着完整 HUD(A/B 两次
        // grab 之间的极短窗口内 selection 落点不稳定)。
        //
        // 正确做法:完全不碰任何相机的 NodeMask,只让 ImGui 本帧"不画任何窗口内容"——
        // ImGui 的实际绘制通过 EarthControlUI::runInternal()(ui/ImGui.cpp 的
        // ImGuiRenderCallback,在 NewFrame 之后、Render 之前调用)驱动,内容为空则 ImGui
        // draw data 为空、Render() 不产生任何像素,finalCamera 本身完全正常参与合成/被
        // ScreenCaptureHandler 选中,截到的就是"没有 ImGui 内容的真实最终帧"。isHudHidden()
        // 供 EarthControlUI::runInternal() 在最开头查询,为 true 时跳过全部 ImGui::Begin
        // 窗口(含 Earth Control 面板与 AIChatUI 对话条/卡片),但仍然要正常调用
        // NewFrame/Render(由 ImGuiNewFrameCallback/ImGuiRenderCallback 负责,与本类无关)。
        MediaManager(osgViewer::Viewer* viewer, AICardPanel* cards, const std::string& apiKeyOrEmpty);
        ~MediaManager();

        JobManager* jobs() { return &_jobs; }

        // 供 EarthControlUI::runInternal() 每帧查询:true 时本帧不画任何 ImGui 窗口内容。
        // draw traversal 读取、FRAME owner 写入，因此计数必须原子发布。
        bool isHudHidden() const { return _hudHideCount.load() > 0; }

        // 仅测试用(ai_setup.cpp 的 EARTH_AI_AUTOSUBMIT2 钩子):照片状态机是否已回到 IDLE
        // (上一次 generate_photo 的整条快照->生图流水线已经完全跑完,包括 DONE_HANDLED 那
        // 一次收尾 tick)——用来判断"何时可以安全地再触发一次 generate_photo 而不会撞上
        // startPhotoJob() 的 already-running 防重入拒绝"。业务代码不要依赖这个方法。
        bool photoIdleForTest() const { return _state == IDLE; }

        // 快门补光:抓帧期间把 WorldSunDir 临时对准相机(夜面/背光视角否则拍出全黑构图参考,
        // banana 只能纯靠坐标推理)。main 注入 EarthAtmosphereOcean 指针;为空则跳过补光。
        void setEarthUniforms(osgVerse::EarthAtmosphereOcean* e) { _earth = e; }
        void setContentSize(int w, int h) { _grabber.setContentSize(w, h); _videoGrabber.setContentSize(w, h); }
        void applyFillLight();   // 内部:把太阳对准相机(快门补光)

        // v0.15-vision 收尾修复:MediaManager 先于 AIChatCore 构造(ai_setup.cpp 里 generate_photo/
        // generate_video 工具的 execute 需要先捕获 MediaManager 指针,才能注册进 aiCore 构造时
        // 吃的 ToolRegistry),不能在 MediaManager 构造函数参数里直接要 AIChatCore*——用这个
        // setter 由 ai_setup.cpp 在 aiCore 构造完成后补上引用。可为空:没有 EARTH_AI_KEY/
        // EARTH_AI_FAKE 时 aiCore 本身就不存在,此时不会调用本方法,_chatCore 保持
        // nullptr——每个使用处都要 null 检查后再解引用(见 .cpp FAILED 分支)。
        void setChatCore(AIChatCore* core) { _chatCore = core; }

        // generate_photo 工具入口(主线程调用,在 AIChatCore::drainMainThread 的工具执行阶段):
        // 建 Job → 触发抓帧 → 立即返回 {"status":"started","job_id":N}(不等生图完成)。
        // 若已有照片任务在跑,返回 {"error":"photo job already running"}。
        // lla=(纬度弧度,经度弧度,高度米),与 EarthManipulator::computeEyeLatLonHeight() 返回值
        // 约定一致；自然语言工具层要求每次显式提供本次任务自己的目标坐标。
        picojson::value startPhotoJob(const std::string& stylePrompt, const osg::Vec3d& lla,
                                      bool showCameraPlatform = false);

        void update();   // 主线程每帧调:轮询抓帧就绪 → 起工作线程生图/生视频 → 完成后推卡片/收尾 Job

        // ---- 视频两点巡航流程(Task 9):A 点 -> B 点 -> 确认 Modal -> 提交 -> 轮询 -> 完成 ----
        // 状态机见 .cpp VideoPhase 注释。UI(🎬 按钮/确认 Modal)与 generate_video 工具
        // 共用同一套状态,任何入口都必须先过两点采集 + 确认这两步,不允许绕过。

        // 当前视频流程所处阶段,UI(三态按钮/确认 Modal)与 generate_video 工具都据此判断
        // 该进入哪一步——VIDEO_IDLE 即"没有视频任务在跑",调用方按需自行与该值比较
        // (未单独提供 videoBusy() 之类的派生便利函数,phase != VIDEO_IDLE 已经足够直白)。
        VideoPhaseKindPublic videoPhase() const;

        // 记录 A 点(抓快照 + 记相机位姿)。若已有视频任务在跑(非 IDLE)返回 false。
        // style:generate_video 工具的可选风格描述,原样透传给 confirmVideo() 里 banana
        // 生图与 buildVideoPrompt 的 styleSuffix;UI 🎬 按钮走这条路径时不带风格,传空串。
        bool beginVideoCapture(const osg::Vec3d& llaA, const std::string& style = std::string());
        // 记录 B 点。要求当前处于"已录 A、等待 B"阶段,否则返回 false。
        bool captureVideoEnd(const osg::Vec3d& llaB);
        // 两点都已抓到快照文件后才能取(A/B 快照仍在写盘时返回 false)。
        // 供 UI 确认 Modal 展示:A/B 坐标 + 视频提示词预览(buildVideoPrompt 输出,含运镜
        // 语言,不再是纯 buildMotionPrompt 的轨迹句子)+ 状态是否就绪。
        struct PendingVideoInfo
        {
            bool ready = false;      // 两点快照都已就绪,可以画确认 Modal 了
            osg::Vec3d llaA, llaB;
            std::string motionPrompt;   // 展示用的最终视频提示词(buildVideoPrompt 输出;字段名沿用旧称避免波及 ai_ui.cpp 之外的引用)
        };
        struct VideoUiSnapshot
        {
            VideoPhaseKindPublic phase = VIDEO_IDLE;
            PendingVideoInfo pending;
            std::string commandError;
        };
        PendingVideoInfo pendingVideoInfo() const;

        // draw traversal 只提交值类型请求、读取不可变快照；live VideoJob 只由 FRAME update()
        // 以及同属 FRAME owner 的 AI 工具 drain 访问。
        void enqueueVideoRequest(const VideoUiRequest& request) { _videoRequests.push(request); }
        VideoUiSnapshot videoUiSnapshot() const;

        // 用户在确认 Modal 里点「确认生成」:真正建 Job、起 worker 提交 Veo 请求。
        // 要求当前处于 AWAIT_CONFIRM 阶段,否则返回 error(不消费任何状态)。
        picojson::value confirmVideo();
        // 用户点「取消」或按 ESC:整个视频流程清零回 IDLE,不产生任何费用。
        void cancelVideo();

    private:
        enum PendingState { IDLE, WAITING_VIEW_RENDER, WAITING_SNAPSHOT, GENERATING, DONE_HANDLED };

        osgViewer::Viewer* _viewer;
        AICardPanel* _cards;
        std::string _apiKey;
        SnapshotGrabber _grabber;
        JobManager _jobs;
        // v0.15-vision 收尾修复:异步生图/生视频 FAILED 时把错误呈现到聊天记录(见 setChatCore
        // 注释)。可空——每处使用前必须 null 检查。
        AIChatCore* _chatCore = nullptr;

        // ---- HUD 隐藏(用户反馈 1:抓帧不能带上 ImGui 面板/对话条)----
        // _hudHideCount 是引用计数而非布尔:照片流程 + 视频 A 点 + 视频 B 点三条 grab() 路径
        // 可能在极短时间内先后触发(例如视频状态机两次 grab 之间用户又点了照片按钮),用计数
        // 而不是布尔能正确处理"多个抓帧请求重叠"的情况——计数 >0 时 isHudHidden() 为 true,
        // 只有归零那次 EarthControlUI::runInternal() 才恢复画 ImGui 窗口。不再持有任何相机
        // 指针/NodeMask(见构造函数注释:曾经的 NodeMask 方案有严重副作用,已弃用)。
        // hudHide()/hudRestore() 由 FRAME owner 写，isHudHidden() 由 draw traversal 读；原子
        // 计数既消除数据竞争，也让 hudRestore() 可以用 CAS 保证永不下溢。
        std::atomic<int> _hudHideCount;
        osgVerse::EarthAtmosphereOcean* _earth = nullptr;   // 快门补光用(可空)
        osg::Vec3 _savedSunDir;                              // 补光前的太阳方向(恢复用)
        float _savedLabelOpacity = 1.0f;                     // 快门前的标注层透明度(恢复用)
        bool _sunDirSaved = false;
        void hudHide();      // 第一次调用(count 0->1):isHudHidden() 从此开始返回 true
        void hudRestore();   // 最后一次调用(count 1->0):isHudHidden() 恢复返回 false

        // 单个 pending 照片任务的状态机(见类注释:同一时刻只支持一个)。
        PendingState _state;
        int _jobId;
        std::string _snapPath, _genPath, _prompt;
        std::thread _worker;
        bool _workerJoinable;
        unsigned int _viewRenderUpdateTicks;
        int _waitSnapshotTicks;  // 进入 WAITING_SNAPSHOT 后累计的 update() 调用次数,超阈值判超时

        void joinWorkerIfAny();

        // 照片与视频状态机各自保留原有 early return；public update() 顺序调用两个 helper，
        // 再从单一 epilogue 发布本 tick 的视频 UI 快照。
        void updatePhotoInternal();
        void updateVideoInternal();
        void applyVideoOwnerCommandResult(bool succeeded);

        // 安全地把 *_video 重置为初始状态:先 join 掉可能还 joinable 的 worker 线程,
        // 再做 *_video = VideoJob()(move-assign)。std::thread 的 move 赋值要求目标线程
        // 对象此刻不 joinable,否则直接 std::terminate——本函数是 updateVideoInternal()/
        // cancelVideo() 里所有"重置视频状态"的唯一入口,不再各处手写裸的
        // "*_video = VideoJob()",避免遗漏 join 埋雷。
        void resetVideo();

        // ---- 视频状态机内部实现(.cpp 里定义完整 enum VideoPhase,这里只前置声明用到的类型)----
        struct VideoJob;   // .cpp 内定义:review 建议的 PendingJob 提取,视频专用(字段与照片不同,
                           // 独立结构体比硬凑一个通用 PendingJob 更清楚——见 .cpp 头注释)。
        VideoJob* _video;  // 指针以避免本头文件暴露 VideoJob 定义(pimpl 风格,video 专属状态)

        VideoUiRequestQueue _videoRequests;
        std::string _videoCommandError;
        mutable std::mutex _videoSnapshotMutex;
        VideoUiSnapshot _videoSnapshot;

        // 独立的第二个 SnapshotGrabber:照片流程的 _grabber 与视频的 A/B 快照都可能同时
        // "在等待稳定"(用户点了视频 A 点又几乎同时点了照片按钮),共用一个 SnapshotGrabber
        // 会导致 grab() 互相覆盖对方的捕获目标。两条流程完全独立、开销可忽略(只是一个
        // ScreenCaptureHandler),分开更安全。
        SnapshotGrabber _videoGrabber;
    };
}
#endif
