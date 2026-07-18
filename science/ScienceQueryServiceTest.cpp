#include "ScienceQueryService.h"

#include <cmath>
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

    struct ProviderEvents
    {
        int submits = 0;
        int clears = 0;
        std::vector<std::uint64_t> cancellations;
        bool destroyed = false;
    };

    earthscience::ScienceSourceDescriptor makeDescriptor(
        earthscience::ScienceSourceHealth health =
            earthscience::ScienceSourceHealth::Ready)
    {
        earthscience::ScienceSourceDescriptor source;
        source.id = "alphaearth-foundations";
        source.name = "AlphaEarth Foundations";
        source.category = "annual embedding";
        source.providerVersion = "1.1";
        source.attribution = "Google / Google DeepMind / source.coop";
        source.firstYear = 2017;
        source.lastYear = 2025;
        source.nativeResolutionMeters = 10.0;
        source.componentCount = 64;
        source.health = health;
        source.healthMessage = health ==
            earthscience::ScienceSourceHealth::Unavailable
                ? "compact index is missing" : "Ready";
        source.variables = {
            {"A01", "Embedding A01", "1", "embedding", 1},
            {"A16", "Embedding A16", "1", "embedding", 1},
            {"A09", "Embedding A09", "1", "embedding", 1},
            {"embedding64", "Embedding A00-A63", "1", "embedding", 64},
        };
        earthscience::ScienceVisualizationDescriptor visualization;
        visualization.id = "false-color-a01-a16-a09";
        visualization.displayName = "False color A01/A16/A09";
        visualization.kind =
            earthscience::ScienceVisualizationKind::FalseColor;
        visualization.channelVariables = {"A01", "A16", "A09"};
        source.visualizations.push_back(visualization);
        source.capabilities.pointQuery = true;
        source.capabilities.boundingBoxQuery = true;
        source.capabilities.currentViewQuery = true;
        source.capabilities.explicitYears = true;
        source.capabilities.rasterLayerOutput = true;
        source.capabilities.embeddingOutput = true;
        source.capabilities.timeSeriesOutput = true;
        source.capabilities.analysisOutput = true;
        source.capabilities.exportOutput = true;
        source.capabilities.minimumSpanMeters = 2560.0;
        source.capabilities.maximumSpanMeters = 81920.0;
        return source;
    }

    earthscience::ScienceSourceDescriptor makeSentinelDescriptor()
    {
        earthscience::ScienceSourceDescriptor source;
        source.id = "sentinel-2-l2a";
        source.name = "Sentinel-2 Level-2A";
        source.category = "optical imagery";
        source.providerVersion = "earth-search-v1";
        source.attribution = "Copernicus Sentinel data / Element 84 / AWS";
        source.firstYear = 2018;
        source.lastYear = 2026;
        source.nativeResolutionMeters = 10.0;
        source.componentCount = 3;
        source.health = earthscience::ScienceSourceHealth::Ready;
        source.healthMessage = "Ready on demand";
        source.variables = {
            {"visual", "True-color visual", "1", "display-rgb", 3},
        };
        earthscience::ScienceVisualizationDescriptor visualization;
        visualization.id = "natural-color-visual";
        visualization.displayName = "Sentinel-2 true color";
        visualization.kind =
            earthscience::ScienceVisualizationKind::NaturalColor;
        visualization.channelVariables = {"visual"};
        source.visualizations.push_back(visualization);
        source.capabilities.pointQuery = true;
        source.capabilities.intervalTime = true;
        source.capabilities.rasterLayerOutput = true;
        source.capabilities.minimumSpanMeters = 2560.0;
        source.capabilities.maximumSpanMeters = 81920.0;
        return source;
    }

    earthscience::GeoTemporalQuery makeSentinelQuery()
    {
        earthscience::GeoTemporalQuery query;
        query.sourceId = "sentinel-2-l2a";
        query.geometry.kind = earthscience::ScienceGeometryKind::Point;
        query.geometry.point.latitude = 35.68;
        query.geometry.point.longitude = 139.76;
        query.geometry.requestedSpanMeters = 10000.0;
        query.time.mode = earthscience::ScienceTimeMode::Interval;
        query.time.intervalStart = "2026-06-18T00:00:00Z";
        query.time.intervalEnd = "2026-07-18T23:59:59Z";
        query.variables = {"visual"};
        query.targetResolutionMeters = 10.0;
        query.outputKind = earthscience::ScienceOutputKind::RasterLayer;
        query.visualizationId = "natural-color-visual";
        query.sceneFilters.maximumCloudCoverPercent = 20.0;
        query.sceneFilters.maximumScenes = 10;
        return query;
    }

    earthscience::GeoTemporalQuery makeQuery()
    {
        earthscience::GeoTemporalQuery query;
        query.sourceId = "alphaearth-foundations";
        query.geometry.kind = earthscience::ScienceGeometryKind::Point;
        query.geometry.point.latitude = 35.36;
        query.geometry.point.longitude = 138.73;
        query.geometry.requestedSpanMeters = 20000.0;
        query.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        query.time.explicitYears = {2025};
        query.variables = {"A01", "A16", "A09"};
        query.targetResolutionMeters = 10.0;
        query.aggregation = earthscience::ScienceAggregation::None;
        query.outputKind = earthscience::ScienceOutputKind::RasterLayer;
        query.priority = earthscience::SciencePriority::Visible;
        query.visualizationId = "false-color-a01-a16-a09";
        return query;
    }

    earthscience::GeoTemporalQuery makeAnalysisQuery()
    {
        earthscience::GeoTemporalQuery query = makeQuery();
        query.geometry.kind = earthscience::ScienceGeometryKind::BoundingBox;
        query.geometry.bounds = {138.60, 35.20, 138.86, 35.46};
        query.time.explicitYears = {2017, 2018};
        query.variables = {"embedding64"};
        query.outputKind = earthscience::ScienceOutputKind::Analysis;
        query.priority = earthscience::SciencePriority::InteractiveResearch;
        query.visualizationId.clear();
        query.analysis.kind =
            earthscience::ScienceAnalysisKind::RegionalChange;
        query.analysis.baselineYear = 2017;
        query.analysis.comparisonYear = 2018;
        query.analysis.gridSize = 4;
        return query;
    }

    earthscience::GeoTemporalQuery makePointSeriesQuery(
        int firstYear, int lastYear)
    {
        earthscience::GeoTemporalQuery query = makeQuery();
        query.geometry.requestedSpanMeters = 0.0;
        query.time.explicitYears.clear();
        for (int year = firstYear; year <= lastYear; ++year)
            query.time.explicitYears.push_back(year);
        query.variables = {"embedding64"};
        query.outputKind = earthscience::ScienceOutputKind::TimeSeries;
        query.priority =
            earthscience::SciencePriority::InteractiveResearch;
        query.visualizationId.clear();
        query.analysis.kind =
            earthscience::ScienceAnalysisKind::PointSeries;
        query.analysis.baselineYear = firstYear;
        query.analysis.comparisonYear = lastYear;
        query.analysis.gridSize = 0;
        return query;
    }

    std::shared_ptr<const earthscience::ScienceArtifact> makeArtifact(
        const std::string& id, std::uint64_t providerGeneration)
    {
        auto artifact = std::make_shared<earthscience::ScienceArtifact>();
        artifact->artifactId = id;
        artifact->generation = providerGeneration;
        artifact->raster.width = 256;
        artifact->raster.height = 256;
        return artifact;
    }

    earthscience::ScienceProgress makeProgress(
        earthscience::ScienceProgressStage stage,
        std::uint64_t completedUnits = 0, std::uint64_t totalUnits = 0,
        const std::string& unit = std::string(),
        double elapsedSeconds = 0.0)
    {
        earthscience::ScienceProgress progress;
        progress.stage = stage;
        progress.completedUnits = completedUnits;
        progress.totalUnits = totalUnits;
        progress.determinate = totalUnits != 0;
        progress.unit = unit;
        progress.elapsedSeconds = elapsedSeconds;
        return progress;
    }

    class ControlledProvider : public earthscience::IScienceProvider
    {
    public:
        ControlledProvider(earthscience::ScienceSourceDescriptor descriptor,
                           ProviderEvents* events)
            : _descriptor(std::move(descriptor)), _events(events)
        {
            _snapshot.state = earthscience::ScienceJobState::Idle;
        }

        ~ControlledProvider() override
        {
            if (_events) _events->destroyed = true;
        }

        earthscience::ScienceSourceDescriptor descriptor() const override
        {
            return _descriptor;
        }

        std::uint64_t submit(
            const earthscience::GeoTemporalQuery&) override
        {
            if (_events) ++_events->submits;
            _snapshot = earthscience::ScienceProviderSnapshot();
            _snapshot.generation = ++_generation;
            _snapshot.state = earthscience::ScienceJobState::Queued;
            _snapshot.message = "Queued";
            return _generation;
        }

        earthscience::ScienceProviderSnapshot snapshot() const override
        {
            return _snapshot;
        }

        void cancel(std::uint64_t generation) override
        {
            if (_events) _events->cancellations.push_back(generation);
            if (generation == _snapshot.generation)
            {
                _snapshot.state = earthscience::ScienceJobState::Cancelled;
                _snapshot.message = "Cancelled";
            }
        }

        void clear() override
        {
            if (_events) ++_events->clears;
            _snapshot = earthscience::ScienceProviderSnapshot();
        }

        void publish(std::uint64_t generation,
                     earthscience::ScienceJobState state,
                     const earthscience::ScienceProgress& progress,
                     const std::string& message,
                     std::shared_ptr<const earthscience::ScienceArtifact>
                         artifact = nullptr)
        {
            _snapshot.generation = generation;
            _snapshot.state = state;
            _snapshot.progress = progress;
            _snapshot.message = message;
            _snapshot.artifact = std::move(artifact);
        }

        std::uint64_t generation() const { return _generation; }

    private:
        earthscience::ScienceSourceDescriptor _descriptor;
        ProviderEvents* _events = nullptr;
        std::uint64_t _generation = 0;
        earthscience::ScienceProviderSnapshot _snapshot;
    };

    struct ServiceFixture
    {
        ProviderEvents events;
        ControlledProvider* provider = nullptr;
        std::unique_ptr<earthscience::ScienceQueryService> service;

        explicit ServiceFixture(
            earthscience::ScienceSourceHealth health =
                earthscience::ScienceSourceHealth::Ready)
        {
            auto registry =
                std::make_unique<earthscience::ScienceSourceRegistry>();
            auto ownedProvider = std::make_unique<ControlledProvider>(
                makeDescriptor(health), &events);
            provider = ownedProvider.get();
            std::string error;
            require(registry->add(std::move(ownedProvider), error),
                    "test provider registration failed");
            service = std::make_unique<earthscience::ScienceQueryService>(
                std::move(registry));
        }
    };

    void requireRejectedWithoutDispatch(
        earthscience::ScienceQueryService& service,
        ControlledProvider& provider,
        const earthscience::GeoTemporalQuery& query,
        const std::string& expectedMessage)
    {
        const std::uint64_t before = provider.generation();
        const std::uint64_t jobId = service.submit(query);
        const earthscience::ScienceJobSnapshot snapshot = service.snapshot();
        require(jobId != 0 && snapshot.jobId == jobId,
                "rejected query did not receive a traceable service job id");
        require(snapshot.state == earthscience::ScienceJobState::Failed,
                "invalid query did not fail synchronously");
        require(snapshot.message == expectedMessage,
                "invalid query did not report the precise field failure");
        require(provider.generation() == before,
                "invalid query reached the provider");
    }

    void testRejectsUnknownUnavailableAndUnsupportedQueries()
    {
        ServiceFixture fixture;
        earthscience::GeoTemporalQuery query = makeQuery();
        query.sourceId = "missing";
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "unknown science source: missing");

        query = makeQuery();
        query.geometry.kind = earthscience::ScienceGeometryKind::BoundingBox;
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "unsupported science geometry: bounding-box");

        query = makeQuery();
        query.geometry.point.latitude = std::nan("");
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "latitude must be finite and inside [-90, 90]");

        query = makeQuery();
        query.geometry.point.longitude = 181.0;
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "longitude must be finite and inside [-180, 180]");

        query = makeQuery();
        query.geometry.requestedSpanMeters = 1000.0;
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "requested span must be inside [2560, 81920] meters");

        query = makeQuery();
        query.time.mode = earthscience::ScienceTimeMode::Interval;
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "unsupported science time selection: interval");

        query = makeQuery();
        query.time.explicitYears = {2018, 2025};
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "science query requires exactly one explicit year");

        query = makeQuery();
        query.time.explicitYears = {2016};
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "science year must be inside [2017, 2025]");

        query = makeQuery();
        query.visualizationId = "natural-color";
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "unknown science visualization: natural-color");

        query = makeQuery();
        query.variables = {"A01"};
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "science variables do not match visualization: "
            "false-color-a01-a16-a09");

        query = makeQuery();
        query.aggregation = earthscience::ScienceAggregation::Mean;
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "unsupported science aggregation: mean");

        query = makeQuery();
        query.outputKind = earthscience::ScienceOutputKind::Table;
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "unsupported science output: table");

        ServiceFixture unavailable(
            earthscience::ScienceSourceHealth::Unavailable);
        requireRejectedWithoutDispatch(
            *unavailable.service, *unavailable.provider, makeQuery(),
            "science source unavailable: compact index is missing");
        require(unavailable.service->listSources().size() == 1,
                "unavailable source disappeared from the catalog");
    }

    void testReplacementRejectsStaleResultsAndOldCancellation()
    {
        ServiceFixture fixture;
        const std::uint64_t firstJob = fixture.service->submit(makeQuery());
        const std::uint64_t firstGeneration = fixture.provider->generation();
        fixture.provider->publish(firstGeneration,
            earthscience::ScienceJobState::Fetching,
            makeProgress(earthscience::ScienceProgressStage::Reading,
                         1, 4, "tiles"),
            "Fetching first");
        const earthscience::ScienceJobSnapshot firstProgress =
            fixture.service->snapshot();
        require(firstProgress.state == earthscience::ScienceJobState::Fetching,
                "provider progress was not reconciled");
        require(firstProgress.progress.determinate &&
                    firstProgress.progress.completedUnits == 1 &&
                    firstProgress.progress.totalUnits == 4 &&
                    firstProgress.progress.unit == "tiles" &&
                    firstProgress.progress.legacyFraction() == 0.25f,
                "measurable provider progress lost its evidence");

        earthscience::GeoTemporalQuery secondQuery = makeQuery();
        secondQuery.time.explicitYears = {2018};
        const std::uint64_t secondJob = fixture.service->submit(secondQuery);
        const std::uint64_t secondGeneration = fixture.provider->generation();
        require(secondJob > firstJob,
                "service job ids are not monotonically increasing");
        require(fixture.events.cancellations.size() == 1 &&
                    fixture.events.cancellations[0] == firstGeneration,
                "replacement did not cancel the previous provider generation");

        fixture.provider->publish(firstGeneration,
            earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 1, 1,
                         "artifact"),
            "stale",
            makeArtifact("stale", firstGeneration));
        earthscience::ScienceJobSnapshot snapshot = fixture.service->snapshot();
        require(snapshot.jobId == secondJob &&
                    snapshot.state == earthscience::ScienceJobState::Queued,
                "stale provider completion changed the current service job");
        require(!snapshot.lastSuccessfulArtifact,
                "stale provider completion published an artifact");
        require(!fixture.service->findArtifact("stale"),
                "stale provider completion entered the artifact store");

        fixture.service->cancel(firstJob);
        require(fixture.events.cancellations.size() == 1,
                "old job id cancelled the newer provider generation");

        fixture.provider->publish(secondGeneration,
            earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 1, 1,
                         "artifact"),
            "Ready",
            makeArtifact("current", secondGeneration));
        snapshot = fixture.service->snapshot();
        require(snapshot.state == earthscience::ScienceJobState::Ready &&
                    snapshot.lastSuccessfulArtifact &&
                    snapshot.lastSuccessfulArtifact->artifactId == "current",
                "matching provider completion did not publish its artifact");
        require(snapshot.lastSuccessfulArtifact->generation == secondJob,
                "published artifact did not use the service generation");
    }

    void testRetainsLastGoodAcrossFetchingFailureAndCancellation()
    {
        ServiceFixture fixture;
        const std::uint64_t firstJob = fixture.service->submit(makeQuery());
        const std::uint64_t firstGeneration = fixture.provider->generation();
        fixture.provider->publish(firstGeneration,
            earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 1, 1,
                         "artifact"),
            "Ready",
            makeArtifact("last-good", firstGeneration));
        require(fixture.service->snapshot().lastSuccessfulArtifact != nullptr,
                "initial successful artifact was not retained");

        earthscience::GeoTemporalQuery replacement = makeQuery();
        replacement.time.explicitYears = {2018};
        const std::uint64_t secondJob = fixture.service->submit(replacement);
        const std::uint64_t secondGeneration = fixture.provider->generation();
        fixture.provider->publish(secondGeneration,
            earthscience::ScienceJobState::Fetching,
            makeProgress(earthscience::ScienceProgressStage::Decoding),
            "Fetching");
        earthscience::ScienceJobSnapshot snapshot = fixture.service->snapshot();
        require(snapshot.state == earthscience::ScienceJobState::Fetching &&
                    !snapshot.progress.determinate &&
                    snapshot.progress.legacyFraction() == 0.0f &&
                    snapshot.lastSuccessfulArtifact &&
                    snapshot.lastSuccessfulArtifact->artifactId == "last-good",
                "replacement fetch erased the last successful artifact");

        fixture.provider->publish(secondGeneration,
            earthscience::ScienceJobState::Failed,
            makeProgress(earthscience::ScienceProgressStage::Failed),
            "controlled failure");
        snapshot = fixture.service->snapshot();
        require(snapshot.state == earthscience::ScienceJobState::Failed &&
                    snapshot.lastSuccessfulArtifact &&
                    snapshot.lastSuccessfulArtifact->artifactId == "last-good",
                "replacement failure erased the last successful artifact");

        const std::uint64_t thirdJob = fixture.service->submit(makeQuery());
        fixture.service->cancel(thirdJob);
        snapshot = fixture.service->snapshot();
        require(snapshot.state == earthscience::ScienceJobState::Cancelled &&
                    snapshot.lastSuccessfulArtifact &&
                    snapshot.lastSuccessfulArtifact->artifactId == "last-good",
                "replacement cancellation erased the last successful artifact");
        require(secondJob < thirdJob && firstJob < secondJob,
                "accepted service job ids stopped increasing");

        fixture.service->clearArtifact();
        snapshot = fixture.service->snapshot();
        require(!snapshot.lastSuccessfulArtifact &&
                    !snapshot.lastSuccessfulPreviewArtifact &&
                    !snapshot.lastSuccessfulAnalysisArtifact &&
                    !snapshot.displayArtifact,
                "compatibility artifact clear did not remove all results");
        require(fixture.events.clears == 0,
                "artifact clear altered provider transient state");
    }

    void testDestructionCancelsBeforeProviderDestruction()
    {
        ProviderEvents events;
        {
            auto registry =
                std::make_unique<earthscience::ScienceSourceRegistry>();
            auto provider = std::make_unique<ControlledProvider>(
                makeDescriptor(), &events);
            std::string error;
            require(registry->add(std::move(provider), error),
                    "shutdown provider registration failed");
            earthscience::ScienceQueryService service(std::move(registry));
            service.submit(makeQuery());
        }
        require(events.cancellations.size() == 1 &&
                    events.cancellations[0] == 1,
                "service destruction did not cancel the active generation");
        require(events.destroyed,
                "service destruction did not release provider ownership");
    }

    void testRetainsPreviewAnalysisAndDisplayIndependently()
    {
        ServiceFixture fixture;
        fixture.service->submit(makeQuery());
        const std::uint64_t previewGeneration = fixture.provider->generation();
        fixture.provider->publish(
            previewGeneration, earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 1, 1,
                         "artifact"),
            "Ready", makeArtifact("preview", previewGeneration));
        earthscience::ScienceJobSnapshot snapshot = fixture.service->snapshot();
        require(snapshot.lastSuccessfulPreviewArtifact &&
                    snapshot.lastSuccessfulPreviewArtifact->artifactId ==
                        "preview" &&
                    !snapshot.lastSuccessfulAnalysisArtifact &&
                    snapshot.displayArtifact ==
                        snapshot.lastSuccessfulPreviewArtifact &&
                    snapshot.lastSuccessfulArtifact == snapshot.displayArtifact,
                "preview success did not become last-good and display");
        require(fixture.service->findArtifact("preview") ==
                    snapshot.displayArtifact,
                "preview success was not retained in the artifact store");

        fixture.service->submit(makeAnalysisQuery());
        const std::uint64_t analysisGeneration = fixture.provider->generation();
        fixture.provider->publish(
            analysisGeneration, earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 1, 1,
                         "artifact"),
            "Ready", makeArtifact("analysis", analysisGeneration));
        snapshot = fixture.service->snapshot();
        require(snapshot.lastSuccessfulPreviewArtifact &&
                    snapshot.lastSuccessfulPreviewArtifact->artifactId ==
                        "preview" &&
                    snapshot.lastSuccessfulAnalysisArtifact &&
                    snapshot.lastSuccessfulAnalysisArtifact->artifactId ==
                        "analysis" &&
                    snapshot.displayArtifact &&
                    snapshot.displayArtifact->artifactId == "preview",
                "analysis success changed preview or display retention");

        require(fixture.service->showArtifact("analysis"),
                "stored analysis could not be selected for display");
        snapshot = fixture.service->snapshot();
        require(snapshot.displayArtifact &&
                    snapshot.displayArtifact->artifactId == "analysis" &&
                    snapshot.lastSuccessfulArtifact == snapshot.displayArtifact &&
                    snapshot.lastSuccessfulPreviewArtifact->artifactId ==
                        "preview" &&
                    snapshot.lastSuccessfulAnalysisArtifact->artifactId ==
                        "analysis",
                "showArtifact changed last-good state or missed display");
        require(!fixture.service->showArtifact("missing") &&
                    fixture.service->snapshot().displayArtifact ==
                        snapshot.displayArtifact,
                "unknown showArtifact request changed display");

        fixture.service->submit(makeQuery());
        const std::uint64_t failedGeneration = fixture.provider->generation();
        fixture.provider->publish(
            failedGeneration, earthscience::ScienceJobState::Failed,
            makeProgress(earthscience::ScienceProgressStage::Failed),
            "replacement failure");
        snapshot = fixture.service->snapshot();
        require(snapshot.lastSuccessfulPreviewArtifact->artifactId ==
                    "preview" &&
                    snapshot.lastSuccessfulAnalysisArtifact->artifactId ==
                        "analysis" &&
                    snapshot.displayArtifact->artifactId == "analysis",
                "replacement failure erased independent retained state");

        fixture.service->clearArtifacts();
        snapshot = fixture.service->snapshot();
        require(!snapshot.lastSuccessfulPreviewArtifact &&
                    !snapshot.lastSuccessfulAnalysisArtifact &&
                    !snapshot.displayArtifact &&
                    !snapshot.lastSuccessfulArtifact &&
                    !fixture.service->findArtifact("preview") &&
                    !fixture.service->findArtifact("analysis"),
                "clearArtifacts did not remove every retained artifact");
    }

    void testEstimatesExactCostAndRequiresEvidenceForDuration()
    {
        ServiceFixture fixture;
        earthscience::GeoTemporalQuery query = makeAnalysisQuery();
        const earthscience::ScienceQueryCost before =
            fixture.service->estimate(query);
        require(before.resultCells == 32 &&
                    before.sourceBytesUpperBound > 2048 &&
                    before.residentBytesUpperBound == 43456,
                "analysis cell or memory upper bound is not exact");
        require(!before.durationDeterminate &&
                    before.estimatedDurationSeconds == 0.0,
                "cost estimate fabricated duration before throughput evidence");

        fixture.service->submit(query);
        const std::uint64_t generation = fixture.provider->generation();
        fixture.provider->publish(
            generation, earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 1, 1,
                         "artifact", 2.0),
            "Ready", makeArtifact("timed-analysis", generation));
        fixture.service->snapshot();
        const earthscience::ScienceQueryCost after =
            fixture.service->estimate(query);
        require(after.durationDeterminate &&
                    std::abs(after.estimatedDurationSeconds - 2.0) < 1.0e-9,
                "successful provider throughput did not enable duration");
    }

    void testPreviewThroughputDoesNotFabricateAnalysisDuration()
    {
        ServiceFixture fixture;
        fixture.service->submit(makeQuery());
        const std::uint64_t generation = fixture.provider->generation();
        fixture.provider->publish(
            generation, earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 1, 1,
                         "artifact", 1.0),
            "Ready", makeArtifact("timed-preview", generation));
        fixture.service->snapshot();

        const earthscience::ScienceQueryCost analysis =
            fixture.service->estimate(makeAnalysisQuery());
        require(!analysis.durationDeterminate &&
                    analysis.estimatedDurationSeconds == 0.0,
                "preview throughput fabricated a 64D analysis duration");
    }

    void testThroughputEvidenceIsolatedByPhysicalWorkload()
    {
        ServiceFixture fixture;
        earthscience::GeoTemporalQuery point =
            makePointSeriesQuery(2017, 2025);
        fixture.service->submit(point);
        const std::uint64_t generation = fixture.provider->generation();
        fixture.provider->publish(
            generation, earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 9, 9,
                         "year", 55.0),
            "Ready", makeArtifact("timed-point-series", generation));
        fixture.service->snapshot();

        const earthscience::ScienceQueryCost samePoint =
            fixture.service->estimate(point);
        require(samePoint.durationDeterminate &&
                    std::abs(samePoint.estimatedDurationSeconds - 55.0) <
                        1.0e-9,
                "same point workload lost observed duration evidence");
        point.limits.maximumDurationSeconds = 54.0;
        const int submitsBeforeBudget = fixture.events.submits;
        fixture.service->submit(point);
        const earthscience::ScienceJobSnapshot rejected =
            fixture.service->snapshot();
        require(rejected.state == earthscience::ScienceJobState::Failed &&
                    rejected.message ==
                        "analysis exceeds maximum duration" &&
                    fixture.events.submits == submitsBeforeBudget,
                "same-workload duration budget gate was weakened");

        earthscience::GeoTemporalQuery regional = makeAnalysisQuery();
        regional.analysis.gridSize = 128;
        regional.limits.maximumDurationSeconds = 240.0;
        const earthscience::ScienceQueryCost regionalCost =
            fixture.service->estimate(regional);
        require(!regionalCost.durationDeterminate &&
                    regionalCost.estimatedDurationSeconds == 0.0,
                "point throughput fabricated regional duration");
        const int submitsBeforeRegional = fixture.events.submits;
        fixture.service->submit(regional);
        require(fixture.events.submits == submitsBeforeRegional + 1 &&
                    fixture.service->snapshot().state ==
                        earthscience::ScienceJobState::Queued,
                "fabricated point duration blocked regional dispatch");

        const earthscience::ScienceQueryCost twoYearPoint =
            fixture.service->estimate(makePointSeriesQuery(2017, 2018));
        require(!twoYearPoint.durationDeterminate &&
                    twoYearPoint.estimatedDurationSeconds == 0.0,
                "nine-year point throughput polluted two-year point work");
    }

    void testProviderPreviewCostsAndArtifactsRemainIsolated()
    {
        ProviderEvents alphaEvents, otherEvents;
        auto registry =
            std::make_unique<earthscience::ScienceSourceRegistry>();
        auto alpha = std::make_unique<ControlledProvider>(
            makeDescriptor(), &alphaEvents);
        ControlledProvider* alphaPointer = alpha.get();
        earthscience::ScienceSourceDescriptor otherDescriptor =
            makeDescriptor();
        otherDescriptor.id = "other-foundations";
        auto other = std::make_unique<ControlledProvider>(
            otherDescriptor, &otherEvents);
        ControlledProvider* otherPointer = other.get();
        std::string error;
        require(registry->add(std::move(alpha), error) &&
                    registry->add(std::move(other), error),
                "two-provider fixture registration failed");
        earthscience::ScienceQueryService service(std::move(registry));

        service.submit(makeQuery());
        const std::uint64_t generation = alphaPointer->generation();
        alphaPointer->publish(
            generation, earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 1, 1,
                         "artifact", 1.0),
            "Ready", makeArtifact("alpha-preview", generation));
        const earthscience::ScienceJobSnapshot alphaReady =
            service.snapshot();
        require(alphaReady.lastSuccessfulPreviewArtifact &&
                    alphaReady.lastSuccessfulPreviewArtifact->artifactId ==
                        "alpha-preview",
                "AlphaEarth preview fixture did not become last preview");

        earthscience::GeoTemporalQuery impostor = makeQuery();
        impostor.sourceId = "other-foundations";
        const earthscience::ScienceQueryCost impostorCost =
            service.estimate(impostor);
        require(impostorCost.resultCells == 256u * 256u &&
                    !impostorCost.durationDeterminate,
                "another provider lost raster cost or inherited AlphaEarth timing");

        service.submit(impostor);
        const earthscience::ScienceJobSnapshot queued = service.snapshot();
        require(queued.state == earthscience::ScienceJobState::Queued &&
                    otherEvents.submits == 1 &&
                    queued.lastSuccessfulPreviewArtifact ==
                        alphaReady.lastSuccessfulPreviewArtifact &&
                    queued.displayArtifact == alphaReady.displayArtifact,
                "new provider dispatch discarded the retained AlphaEarth preview");

        const std::uint64_t otherGeneration = otherPointer->generation();
        otherPointer->publish(
            otherGeneration, earthscience::ScienceJobState::Ready,
            makeProgress(earthscience::ScienceProgressStage::Ready, 1, 1,
                         "artifact", 2.0),
            "Ready", makeArtifact("other-preview", otherGeneration));
        const earthscience::ScienceJobSnapshot otherReady = service.snapshot();
        require(otherReady.lastSuccessfulPreviewArtifact &&
                    otherReady.lastSuccessfulPreviewArtifact->artifactId ==
                        "other-preview" &&
                    otherReady.displayArtifact ==
                        otherReady.lastSuccessfulPreviewArtifact,
                "ready provider preview did not replace the displayed artifact");
    }

    void testDispatchesBoundedIntervalRasterForCapableProvider()
    {
        ProviderEvents events;
        auto registry =
            std::make_unique<earthscience::ScienceSourceRegistry>();
        auto ownedProvider = std::make_unique<ControlledProvider>(
            makeSentinelDescriptor(), &events);
        ControlledProvider* provider = ownedProvider.get();
        std::string error;
        require(registry->add(std::move(ownedProvider), error),
                "Sentinel provider fixture registration failed");
        earthscience::ScienceQueryService service(std::move(registry));

        const earthscience::GeoTemporalQuery query = makeSentinelQuery();
        service.submit(query);
        require(events.submits == 1 && provider->generation() == 1 &&
                    service.snapshot().state ==
                        earthscience::ScienceJobState::Queued,
                "valid interval raster query did not dispatch");
        const earthscience::ScienceQueryCost cost = service.estimate(query);
        require(cost.resultCells == 256u * 256u &&
                    cost.sourceBytesUpperBound > 0,
                "Sentinel raster query lost its bounded cost");

        earthscience::GeoTemporalQuery invalid = query;
        invalid.time.intervalStart = "2026-08-01T00:00:00Z";
        requireRejectedWithoutDispatch(
            service, *provider, invalid,
            "science interval must be non-empty and ordered");
        invalid = query;
        invalid.sceneFilters.maximumCloudCoverPercent = 101.0;
        requireRejectedWithoutDispatch(
            service, *provider, invalid,
            "scene cloud cover must be inside [0, 100]");
        invalid = query;
        invalid.sceneFilters.maximumScenes = 11;
        requireRejectedWithoutDispatch(
            service, *provider, invalid,
            "scene candidate limit must be inside [1, 10]");
        invalid = query;
        invalid.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        invalid.time.explicitYears = {2026};
        requireRejectedWithoutDispatch(
            service, *provider, invalid,
            "science source does not support explicit years");
    }

    void testRequiresConfirmationAndEnforcesEstimatedBudgets()
    {
        ServiceFixture fixture;
        earthscience::GeoTemporalQuery query = makeAnalysisQuery();
        query.analysis.gridSize = 256;
        query.analysis.confirmedLargeRequest = false;
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "256x256 analysis requires explicit confirmation");

        query.analysis.confirmedLargeRequest = true;
        query.limits.maximumMemoryBytes = 1024;
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "analysis exceeds maximum memory");

        query = makeAnalysisQuery();
        query.time.explicitYears = {2017, 2018, 2019};
        query.analysis.confirmedLargeRequest = false;
        requireRejectedWithoutDispatch(
            *fixture.service, *fixture.provider, query,
            "multi-year regional analysis requires explicit confirmation");
    }
}

int main()
{
    testRejectsUnknownUnavailableAndUnsupportedQueries();
    testReplacementRejectsStaleResultsAndOldCancellation();
    testRetainsLastGoodAcrossFetchingFailureAndCancellation();
    testRetainsPreviewAnalysisAndDisplayIndependently();
    testEstimatesExactCostAndRequiresEvidenceForDuration();
    testPreviewThroughputDoesNotFabricateAnalysisDuration();
    testThroughputEvidenceIsolatedByPhysicalWorkload();
    testDispatchesBoundedIntervalRasterForCapableProvider();
    testProviderPreviewCostsAndArtifactsRemainIsolated();
    testRequiresConfirmationAndEnforcesEstimatedBudgets();
    testDestructionCancelsBeforeProviderDestruction();
    std::cout << "[OK] ScienceEarth single-active-job query service\n";
    return 0;
}
