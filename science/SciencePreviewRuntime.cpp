#include "SciencePreviewRuntime.h"

#include "AlphaEarthEmbeddingReader.h"
#include "AlphaEarthMosaic.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <thread>

namespace earthscience
{
namespace
{
    constexpr int PREVIEW_SIZE = 256;
    constexpr int GROUND_GRID_SIZE = 33;
    constexpr int RGB_BANDS[] = {2, 17, 10};
    constexpr const char* RGB_NAMES[] = {"A01", "A16", "A09"};

    ScienceWgs84Bounds previewBounds(double latitude, double longitude,
                                     double requestedSpanMeters)
    {
        constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
        constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
        constexpr double PI = 3.14159265358979323846;
        const double span = requestedSpanMeters > 0.0
            ? requestedSpanMeters : 81920.0;
        const double latitudeHalfSpan =
            span * 0.5 / METERS_PER_LATITUDE_DEGREE;
        const double longitudeScale = METERS_PER_LONGITUDE_DEGREE *
            std::max(0.01, std::cos(latitude * PI / 180.0));
        const double longitudeHalfSpan = span * 0.5 / longitudeScale;
        return {
            longitude - longitudeHalfSpan,
            latitude - latitudeHalfSpan,
            longitude + longitudeHalfSpan,
            latitude + latitudeHalfSpan};
    }

    std::shared_ptr<const ScienceGroundGrid> previewGroundGrid(
        const ScienceWgs84Bounds& bounds)
    {
        ScienceGroundGrid grid;
        grid.columns = GROUND_GRID_SIZE;
        grid.rows = GROUND_GRID_SIZE;
        grid.points.reserve(GROUND_GRID_SIZE * GROUND_GRID_SIZE);
        for (int row = 0; row < GROUND_GRID_SIZE; ++row)
            for (int column = 0; column < GROUND_GRID_SIZE; ++column)
                grid.points.push_back({
                    bounds.west + (bounds.east - bounds.west) * column /
                        static_cast<double>(GROUND_GRID_SIZE - 1),
                    bounds.north - (bounds.north - bounds.south) * row /
                        static_cast<double>(GROUND_GRID_SIZE - 1)});
        return std::make_shared<const ScienceGroundGrid>(std::move(grid));
    }

}

struct SciencePreviewRuntime::Impl
{
    struct Request
    {
        std::uint64_t generation = 0;
        double latitude = 0.0;
        double longitude = 0.0;
        int year = 2025;
        double requestedSpanMeters = 0.0;
        std::chrono::steady_clock::time_point startedAt;
    };

    explicit Impl(const std::string& path)
        : assetSetResolver(
              [path](const ScienceWgs84Bounds& bounds, int year,
                     std::vector<AlphaEarthAsset>& assets,
                     std::string& error)
              {
                  return alphaearthdetail::resolveAlphaEarthAssetsFromIndex(
                      path, bounds, year, assets, error);
              }),
          available(std::filesystem::is_regular_file(path))
    {
        initialize();
    }

    explicit Impl(AlphaEarthAssetSetResolver resolver)
        : assetSetResolver(std::move(resolver)),
          available(static_cast<bool>(assetSetResolver)),
          injectedLocalResolver(true)
    {
        initialize();
    }

