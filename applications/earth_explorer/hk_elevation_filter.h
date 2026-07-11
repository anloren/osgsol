#ifndef OSGSOL_HK_ELEVATION_FILTER_H
#define OSGSOL_HK_ELEVATION_FILTER_H

#include <algorithm>
#include <cmath>
#include <osg/Math>

namespace earthterrain
{
inline void applyHongKongElevationFilter(float* hts, int w, int h,
                                         int x, int y, int z)
{
    if (!hts || w <= 0 || h <= 0) return;

    int tz = z, tx = x, tyTMS = y;
    if (z > 15)
    {
        int dz = z - 15;
        tx = x >> dz;
        tyTMS = y >> dz;
        tz = 15;
    }
    const double n = static_cast<double>(1 << tz);
    const int tyXYZ = static_cast<int>(n) - 1 - tyTMS;
    const double lonMin = tx / n * 360.0 - 180.0;
    const double lonSpan = 360.0 / n;
    const double latN = std::atan(std::sinh(osg::PI * (1.0 - 2.0 * tyXYZ / n))) *
                        180.0 / osg::PI;
    const double latS = std::atan(std::sinh(osg::PI *
                        (1.0 - 2.0 * (tyXYZ + 1) / n))) * 180.0 / osg::PI;
    if (lonMin > 114.37 || lonMin + lonSpan < 113.88 ||
        latS > 22.44 || latN < 22.17)
        return;

    const auto smooth01 = [](double t) {
        t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
        return t * t * (3.0 - 2.0 * t);
    };
    const auto outerW = [&smooth01](double lat, double lon,
                                    double la0, double la1,
                                    double lo0, double lo1, double feather) {
        const double dla = std::max(std::max(la0 - lat, lat - la1), 0.0);
        const double dlo = std::max(std::max(lo0 - lon, lon - lo1), 0.0);
        return smooth01(std::sqrt(dla * dla + dlo * dlo) / feather);
    };

    for (int row = 0; row < h; ++row)
    {
        const double yf = static_cast<double>(tyXYZ) + 1.0 -
                          (static_cast<double>(row) + 0.5) / static_cast<double>(h);
        const double lat = std::atan(std::sinh(osg::PI * (1.0 - 2.0 * yf / n))) *
                           180.0 / osg::PI;
        for (int col = 0; col < w; ++col)
        {
            const double lon = lonMin + (static_cast<double>(col) + 0.5) /
                               static_cast<double>(w) * lonSpan;
            const double wMetro = outerW(lat, lon, 22.19, 22.42,
                                         113.90, 114.35, 0.02);
            if (wMetro >= 1.0) continue;
            const double wFlat = outerW(lat, lon, 22.276, 22.335,
                                        114.115, 114.225, 0.012);
            float& value = hts[row * w + col];
            const double metroHeight = static_cast<double>(value) * 0.5 - 2.0;
            const double innerHeight = wFlat * metroHeight;
            value = static_cast<float>(wMetro * static_cast<double>(value) +
                                       (1.0 - wMetro) * innerHeight);
        }
    }
}
}

#endif
