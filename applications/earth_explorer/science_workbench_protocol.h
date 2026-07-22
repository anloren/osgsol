#ifndef EARTH_SCIENCE_WORKBENCH_PROTOCOL_H
#define EARTH_SCIENCE_WORKBENCH_PROTOCOL_H

#include "science_plugin_api.h"
#include "science_workbench_model.h"

#include <ScienceQueryTypes.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

std::string serializeScienceWorkbenchSnapshot(
    const ScienceWorkbenchViewModel& model,
    const std::vector<earthscience::ScienceSourceDescriptor>& sources,
    std::shared_ptr<const earthscience::ScienceArtifact> activeArtifact);

bool copyScienceWorkbenchSnapshot(
    const std::string& snapshot,
    OsgSolScienceUiBufferV1* output);

bool parseScienceWorkbenchAction(
    const char* utf8,
    std::size_t size,
    ScienceWorkbenchAction& action,
    std::string& error);

#endif
