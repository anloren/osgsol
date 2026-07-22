#pragma once

#include <string>
#include <unordered_set>

enum class RmlInputKey
{
    Unknown,
    Backspace,
    Delete,
    Escape,
    Return,
    Tab,
    Left,
    Right,
    Up,
    Down,
    Home,
    End,
    PageUp,
    PageDown,
    A,
    C,
    V,
    X
};

class RmlInputSink
{
public:
    virtual ~RmlInputSink() = default;
    virtual bool processWheel(float x, float y, int modifiers) = 0;
    virtual bool processPointerButton(int button, bool pressed,
                                      int modifiers) = 0;
    virtual bool processKey(RmlInputKey key, bool pressed,
                            int modifiers) = 0;
    virtual bool processText(const std::string& utf8) = 0;
    virtual void focusChanged(bool focused) = 0;
};

// Owns only normalized input state. It deliberately has no renderer or OSG
// dependency, so edge scrolling, focus transfer, pointer capture and IME
// composition remain deterministic and unit-testable.
class RmlInputBridge
{
public:
    explicit RmlInputBridge(RmlInputSink& sink);

    void setUiFocused(bool focused);
    bool uiFocused() const { return _focused; }

    bool processWheel(float x, float y, int modifiers);
    bool processPointerButton(int button, bool pressed, int modifiers);
    bool processKey(RmlInputKey key, bool pressed, int modifiers);
    bool processKeyTap(RmlInputKey key, int modifiers);

    void setMarkedText(const std::string& utf8);
    const std::string& markedText() const { return _markedText; }
    void cancelMarkedText();
    bool commitText(const std::string& utf8);

private:
    RmlInputSink& _sink;
    bool _focused = false;
    std::unordered_set<int> _pressedButtons;
    std::string _markedText;
};
