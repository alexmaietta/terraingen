#include "log_buffer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace terraingen::viewer {

LogBuffer::LogBuffer(std::size_t capacity) : capacity_(std::max<std::size_t>(capacity, 1)) {}

void LogBuffer::Add(const LogRecord& record) {
    const std::lock_guard lock(mutex_);
    if (records_.size() == capacity_) {
        records_.pop_front();
    }
    records_.push_back(record);
    ++version_;
}

LogSink LogBuffer::Sink() {
    return [this, standardError = StandardErrorLogSink()](const LogRecord& record) {
        Add(record);
        if (record.level >= LogLevel::Warning) {
            standardError(record);
        }
    };
}

void LogBuffer::Clear() {
    const std::lock_guard lock(mutex_);
    records_.clear();
    ++version_;
}

std::uint64_t LogBuffer::Version() const {
    const std::lock_guard lock(mutex_);
    return version_;
}

std::vector<LogRecord> LogBuffer::Snapshot() const {
    const std::lock_guard lock(mutex_);
    return {records_.begin(), records_.end()};
}

std::string FormatLogLine(const LogRecord& record, std::chrono::system_clock::time_point start) {
    const double seconds = std::chrono::duration<double>(record.time - start).count();
    std::array<char, 32> prefix{};
    std::snprintf(prefix.data(), prefix.size(), "%9.3f %-7s ", seconds,
                  std::string(LogLevelName(record.level)).c_str());
    return prefix.data() + record.category + ": " + record.message;
}

bool LogRecordMatches(const LogRecord& record, LogLevel minLevel, std::string_view filter) {
    if (record.level < minLevel) {
        return false;
    }
    if (filter.empty()) {
        return true;
    }
    const auto contains = [filter](std::string_view text) {
        return std::search(text.begin(), text.end(), filter.begin(), filter.end(),
                           [](char a, char b) {
                               return std::tolower(static_cast<unsigned char>(a)) ==
                                      std::tolower(static_cast<unsigned char>(b));
                           }) != text.end();
    };
    return contains(record.category) || contains(record.message);
}

}  // namespace terraingen::viewer
