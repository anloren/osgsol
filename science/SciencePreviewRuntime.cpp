#include "SciencePreviewRuntime.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <sqlite3.h>
#include <thread>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <gdal_priv.h>
#include <gdal_frmts.h>

namespace earthscience
{
namespace
{
    constexpr int PREVIEW_SIZE = 256;
    constexpr int RGB_BANDS[] = {2, 17, 10};
    constexpr const char* RGB_NAMES[] = {"A01", "A16", "A09"};

    struct TileRecord
    {
        std::string datasetId;
        std::string relativePath;
        std::string version;
        std::string baseUrl;
        double west = 0.0;
        double south = 0.0;
        double east = 0.0;
        double north = 0.0;
    };

    struct SqliteCloser
    {
        void operator()(sqlite3* database) const
        {
            if (database) sqlite3_close(database);
        }
    };

    struct StatementCloser
    {
        void operator()(sqlite3_stmt* statement) const
        {
            if (statement) sqlite3_finalize(statement);
        }
    };

    struct DatasetCloser
    {
        void operator()(GDALDataset* dataset) const
        {
            if (dataset) GDALClose(dataset);
        }
    };

    using SqlitePtr = std::unique_ptr<sqlite3, SqliteCloser>;
    using StatementPtr = std::unique_ptr<sqlite3_stmt, StatementCloser>;
    using DatasetPtr = std::unique_ptr<GDALDataset, DatasetCloser>;

    std::string sqliteText(sqlite3_stmt* statement, int column)
    {
        const unsigned char* value = sqlite3_column_text(statement, column);
        return value ? reinterpret_cast<const char*>(value) : std::string();
    }

    bool selectTile(const std::string& indexPath, double latitude,
                    double longitude, int year, TileRecord& tile,
                    std::string& error)
    {
        sqlite3* rawDatabase = nullptr;
        const std::string uri = "file:" + indexPath + "?mode=ro&immutable=1";
        if (sqlite3_open_v2(uri.c_str(), &rawDatabase,
                            SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) !=
            SQLITE_OK)
        {
            error = rawDatabase ? sqlite3_errmsg(rawDatabase)
                                : "could not open AlphaEarth index";
            if (rawDatabase) sqlite3_close(rawDatabase);
            return false;
        }
        SqlitePtr database(rawDatabase);

        const char* query =
            "SELECT t.dataset_id,t.cog_path,t.source_version,"
            "t.min_lon,t.min_lat,t.max_lon,t.max_lat,m.asset_base_url "
            "FROM tile_rtree r JOIN tiles t ON t.id=r.id "
            "CROSS JOIN metadata m "
            "WHERE t.year=?1 AND r.min_lon<=?2 AND r.max_lon>=?2 "
            "AND r.min_lat<=?3 AND r.max_lat>=?3 "
            "ORDER BY t.id LIMIT 1";
        sqlite3_stmt* rawStatement = nullptr;
        if (sqlite3_prepare_v2(database.get(), query, -1, &rawStatement,
                               nullptr) != SQLITE_OK)
        {
            error = sqlite3_errmsg(database.get());
            return false;
        }
        StatementPtr statement(rawStatement);
        sqlite3_bind_int(statement.get(), 1, year);
        sqlite3_bind_double(statement.get(), 2, longitude);
        sqlite3_bind_double(statement.get(), 3, latitude);
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE)
        {
            error = "No AlphaEarth tile covers this point and year";
            return false;
        }
        if (step != SQLITE_ROW)
        {
            error = sqlite3_errmsg(database.get());
            return false;
        }
        tile.datasetId = sqliteText(statement.get(), 0);
        tile.relativePath = sqliteText(statement.get(), 1);
        tile.version = sqliteText(statement.get(), 2);
        tile.west = sqlite3_column_double(statement.get(), 3);
        tile.south = sqlite3_column_double(statement.get(), 4);
        tile.east = sqlite3_column_double(statement.get(), 5);
        tile.north = sqlite3_column_double(statement.get(), 6);
        tile.baseUrl = sqliteText(statement.get(), 7);
        if (tile.datasetId.empty() || tile.relativePath.empty() ||
            tile.baseUrl.rfind("https://data.source.coop/", 0) != 0)
        {
            error = "AlphaEarth index returned an invalid source record";
            return false;
        }
        return true;
    }

    unsigned char displayValue(std::int8_t raw)
    {
        const double normalized = std::abs(static_cast<double>(raw)) / 127.5;
        const double scientific = std::copysign(
            normalized * normalized, static_cast<double>(raw));
        const double display = std::clamp((scientific + 0.3) / 0.6,
                                          0.0, 1.0);
        return static_cast<unsigned char>(std::lround(display * 255.0));
    }

