#include <curl/curl.h>

#include <cstdlib>
#include <string>

namespace
{
bool shouldFailRemove()
{
    const char* value = std::getenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE");
    const int remaining = value ? std::atoi(value) : 0;
    if (remaining <= 0) return false;
    if (remaining == 1)
        unsetenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE");
    else
    {
        const std::string next = std::to_string(remaining - 1);
        setenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE", next.c_str(), 1);
    }
    return true;
}
}

#if defined(__APPLE__)

extern "C" CURLMcode osgSolTestCurlMultiRemoveHandle(
    CURLM* multiHandle, CURL* easyHandle)
{
    if (shouldFailRemove()) return CURLM_INTERNAL_ERROR;
    return curl_multi_remove_handle(multiHandle, easyHandle);
}

__attribute__((used)) static const struct
{
    const void* replacement;
    const void* replacee;
} g_curlRemoveInterpose __attribute__((section("__DATA,__interpose"))) = {
    reinterpret_cast<const void*>(&osgSolTestCurlMultiRemoveHandle),
    reinterpret_cast<const void*>(&curl_multi_remove_handle)
};

#elif defined(__linux__)

#include <dlfcn.h>

extern "C" CURLMcode curl_multi_remove_handle(
    CURLM* multiHandle, CURL* easyHandle)
{
    using RemoveFunction = CURLMcode (*)(CURLM*, CURL*);
    static const auto realRemove = reinterpret_cast<RemoveFunction>(
        dlsym(RTLD_NEXT, "curl_multi_remove_handle"));
    if (shouldFailRemove()) return CURLM_INTERNAL_ERROR;
    return realRemove(multiHandle, easyHandle);
}

#endif
