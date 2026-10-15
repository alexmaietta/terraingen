/// @file
/// @brief Tests of the JSON paths and value checks in pipeline/config_checks.h.
#include "terraingen/pipeline/config_checks.h"

#include <gtest/gtest.h>

#include <limits>
#include <string>

namespace config = terraingen::config;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

template <typename Check> std::string ErrorOf(Check check) {
    try {
        check();
    } catch (const terraingen::ConfigError& error) {
        return error.what();
    }
    return "";
}

}  // namespace

TEST(ConfigChecks, BuildsJsonPaths) {
    EXPECT_EQ(config::Child("", "surface"), "surface");
    EXPECT_EQ(config::Child("surface", "radius"), "surface.radius");
    EXPECT_EQ(config::Element("layers", 2), "layers[2]");
    EXPECT_EQ(config::Child(config::Element("layers", 0), "hurst"), "layers[0].hurst");
}

TEST(ConfigChecks, PassValidValues) {
    EXPECT_NO_THROW(config::RequirePositive(1.0, "a"));
    EXPECT_NO_THROW(config::RequireNonNegative(0.0, "a"));
    EXPECT_NO_THROW(config::RequireFinite(-1.0, "a"));
}

TEST(ConfigChecks, ReportInvalidValuesAtTheirPath) {
    EXPECT_EQ(ErrorOf([] { config::RequirePositive(0.0, "a.b"); }), "a.b: must be positive");
    EXPECT_EQ(ErrorOf([] { config::RequireNonNegative(-1.0, "c"); }), "c: must not be negative");
    EXPECT_EQ(ErrorOf([] { config::RequireFinite(kNaN, "d[1]"); }), "d[1]: must be finite");
}
