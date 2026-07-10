# osgsol Wave 0 Safety Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the Wave 0 default-build safety and release findings with regression coverage, keep unavailable optional runtimes explicitly unsupported, and deliver a fully verified private `codex/wave0-safety` branch.

**Architecture:** Establish CTest as the shared gate first, then land five independently reviewable batches: macOS release integrity, MCP lifecycle and parsing, ImGui draw-thread input dispatch, bounded untrusted input parsing, and optional-runtime support truthfulness. Security-sensitive parsers are extracted into small deterministic helpers; interactive examples remain examples and are never registered as offline tests.

**Tech Stack:** C++17, CMake/CTest 3.10+, OpenSceneGraph 3.6.5, ImGui, libhv, zlib, Bash, macOS `codesign`/`PlistBuddy`.

## Global Constraints

- Work only in `/Users/USER/osgsol` on `codex/wave0-safety`; do not merge `master`, create a pull request, or change GitHub visibility.
- Baseline tag `osgsol-baseline-2026-07-10` must resolve to `75e0a8f4a3b9638dd7e89bd90975b2a653b2fe9c` on private remote `anloren/osgsol`.
- Treat `docs/superpowers/2026-07-10-osgsol-full-project-review.md` and `docs/superpowers/research/2026-07-10-osgsol-full-project-review-findings.json` as the factual scope.
- For every production change, add the failing regression test or equivalent failure probe first and observe the expected failure before implementation.
- Every batch must pass its targeted test, the six offline tests (`Feeds`, `Ai_Chat`, `Ais`, `Satellite`, `TileOverlay`, `WorldTools`), and any required build before its independent commit.
- Do not modify findings outside D04, D01-D03, C01-C02, C06, I01-I02-I07, and the unavailable optional C03-C04-C10 status.
- Do not introduce broad refactors, new product features, public network tests, or changes to the globe shader stack.
- Run at most one `cmake --build` process at a time.
- Any EarthExplorer runtime verification must set `EARTH_OFFSCREEN=1`.
- Final packaging must reject an exported `EARTH_AI_KEY`, contain no key or `imgui.ini`, smoke-run before final `codesign --verify --deep --strict`, and use the fresh Release install tree.
- Current dependency decision is fixed by live cache evidence: `ONNXRUNTIME_INCLUDE_DIR-NOTFOUND`, `ONNXRUNTIME_LIB_DIR-NOTFOUND`, `BULLET_INCLUDE_DIR-NOTFOUND`, and `BULLET_LIB_DIR-NOTFOUND`; Wave 0 marks those capabilities unsupported instead of claiming fixes.

---

## File Map

- `CMakeLists.txt`: enable CTest when `BUILD_TESTING` is enabled.
- `tests/CMakeLists.txt`: own the `NEW_CTEST` helper and register only deterministic offline tests.
- `tests/package_macos_tests.sh`: end-to-end credential, smoke-run, bundle-mutation, and signature regression.
- `ui/ImGui.h`, `ui/ImGui.cpp`: resolve ImGui persistence outside an application bundle.
- `packaging/package_macos.sh`: reject key-bearing environments and fail closed on signing/verification.
- `tests/mcp_safety_tests.cpp`, `ai/McpServer.h`, `ai/McpServer.cpp`: cursor, worker exception, SSE disconnect, and stop lifecycle coverage.
- `ui/ImGuiInputQueue.h`: private immutable event queue and atomically published capture state.
- `tests/imgui_threading_tests.cpp`, `ui/ImGui.cpp`, `ui/ImGui3D.cpp`, `ui/CMakeLists.txt`: move input API calls and context startup onto the draw traversal.
- `plugins/osgdb_web/GzipUtils.h`, `plugins/osgdb_web/ReaderWriterWeb.cpp`: bounded gzip streaming.
- `readerwriter/SafeGltfInput.h`, `readerwriter/SafeGltfInput.cpp`, `readerwriter/LoadSceneGLTF.cpp`, `readerwriter/CMakeLists.txt`: checked 3D Tiles headers and bounded remote glTF resources.
- `tests/input_safety_tests.cpp`: deterministic gzip, 3D Tiles, and remote-chunk boundary regressions.
- `ai/CMakeLists.txt`, `animation/CMakeLists.txt`: explicit disabled status and conditional Bullet API installation.
- `docs/superpowers/2026-07-10-wave0-optional-support.md`: reproducible optional-runtime support matrix.

### Task 1: D04 CTest Offline Gate

**Files:**
- Modify: `CMakeLists.txt:1-15`
- Modify: `tests/CMakeLists.txt:41-54,128-133`

**Interfaces:**
- Consumes: existing `NEW_TEST(EXECUTABLE_NAME SOURCE_FILE)`.
- Produces: `NEW_CTEST(EXECUTABLE_NAME SOURCE_FILE TEST_LABEL TEST_TIMEOUT)` and the `offline` CTest label used by every later batch.

- [ ] **Step 1: Record the failing discovery probe**

Run:

```bash
ctest --test-dir /Users/USER/osgsol/build/osgsol_core -N -L offline
```

Expected: `Total Tests: 0`.

- [ ] **Step 2: Enable CTest at project scope**

Add immediately after `PROJECT(osgVerse)`:

```cmake
INCLUDE(CTest)
```

`CTest` creates the cache option `BUILD_TESTING` and calls `ENABLE_TESTING()` only when that option is on.

- [ ] **Step 3: Add the deterministic-test macro**

Add after `NEW_TEST` in `tests/CMakeLists.txt`:

```cmake
MACRO(NEW_CTEST EXECUTABLE_NAME SOURCE_FILE TEST_LABEL TEST_TIMEOUT)
    NEW_TEST(${EXECUTABLE_NAME} ${SOURCE_FILE})
    IF(BUILD_TESTING)
        ADD_TEST(NAME ${EXECUTABLE_NAME} COMMAND $<TARGET_FILE:${EXECUTABLE_NAME}>)
        SET_TESTS_PROPERTIES(${EXECUTABLE_NAME} PROPERTIES
            WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
            LABELS "${TEST_LABEL}"
            TIMEOUT "${TEST_TIMEOUT}")
    ENDIF()
ENDMACRO(NEW_CTEST)
```

Replace exactly the six Wave 0 calls with:

