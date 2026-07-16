#include "AlphaEarthEmbeddingRuntime.h"
#include "AlphaEarthEmbeddingReader.h"

#include "ScienceEmbedding.h"
#include "ScienceQueryService.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <sqlite3.h>

namespace
{
    constexpr double WEST = 100.0;
    constexpr double SOUTH = 20.0;
    constexpr double EAST = 100.08;
    constexpr double NORTH = 20.08;

    void require(bool condition, const std::string& message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    bool nearlyEqual(double left, double right, double tolerance = 1.0e-6)
    {
        return std::abs(left - right) <= tolerance;
    }

    std::int8_t fixtureValue(int band, int x, int y)
    {
        if (band == 64 && x == 1 && y == 1) return -128;
        return static_cast<std::int8_t>(((band * 3 + x + y * 8) % 120) + 1);
    }

    class LocalFixture
    {
    public:
        explicit LocalFixture(bool rotated = false,
                              int firstComponent = 0)
        {
            static std::atomic<unsigned int> sequence{0};
            const std::string name = "osgsol-alphaearth-64d-" +
                std::to_string(++sequence) + ".tif";
            _path = (std::filesystem::temp_directory_path() / name).string();

            GDALAllRegister();
            GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
            require(driver != nullptr, "GTiff driver is unavailable");
            GDALDataset* dataset = driver->Create(
                _path.c_str(), 8, 8, 64, GDT_Int8, nullptr);
            require(dataset != nullptr, "could not create the 64-band fixture");

            double transform[6] = {WEST, 0.01, rotated ? 0.002 : 0.0,
                                   NORTH, rotated ? 0.001 : 0.0, -0.01};
            require(dataset->SetGeoTransform(transform) == CE_None,
                    "could not set the fixture geotransform");
            OGRSpatialReference wgs84;
            wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
            require(wgs84.importFromEPSG(4326) == OGRERR_NONE,
                    "could not create fixture EPSG:4326");
            char* projection = nullptr;
            require(wgs84.exportToWkt(&projection) == OGRERR_NONE,
                    "could not export fixture EPSG:4326");
            require(dataset->SetProjection(projection) == CE_None,
                    "could not set the fixture projection");
            CPLFree(projection);

            std::vector<std::int8_t> pixels(64);
            for (int bandIndex = 1; bandIndex <= 64; ++bandIndex)
            {
                GDALRasterBand* band = dataset->GetRasterBand(bandIndex);
                char description[4] = {};
                std::snprintf(description, sizeof(description),
                              "A%02d", bandIndex - 1 + firstComponent);
                band->SetDescription(description);
                band->SetNoDataValue(-128.0);
                for (int y = 0; y < 8; ++y)
                {
                    for (int x = 0; x < 8; ++x)
                        pixels[static_cast<std::size_t>(y * 8 + x)] =
                            fixtureValue(bandIndex, x, y);
                }
                require(band->RasterIO(
                            GF_Write, 0, 0, 8, 8, pixels.data(), 8, 8,
                            GDT_Int8, 0, 0, nullptr) == CE_None,
                        "could not write fixture band " +
                            std::to_string(bandIndex));
            }
            GDALClose(dataset);
        }

        ~LocalFixture()
        {
            std::error_code ignored;
            std::filesystem::remove(_path, ignored);
        }

        const std::string& path() const { return _path; }

        earthscience::AlphaEarthAsset asset(int year) const
        {
            earthscience::AlphaEarthAsset asset;
            asset.datasetId = "local-alphaearth-" + std::to_string(year);
            asset.pathOrUrl = _path;
            asset.sourceVersion = "fixture-v1";
            asset.indexedBounds = {WEST, SOUTH, EAST, NORTH};
            return asset;
        }

        void changeDescription(int bandIndex, const char* description)
        {
            GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(
                _path.c_str(), GDAL_OF_RASTER | GDAL_OF_UPDATE,
                nullptr, nullptr, nullptr));
            require(dataset != nullptr, "could not reopen fixture for update");
            dataset->GetRasterBand(bandIndex)->SetDescription(description);
            GDALClose(dataset);
        }

    private:
        std::string _path;
    };

