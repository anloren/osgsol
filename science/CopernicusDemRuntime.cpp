#include "CopernicusDemRuntime.h"

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
#include <utility>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <cpl_vsi.h>
#include <gdal_frmts.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>

namespace earthscience
{
namespace
{
    constexpr int PREVIEW_SIZE = 256;
    constexpr int GROUND_GRID_SIZE = 33;
    constexpr std::uint64_t MAX_NATIVE_CELLS = 16u * 1024u * 1024u;

    struct DatasetCloser
    {
        void operator()(GDALDataset* dataset) const
        {
            if (dataset) GDALClose(dataset);
        }
    };
    using DatasetPtr = std::unique_ptr<GDALDataset, DatasetCloser>;

    struct VrtOptionsCloser
    {
        void operator()(GDALBuildVRTOptions* options) const
        {
            if (options) GDALBuildVRTOptionsFree(options);
        }
    };
    using VrtOptionsPtr =
        std::unique_ptr<GDALBuildVRTOptions, VrtOptionsCloser>;

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

    int cancelProgress(double, const char*, void* userData)
    {
        const auto* cancelled = static_cast<const std::function<bool()>*>(
            userData);
        return cancelled && *cancelled && (*cancelled)() ? FALSE : TRUE;
    }

    bool validSample(float value, int hasNoData, double noData,
                     unsigned char mask)
    {
        return mask != 0 && std::isfinite(value) &&
            (!hasNoData || static_cast<double>(value) != noData);
    }

    unsigned char blend(unsigned char left, unsigned char right, double amount)
    {
        const double value = static_cast<double>(left) +
            (static_cast<double>(right) - static_cast<double>(left)) * amount;
        return static_cast<unsigned char>(std::clamp(value, 0.0, 255.0));
    }

    void hypsometricColor(float elevation, unsigned char* rgba)
    {
        struct Stop { double value; unsigned char r, g, b; };
        static const Stop stops[] = {
            {-500.0, 38, 88, 168}, {0.0, 72, 132, 187},
            {200.0, 98, 167, 92}, {1000.0, 201, 184, 124},
            {3000.0, 145, 105, 77}, {6000.0, 244, 244, 244},
            {9000.0, 255, 255, 255},
        };
        const Stop* lower = &stops[0];
        const Stop* upper = &stops[sizeof(stops) / sizeof(stops[0]) - 1];
        for (std::size_t index = 1;
             index < sizeof(stops) / sizeof(stops[0]); ++index)
        {
            if (elevation <= stops[index].value)
            {
                lower = &stops[index - 1];
                upper = &stops[index];
                break;
            }
        }
        const double amount = upper->value == lower->value ? 0.0 :
            std::clamp((static_cast<double>(elevation) - lower->value) /
                (upper->value - lower->value), 0.0, 1.0);
        rgba[0] = blend(lower->r, upper->r, amount);
        rgba[1] = blend(lower->g, upper->g, amount);
        rgba[2] = blend(lower->b, upper->b, amount);
        rgba[3] = 210;
    }

    bool checkedCellCount(int width, int height, std::uint64_t& count)
    {
        if (width <= 0 || height <= 0) return false;
        count = static_cast<std::uint64_t>(width) *
            static_cast<std::uint64_t>(height);
        return count <= MAX_NATIVE_CELLS;
    }

