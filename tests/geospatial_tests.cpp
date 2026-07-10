// Deterministic geospatial helper tests. The flight implementation is included
// directly so this pure seam remains independent of the EarthExplorer runtime.
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <modeling/Math.h>
#include "../applications/earth_explorer/geo_bbox.h"
#include "../applications/earth_explorer/flight_math.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)

namespace
{
    const double kEpsilon = 1e-9;

    bool near(double actual, double expected, double tolerance = kEpsilon)
    { return std::fabs(actual - expected) <= tolerance; }

    earthgeo::GeoBBox bbox(double latMin, double lonMin, double latMax, double lonMax)
    {
        earthgeo::GeoBBox value;
        value.latMin = latMin; value.lonMin = lonMin;
        value.latMax = latMax; value.lonMax = lonMax;
        return value;
    }

    void checkTransportBoxes(const earthgeo::GeoBBox& input,
                             const std::vector<earthgeo::GeoBBox>& boxes)
    {
        CHECK(boxes.size() == 1 || boxes.size() == 2);
        double span = 0.0;
        for (std::size_t i = 0; i < boxes.size(); ++i)
        {
            CHECK(boxes[i].latMin >= -85.0 && boxes[i].latMax <= 85.0);
            CHECK(boxes[i].lonMin >= -180.0 && boxes[i].lonMax <= 180.0);
            CHECK(boxes[i].latMin <= boxes[i].latMax);
            CHECK(boxes[i].lonMin < boxes[i].lonMax);
            span += boxes[i].lonMax - boxes[i].lonMin;
        }
        CHECK(near(span, earthgeo::longitudeSpan(input)));
    }

    void testLongitudeHelpers()
    {
        using namespace earthgeo;
        CHECK(near(normalizeLongitude(180.0), -180.0));
        CHECK(near(normalizeLongitude(540.0), -180.0));
        CHECK(near(normalizeLongitude(-181.0), 179.0));
        CHECK(near(unwrapLongitudeNear(-179.0, 179.0), 181.0));
        CHECK(near(unwrapLongitudeNear(179.0, -179.0), -181.0));
        CHECK(near(longitudeSpan(bbox(-10.0, 179.0, 10.0, -179.0)), 2.0));
        std::cout << "[OK] longitude normalization and unwrapping\n";
    }

