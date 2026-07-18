#ifndef OSGSOL_SCIENCE_PROVIDER_H
#define OSGSOL_SCIENCE_PROVIDER_H

#include <cstdint>
#include <memory>
#include <string>

#include "ScienceQueryTypes.h"

namespace earthscience
{
    struct ScienceProviderSnapshot
    {
        std::uint64_t generation = 0;
        ScienceJobState state = ScienceJobState::Unavailable;
        ScienceProgress progress;
        std::string message;
        std::shared_ptr<const ScienceArtifact> artifact;
    };

    class IScienceProvider
    {
    public:
        virtual ~IScienceProvider() = default;

        virtual ScienceSourceDescriptor descriptor() const = 0;
        virtual bool validateQuery(
            const GeoTemporalQuery& query, std::string& error) const
        {
            static_cast<void>(query);
            error.clear();
            return true;
        }
        virtual std::uint64_t submit(const GeoTemporalQuery& query) = 0;
        virtual ScienceProviderSnapshot snapshot() const = 0;
        virtual void cancel(std::uint64_t generation) = 0;
        virtual void clear() = 0;
    };
}

#endif
