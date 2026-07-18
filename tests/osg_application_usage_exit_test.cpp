#include <osg/ApplicationUsage>
#include <osg/ArgumentParser>
#include <osgViewer/Viewer>
#include "../applications/earth_explorer/earth_exit.h"

int main(int argc, char** argv)
{
#if defined(__APPLE__)
    osg::ApplicationUsage* usage = osg::ApplicationUsage::instance();
    const int referencesBeforePin = usage->referenceCount();
    if (earthexit::pinApplicationUsageForProcessLifetime() != usage) return 1;
    if (usage->referenceCount() != referencesBeforePin + 1) return 2;
    if (earthexit::pinApplicationUsageForProcessLifetime() != usage) return 3;
    if (usage->referenceCount() != referencesBeforePin + 1) return 4;
#endif
    osg::ArgumentParser arguments(&argc, argv);
    osg::ApplicationUsage::instance()->setApplicationName(arguments.getApplicationName());
    osgViewer::Viewer viewer;
    viewer.setDone(true);
    return viewer.run();
}
