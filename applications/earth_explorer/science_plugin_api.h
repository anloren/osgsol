#ifndef EARTH_SCIENCE_PLUGIN_API_H
#define EARTH_SCIENCE_PLUGIN_API_H

#include <cstddef>
#include <cstdint>

class LayerManager;
namespace earthai { class ToolRegistry; }
namespace osg { class Node; }
namespace osgVerse { class EarthManipulator; }

static const std::uint32_t OSGSOL_SCIENCE_PLUGIN_ABI_V3 = 3u;

typedef void* (*OsgSolScienceAllocateFunction)(std::size_t size,
                                               void* userData);
typedef void (*OsgSolScienceDeallocateFunction)(void* pointer,
                                                void* userData);

struct OsgSolScienceGuiBridgeV1
{
    void* context;
    OsgSolScienceAllocateFunction allocate;
    OsgSolScienceDeallocateFunction deallocate;
    void* allocatorUserData;
};

struct OsgSolGeoRasterFrameV1
{
    std::uint32_t structSize;
    std::uint64_t generation;
    std::int32_t width;
    std::int32_t height;
    std::uint64_t rowBytes;
    const unsigned char* rgba;
    double west;
    double south;
    double east;
    double north;
};

typedef bool (*OsgSolPublishGeoRasterFunction)(
    const OsgSolGeoRasterFrameV1* frame,
    void* userData,
    char* error,
    std::size_t errorSize);

struct OsgSolGeoRasterBridgeV1
{
    std::uint32_t structSize;
    void* userData;
    OsgSolPublishGeoRasterFunction publishCopy;
    void (*clear)(std::uint64_t generation, void* userData);
};

struct OsgSolSciencePluginApiV3
{
    std::uint32_t abiVersion;
    std::uint32_t structSize;
    void* (*create)(const char* indexPath, char* error, std::size_t errorSize);
    void (*destroy)(void* session);
    osg::Node* (*sceneNode)(void* session);
    void (*setVisible)(void* session, bool visible);
    void (*bindGeoRaster)(void* session,
                          const OsgSolGeoRasterBridgeV1* bridge);
    void (*registerAiTools)(void* session,
                            earthai::ToolRegistry* tools,
                            LayerManager* layers,
                            osgVerse::EarthManipulator* manipulator);
    void (*bindGui)(void* session,
                    const OsgSolScienceGuiBridgeV1* bridge);
    void (*drawOperations)(void* session,
                           LayerManager* layers,
                           osgVerse::EarthManipulator* manipulator);
    void (*drawResults)(void* session, LayerManager* layers);
};

typedef const OsgSolSciencePluginApiV3* (*OsgSolScienceAnchor)();

#endif
