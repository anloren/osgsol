#ifndef EARTH_RENDER_EFFECTS_H
#define EARTH_RENDER_EFFECTS_H

#include <string>
#include <vector>

namespace osg { class Camera; class Group; class Node; }
namespace osgViewer { class View; }
namespace osgVerse { struct EarthAtmosphereOcean; }
namespace terrainoverlay { class TerrainScienceOverlay; }

std::vector<osg::Camera*> configureEarthRendering(
    osgViewer::View& viewer, osg::Group* root, osg::Node* earth,
    osgVerse::EarthAtmosphereOcean& earthRenderingUtils,
    terrainoverlay::TerrainScienceOverlay* scienceOverlay,
    const std::string& mainFolder, unsigned int mask, int w, int h);

#endif
