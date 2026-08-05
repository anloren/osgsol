// tests/ai_chat_tests.cpp
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include "../applications/earth_explorer/ai_tools.h"
#include <picojson.h>

// Release 构建带 -DNDEBUG 会吞掉 assert —— 用自定义 CHECK 保证断言永远生效
#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::exit(1); } } while (0)

// ai_chat.cpp 的 httpRequestRetry 依赖 earthcfg::getInt("http.retries")——同 ais_tests.cpp/
// feed_layer_tests.cpp/world_tools_tests.cpp 的既有模式,直接 include 实现文件把符号编译进本单元
// (否则链接期报 earthcfg::getInt 未定义;这里必须先于 ai_chat.cpp 之前 include)。
#include "../applications/earth_explorer/earth_config.cpp"
// ai_chat.cpp 目前没有独立的 CMake 目标可链接进测试可执行文件(NEW_TEST 宏只接受单个源文件),
// 为了不改动测试的链接规则,这里直接 #include 实现文件把它编译进本测试的翻译单元。
#include "../applications/earth_explorer/ai_chat.cpp"
// Task 9:buildMotionPrompt 是 header-only 纯函数(只依赖 osg/Vec3d + string + cmath),
// 不像 ai_media.cpp 那样拖 osgViewer/libhv 等重依赖,可以直接 include 到测试翻译单元。
#include "../applications/earth_explorer/ai_motion.h"
// 提示词工程(用户反馈 2/3 的核心改动):header-only 纯函数,依赖约束与 ai_motion.h 相同,
// 直接 include 到测试翻译单元。
#include "../applications/earth_explorer/ai_prompts.h"
#include "../applications/earth_explorer/ai_cinematic_request.h"
#if __has_include("../applications/earth_explorer/ai_photo_request.h")
#include "../applications/earth_explorer/ai_photo_request.h"
#define OSGSOL_HAS_PHOTO_REQUEST 1
#else
#define OSGSOL_HAS_PHOTO_REQUEST 0
#endif
// 全局键盘闸纯判定逻辑(earth_main 链首 GlobalKeyboardGate 的核心):header-only、
// 零依赖(只有 <set>),直接 include 进本翻译单元做单测。
#include "../applications/earth_explorer/input_gate.h"
// P1 Task 1 测试拆分:earthfeed::/earthgeo::/LayerManager/FeedSelection 相关单测已整体
// 迁往 tests/feed_layer_tests.cpp(osgVerse_Test_Feeds),本文件只留 AI 块。

#ifdef _WIN32
#include <windows.h>
static void usleep(unsigned int usec) { Sleep(usec / 1000 == 0 ? 1 : usec / 1000); }
#else
#include <unistd.h>
#endif

static picojson::value parse(const std::string& s)
{ picojson::value v; picojson::parse(v, s); return v; }

static std::string readWholeFile(const std::string& path)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

static std::string generatePhotoToolBlock()
{
    const std::string source = readWholeFile(
        std::string(OSGVERSE_SOURCE_DIR) + "/applications/earth_explorer/ai_setup.cpp");
    const size_t begin = source.find("earthai::Tool photo; photo.name = \"generate_photo\"");
    const size_t end = source.find("aiRegistry->add(photo);", begin);
    CHECK(begin != std::string::npos);
    CHECK(end != std::string::npos);
    return source.substr(begin, end - begin);
}

static std::string photoCameraUnavailableBlock()
{
    const std::string source = readWholeFile(
        std::string(OSGVERSE_SOURCE_DIR) + "/applications/earth_explorer/ai_media.cpp");
    const size_t updateBegin = source.find(
        "void MediaManager::updatePhotoInternal()");
    const size_t begin = source.find(
        "if (!_viewer || !_viewer->getCamera() || !_photoManipulator)",
        updateBegin);
    const size_t end = source.find("_captureRequest = makePhotoCaptureRequest(", begin);
    CHECK(begin != std::string::npos);
    CHECK(end != std::string::npos);
    return source.substr(begin, end - begin);
}

static std::string photoCapturePromptBlock()
{
    const std::string source = readWholeFile(
        std::string(OSGVERSE_SOURCE_DIR) + "/applications/earth_explorer/ai_media.cpp");
    const size_t begin = source.find("_captureRequest = makePhotoCaptureRequest(");
    const size_t end = source.find("_grabber.cropToViewport", begin);
    CHECK(begin != std::string::npos);
    CHECK(end != std::string::npos);
    return source.substr(begin, end - begin);
}

