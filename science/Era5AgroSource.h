#ifndef OSGSOL_ERA5_AGRO_SOURCE_H
#define OSGSOL_ERA5_AGRO_SOURCE_H

#include <string>
#include <vector>

#include "ScienceQueryTypes.h"

namespace earthscience
{
    enum class Era5AgroProduct
    {
        LandSurface,
        AgriculturalClimate,
    };

    const char* era5AgroSourceId(Era5AgroProduct product);
    std::vector<std::string> era5AgroVariableIds(Era5AgroProduct product);
    ScienceSourceDescriptor describeEra5Agro(Era5AgroProduct product);

    bool buildEra5AgroRequestUrl(
        Era5AgroProduct product, const GeoTemporalQuery& query,
        std::string& url, std::string& error);

    bool parseEra5AgroAnnualArtifact(
        Era5AgroProduct product, const GeoTemporalQuery& query,
        const std::string& requestUrl, const std::string& json,
        ScienceArtifact& artifact, std::string& error);
}

#endif
