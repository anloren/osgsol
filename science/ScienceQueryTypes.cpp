#include "ScienceQueryTypes.h"

namespace earthscience
{
const char* scienceSourceHealthName(ScienceSourceHealth health)
{
    switch (health)
    {
    case ScienceSourceHealth::Unavailable: return "unavailable";
    case ScienceSourceHealth::Ready: return "ready";
    case ScienceSourceHealth::Busy: return "busy";
    case ScienceSourceHealth::Degraded: return "degraded";
    }
    return "unknown";
}

const char* scienceJobStateName(ScienceJobState state)
{
    switch (state)
    {
    case ScienceJobState::Unavailable: return "unavailable";
    case ScienceJobState::Idle: return "idle";
    case ScienceJobState::Queued: return "queued";
    case ScienceJobState::Fetching: return "fetching";
    case ScienceJobState::Ready: return "ready";
    case ScienceJobState::Failed: return "failed";
    case ScienceJobState::Cancelled: return "cancelled";
    }
    return "unknown";
}
}
