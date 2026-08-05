#ifndef EARTH_AI_CINEMATIC_REQUEST_H
#define EARTH_AI_CINEMATIC_REQUEST_H

// 时空影像工作台的纯值类型合同。这里只描述一次生成“是什么”，不访问相机、不改操纵器、
// 不做网络请求。正式请求必须携带快门边界冻结的 PhotoCaptureRequest，worker 随后只复制
// 这个值，避免 UI、相机或上一张生成结果在异步执行期间串入本次任务。
#include "ai_photo_request.h"
#include <array>
#include <cmath>
#include <string>

namespace earthai
{
    enum CinematicMediaKind
    {
        CINEMATIC_IMAGE = 0,
        CINEMATIC_VIDEO
    };

    enum CinematicEra
    {
        CINEMATIC_ERA_PRESENT = 0,
        CINEMATIC_ERA_1920S,
        CINEMATIC_ERA_CAMBRIAN_CHENGJIANG,
        CINEMATIC_ERA_CUSTOM
    };

    enum CinematicLocalTime
    {
        CINEMATIC_TIME_AUTO = 0,
        CINEMATIC_TIME_DAWN,
        CINEMATIC_TIME_NOON,
        CINEMATIC_TIME_1900,
        CINEMATIC_TIME_NIGHT,
        CINEMATIC_TIME_CUSTOM
    };

    enum CinematicVisualStyle
    {
        CINEMATIC_STYLE_SCIENTIFIC = 0,
        CINEMATIC_STYLE_ULTRA_REAL,
        CINEMATIC_STYLE_ARCHIVAL_AMBER,
        CINEMATIC_STYLE_DOCUMENTARY,
        CINEMATIC_STYLE_CINEMATIC,
        CINEMATIC_STYLE_ANIME,
        CINEMATIC_STYLE_CUSTOM
    };

    enum CinematicCameraMotion
    {
        CINEMATIC_MOTION_STATIC = 0,
        CINEMATIC_MOTION_AERIAL_TOUR,
        CINEMATIC_MOTION_ORBIT_360,
        CINEMATIC_MOTION_DIVE,
        CINEMATIC_MOTION_CRANE_REVEAL,
        CINEMATIC_MOTION_TRUCK,
        CINEMATIC_MOTION_POINT_TO_POINT
    };

    struct CinematicGenerationSettings
    {
        CinematicMediaKind mediaKind = CINEMATIC_IMAGE;
        CinematicEra era = CINEMATIC_ERA_PRESENT;
        CinematicLocalTime localTime = CINEMATIC_TIME_AUTO;
        CinematicVisualStyle visualStyle = CINEMATIC_STYLE_SCIENTIFIC;
        CinematicCameraMotion motion = CINEMATIC_MOTION_STATIC;
        std::string customEra;
        std::string customLocalTime;
        std::string customStyle;
        std::string userPrompt;
        int durationSeconds = 8;
        // Omni video currently generates audio by default. Video presets explicitly enable
        // restrained ambience; false remains a best-effort prompt until deterministic
        // post-processing can strip the returned audio stream.
        bool includeGeneratedAudio = false;
    };

    struct CinematicGenerationRequest
    {
        PhotoCaptureRequest anchor;
        CinematicGenerationSettings settings;
        PhotoCaptureRequest endAnchor;
        bool hasEndAnchor = false;

        // 默认永远为空。只有未来明确的“继续编辑上一张”流程才允许填写；当前工作台没有
        // 这个入口，因此每次生成天然独立，不可能隐式沿用旧香港图、旧卫星或旧构图。
        std::string previousArtifactId;
    };

    struct CinematicImageOutputOptions
    {
        std::string aspectRatio = "16:9";
        std::string imageSize = "2K";
    };

    struct CinematicVideoOutputOptions
    {
        std::string aspectRatio = "16:9";
        int durationSeconds = 8;
    };

