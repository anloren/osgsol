#include <osg/ApplicationUsage>
#include <osg/ArgumentParser>
#include <osgViewer/Viewer>

int main(int argc, char** argv)
{
    osg::ArgumentParser arguments(&argc, argv);
    osg::ApplicationUsage::instance()->setApplicationName(arguments.getApplicationName());
    osgViewer::Viewer viewer;
    viewer.setDone(true);
    return viewer.run();
}
