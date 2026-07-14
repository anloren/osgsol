#include <curl/curl.h>

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <map>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <time.h>

namespace
{
std::mutex& faultMutex()
{
    static std::mutex mutex;
    return mutex;
}

constexpr unsigned int MAX_FAULT_COUNT = 1024;
const char* const FAULT_PARSE_ERROR =
    "OSGSOL_TEST_CURL_FAULT_PARSE_ERROR";
const char* const REMOVE_ROLLBACK_FAILURE =
    "OSGSOL_TEST_CURL_REMOVE_ROLLBACK_FAILURE";

bool parseFaultCountLocked(const char* name, unsigned int& remaining)
{
    const char* value = std::getenv(name);
    if (value == nullptr)
    {
        remaining = 0;
        return true;
    }
    if (*value == '\0')
    {
        setenv(FAULT_PARSE_ERROR, name, 1);
        return false;
    }
    unsigned int parsed = 0;
    for (const char* cursor = value; *cursor != '\0'; ++cursor)
    {
        if (*cursor < '0' || *cursor > '9')
        {
            setenv(FAULT_PARSE_ERROR, name, 1);
            return false;
        }
        const unsigned int digit = static_cast<unsigned int>(*cursor - '0');
        if (parsed > (MAX_FAULT_COUNT - digit) / 10)
        {
            setenv(FAULT_PARSE_ERROR, name, 1);
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    remaining = parsed;
    return true;
}

void storeFaultCountLocked(const char* name, unsigned int remaining)
{
    if (remaining == 0)
        unsetenv(name);
    else
    {
        const std::string next = std::to_string(remaining);
        setenv(name, next.c_str(), 1);
    }
}

bool consumeFaultLocked(const char* name)
{
    unsigned int remaining = 0;
    if (!parseFaultCountLocked(name, remaining) || remaining == 0)
        return false;
    storeFaultCountLocked(name, remaining - 1);
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

std::set<CURL*>& completedTransientHandles()
{
    static std::set<CURL*> handles;
    return handles;
}

std::set<CURL*>& logicallyAttachedAfterInjectedRemoveFailure()
{
    static std::set<CURL*> handles;
    return handles;
}

std::map<CURLM*, CURL*>& headRetryPerformHandles()
{
    static std::map<CURLM*, CURL*> handles;
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
    for (const char* cursor = text; *cursor != '\0'; ++cursor)
    {
        if (*cursor < '0' || *cursor > '9') return;
    }
    char* end = nullptr;
    errno = 0;
    const long long milliseconds = std::strtoll(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || milliseconds <= 0 ||
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
    const char* const name = "OSGSOL_TEST_CURLMSG_TRANSPORT_ON_500";
    unsigned int remaining = 0;
    if (!parseFaultCountLocked(name, remaining) || remaining == 0)
        return false;
    storeFaultCountLocked(name, remaining - 1);
    return remaining == 1;
}

template<typename AddFunction>
CURLMcode forwardAdd(AddFunction realAdd, CURLM* multiHandle,
                     CURL* easyHandle)
{
    const CURLMcode result = realAdd(multiHandle, easyHandle);
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        if (result == CURLM_OK)
        {
            attachedHandles()[easyHandle] = multiHandle;
            if (completedTransientHandles().count(easyHandle) != 0)
            {
                if (consumeFaultLocked(
                        "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_ADD"))
                    return CURLM_INTERNAL_ERROR;
                headRetryPerformHandles()[multiHandle] = easyHandle;
                completedTransientHandles().erase(easyHandle);
            }
            if (consumeFaultLocked("OSGSOL_TEST_FAIL_NEXT_CURL_ADD"))
                return CURLM_INTERNAL_ERROR;
        }
    }
    return result;
}

template<typename RemoveFunction, typename AddFunction>
CURLMcode forwardRemove(RemoveFunction realRemove, AddFunction realAdd,
                        CURLM* multiHandle, CURL* easyHandle)
{
    const CURLMcode result = realRemove(multiHandle, easyHandle);
    if (result != CURLM_OK) return result;
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        const bool headRetryTarget =
            completedTransientHandles().count(easyHandle) != 0;
        const char* faultName = headRetryTarget
            ? "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_REMOVE"
            : "OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE";
        unsigned int remaining = 0;
        if (!parseFaultCountLocked(faultName, remaining))
            remaining = 0;
        if (remaining > 0)
        {
            const CURLMcode rollback = realAdd(multiHandle, easyHandle);
            if (rollback != CURLM_OK)
            {
                setenv(REMOVE_ROLLBACK_FAILURE, faultName, 1);
                attachedHandles().erase(easyHandle);
                logicallyAttachedAfterInjectedRemoveFailure().erase(
                    easyHandle);
                completedTransientHandles().erase(easyHandle);
                return CURLM_OK;
            }
            storeFaultCountLocked(faultName, remaining - 1);
            attachedHandles()[easyHandle] = multiHandle;
            if (headRetryTarget)
                logicallyAttachedAfterInjectedRemoveFailure().insert(
                    easyHandle);
            return CURLM_INTERNAL_ERROR;
        }
        attachedHandles().erase(easyHandle);
        logicallyAttachedAfterInjectedRemoveFailure().erase(easyHandle);
        if (!headRetryTarget)
            completedTransientHandles().erase(easyHandle);
    }
    return result;
}

template<typename PerformFunction>
CURLMcode forwardPerform(PerformFunction realPerform, CURLM* multiHandle,
                         int* runningHandles)
{
    const CURLMcode result = realPerform(multiHandle, runningHandles);
    if (result != CURLM_OK) return result;
    const std::lock_guard<std::mutex> lock(faultMutex());
    const auto headRetry = headRetryPerformHandles().find(multiHandle);
    if (headRetry != headRetryPerformHandles().end())
    {
        headRetryPerformHandles().erase(headRetry);
        if (consumeFaultLocked(
                "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_PERFORM"))
            return CURLM_INTERNAL_ERROR;
    }
    return consumeFaultLocked("OSGSOL_TEST_FAIL_NEXT_CURL_PERFORM")
        ? CURLM_INTERNAL_ERROR : result;
}

template<typename CleanupFunction>
void forwardCleanup(CleanupFunction realCleanup, CURL* easyHandle)
{
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        if (attachedHandles().find(easyHandle) != attachedHandles().end())
            setenv("OSGSOL_TEST_CLEANUP_WHILE_ATTACHED", "1", 1);
        attachedHandles().erase(easyHandle);
        logicallyAttachedAfterInjectedRemoveFailure().erase(easyHandle);
        getInfoStates().erase(easyHandle);
        completedTransientHandles().erase(easyHandle);
        for (auto iterator = headRetryPerformHandles().begin();
             iterator != headRetryPerformHandles().end();)
        {
            if (iterator->second == easyHandle)
                iterator = headRetryPerformHandles().erase(iterator);
            else
                ++iterator;
        }
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
                    &responseCode) == CURLE_OK)
    {
        if (responseCode == 500)
        {
            const std::lock_guard<std::mutex> lock(faultMutex());
            completedTransientHandles().insert(message->easy_handle);
        }
        if (shouldReplaceDoneResult(responseCode))
            message->data.result = CURLE_RECV_ERROR;
    }
    return message;
}

template<typename Value>
bool consumeOverrideLocked(const char* name, Value& replacement)
{
    const char* value = std::getenv(name);
    if (!value || !*value) return false;
    const char* cursor = value;
    if (*cursor == '-') ++cursor;
    if (*cursor == '\0')
    {
        setenv(FAULT_PARSE_ERROR, name, 1);
        return false;
    }
    for (; *cursor != '\0'; ++cursor)
    {
        if (*cursor < '0' || *cursor > '9')
        {
            setenv(FAULT_PARSE_ERROR, name, 1);
            return false;
        }
    }
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(value, &end, 10);
    if (errno == ERANGE || end == value || *end != '\0' ||
        parsed < static_cast<long long>(std::numeric_limits<Value>::min()) ||
        parsed > static_cast<long long>(std::numeric_limits<Value>::max()))
    {
        setenv(FAULT_PARSE_ERROR, name, 1);
        return false;
    }
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

struct SavedEnvironment
{
    explicit SavedEnvironment(const char* variableName)
        : name(variableName)
    {
        const char* value = std::getenv(name.c_str());
        present = value != nullptr;
        if (present) saved = value;
    }

    ~SavedEnvironment()
    {
        if (present)
            setenv(name.c_str(), saved.c_str(), 1);
        else
            unsetenv(name.c_str());
    }

    std::string name;
    std::string saved;
    bool present = false;
};

bool environmentEquals(const char* name, const char* expected)
{
    const char* actual = std::getenv(name);
    return actual != nullptr && std::string(actual) == expected;
}
}

extern "C" int osgSolTestRunCurlFaultSelfTests()
{
    const char* const addFault = "OSGSOL_TEST_FAIL_NEXT_CURL_ADD";
    const char* const removeFault = "OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE";
    const char* const performFault = "OSGSOL_TEST_FAIL_NEXT_CURL_PERFORM";
    const char* const headRetryAddFault =
        "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_ADD";
    const char* const headRetryRemoveFault =
        "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_REMOVE";
    const char* const headRetryPerformFault =
        "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_PERFORM";
    SavedEnvironment savedAdd(addFault);
    SavedEnvironment savedRemove(removeFault);
    SavedEnvironment savedPerform(performFault);
    SavedEnvironment savedHeadRetryAdd(headRetryAddFault);
    SavedEnvironment savedHeadRetryRemove(headRetryRemoveFault);
    SavedEnvironment savedHeadRetryPerform(headRetryPerformFault);
    SavedEnvironment savedConnectionOverride(
        "OSGSOL_TEST_CURLINFO_CONN_ID_ONCE");
    SavedEnvironment savedParseError(FAULT_PARSE_ERROR);
    SavedEnvironment savedCleanup("OSGSOL_TEST_CLEANUP_WHILE_ATTACHED");
    SavedEnvironment savedRollbackFailure(REMOVE_ROLLBACK_FAILURE);
    unsetenv(addFault);
    unsetenv(removeFault);
    unsetenv(performFault);
    unsetenv(headRetryAddFault);
    unsetenv(headRetryRemoveFault);
    unsetenv(headRetryPerformFault);
    unsetenv("OSGSOL_TEST_CURLINFO_CONN_ID_ONCE");
    unsetenv(FAULT_PARSE_ERROR);
    unsetenv("OSGSOL_TEST_CLEANUP_WHILE_ATTACHED");
    unsetenv(REMOVE_ROLLBACK_FAILURE);

    CURLM* const multi = reinterpret_cast<CURLM*>(static_cast<uintptr_t>(0x10));
    CURL* const easy = reinterpret_cast<CURL*>(static_cast<uintptr_t>(0x20));
    const auto addOk = [](CURLM*, CURL*) { return CURLM_OK; };
    const auto addError = [](CURLM*, CURL*) { return CURLM_BAD_HANDLE; };
    const auto removeOk = [](CURLM*, CURL*) { return CURLM_OK; };
    const auto removeError = [](CURLM*, CURL*) { return CURLM_BAD_EASY_HANDLE; };
    const auto performOk = [](CURLM*, int*) { return CURLM_OK; };
    const auto performError = [](CURLM*, int*) { return CURLM_BAD_HANDLE; };
    const auto cleanup = [](CURL*) {};
    bool physicallyAttached = false;
    bool failNextStatefulAdd = false;
    bool failNextStatefulRemove = false;
    const auto statefulAdd = [&](CURLM*, CURL*)
    {
        if (failNextStatefulAdd)
        {
            failNextStatefulAdd = false;
            return CURLM_BAD_HANDLE;
        }
        if (physicallyAttached) return CURLM_ADDED_ALREADY;
        physicallyAttached = true;
        return CURLM_OK;
    };
    const auto statefulRemove = [&](CURLM*, CURL*)
    {
        if (failNextStatefulRemove)
        {
            failNextStatefulRemove = false;
            return CURLM_BAD_EASY_HANDLE;
        }
        if (!physicallyAttached) return CURLM_BAD_EASY_HANDLE;
        physicallyAttached = false;
        return CURLM_OK;
    };

    setenv(addFault, "1", 1);
    if (forwardAdd(addOk, multi, easy) != CURLM_INTERNAL_ERROR ||
        std::getenv(addFault) != nullptr)
        return 1;
    forwardCleanup(cleanup, easy);
    if (attachedHandles().find(easy) != attachedHandles().end() ||
        getInfoStates().find(easy) != getInfoStates().end())
        return 2;

    setenv(addFault, "1", 1);
    if (forwardAdd(addError, multi, easy) != CURLM_BAD_HANDLE ||
        !environmentEquals(addFault, "1"))
        return 3;
    unsetenv(addFault);

    if (forwardAdd(statefulAdd, multi, easy) != CURLM_OK ||
        !physicallyAttached)
        return 4;
    setenv(removeFault, "1", 1);
    if (forwardRemove(statefulRemove, statefulAdd, multi, easy) !=
            CURLM_INTERNAL_ERROR || std::getenv(removeFault) != nullptr ||
        !physicallyAttached)
        return 5;
    if (forwardRemove(statefulRemove, statefulAdd, multi, easy) != CURLM_OK ||
        physicallyAttached)
        return 27;
    forwardCleanup(cleanup, easy);

    if (forwardAdd(statefulAdd, multi, easy) != CURLM_OK) return 6;
    setenv(removeFault, "1", 1);
    failNextStatefulRemove = true;
    if (forwardRemove(statefulRemove, statefulAdd, multi, easy) !=
            CURLM_BAD_EASY_HANDLE || !environmentEquals(removeFault, "1") ||
        !physicallyAttached)
        return 7;
    unsetenv(removeFault);
    if (forwardRemove(statefulRemove, statefulAdd, multi, easy) != CURLM_OK ||
        physicallyAttached)
        return 8;
    forwardCleanup(cleanup, easy);

    int runningHandles = 0;
    setenv(performFault, "1", 1);
    if (forwardPerform(performOk, multi, &runningHandles) !=
            CURLM_INTERNAL_ERROR ||
        std::getenv(performFault) != nullptr)
        return 9;
    setenv(performFault, "1", 1);
    if (forwardPerform(performError, multi, &runningHandles) !=
            CURLM_BAD_HANDLE ||
        !environmentEquals(performFault, "1"))
        return 10;
    unsetenv(performFault);

    setenv(addFault, "1025", 1);
    if (forwardAdd(addOk, multi, easy) != CURLM_OK ||
        !environmentEquals(addFault, "1025") ||
        !environmentEquals(FAULT_PARSE_ERROR, addFault))
        return 11;
    unsetenv(addFault);
    unsetenv(FAULT_PARSE_ERROR);
    if (forwardRemove(removeOk, addOk, multi, easy) != CURLM_OK) return 12;

    curl_off_t invalidOverride = 7;
    setenv("OSGSOL_TEST_CURLINFO_CONN_ID_ONCE", "1x", 1);
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        if (consumeOverrideLocked(
                "OSGSOL_TEST_CURLINFO_CONN_ID_ONCE", invalidOverride) ||
            invalidOverride != 7 ||
            !environmentEquals(FAULT_PARSE_ERROR,
                               "OSGSOL_TEST_CURLINFO_CONN_ID_ONCE"))
            return 16;
    }
    unsetenv("OSGSOL_TEST_CURLINFO_CONN_ID_ONCE");
    unsetenv(FAULT_PARSE_ERROR);

