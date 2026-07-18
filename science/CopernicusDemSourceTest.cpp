#include "CopernicusDemSource.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    earthscience::ScienceGeometry point(
        double latitude, double longitude, double spanMeters)
    {
        earthscience::ScienceGeometry geometry;
        geometry.kind = earthscience::ScienceGeometryKind::Point;
        geometry.point = {latitude, longitude};
        geometry.requestedSpanMeters = spanMeters;
        return geometry;
    }

    const earthscience::CopernicusDemCell* findCell(
        const std::vector<earthscience::CopernicusDemCell>& cells,
        const std::string& id)
    {
        for (const auto& cell : cells)
            if (cell.id == id) return &cell;
        return nullptr;
    }

    void testDerivesOfficialTokyoObject()
    {
        std::vector<earthscience::CopernicusDemCell> cells;
        std::string error;
        require(earthscience::deriveCopernicusDemCells(
                    point(35.6895, 139.6917, 20000.0), cells, error),
                "Tokyo DEM cells were rejected");
        require(error.empty() && cells.size() == 1,
                "Tokyo request must fit one bounded geocell");
        const std::string id =
            "Copernicus_DSM_COG_10_N35_00_E139_00_DEM";
        require(cells.front().id == id,
                "Tokyo geocell id does not match the official layout");
        require(cells.front().url ==
                    "https://copernicus-dem-30m.s3.amazonaws.com/" + id +
                    "/" + id + ".tif",
                "Tokyo COG URL is not the allowlisted official object");
        require(cells.front().bounds.west == 139.0 &&
                    cells.front().bounds.south == 35.0 &&
                    cells.front().bounds.east == 140.0 &&
                    cells.front().bounds.north == 36.0,
                "Tokyo geocell bounds changed");
    }

    void testAddressesImportantProjectLocations()
    {
        std::vector<earthscience::CopernicusDemCell> cells;
        std::string error;
        require(earthscience::deriveCopernicusDemCells(
                    point(22.3193, 114.1694, 20000.0), cells, error) &&
                    findCell(cells,
                        "Copernicus_DSM_COG_10_N22_00_E114_00_DEM"),
                "Hong Kong geocell is missing");

        require(earthscience::deriveCopernicusDemCells(
                    point(37.3708, -121.9663, 20000.0), cells, error) &&
                    findCell(cells,
                        "Copernicus_DSM_COG_10_N37_00_W123_00_DEM") &&
                    findCell(cells,
                        "Copernicus_DSM_COG_10_N37_00_W122_00_DEM"),
                "NVIDIA headquarters boundary cells are incomplete");

        require(earthscience::deriveCopernicusDemCells(
                    point(-33.4489, -70.6693, 10000.0), cells, error) &&
                    findCell(cells,
                        "Copernicus_DSM_COG_10_S34_00_W071_00_DEM"),
                "southern/western hemisphere formatting is wrong");
    }

    void testWrapsAntimeridianWithoutUntrustedUrls()
    {
        std::vector<earthscience::CopernicusDemCell> cells;
        std::string error;
        require(earthscience::deriveCopernicusDemCells(
                    point(0.25, 179.999, 10000.0), cells, error),
                "antimeridian request was rejected");
        require(findCell(cells,
                    "Copernicus_DSM_COG_10_N00_00_E179_00_DEM") &&
                    findCell(cells,
                    "Copernicus_DSM_COG_10_N00_00_W180_00_DEM"),
                "antimeridian did not wrap to E179/W180 cells");
        for (const auto& cell : cells)
            require(cell.url.rfind(
                        "https://copernicus-dem-30m.s3.amazonaws.com/", 0) == 0 &&
                        cell.url.find("..") == std::string::npos,
                    "derived DEM URL escaped the allowlisted bucket");
    }

    void testRejectsMalformedAndUnboundedRequests()
    {
        std::vector<earthscience::CopernicusDemCell> cells;
        std::string error;
        require(!earthscience::deriveCopernicusDemCells(
                    point(NAN, 0.0, 1000.0), cells, error) && !error.empty(),
                "non-finite latitude was accepted");
        require(!earthscience::deriveCopernicusDemCells(
                    point(0.0, 181.0, 1000.0), cells, error) && !error.empty(),
                "out-of-range longitude was accepted");
        require(!earthscience::deriveCopernicusDemCells(
                    point(0.0, 0.0, 0.0), cells, error) && !error.empty(),
                "zero span was accepted");

        earthscience::ScienceGeometry bounds = point(0.0, 0.0, 1000.0);
        bounds.kind = earthscience::ScienceGeometryKind::BoundingBox;
        require(!earthscience::deriveCopernicusDemCells(
                    bounds, cells, error) && !error.empty(),
                "unsupported geometry was accepted");

        require(!earthscience::deriveCopernicusDemCells(
                    point(86.0, 0.0, 500000.0), cells, error) &&
                    error.find("tile limit") != std::string::npos,
                "polar request bypassed the hard DEM tile limit");
    }
}

int main()
{
    testDerivesOfficialTokyoObject();
    testAddressesImportantProjectLocations();
    testWrapsAntimeridianWithoutUntrustedUrls();
    testRejectsMalformedAndUnboundedRequests();
    std::cout << "[OK] Copernicus DEM source addressing\n";
    return 0;
}

