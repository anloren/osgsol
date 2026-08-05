#include "ai_setup.h"
#include "ai_photo_request.h"
#include "flight_data.h"
#include "ai_ui.h"
#include "ai_media.h"
#include "ai_prompts.h"
#include "earth_config.h"
#include <readerwriter/EarthManipulator.h>
#include <modeling/Math.h>
#include <osg/Notify>
#include <osg/Math>
#include <memory>

// 汇总类工具（get_quakes_summary / get_flights_summary）高度雷同：懒开启图层 → 取
// summaryJson → 解析 → count<=0 时补 loadingNote → 打日志。抽成一个通用小工厂，
// 两处注册只需传各自的 layerId/描述/isEnabled+summaryJson 回调。
static earthai::Tool makeSummaryTool(const std::string& name, const std::string& descCn,
                                     const std::string& layerId, LayerManager* layers,
                                     std::function<std::string()> summaryFn,
                                     std::function<bool()> isEnabledFn)
{
    earthai::Tool t; t.name = name; t.description = descCn;
    t.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
    t.execute = [name, layerId, layers, summaryFn, isEnabledFn](const picojson::value&) {
        if (!isEnabledFn())   // 懒开启,触发抓取
            layers->setEnabled(layerId, true);
        std::string json = summaryFn();
        picojson::value v; std::string perr = picojson::parse(v, json);
        if (!perr.empty() || !v.is<picojson::object>())
        {
            picojson::object err; err["error"] = picojson::value("bad summary json: " + perr);
            return picojson::value(err);
        }
        picojson::object& obj = v.get<picojson::object>();
        double n = obj.count("count") ? obj["count"].get<double>() : 0.0;
        if (n <= 0.0)
            obj["loadingNote"] = picojson::value(
                std::string(u8"图层已自动开启，数据抓取中，请稍后再查询一次"));
        OSG_NOTICE << "[AIChat] " << name << " count=" << (long long)n << std::endl;
        return picojson::value(obj);
    };
    return t;
}

// Esc is installed independently of ImGui. Deterministic local capture intentionally hides all
// UI for clean frames, but the owner queue remains reachable and handles cancellation next FRAME.
class VideoEscapeCancelHandler : public osgGA::GUIEventHandler
{
public:
    explicit VideoEscapeCancelHandler(earthai::MediaManager* media) : _media(media) {}
    bool handle(const osgGA::GUIEventAdapter& event,
                osgGA::GUIActionAdapter&) override
    {
        if (!_media || event.getEventType() != osgGA::GUIEventAdapter::KEYDOWN ||
            event.getKey() != osgGA::GUIEventAdapter::KEY_Escape ||
            _media->videoPhase() == earthai::VIDEO_IDLE)
            return false;
        earthai::VideoUiRequest request;
        request.kind = earthai::VideoUiRequest::Cancel;
        _media->enqueueVideoRequest(request);
        return true;
    }
private:
    earthai::MediaManager* _media;
};

// FRAME drain（工具必须主线程执行；与降水层 FRAME handler 同模式）。
// 同时承担 EARTH_AI_AUTOSUBMIT 的延迟提交：数据类图层（地震/航班）的抓取线程
// 在另一个线程跑，若开局第 0 帧就 submit，AI 工具可能在 fixture/网络数据落地
// 之前就查询到空结果——用 EARTH_AI_AUTOSUBMIT_DELAY_FRAMES（默认 0，立即提交）
// 推迟到第 N 帧再提交，给数据同步（syncIfDirty，主线程 update 遍历）留出时间。
//
// 测试专用:EARTH_AI_VIDEO_AUTOTEST=1(见 update() 里的 _videoAutotest 分支)——headless
// E2E 没法真的点 UI 按钮/在确认 Modal 里点「确认」，用这个钩子在固定帧数程序化地走一遍
// beginVideoCapture → captureVideoEnd → confirmVideo，绕开 UI 但复用完全相同的
// MediaManager 状态机（因此仍然验证了真实状态机，只是跳过了鼠标点击这一步）。
// 只在环境变量设置时激活，不影响正常运行路径。
class AIFrameHandler : public osgGA::GUIEventHandler
{
    earthai::AIChatCore* _core;
    earthai::MediaManager* _media;   // 可为 null(无 key 且无 EARTH_AI_FAKE_IMG/EARTH_AI_FAKE)
    osgVerse::EarthManipulator* _mani;
    std::string _autoSubmitText; int _delayFrames; int _frameCount; bool _fired;
    size_t _loggedEntries = 0;       // autosubmit 模式:已落日志的 transcript 条目数

