// applications/earth_explorer/ai_chat.cpp
// AIChatCore 代理循环实现 + FakeProvider(离线可测)。
// 设计要点:worker 线程只读 provider->chat() 的输入(contents/decls 字符串快照),
// 产出结果通过 _mutex 保护的 _pending* 交给主线程;worker 绝不直接碰 registry/transcript。
#include "ai_chat.h"
#include "earth_config.h"
#include "3rdparty/libhv/all/client/requests.h"
#include <OpenThreads/Thread>
#include <fstream>
#include <sstream>
#include <iostream>

namespace earthai
{
    // 错误摘要截断:截到 200 字符——原文可能很长(base64/大段文本),错误提示没必要塞爆日志/UI。
    // drainMainThread(工具异常兜底)与 GeminiProvider(响应错误摘要)共用。
    static std::string truncate200(const std::string& s)
    {
        if (s.size() <= 200) return s;
        std::string r = s.substr(0, 200);
        // UTF-8 修补:硬切可能切在多字节字符中间,先去掉残缺的续字节(10xxxxxx),
        // 再去掉一个孤立的多字节首字节(11xxxxxx),避免输出乱码尾巴。
        while (!r.empty() && ((unsigned char)r.back() & 0xC0) == 0x80) r.pop_back();
        if (!r.empty() && (unsigned char)r.back() >= 0xC0) r.pop_back();
        return r;
    }

    // 连接层失败(resp 为空:DNS/TLS 握手/连接拒绝/超时无响应)自动重试,带线性退避。
    // 真实 HTTP 错误码(4xx/5xx)不重试(是服务端语义,重试无益)。retries 从配置读
    // (earthcfg "http.retries",默认 3)。与 ai_media.cpp 的同名 static helper 逻辑一致
    // (两处调用点各一份,量小不值得为此新建共享头)。
    static requests::Response httpRequestRetry(requests::Request req)
    {
        int retries = earthcfg::getInt("http.retries");   // 默认 3
        requests::Response resp;
        for (int attempt = 0; attempt <= retries; ++attempt)
        {
            resp = requests::request(req);
            if (resp) return resp;                          // 拿到任何响应(含 4xx/5xx)即返回
            if (attempt < retries)
                OpenThreads::Thread::microSleep((attempt + 1) * 300 * 1000);  // 300/600/900ms 退避
        }
        return resp;   // 仍为空 = 连接层彻底失败
    }

    // ---------------- FakeProvider ----------------

    static LLMTurn parseScriptItem(const picojson::value& item)
    {
        LLMTurn turn;
        if (item.contains("error"))
        {
            turn.error = item.get("error").to_str();
            return turn;
        }
        if (item.contains("text"))
            turn.text = item.get("text").to_str();
        if (item.contains("calls") && item.get("calls").is<picojson::array>())
        {
            const picojson::array& calls = item.get("calls").get<picojson::array>();
            for (size_t i = 0; i < calls.size(); ++i)
            {
                FunctionCall fc;
                fc.name = calls[i].get("name").to_str();
                if (calls[i].contains("args")) fc.args = calls[i].get("args");
                turn.calls.push_back(fc);
            }
        }
        return turn;
    }

    bool FakeProvider::loadFromString(const std::string& json)
    {
        picojson::value v; std::string err = picojson::parse(v, json);
        if (!err.empty() || !v.is<picojson::array>()) return false;

        std::lock_guard<std::mutex> g(_mutex);
        _script.clear(); _cursor = 0;
        const picojson::array& arr = v.get<picojson::array>();
        for (size_t i = 0; i < arr.size(); ++i)
            _script.push_back(parseScriptItem(arr[i]));
        return true;
    }

    bool FakeProvider::loadFromFile(const std::string& path)
    {
        std::ifstream ifs(path.c_str());
        if (!ifs.is_open()) return false;
        std::stringstream ss; ss << ifs.rdbuf();
        return loadFromString(ss.str());
    }

