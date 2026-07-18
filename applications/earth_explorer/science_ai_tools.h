#ifndef EARTH_SCIENCE_AI_TOOLS_H
#define EARTH_SCIENCE_AI_TOOLS_H

#include <string>

class LayerManager;
class SciencePreviewLayer;
namespace osgVerse { class EarthManipulator; }
namespace earthai { class ToolRegistry; }
namespace earthscience { class ScienceQueryService; }

void registerScienceResearchTools(
    earthai::ToolRegistry* tools,
    earthscience::ScienceQueryService* service,
    SciencePreviewLayer* layer,
    LayerManager* layers,
    osgVerse::EarthManipulator* manipulator,
    const std::string& researchRoot = std::string());

#endif