```cmake
NEW_CTEST(osgVerse_Test_Ai_Chat ai_chat_tests.cpp offline 120)
NEW_CTEST(osgVerse_Test_Feeds feed_layer_tests.cpp offline 120)
NEW_CTEST(osgVerse_Test_TileOverlay tile_overlay_tests.cpp offline 120)
NEW_CTEST(osgVerse_Test_Satellite satellite_tests.cpp offline 120)
NEW_CTEST(osgVerse_Test_Ais ais_tests.cpp offline 120)
NEW_CTEST(osgVerse_Test_WorldTools world_tools_tests.cpp offline 120)
```

Do not register `NEW_EXAMPLE` targets or other historical `NEW_TEST` targets in this batch.

- [ ] **Step 4: Reconfigure and prove discovery**

Run:

```bash
cmake -S /Users/USER/osgsol -B /Users/USER/osgsol/build/osgsol_core
ctest --test-dir /Users/USER/osgsol/build/osgsol_core -N -L offline
```

Expected: the six exact names above and `Total Tests: 6`.

- [ ] **Step 5: Run the gate**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core --target \
  osgVerse_Test_Ai_Chat osgVerse_Test_Feeds osgVerse_Test_TileOverlay \
  osgVerse_Test_Satellite osgVerse_Test_Ais osgVerse_Test_WorldTools -j2
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -L offline --output-on-failure
```

Expected: `100% tests passed, 0 tests failed out of 6`.

- [ ] **Step 6: Commit D04**

```bash
git add CMakeLists.txt tests/CMakeLists.txt
git commit -m "test: register wave0 offline ctest gate"
```

### Task 2: D01 Credential, D02 Runtime Persistence, and D03 Signing Integrity

**Files:**
- Create: `tests/package_macos_tests.sh`
- Create: `tests/imgui_settings_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `ui/ImGui.h`
- Modify: `ui/ImGui.cpp:118-168`
- Modify: `packaging/package_macos.sh:1-80`

**Interfaces:**
- Consumes: `NEW_CTEST` from Task 1 and the existing `EARTH_OFFSCREEN`/`EARTH_AUTOCAP` smoke hooks.
- Produces: `std::string osgVerse::defaultImGuiSettingsPath()`, `OSGVERSE_SDK` packaging override, and a fail-closed package command.

- [ ] **Step 1: Write the failing ImGui path test**

Create `tests/imgui_settings_tests.cpp`:

```cpp
#include <ui/ImGui.h>
#include <cstdlib>
#include <iostream>
#include <string>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

int main()
{
    const std::string path = osgVerse::defaultImGuiSettingsPath();
    const char* home = std::getenv("HOME");
    if (home && home[0])
    {
        CHECK(!path.empty());
        CHECK(path[0] == '/');
        CHECK(path.find(".app/Contents") == std::string::npos);
#if defined(__APPLE__)
        CHECK(path == std::string(home) +
              "/Library/Application Support/osgVerse/imgui.ini");
#endif
    }
    std::cout << "[OK] ImGui settings path\n";
    return 0;
}
```

Register it with:

```cmake
NEW_CTEST(osgVerse_Test_ImGuiSettings imgui_settings_tests.cpp offline 30)
```

- [ ] **Step 2: Verify the new test fails before implementation**

Run:

```bash
cmake -S /Users/USER/osgsol -B /Users/USER/osgsol/build/osgsol_core
cmake --build /Users/USER/osgsol/build/osgsol_core \
  --target osgVerse_Test_ImGuiSettings -j2
```

Expected: compile or link failure naming `defaultImGuiSettingsPath`.

- [ ] **Step 3: Write the failing package regression**

Create executable `tests/package_macos_tests.sh` with these behaviors:

```bash
#!/bin/bash
set -euo pipefail

if [ "$(uname -s)" != "Darwin" ]; then
    echo "SKIP: macOS packaging test"
    exit 0
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${OSGVERSE_SDK:-$ROOT/build/sdk_core}"
APP="$ROOT/dist/EarthExplorer.app"
LOG="$(mktemp -t osgsol-package.XXXXXX)"
HOME_DIR="$(mktemp -d -t osgsol-home.XXXXXX)"
trap 'rm -f "$LOG" /tmp/earth_capture_0.png; rm -rf "$HOME_DIR"' EXIT

rm -rf "$APP"
if EARTH_AI_KEY="wave0-secret-must-not-ship" \
   OSGVERSE_SDK="$SDK" bash "$ROOT/packaging/package_macos.sh" >"$LOG" 2>&1; then
    echo "FAIL: packaging accepted EARTH_AI_KEY" >&2
    exit 1
fi
grep -q "Refusing to package while EARTH_AI_KEY is set" "$LOG"
test ! -e "$APP"

env -u EARTH_AI_KEY OSGVERSE_SDK="$SDK" \
    bash "$ROOT/packaging/package_macos.sh"
test -x "$APP/Contents/MacOS/osgVerse_EarthExplorer"
if /usr/libexec/PlistBuddy -c 'Print :LSEnvironment:EARTH_AI_KEY' \
   "$APP/Contents/Info.plist" >/dev/null 2>&1; then
    echo "FAIL: plist contains EARTH_AI_KEY" >&2
    exit 1
fi

env -u EARTH_AI_KEY HOME="$HOME_DIR" EARTH_IME=0 \
    EARTH_OFFSCREEN=1 EARTH_AUTOCAP=100 \
    "$APP/Contents/MacOS/osgVerse_EarthExplorer" >"$LOG" 2>&1
grep -q "\[Earth\] offscreen context" "$LOG"
if find "$APP" -name imgui.ini -print -quit | grep -q .; then
    echo "FAIL: runtime wrote imgui.ini into signed bundle" >&2
    exit 1
fi
codesign --verify --deep --strict "$APP"
echo "[OK] macOS package credential, smoke, and signature checks"
```

Run it against the current implementation:

```bash
chmod +x /Users/USER/osgsol/tests/package_macos_tests.sh
bash /Users/USER/osgsol/tests/package_macos_tests.sh
```

Expected: failure because the current script accepts and embeds the marker key.

- [ ] **Step 4: Implement the external ImGui settings path**

Declare in `ui/ImGui.h`:

```cpp
std::string defaultImGuiSettingsPath();
```

Add the needed `<string>` include. Implement in `ui/ImGui.cpp`:

```cpp
std::string osgVerse::defaultImGuiSettingsPath()
{
#if defined(_WIN32)
    const char* base = std::getenv("LOCALAPPDATA");
    return (base && base[0]) ? std::string(base) + "/osgVerse/imgui.ini" : std::string();
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    return (home && home[0]) ? std::string(home) +
        "/Library/Application Support/osgVerse/imgui.ini" : std::string();
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) return std::string(xdg) + "/osgVerse/imgui.ini";
    const char* home = std::getenv("HOME");
    return (home && home[0]) ? std::string(home) + "/.config/osgVerse/imgui.ini" : std::string();
#endif
}
```

Immediately after `ImGuiIO& io = ImGui::GetIO();` in `startImGuiContext`, keep the filename alive for the context and create its parent directory:

```cpp
static std::string s_iniFilename;
s_iniFilename = defaultImGuiSettingsPath();
if (!s_iniFilename.empty() && osgDB::makeDirectoryForFile(s_iniFilename))
    io.IniFilename = s_iniFilename.c_str();
else
    io.IniFilename = NULL;
```

Add the `<cstdlib>` and `<osgDB/FileUtils>` includes required by that code.

- [ ] **Step 5: Make packaging reject keys and fail closed**

At the start of `packaging/package_macos.sh`, use:

```bash
#!/bin/bash
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${OSGVERSE_SDK:-$REPO/build/sdk_core}"
APP="$REPO/dist/EarthExplorer.app"
PLUGVER="osgPlugins-3.6.5"

if [ -n "${EARTH_AI_KEY:-}" ]; then
    echo "[error] Refusing to package while EARTH_AI_KEY is set; unset it and use per-user configuration." >&2
    exit 64
fi
```

Delete the entire plist injection block. Replace the signing tail with:

```bash
if find "$APP" -name imgui.ini -print -quit | grep -q .; then
    echo "[error] Refusing to sign a bundle containing imgui.ini" >&2
    exit 65
fi

codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"
echo "Built and verified: $APP"
```

- [ ] **Step 6: Run targeted and shared tests**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core \
  --target osgVerse_Test_ImGuiSettings install -j2
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -R 'osgVerse_Test_ImGuiSettings' --output-on-failure
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -L offline --output-on-failure
bash /Users/USER/osgsol/tests/package_macos_tests.sh
```

Expected: all CTest cases pass; the key-bearing package exits 64 without creating the app; the keyless package smoke-runs and still passes strict verification.

- [ ] **Step 7: Commit the Release batch**

```bash
git add packaging/package_macos.sh tests/package_macos_tests.sh \
  tests/imgui_settings_tests.cpp tests/CMakeLists.txt ui/ImGui.h ui/ImGui.cpp
git commit -m "fix: harden macos release packaging"
```

### Task 3: C01-C02 MCP Lifecycle and Exception Boundary

**Files:**
- Create: `tests/mcp_safety_tests.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `ai/McpServer.h:7-73`
- Modify: `ai/McpServer.cpp:113-179,282-360,451-515,801-814`

**Interfaces:**
- Consumes: public `McpServer::paginateResults`, `McpServer::start/stop`, libhv `requests::request`, and `NEW_CTEST`.
- Produces: non-throwing cursor validation, a worker exception boundary, atomic stop state, and owner-only joining of SSE workers.

- [ ] **Step 1: Write the failing MCP regression binary**

Create `tests/mcp_safety_tests.cpp` that performs these three checks in order:

```cpp
#include <ai/McpServer.h>
#include <requests.h>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

static bool isInvalidParams(const picojson::value& value)
{
    return value.is<picojson::object>() && value.contains("code") &&
           value.get("code").get<double>() == osgVerse::InvalidParams;
}

int main()
{
    osg::ref_ptr<osgVerse::McpServer> pagination = new osgVerse::McpServer;
    picojson::array items;
    items.push_back(picojson::value("zero"));
    items.push_back(picojson::value("one"));
    CHECK(isInvalidParams(pagination->paginateResults(items, "cursor_-1", 1, "items")));
    CHECK(isInvalidParams(pagination->paginateResults(items, "cursor_bad", 1, "items")));
    CHECK(isInvalidParams(pagination->paginateResults(
        items, "cursor_999999999999999999999999", 1, "items")));
    CHECK(isInvalidParams(pagination->paginateResults(items, "cursor_3", 1, "items")));
    CHECK(pagination->paginateResults(items, "cursor_1", 1, "items")
              .get("items").get<picojson::array>()[0].get<std::string>() == "one");

    std::promise<void> continued;
    {
        osgVerse::ThreadPool pool(1);
        pool.enqueue([] { throw std::runtime_error("expected worker test exception"); });
        pool.enqueue([&continued] { continued.set_value(); });
        CHECK(continued.get_future().wait_for(std::chrono::seconds(2)) ==
              std::future_status::ready);
    }

    osg::ref_ptr<osgVerse::McpServer> lifecycle = new osgVerse::McpServer;
    lifecycle->start("127.0.0.1", 19876);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    requests::Request request(new HttpRequest);
    request->method = HTTP_GET;
    request->url = "http://127.0.0.1:19876/sse";
    request->timeout = 1;
    requests::request(request);
    std::this_thread::sleep_for(std::chrono::seconds(6));
    lifecycle->stop();
    std::cout << "[OK] MCP cursor, worker, and SSE lifecycle safety\n";
    return 0;
}
```

Register with:

```cmake
NEW_CTEST(osgVerse_Test_McpSafety mcp_safety_tests.cpp offline 30)
```

- [ ] **Step 2: Observe the current MCP failure**

Run:

```bash
cmake -S /Users/USER/osgsol -B /Users/USER/osgsol/build/osgsol_core
cmake --build /Users/USER/osgsol/build/osgsol_core \
  --target osgVerse_Test_McpSafety -j2
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -R osgVerse_Test_McpSafety --output-on-failure
```

Expected: non-zero exit from invalid cursor access, uncaught worker exception, or SSE self-join termination.

- [ ] **Step 3: Add the thread-pool exception boundary**

Add `<atomic>`, `<cstdio>`, `<exception>`, and `<limits>` includes in `ai/McpServer.h`. Replace bare `task();` with:

