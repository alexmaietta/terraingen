// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/build_info.h"
#include "terraingen/util/version.h"

#include <gtest/gtest.h>

#include <regex>
#include <string>

TEST(Version, MatchesVersionFile) {
    const terraingen::Version version = terraingen::GetVersion();
    const std::string actual = std::to_string(version.major) + "." + std::to_string(version.minor) +
                               "." + std::to_string(version.patch);
    EXPECT_EQ(actual, TERRAINGEN_EXPECTED_VERSION);
}

TEST(Version, ApiVersionMatchesHeaders) {
    EXPECT_EQ(terraingen::GetApiVersion(), TERRAINGEN_API_VERSION);
}

TEST(BuildInfo, IsPopulated) {
    const terraingen::BuildInfo info = terraingen::GetBuildInfo();
    EXPECT_FALSE(info.gitSha.empty());
    EXPECT_TRUE(
        std::regex_match(info.buildDate, std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)")))
        << info.buildDate;
}
