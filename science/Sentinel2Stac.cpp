#include "Sentinel2Stac.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <tuple>

#include "picojson.h"

namespace earthscience
{
namespace
{
    constexpr std::size_t MAX_STAC_BYTES = 2u * 1024u * 1024u;
    constexpr std::size_t MAX_SCENES = 10;
    constexpr const char* SEARCH_ENDPOINT =
        "https://earth-search.aws.element84.com/v1/search";
    constexpr const char* ASSET_PREFIX =
        "https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
        "sentinel-s2-l2a-cogs/";

    bool digit(char value)
    {
        return value >= '0' && value <= '9';
    }

    int integerAt(const std::string& value, std::size_t offset,
                  std::size_t length)
    {
        int result = 0;
        for (std::size_t index = 0; index < length; ++index)
        {
            if (!digit(value[offset + index])) return -1;
            result = result * 10 + value[offset + index] - '0';
        }
        return result;
    }

    bool leapYear(int year)
    {
        return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    }

    int daysInMonth(int year, int month)
    {
        static const int DAYS[] =
            {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month < 1 || month > 12) return 0;
        return month == 2 && leapYear(year) ? 29 : DAYS[month - 1];
    }

    bool normalizeUtc(const std::string& value, std::string& normalized)
    {
        if (value.size() < 20 || value.size() > 30 ||
            value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
            value[13] != ':' || value[16] != ':' || value.back() != 'Z')
            return false;

        const int year = integerAt(value, 0, 4);
        const int month = integerAt(value, 5, 2);
        const int day = integerAt(value, 8, 2);
        const int hour = integerAt(value, 11, 2);
        const int minute = integerAt(value, 14, 2);
        const int second = integerAt(value, 17, 2);
        if (year < 1 || month < 1 || month > 12 || day < 1 ||
            day > daysInMonth(year, month) || hour < 0 || hour > 23 ||
            minute < 0 || minute > 59 || second < 0 || second > 60)
            return false;

        std::string fraction;
        if (value.size() > 20)
        {
            if (value[19] != '.' || value.size() == 21) return false;
            fraction = value.substr(20, value.size() - 21);
            if (fraction.empty() || fraction.size() > 9 ||
                !std::all_of(fraction.begin(), fraction.end(), digit))
                return false;
        }
        fraction.resize(9, '0');
        normalized = value.substr(0, 19) + '.' + fraction + 'Z';
        return true;
    }

    bool validBounds(const ScienceWgs84Bounds& bounds)
    {
        return std::isfinite(bounds.west) && std::isfinite(bounds.south) &&
            std::isfinite(bounds.east) && std::isfinite(bounds.north) &&
            bounds.west >= -180.0 && bounds.east <= 180.0 &&
            bounds.south >= -90.0 && bounds.north <= 90.0 &&
            bounds.west < bounds.east && bounds.south < bounds.north;
    }

    std::string percentEncode(const std::string& value)
    {
        std::ostringstream stream;
        stream << std::uppercase << std::hex;
        for (unsigned char byte : value)
        {
            if (std::isalnum(byte) || byte == '-' || byte == '_' ||
                byte == '.' || byte == '~')
                stream << static_cast<char>(byte);
            else
                stream << '%' << std::setw(2) << std::setfill('0')
                       << static_cast<int>(byte);
        }
        return stream.str();
    }

    bool validItemId(const std::string& id)
    {
        if (id.empty() || id.size() > 256) return false;
        return std::all_of(id.begin(), id.end(), [](unsigned char value)
        {
            return std::isalnum(value) || value == '-' || value == '_' ||
                   value == '.';
        });
    }

    bool validAssetUrl(const std::string& url)
    {
        if (url.size() <= std::char_traits<char>::length(ASSET_PREFIX) ||
            url.rfind(ASSET_PREFIX, 0) != 0 ||
            url.find('?') != std::string::npos ||
            url.find('#') != std::string::npos ||
            url.find("..") != std::string::npos)
            return false;
        return url.size() >= 4 && url.compare(url.size() - 4, 4, ".tif") == 0;
    }

