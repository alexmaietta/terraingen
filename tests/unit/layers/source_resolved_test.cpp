// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/source_resolved.h"

#include "support/boulder_records.h"
#include "support/golombek_rapp.h"
#include "terraingen/distribution/power_law.h"
#include "terraingen/layers/bedforms.h"
#include "terraingen/layers/boulders/boulder_layer.h"
#include "terraingen/layers/craters/crater_layer.h"
#include "terraingen/layers/craters/profiles/fassett_thomson.h"
#include "terraingen/layers/craters/shaping/morphometry.h"
#include "terraingen/layers/dunes.h"
#include "terraingen/layers/noise_layer.h"
#include "terraingen/sources/raster_source.h"
#include "terraingen/sources/source_chain.h"
#include "terraingen/sources/synthetic_source.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

using terraingen::GeoTransform;
using terraingen::LayerResolvedLimit;
using terraingen::Raster;
using terraingen::RasterSource;
using terraingen::Source;
using terraingen::SourceResolvedLimit;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

/// A flat source of `cells` x `cells` cells of `size` meters with its top-left corner at
/// (`left`, `top`).
std::shared_ptr<const Source> Dem(std::size_t cells, double size, double left, double top) {
    Raster<float> heights(cells, cells, 0.0f);
    heights.SetTransform(GeoTransform::NorthUp(left, top, size));
    return std::make_shared<RasterSource>(heights);
}

/// The context of a point query at `spacing` meters over `source`.
terraingen::LayerContext Over(const std::shared_ptr<const Source>& source, double spacing) {
    terraingen::LayerContext context{spacing};
    context.source = source.get();
    return context;
}

/// Transverse dunes 550 m apart, without ripples.
terraingen::DuneOptions Crests() {
    terraingen::DuneOptions dunes;
    dunes.shape = terraingen::DuneShape::Transverse;
    return dunes;
}

}  // namespace

TEST(SourceResolvedLimit, IsTheCellsAcrossOfTheDataGivingTheHeight) {
    const auto dem = Dem(10, 5.0, 0.0, 50.0);
    const auto limit = SourceResolvedLimit(dem, 4.0);
    EXPECT_EQ(limit->ValueAt(20.0, 20.0), 20.0);
    EXPECT_EQ(limit->ValueAt(-100.0, 20.0), kInf) << "no data off the DEM";
    EXPECT_EQ(limit->Max(), kInf);
    EXPECT_EQ(SourceResolvedLimit(std::make_shared<terraingen::SyntheticSource>(), 4.0)
                  ->ValueAt(0.0, 0.0),
              kInf)
        << "synthetic noise without posts resolves nothing";
}

TEST(SourceResolvedLimit, GivesEachSourceOfAChainItsOwnLimit) {
    const auto chain =
        std::make_shared<terraingen::SourceChain>(std::vector<std::shared_ptr<const Source>>{
            Dem(10, 10.0, 0.0, 100.0), Dem(20, 50.0, -500.0, 500.0)});
    const auto limit = SourceResolvedLimit(chain, 4.0);
    EXPECT_EQ(limit->ValueAt(50.0, 50.0), 40.0);
    EXPECT_EQ(limit->ValueAt(300.0, 300.0), 200.0);
    EXPECT_EQ(limit->ValueAt(900.0, 900.0), kInf);
}

TEST(SourceResolvedLimit, IsInfiniteInNoDataHoles) {
    Raster<float> heights(8, 8, 0.0f);
    heights.SetTransform(GeoTransform::NorthUp(0.0, 80.0, 10.0));
    heights.SetNoData(-9999.0f);
    for (std::size_t row = 2; row < 6; ++row) {
        for (std::size_t col = 2; col < 6; ++col) {
            heights(col, row) = -9999.0f;
        }
    }
    const auto limit = SourceResolvedLimit(std::make_shared<RasterSource>(heights), 4.0);
    EXPECT_EQ(limit->ValueAt(5.0, 75.0), 40.0);
    EXPECT_EQ(limit->ValueAt(40.0, 40.0), kInf);
}

