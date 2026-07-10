// geo_primitives:大圆弧线/贴地折线原语实现(T3)。
// 顶点数学 = 单位 ECEF 方向向量间的球面插值(slerp);弧的半径 = 端点半径线性过渡 +
// 正弦弧高,折线的半径 = 逐细分点取局部 WGS84 椭球面半径(贴地不掉不浮);
// 渲染 = 自成一体的细线着色器(参照 feed_layer.cpp 的点精灵着色器模式:独立 Program、
// 独立 StateSet、RenderBin 11,与 globe 的 scattering 管线零交集)。
#include <osg/Geode>
#include <osg/LineWidth>
#include <osg/Quat>
#include <modeling/Math.h>
#include <pipeline/Pipeline.h>
#include <iostream>
#include <cmath>
#include "geo_primitives.h"

namespace earthgeo
{
namespace
{
    const double kMeanEarthRadius = 6371000.0;   // 弧长估算用平均半径(弧高比例的基准)

    // 线着色器:顶点色直通(弧的 O→D 渐变在 CPU 侧按 t 插进顶点色,折线则通体同色),
    // texcoord0.x 携带沿线参数 t,FlowPhase>0 时叠加沿线流动的亮度脉冲(P1 O-D 流向层
    // 用 uniform 回调驱动;demo 恒 0=静态)。双输出(fragColor/fragOrigin)跟随 feed_layer
    // 的 MRT 约定。
    const char* lineVertCode = {
        "VERSE_VS_OUT vec4 lineColor;\n"
        "VERSE_VS_OUT float lineT;\n"
        "void main() {\n"
        "    lineColor = osg_Color;\n"
        "    lineT = osg_MultiTexCoord0.x;\n"
        "    gl_Position = VERSE_MATRIX_MVP * osg_Vertex;\n"
        "}\n"
    };
    const char* lineFragCode = {
        "uniform float FlowPhase;\n"
        "VERSE_FS_IN vec4 lineColor;\n"
        "VERSE_FS_IN float lineT;\n"
        "#ifdef VERSE_GLES3\n"
        "layout(location = 0) VERSE_FS_OUT vec4 fragColor;\n"
        "layout(location = 1) VERSE_FS_OUT vec4 fragOrigin;\n"
        "#endif\n"
        "void main() {\n"
        "    float glow = (FlowPhase > 0.0)\n"
        "               ? 0.35 * (0.5 + 0.5 * sin((lineT - FlowPhase) * 62.8318)) : 0.0;\n"
        "    vec3 rgb = clamp(lineColor.rgb * (1.0 + glow), 0.0, 1.0);\n"
        "#ifdef VERSE_GLES3\n"
        "    fragColor = vec4(rgb, lineColor.a); fragOrigin = vec4(1.0);\n"
        "#else\n"
        "    gl_FragData[0] = vec4(rgb, lineColor.a); gl_FragData[1] = vec4(1.0);\n"
        "#endif\n"
        "}\n"
    };

