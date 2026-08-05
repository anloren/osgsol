#include "science_temporal_adapter.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace
{
    std::string yearBoundary(int year, bool end)
    {
        std::ostringstream value;
        value << std::setfill('0') << std::setw(4) << year
              << (end ? "-12-31T23:59:59Z" : "-01-01T00:00:00Z");
        return value.str();
    }

    bool annualSource(const std::string& id)
    {
        return id == "alphaearth-foundations" ||
            id == "era5-land-surface-history" ||
            id == "era5-agricultural-climate";
    }
}

std::unique_ptr<earthproject::IEarthTemporalAdapter>
makeScienceTemporalAdapter(
    const earthscience::ScienceSourceDescriptor& source)
{
    if (source.firstYear <= 0 || source.lastYear < source.firstYear)
        return nullptr;
    earthproject::BoundedTemporalAdapterOptions options;
    options.id = source.id;
    options.availability.known = true;
    if (annualSource(source.id))
    {
        options.allowDiscreteValues = true;
        options.availability.firstValue = std::to_string(source.firstYear);
        options.availability.lastValue = std::to_string(source.lastYear);
        const int count = source.lastYear - source.firstYear + 1;
        if (count <= static_cast<int>(earthproject::kMaximumTemporalValues))
        {
            for (int year = source.firstYear; year <= source.lastYear; ++year)
                options.availability.values.push_back(std::to_string(year));
        }
    }
    else if (source.id == "sentinel-2-l2a")
    {
        options.allowInstant = true;
        options.allowInterval = true;
        // The workbench composes a year draft first, then converts that draft
        // into an exact STAC search interval immediately before submission.
        options.allowDiscreteValues = true;
        options.availability.firstValue = yearBoundary(source.firstYear, false);
        options.availability.lastValue = yearBoundary(source.lastYear, true);
    }
    else
        return nullptr;
    return std::unique_ptr<earthproject::IEarthTemporalAdapter>(
        new earthproject::BoundedEarthTemporalAdapter(std::move(options)));
}

earthproject::EarthTemporalSelection earthTemporalSelectionForQuery(
    const earthscience::GeoTemporalQuery& query)
{
    earthproject::EarthTemporalSelection selection;
    switch (query.time.mode)
    {
    case earthscience::ScienceTimeMode::Instant:
        selection.kind = earthproject::TemporalSelectionKind::Instant;
        selection.startValue = query.time.instant;
        break;
    case earthscience::ScienceTimeMode::Interval:
        selection.kind = earthproject::TemporalSelectionKind::Interval;
        selection.startValue = query.time.intervalStart;
        selection.endValue = query.time.intervalEnd;
        break;
    case earthscience::ScienceTimeMode::ExplicitYears:
        selection.kind = earthproject::TemporalSelectionKind::DiscreteValues;
        selection.values.reserve(query.time.explicitYears.size());
        for (int year : query.time.explicitYears)
            selection.values.push_back(std::to_string(year));
        break;
    }
    return selection;
}

earthproject::EarthTemporalSelection earthTemporalSelectionForArtifact(
    const earthscience::ScienceArtifact& artifact)
{
    if (artifact.query.sourceId == "sentinel-2-l2a")
    {
        for (const earthscience::ScienceSourceReference& reference :
             artifact.sourceReferences)
        {
            if (reference.sourceId != artifact.query.sourceId ||
                reference.acquisitionTime.empty())
                continue;
            earthproject::EarthTemporalSelection selection;
            selection.kind = earthproject::TemporalSelectionKind::Instant;
            selection.startValue = reference.acquisitionTime;
            return selection;
        }
    }
    return earthTemporalSelectionForQuery(artifact.query);
}