    LLMTurn FakeProvider::chat(const std::string& /*contentsJson*/, const std::string& /*declsJson*/)
    {
        std::lock_guard<std::mutex> g(_mutex);
        if (_cursor >= _script.size())
        {
            LLMTurn turn; turn.error = "fake script exhausted"; return turn;
        }
        return _script[_cursor++];
    }

    // ---------------- contents JSON 序列化 ----------------

    std::string buildContentsJson(const std::vector<HistoryItem>& history)
    {
        std::string s = "[";
        for (size_t i = 0; i < history.size(); ++i)
        {
            const HistoryItem& h = history[i];
            picojson::object entry;
            picojson::object part;
            bool useRawPart = false; picojson::value rawPart;
            switch (h.role)
            {
            case HistoryItem::USER_TEXT:
                entry["role"] = picojson::value("user");
                part["text"] = picojson::value(h.text);
                break;
            case HistoryItem::MODEL_TEXT:
                entry["role"] = picojson::value("model");
                part["text"] = picojson::value(h.text);
                break;
            case HistoryItem::MODEL_CALL:
            {
                entry["role"] = picojson::value("model");
                // 有原样 part(真 Gemini 响应解析而来)→ 原文回发,保住 thoughtSignature 等
                // 未建模字段(gemini-3.5-flash 缺它会 400)。parse 失败或非对象则回退重建。
                if (!h.rawPartJson.empty())
                {
                    picojson::value rv; std::string rerr = picojson::parse(rv, h.rawPartJson);
                    if (rerr.empty() && rv.is<picojson::object>()) { useRawPart = true; rawPart = rv; }
                }
                if (!useRawPart)   // FakeProvider / 旧数据路径:按 name+args 重建
                {
                    picojson::object fc;
                    fc["name"] = picojson::value(h.callName);
                    fc["args"] = h.callArgs.is<picojson::null>() ? picojson::value(picojson::object()) : h.callArgs;
                    if (!h.callId.empty()) fc["id"] = picojson::value(h.callId);
                    part["functionCall"] = picojson::value(fc);
                }
                break;
            }
            case HistoryItem::TOOL_RESPONSE:
            {
                entry["role"] = picojson::value("user");
                picojson::object fr;
                fr["name"] = picojson::value(h.callName);
                if (!h.callId.empty()) fr["id"] = picojson::value(h.callId);
                fr["response"] = h.toolResponse.is<picojson::null>() ? picojson::value(picojson::object()) : h.toolResponse;
                part["functionResponse"] = picojson::value(fr);
                break;
            }
            }
            picojson::array parts; parts.push_back(useRawPart ? rawPart : picojson::value(part));
            entry["parts"] = picojson::value(parts);
            s += picojson::value(entry).serialize();
            if (i + 1 < history.size()) s += ",";
        }
        return s + "]";
    }

    // ---------------- AIChatCore ----------------

    AIChatCore::AIChatCore(LLMProvider* p, ToolRegistry* reg)
        : _provider(p), _registry(reg)
    {}

    AIChatCore::~AIChatCore()
    {
        // 析构时若还有活着的 worker,必须 join,避免进程退出时崩溃
        joinWorkerIfAny();
    }

    void AIChatCore::joinWorkerIfAny()
    {
        if (_workerJoinable && _worker.joinable())
        {
            _worker.join();
            _workerJoinable = false;
        }
    }

