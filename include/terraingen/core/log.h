#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace terraingen {

/// Severity of a log message, in increasing order. `Off` disables logging when used as the
/// threshold.
enum class LogLevel { Trace, Debug, Info, Warning, Error, Off };

/// Lower-case name of `level`: "trace", "debug", "info", "warning", "error" or "off".
std::string_view LogLevelName(LogLevel level);

/// The level named `name` (as `LogLevelName` spells it, case-insensitive, or "warn"), if any.
std::optional<LogLevel> ParseLogLevel(std::string_view name);

/// One log message.
struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string category;  ///< The part of TerrainGen that logged it, such as "craters".
    std::string message;
    std::chrono::system_clock::time_point time;
};

/// Receives log records. TerrainGen calls the sink from whichever thread logged, one record at a
/// time; a sink must not log itself (such messages are dropped).
using LogSink = std::function<void(const LogRecord&)>;

/// The default sink: one line per record on standard error, "[terraingen] warning craters: ...".
LogSink StandardErrorLogSink();

/// Routes every record at or above the threshold to `sink`; an empty sink restores
/// `StandardErrorLogSink`.
void SetLogSink(LogSink sink);

/// Sets the lowest level that is logged. The default, `Warning`, keeps an embedded library quiet;
/// applications usually lower it to `Info`.
void SetLogLevel(LogLevel level);

LogLevel GetLogLevel();

/// Whether messages at `level` are currently logged; check this before building expensive
/// messages.
bool LogEnabled(LogLevel level);

/// Sends `message` to the sink if `level` is enabled.
void Log(LogLevel level, std::string_view category, std::string message);

/// Builds a message from `parts` with `operator<<` and logs it, doing no formatting work when
/// `level` is disabled.
template <typename... Parts>
void Log(LogLevel level, std::string_view category, const Parts&... parts) {
    if (!LogEnabled(level)) {
        return;
    }
    std::ostringstream message;
    (message << ... << parts);
    Log(level, category, message.str());
}

template <typename... Parts> void LogDebug(std::string_view category, const Parts&... parts) {
    Log(LogLevel::Debug, category, parts...);
}

template <typename... Parts> void LogInfo(std::string_view category, const Parts&... parts) {
    Log(LogLevel::Info, category, parts...);
}

template <typename... Parts> void LogWarning(std::string_view category, const Parts&... parts) {
    Log(LogLevel::Warning, category, parts...);
}

/// Measures elapsed wall-clock time from construction, for timing statements in log messages.
class Stopwatch {
  public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}

    double Milliseconds() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
            .count();
    }

  private:
    std::chrono::steady_clock::time_point start_;
};

/// A duration for messages: "0.42 ms", "91.6 ms" or "2.31 s".
std::string FormatDuration(double milliseconds);

}  // namespace terraingen
