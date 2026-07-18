#ifndef EARTH_AI_PHOTO_REQUEST_H
#define EARTH_AI_PHOTO_REQUEST_H

#include <osg/Matrixd>
#include <osg/Vec3d>
#include <picojson.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

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

    struct PhotoCameraContext
    {
        osg::Vec3d cameraEyeLla;
        osg::Vec3d viewTargetLla;
        osg::Matrixd visibleViewMatrix;
        osg::Matrixd visibleProjectionMatrix;
        int viewportWidth = 0;
        int viewportHeight = 0;

        double headingDeg = std::numeric_limits<double>::quiet_NaN();
        double offNadirDeg = std::numeric_limits<double>::quiet_NaN();
        double verticalFovDeg = std::numeric_limits<double>::quiet_NaN();
        double aspectRatio = std::numeric_limits<double>::quiet_NaN();
        double groundFootprintSpanKm = std::numeric_limits<double>::quiet_NaN();
        bool headingValid = false;
        bool viewTargetValid = false;
        bool offNadirValid = false;
        bool verticalFovValid = false;
        bool aspectRatioValid = false;
        bool groundFootprintSpanValid = false;
    };

    inline osg::Vec3d photoLlaToEcef(const osg::Vec3d& lla)
    {
        static const double kA = 6378137.0;
        static const double kB = 6356752.3142451793;
        const double sinLat = std::sin(lla[0]);
        const double cosLat = std::cos(lla[0]);
        const double sinLon = std::sin(lla[1]);
        const double cosLon = std::cos(lla[1]);
        const double e2 = 1.0 - (kB * kB) / (kA * kA);
        const double n = kA / std::sqrt(1.0 - e2 * sinLat * sinLat);
        return osg::Vec3d((n + lla[2]) * cosLat * cosLon,
                          (n + lla[2]) * cosLat * sinLon,
                          ((1.0 - e2) * n + lla[2]) * sinLat);
    }

    inline bool photoIntersectWgs84(const osg::Vec3d& origin, const osg::Vec3d& direction,
                                    osg::Vec3d& intersectionLla)
    {
        static const double kA = 6378137.0;
        static const double kB = 6356752.3142451793;
        const double a2 = kA * kA, b2 = kB * kB;
        const double qa = (direction[0] * direction[0] + direction[1] * direction[1]) / a2
                        + direction[2] * direction[2] / b2;
        const double qb = 2.0 * ((origin[0] * direction[0] + origin[1] * direction[1]) / a2
                        + origin[2] * direction[2] / b2);
        const double qc = (origin[0] * origin[0] + origin[1] * origin[1]) / a2
                        + origin[2] * origin[2] / b2 - 1.0;
        const double discriminant = qb * qb - 4.0 * qa * qc;
        if (!(qa > 0.0) || discriminant < 0.0) return false;
        const double root = std::sqrt(discriminant);
        const double t0 = (-qb - root) / (2.0 * qa);
        const double t1 = (-qb + root) / (2.0 * qa);
        double t = std::numeric_limits<double>::infinity();
        if (t0 > 0.0) t = t0;
        if (t1 > 0.0 && t1 < t) t = t1;
        if (!std::isfinite(t)) return false;

        const osg::Vec3d p = origin + direction * t;
        const double horizontalNormal = std::sqrt(
            (p[0] / a2) * (p[0] / a2) + (p[1] / a2) * (p[1] / a2));
        intersectionLla.set(std::atan2(p[2] / b2, horizontalNormal),
                            std::atan2(p[1], p[0]), 0.0);
        return true;
    }

    inline PhotoCameraContext makePhotoCameraContext(
        const osg::Vec3d& cameraEyeLla, const osg::Vec3d& viewTargetLla,
        const osg::Matrixd& visibleViewMatrix,
        const osg::Matrixd& visibleProjectionMatrix,
        int viewportWidth, int viewportHeight)
    {
        PhotoCameraContext context;
        context.cameraEyeLla = cameraEyeLla;
        context.viewTargetLla = viewTargetLla;
        context.viewTargetValid = true;
        context.visibleViewMatrix = visibleViewMatrix;
        context.visibleProjectionMatrix = visibleProjectionMatrix;
        context.viewportWidth = viewportWidth;
        context.viewportHeight = viewportHeight;

        osg::Vec3d eyeWorld, lookAtWorld, cameraUp;
        visibleViewMatrix.getLookAt(eyeWorld, lookAtWorld, cameraUp, 1.0);
        osg::Vec3d look = lookAtWorld - eyeWorld;
        if (look.normalize() > 0.0)
        {
            const double lat = cameraEyeLla[0], lon = cameraEyeLla[1];
            osg::Vec3d localUp(std::cos(lat) * std::cos(lon),
                               std::cos(lat) * std::sin(lon), std::sin(lat));
            localUp.normalize();
            const osg::Vec3d nadir = -localUp;
            const double nadirDot = std::max(-1.0, std::min(1.0, look * nadir));
            context.offNadirDeg = std::acos(nadirDot) * 57.29577951308232;
            context.offNadirValid = std::isfinite(context.offNadirDeg);

            osg::Vec3d horizontal = look - localUp * (look * localUp);
            if (horizontal.normalize() > 1.0e-9)
            {
                const osg::Vec3d east(-std::sin(lon), std::cos(lon), 0.0);
                const osg::Vec3d north(-std::sin(lat) * std::cos(lon),
                                       -std::sin(lat) * std::sin(lon), std::cos(lat));
                context.headingDeg = std::atan2(horizontal * east, horizontal * north)
                                   * 57.29577951308232;
                if (context.headingDeg < 0.0) context.headingDeg += 360.0;
                context.headingValid = std::isfinite(context.headingDeg);
            }
        }

        double projectionAspect = 0.0, zNear = 0.0, zFar = 0.0;
        context.verticalFovValid = visibleProjectionMatrix.getPerspective(
            context.verticalFovDeg, projectionAspect, zNear, zFar)
            && std::isfinite(context.verticalFovDeg) && context.verticalFovDeg > 0.0;
        if (viewportWidth > 0 && viewportHeight > 0)
        {
            context.aspectRatio = (double)viewportWidth / (double)viewportHeight;
            context.aspectRatioValid = std::isfinite(context.aspectRatio)
                                    && context.aspectRatio > 0.0;
        }
        else if (projectionAspect > 0.0 && std::isfinite(projectionAspect))
        {
            context.aspectRatio = projectionAspect;
            context.aspectRatioValid = true;
        }

        osg::Matrixd inverseViewProjection;
        const osg::Matrixd viewProjection = visibleViewMatrix * visibleProjectionMatrix;
        if (inverseViewProjection.invert(viewProjection))
        {
            const osg::Vec3d eyeEcef = photoLlaToEcef(cameraEyeLla);
            const osg::Vec3d worldOffset = eyeWorld - eyeEcef;
            static const double kCorners[4][2] = {
                { -1.0, -1.0 }, { 1.0, -1.0 }, { 1.0, 1.0 }, { -1.0, 1.0 }
            };
            std::vector<osg::Vec3d> cornerLlas;
            for (size_t i = 0; i < 4; ++i)
            {
                const osg::Vec3d farWorld = osg::Vec3d(
                    kCorners[i][0], kCorners[i][1], 1.0) * inverseViewProjection;
                osg::Vec3d direction = farWorld - eyeWorld;
                if (direction.normalize() <= 0.0) continue;
                osg::Vec3d hitLla;
                if (photoIntersectWgs84(eyeWorld - worldOffset, direction, hitLla))
                    cornerLlas.push_back(hitLla);
            }
            if (cornerLlas.size() == 4)
            {
                double maxDistanceM = 0.0;
                for (size_t i = 0; i < cornerLlas.size(); ++i)
                    for (size_t j = i + 1; j < cornerLlas.size(); ++j)
                        maxDistanceM = std::max(maxDistanceM,
                            photoSurfaceDistanceMeters(cornerLlas[i], cornerLlas[j]));
                if (maxDistanceM > 0.0 && std::isfinite(maxDistanceM))
                {
                    context.groundFootprintSpanKm = maxDistanceM / 1000.0;
                    context.groundFootprintSpanValid = true;
                }
            }
        }
        return context;
    }

    inline bool photoTargetVisibleInCameraContext(
        const osg::Vec3d& targetLla,
        const PhotoCameraContext& camera)
    {
        osg::Vec3d eyeWorld, lookAtWorld, cameraUp;
        camera.visibleViewMatrix.getLookAt(
            eyeWorld, lookAtWorld, cameraUp, 1.0);
        const osg::Vec3d eyeEcef = photoLlaToEcef(camera.cameraEyeLla);
        const osg::Vec3d worldOffset = eyeWorld - eyeEcef;
        osg::Vec3d targetSurfaceLla = targetLla;
        targetSurfaceLla[2] = 0.0;
        const osg::Vec3d targetWorld =
            photoLlaToEcef(targetSurfaceLla) + worldOffset;
        const osg::Vec4d clip = osg::Vec4d(
            targetWorld[0], targetWorld[1], targetWorld[2], 1.0) *
            (camera.visibleViewMatrix * camera.visibleProjectionMatrix);
        if (!std::isfinite(clip[0]) || !std::isfinite(clip[1]) ||
            !std::isfinite(clip[2]) || !std::isfinite(clip[3]) ||
            clip[3] <= 1.0e-12)
            return false;
        const double x = clip[0] / clip[3];
        const double y = clip[1] / clip[3];
        const double z = clip[2] / clip[3];
        static const double kEdgeTolerance = 1.02;
        return std::abs(x) <= kEdgeTolerance &&
               std::abs(y) <= kEdgeTolerance &&
               z >= -1.0 && z <= 1.0;
    }

    inline bool photoCameraContextsMatchFrame(
        const PhotoCameraContext& expected,
        const PhotoCameraContext& current)
    {
        if (expected.viewportWidth != current.viewportWidth ||
            expected.viewportHeight != current.viewportHeight)
            return false;
        static const double kMatrixTolerance = 1.0e-10;
        for (int row = 0; row < 4; ++row)
        {
            for (int column = 0; column < 4; ++column)
            {
                if (std::abs(expected.visibleViewMatrix(row, column) -
                             current.visibleViewMatrix(row, column)) >
                        kMatrixTolerance ||
                    std::abs(expected.visibleProjectionMatrix(row, column) -
                             current.visibleProjectionMatrix(row, column)) >
                        kMatrixTolerance)
                    return false;
            }
        }
        return true;
    }

    inline const char* photoCaptureGateError(
        bool animationRunning,
        const PhotoViewGate& gate,
        const osg::Vec3d& targetLla,
        const PhotoCameraContext& camera)
    {
        if (animationRunning) return "camera_flight_in_progress";
        if (!gate.photoRequestedThisTurn()) return "photo_not_requested";
        if (!photoTargetVisibleInCameraContext(targetLla, camera))
            return "photo_target_not_visible";
        if (gate.flewThisTurn()) return "photo_view_not_confirmed";
        return NULL;
    }

    struct PhotoCaptureRequest
    {
        osg::Vec3d targetLla;
        PhotoCameraContext camera;
        std::string style;
        bool showCameraPlatform = false;
        long long requestId = 0;
    };

    inline PhotoCaptureRequest makePhotoCaptureRequest(
        const PhotoRequest& input, const PhotoCameraContext& camera, long long requestId)
    {
        PhotoCaptureRequest request;
        request.targetLla = input.lla;
        request.camera = camera;
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
               "\"alt_km\":{\"type\":\"number\",\"description\":\"可选目标/导航高度;不代表快门相机高度，快门几何始终读取当前画面\"},"
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