    void testAntimeridianSplit()
    {
        using namespace earthgeo;
        std::vector<GeoBBox> ordinary =
            splitAntimeridianBBox(bbox(20.0, 110.0, 30.0, 120.0));
        CHECK(ordinary.size() == 1);
        CHECK(near(ordinary[0].lonMin, 110.0) && near(ordinary[0].lonMax, 120.0));
        checkTransportBoxes(bbox(20.0, 110.0, 30.0, 120.0), ordinary);

        GeoBBox eastInput = bbox(-10.0, 174.0, 10.0, 184.0);
        std::vector<GeoBBox> east = splitAntimeridianBBox(eastInput);
        CHECK(east.size() == 2);
        CHECK(near(east[0].lonMin, 174.0) && near(east[0].lonMax, 180.0));
        CHECK(near(east[1].lonMin, -180.0) && near(east[1].lonMax, -176.0));
        checkTransportBoxes(eastInput, east);

        GeoBBox wrappedInput = bbox(-10.0, 179.0, 10.0, -179.0);
        std::vector<GeoBBox> wrapped = splitAntimeridianBBox(wrappedInput);
        CHECK(wrapped.size() == 2);
        CHECK(near(wrapped[0].lonMin, 179.0) && near(wrapped[0].lonMax, 180.0));
        CHECK(near(wrapped[1].lonMin, -180.0) && near(wrapped[1].lonMax, -179.0));
        checkTransportBoxes(wrappedInput, wrapped);

        GeoBBox westInput = bbox(-10.0, -184.0, 10.0, -174.0);
        std::vector<GeoBBox> west = splitAntimeridianBBox(westInput);
        CHECK(west.size() == 2);
        CHECK(near(west[0].lonMin, 176.0) && near(west[0].lonMax, 180.0));
        CHECK(near(west[1].lonMin, -180.0) && near(west[1].lonMax, -174.0));
        checkTransportBoxes(westInput, west);

        GeoBBox globalInput = bbox(-100.0, -180.0, 100.0, 180.0);
        std::vector<GeoBBox> global = splitAntimeridianBBox(globalInput);
        CHECK(global.size() == 1);
        CHECK(near(global[0].latMin, -85.0) && near(global[0].latMax, 85.0));
        CHECK(near(global[0].lonMin, -180.0) && near(global[0].lonMax, 180.0));
        checkTransportBoxes(globalInput, global);

        std::vector<GeoBBox> shiftedGlobal =
            splitAntimeridianBBox(bbox(-85.0, 0.0, 85.0, 360.0));
        CHECK(shiftedGlobal.size() == 1);
        CHECK(near(shiftedGlobal[0].lonMin, -180.0) &&
              near(shiftedGlobal[0].lonMax, 180.0));

        std::vector<GeoBBox> overGlobal =
            splitAntimeridianBBox(bbox(-85.0, -200.0, 85.0, 200.01));
        CHECK(overGlobal.size() == 1);
        CHECK(near(overGlobal[0].lonMin, -180.0) && near(overGlobal[0].lonMax, 180.0));

        std::vector<GeoBBox> eastEdge =
            splitAntimeridianBBox(bbox(-10.0, 170.0, 10.0, 180.0));
        CHECK(eastEdge.size() == 1);
        CHECK(near(eastEdge[0].lonMin, 170.0) && near(eastEdge[0].lonMax, 180.0));
        std::vector<GeoBBox> westEdge =
            splitAntimeridianBBox(bbox(-10.0, -180.0, 10.0, -170.0));
        CHECK(westEdge.size() == 1);
        CHECK(near(westEdge[0].lonMin, -180.0) && near(westEdge[0].lonMax, -170.0));

        std::vector<GeoBBox> shiftedEastEdge =
            splitAntimeridianBBox(bbox(-10.0, -190.0, 10.0, -180.0));
        CHECK(shiftedEastEdge.size() == 1);
        CHECK(near(shiftedEastEdge[0].lonMin, 170.0) &&
              near(shiftedEastEdge[0].lonMax, 180.0));
        std::vector<GeoBBox> shiftedWestEdge =
            splitAntimeridianBBox(bbox(-10.0, 180.0, 10.0, 190.0));
        CHECK(shiftedWestEdge.size() == 1);
        CHECK(near(shiftedWestEdge[0].lonMin, -180.0) &&
              near(shiftedWestEdge[0].lonMax, -170.0));
        std::cout << "[OK] antimeridian transport boxes\n";
    }

    void testInflationAndRefresh()
    {
        using namespace earthgeo;
        GeoBBox wrapped = bbox(-10.0, 179.0, 10.0, -179.0);
        GeoBBox inflatedWrapped = inflateUnwrappedBBox(wrapped, 1.5);
        CHECK(near(inflatedWrapped.latMin, -15.0));
        CHECK(near(inflatedWrapped.latMax, 15.0));
        CHECK(near(inflatedWrapped.lonMin, 178.5));
        CHECK(near(inflatedWrapped.lonMax, 181.5));

        GeoBBox subscribed = bbox(-15.0, 171.5, 15.0, 186.5);
        GeoBBox nearEast = bbox(-10.0, 174.0, 10.0, 184.0);  // center 179
        GeoBBox nearWest = bbox(-10.0, -184.0, 10.0, -174.0); // center -179
        CHECK(!unwrappedBBoxNeedsRefresh(subscribed, nearEast));
        CHECK(!unwrappedBBoxNeedsRefresh(subscribed, nearWest));

        GeoBBox moved = bbox(-10.0, -170.0, 10.0, -160.0);
        CHECK(unwrappedBBoxNeedsRefresh(subscribed, moved));
        GeoBBox wider = bbox(-20.0, 160.0, 20.0, 200.0);
        CHECK(unwrappedBBoxNeedsRefresh(subscribed, wider));
        GeoBBox narrower = bbox(-2.0, 178.0, 2.0, 180.0);
        CHECK(unwrappedBBoxNeedsRefresh(subscribed, narrower));
        std::cout << "[OK] wrapped inflation and refresh decisions\n";
    }

