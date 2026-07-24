#include "product_ui/rml_input_bridge.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
void expect(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << std::endl;
    std::exit(1);
}

class RecordingSink : public RmlInputSink
{
public:
    bool processWheel(float x, float y, int modifiers) override
    {
        wheelX.push_back(x);
        wheelY.push_back(y);
        wheelModifiers.push_back(modifiers);
        const bool result = nextWheelConsumed;
        nextWheelConsumed = true;
        return result;
    }

    bool processPointerButton(int button, bool pressed, int) override
    {
        pointerButtons.push_back(button);
        pointerStates.push_back(pressed);
        return pointerConsumed;
    }

    bool processKey(RmlInputKey key, bool pressed, int) override
    {
        keys.push_back(key);
        keyStates.push_back(pressed);
        return keyConsumed;
    }

    bool processText(const std::string& utf8) override
    {
        texts.push_back(utf8);
        return textConsumed;
    }

    void focusChanged(bool focused) override
    {
        focusStates.push_back(focused);
    }

    bool nextWheelConsumed = true;
    bool pointerConsumed = true;
    bool keyConsumed = true;
    bool textConsumed = true;
    std::vector<float> wheelX;
    std::vector<float> wheelY;
    std::vector<int> wheelModifiers;
    std::vector<int> pointerButtons;
    std::vector<bool> pointerStates;
    std::vector<RmlInputKey> keys;
    std::vector<bool> keyStates;
    std::vector<std::string> texts;
    std::vector<bool> focusStates;
};

void testWheelAlwaysReachesUiInBothDirections()
{
    RecordingSink sink;
    RmlInputBridge bridge(sink);
    bridge.setUiFocused(true);
    expect(bridge.processWheel(0.0f, -1.0f, 0),
           "downward wheel must be offered to focused UI");
    expect(bridge.processWheel(0.0f, 1.0f, 0),
           "upward wheel after an edge must still be offered to UI");
    expect(sink.wheelY.size() == 2 && sink.wheelY[0] == -1.0f &&
               sink.wheelY[1] == 1.0f,
           "wheel direction must not latch at a scroll edge");
}

void testNestedScrollCanHandUnconsumedWheelToWorld()
{
    RecordingSink sink;
    RmlInputBridge bridge(sink);
    bridge.setUiFocused(true);
    sink.nextWheelConsumed = false;
    expect(!bridge.processWheel(0.0f, -1.0f, 0),
           "unconsumed nested scroll must be returned to caller");
    expect(sink.wheelY.size() == 1,
           "nested scroll handoff must still offer the event exactly once");
}

void testFocusLossReleasesCapturedPointer()
{
    RecordingSink sink;
    RmlInputBridge bridge(sink);
    bridge.setUiFocused(true);
    expect(bridge.processPointerButton(1, true, 0),
           "focused UI must receive pointer press");
    bridge.setUiFocused(false);
    expect(sink.pointerStates.size() == 2 && sink.pointerStates[0] &&
               !sink.pointerStates[1],
           "focus loss must synthesize release for captured pointer");
    expect(sink.focusStates.size() == 2 && sink.focusStates[0] &&
               !sink.focusStates[1],
           "focus transfer must be explicit and balanced");
}

void testNavigationAndActionKeysAreBalancedKeyEvents()
{
    RecordingSink sink;
    RmlInputBridge bridge(sink);
    bridge.setUiFocused(true);
    expect(bridge.processKeyTap(RmlInputKey::Escape, 0),
           "Escape tap must be consumed by focused UI");
    expect(bridge.processKeyTap(RmlInputKey::Return, 0),
           "Return tap must be consumed by focused UI");
    expect(bridge.processKeyTap(RmlInputKey::Tab, 0),
           "Tab tap must be consumed by focused UI");
    expect(sink.keys.size() == 6 &&
               sink.keys[0] == RmlInputKey::Escape &&
               sink.keys[1] == RmlInputKey::Escape &&
               sink.keys[2] == RmlInputKey::Return &&
               sink.keys[3] == RmlInputKey::Return &&
               sink.keys[4] == RmlInputKey::Tab &&
               sink.keys[5] == RmlInputKey::Tab,
           "Escape, Return and Tab must all send down/up pairs");
    expect(sink.keyStates[0] && !sink.keyStates[1] &&
               sink.keyStates[2] && !sink.keyStates[3] &&
               sink.keyStates[4] && !sink.keyStates[5],
           "key tap transitions must be balanced");
}

void testChineseCompositionCommitsOnlyFinalText()
{
    RecordingSink sink;
    RmlInputBridge bridge(sink);
    bridge.setUiFocused(true);
    bridge.setMarkedText("shen");
    bridge.setMarkedText("深圳");
    expect(sink.texts.empty(),
           "marked text must not leak partial composition into document");
    expect(bridge.markedText() == "深圳",
           "new marked text must replace the previous composition");
    expect(bridge.commitText("深圳湾"),
           "committed Chinese text must enter focused document");
    expect(sink.texts.size() == 1 && sink.texts[0] == "深圳湾",
           "only final committed UTF-8 text may be injected");
    expect(bridge.markedText().empty(),
           "commit must clear the marked composition");
}
}

int main()
{
    testWheelAlwaysReachesUiInBothDirections();
    testNestedScrollCanHandUnconsumedWheelToWorld();
    testFocusLossReleasesCapturedPointer();
    testNavigationAndActionKeysAreBalancedKeyEvents();
    testChineseCompositionCommitsOnlyFinalText();
    std::cout << "RmlInputBridge tests passed" << std::endl;
    return 0;
}