    ScienceRasterWindow deriveDemWindow(
        int rasterWidth, int rasterHeight, const double transform[6],
        const char* projection, const GeoTemporalQuery& query,
        std::string& error)
    {
        ScienceRasterWindow window;
        error.clear();
        if (rasterWidth <= 0 || rasterHeight <= 0 || !projection ||
            !*projection || !std::isfinite(query.geometry.point.latitude) ||
            !std::isfinite(query.geometry.point.longitude) ||
            !std::isfinite(query.geometry.requestedSpanMeters) ||
            query.geometry.requestedSpanMeters <= 0.0)
        {
            error = "Copernicus DEM window inputs are invalid";
            return window;
        }
        OGRSpatialReference source;
        source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (source.SetFromUserInput(projection) != OGRERR_NONE)
        {
            error = "Copernicus DEM source CRS is invalid";
            return window;
        }
        if (!source.IsGeographic())
        {
            window = derivePreviewWindow(
                rasterWidth, rasterHeight, transform, projection,
                query.geometry.point.latitude,
                query.geometry.point.longitude,
                query.geometry.requestedSpanMeters, PREVIEW_SIZE, error);
            if (!error.empty())
            {
                const std::string alpha = "AlphaEarth";
                const std::size_t position = error.find(alpha);
                if (position != std::string::npos)
                    error.replace(position, alpha.size(), "Copernicus DEM");
            }
            return window;
        }

        double inverse[6] = {};
        if (!GDALInvGeoTransform(transform, inverse))
        {
            error = "Copernicus DEM affine transform is not invertible";
            return window;
        }
        double pixelX = 0.0, pixelY = 0.0;
        GDALApplyGeoTransform(
            inverse, query.geometry.point.longitude,
            query.geometry.point.latitude, &pixelX, &pixelY);
        if (pixelX < 0.0 || pixelY < 0.0 ||
            pixelX > rasterWidth || pixelY > rasterHeight)
        {
            error = "Copernicus DEM view center is outside the selected cells";
            return window;
        }
        constexpr double PI_VALUE = 3.14159265358979323846;
        constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
        constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
        const double longitudeScale = METERS_PER_LONGITUDE_DEGREE *
            std::max(0.01, std::cos(
                query.geometry.point.latitude * PI_VALUE / 180.0));
        const double xResolution =
            std::hypot(transform[1], transform[4]) * longitudeScale;
        const double yResolution =
            std::hypot(transform[2], transform[5]) *
            METERS_PER_LATITUDE_DEGREE;
        if (!std::isfinite(xResolution) || !std::isfinite(yResolution) ||
            xResolution <= 0.0 || yResolution <= 0.0)
        {
            error = "Copernicus DEM source pixel resolution is unavailable";
            return window;
        }
        window.width = std::clamp(static_cast<int>(std::lround(
            query.geometry.requestedSpanMeters / xResolution)),
            1, rasterWidth);
        window.height = std::clamp(static_cast<int>(std::lround(
            query.geometry.requestedSpanMeters / yResolution)),
            1, rasterHeight);
        window.x = std::clamp(
            static_cast<int>(std::lround(pixelX - window.width * 0.5)),
            0, rasterWidth - window.width);
        window.y = std::clamp(
            static_cast<int>(std::lround(pixelY - window.height * 0.5)),
            0, rasterHeight - window.height);
        window.sourceResolutionMeters = std::max(xResolution, yResolution);
        window.displayResolutionMeters = std::max(
            xResolution * window.width / PREVIEW_SIZE,
            yResolution * window.height / PREVIEW_SIZE);
        return window;
    }

    ScienceWgs84Bounds gridBounds(const ScienceGroundGrid& grid)
    {
        ScienceWgs84Bounds bounds;
        if (grid.points.empty()) return bounds;
        bounds = {grid.points.front().longitude, grid.points.front().latitude,
                  grid.points.front().longitude, grid.points.front().latitude};
        for (const ScienceGroundPoint& point : grid.points)
        {
            bounds.west = std::min(bounds.west, point.longitude);
            bounds.south = std::min(bounds.south, point.latitude);
            bounds.east = std::max(bounds.east, point.longitude);
            bounds.north = std::max(bounds.north, point.latitude);
        }
        return bounds;
    }

