#include "AlphaEarthProvider.h"

#include <cstdio>
#include <utility>

namespace earthscience
{
namespace
{
    ScienceJobState translateState(AlphaEarthPreviewState state)
    {
        switch (state)
        {
        case AlphaEarthPreviewState::Unavailable:
            return ScienceJobState::Unavailable;
        case AlphaEarthPreviewState::Idle: return ScienceJobState::Idle;
        case AlphaEarthPreviewState::Queued: return ScienceJobState::Queued;
        case AlphaEarthPreviewState::Fetching:
            return ScienceJobState::Fetching;
        case AlphaEarthPreviewState::Ready: return ScienceJobState::Ready;
        case AlphaEarthPreviewState::Failed: return ScienceJobState::Failed;
        case AlphaEarthPreviewState::Cancelled:
            return ScienceJobState::Cancelled;
        }
        return ScienceJobState::Failed;
    }

    ScienceProgressStage translateProgressStage(AlphaEarthPreviewState state)
    {
        switch (state)
        {
        case AlphaEarthPreviewState::Unavailable:
        case AlphaEarthPreviewState::Idle:
            return ScienceProgressStage::Idle;
        case AlphaEarthPreviewState::Queued:
            return ScienceProgressStage::Queued;
        case AlphaEarthPreviewState::Fetching:
            return ScienceProgressStage::Reading;
        case AlphaEarthPreviewState::Ready:
            return ScienceProgressStage::Ready;
        case AlphaEarthPreviewState::Failed:
            return ScienceProgressStage::Failed;
        case AlphaEarthPreviewState::Cancelled:
            return ScienceProgressStage::Cancelled;
        }
        return ScienceProgressStage::Failed;
    }

    ScienceProgress translateProgress(const AlphaEarthPreviewSnapshot& snapshot)
    {
        ScienceProgress progress;
        progress.stage = translateProgressStage(snapshot.state);
        progress.elapsedSeconds = snapshot.elapsedSeconds;
        if (snapshot.state == AlphaEarthPreviewState::Ready &&
            snapshot.progress >= 1.0f)
        {
            progress.completedUnits = 1;
            progress.totalUnits = 1;
            progress.determinate = true;
            progress.unit = "artifact";
        }
        return progress;
    }

