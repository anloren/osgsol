#ifndef OSGSOL_ERA5_AGRO_PROVIDER_H
#define OSGSOL_ERA5_AGRO_PROVIDER_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "Era5AgroSource.h"
#include "ScienceProvider.h"

namespace earthscience
{
    class IEra5AgroIo
    {
    public:
        virtual ~IEra5AgroIo() = default;
        virtual bool fetch(
            const std::string& url, std::size_t maximumBytes,
            const std::function<bool()>& cancelled,
            std::string& body, std::string& error) = 0;
    };

    class Era5AgroProvider : public IScienceProvider
    {
    public:
        explicit Era5AgroProvider(Era5AgroProduct product);
        Era5AgroProvider(
            Era5AgroProduct product, std::unique_ptr<IEra5AgroIo> io);
        ~Era5AgroProvider() override;

        Era5AgroProvider(const Era5AgroProvider&) = delete;
        Era5AgroProvider& operator=(const Era5AgroProvider&) = delete;

        ScienceSourceDescriptor descriptor() const override;
        bool validateQuery(
            const GeoTemporalQuery& query, std::string& error) const override;
        std::uint64_t submit(const GeoTemporalQuery& query) override;
        ScienceProviderSnapshot snapshot() const override;
        void cancel(std::uint64_t generation) override;
        void clear() override;

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };
}

#endif
