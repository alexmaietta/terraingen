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

/// @brief Severity of a log message, in increasing order; `Off` as the threshold disables logging.
enum class LogLevel { Trace, Debug, Info, Warning, Error, Off };

/// @brief The lower-case name of a level.
/// @param level The level.
/// @return "trace", "debug", "info", "warning", "error" or "off".
std::string_view LogLevelName(LogLevel level);

/// @brief Parses a level name.
/// @param name A name as `LogLevelName` spells it, case-insensitive, or "warn".
/// @return The level, or nothing if `name` names none.
std::optional<LogLevel> ParseLogLevel(std::string_view name);

/// @brief One log message.
struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::string category;  ///< The part of TerrainGen that logged it, such as "craters".
    std::string message;
    std::chrono::system_clock::time_point time;  ///< When it was logged.
};

/// @brief Receives log records, one at a time from whichever thread logged; a sink must not log
/// itself (such messages are dropped).
using LogSink = std::function<void(const LogRecord&)>;

/// @brief The default sink: one line per record on standard error, "[terraingen] warning
/// craters: ...".
/// @return The sink.
LogSink StandardErrorLogSink();

/// @brief Routes every record at or above the threshold to a sink.
/// @param sink The sink; an empty one restores `StandardErrorLogSink`.
void SetLogSink(LogSink sink);

/// @brief Sets the lowest level that is logged; the default is `Warning`.
/// @param level The threshold.
void SetLogLevel(LogLevel level);

/// @brief The lowest level that is logged.
/// @return The threshold.
LogLevel GetLogLevel();

/// @brief Whether messages at a level are logged; check before building expensive messages.
/// @param level The level.
/// @return True if `level` is not `Off` and at or above the threshold.
bool LogEnabled(LogLevel level);

/// @brief Sends a message to the sink if its level is enabled.
/// @param level The message's level.
/// @param category The part of TerrainGen logging it.
/// @param message The text.
void Log(LogLevel level, std::string_view category, std::string message);

/// @brief Builds a message from parts with `operator<<` and logs it, doing no formatting work when
/// the level is disabled.
/// @param level The message's level.
/// @param category The part of TerrainGen logging it.
/// @param parts The values streamed into the message, in order.
template <typename... Parts>
void Log(LogLevel level, std::string_view category, const Parts&... parts) {
    if (!LogEnabled(level)) {
        return;
    }
    std::ostringstream message;
    (message << ... << parts);
    Log(level, category, message.str());
}

/// @brief Logs a message at `Debug` level.
/// @param category The part of TerrainGen logging it.
/// @param parts The values streamed into the message, in order.
template <typename... Parts> void LogDebug(std::string_view category, const Parts&... parts) {
    Log(LogLevel::Debug, category, parts...);
}

/// @brief Logs a message at `Info` level.
/// @param category The part of TerrainGen logging it.
/// @param parts The values streamed into the message, in order.
template <typename... Parts> void LogInfo(std::string_view category, const Parts&... parts) {
    Log(LogLevel::Info, category, parts...);
}

/// @brief Logs a message at `Warning` level.
/// @param category The part of TerrainGen logging it.
/// @param parts The values streamed into the message, in order.
template <typename... Parts> void LogWarning(std::string_view category, const Parts&... parts) {
    Log(LogLevel::Warning, category, parts...);
}

/// @brief Measures elapsed wall-clock time from construction, for timing log messages.
class Stopwatch {
  public:
    /// @brief Starts timing.
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}

    /// @brief The time since construction.
    /// @return Elapsed milliseconds.
    double Milliseconds() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
            .count();
    }

  private:
    std::chrono::steady_clock::time_point start_;
};

/// @brief Formats a duration for messages: "0.42 ms", "91.6 ms" or "2.31 s".
/// @param milliseconds The duration in milliseconds.
/// @return The formatted duration.
std::string FormatDuration(double milliseconds);

}  // namespace terraingen
