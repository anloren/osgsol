#include "ScienceProvider.h"
#include "ScienceSourceRegistry.h"

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

    class FakeProvider : public earthscience::IScienceProvider
    {
    public:
        FakeProvider(std::string id, earthscience::ScienceSourceHealth health,
                     int* destructionCount)
            : _destructionCount(destructionCount)
        {
            _descriptor.id = std::move(id);
            _descriptor.name = _descriptor.id + " source";
            _descriptor.health = health;
        }

        ~FakeProvider() override
        {
            if (_destructionCount) ++*_destructionCount;
        }

        earthscience::ScienceSourceDescriptor descriptor() const override
        {
            return _descriptor;
        }

        std::uint64_t submit(
            const earthscience::GeoTemporalQuery&) override
        {
            return ++_generation;
        }

        earthscience::ScienceProviderSnapshot snapshot() const override
        {
            return _snapshot;
        }

        void cancel(std::uint64_t generation) override
        {
            if (generation == _generation)
                _snapshot.state = earthscience::ScienceJobState::Cancelled;
        }

        void clear() override
        {
            _snapshot = earthscience::ScienceProviderSnapshot();
        }

    private:
        earthscience::ScienceSourceDescriptor _descriptor;
        earthscience::ScienceProviderSnapshot _snapshot;
        std::uint64_t _generation = 0;
        int* _destructionCount = nullptr;
    };

    std::unique_ptr<earthscience::IScienceProvider> makeProvider(
        const std::string& id, earthscience::ScienceSourceHealth health,
        int* destructionCount)
    {
        return std::unique_ptr<earthscience::IScienceProvider>(
            new FakeProvider(id, health, destructionCount));
    }

    void testRejectsNullAndEmptyProviderIds()
    {
        earthscience::ScienceSourceRegistry registry;
        std::string error;
        require(!registry.add(nullptr, error),
                "registry accepted a null provider");
        require(error == "science provider is null",
                "null provider error is not precise");

        int destructions = 0;
        error.clear();
        require(!registry.add(makeProvider(
                    "", earthscience::ScienceSourceHealth::Unavailable,
                    &destructions), error),
                "registry accepted an empty provider id");
        require(error == "science provider id is empty",
                "empty provider id error is not precise");
        require(destructions == 1,
                "rejected provider ownership was leaked");
    }

    void testListsDeterministicallyAndKeepsUnavailableSources()
    {
        int destructions = 0;
        {
            earthscience::ScienceSourceRegistry registry;
            std::string error;
            require(registry.add(makeProvider(
                        "zeta", earthscience::ScienceSourceHealth::Ready,
                        &destructions), error),
                    "first provider registration failed");
            require(registry.add(makeProvider(
                        "alpha", earthscience::ScienceSourceHealth::Unavailable,
                        &destructions), error),
                    "second provider registration failed");

            const std::vector<earthscience::ScienceSourceDescriptor> sources =
                registry.listSources();
            require(sources.size() == 2,
                    "catalog omitted a registered provider");
            require(sources[0].id == "alpha" && sources[1].id == "zeta",
                    "catalog order is not deterministic by source id");
            require(sources[0].health ==
                        earthscience::ScienceSourceHealth::Unavailable,
                    "catalog hid or rewrote unavailable provider health");
            require(registry.find("alpha") != nullptr,
                    "exact provider lookup failed");
            require(registry.find("missing") == nullptr,
                    "missing provider lookup returned a provider");

            error.clear();
            require(!registry.add(makeProvider(
                        "zeta", earthscience::ScienceSourceHealth::Degraded,
                        &destructions), error),
                    "registry replaced a provider through duplicate id");
            require(error == "science provider id already exists: zeta",
                    "duplicate provider error is not precise");
            require(registry.find("zeta")->descriptor().health ==
                        earthscience::ScienceSourceHealth::Ready,
                    "duplicate registration replaced the first provider");
        }
        require(destructions == 3,
                "registry did not destroy each accepted/rejected provider once");
    }
}

int main()
{
    testRejectsNullAndEmptyProviderIds();
    testListsDeterministicallyAndKeepsUnavailableSources();
    std::cout << "[OK] ScienceEarth provider registry ownership and catalog\n";
    return 0;
}
