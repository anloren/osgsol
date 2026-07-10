# P2 卫星层(CelesTrak TLE + SGP4)Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 给 EarthExplorer 新增一个独立的卫星可视化图层——精选组(空间站/导航星座/气象卫星,约 200 颗,可点选看轨道线+地面足迹+详情)+ Starlink 全量点云壳层(约 7000 颗,纯视觉,不可点选),数据源为 CelesTrak TLE,轨道用 SGP4 递推。

**Architecture:** 新增独立模块 `sat_math.{h,cpp}`(纯函数:TLE 解析、SGP4 包装、足迹圆/轨道采样几何,unit-testable,不建 GL 上下文)+ `sat_data.{h,cpp}`(仿 `flight_data.cpp` 模板:后台线程拉取+缓存+SGP4 递推、GL_POINTS 点精灵渲染、拾取、右上角详情卡)。SGP4 算法本身 vendor 自 `dnwrnr/sgp4`(Apache-2.0,已实测编译+数值验证)到 `3rdparty/sgp4/`。零侵入 `scattering_globe` 着色器和 `osgDB::DatabasePager` 分页机制(红线)。

**Tech Stack:** C++14、OSG(osg::Geometry/PagedLOD 不涉及,纯 GL_POINTS/GL_LINE_STRIP)、libhv(`requests`,已用于 flight/precip)、picojson 不需要(TLE 是纯文本格式,非 JSON)、vendored `libsgp4`(dnwrnr/sgp4,Apache-2.0)。

## Global Constraints

- 红线:不碰 `assets/shaders/scattering_globe.*.glsl`、不碰 `osgDB::DatabasePager`/`PagedLOD` 分页机制(卫星是独立 GL_POINTS 图层,不流式分页)。
- OSG 日志宏(`OSG_WARN`/`OSG_NOTICE`)出现在 `if`/`else` 里必须加大括号(本仓库反复踩过 dangling-else 坑,`docs/superpowers/plans` 历次计划都有此条)。
- 每个渲染管线级/新模块改动后必须跑 4 类历史回归分类器(global/pole/kunming/hk)截图确认无交叉污染。
- 「全部」预设纳入 精选组三类目(空间站/导航星座/气象),**不纳入 Starlink**(spec 明确要求,避免默认喧宾夺主+带宽成本)。
- 新增 C++ 文件遵循本仓库既有约定:每帧渲染逻辑走 update callback,后台网络线程不碰 GL/OSG 场景图对象,只经加锁的 pending 快照与主线程交换(同 `flight_data.cpp`/`precip_data.cpp`)。
- 测试用自定义 `CHECK` 宏(防 Release `-DNDEBUG` 吞掉 `assert`),同 `tests/feed_layer_tests.cpp`/`tests/tile_overlay_tests.cpp` 既有约定。

---

### Task 1: Vendor SGP4 library

**Files:**
- Create: `3rdparty/sgp4/CoordGeodetic.h`, `3rdparty/sgp4/CoordGeodetic.cc`
- Create: `3rdparty/sgp4/CoordTopocentric.h`, `3rdparty/sgp4/CoordTopocentric.cc`
- Create: `3rdparty/sgp4/DateTime.h`, `3rdparty/sgp4/DateTime.cc`
- Create: `3rdparty/sgp4/DecayedException.h`, `3rdparty/sgp4/DecayedException.cc`
- Create: `3rdparty/sgp4/Eci.h`, `3rdparty/sgp4/Eci.cc`
- Create: `3rdparty/sgp4/Globals.h`, `3rdparty/sgp4/Globals.cc`
- Create: `3rdparty/sgp4/Observer.h`, `3rdparty/sgp4/Observer.cc`
- Create: `3rdparty/sgp4/OrbitalElements.h`, `3rdparty/sgp4/OrbitalElements.cc`
- Create: `3rdparty/sgp4/SatelliteException.h`, `3rdparty/sgp4/SatelliteException.cc`
- Create: `3rdparty/sgp4/SGP4.h`, `3rdparty/sgp4/SGP4.cc`
- Create: `3rdparty/sgp4/SolarPosition.h`, `3rdparty/sgp4/SolarPosition.cc`
- Create: `3rdparty/sgp4/Tle.h`, `3rdparty/sgp4/Tle.cc`
- Create: `3rdparty/sgp4/TleException.h`, `3rdparty/sgp4/TleException.cc`
- Create: `3rdparty/sgp4/TimeSpan.h`, `3rdparty/sgp4/TimeSpan.cc`
- Create: `3rdparty/sgp4/Util.h`, `3rdparty/sgp4/Util.cc`
- Create: `3rdparty/sgp4/Vector.h`, `3rdparty/sgp4/Vector.cc`
- Modify: `3rdparty/CMakeLists.txt`
- Test: `tests/satellite_tests.cpp` (新建,本任务只写第一段断言,Task 2 会继续扩充)
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Produces: `libsgp4::Tle(line1, line2)`、`libsgp4::SGP4(tle)`、`sgp4.FindPosition(tsinceMinutes) -> libsgp4::Eci`、`eci.Position()/.Velocity() -> libsgp4::Vector{x,y,z}`(TEME 系,km / km/s)、`eci.ToGeodetic() -> libsgp4::CoordGeodetic{latitude,longitude(弧度),altitude(km)}`、`tle.Epoch() -> libsgp4::DateTime`、`tle.MeanMotion() -> double`(圈/天)、`libsgp4::DateTime::Now()`、`(dt1-dt2).TotalMinutes()`。这些是后续 Task 2 `sat_math.cpp` 唯一直接依赖 vendored 库的地方。

**背景**:dnwrnr/sgp4(Apache License 2.0)是 SGP4(Simplified General Perturbations 4)轨道递推算法的成熟 C++ 移植,已实测在本仓库工具链下编译通过、数值与 Python 官方 `sgp4` 包(Brandon Rhodes 维护,广泛用作行业参考实现)完全一致(见下方验证向量)。**不要**尝试手写/精简移植 SGP4 算法本身——它包含深空共振修正等大量数值细节,手抄极易引入难以察觉的静默错误;直接 vendor 经过验证的完整实现风险最低。

固定 commit(2026-07-05 核实为 `master` 最新):`d176906dbfaf8361eee1f83bd1dc1bacc6582798`。

- [ ] **Step 1: 拉取 vendor 源码**

```bash
mkdir -p 3rdparty/sgp4
SHA=d176906dbfaf8361eee1f83bd1dc1bacc6582798
BASE="https://raw.githubusercontent.com/dnwrnr/sgp4/$SHA/libsgp4"
for f in CoordGeodetic CoordTopocentric DateTime DecayedException Eci Globals Observer \
         OrbitalElements SatelliteException SGP4 SolarPosition Tle TleException TimeSpan Util Vector; do
  curl -sf -o "3rdparty/sgp4/${f}.h" "$BASE/${f}.h" || { echo "FAILED: $f.h"; exit 1; }
  curl -sf -o "3rdparty/sgp4/${f}.cc" "$BASE/${f}.cc" || { echo "FAILED: $f.cc"; exit 1; }
done
find 3rdparty/sgp4 -size 0   # 必须无输出(零字节文件=拉取失败,需重跑)
```

Expected: 命令无 "FAILED" 输出,`find ... -size 0` 无输出。`wc -l 3rdparty/sgp4/*.h 3rdparty/sgp4/*.cc | tail -1` 应显示约 5139 行。

- [ ] **Step 2: 唯一必要的兼容性补丁**

`Util.cc` 用了 C++17 专属的嵌套命名空间语法 `namespace libsgp4::Util { ... }`。本仓库默认 `CMAKE_CXX_STANDARD 14`(根 `CMakeLists.txt:8`),该语法在 clang/gcc 下能靠编译器扩展勉强过(带警告),但不能保证所有目标编译器都支持——拆成两层嵌套命名空间是纯语法调整、零逻辑变化。

用 Edit 工具修改 `3rdparty/sgp4/Util.cc`:
- 第一处:`namespace libsgp4::Util` → `namespace libsgp4 { namespace Util`(该行是文件里 `#include` 之后的第一个非空行)
- 第二处(文件末尾):`} // namespace libsgp4::Util` → `} } // namespace libsgp4::Util`

- [ ] **Step 3: 接入 3rdparty 构建**

`3rdparty/CMakeLists.txt` 现有的 `SET(LIBRARY_FILES ...)` 大列表(约第 535 行起)里找到这一行:

```cmake
    clipper2/clipper.engine.cpp clipper2/clipper.offset.cpp clipper2/clipper.rectclip.cpp pybind11/pybind11.h
```

紧接着这一行之后插入新的一行(风格与既有条目一致:同一行列出该库的全部 .h/.cc 文件):

```cmake
    sgp4/CoordGeodetic.h sgp4/CoordGeodetic.cc sgp4/CoordTopocentric.h sgp4/CoordTopocentric.cc
    sgp4/DateTime.h sgp4/DateTime.cc sgp4/DecayedException.h sgp4/DecayedException.cc sgp4/Eci.h sgp4/Eci.cc
    sgp4/Globals.h sgp4/Globals.cc sgp4/Observer.h sgp4/Observer.cc sgp4/OrbitalElements.h sgp4/OrbitalElements.cc
    sgp4/SatelliteException.h sgp4/SatelliteException.cc sgp4/SGP4.h sgp4/SGP4.cc sgp4/SolarPosition.h sgp4/SolarPosition.cc
    sgp4/Tle.h sgp4/Tle.cc sgp4/TleException.h sgp4/TleException.cc sgp4/TimeSpan.h sgp4/TimeSpan.cc
    sgp4/Util.h sgp4/Util.cc sgp4/Vector.h sgp4/Vector.cc
```

（CMake 允许一个 `SET` 内跨多行续写字符串列表,不需要额外的 `${...}` 拼接——参照同文件里 `IMGUI_SOURCE_FILES` 等变量的多行写法即可,这里直接内联到 `LIBRARY_FILES` 本体,不需要单独定义一个新变量。）

- [ ] **Step 4: 写最小验证测试(直接调用 vendored API,不经过我们自己的封装)**

Create `tests/satellite_tests.cpp`:

```cpp
// tests/satellite_tests.cpp — 卫星层单测。Task 1 只验证 vendored SGP4 库本身正确
// (直接调用 libsgp4:: API,不经封装),Task 2 会在本文件继续追加 sat_math.h 封装层的测试。
#include <iostream>
#include <cstdlib>
#include <cmath>
#include "3rdparty/sgp4/Tle.h"
#include "3rdparty/sgp4/SGP4.h"
#include "3rdparty/sgp4/Eci.h"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)

// 官方 SGP4 验证测试集(Vallado "Revisiting Spacetrack Report #3", CelesTrak SGP4-VER.TLE
// 附带的标准回归用例,卫星 #5)。参考向量用独立的 Python 官方 sgp4 包(Brandon Rhodes 维护,
// 业界公认参考实现)在 2026-07-05 生成,与本仓库 vendored libsgp4 数值核对完全一致
// (t=0/360 分钟,位置误差 < 1e-5 km,速度误差 < 1e-5 km/s)。
static const char* kTle5Line1 = "1 00005U 58002B   00179.78495062  .00000023  00000-0  28098-4 0  4753";
static const char* kTle5Line2 = "2 00005  34.2682 348.7242 1859667 331.7664  19.3264 10.82419157413667";

int main(int, char**)
{
    using namespace libsgp4;

    // ---- vendored 库本身的正确性(直接 API,独立于我们的封装)----
    {
        Tle tle(kTle5Line1, kTle5Line2);
        SGP4 sgp4(tle);

        Eci eci0 = sgp4.FindPosition(0.0);
        Vector p0 = eci0.Position(), v0 = eci0.Velocity();
        CHECK(std::fabs(p0.x - 7022.465293) < 1e-3);
        CHECK(std::fabs(p0.y - (-1400.082968)) < 1e-3);
        CHECK(std::fabs(p0.z - 0.039952) < 1e-3);
        CHECK(std::fabs(v0.x - 1.893841) < 1e-4);
        CHECK(std::fabs(v0.y - 6.405894) < 1e-4);
        CHECK(std::fabs(v0.z - 4.534807) < 1e-4);

        Eci eci360 = sgp4.FindPosition(360.0);
        Vector p360 = eci360.Position(), v360 = eci360.Velocity();
        CHECK(std::fabs(p360.x - (-7154.031202)) < 1e-3);
        CHECK(std::fabs(p360.y - (-3783.176825)) < 1e-3);
        CHECK(std::fabs(p360.z - (-3536.194123)) < 1e-3);
        CHECK(std::fabs(v360.x - 4.741887) < 1e-4);
        CHECK(std::fabs(v360.y - (-4.151818)) < 1e-4);
        CHECK(std::fabs(v360.z - (-2.093935)) < 1e-4);

        std::cout << "[satellite_tests] vendored SGP4 library numeric verification OK" << std::endl;
    }

    std::cout << "[satellite_tests] all OK (Task 1 subset)" << std::endl;
    return 0;
}
```

- [ ] **Step 5: 注册测试目标**

在 `tests/CMakeLists.txt` 里,紧跟 `NEW_TEST(osgVerse_Test_TileOverlay tile_overlay_tests.cpp)` 那一行之后新增:

```cmake
NEW_TEST(osgVerse_Test_Satellite satellite_tests.cpp)  # P2 satellite layer: SGP4 vendor + sat_math unit tests
```

- [ ] **Step 6: 构建并验证测试通过(RED→GREEN 的 GREEN 端——本任务没有先写失败测试,因为这是纯粹验证第三方库本身,不是我们自己的行为;Task 2 起才有真正的 TDD RED 阶段)**

```bash
cmake -S . -B build/verse_core
cmake --build build/verse_core --target osgVerse_Test_Satellite --config Release 2>&1 | grep -c "error:"
```

Expected: `0`(零编译错误。若非零,先检查 Step 2 的 `Util.cc` 补丁是否正确应用,再检查 Step 3 的 CMake 列表语法)。

```bash
cmake --build build/verse_core --target install --config Release 2>&1 | grep -c "error:"
DYLD_LIBRARY_PATH=build/sdk_core/lib build/verse_core/bin/osgVerse_Test_Satellite
echo "exit=$?"
```

Expected: 打印 `[satellite_tests] vendored SGP4 library numeric verification OK` 和 `[satellite_tests] all OK (Task 1 subset)`,`exit=0`。

- [ ] **Step 7: Commit**

```bash
git add 3rdparty/sgp4 3rdparty/CMakeLists.txt tests/satellite_tests.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(earth): vendor SGP4 orbit propagator (dnwrnr/sgp4, Apache-2.0)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: `sat_math.h/.cpp` — 纯函数封装层(TLE 解析 + SGP4 包装 + 轨道/足迹几何)

**Files:**
- Create: `applications/earth_explorer/sat_math.h`
- Create: `applications/earth_explorer/sat_math.cpp`
- Modify: `tests/satellite_tests.cpp`(追加断言,不删除 Task 1 已有内容)

**Interfaces:**
- Consumes: Task 1 的 `3rdparty/sgp4/{Tle,SGP4,Eci,CoordGeodetic,Vector}.h`。
- Produces:
  - `struct earthsat::TleEntry { std::string name, line1, line2; };`
  - `std::vector<earthsat::TleEntry> earthsat::parseTleBlock(const std::string& text);`
  - `struct earthsat::PropagatedState { double latDeg, lonDeg, altKm, speedKmS; bool valid; };`
  - `earthsat::PropagatedState earthsat::propagateOne(const std::string& line1, const std::string& line2, double minutesSinceEpoch);`
  - `double earthsat::orbitalPeriodMinutes(const std::string& line1, const std::string& line2);`
  - `double earthsat::footprintRadiusKm(double altKm);`
  - `void earthsat::destinationPoint(double lat1Deg, double lon1Deg, double bearingDeg, double distanceKm, double& outLatDeg, double& outLonDeg);`
  - `std::vector<osg::Vec3d> earthsat::buildOrbitVertices(const std::string& line1, const std::string& line2, double startMinutesSinceEpoch, int n);`(ECEF 米)
  - `std::vector<osg::Vec3d> earthsat::buildFootprintVertices(double centerLatDeg, double centerLonDeg, double radiusKm, int n);`(ECEF 米,闭合环:返回 n+1 个点,首尾重合)
  - Task 3 的 `sat_data.cpp` 只依赖这一层,不直接碰 `libsgp4::*`。

- [ ] **Step 1: 写失败的测试(追加到 `tests/satellite_tests.cpp`,`main()` 的 `return 0;` 之前插入)**

```cpp
    // ---- sat_math.h 封装层(Task 2)----
    {
        using namespace earthsat;

        // parseTleBlock:CelesTrak gp.php?FORMAT=tle 的标准 3 行一组格式
        std::string block =
            "ISS (ZARYA)             \n"
            "1 25544U 98067A   26185.47597407  .00009720  00000+0  18499-3 0  9996\n"
            "2 25544  51.6303 214.5154 0006744 255.1217 104.9025 15.48889796574435\n"
            "CSS (TIANHE)            \n"
            "1 48274U 21035A   26184.22810215  .00007882  00000+0  10678-3 0  9997\n"
            "2 48274  41.4672 224.3616 0002880 262.1202  97.9309 15.57904594296214\n";
        std::vector<TleEntry> entries = parseTleBlock(block);
        CHECK(entries.size() == 2);
        CHECK(entries[0].name == "ISS (ZARYA)");
        CHECK(entries[0].line1.substr(0, 7) == "1 25544");
        CHECK(entries[1].name == "CSS (TIANHE)");

        // propagateOne:与 Task 1 同一颗验证卫星 #5,同一组参考数值,但这次经我们自己的
        // 封装(含 ToGeodetic 转换),额外验证经纬度/高度/速度/单位换算没有在封装层引入误差。
        PropagatedState s0 = propagateOne(kTle5Line1, kTle5Line2, 0.0);
        CHECK(s0.valid);
        CHECK(std::fabs(s0.latDeg - 0.00032159) < 1e-4);
        CHECK(std::fabs(s0.lonDeg - 149.95573578) < 1e-4);
        CHECK(std::fabs(s0.altKm - 782.538928) < 1e-2);
        CHECK(std::fabs(s0.speedKmS - 8.073821) < 1e-3);

        PropagatedState s360 = propagateOne(kTle5Line1, kTle5Line2, 360.0);
        CHECK(s360.valid);
        CHECK(std::fabs(s360.latDeg - (-23.70534585)) < 1e-4);
        CHECK(std::fabs(s360.lonDeg - (-81.14467633)) < 1e-4);
        CHECK(std::fabs(s360.altKm - 2456.908168) < 1e-2);

        // orbitalPeriodMinutes:1440 / meanMotion(圈/天),卫星 #5 的 TLE 平均运动算出约 133.035 分钟
        double period = orbitalPeriodMinutes(kTle5Line1, kTle5Line2);
        CHECK(std::fabs(period - 133.035339) < 1e-3);

        // footprintRadiusKm:0° 仰角地面覆盖半径,Re=6371km 球近似(与 flight_data.cpp 一致)
        CHECK(std::fabs(footprintRadiusKm(400.0) - 2200.836319) < 1e-2);
        CHECK(std::fabs(footprintRadiusKm(20200.0) - 8464.922347) < 1e-1);
        CHECK(std::fabs(footprintRadiusKm(35786.0) - 9041.019336) < 1e-1);

        // destinationPoint:赤道原点、1000km、四个方位角(球面终点公式的解析验证)
        double lat, lon;
        destinationPoint(0.0, 0.0, 0.0, 1000.0, lat, lon);
        CHECK(std::fabs(lat - 8.99321606) < 1e-5 && std::fabs(lon - 0.0) < 1e-5);
        destinationPoint(0.0, 0.0, 90.0, 1000.0, lat, lon);
        CHECK(std::fabs(lat - 0.0) < 1e-5 && std::fabs(lon - 8.99321606) < 1e-5);
        destinationPoint(0.0, 0.0, 180.0, 1000.0, lat, lon);
        CHECK(std::fabs(lat - (-8.99321606)) < 1e-5 && std::fabs(lon - 0.0) < 1e-5);
        destinationPoint(0.0, 0.0, 270.0, 1000.0, lat, lon);
        CHECK(std::fabs(lat - 0.0) < 1e-5 && std::fabs(lon - (-8.99321606)) < 1e-5);

        // buildFootprintVertices:n+1 个点闭合环,首尾重合;每个点与 destinationPoint+
        // convertLLAtoECEF 直转结果一致(捕获"接线接错"类 bug,不需要额外的外部真值表)。
        std::vector<osg::Vec3d> ring = buildFootprintVertices(0.0, 0.0, 1000.0, 4);
        CHECK(ring.size() == 5);
        double lat0, lon0; destinationPoint(0.0, 0.0, 0.0, 1000.0, lat0, lon0);
        osg::Vec3d expect0 = osgVerse::Coordinate::convertLLAtoECEF(
            osg::Vec3d(osg::DegreesToRadians(lat0), osg::DegreesToRadians(lon0), 0.0));
        CHECK((ring[0] - expect0).length() < 1.0);          // 1 米内(浮点/球近似容差)
        CHECK((ring[4] - ring[0]).length() < 1e-6);          // 首尾重合

        // buildOrbitVertices:180 个采样点覆盖一整圈周期;首点应与 propagateOne(...,0.0)
        // 经 convertLLAtoECEF 直转结果一致(同样是自洽性检查,不依赖第三个独立真值源)。
        std::vector<osg::Vec3d> orbit = buildOrbitVertices(kTle5Line1, kTle5Line2, 0.0, 180);
        CHECK(orbit.size() == 180);
        osg::Vec3d expectFirst = osgVerse::Coordinate::convertLLAtoECEF(
            osg::Vec3d(osg::DegreesToRadians(s0.latDeg), osg::DegreesToRadians(s0.lonDeg), s0.altKm * 1000.0));
        CHECK((orbit[0] - expectFirst).length() < 1.0);

        std::cout << "[satellite_tests] sat_math wrapper OK" << std::endl;
    }
```

同时在文件头部 `#include` 区新增:

```cpp
#include "../applications/earth_explorer/sat_math.h"
#include <modeling/Math.h>   // osgVerse::Coordinate::convertLLAtoECEF(测试自己直转用,与实现一致)
```

（注意:`sat_math.cpp` 的实现文件本身**不要**在测试里 `#include` 进来——不同于 `feed_layer_tests.cpp` 把实现 `.cpp` 直接 include 进测试翻译单元的写法,`sat_math.cpp` 走独立编译+链接,因为它依赖的 `3rdparty/sgp4` 符号已经在 `osgVerseReaderWriter`/`osgVerseDependency` 里,`NEW_TEST` 宏默认链接这些库,不需要 include 大法。）

- [ ] **Step 2: 运行确认失败(证明 Step 1 断言确实咬住尚未实现的接口)**

```bash
cmake --build build/verse_core --target osgVerse_Test_Satellite --config Release 2>&1 | grep -E "error:" | head -20
```

Expected: 编译报错,提示 `sat_math.h`、`earthsat` 等未定义(因为 Step 1 只写了测试,实现还不存在)。

- [ ] **Step 3: 写 `sat_math.h`**

```cpp
#ifndef EARTH_SAT_MATH_H
#define EARTH_SAT_MATH_H
// sat_math.h — 卫星层(P2)的纯函数底座:TLE 解析、SGP4 递推包装、轨道/足迹几何采样。
// 设计约定(与 geo_primitives.h 一致):纯函数,不建 viewer/GL 上下文即可调用,
// tests/satellite_tests.cpp 直接单测。场景图构建(点云/轨道线/足迹圆的 Geometry)
// 在 sat_data.cpp,不在这里。
// 单位约定:经纬度用**度**(与本仓库其余 feed 模块一致,如 FlightInfo/FeedPoint);
// 高度/半径用**千米**(TLE/SGP4 生态惯用单位,与 vendored libsgp4 API 直接对应,
// 减少转换出错概率);ECEF 输出统一用**米**(与 osgVerse::Coordinate::convertLLAtoECEF
// 的既有约定一致)。
#include <string>
#include <vector>
#include <osg/Vec3d>

namespace earthsat
{
    // 单颗卫星的原始 TLE 三行(名称+两行元素)。
    struct TleEntry { std::string name, line1, line2; };

    // 解析 CelesTrak gp.php?FORMAT=tle 的标准返回格式:每卫星 3 行(名称行右侧空格
    // 补齐到 24 字符,不含数字前缀;line1/line2 各 69 字符)。按 3 行一组切分,
    // 跳过空行;末尾不足 3 行的残余丢弃(容忍截断的网络响应)。
    std::vector<TleEntry> parseTleBlock(const std::string& text);

    // 单颗卫星在其自身 TLE 历元之后 minutesSinceEpoch 分钟时的地心状态(WGS72 球近似
    // 大地坐标,经 libsgp4::Eci::ToGeodetic 转换)。传播失败(轨道衰减/元素非法等
    // libsgp4 内部异常)时 valid=false,其余字段保持默认值,调用方应跳过该卫星
    // (不崩溃、不假装有效数据)。
    struct PropagatedState
    {
        double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
        bool valid = false;
    };
    PropagatedState propagateOne(const std::string& line1, const std::string& line2,
                                double minutesSinceEpoch);

    // 轨道周期(分钟),直接由 TLE 的平均运动(圈/天,line2 倒数第二个字段)反推:
    // 1440 / meanMotionRevPerDay。传播失败(TLE 非法)时返回 0.0。
    double orbitalPeriodMinutes(const std::string& line1, const std::string& line2);

    // 0° 仰角地面覆盖半径(km):Re*acos(Re/(Re+altKm)),Re 取平均地球半径 6371km
    // (与仓库既有 flight_data.cpp 的球近似一致,非 WGS84 椭球——够用于可视化,不用于
    // 精确覆盖预测)。altKm<=0 时返回 0。
    double footprintRadiusKm(double altKm);

    // 球面终点公式(方位角+距离,Re=6371km 球近似):从 (lat1,lon1) 出发,沿方位角
    // bearingDeg(0=正北,顺时针)走 distanceKm 后的终点经纬度。足迹圆/轨道采样的
    // 共用几何底座。
    void destinationPoint(double lat1Deg, double lon1Deg, double bearingDeg, double distanceKm,
                         double& outLatDeg, double& outLonDeg);

    // 未来一整圈轨道采样点(ECEF,米),从 tsince=startMinutesSinceEpoch 起按周期
    // 均分 n 个点(n<2 钳到 2)。传播失败的采样点会被跳过——返回数组长度可能 < n
    // (调用方应按实际返回长度渲染,不假设恰好 n 个)。
    std::vector<osg::Vec3d> buildOrbitVertices(const std::string& line1, const std::string& line2,
                                               double startMinutesSinceEpoch, int n);

    // 星下点为心的地面足迹圆(ECEF,米,贴地 altitude=0),按方位角均分 n 段闭合环:
    // 返回 n+1 个点,首尾重合(便于直接喂给 LINE_STRIP 渲染出闭合圆,不需要额外
    // 处理收尾)。n<3 钳到 3。
    std::vector<osg::Vec3d> buildFootprintVertices(double centerLatDeg, double centerLonDeg,
                                                   double radiusKm, int n);
}
#endif
```

