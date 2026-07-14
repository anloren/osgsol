#ifndef OSGSOL_SCIENCE_QUERY_SERVICE_H
#define OSGSOL_SCIENCE_QUERY_SERVICE_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ScienceSourceRegistry.h"

namespace earthscience
{
    class ScienceQueryService
    {
    public:
        explicit ScienceQueryService(
            std::unique_ptr<ScienceSourceRegistry> registry);
        ~ScienceQueryService();

        ScienceQueryService(const ScienceQueryService&) = delete;
        ScienceQueryService& operator=(const ScienceQueryService&) = delete;

        std::vector<ScienceSourceDescriptor> listSources() const;
        std::uint64_t submit(const GeoTemporalQuery& query);
        void cancel(std::uint64_t jobId);
        void clearArtifact();
        ScienceJobSnapshot snapshot();

    private:
        bool validate(const GeoTemporalQuery& query,
                      const ScienceSourceDescriptor& source,
                      std::string& error) const;
        void cancelActiveProvider();

        mutable std::mutex _mutex;
        std::unique_ptr<ScienceSourceRegistry> _registry;
        IScienceProvider* _activeProvider = nullptr;
        std::string _activeSourceId;
        std::uint64_t _activeProviderGeneration = 0;
        std::uint64_t _nextJobId = 0;
        bool _providerActive = false;
        ScienceJobSnapshot _state;
    };
}

#endif
