#include "ScienceProcessingPluginApi.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
const char* FORMATS[] = {"geoparquet", "parquet"};

struct Context
{
    std::string request;
    bool cancelled = false;
};

void copyError(char* output, std::size_t size, const char* message)
{
    if (!output || size == 0) return;
    std::snprintf(output, size, "%s", message);
}

bool capability(std::size_t index,
                OsgSolScienceProcessingCapabilityV1* output)
{
    if (index != 0 || !output || output->structSize < sizeof(*output))
        return false;
    output->id = "duckdb-spatial";
    output->displayName = "DuckDB Spatial / GeoParquet";
    output->engineId = "duckdb-spatial";
    output->inputFormats = FORMATS;
    output->inputFormatCount = 2;
    output->outputKindMask =
        (std::uint64_t(1) << 1) | (std::uint64_t(1) << 2) |
        (std::uint64_t(1) << 5);
    output->cancellable = true;
    output->persistentRecord = true;
    return true;
}

void* create(char*, std::size_t)
{
    return new Context;
}

void destroy(void* context)
{
    delete static_cast<Context*>(context);
}

bool submit(void* context, const char* request, std::size_t size,
            std::uint64_t* token, char* error, std::size_t errorSize)
{
    if (!context || !request || size == 0 || !token)
    {
        copyError(error, errorSize, "invalid request");
        return false;
    }
    Context* state = static_cast<Context*>(context);
    state->request.assign(request, size);
    state->cancelled = false;
    *token = 9;
    return true;
}

bool snapshot(void* context, std::uint64_t token,
              OsgSolScienceProcessingBufferV1* output,
              char* error, std::size_t errorSize)
{
    if (!context || token != 9 || !output ||
        output->structSize < sizeof(*output))
    {
        copyError(error, errorSize, "invalid token");
        return false;
    }
    const Context* state = static_cast<const Context*>(context);
    const std::string result = state->cancelled
        ? "{\"state\":\"cancelled\"}"
        : "{\"state\":\"ready\",\"engine\":\"duckdb-spatial\"}";
    output->bytesWritten = 0;
    output->bytesRequired = result.size() + 1;
    if (!output->utf8 || output->capacity < output->bytesRequired)
        return false;
    std::memcpy(output->utf8, result.c_str(), result.size() + 1);
    output->bytesWritten = result.size();
    return true;
}

bool cancel(void* context, std::uint64_t token,
            char* error, std::size_t errorSize)
{
    if (!context || token != 9)
    {
        copyError(error, errorSize, "invalid token");
        return false;
    }
    static_cast<Context*>(context)->cancelled = true;
    return true;
}
}

extern "C" const OsgSolScienceProcessingPluginApiV1*
osgSolScienceProcessingPlugin()
{
    static const OsgSolScienceProcessingPluginApiV1 api = {
        OSGSOL_SCIENCE_PROCESSING_PLUGIN_ABI_V1,
        sizeof(OsgSolScienceProcessingPluginApiV1),
        "org.osgsol.test.duckdb", "1.0.0", "DuckDB test plugin", 1,
        capability, create, destroy, submit, snapshot, cancel};
    return &api;
}
