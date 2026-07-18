#include "Sentinel2Runtime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <thread>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_http.h>
#include <cpl_string.h>
#include <gdal_priv.h>
#include <gdal_frmts.h>

namespace earthscience
{
namespace
{
    constexpr std::size_t MAX_STAC_BYTES = 2u * 1024u * 1024u;
    constexpr int PREVIEW_SIZE = 256;
    constexpr int GROUND_GRID_SIZE = 33;

    struct DatasetCloser
    {
        void operator()(GDALDataset* dataset) const
        {
            if (dataset) GDALClose(dataset);
        }
    };
    using DatasetPtr = std::unique_ptr<GDALDataset, DatasetCloser>;

    void configureGdal()
    {
        static std::once_flag registration;
        std::call_once(registration, []()
        {
            GDALRegister_GTiff();
            GDALRegister_VRT();
            GDALRegister_MEM();
            CPLSetConfigOption("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR");
            CPLSetConfigOption(
                "CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".tif,.tiff");
            CPLSetConfigOption("GDAL_HTTP_VERSION", "2TLS");
            CPLSetConfigOption("GDAL_HTTP_MULTIPLEX", "YES");
            CPLSetConfigOption(
                "GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES");
            CPLSetConfigOption("GDAL_HTTP_CONNECTTIMEOUT", "8");
            CPLSetConfigOption("GDAL_HTTP_TIMEOUT", "25");
        });
    }

    void sentinelError(std::string& error)
    {
        const std::string alpha = "AlphaEarth";
        std::size_t position = 0;
        while ((position = error.find(alpha, position)) != std::string::npos)
        {
            error.replace(position, alpha.size(), "Sentinel-2");
            position += 10;
        }
    }

    int cancelProgress(double, const char*, void* userData)
    {
        const auto* cancelled = static_cast<const std::function<bool()>*>(
            userData);
        return cancelled && *cancelled && (*cancelled)() ? FALSE : TRUE;
    }

    bool readVisualDataset(
        const std::string& datasetPath, const GeoTemporalQuery& query,
        const std::function<bool()>& cancelled,
        ScienceRasterPayload& output, std::string& error)
    {
        output = ScienceRasterPayload();
        configureGdal();
        if (cancelled()) { error = "cancelled"; return false; }
        CPLErrorReset();
        DatasetPtr dataset(static_cast<GDALDataset*>(GDALOpenEx(
            datasetPath.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
            nullptr, nullptr, nullptr)));
        if (!dataset)
        {
            error = CPLGetLastErrorMsg();
            if (error.empty()) error = "Sentinel-2 visual COG could not be opened";
            return false;
        }
        if (dataset->GetRasterCount() != 3 ||
            dataset->GetRasterXSize() <= 0 || dataset->GetRasterYSize() <= 0)
        {
            error = "Sentinel-2 visual COG must expose exactly three bands";
            return false;
        }
        for (int bandIndex = 1; bandIndex <= 3; ++bandIndex)
        {
            GDALRasterBand* band = dataset->GetRasterBand(bandIndex);
            if (!band || band->GetRasterDataType() != GDT_Byte)
            {
                error = "Sentinel-2 visual COG bands must be UInt8";
                return false;
            }
            const GDALColorInterp expected = bandIndex == 1 ? GCI_RedBand :
                (bandIndex == 2 ? GCI_GreenBand : GCI_BlueBand);
            if (band->GetColorInterpretation() != expected)
            {
                error = "Sentinel-2 visual COG must expose RGB band semantics";
                return false;
            }
        }
        double geotransform[6] = {};
        const char* sourceWkt = dataset->GetProjectionRef();
        if (dataset->GetGeoTransform(geotransform) != CE_None ||
            !sourceWkt || !*sourceWkt)
        {
            error = "Sentinel-2 visual COG georeference is missing";
            return false;
        }
        ScienceRasterWindow window = derivePreviewWindow(
            dataset->GetRasterXSize(), dataset->GetRasterYSize(),
            geotransform, sourceWkt, query.geometry.point.latitude,
            query.geometry.point.longitude,
            query.geometry.requestedSpanMeters, PREVIEW_SIZE, error);
        sentinelError(error);
        if (!error.empty()) return false;

        double windowTransform[6] = {};
        std::copy(geotransform, geotransform + 6, windowTransform);
        GDALApplyGeoTransform(geotransform, window.x, window.y,
                              &windowTransform[0], &windowTransform[3]);
        ScienceGroundGrid grid = buildPreviewGroundGrid(
            window.width, window.height, windowTransform, sourceWkt,
            GROUND_GRID_SIZE, GROUND_GRID_SIZE, error);
        sentinelError(error);
        if (!error.empty() || grid.points.empty()) return false;

        std::vector<unsigned char> rgb(PREVIEW_SIZE * PREVIEW_SIZE * 3);
        GDALRasterIOExtraArg extra;
        INIT_RASTERIO_EXTRA_ARG(extra);
        extra.eResampleAlg = GRIORA_NearestNeighbour;
        extra.pfnProgress = cancelProgress;
        extra.pProgressData = const_cast<std::function<bool()>*>(&cancelled);
        int bandMap[] = {1, 2, 3};
        if (dataset->RasterIO(
                GF_Read, window.x, window.y, window.width, window.height,
                rgb.data(), PREVIEW_SIZE, PREVIEW_SIZE, GDT_Byte,
                3, bandMap, 3, PREVIEW_SIZE * 3, 1, &extra) != CE_None)
        {
            if (cancelled()) error = "cancelled";
            else
            {
                error = CPLGetLastErrorMsg();
                if (error.empty()) error = "Sentinel-2 visual COG read failed";
            }
            return false;
        }
        if (cancelled()) { error = "cancelled"; return false; }

        std::vector<unsigned char> rgba(PREVIEW_SIZE * PREVIEW_SIZE * 4);
        for (std::size_t pixel = 0; pixel < rgb.size() / 3; ++pixel)
        {
            const std::size_t source = pixel * 3;
            const std::size_t destination = pixel * 4;
            rgba[destination] = rgb[source];
            rgba[destination + 1] = rgb[source + 1];
            rgba[destination + 2] = rgb[source + 2];
            rgba[destination + 3] =
                (rgb[source] || rgb[source + 1] || rgb[source + 2]) ? 210 : 0;
        }
        output.bounds = {grid.points.front().longitude,
                         grid.points.front().latitude,
                         grid.points.front().longitude,
                         grid.points.front().latitude};
        for (const ScienceGroundPoint& point : grid.points)
        {
            output.bounds.west = std::min(output.bounds.west, point.longitude);
            output.bounds.south = std::min(output.bounds.south, point.latitude);
            output.bounds.east = std::max(output.bounds.east, point.longitude);
            output.bounds.north = std::max(output.bounds.north, point.latitude);
        }
        output.width = output.height = PREVIEW_SIZE;
        output.sourceWindowWidth = window.width;
        output.sourceWindowHeight = window.height;
        output.sourceResolutionMeters = window.sourceResolutionMeters;
        output.displayResolutionMeters = window.displayResolutionMeters;
        output.rgba = std::make_shared<const std::vector<unsigned char>>(
            std::move(rgba));
        output.groundGrid = std::make_shared<const ScienceGroundGrid>(
            std::move(grid));
        error.clear();
        return true;
    }

    class UnconfiguredIo : public ISentinel2Io
    {
    public:
        bool fetchStac(const std::string&, std::size_t,
                       const std::function<bool()>&,
                       std::string&, std::string& error) override
        {
            error = "Sentinel-2 production I/O is not configured";
            return false;
        }

        bool readVisual(const Sentinel2Scene&, const GeoTemporalQuery&,
                        const std::function<bool()>&,
                        ScienceRasterPayload&, std::string& error) override
        {
            error = "Sentinel-2 production I/O is not configured";
            return false;
        }
    };

    class ProductionIo : public ISentinel2Io
    {
    public:
        bool fetchStac(const std::string& url, std::size_t maximumBytes,
                       const std::function<bool()>& cancelled,
                       std::string& body, std::string& error) override
        {
            body.clear();
            if (cancelled()) { error = "cancelled"; return false; }
            if (url.rfind(
                    "https://earth-search.aws.element84.com/v1/search?", 0) != 0)
            {
                error = "Sentinel-2 STAC endpoint is not allowlisted";
                return false;
            }
            char** options = nullptr;
            options = CSLSetNameValue(options, "CONNECTTIMEOUT", "8");
            options = CSLSetNameValue(options, "TIMEOUT", "15");
            options = CSLSetNameValue(options, "MAX_RETRY", "0");
            options = CSLSetNameValue(
                options, "HEADERS", "Accept: application/geo+json");
            CPLHTTPResult* result = CPLHTTPFetchEx(
                url.c_str(), options, cancelProgress,
                const_cast<std::function<bool()>*>(&cancelled),
                nullptr, nullptr);
            CSLDestroy(options);
            if (cancelled())
            {
                if (result) CPLHTTPDestroyResult(result);
                error = "cancelled";
                return false;
            }
            if (!result)
            {
                error = "Sentinel-2 STAC request returned no result";
                return false;
            }
            const std::unique_ptr<CPLHTTPResult, decltype(&CPLHTTPDestroyResult)>
                owned(result, CPLHTTPDestroyResult);
            if (result->nStatus != 0)
            {
                error = result->pszErrBuf && *result->pszErrBuf
                    ? result->pszErrBuf : "Sentinel-2 STAC request failed";
                return false;
            }
            if (result->nDataLen < 0 ||
                static_cast<std::size_t>(result->nDataLen) > maximumBytes)
            {
                error = "Sentinel-2 STAC response exceeds 2 MiB";
                return false;
            }
            if (!result->pabyData || result->nDataLen == 0)
            {
                error = "Sentinel-2 STAC response body is empty";
                return false;
            }
            if (result->pszContentType &&
                std::string(result->pszContentType).find("json") ==
                    std::string::npos)
            {
                error = "Sentinel-2 STAC response is not JSON";
                return false;
            }
            body.assign(reinterpret_cast<const char*>(result->pabyData),
                        static_cast<std::size_t>(result->nDataLen));
            error.clear();
            return true;
        }

        bool readVisual(
            const Sentinel2Scene& scene, const GeoTemporalQuery& query,
            const std::function<bool()>& cancelled,
            ScienceRasterPayload& output, std::string& error) override
        {
            static const std::string prefix =
                "https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
                "sentinel-s2-l2a-cogs/";
            if (scene.visualUrl.rfind(prefix, 0) != 0)
            {
                error = "Sentinel-2 visual asset is not allowlisted";
                return false;
            }
            return readVisualDataset(
                "/vsicurl/" + scene.visualUrl, query, cancelled,
                output, error);
        }
    };

    std::string number(double value)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << value;
        return stream.str();
    }
}

struct Sentinel2Runtime::Impl
{
    struct Request
    {
        std::uint64_t generation = 0;
        GeoTemporalQuery query;
        std::chrono::steady_clock::time_point startedAt;
    };

