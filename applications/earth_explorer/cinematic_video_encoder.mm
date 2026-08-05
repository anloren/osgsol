#include "cinematic_video_encoder.h"

#include <AVFoundation/AVFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreVideo/CoreVideo.h>
#include <ImageIO/ImageIO.h>

#include <cstdio>
#include <sstream>

namespace earthai
{
    namespace
    {
        std::string nsErrorText(NSError* error, const char* fallback)
        {
            if (error && [error localizedDescription])
                return std::string([[error localizedDescription] UTF8String]);
            return std::string(fallback);
        }

        CGImageRef readPng(const std::string& path, std::string& error)
        {
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(
                nullptr, reinterpret_cast<const UInt8*>(path.data()),
                path.size(), false);
            if (!url)
            {
                error = "cannot create URL for frame: " + path;
                return nullptr;
            }
            CGImageSourceRef source = CGImageSourceCreateWithURL(url, nullptr);
            CFRelease(url);
            if (!source)
            {
                error = "cannot open PNG frame: " + path;
                return nullptr;
            }
            CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
            CFRelease(source);
            if (!image) error = "cannot decode PNG frame: " + path;
            return image;
        }

        CVPixelBufferRef makePixelBuffer(
            CGImageRef image, std::size_t width, std::size_t height,
            std::string& error)
        {
            if (CGImageGetWidth(image) != width ||
                CGImageGetHeight(image) != height)
            {
                error = "all orbit frames must have identical dimensions";
                return nullptr;
            }
            NSDictionary* attributes = @{
                (NSString*)kCVPixelBufferCGImageCompatibilityKey: @YES,
                (NSString*)kCVPixelBufferCGBitmapContextCompatibilityKey: @YES,
                (NSString*)kCVPixelBufferWidthKey: @(width),
                (NSString*)kCVPixelBufferHeightKey: @(height),
                (NSString*)kCVPixelBufferPixelFormatTypeKey:
                    @(kCVPixelFormatType_32BGRA)
            };
            CVPixelBufferRef pixel = nullptr;
            const CVReturn created = CVPixelBufferCreate(
                kCFAllocatorDefault, width, height,
                kCVPixelFormatType_32BGRA,
                (__bridge CFDictionaryRef)attributes, &pixel);
            if (created != kCVReturnSuccess || !pixel)
            {
                std::ostringstream stream;
                stream << "CVPixelBufferCreate failed: " << created;
                error = stream.str();
                return nullptr;
            }

            CVPixelBufferLockBaseAddress(pixel, 0);
            void* data = CVPixelBufferGetBaseAddress(pixel);
            const std::size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixel);
            CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
            CGContextRef context = CGBitmapContextCreate(
                data, width, height, 8, bytesPerRow, color,
                kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
            CGColorSpaceRelease(color);
            if (!context)
            {
                CVPixelBufferUnlockBaseAddress(pixel, 0);
                CVPixelBufferRelease(pixel);
                error = "cannot create frame bitmap context";
                return nullptr;
            }

            CGContextSetRGBFillColor(context, 0.0, 0.0, 0.0, 1.0);
            CGContextFillRect(context, CGRectMake(0, 0, width, height));
            // ImageIO and video buffers use opposite row origins. Flip exactly
            // once here so the encoded Earth is never vertically inverted.
            CGContextTranslateCTM(context, 0.0, static_cast<CGFloat>(height));
            CGContextScaleCTM(context, 1.0, -1.0);
            CGContextDrawImage(
                context, CGRectMake(0, 0, width, height), image);
            CGContextRelease(context);
            CVPixelBufferUnlockBaseAddress(pixel, 0);
            return pixel;
        }
    }

