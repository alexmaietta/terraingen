// SPDX-License-Identifier: BSD-3-Clause

#include "config/common.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace config = terraingen::config;

namespace {

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
    EXPECT_EQ(terraingen::FieldPath("", "surface"), "surface");
    EXPECT_EQ(terraingen::FieldPath("surface", "radius"), "surface.radius");
    EXPECT_EQ(terraingen::ItemPath("layers", 2), "layers[2]");
    EXPECT_EQ(terraingen::FieldPath(terraingen::ItemPath("layers", 0), "hurst"), "layers[0].hurst");
}

TEST(ConfigChecks, PassValidValues) {
    EXPECT_NO_THROW(config::CheckNonEmpty("x.tif", "e.path"));
}

TEST(ConfigChecks, ReportInvalidValuesAtTheirPath) {
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

TEST(ConfigChecks, ReportFieldRangeErrorsBelowTheirPath) {
    EXPECT_EQ(ErrorOf([] {
                  config::At("layers[0]", [] {
                      throw terraingen::FieldRangeError("ripples.wavelength_m", "must be positive");
                  });
              }),
              "layers[0].ripples.wavelength_m: must be positive");
}