```cpp
try
{
    task();
}
catch (const std::exception& e)
{
    std::fprintf(stderr, "[McpServer] worker task failed: %s\n", e.what());
}
catch (...)
{
    std::fprintf(stderr, "[McpServer] worker task failed: unknown exception\n");
}
```

- [ ] **Step 4: Make cursor decoding total and bounded**

Replace the throwing decoder with:

```cpp
bool decodeCursor(const std::string& cursor, size_t& offset) const
{
    offset = 0;
    if (cursor.empty()) return true;
    if (cursor.compare(0, 7, "cursor_") != 0 || cursor.size() == 7) return false;
    for (size_t i = 7; i < cursor.size(); ++i)
    {
        const char ch = cursor[i];
        if (ch < '0' || ch > '9') return false;
        const size_t digit = static_cast<size_t>(ch - '0');
        if (offset > ((std::numeric_limits<size_t>::max)() - digit) / 10) return false;
        offset = offset * 10 + digit;
    }
    return true;
}
```

Implement pagination with `size_t` and the protocol error shape already recognized by `handleRequest`:

```cpp
size_t offset = 0;
const size_t total = allItems.size();
if (pageSize <= 0 || !server->decodeCursor(cursor, offset) || offset > total)
    return simpleJson("code", InvalidParams, "message", "Invalid pagination cursor");

const size_t count = static_cast<size_t>(pageSize);
const size_t end = std::min(total, offset + std::min(count, total - offset));
picojson::array pageItems;
for (size_t i = offset; i < end; ++i) pageItems.push_back(allItems[i]);
```

Keep the existing result keys and encode `end` only after a checked cast no larger than `INT_MAX`; change `encodeCursor` to accept `size_t`.

- [ ] **Step 5: Make SSE shutdown owner-driven**

Apply this lifecycle in `JsonRpcServer`:

```cpp
std::atomic<bool> running;
```

- Use `running.load()` in the worker and timer predicates.
- Replace the single five-second worker sleep with 50 iterations of 100 ms that each re-check
  `running.load()` and `ctx->writer->isConnected()`; emit a heartbeat only after all 50 iterations.
  This bounds an owner-side join to roughly 100 ms after stop/disconnect instead of blocking the
  libhv timer thread for the full heartbeat period.
- Remove the worker's call to `closeSession(sessionID)`.
- In the timer's disconnected branch, call `killTimer` and then `closeSession(sessionID)`; the timer/owner, never the worker, joins.
- Change `closeSession` to move one `std::unique_ptr<std::thread>` out while holding `sseMutex`, remove all session maps under that same lock, release the lock, run cleanup callbacks, and join outside the lock.
- Retain a defensive self-thread branch that calls `detach()` instead of `join()` if an unforeseen caller invokes `closeSession` from the worker itself.
- In `cancel()`, execute `if (!running.exchange(false)) return 0;` first, move every thread into a local vector under `sseMutex`, clear session maps, release the lock, join the local threads, and return 0. Do not call `closeSession` while holding `sseMutex` and do not call `OpenThreads::Thread::cancel()`.
- Keep `stop()` as `cancel(); join();`, so the server run loop observes false and calls `server.stop()` before `join()` completes.

The join block used by both owner paths is:

```cpp
if (thread && thread->joinable())
{
    if (thread->get_id() == std::this_thread::get_id()) thread->detach();
    else thread->join();
}
```

- [ ] **Step 6: Run targeted, sanitizer-equivalent stress, and shared tests**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core \
  --target osgVerse_Test_McpSafety -j2
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -R osgVerse_Test_McpSafety --repeat until-fail:5 --output-on-failure
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -L offline --output-on-failure
```

Expected: five lifecycle repetitions and the full offline label pass with no terminate, deadlock, or cursor exception.

- [ ] **Step 7: Commit the MCP batch**

```bash
git add ai/McpServer.h ai/McpServer.cpp tests/mcp_safety_tests.cpp tests/CMakeLists.txt
git commit -m "fix: harden mcp lifecycle and cursor handling"
```

### Task 4: C06 ImGui Draw-Thread Input Ownership

**Files:**
- Create: `ui/ImGuiInputQueue.h`
- Create: `tests/imgui_threading_tests.cpp`
- Modify: `ui/CMakeLists.txt`
- Modify: `ui/ImGui.cpp:32-75,118-174,219-343`
- Modify: `ui/ImGui3D.cpp:11-157`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: OSG event callbacks on the event traversal and PRE_DRAW/draw callbacks on the graphics thread.
- Produces: `ImGuiInputEvent`, `ImGuiInputQueue::push/takeAll/publishCapture`, and draw-only ImGui API dispatch.

- [ ] **Step 1: Write the failing queue test**

Create `tests/imgui_threading_tests.cpp`:

```cpp
#include <ui/ImGuiInputQueue.h>
#include <iostream>
#include <thread>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

int main()
{
    osgVerse::ImGuiInputQueue queue;
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer)
    {
        producers.push_back(std::thread([producer, &queue]
        {
            for (int i = 0; i < 1000; ++i)
                queue.push(osgVerse::ImGuiInputEvent::keyEvent(
                    producer * 1000 + i, true, 0));
        }));
    }
    for (size_t i = 0; i < producers.size(); ++i) producers[i].join();
    CHECK(queue.takeAll().size() == 4000);
    CHECK(queue.takeAll().empty());
    queue.publishCapture(true, false);
    CHECK(queue.wantsMouse());
    CHECK(!queue.wantsKeyboard());
    std::cout << "[OK] ImGui immutable input queue\n";
    return 0;
}
```

Register with:

```cmake
NEW_CTEST(osgVerse_Test_ImGuiThreading imgui_threading_tests.cpp offline 30)
```

- [ ] **Step 2: Verify the test fails before the queue exists**

Run:

```bash
cmake -S /Users/USER/osgsol -B /Users/USER/osgsol/build/osgsol_core
cmake --build /Users/USER/osgsol/build/osgsol_core \
  --target osgVerse_Test_ImGuiThreading -j2
```

Expected: compile failure naming `ui/ImGuiInputQueue.h`.

- [ ] **Step 3: Implement the private immutable queue**

`ui/ImGuiInputQueue.h` defines no ImGui types and contains:

```cpp
#ifndef OSGVERSE_UI_IMGUIINPUTQUEUE_H
#define OSGVERSE_UI_IMGUIINPUTQUEUE_H