    // EARTH_AI_AUTOSUBMIT2(测试专用,v0.15-vision 收尾修复验证用):第一条 autosubmit 完全
    // 走完(_fired 且 core 不再 busy)之后,再自动提交第二条用户文本——用于需要"两轮完整的
    // 异步流程依次跑完"的 E2E 场景(例如验证 pushPhoto() 的同类型旧卡自动替换:两次
    // generate_photo 必须分属两个独立 submit,同一轮内背靠背调用会被 MediaManager 的
    // "已有任务在跑"防重入拒绝,见 ai_media.cpp startPhotoJob() 的 _state != IDLE 检查)。
    // 只在环境变量设置时激活,不影响正常运行路径与既有单条 EARTH_AI_AUTOSUBMIT 行为。
    std::string _autoSubmit2Text; bool _fired2;

    bool _videoAutotest;             // EARTH_AI_VIDEO_AUTOTEST=1
    int _videoFrameCount;
    bool _videoBeganA, _videoCapturedB, _videoConfirmed;
public:
    AIFrameHandler(earthai::AIChatCore* c, earthai::MediaManager* media,
                   osgVerse::EarthManipulator* mani,
                   const std::string& autoSubmitText, int delayFrames)
        : _core(c), _media(media), _mani(mani), _autoSubmitText(autoSubmitText), _delayFrames(delayFrames),
          _frameCount(0), _fired(false),
          _fired2(false),
          _videoAutotest(false), _videoFrameCount(0),
          _videoBeganA(false), _videoCapturedB(false), _videoConfirmed(false)
    {
        const char* env = getenv("EARTH_AI_VIDEO_AUTOTEST");
        _videoAutotest = (env && *env && std::string(env) != "0");
        const char* env2 = getenv("EARTH_AI_AUTOSUBMIT2");
        if (env2 && *env2) _autoSubmit2Text = env2;
    }
    virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter&)
    {
        if (ea.getEventType() == osgGA::GUIEventAdapter::FRAME)
        {
            if (_core) _core->drainMainThread();
            if (_media) _media->update();
            if (_core && !_fired && !_autoSubmitText.empty() && _frameCount >= _delayFrames)
            { _core->submit(_autoSubmitText); _fired = true; }
            if (_core && !_fired) ++_frameCount;

            // 第一条已完全跑完(含其触发的 MediaManager 异步 Job,例如 generate_photo 的
            // 整条快照->生图流水线——不只是 core 不再 busy,还要求 Job 状态机回到 IDLE,
            // 否则第二条 generate_photo 会被 "already running" 拒绝,见上面类头注释)。
            if (_core && _fired && !_fired2 && !_autoSubmit2Text.empty() && !_core->busy()
                && (!_media || _media->photoIdleForTest()))
            { _core->submit(_autoSubmit2Text); _fired2 = true; }

            // autosubmit(headless E2E)模式下把新增会话条目同步落 OSG 日志:
            // GeminiProvider 的错误(如 "HTTP 400: ...")与最终回复文本原本只进 UI 转录,
            // headless 冒烟无从取证——这里补一条日志通道。正常交互(无 autosubmit)不生效。
            if (_core && !_autoSubmitText.empty())
            {
                std::vector<earthai::ChatEntry> ts = _core->transcript();
                static const char* kKinds[] = { "user", "assistant", "tool", "error" };
                for (; _loggedEntries < ts.size(); ++_loggedEntries)
                {
                    const earthai::ChatEntry& e = ts[_loggedEntries];
                    int k = (e.kind >= 0 && e.kind <= 3) ? (int)e.kind : 3;
                    OSG_NOTICE << "[AIChat] transcript/" << kKinds[k] << ": " << e.text << std::endl;
                }
            }

            // ---- EARTH_AI_VIDEO_AUTOTEST 钩子(测试专用,仅当环境变量设置时激活)----
            if (_videoAutotest && _media && _mani)
            {
                ++_videoFrameCount;
                if (!_videoBeganA && _videoFrameCount >= 60)
                {
                    osg::Vec3d llaA = _mani->computeEyeLatLonHeight();
                    _media->beginVideoCapture(llaA);
                    _videoBeganA = true;
                }
                else if (_videoBeganA && !_videoCapturedB && _videoFrameCount >= 120)
                {
                    osg::Vec3d llaB = _mani->computeEyeLatLonHeight();
                    if (_media->captureVideoEnd(llaB)) _videoCapturedB = true;
                    // captureVideoEnd 要求 phase==WAIT_B(A 点快照已稳定);若 A 点还没稳定
                    // (WAIT_A),这里会返回 false,下一帧再试——不推进 _videoCapturedB。
                }
                else if (_videoCapturedB && !_videoConfirmed
                         && _media->videoPhase() == earthai::VIDEO_AWAIT_CONFIRM)
                {
                    // 绕过确认 Modal(headless 无法点击),直接调 confirmVideo() ——
                    // 与真实 UI 按钮调用的是同一个函数,状态机本身没有被绕过。
                    picojson::value r = _media->confirmVideo();
                    OSG_NOTICE << "[AIChat] video autotest confirm -> " << r.serialize() << std::endl;
                    _videoConfirmed = true;
                }
            }
        }
        return false;
    }
};