    osg::Vec3d lla2ecef(double latDeg, double lonDeg, double altM)
    {
        return osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            osg::DegreesToRadians(latDeg), osg::DegreesToRadians(lonDeg), altM));
    }

    // 射线-椭球闭式解:过地心、单位方向 dir 的射线与 WGS84 椭球面交点的地心半径。
    // (R·cosψ/a)² + (R·sinψ/b)² = 1 → R = 1/sqrt((dx²+dy²)/a² + dz²/b²)。
    // 与 convertLLAtoECEF 用同一组 osg::WGS_84_* 常量,细分点与端点贴同一张椭球面。
    double surfaceRadius(const osg::Vec3d& dir)   // dir 必须已归一化
    {
        const double a2 = osg::WGS_84_RADIUS_EQUATOR * osg::WGS_84_RADIUS_EQUATOR;
        const double b2 = osg::WGS_84_RADIUS_POLAR * osg::WGS_84_RADIUS_POLAR;
        return 1.0 / std::sqrt((dir.x() * dir.x() + dir.y() * dir.y()) / a2
                             + dir.z() * dir.z() / b2);
    }

    // 独立线着色器 StateSet:深度测试默认开(不额外设 Depth 属性)、线宽 2、
    // RenderBin 11(同 feed_layer:地表/海洋 pass 之后画)。
    osg::StateSet* createLineStateSet(const std::string& name)
    {
        osg::Shader* vs = new osg::Shader(osg::Shader::VERTEX, lineVertCode);
        osg::Shader* fs = new osg::Shader(osg::Shader::FRAGMENT, lineFragCode);
        vs->setName((name + "_VS").c_str()); fs->setName((name + "_FS").c_str());
        osgVerse::Pipeline::createShaderDefinitions(vs, 100, 130);
        osgVerse::Pipeline::createShaderDefinitions(fs, 100, 130);
        osg::ref_ptr<osg::Program> prog = new osg::Program;
        prog->addShader(vs); prog->addShader(fs);

        osg::StateSet* ss = new osg::StateSet;
        ss->setAttributeAndModes(prog.get(), osg::StateAttribute::ON);
        // 老实交代:macOS GL core profile 已移除宽线(ALIASED_LINE_WIDTH_RANGE=[1,1]),
        // LineWidth(2) 实际画 1px,还可能报 GL_INVALID_VALUE(无害)。保留该属性是因为
        // 兼容 profile/其他平台仍然生效(plan 要求线宽 2);macOS 上要真 2px 得做屏幕
        // 空间扩线(三角带),推迟到 P1 真接 O-D 流向数据时再做。
        ss->setAttributeAndModes(new osg::LineWidth(2.0f), osg::StateAttribute::ON);
        ss->addUniform(new osg::Uniform("FlowPhase", 0.0f));
        ss->setRenderBinDetails(11, "RenderBin");
        return ss;
    }

    // 通用 LINE_STRIP 追加:顶点 + O→D 渐变顶点色 + texcoord0.x=t,一条 strip 一个
    // 独立 primitiveset(plan 要求:多弧共享数组、各自成段,不会首尾粘连)。
    void appendLineStrip(osg::Geometry* geom, const std::vector<osg::Vec3d>& verts,
                         const osg::Vec4& colorO, const osg::Vec4& colorD)
    {
        if (verts.size() < 2) return;
        osg::Vec3Array* va = static_cast<osg::Vec3Array*>(geom->getVertexArray());
        osg::Vec4Array* ca = static_cast<osg::Vec4Array*>(geom->getColorArray());
        osg::Vec2Array* ta = static_cast<osg::Vec2Array*>(geom->getTexCoordArray(0));
        size_t base = va->size(), num = verts.size();
        for (size_t i = 0; i < num; ++i)
        {
            double t = (double)i / (double)(num - 1);
            va->push_back(verts[i]);   // double→float:地球尺度下 ~0.5m 量化,线要素可接受(同 feed 点)
            ca->push_back(colorO * (float)(1.0 - t) + colorD * (float)t);
            ta->push_back(osg::Vec2((float)t, 0.0f));
        }
        geom->addPrimitiveSet(new osg::DrawArrays(GL_LINE_STRIP, (GLint)base, (GLsizei)num));
    }

    // externalSS 非空 = 调用方实例内复用上一轮的 StateSet/Program(6a 性能债修复:
    // 每 sync 不再 1+L 次 compile/link);为空才自建。禁止全局跨实例共享(4ee4c74d)。
    osg::Geometry* createEmptyLineGeometry(const std::string& shaderName,
                                           osg::StateSet* externalSS)
    {
        osg::Geometry* geom = new osg::Geometry;
        geom->setVertexArray(new osg::Vec3Array);
        geom->setColorArray(new osg::Vec4Array, osg::Array::BIND_PER_VERTEX);
        geom->setTexCoordArray(0, new osg::Vec2Array);
        geom->setUseDisplayList(false); geom->setUseVertexBufferObjects(true);
        geom->setCullingActive(false);   // 同 feed:ECEF 大坐标下避免误剔除
        geom->setStateSet(externalSS ? externalSS : createLineStateSet(shaderName));
        return geom;
    }
}

