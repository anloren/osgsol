#ifndef OSGSOL_APPLICATIONS_EARTH_EXPLORER_SCIENCE_OVERLAY_H
#define OSGSOL_APPLICATIONS_EARTH_EXPLORER_SCIENCE_OVERLAY_H

#include <string>

namespace earthscience
{
    inline const std::string& ndviTemplate()
    {
        static const std::string value =
            "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/MODIS_Terra_NDVI_8Day/"
            "default/default/GoogleMapsCompatible_Level9/{z}/{y}/{x}.png";
        return value;
    }

    inline const std::string& nightlightsTemplate()
    {
        static const std::string value =
            "https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/VIIRS_Black_Marble/"
            "default/default/GoogleMapsCompatible_Level8/{z}/{y}/{x}.png";
        return value;
    }

    inline int nativeMaxZoom(const std::string& overlayPath)
    {
        if (overlayPath == ndviTemplate()) return 9;
        if (overlayPath == nightlightsTemplate()) return 8;
        if (overlayPath == "gebco") return 8;
        return -1;
    }
}

#endif