    earthscience::GeoTemporalQuery pointQuery()
    {
        earthscience::GeoTemporalQuery query;
        query.sourceId = "alphaearth-foundations";
        query.geometry.kind = earthscience::ScienceGeometryKind::Point;
        query.geometry.point = {20.055, 100.025};
        query.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        query.time.explicitYears = {2017, 2018};
        query.outputKind = earthscience::ScienceOutputKind::Analysis;
        query.analysis.kind = earthscience::ScienceAnalysisKind::PointSeries;
        query.analysis.baselineYear = 2017;
        return query;
    }

    earthscience::GeoTemporalQuery regionQuery()
    {
        earthscience::GeoTemporalQuery query;
        query.sourceId = "alphaearth-foundations";
        query.geometry.kind = earthscience::ScienceGeometryKind::BoundingBox;
        query.geometry.bounds = {WEST, SOUTH, EAST, NORTH};
        query.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        query.time.explicitYears = {2017, 2018};
        query.outputKind = earthscience::ScienceOutputKind::Analysis;
        query.analysis.kind = earthscience::ScienceAnalysisKind::RegionalChange;
        query.analysis.baselineYear = 2017;
        query.analysis.comparisonYear = 2018;
        query.analysis.gridSize = 4;
        return query;
    }

    earthscience::ScienceProviderSnapshot waitForTerminal(
        earthscience::AlphaEarthEmbeddingRuntime& runtime,
        std::uint64_t generation)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        do
        {
            earthscience::ScienceProviderSnapshot snapshot = runtime.snapshot();
            if (snapshot.generation == generation &&
                (snapshot.state == earthscience::ScienceJobState::Ready ||
                 snapshot.state == earthscience::ScienceJobState::Failed ||
                 snapshot.state == earthscience::ScienceJobState::Cancelled))
                return snapshot;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        while (std::chrono::steady_clock::now() < deadline);
        require(false, "AlphaEarth runtime timed out");
        return {};
    }

    bool contains(const std::vector<std::string>& values,
                  const std::string& needle)
    {
        for (const std::string& value : values)
        {
            if (value.find(needle) != std::string::npos) return true;
        }
        return false;
    }

    earthscience::ScienceSourceDescriptor runtimeDescriptor()
    {
        earthscience::ScienceSourceDescriptor source;
        source.id = "alphaearth-foundations";
        source.firstYear = 2017;
        source.lastYear = 2025;
        source.nativeResolutionMeters = 10.0;
        source.health = earthscience::ScienceSourceHealth::Ready;
        source.variables = {
            {"embedding64", "Embedding A00-A63", "1", "embedding", 64}};
        source.capabilities.pointQuery = true;
        source.capabilities.explicitYears = true;
        source.capabilities.timeSeriesOutput = true;
        return source;
    }

    class RuntimeBackedProvider : public earthscience::IScienceProvider
    {
    public:
        explicit RuntimeBackedProvider(
            earthscience::AlphaEarthAssetResolver resolver)
            : _runtime(std::move(resolver))
        {
        }

        earthscience::ScienceSourceDescriptor descriptor() const override
        {
            return runtimeDescriptor();
        }

        std::uint64_t submit(
            const earthscience::GeoTemporalQuery& query) override
        {
            return _runtime.submit(query);
        }

        earthscience::ScienceProviderSnapshot snapshot() const override
        {
            return _runtime.snapshot();
        }

        void cancel(std::uint64_t generation) override
        {
            _runtime.cancel(generation);
        }

        void clear() override
        {
            const earthscience::ScienceProviderSnapshot state =
                _runtime.snapshot();
            _runtime.cancel(state.generation);
        }

    private:
        earthscience::AlphaEarthEmbeddingRuntime _runtime;
    };

    earthscience::ScienceJobSnapshot waitForServiceTerminal(
        earthscience::ScienceQueryService& service, std::uint64_t jobId)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(5);
        do
        {
            const earthscience::ScienceJobSnapshot snapshot =
                service.snapshot();
            if (snapshot.jobId == jobId &&
                (snapshot.state == earthscience::ScienceJobState::Ready ||
                 snapshot.state == earthscience::ScienceJobState::Failed ||
                 snapshot.state ==
                    earthscience::ScienceJobState::Cancelled))
                return snapshot;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        while (std::chrono::steady_clock::now() < deadline);
        require(false, "runtime-backed service timed out");
        return {};
    }

