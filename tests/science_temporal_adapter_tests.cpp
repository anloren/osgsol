#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../applications/earth_explorer/project/earth_temporal_controller.cpp"
#include "../applications/earth_explorer/science_temporal_adapter.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; \
    std::exit(EXIT_FAILURE); } } while (0)

namespace
{
    earthscience::ScienceSourceDescriptor source(
        const std::string& id, int firstYear, int lastYear)
    {
        earthscience::ScienceSourceDescriptor value;
        value.id = id;
        value.firstYear = firstYear;
        value.lastYear = lastYear;
        return value;
    }

    void testAllPilotScienceSourcesShareOneController()
    {
        earthproject::EarthTemporalController controller;
        std::string error;
        const std::vector<earthscience::ScienceSourceDescriptor> sources = {
            source("alphaearth-foundations", 2017, 2025),
            source("sentinel-2-l2a", 2015, 2026),
            source("era5-land-surface-history", 1940, 2025),
            source("era5-agricultural-climate", 1940, 2025),
        };
        for (const auto& descriptor : sources)
        {
            std::unique_ptr<earthproject::IEarthTemporalAdapter> adapter =
                makeScienceTemporalAdapter(descriptor);
            CHECK(adapter != nullptr);
            CHECK(controller.registerAdapter(std::move(adapter), error));
        }

        earthscience::GeoTemporalQuery alpha;
        alpha.sourceId = "alphaearth-foundations";
        alpha.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        alpha.time.explicitYears = {2017, 2025};
        CHECK(controller.request(alpha.sourceId,
            earthTemporalSelectionForQuery(alpha)).ok);

        earthscience::GeoTemporalQuery sentinel;
        sentinel.sourceId = "sentinel-2-l2a";
        sentinel.time.mode = earthscience::ScienceTimeMode::Interval;
        sentinel.time.intervalStart = "2025-01-01T00:00:00Z";
        sentinel.time.intervalEnd = "2025-12-31T23:59:59Z";
        const earthproject::TemporalRequestResult sentinelRequest =
            controller.request(sentinel.sourceId,
                earthTemporalSelectionForQuery(sentinel));
        CHECK(sentinelRequest.ok);
        earthproject::EarthTemporalSelection sentinelDraft;
        sentinelDraft.kind =
            earthproject::TemporalSelectionKind::DiscreteValues;
        sentinelDraft.values = {"2025"};
        CHECK(controller.request(sentinel.sourceId, sentinelDraft).ok);
        const earthproject::TemporalRequestResult exactSentinelRequest =
            controller.request(sentinel.sourceId,
                earthTemporalSelectionForQuery(sentinel));
        CHECK(exactSentinelRequest.ok);
        CHECK(controller.beginLoading(exactSentinelRequest.token));

        earthscience::ScienceArtifact sentinelArtifact;
        sentinelArtifact.query = sentinel;
        earthscience::ScienceSourceReference scene;
        scene.sourceId = sentinel.sourceId;
        scene.acquisitionTime = "2025-08-02T03:04:05Z";
        sentinelArtifact.sourceReferences.push_back(scene);
        CHECK(controller.markApplied(exactSentinelRequest.token,
            earthTemporalSelectionForArtifact(sentinelArtifact)));
        const earthproject::EarthTemporalState* sentinelState =
            controller.state(sentinel.sourceId);
        CHECK(sentinelState->requested.kind ==
              earthproject::TemporalSelectionKind::Interval);
        CHECK(sentinelState->applied.kind ==
              earthproject::TemporalSelectionKind::Instant);
        CHECK(sentinelState->applied.startValue == scene.acquisitionTime);

        for (const char* id : {"era5-land-surface-history",
                               "era5-agricultural-climate"})
        {
            earthscience::GeoTemporalQuery era;
            era.sourceId = id;
            era.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
            era.time.explicitYears = {2020, 2021, 2022};
            CHECK(controller.request(era.sourceId,
                earthTemporalSelectionForQuery(era)).ok);
            CHECK(controller.state(era.sourceId)->availability.firstValue ==
                  "1940");
        }
    }

    void testUnsupportedOrInvalidSourcesDoNotCreateAdapters()
    {
        CHECK(makeScienceTemporalAdapter(
            source("copernicus-dem-glo-30", 2021, 2021)) == nullptr);
        CHECK(makeScienceTemporalAdapter(
            source("alphaearth-foundations", 0, 0)) == nullptr);
    }
}

int main()
{
    testAllPilotScienceSourcesShareOneController();
    testUnsupportedOrInvalidSourcesDoNotCreateAdapters();
    std::cout << "Science temporal adapter tests OK\n";
    return 0;
}
