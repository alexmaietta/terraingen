// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/distribution/production_surface.h"

#include "terraingen/distribution/ivanov.h"
#include "terraingen/distribution/neukum.h"
#include "terraingen/distribution/power_law.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

using terraingen::Chronology;
using terraingen::PowerLawSfd;
using terraingen::ProductionSurface;
using terraingen::SfdKind;
using terraingen::detail::ExponentialChronology;

TEST(ProductionSurface, ScalesAnyProductionRateToItsChronologyAtOneKilometer) {
    const auto production = std::make_shared<PowerLawSfd>(2.0, 2.0, SfdKind::ProductionRate);
    const Chronology linear = [](double ageGyr) { return 1e-6 * ageGyr; };
    const ProductionSurface surface(production, 3.0, linear);
    EXPECT_EQ(surface.Kind(), SfdKind::Density);
    EXPECT_DOUBLE_EQ(surface.Cumulative(1000.0), 3e-6);
    EXPECT_DOUBLE_EQ(surface.Cumulative(10.0), 3e-6 * 1e4);
    EXPECT_THROW(ProductionSurface(production, 3.0, Chronology{}), std::invalid_argument);
    EXPECT_THROW(ProductionSurface(std::make_shared<PowerLawSfd>(2.0, 2.0), 3.0, linear),
                 std::invalid_argument);
}

TEST(ExponentialChronology, CountsAndRatesFollowTheFormInSquareMeters) {
    const ExponentialChronology chronology{1e-3, 2.0, 0.5, 3.0, "Age out of range"};
    EXPECT_DOUBLE_EQ(chronology.Count(0.0), 0.0);
    EXPECT_DOUBLE_EQ(chronology.Count(1.0), (1e-3 * std::expm1(2.0) + 0.5) / 1e6);
    EXPECT_DOUBLE_EQ(chronology.Rate(1.0), (1e-3 * 2.0 * std::exp(2.0) + 0.5) / 1e6);
    try {
        chronology.Count(3.5);
        FAIL() << "age beyond the maximum accepted";
    } catch (const std::domain_error& error) {
        EXPECT_EQ(std::string(error.what()), "Age out of range");
    }
    EXPECT_THROW(chronology.Rate(-0.1), std::domain_error);
}

TEST(ExponentialChronology, BacksTheNeukumAndIvanovChronologies) {
    const ExponentialChronology neukum{5.44e-14, 6.93, 8.38e-4, terraingen::kMaxLunarAgeGyr,
                                       "lunar"};
    const ExponentialChronology ivanov{2.68e-14, 6.93, 4.13e-4, terraingen::kMaxMarsAgeGyr,
                                       "martian"};
    for (const double age : {0.0, 0.5, 3.8, 4.6}) {
        EXPECT_EQ(terraingen::NeukumLunarChronology(age), neukum.Count(age));
        EXPECT_EQ(terraingen::NeukumLunarChronologyRate(age), neukum.Rate(age));
        EXPECT_EQ(terraingen::IvanovMarsChronology(age), ivanov.Count(age));
        EXPECT_EQ(terraingen::IvanovMarsChronologyRate(age), ivanov.Rate(age));
    }
}