    void testRealRuntimeThroughputEnablesDurationBudget()
    {
        LocalFixture fixture;
        auto registry =
            std::make_unique<earthscience::ScienceSourceRegistry>();
        auto provider = std::make_unique<RuntimeBackedProvider>(
            [&fixture](double, double, int year,
                       earthscience::AlphaEarthAsset& asset,
                       std::string& error)
            {
                asset = fixture.asset(year);
                error.clear();
                return true;
            });
        std::string error;
        require(registry->add(std::move(provider), error),
                "runtime-backed provider registration failed");
        earthscience::ScienceQueryService service(std::move(registry));

        earthscience::GeoTemporalQuery query = pointQuery();
        query.variables = {"embedding64"};
        query.outputKind = earthscience::ScienceOutputKind::TimeSeries;
        const earthscience::ScienceQueryCost before = service.estimate(query);
        require(!before.durationDeterminate,
                "real runtime query started with fabricated duration");

        const std::uint64_t jobId = service.submit(query);
        const earthscience::ScienceJobSnapshot ready =
            waitForServiceTerminal(service, jobId);
        require(ready.state == earthscience::ScienceJobState::Ready &&
                    ready.progress.elapsedSeconds > 0.0,
                "real runtime success did not report elapsed seconds");
        const earthscience::ScienceQueryCost observed =
            service.estimate(query);
        require(observed.durationDeterminate &&
                    observed.estimatedDurationSeconds > 0.0,
                "real runtime success did not enable duration estimate");

        query.limits.maximumDurationSeconds =
            observed.estimatedDurationSeconds * 0.5;
        service.submit(query);
        const earthscience::ScienceJobSnapshot rejected = service.snapshot();
        require(rejected.state == earthscience::ScienceJobState::Failed &&
                    rejected.message == "analysis exceeds maximum duration",
                "observed runtime duration did not enforce duration budget");
    }