    void AIChatCore::submit(const std::string& userText)
    {
        std::function<void(const std::string&)> acceptedCallback;
        {
            std::lock_guard<std::mutex> g(_mutex);
            if (_busy)
            {
                ChatEntry e; e.kind = ChatEntry::TOOL_NOTE; e.text = u8"上一条还在处理中";
                _transcript.push_back(e);
                return;
            }
            ChatEntry ue; ue.kind = ChatEntry::USER; ue.text = userText;
            _transcript.push_back(ue);

            HistoryItem hi; hi.role = HistoryItem::USER_TEXT; hi.text = userText;
            _history.push_back(hi);

            // 历史裁剪:按条目数粗略限长(约 10 轮 user+assistant 交换,含工具调用/结果条目)。
            // 切点必须落在用户文本边界:硬切可能停在 MODEL_CALL 与 TOOL_RESPONSE 之间,
            // 或让历史以非 user 角色开头——两种情况真实 Gemini API 都会 400。
            // 因此算出目标切点后向后推进到下一个 USER_TEXT 才 erase。
            const size_t kMaxHistoryItems = 40;
            if (_history.size() > kMaxHistoryItems)
            {
                size_t cut = _history.size() - kMaxHistoryItems;
                while (cut < _history.size() && _history[cut].role != HistoryItem::USER_TEXT) ++cut;
                _history.erase(_history.begin(), _history.begin() + cut);
            }

            _busy = true;
            _round = 0;
            acceptedCallback = _submitAcceptedCallback;
        }
        if (acceptedCallback) acceptedCallback(userText);
        startWorkerRound();
    }

    void AIChatCore::startWorkerRound()
    {
        std::string contentsJson;
        std::string declsJson;
        {
            std::lock_guard<std::mutex> g(_mutex);
            contentsJson = buildContentsJson(_history);
            // _forceNoTools 为真(刚撞了工具循环上限)时不带 functionDeclarations——
            // GeminiProvider::chat 只在 declsJson 非空且非 "[]" 时才拼 "tools" 字段,
            // 空串等效于"这一轮不给模型任何工具",逼它只能用历史里已有的工具结果收尾。
            // 消费一次立即清零:只影响这一次请求,不影响之后的 submit。
            declsJson = _forceNoTools ? std::string() : _registry->buildDeclarationsJson();
            _forceNoTools = false;
        }

        // 回收上一轮 worker(调用此函数时,前一轮 worker 一定已经把结果写回 _pending* 并退出,
        // 因为 startWorkerRound 只在 submit()/drainMainThread() 里、确认拿到上一轮结果之后才被调用)
        LLMProvider* provider = _provider;
        joinWorkerIfAny();
        _worker = std::thread([this, provider, contentsJson, declsJson]()
        {
            // worker 线程:只调用 provider->chat,绝不碰 registry/transcript/_history 之外的东西,
            // 结果一律通过加锁的 _pending* 交回主线程处理。
            // 注意:_busy 一律由主线程的 drainMainThread 清除(消费完 pending 之后),
            // worker 不清 busy——否则"busy 已清但回复还没上屏"的窗口里 submit 会插队打乱会话。
            LLMTurn turn;
            try { turn = provider->chat(contentsJson, declsJson); }
            catch (const std::exception& e)
            {
                // Provider 内部异常(如 picojson::get 类型不符)转成错误结果,避免异常逃逸 std::terminate
                turn = LLMTurn(); turn.error = std::string("provider exception: ") + e.what();
            }

            std::lock_guard<std::mutex> g(_mutex);
            if (!turn.error.empty())
            {
                ChatEntry e; e.kind = ChatEntry::ERR; e.text = turn.error;
                _pendingEntries.push_back(e);
            }
            else if (!turn.calls.empty())
            {
                _pendingCalls = turn.calls; // 等 drainMainThread 在主线程执行工具
            }
            else
            {
                ChatEntry e; e.kind = ChatEntry::ASSISTANT; e.text = turn.text;
                _pendingEntries.push_back(e);
                _pendingAssistantText = turn.text;
                _hasPendingAssistantText = true;
            }
            _hasPendingResult = true;
        });
        _workerJoinable = true;
    }

    bool AIChatCore::busy() const
    {
        std::lock_guard<std::mutex> g(_mutex);
        return _busy;
    }