int main(int, char**)
{
    using namespace earthai;

    // ---- ToolRegistry / JobManager(Task 1)----
    {
        ToolRegistry reg;
        Tool t; t.name = "fly_to"; t.description = u8"飞到指定经纬度";
        t.parametersJson =
            "{\"type\":\"object\",\"properties\":{"
            "\"lat\":{\"type\":\"number\"},\"lon\":{\"type\":\"number\"},"
            "\"alt_km\":{\"type\":\"number\"}},\"required\":[\"lat\",\"lon\"]}";
        bool called = false; double gotLat = 0.0;
        t.execute = [&](const picojson::value& args) {
            called = true; gotLat = args.get("lat").get<double>();
            return parse("{\"ok\":true}");
        };
        reg.add(t);

        // 第二个工具:覆盖 buildDeclarationsJson 的多元素逗号拼接路径
        Tool t2; t2.name = "set_layer"; t2.description = u8"切换图层";
        t2.parametersJson =
            "{\"type\":\"object\",\"properties\":{"
            "\"name\":{\"type\":\"string\"}},\"required\":[\"name\"]}";
        t2.execute = [&](const picojson::value&) { return parse("{\"ok\":true}"); };
        reg.add(t2);

        // 1) declarations JSON 覆盖 name/description/parameters,以及多工具的逗号拼接
        std::string decls = reg.buildDeclarationsJson();
        picojson::value dv; std::string err = picojson::parse(dv, decls);
        CHECK(err.empty());
        picojson::array& arr = dv.get<picojson::array>();
        CHECK(arr.size() == 2);
        CHECK(arr[0].get("name").to_str() == "fly_to");
        CHECK(arr[0].get("parameters").get("required").get<picojson::array>().size() == 2);
        CHECK(arr[1].get("name").to_str() == "set_layer");

        // 2) dispatch:按名执行,拿到结果
        picojson::value result;
        bool ok = reg.dispatch("fly_to", parse("{\"lat\":40.7,\"lon\":-74.0}"), result);
        CHECK(ok);
        CHECK(called);
        CHECK(gotLat > 40.0);
        CHECK(result.get("ok").get<bool>());

        // 3) 未知工具:dispatch 返回 false,result 带 error 字段
        ok = reg.dispatch("nope", parse("{}"), result);
        CHECK(!ok);
        CHECK(result.contains("error"));

        // 4) JobManager:创建→更新→查询
        JobManager jm;
        int id = jm.create("video", u8"生成巡航视频");
        jm.update(id, AIJob::RUNNING, 0.5f, "", "");
        AIJob snap;
        CHECK(jm.get(id, snap));
        CHECK(snap.status == AIJob::RUNNING);
        CHECK(snap.progress > 0.4f);

        // 4b) JobManager::creepProgress(review 修复:视频轮询进度爬升与 worker 写 DONE/FAILED
        // 的竞态)—— RUNNING 时能推进 progress 且返回 true;job 变成 DONE 之后再调用必须
        // 原样保留 DONE、不得被"进度爬升"这种意图之外的调用悄悄拉回 RUNNING,且返回 false
        // 告诉调用方"这次没有生效"。
        CHECK(jm.creepProgress(id, 0.7f) == true);
        CHECK(jm.get(id, snap));
        CHECK(snap.status == AIJob::RUNNING);
        CHECK(snap.progress > 0.65f);

        jm.update(id, AIJob::DONE, 1.0f, "/tmp/out.mp4", "");
        CHECK(jm.creepProgress(id, 0.9f) == false);
        CHECK(jm.get(id, snap));
        CHECK(snap.status == AIJob::DONE);          // 必须仍是 DONE,没有被 creep 打回 RUNNING
        CHECK(snap.progress > 0.99f);                // progress 也没被 creep 的 0.9f 覆盖

        // 不存在的 job id 同样应该安全返回 false,不崩溃。
        CHECK(jm.creepProgress(id + 999, 0.5f) == false);
        std::cout << "ai_tools tests OK\n";
    }

    // ---- AIChatCore 代理循环(FakeProvider 脚本驱动)----
    {
        // 脚本:第1轮请求 fly_to,第2轮给最终文本
        const char* script =
            "[{\"calls\":[{\"name\":\"fly_to\",\"args\":{\"lat\":40.71,\"lon\":-74.0,\"alt_km\":50}}]},"
            " {\"text\":\"已带你飞到纽约\"}]";
        FakeProvider fp; CHECK(fp.loadFromString(script));
        ToolRegistry reg2; double flownLat = 0.0;
        Tool fly; fly.name = "fly_to"; fly.description = "";
        fly.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        fly.execute = [&](const picojson::value& a)
        { flownLat = a.get("lat").get<double>(); return parse("{\"ok\":true}"); };
        reg2.add(fly);

        AIChatCore core(&fp, &reg2);
        core.submit(u8"飞到纽约");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();
        CHECK(flownLat > 40.0);
        std::vector<ChatEntry> ts = core.transcript();
        CHECK(!ts.empty());
        CHECK(ts.back().text == u8"已带你飞到纽约");
        std::cout << "AIChatCore basic agent loop OK\n";
    }

    // ---- 同一模型回复的多个工具调用必须按原顺序全部执行 ----
    {
        const char* script =
            "[{\"calls\":["
            "{\"name\":\"first\",\"args\":{}},"
            "{\"name\":\"second\",\"args\":{}}]},"
            "{\"text\":\"两个工具均已按顺序完成\"}]";
        FakeProvider fp; CHECK(fp.loadFromString(script));
        ToolRegistry registry;
        std::vector<std::string> order;
        Tool first; first.name = "first"; first.description = "";
        first.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        first.execute = [&order](const picojson::value&) {
            order.push_back("first"); return parse("{\"ok\":true}");
        };
        Tool second; second.name = "second"; second.description = "";
        second.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        second.execute = [&order](const picojson::value&) {
            order.push_back("second"); return parse("{\"ok\":true}");
        };
        registry.add(first); registry.add(second);

        AIChatCore core(&fp, &registry);
        core.submit(u8"按顺序做两步");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();

        CHECK(order.size() == 2);
        CHECK(order[0] == "first");
        CHECK(order[1] == "second");
        CHECK(core.transcript().back().text == u8"两个工具均已按顺序完成");
        std::cout << "AIChatCore same-turn tool ordering OK\n";
    }

    // ---- Gemini 3.5 functionCall.id 必须原样配对到 functionResponse.id ----
    {
        struct FunctionIdProvider : public LLMProvider
        {
            int rounds = 0;
            std::string secondRoundContents;

            virtual LLMTurn chat(
                const std::string& contentsJson, const std::string&)
            {
                if (rounds++ == 0)
                {
                    return parseGeminiResponse(
                        "{\"candidates\":[{\"content\":{\"parts\":[{"
                        "\"functionCall\":{\"name\":\"probe\",\"args\":{},"
                        "\"id\":\"call-3-5-abc\"},"
                        "\"thoughtSignature\":\"sig-3-5\"}],"
                        "\"role\":\"model\"}}]}");
                }
                secondRoundContents = contentsJson;
                LLMTurn turn;
                turn.text = u8"工具结果已收到";
                return turn;
            }
        } provider;

        ToolRegistry registry;
        Tool probe;
        probe.name = "probe";
        probe.description = "probe function id round-trip";
        probe.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        probe.execute = [](const picojson::value&) {
            return parse("{\"ok\":true}");
        };
        registry.add(probe);

        AIChatCore core(&provider, &registry);
        core.submit(u8"调用 probe");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();

        picojson::value contents;
        CHECK(picojson::parse(contents, provider.secondRoundContents).empty());
        const picojson::array& turns = contents.get<picojson::array>();
        CHECK(turns.size() == 3);
        const picojson::value& functionCall =
            turns[1].get("parts").get<picojson::array>()[0].get("functionCall");
        const picojson::value& functionResponse =
            turns[2].get("parts").get<picojson::array>()[0].get("functionResponse");
        CHECK(functionCall.get("id").to_str() == "call-3-5-abc");
        CHECK(functionResponse.contains("id"));
        CHECK(functionResponse.get("id").to_str() == "call-3-5-abc");
        std::cout << "Gemini function call id round-trip OK\n";
    }

    // ---- submit while busy 被礼貌忽略 ----
    {
        // 用条件变量门控的 provider:chat() 阻塞到测试显式放行,
        // 保证第二次 submit 一定发生在第一轮 worker 完成之前(无任何时间假设)。
        struct GatedProvider : public LLMProvider
        {
            std::mutex m; std::condition_variable cv; bool released = false;
            virtual LLMTurn chat(const std::string&, const std::string&)
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this]() { return released; });
                LLMTurn t; t.text = u8"完成"; return t;
            }
            void release()
            {
                { std::lock_guard<std::mutex> g(m); released = true; }
                cv.notify_all();
            }
        } gated;

        ToolRegistry reg3;
        AIChatCore core(&gated, &reg3);
        int acceptedSubmits = 0;
        std::string acceptedText;
        core.setSubmitAcceptedCallback([&acceptedSubmits, &acceptedText](const std::string& text) {
            ++acceptedSubmits;
            acceptedText = text;
        });
        core.submit(u8"第一条");
        CHECK(acceptedSubmits == 1);
        CHECK(acceptedText == u8"第一条");
        CHECK(core.busy()); // worker 被门控阻塞,busy 必然还在
        core.submit(u8"第二条(应被忽略)");
        CHECK(acceptedSubmits == 1); // busy 时被拒绝的文本不算新用户轮次
        gated.release(); // 放行第一轮 worker

        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();

        std::vector<ChatEntry> ts = core.transcript();
        bool sawBusyNote = false;
        for (size_t i = 0; i < ts.size(); ++i)
            if (ts[i].kind == ChatEntry::TOOL_NOTE && ts[i].text == u8"上一条还在处理中") sawBusyNote = true;
        CHECK(sawBusyNote);
        // "第二条"文本不应作为独立 USER 条目出现在 transcript 中
        bool sawSecondUserEntry = false;
        for (size_t i = 0; i < ts.size(); ++i)
            if (ts[i].kind == ChatEntry::USER && ts[i].text == u8"第二条(应被忽略)") sawSecondUserEntry = true;
        CHECK(!sawSecondUserEntry);
        std::cout << "AIChatCore busy-submit ignored OK\n";
    }

    // ---- 三轮脚本(calls -> calls -> text)证明多轮循环能跑通 ----
    {
        const char* script =
            "[{\"calls\":[{\"name\":\"step_a\",\"args\":{\"n\":1}}]},"
            " {\"calls\":[{\"name\":\"step_b\",\"args\":{\"n\":2}}]},"
            " {\"text\":\"两步都做完了\"}]";
        FakeProvider fp; CHECK(fp.loadFromString(script));

        ToolRegistry reg4;
        int callOrder[2] = { 0, 0 }; int callCount = 0;
        Tool a; a.name = "step_a"; a.description = "";
        a.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        a.execute = [&](const picojson::value&) { callOrder[callCount++] = 1; return parse("{\"ok\":true}"); };
        Tool b; b.name = "step_b"; b.description = "";
        b.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        b.execute = [&](const picojson::value&) { callOrder[callCount++] = 2; return parse("{\"ok\":true}"); };
        reg4.add(a); reg4.add(b);

        AIChatCore core(&fp, &reg4);
        core.submit(u8"跑两步");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();

        CHECK(callCount == 2);
        CHECK(callOrder[0] == 1);
        CHECK(callOrder[1] == 2);
        std::vector<ChatEntry> ts = core.transcript();
        CHECK(ts.back().text == u8"两步都做完了");
        std::cout << "AIChatCore multi-round agent loop OK\n";
    }

    // ---- FakeProvider 返回 {"error":"boom"} -> ERR 条目 + busy 清除 ----
    {
        const char* script = "[{\"error\":\"boom\"}]";
        FakeProvider fp; CHECK(fp.loadFromString(script));
        ToolRegistry reg5;

        AIChatCore core(&fp, &reg5);
        core.submit(u8"触发错误");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();

        CHECK(!core.busy());
        std::vector<ChatEntry> ts = core.transcript();
        CHECK(ts.back().kind == ChatEntry::ERR);
        CHECK(ts.back().text == "boom");
        std::cout << "AIChatCore error surfaces as ERR entry OK\n";
    }

    // ---- 上限可配置 + 撞限后优雅收尾(无工具最终回答),不再硬报错中断 ----
    {
        // 把上限调小到 3(默认 30,堆到 31 条脚本才能触发不利于测试),用完复位,不影响后续测试。
        earthcfg::setValue("ai.maxRounds", 3.0);

        // 4 个 calls 条目:第 1~3 轮正常执行工具,第 4 轮撞上限;
        // 紧接一个 text 条目,供撞限后自动追加的"无工具"请求消费——
        // 全过程应在同一次 submit 内自动收尾,不需要用户再发一条消息。
        std::string script = "[";
        for (int i = 0; i < 4; ++i)
            script += "{\"calls\":[{\"name\":\"spin\",\"args\":{\"i\":" + std::to_string(i) + "}}]},";
        script += "{\"text\":\"已经拿到足够数据,答案是42\"}]";
        FakeProvider fp; CHECK(fp.loadFromString(script));

        ToolRegistry reg6; int spinCount = 0;
        Tool spin; spin.name = "spin"; spin.description = "";
        spin.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        spin.execute = [&](const picojson::value&) { ++spinCount; return parse("{\"ok\":true}"); };
        reg6.add(spin);

        AIChatCore core(&fp, &reg6);
        core.submit(u8"多轮任务");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();

        CHECK(!core.busy());
        CHECK(spinCount == 3); // 只执行了 3 轮工具,第 4 轮被上限拦下(未执行)

        std::vector<ChatEntry> ts = core.transcript();
        // 不应再出现硬中断的 ERR"工具循环超限"
        bool sawHardErr = false;
        for (size_t i = 0; i < ts.size(); ++i)
            if (ts[i].kind == ChatEntry::ERR && ts[i].text == u8"工具循环超限") sawHardErr = true;
        CHECK(!sawHardErr);

        // 应出现温和提示(TOOL_NOTE,ai_ui.cpp 渲染灰色,非刺眼的 ERR 红色)
        bool sawGentleNote = false;
        for (size_t i = 0; i < ts.size(); ++i)
            if (ts[i].kind == ChatEntry::TOOL_NOTE &&
                ts[i].text.find(u8"已达工具调用上限") != std::string::npos) sawGentleNote = true;
        CHECK(sawGentleNote);

        // 撞限后自动多跑一轮"无工具"请求,拿到了模型基于已有数据给出的真正文本答案
        CHECK(ts.back().kind == ChatEntry::ASSISTANT);
        CHECK(ts.back().text == u8"已经拿到足够数据,答案是42");

        // 校验历史:functionCall 与 functionResponse 数量必须相等(不配对会让真实 Gemini API 400)
        picojson::value hv; std::string herr = picojson::parse(hv, core.historyContentsForTest());
        CHECK(herr.empty());
        const picojson::array& contents = hv.get<picojson::array>();
        size_t nCalls = 0, nResponses = 0;
        for (size_t i = 0; i < contents.size(); ++i)
        {
            const picojson::array& parts = contents[i].get("parts").get<picojson::array>();
            for (size_t j = 0; j < parts.size(); ++j)
            {
                if (parts[j].contains("functionCall")) ++nCalls;
                if (parts[j].contains("functionResponse")) ++nResponses;
            }
        }
        CHECK(nCalls == 4); // 3 轮执行 + 1 轮被拦(仍记入历史)
        CHECK(nCalls == nResponses); // 被拦的那轮由合成 functionResponse 补齐配对

        earthcfg::setValue("ai.maxRounds", 30.0); // 复位默认值,避免影响后续测试
        std::cout << "AIChatCore configurable loop-limit graceful finish OK\n";
    }

    // ---- 工具执行抛异常:drainMainThread 兜底为 error functionResponse,不崩、busy 清零 ----
    {
        // 第 1 轮调会抛异常的工具(模拟真 Gemini 发错参数类型/漏必填 → picojson get() 抛
        // std::runtime_error),第 2 轮模型收到 error 响应后给最终文本正常收尾。
        const char* script =
            "[{\"calls\":[{\"name\":\"boomer\",\"args\":{\"x\":1}}]},"
            " {\"text\":\"工具出错但我还活着\"}]";
        FakeProvider fp; CHECK(fp.loadFromString(script));

        ToolRegistry reg7;
        Tool boomer; boomer.name = "boomer"; boomer.description = "";
        boomer.parametersJson = "{\"type\":\"object\",\"properties\":{}}";
        boomer.execute = [](const picojson::value&) -> picojson::value
        { throw std::runtime_error("boom"); };
        reg7.add(boomer);

        AIChatCore core(&fp, &reg7);
        core.submit(u8"炸一下");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();

        // 1) 不崩 + busy 清零(异常若穿出 drainMainThread,进程早已 terminate 到不了这里)
        CHECK(!core.busy());
        // 2) 后续轮次正常收尾:模型收到 error 响应后给出的文本上屏
        std::vector<ChatEntry> ts = core.transcript();
        CHECK(ts.back().text == u8"工具出错但我还活着");
        // 3) 历史 functionCall/functionResponse 严格配对,且 response 含 error("tool threw: boom")
        picojson::value hv; std::string herr = picojson::parse(hv, core.historyContentsForTest());
        CHECK(herr.empty());
        const picojson::array& contents = hv.get<picojson::array>();
        size_t nCalls = 0, nResponses = 0; bool sawToolThrew = false;
        for (size_t i = 0; i < contents.size(); ++i)
        {
            const picojson::array& parts = contents[i].get("parts").get<picojson::array>();
            for (size_t j = 0; j < parts.size(); ++j)
            {
                if (parts[j].contains("functionCall")) ++nCalls;
                if (parts[j].contains("functionResponse"))
                {
                    ++nResponses;
                    const picojson::value& resp = parts[j].get("functionResponse").get("response");
                    if (resp.contains("error") &&
                        resp.get("error").to_str().find("tool threw: boom") != std::string::npos)
                        sawToolThrew = true;
                }
            }
        }
        CHECK(nCalls == 1);
        CHECK(nCalls == nResponses);
        CHECK(sawToolThrew);
        std::cout << "AIChatCore tool exception contained OK\n";
    }

    // ---- parseGeminiResponse:纯解析,不碰网络(Task 3)----
    {
        // a) 纯文本响应
        {
            std::string body =
                "{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"你好\"}],\"role\":\"model\"},"
                "\"finishReason\":\"STOP\"}]}";
            LLMTurn t = parseGeminiResponse(body);
            CHECK(t.error.empty());
            CHECK(t.text == u8"你好");
            CHECK(t.calls.empty());
        }
        // b) functionCall 响应
        {
            std::string body =
                "{\"candidates\":[{\"content\":{\"parts\":[{\"functionCall\":{\"name\":\"fly_to\","
                "\"args\":{\"lat\":40.7,\"lon\":-74.0}}}],\"role\":\"model\"}}]}";
            LLMTurn t = parseGeminiResponse(body);
            CHECK(t.error.empty());
            CHECK(t.text.empty());
            CHECK(t.calls.size() == 1);
            CHECK(t.calls[0].name == "fly_to");
            CHECK(t.calls[0].args.get("lat").get<double>() > 40.0);
        }
        // c) 混合 parts:文本 + functionCall 都要收集到
        {
            std::string body =
                "{\"candidates\":[{\"content\":{\"parts\":["
                "{\"text\":\"帮你查一下\"},"
                "{\"functionCall\":{\"name\":\"set_layer\",\"args\":{\"name\":\"basemap\"}}}"
                "],\"role\":\"model\"}}]}";
            LLMTurn t = parseGeminiResponse(body);
            CHECK(t.error.empty());
            CHECK(t.text == u8"帮你查一下");
            CHECK(t.calls.size() == 1);
            CHECK(t.calls[0].name == "set_layer");
        }
        // d) 空 candidates + promptFeedback.blockReason -> error
        {
            std::string body =
                "{\"candidates\":[],\"promptFeedback\":{\"blockReason\":\"SAFETY\"}}";
            LLMTurn t = parseGeminiResponse(body);
            CHECK(!t.error.empty());
            CHECK(t.error.find("SAFETY") != std::string::npos);
            CHECK(t.text.empty());
            CHECK(t.calls.empty());
        }
        // e) thoughtSignature 保留(gemini-3.5-flash function calling 新要求):
        //    与 functionCall 平级的 thoughtSignature(Gemini REST camelCase 实际形态)
        //    必须原样落进 FunctionCall.rawPartJson,后续回发历史时不得丢失,
        //    否则真实 API 400 "Function call is missing a thought_signature"。
        {
            std::string body =
                "{\"candidates\":[{\"content\":{\"parts\":["
                "{\"functionCall\":{\"name\":\"fly_to\",\"args\":{\"lat\":40.7,\"lon\":-74.0}},"
                "\"thoughtSignature\":\"abc\"}"
                "],\"role\":\"model\"}}]}";
            LLMTurn t = parseGeminiResponse(body);
            CHECK(t.error.empty());
            CHECK(t.calls.size() == 1);
            CHECK(t.calls[0].name == "fly_to");
            CHECK(!t.calls[0].rawPartJson.empty());
            CHECK(t.calls[0].rawPartJson.find("thoughtSignature") != std::string::npos);
            CHECK(t.calls[0].rawPartJson.find("abc") != std::string::npos);
            // 签名在 functionCall 内部的形态同样整 part 保留(不对落点做假设)
            std::string body2 =
                "{\"candidates\":[{\"content\":{\"parts\":["
                "{\"functionCall\":{\"name\":\"fly_to\",\"args\":{},"
                "\"thoughtSignature\":\"xyz\"}}"
                "],\"role\":\"model\"}}]}";
            LLMTurn t2 = parseGeminiResponse(body2);
            CHECK(t2.error.empty());
            CHECK(t2.calls.size() == 1);
            CHECK(t2.calls[0].rawPartJson.find("thoughtSignature") != std::string::npos);
            CHECK(t2.calls[0].rawPartJson.find("xyz") != std::string::npos);
        }
        std::cout << "parseGeminiResponse canned-body tests OK\n";
    }

    // ---- buildContentsJson:rawPartJson 原文回发 / 空则按 name+args 重建 ----
    {
        // 1) rawPartJson 非空(真 Gemini 路径):序列化输出必须原样含 thoughtSignature
        std::vector<HistoryItem> hist;
        HistoryItem u; u.role = HistoryItem::USER_TEXT; u.text = "fly to new york"; hist.push_back(u);
        HistoryItem mc; mc.role = HistoryItem::MODEL_CALL; mc.callName = "fly_to";
        mc.callArgs = parse("{\"lat\":40.7,\"lon\":-74.0}");
        mc.rawPartJson =
            "{\"functionCall\":{\"name\":\"fly_to\",\"args\":{\"lat\":40.7,\"lon\":-74.0}},"
            "\"thoughtSignature\":\"abc\"}";
        hist.push_back(mc);
        HistoryItem tr; tr.role = HistoryItem::TOOL_RESPONSE; tr.callName = "fly_to";
        tr.toolResponse = parse("{\"ok\":true}"); hist.push_back(tr);

        std::string cj = buildContentsJson(hist);
        picojson::value cv; std::string cerr = picojson::parse(cv, cj);
        CHECK(cerr.empty());
        CHECK(cj.find("thoughtSignature") != std::string::npos);
        CHECK(cj.find("abc") != std::string::npos);
        const picojson::array& cs = cv.get<picojson::array>();
        CHECK(cs.size() == 3);
        // part 结构完整:functionCall 与 thoughtSignature 同在一个 part 对象里
        const picojson::value& mcPart = cs[1].get("parts").get<picojson::array>()[0];
        CHECK(mcPart.contains("functionCall"));
        CHECK(mcPart.contains("thoughtSignature"));
        CHECK(mcPart.get("thoughtSignature").to_str() == "abc");
        CHECK(mcPart.get("functionCall").get("name").to_str() == "fly_to");

        // 2) rawPartJson 为空(FakeProvider 路径):照旧按 name+args 重建 functionCall
        std::vector<HistoryItem> hist2;
        HistoryItem mc2; mc2.role = HistoryItem::MODEL_CALL; mc2.callName = "set_layer";
        mc2.callArgs = parse("{\"name\":\"quakes\"}"); hist2.push_back(mc2);
        std::string cj2 = buildContentsJson(hist2);
        picojson::value cv2; CHECK(picojson::parse(cv2, cj2).empty());
        const picojson::value& p2 = cv2.get<picojson::array>()[0].get("parts").get<picojson::array>()[0];
        CHECK(p2.contains("functionCall"));
        CHECK(p2.get("functionCall").get("name").to_str() == "set_layer");
        CHECK(cj2.find("thoughtSignature") == std::string::npos);

        // 3) rawPartJson 是坏 JSON(防御):回退重建,不崩、输出仍合法
        std::vector<HistoryItem> hist3;
        HistoryItem mc3; mc3.role = HistoryItem::MODEL_CALL; mc3.callName = "fly_to";
        mc3.callArgs = parse("{\"lat\":1.0}"); mc3.rawPartJson = "{broken json";
        hist3.push_back(mc3);
        std::string cj3 = buildContentsJson(hist3);
        picojson::value cv3; CHECK(picojson::parse(cv3, cj3).empty());
        const picojson::value& p3 = cv3.get<picojson::array>()[0].get("parts").get<picojson::array>()[0];
        CHECK(p3.get("functionCall").get("name").to_str() == "fly_to");
        std::cout << "buildContentsJson thoughtSignature round-trip OK\n";
    }

    // ---- 端到端(解析→历史→回发)signature 不丢:parseGeminiResponse 的 rawPartJson
    //      喂给 MODEL_CALL HistoryItem 后,buildContentsJson 输出原样含签名 ----
    {
        std::string body =
            "{\"candidates\":[{\"content\":{\"parts\":["
            "{\"functionCall\":{\"name\":\"fly_to\",\"args\":{\"lat\":40.7}},"
            "\"thoughtSignature\":\"sig-e2e\"}"
            "],\"role\":\"model\"}}]}";
        LLMTurn t = parseGeminiResponse(body);
        CHECK(t.calls.size() == 1);
        std::vector<HistoryItem> hist;
        HistoryItem mc; mc.role = HistoryItem::MODEL_CALL;
        mc.callName = t.calls[0].name; mc.callArgs = t.calls[0].args;
        mc.rawPartJson = t.calls[0].rawPartJson;   // 与 drainMainThread 的搬运方式一致
        hist.push_back(mc);
        std::string cj = buildContentsJson(hist);
        CHECK(cj.find("sig-e2e") != std::string::npos);
        CHECK(cj.find("thoughtSignature") != std::string::npos);
        std::cout << "thoughtSignature parse->history->contents e2e OK\n";
    }

    // ---- GeminiProvider 可构造、不联网也不崩 ----
    {
        GeminiProvider gp("dummy-key-not-real", "gemini-2.5-flash");
        gp.setSystemPrompt(u8"你是地球助手");
        (void)gp; // 仅验证可构造/可设置系统提示;真实网络请求见 smoke test(见 REPORT)
        std::cout << "GeminiProvider construct OK\n";
    }

    std::cout << "ai_chat tests OK\n";

    // ---- buildMotionPrompt(Task 9,纯函数,header-only ai_motion.h)----
    {
        const double kDeg = 0.017453292519943295;   // 度→弧度,与 osg::DegreesToRadians 等价

        // TST(22.298,114.172,500m) 尖沙咀 -> (22.281,114.158,200m) 中环:
        // 大圆方位角 ≈217.3°(落在 southwest 罗盘扇区),高度 500m->200m 下降。
        {
            osg::Vec3d a(22.298 * kDeg, 114.172 * kDeg, 500.0);
            osg::Vec3d b(22.281 * kDeg, 114.158 * kDeg, 200.0);
            std::string p = earthai::buildMotionPrompt(a, b);
            CHECK(p.find("southwest") != std::string::npos);
            CHECK(p.find("descending") != std::string::npos);
            CHECK(p.find("500") != std::string::npos);
            CHECK(p.find("200") != std::string::npos);
        }

        // 起终点完全相同 -> 无方向意义的移动,高度也不变 -> "holding"
        {
            osg::Vec3d a(22.3 * kDeg, 114.17 * kDeg, 300.0);
            osg::Vec3d b(22.3 * kDeg, 114.17 * kDeg, 300.0);
            std::string p = earthai::buildMotionPrompt(a, b);
            CHECK(p.find("holding") != std::string::npos);
        }

        // 上升案例:B 点高度显著高于 A 点 -> "ascending"
        {
            osg::Vec3d a(22.28 * kDeg, 114.15 * kDeg, 100.0);
            osg::Vec3d b(22.30 * kDeg, 114.20 * kDeg, 800.0);
            std::string p = earthai::buildMotionPrompt(a, b);
            CHECK(p.find("ascending") != std::string::npos);
            CHECK(p.find("100") != std::string::npos);
            CHECK(p.find("800") != std::string::npos);
        }

        // 距离数字确实出现在提示词里(1 位小数格式),粗略检查含小数点。
        {
            osg::Vec3d a(22.298 * kDeg, 114.172 * kDeg, 500.0);
            osg::Vec3d b(22.281 * kDeg, 114.158 * kDeg, 200.0);
            std::string p = earthai::buildMotionPrompt(a, b);
            CHECK(p.find("km") != std::string::npos);
        }

        // review 补充:北向 wrap 用例。8 方位罗盘以 0°=north 为中心、每 45° 一档,north 扇区
        // 横跨 337.5°~360° 与 0°~22.5° 两段,取模实现(见 ai_motion.h octant 计算)最容易在
        // 这个跨零点的边界上出错。B 点选在 A 点"正北略偏西"(经度比 A 略小、纬度显著更大),
        // 大圆方位角 ≈349°,落在 north 扇区但不靠近 337.5° 边界(即不会因为浮点噪声误落进
        // northwest),用来验证 wrap 逻辑而不是靠近似判断猜结果。
        {
            osg::Vec3d a(22.28 * kDeg, 114.17 * kDeg, 300.0);
            osg::Vec3d b(22.40 * kDeg, 114.145 * kDeg, 300.0);
            std::string p = earthai::buildMotionPrompt(a, b);
            CHECK(p.find("north") != std::string::npos);
            CHECK(p.find("northeast") == std::string::npos);
            CHECK(p.find("northwest") == std::string::npos);
        }

        std::cout << "buildMotionPrompt tests OK\n";
    }

    // ---- buildPhotoPrompt / buildVideoPrompt(提示词工程,header-only ai_prompts.h)----
    {
        const double kDeg = 0.017453292519943295;   // 度→弧度,与 osg::DegreesToRadians 等价

        // set_layer 的能力描述来自当前注册表，而不是六个旧 id 的写死文案。
        {
            std::vector<earthai::LayerToolPromptEntry> entries;
            entries.push_back({"alphaearth", "AlphaEarth", true});
            entries.push_back({"flights", u8"实时航班", false});
            const std::string description =
                earthai::buildSetLayerToolDescription(entries);
            CHECK(description.find("alphaearth(AlphaEarth)") != std::string::npos);
            CHECK(description.find(u8"flights(实时航班)") != std::string::npos);
            CHECK(description.find("opacity") != std::string::npos);
            CHECK(description.find("hk3d") == std::string::npos);
        }

        // ScienceEarth 没加载时不得编造能力；加载时须严格遵循串行、
        // 终态轮询、可显示产物、64D 语义和证据不可信边界。
        {
            const std::string prompt = earthai::buildEarthAssistantSystemPrompt();
            CHECK(prompt.find("start_multisource_research") != std::string::npos);
            CHECK(prompt.find("max_cloud_percent") != std::string::npos);
            CHECK(prompt.find("100") != std::string::npos);
            CHECK(prompt.find("get_research_job") != std::string::npos);
            CHECK(prompt.find("show_science_artifact") != std::string::npos);
            CHECK(prompt.find(u8"终态") != std::string::npos);
            CHECK(prompt.find(u8"64 维") != std::string::npos);
            CHECK(prompt.find(u8"因果") != std::string::npos);
            CHECK(prompt.find(u8"不可信证据") != std::string::npos);
            CHECK(prompt.find(u8"未加载") != std::string::npos);
            CHECK(prompt.find(u8"不得移动相机") != std::string::npos);
        }

        // 高空/轨道快门的 prompt 必须由同一不可变 capture context 构造。目标地点、相机
        // 眼点和画面中心是三个不同概念；姿态/FOV/覆盖尺度来自快门时的 view/projection/
        // viewport，而不是靠模型从一句 "aerial view" 猜。这个 synthetic camera 位于
        // 赤道 408km 高度、向东斜视 2° 的地面目标：look vector 的水平投影应朝东。
        {
            const double earthRadius = 6378137.0;
            const osg::Vec3d eyeWorld(earthRadius + 408000.0, 0.0, 0.0);
            const osg::Vec3d targetWorld(
                earthRadius * std::cos(2.0 * kDeg),
                earthRadius * std::sin(2.0 * kDeg), 0.0);
            const osg::Matrixd view = osg::Matrixd::lookAt(
                eyeWorld, targetWorld, osg::Vec3d(0.0, 0.0, 1.0));
            const osg::Matrixd projection = osg::Matrixd::perspective(
                30.0, 1920.0 / 1080.0, 1.0, 20000000.0);

            const earthai::PhotoCameraContext camera = earthai::makePhotoCameraContext(
                osg::Vec3d(0.0, 0.0, 408000.0),
                osg::Vec3d(0.0, 2.0 * kDeg, 0.0),
                view, projection, 1920, 1080);
            CHECK(camera.viewTargetValid);
            CHECK(camera.headingValid);
            CHECK(std::fabs(camera.headingDeg - 90.0) < 0.5);
            CHECK(camera.offNadirValid);
            CHECK(camera.offNadirDeg > 20.0 && camera.offNadirDeg < 40.0);
            CHECK(camera.verticalFovValid);
            CHECK(std::fabs(camera.verticalFovDeg - 30.0) < 1e-6);
            CHECK(camera.aspectRatioValid);
            CHECK(std::fabs(camera.aspectRatio - 1920.0 / 1080.0) < 1e-6);
            CHECK(camera.groundFootprintSpanValid);
            CHECK(camera.groundFootprintSpanKm > 100.0);

            earthai::PhotoRequest visibleInput;
            visibleInput.lla.set(0.0, 2.5 * kDeg, 0.0);
            CHECK(earthai::photoTargetVisibleInCameraContext(
                visibleInput.lla, camera));
            earthai::PhotoRequest outsideInput;
            outsideInput.lla.set(0.0, 40.0 * kDeg, 0.0);
            CHECK(!earthai::photoTargetVisibleInCameraContext(
                outsideInput.lla, camera));
            CHECK(earthai::photoCameraContextsMatchFrame(camera, camera));
            const earthai::PhotoCameraContext resizedCamera =
                earthai::makePhotoCameraContext(
                    osg::Vec3d(0.0, 0.0, 408000.0),
                    osg::Vec3d(0.0, 2.0 * kDeg, 0.0),
                    view, projection, 1280, 720);
            CHECK(!earthai::photoCameraContextsMatchFrame(
                camera, resizedCamera));

            earthai::PhotoRequest input;
            input.lla.set(0.0, 2.5 * kDeg, 0.0);  // intended subject != center of frame
            const earthai::PhotoCaptureRequest capture =
                earthai::makePhotoCaptureRequest(input, camera, 88);
            const std::string p = earthai::buildPhotoPrompt(capture);

            CHECK(p.find("fresh independent generation") != std::string::npos);
            CHECK(p.find("Camera eye (WGS84 ellipsoid)") != std::string::npos);
            CHECK(p.find("Center-of-frame view target") != std::string::npos);
            CHECK(p.find("Intended geographic subject") != std::string::npos);
            CHECK(p.find("2.0000") != std::string::npos);
            CHECK(p.find("2.5000") != std::string::npos);
            CHECK(p.find("heading 90.0") != std::string::npos);
            CHECK(p.find("off-nadir") != std::string::npos);
            CHECK(p.find("vertical FOV 30.0") != std::string::npos);
            CHECK(p.find("visible ground footprint") != std::string::npos);
            CHECK(p.find("HARD CAMERA-GEOMETRY LOCK") != std::string::npos);
            CHECK(p.find("authoritative") != std::string::npos);
            CHECK(p.find("Do not move the camera closer or farther") != std::string::npos);
            CHECK(p.find("zoom, crop, or reframe") != std::string::npos);
            CHECK(p.find("horizon visibility") != std::string::npos);
            CHECK(p.find("Earth curvature") != std::string::npos);
            CHECK(p.find("spaceborne/high-altitude") != std::string::npos);
            CHECK(p.find("spaceborne/orbital") == std::string::npos);
            CHECK(p.find("not drone or aircraft imagery") != std::string::npos);
            CHECK(p.find("foreground mountains") != std::string::npos);
            CHECK(p.find("near-camera clouds") != std::string::npos);
            CHECK(p.find("building-level perspective") != std::string::npos);
            CHECK(p.find("no spacecraft") != std::string::npos);
        }

        // buildPhotoPrompt:格式化坐标(带 N/S/E/W)+ "composition"(构图参考措辞)+
        // "no UI"(禁止 UI/文字/水印/地图标注伪影)都要出现在生成的提示词里。
        {
            osg::Vec3d lla(22.298 * kDeg, 114.172 * kDeg, 500.0);
            std::string p = earthai::buildPhotoPrompt(lla, "");
            CHECK(p.find("22.2980") != std::string::npos);
            CHECK(p.find("114.1720") != std::string::npos);
            CHECK(p.find("composition") != std::string::npos);
            CHECK(p.find("no UI") != std::string::npos);
            CHECK(p.find("fresh independent generation") != std::string::npos);
            CHECK(p.find("no spacecraft") != std::string::npos);
            CHECK(p.find("Center-of-frame view target") == std::string::npos);
        }
        // style 后缀非空时应追加到提示词中。
        {
            osg::Vec3d lla(22.298 * kDeg, 114.172 * kDeg, 500.0);
            std::string p = earthai::buildPhotoPrompt(lla, "dusk, golden hour");
            CHECK(p.find("Style: dusk, golden hour") != std::string::npos);
            CHECK(p.find("no spacecraft") > p.find("Style: dusk, golden hour"));
        }

        // buildVideoPrompt:复用 buildMotionPrompt 的南西(southwest)轨迹用例(与上面
        // buildMotionPrompt 测试同一组坐标),确认方向词与"first frame"措辞都出现。
        {
            osg::Vec3d a(22.298 * kDeg, 114.172 * kDeg, 500.0);
            osg::Vec3d b(22.281 * kDeg, 114.158 * kDeg, 200.0);
            std::string p = earthai::buildVideoPrompt(a, b, "");
            CHECK(p.find("southwest") != std::string::npos);
            CHECK(p.find("first frame") != std::string::npos);
            CHECK(p.find("descending") != std::string::npos);
        }

        // 时空影像工作台：所有图像/视频请求都从本次不可变快门上下文建立，默认不带
        // 任何旧产物引用。历史与深时生成必须把事实、重建假设、创意风格和禁止项分层。
        {
            const osg::Vec3d eyeWorld(6378137.0 + 1200.0, 0.0, 0.0);
            const osg::Vec3d targetWorld(
                6378137.0 * std::cos(0.01),
                6378137.0 * std::sin(0.01), 0.0);
            const osg::Matrixd view = osg::Matrixd::lookAt(
                eyeWorld, targetWorld, osg::Vec3d(0.0, 0.0, 1.0));
            const osg::Matrixd projection = osg::Matrixd::perspective(
                42.0, 16.0 / 9.0, 1.0, 20000000.0);
            const earthai::PhotoCameraContext camera =
                earthai::makePhotoCameraContext(
                    osg::Vec3d(22.2930 * kDeg, 114.1690 * kDeg, 1200.0),
                    osg::Vec3d(22.2870 * kDeg, 114.1690 * kDeg, 0.0),
                    view, projection, 1920, 1080);
            earthai::PhotoRequest input;
            input.lla.set(22.2870 * kDeg, 114.1690 * kDeg, 0.0);
            const earthai::PhotoCaptureRequest capture =
                earthai::makePhotoCaptureRequest(input, camera, 901);

            earthai::CinematicGenerationSettings image =
                earthai::defaultImageCinematicSettings();
            CHECK(image.mediaKind == earthai::CINEMATIC_IMAGE);
            CHECK(image.era == earthai::CINEMATIC_ERA_PRESENT);
            CHECK(image.motion == earthai::CINEMATIC_MOTION_STATIC);

            image.era = earthai::CINEMATIC_ERA_1920S;
            image.localTime = earthai::CINEMATIC_TIME_1900;
            image.visualStyle = earthai::CINEMATIC_STYLE_ARCHIVAL_AMBER;
            image.userPrompt = "Victoria Harbour, Hong Kong";
            earthai::CinematicGenerationRequest historical;
            CHECK(earthai::makeCinematicGenerationRequest(
                capture, image, historical));
            CHECK(historical.anchor.requestId == 901);
            CHECK(historical.previousArtifactId.empty());
            const osg::Matrixd originalView = capture.camera.visibleViewMatrix;
            const std::string historicalPrompt =
                earthai::buildCinematicImagePrompt(historical);
            CHECK(historicalPrompt.find("1920") != std::string::npos);
            CHECK(historicalPrompt.find("19:00 local time") != std::string::npos);
            CHECK(historicalPrompt.find("Victoria Harbour") != std::string::npos);
            CHECK(historicalPrompt.find("historical reconstruction") != std::string::npos);
            CHECK(historicalPrompt.find("not documentary evidence") != std::string::npos);
            CHECK(historicalPrompt.find("anachron") != std::string::npos);
            CHECK(historicalPrompt.find("aged amber") != std::string::npos);
            CHECK(historicalPrompt.find("unmistakable civil twilight") !=
                  std::string::npos);
            CHECK(historicalPrompt.find("modern-looking street grid") !=
                  std::string::npos);
            CHECK(historicalPrompt.find("HARD CAMERA-GEOMETRY LOCK") != std::string::npos);
            CHECK(capture.camera.visibleViewMatrix == originalView);

            earthai::CinematicGenerationSettings cambrian = image;
            cambrian.era = earthai::CINEMATIC_ERA_CAMBRIAN_CHENGJIANG;
            cambrian.localTime = earthai::CINEMATIC_TIME_NOON;
            cambrian.visualStyle = earthai::CINEMATIC_STYLE_SCIENTIFIC;
            cambrian.userPrompt = "Chengjiang biota in Yunnan";
            earthai::CinematicGenerationRequest deepTime;
            CHECK(earthai::makeCinematicGenerationRequest(
                capture, cambrian, deepTime));
            const std::string cambrianPrompt =
                earthai::buildCinematicImagePrompt(deepTime);
            CHECK(cambrianPrompt.find("Early Cambrian") != std::string::npos);
            CHECK(cambrianPrompt.find("approximately 518 million years") != std::string::npos);
            CHECK(cambrianPrompt.find("scientific reconstruction") != std::string::npos);
            CHECK(cambrianPrompt.find("paleogeographic") != std::string::npos);
            CHECK(cambrianPrompt.find("no humans") != std::string::npos);
            CHECK(cambrianPrompt.find("modern buildings") != std::string::npos);
            CHECK(cambrianPrompt.find("never enlarge fossils or animals") !=
                  std::string::npos);
            CHECK(cambrianPrompt.find("pixel-exact ancient surface") !=
                  std::string::npos);

            earthai::CinematicGenerationSettings video =
                earthai::defaultVideoCinematicSettings();
            CHECK(video.mediaKind == earthai::CINEMATIC_VIDEO);
            CHECK(video.motion == earthai::CINEMATIC_MOTION_AERIAL_TOUR);
            CHECK(video.visualStyle == earthai::CINEMATIC_STYLE_ULTRA_REAL);
            CHECK(video.includeGeneratedAudio);
            earthai::CinematicGenerationRequest videoRequest;
            CHECK(earthai::makeCinematicGenerationRequest(
                capture, video, videoRequest));
            std::string videoPrompt =
                earthai::buildCinematicVideoPrompt(videoRequest);
            CHECK(videoPrompt.find("provided first frame") != std::string::npos);
            CHECK(videoPrompt.find("ultra-photorealistic") != std::string::npos);
            CHECK(videoPrompt.find("aerial") != std::string::npos);
            CHECK(videoPrompt.find("start exactly") != std::string::npos);
            CHECK(videoPrompt.find("single unbroken") != std::string::npos);
            CHECK(videoPrompt.find("no edit point") != std::string::npos);
            CHECK(videoPrompt.find("[AUDIO]") != std::string::npos);
            CHECK(videoPrompt.find("ambient sound") != std::string::npos);
            const earthai::CinematicVideoOutputOptions videoOutput =
                earthai::cinematicVideoOutputOptions(videoRequest);
            CHECK(videoOutput.aspectRatio == "16:9");
            CHECK(videoOutput.durationSeconds == video.durationSeconds);
            const picojson::object videoFormat =
                earthai::cinematicGeminiVideoResponseFormat(videoOutput);
            CHECK(videoFormat.at("type").get<std::string>() == "video");
            CHECK(videoFormat.at("aspect_ratio").get<std::string>() == "16:9");
            CHECK(videoFormat.at("duration").get<std::string>() == "8s");
            const picojson::object videoConfig =
                earthai::cinematicGeminiImageToVideoConfig();
            CHECK(videoConfig.at("video_config").get<picojson::object>().at(
                      "task").get<std::string>() == "image_to_video");

            video.motion = earthai::CINEMATIC_MOTION_ORBIT_360;
            CHECK(earthai::cinematicMinimumDurationSeconds(video.motion) == 8);
            CHECK(earthai::cinematicMotionRequiresClosureReview(video.motion));
            CHECK(earthai::buildCinematicVideoPrompt(
                earthai::cinematicRequestUnchecked(capture, video)).find(
                    "360-degree orbit") != std::string::npos);
            CHECK(earthai::buildCinematicVideoPrompt(
                earthai::cinematicRequestUnchecked(capture, video)).find(
                    "front, right, rear and left quadrants") != std::string::npos);
            CHECK(earthai::cinematicMotionLabel(video.motion) ==
                  std::string(u8"360° 环拍"));
            earthai::CinematicGenerationSettings tooShortOrbit = video;
            tooShortOrbit.durationSeconds = 5;
            earthai::CinematicGenerationRequest rejectedOrbit;
            CHECK(!earthai::makeCinematicGenerationRequest(
                capture, tooShortOrbit, rejectedOrbit));
            CHECK(earthai::makeCinematicGenerationRequest(
                capture, video, rejectedOrbit));
            video.motion = earthai::CINEMATIC_MOTION_DIVE;
            CHECK(earthai::cinematicMinimumDurationSeconds(video.motion) == 4);
            CHECK(!earthai::cinematicMotionRequiresClosureReview(video.motion));
            CHECK(earthai::buildCinematicVideoPrompt(
                earthai::cinematicRequestUnchecked(capture, video)).find(
                    "controlled dive") != std::string::npos);
            video.includeGeneratedAudio = false;
            CHECK(earthai::buildCinematicVideoPrompt(
                earthai::cinematicRequestUnchecked(capture, video)).find(
                    "effectively silent") != std::string::npos);
            video.includeGeneratedAudio = true;
            video.motion = earthai::CINEMATIC_MOTION_POINT_TO_POINT;
            CHECK(earthai::cinematicMotionNeedsEndFrame(video.motion));
            CHECK(!earthai::cinematicMotionNeedsEndFrame(
                earthai::CINEMATIC_MOTION_ORBIT_360));
            video.visualStyle = earthai::CINEMATIC_STYLE_ANIME;
            CHECK(earthai::buildCinematicVideoPrompt(
                earthai::cinematicRequestUnchecked(capture, video)).find(
                    "anime") != std::string::npos);

            CHECK(earthai::cinematicImageModelName(NULL) ==
                  "gemini-3.1-flash-image");
            CHECK(earthai::cinematicImageModelName("gemini-3-pro-image") ==
                  "gemini-3-pro-image");
            const earthai::CinematicImageOutputOptions output =
                earthai::cinematicImageOutputOptions(historical);
            CHECK(output.aspectRatio == "16:9");
            CHECK(output.imageSize == "2K");
            const picojson::object config =
                earthai::cinematicGeminiImageGenerationConfig(output);
            CHECK(config.find("responseModalities") != config.end());
            CHECK(config.find("imageConfig") != config.end());
            const picojson::object imageConfig =
                config.find("imageConfig")->second.get<picojson::object>();
            CHECK(imageConfig.find("aspectRatio")->second.get<std::string>() ==
                  "16:9");
            CHECK(imageConfig.find("imageSize")->second.get<std::string>() ==
                  "2K");
        }

        std::cout << "buildPhotoPrompt/buildVideoPrompt tests OK\n";
    }

    // ---- generate_photo 必须携带本次任务自己的目标坐标，不能隐式沿用上一视角 ----
    {
        CHECK(OSGSOL_HAS_PHOTO_REQUEST == 1);
#if OSGSOL_HAS_PHOTO_REQUEST
        earthai::PhotoRequest request;
        std::string error;
        CHECK(!earthai::parsePhotoRequest(parse("{\"style\":\"ISS view\"}"), request, error));
        CHECK(error.find("lat/lon") != std::string::npos);

        CHECK(earthai::parsePhotoRequest(parse(
            "{\"lat\":12.5,\"lon\":34.25,\"alt_km\":408.0,\"style\":\"orbital\"}"),
            request, error));
        CHECK(std::fabs(request.lla[0] - 12.5 * 0.017453292519943295) < 1e-9);
        CHECK(std::fabs(request.lla[1] - 34.25 * 0.017453292519943295) < 1e-9);
        CHECK(std::fabs(request.lla[2] - 408000.0) < 1e-6);
        CHECK(request.style == "orbital");
        CHECK(!request.showCameraPlatform);

        osg::Matrixd hongKongMatrix = osg::Matrixd::rotate(0.4, osg::X_AXIS);
        osg::Matrixd nvidiaMatrix = osg::Matrixd::rotate(0.8, osg::Y_AXIS);

        earthai::PhotoRequest hongKongInput;
        hongKongInput.lla.set(22.298 * 0.017453292519943295,
                              114.172 * 0.017453292519943295, 500.0);
        hongKongInput.style = "harbour";

        earthai::PhotoRequest nvidiaInput;
        nvidiaInput.lla.set(37.3707 * 0.017453292519943295,
                            -121.9631 * 0.017453292519943295, 800.0);
        nvidiaInput.style = "campus";

        earthai::PhotoCameraContext hongKongCamera;
        hongKongCamera.cameraEyeLla = hongKongInput.lla;
        hongKongCamera.viewTargetLla = hongKongInput.lla;
        hongKongCamera.visibleViewMatrix = hongKongMatrix;
        earthai::PhotoCameraContext nvidiaCamera;
        nvidiaCamera.cameraEyeLla = nvidiaInput.lla;
        nvidiaCamera.viewTargetLla = nvidiaInput.lla;
        nvidiaCamera.visibleViewMatrix = nvidiaMatrix;

        const earthai::PhotoCaptureRequest first =
            earthai::makePhotoCaptureRequest(hongKongInput, hongKongCamera, 41);
        const earthai::PhotoCaptureRequest second =
            earthai::makePhotoCaptureRequest(nvidiaInput, nvidiaCamera, 42);

        CHECK(first.requestId == 41);
        CHECK(second.requestId == 42);
        CHECK(first.targetLla != second.targetLla);
        CHECK(first.camera.visibleViewMatrix != second.camera.visibleViewMatrix);
        CHECK(first.camera.cameraEyeLla != second.camera.cameraEyeLla);
        CHECK(first.camera.viewTargetLla != second.camera.viewTargetLla);
        CHECK(first.style == "harbour");
        CHECK(second.style == "campus");

        picojson::value schema;
        CHECK(picojson::parse(schema, earthai::photoToolParametersJson()).empty());
        const picojson::array& required = schema.get("required").get<picojson::array>();
        CHECK(required.size() == 2);
        CHECK(required[0].to_str() == "lat");
        CHECK(required[1].to_str() == "lon");
        CHECK(!earthai::photoCaptureHasFreshView(0));
        CHECK(earthai::photoCaptureHasFreshView(1));
        // 目标拍照必须经过两个硬门槛：当前相机确实在目标附近；若本轮刚执行过 fly_to，
        // 必须等用户看到画面、调整/确认视角后，在下一条用户指令里才能按快门。
        earthai::PhotoViewGate viewGate;
        const osg::Vec3d nvidiaLla(37.3707 * 0.017453292519943295,
                                   -121.9631 * 0.017453292519943295, 800.0);
        const osg::Vec3d hongKongEye(22.298 * 0.017453292519943295,
                                     114.172 * 0.017453292519943295, 800.0);
        const auto cameraLookingAt = [](const osg::Vec3d& eyeLla,
                                        const osg::Vec3d& centerLla) {
            osg::Vec3d surfaceCenter = centerLla;
            surfaceCenter[2] = 0.0;
            const osg::Vec3d eyeWorld = earthai::photoLlaToEcef(eyeLla);
            const osg::Vec3d centerWorld =
                earthai::photoLlaToEcef(surfaceCenter);
            const double lat = centerLla[0];
            const double lon = centerLla[1];
            const osg::Vec3d north(
                -std::sin(lat) * std::cos(lon),
                -std::sin(lat) * std::sin(lon), std::cos(lat));
            return earthai::makePhotoCameraContext(
                eyeLla, surfaceCenter,
                osg::Matrixd::lookAt(eyeWorld, centerWorld, north),
                osg::Matrixd::perspective(30.0, 16.0 / 9.0, 1.0, 20000000.0),
                1920, 1080);
        };
        const earthai::PhotoCameraContext nvidiaCameraContext =
            cameraLookingAt(nvidiaLla, nvidiaLla);
        const earthai::PhotoCameraContext hongKongCameraContext =
            cameraLookingAt(hongKongEye, hongKongEye);
        viewGate.beginUserTurn(u8"去 NVIDIA 总部");
        CHECK(std::string(earthai::photoCaptureGateError(
                  false, viewGate, nvidiaLla, nvidiaCameraContext)) == "photo_not_requested");
        viewGate.beginUserTurn(u8"不要拍照，只去 NVIDIA 总部");
        const char* negativeIntentError = earthai::photoCaptureGateError(
            false, viewGate, nvidiaLla, nvidiaCameraContext);
        CHECK(negativeIntentError != NULL);
        CHECK(std::string(negativeIntentError) == "photo_not_requested");
        CHECK(!earthai::isExplicitPhotoRequest(u8"照片不要拍，只导航"));
        CHECK(!earthai::isExplicitPhotoRequest(u8"别生成照片"));
        CHECK(!earthai::isExplicitPhotoRequest("without taking a picture"));
        CHECK(!earthai::isExplicitPhotoRequest("don't generate an image"));
        CHECK(!earthai::isExplicitPhotoRequest("no pictures"));
        CHECK(!earthai::isExplicitPhotoRequest("show the satellite image layer"));
        CHECK(earthai::isExplicitPhotoRequest(u8"生成一张 ISS 俯拍照"));
        CHECK(earthai::isExplicitPhotoRequest("take a photo"));
        CHECK(earthai::isExplicitPhotoRequest(
            "Do not generate a video; take a photo instead"));
        CHECK(earthai::isExplicitPhotoRequest(
            "Do not take a video; take a photo instead"));

        viewGate.beginUserTurn(u8"生成一张 ISS 俯拍照");
        CHECK(!earthai::photoCameraPlatformAllowed(viewGate, true));
        viewGate.beginUserTurn("take a photo from a spacecraft");
        CHECK(!earthai::photoCameraPlatformAllowed(viewGate, true));
        viewGate.beginUserTurn(u8"拍照，画面中显示空间站和太阳能板");
        CHECK(earthai::photoCameraPlatformAllowed(viewGate, true));
        viewGate.beginUserTurn(u8"拍照，但不要显示空间站或太阳能板");
        CHECK(earthai::isExplicitPhotoRequest(u8"拍照，但不要显示空间站或太阳能板"));
        CHECK(!earthai::photoCameraPlatformAllowed(viewGate, true));
        viewGate.beginUserTurn(u8"拍一张照片");
        CHECK(std::string(earthai::photoCaptureGateError(
                  true, viewGate, nvidiaLla, nvidiaCameraContext)) == "camera_flight_in_progress");
        CHECK(std::string(earthai::photoCaptureGateError(
                  false, viewGate, nvidiaLla, hongKongCameraContext)) == "photo_target_not_visible");
        CHECK(earthai::photoCaptureGateError(
                  false, viewGate, nvidiaLla, nvidiaCameraContext) == NULL);

        // 地面目标的拍照导航不能沿用普通地球总览的 150km 默认高度；否则虽然经纬度到了，
        // 总部建筑仍根本不可见。显式高度（例如 ISS 408km）始终优先。
        CHECK(std::fabs(earthai::photoFlyAltitudeKm(parse("{}")) - 150.0) < 1e-9);
        CHECK(std::fabs(earthai::photoFlyAltitudeKm(parse("{\"for_photo\":true}")) - 2.0) < 1e-9);
        CHECK(std::fabs(earthai::photoFlyAltitudeKm(parse(
                  "{\"for_photo\":true,\"alt_km\":408}")) - 408.0) < 1e-9);
        viewGate.recordFlyTo();
        CHECK(std::string(earthai::photoCaptureGateError(
                  false, viewGate, nvidiaLla, nvidiaCameraContext)) == "photo_view_not_confirmed");
        osg::Vec3d offsetTarget = nvidiaLla;
        offsetTarget[0] += 0.0002 * 0.017453292519943295; // 约 22m，仍在实际视锥内
        const char* offsetSameTurnError = earthai::photoCaptureGateError(
            false, viewGate, offsetTarget, nvidiaCameraContext);
        CHECK(offsetSameTurnError != NULL);
        CHECK(std::string(offsetSameTurnError) == "photo_view_not_confirmed");
        viewGate.beginUserTurn(u8"现在按这个视角拍照");
        CHECK(earthai::photoCaptureGateError(
                  false, viewGate, nvidiaLla, nvidiaCameraContext) == NULL);

        earthai::PhotoViewGate pendingConfirmationGate;
        pendingConfirmationGate.beginUserTurn(u8"去 NVIDIA 总部拍照");
        pendingConfirmationGate.recordFlyTo();
        pendingConfirmationGate.beginUserTurn(u8"就这个视角，可以了");
        CHECK(earthai::photoCaptureGateError(
                  false, pendingConfirmationGate, nvidiaLla,
                  nvidiaCameraContext) == NULL);
        pendingConfirmationGate.consumePhotoAuthorization();
        pendingConfirmationGate.beginUserTurn(u8"可以了");
        CHECK(std::string(earthai::photoCaptureGateError(
                  false, pendingConfirmationGate, nvidiaLla,
                  nvidiaCameraContext)) ==
              "photo_not_requested");

        earthai::PhotoViewGate cancelledConfirmationGate;
        cancelledConfirmationGate.beginUserTurn(u8"去 NVIDIA 总部拍照");
        cancelledConfirmationGate.recordFlyTo();
        cancelledConfirmationGate.beginUserTurn("don't take it yet");
        CHECK(std::string(earthai::photoCaptureGateError(
                  false, cancelledConfirmationGate, nvidiaLla,
                  nvidiaCameraContext)) ==
              "photo_not_requested");
        earthai::PhotoViewGate mixedCancellationGate;
        mixedCancellationGate.beginUserTurn(u8"去 NVIDIA 总部拍照");
        mixedCancellationGate.recordFlyTo();
        CHECK(earthai::isExplicitPhotoRequest(
            "don't take it yet; take a photo when I say"));
        CHECK(earthai::isPhotoShutterCancellation(
            "don't take it yet; take a photo when I say"));
        mixedCancellationGate.beginUserTurn(
            "don't take it yet; take a photo when I say");
        CHECK(!mixedCancellationGate.photoRequestedThisTurn());
        CHECK(std::string(earthai::photoCaptureGateError(
                  false, mixedCancellationGate, nvidiaLla,
                  nvidiaCameraContext)) ==
              "photo_not_requested");
        earthai::PhotoViewGate rejectedViewGate;
        rejectedViewGate.beginUserTurn(u8"去 NVIDIA 总部拍照");
        rejectedViewGate.recordFlyTo();
        rejectedViewGate.beginUserTurn("not this view");
        CHECK(std::string(earthai::photoCaptureGateError(
                  false, rejectedViewGate, nvidiaLla,
                  nvidiaCameraContext)) ==
              "photo_not_requested");

        earthai::PhotoViewGate issGate;
        issGate.beginUserTurn(u8"生成一张 ISS 俯拍照");
        const osg::Vec3d issTarget(10.0 * 0.017453292519943295,
                                   20.0 * 0.017453292519943295, 408000.0);
        const osg::Vec3d issObliqueEye(15.0 * 0.017453292519943295,
                                       20.0 * 0.017453292519943295, 408000.0);
        const earthai::PhotoCameraContext issCameraContext =
            cameraLookingAt(issObliqueEye, issTarget);
        CHECK(earthai::photoCaptureGateError(
                  false, issGate, issTarget, issCameraContext) == NULL);
#endif
        const std::string toolBlock = generatePhotoToolBlock();
        CHECK(toolBlock.find("isAnimationRunning()") != std::string::npos);
        CHECK(toolBlock.find("computeEyeLatLonHeight()") != std::string::npos);
        CHECK(toolBlock.find("photoCaptureGateError") != std::string::npos);
        CHECK(toolBlock.find("makePhotoCameraContext") != std::string::npos);
        CHECK(toolBlock.find("getViewMatrix()") != std::string::npos);
        CHECK(toolBlock.find("getProjectionMatrix()") != std::string::npos);
        CHECK(toolBlock.find("photoCameraPlatformAllowed") != std::string::npos);
        CHECK(toolBlock.find("photoRequestAtVisibleCameraAltitude") == std::string::npos);
        CHECK(toolBlock.find("consumePhotoAuthorization") != std::string::npos);
        CHECK(toolBlock.find("setByEye(") == std::string::npos);
        CHECK(toolBlock.find("stopAnimation(") == std::string::npos);
        CHECK(toolBlock.find("moveTo(") == std::string::npos);

        const std::string cameraFailureBlock = photoCameraUnavailableBlock();
        CHECK(cameraFailureBlock.find("camera unavailable") != std::string::npos);
        CHECK(cameraFailureBlock.find("_cards->removeJob(_jobId)") != std::string::npos);
        CHECK(cameraFailureBlock.find("_chatCore->addErrorNote") != std::string::npos);
        CHECK(cameraFailureBlock.find("failed: camera unavailable") != std::string::npos);
        CHECK(cameraFailureBlock.find("_state = IDLE") != std::string::npos);
        CHECK(cameraFailureBlock.find("return;") != std::string::npos);
        CHECK(cameraFailureBlock.find("hudHide()") == std::string::npos);

        const std::string captureBlock = photoCapturePromptBlock();
        CHECK(captureBlock.find("currentPhotoCameraContext") != std::string::npos);
        CHECK(captureBlock.find("photoCameraContextsMatchFrame") !=
              std::string::npos);
        CHECK(captureBlock.find("photoTargetVisibleInCameraContext") !=
              std::string::npos);
        CHECK(captureBlock.find("buildPhotoPrompt(_captureRequest)") != std::string::npos);
        std::cout << "generate_photo independent target tests OK\n";
    }

    // ---- GlobalKeyboardGate 纯判定逻辑(input_gate.h,earth_main 链首键盘闸)----
    {
        using earthinput::KeyGateLogic;

        // 非打字态:按键完全放行(KEYDOWN/KEYUP 都不吞)
        {
            KeyGateLogic g;
            CHECK(!g.filter(true, 'f', false));
            CHECK(!g.filter(false, 'f', false));
        }

        // 打字态:KEYDOWN/KEYUP 都吞(聊天框打 f/w/s 不再触发全屏/线框/统计)
        {
            KeyGateLogic g;
            CHECK(g.filter(true, 'f', true));
            CHECK(g.filter(false, 'f', true));
        }

        // 配对吞:按下时在打字、松开前已失焦 → KEYUP 仍被吞
        // (防 'o'/'i'/'p' 这类 KEYUP 触发的 EnvironmentHandler 动作被"半截键"误触)
        {
            KeyGateLogic g;
            CHECK(g.filter(true, 'o', true));    // 打字中按下:吞并记账
            CHECK(g.filter(false, 'o', false));  // 失焦后松开:配对吞
            CHECK(!g.filter(false, 'o', false)); // 记账已清:再来一枚孤立 KEYUP 放行
        }

        // 反向:按下时不在打字(动作已在 KEYDOWN 侧发生),松开时在打字 → KEYUP 吞
        // (KEYUP 触发型动作不该发生在打字态;KEYDOWN 触发型早已完成,吞无副作用)
        {
            KeyGateLogic g;
            CHECK(!g.filter(true, 'w', false));
            CHECK(g.filter(false, 'w', true));
        }

        // 各键独立记账,互不串扰;正常按下会清掉同键陈旧记账
        {
            KeyGateLogic g;
            CHECK(g.filter(true, 'a', true));    // 'a' 打字态按下,记账
            CHECK(!g.filter(true, 'b', false));  // 'b' 非打字态按下,放行
            CHECK(!g.filter(false, 'b', false)); // 'b' 松开,放行
            CHECK(g.filter(false, 'a', false));  // 'a' 配对吞
            CHECK(!g.filter(true, 'a', false));  // 'a' 非打字态重按:清账+放行
            CHECK(!g.filter(false, 'a', false)); // 'a' 松开:放行(账已清)
        }

        std::cout << "KeyGateLogic tests OK\n";
    }

    // ---- AIChatCore::addErrorNote()(v0.15-vision 收尾修复:MediaManager 异步生图/生视频
    // 失败路径用它把错误呈现进聊天记录,复用既有 ChatEntry::ERR 的红色渲染,见 ai_ui.cpp)----
    {
        FakeProvider fp; ToolRegistry reg8;
        AIChatCore core(&fp, &reg8);
        core.addErrorNote("test error");
        std::vector<ChatEntry> ts = core.transcript();
        CHECK(!ts.empty());
        CHECK(ts.back().kind == ChatEntry::ERR);
        CHECK(ts.back().text == "test error");
        std::cout << "AIChatCore::addErrorNote appends ERR entry OK\n";
    }

    // ---- 每条用户请求必须自动携带当下 Earth 工作区上下文 ----
    {
        struct ContextCaptureProvider : public LLMProvider
        {
            std::vector<std::string> contents;
            virtual LLMTurn chat(
                const std::string& contentsJson, const std::string&)
            {
                contents.push_back(contentsJson);
                LLMTurn turn;
                turn.text = u8"已读取当前报告";
                return turn;
            }
        } provider;
        ToolRegistry registry;
        AIChatCore core(&provider, &registry);
        int revision = 7;
        core.setContextProvider([&revision]() {
            return std::string("{\"schema\":\"earth-context-v1\",")
                + "\"workspace\":{\"activeModule\":\"science\"},"
                + "\"scienceWorkbench\":{\"revision\":"
                + std::to_string(revision)
                + ",\"activeArtifact\":{\"artifactId\":\"era5-report-3\","
                  "\"series\":[{\"variableId\":\"temperature_2m_mean\","
                  "\"unit\":\"C\",\"years\":[2024,2025],"
                  "\"values\":[22.7,22.589]}]}}}";
        });

        core.submit(u8"解读当下报告");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();
        CHECK(provider.contents.size() == 1);
        picojson::value firstContents;
        CHECK(picojson::parse(firstContents, provider.contents[0]).empty());
        const std::string firstText = firstContents.get<picojson::array>()[0]
            .get("parts").get<picojson::array>()[0].get("text").to_str();
        CHECK(firstText.find("EARTH_CONTEXT_V1") != std::string::npos);
        CHECK(firstText.find("\"activeModule\":\"science\"") !=
              std::string::npos);
        CHECK(firstText.find("\"artifactId\":\"era5-report-3\"") !=
              std::string::npos);
        CHECK(firstText.find("\"unit\":\"C\"") != std::string::npos);
        CHECK(firstText.find(u8"解读当下报告") != std::string::npos);
        CHECK(core.transcript()[0].text == u8"解读当下报告");

        revision = 8;
        core.submit(u8"再按当前状态解释一次");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();
        CHECK(provider.contents.size() == 2);
        picojson::value secondContents;
        CHECK(picojson::parse(secondContents, provider.contents[1]).empty());
        const picojson::array& secondTurns =
            secondContents.get<picojson::array>();
        const std::string secondText = secondTurns.back()
            .get("parts").get<picojson::array>()[0].get("text").to_str();
        CHECK(secondText.find("\"revision\":8") != std::string::npos);
        const std::string previousUserText = secondTurns.front()
            .get("parts").get<picojson::array>()[0].get("text").to_str();
        CHECK(previousUserText == u8"解读当下报告");
        std::cout << "AIChatCore live Earth context injection OK\n";
    }

    // ---- 上下文采集失败或过大时聊天仍可继续，且不会把提示塞爆请求 ----
    {
        struct ContextFailureProvider : public LLMProvider
        {
            std::string contents;
            virtual LLMTurn chat(
                const std::string& contentsJson, const std::string&)
            {
                contents = contentsJson;
                LLMTurn turn;
                turn.text = u8"上下文不可用，但请求仍已处理";
                return turn;
            }
        } provider;
        ToolRegistry registry;
        AIChatCore core(&provider, &registry);
        core.setContextProvider([]() -> std::string {
            throw std::runtime_error("panel snapshot unavailable");
        });
        core.submit(u8"继续普通对话");
        for (int i = 0; i < 500 && core.busy(); ++i)
        { core.drainMainThread(); usleep(10000); }
        core.drainMainThread();
        CHECK(provider.contents.find(u8"继续普通对话") != std::string::npos);
        CHECK(provider.contents.find("context_unavailable") !=
              std::string::npos);
        CHECK(!core.busy());

        ContextFailureProvider oversizedProvider;
        AIChatCore oversizedCore(&oversizedProvider, &registry);
        oversizedCore.setContextProvider([]() {
            return std::string("{\"payload\":\"") +
                std::string(600 * 1024, 'x') + "\"}";
        });
        oversizedCore.submit(u8"读取当前状态");
        for (int i = 0; i < 500 && oversizedCore.busy(); ++i)
        { oversizedCore.drainMainThread(); usleep(10000); }
        oversizedCore.drainMainThread();
        CHECK(oversizedProvider.contents.size() < 160 * 1024);
        CHECK(oversizedProvider.contents.find("context_oversize") !=
              std::string::npos);
        CHECK(oversizedProvider.contents.find(u8"读取当前状态") !=
              std::string::npos);
        std::cout << "AIChatCore Earth context failure boundaries OK\n";
    }

    return 0;
}
