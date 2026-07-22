#include "rml_input_bridge.h"

#include <algorithm>
#include <cmath>
#include <vector>

RmlInputBridge::RmlInputBridge(RmlInputSink& sink) : _sink(sink) {}

void RmlInputBridge::setUiFocused(bool focused)
{
    if (_focused == focused) return;
    if (!focused)
    {
        const std::vector<int> pressed(_pressedButtons.begin(),
                                       _pressedButtons.end());
        for (int button : pressed)
            _sink.processPointerButton(button, false, 0);
        _pressedButtons.clear();
        _markedText.clear();
    }
    _focused = focused;
    _sink.focusChanged(focused);
}

bool RmlInputBridge::processWheel(float x, float y, int modifiers)
{
    if (!_focused || !std::isfinite(x) || !std::isfinite(y)) return false;
    // Every wheel event is forwarded independently. No direction or edge is
    // latched here: RmlUi may bubble an unconsumed nested scroll to the map.
    return _sink.processWheel(x, y, modifiers);
}

bool RmlInputBridge::processPointerButton(int button, bool pressed,
                                          int modifiers)
{
    if (!_focused || button < 0) return false;
    if (pressed) _pressedButtons.insert(button);
    else _pressedButtons.erase(button);
    return _sink.processPointerButton(button, pressed, modifiers);
}

bool RmlInputBridge::processKey(RmlInputKey key, bool pressed, int modifiers)
{
    if (!_focused || key == RmlInputKey::Unknown) return false;
    return _sink.processKey(key, pressed, modifiers);
}

bool RmlInputBridge::processKeyTap(RmlInputKey key, int modifiers)
{
    if (!_focused || key == RmlInputKey::Unknown) return false;
    const bool down = _sink.processKey(key, true, modifiers);
    const bool up = _sink.processKey(key, false, modifiers);
    return down || up;
}

void RmlInputBridge::setMarkedText(const std::string& utf8)
{
    if (!_focused) return;
    _markedText = utf8;
}

void RmlInputBridge::cancelMarkedText()
{
    _markedText.clear();
}

bool RmlInputBridge::commitText(const std::string& utf8)
{
    _markedText.clear();
    if (!_focused || utf8.empty()) return false;
    return _sink.processText(utf8);
}
