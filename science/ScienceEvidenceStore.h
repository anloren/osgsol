#ifndef OSGSOL_SCIENCE_EVIDENCE_STORE_H
#define OSGSOL_SCIENCE_EVIDENCE_STORE_H

#include <string>

#include "ScienceResearchTypes.h"

namespace earthscience
{
    std::string defaultScienceResearchRoot();

    bool makeScienceEvidence(
        const ScienceArtifact& artifact,
        const ScienceSourceDescriptor& source,
        ScienceEvidenceRecord& output,
        std::string& error);

    class ScienceEvidenceStore
    {
    public:
        explicit ScienceEvidenceStore(std::string root);

        const std::string& root() const { return _root; }
        bool saveEvidence(
            const ScienceEvidenceRecord& record, std::string& error) const;
        bool loadEvidence(
            const std::string& evidenceId,
            ScienceEvidenceRecord& record,
            std::string& error) const;
        bool saveResearch(
            const ScienceResearchRecord& record, std::string& error) const;
        bool loadResearch(
            const std::string& researchId,
            ScienceResearchRecord& record,
            std::string& error) const;

    private:
        std::string _root;
    };
}

#endif
