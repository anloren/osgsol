#include "ScienceSourceRegistry.h"

#include <utility>

namespace earthscience
{
bool ScienceSourceRegistry::add(
    std::unique_ptr<IScienceProvider> provider, std::string& error)
{
    if (!provider)
    {
        error = "science provider is null";
        return false;
    }

    const ScienceSourceDescriptor descriptor = provider->descriptor();
    if (descriptor.id.empty())
    {
        error = "science provider id is empty";
        return false;
    }
    if (_providers.find(descriptor.id) != _providers.end())
    {
        error = "science provider id already exists: " + descriptor.id;
        return false;
    }

    error.clear();
    _providers.emplace(descriptor.id, std::move(provider));
    return true;
}

IScienceProvider* ScienceSourceRegistry::find(const std::string& id)
{
    const auto found = _providers.find(id);
    return found == _providers.end() ? nullptr : found->second.get();
}

const IScienceProvider* ScienceSourceRegistry::find(const std::string& id) const
{
    const auto found = _providers.find(id);
    return found == _providers.end() ? nullptr : found->second.get();
}

std::vector<ScienceSourceDescriptor>
ScienceSourceRegistry::listSources() const
{
    std::vector<ScienceSourceDescriptor> sources;
    sources.reserve(_providers.size());
    for (const auto& entry : _providers)
    {
        ScienceSourceDescriptor descriptor = entry.second->descriptor();
        descriptor.id = entry.first;
        sources.push_back(std::move(descriptor));
    }
    return sources;
}
}
