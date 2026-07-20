#include "ScienceResearchBrief.h"
#include "ScienceResearchManager.h"
#include "ScienceResearchSequencer.h"
#include "ScienceQueryService.h"
#include "ScienceSourceRegistry.h"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unistd.h>

namespace
{
    void check(bool condition, const char* expression,
               const char* file, int line)
    {
        if (condition) return;
        throw std::runtime_error(
            std::string("CHECK failed at ") + file + ":" +
            std::to_string(line) + ": " + expression);
    }

#define CHECK(value) check((value), #value, __FILE__, __LINE__)

    class TempDirectory
    {
    public:
        TempDirectory()
        {
            std::string pattern =
                (std::filesystem::temp_directory_path() /
                 "osgsol-research-sequencer-XXXXXX").string();
            std::vector<char> writable(pattern.begin(), pattern.end());
            writable.push_back('\0');
            char* created = mkdtemp(writable.data());
            if (!created) throw std::runtime_error("mkdtemp failed");
            _path = created;
        }

        ~TempDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(_path, ignored);
        }

        const std::string& path() const { return _path; }

    private:
        std::string _path;
    };

    earthscience::ScienceSourceDescriptor descriptor(
        const std::string& id)
    {
        earthscience::ScienceSourceDescriptor source;
        source.id = id;
        source.name = id;
        source.category = "test";
        source.providerVersion = id + "-v1";
        source.attribution = id + " attribution";
        source.firstYear = 2017;
        source.lastYear = 2025;
        source.nativeResolutionMeters = 10.0;
        source.health = earthscience::ScienceSourceHealth::Ready;
        source.variables = {{"value", "Value", "unit", "scalar", 1, 1}};
        earthscience::ScienceVisualizationDescriptor visualization;
        visualization.id = "preview";
        visualization.channelVariables = {"value"};
        source.visualizations.push_back(visualization);
        source.capabilities.pointQuery = true;
        source.capabilities.explicitYears = true;
        source.capabilities.rasterLayerOutput = true;
        source.capabilities.minimumSpanMeters = 100.0;
        source.capabilities.maximumSpanMeters = 100000.0;
        return source;
    }

    class SequencerProvider : public earthscience::IScienceProvider
    {
    public:
        SequencerProvider(std::string id, std::vector<std::string>* order)
            : _source(::descriptor(id)), _order(order) {}

        earthscience::ScienceSourceDescriptor descriptor() const override
        {
            return _source;
        }

        std::uint64_t submit(
            const earthscience::GeoTemporalQuery& query) override
        {
            _query = query;
            _snapshot = earthscience::ScienceProviderSnapshot();
            _snapshot.generation = ++_generation;
            _snapshot.state = earthscience::ScienceJobState::Queued;
            _snapshot.progress.stage =
                earthscience::ScienceProgressStage::Queued;
            _snapshot.message = "Queued";
            if (_order) _order->push_back(_source.id);
            return _generation;
        }

        earthscience::ScienceProviderSnapshot snapshot() const override
        {
            return _snapshot;
        }

        void cancel(std::uint64_t generation) override
        {
            if (generation != _generation) return;
            ++cancelCount;
            _snapshot.state = earthscience::ScienceJobState::Cancelled;
            _snapshot.progress.stage =
                earthscience::ScienceProgressStage::Cancelled;
            _snapshot.message = "Cancelled";
        }

        void clear() override
        {
            _snapshot = earthscience::ScienceProviderSnapshot();
        }

        void publishFetching()
        {
            _snapshot.state = earthscience::ScienceJobState::Fetching;
            _snapshot.progress.stage =
                earthscience::ScienceProgressStage::Reading;
            _snapshot.message = "Reading";
        }

        void publishAnalyzing()
        {
            _snapshot.state = earthscience::ScienceJobState::Fetching;
            _snapshot.progress.stage =
                earthscience::ScienceProgressStage::Analyzing;
            _snapshot.message = "Analyzing";
        }

        void publishFailed(const std::string& message)
        {
            _snapshot.state = earthscience::ScienceJobState::Failed;
            _snapshot.progress.stage =
                earthscience::ScienceProgressStage::Failed;
            _snapshot.message = message;
        }

        void publishReady()
        {
            auto artifact = std::make_shared<earthscience::ScienceArtifact>();
            artifact->artifactId = _source.id + "-" +
                std::to_string(_generation);
            artifact->query = _query;
            artifact->createdAt = "2026-07-20T00:00:00Z";
            artifact->processingVersion = "sequencer-test-v1";
            artifact->raster.bounds = {139.0, 35.0, 140.0, 36.0};
            artifact->raster.sourceResolutionMeters = 10.0;
            artifact->raster.displayResolutionMeters = 40.0;
            earthscience::ScienceSourceReference reference;
            reference.sourceId = _source.id;
            reference.providerVersion = _source.providerVersion;
            reference.datasetId = _source.id + "-dataset";
            reference.originalUrl = "https://example.invalid/" + _source.id;
            reference.requestedCoverage = _query.geometry;
            reference.actualCoverage = artifact->raster.bounds;
            reference.variables = _query.variables;
            reference.units = {"unit"};
            reference.processingSteps = {"deterministic fixture"};
            reference.attribution = _source.attribution;
            artifact->sourceReferences.push_back(std::move(reference));
            _snapshot.state = earthscience::ScienceJobState::Ready;
            _snapshot.progress.stage =
                earthscience::ScienceProgressStage::Ready;
            _snapshot.message = "Ready";
            _snapshot.artifact = std::move(artifact);
        }

        std::uint64_t generation() const { return _generation; }

        int cancelCount = 0;

    private:
        earthscience::ScienceSourceDescriptor _source;
        std::vector<std::string>* _order = nullptr;
        earthscience::GeoTemporalQuery _query;
        std::uint64_t _generation = 0;
        earthscience::ScienceProviderSnapshot _snapshot;
    };

    earthscience::GeoTemporalQuery query(const std::string& sourceId,
                                         int year = 2025)
    {
        earthscience::GeoTemporalQuery value;
        value.sourceId = sourceId;
        value.geometry.kind = earthscience::ScienceGeometryKind::Point;
        value.geometry.point = {35.68, 139.76};
        value.geometry.requestedSpanMeters = 10000.0;
        value.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        value.time.explicitYears = {year};
        value.variables = {"value"};
        value.targetResolutionMeters = 10.0;
        value.outputKind = earthscience::ScienceOutputKind::RasterLayer;
        value.visualizationId = "preview";
        return value;
    }

    earthscience::ScienceResearchRequestStep step(
        const std::string& sourceId, int year = 2025)
    {
        return {sourceId, query(sourceId, year)};
    }

    struct Fixture
    {
        Fixture()
        {
            auto sourceRegistry =
                std::make_unique<earthscience::ScienceSourceRegistry>();
            auto alphaValue = std::make_unique<SequencerProvider>(
                "alphaearth-foundations", &order);
            auto sentinelValue = std::make_unique<SequencerProvider>(
                "sentinel-2-l2a", &order);
            auto demValue = std::make_unique<SequencerProvider>(
                "copernicus-dem-glo-30", &order);
            alpha = alphaValue.get();
            sentinel = sentinelValue.get();
            dem = demValue.get();
            std::string error;
            CHECK(sourceRegistry->add(std::move(alphaValue), error));
            CHECK(sourceRegistry->add(std::move(sentinelValue), error));
            CHECK(sourceRegistry->add(std::move(demValue), error));
            service = std::make_unique<earthscience::ScienceQueryService>(
                std::move(sourceRegistry));
            manager = std::make_unique<earthscience::ScienceResearchManager>(
                temporary.path());
        }

        TempDirectory temporary;
        std::vector<std::string> order;
        SequencerProvider* alpha = nullptr;
        SequencerProvider* sentinel = nullptr;
        SequencerProvider* dem = nullptr;
        std::unique_ptr<earthscience::ScienceQueryService> service;
        std::unique_ptr<earthscience::ScienceResearchManager> manager;
    };

    void testOrderedSuccessAndIdempotentPoll()
    {
        Fixture fixture;
        earthscience::ScienceResearchSequencer sequencer(
            fixture.service.get(), fixture.manager.get());
        std::string error;
        const std::string id = sequencer.start(
            "Three sources",
            {step("alphaearth-foundations"), step("sentinel-2-l2a"),
             step("copernicus-dem-glo-30")}, error);
        CHECK(!id.empty() && error.empty());
        CHECK(fixture.order ==
              std::vector<std::string>({"alphaearth-foundations"}));

        fixture.alpha->publishReady();
        earthscience::ScienceResearchRecord record = sequencer.poll(id, error);
        CHECK(error.empty());
        CHECK(fixture.order == std::vector<std::string>(
            {"alphaearth-foundations", "sentinel-2-l2a"}));
        CHECK(record.steps[0].state == earthscience::ScienceJobState::Ready);
        CHECK(record.steps[1].state == earthscience::ScienceJobState::Queued);
        CHECK(!record.steps[0].evidenceId.empty());

        record = sequencer.poll(id, error);
        CHECK(error.empty());
        CHECK(fixture.order.size() == 2);

        fixture.sentinel->publishReady();
        record = sequencer.poll(id, error);
        CHECK(error.empty());
        CHECK(fixture.order.back() == "copernicus-dem-glo-30");
        fixture.dem->publishReady();
        record = sequencer.poll(id, error);
        CHECK(error.empty());
        CHECK(record.state == earthscience::ScienceResearchState::Ready);

        std::vector<earthscience::ScienceEvidenceRecord> evidence;
        CHECK(fixture.manager->loadEvidence(id, evidence, error));
        CHECK(evidence.size() == 3);
        earthscience::ScienceResearchBrief brief;
        CHECK(earthscience::buildScienceResearchBrief(
            record, evidence, brief, error));
        CHECK(brief.citations.size() == 3);
    }

    void testFailureRetainsPartialEvidence()
    {
        Fixture fixture;
        earthscience::ScienceResearchSequencer sequencer(
            fixture.service.get(), fixture.manager.get());
        std::string error;
        const std::string id = sequencer.start(
            "Partial three sources",
            {step("alphaearth-foundations"), step("sentinel-2-l2a"),
             step("copernicus-dem-glo-30")}, error);
        CHECK(!id.empty());
        fixture.alpha->publishReady();
        sequencer.poll(id, error);
        fixture.sentinel->publishFailed("No matching acquisition");
        sequencer.poll(id, error);
        fixture.dem->publishReady();
        const earthscience::ScienceResearchRecord record =
            sequencer.poll(id, error);
        CHECK(error.empty());
        CHECK(record.state == earthscience::ScienceResearchState::Partial);
        CHECK(record.steps[1].message == "No matching acquisition");
        std::vector<earthscience::ScienceEvidenceRecord> evidence;
        CHECK(fixture.manager->loadEvidence(id, evidence, error));
        CHECK(evidence.size() == 2);
        earthscience::ScienceResearchBrief brief;
        CHECK(earthscience::buildScienceResearchBrief(
            record, evidence, brief, error));
        CHECK(brief.markdown.find("No matching acquisition") !=
              std::string::npos);
    }

    void testCancelStopsActiveAndUnscheduledSteps()
    {
        Fixture fixture;
        earthscience::ScienceResearchSequencer sequencer(
            fixture.service.get(), fixture.manager.get());
        std::string error;
        const std::string id = sequencer.start(
            "Cancel",
            {step("alphaearth-foundations"), step("sentinel-2-l2a")}, error);
        fixture.alpha->publishFetching();
        CHECK(sequencer.cancel(id, error));
        CHECK(error.empty());
        CHECK(fixture.alpha->cancelCount == 1);
        CHECK(fixture.sentinel->generation() == 0);
        const earthscience::ScienceResearchRecord record =
            sequencer.poll(id, error);
        CHECK(error.empty());
        CHECK(record.steps[0].state ==
              earthscience::ScienceJobState::Cancelled);
        CHECK(record.steps[1].state ==
              earthscience::ScienceJobState::Cancelled);

        Fixture analysisFixture;
        earthscience::ScienceResearchSequencer analysisSequencer(
            analysisFixture.service.get(), analysisFixture.manager.get());
        const std::string analysisId = analysisSequencer.start(
            "Cancel analysis", {step("alphaearth-foundations")}, error);
        CHECK(!analysisId.empty());
        analysisFixture.alpha->publishAnalyzing();
        CHECK(analysisSequencer.cancel(analysisId, error));
        CHECK(analysisFixture.alpha->cancelCount == 1);
    }

    void testFirstAndLastFailureStillFinishHonestly()
    {
        {
            Fixture fixture;
            earthscience::ScienceResearchSequencer sequencer(
                fixture.service.get(), fixture.manager.get());
            std::string error;
            const std::string id = sequencer.start(
                "First fails", {step("alphaearth-foundations"),
                                step("sentinel-2-l2a")}, error);
            fixture.alpha->publishFailed("Alpha unavailable");
            sequencer.poll(id, error);
            fixture.sentinel->publishReady();
            const earthscience::ScienceResearchRecord record =
                sequencer.poll(id, error);
            CHECK(error.empty());
            CHECK(record.state == earthscience::ScienceResearchState::Partial);
            CHECK(record.steps.front().message == "Alpha unavailable");
        }
        {
            Fixture fixture;
            earthscience::ScienceResearchSequencer sequencer(
                fixture.service.get(), fixture.manager.get());
            std::string error;
            const std::string id = sequencer.start(
                "Last fails", {step("alphaearth-foundations"),
                               step("sentinel-2-l2a")}, error);
            fixture.alpha->publishReady();
            sequencer.poll(id, error);
            fixture.sentinel->publishFailed("Sentinel unavailable");
            const earthscience::ScienceResearchRecord record =
                sequencer.poll(id, error);
            CHECK(error.empty());
            CHECK(record.state == earthscience::ScienceResearchState::Partial);
            CHECK(record.steps.back().message == "Sentinel unavailable");
        }
    }

    void testRapidResearchRequestsNeverCancelEachOther()
    {
        Fixture fixture;
        earthscience::ScienceResearchSequencer sequencer(
            fixture.service.get(), fixture.manager.get());
        std::string error;
        const std::string first = sequencer.start(
            "First", {step("alphaearth-foundations"),
                       step("sentinel-2-l2a")}, error);
        const std::string second = sequencer.start(
            "Second", {step("copernicus-dem-glo-30")}, error);
        CHECK(!first.empty() && !second.empty());
        CHECK(fixture.alpha->cancelCount == 0);
        CHECK(fixture.dem->generation() == 0);

        fixture.alpha->publishReady();
        sequencer.poll(second, error);
        CHECK(fixture.sentinel->generation() == 1);
        CHECK(fixture.dem->generation() == 0);
        fixture.sentinel->publishReady();
        sequencer.poll(first, error);
        CHECK(fixture.dem->generation() == 1);
        CHECK(fixture.alpha->cancelCount == 0);
        CHECK(fixture.sentinel->cancelCount == 0);
    }

    void testRestartAtStepBoundaryAndValidation()
    {
        Fixture fixture;
        std::string error;
        std::string id;
        {
            earthscience::ScienceResearchSequencer sequencer(
                fixture.service.get(), fixture.manager.get());
            id = sequencer.start(
                "Restart", {step("alphaearth-foundations"),
                            step("sentinel-2-l2a")}, error);
            CHECK(!id.empty());
            fixture.alpha->publishReady();
        }
        {
            earthscience::ScienceResearchSequencer restarted(
                fixture.service.get(), fixture.manager.get());
            const earthscience::ScienceResearchRecord record =
                restarted.poll(id, error);
            CHECK(error.empty());
            CHECK(record.steps[0].state ==
                  earthscience::ScienceJobState::Ready);
            CHECK(fixture.sentinel->generation() == 1);

            const std::string duplicate = restarted.start(
                "Duplicate", {step("copernicus-dem-glo-30"),
                              step("copernicus-dem-glo-30")}, error);
            CHECK(duplicate.empty());
            CHECK(error.find("duplicate") != std::string::npos);

            earthscience::ScienceResearchRequestStep invalid =
                step("copernicus-dem-glo-30");
            invalid.query.sourceId = "sentinel-2-l2a";
            const std::string mismatch = restarted.start(
                "Mismatch", {invalid}, error);
            CHECK(mismatch.empty());
            CHECK(error.find("does not match") != std::string::npos);
        }
    }
}

int main()
{
    try
    {
        testOrderedSuccessAndIdempotentPoll();
        testFailureRetainsPartialEvidence();
        testCancelStopsActiveAndUnscheduledSteps();
        testFirstAndLastFailureStillFinishHonestly();
        testRapidResearchRequestsNeverCancelEachOther();
        testRestartAtStepBoundaryAndValidation();
        std::cout << "[OK] ScienceEarth multi-source research is serial and "
                     "durable\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
