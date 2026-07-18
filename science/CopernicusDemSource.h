#ifndef OSGSOL_COPERNICUS_DEM_SOURCE_H
#define OSGSOL_COPERNICUS_DEM_SOURCE_H

#include <string>
#include <vector>

#include "ScienceQueryTypes.h"

namespace earthscience
{
    struct CopernicusDemCell
    {
        std::string id;
        std::string url;
        ScienceWgs84Bounds bounds;
    };

    bool deriveCopernicusDemCells(
        const ScienceGeometry& geometry,
        std::vector<CopernicusDemCell>& cells,
        std::string& error);
}

#endif

