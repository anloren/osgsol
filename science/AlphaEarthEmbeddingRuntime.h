#ifndef OSGSOL_ALPHAEARTH_EMBEDDING_RUNTIME_H
#define OSGSOL_ALPHAEARTH_EMBEDDING_RUNTIME_H

#include "ScienceProvider.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace earthscience
{
    struct AlphaEarthAsset
    {
        std::string datasetId;
        std::string pathOrUrl;
        std::string sourceVersion;
        ScienceWgs84Bounds indexedBounds;
    };

    using AlphaEarthAssetResolver = std::function<bool(
        double latitude, double longitude, int year,
        AlphaEarthAsset& asset, std::string& error)>;

    class AlphaEarthEmbeddingRuntime
    {
    public:
        explicit AlphaEarthEmbeddingRuntime(const std::string& indexPath);
        explicit AlphaEarthEmbeddingRuntime(AlphaEarthAssetResolver resolver);
        ~AlphaEarthEmbeddingRuntime();

        AlphaEarthEmbeddingRuntime(const AlphaEarthEmbeddingRuntime&) = delete;
        AlphaEarthEmbeddingRuntime& operator=(
            const AlphaEarthEmbeddingRuntime&) = delete;

        std::uint64_t submit(const GeoTemporalQuery& query);
        void cancel(std::uint64_t generation);
        ScienceProviderSnapshot snapshot() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}

#endif
