#ifndef OSGSOL_ALPHAEARTH_EMBEDDING_READER_H
#define OSGSOL_ALPHAEARTH_EMBEDDING_READER_H

#include "AlphaEarthEmbeddingRuntime.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace earthscience
{
namespace alphaearthdetail
{
    struct AlphaEarthReadCallbacks
    {
        std::function<bool()> cancelled;
        std::function<void(const ScienceProgress&, const std::string&)> progress;
    };

    bool resolveAlphaEarthAssetFromIndex(
        const std::string& indexPath, double latitude, double longitude,
        int year, AlphaEarthAsset& asset, std::string& error);

    bool resolveAlphaEarthAssetsFromIndex(
        const std::string& indexPath, const ScienceWgs84Bounds& bounds,
        int year, std::vector<AlphaEarthAsset>& assets, std::string& error);

    bool readAlphaEarthArtifact(
        const GeoTemporalQuery& query,
        const AlphaEarthAssetResolver& resolver,
        const AlphaEarthAssetSetResolver& assetSetResolver,
        bool injectedLocalResolver,
        std::uint64_t generation,
        const AlphaEarthReadCallbacks& callbacks,
        std::shared_ptr<const ScienceArtifact>& artifact,
        std::string& error);
}
}

#endif
