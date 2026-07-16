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

    earthscience::GeoTemporalQuery makeAnalysisQuery()
    {
        earthscience::GeoTemporalQuery query = makeQuery();
        query.time.explicitYears = {2017, 2018};
        query.variables = {"embedding64"};
        query.visualizationId.clear();
        query.outputKind = earthscience::ScienceOutputKind::TimeSeries;
        query.analysis.kind =
            earthscience::ScienceAnalysisKind::PointSeries;
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
        require(source.variables.size() == 65 &&
                    source.variables[0].id == "A00" &&
                    source.variables[63].id == "A63" &&
                    source.variables[64].id == "embedding64" &&
                    source.variables[64].componentCount == 64,
                "AlphaEarth complete 64D variables were not advertised");
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
                    source.capabilities.boundingBoxQuery &&
                    source.capabilities.currentViewQuery &&
                    source.capabilities.explicitYears &&
                    source.capabilities.rasterLayerOutput &&
                    source.capabilities.embeddingOutput &&
                    source.capabilities.timeSeriesOutput &&
                    source.capabilities.analysisOutput &&
                    source.capabilities.exportOutput &&
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

    void testOnlyExactLegacyRasterSignatureUsesPreview()
    {
        const earthscience::GeoTemporalQuery preview = makeQuery();
        require(earthscience::isAlphaEarthLegacyPreviewQuery(preview),
                "exact A01/A16/A09 raster query stopped using preview");

        earthscience::GeoTemporalQuery changed = preview;
        changed.variables = {"embedding64"};
        require(!earthscience::isAlphaEarthLegacyPreviewQuery(changed),
                "embedding64 request was routed to preview");
        changed = preview;
        changed.outputKind = earthscience::ScienceOutputKind::Embedding;
        require(!earthscience::isAlphaEarthLegacyPreviewQuery(changed),
                "embedding output was routed to preview");
        changed = preview;
        changed.time.explicitYears = {2024, 2025};
        require(!earthscience::isAlphaEarthLegacyPreviewQuery(changed),
                "multi-year request was routed to preview");
        changed = preview;
        changed.geometry.kind = earthscience::ScienceGeometryKind::BoundingBox;
        require(!earthscience::isAlphaEarthLegacyPreviewQuery(changed),
                "regional request was routed to preview");
        changed = preview;
        changed.visualizationId.clear();
        require(!earthscience::isAlphaEarthLegacyPreviewQuery(changed),
                "unidentified raster request was routed to preview");

        earthscience::AlphaEarthProvider provider(
            "/definitely/missing/alphaearth.sqlite");
        const std::uint64_t previewGeneration = provider.submit(preview);
        const earthscience::ScienceProviderSnapshot previewState =
            provider.snapshot();
        require(previewGeneration != 0 &&
                    previewState.state ==
                        earthscience::ScienceJobState::Unavailable &&
                    previewState.message == "AlphaEarth index is missing",
                "provider did not dispatch exact legacy query to preview");

        const std::uint64_t analysisGeneration =
            provider.submit(makeAnalysisQuery());
        const earthscience::ScienceProviderSnapshot analysisState =
            provider.snapshot();
        require(analysisGeneration != 0 &&
                    analysisState.generation == analysisGeneration &&
                    analysisState.message != "AlphaEarth index is missing",
                "provider did not dispatch embedding64 query to 64D runtime");
    }

    void testEveryRuntimeStateTranslatesExactly()
    {
        struct StateTranslation
        {
            earthscience::AlphaEarthPreviewState previewState;
            earthscience::ScienceJobState jobState;
            earthscience::ScienceProgressStage progressStage;
        };
        const std::vector<StateTranslation> states = {
            {earthscience::AlphaEarthPreviewState::Unavailable,
             earthscience::ScienceJobState::Unavailable,
             earthscience::ScienceProgressStage::Idle},
            {earthscience::AlphaEarthPreviewState::Idle,
             earthscience::ScienceJobState::Idle,
             earthscience::ScienceProgressStage::Idle},
            {earthscience::AlphaEarthPreviewState::Queued,
             earthscience::ScienceJobState::Queued,
             earthscience::ScienceProgressStage::Queued},
            {earthscience::AlphaEarthPreviewState::Fetching,
             earthscience::ScienceJobState::Fetching,
             earthscience::ScienceProgressStage::Reading},
            {earthscience::AlphaEarthPreviewState::Ready,
             earthscience::ScienceJobState::Ready,
             earthscience::ScienceProgressStage::Ready},
            {earthscience::AlphaEarthPreviewState::Failed,
             earthscience::ScienceJobState::Failed,
             earthscience::ScienceProgressStage::Failed},
            {earthscience::AlphaEarthPreviewState::Cancelled,
             earthscience::ScienceJobState::Cancelled,
             earthscience::ScienceProgressStage::Cancelled},
        };

        for (const auto& state : states)
        {
            earthscience::AlphaEarthPreviewSnapshot legacy;
            legacy.generation = 9;
            legacy.state = state.previewState;
            legacy.progress = 0.4f;
            legacy.message = "state message";
            const earthscience::ScienceProviderSnapshot translated =
                earthscience::translateAlphaEarthSnapshot(
                    legacy, makeQuery());
            require(translated.generation == 9 &&
                        translated.state == state.jobState &&
                        translated.progress.stage == state.progressStage &&
                        !translated.progress.determinate &&
                        translated.progress.completedUnits == 0 &&
                        translated.progress.totalUnits == 0 &&
                        translated.message == "state message",
                    "preview phase hints became fabricated generic progress");
        }

        for (const float phaseHint : {0.05f, 0.2f})
        {
            earthscience::AlphaEarthPreviewSnapshot fetching;
            fetching.state = earthscience::AlphaEarthPreviewState::Fetching;
            fetching.progress = phaseHint;
            const earthscience::ScienceProviderSnapshot translated =
                earthscience::translateAlphaEarthSnapshot(
                    fetching, makeQuery());
            require(translated.progress.stage ==
                        earthscience::ScienceProgressStage::Reading &&
                        !translated.progress.determinate &&
                        translated.progress.legacyFraction() == 0.0f,
                    "preview fetching hint fabricated measurable progress");
        }
    }

    void testReadyArtifactTranslationSharesPayloadAndProvenance()
    {
        earthscience::AlphaEarthPreviewSnapshot legacy;
        legacy.generation = 12;
        legacy.state = earthscience::AlphaEarthPreviewState::Ready;
        legacy.progress = 1.0f;
        legacy.elapsedSeconds = 0.25;
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
        require(translated.progress.stage ==
                    earthscience::ScienceProgressStage::Ready &&
                    translated.progress.determinate &&
                    translated.progress.completedUnits == 1 &&
                    translated.progress.totalUnits == 1 &&
                    translated.progress.elapsedSeconds == 0.25,
                "ready preview did not publish measurable completion");
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
    testOnlyExactLegacyRasterSignatureUsesPreview();
    testEveryRuntimeStateTranslatesExactly();
    testReadyArtifactTranslationSharesPayloadAndProvenance();
    testMissingIndexRemainsVisibleWithoutNetworkAccess();
    std::cout << "[OK] ScienceEarth AlphaEarth provider translation\n";
    return 0;
}
