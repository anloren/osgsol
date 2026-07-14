#include "SciencePreviewRuntime.h"
#include "SciencePreviewSupport.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <ogr_spatialref.h>
#include <osg/CullFace>
#include <osg/Depth>
#include <osg/Geode>
#include <readerwriter/EarthManipulator.h>
#include <applications/earth_explorer/science_preview_layer.h>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    bool near(double actual, double expected, double tolerance = 1e-8)
    {
        return std::abs(actual - expected) <= tolerance;
    }

    void testSouthUpRasterStaysSouthAtTextureBottom()
    {
        const int width = 2, height = 2;
        const std::vector<std::int8_t> southUpRgb = {
            -127, -64, 0,  -96, -32, 32,
              64,  96, 127,  32,  64, 96,
        };
        const std::vector<unsigned char> masks(width * height * 3, 255);
        const std::vector<unsigned char> rgba = earthscience::composePreviewRgba(
            southUpRgb, masks, width, height);

        require(rgba.size() == static_cast<std::size_t>(width * height * 4),
                "RGBA conversion returned the wrong size");
        require(rgba[0] == earthscience::scienceDisplayValue(-127) &&
                rgba[1] == earthscience::scienceDisplayValue(-64) &&
                rgba[2] == earthscience::scienceDisplayValue(0),
                "source south row was not kept at texture v=0");
        require(rgba[width * 4] == earthscience::scienceDisplayValue(64),
                "source north row was not kept at texture v=1");
        require(rgba[3] == 166,
                "valid false color is too opaque to register against the base map");
    }

    void testProjectedRasterBuildsAnExactWgs84Grid()
    {
        OGRSpatialReference projected;
        projected.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        require(projected.importFromEPSG(32610) == OGRERR_NONE,
                "could not create the projected fixture CRS");
        char* wkt = nullptr;
        require(projected.exportToWkt(&wkt) == OGRERR_NONE && wkt,
                "could not serialize the projected fixture CRS");

        const double transform[6] =
            {581920.0, 10.0, 0.0, 4096000.0, 0.0, 10.0};
        std::string error;
        const earthscience::ScienceGroundGrid grid =
            earthscience::buildPreviewGroundGrid(
                8192, 8192, transform, wkt, 3, 3, error);
        CPLFree(wkt);

        require(error.empty(), "exact ground grid construction failed");
        require(grid.columns == 3 && grid.rows == 3 && grid.points.size() == 9,
                "exact ground grid dimensions changed");

        OGRSpatialReference wgs84;
        wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        require(wgs84.importFromEPSG(4326) == OGRERR_NONE,
                "could not create the WGS84 fixture CRS");
        std::unique_ptr<OGRCoordinateTransformation,
                        decltype(&OCTDestroyCoordinateTransformation)> toWgs84(
            OGRCreateCoordinateTransformation(&projected, &wgs84),
            OCTDestroyCoordinateTransformation);
        require(toWgs84 != nullptr, "could not create the fixture transformation");

        double expectedLon = 581920.0;
        double expectedLat = 4096000.0;
        require(toWgs84->Transform(1, &expectedLon, &expectedLat),
                "could not transform the expected southwest corner");
        require(near(grid.points.front().longitude, expectedLon) &&
                near(grid.points.front().latitude, expectedLat),
                "grid southwest corner is not tied to the source affine transform");

        const earthscience::ScienceGroundPoint& northwest = grid.points[6];
        require(northwest.latitude > grid.points.front().latitude,
                "grid north edge is not north of the south edge");
        require(std::abs(northwest.longitude - grid.points.front().longitude) > 1e-4,
                "projected raster was flattened into an axis-aligned lon/lat rectangle");
    }

    void testRequestedViewSelectsAVisibleNativeResolutionWindow()
    {
        OGRSpatialReference projected;
        projected.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        require(projected.importFromEPSG(32610) == OGRERR_NONE,
                "could not create the window fixture CRS");
        OGRSpatialReference wgs84;
        wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        require(wgs84.importFromEPSG(4326) == OGRERR_NONE,
                "could not create the window WGS84 CRS");
        std::unique_ptr<OGRCoordinateTransformation,
                        decltype(&OCTDestroyCoordinateTransformation)> toWgs84(
            OGRCreateCoordinateTransformation(&projected, &wgs84),
            OCTDestroyCoordinateTransformation);
        require(toWgs84 != nullptr, "could not create the window fixture transform");

        double longitude = 622880.0;
        double latitude = 4136960.0;
        require(toWgs84->Transform(1, &longitude, &latitude),
                "could not transform the window fixture center");
        char* wkt = nullptr;
        require(projected.exportToWkt(&wkt) == OGRERR_NONE && wkt,
                "could not serialize the window fixture CRS");
        const double transform[6] =
            {581920.0, 10.0, 0.0, 4096000.0, 0.0, 10.0};
        std::string error;
        const earthscience::ScienceRasterWindow nativeWindow =
            earthscience::derivePreviewWindow(
                8192, 8192, transform, wkt, latitude, longitude,
                2560.0, 256, error);
        require(error.empty(), "native-resolution view window derivation failed");
        require(nativeWindow.x == 3968 && nativeWindow.y == 3968 &&
                nativeWindow.width == 256 && nativeWindow.height == 256,
                "2.56 km request did not select the centered 256x256 native window");
        require(near(nativeWindow.displayResolutionMeters, 10.0),
                "native window is not displayed at the source 10 m resolution");

        error.clear();
        const earthscience::ScienceRasterWindow fullWindow =
            earthscience::derivePreviewWindow(
                8192, 8192, transform, wkt, latitude, longitude,
                81920.0, 256, error);
        CPLFree(wkt);
        require(error.empty(), "full-tile view window derivation failed");
        require(fullWindow.x == 0 && fullWindow.y == 0 &&
                fullWindow.width == 8192 && fullWindow.height == 8192,
                "wide view did not select the complete source tile");
        require(near(fullWindow.displayResolutionMeters, 320.0),
                "wide view does not report its honest display resolution");
    }

    void testViewTargetDoesNotMoveWhenOnlyCameraHeightChanges()
    {
        osg::ref_ptr<osgVerse::EarthManipulator> manipulator =
            new osgVerse::EarthManipulator;
        osg::ref_ptr<osg::EllipsoidModel> ellipsoid = new osg::EllipsoidModel;
        manipulator->setEllipsoid(ellipsoid.get());

        double x = 0.0, y = 0.0, z = 0.0;
        ellipsoid->convertLatLongHeightToXYZ(
            osg::DegreesToRadians(35.36), osg::DegreesToRadians(138.73), 0.0,
            x, y, z);
        manipulator->setCenter(osg::Vec3d(x, y, z));
        manipulator->setDistance(1000.0);
        const osg::Vec3d low = manipulator->computeViewPointLatLonHeight();
        manipulator->setDistance(1000000.0);
        const osg::Vec3d high = manipulator->computeViewPointLatLonHeight();

        require(near(low[0], osg::DegreesToRadians(35.36)) &&
                near(low[1], osg::DegreesToRadians(138.73)),
                "view target does not resolve to the requested ground point");
        require((low - high).length() < 1e-9,
                "view target moved when only camera height changed");
    }

    void testFalseColorMeaningIsMachineReadable()
    {
        const earthscience::AlphaEarthSourceDescriptor descriptor;
        require(descriptor.visualization == "false-color embedding composite",
                "preview visualization is not identified as false color");
        require(descriptor.redBand == "A01" && descriptor.greenBand == "A16" &&
                descriptor.blueBand == "A09",
                "preview color channels are not declared");
        require(near(descriptor.displayMinimum, -0.3) &&
                near(descriptor.displayMaximum, 0.3),
                "preview display range is not declared");
    }

    void testTerrainCannotHideAReadyScienceArtifact()
    {
        earthscience::AlphaEarthPreviewArtifact artifact;
        artifact.width = 2;
        artifact.height = 2;
        artifact.rgba = std::make_shared<const std::vector<unsigned char>>(
            16, 166);
        auto grid = std::make_shared<earthscience::ScienceGroundGrid>();
        grid->columns = 2;
        grid->rows = 2;
        grid->points = {
            {138.70, 35.34}, {138.76, 35.34},
            {138.70, 35.40}, {138.76, 35.40},
        };
        artifact.groundGrid = grid;

        osg::ref_ptr<osg::Node> node = createSciencePreviewArtifactNode(artifact);
        osg::Geode* geode = dynamic_cast<osg::Geode*>(node.get());
        require(geode && geode->getNumDrawables() == 1,
                "science artifact node was not created");
        osg::StateSet* state = geode->getDrawable(0)->getStateSet();
        require(state != nullptr, "science artifact has no render state");
        const osg::Depth* depth = dynamic_cast<const osg::Depth*>(
            state->getAttribute(osg::StateAttribute::DEPTH));
        require(depth && depth->getFunction() == osg::Depth::ALWAYS &&
                !depth->getWriteMask(),
                "terrain depth can still bury a ready science artifact");
        const osg::CullFace* cull = dynamic_cast<const osg::CullFace*>(
            state->getAttribute(osg::StateAttribute::CULLFACE));
        require(cull && cull->getMode() == osg::CullFace::BACK,
                "depth-independent artifact can leak through the globe back face");
    }
}

int main()
{
    testSouthUpRasterStaysSouthAtTextureBottom();
    testProjectedRasterBuildsAnExactWgs84Grid();
    testRequestedViewSelectsAVisibleNativeResolutionWindow();
    testViewTargetDoesNotMoveWhenOnlyCameraHeightChanges();
    testFalseColorMeaningIsMachineReadable();
    testTerrainCannotHideAReadyScienceArtifact();
    std::cout << "[OK] ScienceEarth preview georeference, orientation, target and legend\n";
    return 0;
}
