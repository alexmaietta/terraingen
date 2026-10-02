#include "terraingen/core/log.h"

#include <gtest/gtest.h>

#include <ostream>
#include <string>
#include <thread>
#include <vector>

using terraingen::FormatDuration;
using terraingen::GetLogLevel;
using terraingen::Log;
using terraingen::LogDebug;
using terraingen::LogEnabled;
using terraingen::LogInfo;
using terraingen::LogLevel;
using terraingen::LogLevelName;
using terraingen::LogRecord;
using terraingen::LogWarning;
using terraingen::ParseLogLevel;
using terraingen::SetLogLevel;
using terraingen::SetLogSink;
using terraingen::Stopwatch;

namespace {

class LogTest : public ::testing::Test {
  protected:
    void SetUp() override {
        SetLogSink([this](const LogRecord& record) { records.push_back(record); });
        SetLogLevel(LogLevel::Warning);
    }

    void TearDown() override {
        SetLogSink(nullptr);
        SetLogLevel(LogLevel::Warning);
    }

    std::vector<LogRecord> records;
};

struct CountedFormatting {
    int* count;
};

std::ostream& operator<<(std::ostream& stream, const CountedFormatting& value) {
    ++*value.count;
    return stream << "counted";
}

}  // namespace

TEST(LogLevel, NamesAndParsesEveryLevel) {
    for (const LogLevel level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info,
                                 LogLevel::Warning, LogLevel::Error, LogLevel::Off}) {
        EXPECT_EQ(ParseLogLevel(LogLevelName(level)), level);
    }
    EXPECT_EQ(ParseLogLevel("DEBUG"), LogLevel::Debug);
    EXPECT_EQ(ParseLogLevel("warn"), LogLevel::Warning);
    EXPECT_EQ(ParseLogLevel("verbose"), std::nullopt);
    EXPECT_EQ(LogLevelName(static_cast<LogLevel>(42)), "unknown");
}

TEST_F(LogTest, DefaultsToWarningsAndAbove) {
    EXPECT_EQ(GetLogLevel(), LogLevel::Warning);
    EXPECT_FALSE(LogEnabled(LogLevel::Info));
    EXPECT_TRUE(LogEnabled(LogLevel::Error));
    EXPECT_FALSE(LogEnabled(LogLevel::Off));
    LogInfo("craters", "dropped");
    LogWarning("craters", "kept ", 3, " of ", 4.5);
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].level, LogLevel::Warning);
    EXPECT_EQ(records[0].category, "craters");
    EXPECT_EQ(records[0].message, "kept 3 of 4.5");
    EXPECT_LE(records[0].time, std::chrono::system_clock::now());
}

TEST_F(LogTest, FormatsMessagesOnlyWhenEnabled) {
    int formatted = 0;
    LogDebug("surfaces", CountedFormatting{&formatted});
    EXPECT_EQ(formatted, 0);
    SetLogLevel(LogLevel::Debug);
    LogDebug("surfaces", CountedFormatting{&formatted});
    EXPECT_EQ(formatted, 1);
    Log(LogLevel::Error, "surfaces", std::string("plain"));
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[1].message, "plain");
    SetLogLevel(LogLevel::Off);
    Log(LogLevel::Error, "surfaces", std::string("silenced"));
    EXPECT_EQ(records.size(), 2u);
}

TEST_F(LogTest, DropsRecordsLoggedFromInsideTheSink) {
    SetLogSink([this](const LogRecord& record) {
        records.push_back(record);
        LogWarning("sink", "nested");
    });
    LogWarning("craters", "outer");
    ASSERT_EQ(records.size(), 1u);
    EXPECT_EQ(records[0].message, "outer");
    LogWarning("craters", "again");
    EXPECT_EQ(records.size(), 2u);
}

TEST_F(LogTest, SerializesRecordsFromManyThreads) {
    std::size_t delivered = 0;
    SetLogSink([&delivered](const LogRecord&) { ++delivered; });
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([] {
            for (int i = 0; i < 1000; ++i) {
                LogWarning("threads", i);
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    EXPECT_EQ(delivered, 8000u);
}

TEST_F(LogTest, EmptySinkRestoresStandardError) {
    SetLogSink(nullptr);
    ::testing::internal::CaptureStderr();
    LogWarning("craters", "to stderr");
    EXPECT_EQ(::testing::internal::GetCapturedStderr(),
              "[terraingen] warning craters: to stderr\n");
}

TEST(FormatDuration, ChoosesUnitsAndPrecision) {
    EXPECT_EQ(FormatDuration(0.421), "0.42 ms");
    EXPECT_EQ(FormatDuration(91.62), "91.6 ms");
    EXPECT_EQ(FormatDuration(2312.0), "2.31 s");
}

TEST(Stopwatch, MeasuresElapsedTime) {
    const Stopwatch stopwatch;
    const double first = stopwatch.Milliseconds();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    EXPECT_GE(first, 0.0);
    EXPECT_GE(stopwatch.Milliseconds(), first + 1.0);
}
