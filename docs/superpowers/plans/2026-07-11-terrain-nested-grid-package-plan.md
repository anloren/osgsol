# Nested Terrain Grid and Manual-Test Package Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the non-nested 16×16 terrain mesh with level-aware 17×17/33×33 grids, preserve every Hong Kong height/ancestor/skirt invariant, validate seams, and update the single fixed Desktop manual-test app after all three sub-projects pass.

**Architecture:** `TileCallback` selects a `2^n+1` grid from tile zoom and stores rows/columns on each `osg::Geometry`; creation, elevation updates, dynamic morphing, and skirts all read the same stored dimensions. The existing Hong Kong continuous elevation algorithm is moved unchanged into a small testable header and remains injected through `ElevationFilterFunction`. Packaging is the final integration gate and updates only `/Users/USER/Desktop/osgSol Earth.app`.

**Tech Stack:** C++17, OpenSceneGraph geometry/ECEF utilities, AWS Terrarium GL_FLOAT elevation, CMake/CTest, macOS app bundle/codesign.

## Global Constraints

- Work only in `/Users/USER/osgsol/.worktrees/v0.2-runtime-safety` on `codex/v0.2-runtime-safety`.
- Grid size is exactly `17` below z12 and exactly `33` at z12+.
- Preserve `TileElevationScale=2.0`, `TileSkirtRatio=0.05`, Terrarium decoding, z>15 ancestor addressing, `elevScaleBias`, and the continuous Hong Kong filter numerically.
- Do not restore Hong Kong bbox elevation-path suppression or 6 m prebaked elevation tiles.
- Do not modify globe GLSL, orthophoto URLs, ocean, atmosphere, F2 shader, tile transforms, or local/global height ratios.
- Existing geometry without grid metadata falls back safely to the legacy `16×16` dimensions; newly created terrain always records dimensions.
- Build one target at a time.
- Final delivery updates only `/Users/USER/Desktop/osgSol Earth.app`, bundle id `com.anloren.osgsol.earth`, version/build `0.2.0`, channel `manual-test`.
- Do not create a timestamped app, tag, merge, or pull request.

## File Structure

- `readerwriter/TileCallback.h`: grid-size policy and geometry dimension helpers.
- `readerwriter/TileCallback.cpp`: dynamic dimensions for create/update/skirt/morph paths.
- `applications/earth_explorer/hk_elevation_filter.h`: unchanged continuous Hong Kong height policy in a testable inline function.
- `applications/earth_explorer/earth_main.cpp`: thin environment-controlled callback wrapper and preserved TMS options.
- `tests/terrain_grid_tests.cpp`: grid, geometry, seam, skirt, Hong Kong policy, and source-invariant tests.
- `tests/CMakeLists.txt`: new offline terrain regression target.
- `docs/superpowers/plans/2026-07-11-terrain-nested-grid-package-plan.md`: final test/package evidence.

---

### Task 1: Add RED tests for nested grid dimensions and metadata

**Files:**
- Create: `tests/terrain_grid_tests.cpp`
- Modify: `tests/CMakeLists.txt:139-173`

**Interfaces:**
- Produces: RED assertions for `terrainGridSizeForLevel`, geometry dimensions, vertex counts, and skirt-safe metadata.
- Consumes: `TileCallback::createTileGeometry(...)`.

- [ ] **Step 1: Register the offline test**

```cmake
NEW_CTEST(osgVerse_Test_TerrainGrid terrain_grid_tests.cpp offline 120)
TARGET_COMPILE_DEFINITIONS(osgVerse_Test_TerrainGrid PRIVATE
                           OSGVERSE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
```

- [ ] **Step 2: Create the initial failing test**

