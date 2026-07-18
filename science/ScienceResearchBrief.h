#ifndef OSGSOL_SCIENCE_RESEARCH_BRIEF_H
#define OSGSOL_SCIENCE_RESEARCH_BRIEF_H

#include <cstddef>
#include <string>
#include <vector>

#include "ScienceResearchTypes.h"

namespace earthscience
{
    struct ScienceBriefStatement
    {
        std::string label;
        std::string text;
        std::vector<std::string> evidenceIds;
    };

    struct ScienceBriefSourceRow
    {
        std::size_t citationNumber = 0;
        std::string evidenceId;
        std::string sourceId;
        std::string sourceName;
        std::string datasetId;
        std::string selectedTime;
        ScienceWgs84Bounds coverage;
    };

    struct ScienceBriefCitation
    {
        std::size_t number = 0;
        std::string evidenceId;
        std::string sourceName;
        std::string datasetId;
        std::string providerVersion;
        std::string originalUrl;
        std::string attribution;
    };

    struct ScienceResearchBrief
    {
        std::string researchId;
        std::string question;
        ScienceResearchState state = ScienceResearchState::Draft;
        std::vector<ScienceBriefStatement> observations;
        std::vector<ScienceBriefStatement> inferences;
        std::vector<ScienceBriefStatement> limitations;
        std::vector<ScienceBriefSourceRow> sources;
        std::vector<ScienceBriefCitation> citations;
        std::string markdown;
    };

    bool buildScienceResearchBrief(
        const ScienceResearchRecord& research,
        const std::vector<ScienceEvidenceRecord>& evidence,
        ScienceResearchBrief& brief,
        std::string& error);
}

#endif
