# P2 卫星层设计(世界信息枢纽路线图)

> 状态:草案待用户书面审阅。关联 [[earth-world-info-hub-roadmap]]、[[earth-flight-layer-done]]。

## 背景与目标

「世界信息枢纽」路线图([`2026-07-03-world-info-hub-roadmap.md`](2026-07-03-world-info-hub-roadmap.md))P0/P1 已完成(FeedLayer 框架 + 8 个零 key 数据源)。P2 是路线图定义的"杀手锏"阶段:CelesTrak TLE + SGP4 轨道递推,3D 球面卫星可视化——静态地图做不到的场景,是这条路线图里视觉冲击力最强的一步。

**v1 目标**:
- 精选组(空间站/导航星座/气象卫星,约 200 颗)常显位置点,可点选看轨道线+足迹+详情;
- Starlink 全量(约 7000 颗)作为纯视觉点云壳层,可选开关,不可点选;
- ISS/天宫等"明星卫星"默认常显完整轨道线。

**v1 明确不做**(留后续版本):时间快进/历史回放、导航星座轨道线常显、卫星过境预报、AI 工具接入(路线图 P4)、碰撞/共线检测。

## 架构选型

考察了三种方案:

1. **复用 FeedLayer 框架**——FeedLayer 是"定时刷新+静态几何"模型(见 `feed_layer.cpp`),卫星需要逐帧连续运动(LEO 约 7.5km/s),硬套需要大改框架语义,不采用。
2. **独立 sat_data 模块,仿 flight_data 模板(采用)**——`flight_data.{h,cpp}` 已验证"后台线程算位置 → 双缓冲交主线程 → 逐帧线性外推 → GL_POINTS 点精灵 → 点击详情卡"这套模式解决的正是同构问题(OpenSky 位置查询 vs SGP4 轨道递推)。零侵入 globe 着色器和 DatabasePager 分页机制(红线)。
3. **精选组走 FeedLayer + Starlink 独立机制**——两条渲染/更新路径需要分别维护,复杂度无收益,不采用。

**结论:方案 2**。新增 `applications/earth_explorer/sat_data.{h,cpp}`,接口形状参照 `flight_data.h`:

```cpp
class SatelliteLayer
{
public:
    virtual ~SatelliteLayer() {}
    virtual void setEnabled(bool on) = 0;            // 精选组总开关
    virtual bool isEnabled() const = 0;
    virtual void setStarlinkEnabled(bool on) = 0;     // Starlink 壳层独立开关
    virtual bool isStarlinkEnabled() const = 0;
    virtual void selectByNoradId(int noradId) = 0;    // 点选(拾取回调驱动)或程序化选中(如 ISS 默认)
    virtual void clearSelected() = 0;
    virtual SatelliteInfo getSelected() const = 0;
    virtual std::string summaryJson() const = 0;      // 供未来 P4 AI 工具复用,v1 先占位
};
```

## 数据源与递推

- **TLE 拉取**:CelesTrak `https://celestrak.org/NORAD/elements/gp.php?GROUP=<g>&FORMAT=tle`,7 组:`stations`(空间站)、`gps-ops`、`beidou`、`galileo`、`glonass-ops`(导航星座合并归类"导航")、`weather`(气象)、`starlink`。复用 `requests`(libhv)+ 现有网络基础设施。
- **缓存**:磁盘缓存 24h TTL(`~/Library/Application Support/EarthExplorer/tle_cache/` 或等价路径),遵守 CelesTrak "不要高频拉取"的公开使用建议;启动时缓存过期才联网,不阻塞主线程(参照 `precip_data.cpp` 后台线程 + 主线程 FRAME 应用模式)。
- **SGP4**:vendor `3rdparty/sgp4`(dnwrnr/sgp4 移植,Apache-2.0,单头文件量级,与 3rdparty 现有惯例一致,不引入新构建系统依赖)。
- **递推频率**:精选组(约 200 颗)每 1s 后台重新 SGP4 递推一次;Starlink(约 7000 颗)每 5s 一次(点云不需要精细连续性,减少 CPU)。双缓冲结构交给主线程,主线程逐帧对每颗卫星线性外推位置(同 `flight_data.cpp` 现有平滑滑行手法),避免可感知的跳变。