    void initialize()
    {
        state.state = available ? AlphaEarthPreviewState::Idle
                                : AlphaEarthPreviewState::Unavailable;
        state.message = available ? "Ready"
                                  : "AlphaEarth index is missing";
        worker = std::thread([this]() { run(); });
    }

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop.store(true, std::memory_order_release);
            cancelledThrough.store(generation, std::memory_order_release);
        }
        condition.notify_all();
        if (worker.joinable()) worker.join();
    }

    bool isCancelled(std::uint64_t value) const
    {
        return stop.load(std::memory_order_acquire) ||
               cancelledThrough.load(std::memory_order_acquire) >= value;
    }

    void update(std::uint64_t value, AlphaEarthPreviewState next,
                float progress,
                const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (value != generation || isCancelled(value)) return;
        state.state = next;
        state.progress = progress;
        state.message = message;
    }

    void run()
    {
        for (;;)
        {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this]()
                {
                    return stop.load(std::memory_order_acquire) ||
                           pending.has_value();
                });
                if (stop.load(std::memory_order_acquire)) return;
                request = *pending;
                pending.reset();
                if (request.generation != generation) continue;
                state.state = AlphaEarthPreviewState::Fetching;
                state.progress = 0.05f;
                state.message = "Locating AlphaEarth indexed tiles";
            }

            std::string error;
            if (isCancelled(request.generation)) continue;
            update(request.generation, AlphaEarthPreviewState::Fetching, 0.2f,
                   "Mosaicking A01/A16/A09 preview");

            const ScienceWgs84Bounds bounds = previewBounds(
                request.latitude, request.longitude,
                request.requestedSpanMeters);
            std::vector<AlphaEarthAsset> assets;
            const bool resolved = assetSetResolver && assetSetResolver(
                bounds, request.year, assets, error);
            if (resolved && injectedLocalResolver)
            {
                for (const AlphaEarthAsset& asset : assets)
                {
                    const std::filesystem::path path(asset.pathOrUrl);
                    std::error_code filesystemError;
                    if (asset.pathOrUrl.find("://") != std::string::npos ||
                        asset.pathOrUrl.rfind("/vsi", 0) == 0 ||
                        !path.is_absolute() ||
                        !std::filesystem::is_regular_file(
                            path, filesystemError))
                    {
                        error = "AlphaEarth injected preview resolver requires "
                                "absolute regular local files";
                        break;
                    }
                }
            }
            alphaearthdetail::AlphaEarthMosaicRaster mosaic;
            const std::vector<int> bands = {
                RGB_BANDS[0], RGB_BANDS[1], RGB_BANDS[2]};
            const std::vector<std::string> descriptions = {
                RGB_NAMES[0], RGB_NAMES[1], RGB_NAMES[2]};
            const bool read = resolved && error.empty() &&
                alphaearthdetail::readAlphaEarthMosaic(
                    assets, bounds, PREVIEW_SIZE, PREVIEW_SIZE,
                    bands, descriptions, [this, request]()
                    { return isCancelled(request.generation); },
                    mosaic, error);
            if (isCancelled(request.generation)) continue;
            if (!read)
            {
                finishFailure(request, error.empty()
                    ? "AlphaEarth request was cancelled" : error);
                continue;
            }
            std::vector<unsigned char> rgba = composePreviewRgba(
                mosaic.values, mosaic.masks, PREVIEW_SIZE, PREVIEW_SIZE);
            if (rgba.empty())
            {
                finishFailure(request,
                              "AlphaEarth mosaic RGBA conversion failed");
                continue;
            }
            std::size_t visiblePixels = 0;
            for (std::size_t pixel = 0; pixel < rgba.size() / 4; ++pixel)
                if (rgba[pixel * 4 + 3] != 0) ++visiblePixels;
            if (visiblePixels != rgba.size() / 4)
            {
                finishFailure(request,
                    "AlphaEarth indexed mosaic has incomplete visible coverage");
                continue;
            }

            std::lock_guard<std::mutex> lock(mutex);
            if (request.generation != generation || isCancelled(request.generation))
                continue;
            state.state = AlphaEarthPreviewState::Ready;
            state.progress = 1.0f;
            state.elapsedSeconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - request.startedAt).count();
            state.message = "AlphaEarth preview ready";
            state.artifact.generation = request.generation;
            state.artifact.datasetId =
                "mosaic-" + std::to_string(assets.size());
            state.artifact.sourceUrl = assets.front().pathOrUrl;
            if (assets.size() > 1)
                state.artifact.sourceUrl += " (+" +
                    std::to_string(assets.size() - 1) + " indexed tiles)";
            state.artifact.sourceVersion = assets.front().sourceVersion;
            state.artifact.attribution = descriptor.attribution;
            state.artifact.year = request.year;
            state.artifact.west = bounds.west;
            state.artifact.south = bounds.south;
            state.artifact.east = bounds.east;
            state.artifact.north = bounds.north;
            state.artifact.width = PREVIEW_SIZE;
            state.artifact.height = PREVIEW_SIZE;
            const double span = request.requestedSpanMeters > 0.0
                ? request.requestedSpanMeters : 81920.0;
            state.artifact.sourceWindowWidth =
                std::max(PREVIEW_SIZE, static_cast<int>(std::ceil(span / 10.0)));
            state.artifact.sourceWindowHeight =
                state.artifact.sourceWindowWidth;
            state.artifact.sourceResolutionMeters = 10.0;
            state.artifact.displayResolutionMeters = span / PREVIEW_SIZE;
            state.artifact.rgba =
                std::make_shared<const std::vector<unsigned char>>(
                    std::move(rgba));
            state.artifact.groundGrid = previewGroundGrid(bounds);
        }
    }

    void finishFailure(const Request& request, const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (request.generation != generation || isCancelled(request.generation))
            return;
        state.state = AlphaEarthPreviewState::Failed;
        state.progress = 0.0f;
        state.message = error.empty() ? "AlphaEarth request failed" : error;
    }

    AlphaEarthAssetSetResolver assetSetResolver;
    bool available = false;
    bool injectedLocalResolver = false;
    AlphaEarthSourceDescriptor descriptor;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::uint64_t generation = 0;
    std::atomic<std::uint64_t> cancelledThrough{0};
    std::optional<Request> pending;
    AlphaEarthPreviewSnapshot state;
};

