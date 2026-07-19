#include "Sentinel2Runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <cpl_conv.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    std::string response()
    {
        return "{\"type\":\"FeatureCollection\",\"features\":[{"
            "\"type\":\"Feature\",\"id\":\"S2B_54SUE_20260715_0_L2A\","
            "\"bbox\":[138.7,35.1,140.1,36.2],\"properties\":{"
            "\"datetime\":\"2026-07-15T01:37:22.011000Z\","
            "\"eo:cloud_cover\":4.5},\"assets\":{\"visual\":{"
            "\"href\":\"https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
            "sentinel-s2-l2a-cogs/54/S/UE/2026/7/scene/TCI.tif\","
            "\"type\":\"image/tiff; application=geotiff; "
            "profile=cloud-optimized\",\"roles\":[\"visual\"],"
            "\"gsd\":10}}}]}";
    }

    earthscience::GeoTemporalQuery query(double latitude = 35.68)
    {
        earthscience::GeoTemporalQuery value;
        value.sourceId = "sentinel-2-l2a";
        value.geometry.point = {latitude, 139.76};
        value.geometry.requestedSpanMeters = 10000.0;
        value.time.mode = earthscience::ScienceTimeMode::Interval;
        value.time.intervalStart = "2026-06-18T00:00:00Z";
        value.time.intervalEnd = "2026-07-18T23:59:59Z";
        value.variables = {"visual"};
        value.targetResolutionMeters = 10.0;
        value.visualizationId = "natural-color-visual";
        value.sceneFilters.maximumCloudCoverPercent = 20.0;
        return value;
    }

    earthscience::ScienceRasterPayload raster()
    {
        earthscience::ScienceRasterPayload value;
        value.bounds = {139.70, 35.60, 139.82, 35.76};
        value.width = value.height = 256;
        value.sourceWindowWidth = value.sourceWindowHeight = 1000;
        value.sourceResolutionMeters = 10.0;
        value.displayResolutionMeters = 39.0625;
        value.rgba = std::make_shared<const std::vector<unsigned char>>(
            256u * 256u * 4u, 127);
        auto grid = std::make_shared<earthscience::ScienceGroundGrid>();
        grid->columns = grid->rows = 2;
        grid->points = {
            {35.60, 139.70}, {35.60, 139.82},
            {35.76, 139.70}, {35.76, 139.82},
        };
        value.groundGrid = grid;
        return value;
    }

    class FakeIo : public earthscience::ISentinel2Io
    {
    public:
        bool failFetch = false;
        bool blockFetch = false;
        bool blockRead = false;
        std::atomic<bool> fetchStarted{false};
        std::atomic<bool> readStarted{false};
        std::atomic<int> fetches{0};
        std::atomic<int> reads{0};
        std::string lastUrl;

        bool fetchStac(const std::string& url, std::size_t maximumBytes,
                       const std::function<bool()>& cancelled,
                       std::string& body, std::string& error) override
        {
            ++fetches;
            lastUrl = url;
            fetchStarted.store(true, std::memory_order_release);
            while (blockFetch && !cancelled()) std::this_thread::yield();
            if (cancelled())
            {
                error = "cancelled";
                return false;
            }
            if (url.find("earth-search.aws.element84.com/v1/search") ==
                    std::string::npos || maximumBytes != 2u * 1024u * 1024u)
            {
                error = "unexpected STAC request contract";
                return false;
            }
            if (failFetch)
            {
                error = "simulated STAC timeout";
                return false;
            }
            body = response();
            error.clear();
            return true;
        }

        bool readVisual(
            const earthscience::Sentinel2Scene& scene,
            const earthscience::GeoTemporalQuery&,
            const std::function<bool()>& cancelled,
            earthscience::ScienceRasterPayload& output,
            std::string& error) override
        {
            ++reads;
            readStarted.store(true, std::memory_order_release);
            while (blockRead && !cancelled()) std::this_thread::yield();
            if (cancelled())
            {
                error = "cancelled";
                return false;
            }
            if (scene.itemId.empty())
            {
                error = "scene identity missing";
                return false;
            }
            output = raster();
            error.clear();
            return true;
        }
    };

    earthscience::ScienceProviderSnapshot waitForTerminal(
        earthscience::Sentinel2Runtime& runtime)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const earthscience::ScienceProviderSnapshot state =
                runtime.snapshot();
            if (state.state == earthscience::ScienceJobState::Ready ||
                state.state == earthscience::ScienceJobState::Failed ||
                state.state == earthscience::ScienceJobState::Cancelled)
                return state;
            std::this_thread::yield();
        }
        return runtime.snapshot();
    }

    void testReadyArtifactHasExactSceneEvidence()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        earthscience::Sentinel2Runtime runtime(std::move(io));
        const std::uint64_t generation = runtime.submit(query());
        const earthscience::ScienceProviderSnapshot ready =
            waitForTerminal(runtime);
        require(generation != 0 && ready.generation == generation &&
                    ready.state == earthscience::ScienceJobState::Ready &&
                    ready.progress.stage ==
                        earthscience::ScienceProgressStage::Ready &&
                    ready.progress.elapsedSeconds > 0.0,
                "Sentinel runtime did not reach Ready");
        require(observed->fetches == 1 && observed->reads == 1,
                "Sentinel runtime repeated a bounded request");
        require(observed->lastUrl.find(
                    "query=%7B%22eo%3Acloud_cover%22%3A%7B%22lte%22%3A20%7D%7D") !=
                    std::string::npos,
                "Sentinel runtime did not push the cloud threshold into STAC");
        require(observed->lastUrl.find(
                    "sortby=%2Bproperties.eo%3Acloud_cover%2C-properties.datetime") !=
                    std::string::npos,
                "Sentinel runtime did not request deterministic cloud/time sorting");
        require(ready.artifact && ready.artifact->raster.width == 256 &&
                    ready.artifact->raster.height == 256 &&
                    ready.artifact->sourceReferences.size() == 1,
                "Sentinel runtime lost the raster artifact");
        const auto& reference = ready.artifact->sourceReferences.front();
        const auto hasField = [&reference](const std::string& id)
        {
            return std::any_of(
                reference.fields.begin(), reference.fields.end(),
                [&id](const earthscience::ScienceEvidenceField& field)
                { return field.id == id && !field.value.empty(); });
        };
        require(reference.datasetId == "S2B_54SUE_20260715_0_L2A" &&
                    reference.acquisitionTime ==
                        "2026-07-15T01:37:22.011000Z" &&
                    reference.originalUrl.find("/TCI.tif") !=
                        std::string::npos &&
                    hasField("collection") && hasField("stac_endpoint") &&
                    hasField("visual_asset") &&
                    hasField("display_product") &&
                    hasField("scene_cloud_cover") &&
                    hasField("source_resolution") &&
                    hasField("display_resolution"),
                "Sentinel runtime omitted scene provenance");
    }

    void testCancellationAndFailureDoNotPublishArtifacts()
    {
        auto fetchingIo = std::make_unique<FakeIo>();
        FakeIo* fetchingObserved = fetchingIo.get();
        fetchingObserved->blockFetch = true;
        earthscience::Sentinel2Runtime fetching(std::move(fetchingIo));
        const std::uint64_t fetchingGeneration = fetching.submit(query());
        const auto fetchDeadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (!fetchingObserved->fetchStarted.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < fetchDeadline)
            std::this_thread::yield();
        require(fetchingObserved->fetchStarted.load(std::memory_order_acquire),
                "blocking STAC fetch did not start");
        fetching.cancel(fetchingGeneration);
        const earthscience::ScienceProviderSnapshot fetchCancelled =
            waitForTerminal(fetching);
        require(fetchCancelled.state ==
                    earthscience::ScienceJobState::Cancelled &&
                    !fetchCancelled.artifact,
                "cancelled STAC fetch published an artifact");

        auto blockingIo = std::make_unique<FakeIo>();
        FakeIo* observed = blockingIo.get();
        observed->blockRead = true;
        earthscience::Sentinel2Runtime runtime(std::move(blockingIo));
        const std::uint64_t generation = runtime.submit(query());
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (!observed->readStarted.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        require(observed->readStarted.load(std::memory_order_acquire),
                "blocking read did not start");
        runtime.cancel(generation);
        const earthscience::ScienceProviderSnapshot cancelled =
            waitForTerminal(runtime);
        require(cancelled.state == earthscience::ScienceJobState::Cancelled &&
                    !cancelled.artifact,
                "cancelled Sentinel work published an artifact");

        auto failingIo = std::make_unique<FakeIo>();
        failingIo->failFetch = true;
        earthscience::Sentinel2Runtime failing(std::move(failingIo));
        failing.submit(query());
        const earthscience::ScienceProviderSnapshot failed =
            waitForTerminal(failing);
        require(failed.state == earthscience::ScienceJobState::Failed &&
                    failed.message == "simulated STAC timeout" &&
                    !failed.artifact,
                "STAC failure was hidden or fabricated an artifact");

        auto noCoverageIo = std::make_unique<FakeIo>();
        earthscience::Sentinel2Runtime noCoverage(std::move(noCoverageIo));
        earthscience::GeoTemporalQuery strictCloud = query();
        strictCloud.sceneFilters.maximumCloudCoverPercent = 1.0;
        noCoverage.submit(strictCloud);
        const earthscience::ScienceProviderSnapshot missing =
            waitForTerminal(noCoverage);
        require(missing.state == earthscience::ScienceJobState::Failed &&
                    missing.message.find("raise maximum cloud") !=
                        std::string::npos &&
                    missing.message.find("widen time window") !=
                        std::string::npos &&
                    !missing.artifact,
                "no matching scene did not provide actionable filter guidance");
    }

    void testNewGenerationAndClearRejectStaleRead()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        observed->blockRead = true;
        earthscience::Sentinel2Runtime runtime(std::move(io));
        const std::uint64_t first = runtime.submit(query(35.68));
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (!observed->readStarted.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        require(observed->readStarted.load(std::memory_order_acquire),
                "stale-generation read did not start");
        const std::uint64_t second = runtime.submit(query(35.69));
        observed->blockRead = false;
        const earthscience::ScienceProviderSnapshot ready =
            waitForTerminal(runtime);
        require(second > first && ready.generation == second &&
                    ready.state == earthscience::ScienceJobState::Ready &&
                    ready.artifact && ready.artifact->generation == second,
                "stale generation replaced the newer Sentinel result");

        observed->blockRead = true;
        observed->readStarted.store(false, std::memory_order_release);
        runtime.submit(query(35.70));
        const auto clearDeadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (!observed->readStarted.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < clearDeadline)
            std::this_thread::yield();
        require(observed->readStarted.load(std::memory_order_acquire),
                "clear-generation read did not start");
        runtime.clear();
        observed->blockRead = false;
        for (int index = 0; index < 1000; ++index) std::this_thread::yield();
        const earthscience::ScienceProviderSnapshot cleared = runtime.snapshot();
        require(cleared.state == earthscience::ScienceJobState::Idle &&
                    !cleared.artifact,
                "cleared Sentinel work republished a stale artifact");
    }

    void testLocalProjectedVisualDatasetKeepsRgbAndNorthUpGrid()
    {
        GDALRegister_GTiff();
        const std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("osgsol-sentinel2-" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()) +
             ".tif");
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        require(driver != nullptr, "GTiff driver is unavailable");
        GDALDataset* dataset = driver->Create(
            path.string().c_str(), 512, 512, 3, GDT_Byte, nullptr);
        require(dataset != nullptr, "could not create local visual fixture");

        OGRSpatialReference wgs84, utm;
        wgs84.importFromEPSG(4326);
        utm.importFromEPSG(32654);
        wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        utm.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        OGRCoordinateTransformation* transform =
            OGRCreateCoordinateTransformation(&wgs84, &utm);
        require(transform != nullptr, "could not create fixture transform");
        double centerX = 139.76, centerY = 35.68;
        require(transform->Transform(1, &centerX, &centerY),
                "could not project fixture center");
        OCTDestroyCoordinateTransformation(transform);
        double geotransform[] = {
            centerX - 2560.0, 10.0, 0.0,
            centerY + 2560.0, 0.0, -10.0,
        };
        require(dataset->SetGeoTransform(geotransform) == CE_None,
                "could not set fixture geotransform");
        char* wkt = nullptr;
        utm.exportToWkt(&wkt);
        require(wkt && dataset->SetProjection(wkt) == CE_None,
                "could not set fixture projection");
        CPLFree(wkt);

        std::vector<unsigned char> red(512u * 512u);
        std::vector<unsigned char> green(red.size(), 20);
        std::vector<unsigned char> blue(red.size(), 30);
        for (int y = 0; y < 512; ++y)
            std::fill(red.begin() + static_cast<std::size_t>(y * 512),
                      red.begin() + static_cast<std::size_t>((y + 1) * 512),
                      y < 256 ? 10 : 200);
        red[128u * 512u + 128u] = 0;
        const std::vector<std::vector<unsigned char>*> values =
            {&red, &green, &blue};
        const GDALColorInterp colors[] =
            {GCI_RedBand, GCI_GreenBand, GCI_BlueBand};
        for (int index = 0; index < 3; ++index)
        {
            GDALRasterBand* band = dataset->GetRasterBand(index + 1);
            band->SetColorInterpretation(colors[index]);
            band->SetNoDataValue(0.0);
            require(band->RasterIO(
                        GF_Write, 0, 0, 512, 512, values[index]->data(),
                        512, 512, GDT_Byte, 0, 0, nullptr) == CE_None,
                    "could not write visual fixture band");
        }
        GDALClose(dataset);

        earthscience::GeoTemporalQuery request = query();
        request.geometry.requestedSpanMeters = 2560.0;
        earthscience::ScienceRasterPayload output;
        std::string error;
        const bool read = earthscience::readSentinel2VisualDatasetForTest(
            path.string(), request, output, error);
        std::filesystem::remove(path);
        require(read && error.empty(),
                "local projected Sentinel-2 visual fixture was rejected");
        require(output.width == 256 && output.height == 256 &&
                    output.sourceWindowWidth == 256 &&
                    output.sourceResolutionMeters == 10.0 && output.rgba &&
                    output.rgba->size() == 256u * 256u * 4u,
                "local visual fixture lost bounded dimensions");
        require(output.rgba->at(0) == 0 && output.rgba->at(1) == 20 &&
                    output.rgba->at(2) == 30 && output.rgba->at(3) == 210 &&
                    output.rgba->at(4) == 10 &&
                    output.rgba->at(output.rgba->size() - 4) == 200,
                "local visual fixture changed RGB channels or row order");
        require(output.groundGrid && output.groundGrid->columns == 33 &&
                    output.groundGrid->rows == 33 &&
                    output.groundGrid->points.front().latitude >
                        output.groundGrid->points.back().latitude &&
                    output.bounds.west < output.bounds.east &&
                    output.bounds.south < output.bounds.north,
                "local visual fixture lost north-up georeference");

        const auto writeBlackFixture = [&](const std::filesystem::path& fixture,
                                           bool declareNoData)
        {
            GDALDataset* black = driver->Create(
                fixture.string().c_str(), 512, 512, 3, GDT_Byte, nullptr);
            require(black != nullptr, "could not create black visual fixture");
            require(black->SetGeoTransform(geotransform) == CE_None,
                    "could not georeference black visual fixture");
            char* blackWkt = nullptr;
            utm.exportToWkt(&blackWkt);
            require(blackWkt && black->SetProjection(blackWkt) == CE_None,
                    "could not project black visual fixture");
            CPLFree(blackWkt);
            const GDALColorInterp blackColors[] =
                {GCI_RedBand, GCI_GreenBand, GCI_BlueBand};
            for (int index = 0; index < 3; ++index)
            {
                GDALRasterBand* band = black->GetRasterBand(index + 1);
                band->SetColorInterpretation(blackColors[index]);
                if (declareNoData) band->SetNoDataValue(0.0);
                require(band->Fill(0.0) == CE_None,
                        "could not fill black visual fixture");
            }
            GDALClose(black);
        };

        const std::filesystem::path validBlackPath =
            path.string() + ".valid-black.tif";
        writeBlackFixture(validBlackPath, false);
        output = earthscience::ScienceRasterPayload();
        error.clear();
        require(earthscience::readSentinel2VisualDatasetForTest(
                    validBlackPath.string(), request, output, error) &&
                    output.rgba && output.rgba->at(3) == 210,
                "valid black RGB was incorrectly converted to transparent NoData");
        std::filesystem::remove(validBlackPath);

        const std::filesystem::path noDataBlackPath =
            path.string() + ".nodata-black.tif";
        writeBlackFixture(noDataBlackPath, true);
        output = earthscience::ScienceRasterPayload();
        error.clear();
        require(!earthscience::readSentinel2VisualDatasetForTest(
                    noDataBlackPath.string(), request, output, error) &&
                    error.find("no visible pixels") != std::string::npos,
                "all-NoData visual fixture published a yellow-frame-only raster");
        std::filesystem::remove(noDataBlackPath);

        const std::filesystem::path wrongBandsPath =
            path.string() + ".wrong-bands.tif";
        dataset = driver->Create(
            wrongBandsPath.string().c_str(), 16, 16, 2, GDT_Byte, nullptr);
        require(dataset != nullptr, "could not create wrong-band fixture");
        GDALClose(dataset);
        output = earthscience::ScienceRasterPayload();
        error.clear();
        require(!earthscience::readSentinel2VisualDatasetForTest(
                    wrongBandsPath.string(), request, output, error) &&
                    error.find("exactly three") != std::string::npos,
                "wrong-band visual fixture was accepted");
        std::filesystem::remove(wrongBandsPath);

        const std::filesystem::path noSemanticsPath =
            path.string() + ".no-rgb-semantics.tif";
        dataset = driver->Create(
            noSemanticsPath.string().c_str(), 16, 16, 3, GDT_Byte, nullptr);
        require(dataset != nullptr, "could not create no-semantics fixture");
        for (int index = 1; index <= 3; ++index)
            dataset->GetRasterBand(index)->SetColorInterpretation(
                GCI_Undefined);
        GDALClose(dataset);
        output = earthscience::ScienceRasterPayload();
        error.clear();
        require(!earthscience::readSentinel2VisualDatasetForTest(
                    noSemanticsPath.string(), request, output, error) &&
                    error.find("RGB band semantics") != std::string::npos,
                "visual fixture without RGB semantics was accepted");
        std::filesystem::remove(noSemanticsPath);

        output = earthscience::ScienceRasterPayload();
        error.clear();
        require(!earthscience::readSentinel2VisualDatasetForTest(
                    path.string() + ".missing", request, output, error) &&
                    !error.empty(),
                "missing visual dataset did not expose a GDAL failure");
    }
}

int main()
{
    testReadyArtifactHasExactSceneEvidence();
    testCancellationAndFailureDoNotPublishArtifacts();
    testNewGenerationAndClearRejectStaleRead();
    testLocalProjectedVisualDatasetKeepsRgbAndNorthUpGrid();
    std::cout << "[OK] Sentinel-2 runtime state contract\n";
    return 0;
}
