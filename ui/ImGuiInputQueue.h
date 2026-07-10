#ifndef OSGVERSE_UI_IMGUIINPUTQUEUE_H
#define OSGVERSE_UI_IMGUIINPUTQUEUE_H

#include <atomic>
#include <mutex>
#include <vector>

namespace osgVerse
{
    struct ImGuiInputEvent
    {
        enum Type { Key, MousePosition, MouseButtons, MouseWheel, VirtualMouse };
        Type type;
        int key, buttonMask;
        bool down;
        unsigned int modifiers;
        float x, y, wheel;

        static ImGuiInputEvent keyEvent(int keyCode, bool isDown, unsigned int modMask)
        {
            ImGuiInputEvent event = {};
            event.type = Key; event.key = keyCode; event.down = isDown;
            event.modifiers = modMask; return event;
        }

        static ImGuiInputEvent mousePositionEvent(float px, float py)
        {
            ImGuiInputEvent event = {};
            event.type = MousePosition; event.x = px; event.y = py; return event;
        }

        static ImGuiInputEvent mouseButtonsEvent(float px, float py, int mask)
        {
            ImGuiInputEvent event = mousePositionEvent(px, py);
            event.type = MouseButtons; event.buttonMask = mask; return event;
        }

        static ImGuiInputEvent mouseWheelEvent(float amount)
        {
            ImGuiInputEvent event = {};
            event.type = MouseWheel; event.wheel = amount; return event;
        }

        static ImGuiInputEvent virtualMouseEvent(float px, float py, int mask, float amount)
        {
            ImGuiInputEvent event = {};
            event.type = VirtualMouse; event.x = px; event.y = py;
            event.buttonMask = mask; event.wheel = amount; return event;
        }
    };

    class ImGuiInputQueue
    {
    public:
        ImGuiInputQueue() : _wantsMouse(false), _wantsKeyboard(false) {}

        void push(const ImGuiInputEvent& event)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            _events.push_back(event);
        }

        std::vector<ImGuiInputEvent> takeAll()
        {
            std::vector<ImGuiInputEvent> result;
            std::lock_guard<std::mutex> lock(_mutex);
            result.swap(_events);
            return result;
        }

        void publishCapture(bool mouse, bool keyboard)
        {
            _wantsMouse.store(mouse); _wantsKeyboard.store(keyboard);
        }

        bool wantsMouse() const { return _wantsMouse.load(); }
        bool wantsKeyboard() const { return _wantsKeyboard.load(); }

    private:
        std::mutex _mutex;
        std::vector<ImGuiInputEvent> _events;
        std::atomic<bool> _wantsMouse, _wantsKeyboard;
    };
}

#endif
