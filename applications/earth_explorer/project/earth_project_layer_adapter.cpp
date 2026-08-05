#include "earth_project_layer_adapter.h"

#include "../LayerManager.h"

#include <algorithm>
#include <cmath>

namespace earthproject
{
    namespace
    {
        const char* renderKind(OverlayLayer::Type type)
        {
            switch (type)
            {
            case OverlayLayer::PointFeed: return "point-feed";
            case OverlayLayer::Grid: return "grid";
            case OverlayLayer::RasterTile:
            default: return "raster-tile";
            }
        }

        float safeOpacity(double value)
        {
            if (!std::isfinite(value)) return 1.0f;
            return static_cast<float>(std::max(0.0, std::min(1.0, value)));
        }
    }

    std::vector<EarthLayerDescriptor> snapshotLayerDescriptors(
        const LayerManager& manager)
    {
        const std::vector<OverlayLayer> layers = manager.layersSnapshot();
        std::vector<EarthLayerDescriptor> output;
        output.reserve(layers.size());
        for (std::size_t index = 0u; index < layers.size(); ++index)
        {
            const OverlayLayer& layer = layers[index];
            EarthLayerDescriptor descriptor;
            descriptor.id = layer.id;
            descriptor.displayName = layer.displayName;
            descriptor.group = layer.group;
            descriptor.source = layer.source;
            descriptor.render.kind = renderKind(layer.type);
            descriptor.render.visible = layer.enabled;
            descriptor.render.opacity = layer.opacity;
            descriptor.render.order = static_cast<int>(index);
            descriptor.render.styleId = layer.styleId;
            descriptor.render.legendId = layer.legendId;
            descriptor.temporal = layer.temporal;
            descriptor.capabilities = layer.capabilities;
            descriptor.artifactId = layer.artifactId;
            output.push_back(descriptor);
        }
        return output;
    }

    LayerRestoreResult restoreLayerDisplayState(
        LayerManager& manager,
        const std::vector<EarthLayerDescriptor>& descriptors)
    {
        LayerRestoreResult result;
        const std::vector<OverlayLayer> runtime = manager.layersSnapshot();
        for (const EarthLayerDescriptor& descriptor : descriptors)
        {
            const auto found = std::find_if(
                runtime.begin(), runtime.end(),
                [&descriptor](const OverlayLayer& layer)
                { return layer.id == descriptor.id; });
            if (found == runtime.end())
            {
                if (!descriptor.id.empty())
                    result.missingIds.push_back(descriptor.id);
                continue;
            }

            ++result.matched;
            // No apply callback means the state is owned by the renderer or
            // product shell rather than by the interactive layer registry.
            if (!found->apply)
            {
                ++result.runtimeOwned;
                continue;
            }

            bool changed = false;
            if (found->enabled != descriptor.render.visible)
            {
                manager.setEnabled(found->id, descriptor.render.visible);
                changed = true;
            }
            if (found->hasOpacity)
            {
                const float opacity = safeOpacity(descriptor.render.opacity);
                if (found->opacity != opacity)
                {
                    manager.setOpacity(found->id, opacity);
                    changed = true;
                }
            }
            if (changed) ++result.changed;
        }
        return result;
    }
}
