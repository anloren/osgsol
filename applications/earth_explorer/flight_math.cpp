#include <algorithm>
#include <cmath>
#include <unordered_set>

#include <modeling/Math.h>
#include "flight_math.h"
#include "geo_bbox.h"

namespace earthflight
{
    FlightPosition extrapolateFlightPosition(const FlightTrack& flight,
                                              double elapsedSeconds)
    {
        const double kEarthRadiusM = 6371000.0;
        double elapsed = std::max(0.0, elapsedSeconds);
        double distance = flight.velMS * elapsed;
        double dLat = distance * std::cos(flight.headingRad) / kEarthRadiusM;
        double dLon = distance * std::sin(flight.headingRad) /
                      (kEarthRadiusM * std::cos(osg::DegreesToRadians(flight.lat)));

        FlightPosition position;
        position.lat = flight.lat + osg::RadiansToDegrees(dLat);
        position.lon = earthgeo::normalizeLongitude(
            flight.lon + osg::RadiansToDegrees(dLon));
        position.ecef = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            osg::DegreesToRadians(position.lat), osg::DegreesToRadians(position.lon),
            flight.altM));
        return position;
    }

    std::vector<FlightTrack> mergeFlightsByIcao24(
        const std::vector<FlightTrack>& first,
        const std::vector<FlightTrack>& second)
    {
        std::vector<FlightTrack> merged;
        merged.reserve(first.size() + second.size());
        std::unordered_set<std::string> seen;

        const std::vector<FlightTrack>* inputs[] = { &first, &second };
        for (int input = 0; input < 2; ++input)
        {
            for (std::size_t i = 0; i < inputs[input]->size(); ++i)
            {
                const FlightTrack& flight = (*inputs[input])[i];
                if (!flight.icao24.empty() && !seen.insert(flight.icao24).second)
                    continue;
                merged.push_back(flight);
            }
        }
        return merged;
    }
}