    bool readDemDatasets(
        const std::vector<std::string>& datasetPaths,
        const GeoTemporalQuery& query,
        const std::function<bool()>& cancelled,
        CopernicusDemReadResult& output,
        std::string& error)
    {
        output = CopernicusDemReadResult();
        error.clear();
        if (datasetPaths.empty())
        {
            error = "No Copernicus DEM geocells cover the request";
            return false;
        }
        configureGdal();
        if (cancelled()) { error = "cancelled"; return false; }

        std::vector<DatasetPtr> sources;
        std::vector<GDALDatasetH> sourceHandles;
        sources.reserve(datasetPaths.size());
        sourceHandles.reserve(datasetPaths.size());
        for (const std::string& path : datasetPaths)
        {
            CPLErrorReset();
            DatasetPtr source(static_cast<GDALDataset*>(GDALOpenEx(
                path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                nullptr, nullptr, nullptr)));
            if (!source)
            {
                error = CPLGetLastErrorMsg();
                if (error.empty())
                    error = "Copernicus DEM COG could not be opened";
                return false;
            }
            if (source->GetRasterCount() != 1 ||
                source->GetRasterXSize() <= 0 ||
                source->GetRasterYSize() <= 0)
            {
                error = "Copernicus DEM COG must expose exactly one raster band";
                return false;
            }
            GDALRasterBand* band = source->GetRasterBand(1);
            if (!band || (band->GetRasterDataType() != GDT_Float32 &&
                          band->GetRasterDataType() != GDT_Float64))
            {
                error = "Copernicus DEM COG must expose floating-point height";
                return false;
            }
            double transform[6] = {};
            const char* projection = source->GetProjectionRef();
            if (source->GetGeoTransform(transform) != CE_None ||
                !projection || !*projection)
            {
                error = "Copernicus DEM COG georeference is missing";
                return false;
            }
            sourceHandles.push_back(source.get());
            sources.push_back(std::move(source));
        }

        DatasetPtr mosaic;
        GDALDataset* dataset = sources.front().get();
        std::string vrtPath;
        if (sources.size() > 1)
        {
            static std::atomic<std::uint64_t> vrtCounter{0};
            vrtPath = "/vsimem/osgsol-copernicus-dem-" +
                std::to_string(vrtCounter.fetch_add(
                    1, std::memory_order_acq_rel) + 1) + ".vrt";
            char* arguments[] = {
                const_cast<char*>("-resolution"),
                const_cast<char*>("highest"), nullptr};
            VrtOptionsPtr options(GDALBuildVRTOptionsNew(arguments, nullptr));
            if (!options)
            {
                error = "Copernicus DEM VRT options could not be created";
                return false;
            }
            GDALBuildVRTOptionsSetProgress(
                options.get(), cancelProgress,
                const_cast<std::function<bool()>*>(&cancelled));
            int usageError = FALSE;
            mosaic.reset(static_cast<GDALDataset*>(GDALBuildVRT(
                vrtPath.c_str(), static_cast<int>(sourceHandles.size()),
                sourceHandles.data(), nullptr, options.get(), &usageError)));
            if (!mosaic || usageError)
            {
                VSIUnlink(vrtPath.c_str());
                error = cancelled() ? "cancelled" :
                    "Copernicus DEM geocells could not be composed";
                return false;
            }
            dataset = mosaic.get();
        }

        double transform[6] = {};
        const char* projection = dataset->GetProjectionRef();
        if (dataset->GetGeoTransform(transform) != CE_None ||
            !projection || !*projection)
        {
            if (!vrtPath.empty()) VSIUnlink(vrtPath.c_str());
            error = "Copernicus DEM composed georeference is missing";
            return false;
        }
        ScienceRasterWindow window = deriveDemWindow(
            dataset->GetRasterXSize(), dataset->GetRasterYSize(),
            transform, projection, query, error);
        if (!error.empty())
        {
            if (!vrtPath.empty()) VSIUnlink(vrtPath.c_str());
            return false;
        }
        std::uint64_t nativeCount = 0;
        if (!checkedCellCount(window.width, window.height, nativeCount))
        {
            if (!vrtPath.empty()) VSIUnlink(vrtPath.c_str());
            error = "Copernicus DEM source window exceeds the cell budget";
            return false;
        }

        GDALRasterBand* band = dataset->GetRasterBand(1);
        GDALRasterBand* maskBand = band ? band->GetMaskBand() : nullptr;
        if (!band)
        {
            if (!vrtPath.empty()) VSIUnlink(vrtPath.c_str());
            error = "Copernicus DEM composed band is missing";
            return false;
        }
        GDALRasterIOExtraArg extra;
        INIT_RASTERIO_EXTRA_ARG(extra);
        extra.eResampleAlg = GRIORA_NearestNeighbour;
        extra.pfnProgress = cancelProgress;
        extra.pProgressData =
            const_cast<std::function<bool()>*>(&cancelled);

        std::vector<float> nativeValues(static_cast<std::size_t>(nativeCount));
        std::vector<unsigned char> nativeMask(
            static_cast<std::size_t>(nativeCount), 255);
        if (band->RasterIO(
                GF_Read, window.x, window.y, window.width, window.height,
                nativeValues.data(), window.width, window.height,
                GDT_Float32, 0, 0, &extra) != CE_None ||
            (maskBand && (maskBand->GetMaskFlags() & GMF_ALL_VALID) == 0 &&
             maskBand->RasterIO(
                GF_Read, window.x, window.y, window.width, window.height,
                nativeMask.data(), window.width, window.height,
                GDT_Byte, 0, 0, &extra) != CE_None))
        {
            if (!vrtPath.empty()) VSIUnlink(vrtPath.c_str());
            error = cancelled() ? "cancelled" :
                "Copernicus DEM numeric window read failed";
            return false;
        }

        int hasNoData = FALSE;
        const double noData = band->GetNoDataValue(&hasNoData);
        ScienceScalarSummary summary;
        summary.variableId = "surface_elevation";
        summary.displayName = "Surface elevation";
        summary.unit = "m";
        long double sum = 0.0;
        for (std::size_t index = 0; index < nativeValues.size(); ++index)
        {
            const float value = nativeValues[index];
            if (!validSample(value, hasNoData, noData, nativeMask[index]))
            {
                ++summary.noDataCellCount;
                continue;
            }
            if (!summary.minimumValid || value < summary.minimum)
                summary.minimum = value;
            if (!summary.maximumValid || value > summary.maximum)
                summary.maximum = value;
            summary.minimumValid = summary.maximumValid = true;
            sum += value;
            ++summary.validCellCount;
        }
        if (summary.validCellCount != 0)
        {
            summary.meanValid = true;
            summary.mean = static_cast<double>(
                sum / static_cast<long double>(summary.validCellCount));
        }

        double inverse[6] = {};
        double pixelX = 0.0, pixelY = 0.0;
        if (GDALInvGeoTransform(transform, inverse))
        {
            GDALApplyGeoTransform(
                inverse, query.geometry.point.longitude,
                query.geometry.point.latitude, &pixelX, &pixelY);
            const int centerX = static_cast<int>(std::floor(pixelX));
            const int centerY = static_cast<int>(std::floor(pixelY));
            if (centerX >= 0 && centerY >= 0 &&
                centerX < dataset->GetRasterXSize() &&
                centerY < dataset->GetRasterYSize())
            {
                float centerValue = 0.0f;
                unsigned char centerMask = 255;
                const bool valueRead = band->RasterIO(
                    GF_Read, centerX, centerY, 1, 1, &centerValue,
                    1, 1, GDT_Float32, 0, 0, nullptr) == CE_None;
                const bool maskRead = !maskBand ||
                    (maskBand->GetMaskFlags() & GMF_ALL_VALID) != 0 ||
                    maskBand->RasterIO(
                        GF_Read, centerX, centerY, 1, 1, &centerMask,
                        1, 1, GDT_Byte, 0, 0, nullptr) == CE_None;
                if (valueRead && maskRead &&
                    validSample(centerValue, hasNoData, noData, centerMask))
                {
                    summary.centerValid = true;
                    summary.center = centerValue;
                }
            }
        }

        std::vector<float> displayValues(PREVIEW_SIZE * PREVIEW_SIZE);
        std::vector<unsigned char> displayMask(
            PREVIEW_SIZE * PREVIEW_SIZE, 255);
        if (band->RasterIO(
                GF_Read, window.x, window.y, window.width, window.height,
                displayValues.data(), PREVIEW_SIZE, PREVIEW_SIZE,
                GDT_Float32, 0, 0, &extra) != CE_None ||
            (maskBand && (maskBand->GetMaskFlags() & GMF_ALL_VALID) == 0 &&
             maskBand->RasterIO(
                GF_Read, window.x, window.y, window.width, window.height,
                displayMask.data(), PREVIEW_SIZE, PREVIEW_SIZE,
                GDT_Byte, 0, 0, &extra) != CE_None))
        {
            if (!vrtPath.empty()) VSIUnlink(vrtPath.c_str());
            error = cancelled() ? "cancelled" :
                "Copernicus DEM display window read failed";
            return false;
        }

        std::vector<unsigned char> rgba(PREVIEW_SIZE * PREVIEW_SIZE * 4, 0);
        for (std::size_t index = 0; index < displayValues.size(); ++index)
        {
            if (!validSample(
                    displayValues[index], hasNoData, noData,
                    displayMask[index]))
                continue;
            hypsometricColor(displayValues[index], &rgba[index * 4]);
        }

        double windowTransform[6] = {};
        std::copy(transform, transform + 6, windowTransform);
        GDALApplyGeoTransform(transform, window.x, window.y,
                              &windowTransform[0], &windowTransform[3]);
        ScienceGroundGrid grid = buildPreviewGroundGrid(
            window.width, window.height, windowTransform, projection,
            GROUND_GRID_SIZE, GROUND_GRID_SIZE, error);
        if (!vrtPath.empty())
        {
            mosaic.reset();
            VSIUnlink(vrtPath.c_str());
        }
        if (!error.empty() || grid.points.empty()) return false;
        if (cancelled()) { error = "cancelled"; return false; }

        output.raster.bounds = gridBounds(grid);
        output.raster.width = PREVIEW_SIZE;
        output.raster.height = PREVIEW_SIZE;
        output.raster.sourceWindowWidth = window.width;
        output.raster.sourceWindowHeight = window.height;
        output.raster.sourceResolutionMeters = window.sourceResolutionMeters;
        output.raster.displayResolutionMeters = window.displayResolutionMeters;
        output.raster.rgba =
            std::make_shared<const std::vector<unsigned char>>(std::move(rgba));
        output.raster.groundGrid =
            std::make_shared<const ScienceGroundGrid>(std::move(grid));
        output.summary = std::move(summary);
        output.sourceUrls = datasetPaths;
        return true;
    }

