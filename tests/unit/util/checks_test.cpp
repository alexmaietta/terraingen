#include "terraingen/util/checks.h"

#include <gtest/gtest.h>

#include <limits>
#include <stdexcept>
#include <string>

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

template <typename Check> std::string MessageOf(Check check) {
    try {
        check();
    } catch (const std::invalid_argument& error) {
        return error.what();
    }
    return "";
}

}  // namespace

TEST(Checks, PredicatesRejectNonFiniteValues) {
    EXPECT_TRUE(terraingen::IsPositive(1e-300));
    EXPECT_FALSE(terraingen::IsPositive(0.0));
    EXPECT_FALSE(terraingen::IsPositive(kInf));
    EXPECT_FALSE(terraingen::IsPositive(kNaN));
    EXPECT_TRUE(terraingen::IsNonNegative(0.0));
    EXPECT_FALSE(terraingen::IsNonNegative(-1e-300));
    EXPECT_FALSE(terraingen::IsNonNegative(kInf));
    EXPECT_FALSE(terraingen::IsNonNegative(kNaN));
    EXPECT_TRUE(terraingen::IsInUnitInterval(0.0));
    EXPECT_TRUE(terraingen::IsInUnitInterval(1.0));
    EXPECT_FALSE(terraingen::IsInUnitInterval(1.0 + 1e-15));
    EXPECT_FALSE(terraingen::IsInUnitInterval(kNaN));
}

TEST(Checks, RequirementsPassValidValues) {
    EXPECT_NO_THROW(terraingen::RequirePositive(2.0, "x"));
    EXPECT_NO_THROW(terraingen::RequireNonNegative(0.0, "x"));
    EXPECT_NO_THROW(terraingen::RequireFinite(-3.0, "x"));
    EXPECT_NO_THROW(terraingen::RequireUnitInterval(0.5, "x"));
}

TEST(Checks, RequirementsNameTheValueInTheirMessages) {
    EXPECT_EQ(MessageOf([] { terraingen::RequirePositive(0.0, "Pixel size"); }),
              "Pixel size must be positive and finite");
    EXPECT_EQ(MessageOf([] { terraingen::RequireNonNegative(kNaN, "Sample spacing"); }),
              "Sample spacing must be finite and not negative");
    EXPECT_EQ(MessageOf([] { terraingen::RequireFinite(kInf, "The albedo hue"); }),
              "The albedo hue must be finite");
    EXPECT_EQ(MessageOf([] { terraingen::RequireUnitInterval(-0.5, "Shares"); }),
              "Shares must be in [0, 1]");
}


TEST(Checks, RequireDiameterRejectsDiametersOutsideAnyModelsDomain) {
    EXPECT_NO_THROW(terraingen::RequireDiameter(4.0));
    for (const double diameter : {0.0, -1.0, kInf, kNaN}) {
        EXPECT_THROW(terraingen::RequireDiameter(diameter), std::domain_error) << diameter;
    }
}
