# Hong Kong 3D Tiles Progressive Loading Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Attach the Hong Kong F2 root quickly, page only visible external tilesets, refine from real pixel size at SSE 8, and keep rough/refined content resident long enough to avoid altitude-change thrash and holes.

**Architecture:** The existing `osgdb_3dtiles` reader remains authoritative for transforms, content formats, and refinement. Only external JSON references discovered below a no-content container become bounded deferred `osg::ProxyNode`s; content-bearing REPLACE parents retain their atomic rough-to-refined PagedLOD path. Pixel thresholds are derived directly from bounding radius, geometric error, and SSE, while the application enables pixel mode and clamps diagnostic overrides.

**Tech Stack:** C++17, OpenSceneGraph `ProxyNode`/`PagedLOD`/DatabasePager, picojson, existing `verse_web`, CMake/CTest, Hong Kong LandsD F2 endpoint.

## Global Constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on `codex/v0.2-runtime-safety`.
- Do not change the F2 URL, ECEF transform, `gltfUpAxis`, KTX2/b3dm readers, F2 shader, attribution, or pager thread counts.
- Do not add b3dm disk serialization/cache in this batch.
- Lazy external JSON is allowed only below a tile with no content; refined children of content-bearing REPLACE tiles remain an atomic pager result.
- Rough child index `0` is non-expirable; refined child index `1` has minimum expiry `30.0` seconds.
- Hong Kong defaults to `UsePixelsOnScreen=1` and `MaxScreenSpaceError=8`; `EARTH_3DTILES_SSE` is clamped to `[2,32]`.
- Disabling the layer must continue to set NodeMask `0` and stop new traversal/request generation.
- Build one CMake target at a time.

## File Structure

- `plugins/osgdb_3dtiles/ReaderWriter3dTiles.cpp`: deferred external JSON proxies and PagedLOD pixel/expiry configuration.
- `applications/earth_explorer/tiles3d_data.h`: pure SSE resolution helper.
- `applications/earth_explorer/tiles3d_data.cpp`: default pixel-mode options and root-attach timing.
- `tests/tiles3d_paging_tests.cpp`: local no-network tileset fixtures and scene-graph assertions.
- `tests/CMakeLists.txt`: offline test target linked to the production 3D Tiles plugin.

---

### Task 1: Add a local fixture proving root JSON does not load its external children

**Files:**
- Create: `tests/tiles3d_paging_tests.cpp`
- Modify: `tests/CMakeLists.txt:139-173`

**Interfaces:**
- Consumes: the production plugin entrypoint `osgdb_verse_tiles`.
- Produces: offline assertions for deferred file names, bounds, and load mode.

- [ ] **Step 1: Register the new CTest target**

Add after the TileOverlay target:

```cmake
NEW_CTEST(osgVerse_Test_Tiles3dPaging tiles3d_paging_tests.cpp offline 120)
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_Tiles3dPaging PRIVATE
                           OSGVERSE_3DTILES_PLUGIN_PATH="$<TARGET_FILE:osgdb_verse_tiles>")
ADD_DEPENDENCIES(osgVerse_Test_Tiles3dPaging osgdb_verse_tiles)
```

- [ ] **Step 2: Write the failing root fixture**

Create the test with this complete skeleton and root test:

