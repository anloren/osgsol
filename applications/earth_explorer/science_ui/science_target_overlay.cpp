#include "science_target_overlay.h"

#include <osg/CoordinateSystemNode>
#include <osg/Vec3>
#include <osg/Vec4>

#include <algorithm>
#include <cmath>

namespace
{
constexpr double PI = 3.14159265358979323846;
constexpr int EDGE_SAMPLES = 16;
constexpr int MARKER_SEGMENTS = 24;
constexpr float MARKER_RADIUS = 9.0f;

struct ProjectedPoint
{
    float x = 0.0f;
    float y = 0.0f;
    bool frontFacing = false;
    bool finite = false;
};

double wrapLongitude(double longitude)
{
    while (longitude > 180.0) longitude -= 360.0;
    while (longitude < -180.0) longitude += 360.0;
    return longitude;
}

osg::Vec3d worldPoint(const ScienceOverlayGeoPoint& point)
{
    osg::EllipsoidModel ellipsoid;
    double x = 0.0, y = 0.0, z = 0.0;
    ellipsoid.convertLatLongHeightToXYZ(
        point.latitude * PI / 180.0,
        wrapLongitude(point.longitude) * PI / 180.0,
        0.0, x, y, z);
    return {x, y, z};
}

ProjectedPoint projectPoint(const ScienceOverlayGeoPoint& point,
                            const osg::Matrixd& view,
                            const osg::Matrixd& projection,
                            const osg::Viewport& viewport,
                            const osg::Vec3d& eye)
{
    const osg::Vec3d world = worldPoint(point);
    ProjectedPoint output;
    const osg::Vec3d normal = world / world.length();
    const osg::Vec3d toEye = eye - world;
    output.frontFacing = normal * toEye > 0.0;
    const osg::Vec4d clip = osg::Vec4d(world.x(), world.y(), world.z(), 1.0) *
        view * projection;
    if (!output.frontFacing || !std::isfinite(clip.w()) || clip.w() <= 0.0)
        return output;
    const double ndcX = clip.x() / clip.w();
    const double ndcY = clip.y() / clip.w();
    if (!std::isfinite(ndcX) || !std::isfinite(ndcY)) return output;
    output.x = static_cast<float>(
        viewport.x() + (ndcX + 1.0) * 0.5 * viewport.width());
    output.y = static_cast<float>(
        viewport.y() + (1.0 - (ndcY + 1.0) * 0.5) * viewport.height());
    output.finite = true;
    return output;
}

bool clipTest(float p, float q, float& first, float& last)
{
    if (p == 0.0f) return q >= 0.0f;
    const float ratio = q / p;
    if (p < 0.0f)
    {
        if (ratio > last) return false;
        if (ratio > first) first = ratio;
    }
    else
    {
        if (ratio < first) return false;
        if (ratio < last) last = ratio;
    }
    return true;
}

bool clipSegment(const osg::Viewport& viewport,
                 ScienceOverlayVertex& start,
                 ScienceOverlayVertex& end)
{
    const float minimumX = static_cast<float>(viewport.x());
    const float minimumY = static_cast<float>(viewport.y());
    const float maximumX = minimumX + static_cast<float>(viewport.width());
    const float maximumY = minimumY + static_cast<float>(viewport.height());
    const float deltaX = end.x - start.x;
    const float deltaY = end.y - start.y;
    float first = 0.0f, last = 1.0f;
    if (!clipTest(-deltaX, start.x - minimumX, first, last) ||
        !clipTest(deltaX, maximumX - start.x, first, last) ||
        !clipTest(-deltaY, start.y - minimumY, first, last) ||
        !clipTest(deltaY, maximumY - start.y, first, last))
        return false;
    const ScienceOverlayVertex original = start;
    start = {original.x + first * deltaX, original.y + first * deltaY};
    end = {original.x + last * deltaX, original.y + last * deltaY};
    return true;
}

void appendSegment(const ProjectedPoint& startPoint,
                   const ProjectedPoint& endPoint,
                   const osg::Viewport& viewport,
                   bool dashed,
                   std::vector<ScienceOverlayPath>& output)
{
    if (!startPoint.frontFacing || !endPoint.frontFacing ||
        !startPoint.finite || !endPoint.finite)
        return;
    ScienceOverlayVertex start{startPoint.x, startPoint.y};
    ScienceOverlayVertex end{endPoint.x, endPoint.y};
    if (!clipSegment(viewport, start, end)) return;
    ScienceOverlayPath path;
    path.vertices = {start, end};
    path.dashed = dashed;
    output.push_back(std::move(path));
}

ScienceOverlayGeoPoint interpolate(const ScienceOverlayGeoPoint& start,
                                   const ScienceOverlayGeoPoint& end,
                                   double amount)
{
    return {start.latitude + (end.latitude - start.latitude) * amount,
            start.longitude + (end.longitude - start.longitude) * amount};
}

void appendEdge(const ScienceOverlayGeoPoint& start,
                const ScienceOverlayGeoPoint& end,
                const osg::Matrixd& view,
                const osg::Matrixd& projection,
                const osg::Viewport& viewport,
                const osg::Vec3d& eye,
                bool dashed,
                std::vector<ScienceOverlayPath>& output)
{
    ProjectedPoint previous = projectPoint(
        start, view, projection, viewport, eye);
    for (int sample = 1; sample <= EDGE_SAMPLES; ++sample)
    {
        const ScienceOverlayGeoPoint point = interpolate(
            start, end, static_cast<double>(sample) / EDGE_SAMPLES);
        const ProjectedPoint current = projectPoint(
            point, view, projection, viewport, eye);
        appendSegment(previous, current, viewport, dashed, output);
        previous = current;
    }
}

ScienceOverlayGeoPoint boundsCenter(const ScienceOverlayGeoBounds& bounds)
{
    double east = bounds.east;
    if (east < bounds.west) east += 360.0;
    return {(bounds.south + bounds.north) * 0.5,
            wrapLongitude((bounds.west + east) * 0.5)};
}

void appendBounds(const ScienceOverlayGeoBounds& bounds,
                  const osg::Matrixd& view,
                  const osg::Matrixd& projection,
                  const osg::Viewport& viewport,
                  const osg::Vec3d& eye,
                  bool dashed,
                  std::vector<ScienceOverlayPath>& output)
{
    double east = bounds.east;
    if (east < bounds.west) east += 360.0;
    const ScienceOverlayGeoPoint southWest{bounds.south, bounds.west};
    const ScienceOverlayGeoPoint southEast{bounds.south, east};
    const ScienceOverlayGeoPoint northEast{bounds.north, east};
    const ScienceOverlayGeoPoint northWest{bounds.north, bounds.west};
    appendEdge(southWest, southEast, view, projection, viewport,
               eye, dashed, output);
    appendEdge(southEast, northEast, view, projection, viewport,
               eye, dashed, output);
    appendEdge(northEast, northWest, view, projection, viewport,
               eye, dashed, output);
    appendEdge(northWest, southWest, view, projection, viewport,
               eye, dashed, output);
}

ProjectedPoint appendGeometry(const ScienceOverlayGeometry& geometry,
                              const osg::Matrixd& view,
                              const osg::Matrixd& projection,
                              const osg::Viewport& viewport,
                              const osg::Vec3d& eye,
                              bool dashed,
                              std::vector<ScienceOverlayPath>& output)
{
    if (geometry.kind == ScienceOverlayGeometryKind::Bounds)
    {
        appendBounds(geometry.bounds, view, projection, viewport,
                     eye, dashed, output);
        return projectPoint(boundsCenter(geometry.bounds), view, projection,
                            viewport, eye);
    }

    const ProjectedPoint center = projectPoint(
        geometry.point, view, projection, viewport, eye);
    if (!center.frontFacing || !center.finite) return center;
    const float minimumX = static_cast<float>(viewport.x());
    const float minimumY = static_cast<float>(viewport.y());
    const float maximumX = minimumX + static_cast<float>(viewport.width());
    const float maximumY = minimumY + static_cast<float>(viewport.height());
    if (center.x < minimumX || center.x > maximumX ||
        center.y < minimumY || center.y > maximumY)
        return center;
    ScienceOverlayPath marker;
    marker.closed = true;
    marker.dashed = dashed;
    marker.vertices.reserve(MARKER_SEGMENTS);
    for (int segment = 0; segment < MARKER_SEGMENTS; ++segment)
    {
        const double angle = 2.0 * PI * segment / MARKER_SEGMENTS;
        marker.vertices.push_back({
            center.x + MARKER_RADIUS * static_cast<float>(std::cos(angle)),
            center.y + MARKER_RADIUS * static_cast<float>(std::sin(angle))});
    }
    output.push_back(std::move(marker));
    return center;
}
}

