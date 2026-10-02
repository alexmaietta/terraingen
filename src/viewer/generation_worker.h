#pragma once

#include "terrain_settings.h"
#include "terraingen/core/raster.h"
#include "terraingen/core/raster_stats.h"

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace terraingen::viewer {

/// Outcome of one generation request.
struct GenerationResult {
    std::uint64_t id = 0;
    TerrainSettings settings;
    Raster<float> raster;
    RasterStats stats;
    double milliseconds = 0.0;
    std::string error;  ///< Empty on success.
};

/// Generates terrain on a background thread so the UI stays responsive.
///
/// The latest request wins: a new request replaces any request still waiting, and results of
/// requests that were superseded while running are discarded.
class GenerationWorker {
  public:
    using GenerateFn = std::function<Raster<float>(const TerrainSettings&)>;

    explicit GenerationWorker(GenerateFn generate = GenerateTerrain);

    /// Waits for any generation in progress to finish, then stops the thread.
    ~GenerationWorker();

    GenerationWorker(const GenerationWorker&) = delete;
    GenerationWorker& operator=(const GenerationWorker&) = delete;

    /// Queues `settings`, replacing any request not yet started, and returns the request's id.
    std::uint64_t Request(const TerrainSettings& settings);

    /// Returns the result of the latest request once it is ready, exactly once.
    std::optional<GenerationResult> TakeResult();

    /// True while a request is waiting or running.
    bool Busy() const;

  private:
    void Run();

    GenerateFn generate_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<std::pair<std::uint64_t, TerrainSettings>> pending_;
    std::optional<GenerationResult> result_;
    std::uint64_t latestId_ = 0;
    bool running_ = false;
    bool stop_ = false;
    std::thread thread_;
};

}  // namespace terraingen::viewer
