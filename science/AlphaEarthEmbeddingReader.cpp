#include "AlphaEarthEmbeddingReader.h"

#include "AlphaEarthMosaic.h"
#include "ScienceAnalysisEngine.h"
#include "ScienceEmbedding.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#include <cpl_conv.h>
#include <cpl_error.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <sqlite3.h>

namespace earthscience
{
namespace alphaearthdetail
{
namespace
{
    constexpr std::size_t COMPONENT_COUNT = SCIENCE_EMBEDDING_COMPONENTS;
    constexpr int COMPONENT_BATCH = 8;
    constexpr int MAX_GRID_SIZE = 256;
    constexpr int MAX_YEARS = 9;
    constexpr const char* SOURCE_PREFIX = "https://data.source.coop/";

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

    struct ReadPlan
    {
        int rasterWidth = 0, rasterHeight = 0;
        int x = 0, y = 0, width = 0, height = 0;
        int readWidth = 0, readHeight = 0;
        ScienceWgs84Bounds bounds;
        std::shared_ptr<const ScienceGroundGrid> groundGrid;
        double actualResolutionMeters = 0.0;
        std::array<double, 6> geotransform{};
        std::string projection;
    };

    struct PreparedDataset
    {
        AlphaEarthAsset asset;
        DatasetPtr dataset;
        ReadPlan plan;
    };

    bool fail(std::string& error, const std::string& message)
    {
        error = message;
        return false;
    }

    bool cancelled(const AlphaEarthReadCallbacks& callbacks)
    {
        return callbacks.cancelled && callbacks.cancelled();
    }

    bool checkedMultiply(std::uint64_t left, std::uint64_t right,
                         std::uint64_t& result)
    {
        if (left != 0 && right >
            std::numeric_limits<std::uint64_t>::max() / left)
            return false;
        result = left * right;
        return true;
    }

    bool checkedAdd(std::uint64_t left, std::uint64_t right,
                    std::uint64_t& result)
    {
        if (right > std::numeric_limits<std::uint64_t>::max() - left)
            return false;
        result = left + right;
        return true;
    }

    std::string sqliteText(sqlite3_stmt* statement, int column)
    {
        const unsigned char* value = sqlite3_column_text(statement, column);
        return value ? reinterpret_cast<const char*>(value) : std::string();
    }