#include <atomic>
#include <mutex>
#include <vector>

namespace osgVerse
{
    struct ImGuiInputEvent
    {
        enum Type { Key, MousePosition, MouseButtons, MouseWheel, VirtualMouse };
        Type type;
        int key, buttonMask;
        bool down;
        unsigned int modifiers;
        float x, y, wheel;

        static ImGuiInputEvent keyEvent(int key, bool down, unsigned int modifiers)
        {
            ImGuiInputEvent e = {};
            e.type = Key; e.key = key; e.down = down; e.modifiers = modifiers;
            return e;
        }
    };

    class ImGuiInputQueue
    {
    public:
        ImGuiInputQueue() : _wantsMouse(false), _wantsKeyboard(false) {}
        void push(const ImGuiInputEvent& event)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _events.push_back(event);
        }
        std::vector<ImGuiInputEvent> takeAll()
        {
            std::vector<ImGuiInputEvent> result;
            std::lock_guard<std::mutex> lock(_mutex);
            result.swap(_events);
            return result;
        }
        void publishCapture(bool mouse, bool keyboard)
        {
            _wantsMouse.store(mouse); _wantsKeyboard.store(keyboard);
        }
        bool wantsMouse() const { return _wantsMouse.load(); }
        bool wantsKeyboard() const { return _wantsKeyboard.load(); }

    private:
        std::mutex _mutex;
        std::vector<ImGuiInputEvent> _events;
        std::atomic<bool> _wantsMouse, _wantsKeyboard;
    };
}

#endif
```

Add this private header to `LIBRARY_FILES`, not `LIBRARY_INCLUDE_FILES`, in `ui/CMakeLists.txt`.

- [ ] **Step 4: Queue 2D events without touching ImGui**

Give `ImGuiHandler` an `ImGuiInputQueue`. Its `handle` method must:

- construct `Key` events from key/down/modifier data;
- construct position/button/wheel events only from `GUIEventAdapter` values;
- push the immutable event;
- return `queue.wantsKeyboard()` for keyboard and `queue.wantsMouse()` for pointer input.

The entire method must contain no `ImGui::`, `ImGuiIO`, `ImGuizmo::`, `io.`, or direct shared mouse-state writes.

In PRE_DRAW, call `takeAll()` and translate each event into `io.AddKeyEvent`, `io.AddInputCharacter`, `io.AddMousePosEvent`, `io.AddMouseButtonEvent`, and `io.AddMouseWheelEvent`. After `NewFrame`, publish:

```cpp
handler->_input.publishCapture(
    io.WantCaptureMouse || ImGuizmo::IsUsing(), io.WantCaptureKeyboard);
```

- [ ] **Step 5: Queue 3D and synthetic mouse input**

Apply the same queue to `ImGuiHandler3D`. `ImGuiManager::setMouseInput` must enqueue a `VirtualMouse` containing normalized `pos`, button mask, and wheel without calling `ImGui::GetIO()`; the draw callback scales normalized coordinates with the draw-thread `io.DisplaySize` before calling ImGui input APIs.

- [ ] **Step 6: Move context startup and shutdown requests to draw traversal**

- Remove `startImGuiContext` calls from `initializeEventHandler2D/3D`.
- On the first PRE_DRAW/3D draw callback, create the context, load fonts, and initialize the backend there before `NewFrame`.
- Change `ImGuiManager::shutdown()` to set an atomic release request on the handler; the next draw callback performs backend shutdown and `ImGui::DestroyContext()`.
- Remove `ImGui::DestroyContext()` from handler destructors so an arbitrary destruction thread never calls ImGui.
- If no context exists, a release request is a no-op.

- [ ] **Step 7: Verify thread ownership and regressions**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core \
  --target osgVerse_Test_ImGuiThreading osgVerse_Test_ImGuiSettings -j2
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -R 'osgVerse_Test_ImGui(Threading|Settings)' --repeat until-fail:10 \
  --output-on-failure
python3 - <<'PY'
from pathlib import Path
for name in ('ui/ImGui.cpp', 'ui/ImGui3D.cpp'):
    text = Path('/Users/USER/osgsol', name).read_text()
    for block in text.split('virtual bool handle(')[1:]:
        body = block.split('\n    }', 1)[0]
        assert 'ImGui::' not in body and 'ImGuiIO' not in body and 'ImGuizmo::' not in body, name
print('[OK] event handlers contain no ImGui API calls')
PY
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -L offline --output-on-failure
```

Expected: queue stress passes ten times, static ownership probe passes, and all offline tests remain green.

- [ ] **Step 8: Commit the ImGui batch**

```bash
git add ui/ImGuiInputQueue.h ui/CMakeLists.txt ui/ImGui.cpp ui/ImGui3D.cpp \
  tests/imgui_threading_tests.cpp tests/CMakeLists.txt
git commit -m "fix: dispatch imgui input on draw thread"
```

### Task 5: I01-I02-I07 Bounded Input Parsing

**Files:**
- Create: `plugins/osgdb_web/GzipUtils.h`
- Create: `readerwriter/SafeGltfInput.h`
- Create: `readerwriter/SafeGltfInput.cpp`
- Create: `tests/input_safety_tests.cpp`
- Modify: `plugins/osgdb_web/ReaderWriterWeb.cpp:14-38,281-289`
- Modify: `plugins/osgdb_web/CMakeLists.txt`
- Modify: `readerwriter/LoadSceneGLTF.cpp:84-117,165-228,345-407`
- Modify: `readerwriter/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: zlib `inflate`, tinygltf filesystem callbacks, and libhv streaming `http_cb`.
- Produces: `decompressGzipBounded`, `parseThreeDTileHeader`, `appendRemoteChunk`, `parseContentLength`, and the 64 MiB remote external-resource ceiling.

- [ ] **Step 1: Write the failing input-boundary test**

Create `tests/input_safety_tests.cpp` with a local gzip encoder (`deflateInit2(..., 16 + MAX_WBITS, ...)`) and assertions that:

```cpp
// 1 MiB of zero bytes compresses well above the old 10:1 assumption and round-trips.
CHECK(osgVerse::decompressGzipBounded(compressed.data(), compressed.size(),
    uncompressed, 2u * 1024u * 1024u, 512u, &error));
