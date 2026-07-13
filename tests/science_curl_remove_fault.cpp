#include <curl/curl.h>

#include <cstdarg>
#include <cstdlib>
#include <dlfcn.h>
#include <map>
#include <mutex>
#include <string>
#include <time.h>

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

std::map<CURL*, CURLM*>& attachedHandles()
{
    static std::map<CURL*, CURLM*> handles;
    return handles;
}

void applySteadyClockOffset(clockid_t clockId, timespec* value)
{
    bool isSteadyClock = clockId == CLOCK_MONOTONIC;
#ifdef CLOCK_MONOTONIC_RAW
    isSteadyClock = isSteadyClock || clockId == CLOCK_MONOTONIC_RAW;
#endif
#ifdef CLOCK_UPTIME_RAW
    isSteadyClock = isSteadyClock || clockId == CLOCK_UPTIME_RAW;
#endif
    if (value == nullptr || !isSteadyClock) return;
    const char* text = std::getenv("OSGSOL_TEST_STEADY_CLOCK_OFFSET_MS");
    if (text == nullptr || *text == '\0') return;
    char* end = nullptr;
    const long long milliseconds = std::strtoll(text, &end, 10);
    if (end == text || *end != '\0' || milliseconds <= 0 ||
        milliseconds > 600000)
        return;
    value->tv_sec += static_cast<time_t>(milliseconds / 1000);
    value->tv_nsec += static_cast<long>((milliseconds % 1000) * 1000000);
    if (value->tv_nsec >= 1000000000L)
    {
        ++value->tv_sec;
        value->tv_nsec -= 1000000000L;
    }
}

bool shouldReplaceDoneResult(long responseCode)
{
    if (responseCode != 500) return false;
    const std::lock_guard<std::mutex> lock(faultMutex());
    const char* value = std::getenv(
        "OSGSOL_TEST_CURLMSG_TRANSPORT_ON_500");
    const int remaining = value ? std::atoi(value) : 0;
    if (remaining <= 0) return false;
    if (remaining == 1)
        unsetenv("OSGSOL_TEST_CURLMSG_TRANSPORT_ON_500");
    else
    {
        const std::string next = std::to_string(remaining - 1);
        setenv("OSGSOL_TEST_CURLMSG_TRANSPORT_ON_500", next.c_str(), 1);
    }
    return remaining == 1;
}

template<typename AddFunction>
CURLMcode forwardAdd(AddFunction realAdd, CURLM* multiHandle,
                     CURL* easyHandle)
{
    const CURLMcode result = realAdd(multiHandle, easyHandle);
    if (result == CURLM_OK)
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        attachedHandles()[easyHandle] = multiHandle;
    }
    return result;
}

template<typename RemoveFunction>
CURLMcode forwardRemove(RemoveFunction realRemove, CURLM* multiHandle,
                        CURL* easyHandle)
{
    if (shouldFailRemove()) return CURLM_INTERNAL_ERROR;
    const CURLMcode result = realRemove(multiHandle, easyHandle);
    if (result == CURLM_OK)
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        attachedHandles().erase(easyHandle);
    }
    return result;
}

template<typename CleanupFunction>
void forwardCleanup(CleanupFunction realCleanup, CURL* easyHandle)
{
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        if (attachedHandles().find(easyHandle) != attachedHandles().end())
            setenv("OSGSOL_TEST_CLEANUP_WHILE_ATTACHED", "1", 1);
    }
    realCleanup(easyHandle);
}

