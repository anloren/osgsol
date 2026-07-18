#ifndef OSGSOL_SENTINEL2_STAC_H
#define OSGSOL_SENTINEL2_STAC_H

#include <cstdint>
#include <string>
#include <vector>

#include "ScienceQueryTypes.h"

namespace earthscience
{
    struct Sentinel2Scene
    {
        std::string itemId;
        std::string acquisitionTime;
        std::string visualUrl;
        std::string mediaType;
        ScienceWgs84Bounds bounds;
        double cloudCoverPercent = 0.0;
        double resolutionMeters = 10.0;
    };

    bool makeSentinel2PointSearchBounds(
        const ScienceGeometry& geometry,
        ScienceWgs84Bounds& bounds,
        std::string& error);

    bool buildSentinel2SearchUrl(
        const ScienceWgs84Bounds& bounds,
        const std::string& intervalStart,
        const std::string& intervalEnd,
        std::uint32_t maximumScenes,
        std::string& url,
        std::string& error);

    bool parseSentinel2Items(
        const std::string& json,
        std::vector<Sentinel2Scene>& scenes,
        std::string& error);

    bool selectSentinel2Item(
        const std::vector<Sentinel2Scene>& scenes,
        double maximumCloudCoverPercent,
        Sentinel2Scene& selected,
        std::string& error);
}

#endif