CHECK(uncompressed.size() == 1024u * 1024u);

// The byte cap and ratio cap fail without returning partial data.
CHECK(!osgVerse::decompressGzipBounded(compressed.data(), compressed.size(),
    uncompressed, 1024u, 512u, &error));
CHECK(uncompressed.empty());
CHECK(!osgVerse::decompressGzipBounded(compressed.data(), compressed.size(),
    uncompressed, 2u * 1024u * 1024u, 2u, &error));
CHECK(uncompressed.empty());

// Header shorter than 28 bytes, declared length beyond input, overflowing sections,
// invalid i3dm gltfFormat, and a payload shorter than a GLB header all fail.
osgVerse::ThreeDTileHeader header;
std::vector<char> malformed(4, 0);
CHECK(!osgVerse::parseThreeDTileHeader(malformed, header, &error));

// A valid b3dm with 28-byte container header and 12-byte GLB payload succeeds.
std::vector<char> valid = makeValidB3dmFixture();
CHECK(osgVerse::parseThreeDTileHeader(valid, header, &error));
CHECK(header.kind == osgVerse::ThreeDTileHeader::B3DM);
CHECK(header.payloadOffset == 28u);
CHECK(header.payloadSize == 12u);

// Unknown-length streaming never stores a byte beyond the caller's cap.
std::vector<unsigned char> remote;
CHECK(osgVerse::appendRemoteChunk(remote, "1234", 4, 8, &error));
CHECK(!osgVerse::appendRemoteChunk(remote, "56789", 5, 8, &error));
CHECK(remote.size() == 4u);

size_t length = 0;
CHECK(osgVerse::parseContentLength("67108864", length) && length == 67108864u);
CHECK(!osgVerse::parseContentLength("67108865x", length));
CHECK(!osgVerse::parseContentLength("999999999999999999999999", length));
```

The fixture helper writes every 32-bit field explicitly in little-endian order; it never uses struct casts or host-endian `memcpy`.

Register only when zlib is available:

```cmake
IF(VERSE_WITH_COMMON_LIBRARIES)
    NEW_CTEST(osgVerse_Test_InputSafety input_safety_tests.cpp offline 60)
    TARGET_COMPILE_DEFINITIONS(osgVerse_Test_InputSafety PRIVATE WITH_ZLIB)
    TARGET_INCLUDE_DIRECTORIES(osgVerse_Test_InputSafety PRIVATE
        ../helpers/toolchain_builder/zlib ${ZLIB_INCLUDE_DIR})
    TARGET_LINK_LIBRARIES(osgVerse_Test_InputSafety ${THIRDPARTY_LIBRARIES})
ENDIF()
```

- [ ] **Step 2: Verify the test fails before helper creation**

Run:

```bash
cmake -S /Users/USER/osgsol -B /Users/USER/osgsol/build/osgsol_core
cmake --build /Users/USER/osgsol/build/osgsol_core \
  --target osgVerse_Test_InputSafety -j2
```

Expected: missing-header or missing-symbol failure for the new bounded helpers.

- [ ] **Step 3: Implement bounded gzip streaming**

`plugins/osgdb_web/GzipUtils.h` defines the inline function:

```cpp
bool decompressGzipBounded(const unsigned char* input, size_t inputSize,
                           std::vector<unsigned char>& output,
                           size_t maxOutputBytes, size_t maxRatio,
                           std::string* error);
```

The complete state rules are:

- clear `output` on entry and every failure;
- reject null/empty input, zero limits, and multiplication overflow;
- set the effective ceiling to `min(maxOutputBytes, inputSize * maxRatio)`;
- require `inflateInit2(&stream, 16 + MAX_WBITS) == Z_OK`;
- append each 16 KiB produced chunk only after checking it fits the effective ceiling;
- accept only `Z_OK` while progressing and a final `Z_STREAM_END`;
- reject truncated `Z_BUF_ERROR`, `Z_DATA_ERROR`, `Z_MEM_ERROR`, and no-progress loops;
- call `inflateEnd` exactly once on every initialized path.

Use defaults of 256 MiB and 512:1 in `ReaderWriterWeb.cpp`. Replace the fixed `bufferSize * 10` vector with the returned dynamic vector; on failure log the error and return `ReadResult::ERROR_IN_READING_FILE`.

- [ ] **Step 4: Implement the checked 3D Tiles header parser**

`readerwriter/SafeGltfInput.h` declares:

```cpp
namespace osgVerse
{
    static const size_t MAX_REMOTE_GLTF_RESOURCE_BYTES = 64u * 1024u * 1024u;

    struct ThreeDTileHeader
    {
        enum Kind { NONE, B3DM, I3DM };
        Kind kind;
        unsigned int version, gltfFormat;
        size_t byteLength;
        size_t featureJsonOffset, featureJsonLength;
        size_t featureBinaryOffset, featureBinaryLength;
        size_t batchJsonOffset, batchJsonLength;
        size_t batchBinaryOffset, batchBinaryLength;
        size_t payloadOffset, payloadSize;
        ThreeDTileHeader();
    };

