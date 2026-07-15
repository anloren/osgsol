#ifndef EARTH_CONTROL_LAYOUT_H
#define EARTH_CONTROL_LAYOUT_H

#include <algorithm>

namespace earthui
{

struct EarthControlPanelLayout
{
    float defaultWidth;
    float defaultHeight;
    float minWidth;
    float minHeight;
    float maxWidth;
    float maxHeight;
};

inline EarthControlPanelLayout computeEarthControlPanelLayout(float viewportWidth,
                                                               float viewportHeight)
{
    const float safeWidth = std::max(viewportWidth, 1.0f);
    const float safeHeight = std::max(viewportHeight, 1.0f);
    const float availableWidth = std::max(safeWidth - 40.0f, 1.0f);
    const float availableHeight = std::max(safeHeight - 20.0f - 76.0f, 1.0f);

    EarthControlPanelLayout layout;
    layout.minWidth = std::min(
        availableWidth, std::min(300.0f, std::max(220.0f, safeWidth * 0.28f)));
    layout.maxWidth = std::min(
        availableWidth, std::max(layout.minWidth, std::min(460.0f, safeWidth * 0.42f)));
    layout.defaultWidth = std::clamp(
        std::min(380.0f, safeWidth * 0.33f), layout.minWidth, layout.maxWidth);

    layout.minHeight = std::min(
        availableHeight, std::min(320.0f, std::max(180.0f, safeHeight * 0.50f)));
    layout.maxHeight = std::min(
        availableHeight, std::max(layout.minHeight, std::min(760.0f, availableHeight)));
    layout.defaultHeight = std::clamp(
        std::min(720.0f, safeHeight * 0.72f), layout.minHeight, layout.maxHeight);
    return layout;
}

}

#endif
