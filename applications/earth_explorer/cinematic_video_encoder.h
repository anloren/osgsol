#ifndef EARTH_CINEMATIC_VIDEO_ENCODER_H
#define EARTH_CINEMATIC_VIDEO_ENCODER_H

#include <atomic>
#include <string>
#include <vector>

namespace earthai
{
    // Encode an already-rendered, ordered PNG sequence. Camera continuity is a
    // property of the input sequence; this function never synthesizes, drops,
    // reorders, or interpolates shots.
    bool encodePngSequenceToH264Mp4(
        const std::vector<std::string>& pngPaths,
        int framesPerSecond,
        const std::string& outputPath,
        std::string& error,
        const std::atomic<bool>* cancelRequested = nullptr);
}

#endif