SciencePreviewRuntime::SciencePreviewRuntime(const std::string& indexPath)
    : _impl(new Impl(indexPath))
{
}

SciencePreviewRuntime::SciencePreviewRuntime(
    AlphaEarthAssetSetResolver resolver)
    : _impl(new Impl(std::move(resolver)))
{
}

SciencePreviewRuntime::~SciencePreviewRuntime() = default;

bool SciencePreviewRuntime::available() const
{
    return _impl->available;
}

const AlphaEarthSourceDescriptor& SciencePreviewRuntime::source() const
{
    return _impl->descriptor;
}

std::uint64_t SciencePreviewRuntime::queryPoint(
    double latitude, double longitude, int year, double requestedSpanMeters)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    const std::uint64_t next = ++_impl->generation;
    _impl->cancelledThrough.store(next - 1, std::memory_order_release);
    _impl->state = AlphaEarthPreviewSnapshot();
    _impl->state.generation = next;
    _impl->state.latitude = latitude;
    _impl->state.longitude = longitude;
    _impl->state.year = year;
    if (!_impl->available)
    {
        _impl->state.state = AlphaEarthPreviewState::Unavailable;
        _impl->state.message = "AlphaEarth index is missing";
        return next;
    }
    if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
        latitude < -90.0 || latitude > 90.0 ||
        longitude < -180.0 || longitude > 180.0 ||
        year < _impl->descriptor.firstYear ||
        year > _impl->descriptor.lastYear ||
        !std::isfinite(requestedSpanMeters) || requestedSpanMeters < 0.0)
    {
        _impl->state.state = AlphaEarthPreviewState::Failed;
        _impl->state.message = "Invalid latitude, longitude, or year";
        return next;
    }
    _impl->state.state = AlphaEarthPreviewState::Queued;
    _impl->state.progress = 0.0f;
    _impl->state.message = "Queued";
    _impl->pending = Impl::Request{
        next, latitude, longitude, year, requestedSpanMeters,
        std::chrono::steady_clock::now()};
    _impl->condition.notify_one();
    return next;
}

void SciencePreviewRuntime::cancel()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    _impl->pending.reset();
    _impl->cancelledThrough.store(_impl->generation,
                                  std::memory_order_release);
    if (_impl->state.state == AlphaEarthPreviewState::Queued ||
        _impl->state.state == AlphaEarthPreviewState::Fetching)
    {
        _impl->state.state = AlphaEarthPreviewState::Cancelled;
        _impl->state.progress = 0.0f;
        _impl->state.message = "Cancelled";
    }
}

void SciencePreviewRuntime::clear()
{
    cancel();
    std::lock_guard<std::mutex> lock(_impl->mutex);
    _impl->state = AlphaEarthPreviewSnapshot();
    _impl->state.state = _impl->available
        ? AlphaEarthPreviewState::Idle
        : AlphaEarthPreviewState::Unavailable;
    _impl->state.message = _impl->available ? "Ready"
                                            : "AlphaEarth index is missing";
}

AlphaEarthPreviewSnapshot SciencePreviewRuntime::snapshot() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->state;
}
}
