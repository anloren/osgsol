#include "ai_query.h"
#include "3rdparty/libhv/all/client/requests.h"
#include <fstream>
#include <sstream>
#include <time.h>

namespace earthai
{

class AsyncJsonFetcher::Worker : public OpenThreads::Thread
{
public:
    Worker(AsyncJsonFetcher* o) : _owner(o) {}
    virtual void run()
    {
        while (!_owner->_done)
        {
            std::string url;
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_owner->_mutex);
                if (!_owner->_queue.empty())
                { url = _owner->_queue.front(); _owner->_queue.pop_front(); }
            }
            if (url.empty()) { OpenThreads::Thread::microSleep(100 * 1000); continue; }
            std::string body, err; bool ok = false;
            try { ok = _owner->_fetch(url, body, err); }
            catch (const std::exception& ex) { ok = false; err = std::string("fetch exception: ") + ex.what(); }
            catch (...) { ok = false; err = "fetch exception (unknown)"; }
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_owner->_mutex);
                Entry& e = _owner->_cache[url];
                e.ok = ok; e.body = ok ? body : std::string();
                e.err = ok ? std::string() : (err.empty() ? std::string("fetch failed") : err);
                e.fetchedAt = (double)time(NULL); e.inflight = false;
            }
        }
    }
    AsyncJsonFetcher* _owner;
};

static bool defaultHttpGet(const std::string& url, std::string& bodyOut, std::string& errOut)
{
    requests::Request req(new HttpRequest);
    req->method = HTTP_GET; req->url = url; req->timeout = 15;
    requests::Response resp = requests::request(req);
    if (resp && resp->status_code == 200) { bodyOut = resp->body; return true; }
    std::ostringstream oss; oss << "status=" << (resp ? (int)resp->status_code : -1);
    errOut = oss.str(); return false;
}

AsyncJsonFetcher::AsyncJsonFetcher(FetchFn fn)
    : _fetch(fn ? fn : FetchFn(defaultHttpGet)), _worker(NULL), _done(false)
{ _worker = new Worker(this); _worker->start(); }

AsyncJsonFetcher::~AsyncJsonFetcher()
{ _done = true; if (_worker) { _worker->join(); delete _worker; _worker = NULL; } }

QueryState AsyncJsonFetcher::query(const std::string& url, double ttlSeconds,
    const std::string& fixturePath, std::string& bodyOut, std::string& errOut)
{
    if (!fixturePath.empty())
    {
        std::ifstream fin(fixturePath.c_str());
        if (!fin) { errOut = "fixture not found: " + fixturePath; return QueryState::Failed; }
        std::stringstream ss; ss << fin.rdbuf(); bodyOut = ss.str();
        return QueryState::Ready;
    }
    OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
    std::map<std::string, Entry>::iterator it = _cache.find(url);
    if (it != _cache.end())
    {
        Entry& e = it->second;
        if (e.inflight) return QueryState::Fetching;
        if (!e.ok)   // 失败结果消费一次即弃 → 下次调用重试
        { errOut = e.err; _cache.erase(it); return QueryState::Failed; }
        double age = (double)time(NULL) - e.fetchedAt;
        if (age < ttlSeconds) { bodyOut = e.body; return QueryState::Ready; }
        // ok 但过期 → 走下方重新入队(旧 body 弃用,保持语义简单)
    }
    Entry& e = _cache[url]; e.inflight = true;
    _queue.push_back(url);
    return QueryState::Fetching;
}

void AsyncJsonFetcher::waitIdleForTest()
{
    for (int i = 0; i < 50; ++i)   // 100ms × 50 = 5s 封顶
    {
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
            bool busy = !_queue.empty();
            for (std::map<std::string, Entry>::iterator it = _cache.begin();
                 !busy && it != _cache.end(); ++it)
                if (it->second.inflight) busy = true;
            if (!busy) return;
        }
        OpenThreads::Thread::microSleep(100 * 1000);
    }
}

}
