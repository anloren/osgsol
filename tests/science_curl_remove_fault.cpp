#include <curl/curl.h>

#include <cstdarg>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>

namespace
{
std::mutex& faultMutex()
{
    static std::mutex mutex;
    return mutex;
}

bool shouldFailRemove()
{
    const std::lock_guard<std::mutex> lock(faultMutex());
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

struct GetInfoState
{
    enum class Stage
    {
        None,
        TransientStatus,
        HttpVersion,
        Connection,
    };

    long responseCode = 0;
    Stage stage = Stage::None;
};

std::map<CURL*, GetInfoState>& getInfoStates()
{
    static std::map<CURL*, GetInfoState> states;
    return states;
}

template<typename Value>
bool consumeOverrideLocked(const char* name, Value& replacement)
{
    const char* value = std::getenv(name);
    if (!value || !*value) return false;
    char* end = nullptr;
    const long long parsed = std::strtoll(value, &end, 10);
    if (end == value || *end != '\0') return false;
    unsetenv(name);
    replacement = static_cast<Value>(parsed);
    return true;
}

template<typename GetInfoFunction>
CURLcode forwardGetInfo(GetInfoFunction realGetInfo, CURL* easyHandle,
                        CURLINFO info, void* output)
{
    const CURLcode result = realGetInfo(easyHandle, info, output);
    if (result != CURLE_OK || output == nullptr) return result;

    const std::lock_guard<std::mutex> lock(faultMutex());
    GetInfoState& state = getInfoStates()[easyHandle];
    if (info == CURLINFO_HTTP_CODE)
    {
        state.responseCode = *static_cast<long*>(output);
        state.stage = state.responseCode == 500
            ? GetInfoState::Stage::TransientStatus
            : GetInfoState::Stage::None;
    }
    else if (info == CURLINFO_HTTP_VERSION &&
             state.stage == GetInfoState::Stage::TransientStatus)
    {
        state.stage = GetInfoState::Stage::HttpVersion;
    }
    else if (info == CURLINFO_CONN_ID &&
             state.stage == GetInfoState::Stage::HttpVersion)
    {
        curl_off_t replacement = 0;
        if (consumeOverrideLocked(
                "OSGSOL_TEST_CURLINFO_CONN_ID_ONCE", replacement))
        {
            *static_cast<curl_off_t*>(output) = replacement;
        }
        state.stage = GetInfoState::Stage::Connection;
    }
    else if (info == CURLINFO_REDIRECT_COUNT &&
             state.stage == GetInfoState::Stage::Connection)
    {
        long replacement = 0;
        if (consumeOverrideLocked(
                "OSGSOL_TEST_CURLINFO_REDIRECT_COUNT_ONCE", replacement))
        {
            *static_cast<long*>(output) = replacement;
        }
        state.stage = GetInfoState::Stage::None;
    }
    else
    {
        state.stage = GetInfoState::Stage::None;
    }
    return result;
}
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wvarargs"
#endif

#if defined(__APPLE__)

extern "C" CURLMcode osgSolTestCurlMultiRemoveHandle(
    CURLM* multiHandle, CURL* easyHandle)
{
    if (shouldFailRemove()) return CURLM_INTERNAL_ERROR;
    return curl_multi_remove_handle(multiHandle, easyHandle);
}

extern "C" CURLcode osgSolTestCurlEasyGetinfo(CURL* easyHandle,
                                                CURLINFO info, ...)
{
    va_list arguments;
    va_start(arguments, info);
    void* output = va_arg(arguments, void*);
    va_end(arguments);
    return forwardGetInfo(&curl_easy_getinfo, easyHandle, info, output);
}

__attribute__((used)) static const struct
{
    const void* replacement;
    const void* replacee;
} g_curlRemoveInterpose __attribute__((section("__DATA,__interpose"))) = {
    reinterpret_cast<const void*>(&osgSolTestCurlMultiRemoveHandle),
    reinterpret_cast<const void*>(&curl_multi_remove_handle)
};

__attribute__((used)) static const struct
{
    const void* replacement;
    const void* replacee;
} g_curlGetInfoInterpose __attribute__((section("__DATA,__interpose"))) = {
    reinterpret_cast<const void*>(&osgSolTestCurlEasyGetinfo),
    reinterpret_cast<const void*>(&curl_easy_getinfo)
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

extern "C" CURLcode curl_easy_getinfo(CURL* easyHandle, CURLINFO info, ...)
{
    using GetInfoFunction = CURLcode (*)(CURL*, CURLINFO, ...);
    static const auto realGetInfo = reinterpret_cast<GetInfoFunction>(
        dlsym(RTLD_NEXT, "curl_easy_getinfo"));
    va_list arguments;
    va_start(arguments, info);
    void* output = va_arg(arguments, void*);
    va_end(arguments);
    return forwardGetInfo(realGetInfo, easyHandle, info, output);
}

#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