```cpp
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

#include <osg/NodeVisitor>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osgDB/FileUtils>
#include <osgDB/ReadFile>
#include <osgDB/Registry>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; std::abort(); } } while (0)

namespace
{
    struct GraphVisitor : public osg::NodeVisitor
    {
        GraphVisitor() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}
        void apply(osg::ProxyNode& node) override
        {
            proxies.push_back(&node);
            traverse(node);
        }
        void apply(osg::PagedLOD& node) override
        {
            pagedLods.push_back(&node);
            traverse(node);
        }
        std::vector<osg::ProxyNode*> proxies;
        std::vector<osg::PagedLOD*> pagedLods;
    };

    std::string makeTempDir()
    {
        char path[] = "/tmp/osgsol-tiles3d-XXXXXX";
        char* made = mkdtemp(path);
        CHECK(made != NULL);
        return made;
    }

    void writeText(const std::string& path, const std::string& text)
    {
        std::ofstream output(path.c_str(), std::ios::binary);
        CHECK(output.good());
        output << text;
        CHECK(output.good());
    }
}

int main(int, char**)
{
    CHECK(osgDB::Registry::instance()->loadLibrary(OSGVERSE_3DTILES_PLUGIN_PATH) !=
          osgDB::Registry::NOT_LOADED);

    const std::string dir = makeTempDir();
    const std::string root = dir + "/root.json";
    writeText(root,
        "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":1000,\"root\":{"
        "\"boundingVolume\":{\"sphere\":[0,0,0,1000]},\"geometricError\":500,"
        "\"refine\":\"ADD\",\"children\":["
        "{\"boundingVolume\":{\"sphere\":[100,0,0,50]},\"geometricError\":50,"
        "\"content\":{\"uri\":\"west/tileset.json\"}},"
        "{\"boundingVolume\":{\"sphere\":[-100,0,0,60]},\"geometricError\":60,"
        "\"content\":{\"uri\":\"east/tileset.json\"}}]}}}");

    osg::ref_ptr<osg::Node> node = osgDB::readNodeFile(root + ".verse_tiles");
    CHECK(node.valid());
    GraphVisitor visitor;
    node->accept(visitor);
    CHECK(visitor.proxies.size() == 2);
    CHECK(visitor.proxies[0]->getLoadingExternalReferenceMode() ==
          osg::ProxyNode::DEFER_LOADING_TO_DATABASE_PAGER);
    CHECK(visitor.proxies[0]->getCenterMode() == osg::ProxyNode::USER_DEFINED_CENTER);
    CHECK(visitor.proxies[0]->getNumFileNames() == 1);
    CHECK(visitor.proxies[0]->getFileName(0).find("tileset.json.verse_tiles") !=
          std::string::npos);
    CHECK(visitor.proxies[0]->getRadius() > 0.0f);

    osgDB::deleteFile(root);
    osgDB::removeDirectory(dir);
    std::cout << "[tiles3d_paging_tests] deferred external roots OK\n";
    return 0;
}
```

- [ ] **Step 3: Build and verify RED**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Tiles3dPaging -j2
build/osgsol_core/bin/osgVerse_Test_Tiles3dPaging
```

Expected: the test aborts at `visitor.proxies.size() == 2`; the current reader synchronously tries the missing external JSON files and returns no proxies.

- [ ] **Step 4: Commit RED**

```bash
git add tests/tiles3d_paging_tests.cpp tests/CMakeLists.txt
git commit -m "test: reproduce eager external tileset loading"
```

---

### Task 2: Defer external JSON only below no-content containers

**Files:**
- Modify: `plugins/osgdb_3dtiles/ReaderWriter3dTiles.cpp:235-413`
- Test: `tests/tiles3d_paging_tests.cpp`

**Interfaces:**
- Consumes: parsed child JSON, resolved URI, child bounding sphere, and cloned `osgDB::Options`.
- Produces: `osg::Node* createDeferredExternalTileset(...)` and `DeferExternalTilesets` option propagation.

- [ ] **Step 1: Add the production helper**

Add this protected method beside `createTileChildren`:

```cpp
osg::Node* createDeferredExternalTileset(const std::string& uri,
                                         const osg::BoundingSphered& bound,
                                         const osgDB::Options* options) const
{
    osg::ProxyNode* proxy = new osg::ProxyNode;
    proxy->setName("DeferredTileset:" + uri);
    proxy->setLoadingExternalReferenceMode(
        osg::ProxyNode::DEFER_LOADING_TO_DATABASE_PAGER);
    proxy->setDatabaseOptions(options ? options->cloneOptions() : new osgDB::Options);
    proxy->setFileName(0, uri + ".verse_tiles");
    if (bound.valid())
    {
        proxy->setCenterMode(osg::ProxyNode::USER_DEFINED_CENTER);
        proxy->setCenter(bound.center());
        proxy->setRadius(bound.radius());
    }
    return proxy;
}
```

- [ ] **Step 2: Mark only no-content child groups as lazy**

When the current tile has children, set the cloned child options explicitly:

```cpp
const bool hasRoughContent = !uri.empty();
childOpt->setPluginStringData("DeferExternalTilesets",
                              hasRoughContent ? "0" : "1");