    void AIChatCore::drainMainThread()
    {
        std::vector<FunctionCall> callsToRun;
        bool haveCalls = false;

        {
            std::lock_guard<std::mutex> g(_mutex);
            if (!_hasPendingResult) return;
            _hasPendingResult = false;

            // 把 worker 产出的文本/错误条目搬进 transcript
            for (size_t i = 0; i < _pendingEntries.size(); ++i)
                _transcript.push_back(_pendingEntries[i]);
            _pendingEntries.clear();

            if (_hasPendingAssistantText)
            {
                // 纯文本回复也要计入历史,否则下一次 submit 时模型看不到自己上一轮说了什么
                HistoryItem hi; hi.role = HistoryItem::MODEL_TEXT; hi.text = _pendingAssistantText;
                _history.push_back(hi);
                _hasPendingAssistantText = false;
                _pendingAssistantText.clear();
            }

            if (!_pendingCalls.empty())
            {
                // 记录 model 的 functionCall 条目到历史(用于下一轮 contents)
                for (size_t i = 0; i < _pendingCalls.size(); ++i)
                {
                    HistoryItem hi; hi.role = HistoryItem::MODEL_CALL;
                    hi.callName = _pendingCalls[i].name; hi.callArgs = _pendingCalls[i].args;
                    hi.callId = _pendingCalls[i].id;
                    hi.rawPartJson = _pendingCalls[i].rawPartJson;   // 原样 part 随历史保留
                    _history.push_back(hi);
                }
                callsToRun = _pendingCalls;
                _pendingCalls.clear();
                haveCalls = true;
            }

            // busy 只在这里(主线程、pending 已消费、回复已上屏)清除,
            // 关闭"worker 清 busy 但 drain 还没搬运回复"期间 submit 插队的窗口
            if (!haveCalls) _busy = false;
        }

        if (!haveCalls) return; // 纯文本或错误结果,这一轮代理循环到此结束

        // 代理循环轮次上限检查(在主线程执行工具前检查,避免无限循环)。
        // 上限可配置(earthcfg "ai.maxRounds",默认 30,设置面板可调),不再硬编码。
        int maxRounds = 0;
        bool limitReached = false;
        {
            std::lock_guard<std::mutex> g(_mutex);
            ++_round;
            maxRounds = earthcfg::getInt("ai.maxRounds");
            if (_round > maxRounds) limitReached = true;
        }

        if (limitReached)
        {
            {
                std::lock_guard<std::mutex> g(_mutex);
                // 上面已把这批 functionCall 记入 _history,若就此丢弃会留下"裸 functionCall",
                // 真实 Gemini API 要求 call/response 严格配对,不配对会让之后每次请求都 400。
                // 因此给每个未执行的调用补一条合成 functionResponse,保持历史合法——这批调用
                // 本身不再执行(撞上限后不再信任模型继续调工具)。
                for (size_t i = 0; i < callsToRun.size(); ++i)
                {
                    picojson::object errObj;
                    errObj["error"] = picojson::value("tool loop limit reached");
                    HistoryItem hi; hi.role = HistoryItem::TOOL_RESPONSE;
                    hi.callName = callsToRun[i].name; hi.callId = callsToRun[i].id;
                    hi.toolResponse = picojson::value(errObj);
                    _history.push_back(hi);
                }
                // 不再硬报错中断对话——改为紧接着强制再走一轮"无工具"请求,让模型只能用
                // 历史里已经拿到的工具结果产出纯文本最终回答。transcript 只加一条温和提示
                // (TOOL_NOTE,ai_ui.cpp 里渲染成灰色),不再是刺眼打断会话的 ERR。
                ChatEntry e; e.kind = ChatEntry::TOOL_NOTE;
                e.text = u8"已达工具调用上限(" + std::to_string(maxRounds) + u8"),基于已获取的数据作答";
                _transcript.push_back(e);
                _forceNoTools = true;
            }
            // busy 保持 true:等这一轮强制无工具请求的结果(文本或错误)被 drain 消费后才清 busy
            startWorkerRound();
            return;
        }

        // 主线程执行每个工具调用(这是整个设计的重点:工具必须在主线程跑,因为它们会碰 OSG 场景图)
        for (size_t i = 0; i < callsToRun.size(); ++i)
        {
            const FunctionCall& fc = callsToRun[i];
            picojson::value result;
            // 工具异常兜底(集中一处,工具本身不必各自 try/catch):模型可能把参数发错类型/
            // 漏发必填参数,picojson 的 get() 会抛 std::runtime_error;若不捕获,异常会穿出
            // FRAME handler 直接 terminate 进程且 _busy 卡死。这里把任何异常转成 error
            // functionResponse 回传模型(历史配对不破坏,busy 流转不变),让模型自行纠错。
            try { _registry->dispatch(fc.name, fc.args, result); }
            catch (const std::exception& ex)
            {
                picojson::object errObj;
                errObj["error"] = picojson::value("tool threw: " + truncate200(ex.what()));
                result = picojson::value(errObj);
            }
            catch (...)
            {
                picojson::object errObj;
                errObj["error"] = picojson::value("tool threw: unknown exception");
                result = picojson::value(errObj);
            }

            // 内置中文字体(ChineseFull 范围)不含 ⚙ 的 glyph,会渲染成方块(tofu),改用文字前缀。
            std::string note = u8"[工具] " + fc.name + "(" + fc.args.serialize() + ")";
            std::lock_guard<std::mutex> g(_mutex);
            ChatEntry e; e.kind = ChatEntry::TOOL_NOTE; e.text = note;
            _transcript.push_back(e);

            HistoryItem hi; hi.role = HistoryItem::TOOL_RESPONSE;
            hi.callName = fc.name; hi.callId = fc.id; hi.toolResponse = result;
            _history.push_back(hi);
        }

        // 起下一轮 worker(带上最新的工具结果),busy 保持 true 直到这一轮 worker 产出文本/错误
        startWorkerRound();
    }