std::vector<osg::Vec3d> buildArcVertices(const osg::Vec3d& llaA, const osg::Vec3d& llaB,
                                         int n, double heightScale)
{
    if (n < 2) n = 2;
    osg::Vec3d eA = lla2ecef(llaA.x(), llaA.y(), llaA.z());
    osg::Vec3d eB = lla2ecef(llaB.x(), llaB.y(), llaB.z());
    double rA = eA.length(), rB = eB.length();
    osg::Vec3d dA = eA / rA, dB = eB / rB;

    double cosT = osg::clampBetween(dA * dB, -1.0, 1.0);
    double theta = std::acos(cosT), sinT = std::sin(theta);
    // 对跖点:sinθ≈0 且 cosθ<0,slerp 分母失效且路径不唯一 → 取与 dA 正交的确定
    // 过渡轴,用四元数绕轴旋转,输出稳定(经过极方向的那条子午大圆)。
    bool nearAntipodal = (sinT < 1e-9 && cosT < 0.0);
    osg::Vec3d axis;
    if (nearAntipodal)
    {
        axis = dA ^ osg::Vec3d(0.0, 0.0, 1.0);
        if (axis.length2() < 1e-12) axis = dA ^ osg::Vec3d(1.0, 0.0, 0.0);
        axis.normalize();
    }

    double arcLen = theta * kMeanEarthRadius;   // 弧高基准:大圆弧长(米)
    std::vector<osg::Vec3d> out; out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        double t = (double)i / (double)(n - 1);
        osg::Vec3d dir;
        if (nearAntipodal) dir = osg::Quat(t * theta, axis) * dA;
        else if (sinT < 1e-9) dir = dA;   // A≈B:方向不动,只剩弧高起伏
        else dir = dA * (std::sin((1.0 - t) * theta) / sinT)
                 + dB * (std::sin(t * theta) / sinT);
        dir.normalize();
        // 半径 = 端点半径线性过渡 + 正弦隆起(t=0/1 时严格为 0 → 首尾贴地)
        double r = rA + (rB - rA) * t + heightScale * arcLen * std::sin(osg::PI * t);
        out.push_back(dir * r);
    }
    out.front() = eA; out.back() = eB;   // 首尾钉死为精确 ECEF(消除浮点噪声)
    return out;
}

std::vector<osg::Vec3d> buildPolylineVertices(const std::vector<osg::Vec2d>& latLonDeg,
                                              double liftMeters)
{
    std::vector<osg::Vec3d> out;
    if (latLonDeg.empty()) return out;
    const double kStepRad = osg::DegreesToRadians(1.0);   // 每 ~111km 细分一点,足够贴合大圆

    osg::Vec3d prev = lla2ecef(latLonDeg[0].x(), latLonDeg[0].y(), liftMeters);
    out.push_back(prev);
    for (size_t s = 1; s < latLonDeg.size(); ++s)
    {
        osg::Vec3d cur = lla2ecef(latLonDeg[s].x(), latLonDeg[s].y(), liftMeters);
        double rP = prev.length(), rC = cur.length();
        osg::Vec3d dP = prev / rP, dC = cur / rC;
        double cosT = osg::clampBetween(dP * dC, -1.0, 1.0);
        double theta = std::acos(cosT), sinT = std::sin(theta);
        int steps = (int)std::ceil(theta / kStepRad); if (steps < 1) steps = 1;
        for (int i = 1; i <= steps; ++i)
        {
            double t = (double)i / (double)steps;
            osg::Vec3d dir = (sinT < 1e-9) ? dP
                           : dP * (std::sin((1.0 - t) * theta) / sinT)
                           + dC * (std::sin(t * theta) / sinT);
            dir.normalize();
            // 贴地 = 每个细分点取"该方向上的局部椭球面半径"+抬升。端点半径线性插值是
            // 错的:跨赤道段(如 40°N→40°S)中段会掉到地表下 ~9km、翻越极侧的段会浮空
            // (评审修复)。径向抬升与沿法线抬升夹角 ≤~0.2°,liftMeters 量级下差 <0.1m。
            out.push_back(dir * (surfaceRadius(dir) + liftMeters));
        }
        out.back() = cur;   // 路标顶点钉死为精确 ECEF(段尾)
        prev = cur;
    }
    return out;
}

