#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cpl_conv.h>
#include <cpl_string.h>
#include <cpl_vsi.h>
#include <cpl_vsi_virtual.h>
#include <gdal_frmts.h>
#include <gdal_priv.h>
#include <picojson.h>

#ifndef OSGSOL_SCIENCE_RANGE_SERVER
#error OSGSOL_SCIENCE_RANGE_SERVER must name the instrumented local server
#endif

#ifndef OSGSOL_PYTHON3
#error OSGSOL_PYTHON3 must name the Python interpreter
#endif

namespace
{
    constexpr int SIZE = 256;
    constexpr int BAND_COUNT = 64;
    constexpr std::uint64_t TRANSFER_BUDGET = 1024 * 1024;

    struct LiveCase
    {
        std::string name;
        std::string datasetId;
        std::string url;
        int year = 0;
        double latitude = 0.0;
        double longitude = 0.0;
        std::vector<double> bbox;
    };

    struct LiveMeasurement
    {
        double milliseconds = 0.0;
        std::uint64_t requestedBytes = 0;
        std::uint64_t sourceSize = 0;
        std::vector<int> responseCodes;
    };

    struct DebugCapture
    {
        std::mutex mutex;
        std::vector<std::string> messages;
    };

    [[noreturn]] void fail(const std::string& message)
    {
        std::cerr << "ScienceHttpRanges failure: " << message << std::endl;
        std::exit(1);
    }

    void require(bool condition, const std::string& message)
    {
        if (!condition) fail(message);
    }

    struct ServerProcess
    {
        pid_t pid = -1;

        ~ServerProcess()
        {
            if (pid > 0)
            {
                kill(pid, SIGTERM);
                waitpid(pid, nullptr, 0);
            }
        }

        void stop()
        {
            if (pid <= 0) return;
            kill(pid, SIGTERM);
            int status = 0;
            require(waitpid(pid, &status, 0) == pid, "failed to reap the range server");
            require(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                    "instrumented range server reported a violation");
            pid = -1;
        }
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
            GDALRasterBand* band = dataset->GetRasterBand(bandIndex + 1);
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

    std::uint64_t integerField(const std::string& line, const std::string& field)
    {
        const std::regex expression("\\\"" + field + "\\\"[ ]*:[ ]*([0-9]+)");
        std::smatch match;
        require(std::regex_search(line, match, expression),
                "range log is missing numeric field " + field);
        return std::stoull(match[1].str());
    }

    void verifyLog(const std::filesystem::path& log, std::uint64_t sourceSize)
    {
        std::ifstream stream(log);
        require(stream.good(), "instrumented range log is missing");
        std::uint64_t totalBytes = 0;
        int rangedGets = 0;
        std::string line;
        while (std::getline(stream, line))
        {
            require(line.find("\"method\"") != std::string::npos &&
                    line.find("\"uri\"") != std::string::npos &&
                    line.find("\"range\"") != std::string::npos,
                    "range log omitted method/URI/Range evidence");
            const std::uint64_t response = integerField(line, "response_code");
            const std::uint64_t bytes = integerField(line, "bytes_sent");
            const std::uint64_t recordedSourceSize = integerField(line, "source_size");
            require(recordedSourceSize == sourceSize, "range log source size changed");
            totalBytes += bytes;

            const bool isGet = line.find("\"method\": \"GET\"") != std::string::npos;
            if (isGet)
            {
                ++rangedGets;
                const std::regex rangeExpression("\\\"range\\\"[ ]*:[ ]*(\\\"[^\\\"]*\\\"|null)");
                std::smatch rangeMatch;
                require(std::regex_search(line, rangeMatch, rangeExpression),
                        "range log is missing the Range value");
                const std::string rangeValue = rangeMatch[1].str();
                require(rangeValue != "null",
                        "remote COG GET omitted the Range header");
                require(rangeValue.find(',') == std::string::npos,
                        "comma-separated multi-range syntax was emitted");
                require(response == 206, "remote COG GET did not receive HTTP 206");
            }
            require(!(response == 200 && bytes == sourceSize),
                    "server returned the complete fixture as HTTP 200");
        }
        require(rangedGets > 0, "GDAL emitted no ranged GET requests");
        require(totalBytes <= TRANSFER_BUDGET, "range transfer exceeded the explicit budget");
        require(totalBytes < sourceSize, "range transfer equaled the complete source file");
        std::cout << "ScienceHttpRanges: requests=" << rangedGets
                  << " bytes=" << totalBytes << " budget=" << TRANSFER_BUDGET
                  << " source_size=" << sourceSize << std::endl;
    }

