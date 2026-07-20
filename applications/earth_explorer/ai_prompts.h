#ifndef EARTH_AI_PROMPTS_H
#define EARTH_AI_PROMPTS_H
// 生成式媒体的提示词工程(实景照片 + 巡航视频):header-only 纯函数,只依赖
// <osg/Vec3d>/<string>/<array>/<cstring>/<cmath>/<cstdio>,风格与 ai_motion.h 一致——方便
// tests/ai_chat_tests.cpp 直接 include 单测,不必拖 ai_media.cpp 的 osgViewer/libhv 重依赖。
//
// 背景(用户反馈 2):nano-banana-pro-preview 是带推理能力的图像模型,不是简单的
// "涂抹重绘"。把经纬度/高度这些精确元数据直接告诉它,先让它自己推理"这个坐标现实中
// 是什么地方、有什么地标/地形/植被/建筑",再把渲染图明确降级为"仅供构图/取景参考",
// 才能生成真正贴近现实的照片,而不是把三维引擎的合成质感原样描一遍。
// 英文提示词:Gemini 系列模型对英文指令的理解与遵循力最佳(与 ai_motion.h 的选择一致)。
#include <osg/Vec3d>
#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <cmath>
#include <cstdio>
#include "ai_photo_request.h"
// buildMotionPrompt 同样 header-only、只依赖 osg/Vec3d + string + cmath + cstdio(见该文件
// 头注释),依赖它不会把 ai_prompts.h 拖出"只依赖这四个头"的约束——直接复用其 A->B 轨迹
// 描述(罗盘方位/距离/高度变化),避免两处各写一份雷同的三角函数计算。
#include "ai_motion.h"

namespace earthai
{
    struct ScienceEarthPromptExample
    {
        const char* title;
        const char* sources;
        const char* prompt;
    };

    inline const std::array<ScienceEarthPromptExample, 10>&
    scienceEarthPromptExamples()
    {
        static const std::array<ScienceEarthPromptExample, 10> examples = {{
            {
                u8"香港：三源年度变化简报",
                u8"AlphaEarth + Sentinel-2 + Copernicus DEM",
                u8"研究香港当前视野 2017—2025 年的地表表征变化：先显示 AlphaEarth 伪彩，再找变化热点，用 Sentinel-2 影像和 Copernicus DEM 补充背景，最后给出带来源和局限的简报。"
            },
            {
                u8"深圳湾：两年潜在表征差异",
                u8"AlphaEarth",
                u8"比较深圳湾两侧 2018 与 2025 年的 AlphaEarth 表征差异，分别列出余弦距离、角距离和热点分位数；不要把 64 维分量解释成具体地物。"
            },
            {
                u8"东京：点位年度变化曲线",
                u8"AlphaEarth + Sentinel-2",
                u8"分析东京当前点位 2017—2025 的年度变化曲线，并说明哪一年变化最大；再找一景可用的 Sentinel-2 影像作为观测背景。"
            },
            {
                u8"富士山：变化、PCA、聚类与地形",
                u8"AlphaEarth + Copernicus DEM",
                u8"在富士山当前视野运行区域变化、PCA 和无监督聚类，再结合 Copernicus DEM 的高程证据讨论变化与地形的空间对应；明确这不是因果证明。"
            },
            {
                u8"悉尼海岸：覆盖与 NoData 核查",
                u8"AlphaEarth",
                u8"检查悉尼海岸当前科学图层的真实覆盖范围、NoData、年份和分辨率，显示伪彩并解释黄色边界与颜色分别代表什么。"
            },
            {
                u8"当前视野：可降级三源研究",
                u8"AlphaEarth + Sentinel-2 + Copernicus DEM",
                u8"为当前视野建立 AlphaEarth、Sentinel-2、Copernicus DEM 三源研究；任何一个源失败时也要生成诚实的部分结果和缺失项说明。"
            },
            {
                u8"变化热点：可复核证据输出",
                u8"AlphaEarth",
                u8"比较当前区域两个年份的变化热点，并输出可复核的来源、处理步骤、有效像元数、NoData 数和局限，不要只给结论。"
            },
            {
                u8"先盘点数据，再建议方法",
                u8"当前注册的全部 ScienceEarth 数据源",
                u8"先告诉我当前相机位置和可用科学数据源，再建议最适合这个区域的两种分析方法；等我选择后再提交，不要移动相机。"
            },
            {
                u8"解释本次 PCA、聚类与距离",
                u8"当前 AlphaEarth 结果",
                u8"解释当前 AlphaEarth 结果中的 PCA、聚类和余弦距离各回答什么问题、不能回答什么问题，并用本次结果里的数值举例。"
            },
            {
                u8"生成可审计研究简报",
                u8"当前研究中的全部证据源",
                u8"把当前研究整理成一份可审计简报：区分直接观测与跨数据源推断，逐条附证据编号、时间、覆盖范围和限制。"
            },
        }};
        return examples;
    }

    inline bool insertPromptSuggestion(
        const char* prompt, char* destination, std::size_t capacity)
    {
        if (!prompt || !destination || capacity == 0) return false;
        const std::size_t length = std::strlen(prompt);
        if (length >= capacity) return false;
        std::memcpy(destination, prompt, length + 1);
        return true;
    }

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

    inline std::string photoPromptNumber(double value, const char* format)
    {
        char buffer[48];
        snprintf(buffer, sizeof(buffer), format, value);
        return std::string(buffer);
    }

