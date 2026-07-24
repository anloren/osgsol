#ifndef EARTH_CONTROL_LAYOUT_H
#define EARTH_CONTROL_LAYOUT_H

#include <algorithm>

namespace earthui
{

enum class EarthUiModule
{
    Explore,
    Layers,
    Science,
    Live,
    Satellites,
    City3D,
    Tasks,
    Settings,
};

enum class EarthUiContextKind
{
    View,
    Layer,
    DataSpecific,
    LiveWindow,
    Orbit,
    Object,
    Task,
    None,
};

struct EarthUiShellLayout
{
    float topBarHeight;
    float navigationWidth;
    float outerGap;
    float drawerX;
    float drawerY;
    float drawerWidth;
    float drawerHeight;
    float insightWidth;
    float insightTop;
    float insightHeight;
    float commandX;
    float commandY;
    float commandWidth;
    float commandHeight;
    float contextX;
    float contextY;
    float contextWidth;
    float contextHeight;
    float statusHeight;
};

inline EarthUiContextKind contextKindForModule(EarthUiModule module)
{
    switch (module)
    {
    case EarthUiModule::Explore: return EarthUiContextKind::View;
    case EarthUiModule::Layers: return EarthUiContextKind::Layer;
    case EarthUiModule::Science: return EarthUiContextKind::DataSpecific;
    case EarthUiModule::Live: return EarthUiContextKind::LiveWindow;
    case EarthUiModule::Satellites: return EarthUiContextKind::Orbit;
    case EarthUiModule::City3D: return EarthUiContextKind::Object;
    case EarthUiModule::Tasks: return EarthUiContextKind::Task;
    case EarthUiModule::Settings: return EarthUiContextKind::None;
    }
    return EarthUiContextKind::None;
}

inline EarthUiShellLayout computeEarthUiShellLayout(float viewportWidth,
                                                      float viewportHeight,
                                                      bool drawerOpen)
{
    const float width = std::max(viewportWidth, 1.0f);
    const float height = std::max(viewportHeight, 1.0f);

    EarthUiShellLayout layout;
    layout.topBarHeight = std::clamp(height * 0.045f, 42.0f, 50.0f);
    layout.navigationWidth = std::clamp(width * 0.048f, 64.0f, 76.0f);
    layout.outerGap = std::clamp(width * 0.006f, 6.0f, 10.0f);
    layout.statusHeight = std::clamp(height * 0.027f, 22.0f, 28.0f);
    // The context strip contains a label row and a value row. 46 px clips
    // the second row with the production CJK font at common 900/1080 px
    // window heights, so treat 58 px as the real content minimum.
    layout.contextHeight = std::clamp(height * 0.052f, 58.0f, 64.0f);
    layout.commandHeight = std::clamp(height * 0.082f, 72.0f, 90.0f);

    const float bottomStack = layout.statusHeight + layout.contextHeight +
        layout.commandHeight + layout.outerGap * 3.0f;
    // The module rail and its drawer form one docked instrument. Gaps here
    // expose a meaningless strip of the map and make the scrollbar look
    // detached from its panel.
    layout.drawerX = layout.navigationWidth;
    layout.drawerY = layout.topBarHeight;
    layout.drawerWidth = drawerOpen
        ? std::clamp(width * 0.245f, 310.0f, 372.0f) : 0.0f;
    layout.drawerHeight = std::max(
        1.0f, height - layout.drawerY - bottomStack);

    layout.insightWidth = std::clamp(width * 0.245f, 330.0f, 390.0f);
    layout.insightTop = layout.topBarHeight + layout.outerGap;
    layout.insightHeight = std::max(
        1.0f, height - layout.insightTop - bottomStack);

    // The AI command deck belongs to the visible map corridor. When a drawer
    // is open, start after the wider of the native drawer and the 340 px
    // ScienceEarth composer; otherwise the command input visibly sits under
    // the left panel even though both individual windows remain in-bounds.
    const float leftPanelReserve = drawerOpen
        ? std::max(layout.drawerWidth, 340.0f) : 0.0f;
    const float mapLeft = layout.navigationWidth + leftPanelReserve +
        layout.outerGap;
    const float mapRight = width - layout.insightWidth - layout.outerGap * 2.0f;
    const float availableCommandWidth = std::max(1.0f, mapRight - mapLeft);
    layout.commandWidth = std::min(900.0f, availableCommandWidth);
    layout.commandX = mapLeft +
        std::max(0.0f, (availableCommandWidth - layout.commandWidth) * 0.5f);
    layout.commandY = height - layout.statusHeight - layout.contextHeight -
        layout.commandHeight - layout.outerGap * 2.0f;

    // Context is a compact instrument strip, not a full-width black footer.
    // Keep it close to the module rail while leaving the map visible around
    // the centered AI command deck.
    layout.contextX = layout.navigationWidth + layout.outerGap;
    layout.contextY = height - layout.statusHeight - layout.contextHeight;
    layout.contextWidth = std::min(
        std::max(1.0f, width - layout.contextX - layout.outerGap),
        std::clamp(width * 0.30f, 380.0f, 540.0f));
    return layout;
}

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
    float resultTop;
    float resultRight;
};

