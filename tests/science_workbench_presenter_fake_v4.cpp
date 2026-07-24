#include "../applications/earth_explorer/science_plugin_api.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{
int sessionValue = 7;
bool artifactReady = false;
int artifactGeneration = 0;

void* createSession(const char*, char*, std::size_t)
{
    artifactReady = false;
    artifactGeneration = 0;
    return &sessionValue;
}
void destroySession(void*) {}
osg::Node* sceneNode(void*) { return nullptr; }
void setVisible(void*, bool) {}
void bindGeoRaster(void*, const OsgSolGeoRasterBridgeV1*) {}
void registerAiTools(void*, earthai::ToolRegistry*, LayerManager*,
                     osgVerse::EarthManipulator*) {}
void bindGui(void*, const OsgSolScienceGuiBridgeV1*) {}
void drawOperations(void*, LayerManager*, osgVerse::EarthManipulator*) {}
void drawResults(void*, LayerManager*) {}

bool copySnapshot(void*, OsgSolScienceUiBufferV1* output)
{
    static const std::string draft =
        R"({"revision":7,"schema":"science-workbench-ui-v1","phase":"ready-to-run","selectedMethodId":"annual-summary","activeArtifactId":"","draft":{"sourceId":"era5-agricultural-climate","years":[2017,2025]},"target":{"locked":false},"sources":[{"id":"era5-agricultural-climate","name":"ERA5 Agricultural Climate","category":"climate","spatialSupport":"0.25 degree grid","health":"ready","healthMessage":"","firstYear":1940,"lastYear":2025,"capabilities":{"bounds":false,"timeSeries":true,"analysis":true,"raster":false}},{"id":"alphaearth-foundations","name":"AlphaEarth Foundations","category":"embedding","spatialSupport":"10 m indexed tile","health":"ready","healthMessage":"","firstYear":2017,"lastYear":2025,"capabilities":{"bounds":true,"timeSeries":true,"analysis":true,"raster":true}},{"id":"sentinel-2-l2a","name":"Sentinel-2 Level-2A","category":"imagery","spatialSupport":"10 m scene","health":"ready","healthMessage":"","firstYear":2015,"lastYear":2026,"capabilities":{"bounds":true,"timeSeries":false,"analysis":false,"raster":true}},{"id":"copernicus-dem-glo-30","name":"Copernicus DEM GLO-30","category":"terrain","spatialSupport":"30 m surface model","health":"ready","healthMessage":"","firstYear":2021,"lastYear":2021,"capabilities":{"bounds":true,"timeSeries":false,"analysis":false,"raster":true}},{"id":"era5-land-surface-history","name":"ERA5-Land Surface & Soil History","category":"climate","spatialSupport":"0.1 degree grid","health":"ready","healthMessage":"","firstYear":1940,"lastYear":2025,"capabilities":{"bounds":false,"timeSeries":true,"analysis":true,"raster":false}}]})";
    static const std::string artifactTemplate =
        R"({"revision":8,"schema":"science-workbench-ui-v1","phase":"ready","selectedMethodId":"annual-summary","activeArtifactId":"alphaearth-test-artifact","selectedMetricId":"embedding-distance","draft":{"sourceId":"alphaearth-foundations","years":[2017,2025]},"target":{"locked":true,"hasActualCoverage":false,"requested":{"kind":"point","point":{"latitude":22.3,"longitude":114.17}},"requestedCenter":{"latitude":22.3,"longitude":114.17}},"sources":[{"id":"era5-agricultural-climate","name":"ERA5 Agricultural Climate","category":"climate","spatialSupport":"0.25 degree grid","health":"ready","healthMessage":"","firstYear":1940,"lastYear":2025,"capabilities":{"bounds":false,"timeSeries":true,"analysis":true,"raster":false}},{"id":"alphaearth-foundations","name":"AlphaEarth Foundations","category":"embedding","spatialSupport":"10 m indexed tile","health":"ready","healthMessage":"","firstYear":2017,"lastYear":2025,"capabilities":{"bounds":true,"timeSeries":true,"analysis":true,"raster":true}},{"id":"sentinel-2-l2a","name":"Sentinel-2 Level-2A","category":"imagery","spatialSupport":"10 m scene","health":"ready","healthMessage":"","firstYear":2015,"lastYear":2026,"capabilities":{"bounds":true,"timeSeries":false,"analysis":false,"raster":true}},{"id":"copernicus-dem-glo-30","name":"Copernicus DEM GLO-30","category":"terrain","spatialSupport":"30 m surface model","health":"ready","healthMessage":"","firstYear":2021,"lastYear":2021,"capabilities":{"bounds":true,"timeSeries":false,"analysis":false,"raster":true}},{"id":"era5-land-surface-history","name":"ERA5-Land Surface & Soil History","category":"climate","spatialSupport":"0.1 degree grid","health":"ready","healthMessage":"","firstYear":1940,"lastYear":2025,"capabilities":{"bounds":false,"timeSeries":true,"analysis":true,"raster":false}}],"activeArtifact":{"artifactId":"alphaearth-test-artifact","sourceId":"alphaearth-foundations","processingVersion":"test-v1","createdAt":"2026-07-23T00:00:00Z","series":[{"id":"embedding-distance","name":"Embedding distance","unit":"","aggregation":"point","nativeResolutionMeters":10,"points":[{"year":2017,"value":0,"valid":true},{"year":2025,"value":0.25,"valid":true}]},{"id":"embedding-similarity","name":"Embedding similarity","unit":"","aggregation":"point","nativeResolutionMeters":10,"points":[{"year":2017,"value":1,"valid":true},{"year":2025,"value":0.75,"valid":true}]}]},"reportEvidence":{"availability":"complete","spatialSupport":"10 m indexed tile","sourceLicense":"CC-BY 4.0","sourceDocumentationUrl":"https://example.invalid/alphaearth","sourceAttribution":"test","sourceQualityStatement":"test fixture","limitationsAvailability":"complete","aggregationMethods":["point"],"limitations":[],"warnings":[],"exportCapabilities":{"artifactExport":true,"rasterOutput":true,"tableOutput":true}}})";
    std::string artifact = artifactTemplate;
    if (artifactReady)
    {
        const std::string artifactId =
            "alphaearth-test-artifact-" +
            std::to_string(std::max(artifactGeneration, 1));
        const std::string oldId = "alphaearth-test-artifact";
        for (std::size_t position = 0;
             (position = artifact.find(oldId, position)) != std::string::npos;
             position += artifactId.size())
            artifact.replace(position, oldId.size(), artifactId);
        artifact.replace(
            artifact.find("\"revision\":8"),
            std::strlen("\"revision\":8"),
            "\"revision\":" +
                std::to_string(7 + std::max(artifactGeneration, 1)));
    }
    const std::string& value = artifactReady ? artifact : draft;
    if (!output || output->structSize < sizeof(*output)) return false;
    output->revision = artifactReady
        ? static_cast<std::uint64_t>(
              7 + std::max(artifactGeneration, 1))
        : 7;
    output->bytesWritten = 0;
    output->bytesRequired = value.size() + 1;
    if (!output->utf8 || output->capacity < output->bytesRequired)
        return false;
    std::memcpy(output->utf8, value.data(), value.size());
    output->utf8[value.size()] = '\0';
    output->bytesWritten = value.size();
    return true;
}

bool dispatchAction(void*, const char* action, std::size_t actionSize,
                    char* error, std::size_t errorSize)
{
    const std::string value(action ? action : "", actionSize);
    if (value.find("\"action\":\"run\"") != std::string::npos)
    {
        artifactReady = true;
        ++artifactGeneration;
        return true;
    }
    if (error && errorSize > 0)
        std::snprintf(error, errorSize, "%s", "presenter fake rejected");
    return false;
}

const OsgSolSciencePluginApiV4 api = {
    {
        OSGSOL_SCIENCE_PLUGIN_ABI_V4,
        sizeof(OsgSolSciencePluginApiV4),
        createSession,
        destroySession,
        sceneNode,
        setVisible,
        bindGeoRaster,
        registerAiTools,
        bindGui,
        drawOperations,
        drawResults,
    },
    copySnapshot,
    dispatchAction,
};
}

extern "C" __attribute__((visibility("default")))
const OsgSolSciencePluginApiV3* osgsol_science_g0_probe_anchor()
{
    return &api.v3;
}
