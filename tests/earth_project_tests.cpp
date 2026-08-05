// EarthProject persistence contract tests. This target is deliberately
// renderer-free so project files cannot acquire an OSG/window dependency.
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

#include "../applications/earth_explorer/project/earth_project.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; \
    std::exit(EXIT_FAILURE); } } while (0)

namespace
{
    using namespace earthproject;

    EarthProject sampleProject()
    {
        EarthProject project;
        project.projectId = "project-shenzhen-bay";
        project.name = u8"深圳湾研究";
        project.camera.latitudeDeg = 22.5123;
        project.camera.longitudeDeg = 113.9432;
        project.camera.altitudeKm = 138.0;
        project.camera.headingDeg = 18.0;
        project.camera.pitchDeg = -72.0;
        project.workspace.activeModule = "science";
        project.workspace.drawerOpen = true;
        project.workspace.activeArtifactIds.push_back("era5-agro-7");

        EarthLayerDescriptor layer;
        layer.id = "alphaearth";
        layer.displayName = "AlphaEarth Foundations";
        layer.group = "science";
        layer.source.kind = "science-provider";
        layer.source.providerId = "alphaearth-foundations";
        layer.source.crs = "EPSG:4326";
        layer.source.attribution = "Google / Google DeepMind";
        layer.source.license = "CC-BY-4.0";
        layer.render.kind = "terrain-draped-raster";
        layer.render.visible = true;
        layer.render.opacity = 0.72;
        layer.render.order = 14;
        layer.temporal.mode = TemporalMode::Discrete;
        layer.temporal.granularity = "year";
        layer.temporal.currentValue = "2025";
        layer.temporal.availableValues.push_back("2024");
        layer.temporal.availableValues.push_back("2025");
        layer.capabilities.temporal = true;
        layer.capabilities.identify = true;
        layer.capabilities.analysis = true;
        layer.artifactId = "alphaearth-2025-3";
        project.layers.push_back(layer);
        return project;
    }

    void testRoundTripIsDeterministic()
    {
        const EarthProject input = sampleProject();
        const std::string firstJson = saveProjectJson(input);
        const ProjectLoadResult loaded = loadProjectJson(firstJson);
        CHECK(loaded.ok);
        CHECK(loaded.project.schemaVersion == kCurrentSchemaVersion);
        CHECK(loaded.project.projectId == input.projectId);
        CHECK(loaded.project.camera.latitudeDeg == input.camera.latitudeDeg);
        CHECK(loaded.project.layers.size() == 1u);
        CHECK(loaded.project.layers[0].temporal.currentValue == "2025");
        CHECK(loaded.project.layers[0].artifactId == "alphaearth-2025-3");
        CHECK(saveProjectJson(loaded.project) == firstJson);
    }

    void testNormalizationProtectsRuntimeState()
    {
        const std::string input =
            "{\"schema\":\"osgsol-earth-project\",\"version\":1,"
            "\"projectId\":\"unsafe\",\"name\":\"Unsafe\","
            "\"camera\":{\"latitudeDeg\":120,\"longitudeDeg\":541,"
            "\"altitudeKm\":-5,\"headingDeg\":725,\"pitchDeg\":-130},"
            "\"layers\":["
              "{\"id\":\"dup\",\"render\":{\"opacity\":3}},"
              "{\"id\":\"dup\",\"render\":{\"opacity\":0.4}},"
              "{\"id\":\"\",\"render\":{\"opacity\":0.5}}"
            "]}";
        const ProjectLoadResult loaded = loadProjectJson(input);
        CHECK(loaded.ok);
        CHECK(loaded.project.camera.latitudeDeg == 90.0);
        CHECK(loaded.project.camera.longitudeDeg == -179.0);
        CHECK(loaded.project.camera.altitudeKm == 0.0);
        CHECK(loaded.project.camera.headingDeg == 5.0);
        CHECK(loaded.project.camera.pitchDeg == -90.0);
        CHECK(loaded.project.layers.size() == 1u);
        CHECK(loaded.project.layers[0].render.opacity == 1.0);
        CHECK(!loaded.diagnostics.empty());
    }

