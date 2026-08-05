#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../applications/earth_explorer/project/earth_temporal_controller.cpp"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ \
              << ": " #x << std::endl; \
    std::exit(EXIT_FAILURE); } } while (0)

namespace
{
    using namespace earthproject;

    std::unique_ptr<IEarthTemporalAdapter> annualAdapter(
        const std::string& id, int firstYear, int lastYear)
    {
        BoundedTemporalAdapterOptions options;
        options.id = id;
        options.allowDiscreteValues = true;
        options.availability.known = true;
        options.availability.firstValue = std::to_string(firstYear);
        options.availability.lastValue = std::to_string(lastYear);
        for (int year = firstYear; year <= lastYear; ++year)
            options.availability.values.push_back(std::to_string(year));
        return std::unique_ptr<IEarthTemporalAdapter>(
            new BoundedEarthTemporalAdapter(std::move(options)));
    }

    EarthTemporalSelection years(std::initializer_list<const char*> values)
    {
        EarthTemporalSelection selection;
        selection.kind = TemporalSelectionKind::DiscreteValues;
        for (const char* value : values) selection.values.emplace_back(value);
        return selection;
    }

    void testRequestedAvailableLoadingAndAppliedStayDistinct()
    {
        EarthTemporalController controller;
        std::string error;
        CHECK(controller.registerAdapter(
            annualAdapter("alphaearth-foundations", 2017, 2025), error));

        const TemporalRequestResult requested = controller.request(
            "alphaearth-foundations", years({"2023", "2024"}));
        CHECK(requested.ok);
        const EarthTemporalState* state = controller.state(
            "alphaearth-foundations");
        CHECK(state != nullptr);
        CHECK(state->availability.firstValue == "2017");
        CHECK(state->availability.lastValue == "2025");
        CHECK(state->hasRequested);
        CHECK(state->requested.values.front() == "2023");
        CHECK(!state->hasApplied);
        CHECK(state->loadState == TemporalLoadState::Idle);

        CHECK(controller.beginLoading(requested.token));
        state = controller.state("alphaearth-foundations");
        CHECK(state->loadState == TemporalLoadState::Loading);
        CHECK(!state->hasApplied);

        CHECK(controller.markApplied(requested.token,
                                     years({"2023", "2024"})));
        state = controller.state("alphaearth-foundations");
        CHECK(state->loadState == TemporalLoadState::Applied);
        CHECK(state->hasApplied);
        CHECK(state->applied.values.back() == "2024");
    }

    void testStaleCompletionCannotOverwriteNewerRequest()
    {
        EarthTemporalController controller;
        std::string error;
        CHECK(controller.registerAdapter(
            annualAdapter("era5-land-surface-history", 1940, 2025), error));

        const TemporalRequestResult oldRequest = controller.request(
            "era5-land-surface-history", years({"2020"}));
        CHECK(controller.beginLoading(oldRequest.token));
        const TemporalRequestResult newRequest = controller.request(
            "era5-land-surface-history", years({"2025"}));
        CHECK(newRequest.ok);
        CHECK(newRequest.token.generation > oldRequest.token.generation);
        CHECK(controller.beginLoading(newRequest.token));

        CHECK(!controller.markApplied(oldRequest.token, years({"2020"})));
        CHECK(controller.markApplied(newRequest.token, years({"2025"})));
        const EarthTemporalState* state = controller.state(
            "era5-land-surface-history");
        CHECK(state->applied.values.size() == 1u);
        CHECK(state->applied.values[0] == "2025");
    }

    void testFailureRetainsPreviouslyAppliedTime()
    {
        EarthTemporalController controller;
        std::string error;
        CHECK(controller.registerAdapter(
            annualAdapter("era5-agricultural-climate", 1940, 2025), error));

        const TemporalRequestResult ready = controller.request(
            "era5-agricultural-climate", years({"2024"}));
        CHECK(controller.beginLoading(ready.token));
        CHECK(controller.markApplied(ready.token, years({"2024"})));

        const TemporalRequestResult failed = controller.request(
            "era5-agricultural-climate", years({"2025"}));
        CHECK(controller.beginLoading(failed.token));
        CHECK(controller.markFailed(failed.token, "provider degraded"));
        const EarthTemporalState* state = controller.state(
            "era5-agricultural-climate");
        CHECK(state->loadState == TemporalLoadState::Failed);
        CHECK(state->requested.values[0] == "2025");
        CHECK(state->applied.values[0] == "2024");
        CHECK(state->error == "provider degraded");
    }

    void testUnavailableTimeIsRejectedWithoutMutation()
    {
        EarthTemporalController controller;
        std::string error;
        CHECK(controller.registerAdapter(
            annualAdapter("alphaearth-foundations", 2017, 2025), error));
        const TemporalRequestResult valid = controller.request(
            "alphaearth-foundations", years({"2025"}));
        CHECK(valid.ok);
        const std::uint64_t generation = valid.token.generation;

        const TemporalRequestResult invalid = controller.request(
            "alphaearth-foundations", years({"2026"}));
        CHECK(!invalid.ok);
        CHECK(invalid.errorCode == "temporal-value-unavailable");
        const EarthTemporalState* state = controller.state(
            "alphaearth-foundations");
        CHECK(state->generation == generation);
        CHECK(state->requested.values[0] == "2025");
    }

    void testLiveClockUsesExplicitModeAndSameLifecycle()
    {
        EarthTemporalController controller;
        BoundedTemporalAdapterOptions options;
        options.id = "live-weather";
        options.allowLiveClock = true;
        std::string error;
        CHECK(controller.registerAdapter(
            std::unique_ptr<IEarthTemporalAdapter>(
                new BoundedEarthTemporalAdapter(std::move(options))), error));

        const std::vector<TemporalRequestToken> ticks =
            controller.requestLiveClock("2026-08-05T12:34:56Z");
        CHECK(ticks.size() == 1u);
        CHECK(controller.beginLoading(ticks[0]));
        const EarthTemporalState* state = controller.state("live-weather");
        CHECK(state->requested.kind == TemporalSelectionKind::LiveClock);
        CHECK(state->requested.startValue == "2026-08-05T12:34:56Z");
        CHECK(state->loadState == TemporalLoadState::Loading);
        CHECK(controller.markApplied(ticks[0], state->requested));
        CHECK(controller.state("live-weather")->loadState ==
              TemporalLoadState::Applied);
    }

    void testAdapterAvailabilityMetadataIsBounded()
    {
        EarthTemporalController controller;
        BoundedTemporalAdapterOptions options;
        options.id = "oversized-availability";
        options.allowDiscreteValues = true;
        options.availability.known = true;
        options.availability.values.push_back(std::string(
            kMaximumTemporalValueBytes + 1u, 'x'));
        std::string error;
        CHECK(!controller.registerAdapter(
            std::unique_ptr<IEarthTemporalAdapter>(
                new BoundedEarthTemporalAdapter(std::move(options))), error));
        CHECK(error == "temporal-availability-invalid");
    }
}

int main()
{
    testRequestedAvailableLoadingAndAppliedStayDistinct();
    testStaleCompletionCannotOverwriteNewerRequest();
    testFailureRetainsPreviouslyAppliedTime();
    testUnavailableTimeIsRejectedWithoutMutation();
    testLiveClockUsesExplicitModeAndSameLifecycle();
    testAdapterAvailabilityMetadataIsBounded();
    std::cout << "Earth temporal controller tests OK\n";
    return 0;
}