TEST(SourceResolvedLimit, RejectsANullSourceAndBadCellCounts) {
    EXPECT_THROW(SourceResolvedLimit(nullptr, 4.0), std::invalid_argument);
    for (const double pixels : {0.0, -1.0, kInf, std::nan("")}) {
        EXPECT_THROW(SourceResolvedLimit(Dem(4, 1.0, 0.0, 4.0), pixels), std::invalid_argument)
            << pixels;
        EXPECT_THROW(terraingen::ValidateSourceResolvesPixels(pixels), std::invalid_argument)
            << pixels;
    }
    EXPECT_NO_THROW(terraingen::ValidateSourceResolvesPixels(std::nullopt));
}

TEST(LayerResolvedLimit, IsNullWithoutASourceOrACount) {
    const auto dem = Dem(4, 1.0, 0.0, 4.0);
    EXPECT_EQ(LayerResolvedLimit(nullptr, 4.0, {}), nullptr);
    EXPECT_EQ(LayerResolvedLimit(dem, std::nullopt, {}), nullptr);
    ASSERT_NE(LayerResolvedLimit(dem, 3.0, {}), nullptr);
    EXPECT_EQ(LayerResolvedLimit(dem, 3.0, {})->ValueAt(1.5, 1.5), 3.0);
    EXPECT_FALSE(terraingen::InSource(nullptr, 100.0, 0.0, 0.0));
    EXPECT_EQ(terraingen::SourceResolvedSize(dem.get(), 3.0, {}, 1.5, 1.5), 3.0);
    EXPECT_EQ(terraingen::SourceResolvedSize(nullptr, 3.0, {}, 1.5, 1.5), kInf);
    EXPECT_EQ(terraingen::SourceResolvedSize(dem.get(), std::nullopt, {}, 1.5, 1.5), kInf);
}

TEST(DuneLayer, LeavesOutDunesTheSourceResolves) {
    const terraingen::DuneLayer dunes(Crests());
    const auto fine = Dem(20, 100.0, 0.0, 2000.0);
    const auto coarse = Dem(20, 200.0, 0.0, 4000.0);
    terraingen::DuneOptions unset = Crests();
    unset.sourceResolvesPixels.reset();
    const terraingen::DuneLayer ignored(unset);
    bool relief = false;
    for (double x = 10.0; x < 1000.0; x += 37.0) {
        const float alone = dunes.ApplyAt(0.0f, x, 700.0, {1.0});
        relief = relief || alone != 0.0f;
        EXPECT_EQ(dunes.ApplyAt(0.0f, x, 700.0, Over(fine, 1.0)), 0.0f) << "550 m dunes over 400 m";
        EXPECT_EQ(dunes.ApplyAt(0.0f, x, 700.0, Over(coarse, 1.0)), alone)
            << "550 m dunes under 800 m";
        EXPECT_EQ(ignored.ApplyAt(0.0f, x, 700.0, Over(fine, 1.0)), alone);
        EXPECT_EQ(dunes.ApplyAt(0.0f, x, 2500.0, Over(fine, 1.0)),
                  dunes.ApplyAt(0.0f, x, 2500.0, {1.0}))
            << "off the DEM";
    }
    EXPECT_TRUE(relief);
    terraingen::DuneOptions bad = Crests();
    bad.sourceResolvesPixels = 0.0;
    EXPECT_THROW(terraingen::DuneLayer{bad}, std::invalid_argument);
}

TEST(BoulderLayer, DropsTheBouldersTheSourceResolves) {
    terraingen::BoulderLayerOptions options = terraingen::testing::GolombekRappBoulders();
    options.sizes.coverage = 0.1;
    options.sizes.minDiameter = 0.2;
    options.sizes.maxDiameter = 3.0;
    EXPECT_FALSE(options.sourceResolvesPixels) << "none by default";
    const auto dem = Dem(80, 0.25, 0.0, 20.0);
    const terraingen::Bounds2d region{-20.0, 0.0, 20.0, 20.0};
    const auto all = terraingen::testing::Records(
        terraingen::BoulderLayer(options, nullptr, dem).Boulders(region));
    options.sourceResolvesPixels = 4.0;
    const auto kept = terraingen::testing::Records(
        terraingen::BoulderLayer(options, nullptr, dem).Boulders(region));
    std::size_t resolved = 0;
    std::size_t expected = 0;
    for (const terraingen::Boulder& boulder : all) {
        const bool onDem = boulder.x >= 0.0;
        resolved += onDem && boulder.diameter >= 1.0 ? 1 : 0;
        expected += onDem && boulder.diameter >= 1.0 ? 0 : 1;
    }
    ASSERT_GT(resolved, 0u);
    EXPECT_EQ(kept.size(), expected);
    for (const terraingen::Boulder& boulder : kept) {
        EXPECT_TRUE(boulder.x < 0.0 || boulder.diameter < 1.0) << boulder.x;
    }
    options.sourceResolvesPixels = 0.0;
    EXPECT_THROW(terraingen::BoulderLayer{options}, std::invalid_argument);
}