- [ ] **Step 4: 写 `sat_math.cpp`**

```cpp
#include <cmath>
#include <sstream>
#include <modeling/Math.h>
#include "3rdparty/sgp4/Tle.h"
#include "3rdparty/sgp4/SGP4.h"
#include "3rdparty/sgp4/Eci.h"
#include "3rdparty/sgp4/SatelliteException.h"
#include "3rdparty/sgp4/DecayedException.h"
#include "sat_math.h"

namespace earthsat
{
namespace
{
    const double kEarthRadiusKm = 6371.0;   // 平均地球半径球近似,同 flight_data.cpp 的 6371000.0m
    const double kDeg2Rad = M_PI / 180.0, kRad2Deg = 180.0 / M_PI;
}

std::vector<TleEntry> parseTleBlock(const std::string& text)
{
    std::vector<TleEntry> out;
    std::istringstream in(text);
    std::string line;
    std::vector<std::string> buf;
    while (std::getline(in, line))
    {
        // 去掉行尾 \r(Windows 换行/网络响应常见)
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;
        buf.push_back(line);
        if (buf.size() == 3)
        {
            TleEntry e; e.name = buf[0]; e.line1 = buf[1]; e.line2 = buf[2];
            // 去掉名称行右侧补齐空格(CelesTrak 格式固定补到 24 字符)
            while (!e.name.empty() && e.name.back() == ' ') e.name.pop_back();
            out.push_back(e);
            buf.clear();
        }
    }
    return out;   // 末尾残余(buf.size()<3)丢弃,不报错——容忍截断的网络响应
}

PropagatedState propagateOne(const std::string& line1, const std::string& line2,
                             double minutesSinceEpoch)
{
    PropagatedState result;
    try
    {
        libsgp4::Tle tle(line1, line2);
        libsgp4::SGP4 sgp4(tle);
        libsgp4::Eci eci = sgp4.FindPosition(minutesSinceEpoch);
        libsgp4::CoordGeodetic geo = eci.ToGeodetic();
        libsgp4::Vector v = eci.Velocity();
        result.latDeg = geo.latitude * kRad2Deg;
        result.lonDeg = geo.longitude * kRad2Deg;
        result.altKm = geo.altitude;
        result.speedKmS = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        result.valid = true;
    }
    catch (const std::exception&)
    {
        // 轨道衰减/元素非法等——静默返回 invalid,调用方跳过该卫星(不崩溃)。
        // 真实 CelesTrak 数据里精选组(空间站/导航/气象)均为在轨维护卫星,预期
        // 极少触发;Starlink 全量偶有失效星体属正常现象。
    }
    return result;
}

double orbitalPeriodMinutes(const std::string& line1, const std::string& line2)
{
    try
    {
        libsgp4::Tle tle(line1, line2);
        double meanMotion = tle.MeanMotion();   // 圈/天
        if (meanMotion <= 0.0) return 0.0;
        return 1440.0 / meanMotion;
    }
    catch (const std::exception&) { return 0.0; }
}

double footprintRadiusKm(double altKm)
{
    if (altKm <= 0.0) return 0.0;
    double c = kEarthRadiusKm / (kEarthRadiusKm + altKm);
    if (c > 1.0) c = 1.0; if (c < -1.0) c = -1.0;
    return kEarthRadiusKm * std::acos(c);
}

void destinationPoint(double lat1Deg, double lon1Deg, double bearingDeg, double distanceKm,
                      double& outLatDeg, double& outLonDeg)
{
    double lat1 = lat1Deg * kDeg2Rad, lon1 = lon1Deg * kDeg2Rad, brng = bearingDeg * kDeg2Rad;
    double ang = distanceKm / kEarthRadiusKm;
    double lat2 = std::asin(std::sin(lat1) * std::cos(ang) +
                            std::cos(lat1) * std::sin(ang) * std::cos(brng));
    double lon2 = lon1 + std::atan2(std::sin(brng) * std::sin(ang) * std::cos(lat1),
                                    std::cos(ang) - std::sin(lat1) * std::sin(lat2));
    outLatDeg = lat2 * kRad2Deg;
    outLonDeg = lon2 * kRad2Deg;
}

std::vector<osg::Vec3d> buildOrbitVertices(const std::string& line1, const std::string& line2,
                                           double startMinutesSinceEpoch, int n)
{
    std::vector<osg::Vec3d> out;
    if (n < 2) n = 2;
    double period = orbitalPeriodMinutes(line1, line2);
    if (period <= 0.0) return out;   // 非法 TLE,返回空——调用方应据此跳过渲染
    double step = period / (double)(n - 1);
    out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        double t = startMinutesSinceEpoch + step * (double)i;
        PropagatedState s = propagateOne(line1, line2, t);
        if (!s.valid) continue;   // 单点传播失败——跳过,不中断整条轨道线
        out.push_back(osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            s.latDeg * kDeg2Rad, s.lonDeg * kDeg2Rad, s.altKm * 1000.0)));
    }
    return out;
}

std::vector<osg::Vec3d> buildFootprintVertices(double centerLatDeg, double centerLonDeg,
                                               double radiusKm, int n)
{
    if (n < 3) n = 3;
    std::vector<osg::Vec3d> out; out.reserve(n + 1);
    for (int i = 0; i <= n; ++i)   // <=n:最后一点方位角=360°=0°,与首点重合,闭合环
    {
        double bearing = 360.0 * (double)i / (double)n;
        double lat, lon; destinationPoint(centerLatDeg, centerLonDeg, bearing, radiusKm, lat, lon);
        out.push_back(osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            lat * kDeg2Rad, lon * kDeg2Rad, 0.0)));
    }
    return out;
}
}
```

- [ ] **Step 5: 把 `sat_math.cpp` 接入构建**

`applications/earth_explorer/CMakeLists.txt` 第 2-10 行是 `SET(EXECUTABLE_FILES ...)` 源文件列表。第 4 行现在是:

```cmake
    city_data.cpp ReaderWriterCSV.cpp precip_data.cpp flight_data.cpp tiles3d_data.cpp
```

改成:

```cmake
    city_data.cpp ReaderWriterCSV.cpp precip_data.cpp flight_data.cpp tiles3d_data.cpp sat_math.cpp
```

（本 Task 只加 `sat_math.cpp` 这一个文件;Task 3 Step 4 会再加 `sat_data.cpp`。）

- [ ] **Step 6: 构建并确认测试通过**

```bash
cmake --build build/verse_core --target install --config Release 2>&1 | grep -c "error:"
DYLD_LIBRARY_PATH=build/sdk_core/lib build/verse_core/bin/osgVerse_Test_Satellite
echo "exit=$?"
```

Expected: `0` 个编译错误;程序打印两行 `[satellite_tests] ... OK`,`exit=0`。

- [ ] **Step 7: Commit**

```bash
git add applications/earth_explorer/sat_math.h applications/earth_explorer/sat_math.cpp \
        applications/earth_explorer/CMakeLists.txt tests/satellite_tests.cpp
git commit -m "$(cat <<'EOF'
feat(earth): sat_math pure functions — TLE parse, SGP4 wrapper, orbit/footprint geometry

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: `sat_data.h/.cpp` 骨架 — 数据抓取/缓存 + 点云渲染 + 图层开关(暂不含拾取/轨道线)

**Files:**
- Create: `applications/earth_explorer/sat_data.h`
- Create: `applications/earth_explorer/sat_data.cpp`
- Modify: `applications/earth_explorer/CMakeLists.txt`(加入 `sat_data.cpp`)
- Create: `applications/earth_explorer/test/sat_fixture_precise.tle`(3 颗测试卫星,3 行一组)
- Create: `applications/earth_explorer/test/sat_fixture_starlink.tle`(2 颗测试卫星)

**Interfaces:**
- Consumes: Task 2 的 `earthsat::parseTleBlock`/`propagateOne`;`3rdparty/libhv/all/client/requests.h`(同 `flight_data.cpp`);`osgVerse::Coordinate::convertLLAtoECEF`。
- Produces:
  - `enum class SatCategory { Station, Navigation, Weather, Starlink };`
  - `struct SatelliteInfo { std::string name; int noradId; SatCategory category; double latDeg,lonDeg,altKm,speedKmS; bool valid; };`
  - `class SatelliteLayer { setCategoryEnabled(SatCategory,bool); isCategoryEnabled(SatCategory) const; selectByNoradId(int); clearSelected(); getSelected() const; summaryJson() const; };`(本任务先实现 `setCategoryEnabled`/`isCategoryEnabled`；`selectByNoradId`/`clearSelected`/`getSelected` 在本任务留空实现——返回默认值/no-op，Task 4 补真正逻辑，接口签名现在就定好供 Task 4/5/6 引用)
  - `osg::Node* configureSatelliteLayer(osgViewer::View& viewer, osg::Node* earthRoot, const std::string& mainFolder, SatelliteLayer** outLayer);`
  - EARTH_SATS / EARTH_SATS_FILE / EARTH_STARLINK / EARTH_STARLINK_FILE 四个 env 钩子（在 `sat_data.cpp` 内部读取，语义同 flight/precip：`_FILE` 绕过网络用本地 fixture；无 `_FILE` 后缀的裸开关目前**不在本任务使用**——留给 Task 6 在 `earth_main.cpp` 里通过 `EARTH_SATS`/`EARTH_STARLINK`=1 之类的钩子強制打开对应 `setCategoryEnabled`）。

**背景**:本任务先把"抓取→解析→渲染点云→开关"这条主链路跑通并可离屏验证，拾取/轨道线/详情卡留到 Task 4/5，避免单任务过大、评审颗粒度失控。

- [ ] **Step 1: 写 `sat_data.h`**

```cpp
#ifndef EARTH_SAT_DATA_H
#define EARTH_SAT_DATA_H
#include <string>
#include <osg/Node>
#include <osgViewer/View>

enum class SatCategory { Station, Navigation, Weather, Starlink };

struct SatelliteInfo
{
    std::string name;
    int noradId = 0;
    SatCategory category = SatCategory::Station;
    double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
    bool valid = false;
};

class SatelliteLayer
{
public:
    virtual ~SatelliteLayer() {}
    // 空间站/导航星座/气象卫星/Starlink 四个独立开关(spec 要求,非二元"精选组+starlink")。
    virtual void setCategoryEnabled(SatCategory cat, bool on) = 0;
    virtual bool isCategoryEnabled(SatCategory cat) const = 0;
    // 点击拾取或程序化选中(Task 4 实现;本任务先占位签名)。
    virtual void selectByNoradId(int noradId) = 0;
    virtual void clearSelected() = 0;
    virtual SatelliteInfo getSelected() const = 0;
    // 供未来 P4 AI 工具复用的汇总统计,v1 先占位最小实现(同 flight_data.cpp 的先例)。
    virtual std::string summaryJson() const = 0;
};

extern osg::Node* configureSatelliteLayer(osgViewer::View& viewer, osg::Node* earthRoot,
                                          const std::string& mainFolder, SatelliteLayer** outLayer);
#endif
```

- [ ] **Step 2: 创建 fixture 文件**

Create `applications/earth_explorer/test/sat_fixture_precise.tle`:

```
ISS (ZARYA)             
1 25544U 98067A   26185.47597407  .00009720  00000+0  18499-3 0  9996
2 25544  51.6303 214.5154 0006744 255.1217 104.9025 15.48889796574435
CSS (TIANHE)            
1 48274U 21035A   26184.22810215  .00007882  00000+0  10678-3 0  9997
2 48274  41.4672 224.3616 0002880 262.1202  97.9309 15.57904594296214
NOAA 15                 
1 25338U 98030A   26185.12345678  .00000123  00000-0  12345-3 0  9990
2 25338  98.7210 123.4567 0011234  87.6543 272.5432 14.25874123456789
```

Create `applications/earth_explorer/test/sat_fixture_starlink.tle`:

```
STARLINK-1007           
1 44713U 19074A   26185.50000000  .00002000  00000-0  15000-3 0  9991
2 44713  53.0534  10.1234 0001234  90.1234 269.8765 15.06400000123456
STARLINK-1008           
1 44714U 19074B   26185.50000000  .00002100  00000-0  15100-3 0  9992
2 44714  53.0534  20.1234 0001234  91.1234 268.8765 15.06400000123457
```

（这些行的校验位/字段数值不追求"当前真实在轨",只需满足 TLE 固定列格式、能被 `libsgp4::Tle` 解析、`SGP4::FindPosition` 不抛异常即可——ISS/CSS(天宫)两条沿用 Task 2 测试里已验证过能正常解析的真实历史 TLE。）

- [ ] **Step 3: 写 `sat_data.cpp`(第一版:抓取+缓存+点云渲染+开关,无拾取/轨道线)**

```cpp
#include <osg/Geometry>
#include <osg/Geode>
#include <osg/Point>
#include <osg/NodeCallback>
#include <osgDB/FileUtils>
#include <OpenThreads/Thread>
#include <OpenThreads/Mutex>
#include <modeling/Math.h>
#include <pipeline/Pipeline.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>
#include <vector>
#include "3rdparty/libhv/all/client/requests.h"
#include "sat_math.h"
#include "sat_data.h"