ScienceTargetSupportDisplay scienceTargetSupportDisplay(
    const ScienceOverlayGeometry& requested,
    const std::string& sourceId,
    double nativeResolutionMeters)
{
    ScienceTargetSupportDisplay output;
    output.geometry = requested;
    if (requested.kind != ScienceOverlayGeometryKind::Point) return output;

    double cellDegrees = 0.0;
    if (sourceId == "era5-agricultural-climate")
        cellDegrees = 0.25;
    else if (sourceId == "era5-land-surface-history")
        cellDegrees = 0.1;
    else
        return output;

    const double halfCell = cellDegrees * 0.5;
    output.geometry.kind = ScienceOverlayGeometryKind::Bounds;
    output.geometry.bounds = {
        wrapLongitude(requested.point.longitude - halfCell),
        std::max(-90.0, requested.point.latitude - halfCell),
        wrapLongitude(requested.point.longitude + halfCell),
        std::min(90.0, requested.point.latitude + halfCell)};
    output.estimatedGridCell = true;
    if (cellDegrees == 0.25)
        output.label = u8"计划分析网格约 0.25° / 25–28 km";
    else
        output.label = u8"计划分析网格约 0.1° / 11 km";
    if (nativeResolutionMeters <= 0.0)
        output.label += u8"（分辨率元数据待返回）";
    return output;
}

ScienceTargetOverlayFrame projectScienceTarget(
    const ScienceTargetOverlayInput& input,
    const osg::Matrixd& view,
    const osg::Matrixd& projection,
    const osg::Viewport& viewport)
{
    ScienceTargetOverlayFrame output;
    output.label = input.label;
    const osg::Vec3d eye = osg::Matrixd::inverse(view).getTrans();
    ProjectedPoint label;
    if (input.requestedVisible)
        label = appendGeometry(input.requested, view, projection, viewport,
                               eye, true, output.requested);
    if (input.actualVisible)
    {
        const ProjectedPoint actual = appendGeometry(
            input.actual, view, projection, viewport,
            eye, false, output.actual);
        if (!label.finite) label = actual;
    }
    output.visible = !output.requested.empty() || !output.actual.empty();
    if (label.finite)
        output.labelAnchor.set(label.x, label.y);
    else if (output.visible)
    {
        const std::vector<ScienceOverlayPath>& paths =
            !output.requested.empty() ? output.requested : output.actual;
        output.labelAnchor.set(paths.front().vertices.front().x,
                               paths.front().vertices.front().y);
    }
    return output;
}