```

In `createTileChildren`, read the option once:

```cpp
const bool deferExternalTilesets =
    atoi(opt->getPluginStringData("DeferExternalTilesets").c_str()) > 0;
```

Pass that boolean into the tile-construction overload. After URI resolution, before synchronous `readNodeFile`, use:

```cpp
if (deferExternalTilesets && ext == "json" && !uri.empty())
    return createDeferredExternalTileset(uri, bound, options);
```

Do not set `DeferExternalTilesets=1` for a content-bearing parent; its refined group must still be fully constructed within the pager request before REPLACE can hide the rough child.

- [ ] **Step 3: Run GREEN and inspect the graph**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Tiles3dPaging -j2
build/osgsol_core/bin/osgVerse_Test_Tiles3dPaging
```

Expected: exit `0`, with exactly two deferred proxies even though neither external JSON file exists.

- [ ] **Step 4: Commit**

```bash
git add plugins/osgdb_3dtiles/ReaderWriter3dTiles.cpp \
        tests/tiles3d_paging_tests.cpp
git commit -m "perf: page visible external tilesets lazily"
```

---

### Task 3: Use real pixel-size refinement and retain rough/refined children

**Files:**
- Modify: `plugins/osgdb_3dtiles/ReaderWriter3dTiles.cpp:54-64,261-406`
- Test: `tests/tiles3d_paging_tests.cpp`

**Interfaces:**
- Consumes: `geometricError`, bounding-sphere radius, `MaxScreenSpaceError`.
- Produces: `double computeSwitchPixels(...)`, pixel-mode PagedLOD ranges, non-expiring rough child, and 30-second refined residency.

- [ ] **Step 1: Add failing pure/refinement assertions**

Extend the fixture with a content-bearing REPLACE root. The rough JSON is present; the refined JSON is deliberately absent because it must remain a pager filename at root-read time:

```cpp
const std::string roughRoot = dir + "/rough.json";
writeText(roughRoot,
    "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":0,\"root\":{"
    "\"boundingVolume\":{\"sphere\":[0,0,0,100]},\"geometricError\":0}}}");

const std::string lodRoot = dir + "/lod.json";
writeText(lodRoot,
    "{\"asset\":{\"version\":\"1.1\"},\"geometricError\":25,\"root\":{"
    "\"boundingVolume\":{\"sphere\":[0,0,0,100]},\"geometricError\":25,"
    "\"refine\":\"REPLACE\",\"content\":{\"uri\":\"rough.json\"},"
    "\"children\":[{\"boundingVolume\":{\"sphere\":[0,0,0,50]},"
    "\"geometricError\":10,\"content\":{\"uri\":\"refined.json\"}}]}}}");

osg::ref_ptr<osgDB::Options> options = new osgDB::Options;
options->setPluginStringData("UsePixelsOnScreen", "1");
options->setPluginStringData("MaxScreenSpaceError", "8");
osg::ref_ptr<osg::Node> lodNode =
    osgDB::readNodeFile(lodRoot + ".verse_tiles", options.get());
GraphVisitor lodVisitor;
lodNode->accept(lodVisitor);
CHECK(lodVisitor.pagedLods.size() == 1);
osg::PagedLOD* lod = lodVisitor.pagedLods.front();
CHECK(lod->getRangeMode() == osg::LOD::PIXEL_SIZE_ON_SCREEN);
CHECK(lod->getNumChildrenThatCannotBeExpired() == 1);
CHECK(std::fabs(lod->getMinimumExpiryTime(1) - 30.0) < 1e-9);
CHECK(std::fabs(lod->getMinRange(1) - 64.0f) < 1e-4f);

osgDB::deleteFile(lodRoot);
osgDB::deleteFile(roughRoot);
```

Use `boundingVolume.sphere` radius `100`, `geometricError=25`, and `SSE=8`, so the expected projected-diameter switch is `2*100*8/25 = 64` pixels.

