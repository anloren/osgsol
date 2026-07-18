#ifndef OSGSOL_SENTINEL2_RUNTIME_H
#define OSGSOL_SENTINEL2_RUNTIME_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "ScienceProvider.h"
#include "Sentinel2Stac.h"

namespace earthscience
{
    class ISentinel2Io
    {
    public:
        virtual ~ISentinel2Io() = default;
        virtual bool fetchStac(
            const std::string& url, std::size_t maximumBytes,
            const std::function<bool()>& cancelled,
            std::string& body, std::string& error) = 0;
        virtual bool readVisual(
            const Sentinel2Scene& scene, const GeoTemporalQuery& query,
            const std::function<bool()>& cancelled,
            ScienceRasterPayload& output, std::string& error) = 0;
    };

    class Sentinel2Runtime
    {
    public:
        Sentinel2Runtime();
        explicit Sentinel2Runtime(std::unique_ptr<ISentinel2Io> io);
        ~Sentinel2Runtime();

        Sentinel2Runtime(const Sentinel2Runtime&) = delete;
        Sentinel2Runtime& operator=(const Sentinel2Runtime&) = delete;

        std::uint64_t submit(const GeoTemporalQuery& query);
        ScienceProviderSnapshot snapshot() const;
        void cancel(std::uint64_t generation);
        void clear();

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

    bool readSentinel2VisualDatasetForTest(
        const std::string& datasetPath, const GeoTemporalQuery& query,
        ScienceRasterPayload& output, std::string& error);
}

#endif
