#include "CopernicusDemProvider.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
    void require(bool condition, const char* message)
    {
        if (condition) return;
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }

    earthscience::GeoTemporalQuery query()
    {
        earthscience::GeoTemporalQuery value;
        value.sourceId = "copernicus-dem-glo-30";
        value.geometry.kind = earthscience::ScienceGeometryKind::Point;
        value.geometry.point = {35.68, 139.76};
        value.geometry.requestedSpanMeters = 10000.0;
        value.time.mode = earthscience::ScienceTimeMode::Instant;
        value.time.instant = "2021";
        value.time.publicationTime = "2021";
        value.variables = {"surface_elevation"};
        value.targetResolutionMeters = 30.0;
        value.outputKind = earthscience::ScienceOutputKind::RasterLayer;
        value.visualizationId = "surface-elevation-hypsometric";
        return value;
    }

    class FakeIo : public earthscience::ICopernicusDemIo
    {
    public:
        std::atomic<int> reads{0};
        std::atomic<bool> started{false};
        bool block = false;
        std::string failure;

        bool read(
            const std::vector<earthscience::CopernicusDemCell>& cells,
            const earthscience::GeoTemporalQuery&,
            const std::function<bool()>& cancelled,
            earthscience::CopernicusDemReadResult& output,
            std::string& error) override
        {
            ++reads;
            started.store(true, std::memory_order_release);
            while (block && !cancelled()) std::this_thread::yield();
            if (cancelled())
            {
                error = "cancelled";
                return false;
            }
            if (!failure.empty())
            {
                error = failure;
                return false;
            }
            output.raster.bounds = {139.70, 35.60, 139.82, 35.76};
            output.raster.width = output.raster.height = 256;
            output.raster.sourceWindowWidth = 334;
            output.raster.sourceWindowHeight = 334;
            output.raster.sourceResolutionMeters = 30.0;
            output.raster.displayResolutionMeters = 39.0625;
            output.raster.rgba =
                std::make_shared<const std::vector<unsigned char>>(
                    256u * 256u * 4u, 127);
            auto grid = std::make_shared<earthscience::ScienceGroundGrid>();
            grid->columns = grid->rows = 2;
            grid->points = {
                {35.60, 139.70}, {35.60, 139.82},
                {35.76, 139.70}, {35.76, 139.82},
            };
            output.raster.groundGrid = std::move(grid);
            output.summary.variableId = "surface_elevation";
            output.summary.unit = "m";
            output.summary.centerValid = true;
            output.summary.center = 42.0;
            output.summary.minimumValid = true;
            output.summary.maximumValid = true;
            output.summary.meanValid = true;
            output.summary.minimum = 1.0;
            output.summary.maximum = 88.0;
            output.summary.mean = 35.0;
            output.summary.validCellCount = 111556;
            for (const auto& cell : cells)
                output.sourceUrls.push_back(cell.url);
            error.clear();
            return true;
        }
    };

    earthscience::ScienceProviderSnapshot waitForTerminal(
        earthscience::CopernicusDemProvider& provider)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const earthscience::ScienceProviderSnapshot state =
                provider.snapshot();
            if (state.state == earthscience::ScienceJobState::Ready ||
                state.state == earthscience::ScienceJobState::Failed ||
                state.state == earthscience::ScienceJobState::Cancelled)
                return state;
            std::this_thread::yield();
        }
        return provider.snapshot();
    }

    void testDescriptorIsTruthfulAndConstructionDoesNotUseNetwork()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        earthscience::CopernicusDemProvider provider(std::move(io));
        const earthscience::ScienceSourceDescriptor source =
            provider.descriptor();
        require(observed->reads == 0,
                "DEM provider read the network during construction");
        require(source.id == "copernicus-dem-glo-30" &&
                    source.name == "Copernicus DEM GLO-30" &&
                    source.providerVersion == "aws-glo30-2021" &&
                    source.firstYear == 2021 && source.lastYear == 2021,
                "DEM identity or publication release is wrong");
        require(source.category.find("digital surface model") !=
                    std::string::npos &&
                    source.attribution.find("Copernicus") !=
                        std::string::npos,
                "DEM product type or attribution is ambiguous");
        require(source.nativeResolutionMeters == 30.0 &&
                    source.componentCount == 1 &&
                    source.variables.size() == 1 &&
                    source.variables.front().id == "surface_elevation" &&
                    source.variables.front().unit == "m" &&
                    source.variables.front().dataKind.find(
                        "surface model") != std::string::npos,
                "DEM numeric variable was misdescribed");
        require(source.visualizations.size() == 1 &&
                    source.visualizations.front().id ==
                        "surface-elevation-hypsometric" &&
                    source.visualizations.front().kind ==
                        earthscience::ScienceVisualizationKind::Continuous &&
                    source.visualizations.front().legend.find("DSM") !=
                        std::string::npos,
                "DEM hypsometric display contract is ambiguous");
        require(source.capabilities.pointQuery &&
                    source.capabilities.instantTime &&
                    source.capabilities.rasterLayerOutput &&
                    !source.capabilities.intervalTime &&
                    !source.capabilities.explicitYears &&
                    !source.capabilities.analysisOutput &&
                    !source.capabilities.timeSeriesOutput &&
                    source.capabilities.minimumSpanMeters == 2560.0 &&
                    source.capabilities.maximumSpanMeters == 81920.0 &&
                    source.health == earthscience::ScienceSourceHealth::Ready,
                "DEM bounded static capabilities are not truthful");
    }

    void testProviderRejectsEveryUnsupportedSignature()
    {
        earthscience::CopernicusDemProvider provider(
            std::make_unique<FakeIo>());
        std::string error;
        require(provider.validateQuery(query(), error) && error.empty(),
                "provider rejected the exact DEM preview signature");

        earthscience::GeoTemporalQuery changed = query();
        changed.sourceId = "alphaearth-foundations";
        require(!provider.validateQuery(changed, error),
                "provider accepted another source id");
        changed = query();
        changed.geometry.kind = earthscience::ScienceGeometryKind::CurrentView;
        require(!provider.validateQuery(changed, error),
                "provider accepted non-point geometry");
        changed = query();
        changed.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        changed.time.explicitYears = {2021};
        require(!provider.validateQuery(changed, error),
                "provider presented a static DSM as an annual series");
        changed = query();
        changed.time.publicationTime = "2020";
        require(!provider.validateQuery(changed, error),
                "provider accepted the wrong publication release");
        changed = query();
        changed.variables = {"elevation"};
        require(!provider.validateQuery(changed, error),
                "provider accepted an unsupported variable");
        changed = query();
        changed.visualizationId = "natural-color-visual";
        require(!provider.validateQuery(changed, error),
                "provider accepted a misleading visualization");
        changed = query();
        changed.targetResolutionMeters = 10.0;
        require(!provider.validateQuery(changed, error),
                "provider accepted a target resolution it does not honor");
        changed = query();
        changed.outputKind = earthscience::ScienceOutputKind::Analysis;
        changed.analysis.kind = earthscience::ScienceAnalysisKind::RegionalChange;
        require(!provider.validateQuery(changed, error),
                "provider accepted unimplemented DEM analysis");
        changed = query();
        changed.geometry.requestedSpanMeters = 1000.0;
        require(!provider.validateQuery(changed, error),
                "provider accepted a span below its bounded contract");
    }

    void testProviderDispatchesCancelsClearsAndReturnsEvidence()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        earthscience::CopernicusDemProvider provider(std::move(io));
        const std::uint64_t generation = provider.submit(query());
        const earthscience::ScienceProviderSnapshot ready =
            waitForTerminal(provider);
        require(generation != 0 && ready.generation == generation &&
                    ready.state == earthscience::ScienceJobState::Ready &&
                    ready.artifact &&
                    ready.artifact->query.sourceId ==
                        "copernicus-dem-glo-30" &&
                    ready.artifact->scalarSummaries.size() == 1 &&
                    ready.artifact->sourceReferences.size() == 1 &&
                    observed->reads == 1,
                "provider did not return a referenced DEM artifact");

        observed->block = true;
        observed->started.store(false, std::memory_order_release);
        const std::uint64_t blocked = provider.submit(query());
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (!observed->started.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        require(provider.descriptor().health ==
                    earthscience::ScienceSourceHealth::Busy,
                "active DEM provider was not marked busy");
        provider.cancel(blocked);
        const earthscience::ScienceProviderSnapshot cancelled =
            waitForTerminal(provider);
        require(cancelled.state == earthscience::ScienceJobState::Cancelled &&
                    !cancelled.artifact,
                "DEM cancellation leaked an artifact");
        observed->block = false;
        provider.clear();
        require(provider.snapshot().state == earthscience::ScienceJobState::Idle,
                "DEM clear did not restore ready-on-demand state");
    }

    void testMissingCoverageDoesNotDegradeTheWholeSource()
    {
        auto io = std::make_unique<FakeIo>();
        io->failure =
            "No Copernicus DEM public geocell covers this ocean request";
        earthscience::CopernicusDemProvider provider(std::move(io));
        provider.submit(query());
        const earthscience::ScienceProviderSnapshot missing =
            waitForTerminal(provider);
        require(missing.state == earthscience::ScienceJobState::Failed &&
                    missing.message.find("No Copernicus DEM") == 0,
                "missing DEM coverage was not reported honestly");
        require(provider.descriptor().health ==
                    earthscience::ScienceSourceHealth::Ready,
                "missing DEM coverage degraded the entire source");
    }
}

int main()
{
    testDescriptorIsTruthfulAndConstructionDoesNotUseNetwork();
    testProviderRejectsEveryUnsupportedSignature();
    testProviderDispatchesCancelsClearsAndReturnsEvidence();
    testMissingCoverageDoesNotDegradeTheWholeSource();
    std::cout << "[OK] ScienceEarth Copernicus DEM provider contract\n";
    return 0;
}
