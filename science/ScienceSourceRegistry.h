#ifndef OSGSOL_SCIENCE_SOURCE_REGISTRY_H
#define OSGSOL_SCIENCE_SOURCE_REGISTRY_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ScienceProvider.h"

namespace earthscience
{
    class ScienceSourceRegistry
    {
    public:
        ScienceSourceRegistry() = default;
        ~ScienceSourceRegistry() = default;

        ScienceSourceRegistry(const ScienceSourceRegistry&) = delete;
        ScienceSourceRegistry& operator=(const ScienceSourceRegistry&) = delete;

        bool add(std::unique_ptr<IScienceProvider> provider,
                 std::string& error);
        IScienceProvider* find(const std::string& id);
        const IScienceProvider* find(const std::string& id) const;
        std::vector<ScienceSourceDescriptor> listSources() const;

    private:
        std::map<std::string, std::unique_ptr<IScienceProvider>> _providers;
    };
}

#endif
