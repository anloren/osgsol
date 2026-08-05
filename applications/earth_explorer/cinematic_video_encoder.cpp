#include "cinematic_video_encoder.h"

namespace earthai
{
    bool encodePngSequenceToH264Mp4(
        const std::vector<std::string>&,
        int,
        const std::string&,
        std::string& error)
    {
        error = "deterministic H.264 orbit encoding is currently available on macOS only";
        return false;
    }
}
