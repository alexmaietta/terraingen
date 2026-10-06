#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::CraterAgeModel;
using terraingen::ExtendedNeukumProductionFunction;
using terraingen::FreshAges;
using terraingen::LogPolynomialSfd;
using terraingen::LunarSurface;
using terraingen::NeukumLunarChronology;
using terraingen::NeukumProductionFunction;
using terraingen::PowerLawSfd;
using terraingen::RandomStream;
using terraingen::SfdKind;
using terraingen::SteadyStateAges;
using terraingen::SurfaceModel;
using terraingen::TraskEquilibrium;
using terraingen::UniformAges;

namespace {

SteadyStateAges NeukumTrask(double ageGyr) {
    return SteadyStateAges(std::make_shared<LogPolynomialSfd>(NeukumProductionFunction()),
                           std::make_shared<PowerLawSfd>(TraskEquilibrium()), ageGyr);
}

}  // namespace

TEST(SteadyStateAges, LifetimeIsTheEquilibriumOverProductionRatioCappedAtTheSurfaceAge) {
    const SteadyStateAges ages = NeukumTrask(3.5);
    const LogPolynomialSfd npf = NeukumProductionFunction();
    const PowerLawSfd trask = TraskEquilibrium();
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
    const auto npf = std::make_shared<LogPolynomialSfd>(NeukumProductionFunction());
    const auto trask = std::make_shared<PowerLawSfd>(TraskEquilibrium());
    EXPECT_THROW(SteadyStateAges(nullptr, trask, 1.0), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(npf, nullptr, 1.0), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(trask, trask, 1.0), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(npf, npf, 1.0), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(npf, trask, -0.5), std::domain_error);
    EXPECT_THROW(SteadyStateAges(npf, trask, 4.7), std::domain_error);
}
