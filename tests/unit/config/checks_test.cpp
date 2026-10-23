// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/config/fields/checks.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
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
    EXPECT_NO_THROW(config::CheckFinite(-1.0, "a"));
    EXPECT_NO_THROW(config::CheckNonEmpty("x.tif", "e.path"));
}

TEST(ConfigChecks, ReportInvalidValuesAtTheirPath) {
    EXPECT_EQ(ErrorOf([] { config::CheckFinite(kNaN, "d[1]"); }), "d[1]: must be finite");
    EXPECT_EQ(ErrorOf([] { config::CheckNonEmpty("", "e.path"); }), "e.path: must not be empty");
}

TEST(ConfigChecks, ReportFileErrorsAtTheirPath) {
    EXPECT_EQ(config::AtFile("a", [] { return 3; }), 3);
    EXPECT_EQ(ErrorOf([] { config::AtFile("f.path", [] { throw std::runtime_error("io"); }); }),
              "f.path: io");
    EXPECT_EQ(ErrorOf([] { config::AtFile("g", [] { throw std::invalid_argument("bad"); }); }),
              "g: bad");
    EXPECT_EQ(ErrorOf([] { config::AtFile("h", [] { throw terraingen::ConfigError("x", "y"); }); }),
              "x: y");
}

TEST(ConfigChecks, ReportLibraryErrorsAtTheirPath) {
    EXPECT_EQ(config::At("a", [] { return 3; }), 3);
    EXPECT_EQ(ErrorOf([] { config::At("a", []() -> int { throw std::invalid_argument("bad"); }); }),
              "a: bad");
    EXPECT_EQ(ErrorOf([] { config::At("b", [] { throw std::domain_error("out"); }); }), "b: out");
    EXPECT_THROW(config::At("c", [] { throw std::runtime_error("io"); }), std::runtime_error);
}
