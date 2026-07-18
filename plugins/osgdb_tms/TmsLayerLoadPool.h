#ifndef OSGVERSE_TMS_LAYER_LOAD_POOL_H
#define OSGVERSE_TMS_LAYER_LOAD_POOL_H

#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace osgVerse
{

class TmsLayerLoadPool
{
public:
    TmsLayerLoadPool() : TmsLayerLoadPool(defaultWorkerCount()) {}

    explicit TmsLayerLoadPool(int workers) : _stopping(false)
    {
        if (workers < 0) workers = 0;
        _threads.reserve(static_cast<size_t>(workers));
        for (int i = 0; i < workers; ++i)
            _threads.push_back(std::thread([this]() { workerLoop(); }));
    }

    ~TmsLayerLoadPool() { shutdown(); }

    static TmsLayerLoadPool& instance()
    {
        // This must be an owned function-local object, not a leaked pointer. Its destructor is
        // registered with the TMS plugin and joins every worker before dlclose unloads their code.
        static TmsLayerLoadPool pool;
        return pool;
    }

    void runAll(const std::vector<std::function<void()> >& tasks)
    {
        const size_t taskCount = tasks.size();
        if (taskCount <= 1)
        {
            for (size_t i = 0; i < taskCount; ++i) tasks[i]();
            return;
        }

        struct Latch
        {
            explicit Latch(int initial) : count(initial) {}
            void down()
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (--count == 0) condition.notify_all();
            }
            void wait()
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this]() { return count == 0; });
            }
            std::mutex mutex;
            std::condition_variable condition;
            int count;
        } latch(static_cast<int>(taskCount - 1));

        bool runInline = false;
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            runInline = _stopping || _threads.empty();
            if (!runInline)
            {
                for (size_t i = 1; i < taskCount; ++i)
                {
                    _queue.push_back([&tasks, i, &latch]() {
                        try { tasks[i](); } catch (...) {}
                        latch.down();
                    });
                }
            }
        }

        if (runInline)
        {
            for (size_t i = 0; i < taskCount; ++i) tasks[i]();
            return;
        }

        _queueCondition.notify_all();
        tasks[0]();
        latch.wait();
    }

    void shutdown()
    {
        std::vector<std::thread> threads;
        {
            std::lock_guard<std::mutex> lock(_queueMutex);
            if (_stopping && _threads.empty()) return;
            _stopping = true;
            threads.swap(_threads);
        }
        _queueCondition.notify_all();
        for (size_t i = 0; i < threads.size(); ++i)
        {
            if (threads[i].joinable()) threads[i].join();
        }
    }

    size_t workerCount() const
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        return _threads.size();
    }

    TmsLayerLoadPool(const TmsLayerLoadPool&) = delete;
    TmsLayerLoadPool& operator=(const TmsLayerLoadPool&) = delete;

private:
    static int defaultWorkerCount()
    {
        const char* environment = std::getenv("EARTH_TILE_POOL");
        return environment ? std::atoi(environment) : 8;
    }

    void workerLoop()
    {
        for (;;)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(_queueMutex);
                _queueCondition.wait(lock, [this]() {
                    return _stopping || !_queue.empty();
                });
                if (_queue.empty())
                {
                    if (_stopping) return;
                    continue;
                }
                task = _queue.front();
                _queue.pop_front();
            }
            task();
        }
    }

    mutable std::mutex _queueMutex;
    std::condition_variable _queueCondition;
    std::deque<std::function<void()> > _queue;
    std::vector<std::thread> _threads;
    bool _stopping;
};

}

#endif
