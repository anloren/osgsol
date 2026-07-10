#include "../plugins/osgdb_web/GzipUtils.h"
#include "../readerwriter/SafeGltfInput.h"

#include <zlib.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#define CHECK(x) do { if (!(x)) { \
    std::cerr << "CHECK failed at " << __FILE__ << ":" << __LINE__ << ": " #x "\n"; \
    return 1; } } while (0)

static std::vector<unsigned char> makeGzip(const std::string& text)
{
    z_stream stream = {};
    if (deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8,
                     Z_DEFAULT_STRATEGY) != Z_OK) return std::vector<unsigned char>();
    std::vector<unsigned char> compressed(compressBound(text.size()));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.data()));
    stream.avail_in = static_cast<uInt>(text.size());
    stream.next_out = compressed.data();
    stream.avail_out = static_cast<uInt>(compressed.size());
    if (deflate(&stream, Z_FINISH) != Z_STREAM_END)
    { deflateEnd(&stream); return std::vector<unsigned char>(); }
    compressed.resize(stream.total_out);
    deflateEnd(&stream);
    return compressed;
}

static void put32(std::vector<char>& data, size_t offset, unsigned int value)
{
    data[offset + 0] = static_cast<char>(value & 0xff);
    data[offset + 1] = static_cast<char>((value >> 8) & 0xff);
    data[offset + 2] = static_cast<char>((value >> 16) & 0xff);
    data[offset + 3] = static_cast<char>((value >> 24) & 0xff);
}

static std::vector<char> makeB3dm()
{
    std::vector<char> data(28 + 12, 0);
    data[0] = 'b'; data[1] = '3'; data[2] = 'd'; data[3] = 'm';
    put32(data, 4, 1); put32(data, 8, static_cast<unsigned int>(data.size()));
    data[28] = 'g'; data[29] = 'l'; data[30] = 'T'; data[31] = 'F';
    put32(data, 32, 2); put32(data, 36, 12);
    return data;
}

static std::vector<char> makeI3dm()
{
    std::vector<char> data(32 + 12, 0);
    data[0] = 'i'; data[1] = '3'; data[2] = 'd'; data[3] = 'm';
    put32(data, 4, 1); put32(data, 8, static_cast<unsigned int>(data.size()));
    put32(data, 28, 1);
    data[32] = 'g'; data[33] = 'l'; data[34] = 'T'; data[35] = 'F';
    put32(data, 36, 2); put32(data, 40, 12);
    return data;
}

int main()
{
    std::string payload(1024 * 1024, '\0');
    unsigned int state = 0x12345678u;
    for (size_t i = 0; i < 4096; ++i)
    {
        state = state * 1664525u + 1013904223u;
        payload[i] = static_cast<char>(state >> 24);
    }
    for (size_t i = 4096; i < payload.size(); ++i) payload[i] = payload[i % 4096];
    const std::vector<unsigned char> compressed = makeGzip(payload);
    CHECK(payload.size() > compressed.size() * 10);
    std::vector<unsigned char> inflated;
    CHECK(osgVerse::decompressGzipBounded(compressed.data(), compressed.size(), inflated,
                                          payload.size() + 1, 512));
    CHECK(inflated.size() == payload.size());
    CHECK(std::equal(inflated.begin(), inflated.end(),
                     reinterpret_cast<const unsigned char*>(payload.data())));
    CHECK(!osgVerse::decompressGzipBounded(compressed.data(), compressed.size(), inflated,
                                           payload.size() / 2, 512));
    CHECK(inflated.empty());
    CHECK(!osgVerse::decompressGzipBounded(compressed.data(), compressed.size(), inflated,
                                           payload.size() + 1, 2));
    CHECK(inflated.empty());

    const std::vector<char> b3dm = makeB3dm();
    osgVerse::ThreeDTileHeader header;
    CHECK(osgVerse::parseThreeDTileHeader(b3dm, header));
    CHECK(header.kind == osgVerse::ThreeDTileHeader::B3DM);
    CHECK(header.payloadOffset == 28 && header.payloadSize == 12 && header.gltfFormat == 1);
    std::vector<char> i3dm = makeI3dm();
    CHECK(osgVerse::parseThreeDTileHeader(i3dm, header));
    CHECK(header.payloadOffset == 32 && header.payloadSize == 12 && header.gltfFormat == 1);
    i3dm[28] = 2;
    CHECK(!osgVerse::parseThreeDTileHeader(i3dm, header));
    std::vector<char> malformed = b3dm;
    put32(malformed, 8, 1000000);
    CHECK(!osgVerse::parseThreeDTileHeader(malformed, header));

    std::vector<unsigned char> remote;
    CHECK(osgVerse::appendRemoteChunk(remote, "1234", 4, 8));
    CHECK(!osgVerse::appendRemoteChunk(remote, "56789", 5, 8));
    CHECK(remote.size() == 4);
    size_t contentLength = 0;
    CHECK(osgVerse::parseContentLength(" 4096 ", contentLength) && contentLength == 4096);
    CHECK(!osgVerse::parseContentLength("-1", contentLength));
    CHECK(!osgVerse::parseContentLength("999999999999999999999999", contentLength));

    std::cout << "[OK] gzip, 3D Tiles, and remote resource bounds\n";
    return 0;
}
