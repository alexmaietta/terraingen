#include "terraingen/util/log.h"

#include "terraingen/util/text.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <utility>

namespace terraingen {

namespace {

struct LogState {
    std::mutex mutex;
    LogSink sink = StandardErrorLogSink();
    std::atomic<LogLevel> level{LogLevel::Warning};
};

LogState& State() {
    static LogState state;
    return state;
}

thread_local bool inSink = false;

/// Marks the current thread as inside the sink, so records the sink logs are dropped.
class SinkGuard {
  public:
    SinkGuard() {
        inSink = true;
    }
    ~SinkGuard() {
        inSink = false;
    }
    SinkGuard(const SinkGuard&) = delete;
    SinkGuard& operator=(const SinkGuard&) = delete;
};

}  // namespace

std::string_view LogLevelName(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    case LogLevel::Off:
        return "off";
    }
    return "unknown";
}

std::optional<LogLevel> ParseLogLevel(std::string_view name) {
    const std::string lower = Lower(name);
    if (lower == "warn") {
        return LogLevel::Warning;
    }
    for (const LogLevel level : kLogLevels) {
        if (lower == LogLevelName(level)) {
            return level;
        }
    }
    return std::nullopt;
}

LogSink StandardErrorLogSink() {
    return [](const LogRecord& record) {
        std::cerr << "[terraingen] " << LogLevelName(record.level) << ' ' << record.category << ": "
                  << record.message << '\n';
    };
}

void SetLogSink(LogSink sink) {
    LogState& state = State();
    const std::lock_guard lock(state.mutex);
    state.sink = sink ? std::move(sink) : StandardErrorLogSink();
}

void SetLogLevel(LogLevel level) {
    State().level.store(level, std::memory_order_relaxed);
}

LogLevel GetLogLevel() {
    return State().level.load(std::memory_order_relaxed);
}

bool LogEnabled(LogLevel level) {
    return level != LogLevel::Off && level >= GetLogLevel();
}

void Log(LogLevel level, std::string_view category, std::string message) {
    if (!LogEnabled(level) || inSink) {
        return;
    }
    const LogRecord record{level, std::string(category), std::move(message),
                           std::chrono::system_clock::now()};
    LogState& state = State();
    const std::lock_guard lock(state.mutex);
    const SinkGuard guard;
    state.sink(record);
}

std::string FormatDuration(double milliseconds) {
    std::array<char, 32> buffer{};
    if (milliseconds >= 1000.0) {
        std::snprintf(buffer.data(), buffer.size(), "%.2f s", milliseconds / 1000.0);
    } else if (milliseconds >= 10.0) {
        std::snprintf(buffer.data(), buffer.size(), "%.1f ms", milliseconds);
    } else {
        std::snprintf(buffer.data(), buffer.size(), "%.2f ms", milliseconds);
    }
    return buffer.data();
}

}  // namespace terraingen
