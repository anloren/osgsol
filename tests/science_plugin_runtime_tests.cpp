#include "../applications/earth_explorer/science_plugin_runtime.h"

#include <dlfcn.h>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x "\n"; return 1; } } while (0)

namespace
{
    typedef const int* (*CounterFunction)();

    const int* fakeCounters()
    {
        void* handle = dlopen(OSGSOL_TEST_SCIENCE_PLUGIN, RTLD_NOW | RTLD_LOCAL);
        if (!handle) return nullptr;
        CounterFunction function = reinterpret_cast<CounterFunction>(
            dlsym(handle, "osgsol_science_test_counters"));
        return function ? function() : nullptr;
    }
}

int main()
{
    {
        SciencePluginRuntime runtime;
        CHECK(runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN, "index.sqlite"));
        CHECK(runtime.available());
        CHECK(runtime.error().empty());
        CHECK(runtime.sceneNode() == nullptr);
        runtime.setVisible(true);
        runtime.registerAiTools(nullptr, nullptr, nullptr);
        runtime.drawOperations(nullptr, nullptr);
        runtime.drawResults(nullptr);
        const int* counters = fakeCounters();
        CHECK(counters != nullptr);
        CHECK(counters[0] == 1);
        CHECK(counters[1] == 0);
        CHECK(counters[2] == 1);
        CHECK(counters[3] == 1);
        CHECK(counters[4] == 1);
        CHECK(counters[5] == 1);
        CHECK(counters[6] == 1);
    }
    CHECK(fakeCounters() != nullptr);
    CHECK(fakeCounters()[1] == 1);

    {
        SciencePluginRuntime runtime;
        CHECK(!runtime.load("/definitely/missing/osgdb_science.so", "index"));
        CHECK(!runtime.available());
        CHECK(runtime.error().find("load") != std::string::npos);
        runtime.setVisible(false);
        runtime.drawOperations(nullptr, nullptr);
        runtime.drawResults(nullptr);
    }
    {
        SciencePluginRuntime runtime;
        CHECK(!runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN_MISSING_ANCHOR, "index"));
        CHECK(runtime.error().find("anchor") != std::string::npos);
    }
    {
        SciencePluginRuntime runtime;
        CHECK(!runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN_BAD_ABI, "index"));
        CHECK(runtime.error().find("ABI") != std::string::npos);
    }
    {
        SciencePluginRuntime runtime;
        CHECK(!runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN_NULL_FUNCTION, "index"));
        CHECK(runtime.error().find("function") != std::string::npos);
    }
    {
        SciencePluginRuntime runtime;
        CHECK(!runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN, "fail"));
        CHECK(runtime.error() == "fake create failure");
    }

    std::cout << "[OK] ScienceEarth plugin runtime contract\n";
    return 0;
}