namespace
{
    static const double kCacheTtlSeconds = 86400.0;   // 24h,遵守 CelesTrak 使用建议

    struct Satellite
    {
        std::string name, line1, line2;
        int noradId = 0;
        SatCategory category = SatCategory::Station;
        double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
        osg::Vec3d ecef;
    };

    int noradFromLine1(const std::string& line1)
    {
        if (line1.size() < 7) return 0;
        return atoi(line1.substr(2, 5).c_str());
    }

    // 磁盘缓存新鲜度:sidecar 时间戳文件(纯文本 epoch 秒),避免依赖平台相关的文件
    // mtime API。纯函数,不碰文件系统之外的状态,可单独验证逻辑(此处内联,未单测——
    // 逻辑足够简单,行为由 Step 5 的 E2E fixture 路径间接覆盖)。
    bool cacheIsFresh(double cachedEpoch, double nowEpoch) { return (nowEpoch - cachedEpoch) < kCacheTtlSeconds; }

    std::string readWholeFile(const std::string& path)
    {
        std::ifstream in(path); if (!in) return "";
        std::stringstream ss; ss << in.rdbuf(); return ss.str();
    }

    // 磁盘缓存 + 网络抓取一个 CelesTrak GROUP(不含 fixture 分支,调用方负责 fixture 短路)。
    std::string fetchGroupTextNetwork(const std::string& celestrakGroup, const std::string& cacheDir)
    {
        std::string cacheFile = cacheDir + "/" + celestrakGroup + ".tle";
        std::string tsFile = cacheFile + ".ts";
        double now = (double)time(nullptr);
        {
            std::ifstream tsIn(tsFile);
            double cachedEpoch = -1.0;
            if (tsIn && (tsIn >> cachedEpoch) && cacheIsFresh(cachedEpoch, now))
            {
                std::string cached = readWholeFile(cacheFile);
                if (!cached.empty()) return cached;
            }
        }
        std::string url = "https://celestrak.org/NORAD/elements/gp.php?GROUP=" + celestrakGroup + "&FORMAT=tle";
        requests::Request req(new HttpRequest);
        req->method = HTTP_GET; req->url = url; req->timeout = 20;
        requests::Response resp = requests::request(req);
        if (!resp || resp->status_code != 200)
        {
            std::cout << "[Sat] network fetch failed group=" << celestrakGroup
                       << " status=" << (resp ? (int)resp->status_code : -1) << "\n";
            std::string stale = readWholeFile(cacheFile);   // 网络失败兜底:过期缓存也比没有强
            return stale;
        }
        osgDB::makeDirectory(cacheDir);
        std::ofstream out(cacheFile); if (out) out << resp->body;
        std::ofstream tsOut(tsFile); if (tsOut) tsOut << now;
        return resp->body;
    }

    struct GroupSpec { const char* celestrakGroup; SatCategory category; };
    const GroupSpec kPreciseGroups[] = {
        { "stations",    SatCategory::Station },
        { "gps-ops",     SatCategory::Navigation },
        { "beidou",      SatCategory::Navigation },
        { "galileo",     SatCategory::Navigation },
        { "glonass-ops", SatCategory::Navigation },
        { "weather",     SatCategory::Weather },
    };

    std::vector<Satellite> entriesToSatellites(const std::vector<earthsat::TleEntry>& entries, SatCategory cat)
    {
        std::vector<Satellite> out; out.reserve(entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
        {
            Satellite s; s.name = entries[i].name; s.line1 = entries[i].line1; s.line2 = entries[i].line2;
            s.category = cat; s.noradId = noradFromLine1(entries[i].line1);
            out.push_back(s);
        }
        return out;
    }

    // 精选组(空间站+导航+气象,6 个 CelesTrak GROUP 合并):EARTH_SATS_FILE 设置时整体
    // 短路(全部标 Station 类目,测试用,不区分子类目——真实网络路径每组各自打对应类目)。
    std::vector<Satellite> fetchPreciseSatellites(const std::string& cacheDir)
    {
        const char* fixtureFile = getenv("EARTH_SATS_FILE");
        if (fixtureFile && *fixtureFile)
        {
            std::string text = readWholeFile(fixtureFile);
            if (text.empty()) { std::cout << "[Sat] EARTH_SATS_FILE open failed: " << fixtureFile << "\n"; return {}; }
            std::vector<Satellite> out = entriesToSatellites(earthsat::parseTleBlock(text), SatCategory::Station);
            std::cout << "[Sat] Parsed " << out.size() << " satellites (precise, fixture)\n";
            return out;
        }
        std::vector<Satellite> all;
        for (size_t i = 0; i < sizeof(kPreciseGroups) / sizeof(kPreciseGroups[0]); ++i)
        {
            std::string text = fetchGroupTextNetwork(kPreciseGroups[i].celestrakGroup, cacheDir);
            std::vector<Satellite> group = entriesToSatellites(earthsat::parseTleBlock(text), kPreciseGroups[i].category);
            all.insert(all.end(), group.begin(), group.end());
        }
        std::cout << "[Sat] Parsed " << all.size() << " satellites (precise, network, "
                   << (sizeof(kPreciseGroups) / sizeof(kPreciseGroups[0])) << " groups)\n";
        return all;
    }

    std::vector<Satellite> fetchStarlinkSatellites(const std::string& cacheDir)
    {
        const char* fixtureFile = getenv("EARTH_STARLINK_FILE");
        std::string text;
        bool fromFixture = (fixtureFile && *fixtureFile);
        if (fromFixture)
        {
            text = readWholeFile(fixtureFile);
            if (text.empty()) { std::cout << "[Sat] EARTH_STARLINK_FILE open failed: " << fixtureFile << "\n"; return {}; }
        }
        else text = fetchGroupTextNetwork("starlink", cacheDir);
        std::vector<Satellite> out = entriesToSatellites(earthsat::parseTleBlock(text), SatCategory::Starlink);
        std::cout << "[Sat] Parsed " << out.size() << " satellites (starlink, "
                   << (fromFixture ? "fixture" : "network") << ")\n";
        return out;
    }

    // 点精灵着色器:同 feed_layer.cpp 的"软圆点"款式(卫星不需要航向旋转,比 flight
    // 箭头简单)。逐模块各自持有一份小着色器是本仓库既有惯例(flight/feed 均如此),
    // 不共享全局 Program(避免 P1 已踩过的全局 static 共享 Program 导致瓦片染色的坑)。
    const char* satVertCode = {
        "VERSE_VS_OUT vec4 pointColor;\n"
        "void main() {\n"
        "    pointColor = osg_Color;\n"
        "    gl_PointSize = osg_MultiTexCoord0.x;\n"
        "    gl_Position = VERSE_MATRIX_MVP * osg_Vertex;\n"
        "}\n"
    };
    const char* satFragCode = {
        "VERSE_FS_IN vec4 pointColor;\n"
        "#ifdef VERSE_GLES3\n"
        "layout(location = 0) VERSE_FS_OUT vec4 fragColor;\n"
        "layout(location = 1) VERSE_FS_OUT vec4 fragOrigin;\n"
        "#endif\n"
        "void main() {\n"
        "    vec2 d = gl_PointCoord - vec2(0.5);\n"
        "    float r2 = dot(d, d);\n"
        "    if (r2 > 0.25) discard;\n"
        "    float edge = 1.0 - smoothstep(0.16, 0.25, r2);\n"
        "    vec3 rgb = mix(pointColor.rgb * 0.7, pointColor.rgb, edge);\n"
        "#ifdef VERSE_GLES3\n"
        "    fragColor = vec4(rgb, 1.0); fragOrigin = vec4(1.0);\n"
        "#else\n"
        "    gl_FragData[0] = vec4(rgb, 1.0); gl_FragData[1] = vec4(1.0);\n"
        "#endif\n"
        "}\n"
    };
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif

    osg::Vec4 categoryColor(SatCategory cat)
    {
        switch (cat)
        {
        case SatCategory::Station:    return osg::Vec4(1.00f, 0.85f, 0.20f, 1.0f);  // 空间站:金黄
        case SatCategory::Navigation: return osg::Vec4(0.35f, 0.95f, 0.55f, 1.0f);  // 导航:绿
        case SatCategory::Weather:    return osg::Vec4(0.55f, 0.75f, 1.00f, 1.0f);  // 气象:蓝
        default:                     return osg::Vec4(0.55f, 0.55f, 0.60f, 1.0f);  // Starlink:暗灰壳层
        }
    }
    static const float kSatSizePx = 9.0f;
    static const float kStarlinkSizePx = 3.0f;

    osg::Geode* buildSatGeode(const std::vector<Satellite>& sats, osg::StateSet* sharedSS, float sizePx)
    {
        osg::ref_ptr<osg::Geometry> geom = new osg::Geometry;
        osg::ref_ptr<osg::Vec3Array> verts = new osg::Vec3Array;
        osg::ref_ptr<osg::Vec4Array> colors = new osg::Vec4Array;
        osg::ref_ptr<osg::Vec2Array> sizes = new osg::Vec2Array;   // 同 feed_layer.cpp:Vec2,只用 .x
        for (size_t i = 0; i < sats.size(); ++i)
        {
            verts->push_back(sats[i].ecef);
            colors->push_back(categoryColor(sats[i].category));
            sizes->push_back(osg::Vec2(sizePx, 0.0f));
        }
        geom->setVertexArray(verts.get());
        geom->setColorArray(colors.get(), osg::Array::BIND_PER_VERTEX);
        geom->setTexCoordArray(0, sizes.get());
        geom->addPrimitiveSet(new osg::DrawArrays(GL_POINTS, 0, (GLsizei)verts->size()));
        geom->setUseDisplayList(false); geom->setUseVertexBufferObjects(true);
        geom->setCullingActive(false);
        osg::ref_ptr<osg::Geode> geode = new osg::Geode;
        geode->addDrawable(geom.get()); geode->setStateSet(sharedSS);
        return geode.release();
    }

    class SatelliteLayerImpl;

    class FetchThread : public OpenThreads::Thread
    {
    public:
        FetchThread(SatelliteLayerImpl* o) : _owner(o), _done(false) {}
        virtual int cancel() { _done = true; return OpenThreads::Thread::cancel(); }
        virtual void run();
    protected:
        SatelliteLayerImpl* _owner; bool _done;
    };

    class SyncCallback : public osg::NodeCallback
    {
    public:
        SyncCallback(SatelliteLayerImpl* o) : _owner(o) {}
        virtual void operator()(osg::Node* node, osg::NodeVisitor* nv);
    protected:
        SatelliteLayerImpl* _owner;
    };

    class SatelliteLayerImpl : public osg::Referenced, public SatelliteLayer
    {
    public:
        SatelliteLayerImpl(const std::string& mainFolder)
            : _cacheDir(mainFolder + "/sat_cache"), _root(0),
              _catStation(false), _catNav(false), _catWeather(false), _catStarlink(false),
              _preciseFetchTriggered(false), _starlinkFetchTriggered(false),
              _preciseDirty(false), _starlinkDirty(false), _rebuildNeeded(false),
              _thread(nullptr) {}

        virtual void setCategoryEnabled(SatCategory cat, bool on)
        {
            bool changed = false;
            switch (cat)
            {
            case SatCategory::Station:    changed = (_catStation != on); _catStation = on; break;
            case SatCategory::Navigation: changed = (_catNav != on);     _catNav = on; break;
            case SatCategory::Weather:    changed = (_catWeather != on); _catWeather = on; break;
            case SatCategory::Starlink:   changed = (_catStarlink != on); _catStarlink = on; break;
            }
            if (!changed) return;
            if ((cat == SatCategory::Station || cat == SatCategory::Navigation || cat == SatCategory::Weather) && on)
                _preciseFetchTriggered = true;   // 懒加载:任一精选类目首次开启才联网
            if (cat == SatCategory::Starlink && on)
                _starlinkFetchTriggered = true;  // Starlink 独立懒加载(同 hk3d 模式)
            _rebuildNeeded = true;
            if (_starlinkRoot.valid()) _starlinkRoot->setNodeMask(_catStarlink ? ~0u : 0u);
        }
        virtual bool isCategoryEnabled(SatCategory cat) const
        {
            switch (cat)
            {
            case SatCategory::Station:    return _catStation;
            case SatCategory::Navigation: return _catNav;
            case SatCategory::Weather:    return _catWeather;
            default:                      return _catStarlink;
            }
        }
        // Task 4 补真正实现;本任务先占位(no-op/默认值),不影响本任务的渲染/开关闭环。
        virtual void selectByNoradId(int) {}
        virtual void clearSelected() {}
        virtual SatelliteInfo getSelected() const { return SatelliteInfo(); }
        virtual std::string summaryJson() const { return "{\"count\":0}"; }

        bool preciseFetchTriggered() const { return _preciseFetchTriggered; }
        bool starlinkFetchTriggered() const { return _starlinkFetchTriggered; }
        const std::string& cacheDir() const { return _cacheDir; }

        void postPreciseSnapshot(const std::vector<Satellite>& sats)
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); _pendingPrecise = sats; _preciseDirty = true; }
        void postStarlinkSnapshot(const std::vector<Satellite>& sats)
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); _pendingStarlink = sats; _starlinkDirty = true; }

        // 主线程(update 遍历)调用:有新抓取数据或类目开关变了就重建几何。
        void syncIfDirty()
        {
            bool needRebuild = _rebuildNeeded;
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
                if (_preciseDirty) { _allPrecise = _pendingPrecise; _preciseDirty = false; needRebuild = true; }
                if (_starlinkDirty) { _allStarlink = _pendingStarlink; _starlinkDirty = false; needRebuild = true; }
            }
            if (!needRebuild) return;
            _rebuildNeeded = false;

            std::vector<Satellite> visible;
            for (size_t i = 0; i < _allPrecise.size(); ++i)
            {
                const Satellite& s = _allPrecise[i];
                if ((s.category == SatCategory::Station && _catStation) ||
                    (s.category == SatCategory::Navigation && _catNav) ||
                    (s.category == SatCategory::Weather && _catWeather))
                    visible.push_back(s);
            }
            if (_preciseGeode.valid()) _preciseRoot->removeChild(_preciseGeode.get());
            _preciseGeode = buildSatGeode(visible, _ss.get(), kSatSizePx);
            _preciseRoot->addChild(_preciseGeode.get());

            if (_starlinkGeode.valid()) _starlinkRoot->removeChild(_starlinkGeode.get());
            _starlinkGeode = buildSatGeode(_allStarlink, _ss.get(), kStarlinkSizePx);
            _starlinkRoot->addChild(_starlinkGeode.get());
        }

        osg::Group* buildScene()
        {
            _root = new osg::Group; _root->setName("SatelliteLayer");
            osg::Shader* vs = new osg::Shader(osg::Shader::VERTEX, satVertCode);
            osg::Shader* fs = new osg::Shader(osg::Shader::FRAGMENT, satFragCode);
            vs->setName("Sat_VS"); fs->setName("Sat_FS");
            osgVerse::Pipeline::createShaderDefinitions(vs, 100, 130);
            osgVerse::Pipeline::createShaderDefinitions(fs, 100, 130);
            osg::ref_ptr<osg::Program> prog = new osg::Program;
            prog->addShader(vs); prog->addShader(fs);
            _ss = new osg::StateSet;
            _ss->setAttributeAndModes(prog.get(), osg::StateAttribute::ON);
            _ss->setMode(GL_PROGRAM_POINT_SIZE, osg::StateAttribute::ON);
            _ss->setRenderBinDetails(11, "RenderBin");

            _preciseRoot = new osg::Group; _preciseRoot->setName("SatPrecise");
            _starlinkRoot = new osg::Group; _starlinkRoot->setName("SatStarlink");
            _starlinkRoot->setNodeMask(0);   // 默认关(Starlink 不在"全部"预设里)
            _root->addChild(_preciseRoot.get());
            _root->addChild(_starlinkRoot.get());
            _root->addUpdateCallback(new SyncCallback(this));
            return _root;
        }

    protected:
        virtual ~SatelliteLayerImpl()
        { if (_thread) { _thread->cancel(); _thread->join(); delete _thread; _thread = nullptr; } }

        std::string _cacheDir;
        osg::Group* _root;   // 裸指针:由 UserData 拥有,同 flight_data.cpp 约定(无引用环)
        osg::ref_ptr<osg::Group> _preciseRoot, _starlinkRoot;
        osg::ref_ptr<osg::Geode> _preciseGeode, _starlinkGeode;
        osg::ref_ptr<osg::StateSet> _ss;
        bool _catStation, _catNav, _catWeather, _catStarlink;
        bool _preciseFetchTriggered, _starlinkFetchTriggered;
        std::vector<Satellite> _allPrecise, _allStarlink, _pendingPrecise, _pendingStarlink;
        OpenThreads::Mutex _mutex;
        bool _preciseDirty, _starlinkDirty, _rebuildNeeded;
    public:
        FetchThread* _thread;
    };

    void SyncCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    { _owner->syncIfDirty(); traverse(node, nv); }

    void FetchThread::run()
    {
        bool preciseFetchedOnce = false, starlinkFetchedOnce = false;
        while (!_done)
        {
            if (_owner->preciseFetchTriggered() && !preciseFetchedOnce)
            {
                _owner->postPreciseSnapshot(fetchPreciseSatellites(_owner->cacheDir()));
                preciseFetchedOnce = true;
            }
            if (_owner->starlinkFetchTriggered() && !starlinkFetchedOnce)
            {
                _owner->postStarlinkSnapshot(fetchStarlinkSatellites(_owner->cacheDir()));
                starlinkFetchedOnce = true;
            }
            OpenThreads::Thread::microSleep(200000);  // 200ms 轮询(启动懒加载/一次性抓取足够)
        }
        _done = true;
    }
}

