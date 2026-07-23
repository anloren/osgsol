#include "../applications/earth_explorer/science_plugin_runtime.h"
#include "../applications/earth_explorer/terrain_science_overlay.h"

#include <dlfcn.h>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x "\n"; return 1; } } while (0)

namespace
{
    typedef const int* (*CounterFunction)();
    typedef const OsgSolScienceGuiBridgeV1* (*GuiBridgeFunction)();

    void* testAllocate(std::size_t, void*) { return nullptr; }
    void testFree(void*, void*) {}

    const int* fakeCounters()
    {
        void* handle = dlopen(OSGSOL_TEST_SCIENCE_PLUGIN, RTLD_NOW | RTLD_LOCAL);
        if (!handle) return nullptr;
        CounterFunction function = reinterpret_cast<CounterFunction>(
            dlsym(handle, "osgsol_science_test_counters"));
        return function ? function() : nullptr;
    }

    const OsgSolScienceGuiBridgeV1* fakeGuiBridge()
    {
        void* handle = dlopen(OSGSOL_TEST_SCIENCE_PLUGIN, RTLD_NOW | RTLD_LOCAL);
        if (!handle) return nullptr;
        GuiBridgeFunction function = reinterpret_cast<GuiBridgeFunction>(
            dlsym(handle, "osgsol_science_test_gui_bridge"));
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
        terrainoverlay::TerrainScienceOverlay overlay;
        runtime.bindGeoRaster(&overlay);
        runtime.setVisible(true);
        runtime.registerAiTools(nullptr, nullptr, nullptr);
        const OsgSolScienceGuiBridgeV1 gui = {
            reinterpret_cast<void*>(0x1234),
            testAllocate,
            testFree,
            reinterpret_cast<void*>(0x5678),
        };
        runtime.drawOperations(nullptr, nullptr, gui);
        runtime.drawResults(nullptr, gui);
        const int* counters = fakeCounters();
        CHECK(counters != nullptr);
        CHECK(counters[0] == 1);
        CHECK(counters[1] == 0);
        CHECK(counters[2] == 1);
        CHECK(counters[3] == 1);
        CHECK(counters[4] == 1);
        CHECK(counters[5] == 1);
        CHECK(counters[6] == 1);
        CHECK(counters[7] == 2);
        CHECK(counters[8] == 0);
        CHECK(counters[9] == 1);
        const OsgSolScienceGuiBridgeV1* forwarded = fakeGuiBridge();
        CHECK(forwarded != nullptr);
        CHECK(forwarded->context == gui.context);
        CHECK(forwarded->allocate == gui.allocate);
        CHECK(forwarded->deallocate == gui.deallocate);
        CHECK(forwarded->allocatorUserData == gui.allocatorUserData);
        const OsgSolScienceGuiBridgeV1 missingGui = {};
        runtime.drawOperations(nullptr, nullptr, missingGui);
        runtime.drawResults(nullptr, missingGui);
        CHECK(counters[4] == 1);
        CHECK(counters[5] == 1);
        CHECK(counters[7] == 2);
    }
    CHECK(fakeCounters() != nullptr);
    CHECK(fakeCounters()[1] == 1);
    CHECK(fakeCounters()[9] == 2);
    CHECK(fakeCounters()[10] == 0);
    CHECK(fakeCounters()[11] == 0);

    {
        SciencePluginRuntime runtime;
        CHECK(!runtime.load("/definitely/missing/osgdb_science.so", "index"));
        CHECK(!runtime.available());
        CHECK(runtime.error().find("load") != std::string::npos);
        runtime.setVisible(false);
        const OsgSolScienceGuiBridgeV1 gui = {};
        runtime.drawOperations(nullptr, nullptr, gui);
        runtime.drawResults(nullptr, gui);
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
    {
        SciencePluginRuntime runtime;
        CHECK(runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN_V4, "index"));
        CHECK(runtime.supportsWorkbenchUi());
        std::string snapshot, error;
        CHECK(runtime.copyWorkbenchSnapshot(snapshot, error));
        CHECK(error.empty());
        CHECK(snapshot ==
              "{\"revision\":7,\"schema\":\"science-workbench-ui-v1\"}");
        const std::string run =
            R"({"schema":"science-workbench-action-v1","action":"run"})";
        CHECK(runtime.dispatchWorkbenchAction(run, error));
        CHECK(error.empty());
        CHECK(runtime.copyWorkbenchSnapshot(snapshot, error));
        CHECK(snapshot.find("\"phase\":\"queued\"") != std::string::npos);
        CHECK(!runtime.dispatchWorkbenchAction("{}", error));
        CHECK(error == "fake action rejected");
    }
    {
        SciencePluginRuntime runtime;
        CHECK(runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN, "index"));
        CHECK(!runtime.supportsWorkbenchUi());
        std::string snapshot, error;
        CHECK(!runtime.copyWorkbenchSnapshot(snapshot, error));
        CHECK(error == "ScienceEarth workbench UI is unavailable");
    }
    {
        SciencePluginRuntime runtime;
        CHECK(runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN_REAL, "index.sqlite"));
        CHECK(runtime.supportsWorkbenchUi());
        std::string snapshot, error;
        CHECK(runtime.copyWorkbenchSnapshot(snapshot, error));
        CHECK(snapshot.find("science-workbench-ui-v1") != std::string::npos);
        CHECK(snapshot.find("era5-agricultural-climate") != std::string::npos);

        const std::string target =
            R"({"schema":"science-workbench-action-v1","action":"update-target","geometry":{"kind":"point","point":{"latitude":24.3658,"longitude":104.1796}}})";
        CHECK(runtime.dispatchWorkbenchAction(target, error));
        CHECK(runtime.copyWorkbenchSnapshot(snapshot, error));
        CHECK(snapshot.find("\"phase\":\"ready-to-run\"") !=
              std::string::npos);
        CHECK(snapshot.find("\"locked\":true") != std::string::npos);
    }

    std::cout << "[OK] ScienceEarth plugin runtime contract\n";
    return 0;
}
