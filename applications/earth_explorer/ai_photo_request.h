#ifndef EARTH_AI_PHOTO_REQUEST_H
#define EARTH_AI_PHOTO_REQUEST_H

#include <osg/Matrixd>
#include <osg/Vec3d>
#include <picojson.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace earthai
{
    inline bool isExplicitPhotoRequest(const std::string& text)
    {
        std::string lower = text;
        for (size_t i = 0; i < lower.size(); ++i)
            if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] - 'A' + 'a');

        static const char* kNegatedPhotoActions[] = {
            u8"不要拍", u8"别拍", u8"不拍照", u8"不用拍", u8"无需拍", u8"禁止拍",
            u8"照片不要拍", u8"图片不要生成", u8"不要生成照片", u8"不要生成图片",
            u8"别生成照片", u8"别生成图片", u8"不生成照片", u8"不生成图片",
            "no photo", "no picture", "no pictures", "no snapshot",
            "do not take a photo", "do not take a picture", "do not take a snapshot",
            "don't take a photo", "don't take a picture", "don't take a snapshot",
            "without taking a photo", "without taking a picture", "without taking a snapshot",
            "never take a photo", "never take a picture", "never take a snapshot",
            "avoid taking a photo", "avoid taking a picture", "avoid taking a snapshot",
            "do not generate a photo", "do not generate a picture", "do not generate an image",
            "don't generate a photo", "don't generate a picture", "don't generate an image"
        };
        for (size_t i = 0; i < sizeof(kNegatedPhotoActions) / sizeof(kNegatedPhotoActions[0]); ++i)
            if (lower.find(kNegatedPhotoActions[i]) != std::string::npos) return false;

        static const char* kPositive[] = {
            u8"拍照", u8"拍一张", u8"拍张", u8"生图", u8"生成图片", u8"生成照片", u8"实景照",
            "take a photo", "take a picture", "take a snapshot", "generate a photo",
            "generate a picture", "generate an image", "create a photo", "create an image",
            "photo please", "picture please"
        };
        for (size_t i = 0; i < sizeof(kPositive) / sizeof(kPositive[0]); ++i)
            if (lower.find(kPositive[i]) != std::string::npos) return true;
        return false;
    }

    inline bool isExplicitCameraPlatformRequest(const std::string& text)
    {
        std::string lower = text;
        for (size_t i = 0; i < lower.size(); ++i)
            if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] - 'A' + 'a');

        static const char* kPlatformTerms[] = {
            u8"显示太阳能板", u8"太阳能板出现在画面", u8"看见太阳能板", u8"带上太阳能板",
            u8"空间站出现在画面", u8"显示空间站", u8"看见空间站",
            u8"显示飞行器", u8"看见飞行器", "show the spacecraft",
            "include the spacecraft", "spacecraft visible", "show solar panels",
            "include solar panels", "solar panels visible", "show the iss", "show iss",
            "include the iss", "iss visible", "camera platform visible"
        };
        bool hasPlatformTerm = false;
        for (size_t i = 0; i < sizeof(kPlatformTerms) / sizeof(kPlatformTerms[0]); ++i)
            if (lower.find(kPlatformTerms[i]) != std::string::npos)
            { hasPlatformTerm = true; break; }
        if (!hasPlatformTerm) return false;

        static const char* kNegations[] = {
            u8"不要", u8"别", u8"无需", u8"不显示", u8"不出现", u8"不看见",
            "no ", "not ", "don't", "do not", "without", "hide "
        };
        for (size_t i = 0; i < sizeof(kNegations) / sizeof(kNegations[0]); ++i)
            if (lower.find(kNegations[i]) != std::string::npos) return false;
        return true;
    }

    inline bool isPhotoShutterCancellation(const std::string& text)
    {
        std::string lower = text;
        for (size_t i = 0; i < lower.size(); ++i)
            if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] - 'A' + 'a');
        static const char* kCancellations[] = {
            u8"先别拍", u8"不要拍", u8"还不能拍", u8"等一下", u8"不是这个视角",
            u8"别用这个视角", u8"这个视角不行", "don't take it", "do not take it",
            "don't shoot", "do not shoot", "not yet", "not this view", "not this angle",
            "wait"
        };
        for (size_t i = 0; i < sizeof(kCancellations) / sizeof(kCancellations[0]); ++i)
            if (lower.find(kCancellations[i]) != std::string::npos) return true;
        return false;
    }

    inline bool isPhotoShutterConfirmation(const std::string& text)
    {
        if (isPhotoShutterCancellation(text)) return false;
        std::string lower = text;
        for (size_t i = 0; i < lower.size(); ++i)
            if (lower[i] >= 'A' && lower[i] <= 'Z') lower[i] = (char)(lower[i] - 'A' + 'a');
        static const char* kConfirmations[] = {
            u8"现在拍吧", u8"拍吧", u8"可以拍了", u8"就这个视角", u8"按这个视角",
            u8"这个视角可以", u8"可以了", "shoot now", "take it", "use this view",
            "this view", "this angle", "looks good", "go ahead"
        };
        for (size_t i = 0; i < sizeof(kConfirmations) / sizeof(kConfirmations[0]); ++i)
            if (lower.find(kConfirmations[i]) != std::string::npos) return true;
        return false;
    }

    inline double photoSurfaceDistanceMeters(const osg::Vec3d& a, const osg::Vec3d& b)
    {
        const double dLat = b[0] - a[0];
        const double dLon = b[1] - a[1];
        const double sinLat = std::sin(dLat * 0.5);
        const double sinLon = std::sin(dLon * 0.5);
        double h = sinLat * sinLat + std::cos(a[0]) * std::cos(b[0]) * sinLon * sinLon;
        h = std::max(0.0, std::min(1.0, h));
        return 6378137.0 * 2.0 * std::asin(std::sqrt(h));
    }

    // 目标拍照是明确的两阶段流程：同一用户轮次里 fly_to 只负责把目标显示出来，
    // generate_photo 必须等下一条用户指令（用户已经看到并选择/确认当前视角）才能执行。
    class PhotoViewGate
    {
    public:
        void beginUserTurn(const std::string& userText)
        {
            ++_userTurn;
            const bool cancellation = isPhotoShutterCancellation(userText);
            const bool directPhoto = !cancellation && isExplicitPhotoRequest(userText);
            const bool confirmation = !cancellation && _pendingPhotoConfirmation
                && isPhotoShutterConfirmation(userText);
            const bool directPlatform = !cancellation
                && isExplicitCameraPlatformRequest(userText);
            _photoRequestedThisTurn = directPhoto || confirmation;
            _platformRequestedThisTurn = directPlatform
                || (confirmation && _pendingPlatformRequest);
            if (_pendingPhotoConfirmation && !directPhoto && !confirmation)
            {
                _pendingPhotoConfirmation = false;
                _pendingPlatformRequest = false;
            }
        }
        void recordFlyTo()
        {
            _lastFlyTurn = _userTurn;
            _flew = true;
            _pendingPhotoConfirmation = _photoRequestedThisTurn;
            _pendingPlatformRequest = _platformRequestedThisTurn;
        }
        bool flewThisTurn() const { return _flew && _lastFlyTurn == _userTurn; }
        bool photoRequestedThisTurn() const { return _photoRequestedThisTurn; }
        bool platformRequestedThisTurn() const { return _platformRequestedThisTurn; }
        void consumePhotoAuthorization()
        {
            _photoRequestedThisTurn = false;
            _platformRequestedThisTurn = false;
            _pendingPhotoConfirmation = false;
            _pendingPlatformRequest = false;
        }

    private:
        unsigned long long _userTurn = 0;
        unsigned long long _lastFlyTurn = 0;
        bool _flew = false;
        bool _photoRequestedThisTurn = false;
        bool _platformRequestedThisTurn = false;
        bool _pendingPhotoConfirmation = false;
        bool _pendingPlatformRequest = false;
    };

    inline bool photoCameraPlatformAllowed(const PhotoViewGate& gate, bool modelRequested)
    {
        return modelRequested && gate.platformRequestedThisTurn();
    }

    inline bool photoCaptureHasFreshView(unsigned int completedUpdateTicks)
    {
        return completedUpdateTicks > 0;
    }

    inline const char* photoCaptureGateError(bool animationRunning, const PhotoViewGate& gate,
                                             const osg::Vec3d& targetLla,
                                             const osg::Vec3d& currentEyeLla)
    {
        if (animationRunning) return "camera_flight_in_progress";
        if (!gate.photoRequestedThisTurn()) return "photo_not_requested";

        // 相机地面投影必须落在目标可见范围内。地面近景最低只给 5km，防止香港旧视角
        // 被当成 NVIDIA 总部视角；轨道视角按高度放宽到 2 倍，允许 ISS 倾斜构图时眼点
        // 地面投影偏离目标，最高仍封顶 1500km，避免无限放宽。
        const double eyeHeight = std::max(0.0, currentEyeLla[2]);
        const double visibleRadius = std::max(5000.0, std::min(1500000.0, eyeHeight * 2.0));
        if (photoSurfaceDistanceMeters(targetLla, currentEyeLla) > visibleRadius)
            return "photo_target_not_visible";
        if (gate.flewThisTurn()) return "photo_view_not_confirmed";
        return NULL;
    }

    inline double photoFlyAltitudeKm(const picojson::value& args)
    {
        if (args.contains("alt_km")) return args.get("alt_km").get<double>();
        const bool forPhoto = args.contains("for_photo") && args.get("for_photo").is<bool>()
            && args.get("for_photo").get<bool>();
        return forPhoto ? 2.0 : 150.0;
    }

    struct PhotoRequest
    {
        osg::Vec3d lla;
        std::string style;
        bool showCameraPlatform = false;
    };

    inline PhotoRequest photoRequestAtVisibleCameraAltitude(
        const PhotoRequest& request, const osg::Vec3d& currentEyeLla)
    {
        PhotoRequest visible = request;
        if (std::isfinite(currentEyeLla[2]) && currentEyeLla[2] > 0.0)
            visible.lla[2] = currentEyeLla[2];
        return visible;
    }

    struct PhotoCaptureRequest
    {
        osg::Vec3d targetLla;
        osg::Matrixd visibleCameraMatrix;
        std::string style;
        bool showCameraPlatform = false;
        long long requestId = 0;
    };

    inline PhotoCaptureRequest makePhotoCaptureRequest(
        const PhotoRequest& input, const osg::Matrixd& visibleMatrix, long long requestId)
    {
        PhotoCaptureRequest request;
        request.targetLla = input.lla;
        request.visibleCameraMatrix = visibleMatrix;
        request.style = input.style;
        request.showCameraPlatform = input.showCameraPlatform;
        request.requestId = requestId;
        return request;
    }

    inline std::string photoToolParametersJson()
    {
        return "{\"type\":\"object\",\"properties\":{"
               "\"lat\":{\"type\":\"number\",\"description\":\"本次照片目标纬度\"},"
               "\"lon\":{\"type\":\"number\",\"description\":\"本次照片目标经度\"},"
               "\"alt_km\":{\"type\":\"number\",\"description\":\"目标预期高度;最终以当前可见相机实际高度为准\"},"
               "\"style\":{\"type\":\"string\",\"description\":\"可选风格/时代/天气\"},"
               "\"show_camera_platform\":{\"type\":\"boolean\","
               "\"description\":\"仅当用户明确要求画面出现飞行器/空间站/太阳能板时为true;"
               "从ISS俯拍或ISS视角必须为false\"}},"
               "\"required\":[\"lat\",\"lon\"]}";
    }

    inline bool parsePhotoRequest(const picojson::value& args, PhotoRequest& request,
                                  std::string& error)
    {
        error.clear();
        if (!args.is<picojson::object>() || !args.contains("lat") || !args.contains("lon")
            || !args.get("lat").is<double>() || !args.get("lon").is<double>())
        {
            error = "need number lat/lon for this independent photo request";
            return false;
        }

        double lat = args.get("lat").get<double>();
        double lon = args.get("lon").get<double>();
        double altKm = 150.0;
        if (args.contains("alt_km"))
        {
            if (!args.get("alt_km").is<double>())
            { error = "alt_km must be a number"; return false; }
            altKm = args.get("alt_km").get<double>();
        }
        if (!std::isfinite(lat) || !std::isfinite(lon) || !std::isfinite(altKm)
            || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0 || altKm <= 0.0)
        {
            error = "out of range: lat[-90,90] lon[-180,180] alt_km>0";
            return false;
        }

        request = PhotoRequest();
        static const double kDegToRad = 0.017453292519943295;
        request.lla.set(lat * kDegToRad, lon * kDegToRad, altKm * 1000.0);
        if (args.contains("style"))
        {
            if (!args.get("style").is<std::string>())
            { error = "style must be a string"; return false; }
            request.style = args.get("style").get<std::string>();
        }
        if (args.contains("show_camera_platform"))
        {
            if (!args.get("show_camera_platform").is<bool>())
            { error = "show_camera_platform must be a boolean"; return false; }
            request.showCameraPlatform = args.get("show_camera_platform").get<bool>();
        }
        return true;
    }
}

#endif
