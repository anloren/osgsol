#ifndef MANA_READERWRITER_TERRAINFLOORSTATE_HPP
#define MANA_READERWRITER_TERRAINFLOORSTATE_HPP

#include <cmath>

namespace osgVerse
{
    struct TerrainFloorState
    {
        bool hasSample = false;
        double altitude = 0.0;
    };

    inline double terrainFloorCellThresholdRadians()
    {
        return 0.003 * 3.14159265358979323846 / 180.0;
    }

    inline bool terrainFloorMovedToNewCell(double latitude, double longitude,
                                           double cellLatitude, double cellLongitude)
    {
        return std::fabs(latitude - cellLatitude) + std::fabs(longitude - cellLongitude) >
               terrainFloorCellThresholdRadians();
    }

    inline void updateTerrainFloorSample(TerrainFloorState& state,
                                         bool movedToNewCell,
                                         bool hit,
                                         double altitude)
    {
        if (movedToNewCell)
        {
            state.hasSample = hit;
            state.altitude = hit ? altitude : 0.0;
        }
        else if (hit && (!state.hasSample || altitude > state.altitude))
        {
            state.hasSample = true;
            state.altitude = altitude;
        }
    }
}

#endif