Expected before implementation: expiry assertions fail, and the pixel threshold still depends on the hard-coded 1080/0.5629 conversion.

- [ ] **Step 2: Replace the fixed-screen conversion with a pure pixel threshold**

Add near the top of the plugin:

```cpp
static double computeSwitchPixels(double radius, double geometricError, double sse)
{
    if (!(radius > 0.0) || !(geometricError > 0.0) || !(sse > 0.0))
        return 1.0;
    return osg::clampBetween(2.0 * radius * sse / geometricError,
                             1.0, (double)FLT_MAX);
}
```

Keep raw `geometricError` separate from the legacy distance range. In pixel mode set:

```cpp
const double switchPixels = computeSwitchPixels(bound.radius(), geometricError, sse);
plod->setRangeMode(osg::LOD::PIXEL_SIZE_ON_SCREEN);
if (additive) plod->setRange(0, 0.0f, FLT_MAX);
else plod->setRange(0, 0.0f, static_cast<float>(switchPixels));
plod->setRange(1, static_cast<float>(switchPixels), FLT_MAX);
```

Retain the legacy distance calculation only for callers that do not request pixel mode.

- [ ] **Step 3: Add residency guarantees**

Immediately after configuring child `0` and file child `1`, add:

```cpp
plod->setNumChildrenThatCannotBeExpired(1);
plod->setMinimumExpiryTime(1, 30.0);
```

- [ ] **Step 4: Run GREEN**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Tiles3dPaging -j2
build/osgsol_core/bin/osgVerse_Test_Tiles3dPaging
```

Expected: exit `0`; range mode is pixel size, refined minimum range is `64`, rough child is protected, and refined expiry is `30` seconds.

- [ ] **Step 5: Commit**

```bash
git add plugins/osgdb_3dtiles/ReaderWriter3dTiles.cpp tests/tiles3d_paging_tests.cpp
git commit -m "perf: stabilize 3d tiles pixel refinement"
```

---

### Task 4: Enable and clamp Hong Kong pixel SSE in the application

**Files:**
- Modify: `applications/earth_explorer/tiles3d_data.h`
- Modify: `applications/earth_explorer/tiles3d_data.cpp:86-145`
- Test: `tests/tiles3d_paging_tests.cpp`

**Interfaces:**
- Produces: `double earthtiles3d::resolveScreenSpaceError(const char* value)` returning `[2,32]` with default `8`.
- Consumes: that value to populate cloned root options for HTTP and local paths.

- [ ] **Step 1: Add failing SSE policy assertions**

Include `../applications/earth_explorer/tiles3d_data.h` in the test and add:

```cpp
CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError(NULL) - 8.0) < 1e-9);
CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("") - 8.0) < 1e-9);
CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("1") - 2.0) < 1e-9);
CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("12.5") - 12.5) < 1e-9);
CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("99") - 32.0) < 1e-9);
CHECK(std::fabs(earthtiles3d::resolveScreenSpaceError("bad") - 8.0) < 1e-9);
```

Expected: compile failure because the helper does not exist.

- [ ] **Step 2: Implement the inline resolver**

Add to `tiles3d_data.h`:

```cpp
namespace earthtiles3d
{
    inline double resolveScreenSpaceError(const char* value)
    {
        if (!value || !*value) return 8.0;
        char* end = NULL;
        const double parsed = std::strtod(value, &end);
        if (end == value || *end != '\0' || !std::isfinite(parsed)) return 8.0;
        return parsed < 2.0 ? 2.0 : (parsed > 32.0 ? 32.0 : parsed);
    }
}
```

Add the required `<cmath>` and `<cstdlib>` includes.

- [ ] **Step 3: Use identical options for HTTP and local roots**

Before the `isHttp` branch, create:

```cpp
const double sse = earthtiles3d::resolveScreenSpaceError(getenv("EARTH_3DTILES_SSE"));
std::ostringstream sseText;
sseText << sse;
osg::ref_ptr<osgDB::Options> opt =
    new osgDB::Options(isHttp ? "Extension=verse_tiles" : "");
