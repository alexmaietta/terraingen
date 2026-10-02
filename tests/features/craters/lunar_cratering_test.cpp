#include "terraingen/features/craters/lunar_cratering.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::DiameterRange;
using terraingen::ExtendedNeukumProductionFunction;
using terraingen::LogPolynomial;
using terraingen::NeukumLunarChronology;
using terraingen::NeukumLunarChronologyRate;
using terraingen::NeukumProductionFunction;
using terraingen::PowerLaw;
using terraingen::ProductionSurface;
using terraingen::RandomStream;
using terraingen::SfdKind;
using terraingen::SplicedSfd;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

}  // namespace

TEST(NeukumProductionFunction, IsAProductionRateFromTenMetresTo300Kilometres) {
    const LogPolynomial npf = NeukumProductionFunction();
    EXPECT_EQ(npf.Kind(), SfdKind::ProductionRate);
    EXPECT_EQ(npf.ValidRange().min, 10.0);
    EXPECT_EQ(npf.ValidRange().max, 300000.0);
    EXPECT_NEAR(npf.Cumulative(1000.0), 8.38e-4 / 1e6, 1e-15);
    EXPECT_THROW(npf.Cumulative(5.0), std::domain_error);
    for (const double d : {11.0, 120.0, 5000.0, 2e5}) {
        const double step = 1e-6;
        const double numeric =
            (npf.Cumulative(d * (1.0 - step)) - npf.Cumulative(d * (1.0 + step))) /
            (2.0 * step * d);
        EXPECT_NEAR(npf.Incremental(d), numeric, 1e-6 * numeric) << d;
    }
}

TEST(NeukumLunarChronology, EvaluatesEquationFiveWithinTheLunarAge) {
    EXPECT_EQ(NeukumLunarChronology(0.0), 0.0);
    EXPECT_NEAR(NeukumLunarChronology(1.0) * 1e6, 8.3800006e-4, 1e-11);
    EXPECT_NEAR(NeukumLunarChronology(3.5) * 1e6, 4.792566e-3, 1e-9);
    EXPECT_NO_THROW(NeukumLunarChronology(terraingen::kMaxLunarAgeGyr));
    EXPECT_THROW(NeukumLunarChronology(-0.1), std::domain_error);
    EXPECT_THROW(NeukumLunarChronology(4.7), std::domain_error);
    EXPECT_THROW(NeukumLunarChronology(kNaN), std::domain_error);
}

TEST(ProductionSurface, ScalesTheProductionFunctionToTheChronologyAtOneKilometre) {
    const auto npf = std::make_shared<LogPolynomial>(NeukumProductionFunction());
    const ProductionSurface surface(npf, 3.5);
    EXPECT_EQ(surface.Kind(), SfdKind::Density);
    EXPECT_EQ(surface.AgeGyr(), 3.5);
    EXPECT_EQ(&surface.Production(), npf.get());
    EXPECT_EQ(surface.ValidRange().max, 300000.0);
    EXPECT_NEAR(surface.Cumulative(1000.0), NeukumLunarChronology(3.5), 1e-15);
    const double scale = NeukumLunarChronology(3.5) / npf->Cumulative(1000.0);
    for (const double d : {10.0, 300.0, 1e5}) {
        EXPECT_NEAR(surface.Cumulative(d), scale * npf->Cumulative(d),
                    1e-12 * surface.Cumulative(d));
        EXPECT_NEAR(surface.Incremental(d), scale * npf->Incremental(d),
                    1e-12 * surface.Incremental(d));
    }
    RandomStream surfaceStream(8u, 1u);
    RandomStream npfStream(8u, 1u);
    for (int i = 0; i < 50; ++i) {
        const double expected = npf->SampleDiameter(npfStream, 10.0, 1e4);
        EXPECT_NEAR(surface.SampleDiameter(surfaceStream, 10.0, 1e4), expected, 1e-9 * expected);
    }
}

TEST(ProductionSurface, YoungSurfacesAccumulateCratersAtThePresentDayRate) {
    const auto npf = std::make_shared<LogPolynomial>(NeukumProductionFunction());
    const ProductionSurface young(npf, 0.01);
    const ProductionSurface twice(npf, 0.02);
    EXPECT_NEAR(twice.Cumulative(50.0) / young.Cumulative(50.0), 2.0, 1e-8);
    EXPECT_NEAR(young.Cumulative(50.0), 0.01 * npf->Cumulative(50.0),
                1e-6 * young.Cumulative(50.0));
    const ProductionSurface fresh(npf, 0.0);
    EXPECT_EQ(fresh.Cumulative(50.0), 0.0);
    RandomStream stream(1u, 2u);
    EXPECT_THROW(fresh.SampleDiameter(stream, 10.0, 100.0), std::invalid_argument);
}

TEST(ProductionSurface, InheritsExtrapolationAndRejectsInvalidProduction) {
    auto npf = std::make_shared<LogPolynomial>(NeukumProductionFunction());
    npf->SetAllowExtrapolation(true);
    EXPECT_NO_THROW(ProductionSurface(npf, 1.0).Cumulative(5.0));
    EXPECT_THROW(ProductionSurface(nullptr, 1.0), std::invalid_argument);
    EXPECT_THROW(ProductionSurface(std::make_shared<PowerLaw>(1.0, 2.0), 1.0),
                 std::invalid_argument);
    const auto small =
        std::make_shared<PowerLaw>(1.0, 2.0, SfdKind::ProductionRate, DiameterRange{0.0, 100.0});
    EXPECT_THROW(ProductionSurface(small, 1.0), std::domain_error);
    EXPECT_THROW(ProductionSurface(npf, 5.0), std::domain_error);
}

TEST(ExtendedNeukumProductionFunction, ContinuesBelowTenMetresWithSlope285) {
    const SplicedSfd extended = ExtendedNeukumProductionFunction();
    const LogPolynomial npf = NeukumProductionFunction();
    EXPECT_EQ(extended.Kind(), SfdKind::ProductionRate);
    EXPECT_EQ(extended.ValidRange().min, 2.5);
    EXPECT_EQ(extended.ValidRange().max, 300000.0);
    EXPECT_DOUBLE_EQ(extended.Cumulative(10.0), npf.Cumulative(10.0));
    EXPECT_NEAR(extended.Cumulative(5.0) / extended.Cumulative(10.0), std::pow(2.0, 2.85), 1e-12);
    EXPECT_DOUBLE_EQ(extended.Cumulative(500.0), npf.Cumulative(500.0));
    EXPECT_THROW(extended.Cumulative(2.0), std::domain_error);
    const ProductionSurface surface(std::make_shared<SplicedSfd>(extended), 3.5);
    EXPECT_NEAR(surface.Cumulative(1000.0), NeukumLunarChronology(3.5), 1e-15);
}

TEST(NeukumLunarChronology, RateIsTheDerivativeOfTheChronology) {
    EXPECT_NEAR(NeukumLunarChronologyRate(0.0) * 1e6, 8.38e-4, 1e-12);
    for (const double t : {0.5, 3.0, 4.0}) {
        const double step = 1e-6;
        const double numeric =
            (NeukumLunarChronology(t + step) - NeukumLunarChronology(t - step)) / (2.0 * step);
        EXPECT_NEAR(NeukumLunarChronologyRate(t), numeric, 1e-6 * numeric) << t;
    }
    EXPECT_THROW(NeukumLunarChronologyRate(-1.0), std::domain_error);
    EXPECT_THROW(NeukumLunarChronologyRate(5.0), std::domain_error);
}