    earthflight::FlightTrack makeFlight(const std::string& icao24,
                                        const std::string& callsign)
    {
        earthflight::FlightTrack flight;
        flight.icao24 = icao24;
        flight.callsign = callsign;
        return flight;
    }

    void testFlightExtrapolation()
    {
        using namespace earthflight;
        const double kEarthRadiusM = 6371000.0;
        FlightTrack flight;
        flight.lon = 179.0; flight.lat = 30.0; flight.altM = 10000.0;
        flight.velMS = 250.0; flight.headingRad = osg::DegreesToRadians(90.0);
        flight.ecef.set(1.0, 2.0, 3.0); // output must be recomputed from returned LLA

        FlightPosition zero = extrapolateFlightPosition(flight, 0.0);
        CHECK(near(zero.lon, 179.0));
        CHECK(near(zero.lat, 30.0));
        osg::Vec3d zeroExpected = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            osg::DegreesToRadians(30.0), osg::DegreesToRadians(179.0), 10000.0));
        CHECK((zero.ecef - zeroExpected).length() < 1e-6);

        const double elapsed = 1000.0;
        const double expectedLon = earthgeo::normalizeLongitude(179.0 + osg::RadiansToDegrees(
            flight.velMS * elapsed / (kEarthRadiusM * std::cos(osg::DegreesToRadians(30.0)))));
        FlightPosition positive = extrapolateFlightPosition(flight, elapsed);
        CHECK(near(positive.lat, 30.0));
        CHECK(near(positive.lon, expectedLon));
        osg::Vec3d positiveExpected = osgVerse::Coordinate::convertLLAtoECEF(osg::Vec3d(
            osg::DegreesToRadians(positive.lat), osg::DegreesToRadians(positive.lon), 10000.0));
        CHECK((positive.ecef - positiveExpected).length() < 1e-6);

        FlightPosition negative = extrapolateFlightPosition(flight, -500.0);
        CHECK(near(negative.lon, zero.lon));
        CHECK(near(negative.lat, zero.lat));
        CHECK((negative.ecef - zero.ecef).length() < 1e-6);
        std::cout << "[OK] flight extrapolation\n";
    }

    void testStableFlightMerge()
    {
        using namespace earthflight;
        std::vector<FlightTrack> first;
        first.push_back(makeFlight("abc123", "FIRST-A"));
        first.push_back(makeFlight("", "EMPTY-1"));
        first.push_back(makeFlight("def456", "FIRST-B"));
        first.push_back(makeFlight("abc123", "DUP-A"));
        std::vector<FlightTrack> second;
        second.push_back(makeFlight("def456", "DUP-B"));
        second.push_back(makeFlight("", "EMPTY-2"));
        second.push_back(makeFlight("ghi789", "FIRST-C"));

        std::vector<FlightTrack> merged = mergeFlightsByIcao24(first, second);
        CHECK(merged.size() == 5);
        CHECK(merged[0].icao24 == "abc123" && merged[0].callsign == "FIRST-A");
        CHECK(merged[1].icao24.empty() && merged[1].callsign == "EMPTY-1");
        CHECK(merged[2].icao24 == "def456" && merged[2].callsign == "FIRST-B");
        CHECK(merged[3].icao24.empty() && merged[3].callsign == "EMPTY-2");
        CHECK(merged[4].icao24 == "ghi789" && merged[4].callsign == "FIRST-C");
        std::cout << "[OK] stable flight merge\n";
    }
}

int main(int, char**)
{
    testLongitudeHelpers();
    testAntimeridianSplit();
    testInflationAndRefresh();
    testFlightExtrapolation();
    testStableFlightMerge();
    std::cout << "[geospatial_tests] all OK\n";
    return 0;
}