    bool readPreview(const TileRecord& tile,
                     const std::function<bool()>& cancelled,
                     std::shared_ptr<const std::vector<unsigned char>>& output,
                     std::string& sourceUrl, std::string& error)
    {
        static std::once_flag registration;
        std::call_once(registration, []()
        {
            GDALRegister_GTiff();
            GDALRegister_VRT();
            GDALRegister_MEM();
            CPLSetConfigOption("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR");
            CPLSetConfigOption("CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".tiff");
            CPLSetConfigOption("GDAL_HTTP_VERSION", "2TLS");
            CPLSetConfigOption("GDAL_HTTP_MULTIPLEX", "YES");
            CPLSetConfigOption("GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES");
            CPLSetConfigOption("GDAL_HTTP_CONNECTTIMEOUT", "8");
            CPLSetConfigOption("GDAL_HTTP_TIMEOUT", "25");
        });

        if (cancelled()) return false;
        sourceUrl = tile.baseUrl + tile.relativePath;
        const std::string vsiUrl = "/vsicurl/" + sourceUrl;
        CPLErrorReset();
        DatasetPtr dataset(static_cast<GDALDataset*>(GDALOpenEx(
            vsiUrl.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
            nullptr, nullptr, nullptr)));
        if (!dataset)
        {
            error = CPLGetLastErrorMsg();
            if (error.empty()) error = "AlphaEarth COG could not be opened";
            return false;
        }
        if (cancelled()) return false;
        if (dataset->GetRasterCount() < 17 ||
            dataset->GetRasterXSize() <= 0 || dataset->GetRasterYSize() <= 0)
        {
            error = "AlphaEarth source does not expose the expected 64 bands";
            return false;
        }
        for (int channel = 0; channel < 3; ++channel)
        {
            GDALRasterBand* band = dataset->GetRasterBand(RGB_BANDS[channel]);
            if (!band || band->GetRasterDataType() != GDT_Int8 ||
                std::string(band->GetDescription()) != RGB_NAMES[channel])
            {
                error = "AlphaEarth RGB band metadata changed";
                return false;
            }
        }

        std::vector<std::int8_t> raw(PREVIEW_SIZE * PREVIEW_SIZE * 3);
        GDALRasterIOExtraArg extra;
        INIT_RASTERIO_EXTRA_ARG(extra);
        extra.eResampleAlg = GRIORA_NearestNeighbour;
        int bandMap[] = {RGB_BANDS[0], RGB_BANDS[1], RGB_BANDS[2]};
        if (dataset->RasterIO(
                GF_Read, 0, 0, dataset->GetRasterXSize(),
                dataset->GetRasterYSize(), raw.data(), PREVIEW_SIZE,
                PREVIEW_SIZE, GDT_Int8, 3, bandMap, 3,
                PREVIEW_SIZE * 3, 1, &extra) != CE_None)
        {
            error = CPLGetLastErrorMsg();
            if (error.empty()) error = "AlphaEarth RGB read failed";
            return false;
        }
        if (cancelled()) return false;

        std::vector<unsigned char> masks(
            PREVIEW_SIZE * PREVIEW_SIZE * 3, 255);
        for (int channel = 0; channel < 3; ++channel)
        {
            GDALRasterBand* band = dataset->GetRasterBand(RGB_BANDS[channel]);
            if ((band->GetMaskFlags() & GMF_ALL_VALID) != 0) continue;
            std::vector<unsigned char> channelMask(
                PREVIEW_SIZE * PREVIEW_SIZE, 255);
            if (band->GetMaskBand()->RasterIO(
                    GF_Read, 0, 0, band->GetXSize(), band->GetYSize(),
                    channelMask.data(), PREVIEW_SIZE, PREVIEW_SIZE, GDT_Byte,
                    0, 0, nullptr) != CE_None)
            {
                error = "AlphaEarth NoData mask read failed";
                return false;
            }
            for (std::size_t pixel = 0; pixel < channelMask.size(); ++pixel)
                masks[pixel * 3 + channel] = channelMask[pixel];
        }

        auto rgba = std::make_shared<std::vector<unsigned char>>(
            PREVIEW_SIZE * PREVIEW_SIZE * 4);
        for (int y = 0; y < PREVIEW_SIZE; ++y)
        {
            const int sourceY = PREVIEW_SIZE - 1 - y;
            for (int x = 0; x < PREVIEW_SIZE; ++x)
            {
                const std::size_t source =
                    static_cast<std::size_t>(sourceY * PREVIEW_SIZE + x) * 3;
                const std::size_t destination =
                    static_cast<std::size_t>(y * PREVIEW_SIZE + x) * 4;
                bool valid = true;
                for (int channel = 0; channel < 3; ++channel)
                {
                    (*rgba)[destination + channel] =
                        displayValue(raw[source + channel]);
                    valid = valid && masks[source + channel] != 0;
                }
                (*rgba)[destination + 3] = valid ? 230 : 0;
            }
        }
        output = rgba;
        return true;
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
    };

    explicit Impl(const std::string& path)
        : indexPath(path), available(std::filesystem::is_regular_file(path))
    {
        state.state = available ? PreviewState::Idle
                                : PreviewState::Unavailable;
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

    void update(std::uint64_t value, PreviewState next, float progress,
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
                state.state = PreviewState::Fetching;
                state.progress = 0.05f;
                state.message = "Locating AlphaEarth tile";
            }

            TileRecord tile;
            std::string error;
            if (!selectTile(indexPath, request.latitude, request.longitude,
                            request.year, tile, error))
            {
                finishFailure(request, error);
                continue;
            }
            if (isCancelled(request.generation)) continue;
            update(request.generation, PreviewState::Fetching, 0.2f,
                   "Reading A01/A16/A09 preview");

            std::shared_ptr<const std::vector<unsigned char>> rgba;
            std::string sourceUrl;
            const bool read = readPreview(
                tile, [this, request]()
                { return isCancelled(request.generation); },
                rgba, sourceUrl, error);
            if (isCancelled(request.generation)) continue;
            if (!read)
            {
                finishFailure(request, error.empty()
                    ? "AlphaEarth request was cancelled" : error);
                continue;
            }

            std::lock_guard<std::mutex> lock(mutex);
            if (request.generation != generation || isCancelled(request.generation))
                continue;
            state.state = PreviewState::Ready;
            state.progress = 1.0f;
            state.message = "AlphaEarth preview ready";
            state.artifact.generation = request.generation;
            state.artifact.datasetId = tile.datasetId;
            state.artifact.sourceUrl = sourceUrl;
            state.artifact.sourceVersion = tile.version;
            state.artifact.attribution = descriptor.attribution;
            state.artifact.year = request.year;
            state.artifact.west = tile.west;
            state.artifact.south = tile.south;
            state.artifact.east = tile.east;
            state.artifact.north = tile.north;
            state.artifact.width = PREVIEW_SIZE;
            state.artifact.height = PREVIEW_SIZE;
            state.artifact.rgba = rgba;
        }
    }