osg::Geometry* buildArcGeometry(const std::vector<ArcStrip>& arcs, osg::StateSet* externalSS)
{
    osg::Geometry* geom = createEmptyLineGeometry("GeoArc", externalSS);
    for (size_t i = 0; i < arcs.size(); ++i)
        appendLineStrip(geom, arcs[i].verts, arcs[i].colorO, arcs[i].colorD);
    return geom;
}

osg::Geometry* buildPolylineGeometry(const std::vector<osg::Vec3d>& verts,
                                     const osg::Vec4& color, osg::StateSet* externalSS)
{
    // 折线 = 渐变的退化(O==D,通体纯色);虚线样式等 P1 真有折线数据源时再加。
    osg::Geometry* geom = createEmptyLineGeometry("GeoPolyline", externalSS);
    appendLineStrip(geom, verts, color, color);
    return geom;
}

osg::Node* createArcDemoNode()
{
    // 弧端点抬 10km(防端点与地形 z-fight,视觉上仍"贴地");heightScale=0.12 隆起自然。
    const double kEndAlt = 10000.0, kArcH = 0.12; const int kArcN = 128;
    std::vector<ArcStrip> arcs(3);
    // 京→巴黎(中纬跨欧亚)
    arcs[0].verts = buildArcVertices(osg::Vec3d(39.9042, 116.4074, kEndAlt),
                                     osg::Vec3d(48.8566, 2.3522, kEndAlt), kArcN, kArcH);
    arcs[0].colorO = osg::Vec4(1.0f, 0.8f, 0.2f, 1.0f);    // 金
    arcs[0].colorD = osg::Vec4(0.2f, 0.9f, 1.0f, 1.0f);    // 青
    // 京→纽约(大圆翻高纬,验极区不破)
    arcs[1].verts = buildArcVertices(osg::Vec3d(39.9042, 116.4074, kEndAlt),
                                     osg::Vec3d(40.7128, -74.0060, kEndAlt), kArcN, kArcH);
    arcs[1].colorO = osg::Vec4(1.0f, 0.4f, 0.3f, 1.0f);    // 橘红
    arcs[1].colorD = osg::Vec4(0.6f, 0.4f, 1.0f, 1.0f);    // 紫
    // 上海→悉尼(跨赤道南下)
    arcs[2].verts = buildArcVertices(osg::Vec3d(31.2304, 121.4737, kEndAlt),
                                     osg::Vec3d(-33.8688, 151.2093, kEndAlt), kArcN, kArcH);
    arcs[2].colorO = osg::Vec4(0.3f, 1.0f, 0.5f, 1.0f);    // 绿
    arcs[2].colorD = osg::Vec4(1.0f, 0.5f, 0.9f, 1.0f);    // 粉

    // 折线:东京→香港→新加坡→孟买(贴地航路,抬 15km 防沿海地形 z-fight)
    std::vector<osg::Vec2d> wps;
    wps.push_back(osg::Vec2d(35.6762, 139.7649));
    wps.push_back(osg::Vec2d(22.3027, 114.1772));
    wps.push_back(osg::Vec2d(1.3521, 103.8198));
    wps.push_back(osg::Vec2d(19.0760, 72.8777));

    osg::Geode* geode = new osg::Geode;
    geode->setName("GeoPrimitivesDemo");
    geode->addDrawable(buildArcGeometry(arcs));
    geode->addDrawable(buildPolylineGeometry(buildPolylineVertices(wps, 15000.0),
                                             osg::Vec4(1.0f, 1.0f, 1.0f, 1.0f)));
    std::cout << "[GeoPrim] arc demo enabled: 3 arcs + 1 polyline" << std::endl;
    return geode;
}

osg::Vec4d mercatorTileBBox(int x, int yXYZ, int z)
{
    // 标准 Web Mercator 瓦片金字塔:z 级共 2^z × 2^z 格,原点(0,0)在西北角(左上);
    // 每格边长 = 全球米制宽 2×kM 除以 2^z。GEBCO WMS GetMap 首用(z>8 截断由调用方判)。
    const double kM = 20037508.342789244;
    double n = (double)(1 << z), size = 2.0 * kM / n;
    double xmin = -kM + x * size, ymax = kM - yXYZ * size;
    return osg::Vec4d(xmin, ymax - size, xmin + size, ymax);
}

} // namespace earthgeo