osg::Node* configureSatelliteLayer(osgViewer::View& /*viewer*/, osg::Node* /*earthRoot*/,
                                   const std::string& mainFolder, SatelliteLayer** outLayer)
{
    osg::ref_ptr<SatelliteLayerImpl> impl = new SatelliteLayerImpl(mainFolder);
    osg::Group* root = impl->buildScene();
    root->setUserData(impl.get());
    if (outLayer) *outLayer = impl.get();
    impl->_thread = new FetchThread(impl.get());
    impl->_thread->startThread();
    return root;
}
```

**已知留白(本任务范围内合理简化,后续任务处理)**:
- 位置只在后台线程首次抓取时算一次,**不做逐帧 SGP4 重新递推**(spec 要求的"精选组每 1s / Starlink 每 5s 重新递推+主线程线性外推"留到 Task 4,与拾取/选中逻辑一起实现——本任务先把静态一次性渲染链路打通更容易独立评审)。
- Task 4 会在 `FetchThread::run()` 里加周期性重新传播的分支,并给 `Satellite` 加 `ecefVelocity`/`lastUpdateTime` 字段用于主线程逐帧外推。

- [ ] **Step 4: 加入构建**

`applications/earth_explorer/CMakeLists.txt` 第 4 行(Task 2 Step 5 已经改过一次)现在是:

```cmake
    city_data.cpp ReaderWriterCSV.cpp precip_data.cpp flight_data.cpp tiles3d_data.cpp sat_math.cpp
```

改成:

```cmake
    city_data.cpp ReaderWriterCSV.cpp precip_data.cpp flight_data.cpp tiles3d_data.cpp sat_math.cpp sat_data.cpp
```

- [ ] **Step 5: 离屏 E2E 验证**

```bash
cmake --build build/verse_core --target install --config Release 2>&1 | grep -c "error:"
```

Expected: `0`。

```bash
cd build/sdk_core/bin
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=300 EARTH_SUN_TO_CAMERA=1 EARTH_SATS=1 \
  EARTH_SATS_FILE=/Users/USER/osgverse/applications/earth_explorer/test/sat_fixture_precise.tle \
  ./osgVerse_EarthExplorer 2>&1 | grep -a "Parsed.*satellites"
```

Expected(此时 `EARTH_SATS` 钩子还未在 `earth_main.cpp` 接入,预期看不到任何 `[Sat] Parsed` 输出——这是正常的,Task 3 只交付了 `sat_data.cpp` 本身,还没有从 `earth_main.cpp` 调用 `configureSatelliteLayer`;这一步先确认离屏截图流程本身没有因为新增编译单元而报错/崩溃)：

```bash
ls -la /tmp/earth_capture_0.png
```

Expected: 文件存在(说明整个 app 加了新模块后依然能正常启动/离屏截图,新代码目前只是"编译进去但未被调用",这一步是纯粹的"没有破坏现有功能"回归确认——真正验证 `configureSatelliteLayer` 被调用后的效果留到 Task 6 接入 `earth_main.cpp` 之后)。

- [ ] **Step 6: Commit**

```bash
git add applications/earth_explorer/sat_data.h applications/earth_explorer/sat_data.cpp \
        applications/earth_explorer/CMakeLists.txt \
        applications/earth_explorer/test/sat_fixture_precise.tle \
        applications/earth_explorer/test/sat_fixture_starlink.tle
git commit -m "$(cat <<'EOF'
feat(earth): satellite layer skeleton — TLE fetch/cache + category toggles + point-cloud render

Not yet wired into earth_main.cpp (Task 6); picking/orbit line in Task 4/5.

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: 逐帧递推 + 拾取 + 轨道线 + 足迹圆 + ISS/天宫默认常显

**Files:**
- Modify: `applications/earth_explorer/sat_data.h`
- Modify: `applications/earth_explorer/sat_data.cpp`

**Interfaces:**
- Consumes: Task 2 的 `earthsat::buildOrbitVertices`/`buildFootprintVertices`/`propagateOne`;`applications/earth_explorer/geo_primitives.h` 的 `earthgeo::buildPolylineGeometry(const std::vector<osg::Vec3d>& verts, const osg::Vec4& color, osg::StateSet* externalSS = NULL)`。
- Produces: `SatelliteLayer::selectByNoradId`/`clearSelected`/`getSelected` 真正实现(不再是 Task 3 的占位);`SatelliteInfo` 的 `valid=true` 路径。

**背景**:Task 3 只做了"一次性抓取→静态渲染",本任务补上 spec 要求的三件事:(a) 精选组每 1s / Starlink 每 5s 重新 SGP4 递推,主线程逐帧线性外推(在 ECEF 空间做位置差值外推,而非 `flight_data.cpp` 的经纬度+航向外推——原因:卫星可能跨极地轨道,经纬度/航向的 2D 外推在这种情况下会失真,ECEF 三维线性外推对任意轨道倾角都成立,是更简单也更稳的等价实现,同属"线性外推"范畴);(b) 点击拾取(镜像 `flight_data.cpp` 的 `pickAt`);(c) 选中后画轨道线+足迹圆;(d) ISS(25544)/天宫核心舱(48274)默认常显轨道线(不需要点选)。

- [ ] **Step 1: 修改 `sat_data.h`**——本任务不改公开接口签名(Task 3 已定好),跳过此步骤的代码变更,直接进 Step 2。

- [ ] **Step 2: 扩展 `Satellite` 结构体 + 加入外推所需字段**

在 `sat_data.cpp` 的匿名命名空间里,修改 `Satellite` 结构体:

```cpp
    struct Satellite
    {
        std::string name, line1, line2;
        int noradId = 0;
        SatCategory category = SatCategory::Station;
        double latDeg = 0.0, lonDeg = 0.0, altKm = 0.0, speedKmS = 0.0;
        osg::Vec3d ecef;
        osg::Vec3d ecefVelocity;   // 米/秒,由相邻两次后台传播的 ECEF 差值/时间差算出
        double lastUpdateRefTime = 0.0;   // 该卫星最近一次被传播时的 viewer referenceTime
    };
```

- [ ] **Step 3: 后台线程加周期性重新传播**

修改 `FetchThread::run()`,在原有的一次性抓取逻辑**之后**加入周期性重新传播(不改抓取部分):

```cpp
    // 传播一批卫星的当前 ECI/地心状态,写回 lat/lon/alt/speed/ecef,并用与上一次
    // ecef 的差值算出 ecefVelocity(米/秒)供主线程逐帧外推。now 是 DateTime::Now()
    // 与各自 TLE 历元的分钟差——每颗卫星历元不同,分别计算。
    void repropagate(std::vector<Satellite>& sats, double prevUpdateEpochSeconds, double nowEpochSeconds)
    {
        double dt = nowEpochSeconds - prevUpdateEpochSeconds; if (dt <= 0.0) dt = 1.0;
        for (size_t i = 0; i < sats.size(); ++i)
        {
            Satellite& s = sats[i];
            libsgp4::DateTime nowDt = libsgp4::DateTime::Now();
            double tsince = 0.0;
            try
            {
                libsgp4::Tle tle(s.line1, s.line2);
                tsince = (nowDt - tle.Epoch()).TotalMinutes();
            }
            catch (const std::exception&) { continue; }
            earthsat::PropagatedState st = earthsat::propagateOne(s.line1, s.line2, tsince);
            if (!st.valid) continue;
            osg::Vec3d newEcef = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
                st.latDeg * (M_PI / 180.0), st.lonDeg * (M_PI / 180.0), st.altKm * 1000.0));
            s.ecefVelocity = (newEcef - s.ecef) / dt;
            s.ecef = newEcef; s.latDeg = st.latDeg; s.lonDeg = st.lonDeg;
            s.altKm = st.altKm; s.speedKmS = st.speedKmS;
        }
    }
```