    ScienceSourceReference sourceReference(
        const AlphaEarthPreviewArtifact& legacy,
        const GeoTemporalQuery& query)
    {
        ScienceSourceReference reference;
        reference.sourceId = "alphaearth-foundations";
        reference.providerVersion = legacy.sourceVersion;
        reference.datasetId = legacy.datasetId;
        reference.originalUrl = legacy.sourceUrl;
        reference.requestedCoverage = query.geometry;
        reference.actualCoverage = {
            legacy.west, legacy.south, legacy.east, legacy.north};
        reference.variables = query.variables;
        reference.units.assign(query.variables.size(), "1");
        reference.processingSteps = {
            "indexed AlphaEarth COG mosaic on one WGS84 display grid",
            "A01/A16/A09 false-color normalization",
        };
        reference.acquisitionTime = std::to_string(legacy.year);
        reference.attribution = legacy.attribution;
        return reference;
    }
}

ScienceSourceDescriptor describeAlphaEarth(
    const AlphaEarthSourceDescriptor& source, bool available,
    const std::string& healthMessage)
{
    ScienceSourceDescriptor descriptor;
    descriptor.id = source.id;
    descriptor.name = source.name;
    descriptor.category = "annual surface embedding";
    descriptor.providerVersion = source.version;
    descriptor.attribution = source.attribution;
    descriptor.firstYear = source.firstYear;
    descriptor.lastYear = source.lastYear;
    descriptor.nativeResolutionMeters = source.nativeResolutionMeters;
    descriptor.componentCount = source.bandCount;
    descriptor.experimental = source.experimental;
    descriptor.health = available ? ScienceSourceHealth::Ready
                                  : ScienceSourceHealth::Unavailable;
    descriptor.healthMessage = healthMessage;
    descriptor.variables.reserve(65);
    for (int component = 0; component < 64; ++component)
    {
        char id[4] = {};
        std::snprintf(id, sizeof(id), "A%02d", component);
        descriptor.variables.push_back(
            {id, "Embedding " + std::string(id), "1", "embedding", 1});
    }
    descriptor.variables.push_back(
        {"embedding64", "Embedding A00-A63", "1", "embedding", 64});

    ScienceVisualizationDescriptor visualization;
    visualization.id = "false-color-a01-a16-a09";
    visualization.displayName = "False color A01/A16/A09";
    visualization.kind = ScienceVisualizationKind::FalseColor;
    visualization.channelVariables = {
        source.redBand, source.greenBand, source.blueBand};
    visualization.displayMinimum = source.displayMinimum;
    visualization.displayMaximum = source.displayMaximum;
    visualization.legend =
        "Embedding values; not temperature, vegetation, elevation, or natural color";
    descriptor.visualizations.push_back(std::move(visualization));

    descriptor.capabilities.pointQuery = true;
    descriptor.capabilities.boundingBoxQuery = true;
    descriptor.capabilities.currentViewQuery = true;
    descriptor.capabilities.explicitYears = true;
    descriptor.capabilities.rasterLayerOutput = true;
    descriptor.capabilities.embeddingOutput = true;
    descriptor.capabilities.timeSeriesOutput = true;
    descriptor.capabilities.analysisOutput = true;
    descriptor.capabilities.exportOutput = true;
    descriptor.capabilities.aggregation = true;
    descriptor.capabilities.minimumSpanMeters = 2560.0;
    descriptor.capabilities.maximumSpanMeters = 81920.0;
    return descriptor;
}

bool isAlphaEarthLegacyPreviewQuery(const GeoTemporalQuery& query)
{
    return query.sourceId == "alphaearth-foundations" &&
           query.geometry.kind == ScienceGeometryKind::Point &&
           query.time.mode == ScienceTimeMode::ExplicitYears &&
           query.time.explicitYears.size() == 1 &&
           query.variables ==
               std::vector<std::string>({"A01", "A16", "A09"}) &&
           query.aggregation == ScienceAggregation::None &&
           query.outputKind == ScienceOutputKind::RasterLayer &&
           query.visualizationId == "false-color-a01-a16-a09" &&
           query.analysis.kind == ScienceAnalysisKind::None;
}

ScienceProviderSnapshot translateAlphaEarthSnapshot(
    const AlphaEarthPreviewSnapshot& snapshot,
    const GeoTemporalQuery& query)
{
    ScienceProviderSnapshot translated;
    translated.generation = snapshot.generation;
    translated.state = translateState(snapshot.state);
    translated.progress = translateProgress(snapshot);
    translated.message = snapshot.message;
    if (snapshot.state != AlphaEarthPreviewState::Ready) return translated;

    auto artifact = std::make_shared<ScienceArtifact>();
    artifact->artifactId = "alphaearth-foundations-" +
        snapshot.artifact.datasetId + '-' +
        std::to_string(snapshot.artifact.year);
    artifact->query = query;
    artifact->generation = snapshot.artifact.generation;
    artifact->sourceReferences.push_back(
        sourceReference(snapshot.artifact, query));
    artifact->raster.bounds = {
        snapshot.artifact.west, snapshot.artifact.south,
        snapshot.artifact.east, snapshot.artifact.north};
    artifact->raster.width = snapshot.artifact.width;
    artifact->raster.height = snapshot.artifact.height;
    artifact->raster.sourceWindowWidth =
        snapshot.artifact.sourceWindowWidth;
    artifact->raster.sourceWindowHeight =
        snapshot.artifact.sourceWindowHeight;
    artifact->raster.sourceResolutionMeters =
        snapshot.artifact.sourceResolutionMeters;
    artifact->raster.displayResolutionMeters =
        snapshot.artifact.displayResolutionMeters;
    artifact->raster.rgba = snapshot.artifact.rgba;
    artifact->raster.groundGrid = snapshot.artifact.groundGrid;
    artifact->visualizationId = query.visualizationId;
    artifact->processingVersion = "alphaearth-preview-v2-mosaic";
    translated.artifact = std::move(artifact);
    return translated;
}

AlphaEarthProvider::AlphaEarthProvider(const std::string& indexPath)
    : _previewRuntime(new SciencePreviewRuntime(indexPath)),
      _embeddingRuntime(new AlphaEarthEmbeddingRuntime(indexPath))
{
}

AlphaEarthProvider::~AlphaEarthProvider() = default;

ScienceSourceDescriptor AlphaEarthProvider::descriptor() const
{
    const AlphaEarthPreviewSnapshot state = _previewRuntime->snapshot();
    ScienceSourceDescriptor result = describeAlphaEarth(
        _previewRuntime->source(), _previewRuntime->available(), state.message);
    const ScienceProviderSnapshot embeddingState =
        _embeddingRuntime->snapshot();
    const bool previewBusy = _activeRuntime == RuntimeKind::Preview &&
        (state.state == AlphaEarthPreviewState::Queued ||
         state.state == AlphaEarthPreviewState::Fetching);
    const bool embeddingBusy = _activeRuntime == RuntimeKind::Embedding64 &&
        (embeddingState.state == ScienceJobState::Queued ||
         embeddingState.state == ScienceJobState::Fetching);
    if (previewBusy || embeddingBusy)
        result.health = ScienceSourceHealth::Busy;
    return result;
}

bool AlphaEarthProvider::validateQuery(
    const GeoTemporalQuery& query, std::string& error) const
{
    if (query.sourceId != "alphaearth-foundations")
    {
        error = "AlphaEarth query source id changed";
        return false;
    }
    if (query.outputKind == ScienceOutputKind::RasterLayer &&
        !isAlphaEarthLegacyPreviewQuery(query))
    {
        error =
            "raster-layer output requires the exact AlphaEarth preview signature";
        return false;
    }
    error.clear();
    return true;
}

std::uint64_t AlphaEarthProvider::submit(const GeoTemporalQuery& query)
{
    if (query.sourceId != "alphaearth-foundations") return 0;

    _activeQuery = query;
    if (isAlphaEarthLegacyPreviewQuery(query))
    {
        if (_embeddingGeneration != 0)
            _embeddingRuntime->cancel(_embeddingGeneration);
        _activeRuntime = RuntimeKind::Preview;
        _activeGeneration = _previewRuntime->queryPoint(
            query.geometry.point.latitude, query.geometry.point.longitude,
            query.time.explicitYears.front(),
            query.geometry.requestedSpanMeters);
    }
    else
    {
        _previewRuntime->cancel();
        _activeRuntime = RuntimeKind::Embedding64;
        _embeddingGeneration = _embeddingRuntime->submit(query);
        _activeGeneration = _embeddingGeneration;
    }
    return _activeGeneration;
}

ScienceProviderSnapshot AlphaEarthProvider::snapshot() const
{
    if (_activeRuntime == RuntimeKind::Embedding64)
        return _embeddingRuntime->snapshot();

    const AlphaEarthPreviewSnapshot state = _previewRuntime->snapshot();
    if (_activeRuntime == RuntimeKind::Preview &&
        _activeGeneration != 0 && state.generation != _activeGeneration)
    {
        ScienceProviderSnapshot waiting;
        waiting.generation = _activeGeneration;
        waiting.state = ScienceJobState::Queued;
        waiting.progress.stage = ScienceProgressStage::Queued;
        waiting.message = "Waiting for AlphaEarth generation";
        return waiting;
    }
    return translateAlphaEarthSnapshot(state, _activeQuery);
}

void AlphaEarthProvider::cancel(std::uint64_t generation)
{
    if (generation != _activeGeneration) return;
    if (_activeRuntime == RuntimeKind::Preview)
        _previewRuntime->cancel();
    else if (_activeRuntime == RuntimeKind::Embedding64)
        _embeddingRuntime->cancel(generation);
}

void AlphaEarthProvider::clear()
{
    _previewRuntime->clear();
    if (_embeddingGeneration != 0)
        _embeddingRuntime->cancel(_embeddingGeneration);
    _activeRuntime = RuntimeKind::None;
    _activeGeneration = 0;
    _embeddingGeneration = 0;
    _activeQuery = GeoTemporalQuery();
}
}
