#include "generation_worker.h"

#include <chrono>
#include <exception>

namespace terraingen::viewer {

GenerationWorker::GenerationWorker(GenerateFn generate)
    : generate_(std::move(generate)), thread_([this] { Run(); }) {}

GenerationWorker::~GenerationWorker() {
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    thread_.join();
}

std::uint64_t GenerationWorker::Request(const TerrainSettings& settings) {
    std::uint64_t id = 0;
    {
        const std::lock_guard lock(mutex_);
        id = ++latestId_;
        pending_.emplace(id, settings);
        result_.reset();
    }
    wake_.notify_one();
    return id;
}

std::optional<GenerationResult> GenerationWorker::TakeResult() {
    const std::lock_guard lock(mutex_);
    std::optional<GenerationResult> result = std::move(result_);
    result_.reset();
    return result;
}

bool GenerationWorker::Busy() const {
    const std::lock_guard lock(mutex_);
    return pending_.has_value() || running_;
}

void GenerationWorker::Run() {
    std::unique_lock lock(mutex_);
    while (true) {
        wake_.wait(lock, [this] { return stop_ || pending_.has_value(); });
        if (stop_) {
            return;
        }
        GenerationResult result;
        result.id = pending_->first;
        result.settings = std::move(pending_->second);
        pending_.reset();
        running_ = true;
        lock.unlock();

        const auto start = std::chrono::steady_clock::now();
        try {
            result.raster = generate_(result.settings);
            result.stats = ComputeStats(result.raster);
        } catch (const std::exception& error) {
            result.error = error.what();
        }
        result.milliseconds =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();

        lock.lock();
        running_ = false;
        if (result.id == latestId_) {
            result_ = std::move(result);
        }
    }
}

}  // namespace terraingen::viewer
