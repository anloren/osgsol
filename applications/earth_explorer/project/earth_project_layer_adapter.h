#ifndef OSGSOL_EARTH_PROJECT_LAYER_ADAPTER_H
#define OSGSOL_EARTH_PROJECT_LAYER_ADAPTER_H

#include "earth_project.h"

#include <cstddef>
#include <string>
#include <vector>

class LayerManager;

namespace earthproject
{
    struct LayerRestoreResult
    {
        std::size_t matched = 0u;
        std::size_t changed = 0u;
        std::size_t runtimeOwned = 0u;
        std::vector<std::string> missingIds;
    };

    std::vector<EarthLayerDescriptor> snapshotLayerDescriptors(
        const LayerManager& layers);

    // Restores only display state for runtime-registered, interactive layers.
    // It never creates a provider, imports a URI, changes a callback, replaces
    // source metadata, or applies a temporal value without a temporal adapter.
    LayerRestoreResult restoreLayerDisplayState(
        LayerManager& layers,
        const std::vector<EarthLayerDescriptor>& descriptors);
}

#endif
