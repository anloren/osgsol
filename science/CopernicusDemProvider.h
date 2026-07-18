#ifndef OSGSOL_COPERNICUS_DEM_PROVIDER_H
#define OSGSOL_COPERNICUS_DEM_PROVIDER_H

#include <cstdint>
#include <memory>
#include <string>

#include "CopernicusDemRuntime.h"
#include "ScienceProvider.h"

namespace earthscience
{
    ScienceSourceDescriptor describeCopernicusDem();

    class CopernicusDemProvider : public IScienceProvider
    {
    public:
        CopernicusDemProvider();
        explicit CopernicusDemProvider(std::unique_ptr<ICopernicusDemIo> io);
        ~CopernicusDemProvider() override;

        ScienceSourceDescriptor descriptor() const override;
        bool validateQuery(
            const GeoTemporalQuery& query, std::string& error) const override;
        std::uint64_t submit(const GeoTemporalQuery& query) override;
        ScienceProviderSnapshot snapshot() const override;
        void cancel(std::uint64_t generation) override;
        void clear() override;

    private:
        std::unique_ptr<CopernicusDemRuntime> _runtime;
    };
}

#endif
