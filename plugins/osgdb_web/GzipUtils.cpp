#include "GzipUtils.h"

#include <algorithm>
#include <limits>

#ifdef WITH_ZLIB
#include <zlib.h>
#endif

namespace osgVerse
{
    bool decompressGzipBounded(const unsigned char* input, size_t inputSize,
                               std::vector<unsigned char>& output,
                               size_t maxOutputBytes, size_t maxExpansionRatio,
                               std::string* error)
    {
        output.clear();
        if (error) error->clear();
        if (!input || inputSize == 0 || maxOutputBytes == 0 || maxExpansionRatio == 0)
        {
            if (error) *error = "invalid gzip bounds or input";
            return false;
        }
#ifndef WITH_ZLIB
        (void)input; (void)inputSize; (void)maxOutputBytes; (void)maxExpansionRatio;
        if (error) *error = "zlib support is not enabled";
        return false;
#else
        size_t ratioLimit = maxOutputBytes;
        if (inputSize <= std::numeric_limits<size_t>::max() / maxExpansionRatio)
            ratioLimit = std::min(ratioLimit, inputSize * maxExpansionRatio);
        if (ratioLimit == 0)
        {
            if (error) *error = "gzip expansion limit is zero";
            return false;
        }

        z_stream stream = {};
        if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        {
            if (error) *error = "inflateInit2 failed";
            return false;
        }

        size_t inputOffset = 0;
        int result = Z_OK;
        unsigned char buffer[16384];
        while (result == Z_OK)
        {
            if (stream.avail_in == 0 && inputOffset < inputSize)
            {
                const size_t chunk = std::min(inputSize - inputOffset,
                                              static_cast<size_t>(std::numeric_limits<uInt>::max()));
                stream.next_in = const_cast<Bytef*>(input + inputOffset);
                stream.avail_in = static_cast<uInt>(chunk);
                inputOffset += chunk;
            }

            stream.next_out = buffer;
            stream.avail_out = sizeof(buffer);
            result = inflate(&stream, Z_NO_FLUSH);
            const size_t produced = sizeof(buffer) - stream.avail_out;
            if (produced > ratioLimit - std::min(ratioLimit, output.size()))
            {
                result = Z_MEM_ERROR;
                if (error) *error = "gzip output exceeds configured bound";
                break;
            }
            output.insert(output.end(), buffer, buffer + produced);
            if (result == Z_STREAM_END) break;
            if (result != Z_OK || (produced == 0 && stream.avail_in == 0 && inputOffset >= inputSize))
            {
                if (error && error->empty()) *error = "invalid or truncated gzip stream";
                break;
            }
        }
        inflateEnd(&stream);
        if (result != Z_STREAM_END)
        {
            output.clear();
            return false;
        }
        return true;
#endif
    }
}
