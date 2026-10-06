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