```cpp
#include <cmath>
#include <cstdlib>
#include <iostream>

#include <osg/Geometry>
#include <osg/Image>
#include <osg/Texture2D>

#include <readerwriter/TileCallback.h>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; std::abort(); } } while (0)

static osg::Texture2D* constantElevation(float meters)
{
    osg::Image* image = new osg::Image;
    image->allocateImage(2, 2, 1, GL_RED, GL_FLOAT);
    float* values = reinterpret_cast<float*>(image->data());
    for (int i = 0; i < 4; ++i) values[i] = meters;
    return new osg::Texture2D(image);
}

static osg::Geometry* makeTile(int z)
{
    osg::ref_ptr<osgVerse::TileCallback> callback = new osgVerse::TileCallback(true);
    callback->setTileNumber(0, 0, z);
    callback->setFlatten(false);
    callback->setSkirtRatio(0.05f);
    callback->setElevationScale(2.0f);
    osg::Matrix matrix;
    return callback->createTileGeometry(
        matrix, constantElevation(10.0f),
        osg::Vec3d(0.30, 1.99, 0.0), osg::Vec3d(0.31, 2.00, 0.0),
        0.01, 0.01);
}

int main(int, char**)
{
    CHECK(osgVerse::terrainGridSizeForLevel(0) == 17u);
    CHECK(osgVerse::terrainGridSizeForLevel(11) == 17u);
    CHECK(osgVerse::terrainGridSizeForLevel(12) == 33u);
    CHECK(osgVerse::terrainGridSizeForLevel(19) == 33u);
    CHECK(((osgVerse::terrainGridSizeForLevel(12) - 1u) &
           (osgVerse::terrainGridSizeForLevel(12) - 2u)) == 0u);

    osg::ref_ptr<osg::Geometry> z11 = makeTile(11);
    osg::ref_ptr<osg::Geometry> z12 = makeTile(12);
    unsigned int rows = 0, columns = 0;
    CHECK(osgVerse::tileGeometryGridSize(z11.get(), rows, columns));
    CHECK(rows == 17u && columns == 17u);
    CHECK(z11->getVertexArray()->getNumElements() == 17u * 17u + 4u * 17u);
    CHECK(osgVerse::tileGeometryGridSize(z12.get(), rows, columns));
    CHECK(rows == 33u && columns == 33u);
    CHECK(z12->getVertexArray()->getNumElements() == 33u * 33u + 4u * 33u);

    std::cout << "[terrain_grid_tests] grid dimensions OK\n";
    return 0;
}
```

- [ ] **Step 3: Build and verify RED**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_TerrainGrid -j2
```

Expected: compile failure because `terrainGridSizeForLevel` and `tileGeometryGridSize` do not exist.

- [ ] **Step 4: Commit RED**

```bash
git add tests/terrain_grid_tests.cpp tests/CMakeLists.txt
git commit -m "test: reproduce coarse non-nested terrain grid"
```

---

### Task 2: Make all terrain geometry paths use stored dynamic dimensions

**Files:**
- Modify: `readerwriter/TileCallback.h:16-31`
- Modify: `readerwriter/TileCallback.cpp:17,226-510,848-875`
- Test: `tests/terrain_grid_tests.cpp`

**Interfaces:**
- Produces: `unsigned int terrainGridSizeForLevel(int)` and `bool tileGeometryGridSize(...)`.
- Consumes: Geometry user values `TileGridRows` and `TileGridColumns`.

- [ ] **Step 1: Add grid helpers to the public header**

```cpp
inline unsigned int terrainGridSizeForLevel(int z)
{
    return z >= 12 ? 33u : 17u;
}

inline bool tileGeometryGridSize(const osg::Geometry* geometry,
                                 unsigned int& rows, unsigned int& columns)
{
    rows = columns = 16u;
    if (!geometry) return false;
    unsigned int storedRows = 0, storedColumns = 0;
    if (!geometry->getUserValue("TileGridRows", storedRows) ||
        !geometry->getUserValue("TileGridColumns", storedColumns) ||
        storedRows < 2u || storedColumns < 2u)
        return false;
    rows = storedRows;
    columns = storedColumns;
    return true;
}
```

The `16×16` fallback is compatibility-only for geometry created by old/custom handlers.

- [ ] **Step 2: Use level-aware dimensions during creation**

Replace the creation constants with:

```cpp
const unsigned int numRows = terrainGridSizeForLevel(_z);
const unsigned int numCols = numRows;
```

Immediately after allocating `osg::Geometry`, before calling `updateSkirtData`, add:

```cpp
geom->setUserValue("TileGridRows", numRows);
geom->setUserValue("TileGridColumns", numCols);
```

- [ ] **Step 3: Read the same dimensions during update and skirt generation**

At the start of both `updateTileGeometry` and `updateSkirtData`, replace static constants with:

```cpp
unsigned int numRows = 16u, numCols = 16u;
tileGeometryGridSize(geom, numRows, numCols);
```

Keep all existing UV, elevation scale, normal, index-winding, and downward skirt calculations unchanged.

- [ ] **Step 4: Read geometry dimensions in dynamic morphing**

In `TileManager::updateTileGeometry`, replace static constants with:

```cpp
unsigned int numRows = 16u, numCols = 16u;
tileGeometryGridSize(geom, numRows, numCols);
```

Before the vertex loops add:

```cpp
if (!va || va->size() < numRows * numCols) return;
```

- [ ] **Step 5: Run GREEN and existing tile tests**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_TerrainGrid -j2
build/osgsol_core/bin/osgVerse_Test_TerrainGrid
cmake --build build/osgsol_core --target osgVerse_Test_TileOverlay -j2
build/osgsol_core/bin/osgVerse_Test_TileOverlay
```

Expected: both tests exit `0`; vertex counts exactly match 17/33 top grids plus their four skirt strips.

- [ ] **Step 6: Commit**

```bash
git add readerwriter/TileCallback.h readerwriter/TileCallback.cpp \
        tests/terrain_grid_tests.cpp
git commit -m "fix: use nested adaptive terrain grids"
```