（`libsgp4::DateTime`/`libsgp4::Tle` 需要 `#include "3rdparty/sgp4/DateTime.h"` 和 `#include "3rdparty/sgp4/Tle.h"`——加进文件头部 `#include` 区。`osgVerse::Coordinate` 已经通过 `<modeling/Math.h>` 引入。）

在 `FetchThread::run()` 内,把原来的:

```cpp
            OpenThreads::Thread::microSleep(200000);  // 200ms 轮询(启动懒加载/一次性抓取足够)
```

替换为(改成有周期性重新传播的完整循环体,原有抓取判断保留在前面不动):

```cpp
            static double lastPreciseUpdate = 0.0, lastStarlinkUpdate = 0.0;
            double now = (double)time(nullptr);
            if (preciseFetchedOnce && (now - lastPreciseUpdate) >= 1.0)   // 精选组每 1s
            {
                std::vector<Satellite> snapshot = _owner->currentPrecise();
                repropagate(snapshot, lastPreciseUpdate > 0.0 ? lastPreciseUpdate : now, now);
                _owner->postPreciseSnapshot(snapshot);
                lastPreciseUpdate = now;
            }
            if (starlinkFetchedOnce && (now - lastStarlinkUpdate) >= 5.0)   // Starlink 每 5s
            {
                std::vector<Satellite> snapshot = _owner->currentStarlink();
                repropagate(snapshot, lastStarlinkUpdate > 0.0 ? lastStarlinkUpdate : now, now);
                _owner->postStarlinkSnapshot(snapshot);
                lastStarlinkUpdate = now;
            }
            OpenThreads::Thread::microSleep(200000);
```

（`static` 局部变量在这里可以接受,因为 `FetchThread::run()` 是每个实例独立的单一后台线程方法,不存在多实例并发共享该状态的场景——本模块每个 `SatelliteLayerImpl` 只 `new` 一个 `FetchThread`。）

在 `SatelliteLayerImpl` 里新增两个供后台线程读取"当前列表快照"的辅助方法(加锁):

```cpp
        std::vector<Satellite> currentPrecise()
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); return _allPrecise; }
        std::vector<Satellite> currentStarlink()
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex); return _allStarlink; }
```

（注意:这里读的是 `_allPrecise`/`_allStarlink`——即 Task 3 里"已经被主线程 `syncIfDirty` 消费过的当前状态",而不是 `_pendingXxx`。后台线程与主线程都可能碰 `_allPrecise`,但后台线程只在这两个新增的 `currentXxx()` 加锁读方法里读它,主线程只在 `syncIfDirty()` 加锁写它——同一把 `_mutex`,不会数据竞争。）

- [ ] **Step 4: 主线程逐帧外推**

在 `SatelliteLayerImpl` 加一个 `interpolate(double refTime)` 方法(镜像 `flight_data.cpp` 的同名方法,但操作 ECEF 而非经纬度):

```cpp
        void interpolate(double refTime)
        {
            interpolateOne(_preciseGeode.get(), _visiblePrecise, refTime);
            interpolateOne(_starlinkGeode.get(), _allStarlink, refTime);
        }
    protected:
        void interpolateOne(osg::Geode* geode, std::vector<Satellite>& sats, double refTime)
        {
            if (!geode || sats.empty()) return;
            osg::Geometry* g = geode->getDrawable(0)->asGeometry(); if (!g) return;
            osg::Vec3Array* va = static_cast<osg::Vec3Array*>(g->getVertexArray()); if (!va) return;
            for (size_t i = 0; i < sats.size() && i < va->size(); ++i)
            {
                double elapsed = refTime - sats[i].lastUpdateRefTime; if (elapsed < 0.0) elapsed = 0.0;
                (*va)[i] = sats[i].ecef + sats[i].ecefVelocity * elapsed;
            }
            va->dirty(); g->dirtyBound();
        }
    public:
```

（`_visiblePrecise` 是新增的成员——把 Task 3 `syncIfDirty()` 里的局部变量 `visible` 提升为成员变量,因为 `interpolate()` 也需要按同一份"当前可见子集"外推、且要和 `buildSatGeode` 用的是同一批卫星、同一个下标顺序,否则外推的位置和渲染的顶点对不上号。`lastUpdateRefTime` 的赋值时机也很关键:`repropagate()` 跑在后台线程,只有 wall-clock(`time(nullptr)`)可用,没有 viewer 的 `referenceTime`;真正"外推基准时刻"必须是主线程消费这份快照的那一帧的 `referenceTime`(同 `flight_data.cpp` 的 `_t0 = refTime` 手法),所以要在 `syncIfDirty()` 里统一补写,不能指望后台线程算对。

把 Task 3 的 `syncIfDirty()` **整个方法体**替换成:

```cpp
        // 主线程(update 遍历)调用:有新抓取数据或类目开关变了就重建几何。refTime 是
        // 本帧的 viewer referenceTime,用作 lastUpdateRefTime 基准(供 interpolate() 外推)。
        void syncIfDirty(double refTime)
        {
            bool needRebuild = _rebuildNeeded;
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_mutex);
                // lastUpdateRefTime 只在"这份快照刚到达"的那一帧重置,且只重置这份快照里的
                // 卫星——不能放在 if 外面对全体无条件重置(曾经这样写过,是真实 bug:
                // SyncCallback 同一帧内先 syncIfDirty() 后 interpolate(),两次都传同一个
                // refTime,若每帧都无条件重置,interpolateOne() 算出的 elapsed 永远 ≈0,
                // ecefVelocity*elapsed 外推项形同虚设,卫星只会在每次重新传播时"瞬移"而不是
                // 逐帧平滑滑行——这正是本任务要交付的效果,写反了等于没做。没在这份快照里的
                // 卫星要保留上一次的 lastUpdateRefTime,让 elapsed 跨帧持续增长。
                if (_preciseDirty)
                {
                    _allPrecise = _pendingPrecise; _preciseDirty = false; needRebuild = true;
                    for (size_t i = 0; i < _allPrecise.size(); ++i) _allPrecise[i].lastUpdateRefTime = refTime;
                }
                if (_starlinkDirty)
                {
                    _allStarlink = _pendingStarlink; _starlinkDirty = false; needRebuild = true;
                    for (size_t i = 0; i < _allStarlink.size(); ++i) _allStarlink[i].lastUpdateRefTime = refTime;
                }
            }
            if (_selectedOrbitDirty) needRebuild = true;   // Task 4 Step 5 会用到:选中态变化也要重建

            if (!needRebuild) return;
            _rebuildNeeded = false;

            _visiblePrecise.clear();
            for (size_t i = 0; i < _allPrecise.size(); ++i)
            {
                const Satellite& s = _allPrecise[i];
                if ((s.category == SatCategory::Station && _catStation) ||
                    (s.category == SatCategory::Navigation && _catNav) ||
                    (s.category == SatCategory::Weather && _catWeather))
                    _visiblePrecise.push_back(s);
            }
            if (_preciseGeode.valid()) _preciseRoot->removeChild(_preciseGeode.get());
            _preciseGeode = buildSatGeode(_visiblePrecise, _ss.get(), kSatSizePx);
            _preciseRoot->addChild(_preciseGeode.get());

            if (_starlinkGeode.valid()) _starlinkRoot->removeChild(_starlinkGeode.get());
            _starlinkGeode = buildSatGeode(_allStarlink, _ss.get(), kStarlinkSizePx);
            _starlinkRoot->addChild(_starlinkGeode.get());

            // 无条件重建(不只在 _selectedOrbitDirty 时才画)——精选组每 1s 重新传播一次,
            // 这里顺带把 ISS/天宫的常显轨道线也重算到最新;2-3 条轨道线×180 点 SGP4
            // 重算相对于同一批卫星的逐帧点云更新而言开销可忽略,不需要额外的脏标记
            // 精细控制。_selectedOrbitDirty 仍然保留:它的作用是让"点选/取消选中"能在
            // 当帧就强制 needRebuild=true、立即反映,不用等下一次精选组数据到达
            // (最多 1s 的延迟对交互来说不够跟手)。
            _selectedOrbitDirty = false;
            rebuildOrbitLines();
        }
```

（新增成员 `std::vector<Satellite> _visiblePrecise;`,放在 `_allPrecise, _allStarlink, _pendingPrecise, _pendingStarlink` 那一行旁边。`rebuildOrbitLines()`/`_selectedOrbitDirty` 在本任务 Step 5/6 才定义,这里先引用其名字——C++ 类内成员函数/成员变量互相引用不受声明顺序限制,合法,类的完整定义在编译期是整体可见的。）

把 `SyncCallback::operator()` 从:

```cpp
    void SyncCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    { _owner->syncIfDirty(); traverse(node, nv); }
```

改成:

```cpp
    void SyncCallback::operator()(osg::Node* node, osg::NodeVisitor* nv)
    {
        double refTime = nv->getFrameStamp() ? nv->getFrameStamp()->getReferenceTime() : 0.0;
        _owner->syncIfDirty(refTime);
        _owner->interpolate(refTime);
        traverse(node, nv);
    }
```

- [ ] **Step 5: 拾取**

在匿名命名空间加拾取处理器(镜像 `flight_data.cpp` 的 `FlightPickHandler`/`pickAt`),并在 `SatelliteLayerImpl` 加 `pickAt`：

```cpp
        void pickAt(osg::Camera* cam, float mx, float my)
        {
            if (_visiblePrecise.empty() || !cam->getViewport()) return;
            osg::Vec3d eye, center, up; cam->getViewMatrixAsLookAt(eye, center, up);
            osg::Matrixd VPW = cam->getViewMatrix() * cam->getProjectionMatrix()
                             * cam->getViewport()->computeWindowMatrix();
            double bestD2 = 1e18; int best = -1;
            for (size_t i = 0; i < _visiblePrecise.size(); ++i)
            {
                const osg::Vec3d& P = _visiblePrecise[i].ecef;
                if ((eye * P) <= (P * P)) continue;   // 前半球
                osg::Vec3d win = P * VPW;
                double d2 = (win.x()-mx)*(win.x()-mx) + (win.y()-my)*(win.y()-my);
                float tol = 12.0f;
                if (d2 < (double)(tol*tol) && d2 < bestD2) { bestD2 = d2; best = (int)i; }
            }
            if (best >= 0) selectByNoradIdInternal(_visiblePrecise[best].noradId);
        }
```

`selectByNoradIdInternal` 是真正的选中逻辑(供拾取和公开的 `selectByNoradId` 共用),写在 `SatelliteLayerImpl` 里,替换掉 Task 3 的占位实现:

```cpp
        virtual void selectByNoradId(int noradId) { selectByNoradIdInternal(noradId); }
        virtual void clearSelected()
        {
            OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex);
            _selected = SatelliteInfo();
            _selectedOrbitDirty = true;   // 下一帧清空选中态的轨道线/足迹圆几何
        }
        virtual SatelliteInfo getSelected() const
        { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex); return _selected; }
    protected:
        void selectByNoradIdInternal(int noradId)
        {
            const Satellite* found = nullptr;
            for (size_t i = 0; i < _visiblePrecise.size(); ++i)
                if (_visiblePrecise[i].noradId == noradId) { found = &_visiblePrecise[i]; break; }
            if (!found) return;
            SatelliteInfo info;
            info.valid = true; info.name = found->name; info.noradId = found->noradId;
            info.category = found->category; info.latDeg = found->latDeg; info.lonDeg = found->lonDeg;
            info.altKm = found->altKm; info.speedKmS = found->speedKmS;
            {
                OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex);
                _selected = info; _selectedLine1 = found->line1; _selectedLine2 = found->line2;
            }
            _selectedOrbitDirty = true;   // 下一帧(主线程 update)重建该卫星的轨道线+足迹圆
        }
    public:
```

新增成员:`SatelliteInfo _selected; std::string _selectedLine1, _selectedLine2; mutable OpenThreads::Mutex _selMutex; bool _selectedOrbitDirty = false;`(`_selectedOrbitDirty` 不需要加锁——只在主线程读写:`selectByNoradIdInternal`/`clearSelected` 目前只从主线程调用路径触发,拾取处理器 `PickHandler::handle` 本身就跑在主线程事件遍历里)。

- [ ] **Step 6: 拾取事件处理器 + 轨道线/足迹圆渲染**

