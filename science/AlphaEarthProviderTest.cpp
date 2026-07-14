#include "AlphaEarthProvider.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    earthscience::GeoTemporalQuery makeQuery()
    {
        earthscience::GeoTemporalQuery query;
        query.sourceId = "alphaearth-foundations";
        query.geometry.point.latitude = 35.36;
        query.geometry.point.longitude = 138.73;
        query.geometry.requestedSpanMeters = 20000.0;
        query.time.explicitYears = {2025};
        query.variables = {"A01", "A16", "A09"};
        query.visualizationId = "false-color-a01-a16-a09";
        return query;
    }

    void testDescriptorTranslationIsComplete()
    {
        earthscience::AlphaEarthSourceDescriptor legacy;
        const earthscience::ScienceSourceDescriptor source =
            earthscience::describeAlphaEarth(legacy, true, "Ready");

        require(source.id == "alphaearth-foundations" &&
                    source.name == "AlphaEarth Foundations",
                "AlphaEarth source identity changed during translation");
        require(source.providerVersion == "1.1" &&
                    source.firstYear == 2017 && source.lastYear == 2025,
                "AlphaEarth version or temporal coverage was lost");
        require(source.nativeResolutionMeters == 10.0 &&
                    source.componentCount == 64,
                "AlphaEarth resolution or component count was lost");
        require(source.variables.size() == 3 &&
                    source.variables[0].id == "A01" &&
                    source.variables[1].id == "A16" &&
                    source.variables[2].id == "A09",
                "AlphaEarth supported variables changed");
        require(source.visualizations.size() == 1 &&
                    source.visualizations[0].id ==
                        "false-color-a01-a16-a09" &&
                    source.visualizations[0].channelVariables ==
                        std::vector<std::string>({"A01", "A16", "A09"}),
                "AlphaEarth false-color visualization changed");
        require(source.visualizations[0].displayMinimum == -0.3 &&
                    source.visualizations[0].displayMaximum == 0.3,
                "AlphaEarth false-color display range changed");
        require(source.health == earthscience::ScienceSourceHealth::Ready &&
                    source.healthMessage == "Ready",
                "available AlphaEarth health was not translated");
        require(source.capabilities.pointQuery &&
                    source.capabilities.explicitYears &&
                    source.capabilities.rasterLayerOutput &&
                    source.capabilities.minimumSpanMeters == 2560.0 &&
                    source.capabilities.maximumSpanMeters == 81920.0,
                "AlphaEarth bounded capabilities were not advertised");

        const earthscience::ScienceSourceDescriptor unavailable =
            earthscience::describeAlphaEarth(
                legacy, false, "AlphaEarth index is missing");
        require(unavailable.health ==
                    earthscience::ScienceSourceHealth::Unavailable &&
                    unavailable.healthMessage ==
                        "AlphaEarth index is missing",
                "unavailable AlphaEarth disappeared behind a generic status");
    }

    void testEveryRuntimeStateTranslatesExactly()
    {
        const std::vector<std::pair<
            earthscience::AlphaEarthPreviewState,
            earthscience::ScienceJobState>> states = {
            {earthscience::AlphaEarthPreviewState::Unavailable,
             earthscience::ScienceJobState::Unavailable},
            {earthscience::AlphaEarthPreviewState::Idle,
             earthscience::ScienceJobState::Idle},
            {earthscience::AlphaEarthPreviewState::Queued,
             earthscience::ScienceJobState::Queued},
            {earthscience::AlphaEarthPreviewState::Fetching,
             earthscience::ScienceJobState::Fetching},
            {earthscience::AlphaEarthPreviewState::Ready,
             earthscience::ScienceJobState::Ready},
            {earthscience::AlphaEarthPreviewState::Failed,
             earthscience::ScienceJobState::Failed},
            {earthscience::AlphaEarthPreviewState::Cancelled,
             earthscience::ScienceJobState::Cancelled},
        };

        for (const auto& state : states)
        {
            earthscience::AlphaEarthPreviewSnapshot legacy;
            legacy.generation = 9;
            legacy.state = state.first;
            legacy.progress = 0.4f;
            legacy.message = "state message";
            const earthscience::ScienceProviderSnapshot translated =
                earthscience::translateAlphaEarthSnapshot(
                    legacy, makeQuery());
            require(translated.generation == 9 &&
                        translated.state == state.second &&
                        translated.progress == 0.4f &&
                        translated.message == "state message",
                    "AlphaEarth runtime state translation changed");
        }
    }

    void testReadyArtifactTranslationSharesPayloadAndProvenance()
    {
        earthscience::AlphaEarthPreviewSnapshot legacy;
        legacy.generation = 12;
        legacy.state = earthscience::AlphaEarthPreviewState::Ready;
        legacy.progress = 1.0f;
        legacy.message = "AlphaEarth preview ready";
        legacy.artifact.generation = 12;
        legacy.artifact.datasetId = "195398";
        legacy.artifact.sourceUrl =
            "https://data.source.coop/example/2025.tiff";
        legacy.artifact.sourceVersion = "1.1";
        legacy.artifact.attribution =
            "Google / Google DeepMind / source.coop";
        legacy.artifact.year = 2025;
        legacy.artifact.west = 138.60;
        legacy.artifact.south = 35.20;
        legacy.artifact.east = 138.86;
        legacy.artifact.north = 35.46;
        legacy.artifact.width = 256;
        legacy.artifact.height = 256;
        legacy.artifact.sourceWindowWidth = 2048;
        legacy.artifact.sourceWindowHeight = 2048;
        legacy.artifact.sourceResolutionMeters = 10.0;
        legacy.artifact.displayResolutionMeters = 80.0;
        legacy.artifact.rgba =
            std::make_shared<const std::vector<unsigned char>>(
                256 * 256 * 4, 166);
        auto grid = std::make_shared<earthscience::ScienceGroundGrid>();
        grid->columns = 2;
        grid->rows = 2;
        grid->points = {
            {138.60, 35.20}, {138.86, 35.20},
            {138.60, 35.46}, {138.86, 35.46},
        };
        legacy.artifact.groundGrid = grid;

        const earthscience::ScienceProviderSnapshot translated =
            earthscience::translateAlphaEarthSnapshot(legacy, makeQuery());
        require(translated.artifact != nullptr,
                "ready AlphaEarth snapshot lost its artifact");
        require(translated.artifact->artifactId ==
                    "alphaearth-foundations-195398-2025" &&
                    translated.artifact->generation == 12,
                "AlphaEarth artifact identity changed");
        require(translated.artifact->raster.rgba == legacy.artifact.rgba &&
                    translated.artifact->raster.groundGrid ==
                        legacy.artifact.groundGrid,
                "AlphaEarth adapter copied or replaced the verified payload");
        require(translated.artifact->raster.bounds.west == 138.60 &&
                    translated.artifact->raster.bounds.north == 35.46 &&
                    translated.artifact->raster.displayResolutionMeters ==
                        80.0,
                "AlphaEarth footprint or display resolution changed");
        require(translated.artifact->sourceReferences.size() == 1 &&
                    translated.artifact->sourceReferences[0].datasetId ==
                        "195398" &&
                    translated.artifact->sourceReferences[0].originalUrl ==
                        legacy.artifact.sourceUrl &&
                    translated.artifact->sourceReferences[0].attribution ==
                        legacy.artifact.attribution,
                "AlphaEarth artifact provenance was not preserved");
    }

    void testMissingIndexRemainsVisibleWithoutNetworkAccess()
    {
        earthscience::AlphaEarthProvider provider(
            "/definitely/missing/alphaearth.sqlite");
        const earthscience::ScienceSourceDescriptor source =
            provider.descriptor();
        require(source.id == "alphaearth-foundations" &&
                    source.health ==
                        earthscience::ScienceSourceHealth::Unavailable,
                "missing AlphaEarth index removed the catalog source");
        require(source.healthMessage == "AlphaEarth index is missing",
                "missing AlphaEarth index health reason changed");
    }
}

int main()
{
    testDescriptorTranslationIsComplete();
    testEveryRuntimeStateTranslatesExactly();
    testReadyArtifactTranslationSharesPayloadAndProvenance();
    testMissingIndexRemainsVisibleWithoutNetworkAccess();
    std::cout << "[OK] ScienceEarth AlphaEarth provider translation\n";
    return 0;
}