---

### Task 3: Prove z15 ancestor subtiles share identical boundary samples

**Files:**
- Modify: `tests/terrain_grid_tests.cpp`

**Interfaces:**
- Consumes: `elevScaleBias` and per-tile local-to-world matrices.
- Produces: a deterministic z16/z17 common-edge ECEF seam test.

- [ ] **Step 1: Add a gradient elevation fixture**

```cpp
static osg::Texture2D* gradientElevation()
{
    osg::Image* image = new osg::Image;
    image->allocateImage(65, 65, 1, GL_RED, GL_FLOAT);
    float* values = reinterpret_cast<float*>(image->data());
    for (int y = 0; y < 65; ++y)
        for (int x = 0; x < 65; ++x)
            values[x + y * 65] = static_cast<float>(x + 2 * y);
    return new osg::Texture2D(image);
}
```

- [ ] **Step 2: Add the sibling-edge comparison**

Create two z16 callbacks whose geographic extents meet at `sharedLon`. Use the same gradient texture with biases `(0,0,0.5,0.5)` and `(0.5,0,0.5,0.5)`. Convert the first tile's right-edge and second tile's left-edge local vertices back to world ECEF:

```cpp
const osg::Matrixd leftToWorld = osg::Matrixd::inverse(leftCb->getTileWorldToLocalMatrix());
const osg::Matrixd rightToWorld = osg::Matrixd::inverse(rightCb->getTileWorldToLocalMatrix());
for (unsigned int row = 0; row < 33u; ++row)
{
    const osg::Vec3d leftWorld = osg::Vec3d((*leftVertices)[32u + row * 33u]) * leftToWorld;
    const osg::Vec3d rightWorld = osg::Vec3d((*rightVertices)[row * 33u]) * rightToWorld;
    CHECK((leftWorld - rightWorld).length() < 1e-3);
}
```

Repeat with z17 biases that meet on a quarter boundary. Compare only the top-grid vertices, not skirt vertices.

- [ ] **Step 3: Run the seam test**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_TerrainGrid -j2
build/osgsol_core/bin/osgVerse_Test_TerrainGrid
```

Expected: exit `0`; every common-edge ECEF pair differs by less than `1 mm`. A failure blocks later tasks and must be diagnosed against the exact rational endpoints `x/(numCols-1)` and `y/(numRows-1)` before any production edit; height scale and ancestor bias are out of scope for that diagnosis.

- [ ] **Step 4: Commit**

```bash
git add tests/terrain_grid_tests.cpp readerwriter/TileCallback.cpp
git commit -m "test: lock terrain ancestor seam continuity"
```

---

### Task 4: Extract and lock the existing Hong Kong continuous height policy

**Files:**
- Create: `applications/earth_explorer/hk_elevation_filter.h`
- Modify: `applications/earth_explorer/earth_main.cpp:582-640`
- Modify: `tests/terrain_grid_tests.cpp`

**Interfaces:**
- Produces: `earthterrain::applyHongKongElevationFilter(float*, int, int, int, int, int)`.
- Consumes: the existing `hkElevationFilter` callback signature used by `ElevationFilterFunction`.

- [ ] **Step 1: Add RED policy assertions**

Add `<fstream>` and `<string>`, include the new header, and add these helpers/cases built from real Web-Mercator tile coordinates:

```cpp
struct TmsTile { int x; int y; };

static TmsTile tmsTileForLonLat(double lonDegrees, double latDegrees, int z)
{
    const double n = static_cast<double>(1 << z);
    const double latRadians = osg::DegreesToRadians(latDegrees);
    const int x = static_cast<int>(std::floor((lonDegrees + 180.0) / 360.0 * n));
    const int yXyz = static_cast<int>(std::floor(
        (1.0 - std::log(std::tan(latRadians) + 1.0 / std::cos(latRadians)) /
         osg::PI) * 0.5 * n));
    return TmsTile{x, static_cast<int>(n) - 1 - yXyz};
}

static std::string readWholeFile(const std::string& path)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    CHECK(input.good());
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

const TmsTile coreTile = tmsTileForLonLat(114.17, 22.30, 15);
float core = 40.0f;
earthterrain::applyHongKongElevationFilter(
    &core, 1, 1, coreTile.x, coreTile.y, 15);
CHECK(std::fabs(core) < 1e-6f);

const TmsTile outsideTile = tmsTileForLonLat(0.0, 0.0, 15);
float outside = 40.0f;
earthterrain::applyHongKongElevationFilter(
    &outside, 1, 1, outsideTile.x, outsideTile.y, 15);
CHECK(std::fabs(outside - 40.0f) < 1e-6f);

const TmsTile metroTile = tmsTileForLonLat(114.00, 22.38, 15);
float metro = 40.0f;
earthterrain::applyHongKongElevationFilter(
    &metro, 1, 1, metroTile.x, metroTile.y, 15);
