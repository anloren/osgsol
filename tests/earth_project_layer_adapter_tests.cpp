#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "../applications/earth_explorer/project/earth_project_layer_adapter.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; \
    std::exit(EXIT_FAILURE); } } while (0)

int main()
{
    using namespace earthproject;

    LayerManager layers;
    int applied = 0;
    OverlayLayer science;
    science.id = "alphaearth";
    science.displayName = "AlphaEarth Foundations";
    science.group = "ScienceEarth";
    science.type = OverlayLayer::Grid;
    science.enabled = true;
    science.hasOpacity = true;
    science.opacity = 0.75f;
    science.source.kind = "science-provider";
    science.source.providerId = "alphaearth-foundations";
    science.source.crs = "EPSG:4326";
    science.temporal.mode = TemporalMode::Discrete;
    science.temporal.granularity = "year";
    science.temporal.currentValue = "2025";
    science.capabilities.temporal = true;
    science.capabilities.analysis = true;
    science.styleId = "alphaearth-pca-rgb";
    science.legendId = "alphaearth-pca-rgb-v1";
    science.artifactId = "aef-2025-17";
    science.apply = [&applied](const OverlayLayer&) { ++applied; };
    layers.add(science);

    // A runtime-owned base layer has no apply callback and must not become
    // mutable merely because a project file contains a matching id.
    OverlayLayer base;
    base.id = "base";
    base.displayName = "Imagery";
    base.group = u8"底图 / 标注";
    base.enabled = true;
    layers.add(base);

    const std::vector<EarthLayerDescriptor> snapshot =
        snapshotLayerDescriptors(layers);
    CHECK(snapshot.size() == 2u);
    CHECK(snapshot[0].id == "alphaearth");
    CHECK(snapshot[0].render.kind == "grid");
    CHECK(snapshot[0].render.visible);
    CHECK(snapshot[0].render.opacity == 0.75);
    CHECK(snapshot[0].source.providerId == "alphaearth-foundations");
    CHECK(snapshot[0].temporal.currentValue == "2025");
    CHECK(snapshot[0].capabilities.analysis);
    CHECK(snapshot[0].render.styleId == "alphaearth-pca-rgb");
    CHECK(snapshot[0].render.legendId == "alphaearth-pca-rgb-v1");
    CHECK(snapshot[0].artifactId == "aef-2025-17");

    std::vector<EarthLayerDescriptor> restored = snapshot;
    restored[0].render.visible = false;
    restored[0].render.opacity = 0.25;
    restored[1].render.visible = false;
    EarthLayerDescriptor missing;
    missing.id = "plugin-not-installed";
    missing.render.visible = true;
    restored.push_back(missing);

    const LayerRestoreResult result = restoreLayerDisplayState(layers, restored);
    CHECK(result.matched == 2u);
    CHECK(result.changed == 1u);
    CHECK(result.runtimeOwned == 1u);
    CHECK(result.missingIds.size() == 1u);
    CHECK(result.missingIds[0] == "plugin-not-installed");
    CHECK(!layers.find("alphaearth")->enabled);
    CHECK(layers.find("alphaearth")->opacity == 0.25f);
    CHECK(layers.find("base")->enabled);
    CHECK(applied == 0); // queued changes never call renderer callbacks inline
    CHECK(layers.drainPending() == 2u);
    CHECK(applied == 2);

    std::cout << "EarthProject layer adapter tests OK\n";
    return 0;
}