    std::vector<ChatEntry> AIChatCore::transcript() const
    {
        std::lock_guard<std::mutex> g(_mutex);
        return _transcript;
    }

    void AIChatCore::addErrorNote(const std::string& text)
    {
        std::lock_guard<std::mutex> g(_mutex);
        ChatEntry e; e.kind = ChatEntry::ERR; e.text = text;
        _transcript.push_back(e);
    }

    void AIChatCore::setSubmitAcceptedCallback(
        const std::function<void(const std::string&)>& callback)
    {
        std::lock_guard<std::mutex> g(_mutex);
        _submitAcceptedCallback = callback;
    }

    std::string AIChatCore::historyContentsForTest() const
    {
        // 仅测试用:加锁快照 _history 并序列化,用于校验 call/response 配对
        std::lock_guard<std::mutex> g(_mutex);
        return buildContentsJson(_history);
    }

    // ---------------- GeminiProvider ----------------

    LLMTurn parseGeminiResponse(const std::string& body)
    {
        LLMTurn turn;
        picojson::value v;
        std::string perr = picojson::parse(v, body);
        if (!perr.empty() || !v.is<picojson::object>())
        { turn.error = "bad json: " + truncate200(perr.empty() ? body : perr); return turn; }

        // candidates 缺失/为空:多半是 safety block 或顶层 error,尽量把原因带出来
        if (!v.contains("candidates") || !v.get("candidates").is<picojson::array>()
            || v.get("candidates").get<picojson::array>().empty())
        {
            if (v.contains("promptFeedback") && v.get("promptFeedback").is<picojson::object>()
                && v.get("promptFeedback").contains("blockReason"))
            {
                turn.error = "blocked: " + truncate200(v.get("promptFeedback").get("blockReason").to_str());
            }
            else if (v.contains("error") && v.get("error").is<picojson::object>()
                     && v.get("error").contains("message"))
            {
                turn.error = "api error: " + truncate200(v.get("error").get("message").to_str());
            }
            else turn.error = "no candidates: " + truncate200(body);
            return turn;
        }

        const picojson::value& cand0 = v.get("candidates").get<picojson::array>()[0];
        if (!cand0.is<picojson::object>() || !cand0.contains("content")
            || !cand0.get("content").is<picojson::object>()
            || !cand0.get("content").contains("parts")
            || !cand0.get("content").get("parts").is<picojson::array>())
        {
            // 也可能是 finishReason=SAFETY 等,没有 content——同样尽量带出原因
            if (cand0.is<picojson::object>() && cand0.contains("finishReason"))
                turn.error = "no content: finishReason=" + truncate200(cand0.get("finishReason").to_str());
            else turn.error = "no content parts: " + truncate200(body);
            return turn;
        }

        const picojson::array& parts = cand0.get("content").get("parts").get<picojson::array>();
        for (size_t i = 0; i < parts.size(); ++i)
        {
            const picojson::value& part = parts[i];
            if (!part.is<picojson::object>()) continue;
            if (part.contains("functionCall") && part.get("functionCall").is<picojson::object>())
            {
                const picojson::value& fcv = part.get("functionCall");
                FunctionCall fc;
                if (fcv.contains("name")) fc.name = fcv.get("name").to_str();
                if (fc.name.empty()) continue; // 没有名字的 functionCall 没法执行,跳过
                fc.args = fcv.contains("args") ? fcv.get("args") : picojson::value(picojson::object());
                if (fcv.contains("id") && fcv.get("id").is<std::string>())
                    fc.id = fcv.get("id").to_str();
                // 整个 part 原样留存(含 thoughtSignature——它可能在 functionCall 里也可能
                // 与其平级,不做假设),回发历史时原文带上,见 buildContentsJson MODEL_CALL 分支
                fc.rawPartJson = part.serialize();
                turn.calls.push_back(fc);
            }
            else if (part.contains("text") && part.get("text").is<std::string>())
            {
                turn.text += part.get("text").to_str();
            }
        }

        if (turn.text.empty() && turn.calls.empty()) turn.error = "empty response";
        return turn;
    }