    class UnconfiguredDemIo : public ICopernicusDemIo
    {
    public:
        bool read(const std::vector<CopernicusDemCell>&,
                  const GeoTemporalQuery&, const std::function<bool()>&,
                  CopernicusDemReadResult&, std::string& error) override
        {
            error = "Copernicus DEM production I/O is not configured";
            return false;
        }
    };

    class ProductionDemIo : public ICopernicusDemIo
    {
    public:
        bool read(const std::vector<CopernicusDemCell>& cells,
                  const GeoTemporalQuery& query,
                  const std::function<bool()>& cancelled,
                  CopernicusDemReadResult& output,
                  std::string& error) override
        {
            static const std::string prefix =
                "https://copernicus-dem-30m.s3.amazonaws.com/";
            std::vector<std::string> paths;
            paths.reserve(cells.size());
            for (const CopernicusDemCell& cell : cells)
            {
                if (cell.url.rfind(prefix, 0) != 0 ||
                    cell.url.find("..") != std::string::npos)
                {
                    error = "Copernicus DEM COG URL is not allowlisted";
                    return false;
                }
                paths.push_back("/vsicurl/" + cell.url);
            }
            const bool succeeded = readDemDatasets(
                paths, query, cancelled, output, error);
            if (succeeded)
            {
                output.sourceUrls.clear();
                for (const CopernicusDemCell& cell : cells)
                    output.sourceUrls.push_back(cell.url);
            }
            return succeeded;
        }
    };

