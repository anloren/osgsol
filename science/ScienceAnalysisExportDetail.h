#ifndef OSGSOL_SCIENCE_ANALYSIS_EXPORT_DETAIL_H
#define OSGSOL_SCIENCE_ANALYSIS_EXPORT_DETAIL_H

#include "ScienceQueryTypes.h"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <locale>
#include <numeric>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace earthscience
{
namespace analysisexportdetail
{
    inline constexpr const char* SCHEMA_VERSION =
        "osgsol-science-analysis-evidence-v1";
    inline constexpr const char* CSV_MISSING_VALUE = "NA";

    inline std::vector<int> evidenceYears(const ScienceArtifact& artifact)
    {
        std::vector<int> years;
        if (artifact.embedding.years)
            years = *artifact.embedding.years;
        else
            years = artifact.query.time.explicitYears;
        std::sort(years.begin(), years.end());
        years.erase(std::unique(years.begin(), years.end()), years.end());
        return years;
    }

    inline const char* geometryKindName(ScienceGeometryKind kind)
    {
        switch (kind)
        {
        case ScienceGeometryKind::Point: return "point";
        case ScienceGeometryKind::BoundingBox: return "bounding-box";
        case ScienceGeometryKind::CurrentView: return "current-view";
        }
        return "unknown";
    }

    inline std::vector<std::size_t> sortedSourceIndices(
        const ScienceArtifact& artifact)
    {
        std::vector<std::size_t> order(artifact.sourceReferences.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&artifact](std::size_t left, std::size_t right) {
                      const ScienceSourceReference& a =
                          artifact.sourceReferences[left];
                      const ScienceSourceReference& b =
                          artifact.sourceReferences[right];
                      return std::tie(a.sourceId, a.datasetId,
                                      a.providerVersion, a.originalUrl) <
                             std::tie(b.sourceId, b.datasetId,
                                      b.providerVersion, b.originalUrl);
                  });
        return order;
    }

    inline std::vector<std::string> sortedUnique(
        std::vector<std::string> values)
    {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        return values;
    }

    inline std::vector<std::string> upstreamSourceIds(
        const ScienceArtifact& artifact)
    {
        std::vector<std::string> values;
        if (!artifact.query.sourceId.empty())
            values.push_back(artifact.query.sourceId);
        for (const ScienceSourceReference& source : artifact.sourceReferences)
        {
            if (!source.sourceId.empty()) values.push_back(source.sourceId);
        }
        return sortedUnique(std::move(values));
    }

    inline std::vector<std::string> upstreamDatasetIds(
        const ScienceArtifact& artifact)
    {
        std::vector<std::string> values;
        for (const ScienceSourceReference& source : artifact.sourceReferences)
        {
            if (!source.datasetId.empty()) values.push_back(source.datasetId);
        }
        return sortedUnique(std::move(values));
    }

    inline std::vector<std::string> upstreamUrls(
        const ScienceArtifact& artifact)
    {
        std::vector<std::string> values;
        for (const ScienceSourceReference& source : artifact.sourceReferences)
        {
            if (!source.originalUrl.empty())
                values.push_back(source.originalUrl);
        }
        return sortedUnique(std::move(values));
    }

    inline std::vector<std::string> providerVersions(
        const ScienceArtifact& artifact)
    {
        std::vector<std::string> values;
        for (const ScienceSourceReference& source : artifact.sourceReferences)
        {
            if (!source.providerVersion.empty())
                values.push_back(source.providerVersion);
        }
        return sortedUnique(std::move(values));
    }

    inline std::vector<std::string> evidenceWarnings(
        const ScienceArtifact& artifact)
    {
        std::vector<std::string> warnings = artifact.warnings;
        if (artifact.embedding.warnings)
        {
            warnings.insert(warnings.end(),
                            artifact.embedding.warnings->begin(),
                            artifact.embedding.warnings->end());
        }
        return warnings;
    }

    inline std::size_t sampleCount(
        const ScienceArtifact& artifact, const std::vector<int>& years)
    {
        if (artifact.embedding.width <= 0 ||
            artifact.embedding.height <= 0 || years.empty())
            return 0;
        const std::size_t width =
            static_cast<std::size_t>(artifact.embedding.width);
        const std::size_t height =
            static_cast<std::size_t>(artifact.embedding.height);
        if (width > std::numeric_limits<std::size_t>::max() / height)
            return 0;
        const std::size_t cells = width * height;
        if (years.size() >
            std::numeric_limits<std::size_t>::max() / cells)
            return 0;
        return years.size() * cells;
    }

    inline std::string componentName(int component)
    {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << 'A' << std::setw(2) << std::setfill('0') << component;
        return stream.str();
    }
}
}

#endif