    void testCompletePointAndRegionalReadsUseOneGrid()
    {
        LocalFixture fixture;
        earthscience::AlphaEarthEmbeddingRuntime runtime(
            [&fixture](double, double, int year,
                       earthscience::AlphaEarthAsset& asset,
                       std::string& error)
            {
                if (year != 2017 && year != 2018)
                {
                    error = "No AlphaEarth tile covers this point and year";
                    return false;
                }
                asset = fixture.asset(year);
                error.clear();
                return true;
            });

        const std::uint64_t pointGeneration = runtime.submit(pointQuery());
        const earthscience::ScienceProviderSnapshot point =
            waitForTerminal(runtime, pointGeneration);
        require(point.state == earthscience::ScienceJobState::Ready &&
                    point.artifact,
                "complete point request did not become Ready");
        const earthscience::ScienceEmbeddingPayload& pointEmbedding =
            point.artifact->embedding;
        require(pointEmbedding.width == 1 && pointEmbedding.height == 1 &&
                    pointEmbedding.years &&
                    *pointEmbedding.years == std::vector<int>({2017, 2018}),
                "point payload shape or years changed");
        require(pointEmbedding.values && pointEmbedding.values->size() == 128 &&
                    pointEmbedding.mask &&
                    *pointEmbedding.mask ==
                        std::vector<unsigned char>({1, 1}),
                "point request did not return two complete 64D vectors");
        bool valid = false;
        const double expectedPoint = earthscience::dequantizeAlphaEarth(
            fixtureValue(1, 2, 2), valid);
        require(valid && nearlyEqual(pointEmbedding.values->at(0), expectedPoint) &&
                    nearlyEqual(pointEmbedding.values->at(64), expectedPoint),
                "point values were not decoded with dequantizeAlphaEarth");
        require(pointEmbedding.norms && pointEmbedding.norms->size() == 2 &&
                    std::isfinite(pointEmbedding.norms->front()),
                "point vector norms were not computed");
        require(pointEmbedding.processingSteps &&
                    contains(*pointEmbedding.processingSteps,
                             "nearest-neighbour") &&
                    contains(*pointEmbedding.processingSteps, "dequant"),
                "point processing provenance is incomplete");
        require(point.artifact->sourceReferences.size() == 2 &&
                    point.artifact->sourceReferences.front().datasetId ==
                        "local-alphaearth-2017" &&
                    point.artifact->sourceReferences.front().originalUrl ==
                        fixture.path(),
                "point source provenance is incomplete");
        require(point.artifact->analysis.kind ==
                    earthscience::ScienceAnalysisKind::PointSeries,
                "point-series analysis was not requested after decoding");

        const std::uint64_t regionGeneration = runtime.submit(regionQuery());
        const earthscience::ScienceProviderSnapshot region =
            waitForTerminal(runtime, regionGeneration);
        require(region.state == earthscience::ScienceJobState::Ready &&
                    region.artifact,
                "complete regional request did not become Ready");
        const earthscience::ScienceEmbeddingPayload& regionEmbedding =
            region.artifact->embedding;
        require(regionEmbedding.width == 4 && regionEmbedding.height == 4 &&
                    regionEmbedding.values &&
                    regionEmbedding.values->size() == 2u * 4u * 4u * 64u &&
                    regionEmbedding.mask &&
                    regionEmbedding.mask->size() == 2u * 4u * 4u,
                "regional payload is not two complete 4 by 4 64D grids");
        require(regionEmbedding.bounds.west == WEST &&
                    regionEmbedding.bounds.south == SOUTH &&
                    regionEmbedding.bounds.east == EAST &&
                    regionEmbedding.bounds.north == NORTH,
                "regional payload lost its exact bounds");
        require(regionEmbedding.groundGrid &&
                    regionEmbedding.groundGrid->columns == 4 &&
                    regionEmbedding.groundGrid->rows == 4 &&
                    regionEmbedding.groundGrid->points.size() == 16,
                "regional payload does not have one shared 4 by 4 ground grid");
        const double expectedRegional = earthscience::dequantizeAlphaEarth(
            fixtureValue(1, 1, 1), valid);
        require(valid && nearlyEqual(regionEmbedding.values->front(),
                                     expectedRegional),
                "regional nearest read changed the selected source position");
        require(regionEmbedding.mask->at(0) == 0 &&
                    regionEmbedding.mask->at(16) == 0 &&
                    regionEmbedding.validCellCount == 30 &&
                    regionEmbedding.noDataCellCount == 2,
                "complete-vector NoData masking changed");
        require(regionEmbedding.actualResolutionMeters > 0.0 &&
                    region.artifact->analysis.kind ==
                        earthscience::ScienceAnalysisKind::RegionalChange &&
                    region.artifact->analysis.scalarChangeRaster.groundGrid ==
                        regionEmbedding.groundGrid,
                "regional analysis lost the actual shared grid");
    }

    void testCancellationPublishesNoPartialReady()
    {
        LocalFixture fixture;
        std::mutex mutex;
        std::condition_variable condition;
        bool enteredSecondYear = false;
        bool releaseSecondYear = false;
        int calls = 0;
        earthscience::AlphaEarthEmbeddingRuntime runtime(
            [&](double, double, int year, earthscience::AlphaEarthAsset& asset,
                std::string& error)
            {
                std::unique_lock<std::mutex> lock(mutex);
                ++calls;
                if (calls == 2)
                {
                    enteredSecondYear = true;
                    condition.notify_all();
                    condition.wait(lock, [&] { return releaseSecondYear; });
                }
                asset = fixture.asset(year);
                error.clear();
                return true;
            });

        const std::uint64_t generation = runtime.submit(pointQuery());
        {
            std::unique_lock<std::mutex> lock(mutex);
            require(condition.wait_for(
                        lock, std::chrono::seconds(5),
                        [&] { return enteredSecondYear; }),
                    "fixture resolver did not reach the second year");
        }
        const earthscience::ScienceProviderSnapshot progress = runtime.snapshot();
        require(progress.generation == generation &&
                    progress.state == earthscience::ScienceJobState::Fetching &&
                    progress.progress.stage ==
                        earthscience::ScienceProgressStage::Reading &&
                    progress.progress.completedUnits == 64 &&
                    progress.progress.totalUnits == 64 &&
                    progress.progress.determinate &&
                    progress.progress.unit == "dimensions",
                "eight-band progress did not report a complete first year");

        runtime.cancel(generation);
        const earthscience::ScienceProviderSnapshot cancelled = runtime.snapshot();
        require(cancelled.state == earthscience::ScienceJobState::Cancelled &&
                    !cancelled.artifact,
                "cancel published a partial Ready artifact");
        {
            std::lock_guard<std::mutex> lock(mutex);
            releaseSecondYear = true;
        }
        condition.notify_all();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const earthscience::ScienceProviderSnapshot late = runtime.snapshot();
        require(late.generation == generation &&
                    late.state == earthscience::ScienceJobState::Cancelled &&
                    !late.artifact,
                "late cancelled work replaced the terminal snapshot");
    }