CHECK(std::fabs(metro - 18.0f) < 1e-3f);

float featherSamples[5];
for (int i = 0; i < 5; ++i)
{
    const double lon = 114.35 + 0.005 * static_cast<double>(i);
    const TmsTile tile = tmsTileForLonLat(lon, 22.38, 15);
    featherSamples[i] = 40.0f;
    earthterrain::applyHongKongElevationFilter(
        &featherSamples[i], 1, 1, tile.x, tile.y, 15);
}
for (int i = 1; i < 5; ++i)
{
    CHECK(featherSamples[i] >= featherSamples[i - 1]);
    CHECK(featherSamples[i] - featherSamples[i - 1] < 15.0f);
}
```

Expected: compile failure because the header/function does not exist.

- [ ] **Step 2: Move the algorithm without changing constants or formulas**

Create this guarded inline header; its constants and formulas are copied exactly from the current callback, except environment enablement stays in `earth_main.cpp`:

```cpp
#ifndef OSGSOL_HK_ELEVATION_FILTER_H
#define OSGSOL_HK_ELEVATION_FILTER_H

#include <algorithm>
#include <cmath>
#include <osg/Math>

namespace earthterrain
{
inline void applyHongKongElevationFilter(float* hts, int w, int h,
                                         int x, int y, int z)
{
    if (!hts || w <= 0 || h <= 0) return;

    int tz = z, tx = x, tyTMS = y;
    if (z > 15)
    {
        int dz = z - 15;
        tx = x >> dz;
        tyTMS = y >> dz;
        tz = 15;
    }
    const double n = static_cast<double>(1 << tz);
    const int tyXYZ = static_cast<int>(n) - 1 - tyTMS;
    const double lonMin = tx / n * 360.0 - 180.0;
    const double lonSpan = 360.0 / n;
    const double latN = std::atan(std::sinh(osg::PI * (1.0 - 2.0 * tyXYZ / n))) *
                        180.0 / osg::PI;
    const double latS = std::atan(std::sinh(osg::PI *
                        (1.0 - 2.0 * (tyXYZ + 1) / n))) * 180.0 / osg::PI;
    if (lonMin > 114.37 || lonMin + lonSpan < 113.88 ||
        latS > 22.44 || latN < 22.17)
        return;

    const auto smooth01 = [](double t) {
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        return t * t * (3.0 - 2.0 * t);
    };
    const auto outerW = [&smooth01](double lat, double lon,
                                    double la0, double la1,
                                    double lo0, double lo1, double feather) {
        const double dla = std::max(std::max(la0 - lat, lat - la1), 0.0);
        const double dlo = std::max(std::max(lo0 - lon, lon - lo1), 0.0);
        return smooth01(std::sqrt(dla * dla + dlo * dlo) / feather);
    };

    for (int row = 0; row < h; ++row)
    {
        const double yf = static_cast<double>(tyXYZ) + 1.0 -
                          (static_cast<double>(row) + 0.5) / static_cast<double>(h);
        const double lat = std::atan(std::sinh(osg::PI * (1.0 - 2.0 * yf / n))) *
                           180.0 / osg::PI;
        for (int col = 0; col < w; ++col)
        {
            const double lon = lonMin + (static_cast<double>(col) + 0.5) /
                               static_cast<double>(w) * lonSpan;
            const double wMetro = outerW(lat, lon, 22.19, 22.42,
                                         113.90, 114.35, 0.02);
            if (wMetro >= 1.0) continue;
            const double wFlat = outerW(lat, lon, 22.276, 22.335,
                                        114.115, 114.225, 0.012);
            float& value = hts[row * w + col];
            const double metroHeight = static_cast<double>(value) * 0.5 - 2.0;
            const double innerHeight = wFlat * metroHeight;
            value = static_cast<float>(wMetro * static_cast<double>(value) +
                                       (1.0 - wMetro) * innerHeight);
        }
    }
}
}

#endif
```

- [ ] **Step 3: Keep environment control in the thin callback**

Replace the old body in `earth_main.cpp` with:

```cpp
static void hkElevationFilter(float* hts, int w, int h, int x, int y, int z)
{
    static const bool enabled = []() {
        const char* value = getenv("EARTH_HK_FLATDEM");
        return !(value && *value && atoi(value) == 0);
    }();
    if (!enabled) return;
    earthterrain::applyHongKongElevationFilter(hts, w, h, x, y, z);
}
```

Keep this existing injection unchanged:

```cpp
earthOptions->setPluginData("ElevationFilterFunction", (void*)hkElevationFilter);
```

- [ ] **Step 4: Add source-invariant guards**

Read `earth_main.cpp` and the extracted header in the test and assert:

```cpp
const std::string mainSource = readWholeFile(
    std::string(OSGVERSE_SOURCE_DIR) + "/applications/earth_explorer/earth_main.cpp");
