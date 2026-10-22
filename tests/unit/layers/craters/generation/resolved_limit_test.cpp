#include "terraingen/layers/craters/generation/resolved_limit.h"

#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterSource;
using terraingen::UnresolvedCraterLimit;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

/// A source of `cells` x `cells` cells of `size` with its top-left corner at (`left`, `top`).
std::shared_ptr<const RasterSource> Dem(std::size_t cells, double size, double left, double top,
                                        terraingen::RasterSourceOptions options = {}) {
    Raster<float> heights(cells, cells, 1.0f);
    heights.SetTransform(GeoTransform::NorthUp(left, top, size));
    return std::make_shared<RasterSource>(heights, options);
}

}  // namespace

TEST(UnresolvedCraterLimit, IsTheCellsAcrossOfTheDataGivingTheHeight) {
    const auto dem = Dem(10, 5.0, 0.0, 50.0, {});
    const auto limit = UnresolvedCraterLimit(dem, 4.0);
    EXPECT_EQ(limit->ValueAt(20.0, 20.0), 20.0) << "four 5 m cells, at any sample spacing";
    terraingen::RasterSourceOptions upsampled;
    upsampled.effectivePixelSize = 8.0;
    EXPECT_EQ(UnresolvedCraterLimit(Dem(10, 5.0, 0.0, 50.0, upsampled), 4.0)->ValueAt(20.0, 20.0),
              32.0)
        << "four cells of the effective pixel size of a DEM upsampled before it was saved";
    EXPECT_EQ(limit->ValueAt(-1.0, 20.0), kInf);
    EXPECT_EQ(limit->Max(), kInf);
    EXPECT_EQ(UnresolvedCraterLimit(std::make_shared<terraingen::FlatSource>(4, 4, 1.0), 4.0)
                  ->ValueAt(0.0, 0.0),
              kInf);
}

TEST(UnresolvedCraterLimit, ConvertsDegreesToMetresOnABody) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    const auto dem = Dem(10, 0.01, 10.0, -20.0);
    const auto limit = UnresolvedCraterLimit(dem, 3.0, moon);
    EXPECT_DOUBLE_EQ(limit->ValueAt(10.05, -20.05), 3.0 * 0.01 * moon.UnitLength());
}

TEST(UnresolvedCraterLimit, RejectsANullSourceAndBadCellCounts) {
    const auto dem = Dem(4, 1.0, 0.0, 4.0);
    EXPECT_THROW(UnresolvedCraterLimit(nullptr, 4.0), std::invalid_argument);
    for (const double pixels : {0.0, -1.0, kInf, std::nan("")}) {
        EXPECT_THROW(UnresolvedCraterLimit(dem, pixels), std::invalid_argument) << pixels;
    }
}

TEST(UnresolvedCraterLimit, DropsGeneratedCratersTheDemResolvesWithoutACatalog) {
    const auto dem = Dem(26, 5.0, -65.0, 65.0);
    const terraingen::CraterModels models{
        std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
        std::make_shared<terraingen::UniformAges>(1.0),
        std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
        std::make_shared<terraingen::FassettThomsonProfile>()};
    terraingen::CraterLayerOptions options;
    options.seed = 3;
    options.generation.dMin = 6.0;
    options.generation.dMax = 40.0;
    const terraingen::CraterLayer every(models, options);
    options.generation.completeness = UnresolvedCraterLimit(dem, 4.0);
    const terraingen::CraterLayer limited(models, options);
    const terraingen::Bounds2d region{-60.0, -60.0, 60.0, 60.0};
    const terraingen::CraterList all = every.Craters(region);
    const terraingen::CraterList kept = limited.Craters(region);
    std::size_t large = 0;
    std::size_t small = 0;
    for (std::size_t n = 0; n < all.Size(); ++n) {
        const terraingen::Crater crater = all.At(n);
        const bool resolved =
            crater.diameter >= 20.0 && !std::isnan(dem->HeightAt(crater.x, crater.y, 5.0));
        large += resolved ? 1 : 0;
        small += resolved ? 0 : 1;
    }
    ASSERT_GT(large, 0u);
    ASSERT_GT(small, 0u);
    ASSERT_EQ(kept.Size(), small);
    std::size_t next = 0;
    for (std::size_t n = 0; n < all.Size(); ++n) {
        const terraingen::Crater crater = all.At(n);
        if (crater.diameter >= 20.0 && !std::isnan(dem->HeightAt(crater.x, crater.y, 5.0))) {
            continue;
        }
        EXPECT_EQ(kept.At(next++).id, crater.id);
    }
}