    void testInjectedResolverRejectsRemoteAssetsBeforeGdal()
    {
        const std::vector<std::string> rejectedPaths = {
            "https://example.invalid/must-not-be-requested.tif",
            "relative-alphaearth.tif",
            "/vsimem/injected-alphaearth.tif",
            std::filesystem::temp_directory_path().string()};
        for (const std::string& rejectedPath : rejectedPaths)
        {
            earthscience::AlphaEarthEmbeddingRuntime runtime(
                [&rejectedPath](double, double, int year,
                   earthscience::AlphaEarthAsset& asset,
                   std::string& error)
            {
                asset.datasetId = "remote-probe-" + std::to_string(year);
                asset.pathOrUrl = rejectedPath;
                asset.sourceVersion = "fixture-v1";
                asset.indexedBounds = {WEST, SOUTH, EAST, NORTH};
                error.clear();
                return true;
            });
            earthscience::GeoTemporalQuery query = pointQuery();
            query.time.explicitYears = {2017};
            const earthscience::ScienceProviderSnapshot rejected =
                waitForTerminal(runtime, runtime.submit(query));
            require(rejected.state == earthscience::ScienceJobState::Failed &&
                        rejected.message.find("local") != std::string::npos &&
                        rejected.message.find("open") == std::string::npos,
                    "injected resolver reached GDAL for a non-local asset");
        }
    }

    void testNoCoverageMetadataAndGdalErrorsStayDistinct()
    {
        earthscience::AlphaEarthEmbeddingRuntime noCoverage(
            [](double, double, int, earthscience::AlphaEarthAsset&,
               std::string& error)
            {
                error = "No AlphaEarth tile covers this point and year";
                return false;
            });
        const std::uint64_t missingGeneration = noCoverage.submit(pointQuery());
        const earthscience::ScienceProviderSnapshot missing =
            waitForTerminal(noCoverage, missingGeneration);
        require(missing.state == earthscience::ScienceJobState::Failed &&
                    missing.message ==
                        "No AlphaEarth tile covers this point and year",
                "missing coverage was collapsed into a GDAL failure");

        earthscience::AlphaEarthEmbeddingRuntime brokenGdal(
            [](double, double, int year, earthscience::AlphaEarthAsset& asset,
               std::string& error)
            {
                asset.datasetId = "missing-" + std::to_string(year);
                asset.pathOrUrl = "/tmp/osgsol-alphaearth-does-not-exist.tif";
                asset.sourceVersion = "fixture-v1";
                asset.indexedBounds = {WEST, SOUTH, EAST, NORTH};
                error.clear();
                return true;
            });
        const earthscience::ScienceProviderSnapshot broken = waitForTerminal(
            brokenGdal, brokenGdal.submit(pointQuery()));
        require(broken.state == earthscience::ScienceJobState::Failed &&
                    broken.message !=
                        "No AlphaEarth tile covers this point and year" &&
                    broken.message.find("regular local file") !=
                        std::string::npos,
                "invalid injected path was reported as missing coverage");

        LocalFixture fixture;
        fixture.changeDescription(64, "wrong");
        earthscience::AlphaEarthEmbeddingRuntime metadata(
            [&fixture](double, double, int year,
                       earthscience::AlphaEarthAsset& asset,
                       std::string& error)
            {
                asset = fixture.asset(year);
                error.clear();
                return true;
            });
        const earthscience::ScienceProviderSnapshot invalid = waitForTerminal(
            metadata, metadata.submit(pointQuery()));
        require(invalid.state == earthscience::ScienceJobState::Failed &&
                    invalid.message.find("A63") != std::string::npos,
                "ordered A00-A63 metadata was not validated before reads");

        LocalFixture oneBasedFixture(false, 1);
        earthscience::AlphaEarthEmbeddingRuntime oneBasedMetadata(
            [&oneBasedFixture](double, double, int year,
                               earthscience::AlphaEarthAsset& asset,
                               std::string& error)
            {
                asset = oneBasedFixture.asset(year);
                error.clear();
                return true;
            });
        const earthscience::ScienceProviderSnapshot oneBased =
            waitForTerminal(
                oneBasedMetadata, oneBasedMetadata.submit(pointQuery()));
        require(oneBased.state == earthscience::ScienceJobState::Failed &&
                    oneBased.message.find("A00") != std::string::npos,
                "one-based A01-A64 metadata was accepted as AlphaEarth");
    }

