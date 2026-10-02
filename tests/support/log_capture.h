#pragma once

#include "terraingen/core/log.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace terraingen::testing {

/// Captures log records at or above `level` for its lifetime, then restores the default sink and
/// the `Warning` threshold.
class LogCapture {
  public:
    explicit LogCapture(LogLevel level = LogLevel::Info) {
        SetLogSink([this](const LogRecord& record) { records_.push_back(record); });
        SetLogLevel(level);
    }

    ~LogCapture() {
        SetLogSink(nullptr);
        SetLogLevel(LogLevel::Warning);
    }

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    const std::vector<LogRecord>& Records() const {
        return records_;
    }

    /// Records in `category` at exactly `level`.
    std::vector<LogRecord> Matching(std::string_view category, LogLevel level) const {
        std::vector<LogRecord> matching;
        std::copy_if(records_.begin(), records_.end(), std::back_inserter(matching),
                     [&](const LogRecord& record) {
                         return record.category == category && record.level == level;
                     });
        return matching;
    }

  private:
    std::vector<LogRecord> records_;
};

}  // namespace terraingen::testing
