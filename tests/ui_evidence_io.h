#pragma once

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image_write.h>

#include <filesystem>
#include <vector>

namespace ui_evidence
{
inline bool writePngSibling(const std::filesystem::path& ppmPath,
                            const std::vector<unsigned char>& rgba,
                            int width, int height)
{
    if (width <= 0 || height <= 0 ||
        rgba.size() < static_cast<std::size_t>(width) * height * 4)
        return false;

    std::vector<unsigned char> rgb(
        static_cast<std::size_t>(width) * height * 3);
    for (int screenY = 0; screenY < height; ++screenY)
    {
        const int readY = height - 1 - screenY;
        for (int x = 0; x < width; ++x)
        {
            const std::size_t source =
                (static_cast<std::size_t>(readY) * width + x) * 4;
            const std::size_t target =
                (static_cast<std::size_t>(screenY) * width + x) * 3;
            rgb[target] = rgba[source];
            rgb[target + 1] = rgba[source + 1];
            rgb[target + 2] = rgba[source + 2];
        }
    }

    std::filesystem::path pngPath = ppmPath;
    pngPath.replace_extension(".png");
    return stbi_write_png(
        pngPath.string().c_str(), width, height, 3, rgb.data(),
        width * 3) != 0;
}
}
