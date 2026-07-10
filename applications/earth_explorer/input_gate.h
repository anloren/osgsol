#ifndef EARTH_EXPLORER_INPUT_GATE_H
#define EARTH_EXPLORER_INPUT_GATE_H

#include <set>

namespace earthinput
{

// 全局键盘闸的纯判定逻辑(无 ImGui/OSG 依赖,可直接进单测)。
//
// 语义:ImGui 占用键盘期间(打字 WantTextInput / 控件激活 WantCaptureKeyboard),
// KEYDOWN 记账并吞掉;与之配对的 KEYUP **无论松开时 ImGui 是否仍占用键盘**一律吞掉。
// 配对吞的动机:EnvironmentHandler('o'/'i'/'p')等只认 KEYUP 的处理器,若用户
// "按下时在打字、松开前点走焦点",裸判占用态会把这枚 KEYUP 漏过去,动作照样误触。
// 反向(按下时不在打字、松开时在打字)的 KEYUP 也吞——KEYUP 触发型动作同样不该
// 在打字态发生;KEYDOWN 触发型处理器早已在按下那一刻做完动作,吞 KEYUP 无副作用。
class KeyGateLogic
{
public:
    // isKeyDown: true=KEYDOWN / false=KEYUP;key: osgGA 键值;
    // imguiWantsKeyboard: ImGui 是否占用键盘(WantTextInput || WantCaptureKeyboard)。
    // 返回 true = 事件应被吞掉(调用方返回 true 让 OSG 标记 handled)。
    bool filter(bool isKeyDown, int key, bool imguiWantsKeyboard)
    {
        if (isKeyDown)
        {
            if (imguiWantsKeyboard) { _swallowedDown.insert(key); return true; }
            _swallowedDown.erase(key);   // 正常按下:清掉可能残留的陈旧记账
            return false;
        }

        std::set<int>::iterator it = _swallowedDown.find(key);
        if (it != _swallowedDown.end()) { _swallowedDown.erase(it); return true; }
        return imguiWantsKeyboard;
    }

private:
    std::set<int> _swallowedDown;   // 打字态按下、尚未松开的键
};

}

#endif