    void testLegacyV0MigratesWithoutCredentials()
    {
        const std::string legacy =
            "{\"schema\":\"osgsol-earth-project\",\"version\":0,"
            "\"name\":\"Legacy\",\"apiKey\":\"must-not-survive\","
            "\"camera\":{\"lat\":35,\"lon\":138,\"altKm\":150},"
            "\"layers\":[{\"id\":\"base\",\"name\":\"Imagery\","
            "\"enabled\":true,\"opacity\":0.8,"
            "\"accessToken\":\"also-secret\"}]}";
        const ProjectLoadResult loaded = loadProjectJson(legacy);
        CHECK(loaded.ok);
        CHECK(loaded.migratedFromVersion == 0);
        CHECK(loaded.project.camera.latitudeDeg == 35.0);
        CHECK(loaded.project.camera.longitudeDeg == 138.0);
        CHECK(loaded.project.layers.size() == 1u);
        CHECK(loaded.project.layers[0].displayName == "Imagery");
        CHECK(loaded.project.layers[0].render.visible);
        const std::string saved = saveProjectJson(loaded.project);
        CHECK(saved.find("must-not-survive") == std::string::npos);
        CHECK(saved.find("also-secret") == std::string::npos);
        CHECK(saved.find("apiKey") == std::string::npos);
        CHECK(saved.find("accessToken") == std::string::npos);
    }

    void testFutureAndMalformedProjectsAreRejected()
    {
        ProjectLoadResult loaded = loadProjectJson(
            "{\"schema\":\"osgsol-earth-project\",\"version\":999}");
        CHECK(!loaded.ok);
        CHECK(loaded.errorCode == "unsupported_project_version");

        loaded = loadProjectJson("{not-json");
        CHECK(!loaded.ok);
        CHECK(loaded.errorCode == "invalid_project_json");

        loaded = loadProjectJson(
            "{\"schema\":\"another-product\",\"version\":1}");
        CHECK(!loaded.ok);
        CHECK(loaded.errorCode == "unsupported_project_schema");
    }

    void testSourceUrisDoNotPersistCredentials()
    {
        EarthProject project = sampleProject();
        project.layers[0].source.uri =
            "https://user:password@example.test/tiles?style=dark&"
            "api_key=SECRET-API-KEY&access_token=SECRET-TOKEN#layer";
        const std::string saved = saveProjectJson(project);
        CHECK(saved.find("password") == std::string::npos);
        CHECK(saved.find("SECRET-API-KEY") == std::string::npos);
        CHECK(saved.find("SECRET-TOKEN") == std::string::npos);
        CHECK(saved.find("api_key") == std::string::npos);
        CHECK(saved.find("access_token") == std::string::npos);

        const ProjectLoadResult loaded = loadProjectJson(saved);
        CHECK(loaded.ok);
        CHECK(loaded.project.layers[0].source.uri ==
              "https://example.test/tiles?style=dark#layer");
    }

    void testOversizedProjectIsRejectedBeforeParsing()
    {
        const ProjectLoadResult loaded = loadProjectJson(
            std::string(kMaxProjectJsonBytes + 1u, ' '));
        CHECK(!loaded.ok);
        CHECK(loaded.errorCode == "project_too_large");
    }
}

int main()
{
    testRoundTripIsDeterministic();
    testNormalizationProtectsRuntimeState();
    testLegacyV0MigratesWithoutCredentials();
    testFutureAndMalformedProjectsAreRejected();
    testSourceUrisDoNotPersistCredentials();
    testOversizedProjectIsRejectedBeforeParsing();
    std::cout << "EarthProject tests OK\n";
    return 0;
}