    explicit Impl(std::unique_ptr<ISentinel2Io> ownedIo)
        : io(std::move(ownedIo))
    {
        if (!io) io = std::make_unique<UnconfiguredIo>();
        state.state = ScienceJobState::Idle;
        state.message = "Ready on demand";
        worker = std::thread([this]() { run(); });
    }

    ~Impl()
    {
        stop.store(true, std::memory_order_release);
        cancelThrough(generation.load(std::memory_order_acquire));
        condition.notify_all();
        if (worker.joinable()) worker.join();
    }

    bool cancelled(std::uint64_t value) const
    {
        return stop.load(std::memory_order_acquire) ||
            cancelledThrough.load(std::memory_order_acquire) >= value ||
            value != generation.load(std::memory_order_acquire);
    }

    void cancelThrough(std::uint64_t value)
    {
        std::uint64_t current = cancelledThrough.load(std::memory_order_acquire);
        while (current < value &&
               !cancelledThrough.compare_exchange_weak(
                   current, value, std::memory_order_acq_rel,
                   std::memory_order_acquire)) {}
    }

    void publish(std::uint64_t value, ScienceJobState next,
                 ScienceProgressStage stage, const std::string& message,
                 std::shared_ptr<const ScienceArtifact> artifact = nullptr,
                 double elapsedSeconds = 0.0)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (value != generation.load(std::memory_order_acquire)) return;
        state.generation = value;
        state.state = next;
        state.progress.stage = stage;
        state.progress.determinate = next == ScienceJobState::Ready;
        state.progress.completedUnits = next == ScienceJobState::Ready ? 1 : 0;
        state.progress.totalUnits = next == ScienceJobState::Ready ? 1 : 0;
        state.progress.unit = next == ScienceJobState::Ready ? "scene" : "";
        state.progress.elapsedSeconds = elapsedSeconds;
        state.message = message;
        state.artifact = std::move(artifact);
    }