## 渲染

- **精选组点精灵**:GL_POINTS,按类别配色(空间站/导航/气象三色,复用地震层的点精灵着色器模板),可拾取(复用现有拾取管线,同航班层点击详情的实现路径)。
- **Starlink 壳层**:独立 VBO 纯点云,更小更暗的点(区别于精选组),**不参与拾取**(不接拾取回调,避免 7000 点的拾取开销和"点太密点不准"的体验问题)。
- **点选后**(精选组专属):
  - 画整圈轨道线——取选中卫星未来一个轨道周期(约 90~100 分钟 LEO / 24h GEO,按 TLE 平均运动反推周期)采样点转 ECEF,复用 `geo_primitives.cpp` 的 `buildPolylineVertices`;
  - 画地面足迹圆——以星下点(TLE 递推位置在地表的投影)为心,按当前高度算 0° 仰角覆盖半径,复用现有圆形/环形图元(NHC 飓风锥已有环形绘制先例可参考);
  - 右上角详情卡(名称/NORAD ID/高度/速度),复用 v0.15 引入的共享 `ui_card.h` CardStack,与其它详情卡外观一致。
- **默认常显轨道线**:ISS、天宫等"明星卫星"(硬编码 NORAD ID 白名单,v1 约 3-5 颗)不需要点选即常显完整轨道线,增强首屏可看性;其余精选组卫星按需点选才画。

## 图层目录与钩子

- 图层目录新增分组"卫星 Satellites",4 个独立开关:空间站 / 导航星座 / 气象卫星 / Starlink(壳层)。
- 「全部」预设(`earth_main.cpp` enabledIds)纳入前三个,**不纳入 Starlink**(默认不喧宾夺主,且 7000 点云有一定加载/带宽成本,用户手动开)。
- 测试钩子,与既有 EARTH_* 系列同模式:
  - `EARTH_SATS=1` 强制开启精选组(headless 验证用);
  - `EARTH_SATS_FILE=<本地TLE文件>` 离线 fixture,绕过联网;
  - `EARTH_STARLINK=1` 单独强开 Starlink 壳层;
  - `EARTH_STARLINK_FILE=<本地TLE文件>` 同上。

## 测试计划

- **单测**(`tests/satellite_tests.cpp`,仿 `feed_layer_tests.cpp`/`tile_overlay_tests.cpp` 的 CHECK 宏 + `#include` 实现文件模式):
  - TLE 文本解析正确性(标准两行元素格式,含校验位);
  - SGP4 递推结果对 dnwrnr/sgp4 自带参考验证向量(该库自带测试数据,复用做回归基准,不是我们自己编数值);
  - 足迹圆半径公式(给定轨道高度算 0° 仰角覆盖半径,几何解析解验证);
  - 轨道周期采样点转 ECEF 与直转结果一致性(仿 `feed_layer_tests.cpp` 现有的"与 convertLLAtoECEF 直转米级一致"断言风格)。
- **E2E**(离屏,`EARTH_OFFSCREEN=1`):
  - fixture TLE 文件加载 → 断言精选组/Starlink 点数与 fixture 条目数精确匹配(仿 P1 七源加载计数断言);
  - Starlink 壳层截图(视觉确认点云分布,非黑屏/非全屏纯色);
  - 点选 ISS(或 fixture 里的某颗)→ 轨道线+足迹圆渲染截图。
- **历史回归**:4 类分类器(global/pole/kunming/hk)全部重跑一遍——本次是新增独立模块、不碰 globe 着色器/DatabasePager,预期零交叉影响,但仍需实测confirm(遵循本仓库"渲染管线级改动必须重新过回归"的既有教训,卫星虽不改管线,但新增大量 GL_POINTS 绘制调用,保险起见仍跑)。

## 开放问题(留待实现阶段确认,不阻塞本 spec 批准)

- CelesTrak 是否有明确的 Rate Limit / ToS 条款需要遵守(实现前需 curl 探测 + 读其使用条款页,若有则据此调整缓存 TTL);
- 7 组 GROUP 参数名以 CelesTrak 当前 API 为准(`gp.php` 是新版接口,旧版 `.txt` 端点可能仍可用作 fallback,实现前需实测确认哪个可达)。