    void configureRangeAccess()
    {
        CPLSetConfigOption("GDAL_HTTP_MULTIRANGE", "YES");
        CPLSetConfigOption("GDAL_HTTP_MERGE_CONSECUTIVE_RANGES", "YES");
        CPLSetConfigOption("CPL_VSIL_CURL_ALLOWED_EXTENSIONS", ".tif,.tiff,.vrt");
        CPLSetConfigOption("GDAL_DISABLE_READDIR_ON_OPEN", "EMPTY_DIR");
        CPLSetConfigOption("CPL_VSIL_CURL_USE_HEAD", "YES");
        CPLSetConfigOption("GDAL_HTTP_MAX_RETRY", "3");
        CPLSetConfigOption("GDAL_HTTP_RETRY_DELAY", "0.1");
        CPLSetConfigOption("GDAL_HTTP_RETRY_CODES", "429,500,502,503,504");
        require(std::string(CPLGetConfigOption("GDAL_HTTP_MULTIRANGE", "")) == "YES",
                "GDAL_HTTP_MULTIRANGE must be YES");
    }

    void CPL_STDCALL captureGdalMessage(CPLErr errorClass, CPLErrorNum,
                                        const char* message)
    {
        DebugCapture* capture = static_cast<DebugCapture*>(CPLGetErrorHandlerUserData());
        if (capture && message)
        {
            std::lock_guard<std::mutex> lock(capture->mutex);
            capture->messages.emplace_back(message);
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
        const picojson::value& casesValue = field(rootObject, "cases");
        require(casesValue.is<picojson::array>(), "live cases must be an array");

        std::vector<LiveCase> cases;
        for (const picojson::value& value : casesValue.get<picojson::array>())
        {
            const picojson::object& object = objectValue(value, "live case");
            LiveCase item;
            item.name = field(object, "name").get<std::string>();
            item.datasetId = field(object, "dataset_id").get<std::string>();
            item.url = field(object, "cog_url").get<std::string>();
            item.year = static_cast<int>(field(object, "year").get<double>());
            item.latitude = field(object, "latitude").get<double>();
            item.longitude = field(object, "longitude").get<double>();
            const picojson::array& bbox = field(object, "bbox").get<picojson::array>();
            for (const picojson::value& coordinate : bbox)
                item.bbox.push_back(coordinate.get<double>());
            require(item.bbox.size() == 4 && item.longitude >= item.bbox[0] &&
                    item.latitude >= item.bbox[1] && item.longitude <= item.bbox[2] &&
                    item.latitude <= item.bbox[3],
                    item.name + " point is outside its trusted tile bbox");
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

    LiveMeasurement parseLiveDebug(const DebugCapture& capture)
    {
        LiveMeasurement measurement;
        const std::regex rangeExpression("Downloading ([0-9]+)-([0-9]+)([^ ]*) ");
        const std::regex responseExpression("response_code=([0-9]+)");
        const std::regex sizeExpression("GetFileSize\\([^)]*\\)=([0-9]+)");
        for (const std::string& message : capture.messages)
        {
            std::smatch match;
            if (std::regex_search(message, match, rangeExpression))
            {
                require(match[3].str().find(',') == std::string::npos,
                        "live GDAL request emitted comma-separated multi-range syntax");
                const std::uint64_t start = std::stoull(match[1].str());
                const std::uint64_t end = std::stoull(match[2].str());
                require(end >= start, "live GDAL range is reversed");
                measurement.requestedBytes += end - start + 1;
            }
            if (std::regex_search(message, match, responseExpression))
                measurement.responseCodes.push_back(std::stoi(match[1].str()));
            if (std::regex_search(message, match, sizeExpression))
                measurement.sourceSize = std::stoull(match[1].str());
        }
        require(measurement.requestedBytes > 0, "live query emitted no bounded ranges");
        require(measurement.sourceSize > 0, "live query did not record source size");
        require(measurement.requestedBytes < measurement.sourceSize,
                "live query requested the complete COG");
        require(std::find(measurement.responseCodes.begin(), measurement.responseCodes.end(),
                          206) != measurement.responseCodes.end(),
                "live query recorded no HTTP 206 response");
        const std::set<int> acceptedResponses = {200, 206, 429, 500, 502, 503, 504};
        for (int response : measurement.responseCodes)
            require(acceptedResponses.count(response) != 0,
                    "live query recorded an unexpected HTTP response code");
        return measurement;
    }

    LiveMeasurement runLiveIteration(const LiveCase& item)
    {
        VSICurlClearCache();
        DebugCapture capture;
        CPLPushErrorHandlerEx(captureGdalMessage, &capture);
        CPLSetCurrentErrorHandlerCatchDebug(1);
        CPLSetConfigOption("CPL_DEBUG", "VSICURL");
        const auto started = std::chrono::steady_clock::now();
        const std::string vsiUrl = "/vsicurl/" + item.url;
        GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpenEx(
            vsiUrl.c_str(), GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
        require(dataset != nullptr, item.name + " COG open failed");
        require(dataset->GetRasterXSize() == 8192 && dataset->GetRasterYSize() == 8192 &&
                dataset->GetRasterCount() == 64,
                item.name + " is not the expected 8192x8192x64 tile");
        const int bandMap[] = {2, 17, 10};
        const std::string bandNames[] = {"A01", "A16", "A09"};
        for (int index = 0; index < 3; ++index)
        {
            GDALRasterBand* band = dataset->GetRasterBand(bandMap[index]);
            require(band && band->GetRasterDataType() == GDT_Int8 &&
                    std::string(band->GetDescription()) == bandNames[index],
                    item.name + " RGB band metadata changed");
        }
        std::vector<std::int8_t> rgb(64 * 64 * 3);
        require(dataset->RasterIO(GF_Read, 3968, 3968, 256, 256, rgb.data(),
                                  64, 64, GDT_Int8, 3, bandMap,
                                  3, 64 * 3, 1, nullptr) == CE_None,
                item.name + " bounded RGB window read failed");
        require(std::any_of(rgb.begin(), rgb.end(),
                            [](std::int8_t value) { return value != 0; }),
                item.name + " RGB window is empty after HTTP retries");
        GDALClose(dataset);
        const auto finished = std::chrono::steady_clock::now();
        CPLPopErrorHandler();
        CPLSetConfigOption("CPL_DEBUG", nullptr);

        LiveMeasurement measurement = parseLiveDebug(capture);
        measurement.milliseconds =
            std::chrono::duration<double, std::milli>(finished - started).count();
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

    int runLive(const std::filesystem::path& casesPath, int iterations)
    {
        require(iterations == 5, "Task 5 live evidence requires exactly five iterations");
        configureRangeAccess();
        const std::vector<LiveCase> cases = loadLiveCases(casesPath);
        for (const LiveCase& item : cases)
        {
            std::vector<double> timings;
            std::uint64_t totalBytes = 0;
            std::uint64_t sourceSize = 0;
            std::set<int> responses;
            for (int iteration = 1; iteration <= iterations; ++iteration)
            {
                LiveMeasurement measurement = runLiveIteration(item);
                timings.push_back(measurement.milliseconds);
                totalBytes += measurement.requestedBytes;
                sourceSize = measurement.sourceSize;
                responses.insert(measurement.responseCodes.begin(),
                                 measurement.responseCodes.end());
                std::cout << "ScienceGdalLive case=" << item.name
                          << " fid=" << item.datasetId << " year=" << item.year
                          << " iteration=" << iteration
                          << " milliseconds=" << measurement.milliseconds
                          << " requested_bytes=" << measurement.requestedBytes
                          << " source_size=" << measurement.sourceSize << std::endl;
            }
            std::cout << "ScienceGdalLive summary case=" << item.name
                      << " fid=" << item.datasetId << " year=" << item.year
                      << " median_ms=" << percentile(timings, 0.5)
                      << " p95_ms=" << percentile(timings, 0.95)
                      << " total_requested_bytes=" << totalBytes
                      << " source_size=" << sourceSize << " response_codes=";
            for (int response : responses) std::cout << response << ',';
            std::cout << " complete_cog=false" << std::endl;
        }
        return 0;
    }
}

int main(int argc, char** argv)
{
    registerScienceRuntime();
    if (argc == 5 && std::string(argv[1]) == "--live-cases" &&
        std::string(argv[3]) == "--iterations")
        return runLive(argv[2], std::stoi(argv[4]));
    require(argc == 1, "usage: no arguments, or --live-cases FILE --iterations 5");
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("osgsol-science-http-range-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    const std::filesystem::path fixture = root / "alphaearth-range-fixture.tif";
    const std::filesystem::path ready = root / "ready.txt";
    const std::filesystem::path log = root / "requests.jsonl";
    createFixture(fixture);
    const std::uint64_t sourceSize = std::filesystem::file_size(fixture);

    ServerProcess server = startServer(fixture, ready, log);
    const int port = waitForPort(ready);
    configureRangeAccess();
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
    server.stop();
    verifyLog(log, sourceSize);
    std::filesystem::remove_all(root);
    return 0;
}