    inline CinematicGenerationSettings defaultImageCinematicSettings()
    {
        CinematicGenerationSettings settings;
        settings.mediaKind = CINEMATIC_IMAGE;
        settings.era = CINEMATIC_ERA_PRESENT;
        settings.localTime = CINEMATIC_TIME_AUTO;
        settings.visualStyle = CINEMATIC_STYLE_SCIENTIFIC;
        settings.motion = CINEMATIC_MOTION_STATIC;
        return settings;
    }

    inline CinematicGenerationSettings defaultVideoCinematicSettings()
    {
        CinematicGenerationSettings settings;
        settings.mediaKind = CINEMATIC_VIDEO;
        settings.era = CINEMATIC_ERA_PRESENT;
        settings.localTime = CINEMATIC_TIME_AUTO;
        settings.visualStyle = CINEMATIC_STYLE_ULTRA_REAL;
        settings.motion = CINEMATIC_MOTION_AERIAL_TOUR;
        settings.durationSeconds = 8;
        settings.includeGeneratedAudio = true;
        return settings;
    }

    inline bool cinematicMotionNeedsEndFrame(CinematicCameraMotion motion)
    {
        return motion == CINEMATIC_MOTION_POINT_TO_POINT;
    }

    inline int cinematicMinimumDurationSeconds(CinematicCameraMotion motion)
    {
        // Paid Gemini validation on 2026-08-05 found that a short orbit can return a
        // valid MP4 while only performing a push-in or partial arc. Eight seconds is
        // still not a guarantee, but it is the shortest product duration at which a
        // real four-quadrant orbit completed in repeated concrete-location tests.
        return motion == CINEMATIC_MOTION_ORBIT_360 ? 8 : 4;
    }

    inline bool cinematicMotionRequiresClosureReview(CinematicCameraMotion motion)
    {
        return motion == CINEMATIC_MOTION_ORBIT_360;
    }

    inline bool cinematicMotionUsesDeterministicLocalRenderer(
        CinematicCameraMotion motion)
    {
        return motion == CINEMATIC_MOTION_ORBIT_360;
    }

    inline bool cinematicMotionProductionReady(CinematicCameraMotion motion)
    {
        // A strict paid playback review on 2026-08-05 found multiple shot changes in an
        // Omni-generated "360 orbit" even though the request explicitly prohibited cuts.
        // Provider generation must stay blocked for this motion.  The application-owned
        // deterministic renderer is selected separately and does not call a provider.
        return motion != CINEMATIC_MOTION_ORBIT_360;
    }

    inline bool cinematicSubmissionCanStart(
        const CinematicGenerationSettings& settings, bool mediaAvailable,
        bool providerAvailable)
    {
        if (!mediaAvailable) return false;

        if (settings.mediaKind == CINEMATIC_IMAGE)
            return settings.motion == CINEMATIC_MOTION_STATIC && providerAvailable;

        const bool localRenderer =
            cinematicMotionUsesDeterministicLocalRenderer(settings.motion);
        if (localRenderer) return true;
        return cinematicMotionProductionReady(settings.motion) && providerAvailable;
    }

    inline CinematicGenerationSettings normalizedCinematicSubmissionSettings(
        const CinematicGenerationSettings& settings)
    {
        CinematicGenerationSettings normalized = settings;
        if (cinematicMotionUsesDeterministicLocalRenderer(normalized.motion))
        {
            // A local orbit records the visible osgSol scene exactly as it is now.  It
            // cannot truthfully apply a historical era, time override, restyle, prompt,
            // or generated audio, so keep the submitted value contract explicit.
            normalized.era = CINEMATIC_ERA_PRESENT;
            normalized.localTime = CINEMATIC_TIME_AUTO;
            normalized.visualStyle = CINEMATIC_STYLE_SCIENTIFIC;
            normalized.durationSeconds = 8;
            normalized.includeGeneratedAudio = false;
            normalized.customEra.clear();
            normalized.customLocalTime.clear();
            normalized.customStyle.clear();
            normalized.userPrompt.clear();
        }
        return normalized;
    }

    inline std::string cinematicImageModelName(const char* configuredModel)
    {
        return (configuredModel && *configuredModel)
            ? std::string(configuredModel)
            : std::string("gemini-3.1-flash-image");
    }

