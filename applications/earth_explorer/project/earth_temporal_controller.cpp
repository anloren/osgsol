#include "earth_temporal_controller.h"

#include <algorithm>
#include <set>
#include <utility>

namespace earthproject
{
    namespace
    {
        bool validText(const std::string& value)
        {
            if (value.empty() || value.size() > kMaximumTemporalValueBytes)
                return false;
            return std::none_of(
                value.begin(), value.end(),
                [](unsigned char character) { return character < 0x20u; });
        }

        std::string boundedError(const std::string& error)
        {
            return error.substr(0u, kMaximumTemporalErrorBytes);
        }

        bool withinAvailability(
            const EarthTemporalAvailability& availability,
            const std::string& value)
        {
            if (!availability.known) return true;
            if (!availability.values.empty())
                return std::find(availability.values.begin(),
                                 availability.values.end(), value) !=
                    availability.values.end();
            if (!availability.firstValue.empty() &&
                value < availability.firstValue)
                return false;
            if (!availability.lastValue.empty() &&
                value > availability.lastValue)
                return false;
            return true;
        }
    }

    bool sameTemporalSelection(
        const EarthTemporalSelection& lhs,
        const EarthTemporalSelection& rhs)
    {
        return lhs.kind == rhs.kind && lhs.startValue == rhs.startValue &&
            lhs.endValue == rhs.endValue && lhs.values == rhs.values;
    }

    const char* temporalSelectionKindName(TemporalSelectionKind kind)
    {
        switch (kind)
        {
        case TemporalSelectionKind::Instant: return "instant";
        case TemporalSelectionKind::Interval: return "interval";
        case TemporalSelectionKind::DiscreteValues: return "discrete-values";
        case TemporalSelectionKind::LiveClock: return "live-clock";
        case TemporalSelectionKind::None:
        default: return "none";
        }
    }

    const char* temporalLoadStateName(TemporalLoadState state)
    {
        switch (state)
        {
        case TemporalLoadState::Loading: return "loading";
        case TemporalLoadState::Applied: return "applied";
        case TemporalLoadState::Failed: return "failed";
        case TemporalLoadState::Cancelled: return "cancelled";
        case TemporalLoadState::Idle:
        default: return "idle";
        }
    }

    BoundedEarthTemporalAdapter::BoundedEarthTemporalAdapter(
        BoundedTemporalAdapterOptions options)
        : _options(std::move(options))
    {
    }

    std::string BoundedEarthTemporalAdapter::id() const
    {
        return _options.id;
    }

    EarthTemporalAvailability
    BoundedEarthTemporalAdapter::availability() const
    {
        return _options.availability;
    }

    bool BoundedEarthTemporalAdapter::supportsLiveClock() const
    {
        return _options.allowLiveClock;
    }

    bool BoundedEarthTemporalAdapter::validateSelection(
        const EarthTemporalSelection& selection,
        std::string& errorCode) const
    {
        errorCode.clear();
        bool allowed = false;
        switch (selection.kind)
        {
        case TemporalSelectionKind::Instant:
            allowed = _options.allowInstant;
            if (allowed && (!validText(selection.startValue) ||
                !selection.endValue.empty() || !selection.values.empty()))
                errorCode = "temporal-selection-invalid";
            else if (allowed && !withinAvailability(
                         _options.availability, selection.startValue))
                errorCode = "temporal-value-unavailable";
            break;
        case TemporalSelectionKind::Interval:
            allowed = _options.allowInterval;
            if (allowed && (!validText(selection.startValue) ||
                !validText(selection.endValue) ||
                selection.startValue > selection.endValue ||
                !selection.values.empty()))
                errorCode = "temporal-selection-invalid";
            else if (allowed &&
                (!withinAvailability(_options.availability,
                                     selection.startValue) ||
                 !withinAvailability(_options.availability,
                                     selection.endValue)))
                errorCode = "temporal-value-unavailable";
            break;
        case TemporalSelectionKind::DiscreteValues:
            allowed = _options.allowDiscreteValues;
            if (allowed && (selection.values.empty() ||
                selection.values.size() > kMaximumTemporalValues ||
                !selection.startValue.empty() || !selection.endValue.empty()))
                errorCode = "temporal-selection-invalid";
            else if (allowed)
            {
                std::set<std::string> seen;
                std::string previous;
                for (const std::string& value : selection.values)
                {
                    if (!validText(value) ||
                        (!previous.empty() && value <= previous))
                    {
                        errorCode = "temporal-selection-invalid";
                        break;
                    }
                    if (!seen.insert(value).second)
                    {
                        errorCode = "temporal-selection-invalid";
                        break;
                    }
                    if (!withinAvailability(_options.availability, value))
                    {
                        errorCode = "temporal-value-unavailable";
                        break;
                    }
                    previous = value;
                }
            }
            break;
        case TemporalSelectionKind::LiveClock:
            allowed = _options.allowLiveClock;
            if (allowed && (!validText(selection.startValue) ||
                !selection.endValue.empty() || !selection.values.empty()))
                errorCode = "temporal-selection-invalid";
            break;
        case TemporalSelectionKind::None:
        default:
            errorCode = "temporal-selection-required";
            return false;
        }
        if (!allowed)
        {
            errorCode = "temporal-mode-unsupported";
            return false;
        }
        return errorCode.empty();
    }