    const picojson::value* member(
        const picojson::object& object, const char* name)
    {
        const auto found = object.find(name);
        return found == object.end() ? nullptr : &found->second;
    }

    bool parseBounds(const picojson::value& value, ScienceWgs84Bounds& bounds)
    {
        if (!value.is<picojson::array>()) return false;
        const picojson::array& values = value.get<picojson::array>();
        if (values.size() != 4 ||
            !std::all_of(values.begin(), values.end(),
                         [](const picojson::value& item)
                         { return item.is<double>(); }))
            return false;
        bounds = {values[0].get<double>(), values[1].get<double>(),
                  values[2].get<double>(), values[3].get<double>()};
        return validBounds(bounds);
    }

    bool parseScene(const picojson::value& value, Sentinel2Scene& scene,
                    std::string& error)
    {
        if (!value.is<picojson::object>())
        {
            error = "Sentinel-2 STAC feature is not an object";
            return false;
        }
        const picojson::object& object = value.get<picojson::object>();
        const picojson::value* type = member(object, "type");
        const picojson::value* id = member(object, "id");
        const picojson::value* bbox = member(object, "bbox");
        const picojson::value* properties = member(object, "properties");
        const picojson::value* assets = member(object, "assets");
        if (!type || !type->is<std::string>() ||
            type->get<std::string>() != "Feature" ||
            !id || !id->is<std::string>() ||
            !validItemId(id->get<std::string>()) || !bbox ||
            !parseBounds(*bbox, scene.bounds) || !properties ||
            !properties->is<picojson::object>() || !assets ||
            !assets->is<picojson::object>())
        {
            error = "Sentinel-2 STAC feature identity or bounds are invalid";
            return false;
        }
        scene.itemId = id->get<std::string>();

        const picojson::object& propertyObject =
            properties->get<picojson::object>();
        const picojson::value* datetime = member(propertyObject, "datetime");
        const picojson::value* cloud =
            member(propertyObject, "eo:cloud_cover");
        std::string normalized;
        if (!datetime || !datetime->is<std::string>() ||
            !normalizeUtc(datetime->get<std::string>(), normalized) ||
            !cloud || !cloud->is<double>() ||
            !std::isfinite(cloud->get<double>()) ||
            cloud->get<double>() < 0.0 || cloud->get<double>() > 100.0)
        {
            error = "Sentinel-2 acquisition time or cloud cover is invalid";
            return false;
        }
        scene.acquisitionTime = datetime->get<std::string>();
        scene.cloudCoverPercent = cloud->get<double>();

        const picojson::object& assetObject = assets->get<picojson::object>();
        const picojson::value* visual = member(assetObject, "visual");
        if (!visual || !visual->is<picojson::object>())
        {
            error = "Sentinel-2 visual asset is missing";
            return false;
        }
        const picojson::object& visualObject = visual->get<picojson::object>();
        const picojson::value* href = member(visualObject, "href");
        const picojson::value* mediaType = member(visualObject, "type");
        const picojson::value* gsd = member(visualObject, "gsd");
        const picojson::value* roles = member(visualObject, "roles");
        if (!href || !href->is<std::string>() ||
            !validAssetUrl(href->get<std::string>()) || !mediaType ||
            !mediaType->is<std::string>() ||
            mediaType->get<std::string>().rfind("image/tiff", 0) != 0 ||
            mediaType->get<std::string>().find("profile=cloud-optimized") ==
                std::string::npos || !gsd || !gsd->is<double>() ||
            std::abs(gsd->get<double>() - 10.0) > 1e-9 || !roles ||
            !roles->is<picojson::array>())
        {
            error = "Sentinel-2 visual COG metadata is invalid";
            return false;
        }
        const picojson::array& roleValues = roles->get<picojson::array>();
        const bool visualRole = std::any_of(
            roleValues.begin(), roleValues.end(), [](const picojson::value& role)
            { return role.is<std::string>() && role.get<std::string>() == "visual"; });
        if (!visualRole)
        {
            error = "Sentinel-2 visual asset lacks the visual role";
            return false;
        }
        scene.visualUrl = href->get<std::string>();
        scene.mediaType = mediaType->get<std::string>();
        scene.resolutionMeters = gsd->get<double>();
        return true;
    }

