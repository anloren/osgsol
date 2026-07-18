#ifndef OSGSOL_SENTINEL2_PROVIDER_H
#define OSGSOL_SENTINEL2_PROVIDER_H

#include <cstdint>
#include <memory>
#include <string>

#include "ScienceProvider.h"
#include "Sentinel2Runtime.h"

namespace earthscience
{
    ScienceSourceDescriptor describeSentinel2();

    class Sentinel2Provider : public IScienceProvider
    {
    public:
        Sentinel2Provider();
        explicit Sentinel2Provider(std::unique_ptr<ISentinel2Io> io);
        ~Sentinel2Provider() override;

        ScienceSourceDescriptor descriptor() const override;
        bool validateQuery(
            const GeoTemporalQuery& query, std::string& error) const override;
        std::uint64_t submit(const GeoTemporalQuery& query) override;
        ScienceProviderSnapshot snapshot() const override;
        void cancel(std::uint64_t generation) override;
        void clear() override;

    private:
        std::unique_ptr<Sentinel2Runtime> _runtime;
    };
}

#endif