    void testProductionResolverUsesImmutableIndexSchemaAndSourceCoopPrefix()
    {
        static std::atomic<unsigned int> sequence{0};
        const std::string path = (std::filesystem::temp_directory_path() /
            ("osgsol-alphaearth-index-" +
             std::to_string(++sequence) + ".sqlite")).string();
        std::error_code staleError;
        std::filesystem::remove(path, staleError);
        sqlite3* database = nullptr;
        require(sqlite3_open(path.c_str(), &database) == SQLITE_OK,
                "could not create resolver fixture index");
        const char* schema =
            "CREATE TABLE metadata(asset_base_url TEXT NOT NULL);"
            "CREATE TABLE tiles(id INTEGER PRIMARY KEY,year INTEGER NOT NULL,"
            "dataset_id TEXT NOT NULL,cog_path TEXT NOT NULL,"
            "source_version TEXT NOT NULL,min_lon REAL NOT NULL,"
            "min_lat REAL NOT NULL,max_lon REAL NOT NULL,max_lat REAL NOT NULL);"
            "CREATE VIRTUAL TABLE tile_rtree USING "
            "rtree(id,min_lon,max_lon,min_lat,max_lat);"
            "INSERT INTO metadata VALUES('https://data.source.coop/google/');"
            "INSERT INTO tiles VALUES(1,2017,'dataset-2017',"
            "'must-not-open.blocked',"
            "'v1',100,20,101,21);"
            "INSERT INTO tile_rtree VALUES(1,100,101,20,21);";
        char* sqliteError = nullptr;
        require(sqlite3_exec(database, schema, nullptr, nullptr,
                             &sqliteError) == SQLITE_OK,
                sqliteError ? sqliteError : "could not seed resolver fixture");
        sqlite3_free(sqliteError);
        sqlite3_close(database);

        earthscience::AlphaEarthAsset asset;
        std::string error;
        require(earthscience::alphaearthdetail::
                    resolveAlphaEarthAssetFromIndex(
                        path, 20.5, 100.5, 2017, asset, error) &&
                    asset.datasetId == "dataset-2017" &&
                    asset.pathOrUrl ==
                        "https://data.source.coop/google/"
                        "must-not-open.blocked" &&
                    asset.sourceVersion == "v1" &&
                    asset.indexedBounds.west == 100.0,
                "production resolver lost the immutable index fields");

        earthscience::GeoTemporalQuery preOpenQuery = pointQuery();
        preOpenQuery.time.explicitYears = {2017};
        preOpenQuery.limits.maximumBytes = 1;
        earthscience::AlphaEarthEmbeddingRuntime preOpenRuntime(path);
        const earthscience::ScienceProviderSnapshot preOpenRejected =
            waitForTerminal(preOpenRuntime,
                            preOpenRuntime.submit(preOpenQuery));
        require(preOpenRejected.state == earthscience::ScienceJobState::Failed &&
                    preOpenRejected.message.find("source-byte budget") !=
                        std::string::npos,
                "production source budget was checked after remote GDAL open");

        database = nullptr;
        require(sqlite3_open(path.c_str(), &database) == SQLITE_OK,
                "could not reopen resolver fixture index");
        require(sqlite3_exec(database,
                    "UPDATE metadata SET asset_base_url='https://example.com/'",
                    nullptr, nullptr, nullptr) == SQLITE_OK,
                "could not change resolver fixture prefix");
        sqlite3_close(database);
        require(!earthscience::alphaearthdetail::
                    resolveAlphaEarthAssetFromIndex(
                        path, 20.5, 100.5, 2017, asset, error) &&
                    error.find("source.coop") != std::string::npos,
                "production resolver accepted an untrusted asset prefix");

        database = nullptr;
        require(sqlite3_open(path.c_str(), &database) == SQLITE_OK,
                "could not reopen resolver fixture without metadata");
        require(sqlite3_exec(database, "DELETE FROM metadata",
                    nullptr, nullptr, nullptr) == SQLITE_OK,
                "could not remove resolver metadata");
        sqlite3_close(database);
        require(!earthscience::alphaearthdetail::
                    resolveAlphaEarthAssetFromIndex(
                        path, 20.5, 100.5, 2017, asset, error) &&
                    error.find("metadata fault") != std::string::npos &&
                    error != "No AlphaEarth tile covers this point and year",
                "empty index metadata was misreported as no coverage");
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }

