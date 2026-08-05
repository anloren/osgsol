#include <AVFoundation/AVFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../applications/earth_explorer/cinematic_video_encoder.h"

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x << std::endl; \
    std::exit(EXIT_FAILURE); } } while (0)

namespace
{
    bool writeFixturePng(const std::string& path, int frameIndex)
    {
        const std::size_t width = 64, height = 32;
        std::vector<unsigned char> rgba(width * height * 4, 255);
        for (std::size_t y = 0; y < height; ++y)
        {
            for (std::size_t x = 0; x < width; ++x)
            {
                const std::size_t offset = (y * width + x) * 4;
                rgba[offset + 0] = static_cast<unsigned char>(frameIndex * 23);
                rgba[offset + 1] = static_cast<unsigned char>(x * 4);
                rgba[offset + 2] = static_cast<unsigned char>(y * 8);
            }
        }
        CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
        CGDataProviderRef provider = CGDataProviderCreateWithData(
            nullptr, rgba.data(), rgba.size(), nullptr);
        CGImageRef image = CGImageCreate(
            width, height, 8, 32, width * 4, color,
            kCGImageAlphaLast | kCGBitmapByteOrderDefault,
            provider, nullptr, false, kCGRenderingIntentDefault);
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            nullptr, reinterpret_cast<const UInt8*>(path.data()),
            path.size(), false);
        CGImageDestinationRef destination = CGImageDestinationCreateWithURL(
            url, CFSTR("public.png"), 1, nullptr);
        bool ok = destination && image;
        if (ok)
        {
            CGImageDestinationAddImage(destination, image, nullptr);
            ok = CGImageDestinationFinalize(destination);
        }
        if (destination) CFRelease(destination);
        if (url) CFRelease(url);
        if (image) CGImageRelease(image);
        if (provider) CGDataProviderRelease(provider);
        if (color) CGColorSpaceRelease(color);
        return ok;
    }
}

int main(int, char**)
{
    @autoreleasepool
    {
        char directoryTemplate[] = "/tmp/osgsol-cinematic-encoder-XXXXXX";
        char* directory = mkdtemp(directoryTemplate);
        CHECK(directory != nullptr);

        std::vector<std::string> frames;
        for (int index = 0; index < 8; ++index)
        {
            const std::string path = std::string(directory) + "/frame-" +
                std::to_string(index) + ".png";
            CHECK(writeFixturePng(path, index));
            frames.push_back(path);
        }
        const std::string output = std::string(directory) + "/orbit.mp4";
        std::string error;
        CHECK(earthai::encodePngSequenceToH264Mp4(
            frames, 4, output, error));
        CHECK(error.empty());

        std::ifstream stream(output.c_str(), std::ios::binary | std::ios::ate);
        CHECK(stream.is_open());
        CHECK(stream.tellg() > 1000);
        stream.close();

        NSURL* url = [NSURL fileURLWithPath:
            [NSString stringWithUTF8String:output.c_str()]];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:url options:nil];
        NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType:AVMediaTypeVideo];
        CHECK([tracks count] == 1);
        const double seconds = CMTimeGetSeconds([asset duration]);
        CHECK(seconds >= 1.9 && seconds <= 2.1);

        std::cout << "[cinematic_video_encoder_tests] native H.264 MP4 pass\n";
    }
    return 0;
}
