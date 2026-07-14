#ifndef OSGSOL_ALPHAEARTH_PROVIDER_H
#define OSGSOL_ALPHAEARTH_PROVIDER_H

#include <cstdint>
#include <memory>
#include <string>

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

    class AlphaEarthProvider : public IScienceProvider
    {
    public:
        explicit AlphaEarthProvider(const std::string& indexPath);
        ~AlphaEarthProvider() override;

        ScienceSourceDescriptor descriptor() const override;
        std::uint64_t submit(const GeoTemporalQuery& query) override;
        ScienceProviderSnapshot snapshot() const override;
        void cancel(std::uint64_t generation) override;
        void clear() override;

    private:
        std::unique_ptr<SciencePreviewRuntime> _runtime;
        std::uint64_t _activeGeneration = 0;
        GeoTemporalQuery _activeQuery;
    };
}

#endif