const std::string filterSource = readWholeFile(
    std::string(OSGVERSE_SOURCE_DIR) +
    "/applications/earth_explorer/hk_elevation_filter.h");
CHECK(mainSource.find("TileElevationScale=2.0") != std::string::npos);
CHECK(mainSource.find("TileSkirtRatio=") != std::string::npos);
CHECK(mainSource.find("ElevationFilterFunction") != std::string::npos);
CHECK(mainSource.find("x >> dz") != std::string::npos);
CHECK(mainSource.find("y >> dz") != std::string::npos);
CHECK(filterSource.find("if (z > 15)") != std::string::npos);
CHECK(filterSource.find("x >> dz") != std::string::npos);
CHECK(filterSource.find("y >> dz") != std::string::npos);
```

- [ ] **Step 5: Run GREEN and commit**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_TerrainGrid -j2
build/osgsol_core/bin/osgVerse_Test_TerrainGrid
git add applications/earth_explorer/hk_elevation_filter.h \
        applications/earth_explorer/earth_main.cpp tests/terrain_grid_tests.cpp
git commit -m "test: preserve Hong Kong terrain continuity policy"
```

Expected: exit `0`; core, metro, outside, feather, and source-invariant checks pass.

---

### Task 5: Complete integrated regression and update the fixed Desktop app

**Files:**
- Modify: `docs/superpowers/plans/2026-07-11-terrain-nested-grid-package-plan.md` (append evidence only)
- Runtime delivery: `/Users/USER/Desktop/osgSol Earth.app`

**Interfaces:**
- Consumes: completed photo, 3D Tiles, and terrain plans.
- Produces: pushed source, fresh Release install, verified fixed Desktop app, and manual acceptance handoff.

- [ ] **Step 1: Build affected targets sequentially**

```bash
cmake --build build/osgsol_core --target osgVerse_Test_Ai_Chat -j2
cmake --build build/osgsol_core --target osgVerse_Test_Tiles3dPaging -j2
cmake --build build/osgsol_core --target osgVerse_Test_TerrainGrid -j2
cmake --build build/osgsol_core --target osgVerse_Test_TileOverlay -j2
cmake --build build/osgsol_core --target osgVerse_Test_EarthManipulator -j2
cmake --build build/osgsol_core --target osgVerse_EarthExplorer -j2
```

- [ ] **Step 2: Run full offline and preservation gates**

```bash
ctest --test-dir build/osgsol_core -L offline --output-on-failure
rg -n 'TileElevationScale=2\.0|ElevationFilterFunction|TileSkirtRatio|UsePixelsOnScreen|camera_flight_in_progress' \
  applications readerwriter plugins tests
git diff --check
git status --short
```

Expected: all offline tests pass; every invariant token appears in production and tests; only intended files are modified.

- [ ] **Step 3: Run visual regression before packaging**

Use the fresh worktree executable and record screenshots/logs for:

1. Global high altitude: atmosphere, ocean, terminator normal.
2. North pole: closed surface and undistorted imagery.
3. Kunming low oblique: no underside exposure or camera penetration.
4. Hong Kong with F2 off: smoother near-ground terrain, no bright fake mounds, holes, or stretched map.
5. Hong Kong with F2 on: coarse mesh appears quickly, refines while stationary, survives a short altitude round trip, no empty REPLACE gap.
6. NVIDIA oblique photo: no camera jump at shutter.
7. ISS oblique photo: visible view preserved; no platform unless explicitly requested.
8. Hong Kong photo followed by NVIDIA: no reused image/coordinates/paths.

Any regression in height ratio, holes, or map distortion blocks packaging.

- [ ] **Step 4: Create a fresh Release install and generic signed bundle**

```bash
env -u EARTH_AI_KEY cmake --build build/osgsol_core --target install -j2
env -u EARTH_AI_KEY \
  OSGVERSE_SDK="/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/sdk_core" \
  OSG_ROOT="/Users/USER/osgsol/build/sdk_core" \
  bash packaging/package_macos.sh
```

Expected: `dist/EarthExplorer.app` is freshly built and passes the packaging script's immediate strict signature check.

- [ ] **Step 5: Stage the formal osgSol identity outside Desktop**