```cpp
    static const float kSatDragThreshPx2 = 25.0f;

    class SatPickHandler : public osgGA::GUIEventHandler
    {
    public:
        SatPickHandler(SatelliteLayerImpl* o) : _owner(o), _downX(0.0f), _downY(0.0f), _pushed(false) {}
        virtual bool handle(const osgGA::GUIEventAdapter& ea, osgGA::GUIActionAdapter& aa)
        {
            if (ea.getEventType() == osgGA::GUIEventAdapter::PUSH
                && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            { _downX = ea.getX(); _downY = ea.getY(); _pushed = true; }
            else if (ea.getEventType() == osgGA::GUIEventAdapter::RELEASE
                     && ea.getButton() == osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON)
            {
                float dx = ea.getX() - _downX, dy = ea.getY() - _downY;
                if (_pushed && dx*dx + dy*dy < kSatDragThreshPx2)
                {
                    osgViewer::View* view = static_cast<osgViewer::View*>(&aa);
                    osg::Camera* cam = view->getCamera();
                    const osg::Viewport* vp = cam->getViewport();
                    if (vp)
                    {
                        float my = ea.getY();
                        if (ea.getMouseYOrientation() == osgGA::GUIEventAdapter::Y_INCREASING_DOWNWARDS)
                            my = vp->height() - my;
                        _owner->pickAt(cam, ea.getX(), my);
                    }
                }
                _pushed = false;
            }
            return false;
        }
    protected:
        SatelliteLayerImpl* _owner; float _downX, _downY; bool _pushed;
    };
```

在 `SatelliteLayerImpl::buildScene()` 里(在 `return _root;` 之前)加轨道线/足迹圆的挂载点:

```cpp
            _orbitRoot = new osg::Group; _orbitRoot->setName("SatOrbitLines");
            _root->addChild(_orbitRoot.get());
```

新增成员 `osg::ref_ptr<osg::Group> _orbitRoot;`。（`syncIfDirty(double refTime)` 末尾调用 `rebuildOrbitLines()` 的那几行已经在 Task 4 Step 4 重写 `syncIfDirty` 时一并写好了(`_selectedOrbitDirty = false; rebuildOrbitLines();`,每次 `needRebuild` 都无条件调用,不只是选中态变化时),本步骤不需要再改一遍 `syncIfDirty`,只需要把下面 `rebuildOrbitLines()` 的方法体真正实现补上,之前只是被引用了名字。）

`rebuildOrbitLines()` 实现(常显 ISS/天宫 + 当前选中卫星,选中的若与常显重复则跳过重复画线,只补足迹圆):

```cpp
    protected:
        static bool isAlwaysShow(int noradId) { return noradId == 25544 || noradId == 48274; }

        // 从"现在"起算的 tsince(分钟since该 TLE 自身历元),与 repropagate() 用的是
        // 同一手法——TLE 历元通常是数天前(CelesTrak 数据刷新周期),轨道线必须从
        // "现在"画起,不能从历元(tsince=0)画起,否则画的是"过去某一整圈"而非
        // "未来一整圈"(spec 原话:"画未来一整圈轨道线")。
        static double tsinceNow(const std::string& line1, const std::string& line2)
        {
            try
            {
                libsgp4::Tle tle(line1, line2);
                return (libsgp4::DateTime::Now() - tle.Epoch()).TotalMinutes();
            }
            catch (const std::exception&) { return 0.0; }
        }

        void rebuildOrbitLines()
        {
            _orbitRoot->removeChildren(0, _orbitRoot->getNumChildren());
            for (size_t i = 0; i < _allPrecise.size(); ++i)
            {
                if (!isAlwaysShow(_allPrecise[i].noradId)) continue;
                double tsince = tsinceNow(_allPrecise[i].line1, _allPrecise[i].line2);
                std::vector<osg::Vec3d> verts = earthsat::buildOrbitVertices(
                    _allPrecise[i].line1, _allPrecise[i].line2, tsince, 180);
                if (verts.size() < 2) continue;
                osg::Geometry* g = earthgeo::buildPolylineGeometry(verts, osg::Vec4(0.4f, 0.9f, 1.0f, 1.0f));
                osg::Geode* geode = new osg::Geode; geode->addDrawable(g);
                _orbitRoot->addChild(geode);
            }
            SatelliteInfo sel; std::string line1, line2;
            { OpenThreads::ScopedLock<OpenThreads::Mutex> lk(_selMutex);
              sel = _selected; line1 = _selectedLine1; line2 = _selectedLine2; }
            if (!sel.valid) return;
            if (!isAlwaysShow(sel.noradId))   // 避免与上面常显的 ISS/天宫重复画同一条线
            {
                double tsince = tsinceNow(line1, line2);
                std::vector<osg::Vec3d> verts = earthsat::buildOrbitVertices(line1, line2, tsince, 180);
                if (verts.size() >= 2)
                {
                    osg::Geometry* g = earthgeo::buildPolylineGeometry(verts, osg::Vec4(1.0f, 0.9f, 0.3f, 1.0f));
                    osg::Geode* geode = new osg::Geode; geode->addDrawable(g);
                    _orbitRoot->addChild(geode);
                }
            }
            double radiusKm = earthsat::footprintRadiusKm(sel.altKm);
            std::vector<osg::Vec3d> ring = earthsat::buildFootprintVertices(sel.latDeg, sel.lonDeg, radiusKm, 64);
            if (ring.size() >= 2)
            {
                osg::Geometry* g = earthgeo::buildPolylineGeometry(ring, osg::Vec4(1.0f, 0.5f, 0.2f, 1.0f));
                osg::Geode* geode = new osg::Geode; geode->addDrawable(g);
                _orbitRoot->addChild(geode);
            }
        }
    public:
```

（`tsinceNow` 需要 `#include "3rdparty/sgp4/DateTime.h"` 和 `#include "3rdparty/sgp4/Tle.h"`——Task 4 Step 3 已经因为 `repropagate()` 加过这两个 include,这里复用,不重复加。选中态的轨道线目前只在**点选那一刻**算一次(用当时的 `tsinceNow`),之后不随时间推移重新刷新——这是 v1 的既定简化,已在文末 Self-Review Notes 里说明,不在本步骤重复展开。）

在文件头部 `#include` 区加 `#include "geo_primitives.h"`。

在 `configureSatelliteLayer()` 里(`impl->_thread->startThread();` 之后)加拾取处理器注册:

```cpp
    viewer.addEventHandler(new SatPickHandler(impl.get()));
```

（这就需要 `configureSatelliteLayer` 的 `viewer` 参数不再是 unused——把签名里的 `osgViewer::View& /*viewer*/` 改回 `osgViewer::View& viewer`。）

- [ ] **Step 7: 离屏 E2E 验证**

（不需要单独的"首次抓取后自动选中 ISS"步骤——Task 4 Step 4 已经把 `rebuildOrbitLines()` 改成了每次 `needRebuild` 时无条件调用,而 `needRebuild` 在**第一批精选组数据到达**时(`_preciseDirty` 从 `syncIfDirty` 里被置位)必然为 true,`rebuildOrbitLines()` 内部本来就无条件遍历 `_allPrecise` 找 ISS/天宫画线,不需要额外的"自动选中"胶水代码。这是写计划时自查(self-review)发现上一版设计有 bug 后的简化——见文末 Self-Review Notes。）

```bash
cmake --build build/verse_core --target install --config Release 2>&1 | grep -c "error:"
cd build/sdk_core/bin
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=300 EARTH_SUN_TO_CAMERA=1 EARTH_SATS=1 \
  EARTH_SATS_FILE=/Users/USER/osgverse/applications/earth_explorer/test/sat_fixture_precise.tle \
  ./osgVerse_EarthExplorer 2>&1 | tail -5
```

Expected: `0` 编译错误,程序正常退出(此时 `EARTH_SATS` 钩子仍未接入 `earth_main.cpp`,离屏截图仍然只是"没有崩溃"的回归确认;拾取/轨道线的真正可视验证要等 Task 6 接入图层注册之后)。

- [ ] **Step 8: Commit**

```bash
git add applications/earth_explorer/sat_data.cpp
git commit -m "$(cat <<'EOF'
feat(earth): satellite picking, orbit line, footprint circle, ISS/Tiangong always-show

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: 详情卡(CardStack)

**Files:**
- Modify: `applications/earth_explorer/EarthControlUI.h`

**Interfaces:**
- Consumes: Task 4 的 `SatelliteLayer::getSelected()`/`clearSelected()`;`applications/earth_explorer/ui_card.h` 的 `earthui::Card`/`earthui::CardStack::upsert`。

- [ ] **Step 1: 加指针成员**

在 `EarthControlUI.h` 里找到 `FlightLayer* _flight = nullptr;    // 由 main 注入` 这一行,紧接着加:

```cpp
    SatelliteLayer* _satellites = nullptr;  // 由 main 注入
```

并在文件头部 `#include "flight_data.h"` 旁边加 `#include "sat_data.h"`。

- [ ] **Step 2: 加详情卡绘制块**

在航班详情卡那个 `if (_flight) { ... }` 块**之后**(紧邻着,同一缩进层级)加:

```cpp
        if (_satellites)
        {
            SatelliteInfo si = _satellites->getSelected();
            if (si.valid)
            {
                earthui::Card card;
                card.id = "satellite_detail";
                card.style.chipLabel = u8"卫星";
                card.title = u8"卫星详情 Satellite";
                card.drawBody = [si]() {
                    const char* catName = "?";
                    switch (si.category)
                    {
                    case SatCategory::Station:    catName = u8"空间站"; break;
                    case SatCategory::Navigation: catName = u8"导航星座"; break;
                    case SatCategory::Weather:    catName = u8"气象卫星"; break;
                    default:                      catName = u8"Starlink"; break;
                    }
                    ImGui::Text(u8"名称 Name: %s", si.name.c_str());
                    ImGui::Text(u8"NORAD ID: %d", si.noradId);
                    ImGui::Text(u8"类目 Category: %s", catName);
                    ImGui::Text(u8"高度 Alt: %.1f km", si.altKm);
                    ImGui::Text(u8"速度 Speed: %.2f km/s", si.speedKmS);
                    ImGui::Text(u8"经纬 LatLon: %.3f, %.3f", si.latDeg, si.lonDeg);
                };
                card.onClose = [this]() { _satellites->clearSelected(); };
                _cardStack.upsert(card);
            }
        }
```

- [ ] **Step 3: 本任务不改 `earth_main.cpp`**

已确认 `earth_main.cpp:1000` 现有写法是 `ctrlUI->_flight = flightLayer;`。真正给 `ctrlUI->_satellites` 赋值的那一行属于 Task 6(因为 Task 6 才会在 `earth_main.cpp` 里创建 `satelliteLayer` 局部变量),本任务(Task 5)只负责 `EarthControlUI.h` 里的成员声明+详情卡绘制块,跳过本步骤,直接进 Step 4。

- [ ] **Step 4: 构建确认零编译错误**

```bash
cmake --build build/verse_core --target install --config Release 2>&1 | grep -c "error:"
```

Expected: `0`。

- [ ] **Step 5: Commit**

```bash
git add applications/earth_explorer/EarthControlUI.h
git commit -m "$(cat <<'EOF'
feat(earth): satellite detail card in shared CardStack

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: `earth_main.cpp` 注册 — 图层目录 + 「全部」预设 + env 钩子

**Files:**
- Modify: `applications/earth_explorer/earth_main.cpp`

**Interfaces:**
- Consumes: Task 3/4 的 `configureSatelliteLayer`;Task 5 的 `EarthControlUI::_satellites`;`LayerManager::add`/`OverlayLayer`。

- [ ] **Step 1: 调用 `configureSatelliteLayer`**

找到航班层的注册代码(`earth_main.cpp` 里 `FlightLayer* flightLayer = nullptr; sceneCamera->addChild(configureFlightLayer(...));`),紧邻着加:

```cpp
    SatelliteLayer* satelliteLayer = nullptr;
    sceneCamera->addChild(configureSatelliteLayer(viewer, earthRoot.get(), mainFolder, &satelliteLayer));
```

并在文件头部 `#include "flight_data.h"` 旁边加 `#include "sat_data.h"`。

- [ ] **Step 2: 把指针接到 `EarthControlUI`**

`earth_main.cpp:1000` 现有代码:

```cpp
    ctrlUI->_flight = flightLayer;
```

紧接着这一行之后加:

```cpp
    ctrlUI->_satellites = satelliteLayer;
```

- [ ] **Step 3: 注册 4 个 OverlayLayer + env 强开钩子**

在 hk3d 图层注册代码(`OverlayLayer hk3d; ... layerMgr.add(hk3d);`)之后加:

```cpp
        OverlayLayer satStations; satStations.id = "satstations"; satStations.displayName = u8"空间站 (CelesTrak)";
        satStations.group = u8"卫星 Satellites"; satStations.enabled = false; satStations.hasOpacity = false;
        SatelliteLayer* satptr = satelliteLayer;
        satStations.apply = [satptr](const OverlayLayer& l) { if (satptr) satptr->setCategoryEnabled(SatCategory::Station, l.enabled); };
        layerMgr.add(satStations);

        OverlayLayer satNav; satNav.id = "satnav"; satNav.displayName = u8"导航星座 (GPS/北斗/Galileo/GLONASS)";
        satNav.group = u8"卫星 Satellites"; satNav.enabled = false; satNav.hasOpacity = false;
        satNav.apply = [satptr](const OverlayLayer& l) { if (satptr) satptr->setCategoryEnabled(SatCategory::Navigation, l.enabled); };
        layerMgr.add(satNav);

        OverlayLayer satWx; satWx.id = "satwx"; satWx.displayName = u8"气象卫星 (CelesTrak)";
        satWx.group = u8"卫星 Satellites"; satWx.enabled = false; satWx.hasOpacity = false;
        satWx.apply = [satptr](const OverlayLayer& l) { if (satptr) satptr->setCategoryEnabled(SatCategory::Weather, l.enabled); };
        layerMgr.add(satWx);

        OverlayLayer starlink; starlink.id = "starlink"; starlink.displayName = u8"Starlink 星链 (全量点云)";
        starlink.group = u8"卫星 Satellites"; starlink.enabled = false; starlink.hasOpacity = false;
        starlink.subtitle = u8"约 7000 颗,纯视觉壳层,不可点选";
        starlink.apply = [satptr](const OverlayLayer& l) { if (satptr) satptr->setCategoryEnabled(SatCategory::Starlink, l.enabled); };
        layerMgr.add(starlink);
```

