#ifndef EARTH_AI_CHAT_H
#define EARTH_AI_CHAT_H
// AI Chat 代理循环:LLMProvider 抽象 + FakeProvider(离线可测) + AIChatCore 状态机。
// 本头文件不依赖 OSG,纯 std + picojson,可被 tests/ 单测直接 include 实现文件。
#include "ai_tools.h"
#include <functional>
#include <thread>

namespace earthai
{
    // 一轮模型输出:要么纯文本,要么一批函数调用。
    // rawPartJson:模型返回的整个 functionCall part 的原样序列化(含 thoughtSignature 等
    // 未建模字段)。gemini-3.5-flash 起,把含 functionCall 的历史发回去时必须原样带上
    // thought_signature,否则 400 "Function call is missing a thought_signature"。
    // 该字段可能在 functionCall 里也可能与其平级——不做假设,整个 part 原文保留。
    // FakeProvider 路径不产生此字段(为空),序列化时走重建分支。
    struct FunctionCall { std::string name; picojson::value args; std::string rawPartJson; };
    struct LLMTurn { std::string text; std::vector<FunctionCall> calls; std::string error; };

    class LLMProvider
    {
    public:
        virtual ~LLMProvider() {}
        // contentsJson: Gemini contents 数组(序列化字符串);declsJson: functionDeclarations 数组。
        // 同步调用,由 AIChatCore 的工作线程执行,不得在主线程调。
        virtual LLMTurn chat(const std::string& contentsJson, const std::string& declsJson) = 0;
    };

    // EARTH_AI_FAKE=<json文件>:数组,按序出队,每项 {"text":...} 或 {"calls":[{name,args}]}
    class FakeProvider : public LLMProvider
    {
    public:
        bool loadFromFile(const std::string& path);
        bool loadFromString(const std::string& json);
        virtual LLMTurn chat(const std::string& contentsJson, const std::string& declsJson);
    private:
        std::vector<LLMTurn> _script; size_t _cursor = 0; std::mutex _mutex;
    };

    struct ChatEntry { enum Kind { USER, ASSISTANT, TOOL_NOTE, ERR } kind; std::string text; };

    // 会话历史条目(内部使用),用于序列化 Gemini contents 数组
    struct HistoryItem
    {
        enum Role { USER_TEXT, MODEL_TEXT, MODEL_CALL, TOOL_RESPONSE } role;
        std::string text;              // USER_TEXT / MODEL_TEXT 用
        std::string callName;          // MODEL_CALL / TOOL_RESPONSE 用
        picojson::value callArgs;      // MODEL_CALL 用(functionCall.args)
        picojson::value toolResponse;  // TOOL_RESPONSE 用(functionResponse.response)
        std::string rawPartJson;       // MODEL_CALL 用:原样 part(含 thoughtSignature),
                                       // 非空则序列化时原文回发,空则按 name+args 重建
    };

    // 把历史条目序列化为 Gemini contents JSON 数组字符串;Task 3 的 GeminiProvider 直接复用
    std::string buildContentsJson(const std::vector<HistoryItem>& history);

    // 纯解析函数(不含网络):把 Gemini generateContent 的响应体解析成 LLMTurn。
    // 抽出来单独可测——防御式解析,任何字段缺失/类型不符都落到 turn.error,不抛异常。
    LLMTurn parseGeminiResponse(const std::string& body);

    // 真网络 Provider:调用 Gemini REST API(v1beta generateContent),同步阻塞,供 worker 线程调用。
    class GeminiProvider : public LLMProvider
    {
    public:
        GeminiProvider(const std::string& apiKey, const std::string& model);
        void setSystemPrompt(const std::string& p) { _systemPrompt = p; }
        virtual LLMTurn chat(const std::string& contentsJson, const std::string& declsJson);
    private:
        std::string _apiKey, _model, _systemPrompt;
    };

    class AIChatCore
    {
    public:
        AIChatCore(LLMProvider* p, ToolRegistry* reg);   // 不持有 provider/registry 所有权
        ~AIChatCore();
        void submit(const std::string& userText);        // 主线程调;若 busy 则忽略并提示
        bool busy() const;
        // 主线程每帧调:执行排队的工具调用(工具必须主线程),推进代理循环
        void drainMainThread();
        std::vector<ChatEntry> transcript() const;       // UI 读(加锁快照)
        // 供外部子系统(如 MediaManager 的异步生图/生视频失败路径)把错误呈现到聊天记录里,
        // 复用既有 ChatEntry::ERR 的红色渲染(ai_ui.cpp),不用各自发明一套错误 UI。
        // 线程安全:内部加 _mutex 锁,任意线程调用都安全(尽管目前唯一调用方 MediaManager::update()
        // 是主线程调,加锁是为了和 _transcript 的其它读写保持一致的保护,不依赖调用方在哪个线程)。
        void addErrorNote(const std::string& text);
        // 成功接受一条新用户指令时调用；busy 而被拒绝的 submit 不触发。照片两阶段门控
        // 用它区分“同一指令里刚飞到就立刻拍”和“用户看过画面后再次确认拍摄”。
        void setSubmitAcceptedCallback(const std::function<void(const std::string&)>& callback);
        // 仅测试用:导出当前 _history 序列化后的 Gemini contents JSON,
        // 供单测校验 functionCall/functionResponse 严格配对;业务代码不要调用
        std::string historyContentsForTest() const;
        // 注:系统提示词不在这里设置——请调用 GeminiProvider::setSystemPrompt(),
        // AIChatCore 本身不读取/不转发任何 systemPrompt 字段。

    private:
        void startWorkerRound();   // 内部:基于当前 _history 拼 contents,起一轮工作线程
        void joinWorkerIfAny();    // 内部:等待并回收上一轮 worker(若还在跑)

        LLMProvider* _provider;
        ToolRegistry* _registry;

        mutable std::mutex _mutex;
        std::vector<ChatEntry> _transcript;     // 供 UI 读的会话文本
        std::vector<HistoryItem> _history;      // 供 contents 序列化的完整历史(含函数调用/结果)
        bool _busy = false;
        int _round = 0;                         // 当前 submit 的代理循环轮次计数
        // 上限不再硬编码——ai_chat.cpp 里改读 earthcfg::getInt("ai.maxRounds")(默认 30,
        // 设置面板可调)。撞上限后不再硬报错中断,而是把下面这个标志置位,强制紧接着的
        // 一轮请求不带 tools 声明,逼模型只用历史里已有的工具结果给出纯文本收尾。
        // 线程判断:本标志只在主线程读写——submit()/drainMainThread()/startWorkerRound()
        // 均由主线程调用;startWorkerRound() 把它读出决定 declsJson 后立即清零、再把
        // contentsJson/declsJson 的字符串快照按值传给 worker 线程,worker 线程本身不碰
        // 这个标志。因此普通 bool 已足够,不需要 atomic;为与本类其余状态字段一致的加锁
        // 纪律,仍在 _mutex 保护下读写(见 ai_chat.cpp)。
        bool _forceNoTools = false;
        std::function<void(const std::string&)> _submitAcceptedCallback;

        // 工作线程产出,主线程 drain 时消费
        std::vector<ChatEntry> _pendingEntries;
        std::vector<FunctionCall> _pendingCalls;
        bool _hasPendingResult = false;         // worker 已产出(文本/错误/调用)但还未被 drain 消费
        std::string _pendingAssistantText;      // worker 产出的纯文本回复,drain 时补记入 _history
        bool _hasPendingAssistantText = false;

        std::thread _worker;
        bool _workerJoinable = false;
    };
}
#endif