    void finishCancelled(std::uint64_t value)
    {
        publish(value, ScienceJobState::Cancelled,
                ScienceProgressStage::Cancelled, "Cancelled by user");
    }

    void execute(const Request& request)
    {
        const auto isCancelled = [this, value = request.generation]()
        { return cancelled(value); };
        publish(request.generation, ScienceJobState::Fetching,
                ScienceProgressStage::Locating,
                "Searching Sentinel-2 scenes");
        std::string url, error;
        ScienceWgs84Bounds bounds;
        if (!makeSentinel2PointSearchBounds(
                request.query.geometry, bounds, error) ||
            !buildSentinel2SearchUrl(
                bounds, request.query.time.intervalStart,
                request.query.time.intervalEnd,
                request.query.sceneFilters.maximumCloudCoverPercent,
                request.query.sceneFilters.maximumScenes, url, error))
        {
            publish(request.generation, ScienceJobState::Failed,
                    ScienceProgressStage::Failed, error);
            return;
        }
        std::string body;
        if (!io->fetchStac(
                url, MAX_STAC_BYTES, isCancelled, body, error))
        {
            if (isCancelled()) finishCancelled(request.generation);
            else publish(request.generation, ScienceJobState::Failed,
                         ScienceProgressStage::Failed, error);
            return;
        }
        if (isCancelled()) { finishCancelled(request.generation); return; }

        std::vector<Sentinel2Scene> scenes;
        Sentinel2Scene scene;
        if (!parseSentinel2Items(body, scenes, error) ||
            !selectSentinel2Item(
                scenes, request.query.sceneFilters.maximumCloudCoverPercent,
                scene, error))
        {
            publish(request.generation, ScienceJobState::Failed,
                    ScienceProgressStage::Failed, error);
            return;
        }
        publish(request.generation, ScienceJobState::Fetching,
                ScienceProgressStage::Reading,
                "Reading selected Sentinel-2 visual COG");
        ScienceRasterPayload raster;
        if (!io->readVisual(scene, request.query, isCancelled, raster, error))
        {
            if (isCancelled()) finishCancelled(request.generation);
            else publish(request.generation, ScienceJobState::Failed,
                         ScienceProgressStage::Failed, error);
            return;
        }
        if (isCancelled()) { finishCancelled(request.generation); return; }

        auto artifact = std::make_shared<ScienceArtifact>();
        artifact->artifactId = "sentinel-2-l2a-" + scene.itemId;
        artifact->query = request.query;
        artifact->generation = request.generation;
        artifact->raster = std::move(raster);
        artifact->visualizationId = request.query.visualizationId;
        artifact->processingVersion = "sentinel-2-visual-v1";
        ScienceSourceReference reference;
        reference.sourceId = "sentinel-2-l2a";
        reference.providerVersion = "earth-search-v1";
        reference.datasetId = scene.itemId;
        reference.originalUrl = scene.visualUrl;
        reference.requestedCoverage = request.query.geometry;
        reference.actualCoverage = artifact->raster.bounds;
        reference.variables = {"visual"};
        reference.units = {"display RGB"};
        reference.processingSteps = {
            "Earth Search STAC Item Search",
            "bounded visual COG window read",
            "256x256 natural-color display resampling",
        };
        reference.acquisitionTime = scene.acquisitionTime;
        reference.attribution =
            "Copernicus Sentinel data · Element 84 Earth Search · AWS Open Data";
        reference.fields = {
            {"scene_id", "Scene ID", scene.itemId, ""},
            {"collection", "Collection", "sentinel-2-l2a", ""},
            {"stac_endpoint", "STAC endpoint",
             "https://earth-search.aws.element84.com/v1/search", ""},
            {"visual_asset", "Selected visual COG", scene.visualUrl, ""},
            {"display_product", "Display product",
             "Sentinel-2 natural color (TCI)", ""},
            {"scene_cloud_cover", "Scene cloud cover",
             number(scene.cloudCoverPercent), "%"},
            {"source_resolution", "Source resolution",
             number(artifact->raster.sourceResolutionMeters), "m"},
            {"display_resolution", "Display resolution",
             number(artifact->raster.displayResolutionMeters), "m/pixel"},
            {"stac_request_count", "STAC requests", "1", "request"},
            {"selected_asset_count", "Selected COG assets", "1", "asset"},
            {"cog_access_mode", "COG access mode",
             "GDAL /vsicurl/ bounded window", ""},
            {"full_object_fallback", "Full-object fallback", "none", ""},
        };
        artifact->sourceReferences.push_back(std::move(reference));
        artifact->warnings.push_back(
            "Scene cloud cover is scene-wide, not a per-pixel cloud mask");
        const double elapsedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - request.startedAt).count();
        publish(request.generation, ScienceJobState::Ready,
                ScienceProgressStage::Ready, "Sentinel-2 scene ready",
                std::move(artifact), elapsedSeconds);
    }

    void run()
    {
        while (!stop.load(std::memory_order_acquire))
        {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this]()
                { return stop.load(std::memory_order_acquire) || pending.has_value(); });
                if (stop.load(std::memory_order_acquire)) break;
                request = *pending;
                pending.reset();
            }
            execute(request);
        }
    }

    std::unique_ptr<ISentinel2Io> io;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;
    std::optional<Request> pending;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> cancelledThrough{0};
    std::atomic<std::uint64_t> generation{0};
    ScienceProviderSnapshot state;
};

