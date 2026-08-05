#ifndef OSGSOL_SCIENCE_PROCESSING_PLUGIN_API_H
#define OSGSOL_SCIENCE_PROCESSING_PLUGIN_API_H

#include <cstddef>
#include <cstdint>

static const std::uint32_t OSGSOL_SCIENCE_PROCESSING_PLUGIN_ABI_V1 = 1u;

struct OsgSolScienceProcessingCapabilityV1
{
    std::uint32_t structSize;
    const char* id;
    const char* displayName;
    const char* engineId;
    const char* const* inputFormats;
    std::size_t inputFormatCount;
    std::uint64_t outputKindMask;
    bool cancellable;
    bool persistentRecord;
};

typedef bool (*OsgSolScienceProcessingCapabilityFunction)(
    std::size_t index, OsgSolScienceProcessingCapabilityV1* output);

struct OsgSolScienceProcessingBufferV1
{
    std::uint32_t structSize;
    char* utf8;
    std::size_t capacity;
    std::size_t bytesWritten;
    std::size_t bytesRequired;
};

struct OsgSolScienceProcessingPluginApiV1
{
    std::uint32_t abiVersion;
    std::uint32_t structSize;
    const char* pluginId;
    const char* pluginVersion;
    const char* displayName;
    std::size_t capabilityCount;
    OsgSolScienceProcessingCapabilityFunction capability;
    void* (*create)(char* error, std::size_t errorSize);
    void (*destroy)(void* context);
    bool (*submit)(void* context,
                   const char* requestUtf8,
                   std::size_t requestSize,
                   std::uint64_t* token,
                   char* error,
                   std::size_t errorSize);
    bool (*snapshot)(void* context,
                     std::uint64_t token,
                     OsgSolScienceProcessingBufferV1* output,
                     char* error,
                     std::size_t errorSize);
    bool (*cancel)(void* context,
                   std::uint64_t token,
                   char* error,
                   std::size_t errorSize);
};

typedef const OsgSolScienceProcessingPluginApiV1*
    (*OsgSolScienceProcessingPluginAnchor)();

#endif