    for (int reuse = 0; reuse < 2; ++reuse)
    {
        if (forwardAdd(addOk, multi, easy) != CURLM_OK) return 13;
        {
            const std::lock_guard<std::mutex> lock(faultMutex());
            getInfoStates()[easy].stage = GetInfoState::Stage::Connection;
        }
        if (forwardRemove(removeOk, addOk, multi, easy) != CURLM_OK) return 14;
        forwardCleanup(cleanup, easy);
        const std::lock_guard<std::mutex> lock(faultMutex());
        if (attachedHandles().find(easy) != attachedHandles().end() ||
            getInfoStates().find(easy) != getInfoStates().end())
            return 15;
    }

    if (forwardAdd(statefulAdd, multi, easy) != CURLM_OK ||
        !physicallyAttached)
        return 17;
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        completedTransientHandles().insert(easy);
    }
    setenv(headRetryRemoveFault, "1", 1);
    if (forwardRemove(statefulRemove, statefulAdd, multi, easy) !=
            CURLM_INTERNAL_ERROR ||
        std::getenv(headRetryRemoveFault) != nullptr ||
        !physicallyAttached)
        return 18;
    if (forwardRemove(statefulRemove, statefulAdd, multi, easy) != CURLM_OK ||
        physicallyAttached)
        return 23;
    forwardCleanup(cleanup, easy);

    if (forwardAdd(statefulAdd, multi, easy) != CURLM_OK ||
        !physicallyAttached)
        return 24;
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        completedTransientHandles().insert(easy);
    }
    setenv(headRetryRemoveFault, "2", 1);
    if (forwardRemove(statefulRemove, statefulAdd, multi, easy) !=
            CURLM_INTERNAL_ERROR ||
        !environmentEquals(headRetryRemoveFault, "1") ||
        !physicallyAttached ||
        forwardRemove(statefulRemove, statefulAdd, multi, easy) !=
            CURLM_INTERNAL_ERROR ||
        std::getenv(headRetryRemoveFault) != nullptr ||
        !physicallyAttached)
        return 25;
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        if (logicallyAttachedAfterInjectedRemoveFailure().count(easy) != 1 ||
            attachedHandles().count(easy) != 1)
            return 26;
        logicallyAttachedAfterInjectedRemoveFailure().erase(easy);
        attachedHandles().erase(easy);
        completedTransientHandles().erase(easy);
    }
    physicallyAttached = false;

    if (forwardAdd(statefulAdd, multi, easy) != CURLM_OK) return 28;
    setenv(removeFault, "1", 1);
    failNextStatefulAdd = true;
    if (forwardRemove(statefulRemove, statefulAdd, multi, easy) != CURLM_OK ||
        physicallyAttached || !environmentEquals(removeFault, "1") ||
        !environmentEquals(REMOVE_ROLLBACK_FAILURE, removeFault))
        return 29;
    unsetenv(removeFault);
    unsetenv(REMOVE_ROLLBACK_FAILURE);
    forwardCleanup(cleanup, easy);

    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        completedTransientHandles().insert(easy);
    }
    setenv(headRetryAddFault, "1", 1);
    if (forwardAdd(addOk, multi, easy) != CURLM_INTERNAL_ERROR ||
        std::getenv(headRetryAddFault) != nullptr)
        return 19;
    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        attachedHandles().erase(easy);
        completedTransientHandles().erase(easy);
    }

    {
        const std::lock_guard<std::mutex> lock(faultMutex());
        completedTransientHandles().insert(easy);
    }
    if (forwardAdd(addOk, multi, easy) != CURLM_OK) return 20;
    setenv(headRetryPerformFault, "1", 1);
    if (forwardPerform(performOk, multi, &runningHandles) !=
            CURLM_INTERNAL_ERROR ||
        std::getenv(headRetryPerformFault) != nullptr)
        return 21;
    if (forwardRemove(removeOk, addOk, multi, easy) != CURLM_OK) return 22;
    forwardCleanup(cleanup, easy);
    return 0;
}