    std::string number(double value)
    {
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(2) << value;
        return stream.str();
    }

    std::string joinedCellIds(const std::vector<CopernicusDemCell>& cells)
    {
        std::string result;
        for (const CopernicusDemCell& cell : cells)
        {
            if (!result.empty()) result += ',';
            result += cell.id;
        }
        return result;
    }
}

struct CopernicusDemRuntime::Impl
{
    struct Request
    {
        std::uint64_t generation = 0;
        GeoTemporalQuery query;
        std::chrono::steady_clock::time_point startedAt;
    };

    explicit Impl(std::unique_ptr<ICopernicusDemIo> ownedIo)
        : io(std::move(ownedIo))
    {
        if (!io) io = std::make_unique<UnconfiguredDemIo>();
        state.state = ScienceJobState::Idle;
        state.message = "Ready on demand";
        worker = std::thread([this]() { run(); });
    }

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop.store(true, std::memory_order_release);
        }
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
        state.progress.unit = next == ScienceJobState::Ready ? "artifact" : "";
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
                "Locating Copernicus DEM geocells");
        std::vector<CopernicusDemCell> cells;
        std::string error;
        if (!deriveCopernicusDemCells(request.query.geometry, cells, error))
        {
            publish(request.generation, ScienceJobState::Failed,
                    ScienceProgressStage::Failed, error);
            return;
        }
        if (isCancelled()) { finishCancelled(request.generation); return; }

        publish(request.generation, ScienceJobState::Fetching,
                ScienceProgressStage::Reading,
                "Reading bounded Copernicus DEM COG window");
        CopernicusDemReadResult result;
        if (!io->read(cells, request.query, isCancelled, result, error))
        {
            if (isCancelled()) finishCancelled(request.generation);
            else publish(request.generation, ScienceJobState::Failed,
                         ScienceProgressStage::Failed, error);
            return;
        }
        if (isCancelled()) { finishCancelled(request.generation); return; }

        auto artifact = std::make_shared<ScienceArtifact>();
        artifact->artifactId = "copernicus-dem-glo-30-" +
            std::to_string(request.generation);
        artifact->query = request.query;
        artifact->generation = request.generation;
        artifact->raster = std::move(result.raster);
        artifact->scalarSummaries.push_back(std::move(result.summary));
        artifact->visualizationId = request.query.visualizationId;
        artifact->warnings = std::move(result.warnings);
        artifact->warnings.push_back(
            "Copernicus DEM is a DSM; buildings, infrastructure, and "
            "vegetation can contribute to height");
        artifact->processingVersion = "copernicus-dem-hypsometric-v1";

        ScienceSourceReference reference;
        reference.sourceId = "copernicus-dem-glo-30";
        reference.providerVersion = "aws-glo30-2021";
        reference.datasetId = joinedCellIds(cells);
        const std::vector<std::string>& urls = result.sourceUrls.empty()
            ? std::vector<std::string>() : result.sourceUrls;
        reference.originalUrl = !urls.empty() ? urls.front() : cells.front().url;
        reference.requestedCoverage = request.query.geometry;
        reference.actualCoverage = artifact->raster.bounds;
        reference.variables = {"surface_elevation"};
        reference.units = {"m"};
        reference.processingSteps = {
            "bounded Copernicus DEM GLO-30 COG window read",
            "valid-cell scalar statistics",
            "fixed hypsometric display mapping",
        };
        reference.publicationTime = "2021";
        reference.attribution =
            "Copernicus DEM GLO-30 · European Union and ESA · AWS Open Data";
        const ScienceScalarSummary& summary = artifact->scalarSummaries.front();
        reference.fields = {
            {"surface_model", "Surface model",
             "DSM including buildings, infrastructure, and vegetation", ""},
            {"horizontal_crs", "Horizontal CRS", "WGS 84 (EPSG:4326)", ""},
            {"vertical_datum", "Vertical datum", "EGM2008", ""},
            {"vertical_unit", "Vertical unit", "metres", "m"},
            {"product_release", "Product release", "2021", ""},
            {"source_resolution", "Source resolution",
             number(artifact->raster.sourceResolutionMeters), "m nominal"},
            {"display_resolution", "Display resolution",
             number(artifact->raster.displayResolutionMeters), "m/pixel"},
            {"valid_cells", "Valid cells",
             std::to_string(summary.validCellCount), "cell"},
            {"nodata_cells", "NoData cells",
             std::to_string(summary.noDataCellCount), "cell"},
            {"cog_access_mode", "COG access mode",
             "GDAL /vsicurl/ bounded window", ""},
            {"full_object_fallback", "Full-object fallback", "none", ""},
        };
        artifact->sourceReferences.push_back(std::move(reference));
        const double elapsedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - request.startedAt).count();
        publish(request.generation, ScienceJobState::Ready,
                ScienceProgressStage::Ready, "Copernicus DEM artifact ready",
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
                { return stop.load(std::memory_order_acquire) || pending; });
                if (stop.load(std::memory_order_acquire)) break;
                request = *pending;
                pending.reset();
            }
            execute(request);
        }
    }

    std::unique_ptr<ICopernicusDemIo> io;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;
    std::optional<Request> pending;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> cancelledThrough{0};
    std::atomic<std::uint64_t> generation{0};
    ScienceProviderSnapshot state;
};