    // 实景照片提示词以快门时的不可变 PhotoCaptureRequest 为唯一几何事实源。目标地点、
    // 相机眼点、画面中心各自独立；可计算的姿态/视场/地面覆盖尺度由 view/projection/
    // viewport 派生。输入截图的几何必须被模型原样保留，只允许替换合成材质与标签。
    inline std::string buildPhotoPrompt(const PhotoCaptureRequest& capture)
    {
        static const double kRad2Deg = 57.29577951308232;
        const PhotoCameraContext& camera = capture.camera;
        const double eyeAltKm = camera.cameraEyeLla[2] / 1000.0;
        std::string p = "You are creating a real photograph. Treat this as a fresh independent generation: "
                        "do not reuse, continue, edit, or copy any previous generated photograph. "
                        "Camera eye (WGS84 ellipsoid): ";
        p += formatLatLonDeg(camera.cameraEyeLla[0] * kRad2Deg,
                             camera.cameraEyeLla[1] * kRad2Deg);
        p += ", altitude ";
        p += photoPromptNumber(eyeAltKm, "%.1f");
        p += " km above the WGS84 ellipsoid. ";
        if (camera.viewTargetValid)
        {
            p += "Center-of-frame view target: ";
            p += formatLatLonDeg(camera.viewTargetLla[0] * kRad2Deg,
                                 camera.viewTargetLla[1] * kRad2Deg);
            p += ". ";
        }
        p += "Intended geographic subject: ";
        p += formatLatLonDeg(capture.targetLla[0] * kRad2Deg,
                             capture.targetLla[1] * kRad2Deg);
        p += ". The intended subject identifies the requested region, but it must not override "
             "the current screenshot's camera geometry or center-of-frame composition. ";

        p += "Measured view geometry: ";
        if (camera.headingValid)
        {
            p += "heading ";
            p += photoPromptNumber(camera.headingDeg, "%.1f");
            p += " degrees clockwise from true north; ";
        }
        else p += "heading undefined because the optical axis is near nadir; ";
        if (camera.offNadirValid)
        {
            p += "off-nadir angle ";
            p += photoPromptNumber(camera.offNadirDeg, "%.1f");
            p += " degrees (0 is straight down); ";
        }
        if (camera.verticalFovValid)
        {
            p += "vertical FOV ";
            p += photoPromptNumber(camera.verticalFovDeg, "%.1f");
            p += " degrees; ";
        }
        if (camera.aspectRatioValid)
        {
            p += "aspect ratio ";
            p += photoPromptNumber(camera.aspectRatio, "%.3f");
            p += "; ";
        }
        if (camera.groundFootprintSpanValid)
        {
            p += "visible ground footprint maximum span approximately ";
            p += photoPromptNumber(camera.groundFootprintSpanKm, "%.1f");
            p += " km; ";
        }

        p += "HARD CAMERA-GEOMETRY LOCK: the attached current-render screenshot is authoritative "
             "for camera geometry, projection, framing, scale, perspective and composition. "
             "Do not move the camera closer or farther, raise or lower its altitude, zoom, crop, or reframe. "
             "Do not change heading, off-nadir angle, roll, vertical FOV, aspect ratio, horizon visibility, "
             "Earth curvature visibility, perspective, or visible ground footprint. Preserve exactly whether "
             "the horizon or Earth curvature is visible in the screenshot; never invent or remove either. "
             "Replace only the synthetic map texture, labels and rendering artifacts with physically plausible "
             "real-world appearance at the same scale and viewpoint. ";

        if (camera.cameraEyeLla[2] >= 100000.0)
        {
            p += "This is a spaceborne/high-altitude Earth-observation viewpoint, not drone or aircraft imagery. "
                 "Do not turn it into a low-altitude photograph: no foreground mountains, no near-camera clouds, "
                 "no building-level perspective, no low-altitude parallax, and no invented close-up detail. "
                 "Surface detail and atmospheric effects must remain physically plausible for the stated altitude "
                 "and visible ground footprint. ";
        }
        else if (camera.cameraEyeLla[2] >= 20000.0)
        {
            p += "This is a very-high-altitude Earth-observation viewpoint, not a low drone view. "
                 "Do not introduce foreground terrain, near-camera clouds, building-level perspective, "
                 "low-altitude parallax or a smaller ground footprint. ";
        }
        else
        {
            p += "Output a photorealistic aerial photograph while preserving the locked camera geometry. ";
        }

        p += "Reason about the real terrain, coastline, vegetation and architecture actually found in the "
             "visible region. Use true-to-life materials, water color, vegetation, atmospheric haze and natural "
             "lighting. Absolutely no UI elements, no text overlays, no watermarks, no map labels, no borders.";
        if (!capture.style.empty())
        {
            p += " Style: ";
            p += capture.style;
            p += ". This style applies to appearance only and must not override camera geometry.";
        }
        if (!capture.showCameraPlatform)
            p += " The camera position is only the viewpoint, not a subject: show no spacecraft, "
                 "no aircraft, no drone, no satellite, no solar panels, no window frame, and no "
                 "other camera-platform parts.";
        else
            p += " The user explicitly requested that the camera platform or vehicle be visible.";
        return p;
    }

    // 视频首帧等旧调用只有一个相机 LLA、没有完整快门矩阵。保留兼容入口；正式照片路径
    // 必须调用上面的 PhotoCaptureRequest 重载，不能退回这个缺少姿态的兼容分支。
    inline std::string buildPhotoPrompt(const osg::Vec3d& lla, const std::string& styleSuffix,
                                        bool showCameraPlatform = false)
    {
        PhotoRequest request;
        request.lla = lla;
        request.style = styleSuffix;
        request.showCameraPlatform = showCameraPlatform;
        PhotoCameraContext camera;
        camera.cameraEyeLla = lla;
        return buildPhotoPrompt(makePhotoCaptureRequest(request, camera, 0));
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
