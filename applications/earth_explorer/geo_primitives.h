#ifndef EARTH_GEO_PRIMITIVES_H
#define EARTH_GEO_PRIMITIVES_H
// T3(世界信息枢纽 P0):大圆弧线/贴地折线渲染原语。
// 为 P1+ 的 O-D 流向类数据源(航线/贸易流/攻击弧)准备的几何底座,P0 只落原语本身
// + EARTH_ARC_DEMO 演示钩子,不接任何 feed。
// 设计约定(与 feed_layer 一致):
//   - lla = (纬度°, 经度°, 高度m)——注意纬度在前,与 feed_layer.cpp 的 lat/lon 次序一致;
//   - 顶点数学(buildArcVertices/buildPolylineVertices)是纯函数,不建 viewer/GL 上下文
//     即可调用,tests/ai_chat_tests.cpp 直接单测;
//   - 场景图构建(buildArcGeometry/buildPolylineGeometry)自带独立着色器 StateSet,
//     红线:绝不触碰 globe 的 scattering 着色器/太阳/海洋。
// 已知渲染限制(P0 接受、P1 再解):
//   - 线宽:StateSet 设了 LineWidth(2),但 macOS GL core profile 宽线已被移除
//     (范围 [1,1]),实际渲染 1px;真 2px 需屏幕空间扩线,推迟到 P1;
//   - 颜色 alpha:着色器会把 alpha 写进片元,但 StateSet 未开混合(BlendFunc),
//     alpha<1 仍按不透明画——颜色参数的 alpha 当前被忽略,想要半透明等 P1 一起做。
//   - StateSet/Program 复用:构建函数接受可选的外部 StateSet(externalSS)——传入时
//     直接挂用、不再自建 Program(消掉跨刷新的重复 compile/link);由调用方**实例内**
//     持有并复用。红线:禁止全局 static 跨实例共享 Program——已实证(4ee4c74d 回退
//     记录)会引发全球瓦片非确定性染色。场景图构建须在主线程。
#include <osg/Vec2d>
#include <osg/Vec3d>
#include <osg/Vec4>
#include <osg/Geometry>
#include <osg/Node>
#include <vector>

namespace earthgeo
{
    // 大圆弧顶点(ECEF):A→B 沿大圆 slerp,n 个顶点(n<2 钳到 2);
    // 弧高 = heightScale × 大圆长 × sin(πt)——首尾贴地(取 lla 高度),中点最高。
    // 对跖点(大圆方向数学上不唯一)取确定的过渡轴,保证输出稳定、无 NaN。
    std::vector<osg::Vec3d> buildArcVertices(const osg::Vec3d& llaA, const osg::Vec3d& llaB,
                                             int n, double heightScale);

    // 贴地折线顶点(ECEF):路标 (纬度°, 经度°) 依次相连,每段沿大圆按 ~1°(≈111km)
    // 步长细分(路标顶点精确保留);liftMeters 沿椭球法线整体抬升(防与地表 z-fight)。
    std::vector<osg::Vec3d> buildPolylineVertices(const std::vector<osg::Vec2d>& latLonDeg,
                                                  double liftMeters = 0.0);

    // 一条弧的渲染描述:顶点(buildArcVertices 的输出)+ 起终点颜色(顶点色 O→D 渐变;
    // alpha 当前被忽略——未开混合,见文件头"已知渲染限制")。
    struct ArcStrip
    {
        std::vector<osg::Vec3d> verts;
        osg::Vec4 colorO, colorD;
    };

    // 多条弧合入一个 Geometry:共享顶点数组,每弧独立 LINE_STRIP primitiveset;
    // externalSS 为空时自建弧线着色器 StateSet(顶点色渐变 + FlowPhase 流动动画
    // uniform,默认 0=静态),非空时直接挂用(实例内复用,见文件头"StateSet/Program
    // 复用"——选"重载不自建"而非"建后覆盖":后者每次 sync 仍白造一个 Program 垃圾对象)。
    osg::Geometry* buildArcGeometry(const std::vector<ArcStrip>& arcs,
                                    osg::StateSet* externalSS = NULL);

    // 单条折线 Geometry:纯色 LINE_STRIP(虚线样式 P1 需要时再加),同款独立着色器;
    // color 的 alpha 当前被忽略(未开混合,见文件头"已知渲染限制");externalSS 同上。
    osg::Geometry* buildPolylineGeometry(const std::vector<osg::Vec3d>& verts,
                                         const osg::Vec4& color,
                                         osg::StateSet* externalSS = NULL);

    // EARTH_ARC_DEMO 演示层(不进产品,headless 验收/回归复现用):
    // 3 条固定弧(京→巴黎、京→纽约、上海→悉尼)+ 1 条折线(东京→香港→新加坡→孟买)。
    osg::Node* createArcDemoNode();

    // P6a:XYZ 瓦片坐标(yXYZ=原点左上) → EPSG:3857 米制 bbox(xmin,ymin,xmax,ymax)。
    // WMS GetMap 按瓦片金字塔合成请求用(GEBCO 首用)。
    osg::Vec4d mercatorTileBBox(int x, int yXYZ, int z);
}
#endif