    bool EarthTemporalController::registerAdapter(
        std::unique_ptr<IEarthTemporalAdapter> adapter,
        std::string& errorCode)
    {
        errorCode.clear();
        if (!adapter)
        {
            errorCode = "temporal-adapter-required";
            return false;
        }
        const std::string id = adapter->id();
        if (id.empty() || id.size() > kMaximumTemporalIdBytes)
        {
            errorCode = "temporal-adapter-id-invalid";
            return false;
        }
        if (_entries.find(id) != _entries.end())
        {
            errorCode = "temporal-adapter-duplicate";
            return false;
        }
        const EarthTemporalAvailability availability =
            adapter->availability();
        if (availability.values.size() > kMaximumTemporalValues ||
            (!availability.firstValue.empty() &&
             !validText(availability.firstValue)) ||
            (!availability.lastValue.empty() &&
             !validText(availability.lastValue)) ||
            (!availability.firstValue.empty() &&
             !availability.lastValue.empty() &&
             availability.firstValue > availability.lastValue))
        {
            errorCode = "temporal-availability-invalid";
            return false;
        }
        std::string previous;
        for (const std::string& value : availability.values)
        {
            if (!validText(value) ||
                (!previous.empty() && value <= previous))
            {
                errorCode = "temporal-availability-invalid";
                return false;
            }
            previous = value;
        }
        Entry entry;
        entry.state.adapterId = id;
        entry.state.availability = availability;
        entry.adapter = std::move(adapter);
        _entries.emplace(id, std::move(entry));
        return true;
    }

    TemporalRequestResult EarthTemporalController::request(
        const std::string& adapterId,
        const EarthTemporalSelection& selection)
    {
        TemporalRequestResult result;
        const auto found = _entries.find(adapterId);
        if (found == _entries.end())
        {
            result.errorCode = "temporal-adapter-not-found";
            return result;
        }
        std::string error;
        if (!found->second.adapter->validateSelection(selection, error))
        {
            result.errorCode = error;
            return result;
        }
        EarthTemporalState& state = found->second.state;
        const bool reuse = state.hasRequested &&
            sameTemporalSelection(state.requested, selection) &&
            (state.loadState == TemporalLoadState::Idle ||
             state.loadState == TemporalLoadState::Loading);
        if (!reuse)
        {
            ++state.generation;
            if (state.generation == 0u) ++state.generation;
        }
        state.hasRequested = true;
        state.requested = selection;
        if (!reuse) state.loadState = TemporalLoadState::Idle;
        state.error.clear();
        result.ok = true;
        result.token = {adapterId, state.generation};
        return result;
    }

    EarthTemporalController::Entry* EarthTemporalController::current(
        const TemporalRequestToken& token)
    {
        const auto found = _entries.find(token.adapterId);
        if (found == _entries.end() || token.generation == 0u ||
            found->second.state.generation != token.generation)
            return nullptr;
        return &found->second;
    }

    bool EarthTemporalController::beginLoading(
        const TemporalRequestToken& token)
    {
        Entry* entry = current(token);
        if (!entry || !entry->state.hasRequested) return false;
        entry->state.loadState = TemporalLoadState::Loading;
        entry->state.error.clear();
        return true;
    }

    bool EarthTemporalController::markApplied(
        const TemporalRequestToken& token,
        const EarthTemporalSelection& appliedSelection)
    {
        Entry* entry = current(token);
        if (!entry) return false;
        std::string error;
        if (!entry->adapter->validateSelection(appliedSelection, error))
            return false;
        entry->state.hasApplied = true;
        entry->state.applied = appliedSelection;
        entry->state.loadState = TemporalLoadState::Applied;
        entry->state.error.clear();
        return true;
    }

    bool EarthTemporalController::markFailed(
        const TemporalRequestToken& token,
        const std::string& error)
    {
        Entry* entry = current(token);
        if (!entry) return false;
        entry->state.loadState = TemporalLoadState::Failed;
        entry->state.error = boundedError(error);
        return true;
    }

    bool EarthTemporalController::markCancelled(
        const TemporalRequestToken& token)
    {
        Entry* entry = current(token);
        if (!entry) return false;
        entry->state.loadState = TemporalLoadState::Cancelled;
        entry->state.error.clear();
        return true;
    }

    std::vector<TemporalRequestToken>
    EarthTemporalController::requestLiveClock(
        const std::string& utcInstant)
    {
        std::vector<TemporalRequestToken> tokens;
        EarthTemporalSelection selection;
        selection.kind = TemporalSelectionKind::LiveClock;
        selection.startValue = utcInstant;
        for (const auto& item : _entries)
        {
            if (!item.second.adapter->supportsLiveClock()) continue;
            const TemporalRequestResult result = request(item.first, selection);
            if (result.ok) tokens.push_back(result.token);
        }
        return tokens;
    }

    const EarthTemporalState* EarthTemporalController::state(
        const std::string& adapterId) const
    {
        const auto found = _entries.find(adapterId);
        return found == _entries.end() ? nullptr : &found->second.state;
    }
}
