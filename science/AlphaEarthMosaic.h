#ifndef OSGSOL_ALPHAEARTH_MOSAIC_H
#define OSGSOL_ALPHAEARTH_MOSAIC_H

#include "AlphaEarthEmbeddingRuntime.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace earthscience
{
namespace alphaearthdetail
{
    struct AlphaEarthMosaicRaster
    {
        int width = 0;
        int height = 0;
        std::vector<std::int8_t> values;
        std::vector<unsigned char> masks;
    };

    bool alphaEarthAssetsCoverBounds(
        const std::vector<AlphaEarthAsset>& assets,
        const ScienceWgs84Bounds& bounds);

    bool readAlphaEarthMosaic(
        const std::vector<AlphaEarthAsset>& assets,
        const ScienceWgs84Bounds& bounds,
        int width, int height,
        const std::vector<int>& sourceBands,
        const std::vector<std::string>& expectedDescriptions,
        const std::function<bool()>& cancelled,
        AlphaEarthMosaicRaster& output,
        std::string& error);
}
}

#endif
