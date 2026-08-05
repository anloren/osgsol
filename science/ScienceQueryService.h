#ifndef OSGSOL_SCIENCE_QUERY_SERVICE_H
#define OSGSOL_SCIENCE_QUERY_SERVICE_H

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ScienceArtifactStore.h"
#include "ScienceProcessingRegistry.h"
#include "ScienceProcessingStore.h"
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
        std::vector<ScienceProcessingCapability>
            listProcessingCapabilities() const;
        bool registerProcessingPlugin(
            const ScienceProcessingPluginManifest& manifest,
            const std::vector<ScienceProcessingCapability>& capabilities,
            std::string& error);
        bool configureProcessingHistory(
            const std::string& root, std::string& error);
        bool validateQuery(
            const GeoTemporalQuery& query, std::string& error) const;
        ScienceQueryCost estimate(const GeoTemporalQuery& query) const;
        std::uint64_t submit(const GeoTemporalQuery& query);
        void cancel(std::uint64_t jobId);
        std::shared_ptr<const ScienceArtifact> findArtifact(
            const std::string& id) const;
        bool showArtifact(const std::string& id);
        void clearArtifacts();
        void clearArtifact();
        ScienceJobSnapshot snapshot();

    private:
        bool validate(const GeoTemporalQuery& query,
                      const IScienceProvider& provider,
                      std::string& error) const;
        ScienceQueryCost estimateUnlocked(
            const GeoTemporalQuery& query) const;
        void cancelActiveProvider();
        void publishProcessingRecord(ScienceProcessingRecord record);
        void cancelProcessingRecord(const std::string& message);
        void syncProcessingRecord();

        mutable std::mutex _mutex;
        std::unique_ptr<ScienceSourceRegistry> _registry;
        std::unique_ptr<ScienceProcessingRegistry> _processingRegistry;
        std::unique_ptr<ScienceProcessingStore> _processingStore;
        IScienceProvider* _activeProvider = nullptr;
        std::string _activeSourceId;
        std::uint64_t _activeProviderGeneration = 0;
        std::uint64_t _nextJobId = 0;
        bool _providerActive = false;
        ScienceArtifactStore _artifacts;
        std::map<std::string, double> _throughputCellsPerSecond;
        ScienceJobSnapshot _state;
    };
}

#endif
