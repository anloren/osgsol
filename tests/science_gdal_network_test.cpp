#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <CommonCrypto/CommonDigest.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <csignal>
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
    constexpr int SIZE = 256;
    constexpr int BAND_COUNT = 64;
    constexpr std::int8_t NODATA_VALUE = -128;
    constexpr int LIVE_OVERVIEW_FACTOR = 4;
    constexpr std::uint64_t TRANSFER_BUDGET = 1024 * 1024;
    constexpr std::uint64_t LIVE_TRANSFER_BUDGET = 16 * 1024 * 1024;
    constexpr double MAX_MEDIAN_MS = 3000.0;
    constexpr double MAX_P95_MS = 8000.0;

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
        std::map<int, int> transientRetryCodes;
        std::vector<int> responseCodes;
    };

    struct MetadataPrefetchProof
    {
        bool enabled = false;
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
    };

    struct HttpProof
    {
        int actualGetCount = 0;
        int actualHeadCount = 0;
        int successfulGetCount = 0;
        int transientRetryCount = 0;
        int statsGetOperationCount = 0;
        int statsHeadCount = 0;
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
        std::vector<int> responseCodes;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> successfulByteIntervals;
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
        std::string method;
        std::string range;
        std::uint64_t start = 0;
        std::uint64_t end = 0;
        int status = 0;
        std::uint64_t attemptedBodyBytes = 0;
        bool aborted = false;
    };

    struct Http2Evidence
    {
        std::vector<Http2StreamEvidence> streams;
        std::uint64_t maximumAttemptedBodyBytes = 0;
        std::uint64_t totalAttemptedBodyBytes = 0;
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
    };

    struct DebugCapture
    {
        std::mutex mutex;
        std::vector<std::string> messages;
        std::vector<std::chrono::steady_clock::time_point> timestamps;
    };

    MetadataPrefetchProof buildMetadataPrefetchProof(
        const DebugCapture& capture, const HttpProof& httpProof);

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

    class ScopedPathSpecificOption
    {
    public:
        ScopedPathSpecificOption(const std::string& path,
                                 const char* key, const char* value)
            : _path(path), _key(key)
        {
            if (_path.empty() || _key.empty() || value == nullptr)
                throw std::invalid_argument(
                    "path-specific option arguments must not be empty");
            VSISetPathSpecificOption(_path.c_str(), _key.c_str(), value);
        }

        ~ScopedPathSpecificOption()
        {
            VSISetPathSpecificOption(_path.c_str(), _key.c_str(), nullptr);
        }

        ScopedPathSpecificOption(const ScopedPathSpecificOption&) = delete;
        ScopedPathSpecificOption& operator=(const ScopedPathSpecificOption&) = delete;

    private:
        std::string _path;
        std::string _key;
    };

    const picojson::value& field(const picojson::object& object,
                                 const std::string& name);
    [[noreturn]] void fail(const std::string& message);
    void require(bool condition, const std::string& message);

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
            const std::string budget = std::to_string(4 * 131072);
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

    Http2Evidence verifyHttp2Log(const std::filesystem::path& log)
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
                        event == "stream_end",
                    "HTTP/2 server emitted an unknown event");
            const std::string session =
                field(object, "session_id").get<std::string>();
            const int streamId =
                static_cast<int>(field(object, "stream_id").get<double>());
            Http2StreamEvidence& record = records[{session, streamId}];
            record.sessionId = session;
            record.streamId = streamId;
            const std::uint64_t timestamp = static_cast<std::uint64_t>(
                field(object, "monotonic_ns").get<double>());
            if (event == "stream_start")
            {
                require(record.start == 0, "duplicate HTTP/2 stream_start event");
                record.start = timestamp;
                record.method = field(object, "method").get<std::string>();
                const picojson::value& range = field(object, "range");
                record.range = range.is<std::string>() ? range.get<std::string>() : "";
            }
            else if (event == "response_headers")
            {
                record.status =
                    static_cast<int>(field(object, "status").get<double>());
                require(field(object, "violation").is<picojson::null>(),
                        "HTTP/2 server rejected a client request");
            }
            else
            {
                require(record.end == 0, "duplicate HTTP/2 stream_end event");
                record.end = timestamp;
                record.attemptedBodyBytes = static_cast<std::uint64_t>(
                    field(object, "attempted_body_bytes").get<double>());
                evidence.totalAttemptedBodyBytes = std::max(
                    evidence.totalAttemptedBodyBytes,
                    static_cast<std::uint64_t>(
                        field(object,
                            "total_attempted_body_bytes").get<double>()));
                record.aborted = field(object, "aborted").get<bool>();
            }
        }
        for (const auto& item : records)
        {
            require(item.second.start > 0 && item.second.end > item.second.start &&
                        item.second.status > 0,
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

        const PrefetchCase cases[] = {
            {"success", true, false},
            {"range-503", true, true},
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
        const char* selectedMode =
            CPLGetConfigOption("OSGSOL_TEST_PREFETCH_CASE", nullptr);
        int executedCases = 0;
        for (const PrefetchCase& prefetchCase : cases)
        {
            const std::string caseName = std::string(prefetchCase.mode) +
                (prefetchCase.variant[0] ? "-" + std::string(prefetchCase.variant) : "");
            if (selectedMode && std::string(selectedMode) != caseName)
                continue;
            ++executedCases;
            const std::filesystem::path ready =
                root / (std::string("http2-") + caseName + ".ready");
            const std::filesystem::path log =
                root / (std::string("http2-") + caseName + ".jsonl");
            ServerProcess server = startHttp2Server(
                fixture, ready, log, certificate, key, prefetchCase.mode,
                std::string(prefetchCase.variant) == "http1" ? "http1" : "h2");
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
                {"GDAL_HTTP_RETRY_DELAY", "0.01"},
                {"OSGSOL_VSICURL_PREFETCH_HEAD_RANGE",
                    std::string(prefetchCase.activation) == "global" ? "YES" : ""},
            });
            VSICurlClearCache();
            VSINetworkStatsReset();
            DebugCapture capture;
            bool opened = false;
            {
                ScopedGdalErrorCapture errorCapture(capture);
                if (std::string(prefetchCase.activation) == "path")
                {
                    VSISetPathSpecificOption(vsiUrl.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
                    if (std::string(prefetchCase.variant) == "remove-retry")
                    {
                        setenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE", "1", 1);
                    }
                    if (std::string(prefetchCase.variant) == "remove-failure")
                    {
                        setenv("OSGSOL_TEST_FAIL_NEXT_CURL_REMOVE", "2", 1);
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
                    GDALClose(dataset);
                }
                if (std::string(prefetchCase.activation) == "path")
                    VSIClearPathSpecificOptions(vsiUrl.c_str());
            }
            server.stop();
            const Http2Evidence evidence = verifyHttp2Log(log);

            const Http2StreamEvidence* head = nullptr;
            const Http2StreamEvidence* firstRange = nullptr;
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
            require(containsDebug(capture, "ParallelHeadRange: started") ==
                        expectedStarted,
                    caseName + " coordinator activation changed");
            if (std::string(prefetchCase.mode) != "success" &&
                prefetchCase.requireSharedHttp2)
                verifyTransport();

            const bool published =
                containsDebug(capture, "ParallelHeadRange: published");
            const bool fallback =
                containsDebug(capture, "ParallelHeadRange: fallback=");
            const bool expectedPublication =
                std::string(prefetchCase.mode) == "success" &&
                (prefetchCase.variant[0] == '\0' ||
                 prefetchCase.completionOrder[0] != '\0');
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
            require(countDebug(capture,
                        "ParallelHeadRange: detach-success=head") ==
                        (expectedStarted ? 1 : 0) &&
                    countDebug(capture,
                        "ParallelHeadRange: detach-success=range") ==
                        (expectedStarted && !removeFailure && !removeRetry
                             ? 1 : 0),
                    caseName + " detach results changed");
            require(countDebug(capture,
                        "ParallelHeadRange: cleanup-call=head") ==
                        (expectedStarted ? 1 : 0) &&
                    countDebug(capture,
                        "ParallelHeadRange: cleanup-call=range") ==
                        (expectedStarted ? 1 : 0),
                    caseName + " cleanup call counts changed");
            require(countDebug(capture,
                        "ParallelHeadRange: cleanup-success=head") ==
                        (expectedStarted ? 1 : 0) &&
                    countDebug(capture,
                        "ParallelHeadRange: cleanup-success=range") ==
                        (expectedStarted && !removeFailure ? 1 : 0),
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
            require(countDebug(capture,
                        "ParallelHeadRange: file-property-published") ==
                        prefetchCase.expectedFilePropertyPublications,
                    caseName + " file-property publication count changed");
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
            else if (!prefetchCase.opens)
            {
                require(rangeCount == 1,
                        std::string(prefetchCase.mode) +
                            " retried an invalid integrity response");
            }
            if (!prefetchCase.opens && !prefetchCase.fallback &&
                std::string(prefetchCase.mode) != "range-200-body")
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
        bool enforceLatency = false;
    };

    const char* profileName(RangeProfile profile)
    {
        if (profile == RangeProfile::Prefetch) return "prefetch";
        return profile == RangeProfile::Optimized ? "optimized" : "baseline";
    }

    LiveCommand parseLiveCommand(const std::vector<std::string>& arguments)
    {
        require(arguments.size() == 10 || arguments.size() == 11,
                "live command requires cases, iterations, profile, evidence, and summary paths");
        require(arguments[0] == "--live-cases" && arguments[2] == "--iterations" &&
                arguments[4] == "--profile" && arguments[6] == "--evidence-dir" &&
                arguments[8] == "--summary-json",
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
        if (arguments.size() == 11)
        {
            require(arguments[10] == "--enforce-latency",
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
            {
                ScopedPathSpecificOption activation(activationPath,
                    "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
                require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                            "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")) == "YES",
                        "prefetch path option did not activate");
            }
            require(std::string(VSIGetPathSpecificOption(activationPath.c_str(),
                        "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")).empty(),
                    "prefetch path option survived RAII cleanup");
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
        stream << "},\n"
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

    HttpProof buildHttpProof(const DebugCapture& capture, const std::string& statsJson)
    {
        struct Request
        {
            std::string method;
            std::string range;
        };
        struct Response
        {
            int code = 0;
            std::map<std::string, std::string> headers;
        };
        std::vector<Request> requests;
        std::vector<Response> responses;
        std::vector<std::pair<std::string, int>> retryEvents;
        Response currentResponse;
        bool responseOpen = false;
        int logicalGetOperations = 0;

        for (const std::string& message : capture.messages)
        {
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
                std::string uri;
                firstLineStream >> uri;
                const auto headers = parseHeaders(stream);
                const auto range = headers.find("range");
                if (range != headers.end()) request.range = range->second;
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
                    if (responseOpen) responses.push_back(currentResponse);
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
            if (message.rfind("VSICURL: Got response_code=206", 0) == 0 ||
                message == "VSICURL: Download completed")
                ++logicalGetOperations;
            static const std::regex retryPattern(
                R"(HTTP error code for .* range ([0-9]+-[0-9]+): ([0-9]+)\. Retrying)");
            std::smatch retryMatch;
            if (std::regex_search(message, retryMatch, retryPattern))
                retryEvents.emplace_back("bytes=" + retryMatch[1].str(),
                                         std::stoi(retryMatch[2].str()));
        }
        if (responseOpen) responses.push_back(currentResponse);

        HttpProof proof;
        const std::set<int> transientCodes = {429, 500, 502, 503, 504};
        std::map<std::string, int> requestedRanges;
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
        }

        std::map<std::string, int> successfulRanges;
        std::map<int, int> transientResponses;
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
                proof.declaredTransientBytes += std::stoull(contentLength->second);
                ++transientResponses[response.code];
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
            require(transientCodes.count(retry.second) != 0,
                    "CPL retry event used an unlisted transient response code");
            ++retriesByRange[retry.first];
            ++retryCodes[retry.second];
            require(retriesByRange[retry.first] <= 3,
                    "ranged GET exceeded the three-retry policy");
            require(requestedRanges.count(retry.first) != 0,
                    "CPL retry event names a Range that was never emitted");
        }
        require(retryCodes == transientResponses,
                "transient HTTP responses do not reconcile with CPL retry events");
        for (const auto& request : requestedRanges)
        {
            const int successful = successfulRanges[request.first];
            const int retries = retriesByRange[request.first];
            require(successful > 0,
                    "emitted GET Range had no final HTTP 206 response");
            require(request.second == successful + retries,
                    "emitted GET Range count does not reconcile with retries and HTTP 206");
        }
        require(proof.actualGetCount ==
                    proof.successfulGetCount + static_cast<int>(retryEvents.size()),
                "GET request count does not reconcile with successes and retries");
        proof.transientRetryCount = static_cast<int>(retryEvents.size());
        proof.transientRetryCodes = retryCodes;
        proof.actualHttpBodyBytes = proof.successfulRangeBytes;
        proof.conservativeBodyUpperBound =
            proof.successfulRangeBytes + proof.declaredTransientBytes;
        require(proof.actualGetCount > 0 && proof.successfulRangeBytes > 0 &&
                proof.successfulRangeBytes < proof.sourceSize,
                "HTTP proof is empty or equals the complete object");

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
        require(statsBytes == proof.successfulRangeBytes,
                "VSINetworkStats downloaded bytes disagree with HTTP response bodies");
        require(proof.statsHeadCount == proof.actualHeadCount,
                "VSINetworkStats HEAD count disagrees with curl headers");
        require(proof.statsGetOperationCount == logicalGetOperations,
                "VSINetworkStats GET operations disagree with CPL read operations");
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
            long long connectionId = -1;
        };

        require(capture.messages.size() == capture.timestamps.size(),
                "metadata prefetch evidence is missing steady-clock timestamps");
        std::vector<RequestEvidence> requests;
        std::vector<ResponseEvidence> responses;
        int activeResponse = -1;
        std::vector<int> pendingConnections;
        int pendingStreamId = -1;
        bool pendingMultiplexReuse = false;
        int publicationCount = 0;
        std::vector<std::string> fallbackReasons;
        static const std::regex connectionPattern(
            R"(Connection #([0-9]+) to host .+ left intact)");
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
                    const bool isHead = response.code == 200 &&
                        response.headers.count("content-length") != 0 &&
                        response.headers.count("content-range") == 0;
                    const bool isRange = response.code == 206 &&
                        response.headers.count("content-range") != 0;
                    if (isHead || isRange)
                        pendingConnections.push_back(activeResponse);
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
                std::smatch connectionMatch;
                if (std::regex_search(message, connectionMatch, connectionPattern))
                {
                    const long long connectionId =
                        std::stoll(connectionMatch[1].str());
                    if (!pendingConnections.empty())
                    {
                        require(pendingConnections.size() == 1,
                                "metadata prefetch connection roles are ambiguous");
                        responses[pendingConnections.front()].connectionId =
                            connectionId;
                        pendingConnections.clear();
                    }
                }
                if (message.find("ParallelHeadRange: published") != std::string::npos)
                    ++publicationCount;
                const std::string fallbackPrefix = "ParallelHeadRange: fallback=";
                const std::size_t fallback = message.find(fallbackPrefix);
                if (fallback != std::string::npos)
                    fallbackReasons.push_back(message.substr(
                        fallback + fallbackPrefix.size()));
            }
        }
        require(activeResponse < 0 && pendingConnections.empty(),
                "metadata prefetch response or connection evidence is incomplete");

        const RequestEvidence* headRequest = nullptr;
        const RequestEvidence* rangeRequest = nullptr;
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
                    if (!rangeRequest) rangeRequest = &request;
                }
            }
        }
        require(headRequestCount == 1 && headRequest != nullptr &&
                httpProof.actualHeadCount == 1,
                "metadata prefetch requires exactly one HEAD request");
        require(rangeRequestCount == 1 && rangeRequest != nullptr,
                "metadata prefetch requires exactly one initial 128 KiB Range request");
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
        require(headRequest->streamId > 0 && rangeRequest->streamId > 0 &&
                rangeRequest->streamId > headRequest->streamId &&
                headRequest->uri == rangeRequest->uri &&
                rangeRequest->multiplexReuse &&
                headResponse->connectionId >= 0 &&
                headResponse->connectionId == rangeResponse->connectionId,
                "metadata prefetch lacks unambiguous shared HTTP/2 connection evidence");
        require(headRequest->sent < rangeRequest->sent &&
                rangeRequest->sent < headResponse->completed,
                "metadata prefetch request headers did not overlap HEAD completion");
        require(publicationCount == 1,
                "metadata prefetch cache publication evidence is missing or ambiguous");
        require(fallbackReasons.empty(),
                "metadata prefetch formal proof contains a fallback");

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
        const HttpProof proof = buildHttpProof(capture, stats);
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

        const auto isRejected = [&stats](const DebugCapture& candidate)
        {
            try
            {
                static_cast<void>(buildHttpProof(candidate, stats));
                return false;
            }
            catch (const std::exception&)
            {
                return true;
            }
        };

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
        const HttpProof multiplexedProof = buildHttpProof(multiplexed, multiplexedStats);
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
            buildHttpProof(highOffsetFirst, highOffsetFirstStats);
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
        };
        HttpProof prefetchHttp;
        prefetchHttp.actualHeadCount = 1;
        prefetchHttp.actualGetCount = 1;
        prefetchHttp.successfulGetCount = 1;
        prefetchHttp.successfulByteIntervals = {{0, 131071}};
        prefetchHttp.metadataPrefetch = buildMetadataPrefetchProof(prefetch, prefetchHttp);
        const std::string expectedMetadata =
            "  \"metadata_prefetch\": {\n"
            "    \"enabled\": true,\n"
            "    \"head_request_count\": 1,\n"
            "    \"range_request_count\": 1,\n"
            "    \"range_start\": 0,\n"
            "    \"range_end\": 131071,\n"
            "    \"head_http_version\": 2,\n"
            "    \"range_http_version\": 2,\n"
            "    \"shared_connection\": true,\n"
            "    \"requests_overlapped\": true,\n"
            "    \"cache_published\": true,\n"
            "    \"fallback_reason\": \"\"\n"
            "  }";
        require(serializeProof(prefetchHttp).find(expectedMetadata) != std::string::npos,
                "metadata_prefetch exact serialized object changed");

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
        distinctConnection.messages[14] =
            "CURL_INFO_TEXT: Connection #8 to host proxy.example left intact";
        require(prefetchRejected(distinctConnection),
                "metadata prefetch proof accepted distinct connections");
        DebugCapture missingRangeConnection;
        missingRangeConnection.messages = prefetch.messages;
        missingRangeConnection.timestamps = prefetch.timestamps;
        missingRangeConnection.messages.erase(
            missingRangeConnection.messages.begin() + 10);
        missingRangeConnection.timestamps.erase(
            missingRangeConnection.timestamps.begin() + 10);
        require(prefetchRejected(missingRangeConnection),
                "metadata prefetch proof accepted one marker for two responses");
        DebugCapture ambiguousConnections;
        ambiguousConnections.messages = prefetch.messages;
        ambiguousConnections.timestamps = prefetch.timestamps;
        ambiguousConnections.messages.erase(
            ambiguousConnections.messages.begin() + 10);
        ambiguousConnections.timestamps.erase(
            ambiguousConnections.timestamps.begin() + 10);
        ambiguousConnections.messages.insert(
            ambiguousConnections.messages.begin() + 14,
            "CURL_INFO_TEXT: Connection #8 to host proxy.example left intact");
        ambiguousConnections.timestamps.insert(
            ambiguousConnections.timestamps.begin() + 14,
            epoch + std::chrono::milliseconds(16));
        require(prefetchRejected(ambiguousConnections),
                "metadata prefetch proof mapped ambiguous responses to connection #7");
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
        ambiguousPublication.timestamps.push_back(epoch + std::chrono::milliseconds(17));
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
        fallback.timestamps.push_back(epoch + std::chrono::milliseconds(17));
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
        std::unique_ptr<ScopedPathSpecificOption> prefetchActivation;
        if (profile == RangeProfile::Prefetch)
        {
            prefetchActivation = std::make_unique<ScopedPathSpecificOption>(
                vsiUrl, "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "YES");
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
        prefetchActivation.reset();
        require(std::string(VSIGetPathSpecificOption(vsiUrl.c_str(),
                    "OSGSOL_VSICURL_PREFETCH_HEAD_RANGE", "")).empty(),
                "metadata prefetch path option survived dataset close");
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
        HttpProof proof = buildHttpProof(capture, statsJson);
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
        measurement.transientRetryCodes = proof.transientRetryCodes;
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

        picojson::object bytes;
        bytes["successful_range"] = picojson::value(
            static_cast<double>(measurement.successfulRangeBytes));
        bytes["declared_transient"] = picojson::value(
            static_cast<double>(measurement.declaredTransientBytes));
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
        std::uint64_t totalActualBodyBytes = 0;
        std::uint64_t totalConservativeBodyUpperBound = 0;
        std::uint64_t sourceSize = 0;
        int totalActualGets = 0;
        int totalActualHeads = 0;
        int totalStatsGetOperations = 0;
        int totalSuccessfulGets = 0;
        int totalRetries = 0;
        std::map<int, int> retryCodes;
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
            totalActualBodyBytes += measurement.actualHttpBodyBytes;
            totalConservativeBodyUpperBound +=
                measurement.conservativeBodyUpperBound;
            totalActualGets += measurement.actualGetCount;
            totalActualHeads += measurement.actualHeadCount;
            totalStatsGetOperations += measurement.statsGetOperationCount;
            totalSuccessfulGets += measurement.successfulGetCount;
            totalRetries += measurement.transientRetryCount;
            for (const auto& retry : measurement.transientRetryCodes)
                retryCodes[retry.first] += retry.second;
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
        picojson::object bytes;
        bytes["successful_range"] = picojson::value(
            static_cast<double>(totalSuccessfulBytes));
        bytes["actual_http_body"] = picojson::value(
            static_cast<double>(totalActualBodyBytes));
        bytes["declared_transient"] = picojson::value(
            static_cast<double>(totalDeclaredTransientBytes));
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
                          "--enforce-latency"}),
                "latency enforcement with fewer than five iterations was accepted");
        require(rejected({"--live-cases", "cases.json", "--iteration", "5",
                          "--profile", "optimized", "--evidence-dir", "evidence",
                          "--summary-json", "summary.json"}),
                "unknown live argument was accepted");
        require(rejected({"--live-cases", "cases.json", "--iterations", "5",
                          "--profile", "prefetch", "--summary-json", "summary.json"}),
                "prefetch command without an explicit evidence directory was accepted");

        const auto baseline = parseLiveCommand(
            {"--live-cases", "cases.json", "--iterations", "1", "--profile",
             "baseline", "--evidence-dir", "baseline-evidence",
             "--summary-json", "baseline.json"});
        require(baseline.profile == RangeProfile::Baseline && baseline.iterations == 1 &&
                !baseline.enforceLatency && baseline.summaryPath == "baseline.json" &&
                baseline.evidenceDirectory == "baseline-evidence",
                "baseline smoke command parsed incorrectly");
        const auto optimized = parseLiveCommand(
            {"--live-cases", "cases.json", "--iterations", "5", "--profile",
             "optimized", "--evidence-dir", "optimized-evidence",
             "--summary-json", "optimized.json", "--enforce-latency"});
        require(optimized.profile == RangeProfile::Optimized &&
                optimized.iterations == 5 && optimized.enforceLatency,
                "optimized enforced command parsed incorrectly");
        const auto prefetchCommand = parseLiveCommand(
            {"--live-cases", "cases.json", "--iterations", "5", "--profile",
             "prefetch", "--evidence-dir", "prefetch-evidence",
             "--summary-json", "prefetch.json", "--enforce-latency"});
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
            measurement.transientRetryCodes = {{503, 1}};
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
                field(field(firstIteration, "bytes").get<picojson::object>(),
                      "successful_range").get<double>() == 128.0,
                "live iteration timing, phase, HTTP, or byte JSON changed");
        const picojson::object& latency =
            field(synthetic, "latency_ms").get<picojson::object>();
        require(field(latency, "median").get<double>() == 3000.0 &&
                field(latency, "p95").get<double>() == 5000.0,
                "live case latency summary JSON changed");
        require(field(field(synthetic, "http_counts").get<picojson::object>(),
                      "actual_get").get<double>() == 10.0 &&
                field(field(synthetic, "bytes").get<picojson::object>(),
                      "successful_range").get<double>() == 640.0 &&
                field(synthetic, "response_codes").get<picojson::array>().size() == 2 &&
                field(synthetic, "transient_retry_codes").get<picojson::object>()
                    .count("503") == 1,
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
                const std::filesystem::path& summaryPath, bool enforceLatency)
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
            "--summary-json FILE [--enforce-latency]");
    if (liveMode)
    {
        prepareLiveEvidenceDirectory(liveCommand.evidenceDirectory);
        initializeLiveSummary(liveCommand.summaryPath, liveCommand.profile);
    }

    const RangeProfile processProfile = liveMode
        ? liveCommand.profile : RangeProfile::Optimized;
    ScopedGdalConfig processConfig(rangeAccessConfig(processProfile));
    verifyRangeProfiles();
    verifyRangeAccessConfig(processProfile);
    verifyLatencyGateRegression();
    verifyLiveCommandAndSummaryRegression();
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
                       liveCommand.summaryPath,
                       liveCommand.enforceLatency);
    UniqueTempDirectory temporary("osgsol-science-http-range");
    const std::filesystem::path root = temporary.path();
    const std::filesystem::path fixture = root / "alphaearth-range-fixture.tif";
    const std::filesystem::path ready = root / "ready.txt";
    const std::filesystem::path log = root / "requests.jsonl";
    createFixture(fixture);
    const std::uint64_t sourceSize = std::filesystem::file_size(fixture);

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
    const HttpProof proof = buildHttpProof(capture, statsJson);
    verifyOptimizedMetadataIntervals(proof);
    server.stop();
    const LocalServerEvidence local = verifyLog(log, sourceSize);
    require(local.getCount == proof.actualGetCount &&
            local.headCount == proof.actualHeadCount &&
            local.committedBytes == proof.actualHttpBodyBytes &&
            proof.actualHttpBodyBytes <= TRANSFER_BUDGET,
            "local server, curl headers, and VSINetworkStats evidence disagree");
    writeParsedProof(OSGSOL_SCIENCE_EVIDENCE_DIR, "local", proof);
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
