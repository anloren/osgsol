#include "Era5AgroProvider.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include <cpl_http.h>
#include <cpl_string.h>

namespace earthscience
{
namespace
{
    constexpr std::size_t MAX_RESPONSE_BYTES = 8u * 1024u * 1024u;
    constexpr const char* ENDPOINT =
        "https://archive-api.open-meteo.com/v1/archive?";

    int cancelProgress(double, const char*, void* userData)
    {
        const auto* cancelled = static_cast<const std::function<bool()>*>(
            userData);
        return cancelled && *cancelled && (*cancelled)() ? FALSE : TRUE;
    }

    class ProductionIo : public IEra5AgroIo
    {
    public:
        bool fetch(
            const std::string& url, std::size_t maximumBytes,
            const std::function<bool()>& cancelled,
            std::string& body, std::string& error) override
        {
            body.clear();
            if (cancelled()) { error = "cancelled"; return false; }
            if (url.rfind(ENDPOINT, 0) != 0)
            {
                error = "ERA5 agricultural endpoint is not allowlisted";
                return false;
            }

            char** options = nullptr;
            options = CSLSetNameValue(options, "MAX_RETRY", "0");
            options = CSLSetNameValue(options, "CONNECTTIMEOUT", "5");
            options = CSLSetNameValue(options, "TIMEOUT", "20");
            options = CSLSetNameValue(
                options, "HEADERS", "Accept: application/json");
            CPLHTTPResult* result = CPLHTTPFetchEx(
                url.c_str(), options, cancelProgress,
                const_cast<std::function<bool()>*>(&cancelled),
                nullptr, nullptr);
            CSLDestroy(options);
            if (cancelled())
            {
                if (result) CPLHTTPDestroyResult(result);
                error = "cancelled";
                return false;
            }
            if (!result)
            {
                error = "ERA5 agricultural request returned no result";
                return false;
            }
            const std::unique_ptr<CPLHTTPResult, decltype(&CPLHTTPDestroyResult)>
                owned(result, CPLHTTPDestroyResult);
            if (result->nStatus != 0)
            {
                error = result->pszErrBuf && *result->pszErrBuf
                    ? result->pszErrBuf :
                      "ERA5 agricultural request failed";
                return false;
            }
            if (result->nDataLen < 0 ||
                static_cast<std::size_t>(result->nDataLen) > maximumBytes)
            {
                error = "ERA5 agricultural response exceeds 8 MiB";
                return false;
            }
            if (!result->pabyData || result->nDataLen == 0)
            {
                error = "ERA5 agricultural response body is empty";
                return false;
            }
            if (result->pszContentType &&
                std::string(result->pszContentType).find("json") ==
                    std::string::npos)
            {
                error = "ERA5 agricultural response is not JSON";
                return false;
            }
            body.assign(reinterpret_cast<const char*>(result->pabyData),
                        static_cast<std::size_t>(result->nDataLen));
            error.clear();
            return true;
        }
    };
}

struct Era5AgroProvider::Impl
{
    struct Request
    {
        std::uint64_t generation = 0;
        GeoTemporalQuery query;
        std::chrono::steady_clock::time_point startedAt;
    };

    Impl(Era5AgroProduct selectedProduct, std::unique_ptr<IEra5AgroIo> ownedIo)
        : product(selectedProduct), io(std::move(ownedIo))
    {
        if (!io) io = std::make_unique<ProductionIo>();
        state.state = ScienceJobState::Idle;
        state.message = "Ready on demand";
        worker = std::thread([this]() { run(); });
    }

    ~Impl()
    {
        stop.store(true, std::memory_order_release);
        cancelThrough(generation.load(std::memory_order_acquire));
        condition.notify_all();
        if (worker.joinable()) worker.join();
    }

    bool cancelled(std::uint64_t value) const
    {
        return stop.load(std::memory_order_acquire) ||
            cancelledThrough.load(std::memory_order_acquire) >= value ||
            value != generation.load(std::memory_order_acquire);
    }

    void cancelThrough(std::uint64_t value)
    {
        std::uint64_t current = cancelledThrough.load(std::memory_order_acquire);
        while (current < value &&
               !cancelledThrough.compare_exchange_weak(
                   current, value, std::memory_order_acq_rel,
                   std::memory_order_acquire)) {}
    }

    void publish(
        std::uint64_t value, ScienceJobState next,
        ScienceProgressStage stage, const std::string& message,
        std::shared_ptr<const ScienceArtifact> artifact = nullptr,
        double elapsedSeconds = 0.0)
    {
        if (next != ScienceJobState::Cancelled && cancelled(value)) return;
        std::lock_guard<std::mutex> lock(mutex);
        if (value != generation.load(std::memory_order_acquire) ||
            (next != ScienceJobState::Cancelled && cancelled(value)))
            return;
        state.generation = value;
        state.state = next;
        state.progress.stage = stage;
        state.progress.determinate = next == ScienceJobState::Ready;
        state.progress.completedUnits = next == ScienceJobState::Ready ? 1 : 0;
        state.progress.totalUnits = next == ScienceJobState::Ready ? 1 : 0;
        state.progress.unit = next == ScienceJobState::Ready ? "profile" : "";
        state.progress.elapsedSeconds = elapsedSeconds;
        state.message = message;
        state.artifact = std::move(artifact);
    }

