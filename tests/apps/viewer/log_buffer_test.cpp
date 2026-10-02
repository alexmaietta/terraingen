#include "log_buffer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <string>

using terraingen::LogLevel;
using terraingen::LogRecord;
using terraingen::viewer::FormatLogLine;
using terraingen::viewer::LogBuffer;
using terraingen::viewer::LogRecordMatches;

namespace {

LogRecord Record(LogLevel level, std::string category, std::string message,
                 std::chrono::system_clock::time_point time = {}) {
    return {level, std::move(category), std::move(message), time};
}

}  // namespace

TEST(LogBuffer, KeepsTheNewestRecordsInOrder) {
    LogBuffer buffer(3);
    const auto first = buffer.Version();
    for (int i = 0; i < 5; ++i) {
        buffer.Add(Record(LogLevel::Info, "craters", std::to_string(i)));
    }
    EXPECT_EQ(buffer.Version(), first + 5);
    const auto records = buffer.Snapshot();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].message, "2");
    EXPECT_EQ(records[2].message, "4");
    buffer.Clear();
    EXPECT_TRUE(buffer.Snapshot().empty());
    EXPECT_EQ(buffer.Version(), first + 6);
    LogBuffer tiny(0);
    tiny.Add(Record(LogLevel::Info, "a", "kept"));
    EXPECT_EQ(tiny.Snapshot().size(), 1u);
}

TEST(LogBuffer, SinkStoresEveryRecordAndEchoesWarnings) {
    LogBuffer buffer;
    const terraingen::LogSink sink = buffer.Sink();
    ::testing::internal::CaptureStderr();
    sink(Record(LogLevel::Info, "viewer", "quiet"));
    sink(Record(LogLevel::Warning, "viewer", "loud"));
    EXPECT_EQ(::testing::internal::GetCapturedStderr(), "[terraingen] warning viewer: loud\n");
    EXPECT_EQ(buffer.Snapshot().size(), 2u);
}

TEST(FormatLogLine, ShowsSecondsLevelCategoryAndMessage) {
    const auto start = std::chrono::system_clock::time_point{} + std::chrono::seconds(100);
    const LogRecord record = Record(LogLevel::Info, "craters", "Generated 12 craters",
                                    start + std::chrono::milliseconds(12345));
    EXPECT_EQ(FormatLogLine(record, start), "   12.345 info    craters: Generated 12 craters");
}

TEST(LogRecordMatches, FiltersByLevelAndCaseInsensitiveText) {
    const LogRecord record = Record(LogLevel::Info, "craters", "Applied 5 craters");
    EXPECT_TRUE(LogRecordMatches(record, LogLevel::Info, ""));
    EXPECT_FALSE(LogRecordMatches(record, LogLevel::Warning, ""));
    EXPECT_TRUE(LogRecordMatches(record, LogLevel::Debug, "CRATERS"));
    EXPECT_TRUE(LogRecordMatches(record, LogLevel::Debug, "applied"));
    EXPECT_FALSE(LogRecordMatches(record, LogLevel::Debug, "surfaces"));
}
