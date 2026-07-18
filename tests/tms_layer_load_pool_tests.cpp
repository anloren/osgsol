#include "../plugins/osgdb_tms/TmsLayerLoadPool.h"

#include <atomic>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

int main()
{
    osgVerse::TmsLayerLoadPool pool(8);
    CHECK(pool.workerCount() == 8);

    std::atomic<int> completed(0);
    std::vector<std::function<void()> > tasks;
    for (int i = 0; i < 5; ++i)
        tasks.push_back([&completed]() { completed.fetch_add(1); });
    pool.runAll(tasks);
    CHECK(completed.load() == 5);

    pool.shutdown();
    CHECK(pool.workerCount() == 0);
    pool.shutdown();

    const std::thread::id caller = std::this_thread::get_id();
    std::atomic<bool> ranInline(false);
    tasks.clear();
    tasks.push_back([&ranInline, caller]() {
        ranInline.store(std::this_thread::get_id() == caller);
    });
    pool.runAll(tasks);
    CHECK(ranInline.load());

    std::cout << "[OK] TMS layer workers stop before plugin unload\n";
    return 0;
}