    void testBudgetRejectionPrecedesReadAndExplicitMeanAggregatesVectors()
    {
        LocalFixture fixture;
        int resolutions = 0;
        auto resolver = [&](double, double, int year,
                            earthscience::AlphaEarthAsset& asset,
                            std::string& error)
        {
            ++resolutions;
            asset = fixture.asset(year);
            error.clear();
            return true;
        };
        earthscience::AlphaEarthEmbeddingRuntime runtime(resolver);
        earthscience::GeoTemporalQuery rejected = regionQuery();
        rejected.limits.maximumBytes = 1;
        const earthscience::ScienceProviderSnapshot overBudget =
            waitForTerminal(runtime, runtime.submit(rejected));
        require(overBudget.state == earthscience::ScienceJobState::Failed &&
                    overBudget.message.find("source-byte budget") !=
                        std::string::npos && resolutions == 1,
                "oversized source read was not rejected from metadata cost");

        earthscience::GeoTemporalQuery memoryRejected = regionQuery();
        memoryRejected.limits.maximumMemoryBytes = 11000;
        const earthscience::ScienceProviderSnapshot overMemory =
            waitForTerminal(runtime, runtime.submit(memoryRejected));
        require(overMemory.state == earthscience::ScienceJobState::Failed &&
                    overMemory.message.find("resident-memory budget") !=
                        std::string::npos,
                "peak retained/combine/analysis memory was underestimated");

        earthscience::GeoTemporalQuery mean = regionQuery();
        mean.outputKind = earthscience::ScienceOutputKind::Embedding;
        mean.analysis.kind = earthscience::ScienceAnalysisKind::None;
        mean.aggregation = earthscience::ScienceAggregation::Mean;
        mean.limits.maximumBytes = 1024 * 1024;
        mean.limits.maximumMemoryBytes = 4 * 1024 * 1024;
        mean.limits.maximumResultCells = 2;
        const earthscience::ScienceProviderSnapshot aggregated =
            waitForTerminal(runtime, runtime.submit(mean));
        require(aggregated.state == earthscience::ScienceJobState::Ready &&
                    aggregated.artifact &&
                    aggregated.artifact->embedding.width == 1 &&
                    aggregated.artifact->embedding.height == 1 &&
                    aggregated.artifact->embedding.values &&
                    aggregated.artifact->embedding.values->size() == 128 &&
                    aggregated.artifact->embedding.mask &&
                    aggregated.artifact->embedding.mask->size() == 2,
                "explicit mean did not aggregate dequantized 64D vectors");
    }

    void testGridUpsamplingRequiresExplicitPermission()
    {
        LocalFixture fixture;
        earthscience::AlphaEarthEmbeddingRuntime runtime(
            [&fixture](double, double, int year,
                       earthscience::AlphaEarthAsset& asset,
                       std::string& error)
            {
                asset = fixture.asset(year);
                error.clear();
                return true;
            });
        earthscience::GeoTemporalQuery query = regionQuery();
        query.time.explicitYears = {2017};
        query.outputKind = earthscience::ScienceOutputKind::Embedding;
        query.analysis.kind = earthscience::ScienceAnalysisKind::None;
        query.analysis.gridSize = 9;
        query.limits.allowUpsampling = false;
        const earthscience::ScienceProviderSnapshot rejected =
            waitForTerminal(runtime, runtime.submit(query));
        require(rejected.state == earthscience::ScienceJobState::Failed &&
                    rejected.message.find("upsampling") != std::string::npos,
                "grid larger than the source window was silently upsampled");
    }

