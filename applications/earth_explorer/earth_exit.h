#ifndef OSGVERSE_EARTH_EXIT_H
#define OSGVERSE_EARTH_EXIT_H

#include <atomic>
#include <limits>
#if defined(__APPLE__)
#include <osg/ApplicationUsage>
#endif

namespace earthexit
{

// ImGui is rendered by an OSG graphics thread.  A button in that callback must
// not end Viewer::run() directly because the main thread can otherwise start
// destroying callback owners while the draw callback is still using them.
// The render thread only publishes this one-way request; event traversal owns
// the actual ViewerBase::setDone() call.
class QuitRequest
{
public:
    void request()
    {
        _requested.store(true, std::memory_order_release);
    }

    bool consume()
    {
        return _requested.exchange(false, std::memory_order_acq_rel);
    }

private:
    std::atomic<bool> _requested{false};
};

inline bool parsePositiveFrameCount(const char* text, unsigned int& frames)
{
    frames = 0;
    if (!text || !*text) return false;

    unsigned int parsed = 0;
    for (const char* cursor = text; *cursor; ++cursor)
    {
        if (*cursor < '0' || *cursor > '9') return false;
        const unsigned int digit = static_cast<unsigned int>(*cursor - '0');
        if (parsed > (std::numeric_limits<unsigned int>::max() - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }
    if (parsed == 0) return false;
    frames = parsed;
    return true;
}

#if defined(__APPLE__)
// OSG 3.6.5 owns ApplicationUsage through a function-local ref_ptr and releases it from
// __cxa_finalize after main returns.  Two real macOS Earth sessions have shown its final map
// teardown entering libsystem_malloc with an invalid node pointer.  Usage metadata is process-lifetime
// state, so retain exactly one process-lifetime reference and let macOS reclaim it with the rest
// of the address space.  This preserves the normal viewer shutdown and exit(0) path: no _Exit,
// signal interception, crash suppression, or system setting is involved.
inline osg::ApplicationUsage* pinApplicationUsageForProcessLifetime()
{
    static osg::ApplicationUsage* const usage = []() {
        osg::ApplicationUsage* value = osg::ApplicationUsage::instance();
        value->ref();
        return value;
    }();
    return usage;
}
#endif

}

#endif