opt->setPluginStringData("UsePixelsOnScreen", "1");
opt->setPluginStringData("MaxScreenSpaceError", sseText.str());
```

Then use the same `opt` in either `readNodeFile` branch. Remove duplicated environment parsing.

- [ ] **Step 4: Add root-attach timing**

Store a `std::chrono::steady_clock::time_point _loadStartedAt` when the first background load begins. When the lightweight root is attached, log:

```cpp
const long long attachedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - _loadStartedAt).count();
OSG_NOTICE << "[Tiles3D] root_attached_ms=" << attachedMs
           << " sse=" << sse << " lazy_root=1" << std::endl;
```

Store the chosen SSE in the layer object so this log does not read the environment again.

- [ ] **Step 5: Run focused tests and commit**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Tiles3dPaging -j2
build/osgsol_core/bin/osgVerse_Test_Tiles3dPaging
git add applications/earth_explorer/tiles3d_data.h \
        applications/earth_explorer/tiles3d_data.cpp \
        tests/tiles3d_paging_tests.cpp
git commit -m "perf: enable bounded Hong Kong pixel SSE"
```

Expected: test exits `0`; invalid values resolve to `8`, and valid values clamp to `[2,32]`.

---

### Task 5: Verify offline behavior and live F2 latency without overclaiming

**Files:**
- Modify: `docs/superpowers/plans/2026-07-11-hk-3dtiles-progressive-plan.md` (append evidence only)

**Interfaces:**
- Consumes: Tasks 1-4.
- Produces: reproducible offline graph evidence and live timing evidence.

- [ ] **Step 1: Run focused and complete offline suites**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Tiles3dPaging -j2
build/osgsol_core/bin/osgVerse_Test_Tiles3dPaging
ctest --test-dir build/osgsol_core -L offline --output-on-failure
git diff --check
```

Expected: all commands exit `0`; the new test reports deferred roots and stable pixel refinement.

- [ ] **Step 2: Build the app**

```bash
cmake --build build/osgsol_core --target osgVerse_EarthExplorer -j2
```

- [ ] **Step 3: Run a cold-cache live diagnostic**

```bash
TMP_HOME="$(mktemp -d /tmp/osgsol-f2-home.XXXXXX)"
env -u EARTH_AI_KEY HOME="$TMP_HOME" OSG_NOTIFY_LEVEL=INFO \
  EARTH_IME=0 EARTH_OFFSCREEN=1 EARTH_AUTOCAP=1500 \
  EARTH_3DTILES=1 EARTH_3DTILES_SSE=8 \
  build/osgsol_core/bin/osgVerse_EarthExplorer \
  --goto 22.298 114.1722 0.6 \
  > /tmp/osgsol-f2-progressive.log 2>&1
rg -n 'root_attached_ms|DeferredTileset|Tiles3D|b3dm|KTX2' \
  /tmp/osgsol-f2-progressive.log
rm -rf "$TMP_HOME"
```

Acceptance:

- `root_attached_ms <= 5000` under a normally responding endpoint.
- Root attachment happens before all 17 top-level external JSONs are fetched.
- A visible coarse child appears within 12 seconds under a normally responding endpoint.
- If the public endpoint is slow or unavailable, record raw times/status and do not claim a fabricated percentage.

- [ ] **Step 4: Manual altitude-change acceptance**

At Hong Kong F2:

1. Hold at 600-1000 m until visible buildings refine.
2. Raise altitude briefly, return within 30 seconds, and confirm the prior refined area does not revert to an empty gap or restart from the coarsest texture.
3. Pan to a neighboring district and confirm only visible/near-visible partitions request content.
4. Disable F2 and confirm no new F2 requests start after NodeMask becomes `0`; re-enable and confirm resident content returns immediately.

- [ ] **Step 5: Record evidence and commit**

Append exact test counts, `root_attached_ms`, first-visible time, request ordering, and manual results, then:

```bash
git add docs/superpowers/plans/2026-07-11-hk-3dtiles-progressive-plan.md
git commit -m "docs: record Hong Kong paging verification"
```