struct ScienceWorkbenchLayout
{
    float composerWidth;
    float mapWidth;
    float reportX;
    float reportY;
    float reportWidth;
    float reportHeight;
    float aiComposerY;
    bool compact;
};

struct AiCommandRowLayout
{
    float inputWidth;
    float actionWidth;
    bool actionsOnNextLine;
};

struct BoundedWindowLayout
{
    float x;
    float y;
    float width;
    float maxHeight;
};

struct MapToastLayout
{
    float x;
    float y;
    float width;
};

inline AiCommandRowLayout computeAiCommandRowLayout(
    float contentWidth, float itemSpacing, float cancelButtonWidth,
    bool hasActions, bool waitingForVideoEnd)
{
    AiCommandRowLayout layout;
    const int actionCount = waitingForVideoEnd ? 4 : 3;
    layout.actionWidth = hasActions
        ? 52.0f + 40.0f + (waitingForVideoEnd ? 64.0f : 40.0f) +
            (waitingForVideoEnd ? std::max(0.0f, cancelButtonWidth) : 0.0f) +
            std::max(0.0f, itemSpacing) *
                static_cast<float>(actionCount - 1)
        : 0.0f;
    const float safeWidth = std::max(1.0f, contentWidth);
    layout.actionsOnNextLine =
        hasActions && safeWidth < layout.actionWidth + 180.0f;
    layout.inputWidth = layout.actionsOnNextLine
        ? safeWidth : std::max(140.0f, safeWidth - layout.actionWidth);
    return layout;
}

inline BoundedWindowLayout computeCenteredModalLayout(
    float viewportWidth, float viewportHeight, float desiredWidth,
    float desiredMaxHeight)
{
    const float width = std::max(viewportWidth, 1.0f);
    const float height = std::max(viewportHeight, 1.0f);
    const float margin = std::clamp(
        std::min(width, height) * 0.035f, 12.0f, 32.0f);
    BoundedWindowLayout layout;
    layout.width = std::min(
        std::max(280.0f, desiredWidth),
        std::max(1.0f, width - margin * 2.0f));
    layout.maxHeight = std::min(
        std::max(180.0f, desiredMaxHeight),
        std::max(1.0f, height - margin * 2.0f));
    layout.x = width * 0.5f;
    layout.y = height * 0.5f;
    return layout;
}