    bool encodePngSequenceToH264Mp4(
        const std::vector<std::string>& pngPaths,
        int framesPerSecond,
        const std::string& outputPath,
        std::string& error)
    {
        error.clear();
        if (pngPaths.size() < 2)
        {
            error = "at least two orbit frames are required";
            return false;
        }
        if (framesPerSecond <= 0 || framesPerSecond > 120)
        {
            error = "frame rate must be between 1 and 120 fps";
            return false;
        }
        if (outputPath.empty())
        {
            error = "output path is empty";
            return false;
        }

        @autoreleasepool
        {
            CGImageRef first = readPng(pngPaths.front(), error);
            if (!first) return false;
            const std::size_t width = CGImageGetWidth(first);
            const std::size_t height = CGImageGetHeight(first);
            CGImageRelease(first);
            if (width < 2 || height < 2 || width > 16384 || height > 16384 ||
                (width % 2) != 0 || (height % 2) != 0)
            {
                error = "H.264 orbit frames require even dimensions between 2 and 16384";
                return false;
            }

            std::remove(outputPath.c_str());
            NSString* outputString = [NSString stringWithUTF8String:outputPath.c_str()];
            if (!outputString)
            {
                error = "output path is not valid UTF-8";
                return false;
            }
            NSURL* outputUrl = [NSURL fileURLWithPath:outputString];
            NSError* writerError = nil;
            AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:outputUrl
                fileType:AVFileTypeMPEG4 error:&writerError];
            if (!writer)
            {
                error = nsErrorText(writerError, "cannot create AVAssetWriter");
                return false;
            }

            const NSInteger bitrate = static_cast<NSInteger>(
                std::max<std::size_t>(2000000, width * height * 6));
            NSDictionary* compression = @{
                AVVideoAverageBitRateKey: @(bitrate),
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel
            };
            NSDictionary* settings = @{
                AVVideoCodecKey: AVVideoCodecTypeH264,
                AVVideoWidthKey: @(width),
                AVVideoHeightKey: @(height),
                AVVideoCompressionPropertiesKey: compression
            };
            AVAssetWriterInput* input = [AVAssetWriterInput
                assetWriterInputWithMediaType:AVMediaTypeVideo
                outputSettings:settings];
            input.expectsMediaDataInRealTime = NO;
            NSDictionary* pixelAttributes = @{
                (NSString*)kCVPixelBufferPixelFormatTypeKey:
                    @(kCVPixelFormatType_32BGRA),
                (NSString*)kCVPixelBufferWidthKey: @(width),
                (NSString*)kCVPixelBufferHeightKey: @(height)
            };
            AVAssetWriterInputPixelBufferAdaptor* adaptor =
                [AVAssetWriterInputPixelBufferAdaptor
                    assetWriterInputPixelBufferAdaptorWithAssetWriterInput:input
                    sourcePixelBufferAttributes:pixelAttributes];
            if (![writer canAddInput:input])
            {
                error = "AVAssetWriter rejected the H.264 video input";
                return false;
            }
            [writer addInput:input];
            if (![writer startWriting])
            {
                error = nsErrorText([writer error], "failed to start H.264 writer");
                return false;
            }
            [writer startSessionAtSourceTime:kCMTimeZero];

            for (std::size_t index = 0; index < pngPaths.size(); ++index)
            {
                while (![input isReadyForMoreMediaData])
                {
                    if ([writer status] == AVAssetWriterStatusFailed)
                    {
                        error = nsErrorText([writer error], "H.264 writer failed");
                        [input markAsFinished];
                        [writer cancelWriting];
                        return false;
                    }
                    [NSThread sleepForTimeInterval:0.001];
                }

                CGImageRef image = readPng(pngPaths[index], error);
                if (!image)
                {
                    [input markAsFinished];
                    [writer cancelWriting];
                    return false;
                }
                CVPixelBufferRef pixel = makePixelBuffer(
                    image, width, height, error);
                CGImageRelease(image);
                if (!pixel)
                {
                    [input markAsFinished];
                    [writer cancelWriting];
                    return false;
                }
                const CMTime timestamp = CMTimeMake(
                    static_cast<int64_t>(index), framesPerSecond);
                const BOOL appended = [adaptor appendPixelBuffer:pixel
                    withPresentationTime:timestamp];
                CVPixelBufferRelease(pixel);
                if (!appended)
                {
                    error = nsErrorText([writer error], "failed to append orbit frame");
                    [input markAsFinished];
                    [writer cancelWriting];
                    return false;
                }
            }

            [input markAsFinished];
            dispatch_semaphore_t completed = dispatch_semaphore_create(0);
            [writer finishWritingWithCompletionHandler:^{
                dispatch_semaphore_signal(completed);
            }];
            const long timedOut = dispatch_semaphore_wait(
                completed, dispatch_time(DISPATCH_TIME_NOW,
                    static_cast<int64_t>(60.0 * NSEC_PER_SEC)));
            if (timedOut != 0)
            {
                [writer cancelWriting];
                error = "timed out finalizing H.264 MP4";
                return false;
            }
            if ([writer status] != AVAssetWriterStatusCompleted)
            {
                error = nsErrorText([writer error], "H.264 MP4 did not complete");
                return false;
            }
            return true;
        }
    }
}