```bash
STAGE="/tmp/osgSol Earth.app"
rm -rf "$STAGE"
ditto "dist/EarthExplorer.app" "$STAGE"
mv "$STAGE/Contents/MacOS/osgVerse_EarthExplorer" \
   "$STAGE/Contents/MacOS/osgSol_Earth"
/usr/libexec/PlistBuddy -c 'Set :CFBundleName osgSol Earth' "$STAGE/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleDisplayName osgSol Earth' "$STAGE/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleIdentifier com.anloren.osgsol.earth' "$STAGE/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleVersion 0.2.0' "$STAGE/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleShortVersionString 0.2.0' "$STAGE/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Set :CFBundleExecutable osgSol_Earth' "$STAGE/Contents/Info.plist"
/usr/libexec/PlistBuddy -c 'Add :OSGSolBuildChannel string manual-test' "$STAGE/Contents/Info.plist" 2>/dev/null || \
  /usr/libexec/PlistBuddy -c 'Set :OSGSolBuildChannel manual-test' "$STAGE/Contents/Info.plist"
SOURCE_COMMIT="$(git rev-parse HEAD)"
/usr/libexec/PlistBuddy -c "Add :OSGSolSourceCommit string $SOURCE_COMMIT" "$STAGE/Contents/Info.plist" 2>/dev/null || \
  /usr/libexec/PlistBuddy -c "Set :OSGSolSourceCommit $SOURCE_COMMIT" "$STAGE/Contents/Info.plist"
xattr -cr "$STAGE"
codesign --force --deep --sign - "$STAGE"
codesign --verify --deep --strict "$STAGE"
```

- [ ] **Step 6: Replace only the fixed Desktop app and smoke it**

```bash
FIXED="/Users/USER/Desktop/osgSol Earth.app"
rm -rf "$FIXED"
ditto "$STAGE" "$FIXED"
chmod 555 "$FIXED/Contents/MacOS"

HOME_DIR="$(mktemp -d /tmp/osgsol-final-home.XXXXXX)"
env -u EARTH_AI_KEY HOME="$HOME_DIR" EARTH_IME=0 \
  EARTH_OFFSCREEN=1 EARTH_AUTOCAP=300 \
  "$FIXED/Contents/MacOS/osgSol_Earth" \
  > /tmp/osgsol-final-smoke.log 2>&1
rm -rf "$HOME_DIR"
grep -q '\[Earth\] offscreen context' /tmp/osgsol-final-smoke.log
grep -q '\[Earth\] offscreen capture saved' /tmp/osgsol-final-smoke.log
test -z "$(find "$FIXED" -name imgui.ini -print -quit)"
test -z "$(find "$FIXED/Contents/MacOS" -maxdepth 1 -name 'libhv.*.log' -print -quit)"
```

- [ ] **Step 7: Clean FileProvider metadata, re-sign, and verify exact identity**

```bash
chmod 755 "$FIXED/Contents/MacOS"
xattr -cr "$FIXED"
codesign --force --deep --sign - "$FIXED"
chmod 555 "$FIXED/Contents/MacOS"
codesign --verify --deep --strict "$FIXED"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$FIXED/Contents/Info.plist")" = \
     "com.anloren.osgsol.earth"
test "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$FIXED/Contents/Info.plist")" = \
     "0.2.0"
test "$(/usr/libexec/PlistBuddy -c 'Print :OSGSolBuildChannel' "$FIXED/Contents/Info.plist")" = \
     "manual-test"
test "$(/usr/libexec/PlistBuddy -c 'Print :OSGSolSourceCommit' "$FIXED/Contents/Info.plist")" = \
     "$(git rev-parse HEAD)"
```

Record the known external caveat separately if iCloud FileProvider later recreates root FinderInfo; the immediate post-cleanup strict check, runtime smoke, executable hash, and unchanged bundle contents are the authoritative evidence.

- [ ] **Step 8: Record evidence, commit, and push**

Append exact focused/offline counts, screenshot paths/hashes, F2 timings, manual photo outcomes, package commit, executable SHA-256, identity values, and immediate strict-signature result. Then:

```bash
git add docs/superpowers/plans/2026-07-11-photo-visible-camera-plan.md \
        docs/superpowers/plans/2026-07-11-hk-3dtiles-progressive-plan.md \
        docs/superpowers/plans/2026-07-11-terrain-nested-grid-package-plan.md
git commit -m "docs: record photo tiles and terrain acceptance"
git push origin codex/v0.2-runtime-safety
```

Do not tag, merge, create a pull request, or create any additional Desktop app.

## Task 5 integrated release evidence (2026-07-11)

### Release decision and explicit polar exception

The first pre-package review stopped on a visible radial imagery starburst at the North Pole. A
read-only A/B run then used the same isolated-HOME offscreen command, `--goto 89 0 20000`, against
the existing fixed Desktop package at source
`c3a8ebb3002be3f9d1d1b9aaa64da1ccbaa1e303` and the current worktree at source
`0eecbef178e19a55552b7d06eb78bb32d381a34c`. Direct inspection showed the same starburst location,
scale, orientation, and closed globe in both. In a radius-240 polar region centered at `(960,540)`,
the images had RGB correlation `0.996363443`, MAE `1.925544`, and PSNR `34.898839 dB`.

