#include <algorithm>
#include <atomic>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <CommonCrypto/CommonDigest.h>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <exception>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <csignal>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cpl_conv.h>
#include <cpl_string.h>
#include <cpl_vsi.h>
#include <cpl_vsi_virtual.h>
#include <gdal_frmts.h>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <picojson.h>

#ifndef OSGSOL_SCIENCE_RANGE_SERVER
#error OSGSOL_SCIENCE_RANGE_SERVER must name the instrumented local server
#endif

#ifndef OSGSOL_PYTHON3
#error OSGSOL_PYTHON3 must name the Python interpreter
#endif

#ifndef OSGSOL_NODE
#error OSGSOL_NODE must name the Node.js interpreter
#endif

#ifndef OSGSOL_SCIENCE_HTTP2_RANGE_SERVER
#error OSGSOL_SCIENCE_HTTP2_RANGE_SERVER must name the local HTTP/2 server
#endif

#ifndef OSGSOL_SCIENCE_EVIDENCE_DIR
#error OSGSOL_SCIENCE_EVIDENCE_DIR must name a build-tree evidence directory
#endif

#ifndef OSGSOL_SCIENCE_BUILD_DIR
#error OSGSOL_SCIENCE_BUILD_DIR must name the active build directory
#endif

#ifndef OSGSOL_SCIENCE_PROTECTED_EVIDENCE_DIR
#error OSGSOL_SCIENCE_PROTECTED_EVIDENCE_DIR must name the protected evidence directory
#endif

namespace
{
    std::shared_ptr<std::recursive_mutex> pathSpecificOptionLeaseMutex(
        const std::string& path)
    {
        struct Registry
        {
            std::mutex mutex;
            std::map<std::string, std::weak_ptr<std::recursive_mutex>> leases;
        };
        static Registry registry;
        std::lock_guard<std::mutex> lock(registry.mutex);
        for (auto iterator = registry.leases.begin();
             iterator != registry.leases.end();)
        {
            if (iterator->second.expired())
                iterator = registry.leases.erase(iterator);
            else
                ++iterator;
        }
        auto& weakLease = registry.leases[path];
        std::shared_ptr<std::recursive_mutex> lease = weakLease.lock();
        if (!lease)
        {
            lease = std::make_shared<std::recursive_mutex>();
            weakLease = lease;
        }
        return lease;
    }

    constexpr int SIZE = 256;
    constexpr int BAND_COUNT = 64;
    constexpr std::int8_t NODATA_VALUE = -128;
    constexpr int LIVE_OVERVIEW_FACTOR = 4;
    constexpr std::uint64_t TRANSFER_BUDGET = 1024 * 1024;
    constexpr std::uint64_t HTTP2_TEST_BODY_BUDGET = 4 * 131072;
    constexpr std::uint64_t LIVE_TRANSFER_BUDGET = 16 * 1024 * 1024;
    constexpr double MAX_MEDIAN_MS = 3000.0;
    constexpr double MAX_P95_MS = 8000.0;
    constexpr long long TEST_CONNECTION_ID_SENTINEL =
        922337203685477000LL;
    constexpr auto COORDINATOR_RETRY_CHRONOLOGY_ROUNDING_TOLERANCE =
        std::chrono::microseconds(999);

    struct LiveCase
    {
        std::string name;
        std::string datasetId;
        std::string rawPath;
        std::string rawLocation;
        std::string rawCrs;
        std::string utmZone;
        std::string recordFingerprint;
        std::string url;
        int year = 0;
        double latitude = 0.0;
        double longitude = 0.0;
        std::vector<double> bbox;
        std::vector<double> utmBbox;
    };

    struct PhaseTimings
    {
        double openMs = 0.0;
        double georeferenceMs = 0.0;
        double readMs = 0.0;
        double closeMs = 0.0;
        double totalMs = 0.0;
    };

    struct LatencySummary
    {
        double medianMs = 0.0;
        double p95Ms = 0.0;
    };

    struct LiveMeasurement
    {
        double milliseconds = 0.0;
        PhaseTimings phases;
        std::uint64_t successfulRangeBytes = 0;
        std::uint64_t declaredTransientBytes = 0;
        std::uint64_t actualHttpBodyBytes = 0;
        std::uint64_t conservativeBodyUpperBound = 0;
        std::uint64_t sourceSize = 0;
        int actualGetCount = 0;
        int actualHeadCount = 0;
        int statsGetOperationCount = 0;
        int successfulGetCount = 0;
        int transientRetryCount = 0;
        int immediateTransientRetryCount = 0;
        int coordinatorTransientRetryCount = 0;
        std::uint64_t immediateTransientRetryBytes = 0;
        std::uint64_t coordinatorTransientRetryBytes = 0;
        std::map<int, int> transientRetryCodes;
        std::map<int, int> immediateTransientRetryCodes;
        std::map<int, int> coordinatorTransientRetryCodes;
        std::vector<int> responseCodes;
    };

    struct CoordinatorRetryEvidence
    {
        std::string range;
        int code = 0;
        std::uint64_t bytes = 0;
        int attempt = 0;
        long long delayMs = 0;
        long long connectionId = -1;
        int httpMajor = 0;
    };

    struct ImmediateRetryEvidence
    {
        std::string range;
        int code = 0;
        std::uint64_t bytes = 0;
        int attempt = 0;
        long long delayMs = 0;
        long long connectionId = -1;
        int httpMajor = 0;
    };

    enum class AttributionMode
    {
        AttributedV6,
        LegacyFrozen,
    };

    struct ScienceTransportCompletion
    {
        std::uint64_t ordinal = 0;
        std::string context;
        std::string scope;
        std::string role;
        std::uint64_t request = 0;
        int attempt = 0;
        std::string method;
        std::string range;
        int curlCode = 0;
        int status = 0;
        int httpMajor = 0;
        int redirects = 0;
        long long connectionId = -1;
        int contentLengthCount = 0;
        bool contentLengthValid = false;
        std::uint64_t declaredContentLength = 0;
        int contentRangeCount = 0;
        bool contentRangeValid = false;
        long long contentStart = -1;
        long long contentEnd = -1;
        long long contentTotal = -1;
        std::uint64_t actualBodyBytes = 0;
        std::size_t messageIndex = 0;
        bool admissionValid = true;
        std::string admissionReason;
    };

    struct HeadRetryEvidence
    {
        int retryOrdinal = 0;
        std::uint64_t request = 0;
        int failedAttempt = 0;
        int scheduledAttempt = 0;
        long long delayMs = 0;
        int failedStatus = 0;
        long long failedConnectionId = -1;
        int failedHttpMajor = 0;
        std::uint64_t failedDeclaredContentLength = 0;
        std::uint64_t failedActualBodyBytes = 0;
        std::string context;
        std::size_t messageIndex = 0;
    };

    struct ScienceServerRequest
    {
        std::string correlation;
        std::string method;
        std::string range;
        std::string sessionId;
        int streamId = 0;
        std::string path;
        int status = 0;
        std::uint64_t start = 0;
        std::uint64_t response = 0;
        std::uint64_t end = 0;
        std::uint64_t attemptedBodyBytes = 0;
        std::uint64_t contentLength = 0;
        std::string contentRange;
        bool aborted = false;
        int startCount = 0;
        int responseCount = 0;
        int endCount = 0;
        int protocolErrorCount = 0;
        std::string protocolError;
    };

    struct AttributedEventEvidence
    {
        std::string kind;
        std::string context;
        std::uint64_t request = 0;
        int attempt = 0;
        int scheduledAttempt = 0;
        std::string range;
        int status = 0;
        std::uint64_t bytes = 0;
        long long delayMs = 0;
        long long connectionId = -1;
        int httpMajor = 0;
        std::string reason;
        std::size_t messageIndex = 0;
    };

    struct AttributedTransportProof
    {
        std::vector<ScienceTransportCompletion> completions;
        std::vector<HeadRetryEvidence> headRetries;
        std::vector<AttributedEventEvidence> events;
        int fallbackCount = 0;
        int cachePublicationCount = 0;
        int propertyPublicationCount = 0;
        bool qualified = false;
    };

    struct CoordinatorRetryBlockedEvidence
    {
        std::string range;
        int code = 0;
        std::string reason;
        long long connectionId = -1;
        int httpMajor = 0;
    };

    struct MetadataPrefetchProof
    {
        bool enabled = false;
        bool attributedTransportQualified = false;
        int headRequestCount = 0;
        int rangeRequestCount = 0;
        std::uint64_t rangeStart = 0;
        std::uint64_t rangeEnd = 0;
        int headHttpVersion = 0;
        int rangeHttpVersion = 0;
        bool sharedConnection = false;
        bool requestsOverlapped = false;
        bool cachePublished = false;
        std::string fallbackReason;
        std::vector<CoordinatorRetryEvidence> coordinatorRetries;
    };

    struct HttpProof
    {
        bool attributedTransportQualified = false;
        int actualGetCount = 0;
        int actualHeadCount = 0;
        int successfulGetCount = 0;
        int transientRetryCount = 0;
        int immediateTransientRetryCount = 0;
        int coordinatorTransientRetryCount = 0;
        int coordinatorTransientFallbackCount = 0;
        int statsGetOperationCount = 0;
        int statsHeadCount = 0;
        int coordinatorLogicalGetCount = 0;
        std::uint64_t coordinatorLogicalGetBytes = 0;
        std::uint64_t coordinatorTransientRetryBytes = 0;
        std::uint64_t immediateTransientRetryBytes = 0;
        std::uint64_t coordinatorTransientFallbackBytes = 0;
        std::uint64_t successfulRangeBytes = 0;
        std::uint64_t declaredTransientBytes = 0;
        std::uint64_t actualHttpBodyBytes = 0;
        std::uint64_t conservativeBodyUpperBound = 0;
        std::uint64_t sourceSize = 0;
        std::uint64_t transferBudget = 0;
        int overviewFactor = 0;
        int rawWindowX = 0;
        int rawWindowY = 0;
        int rawWindowSize = 0;
        std::string sourceCrs;
        std::array<double, 6> geotransform = {};
        std::array<double, 2> projectedPoint = {};
        std::array<double, 2> rawPixel = {};
        std::array<double, 4> verifiedWgs84Bbox = {};
        std::map<int, int> transientRetryCodes;
        std::map<int, int> immediateTransientRetryCodes;
        std::map<int, int> coordinatorTransientRetryCodes;
        std::map<int, int> coordinatorTransientFallbackCodes;
        std::vector<int> responseCodes;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> successfulByteIntervals;
        std::vector<ImmediateRetryEvidence> immediateRetries;
        std::vector<HeadRetryEvidence> headRetries;
        std::vector<AttributedEventEvidence> scienceTransportEvents;
        std::vector<ScienceTransportCompletion> scienceTransportCompletions;
        int coordinatorHeadTransientRetryCount = 0;
        std::uint64_t coordinatorHeadTransientRetryDeclaredBytes = 0;
        std::uint64_t coordinatorHeadTransientRetryActualBodyBytes = 0;
        std::map<int, int> coordinatorHeadTransientRetryCodes;
        int scienceTransportResponseCount = 0;
        std::string scienceTransportAttribution;
        MetadataPrefetchProof metadataPrefetch;
    };

    struct LocalServerEvidence
    {
        int getCount = 0;
        int headCount = 0;
        std::uint64_t committedBytes = 0;
    };

    struct Http2StreamEvidence
    {
        std::string sessionId;
        int streamId = 0;
        std::string correlation;
        std::string method;
        std::string range;
        std::string path;
        std::uint64_t start = 0;
        std::uint64_t response = 0;
        std::uint64_t end = 0;
        int status = 0;
        std::uint64_t attemptedBodyBytes = 0;
        std::uint64_t contentLength = 0;
        std::string contentRange;
        bool aborted = false;
        int startCount = 0;
        int responseCount = 0;
        int endCount = 0;
        int protocolErrorCount = 0;
        std::string protocolError;
    };

    struct Http2Evidence
    {
        std::vector<Http2StreamEvidence> streams;
        std::uint64_t maximumAttemptedBodyBytes = 0;
        std::uint64_t totalAttemptedBodyBytes = 0;
        std::uint64_t totalReservedBodyBytes = 0;
    };

    struct PrefetchCase
    {
        const char* mode;
        bool opens;
        bool fallback;
        const char* variant = "";
        const char* httpVersion = "2TLS";
        int exactRangeCount = -1;
        const char* fallbackReason = "";
        bool requireSharedHttp2 = true;
        const char* activation = "path";
        const char* completionOrder = "";
        int exactHeadCount = -1;
        int expectedFilePropertyPublications = 1;
        const char* blockedReason = "";
        const char* getInfoFault = "";
        int transportFaultOrdinal = 0;
        bool verifyOperationScope = false;
        const char* retryDelay = "0.1";
        bool verifyCrossThreadScope = false;
    };

    struct DebugCapture
    {
        std::mutex mutex;
        std::vector<std::string> messages;
        std::vector<std::chrono::steady_clock::time_point> timestamps;
    };

    MetadataPrefetchProof buildMetadataPrefetchProof(
        const DebugCapture& capture, const HttpProof& httpProof);
    HttpProof buildHttpProof(const DebugCapture& capture,
                             const std::string& statsJson,
                             AttributionMode mode);
    AttributedTransportProof buildAttributedTransportProof(
        const DebugCapture& capture,
        const std::vector<ScienceServerRequest>& serverRequests = {});
    std::string requireNetworkStatsEvidence();
    bool parseCoordinatorRetryBlockedEvidence(
        const std::string& message, CoordinatorRetryBlockedEvidence& evidence);

    void CPL_STDCALL captureGdalMessage(CPLErr errorClass, CPLErrorNum errorNumber,
                                        const char* message);

    class ScopedGdalErrorCapture
    {
    public:
        explicit ScopedGdalErrorCapture(DebugCapture& capture)
        {
            CPLPushErrorHandlerEx(captureGdalMessage, &capture);
            CPLSetCurrentErrorHandlerCatchDebug(1);
        }

        ~ScopedGdalErrorCapture() { CPLPopErrorHandler(); }

        ScopedGdalErrorCapture(const ScopedGdalErrorCapture&) = delete;
        ScopedGdalErrorCapture& operator=(const ScopedGdalErrorCapture&) = delete;
    };

    class ScopedGdalConfig
    {
    public:
        explicit ScopedGdalConfig(
            const std::vector<std::pair<std::string, std::string>>& values)
        {
            for (const auto& value : values)
            {
                const char* previous = CPLGetConfigOption(value.first.c_str(), nullptr);
                _previous.push_back({value.first, previous ? previous : "", previous != nullptr});
                CPLSetConfigOption(value.first.c_str(), value.second.c_str());
            }
        }

        ~ScopedGdalConfig()
        {
            for (auto iterator = _previous.rbegin(); iterator != _previous.rend(); ++iterator)
                CPLSetConfigOption(iterator->key.c_str(),
                                   iterator->present ? iterator->value.c_str() : nullptr);
            VSINetworkStatsReset();
        }

        ScopedGdalConfig(const ScopedGdalConfig&) = delete;
        ScopedGdalConfig& operator=(const ScopedGdalConfig&) = delete;

    private:
        struct PreviousValue
        {
            std::string key;
            std::string value;
            bool present;
        };
        std::vector<PreviousValue> _previous;
    };

    class ScopedEnvironmentVariable
    {
    public:
        ScopedEnvironmentVariable(const char* name, const char* value)
            : _name(name)
        {
            const char* previous = std::getenv(name);
            _present = previous != nullptr;
            if (_present) _value = previous;
            if (setenv(name, value, 1) != 0)
                throw std::runtime_error("failed to set test environment");
        }

        ~ScopedEnvironmentVariable()
        {
            if (_present)
                setenv(_name.c_str(), _value.c_str(), 1);
            else
                unsetenv(_name.c_str());
        }

        ScopedEnvironmentVariable(const ScopedEnvironmentVariable&) = delete;
        ScopedEnvironmentVariable& operator=(
            const ScopedEnvironmentVariable&) = delete;

    private:
        std::string _name;
        std::string _value;
        bool _present = false;
    };

    class ScopedPathSpecificOption
    {
    public:
        ScopedPathSpecificOption(const std::string& path,
                                 const char* key, const char* value)
            : _pathMutex(pathSpecificOptionLeaseMutex(path)),
              _lock(*_pathMutex), _path(path), _key(key)
        {
            if (_path.empty() || _key.empty() || value == nullptr)
                throw std::invalid_argument(
                    "path-specific option arguments must not be empty");
            const char* previous = VSIGetPathSpecificOption(
                _path.c_str(), _key.c_str(), nullptr);
            const char* global = CPLGetConfigOption(_key.c_str(), nullptr);
            _hadPreviousPathValue = previous != nullptr && previous != global;
            if (_hadPreviousPathValue)
                _previousPathValue = previous;
            VSISetPathSpecificOption(_path.c_str(), _key.c_str(), value);
        }

        ~ScopedPathSpecificOption()
        {
            VSISetPathSpecificOption(
                _path.c_str(), _key.c_str(),
                _hadPreviousPathValue ? _previousPathValue.c_str() : nullptr);
        }

        ScopedPathSpecificOption(const ScopedPathSpecificOption&) = delete;
        ScopedPathSpecificOption& operator=(const ScopedPathSpecificOption&) = delete;

    private:
        std::shared_ptr<std::recursive_mutex> _pathMutex;
        std::unique_lock<std::recursive_mutex> _lock;
        std::string _path;
        std::string _key;
        std::string _previousPathValue;
        bool _hadPreviousPathValue = false;
    };

    std::string nextPrefetchOperationId()
    {
        static std::atomic<std::uint64_t> sequence{0};
        return "science-prefetch-operation-" +
            std::to_string(++sequence);
    }

    const picojson::value& field(const picojson::object& object,
                                 const std::string& name);
    [[noreturn]] void fail(const std::string& message);
    void require(bool condition, const std::string& message);
    void checkedAdd(std::uint64_t& target, std::uint64_t value,
                    const std::string& label);
    std::vector<ScienceServerRequest> scienceServerRequests(
        const Http2Evidence& evidence);
    std::uint64_t checkedJsonUnsigned(
        const picojson::value& value, const std::string& label,
        std::uint64_t maximum =
            std::numeric_limits<std::uint64_t>::max());

    std::string sha256(const std::string& payload)
    {
        unsigned char digest[CC_SHA256_DIGEST_LENGTH] = {};
        CC_SHA256(payload.data(), static_cast<CC_LONG>(payload.size()), digest);
        std::ostringstream stream;
        stream << std::hex << std::setfill('0');
        for (unsigned char byte : digest)
            stream << std::setw(2) << static_cast<unsigned int>(byte);
        return stream.str();
    }

    std::string canonicalNumber(double value)
    {
        std::ostringstream stream;
        stream << std::setprecision(17) << std::defaultfloat << value;
        return stream.str();
    }

    std::string canonicalRecord(const LiveCase& item)
    {
        require(item.bbox.size() == 4, "raw bbox must contain four coordinates");
        return "{\"fid\":\"" + item.datasetId + "\",\"location\":\"" +
            item.rawLocation + "\",\"path\":\"" + item.rawPath +
            "\",\"wgs84_east\":\"" + canonicalNumber(item.bbox[2]) +
            "\",\"wgs84_north\":\"" + canonicalNumber(item.bbox[3]) +
            "\",\"wgs84_south\":\"" + canonicalNumber(item.bbox[1]) +
            "\",\"wgs84_west\":\"" + canonicalNumber(item.bbox[0]) +
            "\",\"year\":" + std::to_string(item.year) + "}";
    }

    [[noreturn]] void fail(const std::string& message)
    {
        throw std::runtime_error(message);
    }

    void require(bool condition, const std::string& message)
    {
        if (!condition) fail(message);
    }

    using CurlFaultSelfTest = int (*)();
    using CurlRetainedStateCount = std::size_t (*)();

    CurlFaultSelfTest curlFaultSelfTest()
    {
        static CurlFaultSelfTest function = reinterpret_cast<CurlFaultSelfTest>(
            dlsym(RTLD_DEFAULT, "osgSolTestRunCurlFaultSelfTests"));
        return function;
    }

    bool curlFaultInterposerAvailable()
    {
        return curlFaultSelfTest() != nullptr;
    }

    CurlRetainedStateCount curlRetainedStateCount()
    {
        static CurlRetainedStateCount function =
            reinterpret_cast<CurlRetainedStateCount>(dlsym(
                RTLD_DEFAULT, "osgSolTestHeadRetryRetainedStateCount"));
        return function;
    }

    CurlRetainedStateCount curlAttachedStateCount()
    {
        static CurlRetainedStateCount function =
            reinterpret_cast<CurlRetainedStateCount>(dlsym(
                RTLD_DEFAULT, "osgSolTestAttachedStateCount"));
        return function;
    }

    void verifyCurlFaultInterposerSelfTests()
    {
        const CurlFaultSelfTest selfTest = curlFaultSelfTest();
        if (selfTest == nullptr)
        {
            std::cout << "ScienceCurlFault: unavailable; fault-dependent cases skipped"
                      << std::endl;
            return;
        }
        const int result = selfTest();
        require(result == 0,
                "curl fault interposer self-test failed at case " +
                    std::to_string(result));
        std::cout << "ScienceCurlFault: add/remove/perform one-shot and "
                     "handle-reuse self-tests passed"
                  << std::endl;
    }

    void verifyPathSpecificOptionLease()
    {
        const char* selectedPhase =
            std::getenv("OSGSOL_TEST_PATH_LEASE_PHASE");
        const std::string path =
            "/vsicurl/https://data.example/path-lease-regression.tif";
        const char* key = "OSGSOL_VSICURL_PREFETCH_OPERATION_ID";

        if (selectedPhase == nullptr ||
            std::string(selectedPhase) == "nested")
        {
            VSISetPathSpecificOption(path.c_str(), key, "OLD");
            {
                ScopedPathSpecificOption outer(path, key, "OUTER");
                require(std::string(VSIGetPathSpecificOption(
                            path.c_str(), key, "")) == "OUTER",
                        "outer path-option lease did not activate");
                {
                    ScopedPathSpecificOption inner(path, key, "INNER");
                    require(std::string(VSIGetPathSpecificOption(
                                path.c_str(), key, "")) == "INNER",
                            "inner path-option lease did not activate");
                }
                require(std::string(VSIGetPathSpecificOption(
                            path.c_str(), key, "")) == "OUTER",
                        "nested path-option lease did not restore outer value");
            }
            require(std::string(VSIGetPathSpecificOption(
                        path.c_str(), key, "")) == "OLD",
                    "outer path-option lease did not restore old value");
            VSISetPathSpecificOption(path.c_str(), key, nullptr);
        }

        if (selectedPhase == nullptr ||
            std::string(selectedPhase) == "concurrent")
        {
            struct LeaseState
            {
                std::mutex mutex{};
                std::condition_variable condition{};
                bool firstEntered = false;
                bool secondAttempted = false;
                bool secondAcquired = false;
                bool differentPathAcquired = false;
                bool releaseFirst = false;
            } state;
            VSISetPathSpecificOption(path.c_str(), key, "OLD");
            std::thread first([&]()
            {
                ScopedPathSpecificOption lease(path, key, "FIRST");
                std::unique_lock<std::mutex> lock(state.mutex);
                state.firstEntered = true;
                state.condition.notify_all();
                state.condition.wait(lock,
                    [&]() { return state.releaseFirst; });
            });
            {
                std::unique_lock<std::mutex> lock(state.mutex);
                state.condition.wait(lock,
                    [&]() { return state.firstEntered; });
            }
            std::thread differentPath([&]()
            {
                ScopedPathSpecificOption lease(
                    path + ".other", key, "DIFFERENT");
                std::lock_guard<std::mutex> lock(state.mutex);
                state.differentPathAcquired = true;
                state.condition.notify_all();
            });
            bool differentPathAcquiredBeforeRelease = false;
            {
                std::unique_lock<std::mutex> lock(state.mutex);
                differentPathAcquiredBeforeRelease = state.condition.wait_for(
                    lock, std::chrono::milliseconds(100),
                    [&]() { return state.differentPathAcquired; });
            }
            std::thread second([&]()
            {
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    state.secondAttempted = true;
                    state.condition.notify_all();
                }
                ScopedPathSpecificOption lease(path, key, "SECOND");
                std::lock_guard<std::mutex> lock(state.mutex);
                state.secondAcquired = true;
                state.condition.notify_all();
            });
            bool acquiredBeforeRelease = false;
            {
                std::unique_lock<std::mutex> lock(state.mutex);
                state.condition.wait(lock,
                    [&]() { return state.secondAttempted; });
                acquiredBeforeRelease = state.condition.wait_for(
                    lock, std::chrono::milliseconds(100),
                    [&]() { return state.secondAcquired; });
                state.releaseFirst = true;
                state.condition.notify_all();
            }
            first.join();
            second.join();
            differentPath.join();
            require(differentPathAcquiredBeforeRelease,
                    "different-path option lease was unnecessarily serialized");
            require(!acquiredBeforeRelease,
                    "concurrent path-option lease was not serialized");
            require(state.secondAcquired,
                    "second path-option lease never acquired after release");
            require(std::string(VSIGetPathSpecificOption(
                        path.c_str(), key, "")) == "OLD",
                    "concurrent path-option lease did not restore old value");
            VSISetPathSpecificOption(path.c_str(), key, nullptr);
        }
    }

    struct ServerProcess
    {
        pid_t pid = -1;

        ServerProcess() = default;
        ServerProcess(const ServerProcess&) = delete;
        ServerProcess& operator=(const ServerProcess&) = delete;

        ServerProcess(ServerProcess&& other) noexcept : pid(other.pid)
        {
            other.pid = -1;
        }

        ServerProcess& operator=(ServerProcess&& other) noexcept
        {
            if (this != &other)
            {
                terminateAndReap(false);
                pid = other.pid;
                other.pid = -1;
            }
            return *this;
        }

        ~ServerProcess()
        {
            terminateAndReap(false);
        }

        void stop()
        {
            terminateAndReap(true);
        }

    private:
        void terminateAndReap(bool requireClean)
        {
            if (pid <= 0) return;
            const pid_t child = pid;
            pid = -1;
            if (kill(child, SIGTERM) != 0 && errno != ESRCH && requireClean)
                fail("failed to terminate the range server");
            int status = 0;
            pid_t waited = -1;
            do
            {
                waited = waitpid(child, &status, 0);
            }
            while (waited < 0 && errno == EINTR);
            if (requireClean)
            {
                require(waited == child, "failed to reap the range server");
                require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                        "instrumented range server reported a violation");
            }
        }
    };

    class UniqueTempDirectory
    {
    public:
        explicit UniqueTempDirectory(const std::string& prefix)
        {
            std::string pattern =
                (std::filesystem::temp_directory_path() / (prefix + "-XXXXXX")).string();
            std::vector<char> buffer(pattern.begin(), pattern.end());
            buffer.push_back('\0');
            char* result = mkdtemp(buffer.data());
            require(result != nullptr, "mkdtemp failed");
            _path = result;
        }

        ~UniqueTempDirectory()
        {
            std::error_code ignored;
            std::filesystem::remove_all(_path, ignored);
        }

        const std::filesystem::path& path() const { return _path; }

        UniqueTempDirectory(const UniqueTempDirectory&) = delete;
        UniqueTempDirectory& operator=(const UniqueTempDirectory&) = delete;

    private:
        std::filesystem::path _path;
    };

    void registerScienceRuntime()
    {
        GDALRegister_GTiff();
        GDALRegister_VRT();
        GDALRegister_MEM();
        VSIInstallCurlFileHandler();

        std::vector<std::string> drivers;
        GDALDriverManager* manager = GetGDALDriverManager();
        for (int index = 0; index < manager->GetDriverCount(); ++index)
            drivers.emplace_back(manager->GetDriver(index)->GetDescription());
        std::sort(drivers.begin(), drivers.end());
        require(drivers == std::vector<std::string>({"GTiff", "MEM", "VRT"}),
                "range test registered drivers beyond GTiff/VRT/MEM");
    }

    void createFixture(const std::filesystem::path& path)
    {
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("GTiff");
        require(driver != nullptr, "GTiff driver lookup failed");
        char** options = nullptr;
        options = CSLSetNameValue(options, "TILED", "YES");
        options = CSLSetNameValue(options, "BLOCKXSIZE", "64");
        options = CSLSetNameValue(options, "BLOCKYSIZE", "64");
        options = CSLSetNameValue(options, "COMPRESS", "ZSTD");
        options = CSLSetNameValue(options, "INTERLEAVE", "BAND");
        options = CSLSetNameValue(options, "BIGTIFF", "YES");
        GDALDataset* dataset = driver->Create(
            path.string().c_str(), SIZE, SIZE, BAND_COUNT, GDT_Int8, options);
        CSLDestroy(options);
        require(dataset != nullptr, "failed to create local range fixture");

        std::vector<std::int8_t> pixels(SIZE * SIZE);
        std::uint32_t state = 0x13579bdfU;
        for (int bandIndex = 0; bandIndex < BAND_COUNT; ++bandIndex)
        {
            for (std::int8_t& pixel : pixels)
            {
                state = state * 1664525U + 1013904223U;
                pixel = static_cast<std::int8_t>((state >> 24) & 0x7fU);
            }
            for (int y = 16; y < 20; ++y)
                std::fill_n(pixels.begin() + y * SIZE + 16, 4, NODATA_VALUE);
            for (int y = 24; y < 28; ++y)
                std::fill_n(pixels.begin() + y * SIZE + 24, 4, 0);
            GDALRasterBand* band = dataset->GetRasterBand(bandIndex + 1);
            require(band->SetNoDataValue(NODATA_VALUE) == CE_None,
                    "failed to assign local range fixture NoData");
            require(band->RasterIO(GF_Write, 0, 0, SIZE, SIZE, pixels.data(),
                                   SIZE, SIZE, GDT_Int8, 0, 0, nullptr) == CE_None,
                    "failed to write local range fixture");
        }
        int overviews[] = {2, 4};
        require(dataset->BuildOverviews("NEAREST", 2, overviews, 0, nullptr,
                                        nullptr, nullptr) == CE_None,
                "failed to create local range overviews");
        GDALClose(dataset);
        require(std::filesystem::file_size(path) > TRANSFER_BUDGET,
                "range fixture must be larger than the byte budget");
    }

    int waitForPort(const std::filesystem::path& readyPath)
    {
        for (int attempt = 0; attempt < 250; ++attempt)
        {
            std::ifstream stream(readyPath);
            int port = 0;
            if (stream >> port) return port;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        fail("instrumented range server did not publish a port");
    }

    ServerProcess startServer(const std::filesystem::path& fixture,
                              const std::filesystem::path& ready,
                              const std::filesystem::path& log)
    {
        ServerProcess process;
        process.pid = fork();
        require(process.pid >= 0, "fork failed");
        if (process.pid == 0)
        {
            const std::string budget = std::to_string(TRANSFER_BUDGET);
            execl(OSGSOL_PYTHON3, OSGSOL_PYTHON3, OSGSOL_SCIENCE_RANGE_SERVER,
                  "--file", fixture.string().c_str(),
                  "--ready-file", ready.string().c_str(),
                  "--log-file", log.string().c_str(),
                  "--budget-bytes", budget.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        return process;
    }

    void createSelfSignedCertificate(const std::filesystem::path& certificate,
                                     const std::filesystem::path& key)
    {
        const pid_t pid = fork();
        require(pid >= 0, "fork failed while starting openssl");
        if (pid == 0)
        {
            execl("/usr/bin/openssl", "/usr/bin/openssl", "req", "-x509",
                  "-newkey", "rsa:2048", "-nodes", "-keyout",
                  key.string().c_str(), "-out", certificate.string().c_str(),
                  "-days", "1", "-subj", "/CN=127.0.0.1",
                  static_cast<char*>(nullptr));
            _exit(127);
        }
        int status = 0;
        require(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
                    WEXITSTATUS(status) == 0,
                "failed to generate the local HTTP/2 certificate");
        require(std::filesystem::file_size(certificate) > 0 &&
                    std::filesystem::file_size(key) > 0,
                "openssl did not publish the local HTTP/2 certificate");
    }

    ServerProcess startHttp2Server(const std::filesystem::path& fixture,
                                   const std::filesystem::path& ready,
                                   const std::filesystem::path& log,
                                   const std::filesystem::path& certificate,
                                   const std::filesystem::path& key,
                                   const std::string& mode,
                                   const std::string& protocol)
    {
        ServerProcess process;
        process.pid = fork();
        require(process.pid >= 0, "fork failed while starting HTTP/2 server");
        if (process.pid == 0)
        {
            const std::string budget =
                std::to_string(HTTP2_TEST_BODY_BUDGET);
            setenv("OSGSOL_TEST_SERVER_PROTOCOL", protocol.c_str(), 1);
            execl(OSGSOL_NODE, OSGSOL_NODE, OSGSOL_SCIENCE_HTTP2_RANGE_SERVER,
                  "--file", fixture.string().c_str(),
                  "--ready-file", ready.string().c_str(),
                  "--log-file", log.string().c_str(),
                  "--cert", certificate.string().c_str(),
                  "--key", key.string().c_str(),
                  "--budget-bytes", budget.c_str(),
                  "--mode", mode.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        return process;
    }

    Http2Evidence readHttp2Log(const std::filesystem::path& log)
    {
        std::ifstream stream(log);
        require(stream.good(), "HTTP/2 range log is missing");
        std::map<std::pair<std::string, int>, Http2StreamEvidence> records;
        Http2Evidence evidence;
        std::string line;
        while (std::getline(stream, line))
        {
            picojson::value value;
            const std::string error = picojson::parse(value, line);
            require(error.empty() && value.is<picojson::object>(),
                    "HTTP/2 log line is not a JSON object");
            const picojson::object& object = value.get<picojson::object>();
            const std::string event = field(object, "event").get<std::string>();
            if (event == "session_start") continue;
            require(event == "stream_start" || event == "response_headers" ||
                        event == "protocol_error" || event == "stream_end",
                    "HTTP/2 server emitted an unknown event");
            const std::string session =
                field(object, "session_id").get<std::string>();
            const int streamId = static_cast<int>(checkedJsonUnsigned(
                field(object, "stream_id"), "HTTP/2 stream ID",
                static_cast<std::uint64_t>(
                    std::numeric_limits<int>::max())));
            require(streamId > 0, "HTTP/2 stream ID must be positive");
            Http2StreamEvidence& record = records[{session, streamId}];
            record.sessionId = session;
            record.streamId = streamId;
            const std::uint64_t timestamp = checkedJsonUnsigned(
                field(object, "monotonic_ns"),
                "HTTP/2 monotonic timestamp");
            const std::string path =
                field(object, "path").get<std::string>();
            const picojson::value& correlationValue =
                field(object, "correlation");
            const std::string correlation =
                correlationValue.is<std::string>()
                    ? correlationValue.get<std::string>() : "";
            if (event == "stream_start")
            {
                require(++record.startCount == 1 && record.start == 0,
                        "duplicate HTTP/2 stream_start event");
                record.start = timestamp;
                record.path = path;
                record.correlation = correlation;
                record.method = field(object, "method").get<std::string>();
                const picojson::value& range = field(object, "range");
                record.range = range.is<std::string>() ? range.get<std::string>() : "";
            }
            else if (event == "response_headers")
            {
                require(record.startCount == 1 &&
                            ++record.responseCount == 1 &&
                            record.path == path &&
                            record.correlation == correlation,
                        "HTTP/2 response identity/count differs from stream_start");
                record.response = timestamp;
                record.status = static_cast<int>(checkedJsonUnsigned(
                    field(object, "status"), "HTTP/2 response status", 999));
                record.contentLength = checkedJsonUnsigned(
                    field(object, "content_length"),
                    "HTTP/2 response Content-Length");
                const picojson::value& contentRange =
                    field(object, "content_range");
                record.contentRange = contentRange.is<std::string>()
                    ? contentRange.get<std::string>() : "";
                require(field(object, "violation").is<picojson::null>(),
                        "HTTP/2 server rejected a client request");
            }
            else if (event == "protocol_error")
            {
                require(record.startCount == 1 &&
                            record.responseCount == 0 &&
                            ++record.protocolErrorCount == 1 &&
                            record.path == path &&
                            record.correlation == correlation &&
                            field(object, "method").get<std::string>() ==
                                record.method,
                        "HTTP/2 protocol error identity/count differs from "
                        "stream_start");
                const picojson::value& range = field(object, "range");
                require((range.is<std::string>()
                            ? range.get<std::string>() : "") == record.range,
                        "HTTP/2 protocol error Range differs from stream_start");
                record.response = timestamp;
                record.protocolError =
                    field(object, "error_code").get<std::string>();
                require(record.protocolError ==
                            "ERR_HTTP2_HEADER_SINGLE_VALUE" &&
                            field(object, "violation").is<picojson::null>(),
                        "HTTP/2 duplicate-header fixture did not expose the "
                        "exact local protocol rejection");
            }
            else
            {
                require(record.startCount == 1 &&
                            record.responseCount +
                                record.protocolErrorCount == 1 &&
                            ++record.endCount == 1 && record.end == 0 &&
                            record.path == path &&
                            record.correlation == correlation,
                        "HTTP/2 stream_end identity/count differs from its stream");
                record.end = timestamp;
                record.attemptedBodyBytes = checkedJsonUnsigned(
                    field(object, "attempted_body_bytes"),
                    "HTTP/2 attempted body bytes");
                evidence.totalAttemptedBodyBytes = std::max(
                    evidence.totalAttemptedBodyBytes,
                    checkedJsonUnsigned(
                        field(object, "total_attempted_body_bytes"),
                        "HTTP/2 total attempted body bytes"));
                evidence.totalReservedBodyBytes = std::max(
                    evidence.totalReservedBodyBytes,
                    checkedJsonUnsigned(
                        field(object, "total_reserved_body_bytes"),
                        "HTTP/2 total reserved body bytes"));
                record.aborted = field(object, "aborted").get<bool>();
            }
        }
        for (const auto& item : records)
        {
            require(item.second.startCount == 1 &&
                        item.second.responseCount +
                            item.second.protocolErrorCount == 1 &&
                        item.second.endCount == 1 &&
                        item.second.start > 0 &&
                        item.second.response > item.second.start &&
                        item.second.end >= item.second.response &&
                        (item.second.status > 0 ||
                         item.second.protocolErrorCount == 1),
                    "HTTP/2 stream evidence is incomplete for " +
                        item.second.method + " stream " +
                        std::to_string(item.second.streamId) +
                        " status=" + std::to_string(item.second.status) +
                        " start=" + std::to_string(item.second.start) +
                        " end=" + std::to_string(item.second.end));
            evidence.maximumAttemptedBodyBytes = std::max(
                evidence.maximumAttemptedBodyBytes,
                item.second.attemptedBodyBytes);
            evidence.streams.push_back(item.second);
        }
        require(!evidence.streams.empty(), "HTTP/2 server logged no streams");
        require(evidence.totalReservedBodyBytes <= HTTP2_TEST_BODY_BUDGET,
                "HTTP/2 server oversubscribed its synchronous body budget");
        return evidence;
    }

    Http2Evidence verifyHttp2Log(
        const std::filesystem::path& log,
        bool require206ContentRange = true)
    {
        Http2Evidence evidence = readHttp2Log(log);
        for (const Http2StreamEvidence& stream : evidence.streams)
        {
            if (stream.method == "HEAD")
            {
                require(stream.attemptedBodyBytes == 0,
                        "HTTP/2 HEAD stream attempted a body");
            }
            else if (stream.status == 206)
            {
                require(stream.attemptedBodyBytes == stream.contentLength,
                        "HTTP/2 206 stream body/Content-Length differs");
                if (require206ContentRange)
                    require(!stream.contentRange.empty(),
                            "HTTP/2 206 stream Content-Range is absent");
            }
        }
        return evidence;
    }

    Http2Evidence verifyLegacyInterruptedHttp2Log(
        const std::filesystem::path& log)
    {
        Http2Evidence evidence = readHttp2Log(log);
        int partialRanges = 0;
        std::ostringstream rangeDetails;
        for (const Http2StreamEvidence& stream : evidence.streams)
        {
            if (stream.method == "HEAD")
            {
                require(stream.attemptedBodyBytes == 0,
                        "legacy transport fixture gave HEAD a body");
            }
            else if (stream.status == 206 &&
                     stream.attemptedBodyBytes < stream.contentLength)
            {
                ++partialRanges;
                require(!stream.contentRange.empty(),
                        "legacy interrupted 206 fixture did not prove a bounded "
                        "partial Range body");
            }
            else if (stream.status == 206)
            {
                require(stream.attemptedBodyBytes == stream.contentLength &&
                            !stream.contentRange.empty(),
                        "legacy transport fixture has an invalid complete 206");
            }
            if (stream.status == 206)
            {
                rangeDetails << " attempted=" << stream.attemptedBodyBytes
                             << " declared=" << stream.contentLength
                             << " aborted=" << (stream.aborted ? 1 : 0);
            }
        }
        require(partialRanges == 2,
                "legacy transport fixture did not contain the coordinator "
                "and ordinary partial Ranges:" +
                    rangeDetails.str());
        return evidence;
    }

    bool containsDebug(const DebugCapture& capture, const std::string& needle)
    {
        return std::any_of(capture.messages.begin(), capture.messages.end(),
            [&needle](const std::string& message)
            {
                return message.find(needle) != std::string::npos;
            });
    }

    int countDebug(const DebugCapture& capture, const std::string& needle)
    {
        return static_cast<int>(std::count_if(
            capture.messages.begin(), capture.messages.end(),
            [&needle](const std::string& message)
            {
                return message.find(needle) != std::string::npos;
            }));
    }

    std::uint64_t networkStatsUnsigned(const std::string& statsJson,
                                       const std::string& method,
                                       const std::string& name)
    {
        picojson::value value;
        require(picojson::parse(value, statsJson).empty() &&
                    value.is<picojson::object>(),
                "compatibility network stats are invalid");
        const picojson::object& methods = field(
            value.get<picojson::object>(), "methods").get<picojson::object>();
        const picojson::object& methodObject =
            field(methods, method).get<picojson::object>();
        return checkedJsonUnsigned(field(methodObject, name),
                                   "compatibility network stats " + method +
                                       " " + name);
    }

    std::string parallelDebugSummary(const DebugCapture& capture)
    {
        std::string summary;
        for (const std::string& message : capture.messages)
        {
            if (message.find("ParallelHeadRange:") == std::string::npos)
                continue;
            if (!summary.empty()) summary += " | ";
            summary += message;
        }
        return summary;
    }

    void verifyParallelMetadataPrefetch(const std::filesystem::path& fixture,
                                        const std::filesystem::path& root)
    {
        const std::filesystem::path certificate = root / "http2-cert.pem";
        const std::filesystem::path key = root / "http2-key.pem";
        createSelfSignedCertificate(certificate, key);

        const char* selectedMode =
            CPLGetConfigOption("OSGSOL_TEST_PREFETCH_CASE", nullptr);
        if (selectedMode && std::string(selectedMode).rfind("v6-", 0) == 0)
            return;
        if (selectedMode &&
            std::string(selectedMode) == "blocked-operation-capacity")
        {
            if (!curlFaultInterposerAvailable())
            {
                std::cout << "ScienceHttp2Prefetch: mode="
                             "blocked-operation-capacity skipped="
                             "curl-fault-interposer-unavailable"
                          << std::endl;
                return;
            }
            const std::filesystem::path ready =
                root / "http2-blocked-operation-capacity.ready";
            const std::filesystem::path log =
                root / "http2-blocked-operation-capacity.jsonl";
            ServerProcess server = startHttp2Server(
                fixture, ready, log, certificate, key,
                "range-500-twice", "h2");
            const int port = waitForPort(ready);
            const std::string baseUrl =
                "/vsicurl/https://127.0.0.1:" + std::to_string(port) +
                "/capacity-block/alphaearth-range-fixture.tif";
            const std::string afterClearUrl =
                "/vsicurl/https://127.0.0.1:" + std::to_string(port) +
                "/capacity-block/after-clear/alphaearth-range-fixture.tif";
            const std::string expiryUrl =
                "/vsicurl/https://127.0.0.1:" + std::to_string(port) +
                "/capacity-block/expiry/alphaearth-range-fixture.tif";

            ScopedGdalConfig config({
                {"GDAL_HTTP_UNSAFESSL", "YES"},
                {"GDAL_HTTP_VERSION", "2TLS"},
                {"GDAL_HTTP_PROXY", ""},
                {"GDAL_HTTPS_PROXY", ""},
                {"GDAL_HTTP_MAX_RETRY", "2"},
                {"GDAL_HTTP_RETRY_DELAY", "0.01"},
            });
            VSICurlClearCache();
            DebugCapture capture;
            ScopedGdalErrorCapture errorCapture(capture);

            const auto markBlocked = [&](const std::string& url,
                                         const std::string& token)
            {
                VSISetPathSpecificOption(url.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
                VSISetPathSpecificOption(url.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", token.c_str());
                setenv("OSGSOL_TEST_CURLINFO_CONN_ID_ONCE",
                       std::to_string(TEST_CONNECTION_ID_SENTINEL).c_str(), 1);
                GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(
                    url.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                    nullptr, nullptr, nullptr));
                require(dataset == nullptr,
                        "capacity setup unexpectedly opened a blocked dataset");
                require(std::getenv("OSGSOL_TEST_CURLINFO_CONN_ID_ONCE") == nullptr,
                        "capacity setup did not consume the connection-id fault");
            };
            const auto probeBlocked = [&](const std::string& url,
                                          const std::string& token,
                                          const std::string& label)
            {
                VSISetPathSpecificOption(url.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
                VSISetPathSpecificOption(url.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", token.c_str());
                const auto logSize = std::filesystem::file_size(log);
                const int rejected = countDebug(
                    capture, "ParallelHeadRange: blocked-operation-rejected");
                VSILFILE* blocked = VSIFOpenL(url.c_str(), "rb");
                require(blocked == nullptr, label + " unexpectedly opened");
                require(countDebug(capture,
                            "ParallelHeadRange: blocked-operation-rejected") ==
                            rejected + 1,
                        label + " was not rejected by the actual GDAL handler");
                require(std::filesystem::file_size(log) == logSize,
                        label + " reached the local HTTP/2 server");
            };

            std::vector<std::string> tokens;
            tokens.reserve(257);
            for (int index = 0; index < 257; ++index)
            {
                tokens.push_back("capacity-token-" + std::to_string(index + 1));
                markBlocked(baseUrl, tokens.back());
            }
            probeBlocked(baseUrl, tokens.front(), "first live capacity token");
            probeBlocked(baseUrl, tokens.back(), "257th overflow token");

            VSICurlPartialClearCache(baseUrl.c_str());
            probeBlocked(baseUrl, "capacity-token-after-partial-clear",
                         "handler overflow after PartialClearCache");

            VSICurlClearCache();
            const int rejectedAfterClear = countDebug(
                capture, "ParallelHeadRange: blocked-operation-rejected");
            const auto logSizeBeforeClearProbe =
                std::filesystem::file_size(log);
            VSISetPathSpecificOption(afterClearUrl.c_str(),
                "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
            VSISetPathSpecificOption(afterClearUrl.c_str(),
                "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", "after-clear-token");
            VSILFILE* afterClear = VSIFOpenL(afterClearUrl.c_str(), "rb");
            require(afterClear == nullptr,
                    "always-500 after-clear probe unexpectedly opened");
            require(countDebug(capture,
                        "ParallelHeadRange: blocked-operation-rejected") ==
                        rejectedAfterClear,
                    "ClearCache did not clear handler overflow");
            require(std::filesystem::file_size(log) > logSizeBeforeClearProbe,
                    "ClearCache probe did not reach the local HTTP/2 server");

            VSICurlClearCache();
            for (int index = 0; index < 257; ++index)
            {
                markBlocked(expiryUrl,
                            "expiry-token-" + std::to_string(index + 1));
            }
            probeBlocked(expiryUrl, "expiry-token-1",
                         "live overflow expiry token");
            const int rejectedBeforeExpiryProbe = countDebug(
                capture, "ParallelHeadRange: blocked-operation-rejected");
            const auto logSizeBeforeExpiryProbe =
                std::filesystem::file_size(log);
            {
                ScopedEnvironmentVariable clockOffset(
                    "OSGSOL_TEST_STEADY_CLOCK_OFFSET_MS", "301000");
                VSISetPathSpecificOption(expiryUrl.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", "expiry-token-3");
                VSILFILE* afterExpiry = VSIFOpenL(expiryUrl.c_str(), "rb");
                require(afterExpiry == nullptr,
                        "always-500 post-expiry probe unexpectedly opened");
            }
            require(countDebug(capture,
                        "ParallelHeadRange: blocked-operation-rejected") ==
                        rejectedBeforeExpiryProbe,
                    "bounded handler overflow did not expire safely");
            require(std::filesystem::file_size(log) > logSizeBeforeExpiryProbe,
                    "expired overflow probe did not reach local HTTP/2");

            VSIClearPathSpecificOptions(baseUrl.c_str());
            VSIClearPathSpecificOptions(afterClearUrl.c_str());
            VSIClearPathSpecificOptions(expiryUrl.c_str());
            server.stop();
            const Http2Evidence evidence = verifyHttp2Log(log);
            require(!evidence.streams.empty(),
                    "capacity regression emitted no local HTTP/2 evidence");
            std::cout << "ScienceHttp2Prefetch: mode=blocked-operation-capacity"
                      << " tokens=257 overflow=bounded" << std::endl;
            return;
        }

        std::int8_t expectedByteZero = 0;
        {
            GDALDataset* local = static_cast<GDALDataset*>(GDALOpenEx(
                fixture.string().c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                nullptr, nullptr, nullptr));
            require(local != nullptr &&
                        local->GetRasterBand(1)->RasterIO(
                            GF_Read, 0, 0, 1, 1, &expectedByteZero,
                            1, 1, GDT_Int8, 0, 0, nullptr) == CE_None,
                    "failed to read the coordinator cache oracle byte");
            GDALClose(local);
        }

        const PrefetchCase cases[] = {
            {"success", true, false},
            {"success", true, false, "missing-operation-token", "2TLS", 1,
                "", false, "path-no-token", "", 1, 0},
            {"range-503", true, false, "", "2TLS", 2},
            {"range-429-once", true, false, "", "2TLS", 2,
                "", true, "path", "", 1},
            {"range-500-once", true, false, "", "2TLS", 2,
                "", true, "path", "", 1},
            {"range-502-once", true, false, "", "2TLS", 2,
                "", true, "path", "", 1},
            {"range-503-once", true, false, "", "2TLS", 2,
                "", true, "path", "", 1},
            {"range-504-once", true, false, "", "2TLS", 2,
                "", true, "path", "", 1},
            {"range-500-once", false, true, "head-503-invalid", "2TLS", 1,
                "head-invalid", true, "path", "", 2, -1,
                "head-invalid"},
            {"range-500-once", false, true, "http1-invalid", "1.1", 1,
                "head-protocol", false, "path", "", 2, -1,
                "head-protocol"},
            {"range-500-once", false, true, "connection-invalid", "2TLS", 1,
                "range-connection", true, "path", "", 1, -1,
                "range-connection", "connection"},
            {"range-500-once", false, true, "redirect-invalid", "2TLS", 1,
                "range-redirect", true, "path", "", 1, -1,
                "range-redirect", "redirect"},
            {"range-500-once", false, true, "transport-initial", "2TLS", 1,
                "range-transport", true, "path", "", 1, -1,
                "range-transport", "", 1},
            {"range-500-twice", false, true, "transport-retry", "2TLS", 2,
                "range-transport", true, "path", "", 1, -1,
                "range-transport", "", 2},
            {"range-500-once", false, true, "head-503-operation-scope",
                "2TLS", 2, "head-invalid", true, "path", "", 3, 1,
                "head-invalid", "", 0, true},
            {"range-500-once", false, true, "head-503-cross-thread-scope",
                "2TLS", 1, "head-invalid", true, "path", "", 2, 0,
                "head-invalid", "", 0, false, "0.1", true},
            {"range-500-once", false, true, "invalid-delay-nan",
                "2TLS", 1, "retry-delay", true, "path", "", 1, 0,
                "", "", 0, false, "nan"},
            {"range-500-once", false, true, "invalid-delay-huge",
                "2TLS", 1, "retry-delay", true, "path", "", 1, 0,
                "", "", 0, false, "1e300"},
            {"range-503-exhaust", false, false, "", "2TLS", 3,
                "", true, "path", "", 1},
            {"success", false, false, "range-404", "2TLS", 1,
                "", true, "path", "", 1},
            {"range-200", false, false},
            {"range-200-body", false, false},
            {"short-range", false, false},
            {"size-mismatch", false, false},
            {"success", true, true, "head-503", "2TLS", 2,
                "head-invalid", true},
            {"success", true, true, "head-405", "2TLS", 2,
                "head-invalid", true},
            {"success", true, true, "redirect-source", "2TLS", 4,
                "redirect", true},
            {"success", true, true, "http1", "1.1", 2,
                "protocol", false},
            {"success", false, false, "content-range-missing", "2TLS", 1,
                "", true},
            {"success", false, false, "content-range-malformed", "2TLS", 1,
                "", true},
            {"success", false, false, "content-range-spoof", "2TLS", 1,
                "", true},
            {"success", false, false, "content-range-duplicate", "2TLS", 1,
                "", true},
            {"success", false, false, "content-range-undersized-total",
                "2TLS", 1, "", true},
            {"success", true, false, "range-first", "2TLS", 1,
                "", true, "path", "range-first"},
            {"success", true, false, "head-first", "2TLS", 1,
                "", true, "path", "head-first"},
            {"success", false, true, "head-503-exhaust", "2TLS", 1,
                "head-invalid", true, "path", "", 3},
            {"success", true, true, "transport-interrupt", "2TLS", -1,
                "transport", true, "path", "", 1},
            {"success", true, false, "default-off", "2TLS", 1,
                "", false, "none", "", 1, 0},
            {"success", true, false, "global-only", "2TLS", 1,
                "", false, "global", "", 1, 0},
            {"success", true, true, "remove-retry", "2TLS", 2,
                "detach", true},
            {"success", true, true, "remove-failure", "2TLS", 2,
                "detach", true, "path", "", 2, 0},
        };
        const std::map<std::string, int> transientOnceStatuses = {
            {"range-429-once", 429},
            {"range-500-once", 500},
            {"range-502-once", 502},
            {"range-503-once", 503},
            {"range-504-once", 504},
        };
        int executedCases = 0;
        for (const PrefetchCase& prefetchCase : cases)
        {
            const std::string caseName = std::string(prefetchCase.mode) +
                (prefetchCase.variant[0] ? "-" + std::string(prefetchCase.variant) : "");
            if (!selectedMode && caseName == "success-remove-failure")
                continue;
            if (selectedMode && std::string(selectedMode) != caseName)
                continue;
            ++executedCases;
            const bool requiresFaultInterposer =
                std::string(prefetchCase.variant).find("remove-") == 0 ||
                prefetchCase.getInfoFault[0] != '\0' ||
                prefetchCase.transportFaultOrdinal > 0;
            if (requiresFaultInterposer && !curlFaultInterposerAvailable())
            {
                std::cout << "ScienceHttp2Prefetch: mode=" << caseName
                          << " skipped=curl-fault-interposer-unavailable"
                          << std::endl;
                continue;
            }
            const auto transientOnce =
                transientOnceStatuses.find(prefetchCase.mode);
            const std::filesystem::path ready =
                root / (std::string("http2-") + caseName + ".ready");
            const std::filesystem::path log =
                root / (std::string("http2-") + caseName + ".jsonl");
            ServerProcess server = startHttp2Server(
                fixture, ready, log, certificate, key, prefetchCase.mode,
                std::string(prefetchCase.variant).find("http1") == 0
                    ? "http1" : "h2");
            const int port = waitForPort(ready);
            const std::string vsiUrl = "/vsicurl/https://127.0.0.1:" +
                std::to_string(port) + "/" + caseName +
                "/alphaearth-range-fixture.tif";

            ScopedGdalConfig config({
                {"GDAL_HTTP_UNSAFESSL", "YES"},
                {"GDAL_HTTP_VERSION", prefetchCase.httpVersion},
                {"GDAL_HTTP_PROXY", ""},
                {"GDAL_HTTPS_PROXY", ""},
                {"GDAL_HTTP_MAX_RETRY", "2"},
                {"GDAL_HTTP_RETRY_DELAY", prefetchCase.retryDelay},
                {"OSGSOL_VSICURL_PREFETCH_HEAD_RANGE",
                    std::string(prefetchCase.activation) == "global" ? "YES" : ""},
            });
            VSICurlClearCache();
            VSINetworkStatsReset();
            DebugCapture capture;
            bool opened = false;
            const std::string operationId = nextPrefetchOperationId();
            {
                ScopedGdalErrorCapture errorCapture(capture);
                if (std::string(prefetchCase.activation) == "path" ||
                    std::string(prefetchCase.activation) == "path-no-token")
                {
                    VSISetPathSpecificOption(vsiUrl.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
                    if (std::string(prefetchCase.activation) == "path")
                    {
                        VSISetPathSpecificOption(vsiUrl.c_str(),
                            "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
                            operationId.c_str());
                    }
                    if (std::string(prefetchCase.variant) == "remove-retry")
                    {
                        setenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE", "1", 1);
                    }
                    if (std::string(prefetchCase.variant) == "remove-failure")
                    {
                        setenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE", "2", 1);
                    }
                    if (std::string(prefetchCase.getInfoFault) == "connection")
                    {
                        setenv("OSGSOL_TEST_CURLINFO_CONN_ID_ONCE",
                               std::to_string(TEST_CONNECTION_ID_SENTINEL).c_str(),
                               1);
                    }
                    if (std::string(prefetchCase.getInfoFault) == "redirect")
                    {
                        setenv("OSGSOL_TEST_CURLINFO_REDIRECT_COUNT_ONCE", "1", 1);
                    }
                    if (prefetchCase.transportFaultOrdinal > 0)
                    {
                        setenv("OSGSOL_TEST_CURLMSG_TRANSPORT_ON_500",
                            std::to_string(
                                prefetchCase.transportFaultOrdinal).c_str(), 1);
                    }
                }
                GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(
                    vsiUrl.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                    nullptr, nullptr, nullptr));
                opened = dataset != nullptr;
                if (dataset)
                {
                    std::int8_t byteZero = 0;
                    require(dataset->GetRasterBand(1)->RasterIO(
                                GF_Read, 0, 0, 1, 1, &byteZero,
                                1, 1, GDT_Int8, 0, 0, nullptr) == CE_None,
                            "prefetch fixture byte-zero read failed");
                    require(byteZero == expectedByteZero,
                            caseName + " exposed stale coordinator bytes");
                    GDALClose(dataset);
                }
                if (prefetchCase.getInfoFault[0])
                {
                    const char* faultName =
                        std::string(prefetchCase.getInfoFault) == "connection"
                        ? "OSGSOL_TEST_CURLINFO_CONN_ID_ONCE"
                        : "OSGSOL_TEST_CURLINFO_REDIRECT_COUNT_ONCE";
                    require(std::getenv(faultName) == nullptr,
                            caseName + " did not consume its one-shot curl fault");
                }
                if (prefetchCase.transportFaultOrdinal > 0)
                {
                    require(std::getenv(
                                "OSGSOL_TEST_CURLMSG_TRANSPORT_ON_500") ==
                                nullptr,
                            caseName +
                                " did not consume its exact 500 transport fault");
                }
                if (prefetchCase.verifyCrossThreadScope)
                {
                    std::atomic<bool> crossThreadOpened{false};
                    std::thread sameOperation([&]()
                    {
                        ScopedGdalErrorCapture threadErrorCapture(capture);
                        VSILFILE* blocked = VSIFOpenL(vsiUrl.c_str(), "rb");
                        crossThreadOpened.store(blocked != nullptr);
                        if (blocked != nullptr)
                            VSIFCloseL(blocked);
                    });
                    sameOperation.join();
                    require(!crossThreadOpened.load(),
                        caseName +
                            " same-token cross-thread open escaped blocking");
                }
                if (prefetchCase.verifyOperationScope)
                {
                    for (int probe = 0; probe < 2; ++probe)
                    {
                        VSILFILE* blocked = VSIFOpenL(vsiUrl.c_str(), "rb");
                        require(blocked == nullptr,
                            caseName + " same-token probe " +
                                std::to_string(probe + 2) +
                                " escaped the blocked operation");
                    }
                    const std::string replacementOperationId =
                        nextPrefetchOperationId();
                    VSISetPathSpecificOption(vsiUrl.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
                        replacementOperationId.c_str());
                    VSILFILE* recovered = VSIFOpenL(vsiUrl.c_str(), "rb");
                    require(recovered != nullptr,
                        caseName + " new-token independent open stayed blocked");
                    require(VSIFCloseL(recovered) == 0,
                        caseName + " new-token independent open did not close");
                    VSISetPathSpecificOption(vsiUrl.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
                        operationId.c_str());
                    VSILFILE* original = VSIFOpenL(vsiUrl.c_str(), "rb");
                    require(original == nullptr,
                        caseName +
                            " token A escaped after token B completed");
                }
                if (std::string(prefetchCase.activation) == "path" ||
                    std::string(prefetchCase.activation) == "path-no-token")
                    VSIClearPathSpecificOptions(vsiUrl.c_str());
            }
            std::string coordinatorStatsJson;
            if (transientOnce != transientOnceStatuses.end() ||
                std::string(prefetchCase.mode) == "range-503-exhaust")
            {
                coordinatorStatsJson = requireNetworkStatsEvidence();
            }
            server.stop();
            const bool legacyNegativeContentRange =
                std::string(prefetchCase.variant).find("content-range-") == 0;
            const bool legacyAborted206Body =
                std::string(prefetchCase.variant) == "transport-interrupt";
            const Http2Evidence evidence = legacyAborted206Body
                ? verifyLegacyInterruptedHttp2Log(log)
                : verifyHttp2Log(log, !legacyNegativeContentRange);

            const Http2StreamEvidence* head = nullptr;
            const Http2StreamEvidence* firstRange = nullptr;
            std::vector<const Http2StreamEvidence*> ranges;
            int rangeCount = 0;
            int getWithoutRangeCount = 0;
            int headCount = 0;
            for (const Http2StreamEvidence& stream : evidence.streams)
            {
                if (stream.method == "HEAD")
                {
                    ++headCount;
                    if (head == nullptr) head = &stream;
                }
                if (stream.method == "GET")
                {
                    if (stream.range.empty())
                        ++getWithoutRangeCount;
                    else
                    {
                        ++rangeCount;
                        ranges.push_back(&stream);
                        if (firstRange == nullptr) firstRange = &stream;
                    }
                }
            }
            require(head != nullptr && firstRange != nullptr,
                    "prefetch case omitted HEAD or first Range");
            const auto verifyTransport = [&]()
            {
                require(head->start < firstRange->start &&
                            firstRange->start < head->end,
                        "HEAD and first Range did not overlap");
                require(head->sessionId == firstRange->sessionId,
                        "HEAD and first Range used different HTTP/2 sessions");
                require(firstRange->range == "bytes=0-131071",
                        "prefetch Range changed");
            };
            if (std::string(prefetchCase.mode) == "success" &&
                (prefetchCase.variant[0] == '\0' ||
                 std::string(prefetchCase.variant).find("content-range-") == 0 ||
                 prefetchCase.completionOrder[0] != '\0'))
                verifyTransport();
            require(opened == prefetchCase.opens,
                    std::string(prefetchCase.mode) +
                        " prefetch open result changed: " +
                        parallelDebugSummary(capture));
            const bool expectedStarted =
                std::string(prefetchCase.activation) == "path";
            const bool invalidRetryDelay =
                std::string(prefetchCase.variant).find("invalid-delay-") == 0;
            require(containsDebug(capture, "ParallelHeadRange: started") ==
                        expectedStarted,
                    caseName + " coordinator activation changed");
            if (std::string(prefetchCase.mode) != "success" &&
                prefetchCase.requireSharedHttp2)
                verifyTransport();

            if (transientOnce != transientOnceStatuses.end() &&
                prefetchCase.blockedReason[0] == '\0' && !invalidRetryDelay)
            {
                require(headCount == 1 && ranges.size() == 2 &&
                            ranges[0]->range == "bytes=0-131071" &&
                            ranges[1]->range == "bytes=0-131071" &&
                            ranges[0]->status == transientOnce->second &&
                            ranges[0]->attemptedBodyBytes == 17 &&
                            ranges[1]->status == 206 &&
                            ranges[0]->sessionId == head->sessionId &&
                            ranges[1]->sessionId == head->sessionId,
                        caseName +
                            " did not preserve the one-session retry request contract");
                require(countDebug(capture,
                            "ParallelHeadRange: transient-retry") == 1 &&
                            countDebug(capture,
                            "ParallelHeadRange: transient-fallback") == 0 &&
                            countDebug(capture,
                            "ParallelHeadRange: published") == 1 && opened,
                        caseName +
                            " did not recover through one coordinator retry: " +
                            parallelDebugSummary(capture));

                require(networkStatsUnsigned(
                            coordinatorStatsJson, "HEAD", "count") == 1 &&
                            networkStatsUnsigned(
                                coordinatorStatsJson, "GET", "count") == 1 &&
                            networkStatsUnsigned(coordinatorStatsJson, "GET",
                                                 "downloaded_bytes") == 131089 &&
                            countDebug(capture,
                                "ParallelHeadRange: logical-get-complete ") == 1,
                        caseName +
                            " compatibility behavior/stat tuple did not "
                            "reconcile");
            }
            if (prefetchCase.blockedReason[0])
            {
                const int expectedBlockedRangeCount =
                    prefetchCase.verifyOperationScope ? 2 :
                    prefetchCase.exactRangeCount;
                const int expectedFailedRangeCount =
                    prefetchCase.transportFaultOrdinal == 2 ? 2 : 1;
                require(rangeCount == expectedBlockedRangeCount &&
                            ranges.size() ==
                                static_cast<std::size_t>(
                                    expectedBlockedRangeCount) &&
                            std::count_if(ranges.begin(), ranges.end(),
                            [](const Http2StreamEvidence* range)
                            {
                                return range->status == 500 &&
                                    range->range == "bytes=0-131071";
                            }) == expectedFailedRangeCount,
                        caseName + " emitted an unexpected blocked Range set");
                require(countDebug(capture,
                            "ParallelHeadRange: transient-retry range=") ==
                            (prefetchCase.transportFaultOrdinal == 2 ? 1 : 0) &&
                        countDebug(capture,
                            "ParallelHeadRange: published") ==
                            (prefetchCase.verifyOperationScope ? 1 : 0),
                        caseName + " retried or published an invalid Range: " +
                            parallelDebugSummary(capture));
                std::vector<CoordinatorRetryBlockedEvidence> blocked;
                for (const std::string& message : capture.messages)
                {
                    CoordinatorRetryBlockedEvidence evidence;
                    if (parseCoordinatorRetryBlockedEvidence(message, evidence))
                        blocked.push_back(evidence);
                    else
                    {
                        require(message.find(
                                    "ParallelHeadRange: transient-retry-blocked") ==
                                    std::string::npos,
                                caseName + " emitted a malformed blocked event");
                    }
                }
                const bool hasExpectedBlockedConnection =
                    std::string(prefetchCase.getInfoFault) == "connection"
                    ? blocked.size() == 1 &&
                        blocked.front().connectionId ==
                            TEST_CONNECTION_ID_SENTINEL
                    : blocked.size() == 1 &&
                        blocked.front().connectionId >= 0;
                require(blocked.size() == 1 &&
                            blocked.front().range == "bytes=0-131071" &&
                            blocked.front().code == 500 &&
                            blocked.front().reason == prefetchCase.blockedReason &&
                            hasExpectedBlockedConnection &&
                            blocked.front().httpMajor ==
                                (std::string(prefetchCase.httpVersion) == "1.1"
                                    ? 1 : 2),
                        caseName + " omitted its exact fail-closed retry event");
                require(countDebug(capture,
                            "ParallelHeadRange: blocked-operation-marked") == 1 &&
                            countDebug(capture,
                            "ParallelHeadRange: blocked-operation-expired") == 0 &&
                            countDebug(capture,
                            "ParallelHeadRange: blocked-probe-consumed") == 0,
                        caseName + " did not retain operation-scoped blocking");
                if (prefetchCase.verifyOperationScope)
                {
                    require(countDebug(capture,
                                "ParallelHeadRange: blocked-operation-rejected") >=
                                4 &&
                                countDebug(capture,
                                "ParallelHeadRange: blocked-operation-cleared=") ==
                                0,
                            caseName +
                                " did not block every same-token probe then "
                                "preserve token A across token B");
                }
                if (prefetchCase.verifyCrossThreadScope)
                {
                    require(countDebug(capture,
                                "ParallelHeadRange: blocked-operation-rejected") >=
                                2,
                            caseName +
                                " omitted cross-thread blocked evidence");
                }
            }
            if (std::string(prefetchCase.mode) == "range-503-exhaust")
            {
                require(headCount == 1 && ranges.size() == 3,
                        "range-503-exhaust request counts changed");
                for (const Http2StreamEvidence* range : ranges)
                {
                    require(range->range == "bytes=0-131071" &&
                                range->status == 503 &&
                                range->attemptedBodyBytes == 17 &&
                                range->sessionId == head->sessionId,
                            "range-503-exhaust retry stream changed");
                }
                require(countDebug(capture,
                            "ParallelHeadRange: transient-retry") == 2 &&
                            countDebug(capture,
                            "ParallelHeadRange: transient-fallback") == 1 &&
                            countDebug(capture,
                            "ParallelHeadRange: rejected=status-503") == 1 &&
                            countDebug(capture,
                            "ParallelHeadRange: published") == 0 && !opened,
                        "range-503-exhaust did not fail closed after two retries: " +
                            parallelDebugSummary(capture));
                require(!coordinatorStatsJson.empty(),
                        "range-503-exhaust omitted its legacy compatibility "
                        "statistics; v6 terminal proof is exercised only by "
                        "the attributed HEAD-first matrix");
            }
            if (std::string(prefetchCase.variant) == "range-404")
            {
                require(headCount == 1 && ranges.size() == 1 &&
                            ranges[0]->status == 404 &&
                            ranges[0]->sessionId == head->sessionId &&
                            countDebug(capture,
                            "ParallelHeadRange: transient-retry") == 0 &&
                            countDebug(capture,
                            "ParallelHeadRange: rejected=status-404") == 1,
                        "range-404 was retried as a coordinator transient");
            }
            if (invalidRetryDelay)
            {
                require(!opened && rangeCount == 1 &&
                            countDebug(capture,
                                "ParallelHeadRange: retry-delay-rejected") == 1 &&
                            countDebug(capture,
                                "ParallelHeadRange: transient-retry range=") == 0 &&
                            countDebug(capture,
                                "ParallelHeadRange: blocked-operation-marked") == 1 &&
                            countDebug(capture,
                                "ParallelHeadRange: blocked-operation-rejected") >= 1 &&
                            countDebug(capture,
                                "ParallelHeadRange: published") == 0 &&
                            countDebug(capture,
                                "ParallelHeadRange: file-property-published") == 0,
                        caseName +
                            " did not fail closed on an invalid retry delay: " +
                            parallelDebugSummary(capture));
            }

            const bool published =
                containsDebug(capture, "ParallelHeadRange: published");
            const bool fallback =
                containsDebug(capture, "ParallelHeadRange: fallback=");
            const bool expectedPublication =
                prefetchCase.verifyOperationScope ||
                (std::string(prefetchCase.mode) == "success" &&
                 (prefetchCase.variant[0] == '\0' ||
                  prefetchCase.completionOrder[0] != '\0')) ||
                std::string(prefetchCase.mode) == "range-503" ||
                (transientOnce != transientOnceStatuses.end() &&
                 prefetchCase.blockedReason[0] == '\0' && !invalidRetryDelay);
            require(countDebug(capture, "ParallelHeadRange: published") ==
                        (expectedPublication ? 1 : 0),
                    std::string(prefetchCase.mode) +
                        " cache publication result changed: " +
                        parallelDebugSummary(capture));
            require(fallback == prefetchCase.fallback,
                    std::string(prefetchCase.mode) +
                        " runtime fallback result changed");
            if (prefetchCase.exactHeadCount >= 0)
            {
                require(headCount == prefetchCase.exactHeadCount,
                        caseName + " emitted " + std::to_string(headCount) +
                            " HEADs; expected " +
                            std::to_string(prefetchCase.exactHeadCount));
            }
            if (prefetchCase.completionOrder[0])
            {
                const bool rangeFirst = firstRange->end < head->end;
                require(rangeFirst ==
                            (std::string(prefetchCase.completionOrder) ==
                             "range-first"),
                        caseName + " completion order changed");
            }
            if (!expectedStarted)
            {
                require(head->end < firstRange->start,
                        caseName + " activated overlapping prefetch");
            }
            const bool removeFailure =
                std::string(prefetchCase.variant) == "remove-failure";
            const bool removeRetry =
                std::string(prefetchCase.variant) == "remove-retry";
            const int expectedCoordinatorAttempts = expectedStarted
                ? (prefetchCase.verifyOperationScope ? 2 : 1)
                : 0;
            require(countDebug(capture,
                        "ParallelHeadRange: detach-success=head") ==
                        expectedCoordinatorAttempts &&
                    countDebug(capture,
                        "ParallelHeadRange: detach-success=range") ==
                        (!removeFailure && !removeRetry
                             ? expectedCoordinatorAttempts : 0),
                    caseName + " detach results changed");
            require(countDebug(capture,
                        "ParallelHeadRange: cleanup-call=head") ==
                        expectedCoordinatorAttempts &&
                    countDebug(capture,
                        "ParallelHeadRange: cleanup-call=range") ==
                        expectedCoordinatorAttempts,
                    caseName + " cleanup call counts changed");
            require(countDebug(capture,
                        "ParallelHeadRange: cleanup-success=head") ==
                        expectedCoordinatorAttempts &&
                    countDebug(capture,
                        "ParallelHeadRange: cleanup-success=range") ==
                        (!removeFailure ? expectedCoordinatorAttempts : 0),
                    caseName + " cleanup success counts changed");
            if (removeFailure)
            {
                require(countDebug(capture,
                            "ParallelHeadRange: detach-failure=range "
                            "attempt=1") == 1 &&
                        countDebug(capture,
                            "ParallelHeadRange: detach-failure=range "
                            "attempt=2") == 1 &&
                        countDebug(capture,
                            "ParallelHeadRange: multi-abandoned=success") == 1 &&
                        countDebug(capture,
                            "ParallelHeadRange: cleanup-blocked=range "
                            "reason=attached") == 1 &&
                        countDebug(capture,
                            "ParallelHeadRange: file-property-publication-"
                            "blocked") == 1,
                        "persistent remove failure did not safely abandon "
                        "multi ownership");
            }
            if (removeRetry)
            {
                require(countDebug(capture,
                            "ParallelHeadRange: detach-failure=range "
                            "attempt=1") == 1 &&
                        countDebug(capture,
                            "ParallelHeadRange: detach-retry-success=range") ==
                            1 &&
                        countDebug(capture,
                            "ParallelHeadRange: multi-abandoned=success") == 0,
                        "transient remove failure did not recover by retry");
            }
            if (prefetchCase.expectedFilePropertyPublications >= 0)
            {
                require(countDebug(capture,
                            "ParallelHeadRange: file-property-published") ==
                            prefetchCase.expectedFilePropertyPublications,
                        caseName + " file-property publication count changed");
            }
            if (prefetchCase.exactRangeCount >= 0)
            {
                require(rangeCount == prefetchCase.exactRangeCount,
                        caseName + " emitted " + std::to_string(rangeCount) +
                            " exact Ranges; expected " +
                            std::to_string(prefetchCase.exactRangeCount));
            }
            else if (prefetchCase.opens)
            {
                require(rangeCount == (prefetchCase.fallback ? 2 : 1),
                        std::string(prefetchCase.mode) +
                            " emitted " + std::to_string(rangeCount) +
                            " exact Ranges; expected " +
                            std::to_string(prefetchCase.fallback ? 2 : 1));
            }
            if (std::string(prefetchCase.variant) == "head-405")
                require(getWithoutRangeCount == 1,
                        "HEAD 405 fallback did not issue one header-only GET");
            if (prefetchCase.fallbackReason[0])
                require(containsDebug(capture,
                            std::string("ParallelHeadRange: fallback=") +
                                prefetchCase.fallbackReason),
                        caseName + " fallback reason changed");
            else if (!prefetchCase.opens && prefetchCase.exactRangeCount < 0)
            {
                require(rangeCount == 1,
                        std::string(prefetchCase.mode) +
                            " retried an invalid integrity response");
            }
            if (!prefetchCase.opens && !prefetchCase.fallback &&
                std::string(prefetchCase.mode) != "range-200-body" &&
                !prefetchCase.verifyOperationScope)
            {
                require(!published, std::string(prefetchCase.mode) +
                            " published invalid prefetch bytes");
                require(evidence.maximumAttemptedBodyBytes <= 131072 &&
                            evidence.totalAttemptedBodyBytes <= 131072,
                        std::string(prefetchCase.mode) +
                            " attempted more than the bounded 131072-byte body");
            }
            if (std::string(prefetchCase.mode) == "range-200-body")
            {
                require(containsDebug(capture,
                            "ParallelHeadRange: rejected=writer-overflow "
                            "attempted=131073 accepted=131072") &&
                            evidence.maximumAttemptedBodyBytes == 131073 &&
                            evidence.totalAttemptedBodyBytes == 131073,
                        "capped writer overflow evidence changed");
            }
            std::cout << "ScienceHttp2Prefetch: mode=" << caseName
                      << " opened=" << (opened ? "true" : "false")
                      << " ranges=" << rangeCount
                      << " shared_session="
                      << (prefetchCase.requireSharedHttp2
                              ? "true" : "not-required")
                      << " overlap="
                      << (prefetchCase.requireSharedHttp2
                              ? "true" : "not-required")
                      << " max_attempted_body_bytes="
                      << evidence.maximumAttemptedBodyBytes
                      << std::endl;
        }
        require(executedCases > 0,
                "OSGSOL_TEST_PREFETCH_CASE did not name a prefetch mode");
    }

    std::vector<ScienceServerRequest> scienceServerRequests(
        const Http2Evidence& evidence)
    {
        std::vector<ScienceServerRequest> requests;
        std::set<std::string> correlations;
        for (const Http2StreamEvidence& stream : evidence.streams)
        {
            require(!stream.correlation.empty() &&
                        correlations.insert(stream.correlation).second,
                    "v6 HTTP/2 server record omitted or duplicated correlation");
            ScienceServerRequest request;
            request.correlation = stream.correlation;
            request.method = stream.method;
            request.range = stream.range;
            request.sessionId = stream.sessionId;
            request.streamId = stream.streamId;
            request.path = stream.path;
            request.status = stream.status;
            request.start = stream.start;
            request.response = stream.response;
            request.end = stream.end;
            request.attemptedBodyBytes = stream.attemptedBodyBytes;
            request.contentLength = stream.contentLength;
            request.contentRange = stream.contentRange;
            request.aborted = stream.aborted;
            request.startCount = stream.startCount;
            request.responseCount = stream.responseCount;
            request.endCount = stream.endCount;
            request.protocolErrorCount = stream.protocolErrorCount;
            request.protocolError = stream.protocolError;
            requests.push_back(request);
        }
        return requests;
    }

    std::string mutateNetworkStatsField(
        const std::string& statsJson, const std::string& method,
        const std::string& name)
    {
        picojson::value value;
        require(picojson::parse(value, statsJson).empty() &&
                    value.is<picojson::object>(),
                "network stats mutation fixture is invalid");
        picojson::object& root = value.get<picojson::object>();
        picojson::object& methods =
            root.at("methods").get<picojson::object>();
        picojson::object& methodObject =
            methods.at(method).get<picojson::object>();
        const double original = methodObject.at(name).get<double>();
        methodObject[name] = picojson::value(original + 1.0);
        return value.serialize(true);
    }

    void verifyV6HeadRecoveryContract(const std::filesystem::path& fixture,
                                      const std::filesystem::path& root)
    {
        struct HeadCase
        {
            const char* name;
            const char* mode;
            bool opens;
            int headCount;
            int rangeCount;
            int headRetries;
            int rangeRetries;
            bool publishes;
            bool successfulRange = true;
            const char* retryDelay = "0.1";
            const char* blockReason = "retry-status";
            const char* faultName = "";
            const char* faultValue = "";
            bool persistentDetach = false;
            bool retainsAttachedState = false;
        };
        const HeadCase cases[] = {
            {"v6-head-429-once", "success", true, 2, 1, 1, 0, true},
            {"v6-head-500-once", "success", true, 2, 1, 1, 0, true},
            {"v6-head-502-once", "success", true, 2, 1, 1, 0, true},
            {"v6-head-503-once", "success", true, 2, 1, 1, 0, true},
            {"v6-head-504-once", "success", true, 2, 1, 1, 0, true},
            {"v6-head-500-once-v6-head-content-length-absent", "success",
                true, 2, 1, 1, 0, true},
            {"v6-head-500-thrice", "success", true, 4, 1, 3, 0, true},
            {"v6-head-500-once-v6-range-first", "success", true, 2, 1, 1,
                0, true},
            {"v6-head-500-once-v6-head-first", "success", true, 2, 1, 1,
                0, true},
            {"v6-head-429-once-v6-range-429-once-v6-range-first",
                "range-429-once", true, 2, 2, 1, 1, true},
            {"v6-head-429-once-v6-range-429-once-v6-head-first",
                "range-429-once", true, 2, 2, 1, 1, true},
            {"v6-head-500-once-v6-range-500-once-v6-range-first",
                "range-500-once", true, 2, 2, 1, 1, true},
            {"v6-head-500-once-v6-range-500-once-v6-head-first",
                "range-500-once", true, 2, 2, 1, 1, true},
            {"v6-head-502-once-v6-range-502-once-v6-range-first",
                "range-502-once", true, 2, 2, 1, 1, true},
            {"v6-head-502-once-v6-range-502-once-v6-head-first",
                "range-502-once", true, 2, 2, 1, 1, true},
            {"v6-head-503-once-v6-range-503-once-v6-range-first",
                "range-503-once", true, 2, 2, 1, 1, true},
            {"v6-head-503-once-v6-range-503-once-v6-head-first",
                "range-503-once", true, 2, 2, 1, 1, true},
            {"v6-head-504-once-v6-range-504-once-v6-range-first",
                "range-504-once", true, 2, 2, 1, 1, true},
            {"v6-head-504-once-v6-range-504-once-v6-head-first",
                "range-504-once", true, 2, 2, 1, 1, true},
            {"v6-head-500-once-v6-range-transient-no-content-range",
                "range-500-once", true, 2, 2, 1, 1, true},
            {"v6-head-500-once-v6-range-transient-content-range",
                "range-500-once", true, 2, 2, 1, 1, true},
            {"v6-head-500-exhaust", "success", false, 4, 1, 3, 0, false},
            {"v6-head-500-then-400", "success", false, 2, 1, 1, 0, false},
            {"v6-head-500-then-404", "success", false, 2, 1, 1, 0, false},
            {"v6-head-500-then-405", "success", false, 2, 1, 1, 0, false},
            {"v6-head-500-once-v6-range-exhaust", "range-503-exhaust",
                false, 2, 4, 1, 3, false, false},
            {"v6-head-500-once-v6-delay-nan", "success",
                false, 1, 1, 0, 0, false, true, "nan", "retry-delay"},
            {"v6-head-500-once-v6-delay-huge", "success",
                false, 1, 1, 0, 0, false, true, "1e300", "retry-delay"},
            {"v6-head-500-once-v6-head-remove-fault", "success",
                true, 2, 1, 1, 0, true, true, "0.1", "detach",
                "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_REMOVE", "1"},
            {"v6-head-500-once-v6-head-remove-persistent", "success",
                false, 1, 1, 0, 0, false, true, "0.1", "detach",
                "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_REMOVE", "2", true, true},
            {"v6-head-500-once-v6-head-add-fault", "success",
                false, 1, 1, 1, 0, false, true, "0.1", "add",
                "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_ADD", "1", false, true},
            {"v6-head-500-once-v6-head-perform-fault", "success",
                false, 1, 1, 1, 0, false, true, "0.1", "perform",
                "OSGSOL_TEST_FAIL_HEAD_RETRY_CURL_PERFORM", "1", false,
                true},
            {"v6-head-500-once-v6-size-mismatch", "size-mismatch",
                false, 2, 1, 1, 0, false, true, "0.1",
                "size-mismatch"},
        };
        const char* selectedMode =
            CPLGetConfigOption("OSGSOL_TEST_PREFETCH_CASE", nullptr);
        const std::filesystem::path certificate = root / "v6-http2-cert.pem";
        const std::filesystem::path key = root / "v6-http2-key.pem";
        createSelfSignedCertificate(certificate, key);
        int executed = 0;
        for (const HeadCase& headCase : cases)
        {
            if (selectedMode && std::string(selectedMode) != headCase.name)
                continue;
            ++executed;
            if (headCase.faultName[0] && !curlFaultInterposerAvailable())
            {
                std::cout << "ScienceV6HeadRecovery: mode="
                          << headCase.name
                          << " skipped=curl-fault-interposer-unavailable"
                          << std::endl;
                continue;
            }
            const std::filesystem::path ready =
                root / (std::string(headCase.name) + ".ready");
            const std::filesystem::path log =
                root / (std::string(headCase.name) + ".jsonl");
            ServerProcess server = startHttp2Server(
                fixture, ready, log, certificate, key,
                headCase.mode, "h2");
            const int port = waitForPort(ready);
            const std::string url = "/vsicurl/https://127.0.0.1:" +
                std::to_string(port) + "/" + headCase.name +
                "/fixture.tif";
            ScopedGdalConfig config({
                {"GDAL_HTTP_UNSAFESSL", "YES"},
                {"GDAL_HTTP_VERSION", "2TLS"},
                {"GDAL_HTTP_PROXY", ""},
                {"GDAL_HTTPS_PROXY", ""},
                {"GDAL_HTTP_MAX_RETRY", "3"},
                {"GDAL_HTTP_RETRY_DELAY", headCase.retryDelay},
            });
            VSICurlClearCache();
            VSINetworkStatsReset();
            DebugCapture capture;
            bool opened = false;
            std::size_t retainedStateBefore = 0;
            std::size_t attachedStateBefore = 0;
            if (headCase.retainsAttachedState)
            {
                require(curlAttachedStateCount() != nullptr &&
                            (!headCase.persistentDetach ||
                             curlRetainedStateCount() != nullptr),
                        "attached ownership row lacks interposer state oracle");
                unsetenv("OSGSOL_TEST_CLEANUP_WHILE_ATTACHED");
                attachedStateBefore = curlAttachedStateCount()();
                if (headCase.persistentDetach)
                    retainedStateBefore = curlRetainedStateCount()();
            }
            {
                ScopedGdalErrorCapture errorCapture(capture);
                ScopedPathSpecificOption prefetch(
                    url, "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
                ScopedPathSpecificOption operation(
                    url, "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
                    nextPrefetchOperationId().c_str());
                if (headCase.faultName[0])
                    setenv(headCase.faultName, headCase.faultValue, 1);
                GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(
                    url.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                    nullptr, nullptr, nullptr));
                opened = dataset != nullptr;
                if (dataset)
                {
                    std::int8_t value = 0;
                    require(dataset->GetRasterBand(1)->RasterIO(
                                GF_Read, 0, 0, 1, 1, &value,
                                1, 1, GDT_Int8, 0, 0, nullptr) == CE_None,
                            std::string(headCase.name) +
                                " failed its bounded read");
                    GDALClose(dataset);
                }
                if (headCase.faultName[0])
                    require(std::getenv(headCase.faultName) == nullptr,
                            std::string(headCase.name) +
                                " did not consume its HEAD-retry-only fault");
                if (headCase.retainsAttachedState)
                {
                    const std::uintmax_t serverBytesBeforeProbe =
                        std::filesystem::file_size(log);
                    const int rejectedBeforeProbe = countDebug(
                        capture, "blocked-operation-rejected");
                    VSILFILE* blocked = VSIFOpenL(url.c_str(), "rb");
                    require(blocked == nullptr &&
                                std::filesystem::file_size(log) ==
                                    serverBytesBeforeProbe &&
                                countDebug(capture,
                                    "blocked-operation-rejected") ==
                                    rejectedBeforeProbe + 1 &&
                                std::getenv(
                                    "OSGSOL_TEST_CLEANUP_WHILE_ATTACHED") ==
                                    nullptr &&
                                curlAttachedStateCount()() ==
                                    attachedStateBefore + 1 &&
                                (!headCase.persistentDetach ||
                                 curlRetainedStateCount()() ==
                                    retainedStateBefore + 1),
                            "ownership fault did not retain callback/context "
                            "state, latch before reuse, or avoid attached "
                            "cleanup");
                }
            }
            const std::string statsJson = requireNetworkStatsEvidence();
            server.stop();
            const Http2Evidence evidence = verifyHttp2Log(log, true);
            const AttributedTransportProof proof =
                buildAttributedTransportProof(
                    capture, scienceServerRequests(evidence));
            HttpProof httpProof = buildHttpProof(
                capture, statsJson, AttributionMode::AttributedV6);
            if (headCase.successfulRange)
            {
                if (httpProof.attributedTransportQualified)
                    httpProof.metadataPrefetch =
                        buildMetadataPrefetchProof(capture, httpProof);
            }
            const int actualHeads = static_cast<int>(std::count_if(
                evidence.streams.begin(), evidence.streams.end(),
                [](const Http2StreamEvidence& stream)
                {
                    return stream.method == "HEAD";
                }));
            const int actualRanges = static_cast<int>(std::count_if(
                evidence.streams.begin(), evidence.streams.end(),
                [](const Http2StreamEvidence& stream)
                {
                    return stream.method == "GET" && !stream.range.empty();
                }));
            std::uint64_t serverGetBodies = 0;
            for (const Http2StreamEvidence& stream : evidence.streams)
            {
                if (stream.method == "GET")
                    checkedAdd(serverGetBodies, stream.attemptedBodyBytes,
                               "v6 server GET bodies");
            }
            const int actualRangeRetries = static_cast<int>(std::count_if(
                proof.events.begin(), proof.events.end(),
                [](const AttributedEventEvidence& event)
                {
                    return event.kind == "coordinator-retry";
                }));
            require(opened == headCase.opens &&
                        actualHeads == headCase.headCount &&
                        actualRanges == headCase.rangeCount &&
                        actualHeads == httpProof.actualHeadCount &&
                        actualRanges == httpProof.actualGetCount &&
                        httpProof.statsHeadCount == actualHeads &&
                        httpProof.statsGetOperationCount == 1 &&
                        (!headCase.publishes ||
                         httpProof.statsGetOperationCount ==
                            httpProof.coordinatorLogicalGetCount) &&
                        serverGetBodies == httpProof.actualHttpBodyBytes &&
                        static_cast<int>(proof.headRetries.size()) ==
                            headCase.headRetries &&
                        actualRangeRetries == headCase.rangeRetries &&
                        (proof.cachePublicationCount == 1) ==
                            headCase.publishes &&
                        (proof.propertyPublicationCount == 1) ==
                            headCase.publishes &&
                        proof.qualified == headCase.publishes,
                    std::string(headCase.name) +
                        " did not satisfy the HEAD-first v6 contract");
            if (!headCase.publishes)
            {
                const int ordinaryHeaderGets = static_cast<int>(std::count_if(
                    proof.completions.begin(), proof.completions.end(),
                    [](const ScienceTransportCompletion& completion)
                    {
                        return completion.scope == "ordinary-head" &&
                            completion.method == "GET";
                    }));
                const int tokenBlocks = static_cast<int>(std::count_if(
                    proof.events.begin(), proof.events.end(),
                    [](const AttributedEventEvidence& event)
                    {
                        return event.kind == "operation-blocked";
                    }));
                const int propertyBlocks = static_cast<int>(std::count_if(
                    proof.events.begin(), proof.events.end(),
                    [](const AttributedEventEvidence& event)
                    {
                        return event.kind == "property-blocked";
                    }));
                const bool expectedReasonBlocked = std::any_of(
                    proof.events.begin(), proof.events.end(),
                    [&headCase](const AttributedEventEvidence& event)
                    {
                        return (event.kind == "head-blocked" ||
                                event.kind == "range-blocked") &&
                            event.reason == headCase.blockReason;
                    });
                const auto exactBlockCount = [&](const std::string& kind)
                {
                    return static_cast<int>(std::count_if(
                        proof.events.begin(), proof.events.end(),
                        [&](const AttributedEventEvidence& event)
                        {
                            return event.kind == kind &&
                                event.reason == headCase.blockReason;
                        }));
                };
                require(proof.fallbackCount == 0 && ordinaryHeaderGets == 0 &&
                            tokenBlocks == 1 && propertyBlocks == 1 &&
                            exactBlockCount("head-blocked") +
                                exactBlockCount("range-blocked") == 1 &&
                            exactBlockCount("property-blocked") == 1 &&
                            exactBlockCount("operation-blocked") == 1 &&
                            expectedReasonBlocked &&
                            (std::string(headCase.blockReason) !=
                                    "retry-status" ||
                             containsDebug(capture, "CanRetry=0")),
                        std::string(headCase.name) +
                            " failure row lost exact block reason/admission, "
                            "token blocking, or admitted fallback/header GET");
                const auto statsRejected = [&](const std::string& mutated)
                {
                    try
                    {
                        static_cast<void>(buildHttpProof(
                            capture, mutated,
                            AttributionMode::AttributedV6));
                        return false;
                    }
                    catch (const std::exception&)
                    {
                        return true;
                    }
                };
                require(statsRejected(mutateNetworkStatsField(
                            statsJson, "HEAD", "count")) &&
                            statsRejected(mutateNetworkStatsField(
                                statsJson, "GET", "downloaded_bytes")),
                        std::string(headCase.name) +
                            " accepted mutated negative-path HEAD/body stats");
            }
            if (std::string(headCase.mode) == "range-500-once")
            {
                const auto headRetry = std::find_if(
                    capture.messages.begin(), capture.messages.end(),
                    [](const std::string& message)
                    {
                        return message.find("head-transient-retry context=") !=
                            std::string::npos;
                    });
                const auto rangeRetry = std::find_if(
                    capture.messages.begin(), capture.messages.end(),
                    [](const std::string& message)
                    {
                        return message.find(
                            "ParallelHeadRange: transient-retry context=") !=
                            std::string::npos;
                    });
                require(headRetry != capture.messages.end() &&
                            rangeRetry != capture.messages.end() &&
                            headRetry < rangeRetry,
                        std::string(headCase.name) +
                            " did not recover HEAD before Range");
            }
        }
        if (selectedMode &&
            std::string(selectedMode).rfind("v6-", 0) == 0 && executed > 0)
            require(executed == 1,
                    "OSGSOL_TEST_PREFETCH_CASE did not name one v6 HEAD case");
    }

    void verifyV6InvalidHeadSurfaces(const std::filesystem::path& fixture,
                                     const std::filesystem::path& root)
    {
        struct InvalidHeadCase
        {
            const char* name;
            const char* protocol = "h2";
            const char* faultName = "";
            const char* faultValue = "";
            const char* blockReason = "head-invalid";
            const char* serverMode = "success";
            const char* terminalKind = "head-blocked";
            int expectedHeads = 1;
            int expectedGets = 1;
            bool requireDuplicateProtocolFailure = false;
        };
        const InvalidHeadCase cases[] = {
            {"v6-head-500-once-v6-head-content-length-malformed"},
            {"v6-head-500-once-v6-head-content-range"},
            {"v6-head-500-once-v6-head-curl-error", "h2",
                "OSGSOL_TEST_CURLMSG_TRANSPORT_ON_500", "1"},
            {"v6-head-500-once-v6-redirect-invalid", "h2",
                "OSGSOL_TEST_CURLINFO_REDIRECT_COUNT_ONCE", "1"},
            {"v6-head-http1", "http1"},
            {"v6-head-500-once-v6-connection-different", "h2",
                "OSGSOL_TEST_CURLINFO_CONN_ID_ONCE", "922337203685477000"},
            {"v6-head-500-once-v6-connection-invalid", "h2",
                "OSGSOL_TEST_CURLINFO_CONN_ID_ONCE", "-1"},
            {"v6-head-500-once-v6-final-head-content-length-absent", "h2",
                "", "", "head-invalid", "success", "head-blocked", 2, 1},
            {"v6-head-500-once-v6-range-transient-content-range-malformed",
                "h2", "", "", "range-invalid", "range-500-once",
                "range-blocked"},
            {"v6-head-500-once-v6-range-transient-content-range-spoof",
                "h2", "", "", "range-invalid", "range-500-once",
                "range-blocked"},
            {"v6-head-500-once-v6-range-transient-duplicate-protocol-error",
                "h2", "", "", "range-transport", "range-500-once",
                "range-blocked", 1, 1, true},
            {"v6-head-500-once-v6-range-transient-contradictory",
                "h2", "", "", "range-invalid", "range-500-once",
                "range-blocked"},
            {"v6-head-500-once-v6-range-transient-oversized",
                "h2", "", "", "range-transport", "range-500-once",
                "range-blocked"},
            {"v6-head-500-once-v6-range-transient-unaccounted",
                "h2", "", "", "range-transport", "range-500-once",
                "range-blocked"},
        };
        const char* selectedMode =
            CPLGetConfigOption("OSGSOL_TEST_PREFETCH_CASE", nullptr);
        bool selectedInvalid = selectedMode == nullptr;
        for (const InvalidHeadCase& item : cases)
        {
            if (selectedMode && std::string(selectedMode) == item.name)
                selectedInvalid = true;
        }
        if (!selectedInvalid) return;

        const std::filesystem::path certificate =
            root / "v6-invalid-http2-cert.pem";
        const std::filesystem::path key = root / "v6-invalid-http2-key.pem";
        createSelfSignedCertificate(certificate, key);
        int matched = 0;
        for (const InvalidHeadCase& item : cases)
        {
            if (selectedMode && std::string(selectedMode) != item.name)
                continue;
            ++matched;
            if (item.faultName[0] && !curlFaultInterposerAvailable())
            {
                std::cout << "ScienceV6InvalidHead: mode=" << item.name
                          << " skipped=curl-fault-interposer-unavailable"
                          << std::endl;
                continue;
            }
            const std::filesystem::path ready =
                root / (std::string(item.name) + ".invalid.ready");
            const std::filesystem::path log =
                root / (std::string(item.name) + ".invalid.jsonl");
            ServerProcess server = startHttp2Server(
                fixture, ready, log, certificate, key, item.serverMode,
                item.protocol);
            const int port = waitForPort(ready);
            const std::string url = "/vsicurl/https://127.0.0.1:" +
                std::to_string(port) + "/" + item.name + "/fixture.tif";
            ScopedGdalConfig config({
                {"GDAL_HTTP_UNSAFESSL", "YES"},
                {"GDAL_HTTP_VERSION",
                    std::string(item.protocol) == "http1" ? "1.1" : "2TLS"},
                {"GDAL_HTTP_PROXY", ""},
                {"GDAL_HTTPS_PROXY", ""},
                {"GDAL_HTTP_MAX_RETRY", "3"},
                {"GDAL_HTTP_RETRY_DELAY", "0.1"},
            });
            VSICurlClearCache();
            VSINetworkStatsReset();
            DebugCapture capture;
            bool opened = false;
            {
                ScopedGdalErrorCapture errorCapture(capture);
                ScopedPathSpecificOption prefetch(
                    url, "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
                ScopedPathSpecificOption operation(
                    url, "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
                    nextPrefetchOperationId().c_str());
                if (item.faultName[0])
                    setenv(item.faultName, item.faultValue, 1);
                GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(
                    url.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                    nullptr, nullptr, nullptr));
                opened = dataset != nullptr;
                if (dataset) GDALClose(dataset);
                if (item.faultName[0])
                    require(std::getenv(item.faultName) == nullptr,
                            std::string(item.name) +
                                " did not consume its one-shot curl fault");
                const std::uintmax_t serverBytesBeforeProbe =
                    std::filesystem::file_size(log);
                const int rejectedBeforeProbe = countDebug(
                    capture, "blocked-operation-rejected");
                VSILFILE* blocked = VSIFOpenL(url.c_str(), "rb");
                require(blocked == nullptr,
                        std::string(item.name) +
                            " same-token probe escaped the terminal latch");
                require(std::filesystem::file_size(log) ==
                            serverBytesBeforeProbe &&
                            countDebug(capture,
                                "blocked-operation-rejected") ==
                                rejectedBeforeProbe + 1,
                        std::string(item.name) +
                            " same-token latch probe reached transport or "
                            "lost its rejection event");
            }
            const std::string statsJson = requireNetworkStatsEvidence();
            server.stop();
            const Http2Evidence evidence = readHttp2Log(log);
            const AttributedTransportProof proof =
                buildAttributedTransportProof(
                    capture, scienceServerRequests(evidence));
            const HttpProof httpProof = buildHttpProof(
                capture, statsJson, AttributionMode::AttributedV6);
            const int serverHeads = static_cast<int>(std::count_if(
                evidence.streams.begin(), evidence.streams.end(),
                [](const Http2StreamEvidence& stream)
                {
                    return stream.method == "HEAD";
                }));
            const int serverGets = static_cast<int>(std::count_if(
                evidence.streams.begin(), evidence.streams.end(),
                [](const Http2StreamEvidence& stream)
                {
                    return stream.method == "GET";
                }));
            std::uint64_t serverGetBodies = 0;
            for (const Http2StreamEvidence& stream : evidence.streams)
            {
                if (stream.method == "GET")
                    checkedAdd(serverGetBodies, stream.attemptedBodyBytes,
                               "invalid HEAD server GET bodies");
            }
            const int headerOnlyGets = static_cast<int>(std::count_if(
                capture.messages.begin(), capture.messages.end(),
                [](const std::string& message)
                {
                    return message.rfind("CURL_INFO_HEADER_OUT: GET ", 0) == 0 &&
                        message.find("\nRange:") == std::string::npos &&
                        message.find("\r\nRange:") == std::string::npos;
                }));
            const auto exactBlockCount = [&](const std::string& kind)
            {
                return static_cast<int>(std::count_if(
                    proof.events.begin(), proof.events.end(),
                    [&](const AttributedEventEvidence& event)
                    {
                        return event.kind == kind &&
                            event.reason == item.blockReason;
                }));
            };
            const bool duplicateProtocolFailureObserved =
                std::any_of(proof.completions.begin(),
                    proof.completions.end(),
                    [](const ScienceTransportCompletion& completion)
                    {
                        return completion.role == "range" &&
                            completion.method == "GET" &&
                            completion.curlCode != 0 &&
                            completion.status == 0 &&
                            completion.actualBodyBytes == 0;
                    }) &&
                std::any_of(evidence.streams.begin(), evidence.streams.end(),
                    [](const Http2StreamEvidence& stream)
                    {
                        return stream.method == "GET" &&
                            stream.protocolErrorCount == 1 &&
                            stream.protocolError ==
                                "ERR_HTTP2_HEADER_SINGLE_VALUE" &&
                            stream.status == 0 && stream.aborted;
                    });
            require(!opened && !proof.qualified &&
                        !httpProof.attributedTransportQualified &&
                        serverHeads == httpProof.actualHeadCount &&
                        serverGets == httpProof.actualGetCount &&
                        serverHeads == httpProof.statsHeadCount &&
                        httpProof.statsGetOperationCount == 1 &&
                        serverGetBodies == httpProof.actualHttpBodyBytes &&
                        serverHeads == item.expectedHeads &&
                        serverGets == item.expectedGets &&
                        exactBlockCount(item.terminalKind) == 1 &&
                        exactBlockCount(std::string(item.terminalKind) ==
                                "head-blocked"
                                ? "range-blocked" : "head-blocked") == 0 &&
                        exactBlockCount("property-blocked") == 1 &&
                        exactBlockCount("operation-blocked") == 1 &&
                        proof.headRetries.empty() &&
                        proof.cachePublicationCount == 0 &&
                        proof.propertyPublicationCount == 0 &&
                        proof.fallbackCount == 0 && headerOnlyGets == 0 &&
                        countDebug(capture,
                            "head-transient-retry context=") == 0 &&
                        countDebug(capture, "CanRetry=") == 0 &&
                        (!item.requireDuplicateProtocolFailure ||
                         duplicateProtocolFailureObserved),
                    std::string(item.name) +
                        " did not preserve the authoritative invalid HEAD "
                        "completion/block/stats/server chain");
            const std::string mutatedHeadStats = mutateNetworkStatsField(
                statsJson, "HEAD", "count");
            const std::string mutatedBodyStats = mutateNetworkStatsField(
                statsJson, "GET", "downloaded_bytes");
            const auto statsRejected = [&](const std::string& mutated)
            {
                try
                {
                    static_cast<void>(buildHttpProof(
                        capture, mutated, AttributionMode::AttributedV6));
                    return false;
                }
                catch (const std::exception&)
                {
                    return true;
                }
            };
            require(statsRejected(mutatedHeadStats) &&
                        statsRejected(mutatedBodyStats),
                    std::string(item.name) +
                        " accepted mutated HEAD/body network statistics");
        }
        if (selectedMode) require(matched == 1,
            "OSGSOL_TEST_PREFETCH_CASE did not name one invalid v6 HEAD case");
    }

    void verifyImmediateMultiRangeRetry(const std::filesystem::path& fixture,
                                        const std::filesystem::path& root)
    {
        const char* selectedPrefetch =
            CPLGetConfigOption("OSGSOL_TEST_PREFETCH_CASE", nullptr);
        const char* selectedActivation =
            CPLGetConfigOption("OSGSOL_TEST_MULTIRANGE_CASE", nullptr);
        if (selectedPrefetch &&
            std::string(selectedPrefetch).rfind("v6-", 0) == 0)
            return;
        if (selectedPrefetch != nullptr && selectedActivation == nullptr)
            return;
        const std::filesystem::path certificate =
            root / "multirange-http2-cert.pem";
        const std::filesystem::path key = root / "multirange-http2-key.pem";
        createSelfSignedCertificate(certificate, key);
        constexpr std::array<vsi_l_offset, 3> OFFSETS = {
            262144, 393216, 524288};
        constexpr std::array<size_t, 3> SIZES = {65536, 65536, 65536};
        const std::array<std::string, 3> RANGES = {
            "bytes=262144-327679", "bytes=393216-458751",
            "bytes=524288-589823"};

        std::array<std::vector<unsigned char>, 3> expected;
        std::ifstream fixtureStream(fixture, std::ios::binary);
        require(fixtureStream.good(), "failed to open the multi-range fixture");
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            expected[index].resize(SIZES[index]);
            fixtureStream.seekg(OFFSETS[index]);
            fixtureStream.read(
                reinterpret_cast<char*>(expected[index].data()),
                static_cast<std::streamsize>(expected[index].size()));
            require(fixtureStream.gcount() ==
                        static_cast<std::streamsize>(expected[index].size()),
                    "failed to read an expected multi-range interval");
        }

        struct ActivationCase
        {
            const char* name;
            bool pathSpecific;
            const char* serverMode = "multirange-500-overlap";
            bool expectsSuccess = true;
            int removeFaultCount = 0;
            bool verifyAbandonmentLatch = false;
            const char* injectedFault = "";
            int injectedFaultCount = 0;
        };
        const ActivationCase cases[] = {
            {"path", true},
            {"global-only", false},
            {"remove-transient", true, "multirange-500-overlap", true, 1},
            {"content-range-missing", true, "multirange-success", false},
            {"content-range-malformed", true, "multirange-success", false},
            {"content-range-duplicate", true, "multirange-success", false},
            {"content-range-spoof", true, "multirange-success", false},
            {"content-range-wrong-range", true, "multirange-success", false},
            {"add-fault", true, "multirange-500-overlap",
                false, 0, false, "OSGSOL_TEST_FAIL_NEXT_CURL_ADD", 1},
            {"perform-fault", true, "multirange-500-overlap",
                false, 0, false, "OSGSOL_TEST_FAIL_NEXT_CURL_PERFORM", 1},
            {"remove-persistent", true, "multirange-500-repeat",
                false, 2, true},
        };
        int executedCases = 0;
        for (const ActivationCase& activation : cases)
        {
            if (selectedActivation == nullptr &&
                std::string(activation.name) == "remove-persistent")
                continue;
            if (selectedActivation &&
                std::string(selectedActivation) != activation.name)
            {
                continue;
            }
            ++executedCases;
            if ((activation.removeFaultCount > 0 ||
                 activation.injectedFaultCount > 0) &&
                !curlFaultInterposerAvailable())
            {
                std::cout << "ScienceImmediateMultiRange: mode="
                          << activation.name
                          << " skipped=curl-fault-interposer-unavailable"
                          << std::endl;
                continue;
            }
            const std::filesystem::path ready =
                root / (std::string("multirange-") + activation.name + ".ready");
            const std::filesystem::path log =
                root / (std::string("multirange-") + activation.name + ".jsonl");
            ServerProcess server = startHttp2Server(
                fixture, ready, log, certificate, key,
                activation.serverMode, "h2");
            const int port = waitForPort(ready);
            const std::string vsiUrl = "/vsicurl/https://127.0.0.1:" +
                std::to_string(port) +
                "/multirange-" +
                activation.name +
                "/alphaearth-range-fixture.tif";
            ScopedGdalConfig config({
                {"GDAL_HTTP_UNSAFESSL", "YES"},
                {"GDAL_HTTP_VERSION", "2TLS"},
                {"GDAL_HTTP_PROXY", ""},
                {"GDAL_HTTPS_PROXY", ""},
                {"GDAL_HTTP_MULTIRANGE", "PARALLEL"},
                {"GDAL_HTTP_MULTIPLEX", "YES"},
                {"GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES"},
                {"GDAL_HTTP_MAX_RETRY", "3"},
                {"GDAL_HTTP_RETRY_DELAY", "0.1"},
                {"GDAL_HTTP_RETRY_CODES", "429,500,502,503,504"},
                {"CPL_VSIL_NETWORK_STATS_ENABLED", "YES"},
                {"CPL_CURL_VERBOSE", "YES"},
                {"CPL_CURL_VERBOSE_DATA_IN", "NO"},
                {"CPL_DEBUG", "ON"},
                {"OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY",
                    activation.pathSpecific ? "" : "YES"},
            });
            VSICurlClearCache();
            VSINetworkStatsReset();
            DebugCapture capture;
            std::array<std::vector<unsigned char>, 3> actual;
            std::array<void*, 3> buffers = {};
            for (std::size_t index = 0; index < actual.size(); ++index)
            {
                actual[index].resize(SIZES[index]);
                buffers[index] = actual[index].data();
            }
            int readResult = -1;
            int secondReadResult = -1;
            int immediateRetryCountAfterFirstRead = 0;
            bool firstReadOutputWasEmpty = false;
            std::size_t attachedStateBefore = 0;
            if (std::string(activation.name).find("remove-persistent") !=
                std::string::npos)
            {
                require(curlAttachedStateCount() != nullptr,
                        "persistent immediate row lacks physical ownership oracle");
                attachedStateBefore = curlAttachedStateCount()();
            }
            {
                ScopedGdalErrorCapture errorCapture(capture);
                std::unique_ptr<ScopedPathSpecificOption> pathOption;
                if (activation.pathSpecific)
                {
                    require(std::string(VSIGetPathSpecificOption(
                                vsiUrl.c_str(),
                                "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY",
                                "")).empty(),
                            "immediate multi-range option leaked before activation");
                    pathOption = std::make_unique<ScopedPathSpecificOption>(
                        vsiUrl, "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "YES");
                }
                VSILFILE* file = VSIFOpenL(vsiUrl.c_str(), "rb");
                require(file != nullptr, "failed to open the HTTP/2 multi-range fixture");
                if (activation.removeFaultCount > 0)
                {
                    unsetenv("OSGSOL_TEST_CLEANUP_WHILE_ATTACHED");
                    setenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE",
                        std::to_string(activation.removeFaultCount).c_str(), 1);
                }
                if (activation.injectedFaultCount > 0)
                {
                    setenv(activation.injectedFault,
                        std::to_string(
                            activation.injectedFaultCount).c_str(), 1);
                }
                readResult = VSIFReadMultiRangeL(
                    static_cast<int>(buffers.size()), buffers.data(),
                    OFFSETS.data(), SIZES.data(), file);
                firstReadOutputWasEmpty = std::all_of(actual.begin(), actual.end(),
                    [](const std::vector<unsigned char>& interval)
                    {
                        return std::all_of(interval.begin(), interval.end(),
                            [](unsigned char value) { return value == 0; });
                    });
                if (activation.verifyAbandonmentLatch)
                {
                    immediateRetryCountAfterFirstRead = countDebug(
                        capture, "ReadMultiRange: immediate-retry ");
                    for (auto& interval : actual)
                        std::fill(interval.begin(), interval.end(), 0);
                    secondReadResult = VSIFReadMultiRangeL(
                        static_cast<int>(buffers.size()), buffers.data(),
                        OFFSETS.data(), SIZES.data(), file);
                }
                require(VSIFCloseL(file) == 0,
                        "failed to close the HTTP/2 multi-range fixture");
                const bool flexibleFaultOutcome =
                    activation.injectedFaultCount > 0;
                require(flexibleFaultOutcome ||
                            (readResult == 0) == activation.expectsSuccess,
                        std::string(activation.name) +
                            " VSIFReadMultiRangeL result changed");
                if (readResult == 0)
                {
                    require(actual == expected,
                            "VSIFReadMultiRangeL returned incorrect interval bytes");
                }
                else
                {
                    require(firstReadOutputWasEmpty,
                            std::string(activation.name) +
                                " exposed partial caller output");
                }
                if (activation.removeFaultCount > 0)
                {
                    require(std::getenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE") ==
                                nullptr,
                            std::string(activation.name) +
                                " did not consume its bounded remove fault");
                    require(std::getenv(
                                "OSGSOL_TEST_CLEANUP_WHILE_ATTACHED") ==
                                nullptr,
                            std::string(activation.name) +
                                " cleaned an easy handle while still attached");
                }
                if (activation.injectedFaultCount > 0)
                {
                    require(std::getenv(activation.injectedFault) == nullptr,
                            std::string(activation.name) +
                                " did not consume its successful libcurl fault");
                    require(std::getenv(
                                "OSGSOL_TEST_CLEANUP_WHILE_ATTACHED") ==
                                nullptr,
                            std::string(activation.name) +
                                " cleaned an easy handle while attached");
                }
                pathOption.reset();
                if (activation.pathSpecific)
                {
                    require(std::string(VSIGetPathSpecificOption(
                                vsiUrl.c_str(),
                                "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY",
                                "")).empty(),
                            "immediate multi-range option survived RAII cleanup");
                }
            }
            const std::string statsJson = requireNetworkStatsEvidence();
            server.stop();
            if (activation.injectedFaultCount > 0)
            {
                require(std::filesystem::file_size(log) > 0,
                        std::string(activation.name) +
                            " did not exercise the local HTTP/2 server");
                continue;
            }
            if (std::string(activation.name).find("remove-persistent") !=
                std::string::npos)
            {
                require(countDebug(capture,
                            "ReadMultiRange: detach-failure range=") == 2 &&
                            countDebug(capture,
                            "ReadMultiRange: multi-abandoned=success") == 1 &&
                            curlAttachedStateCount()() > attachedStateBefore,
                        "persistent immediate remove failure did not isolate "
                        "the old multi physical ownership");
                if (activation.verifyAbandonmentLatch)
                {
                    require(secondReadResult == -1 &&
                                countDebug(capture,
                                    "ReadMultiRange: immediate-retry ") ==
                                    immediateRetryCountAfterFirstRead &&
                                countDebug(capture,
                                    "ReadMultiRange: handler-disabled="
                                    "abandoned fail-closed=1") == 1 &&
                                countDebug(capture,
                                    "ReadMultiRange: multi-abandoned=success") ==
                                    1,
                            "persistent abandonment reactivated the custom "
                            "immediate scheduler");
                }
                continue;
            }

            const bool legacyNegativeContentRange =
                std::string(activation.name).find("content-range-") == 0;
            const Http2Evidence evidence = verifyHttp2Log(
                log, !legacyNegativeContentRange);
            if (std::string(activation.serverMode) == "multirange-success")
            {
                const int actualHeadCount = static_cast<int>(std::count_if(
                    evidence.streams.begin(), evidence.streams.end(),
                    [](const Http2StreamEvidence &stream)
                    {
                        return stream.method == "HEAD";
                    }));
                const int actualGetCount = static_cast<int>(std::count_if(
                    evidence.streams.begin(), evidence.streams.end(),
                    [](const Http2StreamEvidence &stream)
                    {
                        return stream.method == "GET";
                    }));
                const int successfulGetCount = static_cast<int>(std::count_if(
                    evidence.streams.begin(), evidence.streams.end(),
                    [](const Http2StreamEvidence &stream)
                    {
                        return stream.method == "GET" && stream.status == 206;
                    }));
                require(actualHeadCount == 1 && actualGetCount == 3 &&
                            successfulGetCount == 3 &&
                            countDebug(capture,
                                "ReadMultiRange: immediate-retry ") == 0 &&
                            countDebug(capture,
                                "ReadMultiRange: strict-content-range-rejected ") ==
                                1,
                        std::string(activation.name) +
                            " did not fail closed on strict Content-Range");
                continue;
            }
            std::map<std::string, std::vector<const Http2StreamEvidence*>> ranges;
            int headCount = 0;
            for (const Http2StreamEvidence& stream : evidence.streams)
            {
                if (stream.method == "HEAD") ++headCount;
                if (stream.method == "GET") ranges[stream.range].push_back(&stream);
            }
            require(headCount == 1 && ranges.size() == RANGES.size(),
                    std::string(activation.name) +
                        " multi-range request set changed");
            for (const std::string& range : RANGES)
            {
                const std::size_t expectedCount =
                    range == RANGES[0] ? 1 : 2;
                require(ranges[range].size() == expectedCount,
                        std::string(activation.name) + " request count changed for " +
                            range);
            }
            const auto* slow = ranges[RANGES[0]].front();
            const auto* failedFirst = ranges[RANGES[1]].front();
            const auto* retryFirst = ranges[RANGES[1]].back();
            const auto* failedSecond = ranges[RANGES[2]].front();
            const auto* retrySecond = ranges[RANGES[2]].back();
            const std::uint64_t latestInitialStart = std::max(
                {slow->start, failedFirst->start, failedSecond->start});
            const std::uint64_t earliestInitialEnd = std::min(
                {slow->end, failedFirst->end, failedSecond->end});
            require(slow->sessionId == failedFirst->sessionId &&
                        slow->sessionId == retryFirst->sessionId &&
                        slow->sessionId == failedSecond->sessionId &&
                        slow->sessionId == retrySecond->sessionId &&
                        latestInitialStart < earliestInitialEnd &&
                        failedFirst->status == 500 &&
                        failedFirst->attemptedBodyBytes == 17 &&
                        failedSecond->status == 500 &&
                        failedSecond->attemptedBodyBytes == 17 &&
                        retryFirst->status == 206 &&
                        retrySecond->status == 206,
                    std::string(activation.name) +
                        " did not preserve one multiplexed HTTP/2 session");
            const std::uint64_t statsHeads =
                networkStatsUnsigned(statsJson, "HEAD", "count");
            const std::uint64_t statsGets =
                networkStatsUnsigned(statsJson, "GET", "count");
            const std::uint64_t statsDownloaded = networkStatsUnsigned(
                statsJson, "GET", "downloaded_bytes");
            require(evidence.totalAttemptedBodyBytes ==
                        3 * 65536 + 2 * 17 &&
                        evidence.totalReservedBodyBytes ==
                            3 * 65536 + 2 * 17 &&
                        statsHeads == 1 && statsGets == 1 &&
                        statsDownloaded == 3 * 65536,
                    std::string(activation.name) +
                        " multi-range bytes or network statistics did not "
                        "reconcile: heads=" + std::to_string(statsHeads) +
                        " gets=" + std::to_string(statsGets) +
                        " downloaded=" + std::to_string(statsDownloaded) +
                        " attempted=" + std::to_string(
                            evidence.totalAttemptedBodyBytes));
            if (activation.pathSpecific)
            {
                require(countDebug(capture,
                            "ReadMultiRange: immediate-retry ") == 2 &&
                            retryFirst->start < slow->end &&
                            retrySecond->start < slow->end,
                        "path immediate retry events are absent or either "
                        "retry remained behind the slow sibling");
                if (std::string(activation.name) == "remove-transient")
                {
                    require(countDebug(capture,
                                "ReadMultiRange: detach-failure range=") == 1 &&
                                countDebug(capture,
                                "ReadMultiRange: detach-retry-success range=") ==
                                1 &&
                                countDebug(capture,
                                "ReadMultiRange: multi-abandoned=success") == 0,
                            "transient immediate remove failure did not "
                            "recover through its bounded second detach");
                }
            }
            else
            {
                require(countDebug(capture,
                            "ReadMultiRange: immediate-retry ") == 0 &&
                            retryFirst->start >= slow->end &&
                            retrySecond->start >= slow->end,
                        "global-only activation entered the immediate branch");
            }
        }
        require(executedCases > 0,
                "OSGSOL_TEST_MULTIRANGE_CASE did not name an activation case");
    }

    void verifyV6CombinedOperationScope(const std::filesystem::path& fixture,
                                        const std::filesystem::path& root)
    {
        const char* selected =
            CPLGetConfigOption("OSGSOL_TEST_PREFETCH_CASE", nullptr);
        static const std::string CASE_NAME =
            "v6-combined-operation-scope";
        if (selected != nullptr && selected != CASE_NAME) return;
        if (!curlFaultInterposerAvailable())
        {
            std::cout << "ScienceV6CombinedScope: skipped="
                         "curl-fault-interposer-unavailable" << std::endl;
            return;
        }

        const std::filesystem::path certificate =
            root / "v6-combined-http2-cert.pem";
        const std::filesystem::path key =
            root / "v6-combined-http2-key.pem";
        const std::filesystem::path ready =
            root / "v6-combined-http2.ready";
        const std::filesystem::path log =
            root / "v6-combined-http2.jsonl";
        createSelfSignedCertificate(certificate, key);
        ServerProcess server = startHttp2Server(
            fixture, ready, log, certificate, key, CASE_NAME, "h2");
        const int port = waitForPort(ready);
        const std::string prefix = "/vsicurl/https://127.0.0.1:" +
            std::to_string(port);
        const std::string independentUrl = prefix +
            "/v6-combined-independent/fixture.tif";
        const std::string primaryUrl = prefix +
            "/v6-combined-primary/fixture.tif";

        ScopedGdalConfig config({
            {"GDAL_HTTP_UNSAFESSL", "YES"},
            {"GDAL_HTTP_VERSION", "2TLS"},
            {"GDAL_HTTP_PROXY", ""},
            {"GDAL_HTTPS_PROXY", ""},
            {"GDAL_HTTP_MULTIRANGE", "PARALLEL"},
            {"GDAL_HTTP_MULTIPLEX", "YES"},
            {"GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES"},
            {"GDAL_HTTP_MAX_RETRY", "3"},
            {"GDAL_HTTP_RETRY_DELAY", "0.1"},
            {"GDAL_HTTP_RETRY_CODES", "429,500,502,503,504"},
            {"CPL_VSIL_NETWORK_STATS_ENABLED", "YES"},
            {"CPL_CURL_VERBOSE", "YES"},
            {"CPL_CURL_VERBOSE_DATA_IN", "NO"},
            {"CPL_DEBUG", "ON"},
        });

        DebugCapture independentCapture;
        std::string independentStats;
        {
            ScopedPathSpecificOption prefetch(
                independentUrl, "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
            ScopedPathSpecificOption operation(
                independentUrl, "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
                "v6-combined-independent-token");
            VSICurlClearCache();
            VSINetworkStatsReset();
            std::exception_ptr threadError;
            std::thread independent([&]()
            {
                try
                {
                    ScopedGdalErrorCapture errorCapture(independentCapture);
                    GDALDataset* dataset = static_cast<GDALDataset*>(
                        GDALOpenEx(independentUrl.c_str(),
                            GDAL_OF_RASTER | GDAL_OF_READONLY,
                            nullptr, nullptr, nullptr));
                    require(dataset != nullptr,
                            "independent v6 operation did not open");
                    GDALClose(dataset);
                }
                catch (...)
                {
                    threadError = std::current_exception();
                }
            });
            independent.join();
            if (threadError) std::rethrow_exception(threadError);
            independentStats = requireNetworkStatsEvidence();
        }

        DebugCapture primaryCapture;
        std::string primaryStats;
        require(curlAttachedStateCount() != nullptr,
                "combined operation lacks physical ownership oracle");
        std::size_t attachedBefore = curlAttachedStateCount()();
        {
            ScopedPathSpecificOption prefetch(
                primaryUrl, "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
            ScopedPathSpecificOption immediate(
                primaryUrl,
                "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "YES");
            ScopedPathSpecificOption operation(
                primaryUrl, "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
                "v6-combined-primary-token");
            VSICurlClearCache();
            VSINetworkStatsReset();

            std::mutex datasetMutex;
            std::condition_variable datasetCondition;
            bool openFinished = false;
            bool releaseDataset = false;
            GDALDataset* liveDataset = nullptr;
            std::exception_ptr openError;
            std::thread openThread([&]()
            {
                try
                {
                    ScopedGdalErrorCapture errorCapture(primaryCapture);
                    liveDataset = static_cast<GDALDataset*>(GDALOpenEx(
                        primaryUrl.c_str(),
                        GDAL_OF_RASTER | GDAL_OF_READONLY,
                        nullptr, nullptr, nullptr));
                    require(liveDataset != nullptr,
                            "combined HEAD-405 fallback did not open dataset");
                }
                catch (...)
                {
                    openError = std::current_exception();
                }
                {
                    std::lock_guard<std::mutex> lock(datasetMutex);
                    openFinished = true;
                }
                datasetCondition.notify_all();
                {
                    std::unique_lock<std::mutex> lock(datasetMutex);
                    datasetCondition.wait(lock,
                        [&]() { return releaseDataset; });
                }
                if (liveDataset != nullptr) GDALClose(liveDataset);
            });
            {
                std::unique_lock<std::mutex> lock(datasetMutex);
                datasetCondition.wait(lock, [&]() { return openFinished; });
            }
            if (openError)
            {
                {
                    std::lock_guard<std::mutex> lock(datasetMutex);
                    releaseDataset = true;
                }
                datasetCondition.notify_all();
                openThread.join();
                std::rethrow_exception(openError);
            }

            constexpr std::array<vsi_l_offset, 3> OFFSETS = {
                262144, 393216, 524288};
            constexpr std::array<size_t, 3> SIZES = {
                65536, 65536, 65536};
            std::array<std::vector<unsigned char>, 3> output;
            std::array<void*, 3> buffers = {};
            for (std::size_t index = 0; index < output.size(); ++index)
            {
                output[index].resize(SIZES[index]);
                buffers[index] = output[index].data();
            }
            int multiResult = 0;
            std::exception_ptr multiError;
            std::thread multiThread([&]()
            {
                try
                {
                    ScopedGdalErrorCapture errorCapture(primaryCapture);
                    VSILFILE* file = VSIFOpenL(primaryUrl.c_str(), "rb");
                    require(file != nullptr,
                            "combined operation multi-range file did not open");
                    unsetenv("OSGSOL_TEST_CLEANUP_WHILE_ATTACHED");
                    setenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE", "2", 1);
                    multiResult = VSIFReadMultiRangeL(
                        static_cast<int>(buffers.size()), buffers.data(),
                        OFFSETS.data(), SIZES.data(), file);
                    require(VSIFCloseL(file) == 0,
                            "combined operation multi-range file did not close");
                }
                catch (...)
                {
                    multiError = std::current_exception();
                }
            });
            multiThread.join();
            {
                std::lock_guard<std::mutex> lock(datasetMutex);
                releaseDataset = true;
            }
            datasetCondition.notify_all();
            openThread.join();
            if (multiError) std::rethrow_exception(multiError);
            require(multiResult == -1 &&
                        std::getenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE") ==
                            nullptr &&
                        std::getenv("OSGSOL_TEST_CLEANUP_WHILE_ATTACHED") ==
                            nullptr &&
                        curlAttachedStateCount()() == attachedBefore + 1 &&
                        countDebug(primaryCapture,
                            "ReadMultiRange: multi-abandoned=success") == 1,
                    "combined operation did not retain its persistent "
                    "multi-range ownership safely");
            primaryStats = requireNetworkStatsEvidence();
        }

        std::exception_ptr serverViolation;
        try
        {
            server.stop();
        }
        catch (...)
        {
            serverViolation = std::current_exception();
        }
        const Http2Evidence allEvidence = verifyHttp2Log(log, true);
        int independentHeads = 0;
        int independentRanges = 0;
        int primaryHeads405 = 0;
        int primaryHeaderGets = 0;
        int primaryFirstRanges = 0;
        int primaryMultiRanges = 0;
        std::uint64_t physicalPrimaryBodies = 0;
        std::uint64_t abandonedPrimaryBodies = 0;
        for (const Http2StreamEvidence& stream : allEvidence.streams)
        {
            const bool independent = stream.path.find(
                "/v6-combined-independent/") != std::string::npos;
            const bool primary = stream.path.find(
                "/v6-combined-primary/") != std::string::npos;
            require(independent || primary,
                    "combined server recorded an out-of-scope path");
            if (independent && stream.method == "HEAD") ++independentHeads;
            if (independent && stream.method == "GET" &&
                stream.range == "bytes=0-131071") ++independentRanges;
            if (primary && stream.method == "HEAD" && stream.status == 405)
                ++primaryHeads405;
            if (primary && stream.method == "GET" && stream.range.empty() &&
                stream.status == 200) ++primaryHeaderGets;
            if (primary && stream.method == "GET" &&
                stream.range == "bytes=0-131071" && stream.status == 206)
                ++primaryFirstRanges;
            if (primary && stream.method == "GET" &&
                stream.range != "bytes=0-131071" && !stream.range.empty())
            {
                ++primaryMultiRanges;
                checkedAdd(abandonedPrimaryBodies,
                           stream.attemptedBodyBytes,
                           "combined abandoned primary bodies");
            }
            if (primary && stream.method == "GET")
                checkedAdd(physicalPrimaryBodies,
                           stream.attemptedBodyBytes,
                           "combined physical primary bodies");
        }
        require(independentHeads == 1 && independentRanges == 1 &&
                    primaryHeads405 == 1 && primaryHeaderGets == 1 &&
                    primaryFirstRanges == 2 && primaryMultiRanges == 3 &&
                    networkStatsUnsigned(primaryStats, "HEAD", "count") == 1 &&
                    networkStatsUnsigned(primaryStats, "GET", "count") == 4 &&
                    networkStatsUnsigned(primaryStats, "GET",
                        "downloaded_bytes") + abandonedPrimaryBodies ==
                        physicalPrimaryBodies,
                "combined server did not observe the real HEAD-405, "
                "ordinary fallback, multi-range, and stats lifecycle: " +
                std::to_string(independentHeads) + "/" +
                std::to_string(independentRanges) + "/" +
                std::to_string(primaryHeads405) + "/" +
                std::to_string(primaryHeaderGets) + "/" +
                std::to_string(primaryFirstRanges) + "/" +
                std::to_string(primaryMultiRanges) + " stats=" +
                std::to_string(networkStatsUnsigned(
                    primaryStats, "HEAD", "count")) + "/" +
                std::to_string(networkStatsUnsigned(
                    primaryStats, "GET", "count")) + "/" +
                std::to_string(networkStatsUnsigned(
                    primaryStats, "GET", "downloaded_bytes")) +
                " physical=" + std::to_string(physicalPrimaryBodies));
        const auto abandonedLedgerAccept = [](
            const Http2Evidence& evidence)
        {
            const std::map<std::string, std::pair<int, std::uint64_t>>
                expected = {
                    {"bytes=262144-327679", {206, 65536}},
                    {"bytes=393216-458751", {500, 17}},
                    {"bytes=524288-589823", {500, 17}},
                };
            std::map<std::string, int> seen;
            for (const Http2StreamEvidence& stream : evidence.streams)
            {
                if (stream.path.find("/v6-combined-primary/") ==
                    std::string::npos)
                    continue;
                const auto item = expected.find(stream.range);
                if (item == expected.end()) continue;
                if (++seen[stream.range] != 1 ||
                    stream.status != item->second.first ||
                    stream.attemptedBodyBytes != item->second.second)
                    return false;
            }
            return seen.size() == expected.size();
        };
        require(abandonedLedgerAccept(allEvidence),
                "combined abandoned multi-range ledger changed");
        std::vector<std::size_t> abandonedIndexes;
        for (std::size_t index = 0;
             index < allEvidence.streams.size(); ++index)
        {
            const Http2StreamEvidence& stream = allEvidence.streams[index];
            if (stream.path.find("/v6-combined-primary/") !=
                    std::string::npos &&
                stream.range != "bytes=0-131071" &&
                !stream.range.empty())
                abandonedIndexes.push_back(index);
        }
        require(abandonedIndexes.size() == 3,
                "combined abandoned mutation set is not exactly three");
        for (std::size_t index : abandonedIndexes)
        {
            Http2Evidence plus = allEvidence;
            ++plus.streams[index].attemptedBodyBytes;
            Http2Evidence minus = allEvidence;
            --minus.streams[index].attemptedBodyBytes;
            Http2Evidence missing = allEvidence;
            missing.streams.erase(missing.streams.begin() +
                static_cast<std::ptrdiff_t>(index));
            require(!abandonedLedgerAccept(plus) &&
                        !abandonedLedgerAccept(minus) &&
                        !abandonedLedgerAccept(missing),
                    "combined abandoned ledger accepted +/-1 or missing "
                    "physical evidence");
        }
        if (serverViolation) std::rethrow_exception(serverViolation);
        Http2Evidence independentEvidence;
        Http2Evidence attributedPrimaryEvidence;
        std::vector<const Http2StreamEvidence*> ordinaryPrimary;
        for (const Http2StreamEvidence& stream : allEvidence.streams)
        {
            if (stream.path.find("/v6-combined-independent/") !=
                std::string::npos)
            {
                independentEvidence.streams.push_back(stream);
            }
            else if (stream.path.find("/v6-combined-primary/") !=
                         std::string::npos && stream.correlation.empty())
            {
                ordinaryPrimary.push_back(&stream);
            }
            else if (stream.path.find("/v6-combined-primary/") !=
                         std::string::npos)
            {
                attributedPrimaryEvidence.streams.push_back(stream);
            }
            else
            {
                fail("combined operation server recorded an unknown path");
            }
        }

        DebugCapture attributedPrimaryCapture;
        DebugCapture ordinaryPrimaryCapture;
        bool ordinaryResponse = false;
        bool ordinaryAwaitingDownload = false;
        for (std::size_t index = 0;
             index < primaryCapture.messages.size(); ++index)
        {
            const std::string& message = primaryCapture.messages[index];
            if (!ordinaryResponse &&
                message.rfind("CURL_INFO_HEADER_OUT: GET ", 0) == 0 &&
                message.find("Range: bytes=0-131071") !=
                    std::string::npos &&
                message.find("X-OSGSol-Science-Correlation:") ==
                    std::string::npos)
            {
                ordinaryResponse = true;
            }
            DebugCapture& destination =
                (ordinaryResponse || ordinaryAwaitingDownload)
                ? ordinaryPrimaryCapture : attributedPrimaryCapture;
            destination.messages.push_back(message);
            if (index < primaryCapture.timestamps.size())
                destination.timestamps.push_back(
                    primaryCapture.timestamps[index]);
            if (ordinaryResponse &&
                message.rfind("CURL_INFO_HEADER_IN: ", 0) == 0 &&
                message.substr(
                    std::string("CURL_INFO_HEADER_IN: ").size())
                    .find_first_not_of(" \t\r\n") == std::string::npos)
            {
                ordinaryResponse = false;
                ordinaryAwaitingDownload = true;
            }
            if (ordinaryAwaitingDownload &&
                message.find("VSICURL: Download completed") !=
                    std::string::npos)
                ordinaryAwaitingDownload = false;
        }
        require(!ordinaryResponse && !ordinaryAwaitingDownload &&
                    ordinaryPrimary.size() == 1 &&
                    ordinaryPrimary.front()->method == "GET" &&
                    ordinaryPrimary.front()->range == "bytes=0-131071" &&
                    ordinaryPrimary.front()->status == 206 &&
                    ordinaryPrimary.front()->attemptedBodyBytes == 131072 &&
                    ordinaryPrimaryCapture.messages.size() >= 4,
                "combined operation ordinary fallback ledger is incomplete");

        const AttributedTransportProof independentProof =
            buildAttributedTransportProof(
                independentCapture,
                scienceServerRequests(independentEvidence));
        const HttpProof independentHttpProof = buildHttpProof(
            independentCapture, independentStats,
            AttributionMode::AttributedV6);

        std::uint64_t attributedPrimaryBodies = 0;
        int attributedPrimaryHeads = 0;
        int attributedPrimaryGets = 0;
        for (const Http2StreamEvidence& stream :
             attributedPrimaryEvidence.streams)
        {
            if (stream.method == "HEAD") ++attributedPrimaryHeads;
            if (stream.method == "GET")
            {
                ++attributedPrimaryGets;
                checkedAdd(attributedPrimaryBodies,
                           stream.attemptedBodyBytes,
                           "combined attributed server bodies");
            }
        }
        const std::uint64_t ordinaryBody =
            ordinaryPrimary.front()->attemptedBodyBytes;
        const auto statsProjection = [](
            std::uint64_t getCount, std::uint64_t headCount,
            std::uint64_t downloadedBytes)
        {
            std::ostringstream stream;
            stream << "{\"methods\":{\"GET\":{\"count\":"
                   << getCount << ",\"downloaded_bytes\":"
                   << downloadedBytes << "},\"HEAD\":{\"count\":"
                   << headCount << "}}}";
            return stream.str();
        };
        const std::uint64_t primaryLogicalGets =
            networkStatsUnsigned(primaryStats, "GET", "count");
        const std::string attributedPrimaryStats = statsProjection(
            primaryLogicalGets,
            static_cast<std::uint64_t>(attributedPrimaryHeads),
            attributedPrimaryBodies);
        const std::string ordinaryStats = statsProjection(
            1, 0, ordinaryBody);
        const AttributedTransportProof primaryProof =
            buildAttributedTransportProof(
                attributedPrimaryCapture,
                scienceServerRequests(attributedPrimaryEvidence));
        const HttpProof primaryHttpProof = buildHttpProof(
            attributedPrimaryCapture, attributedPrimaryStats,
            AttributionMode::AttributedV6);
        const HttpProof ordinaryProof = buildHttpProof(
            ordinaryPrimaryCapture, ordinaryStats,
            AttributionMode::LegacyFrozen);

        std::set<std::string> independentContexts;
        for (const ScienceTransportCompletion& completion :
             independentProof.completions)
            independentContexts.insert(completion.context);
        std::set<std::string> primaryContexts;
        for (const ScienceTransportCompletion& completion :
             primaryProof.completions)
            primaryContexts.insert(completion.context);
        const std::string primaryContext = primaryContexts.empty()
            ? std::string() : *primaryContexts.begin();
        static const std::regex retainedPattern(
            R"(^VSICURL: ReadMultiRange: ownership-retained )"
            R"(context=([0-9a-f]{32}) request=[1-9][0-9]* )"
            R"(range=bytes=[0-9]+-[0-9]+ attached=1$)");
        const bool retainedOriginalContext = std::any_of(
            primaryCapture.messages.begin(), primaryCapture.messages.end(),
            [&](const std::string& message)
            {
                std::smatch match;
                return std::regex_match(message, match, retainedPattern) &&
                    match[1].str() == primaryContext;
            });
        const auto scopeCount = [&](const std::string& scope)
        {
            return static_cast<int>(std::count_if(
                primaryProof.completions.begin(),
                primaryProof.completions.end(),
                [&](const ScienceTransportCompletion& completion)
                {
                    return completion.scope == scope;
                }));
        };
        require(independentProof.qualified &&
                    independentHttpProof.attributedTransportQualified &&
                    independentContexts.size() == 1 &&
                    independentProof.completions.front().ordinal == 1 &&
                    independentProof.completions.front().request == 1 &&
                    primaryContexts.size() == 1 &&
                    independentContexts != primaryContexts &&
                    std::none_of(primaryProof.completions.begin(),
                        primaryProof.completions.end(),
                        [&](const ScienceTransportCompletion& completion)
                        {
                            return independentContexts.count(
                                completion.context) != 0;
                        }) &&
                    !primaryProof.qualified &&
                    primaryProof.fallbackCount == 1 &&
                    primaryProof.cachePublicationCount == 0 &&
                    primaryProof.propertyPublicationCount == 0 &&
                    scopeCount("coordinator") == 2 &&
                    scopeCount("ordinary-head") == 1 &&
                    scopeCount("multirange") == 3 &&
                    retainedOriginalContext,
                "combined live operation did not keep three scopes in one "
                "context or isolate the other path/token proof");

        int primaryHeads = 0;
        int primaryGets = 0;
        std::uint64_t primaryBodies = 0;
        for (const Http2StreamEvidence& stream : allEvidence.streams)
        {
            if (stream.path.find("/v6-combined-primary/") ==
                std::string::npos)
                continue;
            if (stream.method == "HEAD") ++primaryHeads;
            if (stream.method == "GET")
            {
                ++primaryGets;
                checkedAdd(primaryBodies,
                           stream.attemptedBodyBytes,
                           "combined primary server bodies");
            }
        }
        const auto aggregateStatsAccept = [&](const std::string& stats)
        {
            try
            {
                return networkStatsUnsigned(stats, "HEAD", "count") ==
                        static_cast<std::uint64_t>(primaryHeads) &&
                    networkStatsUnsigned(stats, "GET", "count") == 4 &&
                    networkStatsUnsigned(stats, "GET",
                        "downloaded_bytes") + abandonedPrimaryBodies ==
                        primaryBodies;
            }
            catch (const std::exception&)
            {
                return false;
            }
        };
        const auto mutateStats = [](
            const std::string& stats, const std::string& method,
            const std::string& fieldName, double delta)
        {
            picojson::value value;
            require(picojson::parse(value, stats).empty() &&
                        value.is<picojson::object>(),
                    "combined stats mutation fixture is invalid");
            picojson::object& methods =
                value.get<picojson::object>().at("methods")
                    .get<picojson::object>();
            picojson::object& fields =
                methods.at(method).get<picojson::object>();
            fields[fieldName] = picojson::value(
                fields.at(fieldName).get<double>() + delta);
            return value.serialize(true);
        };
        require(primaryHttpProof.actualHeadCount == attributedPrimaryHeads &&
                    primaryHttpProof.actualGetCount == attributedPrimaryGets &&
                    primaryHttpProof.actualHttpBodyBytes ==
                        attributedPrimaryBodies &&
                    ordinaryProof.actualHeadCount == 0 &&
                    ordinaryProof.actualGetCount == 1 &&
                    ordinaryProof.successfulGetCount == 1 &&
                    ordinaryProof.actualHttpBodyBytes == ordinaryBody &&
                    ordinaryProof.statsGetOperationCount == 1 &&
                    primaryHeads == attributedPrimaryHeads &&
                    primaryGets == attributedPrimaryGets + 1 &&
                    primaryBodies == attributedPrimaryBodies + ordinaryBody &&
                    abandonedPrimaryBodies == 65536 + 2 * 17 &&
                    aggregateStatsAccept(primaryStats) &&
                    !aggregateStatsAccept(mutateStats(
                        primaryStats, "HEAD", "count", 1)) &&
                    !aggregateStatsAccept(mutateStats(
                        primaryStats, "HEAD", "count", -1)) &&
                    !aggregateStatsAccept(mutateStats(
                        primaryStats, "GET", "count", 1)) &&
                    !aggregateStatsAccept(mutateStats(
                        primaryStats, "GET", "count", -1)) &&
                    !aggregateStatsAccept(mutateStats(
                        primaryStats, "GET", "downloaded_bytes", 1)) &&
                    !aggregateStatsAccept(mutateStats(
                        primaryStats, "GET", "downloaded_bytes", -1)),
                "combined raw/server/stats ledgers did not reconcile");
        std::cout << "ScienceV6CombinedScope: contexts=2 scopes=3 "
                     "persistent=retained" << std::endl;
    }

    LocalServerEvidence verifyLog(const std::filesystem::path& log,
                                  std::uint64_t sourceSize)
    {
        std::ifstream stream(log);
        require(stream.good(), "instrumented range log is missing");
        struct RequestRecord
        {
            bool started = false;
            bool completed = false;
            std::string method;
            std::string range;
            int responseCode = 0;
            std::uint64_t plannedBytes = 0;
            std::uint64_t actualBytes = 0;
            std::uint64_t totalCommittedBytes = 0;
            std::uint64_t totalReservedBytes = 0;
            bool partialWrite = false;
            bool violation = false;
        };
        std::map<int, RequestRecord> requests;
        std::uint64_t totalBytes = 0;
        std::uint64_t finalReservedBytes = 0;
        LocalServerEvidence evidence;
        std::string line;
        while (std::getline(stream, line))
        {
            picojson::value value;
            const std::string error = picojson::parse(value, line);
            require(error.empty() && value.is<picojson::object>(),
                    "range log line is not a JSON object");
            const picojson::object& object = value.get<picojson::object>();
            const std::string event = field(object, "event").get<std::string>();
            const int requestId = static_cast<int>(field(object, "request_id").get<double>());
            RequestRecord& request = requests[requestId];
            require(static_cast<std::uint64_t>(field(object, "source_size").get<double>()) ==
                    sourceSize, "range log source size changed");
            if (event == "request_start")
            {
                require(!request.started, "duplicate request_start event");
                request.started = true;
                request.method = field(object, "method").get<std::string>();
                const picojson::value& range = field(object, "range");
                request.range = range.is<std::string>() ? range.get<std::string>() : "";
            }
            else
            {
                require(event == "request_complete" && !request.completed,
                        "unexpected or duplicate request completion event");
                request.completed = true;
                request.responseCode =
                    static_cast<int>(field(object, "response_code").get<double>());
                request.plannedBytes = static_cast<std::uint64_t>(
                    field(object, "planned_bytes").get<double>());
                request.actualBytes = static_cast<std::uint64_t>(
                    field(object, "actual_bytes_sent").get<double>());
                request.totalCommittedBytes = static_cast<std::uint64_t>(
                    field(object, "total_committed_bytes").get<double>());
                request.totalReservedBytes = static_cast<std::uint64_t>(
                    field(object, "total_reserved_bytes").get<double>());
                finalReservedBytes = request.totalReservedBytes;
                request.partialWrite = field(object, "partial_write").get<bool>();
                request.violation = !field(object, "violation").is<picojson::null>();
            }
        }
        require(!requests.empty(), "instrumented server logged no requests");
        std::uint64_t maximumCommittedBytes = 0;
        for (const auto& item : requests)
        {
            const RequestRecord& request = item.second;
            require(request.started && request.completed,
                    "request start/completion evidence is incomplete");
            require(!request.partialWrite && !request.violation,
                    "instrumented server recorded a violation or partial write");
            totalBytes += request.actualBytes;
            maximumCommittedBytes = std::max(
                maximumCommittedBytes, request.totalCommittedBytes);
            if (request.method == "GET")
            {
                ++evidence.getCount;
                require(!request.range.empty(), "remote COG GET omitted the Range header");
                require(request.range.find(',') == std::string::npos,
                        "comma-separated multi-range syntax was emitted");
                require(request.responseCode == 206,
                        "remote COG GET did not receive HTTP 206");
                require(request.plannedBytes == request.actualBytes,
                        "server did not commit the complete planned range body");
            }
            else if (request.method == "HEAD")
            {
                ++evidence.headCount;
                require(request.responseCode == 200 && request.actualBytes == 0,
                        "local HEAD response contract changed");
            }
            require(!(request.responseCode == 200 && request.actualBytes == sourceSize),
                    "server returned the complete fixture as HTTP 200");
        }
        evidence.committedBytes = totalBytes;
        require(evidence.getCount > 0, "GDAL emitted no ranged GET requests");
        require(totalBytes <= TRANSFER_BUDGET, "range transfer exceeded the explicit budget");
        require(totalBytes < sourceSize, "range transfer equaled the complete source file");
        require(maximumCommittedBytes == totalBytes,
                "server committed-byte total does not match completed request bodies");
        require(finalReservedBytes == 0,
                "server retained reserved bytes after all requests completed");
        std::cout << "ScienceHttpRanges: requests=" << evidence.getCount
                  << " bytes=" << totalBytes << " budget=" << TRANSFER_BUDGET
                  << " source_size=" << sourceSize << std::endl;
        return evidence;
    }

    enum class RangeProfile { Baseline, Optimized, Prefetch };

    bool usesOptimizedRangeSettings(RangeProfile profile)
    {
        return profile != RangeProfile::Baseline;
    }

    struct LiveCommand
    {
        std::filesystem::path casesPath;
        int iterations = 0;
        RangeProfile profile = RangeProfile::Optimized;
        std::filesystem::path evidenceDirectory;
        std::filesystem::path summaryPath;
        std::filesystem::path completionPath;
        bool enforceLatency = false;
    };

    const std::filesystem::path V6_CANDIDATE_COMPLETION =
        "/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/"
        "science_g0_prefetch/requalification-evidence-v6/"
        "candidate-completion.json";
    const std::filesystem::path V6_FORMAL_COMPLETION =
        "/Users/USER/osgsol/.worktrees/v0.2-runtime-safety/build/"
        "science_g0_prefetch/formal-evidence-v6/live-completion.json";

    bool approvedCompletionTarget(const std::filesystem::path& path,
                                  bool targetExists, bool targetSymlink,
                                  bool tempExists, bool tempSymlink)
    {
        return path.is_absolute() &&
            (path == V6_CANDIDATE_COMPLETION || path == V6_FORMAL_COMPLETION) &&
            !targetExists && !targetSymlink && !tempExists && !tempSymlink;
    }

    void inspectCompletionEntries(const std::filesystem::path& path,
                                  bool& targetExists, bool& targetSymlink,
                                  bool& tempExists, bool& tempSymlink);

    const char* profileName(RangeProfile profile)
    {
        if (profile == RangeProfile::Prefetch) return "prefetch";
        return profile == RangeProfile::Optimized ? "optimized" : "baseline";
    }

    LiveCommand parseLiveCommand(const std::vector<std::string>& arguments)
    {
        require(arguments.size() == 12 || arguments.size() == 13,
                "live command requires cases, iterations, profile, evidence, "
                "summary, and completion paths");
        require(arguments[0] == "--live-cases" && arguments[2] == "--iterations" &&
                arguments[4] == "--profile" && arguments[6] == "--evidence-dir" &&
                arguments[8] == "--summary-json" &&
                arguments[10] == "--completion-json",
                "live command arguments are malformed or out of order");
        require(!arguments[1].empty(), "live case fixture path must not be empty");

        LiveCommand command;
        command.casesPath = arguments[1];
        const char* first = arguments[3].data();
        const char* last = first + arguments[3].size();
        const std::from_chars_result parsed =
            std::from_chars(first, last, command.iterations);
        require(parsed.ec == std::errc() && parsed.ptr == last,
                "live iteration count must be an integer");
        require(command.iterations == 1 || command.iterations == 5,
                "live evidence accepts one smoke iteration or five measured iterations");
        if (arguments[5] == "baseline")
            command.profile = RangeProfile::Baseline;
        else if (arguments[5] == "optimized")
            command.profile = RangeProfile::Optimized;
        else if (arguments[5] == "prefetch")
            command.profile = RangeProfile::Prefetch;
        else
            fail("live profile must be baseline, optimized, or prefetch");
        require(!arguments[7].empty(), "live evidence directory must not be empty");
        command.evidenceDirectory = arguments[7];
        require(!arguments[9].empty() &&
                !std::filesystem::path(arguments[9]).filename().empty(),
                "live summary path must name a file");
        command.summaryPath = arguments[9];
        command.completionPath = arguments[11];
        bool targetSymlink = false;
        bool tempSymlink = false;
        bool targetExists = false;
        bool tempExists = false;
        inspectCompletionEntries(command.completionPath,
                                 targetExists, targetSymlink,
                                 tempExists, tempSymlink);
        require(approvedCompletionTarget(
                    command.completionPath, targetExists, targetSymlink,
                    tempExists, tempSymlink),
                "completion target must be an approved absent fixed path");
        if (arguments.size() == 13)
        {
            require(arguments[12] == "--enforce-latency",
                    "unknown trailing live command argument");
            command.enforceLatency = true;
            require(command.iterations == 5,
                    "latency enforcement requires exactly five iterations");
        }
        return command;
    }

    std::string serializeLiveSummary(RangeProfile profile,
                                     const picojson::array& cases,
                                     const std::string& status)
    {
        require(status == "PASS" || status == "FAIL" || status == "ERROR",
                "live summary status is invalid");
        picojson::object limits;
        limits["median_ms"] = picojson::value(MAX_MEDIAN_MS);
        limits["p95_ms"] = picojson::value(MAX_P95_MS);
        picojson::object root;
        root["profile"] = picojson::value(profileName(profile));
        root["limits"] = picojson::value(limits);
        root["cases"] = picojson::value(cases);
        root["status"] = picojson::value(status);
        return picojson::value(root).serialize(true) + '\n';
    }

    std::string serializeLiveSummary(RangeProfile profile,
                                     const picojson::array& cases,
                                     bool passed)
    {
        return serializeLiveSummary(
            profile, cases, std::string(passed ? "PASS" : "FAIL"));
    }

    void writeSecureBuildFile(const std::filesystem::path& path,
                              const std::string& payload);

    void writeAtomicSummary(const std::filesystem::path& path,
                            const std::string& payload)
    {
        writeSecureBuildFile(path, payload);
    }

    void initializeLiveSummary(const std::filesystem::path& path,
                               RangeProfile profile)
    {
        writeAtomicSummary(path, serializeLiveSummary(
            profile, picojson::array(), std::string("ERROR")));
    }

    std::vector<std::pair<std::string, std::string>> rangeAccessConfig(
        RangeProfile profile)
    {
        return {
            {"GDAL_HTTP_MULTIRANGE", usesOptimizedRangeSettings(profile) ?
                "PARALLEL" : "YES"},
            {"GDAL_HTTP_MULTIPLEX", "YES"},
            {"GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES"},
            {"CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".tif,.tiff,.vrt"},
            {"GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR"},
            {"CPL_VSIL_CURL_USE_HEAD", "YES"},
            {"CPL_VSIL_CURL_CHUNK_SIZE",
                usesOptimizedRangeSettings(profile) ? "131072" : "16384"},
            {"CPL_VSIL_CURL_CACHE_SIZE", "16777216"},
            {"GDAL_HTTP_MAX_RETRY", "3"},
            {"GDAL_HTTP_RETRY_DELAY", "0.1"},
            {"GDAL_HTTP_RETRY_CODES", "429,500,502,503,504"},
            {"CPL_VSIL_NETWORK_STATS_ENABLED", "YES"},
            {"CPL_CURL_VERBOSE", "YES"},
            {"CPL_CURL_VERBOSE_DATA_IN", "NO"},
            {"CPL_DEBUG", "ON"},
        };
    }

    void verifyRangeProfiles()
    {
        ScopedGdalConfig baseline(rangeAccessConfig(RangeProfile::Baseline));
        require(std::string(CPLGetConfigOption("CPL_VSIL_CURL_CHUNK_SIZE", "")) ==
                    "16384", "baseline chunk changed");
        const std::string activationPath =
            "/vsicurl/https://data.example/profile-regression.tiff";
        require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")).empty(),
                "baseline profile leaked the metadata prefetch option");
        require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                    "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "")).empty(),
                "baseline profile leaked the immediate multi-range option");
        require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", "")).empty(),
                "baseline profile leaked the prefetch operation token");
        {
            ScopedGdalConfig optimized(rangeAccessConfig(RangeProfile::Optimized));
            require(std::string(CPLGetConfigOption("CPL_VSIL_CURL_CHUNK_SIZE", "")) ==
                        "131072", "optimized chunk must be 128 KiB");
            require(std::string(CPLGetConfigOption("GDAL_HTTP_MULTIRANGE", "")) ==
                        "PARALLEL", "optimized multirange must be parallel");
            require(std::string(CPLGetConfigOption("GDAL_HTTP_MULTIPLEX", "")) ==
                        "YES", "HTTP/2 multiplexing must remain enabled");
            require(std::string(CPLGetConfigOption("CPL_VSIL_CURL_USE_HEAD", "")) ==
                        "YES", "optimized profile must retain HEAD");
            require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")).empty(),
                    "optimized profile leaked the metadata prefetch option");
            require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                        "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "")).empty(),
                    "optimized profile leaked the immediate multi-range option");
            require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", "")).empty(),
                    "optimized profile leaked the prefetch operation token");
        }
        {
            ScopedGdalConfig prefetch(rangeAccessConfig(RangeProfile::Prefetch));
            require(std::string(CPLGetConfigOption("CPL_VSIL_CURL_CHUNK_SIZE", "")) ==
                        "131072", "prefetch profile must inherit the optimized chunk");
            require(std::string(CPLGetConfigOption("GDAL_HTTP_MULTIRANGE", "")) ==
                        "PARALLEL",
                    "prefetch profile must inherit optimized multirange");
            require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")).empty(),
                    "prefetch activation escaped the dataset-open scope");
            require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                        "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "")).empty(),
                    "immediate activation escaped the dataset-open scope");
            {
                ScopedPathSpecificOption activation(activationPath,
                    "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
                ScopedPathSpecificOption immediateActivation(activationPath,
                    "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "YES");
                const std::string operationId = nextPrefetchOperationId();
                ScopedPathSpecificOption operation(activationPath,
                    "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
                    operationId.c_str());
                require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                            "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")) == "YES",
                        "prefetch path option did not activate");
                require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                            "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "")) == "YES",
                        "immediate multi-range path option did not activate");
                require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                            "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", "")) ==
                            operationId,
                        "prefetch operation token did not activate");
            }
            require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")).empty(),
                    "prefetch path option survived RAII cleanup");
            require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                        "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "")).empty(),
                    "immediate path option survived RAII cleanup");
            require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", "")).empty(),
                    "prefetch operation token survived RAII cleanup");
        }
        require(std::string(CPLGetConfigOption("CPL_VSIL_CURL_CHUNK_SIZE", "")) ==
                    "16384", "nested profile did not restore baseline chunk");
        require(std::string(CPLGetConfigOption("GDAL_HTTP_MULTIRANGE", "")) ==
                    "YES", "nested profile did not restore baseline multirange");
    }

    void verifyRangeAccessConfig(RangeProfile profile)
    {
        require(std::string(CPLGetConfigOption("GDAL_HTTP_MULTIRANGE", "")) ==
                    (usesOptimizedRangeSettings(profile) ? "PARALLEL" : "YES"),
                "GDAL_HTTP_MULTIRANGE does not match the selected profile");
        require(std::string(CPLGetConfigOption("GDAL_HTTP_MULTIPLEX", "")) == "YES" &&
                std::string(CPLGetConfigOption("CPL_VSIL_CURL_USE_HEAD", "")) == "YES" &&
                std::string(CPLGetConfigOption("CPL_VSIL_CURL_ALLOWED_EXTENSIONS", "")) ==
                    ".tif,.tiff,.vrt",
                "GDAL range transport safeguards changed");
        require(std::string(CPLGetConfigOption("GDAL_HTTP_MAX_RETRY", "")) == "3" &&
                std::string(CPLGetConfigOption("GDAL_HTTP_RETRY_CODES", "")) ==
                    "429,500,502,503,504",
                "GDAL bounded retry policy changed");
    }

    std::string serializeProof(const HttpProof& proof)
    {
        std::ostringstream stream;
        stream << "{\n"
               << "  \"metadata_prefetch\": {\n"
               << "    \"enabled\": "
               << (proof.metadataPrefetch.enabled ? "true" : "false") << ",\n"
               << "    \"attributed_transport_qualified\": "
               << (proof.metadataPrefetch.attributedTransportQualified
                       ? "true" : "false") << ",\n"
               << "    \"head_request_count\": "
               << proof.metadataPrefetch.headRequestCount << ",\n"
               << "    \"range_request_count\": "
               << proof.metadataPrefetch.rangeRequestCount << ",\n"
               << "    \"range_start\": " << proof.metadataPrefetch.rangeStart
               << ",\n"
               << "    \"range_end\": " << proof.metadataPrefetch.rangeEnd << ",\n"
               << "    \"head_http_version\": "
               << proof.metadataPrefetch.headHttpVersion << ",\n"
               << "    \"range_http_version\": "
               << proof.metadataPrefetch.rangeHttpVersion << ",\n"
               << "    \"shared_connection\": "
               << (proof.metadataPrefetch.sharedConnection ? "true" : "false")
               << ",\n"
               << "    \"requests_overlapped\": "
               << (proof.metadataPrefetch.requestsOverlapped ? "true" : "false")
               << ",\n"
               << "    \"cache_published\": "
               << (proof.metadataPrefetch.cachePublished ? "true" : "false")
               << ",\n"
               << "    \"coordinator_retries\": [";
        for (std::size_t index = 0;
             index < proof.metadataPrefetch.coordinatorRetries.size(); ++index)
        {
            if (index) stream << ',';
            const CoordinatorRetryEvidence& retry =
                proof.metadataPrefetch.coordinatorRetries[index];
            stream << "{\"range\":"
                   << picojson::value(retry.range).serialize()
                   << ",\"code\":" << retry.code
                   << ",\"bytes\":" << retry.bytes
                   << ",\"attempt\":" << retry.attempt
                   << ",\"delay_ms\":" << retry.delayMs
                   << ",\"connection_id\":" << retry.connectionId
                   << ",\"http_major\":" << retry.httpMajor << '}';
        }
        stream << "],\n"
               << "    \"head_retries\": [";
        for (std::size_t index = 0; index < proof.headRetries.size(); ++index)
        {
            if (index) stream << ',';
            const HeadRetryEvidence& retry = proof.headRetries[index];
            stream << "{\"retry_ordinal\":" << retry.retryOrdinal
                   << ",\"request\":" << retry.request
                   << ",\"failed_attempt\":" << retry.failedAttempt
                   << ",\"scheduled_attempt\":" << retry.scheduledAttempt
                   << ",\"delay_ms\":" << retry.delayMs
                   << ",\"failed_status\":" << retry.failedStatus
                   << ",\"failed_connection_id\":"
                   << retry.failedConnectionId
                   << ",\"failed_http_major\":" << retry.failedHttpMajor
                   << ",\"failed_declared_content_length\":"
                   << retry.failedDeclaredContentLength
                   << ",\"failed_actual_body_bytes\":"
                   << retry.failedActualBodyBytes << '}';
        }
        stream << "],\n"
               << "    \"fallback_reason\": "
               << picojson::value(proof.metadataPrefetch.fallbackReason).serialize()
               << "\n  },\n"
               << "  \"actual_http_get_count\": " << proof.actualGetCount << ",\n"
               << "  \"successful_http_get_count\": " << proof.successfulGetCount
               << ",\n  \"transient_retry_count\": " << proof.transientRetryCount
               << ",\n  \"transient_retry_codes\": {";
        bool firstRetryCode = true;
        for (const auto& retry : proof.transientRetryCodes)
        {
            if (!firstRetryCode) stream << ',';
            stream << "\"" << retry.first << "\":" << retry.second;
            firstRetryCode = false;
        }
        stream << "},\n  \"immediate_transient_retry_count\": "
               << proof.immediateTransientRetryCount
               << ",\n  \"immediate_transient_retry_bytes\": "
               << proof.immediateTransientRetryBytes
               << ",\n  \"immediate_transient_retry_codes\": {";
        bool firstImmediateRetryCode = true;
        for (const auto& retry : proof.immediateTransientRetryCodes)
        {
            if (!firstImmediateRetryCode) stream << ',';
            stream << "\"" << retry.first << "\":" << retry.second;
            firstImmediateRetryCode = false;
        }
        stream << "},\n  \"immediate_retries\": [";
        for (std::size_t index = 0; index < proof.immediateRetries.size(); ++index)
        {
            if (index) stream << ',';
            const ImmediateRetryEvidence& retry = proof.immediateRetries[index];
            stream << "{\"range\":" << picojson::value(retry.range).serialize()
                   << ",\"code\":" << retry.code
                   << ",\"bytes\":" << retry.bytes
                   << ",\"attempt\":" << retry.attempt
                   << ",\"delay_ms\":" << retry.delayMs
                   << ",\"connection_id\":" << retry.connectionId
                   << ",\"http_major\":" << retry.httpMajor << '}';
        }
        stream << "],\n  \"coordinator_transient_retry_count\": "
               << proof.coordinatorTransientRetryCount
               << ",\n  \"coordinator_transient_retry_bytes\": "
               << proof.coordinatorTransientRetryBytes
               << ",\n  \"coordinator_transient_retry_codes\": {";
        bool firstCoordinatorRetryCode = true;
        for (const auto& retry : proof.coordinatorTransientRetryCodes)
        {
            if (!firstCoordinatorRetryCode) stream << ',';
            stream << "\"" << retry.first << "\":" << retry.second;
            firstCoordinatorRetryCode = false;
        }
        stream << "},\n  \"coordinator_transient_fallback_count\": "
               << proof.coordinatorTransientFallbackCount
               << ",\n  \"coordinator_transient_fallback_bytes\": "
               << proof.coordinatorTransientFallbackBytes
               << ",\n  \"coordinator_transient_fallback_codes\": {";
        bool firstCoordinatorCode = true;
        for (const auto& fallback : proof.coordinatorTransientFallbackCodes)
        {
            if (!firstCoordinatorCode) stream << ',';
            stream << "\"" << fallback.first << "\":" << fallback.second;
            firstCoordinatorCode = false;
        }
        stream << "},\n"
               << "  \"coordinator_head_transient_retry_count\": "
               << proof.coordinatorHeadTransientRetryCount
               << ",\n  \"coordinator_head_transient_retry_declared_bytes\": "
               << proof.coordinatorHeadTransientRetryDeclaredBytes
               << ",\n  \"coordinator_head_transient_retry_actual_body_bytes\": "
               << proof.coordinatorHeadTransientRetryActualBodyBytes
               << ",\n  \"coordinator_head_transient_retry_codes\": {";
        bool firstHeadRetryCode = true;
        for (const auto& retry : proof.coordinatorHeadTransientRetryCodes)
        {
            if (!firstHeadRetryCode) stream << ',';
            stream << "\"" << retry.first << "\":" << retry.second;
            firstHeadRetryCode = false;
        }
        stream << "},\n  \"science_transport_response_count\": "
               << proof.scienceTransportResponseCount
               << ",\n  \"science_transport_attribution\": "
               << picojson::value(
                    proof.scienceTransportAttribution).serialize()
               << ",\n  \"science_transport_qualified\": "
               << (proof.attributedTransportQualified ? "true" : "false")
               << ",\n"
               << "  \"actual_http_head_count\": " << proof.actualHeadCount << ",\n"
               << "  \"stats_get_operation_count\": " << proof.statsGetOperationCount
               << ",\n  \"stats_head_count\": " << proof.statsHeadCount
               << ",\n  \"successful_range_bytes\": " << proof.successfulRangeBytes
               << ",\n  \"declared_transient_bytes\": "
               << proof.declaredTransientBytes
               << ",\n  \"actual_http_body_bytes\": " << proof.actualHttpBodyBytes
               << ",\n  \"conservative_body_upper_bound_bytes\": "
               << proof.conservativeBodyUpperBound
               << ",\n  \"source_size\": " << proof.sourceSize;
        if (proof.transferBudget > 0)
            stream << ",\n  \"transfer_budget_bytes\": " << proof.transferBudget;
        stream << ",\n"
               << "  \"response_codes\": [";
        for (std::size_t index = 0; index < proof.responseCodes.size(); ++index)
        {
            if (index) stream << ',';
            stream << proof.responseCodes[index];
        }
        stream << "],\n  \"successful_byte_intervals\": [";
        for (std::size_t index = 0; index < proof.successfulByteIntervals.size(); ++index)
        {
            if (index) stream << ',';
            stream << '[' << proof.successfulByteIntervals[index].first << ','
                   << proof.successfulByteIntervals[index].second << ']';
        }
        stream << ']';
        if (proof.overviewFactor > 0)
        {
            stream << ",\n  \"selected_overview_factor\": " << proof.overviewFactor
                   << ",\n  \"raw_window\": {\"x\":" << proof.rawWindowX
                   << ",\"y\":" << proof.rawWindowY
                   << ",\"size\":" << proof.rawWindowSize << '}'
                   << ",\n  \"source_crs\": \"" << proof.sourceCrs << "\""
                   << ",\n  \"geotransform\": [";
            for (std::size_t index = 0; index < proof.geotransform.size(); ++index)
            {
                if (index) stream << ',';
                stream << std::setprecision(17) << proof.geotransform[index];
            }
            stream << "],\n  \"projected_point\": ["
                   << proof.projectedPoint[0] << ',' << proof.projectedPoint[1]
                   << "],\n  \"raw_pixel\": [" << proof.rawPixel[0] << ','
                   << proof.rawPixel[1] << "],\n  \"verified_wgs84_bbox\": [";
            for (std::size_t index = 0; index < proof.verifiedWgs84Bbox.size(); ++index)
            {
                if (index) stream << ',';
                stream << proof.verifiedWgs84Bbox[index];
            }
            stream << ']';
        }
        stream << "\n}\n";
        return stream.str();
    }

    class SecureDirectoryHandle
    {
    public:
        explicit SecureDirectoryHandle(int descriptor = -1) : _descriptor(descriptor) {}
        ~SecureDirectoryHandle()
        {
            if (_descriptor >= 0) close(_descriptor);
        }

        SecureDirectoryHandle(SecureDirectoryHandle&& other) noexcept
            : _descriptor(other._descriptor)
        {
            other._descriptor = -1;
        }

        SecureDirectoryHandle& operator=(SecureDirectoryHandle&& other) noexcept
        {
            if (this != &other)
            {
                if (_descriptor >= 0) close(_descriptor);
                _descriptor = other._descriptor;
                other._descriptor = -1;
            }
            return *this;
        }

        SecureDirectoryHandle(const SecureDirectoryHandle&) = delete;
        SecureDirectoryHandle& operator=(const SecureDirectoryHandle&) = delete;

        int descriptor() const
        {
            require(_descriptor >= 0, "secure directory descriptor is closed");
            return _descriptor;
        }

    private:
        int _descriptor = -1;
    };

    class SecureFileHandle
    {
    public:
        explicit SecureFileHandle(int descriptor = -1)
            : _descriptor(descriptor) {}
        ~SecureFileHandle()
        {
            if (_descriptor >= 0) close(_descriptor);
        }
        SecureFileHandle(SecureFileHandle&& other) noexcept
            : _descriptor(other._descriptor)
        {
            other._descriptor = -1;
        }
        SecureFileHandle& operator=(SecureFileHandle&& other) noexcept
        {
            if (this != &other)
            {
                if (_descriptor >= 0) close(_descriptor);
                _descriptor = other._descriptor;
                other._descriptor = -1;
            }
            return *this;
        }
        SecureFileHandle(const SecureFileHandle&) = delete;
        SecureFileHandle& operator=(const SecureFileHandle&) = delete;
        int descriptor() const
        {
            require(_descriptor >= 0, "secure file descriptor is closed");
            return _descriptor;
        }

    private:
        int _descriptor = -1;
    };

    bool isStrictDescendant(const std::filesystem::path& path,
                            const std::filesystem::path& parent)
    {
        const std::filesystem::path relative = path.lexically_relative(parent);
        if (relative.empty() || relative == "." || relative.is_absolute()) return false;
        const auto first = relative.begin();
        return first != relative.end() && *first != "..";
    }

    std::filesystem::path absoluteNormalizedPath(const std::filesystem::path& path)
    {
        require(!path.empty(), "live output path must not be empty");
        std::error_code error;
        const std::filesystem::path absolute = std::filesystem::absolute(path, error);
        require(!error, "failed to make live output path absolute: " +
                        error.message());
        return absolute.lexically_normal();
    }

    std::filesystem::path secureBuildRelativePath(const std::filesystem::path& path)
    {
        const std::filesystem::path candidate = absoluteNormalizedPath(path);
        const std::filesystem::path buildRoot =
            absoluteNormalizedPath(OSGSOL_SCIENCE_BUILD_DIR);
        const std::filesystem::path protectedDirectory =
            absoluteNormalizedPath(OSGSOL_SCIENCE_PROTECTED_EVIDENCE_DIR);
        require(candidate != protectedDirectory &&
                !isStrictDescendant(candidate, protectedDirectory),
                "live output path is the protected old evidence directory");
        require(isStrictDescendant(candidate, buildRoot),
                "live output path must be within the active build tree");
        return candidate.lexically_relative(buildRoot);
    }

    [[noreturn]] void failSystemCall(const std::string& operation, int errorNumber)
    {
        fail(operation + ": " + std::strerror(errorNumber));
    }

    SecureDirectoryHandle openSecureBuildDirectory(
        const std::filesystem::path& path, bool create)
    {
        const std::filesystem::path relative = secureBuildRelativePath(path);
        const std::filesystem::path buildRoot =
            absoluteNormalizedPath(OSGSOL_SCIENCE_BUILD_DIR);
        int descriptor = open(buildRoot.c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (descriptor < 0)
            failSystemCall("failed to open active build directory", errno);
        SecureDirectoryHandle current(descriptor);
        for (const auto& component : relative)
        {
            const std::string name = component.string();
            require(!name.empty() && name != "." && name != "..",
                    "live output path contains an unsafe component");
            int next = openat(current.descriptor(), name.c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (next < 0 && errno == ENOENT && create)
            {
                if (mkdirat(current.descriptor(), name.c_str(), 0700) != 0 &&
                    errno != EEXIST)
                {
                    failSystemCall("failed to atomically create live output directory",
                                   errno);
                }
                next = openat(current.descriptor(), name.c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            }
            if (next < 0)
                failSystemCall("failed to securely open live output directory", errno);
            current = SecureDirectoryHandle(next);
        }
        return current;
    }

    void inspectCompletionEntries(const std::filesystem::path& path,
                                  bool& targetExists, bool& targetSymlink,
                                  bool& tempExists, bool& tempSymlink)
    {
        const std::filesystem::path relative = secureBuildRelativePath(path);
        const std::filesystem::path buildRoot =
            absoluteNormalizedPath(OSGSOL_SCIENCE_BUILD_DIR);
        const int rootDescriptor = open(buildRoot.c_str(),
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (rootDescriptor < 0)
            failSystemCall("failed to open completion build root", errno);
        SecureDirectoryHandle current(rootDescriptor);
        for (const auto& component : relative.parent_path())
        {
            const std::string name = component.string();
            require(!name.empty() && name != "." && name != "..",
                    "completion path contains an unsafe component");
            const int next = openat(current.descriptor(), name.c_str(),
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (next < 0 && errno == ENOENT)
            {
                targetExists = targetSymlink = false;
                tempExists = tempSymlink = false;
                return;
            }
            if (next < 0)
                failSystemCall("failed trusted completion traversal", errno);
            current = SecureDirectoryHandle(next);
        }
        const auto inspect = [&current](const std::string& name,
                                        bool& exists, bool& symlink)
        {
            struct stat status = {};
            if (fstatat(current.descriptor(), name.c_str(), &status,
                        AT_SYMLINK_NOFOLLOW) == 0)
            {
                exists = true;
                symlink = S_ISLNK(status.st_mode);
                return;
            }
            require(errno == ENOENT,
                    "failed trusted completion entry inspection");
            exists = false;
            symlink = false;
        };
        const std::string targetName = relative.filename().string();
        inspect(targetName, targetExists, targetSymlink);
        inspect(targetName + ".tmp", tempExists, tempSymlink);
    }

    void requireSafeDestinationAt(const SecureDirectoryHandle& directory,
                                  const std::string& name)
    {
        require(!name.empty() && std::filesystem::path(name).filename() == name,
                "live output filename must be a single path component");
        struct stat status = {};
        if (fstatat(directory.descriptor(), name.c_str(), &status,
                    AT_SYMLINK_NOFOLLOW) == 0)
        {
            require(S_ISREG(status.st_mode),
                    "existing live output target must be a regular file");
            return;
        }
        require(errno == ENOENT,
                "failed to inspect live output destination: " +
                std::string(std::strerror(errno)));
    }

    void writeAtomicFileAt(const SecureDirectoryHandle& directory,
                           const std::string& name, const std::string& payload)
    {
        requireSafeDestinationAt(directory, name);
        std::string temporary;
        int descriptor = -1;
        for (int attempt = 0; attempt < 100 && descriptor < 0; ++attempt)
        {
            temporary = "." + name + ".tmp-" +
                std::to_string(static_cast<long long>(getpid())) + "-" +
                std::to_string(attempt);
            descriptor = openat(directory.descriptor(), temporary.c_str(),
                                O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                                0600);
            if (descriptor < 0 && errno != EEXIST)
                failSystemCall("failed to create temporary live output", errno);
        }
        require(descriptor >= 0, "failed to allocate a unique live output filename");
        try
        {
            std::size_t written = 0;
            while (written < payload.size())
            {
                const ssize_t result = write(descriptor, payload.data() + written,
                                             payload.size() - written);
                if (result < 0 && errno == EINTR) continue;
                if (result <= 0)
                    failSystemCall("failed to write temporary live output",
                                   result < 0 ? errno : EIO);
                written += static_cast<std::size_t>(result);
            }
            if (fsync(descriptor) != 0)
                failSystemCall("failed to sync temporary live output", errno);
            if (close(descriptor) != 0)
            {
                descriptor = -1;
                failSystemCall("failed to close temporary live output", errno);
            }
            descriptor = -1;
            if (renameat(directory.descriptor(), temporary.c_str(),
                         directory.descriptor(), name.c_str()) != 0)
                failSystemCall("failed to atomically publish live output", errno);
            temporary.clear();
            if (fsync(directory.descriptor()) != 0)
                failSystemCall("failed to sync live output directory", errno);
        }
        catch (...)
        {
            if (descriptor >= 0) close(descriptor);
            if (!temporary.empty())
                unlinkat(directory.descriptor(), temporary.c_str(), 0);
            throw;
        }
    }

    void writeSecureBuildFile(const std::filesystem::path& path,
                              const std::string& payload)
    {
        require(!path.empty() && !path.filename().empty(),
                "live output path must name a file");
        SecureDirectoryHandle directory =
            openSecureBuildDirectory(path.parent_path(), true);
        writeAtomicFileAt(directory, path.filename().string(), payload);
    }

    struct BoundFileEvidence
    {
        std::string sha256;
        std::uint64_t size = 0;
        dev_t device = 0;
        ino_t inode = 0;
        uid_t uid = 0;
        nlink_t linkCount = 0;
        mode_t mode = 0;
#if defined(__APPLE__)
        u_int flags = 0;
#endif
    };

    BoundFileEvidence hashFileDescriptorAndOptionallyFreeze(
        SecureFileHandle& file, bool freeze)
    {
        struct stat status = {};
        require(fstat(file.descriptor(), &status) == 0 &&
                    S_ISREG(status.st_mode) && status.st_uid == getuid() &&
                    status.st_nlink == 1 && status.st_size >= 0,
                "completion input is not one owned regular file");
        const dev_t device = status.st_dev;
        const ino_t inode = status.st_ino;
        if (freeze)
        {
            require((status.st_mode & 0222) != 0,
                    "completion input was already frozen");
#if defined(__APPLE__)
            require((status.st_flags & UF_IMMUTABLE) == 0,
                    "completion input already has immutable flags");
#endif
            require(fsync(file.descriptor()) == 0 &&
                        fchmod(file.descriptor(), 0400) == 0,
                    "failed to freeze completion input mode");
#if defined(__APPLE__)
            require(fchflags(file.descriptor(),
                             status.st_flags | UF_IMMUTABLE) == 0,
                    "failed to freeze completion input flags");
#endif
            require(fsync(file.descriptor()) == 0 &&
                        fstat(file.descriptor(), &status) == 0 &&
                        status.st_dev == device && status.st_ino == inode &&
                        S_ISREG(status.st_mode) && status.st_uid == getuid() &&
                        status.st_nlink == 1 && status.st_size >= 0 &&
                        (status.st_mode & 07777) == 0400,
                    "completion input metadata changed while freezing");
#if defined(__APPLE__)
            require((status.st_flags & UF_IMMUTABLE) != 0,
                    "completion input immutable flag did not persist");
#endif
        }
        require(lseek(file.descriptor(), 0, SEEK_SET) == 0,
                "failed to rewind completion input");
        CC_SHA256_CTX context;
        CC_SHA256_Init(&context);
        std::array<unsigned char, 65536> buffer = {};
        std::uint64_t consumed = 0;
        for (;;)
        {
            const ssize_t count = read(
                file.descriptor(), buffer.data(), buffer.size());
            if (count < 0 && errno == EINTR) continue;
            require(count >= 0, "failed to hash completion input");
            if (count == 0) break;
            checkedAdd(consumed, static_cast<std::uint64_t>(count),
                       "completion input bytes");
            CC_SHA256_Update(&context, buffer.data(),
                             static_cast<CC_LONG>(count));
        }
        struct stat finalStatus = {};
        require(fstat(file.descriptor(), &finalStatus) == 0 &&
                    finalStatus.st_dev == device &&
                    finalStatus.st_ino == inode &&
                    finalStatus.st_uid == getuid() &&
                    finalStatus.st_nlink == 1 &&
                    finalStatus.st_size == status.st_size &&
                    consumed == static_cast<std::uint64_t>(status.st_size),
                "completion input changed while hashing");
        std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest = {};
        CC_SHA256_Final(digest.data(), &context);
        std::ostringstream result;
        result << std::hex << std::setfill('0');
        for (unsigned char byte : digest)
            result << std::setw(2) << static_cast<int>(byte);
        BoundFileEvidence evidence;
        evidence.sha256 = result.str();
        evidence.size = consumed;
        evidence.device = finalStatus.st_dev;
        evidence.inode = finalStatus.st_ino;
        evidence.uid = finalStatus.st_uid;
        evidence.linkCount = finalStatus.st_nlink;
        evidence.mode = finalStatus.st_mode;
#if defined(__APPLE__)
        evidence.flags = finalStatus.st_flags;
#endif
        return evidence;
    }

    BoundFileEvidence hashFileAtAndOptionallyFreeze(
        const SecureDirectoryHandle& directory, const std::string& name,
        bool freeze)
    {
        require(!name.empty() && std::filesystem::path(name).filename() == name,
                "completion input filename must be one path component");
        const int descriptor = openat(directory.descriptor(), name.c_str(),
            (freeze ? O_RDWR : O_RDONLY) | O_NOFOLLOW | O_CLOEXEC);
        if (descriptor < 0)
            failSystemCall("failed to open completion input", errno);
        SecureFileHandle file(descriptor);
        return hashFileDescriptorAndOptionallyFreeze(file, freeze);
    }

    BoundFileEvidence hashBuildFileAndOptionallyFreeze(
        const std::filesystem::path& path, bool freeze)
    {
        require(!path.empty() && !path.filename().empty(),
                "completion input must name a build-tree file");
        SecureDirectoryHandle directory =
            openSecureBuildDirectory(path.parent_path(), false);
        return hashFileAtAndOptionallyFreeze(
            directory, path.filename().string(), freeze);
    }

    std::string sha256FileAndOptionallyFreeze(
        const std::filesystem::path& path, bool freeze)
    {
        return hashBuildFileAndOptionallyFreeze(path, freeze).sha256;
    }

    SecureDirectoryHandle openTrustedAbsoluteDirectory(
        const std::filesystem::path& path)
    {
        const std::filesystem::path absolute = absoluteNormalizedPath(path);
        require(absolute.is_absolute(),
                "authorized executable snapshot parent must be absolute");
        const int descriptor = open("/",
            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
        if (descriptor < 0)
            failSystemCall("failed to open filesystem root", errno);
        SecureDirectoryHandle current(descriptor);
        for (const auto& component : absolute.relative_path())
        {
            const std::string name = component.string();
            require(!name.empty() && name != "." && name != "..",
                    "authorized executable snapshot path is unsafe");
            const int next = openat(current.descriptor(), name.c_str(),
                O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
            if (next < 0)
                failSystemCall(
                    "failed trusted executable snapshot traversal", errno);
            current = SecureDirectoryHandle(next);
        }
        return current;
    }

    BoundFileEvidence hashAuthorizedExecutableSnapshot(
        const std::filesystem::path& path)
    {
        const std::filesystem::path absolute = absoluteNormalizedPath(path);
        require(path.is_absolute() && absolute.is_absolute() &&
                    !absolute.filename().empty(),
                "authorized executable snapshot must be an absolute file");
        SecureDirectoryHandle parent =
            openTrustedAbsoluteDirectory(absolute.parent_path());
        BoundFileEvidence evidence = hashFileAtAndOptionallyFreeze(
            parent, absolute.filename().string(), false);
        require((evidence.mode & 0111) != 0,
                "authorized executable snapshot is not executable");
        return evidence;
    }

    void publishImmutableNoReplaceAt(const SecureDirectoryHandle& parent,
                                     const std::string& targetName,
                                     const std::string& payload)
    {
        require(!targetName.empty() &&
                    std::filesystem::path(targetName).filename() == targetName,
                "completion publication filename is unsafe");
        const std::string temporaryName = targetName + ".tmp";
        struct stat existing = {};
        require(fstatat(parent.descriptor(), targetName.c_str(), &existing,
                        AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT,
                "completion publication target already exists");
        require(fstatat(parent.descriptor(), temporaryName.c_str(), &existing,
                        AT_SYMLINK_NOFOLLOW) != 0 && errno == ENOENT,
                "completion publication temp already exists");
        const int descriptor = openat(parent.descriptor(),
            temporaryName.c_str(),
            O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (descriptor < 0)
            failSystemCall("failed to create completion temp", errno);
        SecureFileHandle temporary(descriptor);
        bool renamed = false;
        try
        {
            std::size_t written = 0;
            while (written < payload.size())
            {
                const ssize_t count = write(
                    temporary.descriptor(), payload.data() + written,
                    payload.size() - written);
                if (count < 0 && errno == EINTR) continue;
                require(count > 0, "failed to write completion temp");
                written += static_cast<std::size_t>(count);
            }
            require(fchmod(temporary.descriptor(), 0400) == 0 &&
                        fsync(temporary.descriptor()) == 0,
                    "failed to finalize completion temp mode");
            struct stat before = {};
            require(fstat(temporary.descriptor(), &before) == 0 &&
                        S_ISREG(before.st_mode) && before.st_uid == getuid() &&
                        before.st_nlink == 1 &&
                        (before.st_mode & 07777) == 0400,
                    "completion temp metadata changed");
#if defined(__APPLE__)
            require((before.st_flags & UF_IMMUTABLE) == 0,
                    "completion temp did not begin with normal flags");
#endif
            require(lseek(temporary.descriptor(), 0, SEEK_SET) == 0,
                    "failed to rewind completion temp");
            std::string reread(payload.size(), '\0');
            std::size_t consumed = 0;
            while (consumed < reread.size())
            {
                const ssize_t count = read(temporary.descriptor(),
                    reread.data() + consumed, reread.size() - consumed);
                if (count < 0 && errno == EINTR) continue;
                require(count > 0, "completion temp ended early");
                consumed += static_cast<std::size_t>(count);
            }
            require(reread == payload,
                    "completion temp checksum/hash mismatch");
#if defined(__APPLE__)
            const int renameResult = renameatx_np(
                parent.descriptor(), temporaryName.c_str(),
                parent.descriptor(), targetName.c_str(), RENAME_EXCL);
#else
            const int renameResult = -1;
            errno = ENOTSUP;
#endif
            if (renameResult != 0)
                failSystemCall("failed no-replace completion rename", errno);
            renamed = true;
            require(fsync(parent.descriptor()) == 0,
                    "failed first completion parent fsync");
            const int targetDescriptor = openat(
                parent.descriptor(), targetName.c_str(),
                O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            require(targetDescriptor >= 0,
                    "failed to reopen completion target");
            SecureFileHandle target(targetDescriptor);
            struct stat after = {};
            require(fstat(target.descriptor(), &after) == 0 &&
                        after.st_dev == before.st_dev &&
                        after.st_ino == before.st_ino &&
                        (after.st_mode & 07777) == 0400,
                    "completion target inode/mode changed after rename");
#if defined(__APPLE__)
            require((after.st_flags & UF_IMMUTABLE) == 0 &&
                        fchflags(target.descriptor(),
                                 after.st_flags | UF_IMMUTABLE) == 0 &&
                        fsync(target.descriptor()) == 0 &&
                        fstat(target.descriptor(), &after) == 0 &&
                        (after.st_flags & UF_IMMUTABLE) != 0,
                    "failed to make completion target immutable");
#endif
            require(fsync(parent.descriptor()) == 0,
                    "failed second completion parent fsync");
            const int finalDescriptor = openat(
                parent.descriptor(), targetName.c_str(),
                O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            require(finalDescriptor >= 0,
                    "failed final nofollow reopen of completion target");
            SecureFileHandle finalTarget(finalDescriptor);
            const BoundFileEvidence finalEvidence =
                hashFileDescriptorAndOptionallyFreeze(finalTarget, false);
            require(finalEvidence.device == before.st_dev &&
                        finalEvidence.inode == before.st_ino &&
                        finalEvidence.uid == getuid() &&
                        finalEvidence.linkCount == 1 &&
                        finalEvidence.size == payload.size() &&
                        (finalEvidence.mode & 07777) == 0400 &&
                        finalEvidence.sha256 == sha256(payload),
                    "completion target changed after immutable publication");
#if defined(__APPLE__)
            require((finalEvidence.flags & UF_IMMUTABLE) != 0,
                    "completion target lost immutable flag after parent fsync");
#endif
        }
        catch (...)
        {
            if (!renamed)
                unlinkat(parent.descriptor(), temporaryName.c_str(), 0);
            throw;
        }
    }

    void publishImmutableNoReplace(const std::filesystem::path& path,
                                   const std::string& payload)
    {
        require(!path.empty() && !path.filename().empty(),
                "completion publication must name a build-tree file");
        SecureDirectoryHandle parent =
            openSecureBuildDirectory(path.parent_path(), false);
        publishImmutableNoReplaceAt(
            parent, path.filename().string(), payload);
    }

    std::string completionPayload(pid_t pid,
                                  const std::string& executableHash,
                                  const std::string& summaryHash,
                                  const std::string& manifestHash)
    {
        picojson::object root;
        root["magic"] = picojson::value(
            "osgsol.scienceearth.v6-completion.v1");
        root["pid"] = picojson::value(static_cast<double>(pid));
        root["execution_snapshot_sha256"] = picojson::value(executableHash);
        root["summary_sha256"] = picojson::value(summaryHash);
        root["actual_evidence_manifest_sha256"] =
            picojson::value(manifestHash);
        return picojson::value(root).serialize(true) + '\n';
    }

    bool validCompletionPayload(const std::string& payload)
    {
        static const std::array<const char*, 5> serializedKeys = {{
            "\"magic\"", "\"pid\"", "\"execution_snapshot_sha256\"",
            "\"summary_sha256\"", "\"actual_evidence_manifest_sha256\"",
        }};
        for (const char* key : serializedKeys)
        {
            std::size_t count = 0;
            std::size_t position = 0;
            while ((position = payload.find(key, position)) !=
                   std::string::npos)
            {
                ++count;
                position += std::strlen(key);
            }
            if (count != 1) return false;
        }
        picojson::value value;
        if (!picojson::parse(value, payload).empty() ||
            !value.is<picojson::object>())
            return false;
        const picojson::object& object = value.get<picojson::object>();
        static const std::set<std::string> keys = {
            "magic", "pid", "execution_snapshot_sha256", "summary_sha256",
            "actual_evidence_manifest_sha256"};
        std::set<std::string> actualKeys;
        for (const auto& item : object) actualKeys.insert(item.first);
        if (actualKeys != keys) return false;
        static const std::regex digest(R"(^[0-9a-f]{64}$)");
        if (!field(object, "pid").is<double>()) return false;
        const double pid = field(object, "pid").get<double>();
        return field(object, "magic").is<std::string>() &&
            field(object, "magic").get<std::string>() ==
                "osgsol.scienceearth.v6-completion.v1" &&
            std::isfinite(pid) && pid >= 1.0 && std::floor(pid) == pid &&
            pid <= static_cast<double>(
                std::numeric_limits<pid_t>::max()) &&
            field(object, "execution_snapshot_sha256").is<std::string>() &&
            field(object, "summary_sha256").is<std::string>() &&
            field(object, "actual_evidence_manifest_sha256").is<std::string>() &&
            std::regex_match(field(object,
                "execution_snapshot_sha256").get<std::string>(), digest) &&
            std::regex_match(field(object,
                "summary_sha256").get<std::string>(), digest) &&
            std::regex_match(field(object,
                "actual_evidence_manifest_sha256").get<std::string>(), digest);
    }

    void publishLiveCompletion(const std::filesystem::path& evidenceDirectory,
                               const std::filesystem::path& summaryPath,
                               const std::filesystem::path& completionPath,
                               const std::filesystem::path& executablePath,
                               const std::function<void()>&
                                   afterEvidenceDirectoryOpen = {})
    {
        const std::filesystem::path absoluteEvidenceDirectory =
            absoluteNormalizedPath(evidenceDirectory);
        const std::filesystem::path absoluteSummary =
            absoluteNormalizedPath(summaryPath);
        const std::filesystem::path absoluteCompletion =
            absoluteNormalizedPath(completionPath);
        SecureDirectoryHandle evidence =
            openSecureBuildDirectory(absoluteEvidenceDirectory, false);
        struct stat evidenceStatus = {};
        require(fstat(evidence.descriptor(), &evidenceStatus) == 0 &&
                    S_ISDIR(evidenceStatus.st_mode) &&
                    evidenceStatus.st_uid == getuid(),
                "live evidence directory is not one owned directory");
        if (afterEvidenceDirectoryOpen) afterEvidenceDirectoryOpen();
        const auto requireEvidenceDirectoryStillBound = [&]()
        {
            SecureDirectoryHandle rebound = openSecureBuildDirectory(
                absoluteEvidenceDirectory, false);
            struct stat reboundStatus = {};
            require(fstat(rebound.descriptor(), &reboundStatus) == 0 &&
                        reboundStatus.st_dev == evidenceStatus.st_dev &&
                        reboundStatus.st_ino == evidenceStatus.st_ino,
                    "live evidence directory was rebound during publication");
        };
        requireEvidenceDirectoryStillBound();

        const bool summaryInsideEvidence =
            absoluteSummary.parent_path() == absoluteEvidenceDirectory;
        const bool completionInsideEvidence =
            absoluteCompletion.parent_path() == absoluteEvidenceDirectory;
        const std::string manifestName = "actual-evidence-manifest.json";
        picojson::array entries;
        std::vector<std::string> files;
        const int enumerationDescriptor = dup(evidence.descriptor());
        require(enumerationDescriptor >= 0,
                "failed to duplicate live evidence directory descriptor");
        DIR* rawDirectory = fdopendir(enumerationDescriptor);
        if (!rawDirectory)
        {
            close(enumerationDescriptor);
            failSystemCall("failed to enumerate live evidence directory", errno);
        }
        std::unique_ptr<DIR, int (*)(DIR*)> directory(rawDirectory, closedir);
        errno = 0;
        while (dirent* entry = readdir(directory.get()))
        {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            const bool isSummary = summaryInsideEvidence &&
                name == absoluteSummary.filename().string();
            const bool isManifest = name == manifestName;
            const bool isCompletion = completionInsideEvidence &&
                name == absoluteCompletion.filename().string();
            const bool isPublicationTemp =
                name == manifestName + ".tmp" ||
                (completionInsideEvidence &&
                 name == absoluteCompletion.filename().string() + ".tmp");
            if (isSummary || isManifest || isCompletion || isPublicationTemp)
                continue;
            require(!name.empty() && name.front() != '.',
                    "evidence set contains an unaccounted temporary entry");
            files.push_back(name);
            errno = 0;
        }
        require(errno == 0, "failed while enumerating live evidence directory");
        std::sort(files.begin(), files.end());
        require(std::adjacent_find(files.begin(), files.end()) == files.end(),
                "evidence set contains duplicate names");
        for (const std::string& file : files)
        {
            const BoundFileEvidence bound =
                hashFileAtAndOptionallyFreeze(evidence, file, true);
            require(bound.size <= 9007199254740991ULL,
                    "evidence file size is not exactly representable in JSON");
            picojson::object item;
            item["name"] = picojson::value(file);
            item["size"] = picojson::value(static_cast<double>(bound.size));
            item["sha256"] = picojson::value(bound.sha256);
            entries.push_back(picojson::value(item));
        }
        const BoundFileEvidence summaryEvidence = summaryInsideEvidence
            ? hashFileAtAndOptionallyFreeze(
                  evidence, absoluteSummary.filename().string(), true)
            : hashBuildFileAndOptionallyFreeze(absoluteSummary, true);
        const BoundFileEvidence executableEvidence =
            hashAuthorizedExecutableSnapshot(executablePath);
        picojson::object manifestObject;
        manifestObject["magic"] = picojson::value(
            "osgsol.scienceearth.v6-evidence-manifest.v1");
        manifestObject["files"] = picojson::value(entries);
        manifestObject["summary_sha256"] =
            picojson::value(summaryEvidence.sha256);
        const std::string manifest =
            picojson::value(manifestObject).serialize(true) + '\n';
        requireEvidenceDirectoryStillBound();
        publishImmutableNoReplaceAt(evidence, manifestName, manifest);
        const BoundFileEvidence manifestEvidence =
            hashFileAtAndOptionallyFreeze(evidence, manifestName, false);
        const std::string completion = completionPayload(
            getpid(), executableEvidence.sha256, summaryEvidence.sha256,
            manifestEvidence.sha256);
        if (completionInsideEvidence)
            publishImmutableNoReplaceAt(
                evidence, absoluteCompletion.filename().string(), completion);
        else
            publishImmutableNoReplace(absoluteCompletion, completion);
        requireEvidenceDirectoryStillBound();
    }

    void requireImmutablePublishedFile(
        const std::filesystem::path& path, const std::string& expectedPayload)
    {
        SecureDirectoryHandle directory =
            openSecureBuildDirectory(path.parent_path(), false);
        const int descriptor = openat(directory.descriptor(),
            path.filename().c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
        require(descriptor >= 0, "immutable publication target is missing");
        SecureFileHandle file(descriptor);
        struct stat status = {};
        require(fstat(file.descriptor(), &status) == 0 &&
                    S_ISREG(status.st_mode) && status.st_nlink == 1 &&
                    (status.st_mode & 07777) == 0400,
                "immutable publication target mode/type changed");
#if defined(__APPLE__)
        require((status.st_flags & UF_IMMUTABLE) != 0,
                "immutable publication target lost UF_IMMUTABLE");
#endif
        std::string actual;
        std::array<char, 4096> buffer = {};
        for (;;)
        {
            const ssize_t count = read(
                file.descriptor(), buffer.data(), buffer.size());
            if (count < 0 && errno == EINTR) continue;
            require(count >= 0, "failed to reread immutable publication");
            if (count == 0) break;
            actual.append(buffer.data(), static_cast<std::size_t>(count));
        }
        require(actual == expectedPayload && sha256(actual) ==
                    sha256(expectedPayload),
                "immutable publication checksum/hash mismatch");
    }

    void clearImmutableTestTree(const std::filesystem::path& root)
    {
        std::error_code error;
        const std::filesystem::file_status rootStatus =
            std::filesystem::symlink_status(root, error);
        if (error == std::errc::no_such_file_or_directory) return;
        require(!error,
                "completion cleanup failed to inspect path=" +
                    root.string() + " error=" + error.message());
        if (rootStatus.type() == std::filesystem::file_type::not_found) return;
        require(std::filesystem::is_directory(rootStatus) &&
                    !std::filesystem::is_symlink(rootStatus),
                "completion cleanup root is not a real directory path=" +
                    root.string());
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::none, error);
        const std::filesystem::recursive_directory_iterator end;
        require(!error,
                "completion cleanup failed to enumerate path=" +
                    root.string() + " error=" + error.message());
        for (; iterator != end; iterator.increment(error))
        {
            require(!error,
                    "completion cleanup traversal failed path=" +
                        root.string() + " error=" + error.message());
            const std::filesystem::directory_entry& entry = *iterator;
            const std::filesystem::file_status status =
                entry.symlink_status(error);
            require(!error,
                    "completion cleanup stat failed path=" +
                        entry.path().string() + " error=" + error.message());
            if (std::filesystem::is_symlink(status)) continue;
            if (!std::filesystem::is_regular_file(status)) continue;
            const int descriptor = open(entry.path().c_str(),
                O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
            if (descriptor < 0)
                failSystemCall(
                    "completion cleanup open failed path=" +
                        entry.path().string(), errno);
            int cleanupError = 0;
            struct stat fileStatus = {};
            if (fstat(descriptor, &fileStatus) != 0)
            {
                cleanupError = errno;
            }
            else if (!S_ISREG(fileStatus.st_mode) ||
                fileStatus.st_uid != getuid() ||
                fileStatus.st_nlink != 1)
            {
                cleanupError = EPERM;
            }
#if defined(__APPLE__)
            else if (fchflags(
                         descriptor,
                         fileStatus.st_flags & ~UF_IMMUTABLE) != 0)
            {
                cleanupError = errno;
            }
#endif
            if (cleanupError == 0 && fchmod(descriptor, 0600) != 0)
                cleanupError = errno;
            if (close(descriptor) != 0 && cleanupError == 0)
                cleanupError = errno;
            if (cleanupError != 0)
                failSystemCall(
                    "completion cleanup unfreeze failed path=" +
                        entry.path().string(), cleanupError);
        }
        require(!error,
                "completion cleanup traversal ended with error path=" +
                    root.string() + " error=" + error.message());
        static_cast<void>(std::filesystem::remove_all(root, error));
        require(!error && !std::filesystem::exists(root, error) && !error,
                "completion cleanup remove failed path=" + root.string() +
                    " error=" + error.message());
    }

    class CompletionPrimitiveTreeCleanup
    {
    public:
        explicit CompletionPrimitiveTreeCleanup(
            std::filesystem::path root)
            : _root(std::move(root)) {}

        ~CompletionPrimitiveTreeCleanup() noexcept
        {
            if (_cleaned) return;
            try
            {
                clearImmutableTestTree(_root);
            }
            catch (const std::exception& error)
            {
                std::cerr << "completion primitive cleanup failed path="
                          << _root << " error=" << error.what() << std::endl;
            }
        }

        void cleanupNow()
        {
            if (_cleaned) return;
            clearImmutableTestTree(_root);
            _cleaned = true;
        }

        CompletionPrimitiveTreeCleanup(
            const CompletionPrimitiveTreeCleanup&) = delete;
        CompletionPrimitiveTreeCleanup& operator=(
            const CompletionPrimitiveTreeCleanup&) = delete;

    private:
        std::filesystem::path _root;
        bool _cleaned = false;
    };

    class CompletionPrimitiveFileCleanup
    {
    public:
        explicit CompletionPrimitiveFileCleanup(std::filesystem::path path)
            : _path(std::move(path)) {}

        ~CompletionPrimitiveFileCleanup() noexcept
        {
            if (_removed) return;
            if (unlink(_path.c_str()) != 0 && errno != ENOENT)
            {
                std::cerr << "completion primitive file cleanup failed path="
                          << _path << " error=" << std::strerror(errno)
                          << std::endl;
            }
        }

        void removeNow()
        {
            if (_removed) return;
            if (unlink(_path.c_str()) != 0 && errno != ENOENT)
                failSystemCall(
                    "completion primitive file cleanup failed path=" +
                        _path.string(), errno);
            _removed = true;
            std::error_code error;
            require(!std::filesystem::exists(_path, error) && !error,
                    "completion primitive file survived cleanup path=" +
                        _path.string() + " error=" + error.message());
        }

        CompletionPrimitiveFileCleanup(
            const CompletionPrimitiveFileCleanup&) = delete;
        CompletionPrimitiveFileCleanup& operator=(
            const CompletionPrimitiveFileCleanup&) = delete;

    private:
        std::filesystem::path _path;
        bool _removed = false;
    };

    std::set<std::string> completionPrimitiveRegressionTrees()
    {
        const std::filesystem::path build = OSGSOL_SCIENCE_BUILD_DIR;
        std::error_code error;
        std::set<std::string> trees;
        std::filesystem::directory_iterator iterator(
            build, std::filesystem::directory_options::none, error);
        const std::filesystem::directory_iterator end;
        require(!error,
                "failed to snapshot completion primitive roots path=" +
                    build.string() + " error=" + error.message());
        for (; iterator != end; iterator.increment(error))
        {
            require(!error,
                    "failed to iterate completion primitive roots path=" +
                        build.string() + " error=" + error.message());
            const std::string name = iterator->path().filename().string();
            if (name.rfind("completion-primitive-regression-", 0) == 0)
                trees.insert(name);
        }
        require(!error,
                "completion primitive root snapshot ended with error path=" +
                    build.string() + " error=" + error.message());
        return trees;
    }

    void verifyCompletionPrimitiveRegression()
    {
        const std::filesystem::path root =
            std::filesystem::path(OSGSOL_SCIENCE_BUILD_DIR) /
            ("completion-primitive-regression-" +
             std::to_string(static_cast<long long>(getpid())));
        CompletionPrimitiveTreeCleanup cleanup(root);
        clearImmutableTestTree(root);
        openSecureBuildDirectory(root, true);

        const std::filesystem::path exceptionRoot =
            root.string() + "-exception";
        bool caughtInjectedException = false;
        try
        {
            CompletionPrimitiveTreeCleanup exceptionCleanup(exceptionRoot);
            clearImmutableTestTree(exceptionRoot);
            openSecureBuildDirectory(exceptionRoot, true);
            const std::filesystem::path frozen =
                exceptionRoot / "frozen.json";
            writeSecureBuildFile(frozen, "{\"frozen\":true}\n");
            static_cast<void>(
                hashBuildFileAndOptionallyFreeze(frozen, true));
            throw std::runtime_error(
                "injected completion primitive exception");
        }
        catch (const std::runtime_error& error)
        {
            caughtInjectedException = std::string(error.what()) ==
                "injected completion primitive exception";
        }
        std::error_code exceptionCleanupError;
        require(caughtInjectedException &&
                    !std::filesystem::exists(
                        exceptionRoot, exceptionCleanupError) &&
                    !exceptionCleanupError,
                "completion primitive exception path survived RAII cleanup "
                "path=" + exceptionRoot.string() + " error=" +
                    exceptionCleanupError.message());

        const std::filesystem::path evidence = root / "evidence.json";
        const std::filesystem::path summary = root / "summary.json";
        const std::string evidencePayload = "{\"evidence\":true}\n";
        const std::string summaryPayload = "{\"status\":\"PASS\"}\n";
        writeSecureBuildFile(evidence, evidencePayload);
        writeSecureBuildFile(summary, summaryPayload);
        require(sha256FileAndOptionallyFreeze(evidence, true) ==
                    sha256(evidencePayload) &&
                    sha256FileAndOptionallyFreeze(summary, true) ==
                    sha256(summaryPayload),
                "same-FD freeze/hash result changed");

        const std::string manifestPayload =
            "{\"magic\":\"synthetic-manifest\"}\n";
        const std::filesystem::path manifest = root / "manifest.json";
        publishImmutableNoReplace(manifest, manifestPayload);
        requireImmutablePublishedFile(manifest, manifestPayload);
        const std::string completion = completionPayload(
            getpid(), std::string(64, 'a'), sha256(summaryPayload),
            sha256(manifestPayload));
        const std::filesystem::path completionPath = root / "completion.json";
        publishImmutableNoReplace(completionPath, completion);
        requireImmutablePublishedFile(completionPath, completion);

        const auto rejected = [](const auto& operation)
        {
            try
            {
                operation();
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };
        require(rejected([&]()
                {
                    requireImmutablePublishedFile(
                        completionPath, completion + "tampered");
                }),
                "immutable publication accepted checksum/hash mismatch");
        require(rejected([&]()
                {
                    publishImmutableNoReplace(completionPath, completion);
                }),
                "duplicate publish/rename collision was accepted");
        require(rejected([&]()
                {
                    writeSecureBuildFile(evidence, "mutated\n");
                }) && rejected([&]()
                {
                    writeSecureBuildFile(summary, "mutated\n");
                }),
                "post-completion evidence/summary mutation was accepted");

        const std::filesystem::path preexisting = root / "preexisting.json";
        writeSecureBuildFile(preexisting, "old\n");
        require(rejected([&]()
                {
                    publishImmutableNoReplace(preexisting, "new\n");
                }), "preexisting publication target was accepted");
        require(rejected([&]()
                {
                    requireImmutablePublishedFile(preexisting, "old\n");
                }), "normal-mode target passed immutable flag validation");

        const std::filesystem::path targetSymlink = root / "target-link.json";
        std::filesystem::create_symlink(preexisting, targetSymlink);
        require(rejected([&]()
                {
                    publishImmutableNoReplace(targetSymlink, "new\n");
                }), "symlink publication target was accepted");
        const std::filesystem::path interrupted = root / "interrupted.json";
        writeSecureBuildFile(
            interrupted.string() + ".tmp", "interrupted\n");
        require(rejected([&]()
                {
                    publishImmutableNoReplace(interrupted, "new\n");
                }), "interrupted completion temp was accepted");

        const std::filesystem::path realParent = root / "real-parent";
        openSecureBuildDirectory(realParent, true);
        const std::filesystem::path parentSymlink = root / "parent-link";
        std::filesystem::create_directory_symlink(realParent, parentSymlink);
        require(rejected([&]()
                {
                    publishImmutableNoReplace(
                        parentSymlink / "completion.json", "new\n");
                }), "symlink completion parent was accepted");

        const std::filesystem::path formalEvidence = root / "formal-evidence";
        openSecureBuildDirectory(formalEvidence, true);
        const std::filesystem::path formalCapture =
            formalEvidence / "capture.json";
        const std::filesystem::path formalSummary =
            formalEvidence / "summary.json";
        const std::filesystem::path formalManifest =
            formalEvidence / "actual-evidence-manifest.json";
        const std::filesystem::path formalCompletion =
            root / "formal-completion.json";
        writeSecureBuildFile(formalCapture, "{\"capture\":true}\n");
        writeSecureBuildFile(formalSummary, "{\"status\":\"PASS\"}\n");
        const std::filesystem::path externalSnapshot =
            std::filesystem::path("/private/tmp") /
            ("osgsol-v6-authorized-snapshot-" +
             std::to_string(static_cast<long long>(getpid())));
        if (unlink(externalSnapshot.c_str()) != 0 && errno != ENOENT)
            failSystemCall(
                "failed to clear external executable snapshot fixture", errno);
        CompletionPrimitiveFileCleanup snapshotCleanup(externalSnapshot);
        const int snapshotDescriptor = open(
            externalSnapshot.c_str(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0500);
        require(snapshotDescriptor >= 0,
                "failed to create external executable snapshot fixture");
        {
            SecureFileHandle snapshot(snapshotDescriptor);
            const std::string snapshotPayload = "#!/bin/sh\nexit 0\n";
            std::size_t written = 0;
            while (written < snapshotPayload.size())
            {
                const ssize_t count = write(
                    snapshot.descriptor(), snapshotPayload.data() + written,
                    snapshotPayload.size() - written);
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0)
                    failSystemCall(
                        "failed to write external executable snapshot fixture",
                        count < 0 ? errno : EIO);
                written += static_cast<std::size_t>(count);
            }
            if (fsync(snapshot.descriptor()) != 0)
                failSystemCall(
                    "failed to sync external executable snapshot fixture",
                    errno);
        }
        publishLiveCompletion(formalEvidence, formalSummary,
                              formalCompletion, externalSnapshot);
        const auto readFixture = [](const std::filesystem::path& path)
        {
            std::ifstream stream(path);
            require(stream.good(), "failed to read formal completion fixture");
            std::ostringstream payload;
            payload << stream.rdbuf();
            return payload.str();
        };
        const std::string formalManifestPayload = readFixture(formalManifest);
        require(formalManifestPayload.find("capture.json") != std::string::npos &&
                    formalManifestPayload.find("\"size\": 17") !=
                        std::string::npos &&
                    formalManifestPayload.find("summary.json") == std::string::npos &&
                    validCompletionPayload(readFixture(formalCompletion)),
                "formal completion layout did not keep summary/manifest/"
                "completion disjoint or hash its external executable snapshot");

        const std::filesystem::path reboundEvidence =
            root / "rebound-evidence";
        const std::filesystem::path movedEvidence =
            root / "rebound-evidence-moved";
        openSecureBuildDirectory(reboundEvidence, true);
        writeSecureBuildFile(reboundEvidence / "capture.json",
                             "{\"trusted\":true}\n");
        writeSecureBuildFile(reboundEvidence / "summary.json",
                             "{\"status\":\"PASS\"}\n");
        require(rejected([&]()
                {
                    publishLiveCompletion(
                        reboundEvidence, reboundEvidence / "summary.json",
                        root / "rebound-completion.json", externalSnapshot,
                        [&]()
                        {
                            std::filesystem::rename(
                                reboundEvidence, movedEvidence);
                            openSecureBuildDirectory(reboundEvidence, true);
                            writeSecureBuildFile(
                                reboundEvidence / "capture.json",
                                "{\"attacker\":true}\n");
                            writeSecureBuildFile(
                                reboundEvidence / "summary.json",
                                "{\"status\":\"FAKE\"}\n");
                        });
                }),
                "publishLiveCompletion accepted a rebound evidence directory");
        snapshotCleanup.removeNow();
        cleanup.cleanupNow();
    }

    void prepareLiveEvidenceDirectory(const std::filesystem::path& path)
    {
        static_cast<void>(openSecureBuildDirectory(path, true));
    }

    bool rejectedEvidenceDirectory(const std::filesystem::path& path)
    {
        try
        {
            static_cast<void>(openSecureBuildDirectory(path, false));
            return false;
        }
        catch (const std::exception&)
        {
            return true;
        }
    }

    bool rejectedLiveSummaryPath(const std::filesystem::path& path)
    {
        try
        {
            require(!path.empty() && !path.filename().empty(),
                    "live summary path must name a file");
            SecureDirectoryHandle directory =
                openSecureBuildDirectory(path.parent_path(), false);
            requireSafeDestinationAt(directory, path.filename().string());
            return false;
        }
        catch (const std::exception&)
        {
            return true;
        }
    }

    void writeRawTransportEvidence(const std::filesystem::path& directory,
                                   const std::string& name,
                                   const DebugCapture& capture,
                                   const std::string& statsJson)
    {
        SecureDirectoryHandle secureDirectory =
            openSecureBuildDirectory(directory, true);
        require(capture.messages.size() == capture.timestamps.size(),
                "raw curl/CPL evidence is missing steady-clock timestamps");
        std::ostringstream debugPayload;
        for (std::size_t index = 0; index < capture.messages.size(); ++index)
        {
            const auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                capture.timestamps[index].time_since_epoch()).count();
            debugPayload << timestamp << "ns " << capture.messages[index] << '\n';
        }
        writeAtomicFileAt(secureDirectory, name + "-curl-cpl.log",
                          debugPayload.str());
        writeAtomicFileAt(secureDirectory, name + "-network-stats.json",
                          statsJson + '\n');
    }

    void writeParsedProof(const std::filesystem::path& directory,
                          const std::string& name, const HttpProof& proof)
    {
        SecureDirectoryHandle secureDirectory =
            openSecureBuildDirectory(directory, true);
        writeAtomicFileAt(secureDirectory, name + "-proof.json",
                          serializeProof(proof));
    }

    std::string requireNetworkStatsEvidence()
    {
        char* serialized = VSINetworkStatsGetAsSerializedJSON(nullptr);
        require(serialized != nullptr, "GDAL returned no VSINetworkStats JSON");
        picojson::value value;
        const std::string error = picojson::parse(value, serialized);
        CPLFree(serialized);
        require(error.empty() && value.is<picojson::object>(),
                "GDAL VSINetworkStats output is not valid JSON");
        const picojson::object& root = value.get<picojson::object>();
        const auto methods = root.find("methods");
        require(methods != root.end() && methods->second.is<picojson::object>() &&
                methods->second.get<picojson::object>().count("GET") != 0,
                "GDAL VSINetworkStats did not record GET traffic");
        return value.serialize(true);
    }

    void CPL_STDCALL captureGdalMessage(CPLErr errorClass, CPLErrorNum,
                                        const char* message)
    {
        DebugCapture* capture = static_cast<DebugCapture*>(CPLGetErrorHandlerUserData());
        if (capture && message)
        {
            std::lock_guard<std::mutex> lock(capture->mutex);
            capture->messages.emplace_back(message);
            capture->timestamps.push_back(std::chrono::steady_clock::now());
        }
        if (errorClass >= CE_Warning && message)
            std::cerr << "GDAL: " << message << std::endl;
    }

    const picojson::object& objectValue(const picojson::value& value,
                                        const std::string& label)
    {
        require(value.is<picojson::object>(), label + " must be an object");
        return value.get<picojson::object>();
    }

    const picojson::value& field(const picojson::object& object, const std::string& name)
    {
        const auto iterator = object.find(name);
        require(iterator != object.end(), "live case is missing " + name);
        return iterator->second;
    }

    std::vector<LiveCase> loadLiveCases(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        require(stream.good(), "failed to open live case fixture");
        picojson::value root;
        const std::string error = picojson::parse(root, stream);
        require(error.empty(), "failed to parse live case fixture: " + error);
        const picojson::object& rootObject = objectValue(root, "live fixture root");
        require(static_cast<int>(field(rootObject, "schema_version").get<double>()) == 3,
                "live fixture schema must be version 3");
        require(field(rootObject, "source_index_sha256").get<std::string>() ==
                    "f738e7d274ad582e56e20a3a8b444c6f2a3ece5781f8f9855bb7ca3d9ed2942f",
                "live fixture source-index checksum changed");
        require(field(rootObject, "vrt_strategy").get<std::string>() ==
                    "synthesize_vertical_flip",
                "live fixture VRT strategy changed");
        require(field(rootObject, "dequantization").get<std::string>() ==
                    "sign(v) * pow(abs(v) / 127.5, 2)",
                "live fixture dequantization contract changed");
        const picojson::array& rgbBands =
            field(rootObject, "rgb_bands").get<picojson::array>();
        require(rgbBands.size() == 3 && rgbBands[0].get<std::string>() == "A01" &&
                rgbBands[1].get<std::string>() == "A16" &&
                rgbBands[2].get<std::string>() == "A09",
                "live fixture RGB band order changed");
        const std::string fingerprintDomain =
            field(rootObject, "record_fingerprint_domain").get<std::string>();
        require(fingerprintDomain == "osgsol.aef.raw-index-record.v1",
                "live fixture fingerprint domain changed");
        require(field(rootObject, "record_fingerprint_algorithm").get<std::string>() ==
                    "sha256" &&
                field(rootObject, "record_fingerprint_canonicalization").get<std::string>() ==
                    "json-sort-keys-compact-utf8-numeric-17g-v1",
                "live fixture fingerprint contract changed");
        const picojson::value& casesValue = field(rootObject, "cases");
        require(casesValue.is<picojson::array>(), "live cases must be an array");

        std::vector<LiveCase> cases;
        for (const picojson::value& value : casesValue.get<picojson::array>())
        {
            const picojson::object& object = objectValue(value, "live case");
            LiveCase item;
            item.name = field(object, "name").get<std::string>();
            item.datasetId = field(object, "fid").get<std::string>();
            item.rawPath = field(object, "path").get<std::string>();
            item.rawLocation = field(object, "location").get<std::string>();
            item.rawCrs = field(object, "crs").get<std::string>();
            item.utmZone = field(object, "utm_zone").get<std::string>();
            item.recordFingerprint =
                field(object, "record_fingerprint").get<std::string>();
            item.year = static_cast<int>(field(object, "year").get<double>());
            item.latitude = field(object, "latitude").get<double>();
            item.longitude = field(object, "longitude").get<double>();
            const picojson::array& bbox = field(object, "bbox").get<picojson::array>();
            for (const picojson::value& coordinate : bbox)
                item.bbox.push_back(coordinate.get<double>());
            const picojson::array& utmBbox =
                field(object, "utm_bbox").get<picojson::array>();
            for (const picojson::value& coordinate : utmBbox)
                item.utmBbox.push_back(coordinate.get<double>());
            require(item.bbox.size() == 4 && item.longitude >= item.bbox[0] &&
                    item.latitude >= item.bbox[1] && item.longitude <= item.bbox[2] &&
                    item.latitude <= item.bbox[3],
                    item.name + " point is outside its trusted tile bbox");
            require(item.utmBbox.size() == 4 && item.utmBbox[2] > item.utmBbox[0] &&
                    item.utmBbox[3] > item.utmBbox[1],
                    item.name + " pinned UTM bounds are invalid");
            const std::string s3Prefix =
                "s3://us-west-2.opendata.source.coop/tge-labs/aef/v1/annual/";
            require(item.rawPath.rfind(s3Prefix, 0) == 0,
                    item.name + " raw path is outside the pinned source prefix");
            const std::string relativePath = item.rawPath.substr(s3Prefix.size());
            require(relativePath.rfind(std::to_string(item.year) + "/", 0) == 0 &&
                    relativePath.find('?') == std::string::npos &&
                    relativePath.find('#') == std::string::npos &&
                    relativePath.size() > 5 &&
                    relativePath.substr(relativePath.size() - 5) == ".tiff",
                    item.name + " raw path/year is not canonical");
            const std::string expectedLocation =
                "VRT://vsis3/us-west-2.opendata.source.coop/tge-labs/aef/v1/annual/" +
                relativePath;
            require(item.rawLocation == expectedLocation,
                    item.name + " raw location does not match path");
            item.url = "https://data.source.coop/tge-labs/aef/v1/annual/" +
                relativePath;
            require(sha256(fingerprintDomain + std::string(1, '\0') +
                           canonicalRecord(item)) == item.recordFingerprint,
                    item.name + " raw record fingerprint mismatch");
            cases.push_back(item);
        }
        require(cases.size() == 2, "live fixture must contain NVIDIA and Hong Kong only");
        require(cases[0].name == "nvidia_hq" && cases[0].datasetId == "9790" &&
                cases[0].year == 2025,
                "NVIDIA expected tile/year changed");
        require(cases[1].name == "hong_kong" && cases[1].datasetId == "181593" &&
                cases[1].year == 2025,
                "Hong Kong expected tile/year changed");
        return cases;
    }

    std::string lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char character) { return std::tolower(character); });
        return value;
    }

    std::map<std::string, std::string> parseHeaders(std::istream& stream)
    {
        std::map<std::string, std::string> headers;
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::size_t separator = line.find(':');
            if (separator == std::string::npos) continue;
            std::string value = line.substr(separator + 1);
            while (!value.empty() && value.front() == ' ') value.erase(value.begin());
            headers[lower(line.substr(0, separator))] = value;
        }
        return headers;
    }

    void loadReplayCapture(const std::filesystem::path& path,
                           DebugCapture& capture)
    {
        std::ifstream stream(path);
        require(stream.good(), "failed to open prefetch replay trace");
        static const std::regex recordPattern(R"(^([0-9]+)ns (.*)$)");
        std::string line;
        while (std::getline(stream, line))
        {
            std::smatch match;
            if (std::regex_match(line, match, recordPattern))
            {
                capture.timestamps.emplace_back(
                    std::chrono::nanoseconds(std::stoll(match[1].str())));
                std::string message = match[2].str();
                if (message == "CURL_INFO_HEADER_IN:") message += " ";
                capture.messages.push_back(message);
                continue;
            }
            require(!capture.messages.empty(),
                    "prefetch replay trace starts with a continuation line");
            capture.messages.back() += "\n" + line;
        }
        require(!capture.messages.empty() &&
                capture.messages.size() == capture.timestamps.size(),
                "prefetch replay trace is empty or missing timestamps");
    }

    std::string loadReplayText(const std::filesystem::path& path)
    {
        std::ifstream stream(path);
        require(stream.good(), "failed to open prefetch replay statistics");
        std::ostringstream text;
        text << stream.rdbuf();
        return text.str();
    }

    void insertReplayEventBefore(DebugCapture& capture,
                                 const std::string& boundary,
                                 const std::string& event,
                                 std::chrono::nanoseconds timestamp)
    {
        const auto iterator = std::find_if(
            capture.messages.begin(), capture.messages.end(),
            [&boundary](const std::string& message)
            {
                return message.find(boundary) != std::string::npos;
            });
        require(iterator != capture.messages.end(),
                "prefetch replay event boundary is missing");
        const auto index = static_cast<std::size_t>(
            std::distance(capture.messages.begin(), iterator));
        capture.messages.insert(iterator, event);
        capture.timestamps.insert(capture.timestamps.begin() + index,
                                  std::chrono::steady_clock::time_point(timestamp));
    }

    bool parseCoordinatorRetryEvidence(const std::string& message,
                                       CoordinatorRetryEvidence& evidence)
    {
        static const std::regex coordinatorRetryPattern(
            R"(^VSICURL: ParallelHeadRange: transient-retry )"
            R"(range=(bytes=[0-9]+-[0-9]+) status=([0-9]+) bytes=([0-9]+) )"
            R"(attempt=([0-9]+) delay-ms=([0-9]+) )"
            R"(range-connection=(-?[0-9]+) range-http=([0-9]+)$)");
        std::smatch match;
        if (!std::regex_match(message, match, coordinatorRetryPattern))
            return false;
        evidence.range = match[1].str();
        evidence.code = std::stoi(match[2].str());
        evidence.bytes = std::stoull(match[3].str());
        evidence.attempt = std::stoi(match[4].str());
        evidence.delayMs = std::stoll(match[5].str());
        evidence.connectionId = std::stoll(match[6].str());
        evidence.httpMajor = std::stoi(match[7].str());
        return true;
    }

    bool parseImmediateRetryEvidence(const std::string& message,
                                     ImmediateRetryEvidence& evidence)
    {
        static const std::regex immediateRetryPattern(
            R"(^VSICURL: ReadMultiRange: immediate-retry )"
            R"(range=(bytes=[0-9]+-[0-9]+) status=([0-9]+) bytes=([0-9]+) )"
            R"(attempt=([0-9]+) delay-ms=([0-9]+) )"
            R"(connection=(-?[0-9]+) http=(2)$)");
        std::smatch match;
        if (!std::regex_match(message, match, immediateRetryPattern))
            return false;
        evidence.range = match[1].str();
        evidence.code = std::stoi(match[2].str());
        evidence.bytes = std::stoull(match[3].str());
        evidence.attempt = std::stoi(match[4].str());
        evidence.delayMs = std::stoll(match[5].str());
        evidence.connectionId = std::stoll(match[6].str());
        evidence.httpMajor = std::stoi(match[7].str());
        return true;
    }

    bool parseCoordinatorRetryBlockedEvidence(
        const std::string& message, CoordinatorRetryBlockedEvidence& evidence)
    {
        static const std::regex blockedPattern(
            R"(^VSICURL: ParallelHeadRange: transient-retry-blocked )"
            R"(range=(bytes=0-131071) status=([0-9]+) )"
            R"(reason=([a-z]+(?:-[a-z]+)*) )"
            R"(range-connection=(-?[0-9]+) range-http=([0-9]+)$)");
        std::smatch match;
        if (!std::regex_match(message, match, blockedPattern))
            return false;
        evidence.range = match[1].str();
        evidence.code = std::stoi(match[2].str());
        evidence.reason = match[3].str();
        evidence.connectionId = std::stoll(match[4].str());
        evidence.httpMajor = std::stoi(match[5].str());
        return true;
    }

    std::pair<long long, long long> expectedCoordinatorRetryDelayEnvelopeMs(
        int attempt)
    {
        // CPLHTTPGetNewRetryDelay applies a native factor in [2.0, 2.5] to
        // the configured 100 ms initial delay after every failed attempt.
        static constexpr std::array<std::pair<long long, long long>, 3> delays = {{
            {100, 100},
            {200, 250},
            {400, 625},
        }};
        require(attempt >= 1 &&
                    attempt <= static_cast<int>(delays.size()),
                "coordinator retry attempt has no configured delay");
        return delays[static_cast<std::size_t>(attempt - 1)];
    }

    bool isExpectedCoordinatorRetryDelay(int attempt, long long delayMs)
    {
        const auto envelope = expectedCoordinatorRetryDelayEnvelopeMs(attempt);
        return delayMs >= envelope.first && delayMs <= envelope.second;
    }

    bool isExactFiveStatus(int status)
    {
        static const std::set<int> codes = {429, 500, 502, 503, 504};
        return codes.count(status) != 0;
    }

    std::uint64_t checkedUnsignedDecimal(
        const std::string& text, const std::string& label,
        std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max())
    {
        require(!text.empty(), label + " is empty");
        std::uint64_t value = 0;
        const char* first = text.data();
        const char* last = first + text.size();
        const std::from_chars_result parsed =
            std::from_chars(first, last, value);
        require(parsed.ec == std::errc() && parsed.ptr == last &&
                    value <= maximum,
                label + " is out of range");
        return value;
    }

    int checkedNonnegativeInt(const std::string& text,
                              const std::string& label)
    {
        return static_cast<int>(checkedUnsignedDecimal(
            text, label, static_cast<std::uint64_t>(
                std::numeric_limits<int>::max())));
    }

    long long checkedCoordinate(const std::string& text,
                                const std::string& label)
    {
        if (text == "-1") return -1;
        return static_cast<long long>(checkedUnsignedDecimal(
            text, label, static_cast<std::uint64_t>(
                std::numeric_limits<long long>::max())));
    }

    void checkedAdd(std::uint64_t& target, std::uint64_t value,
                    const std::string& label)
    {
        require(value <= std::numeric_limits<std::uint64_t>::max() - target,
                label + " overflowed");
        target += value;
    }

    std::uint64_t checkedIntervalLength(std::uint64_t start,
                                        std::uint64_t end)
    {
        require(end >= start && end - start <
                    std::numeric_limits<std::uint64_t>::max(),
                "science transport interval length overflowed");
        return end - start + 1;
    }

    std::uint64_t checkedJsonUnsigned(const picojson::value& value,
                                      const std::string& label,
                                      std::uint64_t maximum)
    {
        constexpr std::uint64_t MAX_EXACT_JSON_INTEGER =
            9007199254740991ULL;
        require(value.is<double>(), label + " must be numeric");
        const double number = value.get<double>();
        const std::uint64_t effectiveMaximum =
            std::min(maximum, MAX_EXACT_JSON_INTEGER);
        require(std::isfinite(number) && number >= 0.0 &&
                    std::floor(number) == number &&
                    number <= static_cast<double>(effectiveMaximum),
                label + " must be an exactly representable finite integral "
                "value in range");
        return static_cast<std::uint64_t>(number);
    }

    std::pair<std::uint64_t, std::uint64_t> parseExactByteRange(
        const std::string& range)
    {
        static const std::regex pattern(R"(^bytes=([0-9]+)-([0-9]+)$)");
        std::smatch match;
        require(std::regex_match(range, match, pattern),
                "science transport Range is malformed");
        const std::uint64_t maximum = static_cast<std::uint64_t>(
            std::numeric_limits<long long>::max());
        const std::uint64_t start = checkedUnsignedDecimal(
            match[1].str(), "science transport Range start", maximum);
        const std::uint64_t end = checkedUnsignedDecimal(
            match[2].str(), "science transport Range end", maximum);
        require(end >= start, "science transport Range is reversed");
        return {start, end};
    }

    std::string correlationId(const ScienceTransportCompletion& completion)
    {
        return completion.context + "/" +
            std::to_string(completion.request) + "/" +
            std::to_string(completion.attempt);
    }

    void validateScienceTransportCompletionStructure(
        const ScienceTransportCompletion& completion)
    {
        const bool legalTuple =
            (completion.scope == "coordinator" &&
             completion.role == "head" && completion.method == "HEAD" &&
             completion.range == "none") ||
            (completion.scope == "coordinator" &&
             completion.role == "range" && completion.method == "GET" &&
             completion.range.rfind("bytes=", 0) == 0) ||
            (completion.scope == "multirange" &&
             completion.role == "range" && completion.method == "GET" &&
             completion.range.rfind("bytes=", 0) == 0) ||
            (completion.scope == "ordinary-head" &&
             completion.role == "head" && completion.method == "HEAD" &&
             completion.range == "none") ||
            (completion.scope == "ordinary-head" &&
             completion.role == "head" && completion.method == "GET" &&
             completion.range == "none");
        require(legalTuple, "science transport scope/role/method/range tuple is illegal");
        require(completion.ordinal > 0 && completion.request > 0 &&
                    completion.attempt >= 1 && completion.attempt <= 4,
                "science transport ordinal/request/attempt is out of bounds");
        require(completion.contentLengthCount >= 0 &&
                    completion.contentRangeCount >= 0,
                "science transport header count is negative");
        if (completion.range != "none")
            static_cast<void>(parseExactByteRange(completion.range));
    }

    std::string scienceTransportAdmissionFailure(
        const ScienceTransportCompletion& completion)
    {
        if (completion.curlCode != 0) return "curl";
        if (completion.redirects != 0) return "redirect";
        if (completion.httpMajor != 2) return "http-version";
        if (completion.connectionId < 0) return "connection";
        if (!((completion.contentLengthCount == 0 &&
               !completion.contentLengthValid &&
               completion.declaredContentLength == 0) ||
              (completion.contentLengthCount == 1 &&
               completion.contentLengthValid)))
            return "content-length";

        if (completion.method == "HEAD")
        {
            if (completion.actualBodyBytes != 0) return "body";
            if (completion.contentRangeCount != 0 ||
                completion.contentRangeValid ||
                completion.contentStart != -1 ||
                completion.contentEnd != -1 ||
                completion.contentTotal != -1)
                return "content-range";
            if (!(completion.status == 200 ||
                  isExactFiveStatus(completion.status) ||
                  completion.status == 400 ||
                  completion.status == 404 ||
                  completion.status == 405))
                return "status";
            if (completion.status == 200)
            {
                if (completion.contentLengthCount != 1 ||
                    completion.declaredContentLength == 0)
                    return "content-length";
            }
            return {};
        }

        if (completion.range == "none")
        {
            if (completion.scope != "ordinary-head" ||
                completion.status != 200 ||
                completion.actualBodyBytes != 0 ||
                completion.contentRangeCount != 0 ||
                completion.contentRangeValid ||
                completion.contentStart != -1 ||
                completion.contentEnd != -1 ||
                completion.contentTotal != -1)
                return "ordinary-head";
            return {};
        }

        const auto interval = parseExactByteRange(completion.range);
        if (completion.status == 206)
        {
            if (completion.contentLengthCount != 1 ||
                !completion.contentLengthValid ||
                completion.declaredContentLength !=
                    completion.actualBodyBytes ||
                completion.actualBodyBytes != checkedIntervalLength(
                    interval.first, interval.second))
                return "content-length";
            if (completion.contentRangeCount != 1 ||
                !completion.contentRangeValid ||
                completion.contentStart !=
                    static_cast<long long>(interval.first) ||
                completion.contentEnd !=
                    static_cast<long long>(interval.second) ||
                completion.contentTotal <= completion.contentEnd)
                return "content-range";
            return {};
        }

        if (!isExactFiveStatus(completion.status)) return "status";
        if (completion.actualBodyBytes > 131072) return "body";
        if (completion.contentLengthCount > 1 ||
            (completion.contentLengthCount == 1 &&
             completion.declaredContentLength !=
                completion.actualBodyBytes))
            return "content-length";
        if (completion.contentRangeCount > 1) return "content-range";
        if (completion.contentRangeCount == 0)
        {
            if (completion.contentRangeValid ||
                completion.contentStart != -1 ||
                completion.contentEnd != -1 ||
                completion.contentTotal != -1)
                return "content-range";
        }
        else
        {
            if (!completion.contentRangeValid ||
                completion.contentStart !=
                    static_cast<long long>(interval.first) ||
                completion.contentEnd !=
                    static_cast<long long>(interval.second) ||
                completion.contentTotal <= completion.contentEnd)
                return "content-range";
        }
        return {};
    }

    AttributedTransportProof buildAttributedTransportProof(
        const DebugCapture& capture,
        const std::vector<ScienceServerRequest>& serverRequests)
    {
        struct RawRequest
        {
            std::string method;
            std::string path;
            std::string range;
            std::string correlation;
        };
        struct RawResponse
        {
            int status = 0;
            std::vector<std::string> contentLengths;
            std::vector<std::string> contentRanges;
        };
        using Decision = AttributedEventEvidence;

        static const std::regex responseV1(
            R"(^VSICURL: ScienceTransport: response-v1 ordinal=([0-9]+) )"
            R"(context=([0-9a-f]{32}) )"
            R"(scope=(coordinator|multirange|ordinary-head) role=(head|range) )"
            R"(request=([0-9]+) attempt=([1-4]) method=(HEAD|GET) )"
            R"(range=(none|bytes=[0-9]+-[0-9]+) curl=([0-9]+) status=([0-9]+) )"
            R"(http=([0-2]) redirects=([0-9]+) connection=(-1|[0-9]+) )"
            R"(content-length-count=([0-9]+) content-length-valid=([01]) )"
            R"(declared-content-length=([0-9]+) content-range-count=([0-9]+) )"
            R"(content-range-valid=([01]) content-start=(-1|[0-9]+) )"
            R"(content-end=(-1|[0-9]+) content-total=(-1|[0-9]+) )"
            R"(actual-body-bytes=([0-9]+)$)");
        static const std::regex headRetryPattern(
            R"(^VSICURL: ParallelHeadRange: head-transient-retry )"
            R"(context=([0-9a-f]{32}) retry=([1-3]) request=([0-9]+) )"
            R"(failed-attempt=([1-3]) scheduled-attempt=([2-4]) )"
            R"(status=(429|500|502|503|504) delay-ms=([0-9]+) )"
            R"(connection=([0-9]+) http=(2) declared-content-length=([0-9]+) )"
            R"(actual-body-bytes=(0)$)");
        static const std::regex coordinatorRetryPattern(
            R"(^VSICURL: ParallelHeadRange: transient-retry )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) )"
            R"(failed-attempt=([1-3]) scheduled-attempt=([2-4]) )"
            R"(range=(bytes=[0-9]+-[0-9]+) status=(429|500|502|503|504) )"
            R"(bytes=([0-9]+) delay-ms=([0-9]+) )"
            R"(connection=([0-9]+) http=(2)$)");
        static const std::regex immediateRetryPattern(
            R"(^VSICURL: ReadMultiRange: immediate-retry )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) )"
            R"(failed-attempt=([1-3]) scheduled-attempt=([2-4]) )"
            R"(range=(bytes=[0-9]+-[0-9]+) status=(429|500|502|503|504) )"
            R"(bytes=([0-9]+) delay-ms=([0-9]+) )"
            R"(connection=([0-9]+) http=(2)$)");
        static const std::regex fallbackPattern(
            R"(^VSICURL: ParallelHeadRange: fallback )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) attempt=([1-4]) )"
            R"(reason=([a-z]+(?:-[a-z]+)*)$)");
        static const std::regex headBlockedPattern(
            R"(^VSICURL: ParallelHeadRange: head-transient-retry-blocked )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) attempt=([1-4]) )"
            R"(status=([0-9]+) reason=([a-z]+(?:-[a-z]+)*) )"
            R"(connection=(-1|[0-9]+) http=([0-2])$)");
        static const std::regex rangeBlockedPattern(
            R"(^VSICURL: ParallelHeadRange: transient-retry-blocked )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) attempt=([1-4]) )"
            R"(range=(bytes=[0-9]+-[0-9]+) status=([0-9]+) bytes=([0-9]+) )"
            R"(reason=([a-z]+(?:-[a-z]+)*) connection=(-1|[0-9]+) )"
            R"(http=([0-2])$)");
        static const std::regex logicalGetPattern(
            R"(^VSICURL: ParallelHeadRange: logical-get-complete )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) attempt=([1-4]) )"
            R"(range=(bytes=[0-9]+-[0-9]+) bytes=([0-9]+)$)");
        static const std::regex publicationPattern(
            R"(^VSICURL: ParallelHeadRange: published )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) attempt=([1-4])$)");
        static const std::regex propertyPublicationPattern(
            R"(^VSICURL: ParallelHeadRange: file-property-published )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) attempt=([1-4])$)");
        static const std::regex propertyBlockedPattern(
            R"(^VSICURL: ParallelHeadRange: file-property-publication-blocked )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) attempt=([1-4]) )"
            R"(reason=([a-z]+(?:-[a-z]+)*)$)");
        static const std::regex operationBlockedPattern(
            R"(^VSICURL: ParallelHeadRange: blocked-operation-marked )"
            R"(context=([0-9a-f]{32}) request=([0-9]+) attempt=([1-4]) )"
            R"(reason=([a-z]+(?:-[a-z]+)*)$)");

        AttributedTransportProof proof;
        std::vector<RawRequest> rawRequests;
        std::vector<RawResponse> rawResponses;
        std::vector<Decision> decisions;
        RawResponse currentResponse;
        bool responseOpen = false;

        for (std::size_t index = 0; index < capture.messages.size(); ++index)
        {
            const std::string& message = capture.messages[index];
            const std::string outputPrefix = "CURL_INFO_HEADER_OUT: ";
            const std::string inputPrefix = "CURL_INFO_HEADER_IN: ";
            if (message.rfind(outputPrefix, 0) == 0)
            {
                const std::string payload = message.substr(outputPrefix.size());
                const std::string normalizedPayload = lower("\n" + payload);
                const std::string correlationHeader =
                    "\nx-osgsol-science-correlation:";
                std::size_t correlationHeaderCount = 0;
                for (std::size_t offset = 0;
                     (offset = normalizedPayload.find(
                          correlationHeader, offset)) != std::string::npos;
                     offset += correlationHeader.size())
                {
                    ++correlationHeaderCount;
                }
                std::istringstream stream(payload);
                std::string firstLine;
                std::getline(stream, firstLine);
                if (!firstLine.empty() && firstLine.back() == '\r')
                    firstLine.pop_back();
                std::istringstream firstLineStream(firstLine);
                RawRequest request;
                std::string uri;
                std::string version;
                firstLineStream >> request.method >> uri >> version;
                request.path = uri;
                const auto headers = parseHeaders(stream);
                const auto range = headers.find("range");
                if (range != headers.end()) request.range = range->second;
                const auto correlation = headers.find(
                    "x-osgsol-science-correlation");
                if (request.method != "CONNECT")
                {
                    require(correlation != headers.end() &&
                                correlationHeaderCount == 1,
                            "science raw request omitted or duplicated its "
                            "correlation header");
                    request.correlation = correlation->second;
                    rawRequests.push_back(request);
                }
            }
            else if (message.rfind(inputPrefix, 0) == 0)
            {
                std::string line = message.substr(inputPrefix.size());
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("HTTP/", 0) == 0)
                {
                    require(!responseOpen,
                            "science raw response blocks overlap");
                    responseOpen = true;
                    currentResponse = RawResponse();
                    std::istringstream status(line);
                    std::string version;
                    status >> version >> currentResponse.status;
                }
                else if (line.empty())
                {
                    require(responseOpen,
                            "science raw response ended without status");
                    rawResponses.push_back(currentResponse);
                    responseOpen = false;
                }
                else if (responseOpen)
                {
                    const std::size_t separator = line.find(':');
                    require(separator != std::string::npos,
                            "science raw response header is malformed");
                    std::string value = line.substr(separator + 1);
                    while (!value.empty() && value.front() == ' ')
                        value.erase(value.begin());
                    const std::string name = lower(line.substr(0, separator));
                    if (name == "content-length")
                        currentResponse.contentLengths.push_back(value);
                    if (name == "content-range")
                        currentResponse.contentRanges.push_back(value);
                }
            }

            std::smatch match;
            if (std::regex_match(message, match, responseV1))
            {
                ScienceTransportCompletion completion;
                completion.ordinal = checkedUnsignedDecimal(
                    match[1].str(), "science completion ordinal");
                completion.context = match[2].str();
                completion.scope = match[3].str();
                completion.role = match[4].str();
                completion.request = checkedUnsignedDecimal(
                    match[5].str(), "science completion request");
                completion.attempt = checkedNonnegativeInt(
                    match[6].str(), "science completion attempt");
                completion.method = match[7].str();
                completion.range = match[8].str();
                completion.curlCode = checkedNonnegativeInt(
                    match[9].str(), "science completion curl code");
                completion.status = checkedNonnegativeInt(
                    match[10].str(), "science completion status");
                completion.httpMajor = checkedNonnegativeInt(
                    match[11].str(), "science completion HTTP major");
                completion.redirects = checkedNonnegativeInt(
                    match[12].str(), "science completion redirects");
                completion.connectionId = checkedCoordinate(
                    match[13].str(), "science completion connection");
                completion.contentLengthCount = checkedNonnegativeInt(
                    match[14].str(), "science Content-Length count");
                completion.contentLengthValid = match[15].str() == "1";
                completion.declaredContentLength =
                    checkedUnsignedDecimal(match[16].str(),
                                           "science declared length");
                completion.contentRangeCount = checkedNonnegativeInt(
                    match[17].str(), "science Content-Range count");
                completion.contentRangeValid = match[18].str() == "1";
                completion.contentStart = checkedCoordinate(
                    match[19].str(), "science Content-Range start");
                completion.contentEnd = checkedCoordinate(
                    match[20].str(), "science Content-Range end");
                completion.contentTotal = checkedCoordinate(
                    match[21].str(), "science Content-Range total");
                completion.actualBodyBytes = checkedUnsignedDecimal(
                    match[22].str(), "science actual body bytes");
                completion.messageIndex = index;
                validateScienceTransportCompletionStructure(completion);
                completion.admissionReason =
                    scienceTransportAdmissionFailure(completion);
                completion.admissionValid =
                    completion.admissionReason.empty();
                proof.completions.push_back(completion);
            }
            else
            {
                require(message.find("ScienceTransport: response-v1") ==
                            std::string::npos,
                        "science transport response-v1 event is malformed");
            }

            bool matchedHeadRetry = false;
            if (std::regex_match(message, match, headRetryPattern))
            {
                matchedHeadRetry = true;
                HeadRetryEvidence retry;
                retry.context = match[1].str();
                retry.retryOrdinal = checkedNonnegativeInt(
                    match[2].str(), "HEAD retry ordinal");
                retry.request = checkedUnsignedDecimal(
                    match[3].str(), "HEAD retry request");
                retry.failedAttempt = checkedNonnegativeInt(
                    match[4].str(), "HEAD failed attempt");
                retry.scheduledAttempt = checkedNonnegativeInt(
                    match[5].str(), "HEAD scheduled attempt");
                retry.failedStatus = checkedNonnegativeInt(
                    match[6].str(), "HEAD failed status");
                retry.delayMs = static_cast<long long>(checkedUnsignedDecimal(
                    match[7].str(), "HEAD retry delay",
                    static_cast<std::uint64_t>(
                        std::numeric_limits<long long>::max())));
                retry.failedConnectionId = checkedCoordinate(
                    match[8].str(), "HEAD failed connection");
                retry.failedHttpMajor = checkedNonnegativeInt(
                    match[9].str(), "HEAD failed HTTP major");
                retry.failedDeclaredContentLength =
                    checkedUnsignedDecimal(match[10].str(),
                                           "HEAD failed declared length");
                retry.failedActualBodyBytes = checkedUnsignedDecimal(
                    match[11].str(), "HEAD failed actual body bytes");
                retry.messageIndex = index;
                require(retry.scheduledAttempt == retry.failedAttempt + 1 &&
                            isExpectedCoordinatorRetryDelay(
                                retry.retryOrdinal, retry.delayMs),
                        "HEAD retry transition or delay is invalid");
                proof.headRetries.push_back(retry);
            }
            else
            {
                require(message.find("head-transient-retry ") ==
                            std::string::npos,
                        "HEAD transient retry event is malformed");
            }

            Decision decision;
            bool matchedDecision = false;
            const auto parseRetryDecision = [&](const std::string& kind)
            {
                decision.kind = kind;
                decision.context = match[1].str();
                decision.request = checkedUnsignedDecimal(
                    match[2].str(), kind + " request");
                decision.attempt = checkedNonnegativeInt(
                    match[3].str(), kind + " failed attempt");
                decision.scheduledAttempt = checkedNonnegativeInt(
                    match[4].str(), kind + " scheduled attempt");
                decision.range = match[5].str();
                decision.status = checkedNonnegativeInt(
                    match[6].str(), kind + " status");
                decision.bytes = checkedUnsignedDecimal(
                    match[7].str(), kind + " bytes");
                decision.delayMs = static_cast<long long>(
                    checkedUnsignedDecimal(match[8].str(), kind + " delay",
                        static_cast<std::uint64_t>(
                            std::numeric_limits<long long>::max())));
                decision.connectionId = checkedCoordinate(
                    match[9].str(), kind + " connection");
                decision.httpMajor = checkedNonnegativeInt(
                    match[10].str(), kind + " HTTP major");
            };
            if (std::regex_match(message, match, coordinatorRetryPattern))
            {
                parseRetryDecision("coordinator-retry");
                matchedDecision = true;
            }
            else if (std::regex_match(message, match, immediateRetryPattern))
            {
                parseRetryDecision("immediate-retry");
                matchedDecision = true;
            }
            else if (std::regex_match(message, match, fallbackPattern))
            {
                decision.kind = "fallback";
                decision.context = match[1].str();
                decision.request = checkedUnsignedDecimal(
                    match[2].str(), "fallback request");
                decision.attempt = checkedNonnegativeInt(
                    match[3].str(), "fallback attempt");
                decision.reason = match[4].str();
                matchedDecision = true;
            }
            else if (std::regex_match(message, match, headBlockedPattern))
            {
                decision.kind = "head-blocked";
                decision.context = match[1].str();
                decision.request = checkedUnsignedDecimal(
                    match[2].str(), "HEAD block request");
                decision.attempt = checkedNonnegativeInt(
                    match[3].str(), "HEAD block attempt");
                decision.status = checkedNonnegativeInt(
                    match[4].str(), "HEAD block status");
                decision.reason = match[5].str();
                decision.connectionId = checkedCoordinate(
                    match[6].str(), "HEAD block connection");
                decision.httpMajor = checkedNonnegativeInt(
                    match[7].str(), "HEAD block HTTP major");
                matchedDecision = true;
            }
            else if (std::regex_match(message, match, rangeBlockedPattern))
            {
                decision.kind = "range-blocked";
                decision.context = match[1].str();
                decision.request = checkedUnsignedDecimal(
                    match[2].str(), "Range block request");
                decision.attempt = checkedNonnegativeInt(
                    match[3].str(), "Range block attempt");
                decision.range = match[4].str();
                decision.status = checkedNonnegativeInt(
                    match[5].str(), "Range block status");
                decision.bytes = checkedUnsignedDecimal(
                    match[6].str(), "Range block bytes");
                decision.reason = match[7].str();
                decision.connectionId = checkedCoordinate(
                    match[8].str(), "Range block connection");
                decision.httpMajor = checkedNonnegativeInt(
                    match[9].str(), "Range block HTTP major");
                matchedDecision = true;
            }
            else if (std::regex_match(message, match, logicalGetPattern))
            {
                decision.kind = "logical-get";
                decision.context = match[1].str();
                decision.request = checkedUnsignedDecimal(
                    match[2].str(), "logical GET request");
                decision.attempt = checkedNonnegativeInt(
                    match[3].str(), "logical GET attempt");
                decision.range = match[4].str();
                decision.bytes = checkedUnsignedDecimal(
                    match[5].str(), "logical GET bytes");
                matchedDecision = true;
            }
            else if (std::regex_match(message, match, publicationPattern) ||
                     std::regex_match(message, match,
                                      propertyPublicationPattern))
            {
                decision.kind = message.find("file-property-published") !=
                        std::string::npos
                    ? "property-published" : "published";
                decision.context = match[1].str();
                decision.request = checkedUnsignedDecimal(
                    match[2].str(), decision.kind + " request");
                decision.attempt = checkedNonnegativeInt(
                    match[3].str(), decision.kind + " attempt");
                matchedDecision = true;
            }
            else if (std::regex_match(message, match, propertyBlockedPattern) ||
                     std::regex_match(message, match, operationBlockedPattern))
            {
                decision.kind = message.find(
                        "file-property-publication-blocked") !=
                        std::string::npos
                    ? "property-blocked" : "operation-blocked";
                decision.context = match[1].str();
                decision.request = checkedUnsignedDecimal(
                    match[2].str(), decision.kind + " request");
                decision.attempt = checkedNonnegativeInt(
                    match[3].str(), decision.kind + " attempt");
                decision.reason = match[4].str();
                matchedDecision = true;
            }
            if (matchedDecision)
            {
                decision.messageIndex = index;
                decisions.push_back(decision);
            }
            else if (!matchedHeadRetry)
            {
                static const std::array<const char*, 9> prefixes = {{
                    "VSICURL: ParallelHeadRange: transient-retry ",
                    "VSICURL: ReadMultiRange: immediate-retry ",
                    "VSICURL: ParallelHeadRange: fallback ",
                    "VSICURL: ParallelHeadRange: head-transient-retry-blocked ",
                    "VSICURL: ParallelHeadRange: transient-retry-blocked ",
                    "VSICURL: ParallelHeadRange: logical-get-complete ",
                    "VSICURL: ParallelHeadRange: published ",
                    "VSICURL: ParallelHeadRange: file-property-published ",
                    "VSICURL: ParallelHeadRange: file-property-publication-blocked ",
                }};
                for (const char* prefix : prefixes)
                    require(message.rfind(prefix, 0) != 0,
                            "science decision event has a malformed schema");
                require(message.rfind(
                            "VSICURL: ParallelHeadRange: blocked-operation-marked ",
                            0) != 0,
                        "science operation block has a malformed schema");
            }
        }
        require(!responseOpen, "science raw response is incomplete");
        require(!proof.completions.empty(),
                "AttributedV6 requires response-v1 completion events");

        std::map<std::string, std::uint64_t> nextOrdinal;
        std::map<std::pair<std::string, std::uint64_t>, int> nextAttempt;
        std::map<std::pair<std::string, std::uint64_t>,
                 std::tuple<std::string, std::string, std::string, std::string>>
            requestIdentity;
        std::set<std::tuple<std::string, std::string, std::uint64_t, int>>
            uniqueCompletions;
        std::map<std::string, long long> contextConnections;
        bool contextConnectionsConsistent = true;
        std::set<std::string> contexts;
        for (const ScienceTransportCompletion& completion : proof.completions)
        {
            contexts.insert(completion.context);
            std::uint64_t& ordinal = nextOrdinal[completion.context];
            require(completion.ordinal == ++ordinal,
                    "science transport ordinals are not contiguous in log order");
            const auto requestKey =
                std::make_pair(completion.context, completion.request);
            int& attempt = nextAttempt[requestKey];
            require(completion.attempt == ++attempt,
                    "science transport attempts are not contiguous");
            const auto identity = std::make_tuple(
                completion.scope, completion.role,
                completion.method, completion.range);
            const auto insertedIdentity = requestIdentity.emplace(
                requestKey, identity);
            require(insertedIdentity.second ||
                        insertedIdentity.first->second == identity,
                    "science transport request identity changed across attempts");
            require(uniqueCompletions.emplace(
                        completion.context, completion.scope,
                        completion.request, completion.attempt).second,
                    "science transport completion is duplicated");
            const auto insertedConnection = contextConnections.emplace(
                completion.context, completion.connectionId);
            if (!insertedConnection.second &&
                insertedConnection.first->second != completion.connectionId)
                contextConnectionsConsistent = false;
        }
        std::map<std::string, std::set<std::uint64_t>> contextRequests;
        for (const auto& item : requestIdentity)
            contextRequests[item.first.first].insert(item.first.second);
        for (const auto& item : contextRequests)
        {
            require(!item.second.empty() &&
                        *item.second.rbegin() == item.second.size(),
                    "science transport request IDs are not exactly 1..N");
            std::uint64_t expected = 1;
            for (std::uint64_t request : item.second)
                require(request == expected++,
                        "science transport request ID has a gap");
        }

        std::map<std::string, RawRequest> rawByCorrelation;
        static const std::regex correlationPattern(
            R"(^[0-9a-f]{32}/[1-9][0-9]*/[1-4]$)");
        for (const RawRequest& request : rawRequests)
        {
            require(std::regex_match(request.correlation,
                                     correlationPattern) &&
                        rawByCorrelation.emplace(
                        request.correlation, request).second,
                    "science raw correlation header is malformed or duplicated");
        }
        require(rawByCorrelation.size() == proof.completions.size(),
                "science raw request/completion count differs");
        for (const ScienceTransportCompletion& completion : proof.completions)
        {
            const auto raw = rawByCorrelation.find(correlationId(completion));
            require(raw != rawByCorrelation.end() &&
                        raw->second.method == completion.method &&
                        ((completion.range == "none" &&
                          raw->second.range.empty()) ||
                         raw->second.range == completion.range),
                    "science raw correlation/method/Range does not match completion");
        }

        const auto responseTuple = [](
                int status, int contentLengthCount,
                bool contentLengthValid,
                std::uint64_t declaredContentLength,
                int contentRangeCount, bool contentRangeValid,
                long long contentStart, long long contentEnd,
                long long contentTotal)
        {
            std::ostringstream tuple;
            tuple << status << '|' << contentLengthCount << '|'
                  << (contentLengthValid ? 1 : 0) << '|'
                  << declaredContentLength << '|' << contentRangeCount << '|'
                  << (contentRangeValid ? 1 : 0) << '|' << contentStart << '|'
                  << contentEnd << '|' << contentTotal;
            return tuple.str();
        };
        std::map<std::string, int> rawResponseMultiset;
        for (const RawResponse& response : rawResponses)
        {
            if (response.status == 200 && response.contentLengths.empty() &&
                response.contentRanges.empty())
                continue;
            bool contentLengthValid = false;
            std::uint64_t declaredContentLength = 0;
            if (response.contentLengths.size() == 1)
            {
                try
                {
                    declaredContentLength = checkedUnsignedDecimal(
                        response.contentLengths.front(),
                        "raw response Content-Length");
                    contentLengthValid = true;
                }
                catch (const std::exception&)
                {
                    contentLengthValid = false;
                    declaredContentLength = 0;
                }
            }
            bool contentRangeValid = false;
            long long contentStart = -1;
            long long contentEnd = -1;
            long long contentTotal = -1;
            if (response.contentRanges.size() == 1)
            {
                static const std::regex rawContentRange(
                    R"(^bytes ([0-9]+)-([0-9]+)/([0-9]+)$)");
                std::smatch contentRangeMatch;
                if (std::regex_match(response.contentRanges.front(),
                                     contentRangeMatch, rawContentRange))
                {
                    try
                    {
                        contentStart = checkedCoordinate(
                            contentRangeMatch[1].str(),
                            "raw Content-Range start");
                        contentEnd = checkedCoordinate(
                            contentRangeMatch[2].str(),
                            "raw Content-Range end");
                        contentTotal = checkedCoordinate(
                            contentRangeMatch[3].str(),
                            "raw Content-Range total");
                        contentRangeValid = contentStart >= 0 &&
                            contentEnd >= contentStart &&
                            contentTotal > contentEnd;
                    }
                    catch (const std::exception&)
                    {
                        contentRangeValid = false;
                        contentStart = contentEnd = contentTotal = -1;
                    }
                }
            }
            ++rawResponseMultiset[responseTuple(
                response.status,
                static_cast<int>(response.contentLengths.size()),
                contentLengthValid, declaredContentLength,
                static_cast<int>(response.contentRanges.size()),
                contentRangeValid, contentStart, contentEnd, contentTotal)];
        }
        std::map<std::string, int> eventResponseMultiset;
        for (const ScienceTransportCompletion& completion : proof.completions)
        {
            const bool boundDuplicateProtocolFailure =
                completion.curlCode != 0 && completion.status == 0 &&
                std::any_of(serverRequests.begin(), serverRequests.end(),
                    [&](const ScienceServerRequest& request)
                    {
                        return request.correlation ==
                                correlationId(completion) &&
                            request.status == 0 &&
                            request.responseCount == 0 &&
                            request.protocolErrorCount == 1 &&
                            request.protocolError ==
                                "ERR_HTTP2_HEADER_SINGLE_VALUE";
                    });
            if (boundDuplicateProtocolFailure) continue;
            ++eventResponseMultiset[responseTuple(
                completion.status, completion.contentLengthCount,
                completion.contentLengthValid,
                completion.declaredContentLength,
                completion.contentRangeCount,
                completion.contentRangeValid,
                completion.contentStart, completion.contentEnd,
                completion.contentTotal)];
        }
        require(rawResponseMultiset == eventResponseMultiset,
                "science raw response multiset does not match completions");

        if (!serverRequests.empty())
        {
            std::map<std::string, ScienceServerRequest> serverByCorrelation;
            for (const ScienceServerRequest& request : serverRequests)
            {
                const bool normalResponse = request.status > 0 &&
                    request.responseCount == 1 &&
                    request.protocolErrorCount == 0 && !request.aborted;
                const bool duplicateProtocolFailure = request.status == 0 &&
                    request.responseCount == 0 &&
                    request.protocolErrorCount == 1 && request.aborted &&
                    request.protocolError ==
                        "ERR_HTTP2_HEADER_SINGLE_VALUE";
                require(!request.sessionId.empty() && request.streamId > 0 &&
                            !request.path.empty() &&
                            request.startCount == 1 &&
                            request.endCount == 1 &&
                            request.start > 0 &&
                            request.response > request.start &&
                            request.end >= request.response &&
                            (normalResponse || duplicateProtocolFailure) &&
                            std::regex_match(request.correlation,
                                             correlationPattern) &&
                            serverByCorrelation.emplace(
                                request.correlation, request).second,
                        "science server oracle is incomplete, malformed, or duplicated");
            }
            require(serverByCorrelation.size() == proof.completions.size(),
                    "science server/completion count differs");
            for (const ScienceTransportCompletion& completion : proof.completions)
            {
                const auto server = serverByCorrelation.find(
                    correlationId(completion));
                const bool duplicateProtocolFailure =
                    server != serverByCorrelation.end() &&
                    server->second.protocolErrorCount == 1;
                require(server != serverByCorrelation.end() &&
                            server->second.method == completion.method &&
                            rawByCorrelation.at(correlationId(completion)).path ==
                                server->second.path &&
                            server->second.status == completion.status &&
                            server->second.attemptedBodyBytes ==
                                completion.actualBodyBytes &&
                            (!duplicateProtocolFailure ||
                             (completion.curlCode != 0 &&
                              completion.status == 0 &&
                              completion.actualBodyBytes == 0 &&
                              completion.contentLengthCount == 0 &&
                              completion.contentRangeCount == 0)) &&
                            (!completion.contentLengthValid ||
                             server->second.contentLength ==
                                completion.declaredContentLength) &&
                            ((completion.range == "none" &&
                              server->second.range.empty()) ||
                             server->second.range == completion.range) &&
                            ((completion.contentRangeCount == 0 &&
                              server->second.contentRange.empty()) ||
                             (completion.contentRangeCount == 1 &&
                              !completion.contentRangeValid &&
                              !server->second.contentRange.empty()) ||
                             (completion.contentRangeCount == 1 &&
                              completion.contentRangeValid &&
                              server->second.contentRange == "bytes " +
                                  std::to_string(completion.contentStart) +
                                  "-" +
                                  std::to_string(completion.contentEnd) +
                                  "/" +
                                  std::to_string(completion.contentTotal))),
                        "science server full tuple does not match completion");
            }
        }

        std::map<std::string, int> nextHeadRetry;
        std::map<std::tuple<std::string, std::uint64_t, int>, int>
            headRetryByFailedAttempt;
        for (const HeadRetryEvidence& retry : proof.headRetries)
        {
            require(contexts.count(retry.context) == 1 &&
                        retry.retryOrdinal == ++nextHeadRetry[retry.context],
                    "HEAD retry context/ordinal is invalid");
            const ScienceTransportCompletion* failed = nullptr;
            const ScienceTransportCompletion* scheduled = nullptr;
            for (const ScienceTransportCompletion& completion : proof.completions)
            {
                if (completion.context != retry.context ||
                    completion.request != retry.request)
                    continue;
                if (completion.attempt == retry.failedAttempt)
                    failed = &completion;
                if (completion.attempt == retry.scheduledAttempt)
                    scheduled = &completion;
            }
            require(failed != nullptr && scheduled != nullptr &&
                        failed->scope == "coordinator" &&
                        failed->role == "head" &&
                        failed->method == "HEAD" &&
                        isExactFiveStatus(failed->status) &&
                        failed->messageIndex < retry.messageIndex &&
                        retry.messageIndex < scheduled->messageIndex,
                    "HEAD retry does not bridge two ordered HEAD completions");
            require(failed->status == retry.failedStatus &&
                        failed->connectionId == retry.failedConnectionId &&
                        failed->httpMajor == retry.failedHttpMajor &&
                        failed->declaredContentLength ==
                            retry.failedDeclaredContentLength &&
                        failed->actualBodyBytes ==
                            retry.failedActualBodyBytes,
                    "HEAD retry fields differ from its failed completion");
            ++headRetryByFailedAttempt[{retry.context, retry.request,
                                        retry.failedAttempt}];
        }
        for (const ScienceTransportCompletion& completion : proof.completions)
        {
            if (completion.scope != "coordinator" ||
                completion.role != "head" ||
                !isExactFiveStatus(completion.status))
                continue;
            const auto requestKey =
                std::make_pair(completion.context, completion.request);
            if (completion.attempt < nextAttempt[requestKey])
            {
                require(headRetryByFailedAttempt[{
                            completion.context, completion.request,
                            completion.attempt}] == 1,
                        "retried exact-five HEAD completion lacks its "
                        "scheduled HEAD transition");
            }
        }

        const auto completionFor = [&proof](const Decision& decision,
                                             int attempt)
            -> const ScienceTransportCompletion*
        {
            for (const ScienceTransportCompletion& completion :
                 proof.completions)
            {
                if (completion.context == decision.context &&
                    completion.request == decision.request &&
                    completion.attempt == attempt)
                    return &completion;
            }
            return nullptr;
        };
        std::map<std::tuple<std::string, std::uint64_t, int>, int>
            rangeRetryByFailedAttempt;
        for (const Decision& decision : decisions)
        {
            require(contexts.count(decision.context) == 1,
                    "science decision event references another context");
            const ScienceTransportCompletion* failed = completionFor(
                decision, decision.attempt);
            require(failed != nullptr &&
                        failed->messageIndex < decision.messageIndex,
                    "science decision is not bound after its completion");
            if (decision.kind == "coordinator-retry" ||
                decision.kind == "immediate-retry")
            {
                require(++rangeRetryByFailedAttempt[{
                            decision.context, decision.request,
                            decision.attempt}] == 1,
                        "Range completion has duplicate retry transitions");
                const ScienceTransportCompletion* scheduled = completionFor(
                    decision, decision.scheduledAttempt);
                require(scheduled != nullptr &&
                            decision.scheduledAttempt == decision.attempt + 1 &&
                            decision.messageIndex < scheduled->messageIndex &&
                            failed->role == "range" &&
                            failed->method == "GET" &&
                            failed->scope ==
                                (decision.kind == "coordinator-retry"
                                     ? "coordinator" : "multirange") &&
                            failed->range == decision.range &&
                            failed->status == decision.status &&
                            failed->actualBodyBytes == decision.bytes &&
                            failed->connectionId == decision.connectionId &&
                            failed->httpMajor == decision.httpMajor &&
                            isExpectedCoordinatorRetryDelay(
                                decision.attempt, decision.delayMs),
                        "Range retry event does not bridge exact completions");
            }
            else if (decision.kind == "fallback")
            {
                require(failed->scope == "coordinator" &&
                            (failed->role == "head" ||
                             failed->role == "range"),
                        "fallback is not bound to coordinator transport");
                for (const ScienceTransportCompletion& completion :
                     proof.completions)
                {
                    require(completion.context != decision.context ||
                                completion.scope != "coordinator" ||
                                completion.messageIndex <=
                                    decision.messageIndex,
                            "coordinator attempt occurred after terminal fallback");
                }
                for (const Decision& later : decisions)
                {
                    require(later.context != decision.context ||
                                later.messageIndex <= decision.messageIndex ||
                                (later.kind != "published" &&
                                 later.kind != "property-published"),
                            "publication occurred after terminal fallback");
                }
            }
            else if (decision.kind == "head-blocked")
            {
                require(failed->scope == "coordinator" &&
                            failed->role == "head" &&
                            failed->status == decision.status &&
                            failed->connectionId == decision.connectionId &&
                            failed->httpMajor == decision.httpMajor,
                        "HEAD block fields differ from its completion");
            }
            else if (decision.kind == "range-blocked")
            {
                require(failed->role == "range" &&
                            failed->range == decision.range &&
                            failed->status == decision.status &&
                            failed->actualBodyBytes == decision.bytes &&
                            failed->connectionId == decision.connectionId &&
                            failed->httpMajor == decision.httpMajor,
                        "Range block fields differ from its completion");
            }
            else if (decision.kind == "logical-get")
            {
                require(failed->role == "range" &&
                            failed->method == "GET" &&
                            failed->status == 206 &&
                            failed->range == decision.range &&
                            failed->actualBodyBytes == decision.bytes,
                        "logical GET fields differ from successful completion");
            }
            else if (decision.kind == "published" ||
                     decision.kind == "property-published")
            {
                require(failed->scope == "coordinator" &&
                            failed->role == "range" &&
                            failed->status == 206,
                        "publication is not bound to coordinator Range success");
            }
        }

        for (const ScienceTransportCompletion& completion : proof.completions)
        {
            if (completion.role != "range" ||
                !isExactFiveStatus(completion.status))
                continue;
            const auto requestKey =
                std::make_pair(completion.context, completion.request);
            if (completion.attempt < nextAttempt[requestKey])
            {
                require(rangeRetryByFailedAttempt[{
                            completion.context, completion.request,
                            completion.attempt}] == 1,
                        "retried exact-five Range completion lacks its "
                        "scheduled Range transition");
            }
        }

        std::map<std::string, int> fallbacks;
        std::map<std::string, int> cachePublications;
        std::map<std::string, int> propertyPublications;
        std::map<std::string, std::vector<const Decision*>> terminalBlocks;
        std::map<std::string, std::vector<const Decision*>> propertyBlocks;
        std::map<std::string, std::vector<const Decision*>> operationBlocks;
        for (const Decision& decision : decisions)
        {
            if (decision.kind == "fallback")
            {
                ++fallbacks[decision.context];
                ++proof.fallbackCount;
            }
            if (decision.kind == "published")
            {
                ++cachePublications[decision.context];
                ++proof.cachePublicationCount;
            }
            if (decision.kind == "property-published")
            {
                ++propertyPublications[decision.context];
                ++proof.propertyPublicationCount;
            }
            if (decision.kind == "head-blocked" ||
                decision.kind == "range-blocked")
                terminalBlocks[decision.context].push_back(&decision);
            if (decision.kind == "property-blocked")
                propertyBlocks[decision.context].push_back(&decision);
            if (decision.kind == "operation-blocked")
                operationBlocks[decision.context].push_back(&decision);
        }

        for (const std::string& context : contexts)
        {
            const bool hasAnyBlock = !terminalBlocks[context].empty() ||
                !propertyBlocks[context].empty() ||
                !operationBlocks[context].empty();
            if (!hasAnyBlock) continue;
            require(terminalBlocks[context].size() == 1 &&
                        propertyBlocks[context].size() == 1 &&
                        operationBlocks[context].size() == 1,
                    "terminal transport/property/operation block chain is "
                    "not exactly one-to-one");
            const Decision& terminal = *terminalBlocks[context].front();
            const Decision& property = *propertyBlocks[context].front();
            const Decision& operation = *operationBlocks[context].front();
            require(terminal.request == property.request &&
                        terminal.request == operation.request &&
                        terminal.attempt == property.attempt &&
                        terminal.attempt == operation.attempt &&
                        terminal.reason == property.reason &&
                        terminal.reason == operation.reason &&
                        cachePublications[context] == 0 &&
                        propertyPublications[context] == 0 &&
                        fallbacks[context] == 0,
                    "terminal block chain changed request/attempt/reason or "
                    "coexisted with publication/fallback");
        }

        proof.qualified = std::all_of(
            proof.completions.begin(), proof.completions.end(),
            [](const ScienceTransportCompletion& completion)
            {
                return completion.admissionValid;
            }) && contextConnectionsConsistent;
        for (const std::string& context : contexts)
        {
            int finalHeadStatus = 0;
            int successfulRangeCount = 0;
            std::size_t lastCompletion = 0;
            for (const ScienceTransportCompletion& completion : proof.completions)
            {
                if (completion.context != context) continue;
                lastCompletion = std::max(lastCompletion,
                                          completion.messageIndex);
                if (completion.scope == "coordinator" &&
                    completion.role == "head")
                    finalHeadStatus = completion.status;
                if (completion.scope == "coordinator" &&
                    completion.role == "range" && completion.status == 206)
                    ++successfulRangeCount;
            }
            for (const Decision& decision : decisions)
            {
                if (decision.context == context &&
                    (decision.kind == "published" ||
                     decision.kind == "property-published"))
                    require(decision.messageIndex > lastCompletion,
                            "science publication preceded a completion");
            }
            require(cachePublications[context] <= 1 &&
                        propertyPublications[context] <= 1,
                    "science publication event is duplicated");
            proof.qualified = proof.qualified && fallbacks[context] == 0 &&
                cachePublications[context] == 1 &&
                propertyPublications[context] == 1 &&
                terminalBlocks[context].empty() &&
                propertyBlocks[context].empty() &&
                operationBlocks[context].empty() &&
                finalHeadStatus == 200 && successfulRangeCount == 1;
        }
        proof.events = decisions;
        return proof;
    }

    HttpProof buildAttributedHttpProof(const DebugCapture& capture,
                                       const std::string& statsJson)
    {
        const AttributedTransportProof transport =
            buildAttributedTransportProof(capture);
        HttpProof proof;
        proof.headRetries = transport.headRetries;
        proof.scienceTransportEvents = transport.events;
        proof.scienceTransportCompletions = transport.completions;
        proof.scienceTransportResponseCount =
            static_cast<int>(transport.completions.size());
        proof.scienceTransportAttribution = "response-v1";
        proof.attributedTransportQualified = transport.qualified;
        std::uint64_t conservativeTransientBytes = 0;

        for (const ScienceTransportCompletion& completion : transport.completions)
        {
            proof.responseCodes.push_back(completion.status);
            if (completion.method == "HEAD")
            {
                ++proof.actualHeadCount;
                if (completion.status == 200)
                {
                    if (proof.sourceSize == 0)
                        proof.sourceSize = completion.declaredContentLength;
                    if (proof.sourceSize != completion.declaredContentLength)
                        proof.attributedTransportQualified = false;
                }
                continue;
            }

            ++proof.actualGetCount;
            checkedAdd(proof.actualHttpBodyBytes,
                       completion.actualBodyBytes,
                       "attributed actual HTTP body bytes");
            if (completion.status == 206)
            {
                if (completion.admissionValid)
                {
                    ++proof.successfulGetCount;
                    checkedAdd(proof.successfulRangeBytes,
                               completion.actualBodyBytes,
                               "attributed successful Range bytes");
                    proof.successfulByteIntervals.emplace_back(
                        static_cast<std::uint64_t>(completion.contentStart),
                        static_cast<std::uint64_t>(completion.contentEnd));
                    const std::uint64_t total = static_cast<std::uint64_t>(
                        completion.contentTotal);
                    if (proof.sourceSize == 0) proof.sourceSize = total;
                    if (proof.sourceSize != total)
                        proof.attributedTransportQualified = false;
                }
                continue;
            }

            if (!isExactFiveStatus(completion.status)) continue;
            ++proof.transientRetryCount;
            ++proof.transientRetryCodes[completion.status];
            checkedAdd(proof.declaredTransientBytes,
                       completion.declaredContentLength,
                       "attributed declared transient bytes");
            checkedAdd(conservativeTransientBytes,
                       std::max(completion.declaredContentLength,
                                completion.actualBodyBytes),
                       "attributed conservative transient bytes");
        }
        const auto completionForEvent = [&transport](
                const AttributedEventEvidence& event)
            -> const ScienceTransportCompletion&
        {
            for (const ScienceTransportCompletion& completion :
                 transport.completions)
            {
                if (completion.context == event.context &&
                    completion.request == event.request &&
                    completion.attempt == event.attempt)
                    return completion;
            }
            fail("attributed event lost its bound completion");
        };
        for (const AttributedEventEvidence& event : transport.events)
        {
            const ScienceTransportCompletion& completion =
                completionForEvent(event);
            if (event.kind == "coordinator-retry")
            {
                ++proof.coordinatorTransientRetryCount;
                checkedAdd(proof.coordinatorTransientRetryBytes, event.bytes,
                           "coordinator transient retry bytes");
                ++proof.coordinatorTransientRetryCodes[event.status];
            }
            else if (event.kind == "immediate-retry")
            {
                ++proof.immediateTransientRetryCount;
                checkedAdd(proof.immediateTransientRetryBytes, event.bytes,
                           "immediate transient retry bytes");
                ++proof.immediateTransientRetryCodes[event.status];
            }
            else if (event.kind == "fallback" &&
                     completion.role == "range" &&
                     isExactFiveStatus(completion.status))
            {
                ++proof.coordinatorTransientFallbackCount;
                checkedAdd(proof.coordinatorTransientFallbackBytes,
                           completion.actualBodyBytes,
                           "coordinator transient fallback bytes");
                ++proof.coordinatorTransientFallbackCodes[completion.status];
            }
            else if (event.kind == "logical-get")
            {
                ++proof.coordinatorLogicalGetCount;
                checkedAdd(proof.coordinatorLogicalGetBytes, event.bytes,
                           "coordinator logical GET bytes");
            }
        }
        for (const HeadRetryEvidence& retry : transport.headRetries)
        {
            ++proof.coordinatorHeadTransientRetryCount;
            checkedAdd(proof.coordinatorHeadTransientRetryDeclaredBytes,
                       retry.failedDeclaredContentLength,
                       "coordinator HEAD declared retry bytes");
            checkedAdd(proof.coordinatorHeadTransientRetryActualBodyBytes,
                       retry.failedActualBodyBytes,
                       "coordinator HEAD actual retry bytes");
            ++proof.coordinatorHeadTransientRetryCodes[retry.failedStatus];
        }
        proof.conservativeBodyUpperBound = proof.successfulRangeBytes;
        checkedAdd(proof.conservativeBodyUpperBound,
                   conservativeTransientBytes,
                   "attributed conservative body upper bound");

        picojson::value statsValue;
        const std::string statsError = picojson::parse(statsValue, statsJson);
        require(statsError.empty() && statsValue.is<picojson::object>(),
                "VSINetworkStats JSON is invalid");
        const picojson::object& methods = field(
            statsValue.get<picojson::object>(), "methods").get<picojson::object>();
        const picojson::object& get = field(methods, "GET").get<picojson::object>();
        const picojson::object& head = field(methods, "HEAD").get<picojson::object>();
        proof.statsGetOperationCount = static_cast<int>(checkedJsonUnsigned(
            field(get, "count"), "VSINetworkStats GET count",
            static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
        proof.statsHeadCount = static_cast<int>(checkedJsonUnsigned(
            field(head, "count"), "VSINetworkStats HEAD count",
            static_cast<std::uint64_t>(std::numeric_limits<int>::max())));
        const std::uint64_t statsBytes = checkedJsonUnsigned(
            field(get, "downloaded_bytes"),
            "VSINetworkStats downloaded bytes");
        require(proof.statsHeadCount == proof.actualHeadCount,
                "attributed HEAD count disagrees with VSINetworkStats");
        require(statsBytes == proof.actualHttpBodyBytes,
                "attributed GET bodies disagree with VSINetworkStats");
        require(proof.actualGetCount > 0,
                "attributed HTTP proof contains no physical GET");
        if (proof.attributedTransportQualified)
        {
            require(proof.sourceSize > 0 &&
                        proof.successfulRangeBytes > 0 &&
                        proof.successfulRangeBytes < proof.sourceSize,
                    "qualified attributed HTTP proof is empty or downloaded "
                    "the full source");
        }
        return proof;
    }

    HttpProof buildHttpProof(const DebugCapture& capture, const std::string& statsJson,
                             AttributionMode mode)
    {
        if (mode == AttributionMode::AttributedV6)
            return buildAttributedHttpProof(capture, statsJson);
        require(mode == AttributionMode::LegacyFrozen,
                "HTTP proof attribution mode is invalid");
        struct Request
        {
            std::string method;
            std::string uri;
            std::string range;
            std::size_t messageIndex = 0;
            std::chrono::steady_clock::time_point sent;
        };
        struct Response
        {
            int code = 0;
            std::map<std::string, std::string> headers;
            std::size_t completedIndex = 0;
        };
        struct CoordinatorFallback
        {
            std::string range;
            int code = 0;
            std::uint64_t bytes = 0;
        };
        struct TimedCoordinatorRetry
        {
            CoordinatorRetryEvidence evidence;
            std::size_t messageIndex = 0;
            std::chrono::steady_clock::time_point emitted;
        };
        struct TimedOrdinaryRetry
        {
            std::string range;
            int code = 0;
            std::size_t messageIndex = 0;
            std::chrono::steady_clock::time_point emitted;
        };
        struct TimedImmediateRetry
        {
            ImmediateRetryEvidence evidence;
            std::size_t messageIndex = 0;
            std::chrono::steady_clock::time_point emitted;
        };
        std::vector<Request> requests;
        std::vector<Response> responses;
        std::vector<TimedOrdinaryRetry> retryEvents;
        std::vector<TimedImmediateRetry> immediateRetries;
        std::vector<TimedCoordinatorRetry> coordinatorRetries;
        std::vector<CoordinatorFallback> coordinatorFallbacks;
        Response currentResponse;
        bool responseOpen = false;
        int logicalGetOperations = 0;
        HttpProof proof;
        proof.scienceTransportAttribution = "legacy-frozen";
        const bool hasCompleteTimestamps =
            capture.messages.size() == capture.timestamps.size();

        for (std::size_t messageIndex = 0;
             messageIndex < capture.messages.size(); ++messageIndex)
        {
            const std::string& message = capture.messages[messageIndex];
            const std::string outputPrefix = "CURL_INFO_HEADER_OUT: ";
            const std::string inputPrefix = "CURL_INFO_HEADER_IN: ";
            if (message.rfind(outputPrefix, 0) == 0)
            {
                std::istringstream stream(message.substr(outputPrefix.size()));
                std::string firstLine;
                std::getline(stream, firstLine);
                if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();
                std::istringstream firstLineStream(firstLine);
                Request request;
                firstLineStream >> request.method;
                firstLineStream >> request.uri;
                const auto headers = parseHeaders(stream);
                const auto range = headers.find("range");
                if (range != headers.end()) request.range = range->second;
                request.messageIndex = messageIndex;
                if (hasCompleteTimestamps)
                    request.sent = capture.timestamps[messageIndex];
                requests.push_back(request);
            }
            else if (message.rfind(inputPrefix, 0) == 0)
            {
                std::string line = message.substr(inputPrefix.size());
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("HTTP/", 0) == 0)
                {
                    if (responseOpen) responses.push_back(currentResponse);
                    currentResponse = Response();
                    responseOpen = true;
                    std::istringstream status(line);
                    std::string version;
                    status >> version >> currentResponse.code;
                }
                else if (line.empty())
                {
                    if (responseOpen)
                    {
                        currentResponse.completedIndex = messageIndex;
                        responses.push_back(currentResponse);
                    }
                    responseOpen = false;
                }
                else if (responseOpen)
                {
                    const std::size_t separator = line.find(':');
                    if (separator != std::string::npos)
                    {
                        std::string value = line.substr(separator + 1);
                        while (!value.empty() && value.front() == ' ')
                            value.erase(value.begin());
                        currentResponse.headers[lower(line.substr(0, separator))] = value;
                    }
                }
            }
            static const std::regex logicalGetPattern(
                R"(^VSICURL: ParallelHeadRange: logical-get-complete )"
                R"(bytes=([0-9]+)$)");
            std::smatch logicalGetMatch;
            if (std::regex_match(message, logicalGetMatch, logicalGetPattern))
            {
                ++proof.coordinatorLogicalGetCount;
                require(proof.coordinatorLogicalGetCount == 1,
                        "parallel metadata prefetch logical GET event is duplicated");
                proof.coordinatorLogicalGetBytes =
                    std::stoull(logicalGetMatch[1].str());
                ++logicalGetOperations;
            }
            else
            {
                require(message.find(
                            "ParallelHeadRange: logical-get-complete") ==
                            std::string::npos,
                        "parallel metadata prefetch logical GET event is malformed");
                if (message.rfind("VSICURL: Got response_code=206", 0) == 0 ||
                    message == "VSICURL: Download completed")
                {
                    ++logicalGetOperations;
                }
            }
            static const std::regex retryPattern(
                R"(HTTP error code for .* range ([0-9]+-[0-9]+): ([0-9]+)\. Retrying)");
            std::smatch retryMatch;
            if (std::regex_search(message, retryMatch, retryPattern))
            {
                retryEvents.push_back(
                    {"bytes=" + retryMatch[1].str(),
                     std::stoi(retryMatch[2].str()), messageIndex,
                     hasCompleteTimestamps
                         ? capture.timestamps[messageIndex]
                         : std::chrono::steady_clock::time_point()});
            }
            ImmediateRetryEvidence immediateRetry;
            if (parseImmediateRetryEvidence(message, immediateRetry))
            {
                immediateRetries.push_back(
                    {immediateRetry, messageIndex,
                     hasCompleteTimestamps
                         ? capture.timestamps[messageIndex]
                         : std::chrono::steady_clock::time_point()});
            }
            else
            {
                require(message.find("ReadMultiRange: immediate-retry") ==
                            std::string::npos,
                        "immediate multi-range retry event is malformed");
            }
            CoordinatorRetryEvidence coordinatorRetry;
            if (parseCoordinatorRetryEvidence(message, coordinatorRetry))
            {
                coordinatorRetries.push_back(
                    {coordinatorRetry, messageIndex,
                     hasCompleteTimestamps
                         ? capture.timestamps[messageIndex]
                         : std::chrono::steady_clock::time_point()});
            }
            else
            {
                require(message.find(
                            "ParallelHeadRange: transient-retry") ==
                            std::string::npos,
                        "coordinator transient retry event is malformed");
            }
            static const std::regex coordinatorFallbackPattern(
                R"(^VSICURL: ParallelHeadRange: transient-fallback )"
                R"(range=(bytes=[0-9]+-[0-9]+) status=([0-9]+) )"
                R"(bytes=([0-9]+)$)");
            std::smatch coordinatorFallbackMatch;
            if (std::regex_match(message, coordinatorFallbackMatch,
                                 coordinatorFallbackPattern))
            {
                CoordinatorFallback fallback;
                fallback.range = coordinatorFallbackMatch[1].str();
                fallback.code = std::stoi(coordinatorFallbackMatch[2].str());
                fallback.bytes = std::stoull(coordinatorFallbackMatch[3].str());
                coordinatorFallbacks.push_back(fallback);
                require(coordinatorFallbacks.size() == 1,
                        "coordinator transient fallback event is duplicated");
            }
            else
            {
                require(message.find(
                            "ParallelHeadRange: transient-fallback") ==
                            std::string::npos,
                        "coordinator transient fallback event is malformed");
            }
        }
        if (responseOpen) responses.push_back(currentResponse);

        const std::set<int> transientCodes = {429, 500, 502, 503, 504};
        std::map<std::string, int> requestedRanges;
        std::vector<const Request*> coordinatorRangeRequests;
        int connectRequests = 0;
        for (const Request& request : requests)
        {
            if (request.method == "CONNECT")
            {
                require(request.range.empty(), "CONNECT unexpectedly carried a Range header");
                ++connectRequests;
                continue;
            }
            if (request.method == "HEAD")
            {
                ++proof.actualHeadCount;
                require(request.range.empty(), "HEAD unexpectedly carried a Range header");
                continue;
            }
            require(request.method == "GET", "unexpected HTTP method in curl evidence");
            ++proof.actualGetCount;
            require(!request.range.empty(), "GET omitted the Range header");
            require(request.range.find(',') == std::string::npos,
                    "GET emitted comma-separated multi-range syntax");
            require(request.range.rfind("bytes=", 0) == 0,
                    "GET Range syntax is not bytes=start-end");
            ++requestedRanges[request.range];
            if (request.range == "bytes=0-131071")
                coordinatorRangeRequests.push_back(&request);
        }

        std::map<std::string, int> successfulRanges;
        std::map<int, int> transientResponses;
        std::map<std::pair<int, std::uint64_t>, int> transientResponseBodies;
        int headResponses = 0;
        int connectResponses = 0;
        for (const Response& response : responses)
        {
            proof.responseCodes.push_back(response.code);
            const auto contentRange = response.headers.find("content-range");
            const auto contentLength = response.headers.find("content-length");
            if (transientCodes.count(response.code) != 0)
            {
                require(contentRange == response.headers.end(),
                        "transient response unexpectedly carried Content-Range");
                require(contentLength != response.headers.end(),
                        "transient response omitted Content-Length body accounting");
                const std::uint64_t bodyBytes =
                    std::stoull(contentLength->second);
                proof.declaredTransientBytes += bodyBytes;
                ++transientResponses[response.code];
                ++transientResponseBodies[{response.code, bodyBytes}];
                continue;
            }
            if (contentRange == response.headers.end())
            {
                require(response.code == 200,
                        "HTTP response was neither 200, 206, nor an allowed transient code");
                if (contentLength == response.headers.end())
                {
                    ++connectResponses;
                }
                else
                {
                    ++headResponses;
                    const std::uint64_t size = std::stoull(contentLength->second);
                    if (proof.sourceSize == 0) proof.sourceSize = size;
                    require(proof.sourceSize == size, "HEAD source size changed");
                }
                continue;
            }
            require(response.code == 206, "Content-Range response was not HTTP 206");
            require(contentRange != response.headers.end() &&
                    contentLength != response.headers.end(),
                    "successful ranged GET omitted Content-Range or Content-Length");
            const std::string& value = contentRange->second;
            const std::size_t space = value.find(' ');
            const std::size_t slash = value.find('/');
            require(space != std::string::npos && slash != std::string::npos && slash > space,
                    "Content-Range syntax is invalid");
            const std::string range = "bytes=" + value.substr(space + 1, slash - space - 1);
            require(requestedRanges.count(range) != 0,
                    "Content-Range does not match any emitted GET Range");
            const std::uint64_t responseSize = std::stoull(value.substr(slash + 1));
            if (proof.sourceSize == 0) proof.sourceSize = responseSize;
            require(proof.sourceSize == responseSize, "Content-Range source size changed");
            const std::uint64_t bodyBytes = std::stoull(contentLength->second);
            const std::string interval = range.substr(6);
            const std::size_t dash = interval.find('-');
            require(dash != std::string::npos,
                    "matched Range is missing its interval separator");
            const std::uint64_t start = std::stoull(interval.substr(0, dash));
            const std::uint64_t end = std::stoull(interval.substr(dash + 1));
            require(end >= start && bodyBytes == end - start + 1,
                    "Content-Length does not match Content-Range");
            require(bodyBytes < proof.sourceSize,
                    "ranged GET returned the complete source object");
            proof.successfulRangeBytes += bodyBytes;
            ++proof.successfulGetCount;
            proof.successfulByteIntervals.emplace_back(start, end);
            ++successfulRanges[range];
        }
        require(headResponses == proof.actualHeadCount,
                "HEAD request/response count mismatch (possible GET 200)");
        require(connectResponses == connectRequests,
                "proxy CONNECT request/response count mismatch (possible GET 200)");

        std::map<std::string, int> retriesByRange;
        std::map<int, int> retryCodes;
        for (const auto& retry : retryEvents)
        {
            require(transientCodes.count(retry.code) != 0,
                    "CPL retry event used an unlisted transient response code");
            ++retriesByRange[retry.range];
            ++retryCodes[retry.code];
            require(retriesByRange[retry.range] <= 3,
                    "ranged GET exceeded the three-retry policy");
            require(requestedRanges.count(retry.range) != 0,
                    "CPL retry event names a Range that was never emitted");
        }
        require(immediateRetries.empty() || hasCompleteTimestamps,
                "immediate retry proof is missing steady-clock timestamps");
        std::vector<bool> consumedOrdinaryRetries(retryEvents.size(), false);
        std::map<std::string, int> immediateAttemptsByRange;
        std::map<std::string, long long> immediateConnectionsByRange;
        std::map<std::pair<int, std::uint64_t>, int> immediateResponseBodies;
        for (const TimedImmediateRetry& timedImmediate : immediateRetries)
        {
            const ImmediateRetryEvidence& immediate = timedImmediate.evidence;
            require(transientCodes.count(immediate.code) != 0,
                    "immediate retry used an unlisted transient response code");
            require(immediate.attempt >= 1 && immediate.attempt <= 3 &&
                        immediate.attempt ==
                            ++immediateAttemptsByRange[immediate.range],
                    "immediate retry attempts are not contiguous in 1..3");
            require(isExpectedCoordinatorRetryDelay(
                        immediate.attempt, immediate.delayMs),
                    "immediate retry delay is outside the native envelope");
            require(immediate.connectionId >= 0 && immediate.httpMajor == 2,
                    "immediate retry requires a nonnegative HTTP/2 connection");
            const auto previousConnection =
                immediateConnectionsByRange.find(immediate.range);
            if (previousConnection == immediateConnectionsByRange.end())
                immediateConnectionsByRange[immediate.range] =
                    immediate.connectionId;
            else
                require(previousConnection->second == immediate.connectionId,
                        "immediate retries changed connection");

            int ordinaryIndex = -1;
            for (std::size_t index = 0; index < retryEvents.size(); ++index)
            {
                const TimedOrdinaryRetry& ordinary = retryEvents[index];
                if (!consumedOrdinaryRetries[index] &&
                    ordinary.range == immediate.range &&
                    ordinary.code == immediate.code &&
                    ordinary.messageIndex < timedImmediate.messageIndex)
                {
                    require(ordinaryIndex < 0,
                            "immediate retry matches multiple ordinary events");
                    ordinaryIndex = static_cast<int>(index);
                }
            }
            require(ordinaryIndex >= 0,
                    "immediate retry has no matching ordinary retry event");
            consumedOrdinaryRetries[static_cast<std::size_t>(ordinaryIndex)] = true;
            const TimedOrdinaryRetry& ordinary =
                retryEvents[static_cast<std::size_t>(ordinaryIndex)];

            const Request* failedRequest = nullptr;
            const Request* nextRequest = nullptr;
            for (const Request& request : requests)
            {
                if (request.range != immediate.range) continue;
                if (request.messageIndex < ordinary.messageIndex)
                    failedRequest = &request;
                else if (request.messageIndex > timedImmediate.messageIndex &&
                         nextRequest == nullptr)
                    nextRequest = &request;
            }
            require(failedRequest != nullptr && nextRequest != nullptr,
                    "immediate retry lacks its failed or next exact Range request");
            require(failedRequest->messageIndex < ordinary.messageIndex &&
                        ordinary.messageIndex < timedImmediate.messageIndex &&
                        timedImmediate.messageIndex < nextRequest->messageIndex,
                    "immediate retry chronology is ambiguous");
            const auto earliestRetry = timedImmediate.emitted +
                std::chrono::milliseconds(immediate.delayMs) -
                COORDINATOR_RETRY_CHRONOLOGY_ROUNDING_TOLERANCE;
            require(nextRequest->sent >= earliestRetry,
                    "immediate retry request preceded its declared delay");

            ++immediateResponseBodies[{immediate.code, immediate.bytes}];
            ++proof.immediateTransientRetryCount;
            proof.immediateTransientRetryBytes += immediate.bytes;
            ++proof.immediateTransientRetryCodes[immediate.code];
            proof.immediateRetries.push_back(immediate);
        }
        require(coordinatorRetries.size() <= 3,
                "coordinator Range exceeded the three-retry policy");
        require(coordinatorRetries.empty() || hasCompleteTimestamps,
                "coordinator retry proof is missing steady-clock timestamps");
        std::vector<bool> consumedTransientResponses(responses.size(), false);
        std::map<std::string, int> coordinatorRetriesByRange;
        for (std::size_t index = 0; index < coordinatorRetries.size(); ++index)
        {
            const TimedCoordinatorRetry& timedRetry = coordinatorRetries[index];
            const CoordinatorRetryEvidence& retry = timedRetry.evidence;
            require(retry.range == "bytes=0-131071",
                    "coordinator retry did not name the exact initial Range");
            require(transientCodes.count(retry.code) != 0,
                    "coordinator retry used an unlisted transient response code");
            require(retry.attempt >= 1 && retry.attempt <= 3,
                    "coordinator retry attempt must be in 1..3");
            require(retry.attempt == static_cast<int>(index + 1),
                    "coordinator retry attempts are not contiguous");
            const auto expectedDelayMs =
                expectedCoordinatorRetryDelayEnvelopeMs(retry.attempt);
            require(isExpectedCoordinatorRetryDelay(retry.attempt, retry.delayMs),
                    "coordinator retry delay is outside the configured native "
                    "schedule: "
                    "attempt=" + std::to_string(retry.attempt) +
                    " actual=" + std::to_string(retry.delayMs) +
                    " expected=" + std::to_string(expectedDelayMs.first) +
                    ".." + std::to_string(expectedDelayMs.second));
            require(retry.connectionId >= 0,
                    "coordinator retry connection must be nonnegative");
            require(retry.httpMajor == 2,
                    "coordinator retry requires HTTP/2");
            if (index > 0)
            {
                require(retry.connectionId ==
                            coordinatorRetries.front().evidence.connectionId,
                        "coordinator retries changed connection");
            }
            require(coordinatorRangeRequests.size() > index + 1,
                    "coordinator retry has no following exact Range request");
            const Request& failedRequest = *coordinatorRangeRequests[index];
            const Request& nextRequest = *coordinatorRangeRequests[index + 1];
            require(failedRequest.messageIndex < timedRetry.messageIndex &&
                        timedRetry.messageIndex < nextRequest.messageIndex,
                    "coordinator retry event is outside its request chronology");
            const auto earliestRetryRequest = timedRetry.emitted +
                std::chrono::milliseconds(retry.delayMs) -
                COORDINATOR_RETRY_CHRONOLOGY_ROUNDING_TOLERANCE;
            require(nextRequest.sent >= earliestRetryRequest,
                    "coordinator retry request preceded its declared delay");

            int matchingResponse = -1;
            for (std::size_t responseIndex = 0;
                 responseIndex < responses.size(); ++responseIndex)
            {
                if (consumedTransientResponses[responseIndex])
                    continue;
                const Response& response = responses[responseIndex];
                const auto contentLength =
                    response.headers.find("content-length");
                if (response.code != retry.code ||
                    contentLength == response.headers.end() ||
                    std::stoull(contentLength->second) != retry.bytes ||
                    response.completedIndex <= failedRequest.messageIndex ||
                    response.completedIndex >= timedRetry.messageIndex)
                {
                    continue;
                }
                require(matchingResponse < 0,
                        "coordinator retry matches multiple transient responses");
                matchingResponse = static_cast<int>(responseIndex);
            }
            require(matchingResponse >= 0,
                    "coordinator retry has no matching transient HTTP response");
            consumedTransientResponses[matchingResponse] = true;

            auto responseBody = transientResponseBodies.find(
                {retry.code, retry.bytes});
            require(responseBody != transientResponseBodies.end() &&
                        responseBody->second > 0,
                    "coordinator retry transient body accounting underflowed");
            --responseBody->second;
            auto responseCode = transientResponses.find(retry.code);
            require(responseCode != transientResponses.end() &&
                        responseCode->second > 0,
                    "coordinator retry response code accounting underflowed");
            if (--responseCode->second == 0)
                transientResponses.erase(responseCode);

            ++coordinatorRetriesByRange[retry.range];
            ++proof.coordinatorTransientRetryCount;
            proof.coordinatorTransientRetryBytes += retry.bytes;
            ++proof.coordinatorTransientRetryCodes[retry.code];
        }
        std::map<std::string, int> coordinatorFallbacksByRange;
        for (const CoordinatorFallback& fallback : coordinatorFallbacks)
        {
            require(fallback.range == "bytes=0-131071" &&
                    requestedRanges.count(fallback.range) != 0,
                    "coordinator transient fallback names an unknown Range");
            require(transientCodes.count(fallback.code) != 0,
                    "coordinator fallback used an unlisted transient response code");
            auto response = transientResponseBodies.find(
                {fallback.code, fallback.bytes});
            require(response != transientResponseBodies.end() &&
                    response->second > 0,
                    "coordinator fallback has no matching transient HTTP response");
            --response->second;
            auto responseCode = transientResponses.find(fallback.code);
            require(responseCode != transientResponses.end() &&
                    responseCode->second > 0,
                    "coordinator fallback response code accounting underflowed");
            if (--responseCode->second == 0)
                transientResponses.erase(responseCode);
            ++coordinatorFallbacksByRange[fallback.range];
            ++proof.coordinatorTransientFallbackCount;
            proof.coordinatorTransientFallbackBytes += fallback.bytes;
            ++proof.coordinatorTransientFallbackCodes[fallback.code];
        }
        if (!immediateRetries.empty())
        {
            require(immediateRetries.size() == retryEvents.size() &&
                        std::all_of(consumedOrdinaryRetries.begin(),
                                    consumedOrdinaryRetries.end(),
                                    [](bool consumed) { return consumed; }),
                    "immediate events do not cover every ordinary retry event");
            std::map<std::pair<int, std::uint64_t>, int>
                residualTransientResponseBodies;
            int residualTransientResponseCount = 0;
            for (const auto& response : transientResponseBodies)
            {
                if (response.second <= 0) continue;
                residualTransientResponseBodies[response.first] =
                    response.second;
                residualTransientResponseCount += response.second;
            }
            require(residualTransientResponseCount ==
                        static_cast<int>(immediateRetries.size()) &&
                        immediateResponseBodies ==
                            residualTransientResponseBodies &&
                        proof.immediateTransientRetryCount ==
                            static_cast<int>(retryEvents.size()) &&
                        proof.immediateTransientRetryCodes == retryCodes,
                    "immediate retry events do not reconcile with residual "
                    "ordinary transient response bodies");
        }
        require(retryCodes == transientResponses,
                "transient HTTP responses do not reconcile with CPL retry events");
        for (const auto& request : requestedRanges)
        {
            const int successful = successfulRanges[request.first];
            const int retries = retriesByRange[request.first];
            const int coordinatorRetryCount =
                coordinatorRetriesByRange[request.first];
            const int coordinatorFallbackCount =
                coordinatorFallbacksByRange[request.first];
            require(successful > 0 || coordinatorFallbackCount == 1,
                    "emitted GET Range had neither HTTP 206 nor terminal fallback");
            require(request.second == successful + retries +
                        coordinatorRetryCount +
                        coordinatorFallbackCount,
                    "emitted GET Range count does not reconcile with retries, "
                    "coordinator retries, coordinator fallbacks, and HTTP 206");
        }
        require(proof.actualGetCount ==
                    proof.successfulGetCount +
                        static_cast<int>(retryEvents.size()) +
                        proof.coordinatorTransientRetryCount +
                        proof.coordinatorTransientFallbackCount,
                "GET request count does not reconcile with successes, retries, "
                "coordinator retries, and coordinator fallbacks");
        proof.transientRetryCount = static_cast<int>(retryEvents.size());
        proof.transientRetryCodes = retryCodes;
        proof.actualHttpBodyBytes = proof.successfulRangeBytes +
            proof.coordinatorTransientRetryBytes +
            proof.coordinatorTransientFallbackBytes;
        proof.conservativeBodyUpperBound =
            proof.successfulRangeBytes + proof.declaredTransientBytes;
        require(proof.actualGetCount > 0 && proof.sourceSize > 0 &&
                    ((proof.successfulRangeBytes > 0 &&
                      proof.successfulRangeBytes < proof.sourceSize) ||
                     (proof.successfulRangeBytes == 0 &&
                      proof.coordinatorTransientFallbackCount == 1)),
                "HTTP proof is empty, equals the complete object, or lacks a terminal fallback");

        picojson::value statsValue;
        const std::string statsError = picojson::parse(statsValue, statsJson);
        require(statsError.empty() && statsValue.is<picojson::object>(),
                "VSINetworkStats JSON is invalid");
        const picojson::object& statsRoot = statsValue.get<picojson::object>();
        const picojson::object& methods =
            field(statsRoot, "methods").get<picojson::object>();
        const picojson::object& get = field(methods, "GET").get<picojson::object>();
        const picojson::object& head = field(methods, "HEAD").get<picojson::object>();
        proof.statsGetOperationCount =
            static_cast<int>(field(get, "count").get<double>());
        proof.statsHeadCount = static_cast<int>(field(head, "count").get<double>());
        const std::uint64_t statsBytes = static_cast<std::uint64_t>(
            field(get, "downloaded_bytes").get<double>());
        require(statsBytes == proof.successfulRangeBytes +
                    proof.coordinatorTransientRetryBytes +
                    proof.coordinatorTransientFallbackBytes,
                "VSINetworkStats downloaded bytes disagree with HTTP response bodies");
        require(proof.statsHeadCount == proof.actualHeadCount,
                "VSINetworkStats HEAD count disagrees with curl headers");
        require(proof.statsGetOperationCount == logicalGetOperations,
                "VSINetworkStats GET operations disagree with CPL read operations");
        if (proof.coordinatorLogicalGetCount == 1)
        {
            if (proof.coordinatorTransientFallbackCount == 1)
            {
                require(proof.coordinatorLogicalGetBytes ==
                            proof.coordinatorTransientFallbackBytes,
                        "parallel metadata prefetch logical GET bytes disagree "
                        "with the coordinator fallback");
            }
            else
            {
                require(!proof.successfulByteIntervals.empty(),
                        "parallel metadata prefetch logical GET omitted its interval");
                const auto& interval = proof.successfulByteIntervals.front();
                require(proof.coordinatorLogicalGetBytes ==
                            interval.second - interval.first + 1,
                        "parallel metadata prefetch logical GET bytes disagree "
                        "with the first successful interval");
            }
        }
        return proof;
    }

    int parseHttpVersion(const std::string& token)
    {
        require(token.rfind("HTTP/", 0) == 0,
                "metadata prefetch evidence omitted the HTTP version");
        const std::string version = token.substr(5);
        if (version == "2" || version == "2.0") return 2;
        if (version == "1.1" || version == "1.0") return 1;
        fail("metadata prefetch evidence used an unknown HTTP version");
    }

    MetadataPrefetchProof buildMetadataPrefetchProof(
        const DebugCapture& capture, const HttpProof& httpProof)
    {
        if (httpProof.scienceTransportAttribution == "response-v1")
        {
            MetadataPrefetchProof proof;
            proof.enabled = true;
            proof.attributedTransportQualified =
                httpProof.attributedTransportQualified;
            require(proof.attributedTransportQualified,
                    "attributed metadata prefetch transport is not qualified");
            long long connection = -1;
            for (const ScienceTransportCompletion& completion :
                 httpProof.scienceTransportCompletions)
            {
                if (completion.scope != "coordinator") continue;
                if (connection < 0) connection = completion.connectionId;
                require(completion.connectionId == connection &&
                            completion.httpMajor == 2,
                        "attributed metadata prefetch changed HTTP/2 connection");
                if (completion.role == "head")
                {
                    ++proof.headRequestCount;
                    proof.headHttpVersion = completion.httpMajor;
                }
                if (completion.role == "range")
                {
                    ++proof.rangeRequestCount;
                    proof.rangeHttpVersion = completion.httpMajor;
                }
            }
            proof.rangeStart = 0;
            proof.rangeEnd = 131071;
            proof.sharedConnection = connection >= 0;
            std::size_t firstHeadRequest = capture.messages.size();
            std::size_t firstRangeRequest = capture.messages.size();
            std::size_t firstCompletion = capture.messages.size();
            int publications = 0;
            int fallbacks = 0;
            for (std::size_t index = 0; index < capture.messages.size(); ++index)
            {
                const std::string& message = capture.messages[index];
                if (firstHeadRequest == capture.messages.size() &&
                    message.rfind("CURL_INFO_HEADER_OUT: HEAD ", 0) == 0)
                    firstHeadRequest = index;
                if (firstRangeRequest == capture.messages.size() &&
                    message.rfind("CURL_INFO_HEADER_OUT: GET ", 0) == 0 &&
                    message.find("Range: bytes=0-131071") != std::string::npos)
                    firstRangeRequest = index;
                if (firstCompletion == capture.messages.size() &&
                    message.find("ScienceTransport: response-v1") !=
                        std::string::npos)
                    firstCompletion = index;
                if (message.find(
                        "ParallelHeadRange: published context=") !=
                    std::string::npos)
                    ++publications;
                if (message.find("ParallelHeadRange: fallback context=") !=
                    std::string::npos)
                    ++fallbacks;
            }
            require(fallbacks == 0 && publications == 1,
                    "attributed metadata prefetch contains fallback/publication error");
            proof.requestsOverlapped = firstHeadRequest < firstCompletion &&
                firstRangeRequest < firstCompletion;
            require(proof.headRequestCount >= 1 && proof.headRequestCount <= 4 &&
                        proof.rangeRequestCount >= 1 &&
                        proof.sharedConnection && proof.requestsOverlapped &&
                        !httpProof.successfulByteIntervals.empty() &&
                        httpProof.successfulByteIntervals.front() ==
                            std::make_pair<std::uint64_t, std::uint64_t>(
                                0, 131071),
                    "attributed metadata prefetch proof is incomplete");
            proof.cachePublished = true;
            for (const AttributedEventEvidence& event :
                 httpProof.scienceTransportEvents)
            {
                if (event.kind != "coordinator-retry")
                    continue;
                CoordinatorRetryEvidence retry;
                retry.range = event.range;
                retry.code = event.status;
                retry.bytes = event.bytes;
                retry.attempt = event.attempt;
                retry.delayMs = event.delayMs;
                retry.connectionId = event.connectionId;
                retry.httpMajor = event.httpMajor;
                proof.coordinatorRetries.push_back(retry);
            }
            return proof;
        }
        struct RequestEvidence
        {
            std::string method;
            std::string uri;
            std::string range;
            int httpVersion = 0;
            int streamId = -1;
            bool multiplexReuse = false;
            std::chrono::steady_clock::time_point sent;
        };
        struct ResponseEvidence
        {
            int code = 0;
            int httpVersion = 0;
            std::map<std::string, std::string> headers;
            std::chrono::steady_clock::time_point completed;
        };
        struct TimedCoordinatorRetry
        {
            CoordinatorRetryEvidence evidence;
            std::chrono::steady_clock::time_point emitted;
        };

        require(capture.messages.size() == capture.timestamps.size(),
                "metadata prefetch evidence is missing steady-clock timestamps");
        std::vector<RequestEvidence> requests;
        std::vector<ResponseEvidence> responses;
        int activeResponse = -1;
        int pendingStreamId = -1;
        bool pendingMultiplexReuse = false;
        int publicationCount = 0;
        int rangeDetachSuccessCount = 0;
        int headDetachSuccessCount = 0;
        int transportEventCount = 0;
        std::chrono::steady_clock::time_point rangeDetachSuccess;
        std::chrono::steady_clock::time_point headDetachSuccess;
        std::chrono::steady_clock::time_point transportEvent;
        long long headConnectionId = -1;
        long long rangeConnectionId = -1;
        int headTransportHttp = 0;
        int rangeTransportHttp = 0;
        std::vector<std::string> fallbackReasons;
        std::vector<TimedCoordinatorRetry> coordinatorRetries;
        static const std::regex transportPattern(
            R"(^VSICURL: ParallelHeadRange: transport )"
            R"(head-connection=(-?[0-9]+) range-connection=(-?[0-9]+) )"
            R"(head-http=(-?[0-9]+) range-http=(-?[0-9]+)$)");
        static const std::regex streamPattern(
            R"(\[HTTP/2\] \[([0-9]+)\] OPENED stream)");

        for (std::size_t index = 0; index < capture.messages.size(); ++index)
        {
            const std::string& message = capture.messages[index];
            const std::string outputPrefix = "CURL_INFO_HEADER_OUT: ";
            const std::string inputPrefix = "CURL_INFO_HEADER_IN: ";
            if (message.rfind(outputPrefix, 0) == 0)
            {
                std::istringstream stream(message.substr(outputPrefix.size()));
                std::string firstLine;
                std::getline(stream, firstLine);
                if (!firstLine.empty() && firstLine.back() == '\r') firstLine.pop_back();
                std::istringstream firstLineStream(firstLine);
                RequestEvidence request;
                std::string version;
                firstLineStream >> request.method >> request.uri >> version;
                request.httpVersion = parseHttpVersion(version);
                request.streamId = pendingStreamId;
                request.multiplexReuse = pendingMultiplexReuse;
                const auto headers = parseHeaders(stream);
                const auto range = headers.find("range");
                if (range != headers.end()) request.range = range->second;
                request.sent = capture.timestamps[index];
                requests.push_back(request);
                pendingStreamId = -1;
                pendingMultiplexReuse = false;
            }
            else if (message.rfind(inputPrefix, 0) == 0)
            {
                std::string line = message.substr(inputPrefix.size());
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.rfind("HTTP/", 0) == 0)
                {
                    require(activeResponse < 0,
                            "metadata prefetch response chronology is ambiguous");
                    std::istringstream status(line);
                    std::string version;
                    ResponseEvidence response;
                    status >> version >> response.code;
                    response.httpVersion = parseHttpVersion(version);
                    responses.push_back(response);
                    activeResponse = static_cast<int>(responses.size() - 1);
                }
                else if (line.empty())
                {
                    require(activeResponse >= 0,
                            "metadata prefetch response ended without a status");
                    ResponseEvidence& response = responses[activeResponse];
                    response.completed = capture.timestamps[index];
                    activeResponse = -1;
                }
                else
                {
                    require(activeResponse >= 0,
                            "metadata prefetch response header preceded its status");
                    const std::size_t separator = line.find(':');
                    require(separator != std::string::npos,
                            "metadata prefetch response header is malformed");
                    std::string value = line.substr(separator + 1);
                    while (!value.empty() && value.front() == ' ') value.erase(value.begin());
                    responses[activeResponse].headers[lower(line.substr(0, separator))] =
                        value;
                }
            }
            else
            {
                std::smatch streamMatch;
                if (std::regex_search(message, streamMatch, streamPattern))
                    pendingStreamId = std::stoi(streamMatch[1].str());
                if (message.find("Multiplexed connection found") !=
                    std::string::npos)
                {
                    pendingMultiplexReuse = true;
                }
                if (message ==
                    "VSICURL: ParallelHeadRange: detach-success=range")
                {
                    ++rangeDetachSuccessCount;
                    rangeDetachSuccess = capture.timestamps[index];
                }
                if (message ==
                    "VSICURL: ParallelHeadRange: detach-success=head")
                {
                    ++headDetachSuccessCount;
                    headDetachSuccess = capture.timestamps[index];
                }
                CoordinatorRetryEvidence coordinatorRetry;
                if (parseCoordinatorRetryEvidence(message, coordinatorRetry))
                {
                    coordinatorRetries.push_back(
                        {coordinatorRetry, capture.timestamps[index]});
                }
                else
                {
                    require(message.find(
                                "ParallelHeadRange: transient-retry") ==
                                std::string::npos,
                            "metadata prefetch coordinator retry event is malformed");
                }
                std::smatch transportMatch;
                if (std::regex_match(message, transportMatch, transportPattern))
                {
                    ++transportEventCount;
                    require(transportEventCount == 1,
                            "metadata prefetch transport event is duplicated");
                    require(rangeDetachSuccessCount == 1 &&
                            headDetachSuccessCount == 1,
                            "metadata prefetch transport preceded successful detaches");
                    transportEvent = capture.timestamps[index];
                    headConnectionId = std::stoll(transportMatch[1].str());
                    rangeConnectionId = std::stoll(transportMatch[2].str());
                    headTransportHttp = std::stoi(transportMatch[3].str());
                    rangeTransportHttp = std::stoi(transportMatch[4].str());
                }
                else
                    require(message.find("ParallelHeadRange: transport") ==
                                std::string::npos,
                            "metadata prefetch transport event is malformed");
                if (message.find("ParallelHeadRange: published") != std::string::npos)
                    ++publicationCount;
                const std::string fallbackPrefix = "ParallelHeadRange: fallback=";
                const std::size_t fallback = message.find(fallbackPrefix);
                if (fallback != std::string::npos)
                    fallbackReasons.push_back(message.substr(
                        fallback + fallbackPrefix.size()));
            }
        }
        require(activeResponse < 0,
                "metadata prefetch response evidence is incomplete");
        require(httpProof.coordinatorTransientFallbackCount == 0 &&
                fallbackReasons.empty(),
                "metadata prefetch formal proof contains a fallback");

        const RequestEvidence* headRequest = nullptr;
        const RequestEvidence* rangeRequest = nullptr;
        std::vector<const RequestEvidence*> rangeRequests;
        int headRequestCount = 0;
        int rangeRequestCount = 0;
        bool sawGet = false;
        for (const RequestEvidence& request : requests)
        {
            if (request.method == "CONNECT") continue;
            if (request.method == "HEAD")
            {
                ++headRequestCount;
                if (!headRequest) headRequest = &request;
                continue;
            }
            if (request.method == "GET")
            {
                if (!sawGet)
                {
                    require(request.range == "bytes=0-131071",
                            "metadata prefetch was not the first GET");
                    sawGet = true;
                }
                if (request.range == "bytes=0-131071")
                {
                    ++rangeRequestCount;
                    rangeRequests.push_back(&request);
                    if (!rangeRequest) rangeRequest = &request;
                }
            }
        }
        require(headRequestCount == 1 && headRequest != nullptr &&
                httpProof.actualHeadCount == 1,
                "metadata prefetch requires exactly one HEAD request");
        require(rangeRequestCount ==
                    1 + httpProof.coordinatorTransientRetryCount &&
                    rangeRequest != nullptr,
                "metadata prefetch Range requests do not reconcile with "
                "coordinator retries");
        require(coordinatorRetries.size() ==
                    static_cast<std::size_t>(
                        httpProof.coordinatorTransientRetryCount) &&
                    coordinatorRetries.size() <= 3,
                "metadata prefetch coordinator retry evidence count changed");
        std::uint64_t coordinatorRetryBytes = 0;
        std::map<int, int> coordinatorRetryCodes;
        for (std::size_t index = 0; index < coordinatorRetries.size(); ++index)
        {
            const CoordinatorRetryEvidence& retry =
                coordinatorRetries[index].evidence;
            require(retry.attempt == static_cast<int>(index + 1) &&
                        isExpectedCoordinatorRetryDelay(
                            retry.attempt, retry.delayMs) &&
                        retry.httpMajor == 2 &&
                        retry.connectionId >= 0 &&
                        retry.range == "bytes=0-131071",
                    "metadata prefetch coordinator retry evidence is invalid");
            coordinatorRetryBytes += retry.bytes;
            ++coordinatorRetryCodes[retry.code];
        }
        require(coordinatorRetryBytes ==
                    httpProof.coordinatorTransientRetryBytes &&
                    coordinatorRetryCodes ==
                    httpProof.coordinatorTransientRetryCodes,
                "metadata prefetch coordinator retry accounting changed");
        require(!httpProof.successfulByteIntervals.empty() &&
                httpProof.successfulByteIntervals.front() ==
                    std::make_pair<std::uint64_t, std::uint64_t>(0, 131071),
                "metadata prefetch HTTP proof omitted the exact initial interval");

        const ResponseEvidence* headResponse = nullptr;
        const ResponseEvidence* rangeResponse = nullptr;
        int headResponseCount = 0;
        int rangeResponseCount = 0;
        for (const ResponseEvidence& response : responses)
        {
            const auto contentRange = response.headers.find("content-range");
            if (response.code == 200 && contentRange == response.headers.end() &&
                response.headers.count("content-length") != 0)
            {
                ++headResponseCount;
                headResponse = &response;
            }
            if (response.code == 206 && contentRange != response.headers.end() &&
                contentRange->second.rfind("bytes 0-131071/", 0) == 0)
            {
                ++rangeResponseCount;
                rangeResponse = &response;
            }
        }
        require(headResponseCount == 1 && rangeResponseCount == 1 &&
                headResponse != nullptr && rangeResponse != nullptr,
                "metadata prefetch requires one HEAD 200 and one exact Range 206");
        require(headRequest->httpVersion == 2 && rangeRequest->httpVersion == 2 &&
                headResponse->httpVersion == 2 && rangeResponse->httpVersion == 2,
                "metadata prefetch requires HTTP/2 for HEAD and Range");
        require(httpProof.coordinatorLogicalGetCount == 1,
                "metadata prefetch requires exactly one logical GET event");
        require(rangeDetachSuccessCount == 1 && headDetachSuccessCount == 1 &&
                transportEvent > rangeDetachSuccess &&
                transportEvent > headDetachSuccess,
                "metadata prefetch transport preceded successful detaches");
        require(transportEventCount == 1 && headConnectionId >= 0 &&
                headConnectionId == rangeConnectionId &&
                headTransportHttp == 2 && rangeTransportHttp == 2,
                "metadata prefetch lacks authoritative shared HTTP/2 transport evidence");
        require(headRequest->streamId > 0 && rangeRequest->streamId > 0 &&
                rangeRequest->streamId > headRequest->streamId &&
                headRequest->uri == rangeRequest->uri &&
                rangeRequest->multiplexReuse &&
                headConnectionId == rangeConnectionId,
                "metadata prefetch lacks unambiguous shared HTTP/2 connection evidence");
        for (std::size_t index = 0; index < coordinatorRetries.size(); ++index)
        {
            const RequestEvidence& previousRequest = *rangeRequests[index];
            const RequestEvidence& retryRequest = *rangeRequests[index + 1];
            const TimedCoordinatorRetry& retry = coordinatorRetries[index];
            const auto earliestRetryRequest = retry.emitted +
                std::chrono::milliseconds(retry.evidence.delayMs) -
                COORDINATOR_RETRY_CHRONOLOGY_ROUNDING_TOLERANCE;
            require(retryRequest.sent >= earliestRetryRequest,
                    "metadata prefetch retry request preceded its declared delay");
            require(retryRequest.uri == rangeRequest->uri &&
                        retryRequest.httpVersion == 2 &&
                        retryRequest.streamId > previousRequest.streamId,
                    "metadata prefetch retry request changed URI or HTTP/2 stream");
            require(retry.evidence.connectionId == headConnectionId &&
                        retry.evidence.connectionId == rangeConnectionId &&
                        retry.evidence.httpMajor == 2,
                    "metadata prefetch retry event changed connection or protocol");
        }
        require(headRequest->sent < rangeRequest->sent &&
                rangeRequest->sent < headResponse->completed,
                "metadata prefetch request headers did not overlap HEAD completion");
        require(publicationCount == 1,
                "metadata prefetch cache publication evidence is missing or ambiguous");
        MetadataPrefetchProof proof;
        proof.enabled = true;
        proof.headRequestCount = headRequestCount;
        proof.rangeRequestCount = rangeRequestCount;
        proof.rangeStart = 0;
        proof.rangeEnd = 131071;
        proof.headHttpVersion = headRequest->httpVersion;
        proof.rangeHttpVersion = rangeRequest->httpVersion;
        proof.sharedConnection = true;
        proof.requestsOverlapped = true;
        proof.cachePublished = true;
        proof.fallbackReason.clear();
        for (const TimedCoordinatorRetry& retry : coordinatorRetries)
            proof.coordinatorRetries.push_back(retry.evidence);
        return proof;
    }

    void verifyOptimizedMetadataIntervals(const HttpProof& proof)
    {
        const auto metadata =
            std::make_pair<std::uint64_t, std::uint64_t>(0, 131071);
        require(!proof.successfulByteIntervals.empty() &&
                proof.successfulByteIntervals.front() == metadata,
                "optimized first data interval must be exactly bytes 0-131071");
        for (std::size_t index = 1; index < proof.successfulByteIntervals.size(); ++index)
        {
            const auto& interval = proof.successfulByteIntervals[index];
            require(!(interval.first >= metadata.first &&
                      interval.second <= metadata.second),
                    "later successful interval is wholly inside optimized metadata chunk");
        }
        require(proof.successfulByteIntervals.size() ==
                    static_cast<std::size_t>(proof.successfulGetCount),
                "successful interval evidence does not map one-to-one to HTTP 206 responses");
    }

    void verifyLiveProfileProof(RangeProfile profile, const HttpProof& proof)
    {
        if (usesOptimizedRangeSettings(profile))
            verifyOptimizedMetadataIntervals(proof);
        require(proof.metadataPrefetch.enabled == (profile == RangeProfile::Prefetch),
                "metadata prefetch proof activation does not match the selected profile");
        if (profile == RangeProfile::Prefetch)
        {
            require(proof.attributedTransportQualified &&
                        proof.metadataPrefetch.attributedTransportQualified,
                    "v6 live profile accepted unqualified attributed transport");
        }
        require(profile == RangeProfile::Prefetch ||
                    proof.immediateTransientRetryCount == 0,
                "non-prefetch profile emitted an immediate multi-range retry");
    }

    struct AttributedReplay
    {
        AttributedReplay() = default;
        AttributedReplay(AttributedReplay&& other) noexcept
            : serverRequests(std::move(other.serverRequests)),
              ordinaryTraffic(std::move(other.ordinaryTraffic)),
              ordinaryServerRequests(
                  std::move(other.ordinaryServerRequests)),
              ordinaryStats(std::move(other.ordinaryStats))
        {
            capture.messages = std::move(other.capture.messages);
            capture.timestamps = std::move(other.capture.timestamps);
            ordinaryCapture.messages =
                std::move(other.ordinaryCapture.messages);
            ordinaryCapture.timestamps =
                std::move(other.ordinaryCapture.timestamps);
        }
        AttributedReplay& operator=(AttributedReplay&&) = delete;
        AttributedReplay(const AttributedReplay&) = delete;
        AttributedReplay& operator=(const AttributedReplay&) = delete;

        DebugCapture capture;
        std::vector<ScienceServerRequest> serverRequests;
        std::vector<std::string> ordinaryTraffic;
        DebugCapture ordinaryCapture;
        std::vector<ScienceServerRequest> ordinaryServerRequests;
        std::string ordinaryStats;
    };

    ScienceTransportCompletion coordinatorHeadCompletion(
        const std::string& context, std::uint64_t ordinal,
        std::uint64_t request, int attempt, int status,
        std::uint64_t declaredLength)
    {
        ScienceTransportCompletion completion;
        completion.ordinal = ordinal;
        completion.context = context;
        completion.scope = "coordinator";
        completion.role = "head";
        completion.request = request;
        completion.attempt = attempt;
        completion.method = "HEAD";
        completion.range = "none";
        completion.status = status;
        completion.httpMajor = 2;
        completion.connectionId = 7;
        completion.contentLengthCount = 1;
        completion.contentLengthValid = true;
        completion.declaredContentLength = declaredLength;
        return completion;
    }

    ScienceTransportCompletion rangedCompletion(
        const std::string& context, std::uint64_t ordinal,
        const std::string& scope, std::uint64_t request,
        int attempt, int status, const std::string& range,
        std::uint64_t actualBytes, std::uint64_t total)
    {
        ScienceTransportCompletion completion;
        completion.ordinal = ordinal;
        completion.context = context;
        completion.scope = scope;
        completion.role = "range";
        completion.request = request;
        completion.attempt = attempt;
        completion.method = "GET";
        completion.range = range;
        completion.status = status;
        completion.httpMajor = 2;
        completion.connectionId = 7;
        completion.contentLengthCount = 1;
        completion.contentLengthValid = true;
        completion.declaredContentLength = actualBytes;
        completion.actualBodyBytes = actualBytes;
        if (status == 206)
        {
            const auto interval = parseExactByteRange(range);
            completion.contentRangeCount = 1;
            completion.contentRangeValid = true;
            completion.contentStart = static_cast<long long>(interval.first);
            completion.contentEnd = static_cast<long long>(interval.second);
            completion.contentTotal = static_cast<long long>(total);
        }
        return completion;
    }

    std::string serializeScienceCompletion(
        const ScienceTransportCompletion& completion)
    {
        std::ostringstream stream;
        stream << "VSICURL: ScienceTransport: response-v1 ordinal="
               << completion.ordinal << " context=" << completion.context
               << " scope=" << completion.scope << " role=" << completion.role
               << " request=" << completion.request
               << " attempt=" << completion.attempt
               << " method=" << completion.method
               << " range=" << completion.range
               << " curl=" << completion.curlCode
               << " status=" << completion.status
               << " http=" << completion.httpMajor
               << " redirects=" << completion.redirects
               << " connection=" << completion.connectionId
               << " content-length-count=" << completion.contentLengthCount
               << " content-length-valid="
               << (completion.contentLengthValid ? 1 : 0)
               << " declared-content-length="
               << completion.declaredContentLength
               << " content-range-count=" << completion.contentRangeCount
               << " content-range-valid="
               << (completion.contentRangeValid ? 1 : 0)
               << " content-start=" << completion.contentStart
               << " content-end=" << completion.contentEnd
               << " content-total=" << completion.contentTotal
               << " actual-body-bytes=" << completion.actualBodyBytes;
        return stream.str();
    }

    void appendAttributedRequest(AttributedReplay& replay,
                                 const ScienceTransportCompletion& completion,
                                 const std::string& session, int streamId)
    {
        std::ostringstream raw;
        raw << "CURL_INFO_HEADER_OUT: " << completion.method
            << " /fixture.tif HTTP/2\r\n";
        if (completion.range != "none")
            raw << "Range: " << completion.range << "\r\n";
        raw << "X-OSGSol-Science-Correlation: "
            << correlationId(completion) << "\r\n\r\n";
        replay.capture.messages.push_back(raw.str());
        ScienceServerRequest server;
        server.correlation = correlationId(completion);
        server.method = completion.method;
        server.range = completion.range == "none" ? "" : completion.range;
        server.sessionId = session;
        server.streamId = streamId;
        server.path = "/fixture.tif";
        server.status = completion.status;
        server.start = static_cast<std::uint64_t>(streamId) * 10;
        server.response = server.start + 1;
        server.end = server.response + 1;
        server.attemptedBodyBytes = completion.actualBodyBytes;
        server.contentLength = completion.declaredContentLength;
        if (completion.contentRangeCount == 1)
        {
            server.contentRange = "bytes " +
                std::to_string(completion.contentStart) + "-" +
                std::to_string(completion.contentEnd) + "/" +
                std::to_string(completion.contentTotal);
        }
        server.startCount = 1;
        server.responseCount = 1;
        server.endCount = 1;
        replay.serverRequests.push_back(server);
    }

    void appendAttributedResponse(AttributedReplay& replay,
                                  const ScienceTransportCompletion& completion)
    {
        replay.capture.messages.push_back(
            "CURL_INFO_HEADER_IN: HTTP/2 " +
            std::to_string(completion.status));
        for (int index = 0; index < completion.contentLengthCount; ++index)
            replay.capture.messages.push_back(
                "CURL_INFO_HEADER_IN: content-length: " +
                std::to_string(completion.declaredContentLength));
        for (int index = 0; index < completion.contentRangeCount; ++index)
            replay.capture.messages.push_back(
                "CURL_INFO_HEADER_IN: content-range: bytes " +
                std::to_string(completion.contentStart) + "-" +
                std::to_string(completion.contentEnd) + "/" +
                std::to_string(completion.contentTotal));
        replay.capture.messages.push_back("CURL_INFO_HEADER_IN: ");
    }

    void appendAttributedCompletion(AttributedReplay& replay,
                                    const ScienceTransportCompletion& completion)
    {
        replay.capture.messages.push_back(
            serializeScienceCompletion(completion));
    }

    AttributedReplay passingHeadRecoveryReplay()
    {
        static const std::string context =
            "0123456789abcdef0123456789abcdef";
        AttributedReplay replay;
        const auto head1 = coordinatorHeadCompletion(context, 1, 1, 1, 500, 17);
        const auto range = rangedCompletion(
            context, 2, "coordinator", 2, 1, 206,
            "bytes=0-131071", 131072, 1048576);
        const auto head2 = coordinatorHeadCompletion(
            context, 3, 1, 2, 200, 1048576);
        appendAttributedRequest(replay, head1, "session-a", 1);
        appendAttributedRequest(replay, range, "session-a", 3);
        appendAttributedResponse(replay, range);
        appendAttributedResponse(replay, head1);
        appendAttributedCompletion(replay, head1);
        appendAttributedCompletion(replay, range);
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: head-transient-retry "
            "context=" + context +
            " retry=1 request=1 failed-attempt=1 scheduled-attempt=2 "
            "status=500 delay-ms=100 connection=7 http=2 "
            "declared-content-length=17 actual-body-bytes=0");
        appendAttributedRequest(replay, head2, "session-a", 5);
        appendAttributedResponse(replay, head2);
        appendAttributedCompletion(replay, head2);
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: logical-get-complete context=" +
            context + " request=2 attempt=1 range=bytes=0-131071 "
            "bytes=131072");
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: published context=" + context +
            " request=2 attempt=1");
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: file-property-published context=" +
            context + " request=2 attempt=1");
        return replay;
    }

    AttributedReplay passingRangeRecoveryMirrorReplay()
    {
        static const std::string context =
            "11111111111111111111111111111111";
        AttributedReplay replay;
        const auto head = coordinatorHeadCompletion(
            context, 1, 1, 1, 200, 1048576);
        const auto range1 = rangedCompletion(
            context, 2, "coordinator", 2, 1, 500,
            "bytes=0-131071", 17, 1048576);
        const auto range2 = rangedCompletion(
            context, 3, "coordinator", 2, 2, 206,
            "bytes=0-131071", 131072, 1048576);
        appendAttributedRequest(replay, head, "session-b", 1);
        appendAttributedRequest(replay, range1, "session-b", 3);
        appendAttributedResponse(replay, head);
        appendAttributedResponse(replay, range1);
        appendAttributedCompletion(replay, head);
        appendAttributedCompletion(replay, range1);
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: transient-retry context=" +
            context + " request=2 failed-attempt=1 scheduled-attempt=2 "
            "range=bytes=0-131071 status=500 bytes=17 delay-ms=100 "
            "connection=7 http=2");
        appendAttributedRequest(replay, range2, "session-b", 5);
        appendAttributedResponse(replay, range2);
        appendAttributedCompletion(replay, range2);
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: logical-get-complete context=" +
            context + " request=2 attempt=2 range=bytes=0-131071 "
            "bytes=131072");
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: published context=" + context +
            " request=2 attempt=2");
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: file-property-published context=" +
            context + " request=2 attempt=2");
        return replay;
    }

    AttributedReplay sameStatusOutOfOrderSiblingReplay()
    {
        static const std::string context =
            "33333333333333333333333333333333";
        AttributedReplay replay;
        const auto head = coordinatorHeadCompletion(
            context, 3, 1, 1, 200, 1048576);
        const auto coordinatorRange = rangedCompletion(
            context, 4, "coordinator", 2, 1, 206,
            "bytes=0-131071", 131072, 1048576);
        const auto siblingA = rangedCompletion(
            context, 2, "multirange", 3, 1, 500,
            "bytes=262144-327679", 17, 1048576);
        const auto siblingB = rangedCompletion(
            context, 1, "multirange", 4, 1, 500,
            "bytes=393216-458751", 17, 1048576);

        appendAttributedRequest(replay, head, "session-d", 1);
        appendAttributedRequest(replay, coordinatorRange, "session-d", 3);
        appendAttributedRequest(replay, siblingA, "session-d", 5);
        appendAttributedRequest(replay, siblingB, "session-d", 7);
        appendAttributedResponse(replay, siblingB);
        appendAttributedResponse(replay, siblingA);
        appendAttributedResponse(replay, head);
        appendAttributedResponse(replay, coordinatorRange);
        appendAttributedCompletion(replay, siblingB);
        appendAttributedCompletion(replay, siblingA);
        appendAttributedCompletion(replay, head);
        appendAttributedCompletion(replay, coordinatorRange);
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: published context=" + context +
            " request=2 attempt=1");
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: file-property-published context=" +
            context + " request=2 attempt=1");
        return replay;
    }

    AttributedReplay ordinaryHeadCompatibilityReplay()
    {
        static const std::string context =
            "44444444444444444444444444444444";
        AttributedReplay replay;
        auto methodHead = coordinatorHeadCompletion(
            context, 1, 1, 1, 200, 1048576);
        methodHead.scope = "ordinary-head";
        auto headerOnlyGet = coordinatorHeadCompletion(
            context, 2, 2, 1, 200, 1048576);
        headerOnlyGet.scope = "ordinary-head";
        headerOnlyGet.method = "GET";
        appendAttributedRequest(replay, methodHead, "session-e", 1);
        appendAttributedRequest(replay, headerOnlyGet, "session-e", 3);
        appendAttributedResponse(replay, methodHead);
        appendAttributedResponse(replay, headerOnlyGet);
        appendAttributedCompletion(replay, methodHead);
        appendAttributedCompletion(replay, headerOnlyGet);
        return replay;
    }

    AttributedReplay combinedOperationScopeReplay()
    {
        static const std::string context =
            "89898989898989898989898989898989";
        AttributedReplay replay;
        const auto head = coordinatorHeadCompletion(
            context, 1, 1, 1, 200, 1048576);
        const auto coordinatorRange = rangedCompletion(
            context, 2, "coordinator", 2, 1, 206,
            "bytes=0-131071", 131072, 1048576);
        const auto multiRange = rangedCompletion(
            context, 3, "multirange", 3, 1, 206,
            "bytes=262144-327679", 65536, 1048576);
        auto ordinaryHead = coordinatorHeadCompletion(
            context, 4, 4, 1, 405, 0);
        ordinaryHead.scope = "ordinary-head";
        auto ordinaryHeaderGet = coordinatorHeadCompletion(
            context, 5, 5, 1, 200, 1048576);
        ordinaryHeaderGet.scope = "ordinary-head";
        ordinaryHeaderGet.method = "GET";

        const ScienceTransportCompletion completions[] = {
            head, coordinatorRange, multiRange,
            ordinaryHead, ordinaryHeaderGet,
        };
        int streamId = 1;
        for (const ScienceTransportCompletion& completion : completions)
        {
            appendAttributedRequest(
                replay, completion, "session-combined", streamId);
            streamId += 2;
        }
        for (const ScienceTransportCompletion& completion : completions)
        {
            appendAttributedResponse(replay, completion);
            appendAttributedCompletion(replay, completion);
        }
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: logical-get-complete context=" +
            context + " request=2 attempt=1 range=bytes=0-131071 "
            "bytes=131072");
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: published context=" + context +
            " request=2 attempt=1");
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: file-property-published context=" +
            context + " request=2 attempt=1");
        return replay;
    }

    AttributedReplay v5ShapedTerminalFallbackReplay()
    {
        static const std::string context =
            "66666666666666666666666666666666";
        AttributedReplay replay;
        const auto head = coordinatorHeadCompletion(
            context, 1, 1, 1, 500, 17);
        const auto range = rangedCompletion(
            context, 2, "coordinator", 2, 1, 206,
            "bytes=0-131071", 131072, 1048576);
        appendAttributedRequest(replay, head, "session-f", 1);
        appendAttributedRequest(replay, range, "session-f", 3);
        appendAttributedResponse(replay, head);
        appendAttributedResponse(replay, range);
        appendAttributedCompletion(replay, head);
        appendAttributedCompletion(replay, range);
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: fallback context=" + context +
            " request=1 attempt=1 reason=head-invalid");
        replay.ordinaryTraffic = {
            "HEAD /fixture.tif HTTP/2 status=200",
            "GET /fixture.tif HTTP/2 Range: bytes=0-131071 status=206",
        };
        replay.ordinaryCapture.messages = {
            "CURL_INFO_HEADER_OUT: HEAD /fixture.tif HTTP/2\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 200\r",
            "CURL_INFO_HEADER_IN: content-length: 1048576\r",
            "CURL_INFO_HEADER_IN: ",
            "CURL_INFO_HEADER_OUT: GET /fixture.tif HTTP/2\r\n"
                "Range: bytes=0-131071\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 206\r",
            "CURL_INFO_HEADER_IN: content-length: 131072\r",
            "CURL_INFO_HEADER_IN: content-range: bytes 0-131071/1048576\r",
            "CURL_INFO_HEADER_IN: ",
            "VSICURL: Download completed",
        };
        replay.ordinaryCapture.timestamps.resize(
            replay.ordinaryCapture.messages.size());
        for (std::size_t index = 0;
             index < replay.ordinaryCapture.timestamps.size(); ++index)
        {
            replay.ordinaryCapture.timestamps[index] =
                std::chrono::steady_clock::time_point(
                    std::chrono::nanoseconds(index + 1));
        }
        ScienceServerRequest ordinaryHead;
        ordinaryHead.method = "HEAD";
        ordinaryHead.sessionId = "session-f-ordinary";
        ordinaryHead.streamId = 7;
        ordinaryHead.path = "/fixture.tif";
        ordinaryHead.status = 200;
        ordinaryHead.start = 70;
        ordinaryHead.response = 71;
        ordinaryHead.end = 72;
        ordinaryHead.contentLength = 1048576;
        ordinaryHead.startCount = ordinaryHead.responseCount =
            ordinaryHead.endCount = 1;
        ScienceServerRequest ordinaryRange;
        ordinaryRange.method = "GET";
        ordinaryRange.range = "bytes=0-131071";
        ordinaryRange.sessionId = ordinaryHead.sessionId;
        ordinaryRange.streamId = 9;
        ordinaryRange.path = "/fixture.tif";
        ordinaryRange.status = 206;
        ordinaryRange.start = 90;
        ordinaryRange.response = 91;
        ordinaryRange.end = 92;
        ordinaryRange.attemptedBodyBytes = 131072;
        ordinaryRange.contentLength = 131072;
        ordinaryRange.contentRange = "bytes 0-131071/1048576";
        ordinaryRange.startCount = ordinaryRange.responseCount =
            ordinaryRange.endCount = 1;
        replay.ordinaryServerRequests = {ordinaryHead, ordinaryRange};
        replay.ordinaryStats =
            "{\"methods\":{\"GET\":{\"count\":1,"
            "\"downloaded_bytes\":131072},\"HEAD\":{\"count\":1}}}";
        return replay;
    }

    AttributedReplay terminalHeadWithCacheOnlyReplay()
    {
        static const std::string context =
            "77777777777777777777777777777777";
        AttributedReplay replay;
        const auto head = coordinatorHeadCompletion(
            context, 1, 1, 1, 500, 17);
        const auto range = rangedCompletion(
            context, 2, "coordinator", 2, 1, 206,
            "bytes=0-131071", 131072, 1048576);
        appendAttributedRequest(replay, head, "session-g", 1);
        appendAttributedRequest(replay, range, "session-g", 3);
        appendAttributedResponse(replay, head);
        appendAttributedResponse(replay, range);
        appendAttributedCompletion(replay, head);
        appendAttributedCompletion(replay, range);
        replay.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: published context=" + context +
            " request=2 attempt=1");
        return replay;
    }

    void replaceAll(std::string& value, const std::string& from,
                    const std::string& to)
    {
        std::size_t position = 0;
        while ((position = value.find(from, position)) != std::string::npos)
        {
            value.replace(position, from.size(), to);
            position += to.size();
        }
    }

    AttributedReplay copyAttributedReplay(const AttributedReplay& source)
    {
        AttributedReplay copy;
        copy.capture.messages = source.capture.messages;
        copy.capture.timestamps = source.capture.timestamps;
        copy.serverRequests = source.serverRequests;
        copy.ordinaryTraffic = source.ordinaryTraffic;
        copy.ordinaryCapture.messages = source.ordinaryCapture.messages;
        copy.ordinaryCapture.timestamps = source.ordinaryCapture.timestamps;
        copy.ordinaryServerRequests = source.ordinaryServerRequests;
        copy.ordinaryStats = source.ordinaryStats;
        return copy;
    }

    std::size_t replayMessageIndex(const DebugCapture& capture,
                                   const std::string& needle,
                                   std::size_t occurrence = 0)
    {
        for (std::size_t index = 0; index < capture.messages.size(); ++index)
        {
            if (capture.messages[index].find(needle) == std::string::npos)
                continue;
            if (occurrence-- == 0) return index;
        }
        fail("attributed replay mutation target is missing: " + needle);
    }

    void requireAttributedRejected(const AttributedReplay& replay,
                                   const std::string& description)
    {
        try
        {
            const AttributedTransportProof proof =
                buildAttributedTransportProof(
                    replay.capture, replay.serverRequests);
            if (!proof.qualified) return;
        }
        catch (const std::exception&)
        {
            return;
        }
        fail("AttributedV6 accepted " + description);
    }

    struct CompletionSinkRaceCapture
    {
        DebugCapture capture;
        std::mutex controlMutex;
        std::condition_variable condition;
        bool firstResponseEntered = false;
        bool releaseFirstResponse = false;
        bool watchdogExpired = false;
        bool reentrantCallbackCompleted = false;
        int responseHandlersEntered = 0;
        std::thread::id drainerThread;
        std::vector<std::thread::id> responseHandlerThreads;
    };

    void CPL_STDCALL captureCompletionSinkRaceMessage(
        CPLErr errorClass, CPLErrorNum, const char* message)
    {
        CompletionSinkRaceCapture* state =
            static_cast<CompletionSinkRaceCapture*>(
                CPLGetErrorHandlerUserData());
        if (state == nullptr || message == nullptr) return;
        if (std::string(message) == "completion-sink-reentrant-probe")
        {
            std::lock_guard<std::mutex> lock(state->controlMutex);
            state->reentrantCallbackCompleted = true;
            state->condition.notify_all();
            return;
        }
        const bool response = std::string(message).find(
            "ScienceTransport: response-v1 ") != std::string::npos;
        bool firstResponse = false;
        if (response)
        {
            std::unique_lock<std::mutex> lock(state->controlMutex);
            ++state->responseHandlersEntered;
            if (!state->firstResponseEntered)
            {
                state->firstResponseEntered = true;
                state->drainerThread = std::this_thread::get_id();
                firstResponse = true;
                state->condition.notify_all();
                if (!state->condition.wait_for(
                        lock, std::chrono::seconds(10), [&]()
                        {
                            return state->releaseFirstResponse;
                        }))
                {
                    state->watchdogExpired = true;
                    state->releaseFirstResponse = true;
                    state->condition.notify_all();
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(state->capture.mutex);
            state->capture.messages.emplace_back(message);
            state->capture.timestamps.push_back(
                std::chrono::steady_clock::now());
        }
        if (response)
        {
            std::lock_guard<std::mutex> lock(state->controlMutex);
            state->responseHandlerThreads.push_back(
                std::this_thread::get_id());
            state->condition.notify_all();
        }
        if (firstResponse)
            CPLDebug("OSGSOL_SCIENCE_SINK_RACE",
                     "completion-sink-reentrant-probe");
        if (errorClass >= CE_Warning)
            std::cerr << "GDAL: " << message << std::endl;
    }

    void verifyCompletionSinkConcurrencyRegression(
        const std::filesystem::path& fixture,
        const std::filesystem::path& root)
    {
        const char* selectedMode =
            CPLGetConfigOption("OSGSOL_TEST_PREFETCH_CASE", nullptr);
        if (selectedMode != nullptr &&
            std::string(selectedMode) != "v6-completion-sink-race")
            return;
        const std::filesystem::path certificate =
            root / "v6-sink-race-cert.pem";
        const std::filesystem::path key = root / "v6-sink-race-key.pem";
        const std::filesystem::path ready = root / "v6-sink-race.ready";
        const std::filesystem::path log = root / "v6-sink-race.jsonl";
        createSelfSignedCertificate(certificate, key);
        ServerProcess server = startHttp2Server(
            fixture, ready, log, certificate, key, "success", "h2");
        const int port = waitForPort(ready);
        const std::string url = "/vsicurl/https://127.0.0.1:" +
            std::to_string(port) +
            "/v6-completion-sink-race-head-first/fixture.tif";
        ScopedGdalConfig config({
            {"GDAL_HTTP_UNSAFESSL", "YES"},
            {"GDAL_HTTP_VERSION", "2TLS"},
            {"GDAL_HTTP_PROXY", ""},
            {"GDAL_HTTPS_PROXY", ""},
            {"GDAL_HTTP_MAX_RETRY", "0"},
        });
        VSICurlClearCache();
        CompletionSinkRaceCapture state;
        std::atomic<bool> firstReturned{false};
        std::atomic<bool> secondReturned{false};
        std::atomic<bool> firstOpened{false};
        std::atomic<bool> secondOpened{false};
        ScopedPathSpecificOption prefetch(
            url, "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
        const std::string operationId = nextPrefetchOperationId();
        ScopedPathSpecificOption operation(
            url, "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
            operationId.c_str());
        const auto open = [&](std::atomic<bool>& opened,
                              std::atomic<bool>& returned)
        {
            CPLPushErrorHandlerEx(captureCompletionSinkRaceMessage, &state);
            CPLSetCurrentErrorHandlerCatchDebug(1);
            GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(
                url.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
                nullptr, nullptr, nullptr));
            opened.store(dataset != nullptr);
            if (dataset) GDALClose(dataset);
            returned.store(true);
            {
                std::lock_guard<std::mutex> lock(state.controlMutex);
                state.condition.notify_all();
            }
            CPLPopErrorHandler();
        };

        std::thread first([&]() { open(firstOpened, firstReturned); });
        bool observedFirstResponse = false;
        {
            std::unique_lock<std::mutex> lock(state.controlMutex);
            state.condition.wait_for(lock, std::chrono::seconds(10), [&]()
            {
                return state.firstResponseEntered || firstReturned.load();
            });
            observedFirstResponse = state.firstResponseEntered;
        }
        if (!observedFirstResponse)
        {
            first.join();
            fail("production completion sink emitted no response-v1 event for "
                 "the deterministic race");
        }

        std::thread second([&]() { open(secondOpened, secondReturned); });
        bool secondOperationCompletedIntoPendingQueue = false;
        int responseHandlersBeforeRelease = 0;
        {
            std::unique_lock<std::mutex> lock(state.controlMutex);
            state.condition.wait_for(lock, std::chrono::seconds(5), [&]()
            {
                return secondReturned.load();
            });
            secondOperationCompletedIntoPendingQueue = secondReturned.load();
            responseHandlersBeforeRelease = state.responseHandlersEntered;
        }
        int authoritativeLogResponsesBeforeRelease = 0;
        {
            std::lock_guard<std::mutex> lock(state.capture.mutex);
            authoritativeLogResponsesBeforeRelease =
                static_cast<int>(std::count_if(
                state.capture.messages.begin(), state.capture.messages.end(),
                [](const std::string& message)
                {
                    return message.find(
                        "ScienceTransport: response-v1 ") !=
                        std::string::npos;
                }));
        }
        {
            std::lock_guard<std::mutex> lock(state.controlMutex);
            state.releaseFirstResponse = true;
            state.condition.notify_all();
        }
        first.join();
        second.join();
        server.stop();
        const Http2Evidence serverEvidence = readHttp2Log(log);

        require(firstOpened.load() && secondOpened.load() &&
                    secondOperationCompletedIntoPendingQueue &&
                    responseHandlersBeforeRelease == 1 &&
                    authoritativeLogResponsesBeforeRelease == 0 &&
                    !state.watchdogExpired &&
                    state.reentrantCallbackCompleted,
                "completion sink did not let B finish into the internal "
                "pending queue while A alone owned the paused authoritative "
                "log drainer, or deadlocked its callback");
        static const std::regex prefix(
            R"(ScienceTransport: response-v1 ordinal=([1-9][0-9]*) )"
            R"(context=([0-9a-f]{32}) )"
            R"(scope=(?:coordinator|multirange|ordinary-head) )"
            R"(role=(?:head|range) request=([1-9][0-9]*) )"
            R"(attempt=([1-4]) )");
        std::vector<std::uint64_t> ordinals;
        std::set<std::string> completionCorrelations;
        std::string context;
        for (const std::string& message : state.capture.messages)
        {
            std::smatch match;
            if (!std::regex_search(message, match, prefix)) continue;
            ordinals.push_back(static_cast<std::uint64_t>(
                std::stoull(match[1].str())));
            if (context.empty()) context = match[2].str();
            require(context == match[2].str(),
                    "same path/token race split into multiple contexts");
            completionCorrelations.insert(match[2].str() + "/" +
                match[3].str() + "/" + match[4].str());
        }
        std::set<std::string> serverCorrelations;
        std::set<std::string> serverSessions;
        int serverHeads = 0;
        int serverGets = 0;
        for (const Http2StreamEvidence& stream : serverEvidence.streams)
        {
            serverCorrelations.insert(stream.correlation);
            serverSessions.insert(stream.sessionId);
            if (stream.method == "HEAD") ++serverHeads;
            if (stream.method == "GET") ++serverGets;
        }
        require(ordinals.size() == 4 &&
                    state.responseHandlerThreads.size() == 4 &&
                    std::all_of(state.responseHandlerThreads.begin(),
                        state.responseHandlerThreads.end(),
                        [&](const std::thread::id& thread)
                        {
                            return thread == state.drainerThread;
                        }) &&
                    serverHeads == 2 && serverGets == 2 &&
                    serverSessions.size() == 1 &&
                    serverCorrelations == completionCorrelations,
                "same-token race did not emit both HEAD/Range completions");
        for (std::size_t index = 0; index < ordinals.size(); ++index)
            require(ordinals[index] == index + 1,
                    "real completion sink reordered or skipped a serialized "
                    "completion ordinal");
    }

    void verifyAttributedTransportRegression()
    {
        AttributedReplay passing = passingHeadRecoveryReplay();
        const AttributedTransportProof proof = buildAttributedTransportProof(
            passing.capture, passing.serverRequests);
        require(proof.qualified && proof.completions.size() == 3 &&
                    proof.headRetries.size() == 1 &&
                    proof.headRetries.front().failedStatus == 500 &&
                    proof.headRetries.front().failedDeclaredContentLength == 17 &&
                    proof.headRetries.front().failedActualBodyBytes == 0,
                "passing v6 replay lost authoritative HEAD retry attribution");

        AttributedReplay unrelatedFallback = copyAttributedReplay(passing);
        const std::size_t missingHeadTransition = replayMessageIndex(
            unrelatedFallback.capture, "head-transient-retry context=");
        unrelatedFallback.capture.messages.erase(
            unrelatedFallback.capture.messages.begin() +
                missingHeadTransition);
        unrelatedFallback.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: fallback context=" +
            proof.completions.front().context +
            " request=2 attempt=1 reason=range-invalid");
        requireAttributedRejected(
            unrelatedFallback,
            "a missing HEAD retry transition hidden by a later fallback");

        AttributedReplay mirror = passingRangeRecoveryMirrorReplay();
        const AttributedTransportProof mirrorProof =
            buildAttributedTransportProof(
                mirror.capture, mirror.serverRequests);
        require(mirrorProof.qualified && mirrorProof.headRetries.empty() &&
                    mirrorProof.completions.size() == 3 &&
                    std::count_if(mirrorProof.completions.begin(),
                        mirrorProof.completions.end(),
                        [](const ScienceTransportCompletion& completion)
                        {
                            return completion.role == "range" &&
                                completion.status == 500;
                        }) == 1,
                "HEAD 200/Range 500 mirror was falsely attributed to HEAD");

        AttributedReplay missingRangeTransition =
            copyAttributedReplay(mirror);
        missingRangeTransition.capture.messages.erase(
            missingRangeTransition.capture.messages.begin() +
                replayMessageIndex(
                    missingRangeTransition.capture,
                    "ParallelHeadRange: transient-retry context="));
        requireAttributedRejected(
            missingRangeTransition,
            "a retried Range completion without its retry transition");
        AttributedReplay duplicateRangeTransition =
            copyAttributedReplay(mirror);
        const std::size_t rangeTransition = replayMessageIndex(
            duplicateRangeTransition.capture,
            "ParallelHeadRange: transient-retry context=");
        duplicateRangeTransition.capture.messages.insert(
            duplicateRangeTransition.capture.messages.begin() +
                rangeTransition,
            duplicateRangeTransition.capture.messages[rangeTransition]);
        requireAttributedRejected(
            duplicateRangeTransition,
            "a retried Range completion with duplicate retry transitions");

        AttributedReplay siblings = sameStatusOutOfOrderSiblingReplay();
        const AttributedTransportProof siblingProof =
            buildAttributedTransportProof(
                siblings.capture, siblings.serverRequests);
        require(siblingProof.qualified &&
                    siblingProof.completions.size() == 4 &&
                    siblingProof.completions[0].ordinal == 1 &&
                    siblingProof.completions[0].request == 4 &&
                    siblingProof.completions[0].status == 500 &&
                    siblingProof.completions[1].ordinal == 2 &&
                    siblingProof.completions[1].request == 3 &&
                    siblingProof.completions[1].status == 500 &&
                    siblingProof.completions[0].range ==
                        "bytes=393216-458751" &&
                    siblingProof.completions[1].range ==
                        "bytes=262144-327679",
                "ordinal allocation/enqueue race did not preserve sink order "
                "for same-status siblings completed out of request order");

        AttributedReplay ordinary = ordinaryHeadCompatibilityReplay();
        const AttributedTransportProof ordinaryProof =
            buildAttributedTransportProof(
                ordinary.capture, ordinary.serverRequests);
        require(!ordinaryProof.qualified &&
                    ordinaryProof.completions.size() == 2 &&
                    ordinaryProof.completions[0].method == "HEAD" &&
                    ordinaryProof.completions[1].method == "GET" &&
                    ordinaryProof.completions[1].role == "head" &&
                    ordinaryProof.completions[1].actualBodyBytes == 0,
                "ordinary HEAD compatibility tuples were not parsed as "
                "distinct physical methods");

        AttributedReplay combined = combinedOperationScopeReplay();
        const AttributedTransportProof combinedProof =
            buildAttributedTransportProof(
                combined.capture, combined.serverRequests);
        require(combinedProof.qualified &&
                    combinedProof.completions.size() == 5 &&
                    std::count_if(combinedProof.completions.begin(),
                        combinedProof.completions.end(),
                        [](const ScienceTransportCompletion& completion)
                        {
                            return completion.scope == "coordinator";
                        }) == 2 &&
                    std::count_if(combinedProof.completions.begin(),
                        combinedProof.completions.end(),
                        [](const ScienceTransportCompletion& completion)
                        {
                            return completion.scope == "multirange";
                        }) == 1 &&
                    std::count_if(combinedProof.completions.begin(),
                        combinedProof.completions.end(),
                        [](const ScienceTransportCompletion& completion)
                        {
                            return completion.scope == "ordinary-head";
                        }) == 2 &&
                    std::any_of(combinedProof.completions.begin(),
                        combinedProof.completions.end(),
                        [](const ScienceTransportCompletion& completion)
                        {
                            return completion.scope == "ordinary-head" &&
                                completion.method == "HEAD" &&
                                completion.status == 405;
                        }),
                "one operation context did not span coordinator, multirange, "
                "and initial-HEAD-405 ordinary-head compatibility transports");
        AttributedReplay otherPath = copyAttributedReplay(combined);
        otherPath.serverRequests[2].path = "/other-path/fixture.tif";
        requireAttributedRejected(otherPath,
                                  "a correlated request from another path");

        AttributedReplay nested = copyAttributedReplay(passing);
        AttributedReplay second = passingHeadRecoveryReplay();
        const std::string firstContext =
            "0123456789abcdef0123456789abcdef";
        const std::string secondContext =
            "22222222222222222222222222222222";
        for (std::string& message : second.capture.messages)
            replaceAll(message, firstContext, secondContext);
        for (ScienceServerRequest& request : second.serverRequests)
        {
            replaceAll(request.correlation, firstContext, secondContext);
            request.sessionId = "session-c";
        }
        const std::size_t nestPoint = replayMessageIndex(
            nested.capture, "response-v1 ordinal=2");
        nested.capture.messages.insert(
            nested.capture.messages.begin() + nestPoint,
            second.capture.messages.begin(), second.capture.messages.end());
        nested.serverRequests.insert(nested.serverRequests.end(),
            second.serverRequests.begin(), second.serverRequests.end());
        const AttributedTransportProof nestedProof =
            buildAttributedTransportProof(
                nested.capture, nested.serverRequests);
        require(nestedProof.qualified && nestedProof.completions.size() == 6,
                "interleaved contexts did not keep independent ordinal spaces");

        AttributedReplay crossedContext = copyAttributedReplay(nested);
        const std::size_t crossedPublication = replayMessageIndex(
            crossedContext.capture,
            "published context=" + firstContext);
        replaceAll(crossedContext.capture.messages[crossedPublication],
                   firstContext, secondContext);
        requireAttributedRejected(crossedContext,
                                  "a cross-context publication");

        AttributedReplay fallback = v5ShapedTerminalFallbackReplay();
        const AttributedTransportProof fallbackProof =
            buildAttributedTransportProof(
                fallback.capture, fallback.serverRequests);
        require(!fallbackProof.qualified && fallbackProof.fallbackCount == 1 &&
                    fallbackProof.cachePublicationCount == 0 &&
                    fallbackProof.propertyPublicationCount == 0 &&
                    fallbackProof.completions.size() == 2 &&
                    std::count_if(fallbackProof.completions.begin(),
                        fallbackProof.completions.end(),
                        [](const ScienceTransportCompletion& completion)
                        {
                            return completion.scope == "coordinator" &&
                                completion.role == "head";
                        }) == 1 &&
                    fallback.ordinaryTraffic.size() == 2,
                "v5-shaped fallback became parser ERROR or semantic PASS");
        const auto buildOrdinaryFallbackProof = [](
                const AttributedReplay& replay)
        {
            const HttpProof proof = buildHttpProof(
                replay.ordinaryCapture, replay.ordinaryStats,
                AttributionMode::LegacyFrozen);
            std::uint64_t serverBodies = 0;
            int serverHeads = 0;
            int serverGets = 0;
            for (const ScienceServerRequest& request :
                 replay.ordinaryServerRequests)
            {
                require(request.startCount == 1 &&
                            request.responseCount == 1 &&
                            request.endCount == 1 && !request.aborted &&
                            request.path == "/fixture.tif" &&
                            request.sessionId == "session-f-ordinary",
                        "v5 fallback ordinary server tuple is incomplete");
                if (request.method == "HEAD") ++serverHeads;
                if (request.method == "GET")
                {
                    ++serverGets;
                    checkedAdd(serverBodies, request.attemptedBodyBytes,
                               "v5 fallback ordinary server bytes");
                }
            }
            require(proof.actualHeadCount == 1 &&
                        proof.actualGetCount == 1 &&
                        proof.successfulGetCount == 1 &&
                        proof.statsHeadCount == 1 &&
                        proof.statsGetOperationCount == 1 &&
                        proof.actualHttpBodyBytes == 131072 &&
                        serverHeads == proof.actualHeadCount &&
                        serverGets == proof.actualGetCount &&
                        serverBodies == proof.actualHttpBodyBytes,
                    "v5 fallback ordinary HEAD/Range traffic was not consumed "
                    "by parser/server/stats evidence");
            return proof;
        };
        const HttpProof ordinaryFallbackProof =
            buildOrdinaryFallbackProof(fallback);

        AttributedReplay missingOrdinaryServer =
            copyAttributedReplay(fallback);
        missingOrdinaryServer.ordinaryServerRequests.pop_back();
        bool missingOrdinaryServerRejected = false;
        try
        {
            static_cast<void>(
                buildOrdinaryFallbackProof(missingOrdinaryServer));
        }
        catch (const std::exception&)
        {
            missingOrdinaryServerRejected = true;
        }
        require(missingOrdinaryServerRejected,
                "v5 fallback ordinary server mutation was accepted");

        AttributedReplay attemptAfterFallback = copyAttributedReplay(fallback);
        const auto lateHead = coordinatorHeadCompletion(
            fallbackProof.completions.front().context, 3, 1, 2,
            200, 1048576);
        appendAttributedRequest(
            attemptAfterFallback, lateHead, "session-f", 5);
        appendAttributedResponse(attemptAfterFallback, lateHead);
        appendAttributedCompletion(attemptAfterFallback, lateHead);
        requireAttributedRejected(attemptAfterFallback,
                                  "a coordinator attempt after fallback");

        AttributedReplay publicationAfterFallback =
            copyAttributedReplay(fallback);
        publicationAfterFallback.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: published context=" +
            fallbackProof.completions.front().context +
            " request=2 attempt=1");
        requireAttributedRejected(publicationAfterFallback,
                                  "publication after fallback");

        const std::string terminalStats =
            "{\"methods\":{\"GET\":{\"count\":1,"
            "\"downloaded_bytes\":131072},\"HEAD\":{\"count\":1}}}";
        AttributedReplay incompleteChain =
            terminalHeadWithCacheOnlyReplay();
        const HttpProof incompleteHttp = buildHttpProof(
            incompleteChain.capture, terminalStats,
            AttributionMode::AttributedV6);
        require(!incompleteHttp.attributedTransportQualified,
                "terminal HEAD/cache-only chain became an HTTP semantic PASS");
        bool metadataRejected = false;
        try
        {
            static_cast<void>(buildMetadataPrefetchProof(
                incompleteChain.capture, incompleteHttp));
        }
        catch (const std::exception&)
        {
            metadataRejected = true;
        }
        require(metadataRejected,
                "metadata proof accepted an unqualified attributed chain");
        HttpProof incompleteLive = incompleteHttp;
        incompleteLive.metadataPrefetch.enabled = true;
        incompleteLive.metadataPrefetch.attributedTransportQualified = false;
        bool liveRejected = false;
        try
        {
            verifyLiveProfileProof(RangeProfile::Prefetch, incompleteLive);
        }
        catch (const std::exception&)
        {
            liveRejected = true;
        }
        require(liveRejected,
                "live profile accepted an unqualified attributed chain");

        AttributedReplay missing = copyAttributedReplay(passing);
        missing.capture.messages.erase(missing.capture.messages.begin() +
            replayMessageIndex(missing.capture, "response-v1 ordinal=2"));
        requireAttributedRejected(missing, "a missing completion event");

        AttributedReplay duplicate = copyAttributedReplay(passing);
        const std::size_t duplicateIndex = replayMessageIndex(
            duplicate.capture, "response-v1 ordinal=2");
        duplicate.capture.messages.insert(
            duplicate.capture.messages.begin() + duplicateIndex,
            duplicate.capture.messages[duplicateIndex]);
        requireAttributedRejected(duplicate, "a duplicate completion event");

        const auto completionMutation = [&passing](
                const std::string& from, const std::string& to,
                const std::string& description)
        {
            AttributedReplay candidate = copyAttributedReplay(passing);
            const std::size_t index = replayMessageIndex(
                candidate.capture, "response-v1 ordinal=1");
            const std::size_t position =
                candidate.capture.messages[index].find(from);
            require(position != std::string::npos,
                    "completion mutation source is missing");
            candidate.capture.messages[index].replace(
                position, from.size(), to);
            requireAttributedRejected(candidate, description);
        };
        completionMutation("ordinal=1", "ordinal=4", "an ordinal gap");
        completionMutation("scope=coordinator", "scope=unknown",
                           "an unknown scope");
        completionMutation("role=head", "role=range", "a role swap");
        completionMutation("attempt=1", "attempt=4", "an attempt skip");
        completionMutation("curl=0", "curl=7", "a curl error");
        completionMutation("http=2", "http=1", "HTTP/1 attribution");
        completionMutation("redirects=0", "redirects=1", "a redirect");
        completionMutation("connection=7", "connection=-1",
                           "an unknown connection");
        completionMutation("actual-body-bytes=0", "actual-body-bytes=17",
                           "a HEAD writer body");
        completionMutation("content-length-count=1",
                           "content-length-count=2",
                           "duplicate Content-Length");
        completionMutation("actual-body-bytes=0",
                           "actual-body-bytes=0 trailing=yes",
                           "a trailing field");

        const auto completionMutationAt = [&passing](
                const std::string& eventNeedle,
                const std::string& from, const std::string& to,
                const std::string& description)
        {
            AttributedReplay candidate = copyAttributedReplay(passing);
            const std::size_t index = replayMessageIndex(
                candidate.capture, eventNeedle);
            const std::size_t position =
                candidate.capture.messages[index].find(from);
            require(position != std::string::npos,
                    "targeted completion mutation source is missing");
            candidate.capture.messages[index].replace(
                position, from.size(), to);
            requireAttributedRejected(candidate, description);
        };
        completionMutationAt("response-v1 ordinal=1", "ordinal=1",
                            "ordinal=malformed", "a malformed completion");
        completionMutationAt("response-v1 ordinal=1", "ordinal=1",
                            "ordinal=18446744073709551616",
                            "an overflowing ordinal");
        completionMutationAt("response-v1 ordinal=1",
                            "request=1 attempt=1",
                            "attempt=1 request=1",
                            "reordered completion fields");
        completionMutationAt("response-v1 ordinal=1", "attempt=1",
                            "attempt=0", "attempt zero");
        completionMutationAt("response-v1 ordinal=1", "attempt=1",
                            "attempt=5", "attempt five");
        completionMutationAt("response-v1 ordinal=1", "method=HEAD",
                            "method=GET", "a method swap");
        completionMutationAt("response-v1 ordinal=2",
                            "range=bytes=0-131071",
                            "range=bytes=262144-327679",
                            "a sibling Range substitution");
        completionMutationAt("response-v1 ordinal=2",
                            "range=bytes=0-131071",
                            "range=bytes=0-9223372036854775808",
                            "a Range beyond INT64_MAX");
        completionMutationAt("response-v1 ordinal=2", "request=2",
                            "request=3", "a request ID gap");
        completionMutationAt("response-v1 ordinal=1", "request=1",
                            "request=2", "a duplicate request ID");
        completionMutationAt("response-v1 ordinal=1", "status=500",
                            "status=501", "an unsupported status");
        completionMutationAt("response-v1 ordinal=1",
                            "content-length-valid=1",
                            "content-length-valid=0",
                            "invalid Content-Length evidence");
        completionMutationAt("response-v1 ordinal=1",
                            "declared-content-length=17",
                            "declared-content-length=18",
                            "a mutated Content-Length value");
        completionMutationAt("response-v1 ordinal=2",
                            "content-range-count=1",
                            "content-range-count=0",
                            "a missing Content-Range count");
        completionMutationAt("response-v1 ordinal=2",
                            "content-range-valid=1",
                            "content-range-valid=0",
                            "an invalid Content-Range line");
        completionMutationAt("response-v1 ordinal=2",
                            "content-range-count=1 content-range-valid=1",
                            "content-range-count=2 content-range-valid=0",
                            "an authentic duplicated Content-Range cardinality");
        completionMutationAt("response-v1 ordinal=2", "content-start=0",
                            "content-start=1",
                            "mutated Content-Range coordinates");
        completionMutationAt("response-v1 ordinal=2",
                            "content-total=1048576",
                            "content-total=131071",
                            "an undersized Content-Range total");

        AttributedReplay missingHeadLength = copyAttributedReplay(passing);
        const std::size_t missingHeadLengthEvent = replayMessageIndex(
            missingHeadLength.capture, "response-v1 ordinal=1");
        replaceAll(missingHeadLength.capture.messages[missingHeadLengthEvent],
                   "content-length-count=1 content-length-valid=1 "
                   "declared-content-length=17",
                   "content-length-count=0 content-length-valid=0 "
                   "declared-content-length=0");
        missingHeadLength.capture.messages.erase(
            missingHeadLength.capture.messages.begin() + replayMessageIndex(
                missingHeadLength.capture,
                "CURL_INFO_HEADER_IN: content-length: 17"));
        const std::size_t missingHeadLengthRetry = replayMessageIndex(
            missingHeadLength.capture, "head-transient-retry context=");
        replaceAll(
            missingHeadLength.capture.messages[missingHeadLengthRetry],
            "declared-content-length=17", "declared-content-length=0");
        missingHeadLength.serverRequests.front().contentLength = 0;
        const AttributedTransportProof missingHeadLengthProof =
            buildAttributedTransportProof(
                missingHeadLength.capture,
                missingHeadLength.serverRequests);
        require(missingHeadLengthProof.qualified &&
                    missingHeadLengthProof.headRetries.size() == 1 &&
                    missingHeadLengthProof.headRetries.front().
                        failedDeclaredContentLength == 0,
                "exact-five HEAD without Content-Length was not admitted as "
                "a valid bounded retry");

        AttributedReplay headContentRange = copyAttributedReplay(passing);
        const std::size_t headContentRangeEvent = replayMessageIndex(
            headContentRange.capture, "response-v1 ordinal=1");
        replaceAll(headContentRange.capture.messages[headContentRangeEvent],
                   "content-range-count=0 content-range-valid=0 "
                   "content-start=-1 content-end=-1 content-total=-1",
                   "content-range-count=1 content-range-valid=1 "
                   "content-start=0 content-end=16 content-total=1048576");
        requireAttributedRejected(headContentRange,
                                  "Content-Range on a HEAD response");

        completionMutationAt("response-v1 ordinal=3", "connection=7",
                            "connection=8",
                            "a connection change after HEAD recovery");
        completionMutationAt("response-v1 ordinal=3",
                            "declared-content-length=1048576",
                            "declared-content-length=0",
                            "invalid final HEAD metadata");

        AttributedReplay malformedRawLength = copyAttributedReplay(passing);
        const std::size_t malformedRawLengthLine = replayMessageIndex(
            malformedRawLength.capture,
            "CURL_INFO_HEADER_IN: content-length: 17");
        malformedRawLength.capture.messages[malformedRawLengthLine] =
            "CURL_INFO_HEADER_IN: content-length: malformed";
        requireAttributedRejected(malformedRawLength,
                                  "a malformed raw Content-Length");

        AttributedReplay duplicateRawLength = copyAttributedReplay(passing);
        const std::size_t duplicateRawLengthLine = replayMessageIndex(
            duplicateRawLength.capture,
            "CURL_INFO_HEADER_IN: content-length: 17");
        duplicateRawLength.capture.messages.insert(
            duplicateRawLength.capture.messages.begin() +
                duplicateRawLengthLine,
            duplicateRawLength.capture.messages[duplicateRawLengthLine]);
        requireAttributedRejected(duplicateRawLength,
                                  "a duplicated raw Content-Length");

        AttributedReplay oversizedTransient = copyAttributedReplay(mirror);
        const std::size_t oversizedTransientEvent = replayMessageIndex(
            oversizedTransient.capture, "response-v1 ordinal=2");
        replaceAll(oversizedTransient.capture.messages[oversizedTransientEvent],
                   "declared-content-length=17",
                   "declared-content-length=131073");
        replaceAll(oversizedTransient.capture.messages[oversizedTransientEvent],
                   "actual-body-bytes=17",
                   "actual-body-bytes=131073");
        requireAttributedRejected(oversizedTransient,
                                  "a transient Range body over 128 KiB");

        AttributedReplay spoofedTransientRange = copyAttributedReplay(mirror);
        const std::size_t spoofedTransientEvent = replayMessageIndex(
            spoofedTransientRange.capture, "response-v1 ordinal=2");
        replaceAll(spoofedTransientRange.capture.messages[spoofedTransientEvent],
                   "content-range-count=0 content-range-valid=0 "
                   "content-start=-1 content-end=-1 content-total=-1",
                   "content-range-count=1 content-range-valid=1 "
                   "content-start=1 content-end=131072 content-total=1048576");
        requireAttributedRejected(spoofedTransientRange,
                                  "a spoofed/contradictory transient Range");

        const std::string mirrorStats =
            "{\"methods\":{\"GET\":{\"count\":1,"
            "\"downloaded_bytes\":131089},\"HEAD\":{\"count\":1}}}";
        const HttpProof mirrorHttp = buildHttpProof(
            mirror.capture, mirrorStats, AttributionMode::AttributedV6);
        require(mirrorHttp.attributedTransportQualified &&
                    mirrorHttp.actualHttpBodyBytes == 131089 &&
                    mirrorHttp.statsGetOperationCount == 1,
                "attributed transient Range stats baseline changed");

        AttributedReplay absentTransientLength =
            copyAttributedReplay(mirror);
        const std::size_t absentTransientCompletion = replayMessageIndex(
            absentTransientLength.capture, "response-v1 ordinal=2");
        replaceAll(
            absentTransientLength.capture.messages[absentTransientCompletion],
            "content-length-count=1 content-length-valid=1 "
            "declared-content-length=17",
            "content-length-count=0 content-length-valid=0 "
            "declared-content-length=0");
        absentTransientLength.capture.messages.erase(
            absentTransientLength.capture.messages.begin() +
                replayMessageIndex(
                    absentTransientLength.capture,
                    "CURL_INFO_HEADER_IN: content-length: 17"));
        absentTransientLength.serverRequests[1].contentLength = 0;
        const HttpProof absentTransientLengthProof = buildHttpProof(
            absentTransientLength.capture, mirrorStats,
            AttributionMode::AttributedV6);
        require(absentTransientLengthProof.attributedTransportQualified &&
                    absentTransientLengthProof.declaredTransientBytes == 0 &&
                    absentTransientLengthProof.actualHttpBodyBytes == 131089 &&
                    absentTransientLengthProof.conservativeBodyUpperBound ==
                        131089,
                "Content-Length-absent transient Range undercounted its "
                "conservative body bound");
        bool absentLengthStatsRejected = false;
        try
        {
            static_cast<void>(buildHttpProof(
                absentTransientLength.capture,
                "{\"methods\":{\"GET\":{\"count\":1,"
                "\"downloaded_bytes\":131072},\"HEAD\":{\"count\":1}}}",
                AttributionMode::AttributedV6));
        }
        catch (const std::exception&)
        {
            absentLengthStatsRejected = true;
        }
        require(absentLengthStatsRejected,
                "Content-Length-absent transient Range accepted stats that "
                "omitted its actual body");
        const auto requireStatsRejected = [&mirror](
                const std::string& stats, const std::string& description)
        {
            try
            {
                static_cast<void>(buildHttpProof(
                    mirror.capture, stats, AttributionMode::AttributedV6));
            }
            catch (const std::exception&)
            {
                return;
            }
            fail("AttributedV6 accepted " + description);
        };
        requireStatsRejected(
            "{\"methods\":{\"GET\":{\"count\":1,"
            "\"downloaded_bytes\":131072},\"HEAD\":{\"count\":1}}}",
            "an unaccounted transient body");
        requireStatsRejected(
            "{\"methods\":{\"GET\":{\"count\":1,"
            "\"downloaded_bytes\":131090},\"HEAD\":{\"count\":1}}}",
            "a one-byte VSINetworkStats size mismatch");

        std::uint64_t overflowTarget =
            std::numeric_limits<std::uint64_t>::max();
        bool sumRejected = false;
        try
        {
            checkedAdd(overflowTarget, 1, "synthetic sum");
        }
        catch (const std::exception&)
        {
            sumRejected = true;
        }
        require(sumRejected, "checked byte sum accepted overflow");
        const auto jsonNumberRejected = [](double number)
        {
            try
            {
                static_cast<void>(checkedJsonUnsigned(
                    picojson::value(number), "synthetic JSON integer", 100));
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };
        require(jsonNumberRejected(std::numeric_limits<double>::infinity()) &&
                    jsonNumberRejected(1.5) && jsonNumberRejected(-1.0) &&
                    jsonNumberRejected(101.0),
                "JSON numeric proof accepted nonfinite/fractional/range input");
        const auto wideJsonNumberRejected = [](double number)
        {
            try
            {
                static_cast<void>(checkedJsonUnsigned(
                    picojson::value(number), "wide synthetic JSON integer"));
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };
        require(wideJsonNumberRejected(9007199254740992.0) &&
                    wideJsonNumberRejected(18446744073709551616.0),
                "JSON numeric proof accepted an inexact/overflowing double "
                "before integer conversion");

        AttributedReplay reordered = copyAttributedReplay(passing);
        const std::size_t firstCompletion = replayMessageIndex(
            reordered.capture, "response-v1 ordinal=1");
        const std::size_t secondCompletion = replayMessageIndex(
            reordered.capture, "response-v1 ordinal=2");
        std::swap(reordered.capture.messages[firstCompletion],
                  reordered.capture.messages[secondCompletion]);
        requireAttributedRejected(reordered,
                                  "reordered completion events");

        AttributedReplay wrongContext = copyAttributedReplay(passing);
        const std::size_t wrongContextEvent = replayMessageIndex(
            wrongContext.capture, "response-v1 ordinal=1");
        replaceAll(wrongContext.capture.messages[wrongContextEvent],
                   proof.completions.front().context,
                   "55555555555555555555555555555555");
        requireAttributedRejected(wrongContext,
                                  "a completion with the wrong context");

        AttributedReplay declaredActualMismatch =
            copyAttributedReplay(mirror);
        const std::size_t transientGet = replayMessageIndex(
            declaredActualMismatch.capture, "response-v1 ordinal=2");
        const std::size_t declaredGet =
            declaredActualMismatch.capture.messages[transientGet].find(
                "declared-content-length=17");
        require(declaredGet != std::string::npos,
                "declared/actual mutation source is missing");
        declaredActualMismatch.capture.messages[transientGet].replace(
            declaredGet, std::string("declared-content-length=17").size(),
            "declared-content-length=18");
        requireAttributedRejected(declaredActualMismatch,
                                  "a declared/actual GET mismatch");

        AttributedReplay wrongCorrelation = copyAttributedReplay(passing);
        const std::size_t rawIndex = replayMessageIndex(
            wrongCorrelation.capture, "X-OSGSol-Science-Correlation: ");
        const std::size_t requestField =
            wrongCorrelation.capture.messages[rawIndex].find("/1/1");
        require(requestField != std::string::npos,
                "raw correlation mutation source is missing");
        wrongCorrelation.capture.messages[rawIndex].replace(
            requestField, 4, "/9/1");
        requireAttributedRejected(wrongCorrelation,
                                  "a raw correlation mismatch");

        AttributedReplay duplicateCorrelation = copyAttributedReplay(passing);
        const std::size_t duplicateRawIndex = replayMessageIndex(
            duplicateCorrelation.capture,
            "X-OSGSol-Science-Correlation: ");
        const std::size_t headerTerminator =
            duplicateCorrelation.capture.messages[duplicateRawIndex].find(
                "\r\n\r\n");
        require(headerTerminator != std::string::npos,
                "raw duplicate-correlation mutation source is missing");
        duplicateCorrelation.capture.messages[duplicateRawIndex].insert(
            headerTerminator + 2,
            "X-OSGSol-Science-Correlation: " +
                proof.completions.front().context + "/1/1\r\n");
        requireAttributedRejected(duplicateCorrelation,
                                  "a duplicated raw correlation header");

        AttributedReplay missingCorrelation = copyAttributedReplay(passing);
        const std::size_t missingCorrelationIndex = replayMessageIndex(
            missingCorrelation.capture,
            "X-OSGSol-Science-Correlation: ");
        const std::size_t correlationLine =
            missingCorrelation.capture.messages[missingCorrelationIndex].find(
                "X-OSGSol-Science-Correlation: ");
        const std::size_t correlationEnd =
            missingCorrelation.capture.messages[missingCorrelationIndex].find(
                "\r\n", correlationLine);
        require(correlationLine != std::string::npos &&
                    correlationEnd != std::string::npos,
                "missing-correlation mutation source is missing");
        missingCorrelation.capture.messages[missingCorrelationIndex].erase(
            correlationLine, correlationEnd + 2 - correlationLine);
        requireAttributedRejected(missingCorrelation,
                                  "a missing raw correlation header");

        AttributedReplay pathBearingCorrelation =
            copyAttributedReplay(passing);
        const std::size_t pathCorrelationIndex = replayMessageIndex(
            pathBearingCorrelation.capture,
            "X-OSGSol-Science-Correlation: ");
        const std::size_t pathRequestField =
            pathBearingCorrelation.capture.messages[pathCorrelationIndex].find(
                "/1/1\r\n");
        require(pathRequestField != std::string::npos,
                "path-bearing correlation mutation source is missing");
        pathBearingCorrelation.capture.messages[pathCorrelationIndex].replace(
            pathRequestField, std::string("/1/1").size(),
            "/1/1/secret-path-token");
        requireAttributedRejected(pathBearingCorrelation,
                                  "a path-bearing correlation header");

        AttributedReplay missingRawRequest = copyAttributedReplay(passing);
        missingRawRequest.capture.messages.erase(
            missingRawRequest.capture.messages.begin() +
            replayMessageIndex(missingRawRequest.capture,
                               "CURL_INFO_HEADER_OUT: HEAD"));
        requireAttributedRejected(missingRawRequest,
                                  "a missing raw request tuple");

        AttributedReplay extraRawRequest = copyAttributedReplay(passing);
        const std::size_t rawRequestIndex = replayMessageIndex(
            extraRawRequest.capture, "CURL_INFO_HEADER_OUT: HEAD");
        extraRawRequest.capture.messages.insert(
            extraRawRequest.capture.messages.begin() + rawRequestIndex,
            extraRawRequest.capture.messages[rawRequestIndex]);
        requireAttributedRejected(extraRawRequest,
                                  "an extra raw request tuple");

        AttributedReplay missingRawResponse = copyAttributedReplay(passing);
        missingRawResponse.capture.messages.erase(
            missingRawResponse.capture.messages.begin() +
            replayMessageIndex(missingRawResponse.capture,
                               "CURL_INFO_HEADER_IN: HTTP/2 206"));
        requireAttributedRejected(missingRawResponse,
                                  "a missing raw response status");

        AttributedReplay extraRawResponse = copyAttributedReplay(passing);
        extraRawResponse.capture.messages.push_back(
            "CURL_INFO_HEADER_IN: HTTP/2 500");
        extraRawResponse.capture.messages.push_back(
            "CURL_INFO_HEADER_IN: content-length: 17");
        extraRawResponse.capture.messages.push_back(
            "CURL_INFO_HEADER_IN: ");
        requireAttributedRejected(extraRawResponse,
                                  "an extra raw response tuple");

        AttributedReplay missingDecisionContext =
            copyAttributedReplay(passing);
        missingDecisionContext.capture.messages.push_back(
            "VSICURL: ParallelHeadRange: transient-retry status=500");
        requireAttributedRejected(missingDecisionContext,
                                  "a decision without context");

        AttributedReplay missingServer = copyAttributedReplay(passing);
        missingServer.serverRequests.pop_back();
        requireAttributedRejected(missingServer,
                                  "a missing server-oracle request");

        AttributedReplay extraServer = copyAttributedReplay(passing);
        extraServer.serverRequests.push_back(
            extraServer.serverRequests.front());
        requireAttributedRejected(extraServer,
                                  "an extra server-oracle request");

        AttributedReplay wrongServer = copyAttributedReplay(passing);
        wrongServer.serverRequests.front().range = "bytes=0-131071";
        requireAttributedRejected(wrongServer,
                                  "a server-oracle role/Range mismatch");

        AttributedReplay missingServerEnd = copyAttributedReplay(passing);
        missingServerEnd.serverRequests.front().endCount = 0;
        requireAttributedRejected(missingServerEnd,
                                  "a missing server stream_end event");

        AttributedReplay duplicateServerHeaders = copyAttributedReplay(passing);
        duplicateServerHeaders.serverRequests.front().responseCount = 2;
        requireAttributedRejected(duplicateServerHeaders,
                                  "duplicate server response headers");

        AttributedReplay wrongServerStatus = copyAttributedReplay(passing);
        wrongServerStatus.serverRequests.front().status = 200;
        requireAttributedRejected(wrongServerStatus,
                                  "a mismatched server response status");

        AttributedReplay wrongServerBody = copyAttributedReplay(passing);
        wrongServerBody.serverRequests[1].attemptedBodyBytes = 1;
        requireAttributedRejected(wrongServerBody,
                                  "mismatched server body bytes");

        AttributedReplay wrongServerIdentity = copyAttributedReplay(passing);
        wrongServerIdentity.serverRequests.front().correlation =
            "88888888888888888888888888888888/1/1";
        requireAttributedRejected(wrongServerIdentity,
                                  "mismatched server correlation identity");

        AttributedReplay earlyPublication = copyAttributedReplay(passing);
        const std::size_t publication = replayMessageIndex(
            earlyPublication.capture, "published context=");
        const std::string event = earlyPublication.capture.messages[publication];
        earlyPublication.capture.messages.erase(
            earlyPublication.capture.messages.begin() + publication);
        earlyPublication.capture.messages.insert(
            earlyPublication.capture.messages.begin(), event);
        requireAttributedRejected(earlyPublication,
                                  "publication before completion");

        AttributedReplay duplicatePublication = copyAttributedReplay(passing);
        const std::size_t publicationIndex = replayMessageIndex(
            duplicatePublication.capture, "published context=");
        duplicatePublication.capture.messages.insert(
            duplicatePublication.capture.messages.begin() + publicationIndex,
            duplicatePublication.capture.messages[publicationIndex]);
        requireAttributedRejected(duplicatePublication,
                                  "duplicate publication");

        AttributedReplay fallbackBecamePublication =
            copyAttributedReplay(fallback);
        const std::size_t fallbackDecision = replayMessageIndex(
            fallbackBecamePublication.capture, "fallback context=");
        fallbackBecamePublication.capture.messages[fallbackDecision] =
            "VSICURL: ParallelHeadRange: published context=" +
            fallbackProof.completions.front().context +
            " request=1 attempt=1";
        requireAttributedRejected(fallbackBecamePublication,
                                  "fallback changed to publication");

        AttributedReplay completionAfterRetry = copyAttributedReplay(passing);
        const std::size_t failedCompletion = replayMessageIndex(
            completionAfterRetry.capture, "response-v1 ordinal=1");
        const std::string delayedCompletion =
            completionAfterRetry.capture.messages[failedCompletion];
        completionAfterRetry.capture.messages.erase(
            completionAfterRetry.capture.messages.begin() + failedCompletion);
        const std::size_t delayedRetry = replayMessageIndex(
            completionAfterRetry.capture, "head-transient-retry context=");
        completionAfterRetry.capture.messages.insert(
            completionAfterRetry.capture.messages.begin() + delayedRetry + 1,
            delayedCompletion);
        requireAttributedRejected(completionAfterRetry,
                                  "completion emitted after its retry");

        AttributedReplay falseRangeRetry = copyAttributedReplay(passing);
        const std::size_t falseRetry = replayMessageIndex(
            falseRangeRetry.capture, "head-transient-retry context=");
        falseRangeRetry.capture.messages[falseRetry] =
            "VSICURL: ParallelHeadRange: transient-retry context=" +
            proof.completions.front().context +
            " request=1 failed-attempt=1 scheduled-attempt=2 "
            "range=bytes=0-131071 status=500 bytes=17 delay-ms=100 "
            "connection=7 http=2";
        requireAttributedRejected(falseRangeRetry,
                                  "a HEAD failure matched to a Range retry");

        AttributedReplay wrongRetry = copyAttributedReplay(passing);
        const std::size_t retry = replayMessageIndex(
            wrongRetry.capture, "head-transient-retry context=");
        const std::size_t attempt =
            wrongRetry.capture.messages[retry].find("scheduled-attempt=2");
        require(attempt != std::string::npos,
                "HEAD retry mutation source is missing");
        wrongRetry.capture.messages[retry].replace(
            attempt, std::string("scheduled-attempt=2").size(),
            "scheduled-attempt=3");
        requireAttributedRejected(wrongRetry,
                                  "a skipped scheduled HEAD attempt");

        AttributedReplay wrongRetryDelay = copyAttributedReplay(passing);
        const std::size_t retryDelay = replayMessageIndex(
            wrongRetryDelay.capture, "head-transient-retry context=");
        const std::size_t delay =
            wrongRetryDelay.capture.messages[retryDelay].find("delay-ms=100");
        require(delay != std::string::npos,
                "HEAD retry delay mutation source is missing");
        wrongRetryDelay.capture.messages[retryDelay].replace(
            delay, std::string("delay-ms=100").size(), "delay-ms=99");
        requireAttributedRejected(wrongRetryDelay,
                                  "an invalid HEAD retry delay");

        AttributedReplay crossContextRetry = copyAttributedReplay(nested);
        const std::size_t crossRetry = replayMessageIndex(
            crossContextRetry.capture,
            "head-transient-retry context=" + firstContext);
        replaceAll(crossContextRetry.capture.messages[crossRetry],
                   firstContext, secondContext);
        requireAttributedRejected(crossContextRetry,
                                  "a cross-context retry decision");
    }

    void verifyHttpParserRegression()
    {
        DebugCapture capture;
        capture.messages = {
            "CURL_INFO_HEADER_OUT: CONNECT data.example:443 HTTP/1.1\r\n"
            "Host: data.example:443\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/1.1 200 Connection established\r",
            "CURL_INFO_HEADER_IN: \r",
            "CURL_INFO_HEADER_OUT: HEAD /tile.tiff HTTP/1.1\r\nHost: data.example\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 200\r",
            "CURL_INFO_HEADER_IN: content-length: 1000\r",
            "CURL_INFO_HEADER_IN: \r",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
            "Range: bytes=10-19\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 500\r",
            "CURL_INFO_HEADER_IN: content-length: 17\r",
            "CURL_INFO_HEADER_IN: \r",
            "HTTP error code for https://data.example/tile.tiff range 10-19: 500. "
            "Retrying again in 0.1 secs",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
            "Range: bytes=10-19\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 206\r",
            "CURL_INFO_HEADER_IN: content-range: bytes 10-19/1000\r",
            "CURL_INFO_HEADER_IN: content-length: 10\r",
            "CURL_INFO_HEADER_IN: \r",
            "VSICURL: Got response_code=206",
        };
        const std::string stats =
            "{\"methods\":{\"GET\":{\"count\":1,\"downloaded_bytes\":10},"
            "\"HEAD\":{\"count\":1}}}";
        const HttpProof proof = buildHttpProof(
            capture, stats, AttributionMode::LegacyFrozen);
        require(proof.actualGetCount == 2 && proof.actualHeadCount == 1 &&
                proof.successfulGetCount == 1 && proof.transientRetryCount == 1 &&
                proof.transientRetryCodes == std::map<int, int>({{500, 1}}) &&
                proof.successfulRangeBytes == 10 &&
                proof.declaredTransientBytes == 17 &&
                proof.actualHttpBodyBytes == 10 &&
                proof.conservativeBodyUpperBound == 27 && proof.sourceSize == 1000,
                "proxy/retry/header parser regression fixture failed");
        require(proof.successfulByteIntervals ==
                    std::vector<std::pair<std::uint64_t, std::uint64_t>>({{10, 19}}),
                "HTTP parser did not retain the successful byte interval");

        DebugCapture immediate;
        immediate.messages = capture.messages;
        immediate.timestamps = capture.timestamps;
        immediate.messages.insert(
            immediate.messages.begin() + 12,
            "VSICURL: ReadMultiRange: immediate-retry "
            "range=bytes=10-19 status=500 bytes=17 attempt=1 "
            "delay-ms=100 connection=7 http=2");
        const auto immediateEpoch = std::chrono::steady_clock::time_point();
        immediate.timestamps.resize(immediate.messages.size());
        for (std::size_t index = 0; index < immediate.timestamps.size(); ++index)
        {
            immediate.timestamps[index] = immediateEpoch +
                std::chrono::milliseconds(static_cast<long long>(index));
        }
        immediate.timestamps[10] = immediateEpoch + std::chrono::milliseconds(10);
        immediate.timestamps[11] = immediateEpoch + std::chrono::milliseconds(20);
        immediate.timestamps[12] = immediateEpoch + std::chrono::milliseconds(30);
        for (std::size_t index = 13; index < immediate.timestamps.size(); ++index)
        {
            immediate.timestamps[index] = immediateEpoch +
                std::chrono::milliseconds(
                    130 + static_cast<long long>(index - 13));
        }
        const HttpProof immediateProof = buildHttpProof(
            immediate, stats, AttributionMode::LegacyFrozen);
        require(immediateProof.transientRetryCount == 1 &&
                    immediateProof.immediateTransientRetryCount == 1 &&
                    immediateProof.immediateTransientRetryBytes == 17 &&
                    immediateProof.immediateTransientRetryCodes ==
                        std::map<int, int>{{500, 1}} &&
                    immediateProof.immediateRetries.size() == 1 &&
                    immediateProof.immediateRetries.front().range ==
                        "bytes=10-19" &&
                    immediateProof.immediateRetries.front().connectionId == 7 &&
                    serializeProof(immediateProof).find(
                        "\"immediate_transient_retry_count\": 1") !=
                        std::string::npos,
                "immediate retry parser/schema regression fixture failed");

        DebugCapture simultaneousImmediate;
        simultaneousImmediate.messages = {
            "CURL_INFO_HEADER_OUT: HEAD /tile.tiff HTTP/2\r\n"
            "Host: data.example\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 200\r",
            "CURL_INFO_HEADER_IN: content-length: 1000\r",
            "CURL_INFO_HEADER_IN: \r",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\n"
            "Host: data.example\r\nRange: bytes=10-19\r\n\r\n",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\n"
            "Host: data.example\r\nRange: bytes=20-29\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 500\r",
            "CURL_INFO_HEADER_IN: content-length: 17\r",
            "CURL_INFO_HEADER_IN: \r",
            "CURL_INFO_HEADER_IN: HTTP/2 500\r",
            "CURL_INFO_HEADER_IN: content-length: 17\r",
            "CURL_INFO_HEADER_IN: \r",
            "HTTP error code for https://data.example/tile.tiff range 10-19: 500. "
            "Retrying again in 0.1 secs",
            "VSICURL: ReadMultiRange: immediate-retry "
            "range=bytes=10-19 status=500 bytes=17 attempt=1 "
            "delay-ms=100 connection=7 http=2",
            "HTTP error code for https://data.example/tile.tiff range 20-29: 500. "
            "Retrying again in 0.1 secs",
            "VSICURL: ReadMultiRange: immediate-retry "
            "range=bytes=20-29 status=500 bytes=17 attempt=1 "
            "delay-ms=100 connection=7 http=2",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\n"
            "Host: data.example\r\nRange: bytes=10-19\r\n\r\n",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\n"
            "Host: data.example\r\nRange: bytes=20-29\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 206\r",
            "CURL_INFO_HEADER_IN: content-range: bytes 10-19/1000\r",
            "CURL_INFO_HEADER_IN: content-length: 10\r",
            "CURL_INFO_HEADER_IN: \r",
            "VSICURL: Got response_code=206",
            "CURL_INFO_HEADER_IN: HTTP/2 206\r",
            "CURL_INFO_HEADER_IN: content-range: bytes 20-29/1000\r",
            "CURL_INFO_HEADER_IN: content-length: 10\r",
            "CURL_INFO_HEADER_IN: \r",
            "VSICURL: Got response_code=206",
        };
        simultaneousImmediate.timestamps.resize(
            simultaneousImmediate.messages.size());
        for (std::size_t index = 0;
             index < simultaneousImmediate.timestamps.size(); ++index)
        {
            simultaneousImmediate.timestamps[index] = immediateEpoch +
                std::chrono::milliseconds(static_cast<long long>(index));
        }
        simultaneousImmediate.timestamps[12] = immediateEpoch +
            std::chrono::milliseconds(20);
        simultaneousImmediate.timestamps[13] = immediateEpoch +
            std::chrono::milliseconds(30);
        simultaneousImmediate.timestamps[14] = immediateEpoch +
            std::chrono::milliseconds(31);
        simultaneousImmediate.timestamps[15] = immediateEpoch +
            std::chrono::milliseconds(32);
        simultaneousImmediate.timestamps[16] = immediateEpoch +
            std::chrono::milliseconds(130);
        simultaneousImmediate.timestamps[17] = immediateEpoch +
            std::chrono::milliseconds(132);
        for (std::size_t index = 18;
             index < simultaneousImmediate.timestamps.size(); ++index)
        {
            simultaneousImmediate.timestamps[index] = immediateEpoch +
                std::chrono::milliseconds(
                    133 + static_cast<long long>(index - 18));
        }
        const std::string simultaneousStats =
            "{\"methods\":{\"GET\":{\"count\":2,\"downloaded_bytes\":20},"
            "\"HEAD\":{\"count\":1}}}";
        const HttpProof simultaneousProof =
            buildHttpProof(simultaneousImmediate, simultaneousStats,
                           AttributionMode::LegacyFrozen);
        require(simultaneousProof.actualGetCount == 4 &&
                    simultaneousProof.successfulGetCount == 2 &&
                    simultaneousProof.transientRetryCount == 2 &&
                    simultaneousProof.transientRetryCodes ==
                        std::map<int, int>{{500, 2}} &&
                    simultaneousProof.immediateTransientRetryCount == 2 &&
                    simultaneousProof.immediateTransientRetryBytes == 34 &&
                    simultaneousProof.immediateTransientRetryCodes ==
                        std::map<int, int>{{500, 2}} &&
                    simultaneousProof.immediateRetries.size() == 2 &&
                    simultaneousProof.immediateRetries[0].range ==
                        "bytes=10-19" &&
                    simultaneousProof.immediateRetries[1].range ==
                        "bytes=20-29",
                "simultaneous identical transient responses did not reconcile "
                "as an immediate retry multiset");

        const auto isRejected = [&stats](const DebugCapture& candidate)
        {
            try
            {
                static_cast<void>(buildHttpProof(
                    candidate, stats, AttributionMode::LegacyFrozen));
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };

        DebugCapture malformedImmediate;
        malformedImmediate.messages = immediate.messages;
        malformedImmediate.timestamps = immediate.timestamps;
        malformedImmediate.messages[12].replace(
            malformedImmediate.messages[12].find("http=2"), 6, "http=1");
        require(isRejected(malformedImmediate),
                "HTTP parser accepted a malformed immediate retry event");

        DebugCapture duplicateImmediate;
        duplicateImmediate.messages = immediate.messages;
        duplicateImmediate.timestamps = immediate.timestamps;
        duplicateImmediate.messages.insert(
            duplicateImmediate.messages.begin() + 13,
            duplicateImmediate.messages[12]);
        duplicateImmediate.timestamps.insert(
            duplicateImmediate.timestamps.begin() + 13,
            duplicateImmediate.timestamps[12]);
        require(isRejected(duplicateImmediate),
                "HTTP parser accepted a duplicate immediate retry event");

        DebugCapture unmatchedImmediate;
        unmatchedImmediate.messages = immediate.messages;
        unmatchedImmediate.timestamps = immediate.timestamps;
        unmatchedImmediate.messages[12].replace(
            unmatchedImmediate.messages[12].find("status=500"), 10,
            "status=503");
        require(isRejected(unmatchedImmediate),
                "HTTP parser accepted an unmatched immediate retry event");

        DebugCapture earlyImmediate;
        earlyImmediate.messages = immediate.messages;
        earlyImmediate.timestamps = immediate.timestamps;
        earlyImmediate.timestamps[13] =
            earlyImmediate.timestamps[12] + std::chrono::milliseconds(98);
        require(isRejected(earlyImmediate),
                "HTTP parser accepted an early immediate retry request");

        DebugCapture unlisted;
        unlisted.messages = capture.messages;
        unlisted.messages[8] = "CURL_INFO_HEADER_IN: HTTP/2 501\r";
        unlisted.messages[11] =
            "HTTP error code for https://data.example/tile.tiff range 10-19: 501. "
            "Retrying again in 0.1 secs";
        require(isRejected(unlisted),
                "HTTP parser accepted an unlisted transient response code");

        DebugCapture excessive;
        excessive.messages.assign(capture.messages.begin(), capture.messages.begin() + 7);
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            excessive.messages.push_back(
                "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
                "Range: bytes=10-19\r\n\r\n");
            excessive.messages.push_back("CURL_INFO_HEADER_IN: HTTP/2 500\r");
            excessive.messages.push_back("CURL_INFO_HEADER_IN: content-length: 0\r");
            excessive.messages.push_back("CURL_INFO_HEADER_IN: \r");
            excessive.messages.push_back(
                "HTTP error code for https://data.example/tile.tiff range 10-19: 500. "
                "Retrying again in 0.1 secs");
        }
        excessive.messages.insert(excessive.messages.end(), capture.messages.begin() + 12,
                                  capture.messages.end());
        require(isRejected(excessive),
                "HTTP parser accepted more than three transient retries");

        DebugCapture multiplexed;
        multiplexed.messages = {
            "CURL_INFO_HEADER_OUT: HEAD /tile.tiff HTTP/2\r\nHost: data.example\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 200\r",
            "CURL_INFO_HEADER_IN: content-length: 1000\r",
            "CURL_INFO_HEADER_IN: \r",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
            "Range: bytes=10-19\r\n\r\n",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
            "Range: bytes=20-29\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 500\r",
            "CURL_INFO_HEADER_IN: content-length: 17\r",
            "CURL_INFO_HEADER_IN: \r",
            "CURL_INFO_HEADER_IN: HTTP/2 206\r",
            "CURL_INFO_HEADER_IN: content-range: bytes 20-29/1000\r",
            "CURL_INFO_HEADER_IN: content-length: 10\r",
            "CURL_INFO_HEADER_IN: \r",
            "HTTP error code for https://data.example/tile.tiff range 10-19: 500. "
            "Retrying again in 0.1 secs",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
            "Range: bytes=10-19\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 206\r",
            "CURL_INFO_HEADER_IN: content-range: bytes 10-19/1000\r",
            "CURL_INFO_HEADER_IN: content-length: 10\r",
            "CURL_INFO_HEADER_IN: \r",
            "VSICURL: Got response_code=206",
            "VSICURL: Got response_code=206",
        };
        const std::string multiplexedStats =
            "{\"methods\":{\"GET\":{\"count\":2,\"downloaded_bytes\":20},"
            "\"HEAD\":{\"count\":1}}}";
        const HttpProof multiplexedProof = buildHttpProof(
            multiplexed, multiplexedStats, AttributionMode::LegacyFrozen);
        require(multiplexedProof.actualGetCount == 3 &&
                multiplexedProof.successfulGetCount == 2 &&
                multiplexedProof.transientRetryCount == 1 &&
                multiplexedProof.successfulRangeBytes == 20 &&
                multiplexedProof.declaredTransientBytes == 17 &&
                multiplexedProof.actualHttpBodyBytes == 20 &&
                multiplexedProof.conservativeBodyUpperBound == 37,
                "out-of-order HTTP/2 multiplex retry regression fixture failed");
        require(multiplexedProof.successfulByteIntervals ==
                    std::vector<std::pair<std::uint64_t, std::uint64_t>>(
                        {{20, 29}, {10, 19}}),
                "successful byte intervals did not retain response chronology");

        DebugCapture highOffsetFirst;
        highOffsetFirst.messages = {
            "CURL_INFO_HEADER_OUT: HEAD /tile.tiff HTTP/2\r\nHost: data.example\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 200\r",
            "CURL_INFO_HEADER_IN: content-length: 2000000\r",
            "CURL_INFO_HEADER_IN: \r",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
            "Range: bytes=900000-900009\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 206\r",
            "CURL_INFO_HEADER_IN: content-range: bytes 900000-900009/2000000\r",
            "CURL_INFO_HEADER_IN: content-length: 10\r",
            "CURL_INFO_HEADER_IN: \r",
            "VSICURL: Got response_code=206",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
            "Range: bytes=0-131071\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 206\r",
            "CURL_INFO_HEADER_IN: content-range: bytes 0-131071/2000000\r",
            "CURL_INFO_HEADER_IN: content-length: 131072\r",
            "CURL_INFO_HEADER_IN: \r",
            "VSICURL: Got response_code=206",
        };
        const std::string highOffsetFirstStats =
            "{\"methods\":{\"GET\":{\"count\":2,\"downloaded_bytes\":131082},"
            "\"HEAD\":{\"count\":1}}}";
        const HttpProof highOffsetFirstProof =
            buildHttpProof(highOffsetFirst, highOffsetFirstStats,
                           AttributionMode::LegacyFrozen);
        require(highOffsetFirstProof.successfulByteIntervals ==
                    std::vector<std::pair<std::uint64_t, std::uint64_t>>(
                        {{900000, 900009}, {0, 131071}}),
                "HTTP parser changed successful byte-interval chronology");
        require(highOffsetFirstProof.actualGetCount == 2 &&
                highOffsetFirstProof.successfulGetCount == 2 &&
                highOffsetFirstProof.successfulRangeBytes == 131082 &&
                highOffsetFirstProof.actualHttpBodyBytes == 131082 &&
                highOffsetFirstProof.conservativeBodyUpperBound == 131082,
                "high-offset-first parser fixture changed counts or byte budgets");
        require(serializeProof(highOffsetFirstProof).find(
                    "[[900000,900009],[0,131071]]") != std::string::npos,
                "serialized proof changed successful byte-interval chronology");
        bool highOffsetFirstRejected = false;
        try
        {
            verifyOptimizedMetadataIntervals(highOffsetFirstProof);
        }
        catch (const std::exception&)
        {
            highOffsetFirstRejected = true;
        }
        require(highOffsetFirstRejected,
                "optimized metadata check accepted a high-offset first HTTP 206");

        HttpProof non128KiB = highOffsetFirstProof;
        non128KiB.successfulByteIntervals = {{0, 65535}, {900000, 900009}};
        non128KiB.successfulGetCount = 2;
        bool liveProfileRejected = false;
        try
        {
            verifyLiveProfileProof(RangeProfile::Optimized, non128KiB);
        }
        catch (const std::exception&)
        {
            liveProfileRejected = true;
        }
        require(liveProfileRejected,
                "optimized live profile accepted a non-128 KiB first interval");

        DebugCapture rangedGet200;
        rangedGet200.messages.assign(capture.messages.begin(), capture.messages.begin() + 7);
        rangedGet200.messages.push_back(
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
            "Range: bytes=0-999\r\n\r\n");
        rangedGet200.messages.push_back("CURL_INFO_HEADER_IN: HTTP/2 200\r");
        rangedGet200.messages.push_back("CURL_INFO_HEADER_IN: content-length: 1000\r");
        rangedGet200.messages.push_back("CURL_INFO_HEADER_IN: \r");
        require(isRejected(rangedGet200),
                "HTTP parser accepted HTTP 200 for an emitted ranged GET");

        DebugCapture unknown;
        unknown.messages = capture.messages;
        unknown.messages[3] =
            "CURL_INFO_HEADER_OUT: POST /tile.tiff HTTP/1.1\r\nHost: data.example\r\n\r\n";
        require(isRejected(unknown), "HTTP parser accepted an unknown outbound method");

        DebugCapture prefetch;
        const auto epoch = std::chrono::steady_clock::time_point();
        prefetch.messages = {
            "CURL_INFO_TEXT: [HTTP/2] [1] OPENED stream for https://data.example/tile.tiff",
            "CURL_INFO_HEADER_OUT: HEAD /tile.tiff HTTP/2\r\nHost: data.example\r\n\r\n",
            "CURL_INFO_TEXT: Multiplexed connection found",
            "CURL_INFO_TEXT: Re-using existing connection with proxy proxy.example",
            "CURL_INFO_TEXT: [HTTP/2] [3] OPENED stream for https://data.example/tile.tiff",
            "CURL_INFO_HEADER_OUT: GET /tile.tiff HTTP/2\r\nHost: data.example\r\n"
            "Range: bytes=0-131071\r\n\r\n",
            "CURL_INFO_HEADER_IN: HTTP/2 206\r",
            "CURL_INFO_HEADER_IN: content-range: bytes 0-131071/1000000\r",
            "CURL_INFO_HEADER_IN: content-length: 131072\r",
            "CURL_INFO_HEADER_IN: \r",
            "CURL_INFO_TEXT: Connection #7 to host proxy.example left intact",
            "CURL_INFO_HEADER_IN: HTTP/2 200\r",
            "CURL_INFO_HEADER_IN: content-length: 1000000\r",
            "CURL_INFO_HEADER_IN: \r",
            "CURL_INFO_TEXT: Connection #7 to host proxy.example left intact",
            "VSICURL: ParallelHeadRange: detach-success=range",
            "VSICURL: ParallelHeadRange: detach-success=head",
            "VSICURL: ParallelHeadRange: transport head-connection=7 "
            "range-connection=7 head-http=2 range-http=2",
            "ParallelHeadRange: published",
        };
        prefetch.timestamps = {
            epoch + std::chrono::milliseconds(1),
            epoch + std::chrono::milliseconds(2),
            epoch + std::chrono::milliseconds(3),
            epoch + std::chrono::milliseconds(4),
            epoch + std::chrono::milliseconds(5),
            epoch + std::chrono::milliseconds(6),
            epoch + std::chrono::milliseconds(7),
            epoch + std::chrono::milliseconds(8),
            epoch + std::chrono::milliseconds(9),
            epoch + std::chrono::milliseconds(10),
            epoch + std::chrono::milliseconds(11),
            epoch + std::chrono::milliseconds(12),
            epoch + std::chrono::milliseconds(13),
            epoch + std::chrono::milliseconds(14),
            epoch + std::chrono::milliseconds(15),
            epoch + std::chrono::milliseconds(16),
            epoch + std::chrono::milliseconds(17),
            epoch + std::chrono::milliseconds(18),
            epoch + std::chrono::milliseconds(19),
        };
        HttpProof prefetchHttp;
        prefetchHttp.actualHeadCount = 1;
        prefetchHttp.actualGetCount = 1;
        prefetchHttp.successfulGetCount = 1;
        prefetchHttp.coordinatorLogicalGetCount = 1;
        prefetchHttp.coordinatorLogicalGetBytes = 131072;
        prefetchHttp.successfulByteIntervals = {{0, 131071}};
        prefetchHttp.metadataPrefetch = buildMetadataPrefetchProof(prefetch, prefetchHttp);
        const std::string expectedMetadata =
            "  \"metadata_prefetch\": {\n"
            "    \"enabled\": true,\n"
            "    \"attributed_transport_qualified\": false,\n"
            "    \"head_request_count\": 1,\n"
            "    \"range_request_count\": 1,\n"
            "    \"range_start\": 0,\n"
            "    \"range_end\": 131071,\n"
            "    \"head_http_version\": 2,\n"
            "    \"range_http_version\": 2,\n"
            "    \"shared_connection\": true,\n"
            "    \"requests_overlapped\": true,\n"
            "    \"cache_published\": true,\n"
            "    \"coordinator_retries\": [],\n"
            "    \"head_retries\": [],\n"
            "    \"fallback_reason\": \"\"\n"
            "  }";
        require(serializeProof(prefetchHttp).find(expectedMetadata) != std::string::npos,
                "metadata_prefetch exact serialized object changed");
        picojson::value serializedPrefetchProof;
        require(picojson::parse(serializedPrefetchProof,
                    serializeProof(prefetchHttp)).empty() &&
                    serializedPrefetchProof.is<picojson::object>(),
                "serialized prefetch proof is not valid JSON");
        const picojson::object& serializedPrefetchObject =
            serializedPrefetchProof.get<picojson::object>();
        require(field(serializedPrefetchObject,
                    "coordinator_transient_retry_count").get<double>() == 0.0 &&
                    field(serializedPrefetchObject,
                    "coordinator_transient_retry_bytes").get<double>() == 0.0 &&
                    field(serializedPrefetchObject,
                    "coordinator_transient_retry_codes")
                        .is<picojson::object>() &&
                    field(field(serializedPrefetchObject, "metadata_prefetch")
                        .get<picojson::object>(), "coordinator_retries")
                        .is<picojson::array>(),
                "serialized prefetch proof omitted coordinator retry fields "
                "or changed their types");

        const auto prefetchRejected = [&prefetchHttp](const DebugCapture& candidate)
        {
            try
            {
                static_cast<void>(buildMetadataPrefetchProof(candidate, prefetchHttp));
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };
        DebugCapture missingTimestamp;
        missingTimestamp.messages = prefetch.messages;
        missingTimestamp.timestamps.assign(prefetch.timestamps.begin(),
                                           prefetch.timestamps.end() - 1);
        require(prefetchRejected(missingTimestamp),
                "metadata prefetch proof accepted missing timestamps");
        DebugCapture distinctConnection;
        distinctConnection.messages = prefetch.messages;
        distinctConnection.timestamps = prefetch.timestamps;
        distinctConnection.messages[17] =
            "VSICURL: ParallelHeadRange: transport head-connection=7 "
            "range-connection=8 head-http=2 range-http=2";
        require(prefetchRejected(distinctConnection),
                "metadata prefetch proof accepted distinct connections");
        DebugCapture missingGenericMarker;
        missingGenericMarker.messages = prefetch.messages;
        missingGenericMarker.timestamps = prefetch.timestamps;
        missingGenericMarker.messages.erase(
            missingGenericMarker.messages.begin() + 10);
        missingGenericMarker.timestamps.erase(
            missingGenericMarker.timestamps.begin() + 10);
        require(!prefetchRejected(missingGenericMarker),
                "metadata prefetch proof required a generic connection marker");
        DebugCapture noOverlap;
        noOverlap.messages = prefetch.messages;
        noOverlap.timestamps = prefetch.timestamps;
        noOverlap.timestamps[5] = epoch + std::chrono::milliseconds(15);
        require(prefetchRejected(noOverlap),
                "metadata prefetch proof accepted a GET after HEAD completion");
        DebugCapture ambiguousPublication;
        ambiguousPublication.messages = prefetch.messages;
        ambiguousPublication.timestamps = prefetch.timestamps;
        ambiguousPublication.messages.push_back("ParallelHeadRange: published");
        ambiguousPublication.timestamps.push_back(epoch + std::chrono::milliseconds(20));
        require(prefetchRejected(ambiguousPublication),
                "metadata prefetch proof accepted ambiguous cache publication");
        DebugCapture duplicateHead;
        duplicateHead.messages = prefetch.messages;
        duplicateHead.timestamps = prefetch.timestamps;
        duplicateHead.messages.insert(duplicateHead.messages.begin() + 2,
            "CURL_INFO_HEADER_OUT: HEAD /tile.tiff HTTP/2\r\n"
            "Host: data.example\r\n\r\n");
        duplicateHead.timestamps.insert(duplicateHead.timestamps.begin() + 2,
                                        epoch + std::chrono::milliseconds(2));
        require(prefetchRejected(duplicateHead),
                "metadata prefetch proof accepted duplicate HEAD requests");
        DebugCapture http1Range;
        http1Range.messages = prefetch.messages;
        http1Range.timestamps = prefetch.timestamps;
        http1Range.messages[5].replace(
            http1Range.messages[5].find("HTTP/2"), 6, "HTTP/1.1");
        require(prefetchRejected(http1Range),
                "metadata prefetch proof accepted an HTTP/1.1 Range");
        DebugCapture fallback;
        fallback.messages = prefetch.messages;
        fallback.timestamps = prefetch.timestamps;
        fallback.messages.push_back("ParallelHeadRange: fallback=protocol");
        fallback.timestamps.push_back(epoch + std::chrono::milliseconds(20));
        require(prefetchRejected(fallback),
                "metadata prefetch proof accepted a runtime fallback");
        DebugCapture missingRangeResponse;
        missingRangeResponse.messages = prefetch.messages;
        missingRangeResponse.timestamps = prefetch.timestamps;
        missingRangeResponse.messages[7] =
            "CURL_INFO_HEADER_IN: content-type: application/octet-stream\r";
        require(prefetchRejected(missingRangeResponse),
                "metadata prefetch proof accepted missing exact HTTP 206 evidence");
    }

    void verifyPrefetchReplayRegression()
    {
        const std::filesystem::path fixtureRoot =
            std::filesystem::path(__FILE__).parent_path() / "data" / "science";
        DebugCapture replay;
        loadReplayCapture(fixtureRoot / "prefetch_nvidia_partial_trace.log", replay);
        const std::string stats = loadReplayText(
            fixtureRoot / "prefetch_nvidia_partial_stats.json");
        insertReplayEventBefore(
            replay, "ParallelHeadRange: published",
            "VSICURL: ParallelHeadRange: logical-get-complete bytes=131072",
            std::chrono::nanoseconds(474401786400000));
        insertReplayEventBefore(
            replay, "ParallelHeadRange: logical-get-complete",
            "VSICURL: ParallelHeadRange: transport head-connection=0 "
            "range-connection=0 head-http=2 range-http=2",
            std::chrono::nanoseconds(474401786394000));

        HttpProof proof = buildHttpProof(
            replay, stats, AttributionMode::LegacyFrozen);
        require(proof.statsGetOperationCount == 2,
                "prefetch replay did not preserve two logical GET operations");
        proof.metadataPrefetch = buildMetadataPrefetchProof(replay, proof);
        require(proof.metadataPrefetch.sharedConnection,
                "prefetch replay did not prove the authoritative shared connection");

        const auto messageIndex = [](const DebugCapture& capture,
                                     const std::string& needle)
        {
            const auto iterator = std::find_if(
                capture.messages.begin(), capture.messages.end(),
                [&needle](const std::string& message)
                {
                    return message.find(needle) != std::string::npos;
                });
            require(iterator != capture.messages.end(),
                    "prefetch replay mutation target is missing");
            return static_cast<std::size_t>(
                std::distance(capture.messages.begin(), iterator));
        };
        const auto copyCapture = [](const DebugCapture& source,
                                    DebugCapture& destination)
        {
            destination.messages = source.messages;
            destination.timestamps = source.timestamps;
        };
        const auto eraseEvent = [&messageIndex](DebugCapture& capture,
                                                const std::string& needle)
        {
            const std::size_t index = messageIndex(capture, needle);
            capture.messages.erase(capture.messages.begin() + index);
            capture.timestamps.erase(capture.timestamps.begin() + index);
        };
        const auto httpRejected = [&stats](const DebugCapture& candidate)
        {
            try
            {
                static_cast<void>(buildHttpProof(
                    candidate, stats, AttributionMode::LegacyFrozen));
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };
        const auto metadataRejected = [&proof](const DebugCapture& candidate)
        {
            try
            {
                static_cast<void>(buildMetadataPrefetchProof(candidate, proof));
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };

        DebugCapture transportBeforeDetach;
        copyCapture(replay, transportBeforeDetach);
        const std::string transportEvent = transportBeforeDetach.messages[
            messageIndex(transportBeforeDetach, "ParallelHeadRange: transport")];
        eraseEvent(transportBeforeDetach, "ParallelHeadRange: transport");
        insertReplayEventBefore(
            transportBeforeDetach, "ParallelHeadRange: detach-success=range",
            transportEvent, std::chrono::nanoseconds(474401786390000));
        require(metadataRejected(transportBeforeDetach),
                "prefetch replay accepted transport before successful detaches");

        DebugCapture missingLogical;
        copyCapture(replay, missingLogical);
        eraseEvent(missingLogical, "ParallelHeadRange: logical-get-complete");
        require(httpRejected(missingLogical),
                "prefetch replay accepted a missing logical GET event");
        DebugCapture duplicateLogical;
        copyCapture(replay, duplicateLogical);
        const std::size_t logicalIndex = messageIndex(
            duplicateLogical, "ParallelHeadRange: logical-get-complete");
        duplicateLogical.messages.insert(
            duplicateLogical.messages.begin() + logicalIndex,
            duplicateLogical.messages[logicalIndex]);
        duplicateLogical.timestamps.insert(
            duplicateLogical.timestamps.begin() + logicalIndex,
            duplicateLogical.timestamps[logicalIndex]);
        require(httpRejected(duplicateLogical),
                "prefetch replay accepted duplicate logical GET events");
        DebugCapture mismatchedLogicalBytes;
        copyCapture(replay, mismatchedLogicalBytes);
        mismatchedLogicalBytes.messages[messageIndex(
            mismatchedLogicalBytes, "ParallelHeadRange: logical-get-complete")] =
            "VSICURL: ParallelHeadRange: logical-get-complete bytes=131071";
        require(httpRejected(mismatchedLogicalBytes),
                "prefetch replay accepted mismatched logical GET bytes");
        DebugCapture malformedLogical;
        copyCapture(replay, malformedLogical);
        malformedLogical.messages[messageIndex(
            malformedLogical, "ParallelHeadRange: logical-get-complete")] =
            "VSICURL: ParallelHeadRange: logical-get-complete bytes=oops";
        require(httpRejected(malformedLogical),
                "prefetch replay accepted a malformed logical GET integer");

        DebugCapture missingTransport;
        copyCapture(replay, missingTransport);
        eraseEvent(missingTransport, "ParallelHeadRange: transport");
        require(metadataRejected(missingTransport),
                "prefetch replay accepted a missing transport event");
        DebugCapture duplicateTransport;
        copyCapture(replay, duplicateTransport);
        const std::size_t transportIndex = messageIndex(
            duplicateTransport, "ParallelHeadRange: transport");
        duplicateTransport.messages.insert(
            duplicateTransport.messages.begin() + transportIndex,
            duplicateTransport.messages[transportIndex]);
        duplicateTransport.timestamps.insert(
            duplicateTransport.timestamps.begin() + transportIndex,
            duplicateTransport.timestamps[transportIndex]);
        require(metadataRejected(duplicateTransport),
                "prefetch replay accepted duplicate transport events");

        const auto transportRowRejected =
            [&copyCapture, &messageIndex, &metadataRejected, &replay](
                const std::string& event)
        {
            DebugCapture candidate;
            copyCapture(replay, candidate);
            candidate.messages[messageIndex(
                candidate, "ParallelHeadRange: transport")] = event;
            return metadataRejected(candidate);
        };
        require(transportRowRejected(
                    "VSICURL: ParallelHeadRange: transport head-connection=-1 "
                    "range-connection=-1 head-http=2 range-http=2"),
                "prefetch replay accepted negative connection IDs");
        require(transportRowRejected(
                    "VSICURL: ParallelHeadRange: transport head-connection=0 "
                    "range-connection=8 head-http=2 range-http=2"),
                "prefetch replay accepted distinct connection IDs");
        require(transportRowRejected(
                    "VSICURL: ParallelHeadRange: transport head-connection=0 "
                    "range-connection=0 head-http=1 range-http=2"),
                "prefetch replay accepted HTTP/1 for HEAD");
        require(transportRowRejected(
                    "VSICURL: ParallelHeadRange: transport head-connection=0 "
                    "range-connection=0 head-http=2 range-http=1"),
                "prefetch replay accepted HTTP/1 for Range");
        require(transportRowRejected(
                    "VSICURL: ParallelHeadRange: transport head-connection=zero "
                    "range-connection=0 head-http=2 range-http=2"),
                "prefetch replay accepted a malformed transport integer");

        DebugCapture misleadingGeneric;
        copyCapture(replay, misleadingGeneric);
        bool replacedGeneric = false;
        for (std::size_t index = misleadingGeneric.messages.size(); index-- > 0;)
        {
            if (misleadingGeneric.messages[index].find(
                    "Connection #0 to host 127.0.0.1 left intact") ==
                std::string::npos)
            {
                continue;
            }
            misleadingGeneric.messages[index] =
                "CURL_INFO_TEXT: Connection #8 to host 127.0.0.1 left intact";
            replacedGeneric = true;
            break;
        }
        require(replacedGeneric,
                "prefetch replay lacked the generic marker mutation target");
        require(!metadataRejected(misleadingGeneric),
                "prefetch replay inferred roles from a misleading generic marker");
    }

    void verifyPrefetchTransientFallbackReplayRegression()
    {
        const std::filesystem::path fixtureRoot =
            std::filesystem::path(__FILE__).parent_path() / "data" / "science";
        DebugCapture replay;
        loadReplayCapture(
            fixtureRoot / "prefetch_hong_kong_transient_trace.log", replay);
        const std::string stats = loadReplayText(
            fixtureRoot / "prefetch_hong_kong_transient_stats.json");

        const HttpProof proof = buildHttpProof(
            replay, stats, AttributionMode::LegacyFrozen);
        require(proof.coordinatorTransientFallbackCount == 1 &&
                proof.coordinatorTransientFallbackBytes == 17 &&
                proof.coordinatorTransientFallbackCodes ==
                    std::map<int, int>({{500, 1}}),
                "Hong Kong replay lost coordinator transient fallback accounting");
        require(proof.actualGetCount == 5 && proof.successfulGetCount == 4 &&
                proof.transientRetryCount == 0 &&
                proof.transientRetryCodes.empty() &&
                proof.successfulByteIntervals.size() == 4 &&
                proof.statsGetOperationCount == 3,
                "Hong Kong replay physical/logical GET counts did not reconcile");
        require(proof.successfulRangeBytes == 1264092 &&
                proof.declaredTransientBytes == 17 &&
                proof.actualHttpBodyBytes == 1264109 &&
                proof.conservativeBodyUpperBound == 1264109,
                "Hong Kong replay byte accounting did not reconcile");

        const auto failureMessage = [](const auto& operation)
        {
            try
            {
                operation();
            }
            catch (const std::exception& error)
            {
                return std::string(error.what());
            }
            fail("expected replay mutation to be rejected");
        };
        const std::string formalFailure = failureMessage(
            [&replay, &proof]()
            {
                static_cast<void>(buildMetadataPrefetchProof(replay, proof));
            });
        require(formalFailure ==
                    "metadata prefetch formal proof contains a fallback",
                "Hong Kong replay failed at the wrong formal gate: " +
                    formalFailure);

        const auto messageIndex = [](const DebugCapture& capture,
                                     const std::string& needle)
        {
            const auto iterator = std::find_if(
                capture.messages.begin(), capture.messages.end(),
                [&needle](const std::string& message)
                {
                    return message.find(needle) != std::string::npos;
                });
            require(iterator != capture.messages.end(),
                    "Hong Kong replay mutation target is missing");
            return static_cast<std::size_t>(
                std::distance(capture.messages.begin(), iterator));
        };
        const auto copyCapture = [](const DebugCapture& source,
                                    DebugCapture& destination)
        {
            destination.messages = source.messages;
            destination.timestamps = source.timestamps;
        };
        const auto httpRejected = [&failureMessage, &stats](
                                      const DebugCapture& candidate)
        {
            return !failureMessage(
                [&candidate, &stats]()
                {
                    static_cast<void>(buildHttpProof(
                        candidate, stats, AttributionMode::LegacyFrozen));
                }).empty();
        };
        const auto replaceCoordinatorEvent =
            [&copyCapture, &messageIndex, &httpRejected, &replay](
                const std::string& event)
        {
            DebugCapture candidate;
            copyCapture(replay, candidate);
            candidate.messages[messageIndex(
                candidate, "ParallelHeadRange: transient-fallback")] = event;
            return httpRejected(candidate);
        };

        DebugCapture missingEvent;
        copyCapture(replay, missingEvent);
        const std::size_t missingIndex = messageIndex(
            missingEvent, "ParallelHeadRange: transient-fallback");
        missingEvent.messages.erase(missingEvent.messages.begin() + missingIndex);
        missingEvent.timestamps.erase(missingEvent.timestamps.begin() + missingIndex);
        require(httpRejected(missingEvent),
                "Hong Kong replay accepted a missing coordinator fallback event");

        DebugCapture duplicateEvent;
        copyCapture(replay, duplicateEvent);
        const std::size_t duplicateIndex = messageIndex(
            duplicateEvent, "ParallelHeadRange: transient-fallback");
        duplicateEvent.messages.insert(
            duplicateEvent.messages.begin() + duplicateIndex,
            duplicateEvent.messages[duplicateIndex]);
        duplicateEvent.timestamps.insert(
            duplicateEvent.timestamps.begin() + duplicateIndex,
            duplicateEvent.timestamps[duplicateIndex]);
        require(httpRejected(duplicateEvent),
                "Hong Kong replay accepted duplicate coordinator fallback events");

        require(replaceCoordinatorEvent(
                    "VSICURL: ParallelHeadRange: transient-fallback "
                    "range=bytes=0-131070 status=500 bytes=17"),
                "Hong Kong replay accepted the wrong coordinator Range");
        require(replaceCoordinatorEvent(
                    "VSICURL: ParallelHeadRange: transient-fallback "
                    "range=bytes=0-131071 status=503 bytes=17"),
                "Hong Kong replay accepted the wrong coordinator status");
        require(replaceCoordinatorEvent(
                    "VSICURL: ParallelHeadRange: transient-fallback "
                    "range=bytes=0-131071 status=500 bytes=18"),
                "Hong Kong replay accepted the wrong coordinator byte count");
        require(replaceCoordinatorEvent(
                    "VSICURL: ParallelHeadRange: transient-fallback "
                    "range=bytes=0-131071 status=404 bytes=17"),
                "Hong Kong replay accepted a non-transient coordinator status");
        require(replaceCoordinatorEvent(
                    "VSICURL: ParallelHeadRange: transient-fallback "
                    "range=bytes=zero-131071 status=500 bytes=17"),
                "Hong Kong replay accepted a malformed Range integer");
        require(replaceCoordinatorEvent(
                    "VSICURL: ParallelHeadRange: transient-fallback "
                    "range=bytes=0-131071 status=oops bytes=17"),
                "Hong Kong replay accepted a malformed status integer");
        require(replaceCoordinatorEvent(
                    "VSICURL: ParallelHeadRange: transient-fallback "
                    "range=bytes=0-131071 status=500 bytes=oops"),
                "Hong Kong replay accepted a malformed byte integer");

        DebugCapture unmatchedResponse;
        copyCapture(replay, unmatchedResponse);
        unmatchedResponse.messages[messageIndex(
            unmatchedResponse, "CURL_INFO_HEADER_IN: HTTP/2 500")] =
            "CURL_INFO_HEADER_IN: HTTP/2 503";
        require(httpRejected(unmatchedResponse),
                "Hong Kong replay accepted a coordinator event without a "
                "matching transient response");
    }

    void verifyPrefetchTransientRetryReplayRegression()
    {
        const std::filesystem::path fixtureRoot =
            std::filesystem::path(__FILE__).parent_path() / "data" / "science";
        DebugCapture replay;
        loadReplayCapture(
            fixtureRoot / "prefetch_nvidia_transient_retry_trace.log", replay);
        const std::string stats = loadReplayText(
            fixtureRoot / "prefetch_nvidia_transient_retry_stats.json");

        HttpProof proof = buildHttpProof(
            replay, stats, AttributionMode::LegacyFrozen);
        require(proof.coordinatorTransientRetryCount == 1 &&
                    proof.coordinatorTransientRetryBytes == 17 &&
                    proof.coordinatorTransientRetryCodes ==
                        std::map<int, int>{{500, 1}},
                "NVIDIA retry replay did not preserve coordinator retry accounting");
        require(proof.actualHeadCount == 1 && proof.actualGetCount == 2 &&
                    proof.successfulGetCount == 1 &&
                    proof.statsGetOperationCount == 1,
                "NVIDIA retry replay physical/logical request counts did not reconcile");
        proof.metadataPrefetch = buildMetadataPrefetchProof(replay, proof);
        require(proof.metadataPrefetch.headRequestCount == 1 &&
                    proof.metadataPrefetch.rangeRequestCount == 2 &&
                    proof.metadataPrefetch.cachePublished &&
                    proof.metadataPrefetch.coordinatorRetries.size() == 1 &&
                    proof.metadataPrefetch.coordinatorRetries.front().attempt == 1 &&
                    proof.metadataPrefetch.coordinatorRetries.front().code == 500,
                "NVIDIA retry replay did not prove retried prefetch publication");

        const auto messageIndex = [](const DebugCapture& capture,
                                     const std::string& needle,
                                     std::size_t occurrence = 0)
        {
            for (std::size_t index = 0; index < capture.messages.size(); ++index)
            {
                if (capture.messages[index].find(needle) == std::string::npos)
                    continue;
                if (occurrence-- == 0) return index;
            }
            fail("NVIDIA retry replay mutation target is missing: " + needle);
        };
        const auto eraseMessage = [](DebugCapture& capture, std::size_t index)
        {
            capture.messages.erase(capture.messages.begin() + index);
            capture.timestamps.erase(capture.timestamps.begin() + index);
        };
        const auto insertMessage = [](DebugCapture& capture, std::size_t index,
                                      const std::string& message)
        {
            capture.messages.insert(capture.messages.begin() + index, message);
            capture.timestamps.insert(
                capture.timestamps.begin() + index, capture.timestamps[index]);
        };
        const auto failureMessage = [&stats](const DebugCapture& candidate)
        {
            try
            {
                HttpProof candidateProof = buildHttpProof(
                    candidate, stats, AttributionMode::LegacyFrozen);
                candidateProof.metadataPrefetch =
                    buildMetadataPrefetchProof(candidate, candidateProof);
            }
            catch (const std::exception& error)
            {
                return std::string(error.what());
            }
            return std::string();
        };
        const auto requireRejected = [&failureMessage](
                                         const DebugCapture& candidate,
                                         const std::string& description)
        {
            const std::string message = failureMessage(candidate);
            require(!message.empty(),
                    "NVIDIA retry replay accepted " + description);
        };
        const auto retryEventMutation = [&replay, &messageIndex, &requireRejected](
                                            const std::string& event,
                                            const std::string& description)
        {
            DebugCapture candidate;
            candidate.messages = replay.messages;
            candidate.timestamps = replay.timestamps;
            candidate.messages[messageIndex(
                candidate, "ParallelHeadRange: transient-retry")] = event;
            requireRejected(candidate, description);
        };

        DebugCapture missingEvent;
        missingEvent.messages = replay.messages;
        missingEvent.timestamps = replay.timestamps;
        eraseMessage(missingEvent, messageIndex(
            missingEvent, "ParallelHeadRange: transient-retry"));
        requireRejected(missingEvent, "a missing coordinator retry event");

        DebugCapture duplicateEvent;
        duplicateEvent.messages = replay.messages;
        duplicateEvent.timestamps = replay.timestamps;
        const std::size_t retryIndex = messageIndex(
            duplicateEvent, "ParallelHeadRange: transient-retry");
        insertMessage(duplicateEvent, retryIndex,
                      duplicateEvent.messages[retryIndex]);
        requireRejected(duplicateEvent, "a duplicate coordinator retry event");

        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=503 bytes=17 attempt=1 delay-ms=100 "
            "range-connection=0 range-http=2",
            "the wrong coordinator retry status");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=404 bytes=17 attempt=1 delay-ms=100 "
            "range-connection=0 range-http=2",
            "a nontransient coordinator retry status");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=500 bytes=18 attempt=1 delay-ms=100 "
            "range-connection=0 range-http=2",
            "the wrong coordinator retry body byte count");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=500 bytes=17 attempt=0 delay-ms=100 "
            "range-connection=0 range-http=2",
            "coordinator retry attempt zero");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=500 bytes=17 attempt=4 delay-ms=100 "
            "range-connection=0 range-http=2",
            "coordinator retry attempt four");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=500 bytes=17 attempt=2 delay-ms=100 "
            "range-connection=0 range-http=2",
            "a noncontiguous coordinator retry attempt");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=500 bytes=17 attempt=1 delay-ms=0 "
            "range-connection=0 range-http=2",
            "a zero coordinator retry delay");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=500 bytes=17 attempt=1 delay-ms=101 "
            "range-connection=0 range-http=2",
            "a nonzero wrong coordinator retry delay");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=500 bytes=17 attempt=1 delay-ms=100 "
            "range-connection=8 range-http=2",
            "the wrong coordinator retry connection");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131071 status=500 bytes=17 attempt=1 delay-ms=100 "
            "range-connection=0 range-http=1",
            "HTTP/1 coordinator retry evidence");
        retryEventMutation(
            "VSICURL: ParallelHeadRange: transient-retry "
            "range=bytes=0-131070 status=500 bytes=17 attempt=1 delay-ms=100 "
            "range-connection=0 range-http=2",
            "the wrong coordinator retry Range");

        DebugCapture missingSecondRequest;
        missingSecondRequest.messages = replay.messages;
        missingSecondRequest.timestamps = replay.timestamps;
        eraseMessage(missingSecondRequest, messageIndex(
            missingSecondRequest, "CURL_INFO_HEADER_OUT: GET", 1));
        requireRejected(missingSecondRequest,
                        "a retry event without a second exact Range request");

        DebugCapture upwardRoundedSecondRequest;
        upwardRoundedSecondRequest.messages = replay.messages;
        upwardRoundedSecondRequest.timestamps = replay.timestamps;
        const std::size_t upwardRoundedRetryEventIndex = messageIndex(
            upwardRoundedSecondRequest, "ParallelHeadRange: transient-retry");
        const std::size_t upwardRoundedRequestIndex = messageIndex(
            upwardRoundedSecondRequest, "CURL_INFO_HEADER_OUT: GET", 1);
        upwardRoundedSecondRequest.timestamps[upwardRoundedRequestIndex] =
            upwardRoundedSecondRequest.timestamps[upwardRoundedRetryEventIndex] +
            std::chrono::microseconds(99500);
        const std::string upwardRoundedFailure =
            failureMessage(upwardRoundedSecondRequest);
        require(upwardRoundedFailure.empty(),
                "NVIDIA retry replay rejected an upward-rounded valid delay: " +
                    upwardRoundedFailure);

        DebugCapture earlySecondRequest;
        earlySecondRequest.messages = replay.messages;
        earlySecondRequest.timestamps = replay.timestamps;
        const std::size_t retryEventIndex = messageIndex(
            earlySecondRequest, "ParallelHeadRange: transient-retry");
        const std::size_t secondRequestIndex = messageIndex(
            earlySecondRequest, "CURL_INFO_HEADER_OUT: GET", 1);
        earlySecondRequest.timestamps[secondRequestIndex] =
            earlySecondRequest.timestamps[retryEventIndex] +
            std::chrono::milliseconds(99);
        const std::string earlySecondRequestFailure =
            failureMessage(earlySecondRequest);
        require(earlySecondRequestFailure ==
                    "coordinator retry request preceded its declared delay",
                "NVIDIA retry replay did not reject the 99 ms premature "
                "request at the chronology boundary: " +
                    earlySecondRequestFailure);

        DebugCapture extraHead;
        extraHead.messages = replay.messages;
        extraHead.timestamps = replay.timestamps;
        const std::size_t headIndex = messageIndex(
            extraHead, "CURL_INFO_HEADER_OUT: HEAD");
        insertMessage(extraHead, headIndex, extraHead.messages[headIndex]);
        requireRejected(extraHead, "an extra HEAD request");

        DebugCapture fallbackPresent;
        fallbackPresent.messages = replay.messages;
        fallbackPresent.timestamps = replay.timestamps;
        insertMessage(fallbackPresent, messageIndex(
            fallbackPresent, "ParallelHeadRange: published"),
            "VSICURL: ParallelHeadRange: fallback=status-500");
        requireRejected(fallbackPresent, "a fallback before publication");

        DebugCapture duplicatePublication;
        duplicatePublication.messages = replay.messages;
        duplicatePublication.timestamps = replay.timestamps;
        const std::size_t publicationIndex = messageIndex(
            duplicatePublication, "ParallelHeadRange: published");
        insertMessage(duplicatePublication, publicationIndex,
                      duplicatePublication.messages[publicationIndex]);
        requireRejected(duplicatePublication, "duplicate cache publication");
    }

    struct PixelWindow
    {
        int x = 0;
        int topDownY = 0;
        int size = 256;
    };

    struct GeoreferenceProof
    {
        std::string crs;
        std::array<double, 6> geotransform = {};
        std::array<double, 2> projectedPoint = {};
        std::array<double, 2> rawPixel = {};
        std::array<double, 4> verifiedWgs84Bbox = {};
    };

    PixelWindow deriveGeoreferencedWindow(GDALDataset* dataset,
                                          const LiveCase& item,
                                          GeoreferenceProof* proof = nullptr);

    void verifyGeoreferenceRegression()
    {
        GDALDriver* driver = GetGDALDriverManager()->GetDriverByName("MEM");
        require(driver != nullptr, "MEM driver is unavailable for georeference regression");
        GDALDataset* dataset = driver->Create("", 8192, 8192, 0, GDT_Unknown, nullptr);
        require(dataset != nullptr, "failed to create georeference regression dataset");
        OGRSpatialReference spatialReference;
        require(spatialReference.SetFromUserInput("EPSG:32610") == OGRERR_NONE &&
                dataset->SetSpatialRef(&spatialReference) == CE_None,
                "failed to assign georeference regression CRS");
        double transform[] = {581920.0, 10.0, 0.0, 4096000.0, 0.0, 10.0};
        require(dataset->SetGeoTransform(transform) == CE_None,
                "failed to assign georeference regression transform");

        LiveCase item;
        item.name = "georef_regression";
        item.rawCrs = "EPSG:32610";
        item.utmZone = "10N";
        item.longitude = -121.9631;
        item.latitude = 37.3707;
        item.utmBbox = {581920.0, 4096000.0, 663840.0, 4177920.0};
        item.bbox = {-122.07923449193419, 36.995882066705576,
                     -121.14066509082407, 37.74491179716658};

        const PixelWindow window = deriveGeoreferencedWindow(dataset, item);
        require(window.x == 860 && window.topDownY == 4012 && window.size == 256,
                "georeferenced regression window changed");

        const auto rejected = [dataset](const LiveCase& candidate)
        {
            try
            {
                static_cast<void>(deriveGeoreferencedWindow(dataset, candidate));
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };
        LiveCase wrongCrs = item;
        wrongCrs.rawCrs = "EPSG:32611";
        require(rejected(wrongCrs), "georeference validation accepted a mismatched CRS");
        LiveCase wrongTransform = item;
        wrongTransform.utmBbox[0] += 10.0;
        require(rejected(wrongTransform),
                "georeference validation accepted a mismatched affine transform");
        LiveCase wrongBbox = item;
        wrongBbox.bbox[0] += 0.01;
        require(rejected(wrongBbox),
                "georeference validation accepted a mismatched WGS84 bbox");

        require(spatialReference.SetFromUserInput("EPSG:32650") == OGRERR_NONE &&
                dataset->SetSpatialRef(&spatialReference) == CE_None,
                "failed to assign clipped georeference regression CRS");
        double clippedTransform[] = {172320.0, 10.0, 0.0,
                                     2457600.0, 0.0, 10.0};
        require(dataset->SetGeoTransform(clippedTransform) == CE_None,
                "failed to assign clipped georeference regression transform");
        LiveCase clipped;
        clipped.name = "clipped_georef_regression";
        clipped.rawCrs = "EPSG:32650";
        clipped.utmZone = "50N";
        clipped.longitude = 114.1694;
        clipped.latitude = 22.3193;
        clipped.utmBbox = {172320.0, 2457600.0, 254240.0, 2539520.0};
        clipped.bbox = {114.0, 22.1961523775654,
                        114.61611548102607, 22.945759067760623};
        const PixelWindow clippedWindow =
            deriveGeoreferencedWindow(dataset, clipped);
        require(clippedWindow.x == 3476 && clippedWindow.topDownY == 6732,
                "area-of-use-clipped georeferenced window changed");
        GDALClose(dataset);
    }

    struct GeographicPoint
    {
        double x = 0.0;
        double y = 0.0;
    };

    template<typename Inside, typename Intersect>
    std::vector<GeographicPoint> clipPolygon(
        const std::vector<GeographicPoint>& input, Inside inside, Intersect intersect)
    {
        std::vector<GeographicPoint> output;
        if (input.empty()) return output;
        GeographicPoint previous = input.back();
        bool previousInside = inside(previous);
        for (const GeographicPoint& current : input)
        {
            const bool currentInside = inside(current);
            if (currentInside != previousInside)
                output.push_back(intersect(previous, current));
            if (currentInside) output.push_back(current);
            previous = current;
            previousInside = currentInside;
        }
        return output;
    }

    std::vector<GeographicPoint> clipToAreaOfUse(
        std::vector<GeographicPoint> polygon,
        double west, double south, double east, double north)
    {
        const auto verticalIntersection = [](const GeographicPoint& first,
                                             const GeographicPoint& second,
                                             double x)
        {
            const double fraction = (x - first.x) / (second.x - first.x);
            return GeographicPoint{x, first.y + fraction * (second.y - first.y)};
        };
        const auto horizontalIntersection = [](const GeographicPoint& first,
                                               const GeographicPoint& second,
                                               double y)
        {
            const double fraction = (y - first.y) / (second.y - first.y);
            return GeographicPoint{first.x + fraction * (second.x - first.x), y};
        };
        polygon = clipPolygon(polygon,
            [west](const GeographicPoint& point) { return point.x >= west; },
            [west, &verticalIntersection](const GeographicPoint& first,
                                          const GeographicPoint& second)
            { return verticalIntersection(first, second, west); });
        polygon = clipPolygon(polygon,
            [east](const GeographicPoint& point) { return point.x <= east; },
            [east, &verticalIntersection](const GeographicPoint& first,
                                          const GeographicPoint& second)
            { return verticalIntersection(first, second, east); });
        polygon = clipPolygon(polygon,
            [south](const GeographicPoint& point) { return point.y >= south; },
            [south, &horizontalIntersection](const GeographicPoint& first,
                                             const GeographicPoint& second)
            { return horizontalIntersection(first, second, south); });
        return clipPolygon(polygon,
            [north](const GeographicPoint& point) { return point.y <= north; },
            [north, &horizontalIntersection](const GeographicPoint& first,
                                             const GeographicPoint& second)
            { return horizontalIntersection(first, second, north); });
    }

    PixelWindow deriveGeoreferencedWindow(GDALDataset* dataset,
                                          const LiveCase& item,
                                          GeoreferenceProof* proof)
    {
        require(dataset != nullptr && item.utmBbox.size() == 4 &&
                item.bbox.size() == 4,
                "georeference validation requires complete pinned bounds");
        const OGRSpatialReference* sourceSpatialReference = dataset->GetSpatialRef();
        require(sourceSpatialReference != nullptr,
                item.name + " source dataset has no CRS");
        OGRSpatialReference projected(*sourceSpatialReference);
        projected.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        const char* authority = projected.GetAuthorityName(nullptr);
        const char* code = projected.GetAuthorityCode(nullptr);
        const std::size_t separator = item.rawCrs.find(':');
        require(separator != std::string::npos && authority && code &&
                item.rawCrs.substr(0, separator) == authority &&
                item.rawCrs.substr(separator + 1) == code,
                item.name + " dataset CRS authority/code differs from pinned raw crs");
        require(item.utmZone.size() >= 2 && item.utmZone.back() == 'N',
                item.name + " pinned UTM zone is malformed");
        const int zone = std::stoi(item.utmZone.substr(0, item.utmZone.size() - 1));
        require(std::stoi(code) == 32600 + zone,
                item.name + " UTM zone and CRS code disagree");

        double geotransform[6] = {};
        require(dataset->GetGeoTransform(geotransform) == CE_None,
                item.name + " source dataset has no affine geotransform");
        const std::array<double, 6> expected = {
            item.utmBbox[0],
            (item.utmBbox[2] - item.utmBbox[0]) / dataset->GetRasterXSize(),
            0.0,
            item.utmBbox[1],
            0.0,
            (item.utmBbox[3] - item.utmBbox[1]) / dataset->GetRasterYSize(),
        };
        for (int index = 0; index < 6; ++index)
        {
            const double tolerance = 1e-12 * std::max(1.0, std::abs(expected[index]));
            require(std::abs(geotransform[index] - expected[index]) <= tolerance,
                    item.name + " complete affine geotransform differs from UTM bounds");
        }

        OGRSpatialReference wgs84;
        require(wgs84.SetFromUserInput("EPSG:4326") == OGRERR_NONE,
                "failed to construct EPSG:4326");
        wgs84.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        std::unique_ptr<OGRCoordinateTransformation,
                        decltype(&OCTDestroyCoordinateTransformation)> toProjected(
            OGRCreateCoordinateTransformation(&wgs84, &projected),
            OCTDestroyCoordinateTransformation);
        std::unique_ptr<OGRCoordinateTransformation,
                        decltype(&OCTDestroyCoordinateTransformation)> toWgs84(
            OGRCreateCoordinateTransformation(&projected, &wgs84),
            OCTDestroyCoordinateTransformation);
        require(toProjected && toWgs84,
                item.name + " failed to create CRS transformations");

        double projectedX = item.longitude;
        double projectedY = item.latitude;
        require(toProjected->Transform(1, &projectedX, &projectedY),
                item.name + " failed to transform pinned lon/lat into source CRS");
        double inverse[6] = {};
        require(GDALInvGeoTransform(geotransform, inverse),
                item.name + " affine geotransform is not invertible");
        double rawPixelX = 0.0, rawPixelY = 0.0;
        GDALApplyGeoTransform(inverse, projectedX, projectedY,
                              &rawPixelX, &rawPixelY);
        require(rawPixelX >= 0.0 && rawPixelY >= 0.0 &&
                rawPixelX < dataset->GetRasterXSize() &&
                rawPixelY < dataset->GetRasterYSize(),
                item.name + " transformed point is outside the source raster");

        constexpr int EDGE_SEGMENTS = 20;
        std::vector<GeographicPoint> footprint;
        footprint.reserve(EDGE_SEGMENTS * 4);
        const int width = dataset->GetRasterXSize();
        const int height = dataset->GetRasterYSize();
        const auto addPoint = [&](double pixelX, double pixelY)
        {
            double x = 0.0, y = 0.0;
            GDALApplyGeoTransform(geotransform, pixelX, pixelY, &x, &y);
            footprint.push_back({x, y});
        };
        for (int step = 0; step < EDGE_SEGMENTS; ++step)
            addPoint(width * step / static_cast<double>(EDGE_SEGMENTS), 0.0);
        for (int step = 0; step < EDGE_SEGMENTS; ++step)
            addPoint(width, height * step / static_cast<double>(EDGE_SEGMENTS));
        for (int step = 0; step < EDGE_SEGMENTS; ++step)
            addPoint(width * (EDGE_SEGMENTS - step) /
                     static_cast<double>(EDGE_SEGMENTS), height);
        for (int step = 0; step < EDGE_SEGMENTS; ++step)
            addPoint(0.0, height * (EDGE_SEGMENTS - step) /
                     static_cast<double>(EDGE_SEGMENTS));
        std::vector<double> longitudes, latitudes;
        longitudes.reserve(footprint.size());
        latitudes.reserve(footprint.size());
        for (const GeographicPoint& point : footprint)
        {
            longitudes.push_back(point.x);
            latitudes.push_back(point.y);
        }
        require(toWgs84->Transform(static_cast<int>(footprint.size()),
                                   longitudes.data(), latitudes.data()),
                item.name + " failed to transform raster footprint to WGS84");
        for (std::size_t index = 0; index < footprint.size(); ++index)
            footprint[index] = {longitudes[index], latitudes[index]};
        double areaWest = 0.0, areaSouth = 0.0, areaEast = 0.0, areaNorth = 0.0;
        const char* areaName = nullptr;
        require(projected.GetAreaOfUse(&areaWest, &areaSouth, &areaEast, &areaNorth,
                                       &areaName),
                item.name + " source CRS has no area-of-use bounds");
        footprint = clipToAreaOfUse(footprint, areaWest, areaSouth,
                                    areaEast, areaNorth);
        require(!footprint.empty(), item.name + " clipped WGS84 footprint is empty");
        std::array<double, 4> verifiedBbox = {
            footprint[0].x, footprint[0].y, footprint[0].x, footprint[0].y};
        for (const GeographicPoint& point : footprint)
        {
            verifiedBbox[0] = std::min(verifiedBbox[0], point.x);
            verifiedBbox[1] = std::min(verifiedBbox[1], point.y);
            verifiedBbox[2] = std::max(verifiedBbox[2], point.x);
            verifiedBbox[3] = std::max(verifiedBbox[3], point.y);
        }
        for (int index = 0; index < 4; ++index)
            require(std::abs(verifiedBbox[index] - item.bbox[index]) <= 1e-9,
                    item.name + " transformed/clipped footprint bbox differs from index");

        const int centerX = static_cast<int>(std::floor(rawPixelX));
        const int rawCenterY = static_cast<int>(std::floor(rawPixelY));
        const int topDownCenterY = height - 1 - rawCenterY;
        PixelWindow window;
        window.x = std::max(0, std::min(width - window.size,
                                       centerX - window.size / 2));
        window.topDownY = std::max(0, std::min(height - window.size,
                                              topDownCenterY - window.size / 2));
        window.x -= window.x % LIVE_OVERVIEW_FACTOR;
        window.topDownY -= window.topDownY % LIVE_OVERVIEW_FACTOR;
        if (proof)
        {
            proof->crs = item.rawCrs;
            std::copy(geotransform, geotransform + 6, proof->geotransform.begin());
            proof->projectedPoint = {projectedX, projectedY};
            proof->rawPixel = {rawPixelX, rawPixelY};
            proof->verifiedWgs84Bbox = verifiedBbox;
        }
        return window;
    }

    double checkedDequantize(std::int8_t raw)
    {
        const double normalized = std::abs(static_cast<double>(raw)) / 127.5;
        const double formula = std::copysign(std::pow(normalized, 2.0),
                                             static_cast<double>(raw));
        const double expanded = (raw < 0 ? -1.0 : (raw > 0 ? 1.0 : 0.0)) *
            normalized * normalized;
        require(std::abs(formula - expanded) < 1e-15,
                "AlphaEarth signed dequantization formula changed");
        return formula;
    }

    GDALRasterBand* exactOverview(GDALRasterBand* base, int factor)
    {
        require(base != nullptr && factor > 1, "invalid overview selection request");
        for (int index = 0; index < base->GetOverviewCount(); ++index)
        {
            GDALRasterBand* overview = base->GetOverview(index);
            if (overview && overview->GetXSize() * factor == base->GetXSize() &&
                overview->GetYSize() * factor == base->GetYSize())
                return overview;
        }
        fail("source COG has no exact " + std::to_string(factor) + "x overview");
    }

    struct NormalizedRgbWindow
    {
        std::vector<std::int8_t> rgb;
        std::vector<unsigned char> mask;
    };

    NormalizedRgbWindow readNormalizedOverviewRgbOracle(
        GDALDataset* raw, const PixelWindow& window, const int* bandMap)
    {
        require(window.size % LIVE_OVERVIEW_FACTOR == 0 &&
                window.x % LIVE_OVERVIEW_FACTOR == 0 &&
                window.topDownY % LIVE_OVERVIEW_FACTOR == 0,
                "bbox window is not aligned to the chosen source overview");
        const int rawWindowY = raw->GetRasterYSize() - window.topDownY - window.size;
        require(rawWindowY >= 0 && rawWindowY % LIVE_OVERVIEW_FACTOR == 0,
                "mirrored raw bbox window is not overview-aligned");
        const int overviewX = window.x / LIVE_OVERVIEW_FACTOR;
        const int overviewY = rawWindowY / LIVE_OVERVIEW_FACTOR;
        const int overviewSize = window.size / LIVE_OVERVIEW_FACTOR;
        require(overviewSize == 64,
                "live bbox sample must be an exact 64x64 overview window");

        std::vector<std::int8_t> rgb(overviewSize * overviewSize * 3);
        std::vector<unsigned char> normalizedMasks(overviewSize * overviewSize * 3);
        bool hasValidNonZero = false;
        for (int channel = 0; channel < 3; ++channel)
        {
            GDALRasterBand* overview = exactOverview(
                raw->GetRasterBand(bandMap[channel]), LIVE_OVERVIEW_FACTOR);
            require(overview->GetRasterDataType() == GDT_Int8,
                    "selected RGB overview is not signed Int8");
            std::vector<std::int8_t> values(overviewSize * overviewSize);
            std::vector<unsigned char> masks(overviewSize * overviewSize);
            require(overview->RasterIO(GF_Read, overviewX, overviewY,
                                       overviewSize, overviewSize, values.data(),
                                       overviewSize, overviewSize, GDT_Int8,
                                       0, 0, nullptr) == CE_None,
                    "exact source overview RGB window read failed");
            require(overview->GetMaskBand()->RasterIO(
                        GF_Read, overviewX, overviewY, overviewSize, overviewSize,
                        masks.data(), overviewSize, overviewSize, GDT_Byte,
                        0, 0, nullptr) == CE_None,
                    "exact source overview mask window read failed");
            for (int topY = 0; topY < overviewSize; ++topY)
            {
                const int rawY = overviewSize - 1 - topY;
                for (int x = 0; x < overviewSize; ++x)
                {
                    const std::size_t source = rawY * overviewSize + x;
                    const std::size_t destination =
                        (topY * overviewSize + x) * 3 + channel;
                    rgb[destination] = values[source];
                    normalizedMasks[destination] = masks[source];
                    hasValidNonZero = hasValidNonZero ||
                        (masks[source] != 0 && values[source] != 0);
                }
            }
            const std::size_t top = channel;
            const std::size_t rawBottom = (overviewSize - 1) * overviewSize;
            const std::size_t bottom =
                ((overviewSize - 1) * overviewSize) * 3 + channel;
            require(rgb[top] == values[rawBottom] &&
                    normalizedMasks[top] == masks[rawBottom] &&
                    rgb[bottom] == values[0] && normalizedMasks[bottom] == masks[0],
                    "top-down normalization did not mirror raw overview pixels/masks");
            bool dequantizedValidSample = false;
            for (std::size_t pixel = channel; pixel < rgb.size(); pixel += 3)
            {
                if (normalizedMasks[pixel] == 0) continue;
                require(std::isfinite(checkedDequantize(rgb[pixel])),
                        "dequantized overview sample is non-finite");
                dequantizedValidSample = true;
                break;
            }
            require(dequantizedValidSample,
                    "RGB overview channel has no valid sample to dequantize");
        }
        require(hasValidNonZero,
                "normalized source-overview RGB window has no valid non-zero sample");
        return {std::move(rgb), std::move(normalizedMasks)};
    }

    NormalizedRgbWindow readNormalizedOverviewRgbBatched(
        GDALDataset* raw, const PixelWindow& window, const int* bandMap)
    {
        require(raw != nullptr, "batched RGB dataset is null");
        require(window.size % LIVE_OVERVIEW_FACTOR == 0 &&
                window.x % LIVE_OVERVIEW_FACTOR == 0 &&
                window.topDownY % LIVE_OVERVIEW_FACTOR == 0,
                "batched RGB window is not aligned to the chosen source overview");
        const int rawWindowY = raw->GetRasterYSize() - window.topDownY - window.size;
        require(rawWindowY >= 0 && rawWindowY % LIVE_OVERVIEW_FACTOR == 0,
                "batched mirrored raw window is not overview-aligned");
        const int overviewSize = window.size / LIVE_OVERVIEW_FACTOR;
        require(overviewSize == 64,
                "batched RGB sample must be an exact 64x64 overview window");
        for (int channel = 0; channel < 3; ++channel)
        {
            GDALRasterBand* overview = exactOverview(
                raw->GetRasterBand(bandMap[channel]), LIVE_OVERVIEW_FACTOR);
            require(overview->GetRasterDataType() == GDT_Int8,
                    "batched RGB overview is not signed Int8");
        }

        std::vector<std::int8_t> rawRgb(overviewSize * overviewSize * 3);
        GDALRasterIOExtraArg extra;
        INIT_RASTERIO_EXTRA_ARG(extra);
        extra.eResampleAlg = GRIORA_NearestNeighbour;
        require(raw->RasterIO(
                    GF_Read, window.x, rawWindowY, window.size, window.size,
                    rawRgb.data(), overviewSize, overviewSize, GDT_Int8,
                    3, const_cast<int*>(bandMap), 3, overviewSize * 3, 1,
                    &extra) == CE_None,
                "batched exact-overview RGB read failed");

        std::vector<unsigned char> masks(overviewSize * overviewSize * 3, 255);
        for (int channel = 0; channel < 3; ++channel)
        {
            GDALRasterBand* overview = exactOverview(
                raw->GetRasterBand(bandMap[channel]), LIVE_OVERVIEW_FACTOR);
            const int flags = overview->GetMaskFlags();
            if ((flags & GMF_ALL_VALID) != 0) continue;
            std::vector<unsigned char> channelMask(overviewSize * overviewSize);
            require(overview->GetMaskBand()->RasterIO(
                        GF_Read, window.x / LIVE_OVERVIEW_FACTOR,
                        rawWindowY / LIVE_OVERVIEW_FACTOR,
                        overviewSize, overviewSize, channelMask.data(),
                        overviewSize, overviewSize, GDT_Byte, 0, 0, nullptr) == CE_None,
                    "batched RGB mask read failed");
            for (std::size_t pixel = 0; pixel < channelMask.size(); ++pixel)
                masks[pixel * 3 + channel] = channelMask[pixel];
        }

        std::vector<std::int8_t> topDown(rawRgb.size());
        std::vector<unsigned char> topDownMasks(masks.size());
        for (int topY = 0; topY < overviewSize; ++topY)
        {
            const int sourceY = overviewSize - 1 - topY;
            for (int x = 0; x < overviewSize; ++x)
            {
                for (int channel = 0; channel < 3; ++channel)
                {
                    const std::size_t source =
                        (sourceY * overviewSize + x) * 3 + channel;
                    const std::size_t destination =
                        (topY * overviewSize + x) * 3 + channel;
                    topDown[destination] = rawRgb[source];
                    topDownMasks[destination] = masks[source];
                    require(masks[source] == 0 ||
                            std::isfinite(checkedDequantize(rawRgb[source])),
                            "batched valid sample dequantized non-finite");
                }
            }
        }
        require(std::any_of(topDown.begin(), topDown.end(),
                            [](std::int8_t value) { return value != 0; }),
                "batched RGB is empty");
        return {std::move(topDown), std::move(topDownMasks)};
    }

    void closeDataset(GDALDataset* dataset)
    {
        if (dataset) GDALClose(dataset);
    }

    using DatasetPtr = std::unique_ptr<GDALDataset, decltype(&closeDataset)>;

    LiveMeasurement runLiveIteration(const LiveCase& item, int iteration,
                                     RangeProfile profile,
                                     const std::filesystem::path& evidenceDirectory)
    {
        VSICurlClearCache();
        DebugCapture capture;
        ScopedGdalErrorCapture errorCapture(capture);
        VSINetworkStatsReset();
        const auto started = std::chrono::steady_clock::now();
        const std::string vsiUrl = "/vsicurl/" + item.url;
        require(std::string(VSIGetPathSpecificOption(vsiUrl.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")).empty(),
                "metadata prefetch path option leaked between live iterations");
        require(std::string(VSIGetPathSpecificOption(vsiUrl.c_str(),
                    "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "")).empty(),
                "immediate multi-range path option leaked between live iterations");
        require(std::string(VSIGetPathSpecificOption(vsiUrl.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", "")).empty(),
                "prefetch operation token leaked between live iterations");
        std::unique_ptr<ScopedPathSpecificOption> prefetchActivation;
        std::unique_ptr<ScopedPathSpecificOption> immediateActivation;
        std::unique_ptr<ScopedPathSpecificOption> operationActivation;
        if (profile == RangeProfile::Prefetch)
        {
            const std::string operationId = nextPrefetchOperationId();
            prefetchActivation = std::make_unique<ScopedPathSpecificOption>(
                vsiUrl, "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
            immediateActivation = std::make_unique<ScopedPathSpecificOption>(
                vsiUrl, "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "YES");
            operationActivation = std::make_unique<ScopedPathSpecificOption>(
                vsiUrl, "OSGSOL_VSICURL_PREFETCH_OPERATION_ID",
                operationId.c_str());
        }
        DatasetPtr raw(static_cast<GDALDataset*>(GDALOpenEx(
            vsiUrl.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
            nullptr, nullptr, nullptr)), closeDataset);
        const auto opened = std::chrono::steady_clock::now();
        require(raw != nullptr, item.name + " COG open failed");
        require(raw->GetRasterXSize() == 8192 && raw->GetRasterYSize() == 8192 &&
                raw->GetRasterCount() == 64,
                item.name + " is not the expected 8192x8192x64 tile");
        GeoreferenceProof georeference;
        const PixelWindow window =
            deriveGeoreferencedWindow(raw.get(), item, &georeference);
        const auto georeferenced = std::chrono::steady_clock::now();

        const int bandMap[] = {2, 17, 10};
        const std::string bandNames[] = {"A01", "A16", "A09"};
        for (int index = 0; index < 3; ++index)
        {
            GDALRasterBand* rawBand = raw->GetRasterBand(bandMap[index]);
            require(rawBand && rawBand->GetRasterDataType() == GDT_Int8 &&
                    std::string(rawBand->GetDescription()) == bandNames[index] &&
                    rawBand->GetMaskBand() != nullptr,
                    item.name + " RGB band metadata changed");
        }
        const NormalizedRgbWindow normalized = usesOptimizedRangeSettings(profile)
            ? readNormalizedOverviewRgbBatched(raw.get(), window, bandMap)
            : readNormalizedOverviewRgbOracle(raw.get(), window, bandMap);
        const auto read = std::chrono::steady_clock::now();
        require(normalized.mask.size() == normalized.rgb.size(),
                item.name + " RGB mask size differs from RGB window");
        require(std::any_of(normalized.rgb.begin(), normalized.rgb.end(),
                            [](std::int8_t value) { return value != 0; }),
                item.name + " RGB window is empty after HTTP retries");
        raw.reset();
        operationActivation.reset();
        immediateActivation.reset();
        prefetchActivation.reset();
        require(std::string(VSIGetPathSpecificOption(vsiUrl.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")).empty(),
                "metadata prefetch path option survived dataset close");
        require(std::string(VSIGetPathSpecificOption(vsiUrl.c_str(),
                    "OSGSOL_VSICURL_IMMEDIATE_MULTIRANGE_RETRY", "")).empty(),
                "immediate multi-range path option survived dataset close");
        require(std::string(VSIGetPathSpecificOption(vsiUrl.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_OPERATION_ID", "")).empty(),
                "prefetch operation token survived dataset close");
        const auto finished = std::chrono::steady_clock::now();

        PhaseTimings phases;
        phases.openMs =
            std::chrono::duration<double, std::milli>(opened - started).count();
        phases.georeferenceMs =
            std::chrono::duration<double, std::milli>(georeferenced - opened).count();
        phases.readMs =
            std::chrono::duration<double, std::milli>(read - georeferenced).count();
        phases.closeMs =
            std::chrono::duration<double, std::milli>(finished - read).count();
        phases.totalMs =
            std::chrono::duration<double, std::milli>(finished - started).count();
        require(std::abs(phases.totalMs -
                (phases.openMs + phases.georeferenceMs +
                 phases.readMs + phases.closeMs)) < 0.5,
                "latency phases do not sum to total");

        const std::string statsJson = requireNetworkStatsEvidence();
        const std::string evidenceName = std::string(profileName(profile)) + "-" +
            item.name + "-" + std::to_string(iteration);
        writeRawTransportEvidence(evidenceDirectory, evidenceName, capture, statsJson);
        HttpProof proof = buildHttpProof(
            capture, statsJson, profile == RangeProfile::Prefetch
                ? AttributionMode::AttributedV6
                : AttributionMode::LegacyFrozen);
        if (profile == RangeProfile::Prefetch)
            proof.metadataPrefetch = buildMetadataPrefetchProof(capture, proof);
        verifyLiveProfileProof(profile, proof);
        proof.overviewFactor = LIVE_OVERVIEW_FACTOR;
        proof.rawWindowX = window.x;
        proof.rawWindowY = 8192 - window.topDownY - window.size;
        proof.rawWindowSize = window.size;
        proof.sourceCrs = georeference.crs;
        proof.geotransform = georeference.geotransform;
        proof.projectedPoint = georeference.projectedPoint;
        proof.rawPixel = georeference.rawPixel;
        proof.verifiedWgs84Bbox = georeference.verifiedWgs84Bbox;
        proof.transferBudget = LIVE_TRANSFER_BUDGET;
        require(proof.conservativeBodyUpperBound <= proof.transferBudget &&
                proof.conservativeBodyUpperBound < proof.sourceSize,
                item.name + " conservative HTTP body bound exceeded the live budget");
        writeParsedProof(evidenceDirectory, evidenceName, proof);
        LiveMeasurement measurement;
        measurement.phases = phases;
        measurement.successfulRangeBytes = proof.successfulRangeBytes;
        measurement.declaredTransientBytes = proof.declaredTransientBytes;
        measurement.actualHttpBodyBytes = proof.actualHttpBodyBytes;
        measurement.conservativeBodyUpperBound = proof.conservativeBodyUpperBound;
        measurement.sourceSize = proof.sourceSize;
        measurement.actualGetCount = proof.actualGetCount;
        measurement.actualHeadCount = proof.actualHeadCount;
        measurement.statsGetOperationCount = proof.statsGetOperationCount;
        measurement.successfulGetCount = proof.successfulGetCount;
        measurement.transientRetryCount = proof.transientRetryCount;
        measurement.immediateTransientRetryCount =
            proof.immediateTransientRetryCount;
        measurement.immediateTransientRetryBytes =
            proof.immediateTransientRetryBytes;
        measurement.coordinatorTransientRetryCount =
            proof.coordinatorTransientRetryCount;
        measurement.coordinatorTransientRetryBytes =
            proof.coordinatorTransientRetryBytes;
        measurement.transientRetryCodes = proof.transientRetryCodes;
        measurement.immediateTransientRetryCodes =
            proof.immediateTransientRetryCodes;
        measurement.coordinatorTransientRetryCodes =
            proof.coordinatorTransientRetryCodes;
        measurement.responseCodes = proof.responseCodes;
        measurement.milliseconds = phases.totalMs;
        return measurement;
    }

    double percentile(std::vector<double> values, double fraction)
    {
        require(!values.empty(), "cannot calculate an empty percentile");
        std::sort(values.begin(), values.end());
        const std::size_t index = static_cast<std::size_t>(
            std::ceil(fraction * static_cast<double>(values.size()))) - 1;
        return values[std::min(index, values.size() - 1)];
    }

    LatencySummary summarizeLatency(const std::vector<double>& values)
    {
        return {percentile(values, 0.5), percentile(values, 0.95)};
    }

    bool passesLatencyGate(const LatencySummary& summary)
    {
        return summary.medianMs <= MAX_MEDIAN_MS && summary.p95Ms <= MAX_P95_MS;
    }

    int liveGateExitCode(bool enforceLatency, bool passed)
    {
        return enforceLatency && !passed ? 2 : 0;
    }

    picojson::object retryCountsJson(const std::map<int, int>& counts)
    {
        picojson::object result;
        for (const auto& count : counts)
            result[std::to_string(count.first)] = picojson::value(
                static_cast<double>(count.second));
        return result;
    }

    picojson::array responseCodesJson(const std::vector<int>& codes)
    {
        picojson::array result;
        for (int code : codes)
            result.emplace_back(static_cast<double>(code));
        return result;
    }

    picojson::value liveIterationJson(int iteration,
                                      const LiveMeasurement& measurement)
    {
        picojson::object phases;
        phases["open"] = picojson::value(measurement.phases.openMs);
        phases["georeference"] = picojson::value(
            measurement.phases.georeferenceMs);
        phases["read"] = picojson::value(measurement.phases.readMs);
        phases["close"] = picojson::value(measurement.phases.closeMs);
        phases["total"] = picojson::value(measurement.phases.totalMs);

        picojson::object httpCounts;
        httpCounts["actual_get"] = picojson::value(
            static_cast<double>(measurement.actualGetCount));
        httpCounts["actual_head"] = picojson::value(
            static_cast<double>(measurement.actualHeadCount));
        httpCounts["stats_get_operations"] = picojson::value(
            static_cast<double>(measurement.statsGetOperationCount));
        httpCounts["successful_get"] = picojson::value(
            static_cast<double>(measurement.successfulGetCount));
        httpCounts["transient_retries"] = picojson::value(
            static_cast<double>(measurement.transientRetryCount));
        httpCounts["immediate_transient_retries"] = picojson::value(
            static_cast<double>(measurement.immediateTransientRetryCount));
        httpCounts["coordinator_transient_retries"] = picojson::value(
            static_cast<double>(measurement.coordinatorTransientRetryCount));

        picojson::object bytes;
        bytes["successful_range"] = picojson::value(
            static_cast<double>(measurement.successfulRangeBytes));
        bytes["declared_transient"] = picojson::value(
            static_cast<double>(measurement.declaredTransientBytes));
        bytes["immediate_transient_retry"] = picojson::value(
            static_cast<double>(measurement.immediateTransientRetryBytes));
        bytes["coordinator_transient_retry"] = picojson::value(
            static_cast<double>(measurement.coordinatorTransientRetryBytes));
        bytes["actual_http_body"] = picojson::value(
            static_cast<double>(measurement.actualHttpBodyBytes));
        bytes["conservative_body_upper_bound"] = picojson::value(
            static_cast<double>(measurement.conservativeBodyUpperBound));
        bytes["source_size"] = picojson::value(
            static_cast<double>(measurement.sourceSize));

        picojson::object result;
        result["iteration"] = picojson::value(static_cast<double>(iteration));
        result["timing_ms"] = picojson::value(measurement.milliseconds);
        result["phases_ms"] = picojson::value(phases);
        result["http_counts"] = picojson::value(httpCounts);
        result["bytes"] = picojson::value(bytes);
        result["transient_retry_codes"] = picojson::value(
            retryCountsJson(measurement.transientRetryCodes));
        result["immediate_transient_retry_codes"] = picojson::value(
            retryCountsJson(measurement.immediateTransientRetryCodes));
        result["coordinator_transient_retry_codes"] = picojson::value(
            retryCountsJson(measurement.coordinatorTransientRetryCodes));
        result["response_codes"] = picojson::value(
            responseCodesJson(measurement.responseCodes));
        return picojson::value(result);
    }

    picojson::value liveCaseSummaryJson(
        const LiveCase& item, const std::vector<LiveMeasurement>& measurements)
    {
        require(!measurements.empty(), "live case summary requires measurements");
        std::vector<double> timings;
        std::vector<double> openTimings;
        std::vector<double> georeferenceTimings;
        std::vector<double> readTimings;
        std::vector<double> closeTimings;
        picojson::array iterations;
        double summedOpenMs = 0.0;
        double summedGeoreferenceMs = 0.0;
        double summedReadMs = 0.0;
        double summedCloseMs = 0.0;
        double summedTotalMs = 0.0;
        std::uint64_t totalSuccessfulBytes = 0;
        std::uint64_t totalDeclaredTransientBytes = 0;
        std::uint64_t totalImmediateTransientRetryBytes = 0;
        std::uint64_t totalCoordinatorTransientRetryBytes = 0;
        std::uint64_t totalActualBodyBytes = 0;
        std::uint64_t totalConservativeBodyUpperBound = 0;
        std::uint64_t sourceSize = 0;
        int totalActualGets = 0;
        int totalActualHeads = 0;
        int totalStatsGetOperations = 0;
        int totalSuccessfulGets = 0;
        int totalRetries = 0;
        int totalImmediateRetries = 0;
        int totalCoordinatorRetries = 0;
        std::map<int, int> retryCodes;
        std::map<int, int> immediateRetryCodes;
        std::map<int, int> coordinatorRetryCodes;
        std::set<int> responseCodes;
        for (std::size_t index = 0; index < measurements.size(); ++index)
        {
            const LiveMeasurement& measurement = measurements[index];
            timings.push_back(measurement.milliseconds);
            openTimings.push_back(measurement.phases.openMs);
            georeferenceTimings.push_back(measurement.phases.georeferenceMs);
            readTimings.push_back(measurement.phases.readMs);
            closeTimings.push_back(measurement.phases.closeMs);
            summedOpenMs += measurement.phases.openMs;
            summedGeoreferenceMs += measurement.phases.georeferenceMs;
            summedReadMs += measurement.phases.readMs;
            summedCloseMs += measurement.phases.closeMs;
            summedTotalMs += measurement.phases.totalMs;
            totalSuccessfulBytes += measurement.successfulRangeBytes;
            totalDeclaredTransientBytes += measurement.declaredTransientBytes;
            totalImmediateTransientRetryBytes +=
                measurement.immediateTransientRetryBytes;
            totalCoordinatorTransientRetryBytes +=
                measurement.coordinatorTransientRetryBytes;
            totalActualBodyBytes += measurement.actualHttpBodyBytes;
            totalConservativeBodyUpperBound +=
                measurement.conservativeBodyUpperBound;
            totalActualGets += measurement.actualGetCount;
            totalActualHeads += measurement.actualHeadCount;
            totalStatsGetOperations += measurement.statsGetOperationCount;
            totalSuccessfulGets += measurement.successfulGetCount;
            totalRetries += measurement.transientRetryCount;
            totalImmediateRetries += measurement.immediateTransientRetryCount;
            totalCoordinatorRetries +=
                measurement.coordinatorTransientRetryCount;
            for (const auto& retry : measurement.transientRetryCodes)
                retryCodes[retry.first] += retry.second;
            for (const auto& retry : measurement.immediateTransientRetryCodes)
                immediateRetryCodes[retry.first] += retry.second;
            for (const auto& retry :
                 measurement.coordinatorTransientRetryCodes)
            {
                coordinatorRetryCodes[retry.first] += retry.second;
            }
            require(sourceSize == 0 || sourceSize == measurement.sourceSize,
                    item.name + " source size changed between live iterations");
            sourceSize = measurement.sourceSize;
            responseCodes.insert(measurement.responseCodes.begin(),
                                 measurement.responseCodes.end());
            iterations.push_back(liveIterationJson(
                static_cast<int>(index + 1), measurement));
        }

        const LatencySummary latency = summarizeLatency(timings);
        picojson::object latencyJson;
        latencyJson["median"] = picojson::value(latency.medianMs);
        latencyJson["p95"] = picojson::value(latency.p95Ms);
        picojson::object phaseTotals;
        phaseTotals["open"] = picojson::value(summedOpenMs);
        phaseTotals["georeference"] = picojson::value(summedGeoreferenceMs);
        phaseTotals["read"] = picojson::value(summedReadMs);
        phaseTotals["close"] = picojson::value(summedCloseMs);
        phaseTotals["total"] = picojson::value(summedTotalMs);
        picojson::object phaseMedians;
        phaseMedians["open"] = picojson::value(percentile(openTimings, 0.5));
        phaseMedians["georeference"] = picojson::value(
            percentile(georeferenceTimings, 0.5));
        phaseMedians["read"] = picojson::value(percentile(readTimings, 0.5));
        phaseMedians["close"] = picojson::value(percentile(closeTimings, 0.5));
        phaseMedians["total"] = picojson::value(latency.medianMs);
        picojson::object phases;
        phases["summed"] = picojson::value(phaseTotals);
        phases["median"] = picojson::value(phaseMedians);
        picojson::object httpCounts;
        httpCounts["actual_get"] = picojson::value(
            static_cast<double>(totalActualGets));
        httpCounts["actual_head"] = picojson::value(
            static_cast<double>(totalActualHeads));
        httpCounts["stats_get_operations"] = picojson::value(
            static_cast<double>(totalStatsGetOperations));
        httpCounts["successful_get"] = picojson::value(
            static_cast<double>(totalSuccessfulGets));
        httpCounts["transient_retries"] = picojson::value(
            static_cast<double>(totalRetries));
        httpCounts["immediate_transient_retries"] = picojson::value(
            static_cast<double>(totalImmediateRetries));
        httpCounts["coordinator_transient_retries"] = picojson::value(
            static_cast<double>(totalCoordinatorRetries));
        picojson::object bytes;
        bytes["successful_range"] = picojson::value(
            static_cast<double>(totalSuccessfulBytes));
        bytes["actual_http_body"] = picojson::value(
            static_cast<double>(totalActualBodyBytes));
        bytes["declared_transient"] = picojson::value(
            static_cast<double>(totalDeclaredTransientBytes));
        bytes["immediate_transient_retry"] = picojson::value(
            static_cast<double>(totalImmediateTransientRetryBytes));
        bytes["coordinator_transient_retry"] = picojson::value(
            static_cast<double>(totalCoordinatorTransientRetryBytes));
        bytes["conservative_body_upper_bound"] = picojson::value(
            static_cast<double>(totalConservativeBodyUpperBound));
        bytes["source_size"] = picojson::value(static_cast<double>(sourceSize));
        picojson::array responseCodeSummary;
        for (int response : responseCodes)
            responseCodeSummary.emplace_back(static_cast<double>(response));
        picojson::object result;
        result["name"] = picojson::value(item.name);
        result["fid"] = picojson::value(item.datasetId);
        result["year"] = picojson::value(static_cast<double>(item.year));
        result["iteration_count"] = picojson::value(
            static_cast<double>(measurements.size()));
        result["iterations"] = picojson::value(iterations);
        result["latency_ms"] = picojson::value(latencyJson);
        result["phases_ms"] = picojson::value(phases);
        result["http_counts"] = picojson::value(httpCounts);
        result["bytes"] = picojson::value(bytes);
        result["transient_retry_codes"] = picojson::value(
            retryCountsJson(retryCodes));
        result["immediate_transient_retry_codes"] = picojson::value(
            retryCountsJson(immediateRetryCodes));
        result["coordinator_transient_retry_codes"] = picojson::value(
            retryCountsJson(coordinatorRetryCodes));
        result["response_codes"] = picojson::value(responseCodeSummary);
        result["complete_cog"] = picojson::value(false);
        result["status"] = picojson::value(
            passesLatencyGate(latency) ? "PASS" : "FAIL");
        return picojson::value(result);
    }

    void verifyLatencyGateRegression()
    {
        const LatencySummary exact = summarizeLatency({1000, 2000, 3000, 7000, 8000});
        require(exact.medianMs == 3000.0 && exact.p95Ms == 8000.0,
                "latency percentile boundary changed");
        require(passesLatencyGate(exact), "exact latency limits must pass");
        require(!passesLatencyGate(summarizeLatency({1000, 2000, 3000.01, 7000, 8000})),
                "median above 3 seconds was accepted");
        require(!passesLatencyGate(summarizeLatency({1000, 2000, 2500, 7000, 8000.01})),
                "P95 above 8 seconds was accepted");
    }

    void verifyLiveCommandAndSummaryRegression()
    {
        // Candidate completion exists before the formal process starts.  Use the
        // still-absent formal target for pure command-regression successes so the
        // candidate-to-formal handoff cannot poison its own verifier.
        const std::string completion = V6_FORMAL_COMPLETION.string();
        const auto rejected = [](const std::vector<std::string>& arguments)
        {
            try
            {
                parseLiveCommand(arguments);
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };
        require(rejected({"--live-cases", "cases.json", "--iterations", "5",
                          "--profile", "unknown", "--evidence-dir", "evidence",
                          "--summary-json", "summary.json"}),
                "unknown live profile was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "one",
                          "--profile", "baseline", "--evidence-dir", "evidence",
                          "--summary-json", "summary.json"}),
                "malformed iteration count was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "2",
                          "--profile", "baseline", "--evidence-dir", "evidence",
                          "--summary-json", "summary.json"}),
                "unsupported iteration count was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "5",
                          "--profile", "optimized"}),
                "live command without summary path was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "5",
                          "--profile", "optimized", "--evidence-dir", "evidence",
                          "--summary-json", ""}),
                "live command with an empty summary path was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "5",
                          "--profile", "optimized", "--evidence-dir", "evidence",
                          "--summary-json", "/"}),
                "live command with a directory summary target was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "1",
                          "--profile", "optimized", "--evidence-dir", "evidence",
                          "--summary-json", "summary.json",
                          "--completion-json", completion,
                          "--enforce-latency"}),
                "latency enforcement with fewer than five iterations was accepted");
        require(rejected({"--live-cases", "cases.json", "--iteration", "5",
                          "--profile", "optimized", "--evidence-dir", "evidence",
                          "--summary-json", "summary.json"}),
                "unknown live argument was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "5",
                          "--profile", "prefetch", "--summary-json", "summary.json"}),
                "prefetch command without an explicit evidence directory was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "5",
                          "--profile", "prefetch", "--evidence-dir", "evidence",
                          "--summary-json", "summary.json", "--completion-json",
                          "relative-completion.json"}),
                "relative completion target was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "5",
                          "--profile", "prefetch", "--evidence-dir", "evidence",
                          "--summary-json", "summary.json", "--completion-json",
                          "/tmp/unapproved-completion.json"}),
                "unapproved completion target was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "5",
                          "--profile", "prefetch", "--evidence-dir", "evidence",
                          "--summary-json", "summary.json", "--completion-json",
                          completion, "--completion-json", completion}),
                "duplicate completion target was accepted");
        require(!approvedCompletionTarget(
                    V6_CANDIDATE_COMPLETION, true, false, false, false) &&
                !approvedCompletionTarget(
                    V6_CANDIDATE_COMPLETION, true, true, false, false) &&
                !approvedCompletionTarget(
                    V6_CANDIDATE_COMPLETION, false, false, true, false) &&
                !approvedCompletionTarget(
                    V6_CANDIDATE_COMPLETION, false, false, true, true),
                "completion target accepted existing/symlink target or temp");
        const std::string digestA(64, 'a');
        const std::string digestB(64, 'b');
        const std::string digestC(64, 'c');
        const std::string validCompletion = completionPayload(
            42, digestA, digestB, digestC);
        require(validCompletionPayload(validCompletion),
                "valid completion record failed schema validation");
        std::string wrongHash = validCompletion;
        wrongHash.replace(wrongHash.find(digestB), digestB.size(), "short");
        require(!validCompletionPayload(wrongHash),
                "completion record accepted a checksum/hash mismatch");
        std::string duplicateField = validCompletion;
        const std::size_t closing = duplicateField.rfind('}');
        duplicateField.insert(closing,
            ",\n  \"summary_sha256\": \"" + digestB + "\"\n");
        require(!validCompletionPayload(duplicateField),
                "completion record accepted a duplicate field");
        std::string fractionalPid = validCompletion;
        const std::size_t pidValue = fractionalPid.find("\"pid\": 42");
        require(pidValue != std::string::npos,
                "completion PID mutation source is missing");
        fractionalPid.replace(pidValue, std::string("\"pid\": 42").size(),
                              "\"pid\": 42.5");
        require(!validCompletionPayload(fractionalPid),
                "completion record accepted a fractional PID");

        const auto baseline = parseLiveCommand(
            {"--live-cases", "cases.json", "--iterations", "1", "--profile",
             "baseline", "--evidence-dir", "baseline-evidence",
             "--summary-json", "baseline.json", "--completion-json", completion});
        require(baseline.profile == RangeProfile::Baseline && baseline.iterations == 1 &&
                !baseline.enforceLatency && baseline.summaryPath == "baseline.json" &&
                baseline.evidenceDirectory == "baseline-evidence",
                "baseline smoke command parsed incorrectly");
        const auto optimized = parseLiveCommand(
            {"--live-cases", "cases.json", "--iterations", "5", "--profile",
             "optimized", "--evidence-dir", "optimized-evidence",
             "--summary-json", "optimized.json", "--completion-json", completion,
             "--enforce-latency"});
        require(optimized.profile == RangeProfile::Optimized &&
                optimized.iterations == 5 && optimized.enforceLatency,
                "optimized enforced command parsed incorrectly");
        const auto prefetchCommand = parseLiveCommand(
            {"--live-cases", "cases.json", "--iterations", "5", "--profile",
             "prefetch", "--evidence-dir", "prefetch-evidence",
             "--summary-json", "prefetch.json", "--completion-json", completion,
             "--enforce-latency"});
        require(prefetchCommand.profile == RangeProfile::Prefetch &&
                prefetchCommand.evidenceDirectory == "prefetch-evidence" &&
                std::string(profileName(prefetchCommand.profile)) == "prefetch",
                "prefetch enforced command parsed incorrectly");

        const std::string serialized = serializeLiveSummary(
            RangeProfile::Optimized, picojson::array(), true);
        picojson::value root;
        const std::string error = picojson::parse(root, serialized);
        require(error.empty() && root.is<picojson::object>(),
                "live summary serializer did not produce a JSON object");
        const picojson::object& object = root.get<picojson::object>();
        require(field(object, "profile").get<std::string>() == "optimized" &&
                field(object, "cases").get<picojson::array>().empty() &&
                field(object, "status").get<std::string>() == "PASS",
                "live summary top-level contract changed");
        const picojson::object& limits = field(object, "limits").get<picojson::object>();
        require(field(limits, "median_ms").get<double>() == 3000.0 &&
                field(limits, "p95_ms").get<double>() == 8000.0,
                "live summary latency limits changed");
        require(liveGateExitCode(false, false) == 0,
                "unenforced baseline latency failure returned nonzero");
        require(liveGateExitCode(true, true) == 0,
                "passing enforced latency gate returned nonzero");
        require(liveGateExitCode(true, false) != 0,
                "failing enforced latency gate returned zero");
        picojson::value failedRoot;
        require(picojson::parse(failedRoot, serializeLiveSummary(
                    RangeProfile::Optimized, picojson::array(), false)).empty() &&
                field(failedRoot.get<picojson::object>(), "status").get<std::string>() ==
                    "FAIL",
                "failing live summary did not preserve the gate outcome");

        LiveCase syntheticCase;
        syntheticCase.name = "synthetic";
        syntheticCase.datasetId = "42";
        syntheticCase.year = 2025;
        std::vector<LiveMeasurement> syntheticMeasurements(5);
        for (std::size_t index = 0; index < syntheticMeasurements.size(); ++index)
        {
            LiveMeasurement& measurement = syntheticMeasurements[index];
            measurement.milliseconds = 1000.0 * (index + 1);
            measurement.phases = {100.0, 200.0, 300.0,
                                  400.0 + 1000.0 * index,
                                  measurement.milliseconds};
            measurement.actualGetCount = 2;
            measurement.actualHeadCount = 1;
            measurement.statsGetOperationCount = 1;
            measurement.successfulGetCount = 1;
            measurement.transientRetryCount = 1;
            measurement.immediateTransientRetryCount = 1;
            measurement.coordinatorTransientRetryCount = 1;
            measurement.immediateTransientRetryBytes = 8;
            measurement.coordinatorTransientRetryBytes = 8;
            measurement.transientRetryCodes = {{503, 1}};
            measurement.immediateTransientRetryCodes = {{503, 1}};
            measurement.coordinatorTransientRetryCodes = {{500, 1}};
            measurement.responseCodes = {503, 206};
            measurement.successfulRangeBytes = 128;
            measurement.declaredTransientBytes = 8;
            measurement.actualHttpBodyBytes = 128;
            measurement.conservativeBodyUpperBound = 136;
            measurement.sourceSize = 4096;
        }
        const picojson::object synthetic = liveCaseSummaryJson(
            syntheticCase, syntheticMeasurements).get<picojson::object>();
        require(field(synthetic, "name").get<std::string>() == "synthetic" &&
                field(synthetic, "fid").get<std::string>() == "42" &&
                field(synthetic, "iteration_count").get<double>() == 5.0 &&
                field(synthetic, "status").get<std::string>() == "PASS",
                "live case identity or gate outcome JSON changed");
        const picojson::array& iterations =
            field(synthetic, "iterations").get<picojson::array>();
        require(iterations.size() == 5,
                "live case JSON omitted iteration evidence");
        const picojson::object& firstIteration =
            iterations.front().get<picojson::object>();
        require(field(firstIteration, "timing_ms").get<double>() == 1000.0 &&
                field(field(firstIteration, "phases_ms").get<picojson::object>(),
                      "open").get<double>() == 100.0 &&
                field(field(firstIteration, "http_counts").get<picojson::object>(),
                      "actual_get").get<double>() == 2.0 &&
                field(field(firstIteration, "http_counts").get<picojson::object>(),
                      "coordinator_transient_retries").get<double>() == 1.0 &&
                field(field(firstIteration, "http_counts").get<picojson::object>(),
                      "immediate_transient_retries").get<double>() == 1.0 &&
                field(field(firstIteration, "bytes").get<picojson::object>(),
                      "successful_range").get<double>() == 128.0 &&
                field(field(firstIteration, "bytes").get<picojson::object>(),
                      "coordinator_transient_retry").get<double>() == 8.0 &&
                field(field(firstIteration, "bytes").get<picojson::object>(),
                      "immediate_transient_retry").get<double>() == 8.0 &&
                field(firstIteration, "immediate_transient_retry_codes")
                    .get<picojson::object>().count("503") == 1 &&
                field(firstIteration, "coordinator_transient_retry_codes")
                    .get<picojson::object>().count("500") == 1,
                "live iteration timing, phase, HTTP, or byte JSON changed");
        const picojson::object& latency =
            field(synthetic, "latency_ms").get<picojson::object>();
        require(field(latency, "median").get<double>() == 3000.0 &&
                field(latency, "p95").get<double>() == 5000.0,
                "live case latency summary JSON changed");
        require(field(field(synthetic, "http_counts").get<picojson::object>(),
                      "actual_get").get<double>() == 10.0 &&
                field(field(synthetic, "http_counts").get<picojson::object>(),
                      "coordinator_transient_retries").get<double>() == 5.0 &&
                field(field(synthetic, "http_counts").get<picojson::object>(),
                      "immediate_transient_retries").get<double>() == 5.0 &&
                field(field(synthetic, "bytes").get<picojson::object>(),
                      "successful_range").get<double>() == 640.0 &&
                field(field(synthetic, "bytes").get<picojson::object>(),
                      "coordinator_transient_retry").get<double>() == 40.0 &&
                field(field(synthetic, "bytes").get<picojson::object>(),
                      "immediate_transient_retry").get<double>() == 40.0 &&
                field(synthetic, "response_codes").get<picojson::array>().size() == 2 &&
                field(synthetic, "transient_retry_codes").get<picojson::object>()
                    .count("503") == 1 &&
                field(synthetic, "immediate_transient_retry_codes")
                    .get<picojson::object>().count("503") == 1 &&
                field(synthetic, "coordinator_transient_retry_codes")
                    .get<picojson::object>().count("500") == 1,
                "live case aggregate HTTP, byte, retry, or response JSON changed");

        const std::filesystem::path evidenceRoot =
            std::filesystem::path(OSGSOL_SCIENCE_BUILD_DIR) /
            ("evidence-directory-regression-" +
             std::to_string(static_cast<long long>(getpid())));
        std::error_code cleanupError;
        std::filesystem::remove_all(evidenceRoot, cleanupError);
        const std::filesystem::path summaryDirectory = evidenceRoot / "summary";
        prepareLiveEvidenceDirectory(summaryDirectory);
        const std::filesystem::path summaryPath = summaryDirectory / "summary.json";
        {
            std::ofstream previous(summaryPath);
            previous << "stale";
        }
        initializeLiveSummary(summaryPath, RangeProfile::Optimized);
        std::ifstream initialStream(summaryPath);
        picojson::value initial;
        require(initialStream.good() && picojson::parse(initial, initialStream).empty() &&
                field(initial.get<picojson::object>(), "status").get<std::string>() ==
                    "ERROR",
                "live startup left stale summary evidence in place");
        writeAtomicSummary(summaryPath, serialized);
        std::ifstream summaryStream(summaryPath);
        picojson::value written;
        require(summaryStream.good() && picojson::parse(written, summaryStream).empty() &&
                written.is<picojson::object>(),
                "atomic live summary replacement is not valid JSON");
        for (const auto& entry : std::filesystem::directory_iterator(summaryDirectory))
            require(entry.path() == summaryPath,
                    "atomic live summary left a temporary sibling behind");

        prepareLiveEvidenceDirectory(evidenceRoot / "valid");
        require(std::filesystem::is_directory(evidenceRoot / "valid"),
                "live evidence directory was not created before iteration one");
        {
            std::ofstream file(evidenceRoot / "file");
            file << "not a directory";
        }
        require(rejectedEvidenceDirectory(evidenceRoot / "file"),
                "live evidence accepted an existing non-directory");
        std::filesystem::create_directory_symlink(evidenceRoot / "valid",
                                                  evidenceRoot / "symlink");
        require(rejectedEvidenceDirectory(evidenceRoot / "symlink"),
                "live evidence accepted a symlink");
        require(rejectedEvidenceDirectory(std::filesystem::temp_directory_path() /
                                          "outside-active-build"),
                "live evidence accepted a path outside the active build tree");
        require(rejectedEvidenceDirectory(OSGSOL_SCIENCE_PROTECTED_EVIDENCE_DIR),
                "live evidence accepted the protected old evidence directory");
        require(rejectedLiveSummaryPath(
                    std::filesystem::path(OSGSOL_SCIENCE_PROTECTED_EVIDENCE_DIR) /
                    "live-summary.json"),
                "live summary accepted the protected old evidence directory");
        require(rejectedLiveSummaryPath(
                    std::filesystem::temp_directory_path() / "outside-summary.json"),
                "live summary accepted a path outside the active build tree");
        const std::filesystem::path summaryTarget = evidenceRoot / "summary-target.json";
        const std::filesystem::path summarySymlink = evidenceRoot / "summary-symlink.json";
        {
            std::ofstream target(summaryTarget);
            target << "protected target";
        }
        std::filesystem::create_symlink(summaryTarget, summarySymlink);
        require(rejectedLiveSummaryPath(summarySymlink),
                "live summary accepted an existing symlink target");

        const std::filesystem::path originalDirectory = evidenceRoot / "original";
        const std::filesystem::path movedDirectory = evidenceRoot / "moved";
        const std::filesystem::path decoyDirectory = evidenceRoot / "decoy";
        std::filesystem::create_directory(originalDirectory);
        std::filesystem::create_directory(decoyDirectory);
        {
            auto secureDirectory = openSecureBuildDirectory(originalDirectory, false);
            std::filesystem::rename(originalDirectory, movedDirectory);
            std::filesystem::create_directory_symlink(decoyDirectory,
                                                      originalDirectory);
            writeAtomicFileAt(secureDirectory, "proof.json", "trusted\n");
        }
        std::ifstream trustedStream(movedDirectory / "proof.json");
        std::string trustedPayload;
        std::getline(trustedStream, trustedPayload);
        require(trustedPayload == "trusted" &&
                !std::filesystem::exists(decoyDirectory / "proof.json"),
                "evidence publication followed a substituted directory symlink");
        std::filesystem::remove_all(evidenceRoot, cleanupError);
    }

    int runLive(const std::filesystem::path& casesPath, int iterations,
                RangeProfile profile,
                const std::filesystem::path& evidenceDirectory,
                const std::filesystem::path& summaryPath,
                const std::filesystem::path& completionPath,
                const std::filesystem::path& executablePath,
                bool enforceLatency)
    {
        require(iterations == 1 || iterations == 5,
                "live evidence accepts one smoke iteration or five measured iterations");
        require(!enforceLatency || iterations == 5,
                "latency enforcement requires exactly five iterations");
        prepareLiveEvidenceDirectory(evidenceDirectory);
        verifyRangeAccessConfig(profile);
        const std::vector<LiveCase> cases = loadLiveCases(casesPath);
        picojson::array caseSummaries;
        bool allCasesPassed = true;
        for (const LiveCase& item : cases)
        {
            std::vector<double> timings;
            std::vector<double> openTimings;
            std::vector<double> georeferenceTimings;
            std::vector<double> readTimings;
            std::vector<double> closeTimings;
            std::vector<LiveMeasurement> measurements;
            double summedOpenMs = 0.0;
            double summedGeoreferenceMs = 0.0;
            double summedReadMs = 0.0;
            double summedCloseMs = 0.0;
            double summedTotalMs = 0.0;
            std::uint64_t totalSuccessfulBytes = 0;
            std::uint64_t totalDeclaredTransientBytes = 0;
            std::uint64_t totalActualBodyBytes = 0;
            std::uint64_t totalConservativeBodyUpperBound = 0;
            std::uint64_t sourceSize = 0;
            int totalRetries = 0;
            std::set<int> responses;
            std::map<int, int> retryCodes;
            for (int iteration = 1; iteration <= iterations; ++iteration)
            {
                LiveMeasurement measurement = runLiveIteration(
                    item, iteration, profile, evidenceDirectory);
                timings.push_back(measurement.milliseconds);
                openTimings.push_back(measurement.phases.openMs);
                georeferenceTimings.push_back(measurement.phases.georeferenceMs);
                readTimings.push_back(measurement.phases.readMs);
                closeTimings.push_back(measurement.phases.closeMs);
                summedOpenMs += measurement.phases.openMs;
                summedGeoreferenceMs += measurement.phases.georeferenceMs;
                summedReadMs += measurement.phases.readMs;
                summedCloseMs += measurement.phases.closeMs;
                summedTotalMs += measurement.phases.totalMs;
                totalSuccessfulBytes += measurement.successfulRangeBytes;
                totalDeclaredTransientBytes += measurement.declaredTransientBytes;
                totalActualBodyBytes += measurement.actualHttpBodyBytes;
                totalConservativeBodyUpperBound +=
                    measurement.conservativeBodyUpperBound;
                totalRetries += measurement.transientRetryCount;
                for (const auto& retry : measurement.transientRetryCodes)
                    retryCodes[retry.first] += retry.second;
                require(sourceSize == 0 || sourceSize == measurement.sourceSize,
                        item.name + " source size changed between live iterations");
                sourceSize = measurement.sourceSize;
                responses.insert(measurement.responseCodes.begin(),
                                 measurement.responseCodes.end());
                measurements.push_back(measurement);
                std::cout << "ScienceGdalLive case=" << item.name
                          << " fid=" << item.datasetId << " year=" << item.year
                          << " iteration=" << iteration
                          << " milliseconds=" << measurement.milliseconds
                          << " open_ms=" << measurement.phases.openMs
                          << " georeference_ms=" << measurement.phases.georeferenceMs
                          << " read_ms=" << measurement.phases.readMs
                          << " close_ms=" << measurement.phases.closeMs
                          << " total_ms=" << measurement.phases.totalMs
                          << " successful_range_bytes="
                          << measurement.successfulRangeBytes
                          << " actual_http_body_bytes="
                          << measurement.actualHttpBodyBytes
                          << " declared_transient_bytes="
                          << measurement.declaredTransientBytes
                          << " conservative_body_upper_bound_bytes="
                          << measurement.conservativeBodyUpperBound
                          << " http_gets=" << measurement.actualGetCount
                          << " successful_http_gets="
                          << measurement.successfulGetCount
                          << " transient_retries="
                          << measurement.transientRetryCount
                          << " http_heads=" << measurement.actualHeadCount
                          << " stats_get_operations=" << measurement.statsGetOperationCount
                          << " source_size=" << measurement.sourceSize << std::endl;
            }
            const LatencySummary latency = summarizeLatency(timings);
            const bool casePassed = passesLatencyGate(latency);
            allCasesPassed = allCasesPassed && casePassed;
            std::cout << "ScienceGdalLive summary case=" << item.name
                      << " fid=" << item.datasetId << " year=" << item.year
                      << " median_ms=" << latency.medianMs
                      << " p95_ms=" << latency.p95Ms
                      << " summed_open_ms=" << summedOpenMs
                      << " median_open_ms=" << percentile(openTimings, 0.5)
                      << " summed_georeference_ms=" << summedGeoreferenceMs
                      << " median_georeference_ms="
                      << percentile(georeferenceTimings, 0.5)
                      << " summed_read_ms=" << summedReadMs
                      << " median_read_ms=" << percentile(readTimings, 0.5)
                      << " summed_close_ms=" << summedCloseMs
                      << " median_close_ms=" << percentile(closeTimings, 0.5)
                      << " summed_total_ms=" << summedTotalMs
                      << " median_total_ms=" << percentile(timings, 0.5)
                      << " total_successful_range_bytes=" << totalSuccessfulBytes
                      << " total_actual_http_body_bytes=" << totalActualBodyBytes
                      << " total_declared_transient_bytes="
                      << totalDeclaredTransientBytes
                      << " total_conservative_body_upper_bound_bytes="
                      << totalConservativeBodyUpperBound
                      << " transient_retries=" << totalRetries
                      << " retry_codes=";
            for (const auto& retry : retryCodes)
                std::cout << retry.first << ':' << retry.second << ',';
            std::cout
                      << " source_size=" << sourceSize << " response_codes=";
            for (int response : responses) std::cout << response << ',';
            std::cout << " complete_cog=false status="
                      << (casePassed ? "PASS" : "FAIL") << std::endl;
            caseSummaries.push_back(liveCaseSummaryJson(item, measurements));
        }
        require(!caseSummaries.empty(), "pinned fixture contained no live cases");
        writeAtomicSummary(summaryPath,
                           serializeLiveSummary(profile, caseSummaries, allCasesPassed));
        if (allCasesPassed)
            publishLiveCompletion(evidenceDirectory, summaryPath,
                                  completionPath, executablePath);
        return liveGateExitCode(enforceLatency, allCasesPassed);
    }
}

int runMain(int argc, char** argv)
{
    const bool localMode = argc == 1;
    const bool validateMode = argc == 3 &&
        std::string(argv[1]) == "--validate-live-cases";
    const bool liveMode = argc > 1 && std::string(argv[1]) == "--live-cases";
    LiveCommand liveCommand;
    if (liveMode)
    {
        std::vector<std::string> arguments;
        arguments.reserve(static_cast<std::size_t>(argc - 1));
        for (int index = 1; index < argc; ++index)
            arguments.emplace_back(argv[index]);
        liveCommand = parseLiveCommand(arguments);
    }
    require(localMode || validateMode || liveMode,
            "usage: no arguments, --validate-live-cases FILE, or "
            "--live-cases FILE --iterations 1|5 "
            "--profile baseline|optimized|prefetch --evidence-dir DIRECTORY "
            "--summary-json FILE --completion-json FIXED-ABSENT-FILE "
            "[--enforce-latency]");
    if (liveMode)
    {
        prepareLiveEvidenceDirectory(liveCommand.evidenceDirectory);
        initializeLiveSummary(liveCommand.summaryPath, liveCommand.profile);
    }

    const RangeProfile processProfile = liveMode
        ? liveCommand.profile : RangeProfile::Optimized;
    ScopedGdalConfig processConfig(rangeAccessConfig(processProfile));
    verifyPathSpecificOptionLease();
    verifyRangeProfiles();
    verifyRangeAccessConfig(processProfile);
    verifyLatencyGateRegression();
    verifyLiveCommandAndSummaryRegression();
    const std::set<std::string> completionRootsBefore =
        completionPrimitiveRegressionTrees();
    verifyCompletionPrimitiveRegression();
    require(completionPrimitiveRegressionTrees() == completionRootsBefore,
            "completion primitive regression changed disposable root set");
    verifyCurlFaultInterposerSelfTests();
    verifyPrefetchReplayRegression();
    verifyPrefetchTransientFallbackReplayRegression();
    verifyPrefetchTransientRetryReplayRegression();
    verifyAttributedTransportRegression();
    verifyHttpParserRegression();
    registerScienceRuntime();
    verifyGeoreferenceRegression();
    if (validateMode)
    {
        const std::vector<LiveCase> cases = loadLiveCases(argv[2]);
        std::cout << "ScienceGdalCases: validated " << cases.size()
                  << " pinned raw rows" << std::endl;
        return 0;
    }
    if (liveMode)
        return runLive(liveCommand.casesPath, liveCommand.iterations,
                       liveCommand.profile, liveCommand.evidenceDirectory,
                       liveCommand.summaryPath, liveCommand.completionPath,
                       absoluteNormalizedPath(argv[0]),
                       liveCommand.enforceLatency);
    UniqueTempDirectory temporary("osgsol-science-http-range");
    const std::filesystem::path root = temporary.path();
    const std::filesystem::path fixture = root / "alphaearth-range-fixture.tif";
    const std::filesystem::path ready = root / "ready.txt";
    const std::filesystem::path log = root / "requests.jsonl";
    createFixture(fixture);
    const std::uint64_t sourceSize = std::filesystem::file_size(fixture);

    verifyCompletionSinkConcurrencyRegression(fixture, root);

    {
        DatasetPtr localDataset(static_cast<GDALDataset*>(GDALOpenEx(
            fixture.string().c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY,
            nullptr, nullptr, nullptr)), closeDataset);
        require(localDataset != nullptr, "failed to open local RGB oracle fixture");
        PixelWindow window;
        window.size = std::min(localDataset->GetRasterXSize(),
                               localDataset->GetRasterYSize());
        window.size -= window.size % LIVE_OVERVIEW_FACTOR;
        window.x = (localDataset->GetRasterXSize() - window.size) / 2;
        window.topDownY = (localDataset->GetRasterYSize() - window.size) / 2;
        window.x -= window.x % LIVE_OVERVIEW_FACTOR;
        window.topDownY -= window.topDownY % LIVE_OVERVIEW_FACTOR;
        const int bandMap[] = {2, 17, 10};
        const auto oracle =
            readNormalizedOverviewRgbOracle(localDataset.get(), window, bandMap);
        const auto batched =
            readNormalizedOverviewRgbBatched(localDataset.get(), window, bandMap);
        require(batched.rgb == oracle.rgb && batched.mask == oracle.mask,
                "batched RGB differs from the exact per-band overview oracle");
        bool hasNoData = false;
        bool hasValidZero = false;
        for (std::size_t pixel = 0; pixel < batched.rgb.size(); pixel += 3)
        {
            hasNoData = hasNoData || batched.mask[pixel] == 0;
            hasValidZero = hasValidZero ||
                (batched.rgb[pixel] == 0 && batched.mask[pixel] != 0);
        }
        require(hasNoData && hasValidZero,
                "batched RGB mask does not distinguish NoData from valid zero");
    }

    verifyParallelMetadataPrefetch(fixture, root);
    verifyV6HeadRecoveryContract(fixture, root);
    verifyV6InvalidHeadSurfaces(fixture, root);
    verifyImmediateMultiRangeRetry(fixture, root);

    ServerProcess server = startServer(fixture, ready, log);
    const int port = waitForPort(ready);
    ScopedGdalConfig config(rangeAccessConfig(RangeProfile::Optimized));
    verifyRangeAccessConfig(RangeProfile::Optimized);
    VSICurlClearCache();
    VSINetworkStatsReset();
    DebugCapture capture;
    ScopedGdalErrorCapture errorCapture(capture);
    const std::string url = "/vsicurl/http://127.0.0.1:" + std::to_string(port) +
        "/alphaearth-range-fixture.tif";
    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(
        url.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    require(dataset != nullptr, "GDAL failed to open the local HTTP COG fixture");

    const int bandMap[] = {2, 17, 10};
    constexpr int WINDOW_SIZE = 32;
    std::vector<std::int8_t> rgb(WINDOW_SIZE * WINDOW_SIZE * 3);
    require(dataset->RasterIO(GF_Read, 96, 96, 64, 64, rgb.data(),
                              WINDOW_SIZE, WINDOW_SIZE, GDT_Int8, 3, bandMap,
                              3, WINDOW_SIZE * 3, 1, nullptr) == CE_None,
            "bounded RGB window read failed");
    GDALClose(dataset);
    const std::string statsJson = requireNetworkStatsEvidence();
    writeRawTransportEvidence(OSGSOL_SCIENCE_EVIDENCE_DIR,
                              "local", capture, statsJson);
    const HttpProof proof = buildHttpProof(
        capture, statsJson, AttributionMode::LegacyFrozen);
    verifyOptimizedMetadataIntervals(proof);
    server.stop();
    const LocalServerEvidence local = verifyLog(log, sourceSize);
    require(local.getCount == proof.actualGetCount &&
            local.headCount == proof.actualHeadCount &&
            local.committedBytes == proof.actualHttpBodyBytes &&
            proof.actualHttpBodyBytes <= TRANSFER_BUDGET,
            "local server, curl headers, and VSINetworkStats evidence disagree");
    writeParsedProof(OSGSOL_SCIENCE_EVIDENCE_DIR, "local", proof);
    verifyV6CombinedOperationScope(fixture, root);
    return 0;
}

int main(int argc, char** argv)
{
    try
    {
        return runMain(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "ScienceHttpRanges failure: " << error.what() << std::endl;
        return 1;
    }
}
