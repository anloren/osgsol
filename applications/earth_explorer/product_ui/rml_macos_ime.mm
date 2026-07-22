#if defined(__APPLE__)

#include "rml_macos_ime.h"
#include "rml_ui_runtime.h"
#include "../ime_bridge.h"

namespace
{
void commit(void* userData, const char* utf8)
{
    static_cast<RmlUiRuntime*>(userData)->enqueueCommittedText(utf8 ? utf8 : "");
}

void mark(void* userData, const char* utf8)
{
    static_cast<RmlUiRuntime*>(userData)->enqueueMarkedText(utf8 ? utf8 : "");
}

void cancel(void* userData)
{
    static_cast<RmlUiRuntime*>(userData)->enqueueCancelComposition();
}

void key(void* userData, int value)
{
    RmlInputKey keyValue = RmlInputKey::Unknown;
    switch (value)
    {
    case 1: keyValue = RmlInputKey::Return; break;
    case 2: keyValue = RmlInputKey::Tab; break;
    case 3: keyValue = RmlInputKey::Backspace; break;
    case 4: keyValue = RmlInputKey::Delete; break;
    case 5: keyValue = RmlInputKey::Left; break;
    case 6: keyValue = RmlInputKey::Right; break;
    case 7: keyValue = RmlInputKey::Up; break;
    case 8: keyValue = RmlInputKey::Down; break;
    case 9: keyValue = RmlInputKey::Home; break;
    case 10: keyValue = RmlInputKey::End; break;
    case 11: keyValue = RmlInputKey::Escape; break;
    case 12: keyValue = RmlInputKey::A; break;
    case 13: keyValue = RmlInputKey::C; break;
    case 14: keyValue = RmlInputKey::X; break;
    default: break;
    }
    if (keyValue != RmlInputKey::Unknown)
        static_cast<RmlUiRuntime*>(userData)->enqueueKeyTap(
            keyValue, value >= 12 ? (1 << 3) : 0); // Rml::Input::KM_META
}
}

namespace rmlmacime
{
void connect(RmlUiRuntime* runtime)
{
    earthime::ProductTextTarget target;
    target.userData = runtime;
    target.commitText = commit;
    target.setMarkedText = mark;
    target.cancelComposition = cancel;
    target.keyTap = key;
    earthime::setProductTextTarget(target);
}

void disconnect()
{
    earthime::clearProductTextTarget();
}
}

#endif