    bool parseThreeDTileHeader(const std::vector<char>& data,
                              ThreeDTileHeader& output, std::string* error);
    bool appendRemoteChunk(std::vector<unsigned char>& output,
                           const char* data, size_t size, size_t limit,
                           std::string* error);
    bool parseContentLength(const std::string& text, size_t& output);
}
```

`SafeGltfInput.cpp` reads u32 fields byte-by-byte. It requires exact magic, container version 1, minimum 28/32-byte header, `byteLength <= data.size()`, checked section addition, payload within `byteLength`, i3dm `gltfFormat` in `{0,1}`, and at least 12 payload bytes for embedded GLB. It never performs pointer arithmetic before interval validation.

Add `SafeGltfInput.h/.cpp` to `LIBRARY_FILES`; keep the header private by not adding it to `LIBRARY_INCLUDE_FILES`.

- [ ] **Step 5: Integrate checked tile parsing**

In `LoaderGLTF`:

- call `parseThreeDTileHeader` before reading any B3DM/I3DM field;
- pass the validated feature JSON interval to `ReadRtcCenterFeatureTable`;
- for embedded data, inspect GLB version only after the 12-byte payload check and call `LoadBinaryFromMemory` with exactly `payloadSize`;
- for i3dm URI payload, construct the URI from exactly the validated payload interval and require a non-empty trimmed result;
- on parser failure, append the explicit error and skip tinygltf loading;
- for a plain GLB, require at least 12 bytes before reading version.

Delete `ReadB3dmHeader` and `ReadI3dmHeader` after all callers use the checked structure.

- [ ] **Step 6: Enforce the remote glTF cap while receiving**

Change `HttpRequester::read` to install `req.http_cb`. On `HP_HEADERS_COMPLETE`, safely parse `Content-Length` and set an over-limit flag when above `MAX_REMOTE_GLTF_RESOURCE_BYTES`. On each `HP_BODY`, call `appendRemoteChunk`; once rejected, discard all later chunks. Return false and clear output when over-limit or when the transport fails.

Add a HEAD-based `HttpRequester::size` method. `GetFileSizeInBytes` must:

```cpp
if (!filesize_out) return false;
*filesize_out = 0;
if (rw)
{
    size_t length = 0;
    if (HttpRequester::instance()->size(filepath, length))
    {
        if (length > MAX_REMOTE_GLTF_RESOURCE_BYTES)
        {
            if (err) *err = "remote glTF resource exceeds 64 MiB";
            return false;
        }
        *filesize_out = length;
    }
    return true;
}
```

Missing `Content-Length` remains permitted because the body callback enforces the same hard in-memory ceiling. Call `loader.SetMaxExternalFileSize(MAX_REMOTE_GLTF_RESOURCE_BYTES)` before parsing.

- [ ] **Step 7: Run targeted boundary tests and shared regressions**

Run:

```bash
cmake --build /Users/USER/osgsol/build/osgsol_core \
  --target osgVerse_Test_InputSafety -j2
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -R osgVerse_Test_InputSafety --repeat until-fail:10 --output-on-failure
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -L offline --output-on-failure
```

Expected: ten boundary-test repetitions and the full offline label pass without crash or excessive allocation.

- [ ] **Step 8: Commit the input-boundary batch**

```bash
git add plugins/osgdb_web/GzipUtils.h plugins/osgdb_web/ReaderWriterWeb.cpp \
  plugins/osgdb_web/CMakeLists.txt readerwriter/SafeGltfInput.h \
  readerwriter/SafeGltfInput.cpp readerwriter/LoadSceneGLTF.cpp \
  readerwriter/CMakeLists.txt tests/input_safety_tests.cpp tests/CMakeLists.txt
git commit -m "fix: bound compressed and gltf input parsing"
```

### Task 6: C03-C04-C10 Optional Runtime Truthfulness

**Files:**
- Create: `docs/superpowers/2026-07-10-wave0-optional-support.md`
- Modify: `ai/CMakeLists.txt:13-34`
- Modify: `animation/CMakeLists.txt:1-14`

**Interfaces:**
- Consumes: live CMake cache evidence showing ONNX Runtime and Bullet unavailable.
- Produces: an install tree that exposes neither unavailable API, explicit configure messages, and reproducible OFF flags.

- [ ] **Step 1: Record the failing support-surface probe**

Run:

```bash
test -f /Users/USER/osgsol/build/sdk_core/include/osgVerse/animation/PhysicsEngine.h
rg -n 'ONNXRUNTIME_(INCLUDE_DIR|LIB_DIR).*NOTFOUND|BULLET_(INCLUDE_DIR|LIB_DIR).*NOTFOUND' \
  /Users/USER/osgsol/build/osgsol_core/CMakeCache.txt
```

Expected: the unavailable Bullet header is nevertheless installed, while all four dependency paths are `NOTFOUND`.

- [ ] **Step 2: Make unavailable headers conditional**

Remove `PhysicsEngine.h` from the unconditional `LIBRARY_INCLUDE_FILES` list in `animation/CMakeLists.txt`. In the existing `IF(BULLET_FOUND)` block add both files:

```cmake
SET(LIBRARY_INCLUDE_FILES ${LIBRARY_INCLUDE_FILES} PhysicsEngine.h)
SET(LIBRARY_FILES ${LIBRARY_FILES} PhysicsEngine.h PhysicsEngine.cpp)
```

Add explicit status branches:

```cmake
ELSE()
    MESSAGE(STATUS "[osgVerse] Bullet support: DISABLED; PhysicsEngine is not built or installed")
ENDIF()
```

and in `ai/CMakeLists.txt`:

```cmake
ELSE()
    MESSAGE(STATUS "[osgVerse] ONNX Runtime support: DISABLED; OnnxRuntimeEngine is not built or installed")
ENDIF()
```

Do not edit `OnnxRuntimeEngine.cpp` or `PhysicsEngine.cpp`, because neither dependency is buildable or testable in this Wave 0 environment.

- [ ] **Step 3: Write the support matrix**

Create `docs/superpowers/2026-07-10-wave0-optional-support.md` with:

````markdown
# Wave 0 Optional Runtime Support — 2026-07-10

| Runtime | Configure evidence | Wave 0 state | Findings |
|---|---|---|---|
| ONNX Runtime | include and lib paths are `NOTFOUND` | Explicitly disabled and unsupported in this macOS artifact | C03, C04 remain open for a dependency-enabled job |
| Bullet | include and lib paths are `NOTFOUND` | Explicitly disabled; `PhysicsEngine.h` is not installed | C10 remains open for a dependency-enabled job |

Reproduction flags:

```bash
-DONNXRUNTIME_FEATURE_ENABLED=OFF -DBULLET_FEATURE_ENABLED=OFF
```

No claim is made that C03, C04, or C10 is fixed. Closing those findings requires dependency-enabled compilation plus their dedicated regression tests.
````

- [ ] **Step 4: Prove a fresh disabled install is honest**

Run:

```bash
rm -rf /Users/USER/osgsol/build/wave0_optional_probe \
       /Users/USER/osgsol/build/wave0_optional_sdk
cmake -S /Users/USER/osgsol -B /Users/USER/osgsol/build/wave0_optional_probe \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/Users/USER/osgsol/build/wave0_optional_sdk \
  -DOSG_ROOT=/Users/USER/osgsol/build/sdk_core \
  -DVERSE_BUILD_EXAMPLES=OFF \
  -DONNXRUNTIME_FEATURE_ENABLED=OFF \
  -DBULLET_FEATURE_ENABLED=OFF