The user then explicitly chose **“先打包”** and approved this A/B-confirmed, pre-existing North Pole
Web Mercator starburst as a known limitation for this release. This is a release exception, not a
fix and not evidence that the polar imagery is undistorted. The current terrain-grid changes did not
introduce the observed pattern under this diagnostic.

### Sequential build, offline, and preservation gates

- All six required commands were run separately and in order; each exited `0`:
  `osgVerse_Test_Ai_Chat`, `osgVerse_Test_Tiles3dPaging`, `osgVerse_Test_TerrainGrid`,
  `osgVerse_Test_TileOverlay`, `osgVerse_Test_EarthManipulator`, and
  `osgVerse_EarthExplorer`.
- `ctest --test-dir build/osgsol_core -L offline --output-on-failure` exited `0`:
  **15/15 passed, 0 failed**, real time `14.00 s`.
- The preservation search found `TileElevationScale=2.0`, `ElevationFilterFunction`,
  `TileSkirtRatio`, `UsePixelsOnScreen`, and `camera_flight_in_progress` in production and test
  sources.
- `git diff --check` exited `0`; the worktree was clean before this evidence-only documentation
  update.

### Offscreen diagnostics and screenshots

- Global high altitude, `--goto 15 110 20000`:
  `/tmp/osgsol-task5-global.png`, SHA-256
  `b1ece40ca1a70a13f6d4486dff12680eb498e4a7338ed11e9a2743f55494218f`.
  The 1920x1080 capture showed a closed globe, atmosphere rim, ocean, imagery, and star field.
- Current North Pole global capture:
  `/tmp/osgsol-task5-current-north-pole-global.png`, SHA-256
  `9f0ad57cb7f3ed6f1c32f775c5a5a7128ecf108c1e95502ce365dc01aacda216`.
- Pre-grid fixed-package North Pole A/B capture:
  `/tmp/osgsol-task5-old-north-pole-global.png`, SHA-256
  `8a1cf8211c2777cd3bea46448a52c9517fca377bb51ce92bec47bcbb38326c7f`.
- Kunming low oblique:
  `/tmp/osgsol-task5-kunming-low-oblique.png`, SHA-256
  `ed8b68dda0bafad3187b58570db5b41756279356c9ea80905d303aadf6fea53a`.
  The run exited `0`, but the offscreen image contained only seven near-uniform RGB values; it is
  not underside/camera-penetration acceptance.
- Hong Kong F2 off:
  `/tmp/osgsol-task5-hk-f2-off.png`, SHA-256
  `69e69999ace3bf3fcfdbdfb1ce91a6430646857528e1f653f0b6bfb6beb0b87c`.
  The run exited `0`, but the offscreen image contained only five near-uniform RGB values; it is not
  terrain-shape, mound, hole, or stretch acceptance.
- Hong Kong F2 on, pinned worktree plugin:
  `/tmp/osgsol-task5-hk-f2-on.png`, SHA-256
  `ad6ad1333a862359f0845fe32bd1adeb5fdfc84c90268a6ae42e5bbb19e939eb`.
  The process exited `0` in `25.525 s`. F2 began at `14:10:38.444`, the exact worktree-installed
  `osgdb_verse_tiles.so` loaded at `14:10:40.337`, and
  `root_attached_ms=1926 sse=8 lazy_root=1` appeared at `14:10:40.370`. Only partitions `11` and
  `15` were requested (`2/17` top-level roots). The first KTX2 loaded at `14:10:48.491`,
  `10.047 s` after F2 start; capture occurred at `14:10:55.919`, `17.475 s` after start. No F2
  HTTP 4xx/5xx or `[Tiles3D] FAILED` marker appeared. The capture was still not usable as visible
  building/refinement proof.

No NVIDIA, ISS, Hong Kong altitude round-trip, neighboring-district, F2 toggle, or photo-isolation
manual visual pass is claimed.

### Fresh install and generic package

- With `EARTH_AI_KEY` unset,
  `cmake --build build/osgsol_core --target install -j2` exited `0` and freshly installed the app,
  libraries, resources, tests, and plugins into the worktree `build/sdk_core`.
- With `EARTH_AI_KEY` unset, the exact `OSGVERSE_SDK`/`OSG_ROOT` packaging command exited `0` and
  printed `Built and verified: .../dist/EarthExplorer.app` after its immediate deep/strict check.
- Generic executable SHA-256:
  `9b3a29777bf5ee67f20e7849d9f6f0da966f688ed1a77a44a0a988233d1aefeb`.
- The installed current plugin contained `DeferExternalTilesets` and `DeferredTileset:` and had
  SHA-256 `a8688039dfcdeda3d3d86efc968a884988f2c0721d7cd69d99d5a609844f65b2`.
- The package-rewritten plugin retained both markers and had SHA-256
  `e5bb23ceb88b8a5b650d3053a683619181be9b70eab801d6ad8d135551f1c892`.

### Formal identity, fixed Desktop app, and bundled-plugin proof

