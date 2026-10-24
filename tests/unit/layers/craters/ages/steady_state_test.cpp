// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/ages/steady_state.h"

#include "terraingen/distribution/equilibrium.h"
#include "terraingen/distribution/ivanov.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/random/stream.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::LogPolynomialSfd;
using terraingen::NeukumLunarChronology;
using terraingen::NeukumProductionFunction;
using terraingen::PowerLawSfd;
using terraingen::RandomStream;
using terraingen::SteadyStateAges;
using terraingen::TraskEquilibrium;

namespace {

SteadyStateAges NeukumTrask(double ageGyr) {
    return SteadyStateAges(std::make_shared<LogPolynomialSfd>(NeukumProductionFunction()),
                           std::make_shared<PowerLawSfd>(TraskEquilibrium()), ageGyr,
                           NeukumLunarChronology);
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
    EXPECT_THROW(SteadyStateAges(nullptr, trask, 1.0, NeukumLunarChronology),
                 std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(npf, nullptr, 1.0, NeukumLunarChronology), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(trask, trask, 1.0, NeukumLunarChronology), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(npf, npf, 1.0, NeukumLunarChronology), std::invalid_argument);
    EXPECT_THROW(SteadyStateAges(npf, trask, -0.5, NeukumLunarChronology), std::domain_error);
    EXPECT_THROW(SteadyStateAges(npf, trask, 4.7, NeukumLunarChronology), std::domain_error);
}

TEST(SteadyStateAges, DrawsAgesFromTheChronologyItIsGiven) {
    const SteadyStateAges ages(std::make_shared<LogPolynomialSfd>(NeukumProductionFunction()),
                               std::make_shared<PowerLawSfd>(TraskEquilibrium()), 3.5,
                               terraingen::IvanovMarsChronology);
    RandomStream copy(9u, 2u);
    RandomStream stream(9u, 2u);
    const double lifetime = ages.LifetimeGyr(1000.0);
    for (int i = 0; i < 20; ++i) {
        const double u = copy.NextDouble();
        const double age = ages.SampleAgeGyr(stream, 1000.0);
        EXPECT_NEAR(terraingen::IvanovMarsChronology(age) /
                        terraingen::IvanovMarsChronology(lifetime),
                    u, 1e-9);
    }
}

TEST(SteadyStateAges, RequiresAChronology) {
    const auto npf = std::make_shared<LogPolynomialSfd>(NeukumProductionFunction());
    const auto trask = std::make_shared<PowerLawSfd>(TraskEquilibrium());
    EXPECT_THROW(SteadyStateAges(npf, trask, 1.0, terraingen::Chronology{}), std::invalid_argument);
}

TEST(EquilibriumSurface, GivesTheDensityAndAgesOfOneSurface) {
    const auto production = std::make_shared<LogPolynomialSfd>(NeukumProductionFunction());
    const auto equilibrium = std::make_shared<PowerLawSfd>(TraskEquilibrium());
    const terraingen::EquilibriumSurface surface{production, equilibrium, 3.2,
                                                 NeukumLunarChronology};
    const terraingen::ProductionSurface produced(production, 3.2, NeukumLunarChronology);
    for (const double diameter : {20.0, 200.0, 2000.0}) {
        EXPECT_DOUBLE_EQ(
            surface.Density()->Cumulative(diameter),
            std::fmin(produced.Cumulative(diameter), equilibrium->Cumulative(diameter)))
            << diameter;
        EXPECT_DOUBLE_EQ(surface.Ages()->LifetimeGyr(diameter),
                         NeukumTrask(3.2).LifetimeGyr(diameter));
    }
    EXPECT_EQ(surface.Ages()->SurfaceAgeGyr(), 3.2);
}
