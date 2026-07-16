#include "AlphaEarthEmbeddingRuntime.h"

#include "AlphaEarthEmbeddingReader.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace earthscience
{
struct AlphaEarthEmbeddingRuntime::Impl
{
    struct Request
    {
        std::uint64_t generation = 0;
        GeoTemporalQuery query;
    };

    explicit Impl(AlphaEarthAssetResolver value)
        : resolver(std::move(value))
    {
        state.state = resolver ? ScienceJobState::Idle
                               : ScienceJobState::Unavailable;
        state.progress.stage = resolver ? ScienceProgressStage::Idle
                                        : ScienceProgressStage::Failed;
        state.message = resolver ? "Ready"
                                 : "AlphaEarth asset resolver is missing";
        worker = std::thread([this]() { run(); });
    }

    ~Impl()
    {
        stop.store(true, std::memory_order_release);
        cancelledThrough.store(
            activeGeneration.load(std::memory_order_acquire),
            std::memory_order_release);
        condition.notify_all();
        if (worker.joinable()) worker.join();
    }

    bool isCancelled(std::uint64_t generation) const
    {
        return stop.load(std::memory_order_acquire) ||
               cancelledThrough.load(std::memory_order_acquire) >= generation ||
               activeGeneration.load(std::memory_order_acquire) != generation;
    }

    void publishProgress(std::uint64_t generation,
                         const ScienceProgress& progress,
                         const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (isCancelled(generation) || state.generation != generation ||
            state.state == ScienceJobState::Cancelled)
            return;
        state.state = ScienceJobState::Fetching;
        state.progress = progress;
        state.message = message;
    }

    void run()
    {
        for (;;)
        {
            Request request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this]()
                {
                    return stop.load(std::memory_order_acquire) ||
                           pending.has_value();
                });
                if (stop.load(std::memory_order_acquire)) return;
                request = std::move(*pending);
                pending.reset();
                if (request.generation !=
                    activeGeneration.load(std::memory_order_acquire))
                    continue;
                state.state = ScienceJobState::Fetching;
                state.progress = ScienceProgress();
                state.progress.stage = ScienceProgressStage::Locating;
                state.message = "Locating AlphaEarth source metadata";
                state.artifact.reset();
            }

            alphaearthdetail::AlphaEarthReadCallbacks callbacks;
            callbacks.cancelled = [this, generation = request.generation]()
            {
                return isCancelled(generation);
            };
            callbacks.progress =
                [this, generation = request.generation](
                    const ScienceProgress& progress,
                    const std::string& message)
                {
                    publishProgress(generation, progress, message);
                };
            std::shared_ptr<const ScienceArtifact> artifact;
            std::string error;
            const bool succeeded = alphaearthdetail::readAlphaEarthArtifact(
                request.query, resolver, request.generation, callbacks,
                artifact, error);

            std::lock_guard<std::mutex> lock(mutex);
            if (request.generation !=
                activeGeneration.load(std::memory_order_acquire) ||
                state.generation != request.generation)
                continue;
            if (isCancelled(request.generation))
            {
                state.state = ScienceJobState::Cancelled;
                state.progress = ScienceProgress();
                state.progress.stage = ScienceProgressStage::Cancelled;
                state.message = "Cancelled";
                state.artifact.reset();
                continue;
            }
            if (!succeeded || !artifact)
            {
                state.state = ScienceJobState::Failed;
                state.progress = ScienceProgress();
                state.progress.stage = ScienceProgressStage::Failed;
                state.message = error.empty()
                    ? "AlphaEarth embedding read failed" : error;
                state.artifact.reset();
                continue;
            }
            state.state = ScienceJobState::Ready;
            state.progress = ScienceProgress();
            state.progress.stage = ScienceProgressStage::Ready;
            state.progress.completedUnits = 1;
            state.progress.totalUnits = 1;
            state.progress.determinate = true;
            state.progress.unit = "artifact";
            state.message = "Ready";
            state.artifact = std::move(artifact);
        }
    }

    AlphaEarthAssetResolver resolver;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::optional<Request> pending;
    ScienceProviderSnapshot state;
    std::thread worker;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> activeGeneration{0};
    std::atomic<std::uint64_t> cancelledThrough{0};
};

AlphaEarthEmbeddingRuntime::AlphaEarthEmbeddingRuntime(
    const std::string& indexPath)
    : AlphaEarthEmbeddingRuntime(
        [indexPath](double latitude, double longitude, int year,
                    AlphaEarthAsset& asset, std::string& error)
        {
            return alphaearthdetail::resolveAlphaEarthAssetFromIndex(
                indexPath, latitude, longitude, year, asset, error);
        })
{
}

AlphaEarthEmbeddingRuntime::AlphaEarthEmbeddingRuntime(
    AlphaEarthAssetResolver resolver)
    : _impl(std::make_unique<Impl>(std::move(resolver)))
{
}

AlphaEarthEmbeddingRuntime::~AlphaEarthEmbeddingRuntime() = default;

std::uint64_t AlphaEarthEmbeddingRuntime::submit(
    const GeoTemporalQuery& query)
{
    if (!_impl) return 0;
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->resolver)
    {
        _impl->state.state = ScienceJobState::Unavailable;
        _impl->state.progress.stage = ScienceProgressStage::Failed;
        _impl->state.message = "AlphaEarth asset resolver is missing";
        return 0;
    }
    const std::uint64_t previous =
        _impl->activeGeneration.load(std::memory_order_acquire);
    const std::uint64_t generation = previous + 1;
    if (previous != 0)
        _impl->cancelledThrough.store(previous, std::memory_order_release);
    _impl->activeGeneration.store(generation, std::memory_order_release);
    _impl->pending = Impl::Request{generation, query};
    _impl->state = ScienceProviderSnapshot();
    _impl->state.generation = generation;
    _impl->state.state = ScienceJobState::Queued;
    _impl->state.progress.stage = ScienceProgressStage::Queued;
    _impl->state.message = "Queued";
    _impl->condition.notify_one();
    return generation;
}

void AlphaEarthEmbeddingRuntime::cancel(std::uint64_t generation)
{
    if (!_impl || generation == 0) return;
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (_impl->state.generation != generation ||
        _impl->activeGeneration.load(std::memory_order_acquire) != generation)
        return;
    _impl->cancelledThrough.store(generation, std::memory_order_release);
    if (_impl->pending && _impl->pending->generation == generation)
        _impl->pending.reset();
    _impl->state.state = ScienceJobState::Cancelled;
    _impl->state.progress = ScienceProgress();
    _impl->state.progress.stage = ScienceProgressStage::Cancelled;
    _impl->state.message = "Cancelled";
    _impl->state.artifact.reset();
    _impl->condition.notify_all();
}

ScienceProviderSnapshot AlphaEarthEmbeddingRuntime::snapshot() const
{
    if (!_impl) return ScienceProviderSnapshot();
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->state;
}
}
