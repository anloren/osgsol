#ifndef EARTH_SCIENCE_PLUGIN_API_H
#define EARTH_SCIENCE_PLUGIN_API_H

#include <cstddef>
#include <cstdint>

class LayerManager;
namespace earthai { class ToolRegistry; }
namespace osg { class Node; }
namespace osgVerse { class EarthManipulator; }

static const std::uint32_t OSGSOL_SCIENCE_PLUGIN_ABI_V1 = 1u;

struct OsgSolSciencePluginApiV1
{
    std::uint32_t abiVersion;
    std::uint32_t structSize;
    void* (*create)(const char* indexPath, char* error, std::size_t errorSize);
    void (*destroy)(void* session);
    osg::Node* (*sceneNode)(void* session);
    void (*setVisible)(void* session, bool visible);
    void (*registerAiTools)(void* session,
                            earthai::ToolRegistry* tools,
                            LayerManager* layers,
                            osgVerse::EarthManipulator* manipulator);
    void (*drawOperations)(void* session,
                           LayerManager* layers,
                           osgVerse::EarthManipulator* manipulator);
    void (*drawResults)(void* session, LayerManager* layers);
};

typedef const OsgSolSciencePluginApiV1* (*OsgSolScienceAnchor)();

#endif
