#ifndef OSGVERSE_3DTILES_PAGING_UTILS_H
#define OSGVERSE_3DTILES_PAGING_UTILS_H

#include <cfloat>
#include <cmath>

namespace osgVerse
{
    namespace Tiles3dPaging
    {
        inline double computeSwitchPixels(double radius, double geometricError, double sse)
        {
            if (!std::isfinite(radius) || !std::isfinite(geometricError) ||
                !std::isfinite(sse) || !(radius > 0.0) || !(geometricError > 0.0) ||
                !(sse > 0.0))
                return 1.0;

            const double result = 2.0 * radius * sse / geometricError;
            if (!std::isfinite(result)) return 1.0;
            if (result < 1.0) return 1.0;
            return result > static_cast<double>(FLT_MAX)
                ? static_cast<double>(FLT_MAX) : result;
        }
    }
}

#endif