- [ ] **Step 4: EARTH_SATS / EARTH_STARLINK 强开钩子**

在航班层的 `EARTH_FLIGHTS` 钩子代码块(`if (OverlayLayer* fl = layerMgr.find("flights")) { ... }`)之后加:

```cpp
    if (OverlayLayer* ss = layerMgr.find("satstations"))
    {
        const char* e = getenv("EARTH_SATS");
        if (e && *e) { ss->enabled = (atoi(e) != 0); layerMgr.setEnabled("satstations", ss->enabled); }
    }
    if (OverlayLayer* sl = layerMgr.find("starlink"))
    {
        const char* e = getenv("EARTH_STARLINK");
        if (e && *e) { sl->enabled = (atoi(e) != 0); layerMgr.setEnabled("starlink", sl->enabled); }
    }
```

（headless 验证只需要能强开"其中一类"精选组即可触发抓取/渲染链路,不需要给 nav/weather 也各配一个 env 钩子——`EARTH_SATS` 专门对应 `satstations`,与 `EARTH_SATS_FILE`／`EARTH_STARLINK_FILE` 命名呼应,`EARTH_SATS_FILE` 走的是 `sat_data.cpp` 内部读取,不需要在这里额外处理。）

- [ ] **Step 5: 「全部」预设加入三个精选类目(不含 Starlink)**

找到:

```cpp
        p.name = u8"全部"; p.enabledIds = { "precip", "flights", "hk3d", "gdacs", "quakes",
                                            "eonet", "gdelt", "gpsjam", "unhcr", "nhc",
                                            "bases", "ports", "nuclear", "spaceports", "datacenters" };
```

改成:

```cpp
        p.name = u8"全部"; p.enabledIds = { "precip", "flights", "hk3d", "gdacs", "quakes",
                                            "eonet", "gdelt", "gpsjam", "unhcr", "nhc",
                                            "bases", "ports", "nuclear", "spaceports", "datacenters",
                                            "satstations", "satnav", "satwx" };
```

（不加 `"starlink"`——spec 明确要求「全部」预设不含 Starlink。）

- [ ] **Step 6: 构建 + 离屏 E2E(这次是真正接入后的验证)**

```bash
cmake --build build/verse_core --target install --config Release 2>&1 | grep -c "error:"
cd build/sdk_core/bin
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=300 EARTH_SUN_TO_CAMERA=1 EARTH_SATS=1 \
  EARTH_SATS_FILE=/Users/USER/osgverse/applications/earth_explorer/test/sat_fixture_precise.tle \
  ./osgVerse_EarthExplorer 2>&1 | grep -a "Parsed.*satellites\|Sat\]"
```

Expected: 看到 `[Sat] Parsed 3 satellites (precise, fixture)`(fixture 里 3 颗:ISS/天宫/NOAA15)。

```bash
ls -la /tmp/earth_capture_0.png
```

Expected: 存在,且是这次新生成的(用 `date` 核对时间戳,不要复用旧文件——本仓库有过陈旧截图误判的教训)。

```bash
# EARTH_PRESET 组合验证:「全部」预设应用后三个精选类目应为 enabled
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=10 EARTH_PRESET=u8"全部" \
  EARTH_SATS_FILE=/Users/USER/osgverse/applications/earth_explorer/test/sat_fixture_precise.tle \
  ./osgVerse_EarthExplorer 2>&1 | grep -a "applied\|satstations\|satnav\|satwx"
```

（`EARTH_PRESET` 具体怎么打日志断言,以 `earth_main.cpp` 里现有 `applyPreset` 调用处的既有日志格式为准,照抄该格式验证——不要发明新的断言方式。）

- [ ] **Step 7: 4 类历史回归分类器**

```bash
cd /Users/USER/osgverse
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=300 EARTH_SUN_TO_CAMERA=1 ./build/sdk_core/bin/osgVerse_EarthExplorer > /dev/null 2>&1
python3 applications/earth_explorer/test/classify_rb_swap.py /tmp/earth_capture_0.png
```

Expected: `CLEAN`(global 视角)。

```bash
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=300 EARTH_SUN_TO_CAMERA=1 ./build/sdk_core/bin/osgVerse_EarthExplorer --goto 89.9 0 12000 > /dev/null 2>&1
python3 applications/earth_explorer/test/classify_rb_swap.py /tmp/earth_capture_0.png
```

Expected: `CLEAN`(pole 视角,且亲自截图确认极点仍是闭合星形无破洞——不要只看分类器输出就下结论,分类器只测色偏,不测几何)。

kunming/hk 两个分类器按既有教训(`docs/superpowers/plans/2026-07-04-v015-vision-plan.md` Step 4)预期是 `UNKNOWN`(离屏时序限制的已知问题,非新回归),目检截图确认是已知的"渐变色块/正常港湾细节"而非新的破坏即可,不要花时间试图让它们变成 `CLEAN`。

- [ ] **Step 8: Commit**

```bash
git add applications/earth_explorer/earth_main.cpp
git commit -m "$(cat <<'EOF'
feat(earth): register satellite layers in LayerManager + 全部 preset + EARTH_SATS/_STARLINK hooks

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: 全量回归 + 打包 + 交接文档

**Files:**
- Modify: `HANDOFF.md`
- (无代码改动;本任务是验证+文档收尾)

- [ ] **Step 1: 全部既有单测 + 新单测跑一遍**

```bash
cd /Users/USER/osgverse
for t in osgVerse_Test_Feeds osgVerse_Test_Ai_Chat osgVerse_Test_TileOverlay osgVerse_Test_Satellite; do
  DYLD_LIBRARY_PATH=build/sdk_core/lib build/verse_core/bin/$t > /dev/null 2>&1
  echo "$t exit=$?"
done
```

Expected: 全部 `exit=0`。

- [ ] **Step 2: Starlink 壳层截图验证**

```bash
cd build/sdk_core/bin
rm -f /tmp/earth_capture_0.png
EARTH_OFFSCREEN=1 EARTH_AUTOCAP=300 EARTH_SUN_TO_CAMERA=1 EARTH_STARLINK=1 \
  EARTH_STARLINK_FILE=/Users/USER/osgverse/applications/earth_explorer/test/sat_fixture_starlink.tle \
  ./osgVerse_EarthExplorer 2>&1 | grep -a "starlink"
```

Expected: `[Sat] Parsed 2 satellites (starlink, fixture)`(用离屏截图目检确认 Starlink 的两个暗色小点在球面上可见、不是黑屏/全屏纯色——2 颗点在整个地球画面里非常小,肉眼可能需要放大截图确认,能看到即算通过,数量太少不代表壳层机制有问题,机制已经用 Task 3/4 的其余验证覆盖过)。

- [ ] **Step 3: 打包**

```bash
bash packaging/package_macos.sh 2>&1 | tail -5
codesign -v dist/EarthExplorer.app/Contents/MacOS/osgVerse_EarthExplorer && echo SIG_OK
```

Expected: 打包成功,签名校验通过。

- [ ] **Step 4: 更新 HANDOFF.md**

在 HANDOFF.md 顶部新增一节(参照既有条目的格式:状态+关键 commit+验证结论+待办),简述:P2 卫星层已完成实现,SGP4 vendor 自 dnwrnr/sgp4 并用官方 Python sgp4 包独立核实数值一致;精选组(空间站/导航/气象)+ Starlink 壳层均可用;「全部」预设已纳入精选组;4 类历史回归分类器结果;**真机待验清单**(留给用户在真实网络环境下确认:①真实 CelesTrak 网络抓取成功且 24h 缓存生效;②Starlink 全量 ~7000 点渲染性能可接受;③点选 ISS/其它卫星的轨道线+足迹圆+详情卡观感)。

- [ ] **Step 5: Commit**

```bash
git add HANDOFF.md
git commit -m "$(cat <<'EOF'
docs(earth): HANDOFF — P2 satellite layer implementation complete, pending real-network verification

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>
EOF
)"
```

**本任务不 push、不打 tag**——按本仓库既有约定(见 memory `release-requires-confirmation`),推送时机由用户决定;真机验证通过后再由用户决定何时打标签。

---

## Self-Review Notes(写计划时的自查,供执行者参考)

- **Spec 覆盖**:精选组+Starlink(Task 3/6)、轨道线+足迹圆+详情卡(Task 4/5)、图层目录 4 开关+「全部」预设(Task 6)、EARTH_SATS*/EARTH_STARLINK* 钩子(Task 3/6)、单测+E2E+4 类回归(Task 1/2/6/7)均有对应任务。Spec 里的"开放问题"(CelesTrak Rate Limit/ToS、GROUP 参数名)已在写本计划前实测确认:`gp.php?GROUP=<g>&FORMAT=tle` 可用,返回标准 3 行格式;24h 缓存 TTL 是本计划的保守选择,足以满足"不要高频拉取"的一般性预期。
- **类型一致性**:`SatCategory`/`SatelliteInfo`/`SatelliteLayer` 从 Task 3 定义后,Task 4/5/6 均直接复用同一签名,未改名。`earthsat::` 命名空间(Task 2)与 `SatCategory`(Task 3,不带命名空间,直接放 `sat_data.h` 全局)刻意分开——前者是纯数学层不涉及"卫星分类"这个业务概念,后者是数据/渲染层的分类,职责边界明确。
- **已知 v1 限制**(不在本计划范围内,不要节外生枝):时间快进/历史回放、卫星过境预报、AI 工具接入(P4)、Starlink 全量流式细节加载(spec 已明确是"一次性拉取全量渲染成点云",不追求分块懒加载)、点选卫星的轨道线/足迹圆只在点选那一刻算一次(用当时的 `tsinceNow`),不随时间推移持续刷新——ISS/天宫的"常显"轨道线则会随每次精选组重新传播(每 1s)一起用最新 `tsinceNow` 重算,因为它们走的是 `rebuildOrbitLines()` 里遍历 `_allPrecise` 的那条路径,与"选中"路径分开。
- **写计划时自查揪出的两处设计 bug(已在写 Task 4 时直接改正,记录在此供执行者理解为什么 syncIfDirty/rebuildOrbitLines 长这样)**:①`rebuildOrbitLines()` 起初被设计成只在 `_selectedOrbitDirty` 为真时才调用,而"首次抓取到数据后自动画出 ISS/天宫常显轨道线"这一步只把 `_rebuildNeeded` 置位、没碰 `_selectedOrbitDirty`——按原设计根本不会触发画线,是个不会报错但功能悄悄缺失的 bug。修法是把 `rebuildOrbitLines()` 改成每次 `needRebuild` 就无条件调用(2-3 条轨道线的 SGP4 重算开销可忽略),同时干脆去掉了整个"自动选中 ISS"的胶水步骤——这也是为什么 Task 4 只有 8 步而不是最初设想的 9 步。②`rebuildOrbitLines()` 起初用 `buildOrbitVertices(line1, line2, 0.0, 180)`(即从 TLE 历元起算),但 TLE 历元通常是数天前的时间戳,这样画出来的是"过去某一整圈"而非 spec 要求的"未来一整圈"——修法是新增 `tsinceNow()` 辅助函数,与 `repropagate()` 用同一手法算出"现在"相对该 TLE 历元的分钟数,再喂给 `buildOrbitVertices`。两处都是纯逻辑推理在写计划阶段发现的,不是靠运行时调试——这也是为什么写计划前要做完整的端到端设计推演,而不是拼凑代码片段。
- **执行阶段任务审查又抓到第三处(写计划时自查没抓到,如实记录不遮掩)**:`syncIfDirty()` 里 `lastUpdateRefTime` 的重置循环原先写在 `if (_preciseDirty)`/`if (_starlinkDirty)` 判断**之外**、对 `_allPrecise`/`_allStarlink` 全体无条件执行——由于 `SyncCallback` 同一帧内先调 `syncIfDirty(refTime)` 再调 `interpolate(refTime)`、两次传的是同一个 `refTime`,无条件重置意味着 `interpolateOne()` 算出的 `elapsed` 永远 ≈0,`ecefVelocity*elapsed` 外推项形同虚设——本任务要交付的"逐帧平滑外推"效果被自己写的代码原地抵消,卫星实际观感会是"每次重新传播时瞬移"而非平滑滑行。任务审查子代理精确复现了这条因果链并定位到根因,修法已并入正文(移进各自的 `if` 分支内),上面的代码块已经是修复后的版本。这处 bug 没有被我自己的写计划自查发现,是靠独立的任务审查环节抓住的——记录下来提醒:自查再仔细也会漏,独立评审这一步不能省。
