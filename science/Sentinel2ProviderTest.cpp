#include "Sentinel2Provider.h"

#include <algorithm>
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
        value.sourceId = "sentinel-2-l2a";
        value.geometry.point = {35.68, 139.76};
        value.geometry.requestedSpanMeters = 10000.0;
        value.time.mode = earthscience::ScienceTimeMode::Interval;
        value.time.intervalStart = "2026-06-18T00:00:00Z";
        value.time.intervalEnd = "2026-07-18T23:59:59Z";
        value.variables = {"visual"};
        value.targetResolutionMeters = 10.0;
        value.visualizationId = "natural-color-visual";
        value.sceneFilters.maximumCloudCoverPercent = 20.0;
        value.sceneFilters.maximumScenes = 10;
        return value;
    }

    std::string response()
    {
        return "{\"type\":\"FeatureCollection\",\"features\":[{"
            "\"type\":\"Feature\",\"id\":\"S2B_54SUE_20260715_0_L2A\","
            "\"bbox\":[138.7,35.1,140.1,36.2],\"properties\":{"
            "\"datetime\":\"2026-07-15T01:37:22.011000Z\","
            "\"eo:cloud_cover\":4.5},\"assets\":{\"visual\":{"
            "\"href\":\"https://sentinel-cogs.s3.us-west-2.amazonaws.com/"
            "sentinel-s2-l2a-cogs/54/S/UE/2026/7/scene/TCI.tif\","
            "\"type\":\"image/tiff; application=geotiff; "
            "profile=cloud-optimized\",\"roles\":[\"visual\"],"
            "\"gsd\":10}}}]}";
    }

    class FakeIo : public earthscience::ISentinel2Io
    {
    public:
        std::atomic<int> fetches{0};
        std::atomic<int> reads{0};
        std::atomic<bool> fetchStarted{false};
        bool blockFetch = false;

        bool fetchStac(
            const std::string&, std::size_t,
            const std::function<bool()>& cancelled,
            std::string& body, std::string& error) override
        {
            ++fetches;
            fetchStarted.store(true, std::memory_order_release);
            while (blockFetch && !cancelled()) std::this_thread::yield();
            if (cancelled())
            {
                error = "cancelled";
                return false;
            }
            body = response();
            error.clear();
            return true;
        }

        bool readVisual(
            const earthscience::Sentinel2Scene&,
            const earthscience::GeoTemporalQuery&,
            const std::function<bool()>& cancelled,
            earthscience::ScienceRasterPayload& output,
            std::string& error) override
        {
            ++reads;
            if (cancelled())
            {
                error = "cancelled";
                return false;
            }
            output.bounds = {139.70, 35.60, 139.82, 35.76};
            output.width = output.height = 256;
            output.sourceWindowWidth = output.sourceWindowHeight = 1000;
            output.sourceResolutionMeters = 10.0;
            output.displayResolutionMeters = 39.0625;
            output.rgba =
                std::make_shared<const std::vector<unsigned char>>(
                    256u * 256u * 4u, 127);
            auto grid = std::make_shared<earthscience::ScienceGroundGrid>();
            grid->columns = grid->rows = 2;
            grid->points = {
                {35.60, 139.70}, {35.60, 139.82},
                {35.76, 139.70}, {35.76, 139.82},
            };
            output.groundGrid = std::move(grid);
            error.clear();
            return true;
        }
    };

    earthscience::ScienceProviderSnapshot waitForTerminal(
        earthscience::Sentinel2Provider& provider)
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
        earthscience::Sentinel2Provider provider(std::move(io));
        const earthscience::ScienceSourceDescriptor source =
            provider.descriptor();
        require(observed->fetches == 0 && observed->reads == 0,
                "Sentinel-2 provider probed the network during construction");
        require(source.id == "sentinel-2-l2a" &&
                    source.name == "Sentinel-2 Level-2A" &&
                    source.providerVersion == "earth-search-v1" &&
                    source.firstYear == 2015 && source.lastYear >= 2026,
                "Sentinel-2 identity or official temporal extent is wrong");
        require(source.nativeResolutionMeters == 10.0 &&
                    source.componentCount == 3 &&
                    source.variables.size() == 1 &&
                    source.variables.front().id == "visual" &&
                    source.variables.front().componentCount == 3,
                "Sentinel-2 visual product was misdescribed");
        require(source.visualizations.size() == 1 &&
                    source.visualizations.front().id ==
                        "natural-color-visual" &&
                    source.visualizations.front().kind ==
                        earthscience::ScienceVisualizationKind::NaturalColor &&
                    source.visualizations.front().channelVariables ==
                        std::vector<std::string>({"visual"}) &&
                    source.visualizations.front().legend.find(
                        "not raw reflectance") != std::string::npos,
                "Sentinel-2 natural-color contract is ambiguous");
        require(source.capabilities.pointQuery &&
                    source.capabilities.intervalTime &&
                    source.capabilities.rasterLayerOutput &&
                    !source.capabilities.explicitYears &&
                    !source.capabilities.embeddingOutput &&
                    !source.capabilities.analysisOutput &&
                    source.capabilities.minimumSpanMeters == 2560.0 &&
                    source.capabilities.maximumSpanMeters == 81920.0 &&
                    source.experimental &&
                    source.health == earthscience::ScienceSourceHealth::Ready,
                "Sentinel-2 bounded capabilities are not truthful");
    }

    void testProviderRejectsEveryNonSliceSignature()
    {
        earthscience::Sentinel2Provider provider(
            std::make_unique<FakeIo>());
        std::string error;
        require(provider.validateQuery(query(), error) && error.empty(),
                "provider rejected the exact Sentinel-2 preview signature");

        earthscience::GeoTemporalQuery changed = query();
        changed.sourceId = "alphaearth-foundations";
        require(!provider.validateQuery(changed, error),
                "provider accepted a different source id");
        changed = query();
        changed.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        require(!provider.validateQuery(changed, error),
                "provider accepted explicit-year time");
        changed = query();
        changed.time.intervalStart = "2026-99-18T00:00:00Z";
        require(!provider.validateQuery(changed, error),
                "provider accepted a malformed RFC 3339 interval");
        changed = query();
        changed.variables = {"red", "green", "blue"};
        require(!provider.validateQuery(changed, error),
                "provider accepted unsupported reflectance variables");
        changed = query();
        changed.visualizationId = "false-color-a01-a16-a09";
        require(!provider.validateQuery(changed, error),
                "provider accepted a different visualization");
        changed = query();
        changed.targetResolutionMeters = 20.0;
        require(!provider.validateQuery(changed, error),
                "provider accepted a target resolution it does not honor");
        changed = query();
        changed.sceneFilters.maximumScenes = 9;
        require(!provider.validateQuery(changed, error),
                "provider accepted a non-contract scene limit");
        changed = query();
        changed.geometry.point.longitude = 179.99;
        changed.geometry.requestedSpanMeters = 81920.0;
        require(!provider.validateQuery(changed, error) &&
                    error.find("antimeridian") != std::string::npos,
                "provider accepted an antimeridian-crossing slice");
    }

    void testProviderDispatchesCancelsClearsAndJoinsCleanly()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        earthscience::Sentinel2Provider provider(std::move(io));
        const std::uint64_t generation = provider.submit(query());
        const earthscience::ScienceProviderSnapshot ready =
            waitForTerminal(provider);
        require(generation != 0 && ready.generation == generation &&
                    ready.state == earthscience::ScienceJobState::Ready &&
                    ready.artifact &&
                    ready.artifact->query.sourceId == "sentinel-2-l2a" &&
                    observed->fetches == 1 && observed->reads == 1,
                "provider did not adapt a complete runtime artifact");

        observed->blockFetch = true;
        observed->fetchStarted.store(false, std::memory_order_release);
        const std::uint64_t blocked = provider.submit(query());
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (!observed->fetchStarted.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        require(provider.descriptor().health ==
                    earthscience::ScienceSourceHealth::Busy,
                "active Sentinel-2 provider was not marked busy");
        provider.cancel(blocked);
        const earthscience::ScienceProviderSnapshot cancelled =
            waitForTerminal(provider);
        require(cancelled.state == earthscience::ScienceJobState::Cancelled &&
                    !cancelled.artifact,
                "provider cancellation leaked an artifact");
        observed->blockFetch = false;
        provider.clear();
        require(provider.snapshot().state ==
                    earthscience::ScienceJobState::Idle,
                "provider clear did not restore ready-on-demand state");
    }

    void testNoMatchingSceneDoesNotDegradeTheSource()
    {
        earthscience::Sentinel2Provider provider(
            std::make_unique<FakeIo>());
        earthscience::GeoTemporalQuery strict = query();
        strict.sceneFilters.maximumCloudCoverPercent = 1.0;
        provider.submit(strict);
        const earthscience::ScienceProviderSnapshot missing =
            waitForTerminal(provider);
        const earthscience::ScienceSourceDescriptor source =
            provider.descriptor();
        require(missing.state == earthscience::ScienceJobState::Failed &&
                    missing.message.find("cloud threshold") !=
                        std::string::npos,
                "no-match query did not remain an honest query result");
        require(source.health == earthscience::ScienceSourceHealth::Ready,
                "no matching scene incorrectly degraded the whole source");
    }
}

int main()
{
    testDescriptorIsTruthfulAndConstructionDoesNotUseNetwork();
    testProviderRejectsEveryNonSliceSignature();
    testProviderDispatchesCancelsClearsAndJoinsCleanly();
    testNoMatchingSceneDoesNotDegradeTheSource();
    std::cout << "[OK] ScienceEarth Sentinel-2 provider contract\n";
    return 0;
}
