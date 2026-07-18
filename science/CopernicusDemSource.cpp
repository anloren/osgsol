#include "CopernicusDemSource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <set>
#include <utility>

namespace earthscience
{
namespace
{
    constexpr double PI = 3.14159265358979323846;
    constexpr double METERS_PER_LATITUDE_DEGREE = 110574.0;
    constexpr double METERS_PER_LONGITUDE_DEGREE = 111320.0;
    constexpr std::size_t MAX_CELLS = 16;
    constexpr const char* BUCKET =
        "https://copernicus-dem-30m.s3.amazonaws.com/";

    int wrapLongitudeCell(int longitude)
    {
        int wrapped = (longitude + 180) % 360;
        if (wrapped < 0) wrapped += 360;
        return wrapped - 180;
    }

    std::string cellId(int south, int west)
    {
        char value[64] = {};
        std::snprintf(
            value, sizeof(value),
            "Copernicus_DSM_COG_10_%c%02d_00_%c%03d_00_DEM",
            south < 0 ? 'S' : 'N', std::abs(south),
            west < 0 ? 'W' : 'E', std::abs(west));
        return value;
    }
}

bool deriveCopernicusDemCells(
    const ScienceGeometry& geometry,
    std::vector<CopernicusDemCell>& cells,
    std::string& error)
{
    cells.clear();
    error.clear();
    if (geometry.kind != ScienceGeometryKind::Point)
    {
        error = "Copernicus DEM currently requires point geometry";
        return false;
    }

    const double latitude = geometry.point.latitude;
    const double longitude = geometry.point.longitude;
    const double span = geometry.requestedSpanMeters;
    if (!std::isfinite(latitude) || latitude < -90.0 || latitude > 90.0)
    {
        error = "Copernicus DEM latitude must be finite and inside [-90, 90]";
        return false;
    }
    if (!std::isfinite(longitude) || longitude < -180.0 || longitude > 180.0)
    {
        error = "Copernicus DEM longitude must be finite and inside [-180, 180]";
        return false;
    }
    if (!std::isfinite(span) || span <= 0.0)
    {
        error = "Copernicus DEM span must be finite and positive";
        return false;
    }

    const double latitudeHalfSpan = span * 0.5 / METERS_PER_LATITUDE_DEGREE;
    const double longitudeScale = METERS_PER_LONGITUDE_DEGREE *
        std::max(0.01, std::cos(latitude * PI / 180.0));
    const double longitudeHalfSpan = span * 0.5 / longitudeScale;
    if (!std::isfinite(latitudeHalfSpan) ||
        !std::isfinite(longitudeHalfSpan))
    {
        error = "Copernicus DEM span could not be projected";
        return false;
    }

    const double southLimit = std::max(-90.0, latitude - latitudeHalfSpan);
    const double northLimit = std::min(90.0, latitude + latitudeHalfSpan);
    const double westLimit = longitude - longitudeHalfSpan;
    const double eastLimit = longitude + longitudeHalfSpan;
    const int firstSouth = static_cast<int>(std::floor(southLimit));
    const int lastSouth = static_cast<int>(std::floor(std::nextafter(
        northLimit, -std::numeric_limits<double>::infinity())));
    const int firstWest = static_cast<int>(std::floor(westLimit));
    const int lastWest = static_cast<int>(std::floor(std::nextafter(
        eastLimit, -std::numeric_limits<double>::infinity())));

    std::set<std::pair<int, int>> uniqueCells;
    for (int south = firstSouth; south <= lastSouth; ++south)
    {
        if (south < -90 || south > 89) continue;
        for (int unwrappedWest = firstWest;
             unwrappedWest <= lastWest; ++unwrappedWest)
        {
            uniqueCells.emplace(south, wrapLongitudeCell(unwrappedWest));
            if (uniqueCells.size() > MAX_CELLS)
            {
                cells.clear();
                error = "Copernicus DEM request exceeds the 16 tile limit";
                return false;
            }
        }
    }
    if (uniqueCells.empty())
    {
        error = "Copernicus DEM request has no addressable geocell";
        return false;
    }

    cells.reserve(uniqueCells.size());
    for (const auto& key : uniqueCells)
    {
        CopernicusDemCell cell;
        cell.id = cellId(key.first, key.second);
        cell.url = std::string(BUCKET) + cell.id + '/' + cell.id + ".tif";
        cell.bounds = {
            static_cast<double>(key.second), static_cast<double>(key.first),
            static_cast<double>(key.second + 1),
            static_cast<double>(key.first + 1)};
        cells.push_back(std::move(cell));
    }
    return true;
}
}

