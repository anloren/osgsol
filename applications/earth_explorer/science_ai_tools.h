#ifndef EARTH_SCIENCE_AI_TOOLS_H
#define EARTH_SCIENCE_AI_TOOLS_H

class LayerManager;
class SciencePreviewLayer;
namespace osgVerse { class EarthManipulator; }
namespace earthai { class ToolRegistry; }
namespace earthscience { class SciencePreviewRuntime; }

void registerScienceResearchTools(
    earthai::ToolRegistry* tools,
    earthscience::SciencePreviewRuntime* runtime,
    SciencePreviewLayer* layer,
    LayerManager* layers,
    osgVerse::EarthManipulator* manipulator);

#endif
