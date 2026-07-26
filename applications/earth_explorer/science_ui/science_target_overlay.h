#ifndef EARTH_SCIENCE_UI_TARGET_OVERLAY_H
#define EARTH_SCIENCE_UI_TARGET_OVERLAY_H

#include <osg/Matrix>
#include <osg/Vec2>
#include <osg/Viewport>

#include <string>
#include <vector>

enum class ScienceOverlayGeometryKind
{
    Point,
    Bounds,
};

struct ScienceOverlayGeoPoint
{
    double latitude = 0.0;
    double longitude = 0.0;
};

struct ScienceOverlayGeoBounds
{
    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;
};

struct ScienceOverlayGeometry
{
    ScienceOverlayGeometryKind kind = ScienceOverlayGeometryKind::Point;
    ScienceOverlayGeoPoint point;
    ScienceOverlayGeoBounds bounds;
};

struct ScienceTargetOverlayInput
{
    ScienceOverlayGeometry requested;
    ScienceOverlayGeometry actual;
    bool requestedVisible = false;
    bool actualVisible = false;
    std::string label;
};

struct ScienceTargetSupportDisplay
{
    ScienceOverlayGeometry geometry;
    bool estimatedGridCell = false;
    std::string label;
};

// Point-query climate sources return one provider grid cell. Before a result
// supplies the exact returned cell, show the planned source-grid support around
// the requested point instead of an easy-to-miss generic marker. Other point
// sources stay points: native pixel resolution is not the same as an analysis
// footprint and must not be presented as one.
ScienceTargetSupportDisplay scienceTargetSupportDisplay(
    const ScienceOverlayGeometry& requested,
    const std::string& sourceId,
    double nativeResolutionMeters);

struct ScienceOverlayVertex
{
    float x = 0.0f;
    float y = 0.0f;
};

struct ScienceOverlayPath
{
    std::vector<ScienceOverlayVertex> vertices;
    bool closed = false;
    bool dashed = false;
};

struct ScienceTargetOverlayFrame
{
    std::vector<ScienceOverlayPath> requested;
    std::vector<ScienceOverlayPath> actual;
    osg::Vec2f labelAnchor;
    std::string label;
    bool visible = false;
};

ScienceTargetOverlayFrame projectScienceTarget(
    const ScienceTargetOverlayInput& input,
    const osg::Matrixd& view,
    const osg::Matrixd& projection,
    const osg::Viewport& viewport);

#endif
