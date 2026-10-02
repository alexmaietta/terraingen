#pragma once

#include "terraingen/core/log.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace terraingen::viewer {

/// The most recent log records, shared between the threads that log and the Log panel.
class LogBuffer {
  public:
    /// @param capacity Records kept; older records are discarded first.
    explicit LogBuffer(std::size_t capacity = 5000);

    /// Stores `record`, discarding the oldest record when full. Safe to call from any thread.
    void Add(const LogRecord& record);

    /// A sink that stores records here and also writes warnings and errors to standard error.
    /// The buffer must outlive the sink's installation.
    LogSink Sink();

    void Clear();

    /// Increases whenever records are added or cleared, so readers can skip unchanged buffers.
    std::uint64_t Version() const;

    std::vector<LogRecord> Snapshot() const;

  private:
    mutable std::mutex mutex_;
    std::size_t capacity_;
    std::deque<LogRecord> records_;
    std::uint64_t version_ = 0;
};

/// One line for the Log panel and clipboard: seconds since `start`, level, category and message,
/// such as "   12.345 info    craters: Generated 5392 craters".
std::string FormatLogLine(const LogRecord& record, std::chrono::system_clock::time_point start);

/// Whether `record` is at least `minLevel` and, unless `filter` is empty, contains `filter` in its
/// category or message (case-insensitive).
bool LogRecordMatches(const LogRecord& record, LogLevel minLevel, std::string_view filter);

}  // namespace terraingen::viewer