AIChatRuntime configureAIChat(const AIChatDeps& deps)
{
    osgViewer::Viewer& viewer = *deps.viewer;
    osgVerse::EarthManipulator* mani = deps.mani;
    LayerManager* layerMgr = deps.layers;
    FlightLayer* flightLayer = deps.flights;
    AIChatUI* ui = deps.ui;
    AIChatRuntime runtime;
    // ---- AI Chat（spec: docs/superpowers/specs/2026-07-02-earth-ai-chat-design.md）----
    // 无 EARTH_AI_KEY 且无 EARTH_AI_FAKE → aiCore 为空，后续全部跳过，零影响。
    // registry/aiCore 用裸指针、进程生命周期存活，与本文件其余单例/裸 new 风格一致。
    earthai::ToolRegistry* aiRegistry = new earthai::ToolRegistry;
    earthai::AIChatCore* aiCore = nullptr;
    std::shared_ptr<earthai::PhotoViewGate> photoViewGate(new earthai::PhotoViewGate);
    runtime.tools = aiRegistry;   // 无条件暴露:FeedLayer 等调用方不需要关心 aiCore 是否创建
    runtime.context = std::make_shared<earthai::EarthContextHub>();
    aiRegistry->add(earthai::makeEarthContextTool(runtime.context));
    earthai::ToolRegistry* capabilityRegistry = aiRegistry;
    runtime.context->upsertSection(
        "capabilities", "backend_capabilities", 70,
        [capabilityRegistry]()
        {
            picojson::array tools;
            for (const earthai::Tool& tool : capabilityRegistry->tools())
            {
                picojson::object item;
                item["name"] = picojson::value(tool.name);
                item["description"] = picojson::value(tool.description);
                tools.push_back(picojson::value(item));
            }
            picojson::object result;
            result["toolCount"] =
                picojson::value(static_cast<double>(tools.size()));
            result["tools"] = picojson::value(tools);
            return picojson::value(result);
        });

    // MediaManager owns both paid media and the deterministic local recorder. The latter
    // must be available without an AI key, so the manager exists for every Earth session;
    // only provider requests consult the key or fake-provider configuration.
    std::string aiKeyForMedia = earthcfg::resolveKey("EARTH_AI_KEY");
    earthai::MediaManager* mediaMgr = new earthai::MediaManager(
        &viewer, ui ? ui->cards() : nullptr, aiKeyForMedia, mani,
        deps.captureCamera, deps.captureImage);
    runtime.media = mediaMgr;

    {
        earthai::Tool fly; fly.name = "fly_to";
        fly.description = u8"只把相机飞到指定经纬度与高度，不拍照。用户说地名时你自己换算经纬度。"
            u8"若用户要对目标拍照，for_photo 必须为 true；到达后停止本轮，等待用户选好视角再明确拍照。";
        fly.parametersJson = "{\"type\":\"object\",\"properties\":{"
            "\"lat\":{\"type\":\"number\"},\"lon\":{\"type\":\"number\"},"
            "\"alt_km\":{\"type\":\"number\",\"description\":\"default 150; explicit value wins\"},"
            "\"for_photo\":{\"type\":\"boolean\",\"description\":\"目标拍照导航必须为 true;"
            "省略 alt_km 时使用 2km 地面近景\"}},"
            "\"required\":[\"lat\",\"lon\"]}";
        fly.execute = [mani, photoViewGate](const picojson::value& a) {
            double lat = a.get("lat").get<double>(), lon = a.get("lon").get<double>();
            // v0.15-vision:默认高度从 50km 提到 150km——50km 相对地球半径 6371km 几乎贴地,
            // 画面容易被地表纹理占满(用户真机反馈);150km 能看到明显地表起伏和区域轮廓。
            double alt = earthai::photoFlyAltitudeKm(a);
            if (lat < -90 || lat > 90 || lon < -180 || lon > 180 || alt <= 0.0)
            {
                picojson::object err;
                err["error"] = picojson::value("out of range: lat[-90,90] lon[-180,180] alt_km>0");
                return picojson::value(err);
            }
            mani->setByEye(osg::inDegrees(lat), osg::inDegrees(lon), alt * 1000.0);
            photoViewGate->recordFlyTo();
            OSG_NOTICE << "[AIChat] fly_to " << lat << "," << lon << "," << alt << "km" << std::endl;
            picojson::object r;
            r["ok"] = picojson::value(true);
            r["lat"] = picojson::value(lat);
            r["lon"] = picojson::value(lon);
            r["alt_km"] = picojson::value(alt);
            if (a.contains("for_photo") && a.get("for_photo").is<bool>()
                && a.get("for_photo").get<bool>())
            {
                r["photo_status"] = picojson::value(std::string("awaiting_user_view_confirmation"));
                r["note"] = picojson::value(std::string(
                    u8"目标已显示。停止本轮拍摄；请用户调整/确认视角后，再点击照片或发出下一条拍照指令"));
            }
            return picojson::value(r);
        };
        aiRegistry->add(fly);

        earthai::Tool setLayer; setLayer.name = "set_layer";
        std::vector<earthai::LayerToolPromptEntry> layerPromptEntries;
        const std::vector<OverlayLayer> registeredLayers =
            layerMgr->layersSnapshot();
        for (const OverlayLayer& layer : registeredLayers)
        {
            if (!layer.apply) continue;
            earthai::LayerToolPromptEntry entry;
            entry.id = layer.id;
            entry.displayName = layer.displayName;
            entry.supportsOpacity = layer.hasOpacity;
            layerPromptEntries.push_back(entry);
        }
        setLayer.description =
            earthai::buildSetLayerToolDescription(layerPromptEntries);
        setLayer.parametersJson = "{\"type\":\"object\",\"properties\":{"
            "\"id\":{\"type\":\"string\"},\"enabled\":{\"type\":\"boolean\"},"
            "\"opacity\":{\"type\":\"number\"}},\"required\":[\"id\",\"enabled\"]}";
        LayerManager* lmptr = layerMgr;
        setLayer.execute = [lmptr](const picojson::value& a) {
            std::string id = a.get("id").get<std::string>();
            bool enabled = a.get("enabled").get<bool>();
            const std::vector<OverlayLayer> all = lmptr->layersSnapshot();
            const OverlayLayer* l = nullptr;
            for (size_t i = 0; i < all.size(); ++i)
                if (all[i].id == id) { l = &all[i]; break; }
            if (!l)
            {
                std::string avail;
                for (size_t i = 0; i < all.size(); ++i) avail += (i ? "," : "") + all[i].id;
                picojson::object err;
                err["error"] = picojson::value("unknown layer id " + id + ", available: " + avail);
                return picojson::value(err);
            }
            if (!l->apply)   // "base" 底图常开、无开关动作
            {
                picojson::object err; err["error"] = picojson::value("layer not togglable");
                return picojson::value(err);
            }
            float opacity = l->opacity;
            lmptr->setEnabled(id, enabled);
            if (a.contains("opacity") && l->hasOpacity)
            {
                opacity = osg::clampBetween((float)a.get("opacity").get<double>(), 0.0f, 1.0f);
                lmptr->setOpacity(id, opacity);
            }
            OSG_NOTICE << "[AIChat] set_layer " << id << " " << (enabled ? "on" : "off") << std::endl;
            picojson::object r;
            r["ok"] = picojson::value(true); r["layer"] = picojson::value(id);
            r["enabled"] = picojson::value(enabled);
            r["opacity"] = picojson::value((double)opacity);
            return picojson::value(r);
        };
        aiRegistry->add(setLayer);

        earthai::Tool viewState; viewState.name = "get_view_state";
        viewState.description = u8"读取当前相机的经纬度/高度，以及各图层开关状态。"
            u8"单位：lat/lon 为度，alt_km 为千米。";
        viewState.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        osgVerse::EarthManipulator* maniV = mani;
        viewState.execute = [maniV, lmptr](const picojson::value&) {
            // 与 EarthControlUI「相机 Camera」小节同一取值方式：computeEyeLatLonHeight()
            // 返回 (纬度弧度, 经度弧度, 高度米)。
            osg::Vec3d lla = maniV->computeEyeLatLonHeight();
            picojson::object r;
            r["lat"] = picojson::value(osg::RadiansToDegrees(lla[0]));
            r["lon"] = picojson::value(osg::RadiansToDegrees(lla[1]));
            r["alt_km"] = picojson::value(lla[2] / 1000.0);
            picojson::object layersObj;
            const std::vector<OverlayLayer> all = lmptr->layersSnapshot();
            for (size_t i = 0; i < all.size(); ++i)
                layersObj[all[i].id] = picojson::value(all[i].enabled);
            r["layers"] = picojson::value(layersObj);
            OSG_NOTICE << "[AIChat] get_view_state" << std::endl;
            return picojson::value(r);
        };
        aiRegistry->add(viewState);

        // get_quakes_summary 已随 T2 迁移到 FeedLayer 框架:registerUsgsQuakesFeed
        // (feeds/usgs_quakes_feed.cpp)通过 registerFeedLayer 自动注册同名工具，
        // 这里不再重复注册——ToolRegistry 没有去重，重复注册会产生两个同名工具。
        //
        // get_flights_summary：懒开启图层 + 汇总 JSON，用 makeSummaryTool（见上）。
        FlightLayer* fSummaryPtr = flightLayer;
        aiRegistry->add(makeSummaryTool("get_flights_summary",
            u8"查询当前视口内航班数据汇总：航班数量、高度分桶"
            u8"(<2km/2-8km/>=8km)、最快航班(呼号+速度)。"
            u8"数据来自 OpenSky 实时航班流，覆盖范围随相机视口变化（非全球总数）；"
            u8"若航班层尚未开启会自动开启并开始抓取，此时返回的 count 可能为 0，"
            u8"代表数据仍在加载中，可稍后再查询一次。",
            "flights", lmptr,
            [fSummaryPtr]() { return fSummaryPtr ? fSummaryPtr->summaryJson() : std::string("{}"); },
            [fSummaryPtr]() { return fSummaryPtr && fSummaryPtr->isEnabled(); }));

        // show_chart:模型把统计数据整理成 spec,推给右上角图表卡(AIChatUI::pushChart)。
        // execute 就在 AIChatCore::drainMainThread 里跑(主线程),与 UI draw() 同线程,
        // pushChart 内部因此不需要加锁(见 ai_ui.cpp 头注释)。
        earthai::Tool chart; chart.name = "show_chart";
        chart.description = u8"把数据画成图表卡片,显示在屏幕右上角。"
            u8"type 取值 bar(横向条形图)/donut(环形图)/line(折线图)/stat(单个大数字);"
            u8"labels 与 values 两个数组需等长,分别是每项的标签与数值(stat 图只用第一项:"
            u8"values[0] 是大数字,labels[0] 可选,作为副标题)。"
            u8"unit 应填写数值单位，description 简短说明口径、时间或空间范围；"
            u8"适合在用户要求“画个图”“统计一下”“可视化”之类需求,或你自己觉得图表比文字更清楚时调用。";
        chart.parametersJson = "{\"type\":\"object\",\"properties\":{"
            "\"type\":{\"type\":\"string\",\"enum\":[\"bar\",\"donut\",\"line\",\"stat\"]},"
            "\"title\":{\"type\":\"string\"},"
            "\"unit\":{\"type\":\"string\"},"
            "\"description\":{\"type\":\"string\"},"
            "\"labels\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
            "\"values\":{\"type\":\"array\",\"items\":{\"type\":\"number\"}}},"
            "\"required\":[\"type\",\"values\"]}";
        chart.execute = [ui](const picojson::value& args) {
            if (!ui)
            {
                picojson::object err; err["error"] = picojson::value("ui unavailable");
                return picojson::value(err);
            }
            if (!args.is<picojson::object>() || !args.contains("type")
                || !args.get("type").is<std::string>() || !args.contains("values")
                || !args.get("values").is<picojson::array>())
            {
                picojson::object err;
                err["error"] = picojson::value("spec needs string \"type\" and array \"values\"");
                return picojson::value(err);
            }
            std::string t = args.get("type").get<std::string>();
            size_t n = args.get("values").get<picojson::array>().size();
            ui->pushChart(args);
            OSG_NOTICE << "[AIChat] show_chart type=" << t << " items=" << n << std::endl;
            picojson::object r; r["ok"] = picojson::value(true); return picojson::value(r);
        };
        aiRegistry->add(chart);

        // generate_photo:抓当前渲染帧 → 喂给 Gemini 图像模型 → 生成实拍照片,异步 Job,
        // 结果出现在右上角照片卡(见 ai_media.cpp/ai_cards.cpp)。mediaMgr 为空(无 key 也
        // 无 EARTH_AI_FAKE_IMG)时直接报错,不注册也可以,但注册后模型能看到"为什么不行"
        // 比工具压根不存在更利于它跟用户解释。
        earthai::Tool photo; photo.name = "generate_photo";
        photo.description = u8"generate_photo 只拍摄屏幕当前可见视角，绝不移动或重置相机。用户本条指令必须明确"
            u8"要求照片，且当前画面必须在目标附近。若本条指令调用过 fly_to，本工具一定拒绝；必须停止并等待"
            u8"用户看见目标、选好视角后，在下一条拍照指令中再调用。每次仍必须传本次目标 lat/lon，坐标只描述"
            u8"照片地点，不控制快门相机；生成提示里的高度始终来自当前可见相机。"
            u8"show_camera_platform 默认 false；“从 ISS 俯拍/ISS 视角”仍为 false，只有用户明确"
            u8"要求画面中看见空间站、太阳能板或飞行器时才设 true。";
        photo.parametersJson = earthai::photoToolParametersJson();
        earthai::MediaManager* mediaPtr = mediaMgr;
        osgVerse::EarthManipulator* maniPhoto = mani;
        osgViewer::Viewer* viewerPhoto = &viewer;
        photo.execute = [mediaPtr, maniPhoto, viewerPhoto, photoViewGate](const picojson::value& args) {
            if (!mediaPtr)
            {
                // review:mediaMgr 的实际构造条件是"有 EARTH_AI_KEY 或 EARTH_AI_FAKE"(见上面
                // mediaMgr 构造处的注释),不是 EARTH_AI_FAKE_IMG——FAKE_IMG 只是构造好之后
                // "生图这一步跳过网络"的旁路,不影响 mediaMgr 本身是否被 new 出来。文案改成
                // 与真实判定条件一致,避免用户照着报错设了 FAKE_IMG 却发现工具依旧不可用。
                picojson::object err;
                err["error"] = picojson::value(std::string(
                    "media pipeline unavailable (no EARTH_AI_KEY / EARTH_AI_FAKE)"));
                return picojson::value(err);
            }
            earthai::PhotoRequest request;
            std::string requestError;
            if (!earthai::parsePhotoRequest(args, request, requestError))
            {
                picojson::object err; err["error"] = picojson::value(requestError);
                return picojson::value(err);
            }
            if (!maniPhoto)
            {
                picojson::object err; err["error"] = picojson::value("camera unavailable");
                return picojson::value(err);
            }

            if (!viewerPhoto || !viewerPhoto->getCamera())
            {
                picojson::object err;
                err["error"] = picojson::value("camera unavailable");
                return picojson::value(err);
            }
            int viewportWidth = 0, viewportHeight = 0;
            const osg::Viewport* viewport =
                viewerPhoto->getCamera()->getViewport();
            if (viewport)
            {
                viewportWidth = (int)viewport->width();
                viewportHeight = (int)viewport->height();
            }
            const earthai::PhotoCameraContext currentCamera =
                earthai::makePhotoCameraContext(
                    maniPhoto->computeEyeLatLonHeight(),
                    maniPhoto->computeViewPointLatLonHeight(),
                    viewerPhoto->getCamera()->getViewMatrix(),
                    viewerPhoto->getCamera()->getProjectionMatrix(),
                    viewportWidth, viewportHeight);
            const char* gateError = earthai::photoCaptureGateError(
                maniPhoto->isAnimationRunning(), *photoViewGate,
                request.lla, currentCamera);
            if (gateError)
            {
                picojson::object err;
                err["error"] = picojson::value(std::string(gateError));
                return picojson::value(err);
            }
            request.showCameraPlatform = earthai::photoCameraPlatformAllowed(
                *photoViewGate, request.showCameraPlatform);
            picojson::value r = mediaPtr->startPhotoJob(
                request.style, request.lla, request.showCameraPlatform);
            if (r.is<picojson::object>() && r.contains("status")
                && r.get("status").is<std::string>()
                && r.get("status").get<std::string>() == "started")
                photoViewGate->consumePhotoAuthorization();
            OSG_NOTICE << "[AIChat] generate_photo -> " << r.serialize() << std::endl;
            return r;
        };
        aiRegistry->add(photo);

        // generate_video(Task 9):自然语言路径与 🎬 按钮共用同一套两点采集 + 确认 Modal
        // 状态机(MediaManager 的 videoPhase()/beginVideoCapture()/captureVideoEnd()),
        // 工具本身绝不跳过确认弹窗——即使是模型自主调用,也只推进"记录 A 点"/"记录 B 点"
        // 两步,真正提交(花钱)那一步永远要用户在 Modal 里点「确认」(confirmVideo() 由
        // ai_ui.cpp 的 Modal 按钮调用,这里不调用)。
        earthai::Tool video; video.name = "generate_video";
        video.description = u8"生成一段首尾帧巡航视频(Veo):记录当前相机位置为起点 A,"
            u8"提示用户移动相机到目标位置后再次调用本工具记录终点 B,"
            u8"两点都记录后会弹出确认框(涉及费用,约 $2-6),用户确认后才真正开始生成。"
            u8"本工具从不跳过确认框。";
        video.parametersJson = "{\"type\":\"object\",\"properties\":{"
            "\"style\":{\"type\":\"string\",\"description\":\"可选:风格/时代/天气描述,"
            "透传给生图与视频提示词(如“黄昏”“暴风雪”“电影感胶片颗粒”)\"}}}";
        earthai::MediaManager* mediaVideoPtr = mediaMgr;
        osgVerse::EarthManipulator* maniVideo = mani;
        video.execute = [mediaVideoPtr, maniVideo](const picojson::value& args) {
            if (!mediaVideoPtr)
            {
                // review:同上 generate_photo 的文案统一——mediaMgr 是否被构造只看
                // EARTH_AI_KEY / EARTH_AI_FAKE,与 EARTH_AI_FAKE_MP4 无关。
                picojson::object err;
                err["error"] = picojson::value(std::string(
                    "media pipeline unavailable (no EARTH_AI_KEY / EARTH_AI_FAKE)"));
                return picojson::value(err);
            }
            // review:mani 为空时之前会静默用 (0,0,0) 当相机位姿,记录出一个错误的 A/B 点
            // 却不报错——早退更安全,与 mediaVideoPtr 判空的处理方式一致。
            if (!maniVideo)
            {
                picojson::object err;
                err["error"] = picojson::value(std::string("earth manipulator unavailable"));
                return picojson::value(err);
            }
            osg::Vec3d lla = maniVideo->computeEyeLatLonHeight();

            std::string style;
            if (args.is<picojson::object>() && args.contains("style")
                && args.get("style").is<std::string>())
                style = args.get("style").get<std::string>();

            earthai::VideoPhaseKindPublic phase = mediaVideoPtr->videoPhase();
            if (phase == earthai::VIDEO_IDLE)
            {
                bool ok = mediaVideoPtr->beginVideoCapture(lla, style);
                picojson::object r;
                if (!ok) { r["error"] = picojson::value(std::string("failed to start video capture")); return picojson::value(r); }
                r["status"] = picojson::value(std::string("awaiting_second_point"));
                r["note"] = picojson::value(std::string(
                    u8"已记录起点,请移动相机到目标点后点击视频按钮完成,或再次调用本工具"));
                OSG_NOTICE << "[AIChat] generate_video tool -> awaiting_second_point" << std::endl;
                return picojson::value(r);
            }
            if (phase == earthai::VIDEO_WAIT_B)
            {
                bool ok = mediaVideoPtr->captureVideoEnd(lla);
                picojson::object r;
                if (!ok) { r["error"] = picojson::value(std::string("failed to capture second point")); return picojson::value(r); }
                r["status"] = picojson::value(std::string("awaiting_confirmation"));
                r["note"] = picojson::value(std::string(
                    u8"两点已记录,请在弹出的确认框中确认(涉及费用)"));
                OSG_NOTICE << "[AIChat] generate_video tool -> awaiting_confirmation" << std::endl;
                return picojson::value(r);
            }
            // 其它阶段(A 点/B 点还在抓帧稳定中、已等待确认、或已确认在生成中):
            // 明确告知模型当前状态,不做任何状态转换——避免工具在瞬态阶段被重复调用时
            // 产生意外跳转(例如正在等确认时又把 B 点覆盖掉)。
            picojson::object r;
            const char* phaseName =
                (phase == earthai::VIDEO_WAIT_A) ? "capturing_first_point" :
                (phase == earthai::VIDEO_CAPTURING_B) ? "capturing_second_point" :
                (phase == earthai::VIDEO_AWAIT_CONFIRM) ? "awaiting_confirmation" : "running";
            r["status"] = picojson::value(std::string(phaseName));
            r["note"] = picojson::value(std::string(u8"视频流程正在进行中,请稍候或在界面上操作"));
            return picojson::value(r);
        };
        aiRegistry->add(video);
    }
    // 环境变量优先,回退磁盘 keys.env(双击 .app 读不到环境变量,见 earth_config.h)。
    std::string aiKey = earthcfg::resolveKey("EARTH_AI_KEY");
    const char* aiFake = getenv("EARTH_AI_FAKE");
    if (aiFake && *aiFake)
    {
        // 注意:app 启动早期会把 cwd 切到可执行文件目录(见启动日志 "Working directory"),
        // EARTH_AI_FAKE 用相对路径会相对该目录解析而非命令行所在目录——fixture 建议一律
        // 传绝对路径。loadFromFile 失败时下面的 OSG_WARN 把传入路径原样打出来,便于排查。
        earthai::FakeProvider* fp = new earthai::FakeProvider;
        if (!fp->loadFromFile(aiFake)) OSG_WARN << "[AIChat] bad fake script: " << aiFake << std::endl;
        aiCore = new earthai::AIChatCore(fp, aiRegistry);
    }
    else if (!aiKey.empty())
    {
        const char* m = getenv("EARTH_AI_MODEL");
        earthai::GeminiProvider* gp = new earthai::GeminiProvider(
            aiKey, (m && *m) ? m : "gemini-3.5-flash");
        gp->setSystemPrompt(earthai::buildEarthAssistantSystemPrompt());
        aiCore = new earthai::AIChatCore(gp, aiRegistry);
        // 启动信号(不打 key 值,只打长度):证明 AI key 从环境变量或磁盘 keys.env 读到。
        OSG_NOTICE << "[AIChat] provider=gemini, key configured (len=" << aiKey.size() << ")" << std::endl;
    }
    if (aiCore)
    {
        const std::shared_ptr<earthai::EarthContextHub> earthContext =
            runtime.context;
        aiCore->setContextProvider([earthContext]()
        {
            // Stay below AIChatCore's 128 KiB automatic envelope limit.
            // Larger individual sections remain available through
            // get_earth_context(section=...).
            return earthContext->snapshotJson(120u * 1024u);
        });
        aiCore->setSubmitAcceptedCallback(
            [photoViewGate](const std::string& text) { photoViewGate->beginUserTurn(text); });
        // v0.15-vision 收尾修复:MediaManager 早于 aiCore 构造(见上面 mediaMgr 构造处注释——
        // generate_photo/generate_video 工具的 execute 需要先捕获 mediaMgr 指针才能注册进
        // aiCore 构造时吃的 aiRegistry),这里 aiCore 已就绪,补上反向引用,使 MediaManager
        // 的异步生图/生视频 FAILED 分支能把错误呈现进聊天记录(AIChatCore::addErrorNote)。
        // 两个分支(FakeProvider/GeminiProvider)都会走到这个共同的 "if (aiCore)" 块,
        // 因此一次调用同时覆盖两条路径。
        if (mediaMgr) mediaMgr->setChatCore(aiCore);

    }
    const char* autoSubmit = getenv("EARTH_AI_AUTOSUBMIT");   // headless E2E 用
    const char* delayEnv = getenv("EARTH_AI_AUTOSUBMIT_DELAY_FRAMES");
    int delayFrames = (delayEnv && *delayEnv) ? atoi(delayEnv) : 0;
    // Must precede the FRAME handler and remains active while local capture suppresses ImGui.
    if (mediaMgr) viewer.addEventHandler(new VideoEscapeCancelHandler(mediaMgr));
    viewer.addEventHandler(new AIFrameHandler(aiCore, mediaMgr, mani,
        (autoSubmit && *autoSubmit) ? autoSubmit : "", delayFrames));
    runtime.core = aiCore;
    return runtime;
}
