#ifndef EARTH_AI_PROMPTS_H
#define EARTH_AI_PROMPTS_H
// 生成式媒体的提示词工程(实景照片 + 巡航视频):header-only 纯函数,只依赖
// <osg/Vec3d>/<string>/<cmath>/<cstdio>,风格与 ai_motion.h 一致——方便
// tests/ai_chat_tests.cpp 直接 include 单测,不必拖 ai_media.cpp 的 osgViewer/libhv 重依赖。
//
// 背景(用户反馈 2):nano-banana-pro-preview 是带推理能力的图像模型,不是简单的
// "涂抹重绘"。把经纬度/高度这些精确元数据直接告诉它,先让它自己推理"这个坐标现实中
// 是什么地方、有什么地标/地形/植被/建筑",再把渲染图明确降级为"仅供构图/取景参考",
// 才能生成真正贴近现实的照片,而不是把三维引擎的合成质感原样描一遍。
// 英文提示词:Gemini 系列模型对英文指令的理解与遵循力最佳(与 ai_motion.h 的选择一致)。
#include <osg/Vec3d>
#include <string>
#include <cmath>
#include <cstdio>
// buildMotionPrompt 同样 header-only、只依赖 osg/Vec3d + string + cmath + cstdio(见该文件
// 头注释),依赖它不会把 ai_prompts.h 拖出"只依赖这四个头"的约束——直接复用其 A->B 轨迹
// 描述(罗盘方位/距离/高度变化),避免两处各写一份雷同的三角函数计算。
#include "ai_motion.h"

namespace earthai
{
    // 经纬度格式化:带正负号的十进制度 + N/S/E/W 半球字母,例如 "22.2980°N, 114.1720°E"。
    // lat/lon 单位为度(调用方负责把弧度转成度——与 ai_motion.h 里"内部只吃弧度"的约定
    // 相反,这里刻意先转好度再传入,因为格式化本身就是给模型/用户看的文本,弧度不直观)。
    inline std::string formatLatLonDeg(double latDeg, double lonDeg)
    {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.4f\xC2\xB0%c, %.4f\xC2\xB0%c",
                 std::fabs(latDeg), (latDeg >= 0.0 ? 'N' : 'S'),
                 std::fabs(lonDeg), (lonDeg >= 0.0 ? 'E' : 'W'));
        return std::string(buf);
    }

    // 实景照片提示词:banana pro 是带推理的图像模型——先让它推理该经纬度是什么真实地方,
    // 再以渲染图为构图参考生成真实照片。英文提示词(模型对 EN 支持最佳)。
    // lla = (纬度弧度, 经度弧度, 高度米),与 EarthManipulator::computeEyeLatLonHeight()
    // 的返回值约定一致(调用方直接把相机位姿传进来,本函数内部负责弧度转角度)。
    inline std::string buildPhotoPrompt(const osg::Vec3d& lla, const std::string& styleSuffix,
                                        bool showCameraPlatform = false)
    {
        const double kRad2Deg = 57.29577951308232;
        double latDeg = lla[0] * kRad2Deg;
        double lonDeg = lla[1] * kRad2Deg;
        double altKm = lla[2] / 1000.0;

        char altBuf[32];
        snprintf(altBuf, sizeof(altBuf), "%.1f", altKm);

        std::string p = "You are creating a real photograph. Treat this as a fresh independent generation: "
                        "do not reuse, continue, edit, or copy any previous generated photograph. "
                        "Camera position: latitude ";
        p += formatLatLonDeg(latDeg, lonDeg);
        p += ", altitude ";
        p += altBuf;
        p += " km above ground, aerial view. First, reason step by step about what real "
             "place this is \xE2\x80\x94 the city, landmarks, terrain, coastline, vegetation "
             "and architecture actually found at these coordinates \xE2\x80\x94 and what they "
             "look like from this altitude. The attached rendered image is ONLY a composition "
             "and terrain-layout reference: match its framing, scale and viewpoint, but replace "
             "its synthetic appearance with the real world. Output a photorealistic aerial "
             "photograph: true-to-life landmarks, materials, water color, vegetation, "
             "atmospheric haze, natural lighting with soft shadows. Absolutely no UI elements, "
             "no text overlays, no watermarks, no map labels, no borders.";
        if (!styleSuffix.empty()) { p += " Style: "; p += styleSuffix; }
        if (!showCameraPlatform)
            p += " The camera position is only the viewpoint, not a subject: show no spacecraft, "
                 "no aircraft, no drone, no satellite, no solar panels, no window frame, and no "
                 "other camera-platform parts.";
        else
            p += " The user explicitly requested that the camera platform or vehicle be visible.";
        return p;
    }

    // 巡航视频提示词:起始帧是"已生成的实景照片"(而非原始渲染截图);含地理上下文 +
    // A->B 轨迹(直接复用 buildMotionPrompt 的罗盘方位/距离/高度变化描述句子,不重复实现
    // 一遍三角函数)+ 电影感运镜语言。
    inline std::string buildVideoPrompt(const osg::Vec3d& llaA, const osg::Vec3d& llaB,
                                        const std::string& styleSuffix)
    {
        const double kRad2Deg = 57.29577951308232;
        double latDeg = llaA[0] * kRad2Deg;
        double lonDeg = llaA[1] * kRad2Deg;
        double altKm = llaA[2] / 1000.0;

        char altBuf[32];
        snprintf(altBuf, sizeof(altBuf), "%.1f", altKm);

        std::string trajectorySentence = buildMotionPrompt(llaA, llaB);

        std::string p = "Cinematic aerial drone footage starting exactly from the provided "
                        "photograph (first frame). Location: ";
        p += formatLatLonDeg(latDeg, lonDeg);
        p += ", altitude ";
        p += altBuf;
        p += " km \xE2\x80\x94 reason about the real landscape there and keep it consistent "
             "while the camera moves. Camera motion: ";
        p += trajectorySentence;
        p += " 6-8 seconds, smooth stabilized glide, gentle parallax, photorealistic, "
             "natural motion blur, no text, no UI, no watermarks.";
        if (!styleSuffix.empty()) { p += " Style: "; p += styleSuffix; }
        return p;
    }
}
#endif