    std::string normalizedTime(const Sentinel2Scene& scene)
    {
        std::string result;
        normalizeUtc(scene.acquisitionTime, result);
        return result;
    }
}

bool makeSentinel2PointSearchBounds(
    const ScienceGeometry& geometry,
    ScienceWgs84Bounds& bounds,
    std::string& error)
{
    constexpr double MINIMUM_SPAN_METERS = 2560.0;
    constexpr double MAXIMUM_SPAN_METERS = 81920.0;
    constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
    constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
    constexpr double PI = 3.14159265358979323846;
    bounds = ScienceWgs84Bounds();
    if (geometry.kind != ScienceGeometryKind::Point)
        error = "Sentinel-2 preview requires point geometry";
    else if (!std::isfinite(geometry.point.latitude) ||
             !std::isfinite(geometry.point.longitude) ||
             geometry.point.latitude < -90.0 ||
             geometry.point.latitude > 90.0 ||
             geometry.point.longitude < -180.0 ||
             geometry.point.longitude > 180.0)
        error = "Sentinel-2 point must be valid WGS84 coordinates";
    else if (!std::isfinite(geometry.requestedSpanMeters) ||
             geometry.requestedSpanMeters < MINIMUM_SPAN_METERS ||
             geometry.requestedSpanMeters > MAXIMUM_SPAN_METERS)
        error = "Sentinel-2 span must be inside [2560, 81920] meters";
    else
    {
        const double halfSpan = geometry.requestedSpanMeters * 0.5;
        const double latitudeSpan =
            halfSpan / METERS_PER_LATITUDE_DEGREE;
        const double longitudeScale = METERS_PER_LONGITUDE_DEGREE *
            std::max(0.01, std::cos(geometry.point.latitude * PI / 180.0));
        const double longitudeSpan = halfSpan / longitudeScale;
        bounds = {
            geometry.point.longitude - longitudeSpan,
            geometry.point.latitude - latitudeSpan,
            geometry.point.longitude + longitudeSpan,
            geometry.point.latitude + latitudeSpan,
        };
        if (validBounds(bounds))
        {
            error.clear();
            return true;
        }
        bounds = ScienceWgs84Bounds();
        error = "Sentinel-2 search bounds cross the antimeridian or a pole";
    }
    return false;
}

bool buildSentinel2SearchUrl(
    const ScienceWgs84Bounds& bounds,
    const std::string& intervalStart,
    const std::string& intervalEnd,
    double maximumCloudCoverPercent,
    std::uint32_t maximumScenes,
    std::string& url,
    std::string& error)
{
    url.clear();
    std::string normalizedStart, normalizedEnd;
    if (!validBounds(bounds))
        error = "Sentinel-2 search bounds are invalid or cross the antimeridian";
    else if (!normalizeUtc(intervalStart, normalizedStart) ||
             !normalizeUtc(intervalEnd, normalizedEnd) ||
             normalizedStart > normalizedEnd)
        error = "Sentinel-2 search interval must be ordered RFC 3339 UTC";
    else if (!std::isfinite(maximumCloudCoverPercent) ||
             maximumCloudCoverPercent < 0.0 ||
             maximumCloudCoverPercent > 100.0)
        error = "Sentinel-2 cloud threshold must be inside [0, 100]";
    else if (maximumScenes == 0 || maximumScenes > MAX_SCENES)
        error = "Sentinel-2 search scene limit must be inside [1, 10]";
    else
    {
        std::ostringstream bbox;
        bbox << std::fixed << std::setprecision(8)
             << bounds.west << ',' << bounds.south << ','
             << bounds.east << ',' << bounds.north;
        std::ostringstream cloudQuery;
        cloudQuery << std::setprecision(15)
                   << "{\"eo:cloud_cover\":{\"lte\":"
                   << maximumCloudCoverPercent << "}}";
        url = std::string(SEARCH_ENDPOINT) +
            "?collections=sentinel-2-l2a&bbox=" +
            percentEncode(bbox.str()) + "&datetime=" +
            percentEncode(intervalStart + '/' + intervalEnd) +
            "&query=" + percentEncode(cloudQuery.str()) +
            "&sortby=" + percentEncode(
                "+properties.eo:cloud_cover,-properties.datetime") +
            "&limit=" + std::to_string(maximumScenes);
        error.clear();
        return true;
    }
    return false;
}

bool parseSentinel2Items(
    const std::string& json,
    std::vector<Sentinel2Scene>& scenes,
    std::string& error)
{
    scenes.clear();
    if (json.size() > MAX_STAC_BYTES)
    {
        error = "Sentinel-2 STAC response exceeds 2 MiB";
        return false;
    }
    picojson::value document;
    const std::string parseError = picojson::parse(document, json);
    if (!parseError.empty() || !document.is<picojson::object>())
    {
        error = "Sentinel-2 STAC response is invalid JSON";
        return false;
    }
    const picojson::object& root = document.get<picojson::object>();
    const picojson::value* type = member(root, "type");
    const picojson::value* features = member(root, "features");
    if (!type || !type->is<std::string>() ||
        type->get<std::string>() != "FeatureCollection" || !features ||
        !features->is<picojson::array>())
    {
        error = "Sentinel-2 STAC response is not a FeatureCollection";
        return false;
    }
    const picojson::array& featureValues = features->get<picojson::array>();
    if (featureValues.size() > MAX_SCENES)
    {
        error = "Sentinel-2 STAC response exceeds ten scenes";
        return false;
    }
    std::set<std::string> itemIds;
    scenes.reserve(featureValues.size());
    for (const picojson::value& value : featureValues)
    {
        Sentinel2Scene scene;
        if (!parseScene(value, scene, error))
        {
            scenes.clear();
            return false;
        }
        if (!itemIds.insert(scene.itemId).second)
        {
            scenes.clear();
            error = "Sentinel-2 STAC response contains duplicate item ids";
            return false;
        }
        scenes.push_back(std::move(scene));
    }
    error.clear();
    return true;
}

bool selectSentinel2Item(
    const std::vector<Sentinel2Scene>& scenes,
    double maximumCloudCoverPercent,
    Sentinel2Scene& selected,
    std::string& error)
{
    selected = Sentinel2Scene();
    if (!std::isfinite(maximumCloudCoverPercent) ||
        maximumCloudCoverPercent < 0.0 || maximumCloudCoverPercent > 100.0)
    {
        error = "Sentinel-2 cloud threshold must be inside [0, 100]";
        return false;
    }
    std::vector<const Sentinel2Scene*> candidates;
    for (const Sentinel2Scene& scene : scenes)
        if (scene.cloudCoverPercent <= maximumCloudCoverPercent)
            candidates.push_back(&scene);
    if (candidates.empty())
    {
        error = "No Sentinel-2 scene satisfies the selected cloud threshold; "
                "raise maximum cloud or widen time window";
        return false;
    }
    std::sort(candidates.begin(), candidates.end(),
        [](const Sentinel2Scene* left, const Sentinel2Scene* right)
        {
            if (left->cloudCoverPercent != right->cloudCoverPercent)
                return left->cloudCoverPercent < right->cloudCoverPercent;
            const std::string leftTime = normalizedTime(*left);
            const std::string rightTime = normalizedTime(*right);
            if (leftTime != rightTime) return leftTime > rightTime;
            return left->itemId < right->itemId;
        });
    selected = *candidates.front();
    error.clear();
    return true;
}
}
