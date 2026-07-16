#ifndef OSGSOL_SCIENCE_ANALYSIS_ENGINE_H
#define OSGSOL_SCIENCE_ANALYSIS_ENGINE_H

#include "ScienceQueryTypes.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace earthscience
{
    class ScienceAnalysisEngine
    {
    public:
        static std::shared_ptr<const ScienceAnalysisPayload>
        analyzePointSeries(
            const ScienceEmbeddingPayload& embedding,
            const ScienceAnalysisOptions& options,
            const std::function<bool()>& cancelled, std::string& error);

        static std::shared_ptr<const ScienceAnalysisPayload>
        analyzeRegionalChange(
            const ScienceEmbeddingPayload& embedding,
            const ScienceAnalysisOptions& options,
            const std::function<bool()>& cancelled, std::string& error);

        using YearSliceLoader = std::function<
            std::shared_ptr<const ScienceEmbeddingPayload>(
                int year, std::string& error)>;

        static bool analyzeRegionalAnnualSummaries(
            const std::vector<int>& years, const YearSliceLoader& loadYear,
            ScienceAnalysisPayload& output,
            const std::function<bool()>& cancelled, std::string& error);

        static bool computeLocalPca(
            const ScienceEmbeddingPayload& embedding, int componentCount,
            ScienceAnalysisPayload& output,
            const std::function<bool()>& cancelled, std::string& error);

        static bool computeSphericalClusters(
            const ScienceEmbeddingPayload& embedding, int clusterCount,
            ScienceAnalysisPayload& output,
            const std::function<bool()>& cancelled, std::string& error);

        static ScienceRasterPayload materializeChangeRaster(
            const ScienceAnalysisPayload& analysis,
            const ScienceEmbeddingPayload& embedding, std::string& error);
    };
}

#endif
