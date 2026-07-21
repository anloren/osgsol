#include "Era5AgroProvider.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>

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
        value.sourceId = "era5-land-surface-history";
        value.geometry.kind = earthscience::ScienceGeometryKind::Point;
        value.geometry.point = {35.6762, 139.6503};
        value.time.mode = earthscience::ScienceTimeMode::ExplicitYears;
        value.time.explicitYears = {2021};
        value.variables = {
            "temperature_2m_mean", "relative_humidity_2m_mean",
            "soil_moisture_0_to_100cm_mean"};
        value.targetResolutionMeters = 11100.0;
        value.outputKind = earthscience::ScienceOutputKind::TimeSeries;
        value.analysis.kind = earthscience::ScienceAnalysisKind::PointSeries;
        value.purpose = "annual agricultural climate profile";
        return value;
    }

    std::string response()
    {
        static const int DAYS_PER_MONTH[] = {
            31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        std::ostringstream json;
        json << "{\"latitude\":35.7,\"longitude\":139.7,"
             << "\"generationtime_ms\":1.0,\"utc_offset_seconds\":0,"
             << "\"timezone\":\"GMT\",\"timezone_abbreviation\":\"GMT\","
             << "\"elevation\":40.0,\"daily_units\":{"
             << "\"time\":\"iso8601\","
             << "\"temperature_2m_mean\":\"°C\","
             << "\"relative_humidity_2m_mean\":\"%\","
             << "\"soil_moisture_0_to_100cm_mean\":\"m³/m³\"},"
             << "\"daily\":{\"time\":[";
        int emitted = 0;
        for (int month = 1; month <= 12; ++month)
        {
            for (int day = 1; day <= DAYS_PER_MONTH[month - 1]; ++day)
            {
                if (emitted++) json << ',';
                json << "\"2021-";
                if (month < 10) json << '0';
                json << month << '-';
                if (day < 10) json << '0';
                json << day << '\"';
            }
        }
        json << ']';
        const auto values = [&json](const char* id, double value)
        {
            json << ",\"" << id << "\":[";
            for (int day = 0; day < 365; ++day)
            {
                if (day) json << ',';
                json << value;
            }
            json << ']';
        };
        values("temperature_2m_mean", 16.0);
        values("relative_humidity_2m_mean", 65.0);
        values("soil_moisture_0_to_100cm_mean", 0.28);
        json << "}}";
        return json.str();
    }

    class FakeIo : public earthscience::IEra5AgroIo
    {
    public:
        std::atomic<int> fetches{0};
        std::atomic<bool> started{false};
        std::atomic<bool> blocked{false};
        bool fail = false;

        bool fetch(const std::string& url, std::size_t maximumBytes,
                   const std::function<bool()>& cancelled,
                   std::string& body, std::string& error) override
        {
            ++fetches;
            started.store(true, std::memory_order_release);
            require(url.rfind(
                        "https://archive-api.open-meteo.com/v1/archive?", 0) == 0,
                    "provider sent FakeIo a non-allowlisted URL");
            require(maximumBytes == 8u * 1024u * 1024u,
                    "provider changed the response byte budget");
            while (blocked.load(std::memory_order_acquire) && !cancelled())
                std::this_thread::yield();
            if (cancelled())
            {
                error = "cancelled";
                return false;
            }
            if (fail)
            {
                error = "simulated upstream failure";
                return false;
            }
            body = response();
            error.clear();
            return true;
        }
    };

    earthscience::ScienceProviderSnapshot waitForTerminal(
        earthscience::Era5AgroProvider& provider)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto state = provider.snapshot();
            if (state.state == earthscience::ScienceJobState::Ready ||
                state.state == earthscience::ScienceJobState::Failed ||
                state.state == earthscience::ScienceJobState::Cancelled)
                return state;
            std::this_thread::yield();
        }
        return provider.snapshot();
    }

    void testConstructionIsOfflineAndValidationIsExact()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        earthscience::Era5AgroProvider provider(
            earthscience::Era5AgroProduct::LandSurface, std::move(io));
        require(observed->fetches == 0 &&
                    provider.descriptor().health ==
                        earthscience::ScienceSourceHealth::Ready,
                "ERA5 provider probed the network during construction");
        std::string error;
        require(provider.validateQuery(query(), error) && error.empty(),
                "provider rejected its exact point-series signature");
        auto changed = query();
        changed.variables.pop_back();
        require(!provider.validateQuery(changed, error),
                "provider accepted a partial scientific variable set");
        changed = query();
        changed.time.explicitYears = {2020, 2021, 2022, 2023, 2024,
                                      2019, 2018, 2017, 2016, 2015};
        require(!provider.validateQuery(changed, error),
                "provider accepted more than nine annual slices");
    }

    void testProviderPublishesAndCancelsWithoutLeakingResults()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        earthscience::Era5AgroProvider provider(
            earthscience::Era5AgroProduct::LandSurface, std::move(io));
        const std::uint64_t generation = provider.submit(query());
        const auto ready = waitForTerminal(provider);
        require(generation != 0 && ready.generation == generation &&
                    ready.state == earthscience::ScienceJobState::Ready &&
                    ready.artifact && ready.artifact->variableSeries.size() == 3 &&
                    observed->fetches == 1,
                "ERA5 provider did not publish a complete annual artifact");

        observed->blocked.store(true, std::memory_order_release);
        observed->started.store(false, std::memory_order_release);
        const std::uint64_t blocked = provider.submit(query());
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (!observed->started.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        require(provider.descriptor().health ==
                    earthscience::ScienceSourceHealth::Busy,
                "active ERA5 provider was not marked busy");
        provider.cancel(blocked);
        const auto cancelled = waitForTerminal(provider);
        require(cancelled.state == earthscience::ScienceJobState::Cancelled &&
                    !cancelled.artifact,
                "cancelled ERA5 request leaked an artifact");
        observed->blocked.store(false, std::memory_order_release);
        provider.clear();
        require(provider.snapshot().state == earthscience::ScienceJobState::Idle,
                "ERA5 clear did not restore the ready-on-demand state");
    }

    void testTransportFailureDegradesOnlyItsProvider()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        observed->fail = true;
        earthscience::Era5AgroProvider provider(
            earthscience::Era5AgroProduct::LandSurface, std::move(io));
        provider.submit(query());
        const auto failed = waitForTerminal(provider);
        require(failed.state == earthscience::ScienceJobState::Failed &&
                    failed.message.find("simulated upstream") !=
                        std::string::npos &&
                    provider.descriptor().health ==
                        earthscience::ScienceSourceHealth::Degraded,
                "ERA5 upstream failure was hidden or misclassified");
    }

    void testDestructionCancelsBlockedIoPromptly()
    {
        auto io = std::make_unique<FakeIo>();
        FakeIo* observed = io.get();
        observed->blocked.store(true, std::memory_order_release);
        auto provider = std::make_unique<earthscience::Era5AgroProvider>(
            earthscience::Era5AgroProduct::LandSurface, std::move(io));
        provider->submit(query());

        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(3);
        while (!observed->started.load(std::memory_order_acquire) &&
               std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        require(observed->started.load(std::memory_order_acquire),
                "blocked ERA5 request did not start");

        const auto startedAt = std::chrono::steady_clock::now();
        provider.reset();
        const double elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - startedAt).count();
        require(elapsed < 1.0,
                "ERA5 provider destruction waited on blocked I/O");
    }
}

int main()
{
    testConstructionIsOfflineAndValidationIsExact();
    testProviderPublishesAndCancelsWithoutLeakingResults();
    testTransportFailureDegradesOnlyItsProvider();
    testDestructionCancelsBlockedIoPromptly();
    std::cout << "[OK] ERA5 agricultural provider lifecycle\n";
    return 0;
}