`/tmp/osgSol Earth.app` was staged from the generic bundle, renamed to executable
`osgSol_Earth`, ad-hoc signed, and immediately passed deep/strict verification. The staged identity
and the fixed Desktop identity are:

- `CFBundleName`: `osgSol Earth`
- `CFBundleDisplayName`: `osgSol Earth`
- `CFBundleIdentifier`: `com.anloren.osgsol.earth`
- `CFBundleVersion`: `0.2.0`
- `CFBundleShortVersionString`: `0.2.0`
- `CFBundleExecutable`: `osgSol_Earth`
- `OSGSolBuildChannel`: `manual-test`
- `OSGSolSourceCommit`: `0eecbef178e19a55552b7d06eb78bb32d381a34c`

Only `/Users/USER/Desktop/osgSol Earth.app` was replaced. The prior bundle's mode-555
`Contents/MacOS` first prevented `rm`; restoring that old directory to `755` allowed the same fixed
path to be removed and replaced, after which the new directory was returned to `555`. No additional
Desktop app was created.

The fixed basic smoke used an isolated HOME with `EARTH_AI_KEY` and `OSG_LIBRARY_PATH` unset and
`EARTH_OFFSCREEN=1 EARTH_AUTOCAP=300`. It exited `0`, logged both the offscreen-context and
capture-saved markers, and produced `/tmp/osgsol-task5-final-smoke.png`, SHA-256
`09ef5bc9fea1d5441c9a0ec0ae8729f2f87c207d7923e0e41e30131031d99686`.

A second fixed-app F2 run also left `OSG_LIBRARY_PATH` unset. `DYLD_PRINT_LIBRARIES=1` proved that
the exact file loaded was:

```text
/Users/USER/Desktop/osgSol Earth.app/Contents/lib/osgPlugins-3.6.5/osgdb_verse_tiles.so
```

That file has Mach-O UUID `74AE9120-27A7-32EF-A526-C5E581425538`, retains both current deferral
markers, and has SHA-256
`e5bb23ceb88b8a5b650d3053a683619181be9b70eab801d6ad8d135551f1c892`. F2 began at
`14:15:28.924`; `root_attached_ms=1916` appeared at `14:15:30.840`; only `2/17` top-level
partitions were requested; the first KTX2 loaded at `14:15:35.740`, `6.816 s` after start. The run
exited `0` and produced `/tmp/osgsol-task5-fixed-f2.png`, SHA-256
`ed54563bc8ca0bd33ffa9bbf7b9ce60c8e24bd6da6950ac4830ee0a8724abb71`.

### Final signature, hashes, permissions, and FileProvider caveat

After `xattr -cr`, ad-hoc deep signing, and restoring `Contents/MacOS` to mode `555`, the fixed app
immediately passed `codesign --verify --deep --strict`. Identity checks passed, both plugin markers
remained present, and the bundle contained no `imgui.ini` or `Contents/MacOS/libhv.*.log`.

- Formal/fixed executable SHA-256:
  `707bbbdbd56baf9bcb5e5dac66786a4ee353d3301ac568db793b2a18851f1435`.
- Fixed bundled-plugin SHA-256:
  `e5bb23ceb88b8a5b650d3053a683619181be9b70eab801d6ad8d135551f1c892`.
- Fixed `Info.plist` SHA-256:
  `c2198570ce36d6e1d300adac77867326639465f63c46bea1c86eaecfc83f1c0d`.
- Signature: ad-hoc, identifier `com.anloren.osgsol.earth`; immediate deep/strict result `0`.

Two seconds after the successful immediate check, iCloud FileProvider recreated root
`com.apple.FinderInfo` and `com.apple.fileprovider.fpfs#P` (alongside `com.apple.provenance`), and a
later strict check reported `resource fork, Finder information, or similar detritus not allowed`.
This known external metadata reattachment did not change the recorded executable, plugin, or plist
hashes. Per the release brief, the immediate post-cleanup strict verification, smoke, hashes, and
unchanged bundle contents are authoritative; the cleanup/re-sign/immediate check is repeated at final
handoff.

### Pending manual acceptance

The following remain pending and are not represented as passes:

1. Kunming low-oblique underside exposure and camera penetration.
2. Hong Kong F2-off terrain smoothness and absence of bright fake mounds, holes, or stretched map.
3. Hong Kong F2-on visible coarse/refinement, short altitude round trip, no empty REPLACE gap,
   neighboring-district request locality, and off/on resident-content behavior.
4. NVIDIA oblique photo with no shutter-time camera jump.
5. ISS oblique photo preserving the visible view with no unsolicited platform/solar panels.
6. Hong Kong photo followed by NVIDIA with no image, coordinates, prompt suffix, or output-path
   reuse.

The North Pole radial starburst remains a documented pre-existing known limitation accepted by the
user for this release; it is not fixed.
