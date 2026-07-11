#ifndef OSGSOL_PLUGINS_OSGDB_TMS_TMS_OVERLAY_SELECTION_H
#define OSGSOL_PLUGINS_OSGDB_TMS_TMS_OVERLAY_SELECTION_H

#include <string>
#include <readerwriter/TileCallback.h>

namespace osgVerse
{
    // New TMS children inherit a cloned Options object. Once the application has
    // published an OVERLAY value, that value (including an explicit empty value)
    // owns the child; the cloned option is only a pre-publication fallback.
    inline void applyTmsOverlaySelection(TileCallback& tile, const TileManager& manager,
                                         const std::string& clonedOptionsFallback)
    {
        std::string selected = clonedOptionsFallback;
        manager.tryGetLayerPath(TileCallback::OVERLAY, selected);
        tile.setLayerPath(TileCallback::OVERLAY, selected);
    }
}

#endif
