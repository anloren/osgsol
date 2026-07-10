#ifndef OSGVERSE_WEB_GZIPUTILS_H
#define OSGVERSE_WEB_GZIPUTILS_H

#include <cstddef>
#include <string>
#include <vector>

namespace osgVerse
{
    bool decompressGzipBounded(const unsigned char* input, size_t inputSize,
                               std::vector<unsigned char>& output,
                               size_t maxOutputBytes = 256u * 1024u * 1024u,
                               size_t maxExpansionRatio = 512u,
                               std::string* error = NULL);
}

#endif
