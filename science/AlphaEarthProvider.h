#ifndef OSGSOL_ALPHAEARTH_PROVIDER_H
#define OSGSOL_ALPHAEARTH_PROVIDER_H

#include <cstdint>
#include <memory>
#include <string>

#include "AlphaEarthEmbeddingRuntime.h"
#include "SciencePreviewRuntime.h"
#include "ScienceProvider.h"

namespace earthscience
{
    ScienceSourceDescriptor describeAlphaEarth(
        const AlphaEarthSourceDescriptor& source, bool available,
        const std::string& healthMessage);

    ScienceProviderSnapshot translateAlphaEarthSnapshot(
        const AlphaEarthPreviewSnapshot& snapshot,
        const GeoTemporalQuery& query);

    bool isAlphaEarthLegacyPreviewQuery(const GeoTemporalQuery& query);

    class AlphaEarthProvider : public IScienceProvider
    {
    public:
        explicit AlphaEarthProvider(const std::string& indexPath);
        ~AlphaEarthProvider() override;

        ScienceSourceDescriptor descriptor() const override;
        bool validateQuery(
            const GeoTemporalQuery& query, std::string& error) const override;
        std::uint64_t submit(const GeoTemporalQuery& query) override;
        ScienceProviderSnapshot snapshot() const override;
        void cancel(std::uint64_t generation) override;
        void clear() override;

    private:
        enum class RuntimeKind
        {
            None,
            Preview,
            Embedding64,
        };

        std::unique_ptr<SciencePreviewRuntime> _previewRuntime;
        std::unique_ptr<AlphaEarthEmbeddingRuntime> _embeddingRuntime;
        RuntimeKind _activeRuntime = RuntimeKind::None;
        std::uint64_t _activeGeneration = 0;
        std::uint64_t _embeddingGeneration = 0;
        GeoTemporalQuery _activeQuery;
    };
}

#endif
