// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#include "terraingen/util/log.h"

#include <algorithm>
#include <string_view>
#include <vector>

namespace terraingen::testing {

/// Captures log records at or above `level` for its lifetime, then restores the default sink and
/// the `Warning` threshold.
class LogCapture {
  public:
    explicit LogCapture(LogLevel level = LogLevel::Info) {
        SetLogSink([this](const LogRecord& record) { m_records.push_back(record); });
        SetLogLevel(level);
    }

    ~LogCapture() {
        SetLogSink(nullptr);
        SetLogLevel(LogLevel::Warning);
    }

    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    const std::vector<LogRecord>& Records() const {
        return m_records;
    }

    /// Records in `category` at exactly `level`.
    std::vector<LogRecord> Matching(std::string_view category, LogLevel level) const {
        std::vector<LogRecord> matching;
        std::copy_if(m_records.begin(), m_records.end(), std::back_inserter(matching),
                     [&](const LogRecord& record) {
                         return record.category == category && record.level == level;
                     });
        return matching;
    }

  private:
    std::vector<LogRecord> m_records;
};

}  // namespace terraingen::testing
