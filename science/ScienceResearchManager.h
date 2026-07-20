#ifndef OSGSOL_SCIENCE_RESEARCH_MANAGER_H
#define OSGSOL_SCIENCE_RESEARCH_MANAGER_H

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "ScienceEvidenceStore.h"

namespace earthscience
{
    class ScienceResearchManager
    {
    public:
        explicit ScienceResearchManager(std::string root);

        bool create(
            const std::string& question,
            ScienceResearchRecord& record,
            std::string& error);
        bool get(
            const std::string& researchId,
            ScienceResearchRecord& record,
            std::string& error);
        bool addStep(
            const std::string& researchId,
            std::uint64_t liveJobId,
            const std::string& sourceId,
            ScienceResearchRecord& record,
            std::string& error);
        bool planSteps(
            const std::string& researchId,
            const std::vector<std::string>& sourceIds,
            ScienceResearchRecord& record,
            std::string& error);
        bool activateStep(
            const std::string& researchId,
            std::size_t stepIndex,
            std::uint64_t liveJobId,
            ScienceResearchRecord& record,
            std::string& error);
        bool markStepTerminal(
            const std::string& researchId,
            std::size_t stepIndex,
            ScienceJobState state,
            const std::string& message,
            ScienceResearchRecord& record,
            std::string& error);
        bool observe(
            const std::string& researchId,
            const ScienceJobSnapshot& snapshot,
            const ScienceSourceDescriptor& source,
            ScienceResearchRecord& record,
            std::string& error);
        bool loadEvidence(
            const std::string& researchId,
            std::vector<ScienceEvidenceRecord>& evidence,
            std::string& error);

        const ScienceEvidenceStore& store() const { return _store; }

    private:
        bool loadUnlocked(
            const std::string& researchId,
            ScienceResearchRecord*& record,
            std::string& error);
        bool persistUnlocked(
            ScienceResearchRecord& record,
            std::string& error);
        static void updateState(ScienceResearchRecord& record);

        ScienceEvidenceStore _store;
        std::map<std::string, ScienceResearchRecord> _records;
        std::mutex _mutex;
        std::uint64_t _counter = 0;
    };
}

#endif
