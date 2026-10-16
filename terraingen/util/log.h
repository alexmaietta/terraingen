/// @file
/// @brief Levelled logging to a replaceable sink, and timing helpers for log messages.
#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace terraingen {

/// Severity of a log message, in increasing order; `Off` as the threshold disables logging.
enum class LogLevel { Trace, Debug, Info, Warning, Error, Off };

/// The lower-case name of a level: "trace", "debug", "info", "warning", "error" or "off".
std::string_view LogLevelName(LogLevel level);

/// Parses a level name as `LogLevelName` spells it, case-insensitive, or "warn"; nothing if
/// `name` names no level.
std::optional<LogLevel> ParseLogLevel(std::string_view name);

/// One log message.
struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string category;  ///< The part of TerrainGen that logged it, such as "craters".
    std::string message;
    std::chrono::system_clock::time_point time;  ///< When it was logged.
};

/// Receives log records, one at a time from whichever thread logged; a sink must not log
/// itself (such messages are dropped).
using LogSink = std::function<void(const LogRecord&)>;

/// The default sink: one line per record on standard error, "[terraingen] warning
/// craters: ...".
LogSink StandardErrorLogSink();

/// Routes every record at or above the threshold to `sink`; an empty sink restores
/// `StandardErrorLogSink`.
void SetLogSink(LogSink sink);

/// Sets the lowest level that is logged; the default is `Warning`.
void SetLogLevel(LogLevel level);

/// The lowest level that is logged.
LogLevel GetLogLevel();

/// Whether messages at `level` are logged (not `Off` and at or above the threshold); check
/// before building expensive messages.
bool LogEnabled(LogLevel level);

/// Sends a message to the sink if its level is enabled.
/// @param category The part of TerrainGen logging it.
void Log(LogLevel level, std::string_view category, std::string message);

/// Builds a message from `parts` with `operator<<` and logs it, doing no formatting work when
/// the level is disabled.
/// @param category The part of TerrainGen logging it.
template <typename... Parts>
void Log(LogLevel level, std::string_view category, const Parts&... parts) {
    if (!LogEnabled(level)) {
        return;
    }
    std::ostringstream message;
    (message << ... << parts);
    Log(level, category, message.str());
}

/// Logs a message streamed from `parts` at `Debug` level.
template <typename... Parts> void LogDebug(std::string_view category, const Parts&... parts) {
    Log(LogLevel::Debug, category, parts...);
}

/// Logs a message streamed from `parts` at `Info` level.
template <typename... Parts> void LogInfo(std::string_view category, const Parts&... parts) {
    Log(LogLevel::Info, category, parts...);
}

/// Logs a message streamed from `parts` at `Warning` level.
template <typename... Parts> void LogWarning(std::string_view category, const Parts&... parts) {
    Log(LogLevel::Warning, category, parts...);
}

/// Measures elapsed wall-clock time from construction, for timing log messages.
class Stopwatch {
  public:
    /// Starts timing.
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}

    /// Milliseconds since construction.
    double Milliseconds() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
            .count();
    }

  private:
    std::chrono::steady_clock::time_point start_;
};

/// Formats a duration in milliseconds for messages: "0.42 ms", "91.6 ms" or "2.31 s".
std::string FormatDuration(double milliseconds);

}  // namespace terraingen
