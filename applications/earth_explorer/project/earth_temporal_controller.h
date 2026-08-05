#ifndef OSGSOL_EARTH_TEMPORAL_CONTROLLER_H
#define OSGSOL_EARTH_TEMPORAL_CONTROLLER_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace earthproject
{
    constexpr std::size_t kMaximumTemporalIdBytes = 256u;
    constexpr std::size_t kMaximumTemporalValueBytes = 128u;
    constexpr std::size_t kMaximumTemporalValues = 512u;
    constexpr std::size_t kMaximumTemporalErrorBytes = 512u;

    enum class TemporalSelectionKind
    {
        None,
        Instant,
        Interval,
        DiscreteValues,
        LiveClock
    };

    enum class TemporalLoadState
    {
        Idle,
        Loading,
        Applied,
        Failed,
        Cancelled
    };

    struct EarthTemporalSelection
    {
        TemporalSelectionKind kind = TemporalSelectionKind::None;
        std::string startValue;
        std::string endValue;
        std::vector<std::string> values;
    };

    struct EarthTemporalAvailability
    {
        bool known = false;
        std::string firstValue;
        std::string lastValue;
        std::vector<std::string> values;
    };

    struct EarthTemporalState
    {
        std::string adapterId;
        EarthTemporalAvailability availability;
        bool hasRequested = false;
        EarthTemporalSelection requested;
        TemporalLoadState loadState = TemporalLoadState::Idle;
        bool hasApplied = false;
        EarthTemporalSelection applied;
        std::uint64_t generation = 0u;
        std::string error;
    };

    struct TemporalRequestToken
    {
        std::string adapterId;
        std::uint64_t generation = 0u;

        explicit operator bool() const
        {
            return !adapterId.empty() && generation != 0u;
        }
    };

    struct TemporalRequestResult
    {
        bool ok = false;
        TemporalRequestToken token;
        std::string errorCode;
    };

    class IEarthTemporalAdapter
    {
    public:
        virtual ~IEarthTemporalAdapter() = default;
        virtual std::string id() const = 0;
        virtual EarthTemporalAvailability availability() const = 0;
        virtual bool supportsLiveClock() const = 0;
        virtual bool validateSelection(
            const EarthTemporalSelection& selection,
            std::string& errorCode) const = 0;
    };

    struct BoundedTemporalAdapterOptions
    {
        std::string id;
        EarthTemporalAvailability availability;
        bool allowInstant = false;
        bool allowInterval = false;
        bool allowDiscreteValues = false;
        bool allowLiveClock = false;
    };

    class BoundedEarthTemporalAdapter final : public IEarthTemporalAdapter
    {
    public:
        explicit BoundedEarthTemporalAdapter(
            BoundedTemporalAdapterOptions options);

        std::string id() const override;
        EarthTemporalAvailability availability() const override;
        bool supportsLiveClock() const override;
        bool validateSelection(
            const EarthTemporalSelection& selection,
            std::string& errorCode) const override;

    private:
        BoundedTemporalAdapterOptions _options;
    };

    class EarthTemporalController
    {
    public:
        bool registerAdapter(
            std::unique_ptr<IEarthTemporalAdapter> adapter,
            std::string& errorCode);

        TemporalRequestResult request(
            const std::string& adapterId,
            const EarthTemporalSelection& selection);
        bool beginLoading(const TemporalRequestToken& token);
        bool markApplied(
            const TemporalRequestToken& token,
            const EarthTemporalSelection& appliedSelection);
        bool markFailed(
            const TemporalRequestToken& token,
            const std::string& error);
        bool markCancelled(const TemporalRequestToken& token);

        std::vector<TemporalRequestToken> requestLiveClock(
            const std::string& utcInstant);
        const EarthTemporalState* state(const std::string& adapterId) const;

    private:
        struct Entry
        {
            std::unique_ptr<IEarthTemporalAdapter> adapter;
            EarthTemporalState state;
        };

        Entry* current(const TemporalRequestToken& token);
        std::map<std::string, Entry> _entries;
    };

    bool sameTemporalSelection(
        const EarthTemporalSelection& lhs,
        const EarthTemporalSelection& rhs);
    const char* temporalSelectionKindName(TemporalSelectionKind kind);
    const char* temporalLoadStateName(TemporalLoadState state);
}

#endif