template<typename InfoReadFunction, typename GetInfoFunction>
CURLMsg* forwardInfoRead(InfoReadFunction realInfoRead,
                         GetInfoFunction realGetInfo, CURLM* multiHandle,
                         int* queuedMessages)
{
    CURLMsg* message = realInfoRead(multiHandle, queuedMessages);
    if (message == nullptr || message->msg != CURLMSG_DONE)
        return message;
    long responseCode = 0;
    if (realGetInfo(message->easy_handle, CURLINFO_HTTP_CODE,
                    &responseCode) == CURLE_OK &&
        shouldReplaceDoneResult(responseCode))
    {
        message->data.result = CURLE_RECV_ERROR;
    }
    return message;
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

extern "C" CURLMcode osgSolTestCurlMultiAddHandle(
    CURLM* multiHandle, CURL* easyHandle)
{
    return forwardAdd(&curl_multi_add_handle, multiHandle, easyHandle);
}

extern "C" CURLMcode osgSolTestCurlMultiRemoveHandle(
    CURLM* multiHandle, CURL* easyHandle)
{
    return forwardRemove(&curl_multi_remove_handle, multiHandle, easyHandle);
}

extern "C" void osgSolTestCurlEasyCleanup(CURL* easyHandle)
{
    forwardCleanup(&curl_easy_cleanup, easyHandle);
}

extern "C" CURLMsg* osgSolTestCurlMultiInfoRead(
    CURLM* multiHandle, int* queuedMessages)
{
    return forwardInfoRead(&curl_multi_info_read, &curl_easy_getinfo,
                           multiHandle, queuedMessages);
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

extern "C" int osgSolTestClockGettime(clockid_t clockId, timespec* value)
{
    const int result = clock_gettime(clockId, value);
    if (result == 0) applySteadyClockOffset(clockId, value);
    return result;
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
} g_curlAddInterpose __attribute__((section("__DATA,__interpose"))) = {
    reinterpret_cast<const void*>(&osgSolTestCurlMultiAddHandle),
    reinterpret_cast<const void*>(&curl_multi_add_handle)
};

__attribute__((used)) static const struct
{
    const void* replacement;
    const void* replacee;
} g_curlCleanupInterpose __attribute__((section("__DATA,__interpose"))) = {
    reinterpret_cast<const void*>(&osgSolTestCurlEasyCleanup),
    reinterpret_cast<const void*>(&curl_easy_cleanup)
};

__attribute__((used)) static const struct
{
    const void* replacement;
    const void* replacee;
} g_curlInfoReadInterpose __attribute__((section("__DATA,__interpose"))) = {
    reinterpret_cast<const void*>(&osgSolTestCurlMultiInfoRead),
    reinterpret_cast<const void*>(&curl_multi_info_read)
};

__attribute__((used)) static const struct
{
    const void* replacement;
    const void* replacee;
} g_curlGetInfoInterpose __attribute__((section("__DATA,__interpose"))) = {
    reinterpret_cast<const void*>(&osgSolTestCurlEasyGetinfo),
    reinterpret_cast<const void*>(&curl_easy_getinfo)
};

__attribute__((used)) static const struct
{
    const void* replacement;
    const void* replacee;
} g_clockGettimeInterpose __attribute__((section("__DATA,__interpose"))) = {
    reinterpret_cast<const void*>(&osgSolTestClockGettime),
    reinterpret_cast<const void*>(&clock_gettime)
};

#elif defined(__linux__)

extern "C" CURLMcode curl_multi_add_handle(
    CURLM* multiHandle, CURL* easyHandle)
{
    using AddFunction = CURLMcode (*)(CURLM*, CURL*);
    static const auto realAdd = reinterpret_cast<AddFunction>(
        dlsym(RTLD_NEXT, "curl_multi_add_handle"));
    return forwardAdd(realAdd, multiHandle, easyHandle);
}

extern "C" CURLMcode curl_multi_remove_handle(
    CURLM* multiHandle, CURL* easyHandle)
{
    using RemoveFunction = CURLMcode (*)(CURLM*, CURL*);
    static const auto realRemove = reinterpret_cast<RemoveFunction>(
        dlsym(RTLD_NEXT, "curl_multi_remove_handle"));
    return forwardRemove(realRemove, multiHandle, easyHandle);
}

extern "C" void curl_easy_cleanup(CURL* easyHandle)
{
    using CleanupFunction = void (*)(CURL*);
    static const auto realCleanup = reinterpret_cast<CleanupFunction>(
        dlsym(RTLD_NEXT, "curl_easy_cleanup"));
    forwardCleanup(realCleanup, easyHandle);
}

extern "C" CURLMsg* curl_multi_info_read(
    CURLM* multiHandle, int* queuedMessages)
{
    using InfoReadFunction = CURLMsg* (*)(CURLM*, int*);
    using GetInfoFunction = CURLcode (*)(CURL*, CURLINFO, ...);
    static const auto realInfoRead = reinterpret_cast<InfoReadFunction>(
        dlsym(RTLD_NEXT, "curl_multi_info_read"));
    static const auto realGetInfo = reinterpret_cast<GetInfoFunction>(
        dlsym(RTLD_NEXT, "curl_easy_getinfo"));
    return forwardInfoRead(realInfoRead, realGetInfo,
                           multiHandle, queuedMessages);
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

extern "C" int clock_gettime(clockid_t clockId, timespec* value)
{
    using ClockGettimeFunction = int (*)(clockid_t, timespec*);
    static const auto realClockGettime =
        reinterpret_cast<ClockGettimeFunction>(
            dlsym(RTLD_NEXT, "clock_gettime"));
    const int result = realClockGettime(clockId, value);
    if (result == 0) applySteadyClockOffset(clockId, value);
    return result;
}

#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