// Transient map notices live in the map corridor, never on top of the global
// top-bar actions, module drawer, or Insight Lens. This replaces the previous
// unconstrained top-right auto-size badge.
inline MapToastLayout computeMapToastLayout(
    float viewportWidth, float viewportHeight, bool drawerOpen)
{
    const float width = std::max(viewportWidth, 1.0f);
    const EarthUiShellLayout shell =
        computeEarthUiShellLayout(width, viewportHeight, drawerOpen);
    const float leftPanelReserve = drawerOpen
        ? std::max(shell.drawerWidth, 340.0f) : 0.0f;
    const float left = shell.navigationWidth + leftPanelReserve +
        shell.outerGap;
    const float right = width - shell.insightWidth - shell.outerGap * 2.0f;
    const float corridor = std::max(1.0f, right - left);
    MapToastLayout layout;
    layout.width = std::min(520.0f, corridor);
    layout.x = left + std::max(0.0f, (corridor - layout.width) * 0.5f);
    layout.y = shell.topBarHeight + shell.outerGap;
    return layout;
}

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
    const EarthUiShellLayout shell =
        computeEarthUiShellLayout(safeWidth, safeHeight, true);
    const float availableWidth = std::max(
        safeWidth - shell.navigationWidth - shell.outerGap * 3.0f, 1.0f);
    const float availableHeight = std::max(shell.drawerHeight, 1.0f);

    EarthControlPanelLayout layout;
    layout.minWidth = std::min(
        availableWidth, std::min(300.0f, std::max(220.0f, safeWidth * 0.28f)));
    layout.maxWidth = std::min(availableWidth, std::max(
        layout.minWidth, shell.drawerWidth));
    layout.defaultWidth = std::clamp(shell.drawerWidth,
        layout.minWidth, layout.maxWidth);

    layout.minHeight = std::min(
        availableHeight, std::min(320.0f, std::max(180.0f, safeHeight * 0.50f)));
    layout.maxHeight = std::min(availableHeight,
        std::max(layout.minHeight, availableHeight));
    layout.defaultHeight = std::clamp(availableHeight,
        layout.minHeight, layout.maxHeight);
    return layout;
}

inline ScienceWorkspaceLayout computeScienceWorkspaceLayout(
    float viewportWidth, float viewportHeight, bool resultExpanded)
{
    const float safeWidth = std::max(viewportWidth, 1.0f);
    const float safeHeight = std::max(viewportHeight, 1.0f);
    const EarthUiShellLayout shell =
        computeEarthUiShellLayout(safeWidth, safeHeight, true);
    const EarthControlPanelLayout control =
        computeEarthControlPanelLayout(safeWidth, safeHeight);
    const float horizontalMargins = shell.navigationWidth +
        shell.outerGap * 3.0f;
    const float mapMinimum = safeWidth * 0.30f;

    ScienceWorkspaceLayout layout;
    layout.leftWidth = control.defaultWidth;
    if (resultExpanded)
    {
        const float desired = shell.insightWidth;
        const float available = std::max(
            1.0f, safeWidth - horizontalMargins - layout.leftWidth - mapMinimum);
        layout.resultWidth = std::min(desired, available);
    }
    else
        layout.resultWidth = std::min(44.0f, safeWidth * 0.10f);

    layout.resultWidth = std::min(layout.resultWidth, safeWidth * 0.32f);
    layout.resultHeight = std::min(
        shell.insightHeight, std::max(1.0f, safeHeight - shell.insightTop));
    layout.centerMapWidth = std::max(
        0.0f, safeWidth - horizontalMargins - layout.leftWidth - layout.resultWidth);
    layout.resultTop = shell.insightTop;
    layout.resultRight = shell.outerGap;
    return layout;
}

inline ScienceWorkbenchLayout computeScienceWorkbenchLayout(
    float viewportWidth, float viewportHeight)
{
    const float width = std::max(viewportWidth, 1.0f);
    const float height = std::max(viewportHeight, 1.0f);
    const EarthUiShellLayout shell =
        computeEarthUiShellLayout(width, height, true);

    ScienceWorkbenchLayout layout;
    layout.compact = width <= 1100.0f || height <= 640.0f;
    layout.composerWidth = std::clamp(width * 0.18f, 340.0f, 380.0f);
    const float mapLeft = shell.drawerX + layout.composerWidth;
    layout.mapWidth = std::max(480.0f, width - mapLeft);
    layout.aiComposerY = shell.commandY;

    const float margin = layout.compact ? 16.0f : 32.0f;
    const float desiredWidth = layout.compact ? 680.0f : 900.0f;
    const float desiredHeight = layout.compact ? 500.0f : 680.0f;
    layout.reportWidth = std::min(desiredWidth, width - margin * 2.0f);
    const float reportTopMinimum = shell.topBarHeight + margin;
    const float availableReportHeight = std::max(
        1.0f, layout.aiComposerY - margin - reportTopMinimum);
    layout.reportHeight = std::min(desiredHeight, availableReportHeight);
    layout.reportX = std::max(margin, (width - layout.reportWidth) * 0.5f);
    layout.reportY = std::max(reportTopMinimum,
        (layout.aiComposerY - layout.reportHeight) * 0.5f);
    return layout;
}

}

#endif