    void registerGdal()
    {
        static std::once_flag registration;
        std::call_once(registration, []()
        {
            GDALRegister_GTiff();
            GDALRegister_VRT();
            GDALRegister_MEM();
            CPLSetConfigOption("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR");
            CPLSetConfigOption("CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".tiff,.tif");
            CPLSetConfigOption("GDAL_HTTP_VERSION", "2TLS");
            CPLSetConfigOption("GDAL_HTTP_MULTIPLEX", "YES");
            CPLSetConfigOption("GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES");
            CPLSetConfigOption("GDAL_HTTP_CONNECTTIMEOUT", "8");
            CPLSetConfigOption("GDAL_HTTP_TIMEOUT", "25");
        });
    }

    std::string openPath(const std::string& pathOrUrl)
    {
        return pathOrUrl.rfind("https://", 0) == 0
            ? "/vsicurl/" + pathOrUrl : pathOrUrl;
    }

    bool validateBands(GDALDataset& dataset, std::string& error)
    {
        if (dataset.GetRasterCount() != static_cast<int>(COMPONENT_COUNT))
            return fail(error,
                        "AlphaEarth source must expose exactly 64 bands");
        for (int index = 1; index <= static_cast<int>(COMPONENT_COUNT); ++index)
        {
            GDALRasterBand* band = dataset.GetRasterBand(index);
            char expected[4] = {};
            std::snprintf(expected, sizeof(expected), "A%02d", index - 1);
            if (!band || band->GetRasterDataType() != GDT_Int8 ||
                std::string(band->GetDescription()) != expected)
            {
                error = std::string("AlphaEarth band metadata mismatch at ") +
                    expected + "; expected ordered Int8 A00-A63";
                return false;
            }
        }
        return true;
    }

    bool wgs84ToPixel(GDALDataset& dataset, double longitude,
                      double latitude, double& pixelX, double& pixelY,
                      std::array<double, 6>& geotransform,
                      std::string& projection, std::string& error)
    {
        if (dataset.GetGeoTransform(geotransform.data()) != CE_None)
            return fail(error, "AlphaEarth source geotransform is missing");
        const char* sourceWkt = dataset.GetProjectionRef();
        if (!sourceWkt || !*sourceWkt)
            return fail(error, "AlphaEarth source CRS is missing");
        projection = sourceWkt;

        OGRSpatialReference source, wgs84;
        source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (source.SetFromUserInput(sourceWkt) != OGRERR_NONE ||
            wgs84.importFromEPSG(4326) != OGRERR_NONE)
            return fail(error, "AlphaEarth source CRS is invalid");
        using TransformPtr = std::unique_ptr<
            OGRCoordinateTransformation,
            decltype(&OCTDestroyCoordinateTransformation)>;
        TransformPtr transform(
            OGRCreateCoordinateTransformation(&wgs84, &source),
            OCTDestroyCoordinateTransformation);
        if (!transform || !transform->Transform(1, &longitude, &latitude))
            return fail(error, "AlphaEarth WGS84 coordinate transform failed");

        double inverse[6] = {};
        if (!GDALInvGeoTransform(geotransform.data(), inverse))
            return fail(error, "AlphaEarth geotransform is not invertible");
        GDALApplyGeoTransform(inverse, longitude, latitude, &pixelX, &pixelY);
        return std::isfinite(pixelX) && std::isfinite(pixelY) ? true :
            fail(error, "AlphaEarth pixel coordinate is not finite");
    }

    bool sourceToWgs84(const std::string& projection,
                       std::vector<double>& xCoordinates,
                       std::vector<double>& yCoordinates,
                       std::string& error)
    {
        if (xCoordinates.size() != yCoordinates.size() || xCoordinates.empty())
            return fail(error, "AlphaEarth source coordinate list is invalid");
        OGRSpatialReference source, wgs84;
        source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (source.SetFromUserInput(projection.c_str()) != OGRERR_NONE ||
            wgs84.importFromEPSG(4326) != OGRERR_NONE)
            return fail(error, "AlphaEarth source CRS is invalid");
        using TransformPtr = std::unique_ptr<
            OGRCoordinateTransformation,
            decltype(&OCTDestroyCoordinateTransformation)>;
        TransformPtr transform(
            OGRCreateCoordinateTransformation(&source, &wgs84),
            OCTDestroyCoordinateTransformation);
        if (!transform || !transform->Transform(
                static_cast<int>(xCoordinates.size()), xCoordinates.data(),
                yCoordinates.data()))
            return fail(error,
                        "AlphaEarth source-to-WGS84 transform failed");
        for (std::size_t index = 0; index < xCoordinates.size(); ++index)
        {
            if (!std::isfinite(xCoordinates[index]) ||
                !std::isfinite(yCoordinates[index]))
                return fail(error,
                            "AlphaEarth transformed coordinate is not finite");
        }
        return true;
    }

    double approximateResolutionMeters(const ScienceWgs84Bounds& bounds,
                                       int width, int height)
    {
        constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
        constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
        const double middleLatitude = (bounds.south + bounds.north) * 0.5;
        const double longitudeMeters = std::abs(bounds.east - bounds.west) *
            METERS_PER_LONGITUDE_DEGREE *
            std::max(0.01, std::cos(middleLatitude *
                                   3.14159265358979323846 / 180.0)) /
            static_cast<double>(std::max(1, width));
        const double latitudeMeters = std::abs(bounds.north - bounds.south) *
            METERS_PER_LATITUDE_DEGREE /
            static_cast<double>(std::max(1, height));
        return std::max(longitudeMeters, latitudeMeters);
    }

    double approximateDistanceMeters(const ScienceGroundPoint& left,
                                     const ScienceGroundPoint& right)
    {
        constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
        constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
        const double middleLatitude =
            (left.latitude + right.latitude) * 0.5;
        const double longitudeMeters =
            (right.longitude - left.longitude) *
            METERS_PER_LONGITUDE_DEGREE *
            std::cos(middleLatitude * 3.14159265358979323846 / 180.0);
        const double latitudeMeters =
            (right.latitude - left.latitude) * METERS_PER_LATITUDE_DEGREE;
        return std::hypot(longitudeMeters, latitudeMeters);
    }

    bool deriveActualCoordinates(ReadPlan& plan, std::string& error)
    {
        std::vector<double> cornerX(4), cornerY(4);
        const std::array<double, 4> pixelX = {
            static_cast<double>(plan.x),
            static_cast<double>(plan.x + plan.width),
            static_cast<double>(plan.x + plan.width),
            static_cast<double>(plan.x)};
        const std::array<double, 4> pixelY = {
            static_cast<double>(plan.y), static_cast<double>(plan.y),
            static_cast<double>(plan.y + plan.height),
            static_cast<double>(plan.y + plan.height)};
        for (std::size_t corner = 0; corner < pixelX.size(); ++corner)
            GDALApplyGeoTransform(plan.geotransform.data(), pixelX[corner],
                                  pixelY[corner], &cornerX[corner],
                                  &cornerY[corner]);
        if (!sourceToWgs84(plan.projection, cornerX, cornerY, error))
            return false;
        plan.bounds = {
            *std::min_element(cornerX.begin(), cornerX.end()),
            *std::min_element(cornerY.begin(), cornerY.end()),
            *std::max_element(cornerX.begin(), cornerX.end()),
            *std::max_element(cornerY.begin(), cornerY.end())};

        const std::size_t pointCount = static_cast<std::size_t>(
            plan.readWidth) * static_cast<std::size_t>(plan.readHeight);
        std::vector<double> groundX(pointCount), groundY(pointCount);
        for (int row = 0; row < plan.readHeight; ++row)
        {
            const int sourceRow = std::min(
                plan.height - 1,
                static_cast<int>(std::floor(
                    (static_cast<double>(row) + 0.5) * plan.height /
                    plan.readHeight)));
            for (int column = 0; column < plan.readWidth; ++column)
            {
                const int sourceColumn = std::min(
                    plan.width - 1,
                    static_cast<int>(std::floor(
                        (static_cast<double>(column) + 0.5) * plan.width /
                        plan.readWidth)));
                const std::size_t index = static_cast<std::size_t>(
                    row * plan.readWidth + column);
                GDALApplyGeoTransform(
                    plan.geotransform.data(),
                    static_cast<double>(plan.x + sourceColumn) + 0.5,
                    static_cast<double>(plan.y + sourceRow) + 0.5,
                    &groundX[index], &groundY[index]);
            }
        }
        if (!sourceToWgs84(plan.projection, groundX, groundY, error))
            return false;
        ScienceGroundGrid grid;
        grid.columns = plan.readWidth;
        grid.rows = plan.readHeight;
        grid.points.reserve(pointCount);
        for (std::size_t index = 0; index < pointCount; ++index)
            grid.points.push_back({groundX[index], groundY[index]});
        plan.groundGrid = std::make_shared<const ScienceGroundGrid>(
            std::move(grid));

        double resolution = 0.0;
        if (plan.readWidth > 1)
            resolution = std::max(resolution, approximateDistanceMeters(
                plan.groundGrid->points[0], plan.groundGrid->points[1]));
        if (plan.readHeight > 1)
            resolution = std::max(resolution, approximateDistanceMeters(
                plan.groundGrid->points[0],
                plan.groundGrid->points[plan.readWidth]));
        plan.actualResolutionMeters = resolution > 0.0 ? resolution :
            approximateResolutionMeters(plan.bounds, 1, 1);
        return true;
    }

    bool derivePlan(GDALDataset& dataset, const GeoTemporalQuery& query,
                    ReadPlan& plan, std::string& error)
    {
        plan.rasterWidth = dataset.GetRasterXSize();
        plan.rasterHeight = dataset.GetRasterYSize();
        if (plan.rasterWidth <= 0 || plan.rasterHeight <= 0)
            return fail(error, "AlphaEarth source raster dimensions are invalid");

        if (query.geometry.kind == ScienceGeometryKind::Point)
        {
            double pixelX = 0.0, pixelY = 0.0;
            if (!wgs84ToPixel(dataset, query.geometry.point.longitude,
                              query.geometry.point.latitude, pixelX, pixelY,
                              plan.geotransform, plan.projection, error))
                return false;
            plan.x = static_cast<int>(std::floor(pixelX));
            plan.y = static_cast<int>(std::floor(pixelY));
            if (plan.x < 0 || plan.y < 0 ||
                plan.x >= plan.rasterWidth || plan.y >= plan.rasterHeight)
                return fail(error,
                            "AlphaEarth point is outside the selected source tile");
            plan.width = plan.height = 1;
            plan.readWidth = plan.readHeight = 1;
            return deriveActualCoordinates(plan, error);
        }

        if (query.geometry.kind != ScienceGeometryKind::BoundingBox &&
            query.geometry.kind != ScienceGeometryKind::CurrentView)
            return fail(error, "AlphaEarth query geometry is unsupported");
        const ScienceWgs84Bounds& bounds = query.geometry.bounds;
        if (!std::isfinite(bounds.west) || !std::isfinite(bounds.south) ||
            !std::isfinite(bounds.east) || !std::isfinite(bounds.north) ||
            bounds.west >= bounds.east || bounds.south >= bounds.north)
            return fail(error, "AlphaEarth query bounds are invalid");

        std::array<double, 4> xCoordinates = {
            bounds.west, bounds.east, bounds.east, bounds.west};
        std::array<double, 4> yCoordinates = {
            bounds.south, bounds.south, bounds.north, bounds.north};
        std::array<double, 4> pixelX{}, pixelY{};
        for (std::size_t corner = 0; corner < xCoordinates.size(); ++corner)
        {
            if (!wgs84ToPixel(dataset, xCoordinates[corner],
                              yCoordinates[corner], pixelX[corner],
                              pixelY[corner], plan.geotransform,
                              plan.projection, error))
                return false;
        }
        const double minimumX = *std::min_element(pixelX.begin(), pixelX.end());
        const double maximumX = *std::max_element(pixelX.begin(), pixelX.end());
        const double minimumY = *std::min_element(pixelY.begin(), pixelY.end());
        const double maximumY = *std::max_element(pixelY.begin(), pixelY.end());
        constexpr double PIXEL_TOLERANCE = 1.0e-7;
        if (minimumX < -PIXEL_TOLERANCE || minimumY < -PIXEL_TOLERANCE ||
            maximumX > static_cast<double>(plan.rasterWidth) +
                PIXEL_TOLERANCE ||
            maximumY > static_cast<double>(plan.rasterHeight) +
                PIXEL_TOLERANCE)
            return fail(error,
                        "AlphaEarth single source tile does not fully cover "
                        "the requested bounds");
        plan.x = std::max(0, static_cast<int>(std::floor(minimumX + 1.0e-9)));
        plan.y = std::max(0, static_cast<int>(std::floor(minimumY + 1.0e-9)));
        const int right = std::min(
            plan.rasterWidth,
            static_cast<int>(std::ceil(maximumX - 1.0e-9)));
        const int bottom = std::min(
            plan.rasterHeight,
            static_cast<int>(std::ceil(maximumY - 1.0e-9)));
        plan.width = right - plan.x;
        plan.height = bottom - plan.y;
        if (plan.width <= 0 || plan.height <= 0)
            return fail(error,
                        "AlphaEarth bounds are outside the selected source tile");

        const int gridSize = std::clamp(query.analysis.gridSize, 1,
                                        MAX_GRID_SIZE);
        if (query.aggregation != ScienceAggregation::Mean &&
            !query.limits.allowUpsampling &&
            (gridSize > plan.width || gridSize > plan.height))
            return fail(error,
                        "AlphaEarth grid upsampling requires explicit permission");
        if (query.aggregation == ScienceAggregation::Mean)
        {
            plan.readWidth = plan.width;
            plan.readHeight = plan.height;
        }
        else
        {
            plan.readWidth = gridSize;
            plan.readHeight = gridSize;
        }
        return deriveActualCoordinates(plan, error);
    }

    bool sameSamplingPlan(const ReadPlan& reference, const ReadPlan& candidate)
    {
        return reference.rasterWidth == candidate.rasterWidth &&
               reference.rasterHeight == candidate.rasterHeight &&
               reference.x == candidate.x && reference.y == candidate.y &&
               reference.width == candidate.width &&
               reference.height == candidate.height &&
               reference.readWidth == candidate.readWidth &&
               reference.readHeight == candidate.readHeight &&
               reference.geotransform == candidate.geotransform &&
               reference.projection == candidate.projection;
    }

    bool prepareDataset(const AlphaEarthAsset& asset,
                        const GeoTemporalQuery& query,
                        PreparedDataset& prepared, std::string& error)
    {
        registerGdal();
        CPLErrorReset();
        const std::string path = openPath(asset.pathOrUrl);
        prepared.dataset.reset(static_cast<GDALDataset*>(GDALOpenEx(
            path.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
            nullptr, nullptr, nullptr)));
        if (!prepared.dataset)
        {
            error = CPLGetLastErrorMsg();
            if (error.empty()) error = "AlphaEarth source could not be opened";
            else error = "AlphaEarth source open failed: " + error;
            return false;
        }
        if (!validateBands(*prepared.dataset, error) ||
            !derivePlan(*prepared.dataset, query, prepared.plan, error))
            return false;
        prepared.asset = asset;
        return true;
    }

    bool validIndexedBounds(const ScienceWgs84Bounds& bounds)
    {
        return std::isfinite(bounds.west) && std::isfinite(bounds.south) &&
               std::isfinite(bounds.east) && std::isfinite(bounds.north) &&
               bounds.west < bounds.east && bounds.south < bounds.north;
    }

    bool productionBoundsCoverQuery(const ScienceWgs84Bounds& indexed,
                                    const GeoTemporalQuery& query)
    {
        if (query.geometry.kind == ScienceGeometryKind::Point)
            return query.geometry.point.longitude >= indexed.west &&
                   query.geometry.point.longitude <= indexed.east &&
                   query.geometry.point.latitude >= indexed.south &&
                   query.geometry.point.latitude <= indexed.north;
        const ScienceWgs84Bounds& requested = query.geometry.bounds;
        return requested.west >= indexed.west &&
               requested.south >= indexed.south &&
               requested.east <= indexed.east &&
               requested.north <= indexed.north;
    }

    bool enforceProductionPreOpenBudget(
        const GeoTemporalQuery& query, const AlphaEarthAsset& asset,
        std::size_t yearCount, std::string& error)
    {
        if (!validIndexedBounds(asset.indexedBounds))
            return fail(error,
                        "AlphaEarth production index bounds are invalid");
        if (!productionBoundsCoverQuery(asset.indexedBounds, query))
            return fail(error,
                        "AlphaEarth single indexed tile does not fully cover "
                        "the requested geometry");
        if (yearCount == 0 || query.limits.maximumBytes == 0) return true;

        std::uint64_t sourceCells = 1;
        if (query.geometry.kind != ScienceGeometryKind::Point)
        {
            constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
            constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
            constexpr double SOURCE_RESOLUTION_METERS = 10.0;
            const ScienceWgs84Bounds& bounds = query.geometry.bounds;
            const double middleLatitude =
                (bounds.south + bounds.north) * 0.5;
            const double widthMeters = (bounds.east - bounds.west) *
                METERS_PER_LONGITUDE_DEGREE *
                std::max(0.01, std::cos(middleLatitude *
                    3.14159265358979323846 / 180.0));
            const double heightMeters = (bounds.north - bounds.south) *
                METERS_PER_LATITUDE_DEGREE;
            if (!std::isfinite(widthMeters) || !std::isfinite(heightMeters) ||
                widthMeters <= 0.0 || heightMeters <= 0.0)
                return fail(error,
                            "AlphaEarth production source estimate is invalid");
            const std::uint64_t widthCells =
                static_cast<std::uint64_t>(
                    std::ceil(widthMeters / SOURCE_RESOLUTION_METERS)) + 2;
            const std::uint64_t heightCells =
                static_cast<std::uint64_t>(
                    std::ceil(heightMeters / SOURCE_RESOLUTION_METERS)) + 2;
            if (!checkedMultiply(widthCells, heightCells, sourceCells))
                return fail(error,
                            "AlphaEarth source-byte estimate overflowed");
        }
        std::uint64_t sourceBytes = 0;
        if (!checkedMultiply(sourceCells, COMPONENT_COUNT, sourceBytes) ||
            !checkedMultiply(sourceBytes,
                             static_cast<std::uint64_t>(yearCount),
                             sourceBytes))
            return fail(error, "AlphaEarth source-byte estimate overflowed");
        if (sourceBytes > query.limits.maximumBytes)
            return fail(error, "AlphaEarth source-byte budget exceeded");
        return true;
    }

    bool resolveAndPrepare(const GeoTemporalQuery& query,
                           const AlphaEarthAssetResolver& resolver,
                           bool injectedLocalResolver,
                           int year, std::size_t preOpenBudgetYears,
                           PreparedDataset& prepared,
                           std::string& error)
    {
        const double latitude = query.geometry.kind == ScienceGeometryKind::Point
            ? query.geometry.point.latitude
            : (query.geometry.bounds.south + query.geometry.bounds.north) * 0.5;
        const double longitude = query.geometry.kind == ScienceGeometryKind::Point
            ? query.geometry.point.longitude
            : (query.geometry.bounds.west + query.geometry.bounds.east) * 0.5;
        AlphaEarthAsset asset;
        if (!resolver(latitude, longitude, year, asset, error)) return false;
        if (asset.datasetId.empty() || asset.pathOrUrl.empty() ||
            asset.sourceVersion.empty())
            return fail(error, "AlphaEarth resolver returned incomplete metadata");
        if (injectedLocalResolver)
        {
            const std::filesystem::path localPath(asset.pathOrUrl);
            std::error_code filesystemError;
            if (asset.pathOrUrl.find("://") != std::string::npos ||
                asset.pathOrUrl.rfind("/vsi", 0) == 0 ||
                !localPath.is_absolute() ||
                !std::filesystem::is_regular_file(localPath, filesystemError))
                return fail(error,
                    "AlphaEarth injected resolver requires an absolute "
                    "regular local file");
        }
        else
        {
            if (asset.pathOrUrl.rfind(SOURCE_PREFIX, 0) != 0)
                return fail(error,
                    "AlphaEarth production resolver requires a trusted "
                    "source.coop asset URL");
            if (!enforceProductionPreOpenBudget(
                    query, asset, preOpenBudgetYears, error))
                return false;
        }
        return prepareDataset(asset, query, prepared, error);
    }

    bool enforceBudget(const GeoTemporalQuery& query, const ReadPlan& plan,
                       std::size_t yearCount, std::string& error)
    {
        std::uint64_t sourceCells = 0, sourceBytes = 0;
        if (!checkedMultiply(static_cast<std::uint64_t>(plan.width),
                             static_cast<std::uint64_t>(plan.height),
                             sourceCells) ||
            !checkedMultiply(sourceCells, COMPONENT_COUNT, sourceBytes) ||
            !checkedMultiply(sourceBytes,
                             static_cast<std::uint64_t>(yearCount),
                             sourceBytes))
            return fail(error, "AlphaEarth source-byte estimate overflowed");
        if (query.limits.maximumBytes != 0 &&
            sourceBytes > query.limits.maximumBytes)
            return fail(error, "AlphaEarth source-byte budget exceeded");

        std::uint64_t readCells = 0;
        if (!checkedMultiply(static_cast<std::uint64_t>(plan.readWidth),
                             static_cast<std::uint64_t>(plan.readHeight),
                             readCells))
            return fail(error, "AlphaEarth resident-memory estimate overflowed");
        const std::uint64_t retainedYears =
            query.analysis.kind == ScienceAnalysisKind::RegionalChange
                ? std::min<std::uint64_t>(2, yearCount) : yearCount;
        const std::uint64_t combinedYears =
            query.analysis.kind == ScienceAnalysisKind::RegionalChange
                ? retainedYears : static_cast<std::uint64_t>(yearCount);

        constexpr std::uint64_t PERSISTENT_BYTES_PER_CELL =
            COMPONENT_COUNT * sizeof(float) + sizeof(unsigned char) +
            sizeof(float);
        constexpr std::uint64_t READ_WORK_BYTES_PER_CELL =
            COMPONENT_COUNT * sizeof(std::int8_t) +
            COMPONENT_BATCH * sizeof(std::int8_t);
        constexpr std::uint64_t ANALYSIS_WORK_BYTES_PER_CELL =
            COMPONENT_COUNT * (sizeof(float) + sizeof(double));
        std::uint64_t persistentSlice = 0, retainedBytes = 0;
        std::uint64_t combinedBytes = 0, readWorkBytes = 0;
        std::uint64_t analysisCells = 0, analysisBytes = 0;
        std::uint64_t residentBytes = 0;
        if (!checkedMultiply(readCells, PERSISTENT_BYTES_PER_CELL,
                             persistentSlice) ||
            !checkedMultiply(persistentSlice, retainedYears,
                             retainedBytes) ||
            !checkedMultiply(persistentSlice, combinedYears,
                             combinedBytes) ||
            !checkedMultiply(readCells, READ_WORK_BYTES_PER_CELL,
                             readWorkBytes) ||
            !checkedMultiply(readCells, combinedYears, analysisCells) ||
            !checkedMultiply(analysisCells,
                             ANALYSIS_WORK_BYTES_PER_CELL, analysisBytes) ||
            !checkedAdd(retainedBytes, combinedBytes, residentBytes) ||
            !checkedAdd(residentBytes, readWorkBytes, residentBytes) ||
            !checkedAdd(residentBytes, analysisBytes, residentBytes))
            return fail(error, "AlphaEarth resident-memory estimate overflowed");

        if (query.analysis.kind == ScienceAnalysisKind::PrincipalComponents)
        {
            std::uint64_t covarianceBytes = 0;
            if (!checkedMultiply(COMPONENT_COUNT, COMPONENT_COUNT,
                                 covarianceBytes) ||
                !checkedMultiply(covarianceBytes, sizeof(double),
                                 covarianceBytes) ||
                !checkedAdd(residentBytes, covarianceBytes, residentBytes))
                return fail(error,
                    "AlphaEarth resident-memory estimate overflowed");
        }
        else if (query.analysis.kind == ScienceAnalysisKind::RegionalChange)
        {
            std::uint64_t rasterBytes = 0;
            if (!checkedMultiply(readCells, 64u, rasterBytes) ||
                !checkedAdd(residentBytes, rasterBytes, residentBytes))
                return fail(error,
                    "AlphaEarth resident-memory estimate overflowed");
        }
        if (query.limits.maximumMemoryBytes != 0 &&
            residentBytes > query.limits.maximumMemoryBytes)
            return fail(error, "AlphaEarth resident-memory budget exceeded");

        const std::uint64_t resultCells =
            query.aggregation == ScienceAggregation::Mean
                ? static_cast<std::uint64_t>(yearCount)
                : readCells * static_cast<std::uint64_t>(yearCount);
        if (query.limits.maximumResultCells != 0 &&
            resultCells > query.limits.maximumResultCells)
            return fail(error, "AlphaEarth result-cell budget exceeded");
        return true;
    }

    ScienceSourceReference makeSourceReference(
        const AlphaEarthAsset& asset, const GeoTemporalQuery& query,
        const ReadPlan& plan, int year)
    {
        ScienceSourceReference reference;
        reference.sourceId = "alphaearth-foundations";
        reference.providerVersion = asset.sourceVersion;
        reference.datasetId = asset.datasetId;
        reference.originalUrl = asset.pathOrUrl;
        reference.requestedCoverage = query.geometry;
        reference.actualCoverage = plan.bounds;
        reference.variables.reserve(COMPONENT_COUNT);
        reference.units.reserve(COMPONENT_COUNT);
        for (int component = 0;
             component < static_cast<int>(COMPONENT_COUNT); ++component)
        {
            char name[4] = {};
            std::snprintf(name, sizeof(name), "A%02d", component);
            reference.variables.emplace_back(name);
            reference.units.emplace_back("1");
        }
        reference.processingSteps = {
            "ordered Int8 A00-A63 read in eight-band batches",
            "nearest-neighbour sampling on one shared ground grid",
            "official signed AlphaEarth dequantization",
            "complete-vector NoData masking and norm calculation"};
        if (query.aggregation == ScienceAggregation::Mean)
            reference.processingSteps.push_back(
                "explicit 64D mean-direction aggregation");
        reference.acquisitionTime = std::to_string(year);
        reference.attribution = "Google / Google DeepMind; source.coop; CC-BY 4.0";
        return reference;
    }

    bool readPreparedYear(PreparedDataset& prepared,
                          const GeoTemporalQuery& query, int year,
                          const AlphaEarthReadCallbacks& callbacks,
                          std::shared_ptr<const ScienceEmbeddingPayload>& output,
                          std::string& error)
    {
        const ReadPlan& plan = prepared.plan;
        const std::size_t cellCount = static_cast<std::size_t>(
            plan.readWidth) * static_cast<std::size_t>(plan.readHeight);
        std::vector<std::int8_t> raw(cellCount * COMPONENT_COUNT);
        GDALRasterIOExtraArg extra;
        INIT_RASTERIO_EXTRA_ARG(extra);
        extra.eResampleAlg = GRIORA_NearestNeighbour;

        for (int first = 0; first < static_cast<int>(COMPONENT_COUNT);
             first += COMPONENT_BATCH)
        {
            if (cancelled(callbacks))
                return fail(error, "Cancelled");
            int bandMap[COMPONENT_BATCH] = {};
            for (int band = 0; band < COMPONENT_BATCH; ++band)
                bandMap[band] = first + band + 1;
            std::vector<std::int8_t> batch(cellCount * COMPONENT_BATCH);
            CPLErrorReset();
            if (prepared.dataset->RasterIO(
                    GF_Read, plan.x, plan.y, plan.width, plan.height,
                    batch.data(), plan.readWidth, plan.readHeight,
                    GDT_Int8, COMPONENT_BATCH, bandMap,
                    COMPONENT_BATCH,
                    static_cast<GSpacing>(plan.readWidth * COMPONENT_BATCH),
                    1, &extra) != CE_None)
            {
                error = CPLGetLastErrorMsg();
                if (error.empty()) error = "AlphaEarth 64D read failed";
                else error = "AlphaEarth 64D read failed: " + error;
                return false;
            }
            for (std::size_t cell = 0; cell < cellCount; ++cell)
            {
                for (int band = 0; band < COMPONENT_BATCH; ++band)
                {
                    raw[cell * COMPONENT_COUNT +
                        static_cast<std::size_t>(first + band)] =
                        batch[cell * COMPONENT_BATCH +
                              static_cast<std::size_t>(band)];
                }
            }
            if (cancelled(callbacks))
                return fail(error, "Cancelled");
            if (callbacks.progress)
            {
                ScienceProgress progress;
                progress.stage = ScienceProgressStage::Reading;
                progress.completedUnits =
                    static_cast<std::uint64_t>(first + COMPONENT_BATCH);
                progress.totalUnits = COMPONENT_COUNT;
                progress.determinate = true;
                progress.unit = "dimensions";
                callbacks.progress(progress,
                    "Reading AlphaEarth " + std::to_string(year) +
                    " dimensions " +
                    std::to_string(first + COMPONENT_BATCH) + "/64");
            }
        }

        std::vector<float> values(cellCount * COMPONENT_COUNT);
        std::vector<unsigned char> mask(cellCount, 1);
        std::vector<float> norms(cellCount,
            std::numeric_limits<float>::quiet_NaN());
        std::uint64_t validCount = 0;
        for (std::size_t cell = 0; cell < cellCount; ++cell)
        {
            double normSquared = 0.0;
            bool vectorValid = true;
            for (std::size_t component = 0;
                 component < COMPONENT_COUNT; ++component)
            {
                bool componentValid = false;
                const double value = dequantizeAlphaEarth(
                    raw[cell * COMPONENT_COUNT + component], componentValid);
                values[cell * COMPONENT_COUNT + component] =
                    static_cast<float>(value);
                vectorValid = vectorValid && componentValid;
                if (componentValid) normSquared += value * value;
            }
            if (!vectorValid || !std::isfinite(normSquared) ||
                normSquared <= 0.0)
            {
                mask[cell] = 0;
                continue;
            }
            norms[cell] = static_cast<float>(std::sqrt(normSquared));
            ++validCount;
        }

        ScienceEmbeddingPayload payload;
        payload.years = std::make_shared<const std::vector<int>>(
            std::initializer_list<int>{year});
        payload.width = plan.readWidth;
        payload.height = plan.readHeight;
        payload.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        payload.mask = std::make_shared<const std::vector<unsigned char>>(
            std::move(mask));
        payload.bounds = plan.bounds;
        payload.groundGrid = plan.groundGrid;
        payload.actualResolutionMeters = plan.actualResolutionMeters;
        payload.processingSteps =
            std::make_shared<const std::vector<std::string>>(
                std::initializer_list<std::string>{
                    "nearest-neighbour sampling on a shared grid",
                    "official signed AlphaEarth dequantization",
                    "complete-vector NoData mask",
                    "64D Euclidean norm calculation"});
        payload.validCellCount = validCount;
        payload.noDataCellCount = cellCount - validCount;
        payload.coverageFraction = cellCount == 0 ? 0.0 :
            static_cast<double>(validCount) / static_cast<double>(cellCount);
        payload.norms = std::make_shared<const std::vector<float>>(
            std::move(norms));

        if (query.aggregation == ScienceAggregation::Mean)
        {
            std::array<float, COMPONENT_COUNT> meanDirection{};
            double concentration = 0.0;
            std::string aggregationError;
            if (!aggregateEmbeddingVectors(
                    payload.values->data(), payload.mask->data(), cellCount,
                    meanDirection, concentration, aggregationError))
                return fail(error, aggregationError);
            std::vector<float> aggregatedValues(meanDirection.begin(),
                                                meanDirection.end());
            std::vector<float> aggregatedNorms(1, 1.0f);
            ScienceGroundGrid meanGrid;
            meanGrid.columns = meanGrid.rows = 1;
            meanGrid.points.push_back({
                (plan.bounds.west + plan.bounds.east) * 0.5,
                (plan.bounds.south + plan.bounds.north) * 0.5});
            payload.width = payload.height = 1;
            payload.values = std::make_shared<const std::vector<float>>(
                std::move(aggregatedValues));
            payload.mask =
                std::make_shared<const std::vector<unsigned char>>(
                    std::initializer_list<unsigned char>{1});
            payload.norms = std::make_shared<const std::vector<float>>(
                std::move(aggregatedNorms));
            payload.groundGrid = std::make_shared<const ScienceGroundGrid>(
                std::move(meanGrid));
            payload.validCellCount = 1;
            payload.noDataCellCount = 0;
            payload.coverageFraction = 1.0;
            auto steps = std::make_shared<std::vector<std::string>>(
                *payload.processingSteps);
            steps->push_back(
                "explicit dequantized 64D mean-direction aggregation; "
                "concentration=" + std::to_string(concentration));
            payload.processingSteps = steps;
        }
        output = std::make_shared<const ScienceEmbeddingPayload>(
            std::move(payload));
        return true;
    }

    std::shared_ptr<const ScienceEmbeddingPayload> combineSlices(
        const std::vector<std::shared_ptr<const ScienceEmbeddingPayload>>& slices,
        std::string& error)
    {
        if (slices.empty() || !slices.front())
        {
            error = "AlphaEarth has no decoded year slices";
            return nullptr;
        }
        const ScienceEmbeddingPayload& first = *slices.front();
        const std::size_t cellCount = static_cast<std::size_t>(first.width) *
            static_cast<std::size_t>(first.height);
        std::vector<int> years;
        std::vector<float> values;
        std::vector<unsigned char> mask;
        std::vector<float> norms;
        years.reserve(slices.size());
        values.reserve(slices.size() * cellCount * COMPONENT_COUNT);
        mask.reserve(slices.size() * cellCount);
        norms.reserve(slices.size() * cellCount);
        std::uint64_t validCount = 0, noDataCount = 0;
        for (const auto& slice : slices)
        {
            if (!slice || !slice->years || slice->years->size() != 1 ||
                slice->width != first.width || slice->height != first.height ||
                slice->bounds.west != first.bounds.west ||
                slice->bounds.south != first.bounds.south ||
                slice->bounds.east != first.bounds.east ||
                slice->bounds.north != first.bounds.north ||
                !slice->values || !slice->mask || !slice->norms)
            {
                error = "AlphaEarth year slices do not share one exact grid";
                return nullptr;
            }
            years.push_back(slice->years->front());
            values.insert(values.end(), slice->values->begin(),
                          slice->values->end());
            mask.insert(mask.end(), slice->mask->begin(), slice->mask->end());
            norms.insert(norms.end(), slice->norms->begin(),
                         slice->norms->end());
            validCount += slice->validCellCount;
            noDataCount += slice->noDataCellCount;
        }
        ScienceEmbeddingPayload combined = first;
        combined.years = std::make_shared<const std::vector<int>>(
            std::move(years));
        combined.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        combined.mask = std::make_shared<const std::vector<unsigned char>>(
            std::move(mask));
        combined.norms = std::make_shared<const std::vector<float>>(
            std::move(norms));
        combined.validCellCount = validCount;
        combined.noDataCellCount = noDataCount;
        const std::uint64_t total = validCount + noDataCount;
        combined.coverageFraction = total == 0 ? 0.0 :
            static_cast<double>(validCount) / static_cast<double>(total);
        return std::make_shared<const ScienceEmbeddingPayload>(
            std::move(combined));
    }

    bool validateQuery(const GeoTemporalQuery& query,
                       std::vector<int>& years, std::string& error)
    {
        if (!query.sourceId.empty() &&
            query.sourceId != "alphaearth-foundations")
            return fail(error, "AlphaEarth runtime received another source id");
        if (query.time.mode != ScienceTimeMode::ExplicitYears ||
            query.time.explicitYears.empty())
            return fail(error,
                        "AlphaEarth runtime requires explicit years");
        years = query.time.explicitYears;
        std::sort(years.begin(), years.end());
        if (years.size() > MAX_YEARS ||
            std::adjacent_find(years.begin(), years.end()) != years.end())
            return fail(error,
                        "AlphaEarth runtime requires at most nine unique years");
        if (query.aggregation != ScienceAggregation::None &&
            query.aggregation != ScienceAggregation::Mean)
            return fail(error,
                        "AlphaEarth runtime supports only explicit mean aggregation");
        if (query.geometry.kind == ScienceGeometryKind::Point &&
            query.aggregation == ScienceAggregation::Mean)
            return fail(error, "AlphaEarth point mean aggregation is redundant");
        if (query.analysis.kind == ScienceAnalysisKind::RegionalChange &&
            query.geometry.kind == ScienceGeometryKind::Point)
            return fail(error,
                        "regional change requires a bounding-box grid");
        if (query.analysis.kind == ScienceAnalysisKind::PointSeries &&
            query.geometry.kind != ScienceGeometryKind::Point)
            return fail(error, "point series requires point geometry");
        return true;
    }

    std::shared_ptr<const ScienceGroundGrid> makeRegularGroundGrid(
        const ScienceWgs84Bounds& bounds, int width, int height)
    {
        ScienceGroundGrid grid;
        grid.columns = width;
        grid.rows = height;
        grid.points.reserve(static_cast<std::size_t>(width) * height);
        const double longitudeStep = (bounds.east - bounds.west) / width;
        const double latitudeStep = (bounds.north - bounds.south) / height;
        for (int row = 0; row < height; ++row)
            for (int column = 0; column < width; ++column)
                grid.points.push_back({
                    bounds.west + (column + 0.5) * longitudeStep,
                    bounds.north - (row + 0.5) * latitudeStep});
        return std::make_shared<const ScienceGroundGrid>(std::move(grid));
    }

    bool makeMosaicBudgetPlan(const GeoTemporalQuery& query,
                              ReadPlan& plan, std::string& error)
    {
        if (!validIndexedBounds(query.geometry.bounds))
            return fail(error, "AlphaEarth mosaic query bounds are invalid");
        constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
        constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
        constexpr double SOURCE_RESOLUTION_METERS = 10.0;
        const ScienceWgs84Bounds& bounds = query.geometry.bounds;
        const double middleLatitude = (bounds.south + bounds.north) * 0.5;
        const double widthMeters = (bounds.east - bounds.west) *
            METERS_PER_LONGITUDE_DEGREE *
            std::max(0.01, std::cos(middleLatitude *
                3.14159265358979323846 / 180.0));
        const double heightMeters = (bounds.north - bounds.south) *
            METERS_PER_LATITUDE_DEGREE;
        if (!std::isfinite(widthMeters) || !std::isfinite(heightMeters) ||
            widthMeters <= 0.0 || heightMeters <= 0.0)
            return fail(error, "AlphaEarth mosaic source estimate is invalid");
        plan.width = std::max(1, static_cast<int>(
            std::ceil(widthMeters / SOURCE_RESOLUTION_METERS)) + 2);
        plan.height = std::max(1, static_cast<int>(
            std::ceil(heightMeters / SOURCE_RESOLUTION_METERS)) + 2);
        plan.readWidth = plan.readHeight = std::clamp(
            query.analysis.gridSize, 1, MAX_GRID_SIZE);
        if (!query.limits.allowUpsampling &&
            (plan.readWidth > plan.width || plan.readHeight > plan.height))
            return fail(error,
                        "AlphaEarth grid upsampling requires explicit permission");
        plan.bounds = bounds;
        plan.groundGrid = makeRegularGroundGrid(
            bounds, plan.readWidth, plan.readHeight);
        plan.actualResolutionMeters = approximateResolutionMeters(
            bounds, plan.readWidth, plan.readHeight);
        return true;
    }

    bool validMosaicAsset(const AlphaEarthAsset& asset, bool localOnly,
                          std::string& error)
    {
        if (asset.datasetId.empty() || asset.pathOrUrl.empty() ||
            asset.sourceVersion.empty() ||
            !validIndexedBounds(asset.indexedBounds))
            return fail(error,
                        "AlphaEarth mosaic resolver returned incomplete metadata");
        if (!localOnly)
            return asset.pathOrUrl.rfind(SOURCE_PREFIX, 0) == 0 ? true :
                fail(error,
                    "AlphaEarth production mosaic requires trusted "
                    "source.coop asset URLs");
        const std::filesystem::path localPath(asset.pathOrUrl);
        std::error_code filesystemError;
        if (asset.pathOrUrl.find("://") != std::string::npos ||
            asset.pathOrUrl.rfind("/vsi", 0) == 0 ||
            !localPath.is_absolute() ||
            !std::filesystem::is_regular_file(localPath, filesystemError))
            return fail(error,
                        "AlphaEarth injected mosaic resolver requires "
                        "absolute regular local files");
        return true;
    }

    bool readMosaicYear(
        const GeoTemporalQuery& query,
        const AlphaEarthAssetSetResolver& resolver,
        bool injectedLocalResolver,
        const ReadPlan& plan,
        int year,
        const AlphaEarthReadCallbacks& callbacks,
        std::vector<AlphaEarthAsset>& assets,
        std::shared_ptr<const ScienceEmbeddingPayload>& output,
        std::string& error)
    {
        assets.clear();
        if (!resolver(query.geometry.bounds, year, assets, error)) return false;
        for (const AlphaEarthAsset& asset : assets)
            if (!validMosaicAsset(asset, injectedLocalResolver, error))
                return false;
        if (!alphaEarthAssetsCoverBounds(assets, query.geometry.bounds))
            return fail(error,
                        "AlphaEarth indexed tiles do not fully cover the "
                        "requested geometry");
        std::vector<int> bands(COMPONENT_COUNT);
        std::vector<std::string> descriptions(COMPONENT_COUNT);
        for (std::size_t component = 0; component < COMPONENT_COUNT;
             ++component)
        {
            bands[component] = static_cast<int>(component + 1);
            char name[4] = {};
            std::snprintf(name, sizeof(name), "A%02d",
                          static_cast<int>(component));
            descriptions[component] = name;
        }
        AlphaEarthMosaicRaster mosaic;
        if (!readAlphaEarthMosaic(
                assets, query.geometry.bounds, plan.readWidth,
                plan.readHeight, bands, descriptions, callbacks.cancelled,
                mosaic, error))
            return false;
        const std::size_t cellCount = static_cast<std::size_t>(
            plan.readWidth) * plan.readHeight;
        std::vector<float> values(cellCount * COMPONENT_COUNT);
        std::vector<unsigned char> mask(cellCount, 1);
        std::vector<float> norms(
            cellCount, std::numeric_limits<float>::quiet_NaN());
        std::uint64_t validCount = 0;
        std::uint64_t missingFootprintCells = 0;
        for (std::size_t cell = 0; cell < cellCount; ++cell)
        {
            double normSquared = 0.0;
            bool vectorValid = true;
            bool anyComponentPresent = false;
            for (std::size_t component = 0;
                 component < COMPONENT_COUNT; ++component)
            {
                const std::size_t index = cell * COMPONENT_COUNT + component;
                bool componentValid = mosaic.masks[index] != 0;
                anyComponentPresent = anyComponentPresent || componentValid;
                const double value = dequantizeAlphaEarth(
                    mosaic.values[index], componentValid);
                values[index] = static_cast<float>(value);
                vectorValid = vectorValid && componentValid;
                if (componentValid) normSquared += value * value;
            }
            if (!anyComponentPresent) ++missingFootprintCells;
            if (!vectorValid || !std::isfinite(normSquared) ||
                normSquared <= 0.0)
            {
                mask[cell] = 0;
                continue;
            }
            norms[cell] = static_cast<float>(std::sqrt(normSquared));
            ++validCount;
        }
        if (missingFootprintCells != 0)
            return fail(error,
                        "AlphaEarth indexed mosaic has incomplete geometric "
                        "coverage");
        if (validCount == 0)
            return fail(error,
                        "AlphaEarth mosaic contains no valid 64D cells");
        ScienceEmbeddingPayload payload;
        payload.years = std::make_shared<const std::vector<int>>(
            std::initializer_list<int>{year});
        payload.width = plan.readWidth;
        payload.height = plan.readHeight;
        payload.values = std::make_shared<const std::vector<float>>(
            std::move(values));
        payload.mask = std::make_shared<const std::vector<unsigned char>>(
            std::move(mask));
        payload.bounds = query.geometry.bounds;
        payload.groundGrid = plan.groundGrid;
        payload.actualResolutionMeters = plan.actualResolutionMeters;
        payload.processingSteps =
            std::make_shared<const std::vector<std::string>>(
                std::initializer_list<std::string>{
                    "indexed multi-tile nearest-neighbour WGS84 mosaic",
                    "official signed AlphaEarth dequantization",
                    "complete-vector NoData mask",
                    "64D Euclidean norm calculation"});
        payload.validCellCount = validCount;
        payload.noDataCellCount = cellCount - validCount;
        payload.coverageFraction =
            static_cast<double>(validCount) / cellCount;
        payload.norms = std::make_shared<const std::vector<float>>(
            std::move(norms));
        output = std::make_shared<const ScienceEmbeddingPayload>(
            std::move(payload));
        return true;
    }
}

bool resolveAlphaEarthAssetFromIndex(
    const std::string& indexPath, double latitude, double longitude,
    int year, AlphaEarthAsset& asset, std::string& error)
{
    asset = AlphaEarthAsset();
    error.clear();
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

    const char* metadataQuery =
        "SELECT asset_base_url FROM metadata LIMIT 2";
    sqlite3_stmt* rawMetadataStatement = nullptr;
    if (sqlite3_prepare_v2(database.get(), metadataQuery, -1,
                           &rawMetadataStatement, nullptr) != SQLITE_OK)
        return fail(error, "AlphaEarth index metadata fault: " +
                          std::string(sqlite3_errmsg(database.get())));
    StatementPtr metadataStatement(rawMetadataStatement);
    int metadataStep = sqlite3_step(metadataStatement.get());
    if (metadataStep != SQLITE_ROW)
        return fail(error,
                    "AlphaEarth index metadata fault: expected exactly one "
                    "metadata record");
    const std::string baseUrl = sqliteText(metadataStatement.get(), 0);
    metadataStep = sqlite3_step(metadataStatement.get());
    if (metadataStep != SQLITE_DONE)
        return fail(error,
                    "AlphaEarth index metadata fault: expected exactly one "
                    "metadata record");
    if (baseUrl.rfind(SOURCE_PREFIX, 0) != 0)
        return fail(error,
                    "AlphaEarth index metadata fault: trusted source.coop "
                    "asset prefix is required");

    const char* query =
        "SELECT t.dataset_id,t.cog_path,t.source_version,"
        "t.min_lon,t.min_lat,t.max_lon,t.max_lat "
        "FROM tile_rtree r JOIN tiles t ON t.id=r.id "
        "WHERE t.year=?1 AND r.min_lon<=?2 AND r.max_lon>=?2 "
        "AND r.min_lat<=?3 AND r.max_lat>=?3 "
        "ORDER BY t.id LIMIT 1";
    sqlite3_stmt* rawStatement = nullptr;
    if (sqlite3_prepare_v2(database.get(), query, -1, &rawStatement,
                           nullptr) != SQLITE_OK)
        return fail(error, sqlite3_errmsg(database.get()));
    StatementPtr statement(rawStatement);
    sqlite3_bind_int(statement.get(), 1, year);
    sqlite3_bind_double(statement.get(), 2, longitude);
    sqlite3_bind_double(statement.get(), 3, latitude);
    const int step = sqlite3_step(statement.get());
    if (step == SQLITE_DONE)
        return fail(error,
                    "No AlphaEarth tile covers this point and year");
    if (step != SQLITE_ROW)
        return fail(error, sqlite3_errmsg(database.get()));

    asset.datasetId = sqliteText(statement.get(), 0);
    const std::string relativePath = sqliteText(statement.get(), 1);
    asset.sourceVersion = sqliteText(statement.get(), 2);
    asset.indexedBounds = {
        sqlite3_column_double(statement.get(), 3),
        sqlite3_column_double(statement.get(), 4),
        sqlite3_column_double(statement.get(), 5),
        sqlite3_column_double(statement.get(), 6)};
    if (asset.datasetId.empty() || relativePath.empty() ||
        asset.sourceVersion.empty())
        return fail(error,
                    "AlphaEarth index tile metadata is incomplete");
    asset.pathOrUrl = baseUrl;
    if (!asset.pathOrUrl.empty() && asset.pathOrUrl.back() != '/' &&
        relativePath.front() != '/')
        asset.pathOrUrl.push_back('/');
    else if (!asset.pathOrUrl.empty() && asset.pathOrUrl.back() == '/' &&
             relativePath.front() == '/')
        asset.pathOrUrl.pop_back();
    asset.pathOrUrl += relativePath;
    return true;
}

bool resolveAlphaEarthAssetsFromIndex(
    const std::string& indexPath, const ScienceWgs84Bounds& bounds,
    int year, std::vector<AlphaEarthAsset>& assets, std::string& error)
{
    assets.clear();
    error.clear();
    if (!validIndexedBounds(bounds))
        return fail(error, "AlphaEarth requested mosaic bounds are invalid");
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
        "FROM tile_rtree r JOIN tiles t ON t.id=r.id CROSS JOIN metadata m "
        "WHERE t.year=?1 AND r.min_lon<?2 AND r.max_lon>?3 "
        "AND r.min_lat<?4 AND r.max_lat>?5 ORDER BY t.id";
    sqlite3_stmt* rawStatement = nullptr;
    if (sqlite3_prepare_v2(database.get(), query, -1, &rawStatement,
                           nullptr) != SQLITE_OK)
        return fail(error, sqlite3_errmsg(database.get()));
    StatementPtr statement(rawStatement);
    sqlite3_bind_int(statement.get(), 1, year);
    sqlite3_bind_double(statement.get(), 2, bounds.east);
    sqlite3_bind_double(statement.get(), 3, bounds.west);
    sqlite3_bind_double(statement.get(), 4, bounds.north);
    sqlite3_bind_double(statement.get(), 5, bounds.south);
    for (;;)
    {
        const int step = sqlite3_step(statement.get());
        if (step == SQLITE_DONE) break;
        if (step != SQLITE_ROW)
            return fail(error, sqlite3_errmsg(database.get()));
        const std::string baseUrl = sqliteText(statement.get(), 7);
        if (baseUrl.rfind(SOURCE_PREFIX, 0) != 0)
            return fail(error,
                        "AlphaEarth index metadata fault: trusted "
                        "source.coop asset prefix is required");
        AlphaEarthAsset asset;
        asset.datasetId = sqliteText(statement.get(), 0);
        const std::string relativePath = sqliteText(statement.get(), 1);
        asset.sourceVersion = sqliteText(statement.get(), 2);
        asset.indexedBounds = {
            sqlite3_column_double(statement.get(), 3),
            sqlite3_column_double(statement.get(), 4),
            sqlite3_column_double(statement.get(), 5),
            sqlite3_column_double(statement.get(), 6)};
        if (asset.datasetId.empty() || relativePath.empty() ||
            asset.sourceVersion.empty() ||
            !validIndexedBounds(asset.indexedBounds))
            return fail(error,
                        "AlphaEarth index mosaic metadata is incomplete");
        asset.pathOrUrl = baseUrl;
        if (asset.pathOrUrl.back() != '/' && relativePath.front() != '/')
            asset.pathOrUrl.push_back('/');
        else if (asset.pathOrUrl.back() == '/' &&
                 relativePath.front() == '/')
            asset.pathOrUrl.pop_back();
        asset.pathOrUrl += relativePath;
        assets.push_back(std::move(asset));
    }
    if (assets.empty())
        return fail(error,
                    "No AlphaEarth tiles intersect this region and year");
    if (!alphaEarthAssetsCoverBounds(assets, bounds))
        return fail(error,
                    "AlphaEarth indexed tiles do not fully cover this region "
                    "and year");
    return true;
}

bool readAlphaEarthArtifact(
    const GeoTemporalQuery& query,
    const AlphaEarthAssetResolver& resolver,
    const AlphaEarthAssetSetResolver& assetSetResolver,
    bool injectedLocalResolver,
    std::uint64_t generation,
    const AlphaEarthReadCallbacks& callbacks,
    std::shared_ptr<const ScienceArtifact>& artifact,
    std::string& error)
{
    artifact.reset();
    error.clear();
    if (!resolver && !assetSetResolver)
        return fail(error, "AlphaEarth asset resolver is missing");
    std::vector<int> years;
    if (!validateQuery(query, years, error)) return false;
    if (cancelled(callbacks)) return fail(error, "Cancelled");

    if (callbacks.progress)
    {
        ScienceProgress progress;
        progress.stage = ScienceProgressStage::Locating;
        callbacks.progress(progress, "Locating AlphaEarth source metadata");
    }
    const bool mosaicMode =
        query.geometry.kind != ScienceGeometryKind::Point &&
        static_cast<bool>(assetSetResolver);
    if (!mosaicMode && !resolver)
        return fail(error,
                    "AlphaEarth point query requires a point asset resolver");
    if (mosaicMode && query.aggregation == ScienceAggregation::Mean)
        return fail(error,
                    "AlphaEarth multi-tile mean aggregation is not supported");
    PreparedDataset firstPrepared;
    ReadPlan referencePlan;
    if (mosaicMode)
    {
        if (!makeMosaicBudgetPlan(query, referencePlan, error) ||
            !enforceBudget(query, referencePlan, years.size(), error))
            return false;
    }
    else
    {
        if (!resolveAndPrepare(query, resolver, injectedLocalResolver,
                               years.front(), years.size(), firstPrepared,
                               error))
            return false;
        if (!enforceBudget(query, firstPrepared.plan, years.size(), error))
            return false;
        referencePlan = firstPrepared.plan;
    }

    std::vector<ScienceSourceReference> sourceReferences;
    std::vector<std::shared_ptr<const ScienceEmbeddingPayload>> retained;
    std::shared_ptr<const ScienceAnalysisPayload> analysis;
    bool firstAvailable = true;

    auto loadYear = [&](int year, std::string& loadError)
        -> std::shared_ptr<const ScienceEmbeddingPayload>
    {
        if (cancelled(callbacks))
        {
            loadError = "Cancelled";
            return nullptr;
        }
        if (mosaicMode)
        {
            if (callbacks.progress)
            {
                ScienceProgress progress;
                progress.stage = ScienceProgressStage::Reading;
                callbacks.progress(progress,
                    "Mosaicking AlphaEarth " + std::to_string(year) +
                    " indexed tiles");
            }
            std::vector<AlphaEarthAsset> assets;
            std::shared_ptr<const ScienceEmbeddingPayload> slice;
            if (!readMosaicYear(
                    query, assetSetResolver, injectedLocalResolver,
                    referencePlan, year, callbacks, assets, slice,
                    loadError))
                return nullptr;
            for (const AlphaEarthAsset& asset : assets)
            {
                ReadPlan tilePlan = referencePlan;
                tilePlan.bounds = {
                    std::max(query.geometry.bounds.west,
                             asset.indexedBounds.west),
                    std::max(query.geometry.bounds.south,
                             asset.indexedBounds.south),
                    std::min(query.geometry.bounds.east,
                             asset.indexedBounds.east),
                    std::min(query.geometry.bounds.north,
                             asset.indexedBounds.north)};
                sourceReferences.push_back(makeSourceReference(
                    asset, query, tilePlan, year));
            }
            return slice;
        }
        PreparedDataset prepared;
        if (firstAvailable && year == years.front())
        {
            prepared = std::move(firstPrepared);
            firstAvailable = false;
        }
        else if (!resolveAndPrepare(query, resolver, injectedLocalResolver,
                                    year, 0, prepared, loadError))
            return nullptr;
        if (!sameSamplingPlan(referencePlan, prepared.plan))
        {
            loadError =
                "AlphaEarth yearly sources do not share exact sample positions";
            return nullptr;
        }
        std::shared_ptr<const ScienceEmbeddingPayload> slice;
        if (!readPreparedYear(prepared, query, year, callbacks, slice,
                              loadError))
            return nullptr;
        sourceReferences.push_back(makeSourceReference(
            prepared.asset, query, prepared.plan, year));
        return slice;
    };

    if (query.analysis.kind == ScienceAnalysisKind::RegionalChange)
    {
        if (query.aggregation != ScienceAggregation::None)
            return fail(error,
                        "regional change requires unaggregated cell vectors");
        const int baselineYear = query.analysis.baselineYear == 0
            ? years.front() : query.analysis.baselineYear;
        const int comparisonYear = query.analysis.comparisonYear == 0
            ? years.back() : query.analysis.comparisonYear;
        std::shared_ptr<const ScienceEmbeddingPayload> baselineSlice;
        std::shared_ptr<const ScienceEmbeddingPayload> comparisonSlice;
        ScienceAnalysisPayload annual;
        auto streamingLoader = [&](int year, std::string& loadError)
            -> std::shared_ptr<const ScienceEmbeddingPayload>
        {
            auto slice = loadYear(year, loadError);
            if (slice && year == baselineYear) baselineSlice = slice;
            if (slice && year == comparisonYear) comparisonSlice = slice;
            return slice;
        };
        if (!ScienceAnalysisEngine::analyzeRegionalAnnualSummaries(
                years, streamingLoader, annual, callbacks.cancelled, error))
            return false;
        if (!baselineSlice || !comparisonSlice)
            return fail(error,
                        "regional comparison years are absent from the request");
        retained = {baselineSlice};
        if (comparisonYear != baselineYear)
            retained.push_back(comparisonSlice);
        auto embedding = combineSlices(retained, error);
        if (!embedding) return false;
        if (callbacks.progress)
        {
            ScienceProgress progress;
            progress.stage = ScienceProgressStage::Analyzing;
            callbacks.progress(progress,
                               "Analyzing AlphaEarth regional change");
        }
        auto regional = ScienceAnalysisEngine::analyzeRegionalChange(
            *embedding, query.analysis, callbacks.cancelled, error);
        if (!regional) return false;
        ScienceAnalysisPayload merged = *regional;
        merged.annualSeries = annual.annualSeries;
        analysis = std::make_shared<const ScienceAnalysisPayload>(
            std::move(merged));
        retained.clear();
        retained.push_back(std::move(embedding));
    }
    else
    {
        retained.reserve(years.size());
        for (int year : years)
        {
            std::string loadError;
            auto slice = loadYear(year, loadError);
            if (!slice) return fail(error, loadError);
            retained.push_back(std::move(slice));
        }
    }

    if (cancelled(callbacks)) return fail(error, "Cancelled");
    std::shared_ptr<const ScienceEmbeddingPayload> embedding;
    if (query.analysis.kind == ScienceAnalysisKind::RegionalChange)
        embedding = retained.front();
    else
        embedding = combineSlices(retained, error);
    if (!embedding) return false;

    if (query.analysis.kind == ScienceAnalysisKind::PointSeries)
    {
        if (callbacks.progress)
        {
            ScienceProgress progress;
            progress.stage = ScienceProgressStage::Analyzing;
            callbacks.progress(progress,
                               "Analyzing AlphaEarth point series");
        }
        analysis = ScienceAnalysisEngine::analyzePointSeries(
            *embedding, query.analysis, callbacks.cancelled, error);
        if (!analysis) return false;
    }
    else if (query.analysis.kind == ScienceAnalysisKind::PrincipalComponents)
    {
        ScienceAnalysisPayload output;
        output.kind = ScienceAnalysisKind::PrincipalComponents;
        if (!ScienceAnalysisEngine::computeLocalPca(
                *embedding, query.analysis.pcaComponents, output,
                callbacks.cancelled, error))
            return false;
        analysis = std::make_shared<const ScienceAnalysisPayload>(
            std::move(output));
    }
    else if (query.analysis.kind == ScienceAnalysisKind::SphericalClusters)
    {
        ScienceAnalysisPayload output;
        output.kind = ScienceAnalysisKind::SphericalClusters;
        if (!ScienceAnalysisEngine::computeSphericalClusters(
                *embedding, query.analysis.clusterCount, output,
                callbacks.cancelled, error))
            return false;
        analysis = std::make_shared<const ScienceAnalysisPayload>(
            std::move(output));
    }
    else if (!analysis)
    {
        ScienceAnalysisPayload output;
        output.kind = ScienceAnalysisKind::None;
        analysis = std::make_shared<const ScienceAnalysisPayload>(
            std::move(output));
    }

    if (cancelled(callbacks)) return fail(error, "Cancelled");
    auto result = std::make_shared<ScienceArtifact>();
    result->artifactId = "alphaearth-embedding-" +
        std::to_string(generation);
    result->query = query;
    result->generation = generation;
    result->sourceReferences = std::move(sourceReferences);
    result->embedding = *embedding;
    result->analysis = *analysis;
    result->visualizationId = query.visualizationId;
    result->processingVersion = mosaicMode
        ? "ScienceEarth-64D-v2-mosaic"
        : "ScienceEarth-64D-v1";
    if (query.analysis.kind == ScienceAnalysisKind::RegionalChange)
    {
        std::string rasterError;
        result->raster = ScienceAnalysisEngine::materializeChangeRaster(
            result->analysis, result->embedding, rasterError);
        if (!rasterError.empty()) return fail(error, rasterError);
    }
    artifact = std::move(result);
    return true;
}
}
}
