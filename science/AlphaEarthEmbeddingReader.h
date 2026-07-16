#ifndef OSGSOL_ALPHAEARTH_EMBEDDING_READER_H
#define OSGSOL_ALPHAEARTH_EMBEDDING_READER_H

#include "AlphaEarthEmbeddingRuntime.h"

#include <functional>
#include <memory>
#include <string>

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

    bool readAlphaEarthArtifact(
        const GeoTemporalQuery& query,
        const AlphaEarthAssetResolver& resolver,
        std::uint64_t generation,
        const AlphaEarthReadCallbacks& callbacks,
        std::shared_ptr<const ScienceArtifact>& artifact,
        std::string& error);
}
}

#endif
