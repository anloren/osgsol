#include "science_ui/science_target_overlay.h"

#include <osg/CoordinateSystemNode>
#include <osg/Matrix>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
constexpr double PI = 3.14159265358979323846;

void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

osg::Vec3d world(double latitude, double longitude)
{
    osg::EllipsoidModel ellipsoid;
    double x = 0.0, y = 0.0, z = 0.0;
    ellipsoid.convertLatLongHeightToXYZ(
        latitude * PI / 180.0, longitude * PI / 180.0, 0.0,
        x, y, z);
    return {x, y, z};
}

struct CameraFixture
{
    osg::Matrixd view;
    osg::Matrixd projection;
    osg::ref_ptr<osg::Viewport> viewport =
        new osg::Viewport(0.0, 0.0, 2048.0, 1152.0);
};

CameraFixture cameraAt(double latitude, double longitude)
{
    const osg::Vec3d surface = world(latitude, longitude);
    const osg::Vec3d eye = surface * 3.0;
    osg::Vec3d up(0.0, 0.0, 1.0);
    if (std::abs((surface / surface.length()) * up) > 0.95)
        up.set(0.0, 1.0, 0.0);
    CameraFixture result;
    result.view.makeLookAt(eye, osg::Vec3d(), up);
    result.projection.makePerspective(45.0, 2048.0 / 1152.0,
                                      1000.0, 50000000.0);
    return result;
}

ScienceTargetOverlayInput pointTarget(double latitude, double longitude)
{
    ScienceTargetOverlayInput input;
    input.requested.kind = ScienceOverlayGeometryKind::Point;
    input.requested.point = {latitude, longitude};
    input.requestedVisible = true;
    input.label = "ERA5 农业气候 · 2017–2025";
    return input;
}

void testPointProjectsAtVisibleMapCenter()
{
    const CameraFixture camera = cameraAt(22.3, 114.17);
    const ScienceTargetOverlayFrame frame = projectScienceTarget(
        pointTarget(22.3, 114.17), camera.view, camera.projection,
        *camera.viewport);
    expect(frame.visible, "front-facing point must be visible");
    expect(frame.requested.size() == 1,
           "point target must produce one marker path");
    expect(std::abs(frame.labelAnchor.x() - 1024.0f) < 2.0f,
           "center point label must project near viewport center");
    expect(std::abs(frame.labelAnchor.y() - 576.0f) < 2.0f,
           "center point label must project near viewport center");
}

void testBacksidePointIsRejected()
{
    const CameraFixture camera = cameraAt(0.0, -90.0);
    const ScienceTargetOverlayFrame frame = projectScienceTarget(
        pointTarget(0.0, 90.0), camera.view, camera.projection,
        *camera.viewport);
    expect(!frame.visible, "point behind globe must be hidden");
    expect(frame.requested.empty(),
           "point behind globe must not create marker geometry");
}

void testRequestedAndActualCoverageRemainDistinct()
{
    const CameraFixture camera = cameraAt(35.6762, 139.6503);
    ScienceTargetOverlayInput input = pointTarget(35.6762, 139.6503);
    input.actual.kind = ScienceOverlayGeometryKind::Bounds;
    input.actual.bounds = {139.5, 35.5, 139.75, 35.75};
    input.actualVisible = true;
    const ScienceTargetOverlayFrame frame = projectScienceTarget(
        input, camera.view, camera.projection, *camera.viewport);
    expect(frame.visible, "Tokyo point and cell must be visible");
    expect(!frame.requested.empty(), "requested marker must be present");
    expect(!frame.actual.empty(), "actual cell outline must be present");
    expect(frame.requested.front().dashed,
           "requested geometry must be dashed");
    expect(!frame.actual.front().dashed,
           "actual provider coverage must be solid");
}

void testAntimeridianBoundsProjectWithoutLongChord()
{
    const CameraFixture camera = cameraAt(0.0, 180.0);
    ScienceTargetOverlayInput input;
    input.requested.kind = ScienceOverlayGeometryKind::Bounds;
    input.requested.bounds = {179.0, -1.0, -179.0, 1.0};
    input.requestedVisible = true;
    const ScienceTargetOverlayFrame frame = projectScienceTarget(
        input, camera.view, camera.projection, *camera.viewport);
    expect(frame.visible, "antimeridian target must remain visible");
    expect(!frame.requested.empty(),
           "antimeridian target must produce clipped edge segments");
    for (const ScienceOverlayPath& path : frame.requested)
        for (const ScienceOverlayVertex& vertex : path.vertices)
        {
            expect(std::isfinite(vertex.x) && std::isfinite(vertex.y),
                   "antimeridian vertices must be finite");
            expect(vertex.x >= 0.0f && vertex.x <= 2048.0f &&
                   vertex.y >= 0.0f && vertex.y <= 1152.0f,
                   "projected vertices must be viewport clipped");
        }
}

void testEra5PointShowsPlannedSourceGridBeforeResultsArrive()
{
    ScienceOverlayGeometry requested;
    requested.kind = ScienceOverlayGeometryKind::Point;
    requested.point = {22.5, 114.0};

    const ScienceTargetSupportDisplay climate =
        scienceTargetSupportDisplay(
            requested, "era5-agricultural-climate", 27800.0);
    expect(climate.geometry.kind == ScienceOverlayGeometryKind::Bounds,
           "ERA5 climate point must display its planned 0.25 degree cell");
    expect(std::abs(climate.geometry.bounds.west - 113.875) < 1e-9 &&
               std::abs(climate.geometry.bounds.south - 22.375) < 1e-9 &&
               std::abs(climate.geometry.bounds.east - 114.125) < 1e-9 &&
               std::abs(climate.geometry.bounds.north - 22.625) < 1e-9,
           "ERA5 climate planned cell must be centered on the requested point");
    expect(climate.estimatedGridCell,
           "pre-result ERA5 support must remain explicitly estimated");
    expect(climate.label.find("0.25") != std::string::npos &&
               climate.label.find("25") != std::string::npos,
           "ERA5 climate support label must expose degree and kilometre scale");

    const ScienceTargetSupportDisplay land =
        scienceTargetSupportDisplay(
            requested, "era5-land-surface-history", 11100.0);
    expect(land.geometry.kind == ScienceOverlayGeometryKind::Bounds &&
               std::abs(land.geometry.bounds.west - 113.95) < 1e-9 &&
               std::abs(land.geometry.bounds.east - 114.05) < 1e-9,
           "ERA5-Land point must display its planned 0.1 degree cell");
}

void testNonGridPointKeepsHonestPointMarker()
{
    ScienceOverlayGeometry requested;
    requested.kind = ScienceOverlayGeometryKind::Point;
    requested.point = {22.5, 114.0};
    const ScienceTargetSupportDisplay display =
        scienceTargetSupportDisplay(
            requested, "alphaearth-foundations", 10.0);
    expect(display.geometry.kind == ScienceOverlayGeometryKind::Point,
           "native pixel resolution must not be invented as analysis coverage");
    expect(!display.estimatedGridCell && display.label.empty(),
           "non-grid point sources must not claim an estimated grid cell");
}
}

int main()
{
    testPointProjectsAtVisibleMapCenter();
    testBacksidePointIsRejected();
    testRequestedAndActualCoverageRemainDistinct();
    testAntimeridianBoundsProjectWithoutLongChord();
    testEra5PointShowsPlannedSourceGridBeforeResultsArrive();
    testNonGridPointKeepsHonestPointMarker();
    std::cout << "ScienceTargetOverlay tests passed" << std::endl;
    return 0;
}
