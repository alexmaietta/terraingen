#include "terraingen/distribution/sp8023.h"

#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

using terraingen::SpecificationCurve;

namespace {

double Log10Cumulative(const terraingen::SizeFrequencyDistribution& sfd, double diameter) {
    return std::log10(sfd.Cumulative(diameter));
}

}  // namespace

TEST(Sp8023IntercraterBlocks, FollowsTheDigitizedFigure7) {
    const auto minimum = terraingen::Sp8023IntercraterBlocks(SpecificationCurve::Minimum);
    const auto nominal = terraingen::Sp8023IntercraterBlocks(SpecificationCurve::Nominal);
    const auto maximum = terraingen::Sp8023IntercraterBlocks(SpecificationCurve::Maximum);
    EXPECT_NEAR(Log10Cumulative(*nominal, 1e-4), Log10Cumulative(*maximum, 1e-4), 1e-12)
        << "one curve below 1.8 mm";
    for (const double diameter : {0.01, 0.1, 1.0, 4.0}) {
        EXPECT_LT(minimum->Cumulative(diameter), nominal->Cumulative(diameter)) << diameter;
        EXPECT_LT(nominal->Cumulative(diameter), maximum->Cumulative(diameter)) << diameter;
    }
    EXPECT_NEAR(Log10Cumulative(*maximum, std::pow(10.0, -0.973)), 0.164, 1e-9);
    EXPECT_NEAR(Log10Cumulative(*nominal, 1.0), -2.652, 1e-3) << "about 1 block per 450 m^2";
    EXPECT_NEAR(Log10Cumulative(*minimum, 1.0), -3.526, 1e-3);
    EXPECT_GT(nominal->ValidRange().max, 5.0);
    EXPECT_THROW(terraingen::Sp8023IntercraterBlocks(static_cast<SpecificationCurve>(9)),
                 std::invalid_argument);
}

TEST(Sp8023CraterBlocks, FollowsTheDigitizedFigure9) {
    const auto minimum = terraingen::Sp8023CraterBlocks(SpecificationCurve::Minimum);
    const auto nominal = terraingen::Sp8023CraterBlocks(SpecificationCurve::Nominal);
    const auto maximum = terraingen::Sp8023CraterBlocks(SpecificationCurve::Maximum);
    for (const double diameter : {1.5, 3.0, 5.0}) {
        EXPECT_LT(minimum->Cumulative(diameter), nominal->Cumulative(diameter)) << diameter;
        EXPECT_LT(nominal->Cumulative(diameter), maximum->Cumulative(diameter)) << diameter;
        EXPECT_GT(
            nominal->Cumulative(diameter),
            terraingen::Sp8023IntercraterBlocks(SpecificationCurve::Nominal)->Cumulative(diameter))
            << "blocky crater rims hold more blocks than the plains";
    }
    EXPECT_NEAR(Log10Cumulative(*nominal, std::pow(10.0, 0.597)), -4.028, 1e-9);
    EXPECT_NEAR(nominal->ValidRange().min, 0.849, 0.001);
    EXPECT_THROW(nominal->Cumulative(0.5), std::domain_error) << "beyond the figure";
    EXPECT_THROW(terraingen::Sp8023CraterBlocks(static_cast<SpecificationCurve>(9)),
                 std::invalid_argument);
}
