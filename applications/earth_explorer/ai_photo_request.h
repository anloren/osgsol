#ifndef EARTH_AI_PHOTO_REQUEST_H
#define EARTH_AI_PHOTO_REQUEST_H

#include <osg/Matrixd>
#include <osg/Vec3d>
#include <picojson.h>
#include <cmath>
#include <string>

namespace earthai
{
    inline bool photoCaptureHasFreshView(unsigned int completedUpdateTicks)
    {
        return completedUpdateTicks > 0;
    }

    inline const char* photoCaptureGateError(bool animationRunning)
    {
        return animationRunning ? "camera_flight_in_progress" : NULL;
    }

    struct PhotoRequest
    {
        osg::Vec3d lla;
        std::string style;
        bool showCameraPlatform = false;
    };

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
               "\"alt_km\":{\"type\":\"number\",\"description\":\"相机离地高度,默认150\"},"
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