    void finishCancelled(std::uint64_t value)
    {
        publish(value, ScienceJobState::Cancelled,
                ScienceProgressStage::Cancelled, "Cancelled by user");
    }

    void execute(const Request& request)
    {
        const auto isCancelled = [this, value = request.generation]()
        { return cancelled(value); };
        publish(request.generation, ScienceJobState::Fetching,
                ScienceProgressStage::Locating,
                "Preparing source-grid ERA5 annual profile");

        std::string url, error;
        if (!buildEra5AgroRequestUrl(product, request.query, url, error))
        {
            publish(request.generation, ScienceJobState::Failed,
                    ScienceProgressStage::Failed, error);
            return;
        }
        std::string body;
        publish(request.generation, ScienceJobState::Fetching,
                ScienceProgressStage::Reading,
                "Reading daily ERA5 reanalysis values");
        if (!io->fetch(
                url, MAX_RESPONSE_BYTES, isCancelled, body, error))
        {
            if (isCancelled()) finishCancelled(request.generation);
            else publish(request.generation, ScienceJobState::Failed,
                         ScienceProgressStage::Failed, error);
            return;
        }
        if (isCancelled()) { finishCancelled(request.generation); return; }

        publish(request.generation, ScienceJobState::Fetching,
                ScienceProgressStage::Analyzing,
                "Aggregating complete calendar years");
        auto artifact = std::make_shared<ScienceArtifact>();
        if (!parseEra5AgroAnnualArtifact(
                product, request.query, url, body, *artifact, error))
        {
            publish(request.generation, ScienceJobState::Failed,
                    ScienceProgressStage::Failed, error);
            return;
        }
        if (isCancelled()) { finishCancelled(request.generation); return; }
        artifact->generation = request.generation;
        artifact->artifactId = std::string(era5AgroSourceId(product)) + "-" +
            std::to_string(request.generation);
        const double elapsedSeconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - request.startedAt).count();
        publish(request.generation, ScienceJobState::Ready,
                ScienceProgressStage::Ready, "Annual climate profile ready",
                std::move(artifact), elapsedSeconds);
    }

    void run()
    {
        while (!stop.load(std::memory_order_acquire))
        {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this]()
                { return stop.load(std::memory_order_acquire) || pending.has_value(); });
                if (stop.load(std::memory_order_acquire)) break;
                request = *pending;
                pending.reset();
            }
            execute(request);
        }
    }

    Era5AgroProduct product;
    std::unique_ptr<IEra5AgroIo> io;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;
    std::optional<Request> pending;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> cancelledThrough{0};
    std::atomic<std::uint64_t> generation{0};
    ScienceProviderSnapshot state;
};

Era5AgroProvider::Era5AgroProvider(Era5AgroProduct product)
    : _impl(new Impl(product, std::make_unique<ProductionIo>()))
{
}

Era5AgroProvider::Era5AgroProvider(
    Era5AgroProduct product, std::unique_ptr<IEra5AgroIo> io)
    : _impl(new Impl(product, std::move(io)))
{
}

Era5AgroProvider::~Era5AgroProvider() = default;

ScienceSourceDescriptor Era5AgroProvider::descriptor() const
{
    ScienceSourceDescriptor result = describeEra5Agro(_impl->product);
    const ScienceProviderSnapshot current = snapshot();
    if (current.state == ScienceJobState::Queued ||
        current.state == ScienceJobState::Fetching)
    {
        result.health = ScienceSourceHealth::Busy;
        result.healthMessage = current.message;
    }
    else if (current.state == ScienceJobState::Failed)
    {
        result.health = ScienceSourceHealth::Degraded;
        result.healthMessage = current.message;
    }
    return result;
}

bool Era5AgroProvider::validateQuery(
    const GeoTemporalQuery& query, std::string& error) const
{
    std::string ignoredUrl;
    return buildEra5AgroRequestUrl(_impl->product, query, ignoredUrl, error);
}

std::uint64_t Era5AgroProvider::submit(const GeoTemporalQuery& query)
{
    std::string error;
    if (!validateQuery(query, error)) return 0;
    std::lock_guard<std::mutex> lock(_impl->mutex);
    const std::uint64_t value =
        _impl->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    _impl->cancelThrough(value - 1);
    _impl->pending = Impl::Request{
        value, query, std::chrono::steady_clock::now()};
    _impl->state = ScienceProviderSnapshot();
    _impl->state.generation = value;
    _impl->state.state = ScienceJobState::Queued;
    _impl->state.progress.stage = ScienceProgressStage::Queued;
    _impl->state.message = "Queued ERA5 annual profile";
    _impl->condition.notify_one();
    return value;
}

ScienceProviderSnapshot Era5AgroProvider::snapshot() const
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->state;
}

void Era5AgroProvider::cancel(std::uint64_t generation)
{
    _impl->cancelThrough(generation);
    _impl->finishCancelled(generation);
    _impl->condition.notify_all();
}

void Era5AgroProvider::clear()
{
    std::lock_guard<std::mutex> lock(_impl->mutex);
    const std::uint64_t staleGeneration =
        _impl->generation.fetch_add(1, std::memory_order_acq_rel);
    _impl->cancelThrough(staleGeneration);
    _impl->pending.reset();
    _impl->state = ScienceProviderSnapshot();
    _impl->state.state = ScienceJobState::Idle;
    _impl->state.message = "Ready on demand";
}
}
