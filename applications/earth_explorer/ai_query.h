#pragma once
// P4:AI 工具用的异步 JSON 抓取器。工具 execute 跑在主线程(ai_tools.h 线程契约),不能
// 阻塞网络 → 首次调用登记抓取任务立即返回 Fetching,后台 worker 抓完落缓存,模型稍后
// 重调同一工具命中 Ready——与 FeedLayer「懒开启+loadingNote」同一交互范式。
// 失败结果消费一次即弃(下次调用重试),TTL 内绝不重复入队(限流友好)。
#include <OpenThreads/Thread>
#include <OpenThreads/Mutex>
#include <OpenThreads/ScopedLock>
#include <atomic>
#include <deque>
#include <map>
#include <string>
#include <functional>

namespace earthai
{
    enum class QueryState { Fetching, Ready, Failed };

    class AsyncJsonFetcher
    {
    public:
        typedef std::function<bool(const std::string& url, std::string& bodyOut,
                                   std::string& errOut)> FetchFn;
        explicit AsyncJsonFetcher(FetchFn fn = FetchFn());   // 空=缺省 libhv 同步 GET(15s,worker 内调)
        ~AsyncJsonFetcher();

        // 主线程调用。fixturePath 非空 → 同步读本地文件(离线/单测),不走网络。
        // Ready:bodyOut 有效;Fetching:已入队或抓取中;Failed:errOut 有效。
        QueryState query(const std::string& url, double ttlSeconds,
                         const std::string& fixturePath, std::string& bodyOut, std::string& errOut);
        void waitIdleForTest();   // 单测辅助:轮询等队列清空,封顶 5s

    private:
        struct Entry { std::string body, err; double fetchedAt = 0.0; bool ok = false; bool inflight = false; };
        class Worker;
        FetchFn _fetch;
        OpenThreads::Mutex _mutex;
        std::map<std::string, Entry> _cache;
        std::deque<std::string> _queue;
        Worker* _worker;
        std::atomic<bool> _done;
        friend class Worker;
    };
}