    GeminiProvider::GeminiProvider(const std::string& apiKey, const std::string& model)
        : _apiKey(apiKey), _model(model)
    {}

    LLMTurn GeminiProvider::chat(const std::string& contentsJson, const std::string& declsJson)
    {
        LLMTurn turn;
        std::string body = "{\"system_instruction\":{\"parts\":[{\"text\":" +
            picojson::value(_systemPrompt).serialize() + "}]},"
            "\"contents\":" + contentsJson;
        // Gemini 拒绝空 functionDeclarations 数组——declsJson 为 "[]"/空时整个 tools 字段都不带
        if (!declsJson.empty() && declsJson != "[]")
            body += ",\"tools\":[{\"functionDeclarations\":" + declsJson + "}]";
        body += "}";

        requests::Request req(new HttpRequest);
        req->method = HTTP_POST;
        req->timeout = 30;
        // 注意:key 拼在 URL 里,下面任何日志/错误信息都不得把 req->url 整串打印出来
        req->url = "https://generativelanguage.googleapis.com/v1beta/models/" + _model +
                   ":generateContent?key=" + _apiKey;
        req->headers["Content-Type"] = "application/json";
        req->body = body;

        requests::Response resp = httpRequestRetry(req);
        if (!resp || resp->status_code != 200)
        {
            // resp 为空 = 连接层失败(已重试仍无响应);区分于真实 HTTP 错误码。
            turn.error = resp
                ? ("HTTP " + std::to_string((int)resp->status_code) + ": " + truncate200(resp->body))
                : u8"网络连接失败(多次重试无响应):可能网络中断 / 请求超时 / TLS 握手失败,请检查网络后重试";
            return turn;
        }
        return parseGeminiResponse(resp->body);
    }
}
