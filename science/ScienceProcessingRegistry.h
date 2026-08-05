#ifndef OSGSOL_SCIENCE_PROCESSING_REGISTRY_H
#define OSGSOL_SCIENCE_PROCESSING_REGISTRY_H

#include <map>
#include <string>
#include <vector>

#include "ScienceProcessingTypes.h"

namespace earthscience
{
    class ScienceProcessingRegistry
    {
    public:
        bool add(const ScienceProcessingCapability& capability,
                 std::string& error);
        bool addBuiltInSource(const ScienceSourceDescriptor& source,
                              std::string& error);
        bool registerPlugin(
            const ScienceProcessingPluginManifest& manifest,
            const std::vector<ScienceProcessingCapability>& capabilities,
            std::string& error);
        const ScienceProcessingCapability* find(const std::string& id) const;
        const ScienceProcessingCapability* resolve(
            const GeoTemporalQuery& query) const;
        std::vector<ScienceProcessingCapability> list() const;

    private:
        std::map<std::string, ScienceProcessingCapability> _capabilities;
    };

    std::vector<ScienceProcessingCapability>
        optionalScienceProcessingCapabilities();
}

#endif
