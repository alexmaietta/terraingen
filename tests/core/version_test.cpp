#include "terraingen/build_info.h"
#include "terraingen/version.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

TEST_CASE("Library version matches the VERSION file", "[unit]") {
    const terraingen::Version version = terraingen::GetVersion();
    const std::string actual = std::to_string(version.major) + "." + std::to_string(version.minor) +
                               "." + std::to_string(version.patch);
    CHECK(actual == TERRAINGEN_EXPECTED_VERSION);
}

TEST_CASE("Library API version matches the headers", "[unit]") {
    CHECK(terraingen::GetApiVersion() == TERRAINGEN_API_VERSION);
}

TEST_CASE("Build info is populated", "[unit]") {
    const terraingen::BuildInfo info = terraingen::GetBuildInfo();
    CHECK_FALSE(info.gitSha.empty());
    CHECK_THAT(info.buildDate, Catch::Matchers::Matches(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)"));
}