CopernicusDemRuntime::CopernicusDemRuntime()
    : _impl(new Impl(std::make_unique<ProductionDemIo>())) {}

CopernicusDemRuntime::CopernicusDemRuntime(
    std::unique_ptr<ICopernicusDemIo> io)
    : _impl(new Impl(std::move(io))) {}

CopernicusDemRuntime::~CopernicusDemRuntime() = default;

std::uint64_t CopernicusDemRuntime::submit(const GeoTemporalQuery& query)
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    const std::uint64_t value =
        _impl->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    _impl->cancelThrough(value - 1);
    _impl->pending = Impl::Request{
        value, query, std::chrono::steady_clock::now()};
    _impl->state = ScienceProviderSnapshot();
    _impl->state.generation = value;
    _impl->state.state = ScienceJobState::Queued;
    _impl->state.progress.stage = ScienceProgressStage::Queued;
    _impl->state.message = "Queued Copernicus DEM request";
    _impl->condition.notify_one();
    return value;
}

ScienceProviderSnapshot CopernicusDemRuntime::snapshot() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->state;
}

void CopernicusDemRuntime::cancel(std::uint64_t generation)
{
    _impl->cancelThrough(generation);
    _impl->finishCancelled(generation);
    _impl->condition.notify_all();
}

void CopernicusDemRuntime::clear()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    const std::uint64_t stale =
        _impl->generation.fetch_add(1, std::memory_order_acq_rel);
    _impl->cancelThrough(stale);
    _impl->pending.reset();
    _impl->state = ScienceProviderSnapshot();
    _impl->state.state = ScienceJobState::Idle;
    _impl->state.message = "Ready on demand";
}

bool readCopernicusDemDatasetsForTest(
    const std::vector<std::string>& datasetPaths,
    const GeoTemporalQuery& query,
    CopernicusDemReadResult& output,
    std::string& error)
{
    return readDemDatasets(
        datasetPaths, query, []() { return false; }, output, error);
}
}