    inline std::string closestCinematicAspectRatio(double aspect)
    {
        struct Candidate { const char* label; double value; };
        static const std::array<Candidate, 10> candidates = {{
            { "1:1", 1.0 }, { "2:3", 2.0 / 3.0 }, { "3:2", 3.0 / 2.0 },
            { "3:4", 3.0 / 4.0 }, { "4:3", 4.0 / 3.0 },
            { "4:5", 4.0 / 5.0 }, { "5:4", 5.0 / 4.0 },
            { "9:16", 9.0 / 16.0 }, { "16:9", 16.0 / 9.0 },
            { "21:9", 21.0 / 9.0 }
        }};
        if (!(aspect > 0.0) || !std::isfinite(aspect)) return "16:9";
        std::size_t best = 0;
        double bestDistance = std::numeric_limits<double>::infinity();
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            const double distance = std::fabs(
                std::log(aspect) - std::log(candidates[index].value));
            if (distance < bestDistance)
            { bestDistance = distance; best = index; }
        }
        return candidates[best].label;
    }

    inline CinematicImageOutputOptions cinematicImageOutputOptions(
        const CinematicGenerationRequest& request)
    {
        CinematicImageOutputOptions options;
        if (request.anchor.camera.aspectRatioValid)
            options.aspectRatio = closestCinematicAspectRatio(
                request.anchor.camera.aspectRatio);
        else if (request.anchor.camera.viewportWidth > 0 &&
                 request.anchor.camera.viewportHeight > 0)
            options.aspectRatio = closestCinematicAspectRatio(
                static_cast<double>(request.anchor.camera.viewportWidth) /
                static_cast<double>(request.anchor.camera.viewportHeight));
        // 2K 是工作台默认：足以保留地理细节，同时避免所有预览都强制走 4K 成本。
        // 未来增加显式高质量开关时再按用户选择升到 4K，不能悄悄计费。
        options.imageSize = "2K";
        return options;
    }

    inline picojson::object cinematicGeminiImageGenerationConfig(
        const CinematicImageOutputOptions& options)
    {
        picojson::array modalities;
        modalities.push_back(picojson::value(std::string("TEXT")));
        modalities.push_back(picojson::value(std::string("IMAGE")));
        picojson::object imageConfig;
        imageConfig["aspectRatio"] = picojson::value(options.aspectRatio);
        imageConfig["imageSize"] = picojson::value(options.imageSize);
        picojson::object config;
        config["responseModalities"] = picojson::value(modalities);
        config["imageConfig"] = picojson::value(imageConfig);
        return config;
    }

    inline CinematicVideoOutputOptions cinematicVideoOutputOptions(
        const CinematicGenerationRequest& request)
    {
        CinematicVideoOutputOptions options;
        double aspect = request.anchor.camera.aspectRatio;
        if (!(request.anchor.camera.aspectRatioValid && aspect > 0.0) &&
            request.anchor.camera.viewportWidth > 0 &&
            request.anchor.camera.viewportHeight > 0)
        {
            aspect = static_cast<double>(request.anchor.camera.viewportWidth) /
                static_cast<double>(request.anchor.camera.viewportHeight);
        }
        options.aspectRatio = aspect > 0.0 && aspect < 1.0 ? "9:16" : "16:9";
        options.durationSeconds = request.settings.durationSeconds;
        return options;
    }

    inline picojson::object cinematicGeminiVideoResponseFormat(
        const CinematicVideoOutputOptions& options)
    {
        picojson::object format;
        format["type"] = picojson::value(std::string("video"));
        format["aspect_ratio"] = picojson::value(options.aspectRatio);
        format["duration"] = picojson::value(
            std::to_string(options.durationSeconds) + "s");
        return format;
    }

    inline picojson::object cinematicGeminiImageToVideoConfig()
    {
        picojson::object videoConfig;
        videoConfig["task"] = picojson::value(std::string("image_to_video"));
        picojson::object config;
        config["video_config"] = picojson::value(videoConfig);
        return config;
    }

    inline bool cinematicCaptureIsUsable(const PhotoCaptureRequest& capture)
    {
        return capture.requestId > 0 && capture.camera.viewportWidth > 0
            && capture.camera.viewportHeight > 0;
    }

    inline bool makeCinematicGenerationRequest(
        const PhotoCaptureRequest& anchor,
        const CinematicGenerationSettings& settings,
        CinematicGenerationRequest& output)
    {
        if (!cinematicCaptureIsUsable(anchor)) return false;
        const CinematicGenerationSettings normalized =
            normalizedCinematicSubmissionSettings(settings);
        if (normalized.mediaKind == CINEMATIC_IMAGE &&
            normalized.motion != CINEMATIC_MOTION_STATIC)
            return false;
        if (normalized.mediaKind == CINEMATIC_VIDEO &&
            !cinematicMotionProductionReady(normalized.motion) &&
            !cinematicMotionUsesDeterministicLocalRenderer(normalized.motion))
            return false;
        if (normalized.durationSeconds <
                cinematicMinimumDurationSeconds(normalized.motion) ||
            normalized.durationSeconds > 30)
            return false;

        output = CinematicGenerationRequest();
        output.anchor = anchor;
        output.settings = normalized;
        return true;
    }

    inline CinematicGenerationRequest cinematicRequestUnchecked(
        const PhotoCaptureRequest& anchor,
        const CinematicGenerationSettings& settings)
    {
        CinematicGenerationRequest request;
        request.anchor = anchor;
        request.settings = settings;
        return request;
    }

    inline const char* cinematicEraLabel(CinematicEra era)
    {
        switch (era)
        {
        case CINEMATIC_ERA_PRESENT: return u8"当代";
        case CINEMATIC_ERA_1920S: return u8"1920 年";
        case CINEMATIC_ERA_CAMBRIAN_CHENGJIANG: return u8"寒武纪·澄江生物群";
        case CINEMATIC_ERA_CUSTOM: return u8"自定义时代";
        }
        return u8"当代";
    }

    inline const char* cinematicTimeLabel(CinematicLocalTime time)
    {
        switch (time)
        {
        case CINEMATIC_TIME_AUTO: return u8"依当前光照";
        case CINEMATIC_TIME_DAWN: return u8"黎明";
        case CINEMATIC_TIME_NOON: return u8"正午";
        case CINEMATIC_TIME_1900: return u8"19:00";
        case CINEMATIC_TIME_NIGHT: return u8"深夜";
        case CINEMATIC_TIME_CUSTOM: return u8"自定义时间";
        }
        return u8"依当前光照";
    }

    inline const char* cinematicStyleLabel(CinematicVisualStyle style)
    {
        switch (style)
        {
        case CINEMATIC_STYLE_SCIENTIFIC: return u8"科学写实";
        case CINEMATIC_STYLE_ULTRA_REAL: return u8"超写实";
        case CINEMATIC_STYLE_ARCHIVAL_AMBER: return u8"泛黄档案";
        case CINEMATIC_STYLE_DOCUMENTARY: return u8"纪录片";
        case CINEMATIC_STYLE_CINEMATIC: return u8"电影质感";
        case CINEMATIC_STYLE_ANIME: return u8"动漫";
        case CINEMATIC_STYLE_CUSTOM: return u8"自定义风格";
        }
        return u8"科学写实";
    }

    inline const char* cinematicMotionLabel(CinematicCameraMotion motion)
    {
        switch (motion)
        {
        case CINEMATIC_MOTION_STATIC: return u8"静态图像";
        case CINEMATIC_MOTION_AERIAL_TOUR: return u8"一镜到底航拍";
        case CINEMATIC_MOTION_ORBIT_360: return u8"本地渲染 360° 一镜到底环拍";
        case CINEMATIC_MOTION_DIVE: return u8"俯冲拍摄";
        case CINEMATIC_MOTION_CRANE_REVEAL: return u8"升降揭示";
        case CINEMATIC_MOTION_TRUCK: return u8"平行横移";
        case CINEMATIC_MOTION_POINT_TO_POINT: return u8"两点穿越";
        }
        return u8"静态图像";
    }
}

#endif
