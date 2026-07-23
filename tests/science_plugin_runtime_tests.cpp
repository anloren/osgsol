#include "../applications/earth_explorer/science_plugin_runtime.h"
#include "../applications/earth_explorer/terrain_science_overlay.h"

#include <dlfcn.h>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>

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

    int runLiveEraWorkbench(const std::string& indexPath)
    {
        SciencePluginRuntime runtime;
        if (!runtime.load(OSGSOL_TEST_SCIENCE_PLUGIN_REAL, indexPath))
        {
            std::cerr << runtime.error() << "\n";
            return 1;
        }
        std::string error;
        const auto dispatch = [&runtime, &error](const std::string& body)
        {
            return runtime.dispatchWorkbenchAction(
                "{\"schema\":\"science-workbench-action-v1\"," +
                body + "}", error);
        };
        if (!dispatch(
                "\"action\":\"update-target\",\"geometry\":{\"kind\":"
                "\"point\",\"point\":{\"latitude\":24.3658,"
                "\"longitude\":104.1796}}") ||
            !dispatch(
                "\"action\":\"select-source\",\"sourceId\":"
                "\"era5-agricultural-climate\"") ||
            !dispatch(
                "\"action\":\"set-method\",\"methodId\":"
                "\"annual-summary\"") ||
            !dispatch(
                "\"action\":\"set-year-range\",\"firstYear\":2017,"
                "\"lastYear\":2025") ||
            !dispatch("\"action\":\"run\""))
        {
            std::cerr << "live ERA5 workbench dispatch failed: "
                      << error << "\n";
            return 1;
        }

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(30);
        std::string snapshot;
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (!runtime.copyWorkbenchSnapshot(snapshot, error))
            {
                std::cerr << error << "\n";
                return 1;
            }
            if (snapshot.find("\"phase\":\"ready\"") !=
                    std::string::npos &&
                snapshot.find("\"activeArtifact\":{") !=
                    std::string::npos &&
                snapshot.find(
                    "\"sourceId\":\"era5-agricultural-climate\"") !=
                    std::string::npos &&
                snapshot.find("\"series\":[{") != std::string::npos)
            {
                std::cout << "[OK] live ERA5 workbench opened its report\n";
                return 0;
            }
            if (snapshot.find("\"phase\":\"failed\"") != std::string::npos)
            {
                std::cerr << snapshot << "\n";
                return 1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        std::cerr << "live ERA5 workbench timed out: " << snapshot << "\n";
        return 1;
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--live-era")
        return runLiveEraWorkbench(argv[2]);
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

        const auto dispatch = [&runtime, &error](
            const std::string& action)
        {
            return runtime.dispatchWorkbenchAction(
                "{\"schema\":\"science-workbench-action-v1\"," +
                action + "}", error);
        };
        const auto runAndCancel = [&dispatch, &error, &runtime](
            const std::string& sourceId,
            const std::string& methodId,
            int firstYear, int lastYear) -> bool
        {
            const auto step = [&dispatch, &error, &sourceId](
                const char* name, const std::string& action)
            {
                if (dispatch(action)) return true;
                std::cerr << "Science workbench " << sourceId << " "
                          << name << " failed: " << error << "\n";
                return false;
            };
            if (!step("select-source",
                    "\"action\":\"select-source\",\"sourceId\":\"" +
                    sourceId + "\""))
                return false;
            if (!step("set-method",
                    "\"action\":\"set-method\",\"methodId\":\"" +
                    methodId + "\""))
                return false;
            if (!step("set-year-range",
                    "\"action\":\"set-year-range\",\"firstYear\":" +
                    std::to_string(firstYear) + ",\"lastYear\":" +
                    std::to_string(lastYear)))
                return false;
            if (!step("run", "\"action\":\"run\""))
            {
                std::string current;
                if (runtime.copyWorkbenchSnapshot(current, error))
                    std::cerr << current << "\n";
                return false;
            }
            std::string current;
            if (!runtime.copyWorkbenchSnapshot(current, error)) return false;
            const bool active =
                current.find("\"phase\":\"queued\"") != std::string::npos ||
                current.find("\"phase\":\"fetching\"") != std::string::npos ||
                current.find("\"phase\":\"analyzing\"") != std::string::npos;
            if (active && !step("cancel", "\"action\":\"cancel\""))
                return false;
            if (!error.empty()) return false;
            return true;
        };

        CHECK(runAndCancel(
            "era5-land-surface-history", "annual-summary", 2024, 2025));
        CHECK(runAndCancel(
            "era5-agricultural-climate", "annual-summary", 2024, 2025));
        CHECK(runAndCancel(
            "sentinel-2-l2a", "satellite-preview", 2025, 2025));
        CHECK(runAndCancel(
            "copernicus-dem-glo-30", "terrain-preview", 2021, 2021));
    }

    std::cout << "[OK] ScienceEarth plugin runtime contract\n";
    return 0;
}
