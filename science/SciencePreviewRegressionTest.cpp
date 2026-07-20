#include "SciencePreviewRuntime.h"
#include "SciencePreviewSupport.h"
#include "ScienceQueryService.h"

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
#include <osg/Geometry>
#include <osg/Program>
#include <osgUtil/UpdateVisitor>
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

    earthscience::ScienceSourceDescriptor makeLayerSource()
    {
        earthscience::ScienceSourceDescriptor source;
        source.id = "alphaearth-foundations";
        source.firstYear = 2017;
        source.lastYear = 2025;
        source.nativeResolutionMeters = 10.0;
        source.health = earthscience::ScienceSourceHealth::Ready;
        source.variables = {
            {"A01", "Embedding A01", "1", "embedding", 1},
            {"A16", "Embedding A16", "1", "embedding", 1},
            {"A09", "Embedding A09", "1", "embedding", 1},
            {"embedding64", "Embedding A00-A63", "1", "embedding", 64},
        };
        source.capabilities.pointQuery = true;
        source.capabilities.boundingBoxQuery = true;
        source.capabilities.explicitYears = true;
        source.capabilities.rasterLayerOutput = true;
        source.capabilities.analysisOutput = true;
        source.capabilities.minimumSpanMeters = 2560.0;
        source.capabilities.maximumSpanMeters = 81920.0;
        earthscience::ScienceVisualizationDescriptor visualization;
        visualization.id = "false-color-a01-a16-a09";
        visualization.channelVariables = {"A01", "A16", "A09"};
        source.visualizations.push_back(visualization);
        return source;
    }

    earthscience::GeoTemporalQuery makeLayerQuery(int year)
    {
        earthscience::GeoTemporalQuery query;
        query.sourceId = "alphaearth-foundations";
        query.geometry.point.latitude = 35.36;
        query.geometry.point.longitude = 138.73;
        query.geometry.requestedSpanMeters = 20000.0;
        query.time.explicitYears = {year};
        query.variables = {"A01", "A16", "A09"};
        query.targetResolutionMeters = 10.0;
        query.visualizationId = "false-color-a01-a16-a09";
        return query;
    }

    earthscience::GeoTemporalQuery makeLayerAnalysisQuery()
    {
        earthscience::GeoTemporalQuery query;
        query.sourceId = "alphaearth-foundations";
        query.geometry.kind = earthscience::ScienceGeometryKind::BoundingBox;
        query.geometry.bounds = {138.70, 35.34, 138.76, 35.40};
        query.geometry.requestedSpanMeters = 6000.0;
        query.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        query.time.explicitYears = {2017, 2025};
        query.variables = {"embedding64"};
        query.targetResolutionMeters = 10.0;
        query.outputKind = earthscience::ScienceOutputKind::Analysis;
        query.priority = earthscience::SciencePriority::InteractiveResearch;
        query.analysis.kind =
            earthscience::ScienceAnalysisKind::RegionalChange;
        query.analysis.baselineYear = 2017;
        query.analysis.comparisonYear = 2025;
        query.analysis.gridSize = 4;
        return query;
    }

    std::shared_ptr<const earthscience::ScienceArtifact> makeLayerArtifact(
        const std::string& id, std::uint64_t generation)
    {
        auto artifact = std::make_shared<earthscience::ScienceArtifact>();
        artifact->artifactId = id;
        artifact->generation = generation;
        artifact->raster.width = 2;
        artifact->raster.height = 2;
        artifact->raster.bounds = {138.70, 35.34, 138.76, 35.40};
        artifact->raster.rgba =
            std::make_shared<const std::vector<unsigned char>>(16, 166);
        auto grid = std::make_shared<earthscience::ScienceGroundGrid>();
        grid->columns = 2;
        grid->rows = 2;
        grid->points = {
            {138.70, 35.34}, {138.76, 35.34},
            {138.70, 35.40}, {138.76, 35.40},
        };
        artifact->raster.groundGrid = grid;
        return artifact;
    }

    class LayerProvider : public earthscience::IScienceProvider
    {
    public:
        earthscience::ScienceSourceDescriptor descriptor() const override
        {
            return makeLayerSource();
        }

        std::uint64_t submit(
            const earthscience::GeoTemporalQuery&) override
        {
            _snapshot = earthscience::ScienceProviderSnapshot();
            _snapshot.generation = ++_generation;
            _snapshot.state = earthscience::ScienceJobState::Queued;
            return _generation;
        }

        earthscience::ScienceProviderSnapshot snapshot() const override
        {
            return _snapshot;
        }

        void cancel(std::uint64_t generation) override
        {
            if (generation != _generation) return;
            _snapshot.state = earthscience::ScienceJobState::Cancelled;
        }

        void clear() override
        {
            _snapshot = earthscience::ScienceProviderSnapshot();
        }

        void publish(earthscience::ScienceJobState state,
                     const earthscience::ScienceProgress& progress,
                     const std::string& message,
                     std::shared_ptr<const earthscience::ScienceArtifact>
                         artifact = nullptr)
        {
            _snapshot.generation = _generation;
            _snapshot.state = state;
            _snapshot.progress = progress;
            _snapshot.message = message;
            _snapshot.artifact = std::move(artifact);
        }

        std::uint64_t generation() const { return _generation; }

    private:
        std::uint64_t _generation = 0;
        earthscience::ScienceProviderSnapshot _snapshot;
    };

    void updateLayer(SciencePreviewLayer& layer)
    {
        osgUtil::UpdateVisitor visitor;
        layer.accept(visitor);
    }

    struct RasterCapture
    {
        int publishCount = 0;
        int clearCount = 0;
        std::uint64_t generation = 0;
        int width = 0;
        int height = 0;
        std::vector<unsigned char> pixels;
    };

    bool captureRaster(const OsgSolGeoRasterFrameV1* frame,
                       void* userData, char*, std::size_t)
    {
        RasterCapture* capture = static_cast<RasterCapture*>(userData);
        if (!capture || !frame ||
            frame->structSize < sizeof(OsgSolGeoRasterFrameV1) ||
            !frame->rgba || frame->width <= 0 || frame->height <= 0)
            return false;
        ++capture->publishCount;
        capture->generation = frame->generation;
        capture->width = frame->width;
        capture->height = frame->height;
        const std::size_t byteCount = static_cast<std::size_t>(
            frame->rowBytes * static_cast<std::uint64_t>(frame->height));
        capture->pixels.assign(frame->rgba, frame->rgba + byteCount);
        return true;
    }

    void captureClear(std::uint64_t generation, void* userData)
    {
        RasterCapture* capture = static_cast<RasterCapture*>(userData);
        if (!capture) return;
        ++capture->clearCount;
        capture->generation = generation;
        capture->pixels.clear();
    }

    OsgSolGeoRasterBridgeV1 captureBridge(RasterCapture& capture)
    {
        return OsgSolGeoRasterBridgeV1{
            sizeof(OsgSolGeoRasterBridgeV1),
            &capture,
            captureRaster,
            captureClear,
        };
    }

    earthscience::ScienceProgress layerProgress(
        earthscience::ScienceProgressStage stage,
        std::uint64_t completed = 0, std::uint64_t total = 0)
    {
        earthscience::ScienceProgress progress;
        progress.stage = stage;
        progress.completedUnits = completed;
        progress.totalUnits = total;
        progress.determinate = total != 0;
        progress.unit = total == 0 ? std::string() : "artifact";
        return progress;
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

    void testLayerRetainsLastGoodUntilExplicitReplacementOrRemoval()
    {
        auto registry =
            std::make_unique<earthscience::ScienceSourceRegistry>();
        auto provider = std::make_unique<LayerProvider>();
        LayerProvider* providerPointer = provider.get();
        std::string error;
        require(registry->add(std::move(provider), error),
                "layer provider registration failed");
        earthscience::ScienceQueryService service(std::move(registry));
        RasterCapture capture;
        osg::ref_ptr<SciencePreviewLayer> layer =
            new SciencePreviewLayer(&service);
        const OsgSolGeoRasterBridgeV1 bridge = captureBridge(capture);
        layer->bindGeoRaster(&bridge);
        layer->setVisible(true);

        const std::uint64_t firstJob = service.submit(makeLayerQuery(2025));
        providerPointer->publish(
            earthscience::ScienceJobState::Ready,
            layerProgress(earthscience::ScienceProgressStage::Ready, 1, 1),
            "Ready",
            makeLayerArtifact("first", providerPointer->generation()));
        require(service.snapshot().state ==
                    earthscience::ScienceJobState::Ready,
                "first layer artifact did not complete");
        updateLayer(*layer);
        require(layer->hasArtifact() &&
                    layer->artifactGeneration() == firstJob,
                "layer did not materialize the first service artifact");
        require(capture.publishCount == 1 && capture.width == 2 &&
                    capture.height == 2 && capture.pixels.size() == 16,
                "first display generation was not published exactly once");
        updateLayer(*layer);
        require(capture.publishCount == 1,
                "unchanged display generation was published twice");

        const std::uint64_t secondJob = service.submit(makeLayerQuery(2018));
        providerPointer->publish(
            earthscience::ScienceJobState::Fetching,
            layerProgress(earthscience::ScienceProgressStage::Reading),
            "Fetching");
        require(service.snapshot().lastSuccessfulArtifact != nullptr,
                "replacement fetch lost the service artifact");
        updateLayer(*layer);
        require(layer->artifactGeneration() == firstJob,
                "replacement fetch removed the visible last-good artifact");
        require(capture.publishCount == 1,
                "fetching replacement republished the last-good artifact");

        providerPointer->publish(
            earthscience::ScienceJobState::Failed,
            layerProgress(earthscience::ScienceProgressStage::Failed),
            "Failure");
        require(service.snapshot().state ==
                    earthscience::ScienceJobState::Failed,
                "replacement failure was not published");
        updateLayer(*layer);
        require(layer->artifactGeneration() == firstJob,
                "replacement failure removed the visible last-good artifact");

        const std::uint64_t thirdJob = service.submit(makeLayerQuery(2019));
        providerPointer->publish(
            earthscience::ScienceJobState::Ready,
            layerProgress(earthscience::ScienceProgressStage::Ready, 1, 1),
            "Ready",
            makeLayerArtifact("third", providerPointer->generation()));
        service.snapshot();
        updateLayer(*layer);
        require(thirdJob > secondJob &&
                    layer->artifactGeneration() == thirdJob,
                "new successful artifact did not replace the last-good node");
        require(capture.publishCount == 2,
                "new successful display generation was not published once");

        const earthscience::ScienceJobSnapshot beforeVisibility =
            service.snapshot();
        layer->setVisible(false);
        updateLayer(*layer);
        const earthscience::ScienceJobSnapshot afterVisibility =
            service.snapshot();
        require(beforeVisibility.jobId == afterVisibility.jobId &&
                    beforeVisibility.state == afterVisibility.state &&
                    beforeVisibility.lastSuccessfulArtifact ==
                        afterVisibility.lastSuccessfulArtifact,
                "layer visibility mutated service state");
        require(capture.clearCount == 1 && capture.pixels.empty(),
                "disabling the layer did not clear the host overlay");

        layer->removeArtifact();
        updateLayer(*layer);
        require(!layer->hasArtifact() &&
                    layer->artifactGeneration() == 0,
                "explicit layer removal left an artifact node");
        require(capture.clearCount == 2,
                "explicit removal did not clear the host overlay");
    }

    void testLayerRendersOnlyTheExplicitDisplayArtifact()
    {
        auto registry =
            std::make_unique<earthscience::ScienceSourceRegistry>();
        auto provider = std::make_unique<LayerProvider>();
        LayerProvider* providerPointer = provider.get();
        std::string error;
        require(registry->add(std::move(provider), error),
                "display-selection provider registration failed");
        earthscience::ScienceQueryService service(std::move(registry));
        RasterCapture capture;
        osg::ref_ptr<SciencePreviewLayer> layer =
            new SciencePreviewLayer(&service);
        const OsgSolGeoRasterBridgeV1 bridge = captureBridge(capture);
        layer->bindGeoRaster(&bridge);
        layer->setVisible(true);

        const std::uint64_t previewJob = service.submit(makeLayerQuery(2025));
        providerPointer->publish(
            earthscience::ScienceJobState::Ready,
            layerProgress(earthscience::ScienceProgressStage::Ready, 1, 1),
            "Ready",
            makeLayerArtifact("preview", providerPointer->generation()));
        earthscience::ScienceJobSnapshot snapshot = service.snapshot();
        updateLayer(*layer);
        require(snapshot.displayArtifact &&
                    snapshot.displayArtifact->artifactId == "preview" &&
                    layer->artifactGeneration() == previewJob,
                "older preview was not the initial display artifact");

        const std::uint64_t analysisJob =
            service.submit(makeLayerAnalysisQuery());
        providerPointer->publish(
            earthscience::ScienceJobState::Ready,
            layerProgress(earthscience::ScienceProgressStage::Ready, 1, 1),
            "Ready",
            makeLayerArtifact("analysis", providerPointer->generation()));
        snapshot = service.snapshot();
        require(snapshot.lastSuccessfulAnalysisArtifact &&
                    snapshot.lastSuccessfulAnalysisArtifact->artifactId ==
                        "analysis" &&
                    snapshot.displayArtifact &&
                    snapshot.displayArtifact->artifactId == "preview",
                "hidden analysis replaced display without selection");
        updateLayer(*layer);
        require(layer->artifactGeneration() == previewJob,
                "layer rendered hidden analysis instead of display artifact");

        require(service.showArtifact("analysis"),
                "explicit analysis display selection failed");
        updateLayer(*layer);
        require(analysisJob > previewJob &&
                    layer->artifactGeneration() == analysisJob,
                "layer ignored the explicitly selected display artifact");
    }
}

int main()
{
    testSouthUpRasterStaysSouthAtTextureBottom();
    testProjectedRasterBuildsAnExactWgs84Grid();
    testRequestedViewSelectsAVisibleNativeResolutionWindow();
    testViewTargetDoesNotMoveWhenOnlyCameraHeightChanges();
    testFalseColorMeaningIsMachineReadable();
    testLayerRetainsLastGoodUntilExplicitReplacementOrRemoval();
    testLayerRendersOnlyTheExplicitDisplayArtifact();
    std::cout << "[OK] ScienceEarth preview georeference, orientation, target and legend\n";
    return 0;
}
