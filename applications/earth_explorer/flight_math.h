#ifndef EARTH_FLIGHT_MATH_H
#define EARTH_FLIGHT_MATH_H

#include <string>
#include <vector>
#include <osg/Vec3d>

namespace earthflight
{
    struct FlightTrack
    {
        std::string icao24, callsign, country;
        double lon = 0.0, lat = 0.0, altM = 0.0;
        double velMS = 0.0, headingRad = 0.0;
        osg::Vec3d ecef;
    };

    struct FlightPosition
    {
        double lon = 0.0, lat = 0.0;
        osg::Vec3d ecef;
    };

    FlightPosition extrapolateFlightPosition(const FlightTrack& flight,
                                              double elapsedSeconds);
    std::vector<FlightTrack> mergeFlightsByIcao24(
        const std::vector<FlightTrack>& first,
        const std::vector<FlightTrack>& second);
}

#endif
