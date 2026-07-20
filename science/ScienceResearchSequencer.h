#ifndef OSGSOL_SCIENCE_RESEARCH_SEQUENCER_H
#define OSGSOL_SCIENCE_RESEARCH_SEQUENCER_H

#include <memory>
#include <string>
#include <vector>

#include "ScienceResearchTypes.h"

namespace earthscience
{
    class ScienceQueryService;
    class ScienceResearchManager;

    struct ScienceResearchRequestStep
    {
        std::string sourceId;
        GeoTemporalQuery query;
    };

    class ScienceResearchSequencer
    {
    public:
        ScienceResearchSequencer(
            ScienceQueryService* service,
            ScienceResearchManager* manager);
        ~ScienceResearchSequencer();

        ScienceResearchSequencer(const ScienceResearchSequencer&) = delete;
        ScienceResearchSequencer& operator=(
            const ScienceResearchSequencer&) = delete;

        std::string start(
            const std::string& question,
            const std::vector<ScienceResearchRequestStep>& steps,
            std::string& error);
        ScienceResearchRecord poll(
            const std::string& researchId,
            std::string& error);
        bool cancel(
            const std::string& researchId,
            std::string& error);

    private:
        class Impl;
        std::unique_ptr<Impl> _impl;
    };
}

#endif