TEST(BedformLayer, LeavesOutBedformsTheSourceResolves) {
    terraingen::BedformOptions options;
    options.wavelength = 1.0;
    options.height = 0.1;
    options.cover = 0.9;
    options.fieldSize = 20.0;
    const terraingen::BedformLayer layer(options);
    const auto fine = Dem(50, 0.2, 0.0, 10.0);
    const auto coarse = Dem(50, 0.5, 0.0, 25.0);
    bool relief = false;
    for (double x = 0.1; x < 9.9; x += 0.13) {
        const float alone = layer.ApplyAt(0.0f, x, 5.0, {0.05});
        relief = relief || alone != 0.0f;
        EXPECT_EQ(layer.ApplyAt(0.0f, x, 5.0, Over(fine, 0.05)), 0.0f) << "1 m bedforms over 0.8 m";
        EXPECT_EQ(layer.ApplyAt(0.0f, x, 5.0, Over(coarse, 0.05)), alone)
            << "1 m bedforms under 2 m";
    }
    EXPECT_TRUE(relief);
}

TEST(NoiseLayer, LeavesOutTheNoiseLayersTheSourceResolves) {
    std::vector<terraingen::Fractal> params{terraingen::Fractal{}, terraingen::Fractal{}};
    params[0].wavelength = 1000.0f;
    params[0].amplitude = 50.0f;
    params[1].wavelength = 10.0f;
    params[1].amplitude = 1.0f;
    params[1].seed = 7;
    std::vector<terraingen::Fractal> fine = params;
    fine[0].enabled = false;
    const auto dem = Dem(20, 100.0, 0.0, 2000.0);
    const terraingen::NoiseLayer held(terraingen::NoiseLayerOptions{params, 4.0});
    const terraingen::NoiseLayer small(fine);
    const terraingen::NoiseLayer all(params);
    for (double x = 5.0; x < 1900.0; x += 97.0) {
        EXPECT_EQ(held.ApplyAt(0.0f, x, 300.0, Over(dem, 1.0)),
                  small.ApplyAt(0.0f, x, 300.0, Over(dem, 1.0)))
            << "the 1000 m layer is in the 400 m limit";
        EXPECT_EQ(held.ApplyAt(0.0f, x, 2500.0, Over(dem, 1.0)),
                  all.ApplyAt(0.0f, x, 2500.0, Over(dem, 1.0)))
            << "off the DEM";
        EXPECT_EQ(held.ApplyAt(0.0f, x, 300.0, Over(nullptr, 1.0)),
                  all.ApplyAt(0.0f, x, 300.0, Over(nullptr, 1.0)))
            << "nothing resolved without a source";
    }
    Raster<float> grid(16, 16, 0.0f);
    grid.SetTransform(GeoTransform::NorthUp(100.0, 900.0, 25.0));
    held.Apply(grid, Over(dem, 25.0));
    for (std::size_t row = 0; row < 16; ++row) {
        for (std::size_t col = 0; col < 16; ++col) {
            const auto center = grid.CellCenter(col, row);
            ASSERT_EQ(grid(col, row), held.ApplyAt(0.0f, center.x, center.y, Over(dem, 25.0)));
        }
    }
    EXPECT_EQ(held.Options().sourceResolvesPixels, 4.0);
    EXPECT_THROW(terraingen::NoiseLayer(terraingen::NoiseLayerOptions{params, -1.0}),
                 std::invalid_argument);
}

TEST(CraterLayer, DropsGeneratedCratersTheSourceResolvesWithoutACatalog) {
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
    options.generation.completeness = SourceResolvedLimit(dem, 4.0);
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
