#include "terraingen/features/craters/ages.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::AllFresh;
using terraingen::CraterAgeModel;
using terraingen::ExtendedNeukumProductionFunction;
using terraingen::LogPolynomial;
using terraingen::LunarSurface;
using terraingen::NeukumLunarChronology;
using terraingen::NeukumProductionFunction;
using terraingen::PowerLaw;
using terraingen::RandomStream;
using terraingen::SfdKind;
using terraingen::SteadyStateAges;
using terraingen::SurfaceModel;
using terraingen::TraskEquilibrium;
using terraingen::UniformFreshness;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

SteadyStateAges NeukumTrask(double ageGyr) {
    return SteadyStateAges(std::make_shared<LogPolynomial>(NeukumProductionFunction()),
                           std::make_shared<PowerLaw>(TraskEquilibrium()), ageGyr);
}

}  // namespace

TEST(AllFresh, GivesEveryCraterAgeZeroAndFreshnessOne) {
    const AllFresh fresh;
    RandomStream stream(1u, 1u);
    EXPECT_EQ(fresh.LifetimeGyr(10.0), 0.0);
    EXPECT_EQ(fresh.SampleAgeGyr(stream, 10.0), 0.0);
    EXPECT_EQ(fresh.Freshness(10.0, 0.0), 1.0);
    EXPECT_EQ(fresh.Freshness(10.0, 2.0), 1.0);
    EXPECT_THROW(fresh.LifetimeGyr(0.0), std::domain_error);
    EXPECT_THROW(fresh.SampleAgeGyr(stream, kNaN), std::domain_error);
    EXPECT_THROW(fresh.Freshness(-1.0, 0.0), std::domain_error);
    const std::unique_ptr<CraterAgeModel> base = std::make_unique<AllFresh>();
    EXPECT_EQ(base->LifetimeGyr(1.0), 0.0);
}

TEST(UniformFreshness, DrawsAgesUniformlyOverOneLifetime) {
    const UniformFreshness uniform(2.0);
    RandomStream copy(4u, 2u);
    RandomStream stream(4u, 2u);
    for (int i = 0; i < 50; ++i) {
        const double expected = 2.0 * copy.NextDouble();
        EXPECT_DOUBLE_EQ(uniform.SampleAgeGyr(stream, 1.0 + i), expected);
    }
    EXPECT_EQ(uniform.LifetimeGyr(1e4), 2.0);
    EXPECT_DOUBLE_EQ(uniform.Freshness(5.0, 0.5), 0.75);
    EXPECT_EQ(uniform.Freshness(5.0, 3.0), 0.0);
    EXPECT_EQ(uniform.Freshness(5.0, -1.0), 1.0);
    EXPECT_THROW(UniformFreshness(0.0), std::invalid_argument);
    EXPECT_THROW(UniformFreshness{kInf}, std::invalid_argument);
    EXPECT_THROW(UniformFreshness{kNaN}, std::invalid_argument);
}

TEST(SteadyStateAges, LifetimeIsTheEquilibriumOverProductionRatioCappedAtTheSurfaceAge) {
    const SteadyStateAges ages = NeukumTrask(3.5);
    const LogPolynomial npf = NeukumProductionFunction();
    const PowerLaw trask = TraskEquilibrium();
    for (const double d : {10.0, 100.0, 150.0}) {
        const double expected = trask.Incremental(d) / npf.Incremental(d);
        EXPECT_NEAR(ages.EquilibriumLifetimeGyr(d), expected, 1e-12 * expected) << d;
        EXPECT_NEAR(ages.LifetimeGyr(d), expected, 1e-12 * expected) << d;
    }
    EXPECT_NEAR(ages.LifetimeGyr(100.0), 1.717, 1e-3);
    EXPECT_GT(ages.EquilibriumLifetimeGyr(1000.0), 3.5);
    EXPECT_EQ(ages.LifetimeGyr(1000.0), 3.5);
    EXPECT_EQ(ages.SurfaceAgeGyr(), 3.5);
    EXPECT_THROW(ages.LifetimeGyr(5.0), std::domain_error);
}

TEST(SteadyStateAges, DrawsAgesFromTheChronologyWithinTheLifetime) {
    const SteadyStateAges ages = NeukumTrask(3.5);
    RandomStream copy(5u, 1u);
    RandomStream stream(5u, 1u);
    for (const double d : {10.0, 1000.0}) {
        const double lifetime = ages.LifetimeGyr(d);
        for (int i = 0; i < 20; ++i) {
            const double u = copy.NextDouble();
            const double age = ages.SampleAgeGyr(stream, d);
            EXPECT_GE(age, 0.0);
            EXPECT_LE(age, lifetime);
            EXPECT_NEAR(NeukumLunarChronology(age) / NeukumLunarChronology(lifetime), u, 1e-9) << d;
        }
    }
    const SteadyStateAges fresh = NeukumTrask(0.0);
    EXPECT_EQ(fresh.LifetimeGyr(50.0), 0.0);
    EXPECT_EQ(fresh.SampleAgeGyr(stream, 50.0), 0.0);
}

TEST(SteadyStateAges, RejectsInvalidInputs) {
    const auto npf = std::make_shared<LogPolynomial>(NeukumProductionFunction());
    const auto trask = std::make_shared<PowerLaw>(TraskEquilibrium());
    EXPECT_THROW(SteadyStateAges(nullptr, trask, 1.0), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(npf, nullptr, 1.0), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(trask, trask, 1.0), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(npf, npf, 1.0), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(npf, trask, -0.5), std::domain_error);
    EXPECT_THROW(SteadyStateAges(npf, trask, 4.7), std::domain_error);
}

TEST(LunarSurface, CombinesTheDsneDensityWithSteadyStateAges) {
    const SurfaceModel surface = LunarSurface(3.5);
    EXPECT_EQ(surface.density->Kind(), SfdKind::Density);
    EXPECT_EQ(surface.density->ValidRange().min, 2.5);
    EXPECT_EQ(surface.density->ValidRange().max, 300000.0);
    EXPECT_DOUBLE_EQ(surface.density->Cumulative(50.0), TraskEquilibrium().Cumulative(50.0));
    EXPECT_NEAR(surface.density->Cumulative(1000.0), NeukumLunarChronology(3.5), 1e-15);
    EXPECT_LT(surface.density->Cumulative(1000.0), TraskEquilibrium().Cumulative(1000.0));
    const auto* ages = dynamic_cast<const SteadyStateAges*>(surface.ages.get());
    ASSERT_NE(ages, nullptr);
    EXPECT_EQ(ages->SurfaceAgeGyr(), 3.5);
    EXPECT_EQ(surface.ages->LifetimeGyr(1000.0), 3.5);
    const double expected =
        TraskEquilibrium().Incremental(5.0) / ExtendedNeukumProductionFunction().Incremental(5.0);
    EXPECT_NEAR(surface.ages->LifetimeGyr(5.0), expected, 1e-12 * expected);
    EXPECT_THROW(LunarSurface(5.0), std::domain_error);
}
