#include "terraingen/bodies/mars/surface.h"
#include "terraingen/distribution/ivanov.h"
#include "terraingen/distribution/production_surface.h"
#include "terraingen/layers/craters/ages/uniform.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

TEST(MarsSurface, ScalesProductionToTheChronology) {
    const auto surface = terraingen::MarsSurface(3.0);
    EXPECT_NEAR(surface.density->Cumulative(1000.0), terraingen::IvanovMarsChronology(3.0), 1e-15);
    EXPECT_EQ(surface.density->Kind(), terraingen::SfdKind::Density);
    ASSERT_NE(dynamic_cast<const terraingen::UniformAges*>(surface.ages.get()), nullptr);
    EXPECT_THROW(terraingen::MarsSurface(0.0), std::domain_error);
    EXPECT_THROW(terraingen::MarsSurface(4.7), std::domain_error);
    EXPECT_THROW(terraingen::ProductionSurface(std::make_shared<terraingen::LogPolynomialSfd>(
                                                   terraingen::IvanovMarsProductionFunction()),
                                               1.0, nullptr),
                 std::invalid_argument);
}
