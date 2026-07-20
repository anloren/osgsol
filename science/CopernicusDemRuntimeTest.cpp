#include "CopernicusDemRuntime.h"
#include "ScienceRemoteOpen.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <unistd.h>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    earthscience::GeoTemporalQuery query(double latitude = 35.6895)
    {
        earthscience::GeoTemporalQuery result;
        result.sourceId = "copernicus-dem-glo-30";
        result.geometry.kind = earthscience::ScienceGeometryKind::Point;
        result.geometry.point = {latitude, 139.6917};
        result.geometry.requestedSpanMeters = 20000.0;
        result.time.mode = earthscience::ScienceTimeMode::Instant;
        result.time.publicationTime = "2021";
        result.variables = {"surface_elevation"};
        result.targetResolutionMeters = 30.0;
        result.outputKind = earthscience::ScienceOutputKind::RasterLayer;
        result.visualizationId = "surface-elevation-hypsometric";
        return result;
    }

    earthscience::CopernicusDemReadResult readyResult(double center)
    {
        earthscience::CopernicusDemReadResult result;
        result.raster.bounds = {139.5, 35.5, 139.75, 35.75};
        result.raster.width = 1;
        result.raster.height = 1;
        result.raster.sourceWindowWidth = 2;
        result.raster.sourceWindowHeight = 2;
        result.raster.sourceResolutionMeters = 30.0;
        result.raster.displayResolutionMeters = 100.0;
        result.raster.rgba =
            std::make_shared<const std::vector<unsigned char>>(
                std::vector<unsigned char>{1, 2, 3, 210});
        result.summary.variableId = "surface_elevation";
        result.summary.displayName = "Surface elevation";
        result.summary.unit = "m";
        result.summary.centerValid = true;
        result.summary.center = center;
        result.summary.minimumValid = true;
        result.summary.maximumValid = true;
        result.summary.meanValid = true;
        result.summary.minimum = center - 1.0;
        result.summary.maximum = center + 1.0;
        result.summary.mean = center;
        result.summary.validCellCount = 4;
        result.sourceUrls = {"https://copernicus-dem-30m.s3.amazonaws.com/"
            "Copernicus_DSM_COG_10_N35_00_E139_00_DEM/"
            "Copernicus_DSM_COG_10_N35_00_E139_00_DEM.tif"};
        return result;
    }

    class FakeIo : public earthscience::ICopernicusDemIo
    {
    public:
        std::atomic<bool> block{false};
        std::atomic<bool> fail{false};
        std::atomic<bool> entered{false};
        std::atomic<bool> release{false};
        std::atomic<bool> ignoreCancellation{false};
        std::atomic<int> calls{0};

        bool read(
            const std::vector<earthscience::CopernicusDemCell>& cells,
            const earthscience::GeoTemporalQuery& request,
            const std::function<bool()>& cancelled,
            earthscience::CopernicusDemReadResult& output,
            std::string& error) override
        {
            require(!cells.empty(), "runtime submitted no DEM cells");
            entered.store(true, std::memory_order_release);
            calls.fetch_add(1, std::memory_order_acq_rel);
            while (block.load(std::memory_order_acquire) &&
                   !release.load(std::memory_order_acquire) &&
                   !cancelled())
                std::this_thread::yield();
            if (cancelled() &&
                !ignoreCancellation.load(std::memory_order_acquire))
            {
                error = "cancelled";
                return false;
            }
            if (fail.load(std::memory_order_acquire))
            {
                error = "Copernicus DEM fixture transport failed";
                return false;
            }
            output = readyResult(request.geometry.point.latitude);
            error.clear();
            return true;
        }
    };

    earthscience::ScienceProviderSnapshot waitTerminal(
        earthscience::CopernicusDemRuntime& runtime)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(2);
        earthscience::ScienceProviderSnapshot snapshot;
        do
        {
            snapshot = runtime.snapshot();
            if (snapshot.state == earthscience::ScienceJobState::Ready ||
                snapshot.state == earthscience::ScienceJobState::Failed ||
                snapshot.state == earthscience::ScienceJobState::Cancelled)
                return snapshot;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        require(false, "DEM runtime did not reach a terminal state");
        return snapshot;
    }

    void testPublishesScientificallyBoundedArtifact()
    {
        auto io = std::make_unique<FakeIo>();
        earthscience::CopernicusDemRuntime runtime(std::move(io));
        const std::uint64_t generation = runtime.submit(query());
        const auto snapshot = waitTerminal(runtime);
        require(generation != 0 && snapshot.generation == generation &&
                    snapshot.state == earthscience::ScienceJobState::Ready &&
                    snapshot.progress.stage ==
                        earthscience::ScienceProgressStage::Ready &&
                    snapshot.artifact,
                "DEM runtime did not publish a ready artifact");
        const auto& artifact = *snapshot.artifact;
        require(artifact.query.sourceId == "copernicus-dem-glo-30" &&
                    artifact.scalarSummaries.size() == 1 &&
                    artifact.scalarSummaries.front().center == 35.6895 &&
                    artifact.sourceReferences.size() == 1,
                "DEM artifact lost query or numeric evidence");
        const auto& reference = artifact.sourceReferences.front();
        require(reference.providerVersion == "aws-glo30-2021" &&
                    reference.units == std::vector<std::string>({"m"}) &&
                    reference.publicationTime == "2021" &&
                    reference.attribution.find("Copernicus") !=
                        std::string::npos,
                "DEM artifact lost version, unit, time, or attribution");
        bool datum = false, model = false, bounded = false;
        for (const auto& field : reference.fields)
        {
            datum = datum ||
                (field.id == "vertical_datum" && field.value == "EGM2008");
            model = model ||
                (field.id == "surface_model" &&
                 field.value.find("buildings") != std::string::npos);
            bounded = bounded ||
                (field.id == "full_object_fallback" && field.value == "none");
        }
        require(datum && model && bounded,
                "DEM artifact omitted datum, DSM meaning, or bounded access");
    }

    void testRasterOpenBudgetIsFinite()
    {
        const earthscience::remoteopen::OperationBudget budget =
            earthscience::remoteopen::rasterBudget();
        require(budget.connectSeconds > 0 && budget.totalSeconds > 0 &&
                    budget.connectSeconds < budget.totalSeconds,
                "DEM remote-open budget is not finite");
    }

    void testCancellationAndStaleGenerationCannotPublish()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        observed->block = true;
        earthscience::CopernicusDemRuntime runtime(std::move(io));
        const std::uint64_t first = runtime.submit(query(10.0));
        while (!observed->entered.load(std::memory_order_acquire))
            std::this_thread::yield();
        runtime.cancel(first);
        require(waitTerminal(runtime).state ==
                    earthscience::ScienceJobState::Cancelled,
                "cancelled DEM request published a result");

        observed->entered.store(false, std::memory_order_release);
        observed->block = false;
        observed->release.store(true, std::memory_order_release);
        const std::uint64_t second = runtime.submit(query(20.0));
        const std::uint64_t third = runtime.submit(query(30.0));
        const auto latest = waitTerminal(runtime);
        require(second != third && latest.generation == third &&
                    latest.state == earthscience::ScienceJobState::Ready &&
                    latest.artifact &&
                    latest.artifact->scalarSummaries.front().center == 30.0,
                "stale DEM generation replaced the latest artifact");
    }

    void testFailureIsTypedAndCarriesNoArtifact()
    {
        auto io = std::make_unique<FakeIo>();
        io->fail = true;
        earthscience::CopernicusDemRuntime runtime(std::move(io));
        runtime.submit(query());
        const auto snapshot = waitTerminal(runtime);
        require(snapshot.state == earthscience::ScienceJobState::Failed &&
                    snapshot.progress.stage ==
                        earthscience::ScienceProgressStage::Failed &&
                    snapshot.message ==
                        "Copernicus DEM fixture transport failed" &&
                    !snapshot.artifact,
                "DEM failure was not isolated as a typed terminal result");
    }

    void testLateSuccessCannotRepublishAndDestructionJoins()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        observed->block.store(true, std::memory_order_release);
        observed->ignoreCancellation.store(true, std::memory_order_release);
        std::unique_ptr<earthscience::CopernicusDemRuntime> runtime(
            new earthscience::CopernicusDemRuntime(std::move(io)));
        const std::uint64_t generation = runtime->submit(query());
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(2);
        while (!observed->entered.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        require(observed->entered.load(std::memory_order_acquire),
                "late-success DEM read did not start");
        runtime->cancel(generation);
        observed->release.store(true, std::memory_order_release);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const earthscience::ScienceProviderSnapshot cancelled =
            runtime->snapshot();
        require(cancelled.state == earthscience::ScienceJobState::Cancelled &&
                    !cancelled.artifact,
                "late DEM success republished after cancellation");

        observed->release.store(false, std::memory_order_release);
        observed->ignoreCancellation.store(false, std::memory_order_release);
        observed->entered.store(false, std::memory_order_release);
        runtime->submit(query(35.70));
        const auto joinDeadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(2);
        while (!observed->entered.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < joinDeadline)
            std::this_thread::yield();
        require(observed->entered.load(std::memory_order_acquire),
                "destructor-join DEM read did not start");
        const auto destructionStarted = std::chrono::steady_clock::now();
        runtime.reset();
        require(std::chrono::steady_clock::now() - destructionStarted <
                    std::chrono::milliseconds(500),
                "DEM runtime destruction did not cancel and join");
    }

    std::string fixturePath(const char* suffix)
    {
        return "/tmp/osgsol-copernicus-dem-" +
            std::to_string(static_cast<long long>(getpid())) + '-' + suffix +
            ".tif";
    }

    void writeFixture(const std::string& path, double west,
                      const std::vector<float>& values)
    {
        GDALRegister_GTiff();
        GDALDriver* driver =
            GetGDALDriverManager()->GetDriverByName("GTiff");
        require(driver != nullptr, "GTiff fixture driver is unavailable");
        GDALDataset* dataset = driver->Create(
            path.c_str(), 3, 3, 1, GDT_Float32, nullptr);
        require(dataset != nullptr, "DEM fixture could not be created");
        double transform[6] = {west, 0.1, 0.0, 35.7, 0.0, -0.1};
        require(dataset->SetGeoTransform(transform) == CE_None,
                "DEM fixture geotransform failed");
        OGRSpatialReference wgs84;
        wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        require(wgs84.SetWellKnownGeogCS("WGS84") == OGRERR_NONE,
                "DEM fixture WGS84 definition failed");
        char* wkt = nullptr;
        require(wgs84.exportToWkt(&wkt) == OGRERR_NONE && wkt,
                "DEM fixture WKT export failed");
        require(dataset->SetProjection(wkt) == CE_None,
                "DEM fixture projection failed");
        CPLFree(wkt);
        GDALRasterBand* band = dataset->GetRasterBand(1);
        require(band && band->SetNoDataValue(-9999.0) == CE_None,
                "DEM fixture NoData failed");
        require(values.size() == 9 &&
                    band->RasterIO(GF_Write, 0, 0, 3, 3,
                        const_cast<float*>(values.data()), 3, 3,
                        GDT_Float32, 0, 0, nullptr) == CE_None,
                "DEM fixture raster write failed");
        GDALClose(dataset);
    }

    void testReadsNorthUpNumericEvidenceAndNoData()
    {
        const std::string path = fixturePath("single");
        writeFixture(path, 139.4,
            {-9999.0f, 2.0f, 3.0f,
             4.0f, 5.0f, 6.0f,
             7.0f, 8.0f, 9.0f});
        earthscience::CopernicusDemReadResult result;
        std::string error;
        earthscience::GeoTemporalQuery request = query(35.55);
        request.geometry.point.longitude = 139.55;
        request.geometry.requestedSpanMeters = 50000.0;
        require(earthscience::readCopernicusDemDatasetsForTest(
                    {path}, request, result, error),
                error.c_str());
        require(result.raster.width == 256 && result.raster.height == 256 &&
                    result.raster.sourceWindowWidth == 3 &&
                    result.raster.sourceWindowHeight == 3 &&
                    result.raster.rgba && result.raster.groundGrid,
                "DEM fixture did not produce bounded raster/grid output");
        require(result.summary.centerValid && result.summary.center == 5.0 &&
                    result.summary.minimumValid && result.summary.minimum == 2.0 &&
                    result.summary.maximumValid && result.summary.maximum == 9.0 &&
                    result.summary.meanValid &&
                    std::abs(result.summary.mean - 5.5) < 1e-9 &&
                    result.summary.validCellCount == 8 &&
                    result.summary.noDataCellCount == 1,
                "DEM numeric summary changed valid/NoData semantics");
        const auto& rgba = *result.raster.rgba;
        require(rgba.size() == 256u * 256u * 4u && rgba[3] == 0 &&
                    rgba[rgba.size() - 1] == 210,
                "DEM display did not preserve NoData transparency");
        const auto& points = result.raster.groundGrid->points;
        require(!points.empty() && points.front().latitude >
                    points.back().latitude,
                "DEM output is not north-up");
        std::remove(path.c_str());
    }

    void testComposesAdjacentDatasets()
    {
        const std::string westPath = fixturePath("west");
        const std::string eastPath = fixturePath("east");
        writeFixture(westPath, 139.4,
            {1, 2, 3, 4, 5, 6, 7, 8, 9});
        writeFixture(eastPath, 139.7,
            {11, 12, 13, 14, 15, 16, 17, 18, 19});
        earthscience::CopernicusDemReadResult result;
        std::string error;
        earthscience::GeoTemporalQuery request = query(35.55);
        request.geometry.point.longitude = 139.7;
        request.geometry.requestedSpanMeters = 50000.0;
        require(earthscience::readCopernicusDemDatasetsForTest(
                    {westPath, eastPath}, request, result, error),
                error.c_str());
        require(result.raster.bounds.west < 139.7 &&
                    result.raster.bounds.east > 139.7 &&
                    result.raster.sourceWindowWidth > 3 &&
                    result.summary.minimumValid &&
                    result.summary.maximumValid &&
                    result.summary.minimum == 1.0 &&
                    result.summary.maximum == 19.0,
                "DEM adjacent datasets were not composed into one bounded view");
        std::remove(westPath.c_str());
        std::remove(eastPath.c_str());
    }
}

int main()
{
    try
    {
        testRasterOpenBudgetIsFinite();
        testPublishesScientificallyBoundedArtifact();
        testCancellationAndStaleGenerationCannotPublish();
        testFailureIsTypedAndCarriesNoArtifact();
        testLateSuccessCannotRepublishAndDestructionJoins();
        testReadsNorthUpNumericEvidenceAndNoData();
        testComposesAdjacentDatasets();
        std::cout << "[OK] Copernicus DEM runtime contract\n";
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << "[FAIL] uncaught exception: " << exception.what() << '\n';
    }
    catch (...)
    {
        std::cerr << "[FAIL] uncaught non-standard exception\n";
    }
    return 1;
}