Sentinel2Runtime::Sentinel2Runtime()
    : _impl(new Impl(std::make_unique<ProductionIo>())) {}

Sentinel2Runtime::Sentinel2Runtime(std::unique_ptr<ISentinel2Io> io)
    : _impl(new Impl(std::move(io))) {}

Sentinel2Runtime::~Sentinel2Runtime() = default;

std::uint64_t Sentinel2Runtime::submit(const GeoTemporalQuery& query)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    const std::uint64_t value =
        _impl->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    _impl->cancelThrough(value - 1);
    _impl->pending = Impl::Request{value, query,
        std::chrono::steady_clock::now()};
    _impl->state = ScienceProviderSnapshot();
    _impl->state.generation = value;
    _impl->state.state = ScienceJobState::Queued;
    _impl->state.progress.stage = ScienceProgressStage::Queued;
    _impl->state.message = "Queued Sentinel-2 scene request";
    _impl->condition.notify_one();
    return value;
}

ScienceProviderSnapshot Sentinel2Runtime::snapshot() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->state;
}

void Sentinel2Runtime::cancel(std::uint64_t generation)
{
    _impl->cancelThrough(generation);
    _impl->finishCancelled(generation);
    _impl->condition.notify_all();
}

void Sentinel2Runtime::clear()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    const std::uint64_t staleGeneration =
        _impl->generation.fetch_add(1, std::memory_order_acq_rel);
    _impl->cancelThrough(staleGeneration);
    _impl->pending.reset();
    _impl->state = ScienceProviderSnapshot();
    _impl->state.state = ScienceJobState::Idle;
    _impl->state.message = "Ready on demand";
}

bool readSentinel2VisualDatasetForTest(
    const std::string& datasetPath, const GeoTemporalQuery& query,
    ScienceRasterPayload& output, std::string& error)
{
    return readVisualDataset(
        datasetPath, query, []() { return false; }, output, error);
}
}
