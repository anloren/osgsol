#ifndef OSGSOL_SCIENCE_TEMPORAL_ADAPTER_H
#define OSGSOL_SCIENCE_TEMPORAL_ADAPTER_H

#include "project/earth_temporal_controller.h"

#include <ScienceQueryTypes.h>

#include <memory>

std::unique_ptr<earthproject::IEarthTemporalAdapter>
makeScienceTemporalAdapter(
    const earthscience::ScienceSourceDescriptor& source);

earthproject::EarthTemporalSelection earthTemporalSelectionForQuery(
    const earthscience::GeoTemporalQuery& query);

earthproject::EarthTemporalSelection earthTemporalSelectionForArtifact(
    const earthscience::ScienceArtifact& artifact);

#endif
