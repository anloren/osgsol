#include <cmath>
#include <cstdlib>
#include <iostream>

#include <osg/Geode>
#include <osg/ShapeDrawable>
#include <osgGA/GUIEventAdapter>
#include <osgViewer/View>

#include <readerwriter/EarthManipulator.h>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::abort(); } } while (0)

namespace
{
    const double kSurfaceRadius = 1000.0;
    const double kTolerance = 1e-9;
    const double kRadiusTolerance = 1e-2;

    bool vecNear(const osg::Vec3d& lhs, const osg::Vec3d& rhs, double tolerance)
    {
        return (lhs - rhs).length() <= tolerance;
    }

    bool matrixNear(const osg::Matrixd& lhs, const osg::Matrixd& rhs, double tolerance)
    {
        for (unsigned int row = 0; row < 4; ++row)
        {
            for (unsigned int column = 0; column < 4; ++column)
            {
                if (std::fabs(lhs(row, column) - rhs(row, column)) > tolerance)
                    return false;
            }
        }
        return true;
    }

    bool matrixFinite(const osg::Matrixd& matrix)
    {
        for (unsigned int row = 0; row < 4; ++row)
        {
            for (unsigned int column = 0; column < 4; ++column)
            {
                if (!std::isfinite(matrix(row, column))) return false;
            }
        }
        return true;
    }

    struct Fixture
    {
        Fixture()
        {
            surface = new osg::Geode;
            surface->addDrawable(new osg::ShapeDrawable(
                new osg::Sphere(osg::Vec3(), static_cast<float>(kSurfaceRadius))));

            view = new osgViewer::View;
            view->setSceneData(surface.get());
            view->getCamera()->setProjectionMatrixAsPerspective(35.0, 4.0 / 3.0, 1.0, 20000.0);

            manipulator = new osgVerse::EarthManipulator;
            manipulator->setEllipsoid(new osg::EllipsoidModel(kSurfaceRadius, kSurfaceRadius));
            manipulator->setViewer(view.get());
            manipulator->setWorldNode(surface.get());
            manipulator->setNode(surface.get());
            manipulator->home(0.0);

            osg::Vec3d seededCenter = manipulator->getCenter();
            seededCenter.normalize();
            manipulator->setCenter(seededCenter * (kSurfaceRadius * 0.82));
            view->getCamera()->setViewMatrix(manipulator->getViewMatrix());
        }

        osg::ref_ptr<osgGA::GUIEventAdapter> push(int button, float x, float y)
        {
            osg::ref_ptr<osgGA::GUIEventAdapter> event = new osgGA::GUIEventAdapter;
            event->setEventType(osgGA::GUIEventAdapter::PUSH);
            event->setWindowRectangle(0, 0, 800, 600);
            event->setX(x);
            event->setY(y);
            event->setButton(button);
            event->setButtonMask(button);
            return event;
        }

        osg::ref_ptr<osgGA::GUIEventAdapter> drag(int buttonMask, float x, float y)
        {
            osg::ref_ptr<osgGA::GUIEventAdapter> event = new osgGA::GUIEventAdapter;
            event->setEventType(osgGA::GUIEventAdapter::DRAG);
            event->setWindowRectangle(0, 0, 800, 600);
            event->setX(x);
            event->setY(y);
            event->setButton(0);
            event->setButtonMask(buttonMask);
            return event;
        }

        osg::ref_ptr<osg::Geode> surface;
        osg::ref_ptr<osgViewer::View> view;
        osg::ref_ptr<osgVerse::EarthManipulator> manipulator;
    };

    void checkPressPreservesCamera(int button, float x, float y)
    {
        Fixture fixture;
        const osg::Vec3d centerBefore = fixture.manipulator->getCenter();
        const osg::Matrixd matrixBefore = fixture.manipulator->getMatrix();
        osg::ref_ptr<osgGA::GUIEventAdapter> event = fixture.push(button, x, y);

        CHECK(fixture.manipulator->handle(*event, *fixture.view));
        CHECK(vecNear(fixture.manipulator->getCenter(), centerBefore, kTolerance));
        CHECK(matrixNear(fixture.manipulator->getMatrix(), matrixBefore, kTolerance));
    }

    void checkRightPressUpdatesViewingRadius()
    {
        Fixture fixture;
        const osg::Vec3d centerBefore = fixture.manipulator->getCenter();
        osg::ref_ptr<osgGA::GUIEventAdapter> event = fixture.push(
            osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON, 400.0f, 300.0f);

        CHECK(fixture.manipulator->handle(*event, *fixture.view));
        CHECK(!vecNear(fixture.manipulator->getCenter(), centerBefore, kTolerance));
        CHECK(std::fabs(fixture.manipulator->getCenter().length() - kSurfaceRadius) <=
              kRadiusTolerance);
    }

    void checkMiddleDragRotatesCamera()
    {
        Fixture fixture;
        osg::ref_ptr<osgGA::GUIEventAdapter> push = fixture.push(
            osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON, 400.0f, 300.0f);
        CHECK(fixture.manipulator->handle(*push, *fixture.view));
        const osg::Vec3d centerAfterPush = fixture.manipulator->getCenter();
        const osg::Matrixd matrixAfterPush = fixture.manipulator->getMatrix();

        osg::ref_ptr<osgGA::GUIEventAdapter> drag = fixture.drag(
            osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON, 500.0f, 360.0f);
        CHECK(fixture.manipulator->handle(*drag, *fixture.view));
        const osg::Matrixd matrixAfterDrag = fixture.manipulator->getMatrix();

        CHECK(vecNear(fixture.manipulator->getCenter(), centerAfterPush, kTolerance));
        CHECK(!matrixNear(matrixAfterDrag, matrixAfterPush, kTolerance));
        CHECK(matrixFinite(matrixAfterDrag));
    }
}

int main(int, char**)
{
    // Control: the established left-button path does not acquire a tilt pivot.
    checkPressPreservesCamera(osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON, 400.0f, 300.0f);

    // A middle press only primes rotation. It must not rebase the camera before a drag,
    // regardless of where the press begins.
    checkPressPreservesCamera(osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON, 400.0f, 300.0f);
    checkPressPreservesCamera(osgGA::GUIEventAdapter::MIDDLE_MOUSE_BUTTON, 655.0f, 185.0f);
    checkPressPreservesCamera(osgGA::GUIEventAdapter::LEFT_MOUSE_BUTTON |
                              osgGA::GUIEventAdapter::RIGHT_MOUSE_BUTTON,
                              400.0f, 300.0f);

    // Control: right-button scaling retains its established center-radius setup.
    checkRightPressUpdatesViewingRadius();

    // Middle-button setup still acquires a usable pivot: a subsequent drag rotates safely.
    checkMiddleDragRotatesCamera();

    std::cout << "[earth_manipulator_tests] button-specific PUSH setup and middle DRAG pass\n";
    return 0;
}
