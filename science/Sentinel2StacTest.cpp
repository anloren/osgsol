#include "Sentinel2Stac.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    std::string feature(
        const std::string& id, const std::string& datetime, double cloud,
        const std::string& href =
            "https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
            "sentinel-s2-l2a-cogs/54/S/UE/2026/7/scene/TCI.tif",
        const std::string& mediaType =
            "image/tiff; application=geotiff; profile=cloud-optimized",
        const std::string& bbox = "[138.7,35.1,140.1,36.2]")
    {
        return "{\"type\":\"Feature\",\"id\":\"" + id +
            "\",\"bbox\":" + bbox +
            ",\"properties\":{\"datetime\":\"" + datetime +
            "\",\"eo:cloud_cover\":" + std::to_string(cloud) +
            "},\"assets\":{\"visual\":{\"href\":\"" + href +
            "\",\"type\":\"" + mediaType +
            "\",\"roles\":[\"visual\"],\"gsd\":10}}}";
    }

    std::string collection(const std::vector<std::string>& features)
    {
        std::string json = "{\"type\":\"FeatureCollection\",\"features\":[";
        for (std::size_t index = 0; index < features.size(); ++index)
        {
            if (index != 0) json += ',';
            json += features[index];
        }
        return json + "]}";
    }

    void requireRejected(const std::string& json, const char* message)
    {
        std::vector<earthscience::Sentinel2Scene> scenes;
        std::string error;
        require(!earthscience::parseSentinel2Items(json, scenes, error) &&
                    !error.empty() && scenes.empty(), message);
    }

    void testSearchUrlIsBoundedAndEncoded()
    {
        std::string url, error;
        require(earthscience::buildSentinel2SearchUrl(
                    {139.60, 35.50, 139.95, 35.85},
                    "2026-06-18T00:00:00Z", "2026-07-18T23:59:59Z",
                    20.0, 10, url, error),
                "valid Sentinel-2 search URL was rejected");
        require(error.empty(), "valid search URL returned an error");
        require(url.find("https://earth-search.aws.element84.com/v1/search?") == 0,
                "search URL used an unexpected endpoint");
        require(url.find("collections=sentinel-2-l2a") != std::string::npos,
                "search URL lost the collection");
        require(url.find("datetime=2026-06-18T00%3A00%3A00Z%2F"
                         "2026-07-18T23%3A59%3A59Z") != std::string::npos,
                "search interval was not percent encoded");
        require(url.find("limit=10") != std::string::npos,
                "search URL lost the scene bound");
        require(url.find(
                    "query=%7B%22eo%3Acloud_cover%22%3A%7B%22lte%22%3A20%7D%7D") !=
                    std::string::npos,
                "search URL did not push down the cloud threshold");
        require(url.find(
                    "sortby=%2Bproperties.eo%3Acloud_cover%2C-properties.datetime") !=
                    std::string::npos,
                "search URL lost deterministic cloud/time sorting");

        require(!earthscience::buildSentinel2SearchUrl(
                    {170.0, -10.0, -170.0, 10.0},
                    "2026-06-18T00:00:00Z", "2026-07-18T23:59:59Z",
                    20.0, 10, url, error),
                "antimeridian-crossing bbox was accepted");
        require(!earthscience::buildSentinel2SearchUrl(
                    {139.60, 35.50, 139.95, 35.85},
                    "2026-07-18T00:00:00Z", "2026-06-18T23:59:59Z",
                    20.0, 10, url, error),
                "reversed interval was accepted");
        require(!earthscience::buildSentinel2SearchUrl(
                    {139.60, 35.50, 139.95, 35.85},
                    "2026-06-18T00:00:00Z", "2026-07-18T23:59:59Z",
                    20.0, 11, url, error),
                "unbounded scene request was accepted");
        require(!earthscience::buildSentinel2SearchUrl(
                    {139.60, 35.50, 139.95, 35.85},
                    "2026-06-18T00:00:00Z", "2026-07-18T23:59:59Z",
                    101.0, 10, url, error),
                "invalid cloud threshold was accepted");
    }

    void testParseAndDeterministicSelection()
    {
        const std::string json = collection({
            feature("scene-cloudy", "2026-07-16T01:30:00Z", 18.0),
            feature("scene-clear", "2026-07-15T01:37:22.011000Z", 4.5),
        });
        std::vector<earthscience::Sentinel2Scene> scenes;
        std::string error;
        require(earthscience::parseSentinel2Items(json, scenes, error),
                "valid official-shaped STAC response was rejected");
        require(scenes.size() == 2 && scenes[1].resolutionMeters == 10.0,
                "valid STAC scene metadata was not preserved");

        earthscience::Sentinel2Scene selected;
        require(earthscience::selectSentinel2Item(
                    scenes, 20.0, {35.68, 139.76}, selected, error),
                "valid scenes could not be selected");
        require(selected.itemId == "scene-clear" &&
                    selected.cloudCoverPercent == 4.5,
                "selection did not prefer the lowest cloud cover");

        scenes = {
            scenes[0], scenes[0], scenes[0],
        };
        scenes[0].itemId = "older";
        scenes[0].acquisitionTime = "2026-07-15T01:30:00Z";
        scenes[1].itemId = "z-newer";
        scenes[1].acquisitionTime = "2026-07-16T01:30:00Z";
        scenes[2].itemId = "a-newer";
        scenes[2].acquisitionTime = "2026-07-16T01:30:00Z";
        require(earthscience::selectSentinel2Item(
                    scenes, 20.0, {35.68, 139.76}, selected, error) &&
                    selected.itemId == "a-newer",
                "selection did not use newest time then lexical id");

        require(!earthscience::selectSentinel2Item(
                    scenes, 10.0, {35.68, 139.76}, selected, error) &&
                    error.find("cloud") != std::string::npos,
                "cloud threshold silently accepted a cloudier scene");
    }

    void testSelectionRequiresCoverageOfRequestedPoint()
    {
        earthscience::Sentinel2Scene clearerButAdjacent;
        clearerButAdjacent.itemId = "clear-adjacent-tile";
        clearerButAdjacent.acquisitionTime = "2026-07-16T01:30:00Z";
        clearerButAdjacent.cloudCoverPercent = 1.0;
        clearerButAdjacent.bounds = {139.80, 35.0, 140.5, 36.0};

        earthscience::Sentinel2Scene covering;
        covering.itemId = "covering-tile";
        covering.acquisitionTime = "2026-07-15T01:30:00Z";
        covering.cloudCoverPercent = 8.0;
        covering.bounds = {138.7, 35.0, 139.79, 36.0};

        earthscience::Sentinel2Scene selected;
        std::string error;
        require(earthscience::selectSentinel2Item(
                    {clearerButAdjacent, covering}, 20.0,
                    {35.68, 139.76}, selected, error) &&
                    selected.itemId == "covering-tile",
                "selection preferred an adjacent tile that misses the requested point");

        require(!earthscience::selectSentinel2Item(
                    {clearerButAdjacent}, 20.0, {35.68, 139.76},
                    selected, error) &&
                    error.find("requested point") != std::string::npos,
                "selection did not expose missing point coverage");
    }

    void testResolutionFallsBackToRasterBandMetadata()
    {
        std::string item = feature(
            "missing-asset-gsd", "2025-01-20T03:12:06.317000Z", 0.1);
        const std::string gsd = "\"gsd\":10";
        const std::size_t position = item.find(gsd);
        require(position != std::string::npos,
                "test fixture lost its asset gsd");
        item.replace(
            position, gsd.size(),
            "\"gsd\":null,\"raster:bands\":["
            "{\"data_type\":\"uint8\",\"spatial_resolution\":10},"
            "{\"data_type\":\"uint8\",\"spatial_resolution\":10},"
            "{\"data_type\":\"uint8\",\"spatial_resolution\":10}]");

        std::vector<earthscience::Sentinel2Scene> scenes;
        std::string error;
        require(earthscience::parseSentinel2Items(
                    collection({item}), scenes, error) &&
                    scenes.size() == 1 &&
                    scenes.front().resolutionMeters == 10.0,
                "official Earth Search item with null asset gsd did not"
                " fall back to raster band spatial resolution");
    }

    void testInvalidCandidateDoesNotDiscardValidScene()
    {
        std::string invalid = feature(
            "missing-resolution", "2025-01-20T03:12:06Z", 0.1);
        const std::string gsd = "\"gsd\":10";
        invalid.replace(invalid.find(gsd), gsd.size(), "\"gsd\":null");
        std::vector<earthscience::Sentinel2Scene> scenes;
        std::string error;
        require(earthscience::parseSentinel2Items(
                    collection({
                        feature("usable", "2025-03-24T03:12:06Z", 0.2),
                        invalid,
                    }),
                    scenes, error) &&
                    scenes.size() == 1 &&
                    scenes.front().itemId == "usable",
                "one incomplete Earth Search candidate discarded the valid"
                " Sentinel-2 scene");
    }

    void testUnsafeOrMalformedResponsesAreRejected()
    {
        requireRejected("not-json", "malformed JSON was accepted");
        requireRejected("[]", "non-object STAC response was accepted");
        requireRejected(collection({feature(
            "bad-bbox", "2026-07-15T01:37:22Z", 4.0,
            "https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
            "sentinel-s2-l2a-cogs/a/TCI.tif",
            "image/tiff; application=geotiff; profile=cloud-optimized",
            "[140.0,35.0,139.0,36.0]")}),
            "reversed scene bbox was accepted");
        requireRejected(collection({feature(
            "http", "2026-07-15T01:37:22Z", 4.0,
            "http://sentinel-cogs.s3.us-west-2.amazonaws.com/"
            "sentinel-s2-l2a-cogs/a/TCI.tif")}),
            "non-HTTPS asset was accepted");
        requireRejected(collection({feature(
            "wrong-host", "2026-07-15T01:37:22Z", 4.0,
            "https://example.com/sentinel-s2-l2a-cogs/a/TCI.tif")}),
            "non-allowlisted asset host was accepted");
        requireRejected(collection({feature(
            "jp2", "2026-07-15T01:37:22Z", 4.0,
            "https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
            "sentinel-s2-l2a-cogs/a/TCI.jp2")}),
            "non-COG suffix was accepted");
        requireRejected(collection({feature(
            "wrong-type", "2026-07-15T01:37:22Z", 4.0,
            "https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
            "sentinel-s2-l2a-cogs/a/TCI.tif", "image/jpeg")}),
            "non-COG media type was accepted");
        requireRejected(collection({feature(
            "bad-time", "2026-15-99T99:99:99Z", 4.0)}),
            "invalid acquisition time was accepted");
        requireRejected(collection({feature(
            "bad-cloud", "2026-07-15T01:37:22Z", 101.0)}),
            "out-of-range cloud cover was accepted");

        std::vector<std::string> tooMany;
        for (int index = 0; index < 11; ++index)
            tooMany.push_back(feature(
                "scene-" + std::to_string(index),
                "2026-07-15T01:37:22Z", static_cast<double>(index)));
        requireRejected(collection(tooMany),
                        "more than ten retained scenes were accepted");

        requireRejected(std::string(2 * 1024 * 1024 + 1, 'x'),
                        "oversize STAC response was accepted");
    }
}

int main()
{
    testSearchUrlIsBoundedAndEncoded();
    testParseAndDeterministicSelection();
    testSelectionRequiresCoverageOfRequestedPoint();
    testResolutionFallsBackToRasterBandMetadata();
    testInvalidCandidateDoesNotDiscardValidScene();
    testUnsafeOrMalformedResponsesAreRejected();
    std::cout << "[OK] Sentinel-2 STAC contract\n";
    return 0;
}
