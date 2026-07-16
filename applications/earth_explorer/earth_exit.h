#ifndef OSGVERSE_EARTH_EXIT_H
#define OSGVERSE_EARTH_EXIT_H

#include <limits>

namespace earthexit
{

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

}

#endif
