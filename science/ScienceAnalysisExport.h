#ifndef OSGSOL_SCIENCE_ANALYSIS_EXPORT_H
#define OSGSOL_SCIENCE_ANALYSIS_EXPORT_H

#include "ScienceQueryTypes.h"

#include <string>

namespace earthscience
{
    struct ScienceExportOptions
    {
        bool includeRawComponents = false;
    };

    std::string exportAnalysisCsv(
        const ScienceArtifact& artifact,
        const ScienceExportOptions& options = ScienceExportOptions{});

    std::string exportAnalysisJson(
        const ScienceArtifact& artifact,
        const ScienceExportOptions& options = ScienceExportOptions{});
}

#endif
