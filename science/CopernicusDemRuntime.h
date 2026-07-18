#ifndef OSGSOL_COPERNICUS_DEM_RUNTIME_H
#define OSGSOL_COPERNICUS_DEM_RUNTIME_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CopernicusDemSource.h"
#include "ScienceProvider.h"

namespace earthscience
{
    struct CopernicusDemReadResult
    {
        ScienceRasterPayload raster;
        ScienceScalarSummary summary;
        std::vector<std::string> sourceUrls;
        std::vector<std::string> warnings;
    };

    class ICopernicusDemIo
    {
    public:
        virtual ~ICopernicusDemIo() = default;
        virtual bool read(
            const std::vector<CopernicusDemCell>& cells,
            const GeoTemporalQuery& query,
            const std::function<bool()>& cancelled,
            CopernicusDemReadResult& output,
            std::string& error) = 0;
    };

    class CopernicusDemRuntime
    {
    public:
        CopernicusDemRuntime();
        explicit CopernicusDemRuntime(std::unique_ptr<ICopernicusDemIo> io);
        ~CopernicusDemRuntime();

        CopernicusDemRuntime(const CopernicusDemRuntime&) = delete;
        CopernicusDemRuntime& operator=(const CopernicusDemRuntime&) = delete;

        std::uint64_t submit(const GeoTemporalQuery& query);
        ScienceProviderSnapshot snapshot() const;
        void cancel(std::uint64_t generation);
        void clear();

    private:
        struct Impl;
        std::unique_ptr<Impl> _impl;
    };

    bool readCopernicusDemDatasetsForTest(
        const std::vector<std::string>& datasetPaths,
        const GeoTemporalQuery& query,
        CopernicusDemReadResult& output,
        std::string& error);
}

#endif
