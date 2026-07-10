#ifndef OSGVERSE_UI_IMGUISCROLL_H
#define OSGVERSE_UI_IMGUISCROLL_H

#include <osgGA/GUIEventAdapter>

namespace osgVerse
{
    inline float resolveImGuiWheelAmount(const osgGA::GUIEventAdapter& event)
    {
        osgGA::GUIEventAdapter::ScrollingMotion motion = event.getScrollingMotion();
        if (motion == osgGA::GUIEventAdapter::SCROLL_2D)
            return event.getScrollingDeltaY();
        if (motion == osgGA::GUIEventAdapter::SCROLL_UP) return 1.0f;
        if (motion == osgGA::GUIEventAdapter::SCROLL_DOWN) return -1.0f;
        return 0.0f;
    }
}

#endif