    void finishFailure(const Request& request, const std::string& error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (request.generation != generation || isCancelled(request.generation))
            return;
        state.state = PreviewState::Failed;
        state.progress = 0.0f;
        state.message = error.empty() ? "AlphaEarth request failed" : error;
    }

    std::string indexPath;
    bool available = false;
    ScienceSourceDescriptor descriptor;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::uint64_t generation = 0;
    std::atomic<std::uint64_t> cancelledThrough{0};
    std::optional<Request> pending;
    SciencePreviewSnapshot state;
};

SciencePreviewRuntime::SciencePreviewRuntime(const std::string& indexPath)
    : _impl(new Impl(indexPath))
{
}

SciencePreviewRuntime::~SciencePreviewRuntime() = default;

bool SciencePreviewRuntime::available() const
{
    return _impl->available;
}

const ScienceSourceDescriptor& SciencePreviewRuntime::source() const
{
    return _impl->descriptor;
}

std::uint64_t SciencePreviewRuntime::queryPoint(
    double latitude, double longitude, int year)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    const std::uint64_t next = ++_impl->generation;
    _impl->cancelledThrough.store(next - 1, std::memory_order_release);
    _impl->state = SciencePreviewSnapshot();
    _impl->state.generation = next;
    _impl->state.latitude = latitude;
    _impl->state.longitude = longitude;
    _impl->state.year = year;
    if (!_impl->available)
    {
        _impl->state.state = PreviewState::Unavailable;
        _impl->state.message = "AlphaEarth index is missing";
        return next;
    }
    if (!std::isfinite(latitude) || !std::isfinite(longitude) ||
        latitude < -90.0 || latitude > 90.0 ||
        longitude < -180.0 || longitude > 180.0 ||
        year < _impl->descriptor.firstYear ||
        year > _impl->descriptor.lastYear)
    {
        _impl->state.state = PreviewState::Failed;
        _impl->state.message = "Invalid latitude, longitude, or year";
        return next;
    }
    _impl->state.state = PreviewState::Queued;
    _impl->state.progress = 0.0f;
    _impl->state.message = "Queued";
    _impl->pending = Impl::Request{next, latitude, longitude, year};
    _impl->condition.notify_one();
    return next;
}

void SciencePreviewRuntime::cancel()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    _impl->pending.reset();
    _impl->cancelledThrough.store(_impl->generation,
                                  std::memory_order_release);
    if (_impl->state.state == PreviewState::Queued ||
        _impl->state.state == PreviewState::Fetching)
    {
        _impl->state.state = PreviewState::Cancelled;
        _impl->state.progress = 0.0f;
        _impl->state.message = "Cancelled";
    }
}

void SciencePreviewRuntime::clear()
{
    cancel();
    std::lock_guard<std::mutex> lock(_impl->mutex);
    _impl->state = SciencePreviewSnapshot();
    _impl->state.state = _impl->available ? PreviewState::Idle
                                         : PreviewState::Unavailable;
    _impl->state.message = _impl->available ? "Ready"
                                            : "AlphaEarth index is missing";
}

SciencePreviewSnapshot SciencePreviewRuntime::snapshot() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->state;
}
}
