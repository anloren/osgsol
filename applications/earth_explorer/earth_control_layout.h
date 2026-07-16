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

struct ScienceWorkspaceLayout
{
    float leftWidth;
    float resultWidth;
    float resultHeight;
    float centerMapWidth;
};

template<typename EndLeftPanel, typename DrawScienceResults>
inline void finishLeftThenDrawScienceResults(
    EndLeftPanel endLeftPanel,
    DrawScienceResults drawScienceResults)
{
    endLeftPanel();
    drawScienceResults();
}

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
        availableWidth,
        std::max(layout.minWidth, std::min(460.0f, safeWidth * 0.34f)));
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

inline ScienceWorkspaceLayout computeScienceWorkspaceLayout(
    float viewportWidth, float viewportHeight, bool resultExpanded)
{
    const float safeWidth = std::max(viewportWidth, 1.0f);
    const float safeHeight = std::max(viewportHeight, 1.0f);
    const EarthControlPanelLayout control =
        computeEarthControlPanelLayout(safeWidth, safeHeight);
    const float horizontalMargins = std::min(40.0f, safeWidth * 0.08f);
    const float mapMinimum = safeWidth * 0.30f;

    ScienceWorkspaceLayout layout;
    layout.leftWidth = control.defaultWidth;
    if (resultExpanded)
    {
        const float desired = std::min(420.0f, safeWidth * 0.30f);
        const float available = std::max(
            1.0f, safeWidth - horizontalMargins - layout.leftWidth - mapMinimum);
        layout.resultWidth = std::min(desired, available);
    }
    else
        layout.resultWidth = std::min(44.0f, safeWidth * 0.10f);

    layout.resultWidth = std::min(layout.resultWidth, safeWidth * 0.32f);
    layout.resultHeight = std::min(
        std::min(760.0f, safeHeight * 0.72f),
        std::max(1.0f, safeHeight - 20.0f - 76.0f));
    layout.centerMapWidth = std::max(
        0.0f, safeWidth - horizontalMargins - layout.leftWidth - layout.resultWidth);
    return layout;
}

}

#endif