    void testSingleTileRejectsPartialRequestedCoverage()
    {
        LocalFixture fixture;
        earthscience::AlphaEarthEmbeddingRuntime runtime(
            [&fixture](double, double, int year,
                       earthscience::AlphaEarthAsset& asset,
                       std::string& error)
            {
                asset = fixture.asset(year);
                error.clear();
                return true;
            });
        earthscience::GeoTemporalQuery query = regionQuery();
        query.geometry.bounds.west = WEST - 0.01;
        query.time.explicitYears = {2017};
        query.outputKind = earthscience::ScienceOutputKind::Embedding;
        query.analysis.kind = earthscience::ScienceAnalysisKind::None;
        query.analysis.gridSize = 4;
        const earthscience::ScienceProviderSnapshot rejected =
            waitForTerminal(runtime, runtime.submit(query));
        require(rejected.state == earthscience::ScienceJobState::Failed &&
                    rejected.message.find("single source tile") !=
                        std::string::npos,
                "partially covered request was published with a false bbox");
    }

    void testRotatedSourcePublishesActualSampleGridAndFootprint()
    {
        LocalFixture fixture(true);
        earthscience::AlphaEarthEmbeddingRuntime runtime(
            [&fixture](double, double, int year,
                       earthscience::AlphaEarthAsset& asset,
                       std::string& error)
            {
                asset = fixture.asset(year);
                error.clear();
                return true;
            });
        earthscience::GeoTemporalQuery query = regionQuery();
        query.geometry.bounds = {100.038, 20.034, 100.058, 20.054};
        query.time.explicitYears = {2017};
        query.outputKind = earthscience::ScienceOutputKind::Embedding;
        query.analysis.kind = earthscience::ScienceAnalysisKind::None;
        query.analysis.gridSize = 4;
        const earthscience::ScienceProviderSnapshot ready =
            waitForTerminal(runtime, runtime.submit(query));
        require(ready.state == earthscience::ScienceJobState::Ready &&
                    ready.artifact && ready.artifact->embedding.groundGrid,
                "rotated source request did not become Ready");
        const auto& embedding = ready.artifact->embedding;
        const auto& firstPoint = embedding.groundGrid->points.front();
        const double requestedFirstLongitude =
            query.geometry.bounds.west + 0.125 *
            (query.geometry.bounds.east - query.geometry.bounds.west);
        require(!nearlyEqual(firstPoint.longitude, requestedFirstLongitude) &&
                    embedding.bounds.west < query.geometry.bounds.west &&
                    embedding.bounds.east > query.geometry.bounds.east &&
                    ready.artifact->sourceReferences.size() == 1 &&
                    nearlyEqual(ready.artifact->sourceReferences.front().
                                    actualCoverage.west,
                                embedding.bounds.west) &&
                    nearlyEqual(ready.artifact->sourceReferences.front().
                                    actualCoverage.north,
                                embedding.bounds.north),
                "rotated source reused requested coordinates as actual coverage");
    }
}

int main()
{
    testRealRuntimeThroughputEnablesDurationBudget();
    testRotatedSourcePublishesActualSampleGridAndFootprint();
    testSingleTileRejectsPartialRequestedCoverage();
    testGridUpsamplingRequiresExplicitPermission();
    testInjectedResolverRejectsRemoteAssetsBeforeGdal();
    testCompletePointAndRegionalReadsUseOneGrid();
    testCancellationPublishesNoPartialReady();
    testNoCoverageMetadataAndGdalErrorsStayDistinct();
    testProductionResolverUsesImmutableIndexSchemaAndSourceCoopPrefix();
    testBudgetRejectionPrecedesReadAndExplicitMeanAggregatesVectors();
    std::cout << "AlphaEarth embedding runtime tests passed\n";
    return 0;
}