extern "C" std::size_t osgSolTestHeadRetryRetainedStateCount()
{
    const std::lock_guard<std::mutex> lock(faultMutex());
    return logicallyAttachedAfterInjectedRemoveFailure().size();
}

extern "C" std::size_t osgSolTestAttachedStateCount()
{
    const std::lock_guard<std::mutex> lock(faultMutex());
    return attachedHandles().size();
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
    return forwardRemove(&curl_multi_remove_handle, &curl_multi_add_handle,
                         multiHandle, easyHandle);
}

extern "C" CURLMcode osgSolTestCurlMultiPerform(
    CURLM* multiHandle, int* runningHandles)
{
    return forwardPerform(&curl_multi_perform, multiHandle, runningHandles);
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
} g_curlPerformInterpose __attribute__((section("__DATA,__interpose"))) = {
    reinterpret_cast<const void*>(&osgSolTestCurlMultiPerform),
    reinterpret_cast<const void*>(&curl_multi_perform)
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
    using AddFunction = CURLMcode (*)(CURLM*, CURL*);
    static const auto realAdd = reinterpret_cast<AddFunction>(
        dlsym(RTLD_NEXT, "curl_multi_add_handle"));
    return forwardRemove(realRemove, realAdd, multiHandle, easyHandle);
}

extern "C" CURLMcode curl_multi_perform(
    CURLM* multiHandle, int* runningHandles)
{
    using PerformFunction = CURLMcode (*)(CURLM*, int*);
    static const auto realPerform = reinterpret_cast<PerformFunction>(
        dlsym(RTLD_NEXT, "curl_multi_perform"));
    return forwardPerform(realPerform, multiHandle, runningHandles);
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
