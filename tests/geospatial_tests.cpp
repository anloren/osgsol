// Deterministic geospatial helper tests. The flight implementation is included
// directly so this pure seam remains independent of the EarthExplorer runtime.
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
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

    std::string readSourceFile(const std::string& relative)
    {
        std::ifstream input(std::string(OSGVERSE_SOURCE_DIR) + "/" + relative);
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    }

    std::string extractFunctionBody(const std::string& source,
                                    const std::string& signature)
    {
        size_t signaturePos = source.find(signature);
        if (signaturePos == std::string::npos) return std::string();
        size_t openingBrace = source.find('{', signaturePos + signature.size());
        if (openingBrace == std::string::npos) return std::string();

        int depth = 1;
        bool lineComment = false, blockComment = false;
        bool stringLiteral = false, charLiteral = false, escaped = false;
        for (size_t i = openingBrace + 1; i < source.size(); ++i)
        {
            char c = source[i];
            char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
            if (lineComment)
            {
                if (c == '\n') lineComment = false;
                continue;
            }
            if (blockComment)
            {
                if (c == '*' && next == '/') { blockComment = false; ++i; }
                continue;
            }
            if (stringLiteral || charLiteral)
            {
                if (escaped) { escaped = false; continue; }
                if (c == '\\') { escaped = true; continue; }
                if ((stringLiteral && c == '"') || (charLiteral && c == '\''))
                { stringLiteral = false; charLiteral = false; }
                continue;
            }
            if (c == '/' && next == '/') { lineComment = true; ++i; continue; }
            if (c == '/' && next == '*') { blockComment = true; ++i; continue; }
            if (c == '"') { stringLiteral = true; continue; }
            if (c == '\'') { charLiteral = true; continue; }
            if (c == '{') ++depth;
            else if (c == '}' && --depth == 0)
                return source.substr(openingBrace + 1, i - openingBrace - 1);
        }
        return std::string();
    }

    std::string normalizeCodeOnly(const std::string& source)
    {
        std::string compact;
        compact.reserve(source.size());
        bool lineComment = false, blockComment = false;
        bool stringLiteral = false, charLiteral = false, escaped = false;
        for (size_t i = 0; i < source.size(); ++i)
        {
            char c = source[i];
            char next = (i + 1 < source.size()) ? source[i + 1] : '\0';
            if (lineComment)
            {
                if (c == '\n') lineComment = false;
                continue;
            }
            if (blockComment)
            {
                if (c == '*' && next == '/') { blockComment = false; ++i; }
                continue;
            }
            if (stringLiteral || charLiteral)
            {
                if (escaped) { escaped = false; continue; }
                if (c == '\\') { escaped = true; continue; }
                if ((stringLiteral && c == '"') || (charLiteral && c == '\''))
                {
                    compact.push_back(c);
                    stringLiteral = false; charLiteral = false;
                }
                continue;
            }
            if (c == '/' && next == '/') { lineComment = true; ++i; continue; }
            if (c == '/' && next == '*') { blockComment = true; ++i; continue; }
            if (std::isspace(static_cast<unsigned char>(c))) continue;
            compact.push_back(c);
            if (c == '"') stringLiteral = true;
            else if (c == '\'') charLiteral = true;
        }
        return compact;
    }

    size_t countOccurrences(const std::string& source, const std::string& needle)
    {
        size_t count = 0, offset = 0;
        while ((offset = source.find(needle, offset)) != std::string::npos)
        {
            ++count;
            offset += needle.size();
        }
        return count;
    }

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

    void testFlightRuntimeSourceWiring()
    {
        const std::string normalizedSnippet = normalizeCodeOnly(
            "liveCall(); // s[0].is<std::string>(); splitAntimeridianBBox(fake);\n"
            "const char* fake = \"extrapolateFlightPosition(flight, elapsed)\"; nextCall();");
        CHECK(normalizedSnippet == "liveCall();constchar*fake=\"\";nextCall();");

        const std::string source =
            readSourceFile("applications/earth_explorer/flight_data.cpp");
        CHECK(!source.empty());
        const std::string parseRaw = extractFunctionBody(source, "parseOpenSky(");
        const std::string fetchRaw = extractFunctionBody(source, "fetchFlights(");
        const std::string interpolateRaw = extractFunctionBody(source, "void interpolate(");
        const std::string pickRaw = extractFunctionBody(source, "void pickAt(");
        const std::string pickHandlerRaw =
            extractFunctionBody(source, "class FlightPickHandler");
        const std::string bboxHandlerRaw =
            extractFunctionBody(source, "class FlightBBoxHandler");
        CHECK(!parseRaw.empty());
        CHECK(!fetchRaw.empty());
        CHECK(!interpolateRaw.empty());
        CHECK(!pickRaw.empty());
        CHECK(!pickHandlerRaw.empty());
        CHECK(!bboxHandlerRaw.empty());

        const std::string code = normalizeCodeOnly(source);
        const std::string parse = normalizeCodeOnly(parseRaw);
        const std::string fetch = normalizeCodeOnly(fetchRaw);
        const std::string interpolate = normalizeCodeOnly(interpolateRaw);
        const std::string pick = normalizeCodeOnly(pickRaw);
        const std::string pickHandler = normalizeCodeOnly(pickHandlerRaw);
        const std::string bboxHandler = normalizeCodeOnly(bboxHandlerRaw);

        CHECK(code.find("structFlight{") == std::string::npos);
        CHECK(parse.find("std::vector<earthflight::FlightTrack>out;") !=
              std::string::npos);
        const std::string identityValidation =
            "if(!s[0].is<std::string>())continue;"
            "conststd::string&icao24=s[0].get<std::string>();"
            "if(icao24.empty())continue;";
        const size_t identityPos = parse.find(identityValidation);
        const size_t trackPos = parse.find("earthflight::FlightTrackf;");
        const size_t assignPos = parse.find("f.icao24=icao24;");
        const size_t pushPos = parse.find("out.push_back(f);");
        CHECK(identityPos != std::string::npos);
        CHECK(trackPos != std::string::npos && identityPos < trackPos);
        CHECK(assignPos != std::string::npos && trackPos < assignPos);
        CHECK(pushPos != std::string::npos && assignPos < pushPos);
        CHECK(countOccurrences(parse, "out.push_back(f);") == 1);

        const std::string fixtureRaw =
            extractFunctionBody(fetchRaw, "if (fixtureFile && *fixtureFile)");
        CHECK(!fixtureRaw.empty());
        const std::string fixture = normalizeCodeOnly(fixtureRaw);
        CHECK(countOccurrences(fixture, "std::ifstreamin(fixtureFile);") == 1);
        CHECK(countOccurrences(fixture, "ss<<in.rdbuf();") == 1);
        CHECK(countOccurrences(fixture, "parseOpenSky(ss.str())") == 1);
        CHECK(fixture.find("earthgeo::splitAntimeridianBBox") == std::string::npos);
        CHECK(countOccurrences(fetch, "parseOpenSky(ss.str())") == 1);

        const std::string bboxFlow =
            "earthgeo::GeoBBoxbbox;"
            "bbox.latMin=latMin;bbox.lonMin=lonMin;"
            "bbox.latMax=latMax;bbox.lonMax=lonMax;";
        const size_t bboxFlowPos = fetch.find(bboxFlow);
        const std::string splitFlow =
            "std::vector<earthgeo::GeoBBox>queryBoxes="
            "earthgeo::splitAntimeridianBBox(bbox);";
        const size_t splitPos = fetch.find(splitFlow);
        const size_t loopPos = fetch.find(
            "for(size_ti=0;i<queryBoxes.size();++i)");
        CHECK(bboxFlowPos != std::string::npos);
        CHECK(splitPos != std::string::npos && bboxFlowPos < splitPos);
        CHECK(loopPos != std::string::npos && splitPos < loopPos);

        const std::string networkLoopRaw = extractFunctionBody(
            fetchRaw, "for (size_t i = 0; i < queryBoxes.size(); ++i)");
        CHECK(!networkLoopRaw.empty());
        const std::string networkLoop = normalizeCodeOnly(networkLoopRaw);
        const size_t queryBoxPos = networkLoop.find(
            "constearthgeo::GeoBBox&queryBox=queryBoxes[i];");
        const size_t urlPos = networkLoop.find(
            "snprintf(url,sizeof(url),\"\",queryBox.latMin,queryBox.lonMin,"
            "queryBox.latMax,queryBox.lonMax);");
        const size_t requestPos = networkLoop.find("req->url=url;");
        const size_t parseResponsePos = networkLoop.find(
            "std::vector<earthflight::FlightTrack>fetched=parseOpenSky(resp->body);");
        const size_t mergePos = networkLoop.find(
            "merged=earthflight::mergeFlightsByIcao24(merged,fetched);");
        CHECK(queryBoxPos != std::string::npos);
        CHECK(urlPos != std::string::npos && queryBoxPos < urlPos);
        CHECK(requestPos != std::string::npos && urlPos < requestPos);
        CHECK(parseResponsePos != std::string::npos && requestPos < parseResponsePos);
        CHECK(mergePos != std::string::npos && parseResponsePos < mergePos);
        CHECK(countOccurrences(networkLoop,
              "earthflight::mergeFlightsByIcao24(merged,fetched)") == 1);

        const std::string elapsedFlow =
            "doubleelapsed=std::max(0.0,refTime-_t0);";
        const std::string extrapolateFlow =
            "constearthflight::FlightTrack&flight=_flights[i];"
            "earthflight::FlightPositionposition="
            "earthflight::extrapolateFlightPosition(flight,elapsed);";
        const size_t interpolateElapsed = interpolate.find(elapsedFlow);
        const size_t interpolatePosition = interpolate.find(extrapolateFlow);
        const size_t interpolateVertex = interpolate.find("(*va)[i]=position.ecef;");
        CHECK(interpolateElapsed != std::string::npos);
        CHECK(interpolatePosition != std::string::npos &&
              interpolateElapsed < interpolatePosition);
        CHECK(interpolateVertex != std::string::npos &&
              interpolatePosition < interpolateVertex);
        CHECK(countOccurrences(interpolate,
              "earthflight::extrapolateFlightPosition(flight,elapsed)") == 1);

        const size_t pickElapsed = pick.find(elapsedFlow);
        const size_t pickPosition = pick.find(extrapolateFlow);
        const size_t pickEcef = pick.find("constosg::Vec3d&P=position.ecef;");
        const size_t pickProjection = pick.find("osg::Vec3dwin=P*VPW;");
        const size_t bestPosition = pick.find("bestPosition=position;");
        const size_t selectedPosition = pick.find(
            "info.lon=bestPosition.lon;info.lat=bestPosition.lat;");
        CHECK(pickElapsed != std::string::npos);
        CHECK(pickPosition != std::string::npos && pickElapsed < pickPosition);
        CHECK(pickEcef != std::string::npos && pickPosition < pickEcef);
        CHECK(pickProjection != std::string::npos && pickEcef < pickProjection);
        CHECK(bestPosition != std::string::npos && pickProjection < bestPosition);
        CHECK(selectedPosition != std::string::npos && bestPosition < selectedPosition);
        CHECK(countOccurrences(pick,
              "earthflight::extrapolateFlightPosition(flight,elapsed)") == 1);
        CHECK(pick.find("flight.ecef") == std::string::npos);
        CHECK(pick.find("_flights[i].ecef") == std::string::npos);
        CHECK(pick.find("info.lon=flight.lon") == std::string::npos);
        CHECK(pick.find("info.lat=flight.lat") == std::string::npos);

        const std::string frameFlow =
            "constosg::FrameStamp*frameStamp=view->getFrameStamp();"
            "doublerefTime=frameStamp?frameStamp->getReferenceTime():0.0;";
        const size_t framePos = pickHandler.find(frameFlow);
        const size_t pickCallPos = pickHandler.find(
            "_owner->pickAt(cam,ea.getX(),my,refTime);");
        CHECK(framePos != std::string::npos);
        CHECK(pickCallPos != std::string::npos && framePos < pickCallPos);

        CHECK(bboxHandler.find(
            "if(thetaDeg>=80.0){_owner->setViewBBox(-85.0,-180.0,85.0,180.0);"
            "returnfalse;}") != std::string::npos);
        CHECK(bboxHandler.find(
            "if(latMin<-85.0)latMin=-85.0;if(latMax>85.0)latMax=85.0;") !=
              std::string::npos);
        CHECK(bboxHandler.find("lonMin=-180.0") == std::string::npos);
        CHECK(bboxHandler.find("lonMax=180.0") == std::string::npos);
        std::cout << "[OK] flight runtime source wiring\n";
    }
}

int main(int, char**)
{
    testLongitudeHelpers();
    testAntimeridianSplit();
    testInflationAndRefresh();
    testFlightExtrapolation();
    testStableFlightMerge();
    testFlightRuntimeSourceWiring();
    std::cout << "[geospatial_tests] all OK\n";
    return 0;
}