cmake --build /Users/USER/osgsol/build/wave0_optional_probe --target install -j2
test ! -e /Users/USER/osgsol/build/wave0_optional_sdk/include/osgVerse/ai/OnnxRuntimeEngine.h
test ! -e /Users/USER/osgsol/build/wave0_optional_sdk/include/osgVerse/animation/PhysicsEngine.h
```

Expected: configure prints both `DISABLED` messages, install succeeds, and neither unsupported API header exists.

- [ ] **Step 5: Run the shared gate and commit**

```bash
ctest --test-dir /Users/USER/osgsol/build/osgsol_core \
  -L offline --output-on-failure
git add ai/CMakeLists.txt animation/CMakeLists.txt \
  docs/superpowers/2026-07-10-wave0-optional-support.md
git commit -m "build: mark unavailable optional runtimes unsupported"
```

### Task 7: Fresh Release Exit Gate, Final Review, and Push

**Files:**
- Review: every path changed by Tasks 1-6
- Update only if evidence changes: `docs/superpowers/2026-07-10-wave0-optional-support.md`

**Interfaces:**
- Consumes: every batch commit and all registered offline tests.
- Produces: fresh Release SDK, verified package, review result with no Critical/Important issue, and pushed `codex/wave0-safety`.

- [ ] **Step 1: Confirm branch and commit boundaries**

Run:

```bash
git status --short --branch
git log --oneline --decorate master..HEAD
git diff --check master...HEAD
git diff --stat master...HEAD
```

Expected: clean `codex/wave0-safety`, one plan commit plus one commit per implementation batch, and no whitespace errors.

- [ ] **Step 2: Configure a new-path Release build**

Run:

```bash
rm -rf /Users/USER/osgsol/build/wave0_release \
       /Users/USER/osgsol/build/wave0_sdk
cmake -S /Users/USER/osgsol -B /Users/USER/osgsol/build/wave0_release \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/Users/USER/osgsol/build/wave0_sdk \
  -DOSG_ROOT=/Users/USER/osgsol/build/sdk_core \
  -DVERSE_BUILD_EXAMPLES=ON \
  -DONNXRUNTIME_FEATURE_ENABLED=OFF \
  -DBULLET_FEATURE_ENABLED=OFF
```

Expected: CTest enabled, ONNX Runtime disabled, Bullet disabled, and generated build files under only `build/wave0_release`.

- [ ] **Step 3: Build and install once**

Run:

```bash
cmake --build /Users/USER/osgsol/build/wave0_release --target install -j2
```

Expected: Release install exits 0 and populates `/Users/USER/osgsol/build/wave0_sdk`.

- [ ] **Step 4: Prove CTest discovery and execution**

Run:

```bash
ctest --test-dir /Users/USER/osgsol/build/wave0_release -N -L offline
ctest --test-dir /Users/USER/osgsol/build/wave0_release \
  -L offline --output-on-failure
```

Expected: at least the six mandatory tests are discovered; all registered offline tests pass.

- [ ] **Step 5: Verify both packaging credential modes**

Run:

```bash
rm -rf /Users/USER/osgsol/dist/EarthExplorer.app
if EARTH_AI_KEY=wave0-secret-must-not-ship \
   OSGVERSE_SDK=/Users/USER/osgsol/build/wave0_sdk \
   bash /Users/USER/osgsol/packaging/package_macos.sh; then
  exit 1
fi
test ! -e /Users/USER/osgsol/dist/EarthExplorer.app
env -u EARTH_AI_KEY OSGVERSE_SDK=/Users/USER/osgsol/build/wave0_sdk \
  bash /Users/USER/osgsol/packaging/package_macos.sh
```

Expected: key-bearing invocation exits 64 before creating the bundle; keyless invocation builds and verifies it.

- [ ] **Step 6: Smoke-run, inspect mutation, then strictly verify**

Run:

```bash
SMOKE_HOME="$(mktemp -d -t osgsol-final-home.XXXXXX)"
env -u EARTH_AI_KEY HOME="$SMOKE_HOME" EARTH_IME=0 \
  EARTH_OFFSCREEN=1 EARTH_AUTOCAP=100 \
  /Users/USER/osgsol/dist/EarthExplorer.app/Contents/MacOS/osgVerse_EarthExplorer \
  >/tmp/osgsol-wave0-smoke.log 2>&1
grep -q '\[Earth\] offscreen context' /tmp/osgsol-wave0-smoke.log
test -z "$(find /Users/USER/osgsol/dist/EarthExplorer.app -name imgui.ini -print -quit)"
! /usr/libexec/PlistBuddy -c 'Print :LSEnvironment:EARTH_AI_KEY' \
  /Users/USER/osgsol/dist/EarthExplorer.app/Contents/Info.plist >/dev/null 2>&1
codesign --verify --deep --strict /Users/USER/osgsol/dist/EarthExplorer.app
rm -rf "$SMOKE_HOME"
```

Expected: smoke exits 0, bundle contains neither key nor ini file, and strict verification after runtime exits 0.

- [ ] **Step 7: Perform final adversarial review**

Review `git diff master...HEAD` against every Wave 0 exit condition. Specifically reject the branch if any of these remain:

- a worker can join itself or shutdown joins while holding `sseMutex`;
- malformed cursor parsing can throw or index with a negative/overflowed value;
- an event handler calls ImGui/ImGuizmo or mutates draw-owned IO;
- gzip output can exceed either configured bound;
- any tile section pointer is formed before interval validation;
- a remote glTF callback can retain more than 64 MiB;
- packaging can contain a key, hide a signing failure, or mutate its signed bundle during smoke;
- the fresh install exposes ONNX/Bullet APIs while those runtimes are disabled.

Run targeted tests again for any review edit. The accepted review result is no Critical or Important finding.

- [ ] **Step 8: Push only the safety branch and verify the remote**

Run:

```bash
git status --short --branch
git push origin codex/wave0-safety
git ls-remote origin refs/heads/codex/wave0-safety \
  refs/tags/osgsol-baseline-2026-07-10 \
  'refs/tags/osgsol-baseline-2026-07-10^{}'
```

Expected: remote safety branch equals local `HEAD`, and the dereferenced baseline tag remains `75e0a8f4a3b9638dd7e89bd90975b2a653b2fe9c`.
