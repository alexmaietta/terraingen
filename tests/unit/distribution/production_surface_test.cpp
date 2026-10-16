#include "terraingen/distribution/production_surface.h"

#include "terraingen/distribution/power_law.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

using terraingen::Chronology;
using terraingen::PowerLawSfd;
using terraingen::ProductionSurface;
using terraingen::SfdKind;

TEST(ProductionSurface, ScalesAnyProductionRateToItsChronologyAtOneKilometre) {
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
