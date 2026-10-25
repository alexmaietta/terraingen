// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/sources/source_chain.h"

#include "terraingen/sources/diamond_square.h"
#include "terraingen/sources/flat_source.h"
#include "terraingen/sources/raster_source.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

using terraingen::ChainSeamMode;
using terraingen::ChainSeams;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RasterGrid;
using terraingen::SourceChain;

namespace {

/// Heights x + 2y where `covered` holds, no data elsewhere, marked with `noData` (NaN if unset).
class Patchy final : public terraingen::Source {
  public:
    Patchy(bool (*covered)(double, double), std::optional<float> noData, RasterGrid grid)
        : covered_(covered), noData_(noData), grid_(grid) {}

    RasterGrid Grid() const override {
        return grid_;
    }
    terraingen::Vec2d ReferencePoint() const override {
        return {7.0, 8.0};
    }
    void Fill(Raster<float>& heights, double spacing) const override {
        ++fills;
        const float fill = noData_.value_or(std::numeric_limits<float>::quiet_NaN());
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const auto center = heights.CellCenter(col, row);
                const float height = HeightAt(center.x, center.y, spacing);
                heights(col, row) = std::isnan(height) ? fill : height;
            }
        }
        heights.SetNoData(fill);
    }
    float HeightAt(double x, double y, double /*spacing*/) const override {
        return covered_(x, y) ? static_cast<float>(x + 2.0 * y) : std::nanf("");
    }

    mutable std::atomic<int> fills{0};

  private:
    bool (*covered_)(double, double);
    std::optional<float> noData_;
    RasterGrid grid_;
};

const RasterGrid kGrid{8, 6, GeoTransform::NorthUp(-4.0, 3.0, 1.0)};

/// Heights `height(x, y)` where `covered(x, y)` holds and NaN elsewhere, over `grid`.
class Shaped final : public terraingen::Source {
  public:
    Shaped(double (*height)(double, double), bool (*covered)(double, double), RasterGrid grid)
        : height_(height), covered_(covered), grid_(grid) {}

    RasterGrid Grid() const override {
        return grid_;
    }
    void Fill(Raster<float>& heights, double spacing) const override {
        for (std::size_t row = 0; row < heights.Height(); ++row) {
            for (std::size_t col = 0; col < heights.Width(); ++col) {
                const auto center = heights.CellCenter(col, row);
                heights(col, row) = HeightAt(center.x, center.y, spacing);
            }
        }
    }
    float HeightAt(double x, double y, double /*spacing*/) const override {
        return covered_(x, y) ? static_cast<float>(height_(x, y)) : std::nanf("");
    }

  private:
    double (*height_)(double, double);
    bool (*covered_)(double, double);
    RasterGrid grid_;
};

/// The 32 m square of 1 m cells around the origin that the earlier sources of the seam tests
/// cover.
const RasterGrid kSquare{32, 32, GeoTransform::NorthUp(-16.0, 16.0, 1.0)};

bool Everywhere(double /*x*/, double /*y*/) {
    return true;
}

/// Inside `kSquare`: beyond it a gap is open.
bool InSquare(double x, double y) {
    return std::abs(x) < 16.0 && std::abs(y) < 16.0;
}

/// Outside the 6 m hole at the origin: a closed gap.
bool AroundHole(double x, double y) {
    return !(std::abs(x) < 3.0 && std::abs(y) < 3.0);
}

/// Inside `kSquare` but outside the hole: an open gap and a closed one.
bool InSquareAroundHole(double x, double y) {
    return InSquare(x, y) && AroundHole(x, y);
}

/// The earlier plane of the hole tests.
double Tilted(double x, double y) {
    return 0.1 * x + 0.05 * y + 8.0;
}

/// A later source with relief of its own.
double Wavy(double x, double y) {
    return std::sin(0.3 * x) + 0.5 * std::cos(0.2 * y);
}

ChainSeams Seams(ChainSeamMode mode, double decayLength, double width = 6.0) {
    ChainSeams seams;
    seams.mode = mode;
    seams.decayLength = decayLength;
    seams.width = width;
    return seams;
}

/// The chain of `earlier` over `later` joined by `seams`.
std::shared_ptr<SourceChain> Chain(const std::shared_ptr<const terraingen::Source>& earlier,
                                   const std::shared_ptr<const terraingen::Source>& later,
                                   const ChainSeams& seams) {
    return std::make_shared<SourceChain>(
        std::vector<std::shared_ptr<const terraingen::Source>>{earlier, later}, seams);
}

}  // namespace

TEST(SourceChain, FillsEachGapFromTheNextSourceWithData) {
    const auto east =
        std::make_shared<Patchy>([](double x, double) { return x > 0.0; }, std::nullopt, kGrid);
    const auto north =
        std::make_shared<Patchy>([](double, double y) { return y > 1.0; }, -9999.0f, RasterGrid{});
    const SourceChain partial({east, north});
    EXPECT_EQ(partial.Grid(), kGrid);
    EXPECT_EQ(partial.ReferencePoint().x, 7.0);
    Raster<float> heights = kGrid.MakeRaster();
    partial.Fill(heights);
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto center = heights.CellCenter(col, row);
            const float expected = partial.HeightAt(center.x, center.y, 1.0);
            if (center.x > 0.0 || center.y > 1.0) {
                EXPECT_EQ(heights(col, row), static_cast<float>(center.x + 2.0 * center.y));
                EXPECT_EQ(heights(col, row), expected);
            } else {
                EXPECT_TRUE(heights.IsNoData(heights(col, row)));
                EXPECT_TRUE(std::isnan(heights(col, row))) << "the first source's no-data value";
                EXPECT_TRUE(std::isnan(expected));
            }
        }
    }

    const SourceChain complete(
        {east, north, std::make_shared<terraingen::FlatSource>(2, 2, 1.0, -5.0f)});
    complete.Fill(heights);
    EXPECT_EQ(heights(0, 5), -5.0f);
    EXPECT_EQ(complete.HeightAt(-3.5, -2.5, 1.0), -5.0f);
}

TEST(SourceChain, FillsNaNGapsOfSourcesWithoutANoDataValue) {
    /// NaN where x < 0, as a GeoTIFF without a no-data tag reads, and no no-data value set.
    class Unmarked final : public terraingen::Source {
      public:
        RasterGrid Grid() const override {
            return kGrid;
        }
        void Fill(Raster<float>& heights, double spacing) const override {
            for (std::size_t row = 0; row < heights.Height(); ++row) {
                for (std::size_t col = 0; col < heights.Width(); ++col) {
                    const auto center = heights.CellCenter(col, row);
                    heights(col, row) = HeightAt(center.x, center.y, spacing);
                }
            }
        }
        float HeightAt(double x, double /*y*/, double /*spacing*/) const override {
            return x < 0.0 ? std::nanf("") : 1.0f;
        }
    };
    const SourceChain chain(
        {std::make_shared<Unmarked>(), std::make_shared<terraingen::FlatSource>(2, 2, 1.0, -5.0f)});
    Raster<float> heights = kGrid.MakeRaster();
    chain.Fill(heights);
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            const auto center = heights.CellCenter(col, row);
            EXPECT_EQ(heights(col, row), center.x < 0.0 ? -5.0f : 1.0f);
            EXPECT_EQ(heights(col, row), chain.HeightAt(center.x, center.y, 1.0));
        }
    }
}

TEST(SourceChain, StopsOnceEveryCellHasData) {
    const auto full = std::make_shared<Patchy>([](double, double) { return true; }, -1.0f, kGrid);
    const auto spare = std::make_shared<Patchy>([](double, double) { return true; }, -1.0f, kGrid);
    Raster<float> heights = kGrid.MakeRaster();
    SourceChain({full, spare}).Fill(heights);
    EXPECT_EQ(full->fills, 1);
    EXPECT_EQ(spare->fills, 0);
}

TEST(SourceChain, ResolvesWhatTheMemberGivingTheHeightResolves) {
    Raster<float> fine(20, 20, 1.0f);
    fine.SetTransform(GeoTransform::NorthUp(0.0, 20.0, 1.0));
    fine.SetNoData(-1.0f);
    fine(5, 5) = -1.0f;
    Raster<float> coarse(10, 10, 2.0f);
    coarse.SetTransform(GeoTransform::NorthUp(-40.0, 40.0, 8.0));
    const auto site = std::make_shared<terraingen::RasterSource>(fine);
    const auto region = std::make_shared<terraingen::RasterSource>(coarse);
    const SourceChain chain({site, region, std::make_shared<terraingen::FlatSource>(1, 1, 1.0)});
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ(chain.ResolutionAt(10.3, 10.6), 1.0);
    EXPECT_EQ(chain.ResolutionAt(5.5, 14.5), 8.0) << "the site DEM's hole";
    EXPECT_EQ(chain.ResolutionAt(-20.0, 30.0), 8.0);
    EXPECT_EQ(chain.ResolutionAt(100.0, 100.0), inf) << "only the flat datum";
    EXPECT_EQ(SourceChain({site, region}).ResolutionAt(100.0, 100.0), inf);
}

TEST(SourceChain, UpsamplesEachMemberAsItSays) {
    Raster<float> site(16, 16);
    site.SetTransform(GeoTransform::NorthUp(0.0, 16.0, 1.0));
    for (std::size_t i = 0; i < site.Size(); ++i) {
        site.Values()[i] = static_cast<float>(std::sin(0.3 * static_cast<double>(i)));
    }
    Raster<float> regional(8, 8, 0.0f);
    regional.SetTransform(GeoTransform::NorthUp(-16.0, 32.0, 6.0));
    for (std::size_t i = 0; i < regional.Size(); ++i) {
        regional.Values()[i] = static_cast<float>(i % 5);
    }
    terraingen::RasterSourceOptions bilinear;
    bilinear.upsampler =
        std::make_shared<terraingen::KernelUpsampler>(terraingen::ResampleMethod::Bilinear);
    terraingen::RasterSourceOptions fractal;
    terraingen::DiamondSquareOptions options;
    options.fractalDimension = 2.5;
    options.scale = 0.3;
    fractal.upsampler = std::make_shared<terraingen::DiamondSquareUpsampler>(options);
    const auto first = std::make_shared<terraingen::RasterSource>(site, bilinear);
    const auto second = std::make_shared<terraingen::RasterSource>(regional, fractal);
    const SourceChain chain({first, second});
    const RasterGrid grid{60, 60, GeoTransform::NorthUp(-10.0, 25.0, 0.5)};
    Raster<float> heights = grid.MakeRaster();
    chain.Fill(heights, 0.5);
    Raster<float> fromFirst = grid.MakeRaster();
    first->Fill(fromFirst, 0.5);
    Raster<float> fromSecond = grid.MakeRaster();
    second->Fill(fromSecond, 0.5);
    std::size_t firsts = 0;
    std::size_t seconds = 0;
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto center = heights.CellCenter(col, row);
            const bool inSite = !fromFirst.IsMissing(fromFirst(col, row));
            const float expected = inSite ? fromFirst(col, row) : fromSecond(col, row);
            ASSERT_EQ(heights(col, row), expected) << col << "," << row;
            ASSERT_EQ(chain.HeightAt(center.x, center.y, 0.5), expected);
            ++(inSite ? firsts : seconds);
        }
    }
    EXPECT_GT(firsts, 0u);
    EXPECT_GT(seconds, 0u);
}

TEST(SourceChain, RejectsNoSourcesAndNullOnes) {
    EXPECT_THROW(SourceChain({}), std::invalid_argument);
    EXPECT_THROW(SourceChain({nullptr}), std::invalid_argument);
    const auto flat = std::make_shared<terraingen::FlatSource>(2, 2, 1.0);
    EXPECT_EQ(SourceChain({flat}).Sources().size(), 1u);
}

TEST(SourceChainSeams, NoneKeepsTheFirstHeightWithDataToTheBit) {
    const auto earlier = std::make_shared<Shaped>(Tilted, InSquareAroundHole, kSquare);
    const auto later = std::make_shared<Shaped>(Wavy, Everywhere, RasterGrid{});
    const SourceChain plain({earlier, later});
    EXPECT_EQ(plain.Seams(), ChainSeams{});
    EXPECT_EQ(plain.Seams().mode, ChainSeamMode::None);
    const auto none = Chain(earlier, later, Seams(ChainSeamMode::None, 7.0, 3.0));
    const RasterGrid grid{96, 96, GeoTransform::NorthUp(-24.0, 24.0, 0.5)};
    Raster<float> fromPlain = grid.MakeRaster();
    plain.Fill(fromPlain, 0.5);
    Raster<float> fromNone = grid.MakeRaster();
    none->Fill(fromNone, 0.5);
    for (std::size_t row = 0; row < grid.height; ++row) {
        for (std::size_t col = 0; col < grid.width; ++col) {
            const auto center = fromNone.CellCenter(col, row);
            const float first = earlier->HeightAt(center.x, center.y, 0.5);
            const float expected =
                std::isnan(first) ? later->HeightAt(center.x, center.y, 0.5) : first;
            ASSERT_EQ(fromNone(col, row), expected) << col << "," << row;
            ASSERT_EQ(fromPlain(col, row), expected);
            ASSERT_EQ(none->HeightAt(center.x, center.y, 0.5), expected);
        }
    }
}

TEST(SourceChainSeams, PointsTilesAndGridsAgreeToTheBit) {
    const auto earlier = std::make_shared<Shaped>(Tilted, InSquareAroundHole, kSquare);
    const auto later = std::make_shared<Shaped>(Wavy, Everywhere, RasterGrid{});
    const RasterGrid grid{96, 96, GeoTransform::NorthUp(-24.0, 24.0, 0.5)};
    for (const ChainSeamMode mode :
         {ChainSeamMode::Feather, ChainSeamMode::Height, ChainSeamMode::Slope}) {
        const auto tiled = Chain(earlier, later, Seams(mode, 3.0, 4.0));
        Raster<float> tiles = grid.MakeRaster();
        for (std::size_t tileRow = 0; tileRow < 2; ++tileRow) {
            for (std::size_t tileCol = 0; tileCol < 2; ++tileCol) {
                Raster<float> tile(48, 48);
                tile.SetTransform(GeoTransform::NorthUp(-24.0 + 24.0 * static_cast<double>(tileCol),
                                                        24.0 - 24.0 * static_cast<double>(tileRow),
                                                        0.5));
                tiled->Fill(tile, 0.5);
                for (std::size_t row = 0; row < 48; ++row) {
                    for (std::size_t col = 0; col < 48; ++col) {
                        tiles(col + 48 * tileCol, row + 48 * tileRow) = tile(col, row);
                    }
                }
            }
        }
        const auto whole = Chain(earlier, later, Seams(mode, 3.0, 4.0));
        const auto points = Chain(earlier, later, Seams(mode, 3.0, 4.0));
        Raster<float> heights = grid.MakeRaster();
        whole->Fill(heights, 0.5);
        std::size_t matched = 0;
        for (std::size_t row = 0; row < grid.height; ++row) {
            for (std::size_t col = 0; col < grid.width; ++col) {
                const auto center = heights.CellCenter(col, row);
                ASSERT_EQ(tiles(col, row), heights(col, row)) << col << "," << row;
                ASSERT_EQ(points->HeightAt(center.x, center.y, 0.5), heights(col, row))
                    << col << "," << row;
                if (std::isnan(earlier->HeightAt(center.x, center.y, 0.5)) &&
                    heights(col, row) != later->HeightAt(center.x, center.y, 0.5)) {
                    ++matched;
                }
            }
        }
        EXPECT_GT(matched, 0u) << "the seam shifts the later source";
    }
}

TEST(SourceChainSeams, HeightAndSlopeContinueTheEarlierSourceAcrossAHole) {
    const auto earlier = std::make_shared<Shaped>(Tilted, AroundHole, kSquare);
    const auto later = std::make_shared<Shaped>(
        [](double x, double /*y*/) { return 0.02 * x + 7.5; }, Everywhere, RasterGrid{});
    const SourceChain raw({earlier, later});
    EXPECT_GT(std::abs(raw.HeightAt(3.001, 0.3, 0.25) - raw.HeightAt(2.999, 0.3, 0.25)), 0.4f)
        << "a step at the hole's edge";
    for (const ChainSeamMode mode : {ChainSeamMode::Height, ChainSeamMode::Slope}) {
        const auto chain = Chain(earlier, later, Seams(mode, 100.0));
        for (double y = -2.875; y < 3.0; y += 0.25) {
            for (double x = -2.875; x < 3.0; x += 0.25) {
                ASSERT_NEAR(chain->HeightAt(x, y, 0.25), Tilted(x, y), 1e-3)
                    << x << "," << y << ": a linear difference continues across the hole";
            }
            EXPECT_NEAR(chain->HeightAt(3.001, y, 0.25), chain->HeightAt(2.999, y, 0.25), 2e-3)
                << "heights meet at the edge";
            EXPECT_NEAR(chain->HeightAt(-3.001, y, 0.25), chain->HeightAt(-2.999, y, 0.25), 2e-3);
        }
    }
}

TEST(SourceChainSeams, SlopeMatchesTheEdgesSlopeWhereHeightKinksIt) {
    const auto earlier = std::make_shared<Shaped>(
        [](double x, double /*y*/) { return 0.2 * x + 8.0; }, InSquare, kSquare);
    const auto later =
        std::make_shared<terraingen::FlatSource>(1, 1, 1.0, 0.0f, terraingen::Vec2d{});
    const double y = 0.25;
    const double step = 0.125;
    const auto measure = [&](ChainSeamMode mode) {
        const auto chain = Chain(earlier, later, Seams(mode, 10.0));
        const auto at = [&](double x) { return static_cast<double>(chain->HeightAt(x, y, step)); };
        const double inside = (at(16.0 - step) - at(16.0 - 2.0 * step)) / step;
        const double outside = (at(16.0 + 2.0 * step) - at(16.0 + step)) / step;
        return std::pair<double, double>{std::abs(at(16.001) - at(15.999)),
                                         std::abs(outside - inside)};
    };
    const auto [heightJump, heightKink] = measure(ChainSeamMode::Height);
    const auto [slopeJump, slopeKink] = measure(ChainSeamMode::Slope);
    EXPECT_LT(heightJump, 1.0) << "of an 11.2 m step";
    EXPECT_LT(slopeJump, 0.3);
    EXPECT_GT(heightKink, 0.9) << "height fades the shift out from the edge";
    EXPECT_LT(slopeKink, 0.3) << "slope keeps the edge's slope of 0.2";
    EXPECT_LT(slopeKink, 0.25 * heightKink);
}

TEST(SourceChainSeams, AnOpenEdgesShiftFadesOutAndStopsAtItsReach) {
    const auto earlier =
        std::make_shared<Shaped>([](double, double) { return 8.0; }, InSquare, kSquare);
    const auto later =
        std::make_shared<terraingen::FlatSource>(1, 1, 1.0, 0.0f, terraingen::Vec2d{});
    const double y = 0.25;
    for (const ChainSeamMode mode : {ChainSeamMode::Height, ChainSeamMode::Slope}) {
        const auto chain = Chain(earlier, later, Seams(mode, 4.0));
        const auto at = [&](double x) { return chain->HeightAt(x, y, 0.5); };
        EXPECT_GT(at(16.25), 5.0f) << "near the edge most of the 8 m";
        EXPECT_LT(at(20.0), at(16.25));
        EXPECT_GT(at(20.0), 0.0f);
        EXPECT_LT(at(24.0), at(20.0));
        for (double x = 30.0; x < 45.0; x += 0.25) {
            ASSERT_EQ(at(x), 0.0f) << x << ": nothing from three decay lengths on";
            ASSERT_EQ(at(-x), 0.0f);
            ASSERT_EQ(chain->HeightAt(y, x, 0.5), 0.0f);
        }
    }
    const auto height = Chain(earlier, later, Seams(ChainSeamMode::Height, 4.0));
    EXPECT_LT(height->HeightAt(20.0, y, 0.5), 0.6f * 8.0f) << "a decay length out";
}

TEST(SourceChainSeams, FeatherFadesTheEdgesDifferenceOutAcrossItsWidth) {
    const auto earlier =
        std::make_shared<Shaped>([](double, double) { return 8.0; }, InSquare, kSquare);
    const auto later =
        std::make_shared<terraingen::FlatSource>(1, 1, 1.0, 0.0f, terraingen::Vec2d{});
    const auto chain = Chain(earlier, later, Seams(ChainSeamMode::Feather, 4.0, 6.0));
    const auto at = [&](double x) { return chain->HeightAt(x, 0.25, 0.5); };
    EXPECT_GT(at(16.25), 5.0f);
    EXPECT_LT(at(16.25), 8.01f);
    EXPECT_LT(at(19.0), at(16.25));
    EXPECT_GT(at(19.0), 0.0f);
    for (double x = 24.0; x < 40.0; x += 0.25) {
        ASSERT_EQ(at(x), 0.0f) << x << ": nothing beyond the band";
    }
    EXPECT_EQ(at(0.0), 8.0f) << "the earlier source is kept where it has data";
}

TEST(SourceChainSeams, MatchesEachSourceToTheChainBeforeIt) {
    const auto site = std::make_shared<Shaped>(
        [](double, double) { return 10.0; },
        [](double x, double y) { return std::abs(x) < 8.0 && std::abs(y) < 8.0; },
        RasterGrid{16, 16, GeoTransform::NorthUp(-8.0, 8.0, 1.0)});
    const auto region = std::make_shared<Shaped>(
        [](double, double) { return 2.0; },
        [](double x, double y) { return std::abs(x) < 32.0 && std::abs(y) < 32.0; },
        RasterGrid{32, 32, GeoTransform::NorthUp(-32.0, 32.0, 2.0)});
    const auto datum =
        std::make_shared<terraingen::FlatSource>(1, 1, 1.0, -5.0f, terraingen::Vec2d{});
    const SourceChain chain({site, region, datum}, Seams(ChainSeamMode::Slope, 6.0));
    ASSERT_EQ(chain.Sources().size(), 3u);
    const double y = 0.25;
    const auto at = [&](double x) { return chain.HeightAt(x, y, 0.5); };
    EXPECT_EQ(at(0.0), 10.0f);
    EXPECT_LT(std::abs(at(8.001) - at(7.999)), 0.8f) << "of the 8 m step from the site";
    EXPECT_LT(std::abs(at(32.001) - at(31.999)), 0.7f) << "of the 7 m step from the region";
    EXPECT_EQ(at(-29.5), 2.0f) << "the region beyond the site's seam";
    EXPECT_EQ(at(60.0), -5.0f) << "the datum beyond the region's seam";
    Raster<float> heights(40, 2);
    heights.SetTransform(GeoTransform::NorthUp(-40.0, 0.5, 2.0));
    chain.Fill(heights, 0.5);
    for (std::size_t col = 0; col < heights.Width(); ++col) {
        const auto center = heights.CellCenter(col, 0);
        EXPECT_EQ(heights(col, 0), chain.HeightAt(center.x, center.y, 0.5)) << col;
    }
}

TEST(SourceChainSeams, RejectsBadOptionsAndLatticesTooLarge) {
    const auto earlier = std::make_shared<Shaped>(Tilted, InSquare, kSquare);
    const auto later =
        std::make_shared<terraingen::FlatSource>(1, 1, 1.0, 0.0f, terraingen::Vec2d{});
    EXPECT_THROW(Chain(earlier, later, Seams(ChainSeamMode::Height, 0.0)), std::invalid_argument);
    EXPECT_THROW(Chain(earlier, later, Seams(ChainSeamMode::Feather, 4.0, -1.0)),
                 std::invalid_argument);
    EXPECT_THROW(Chain(earlier, later, Seams(ChainSeamMode::None, std::nan(""))),
                 std::invalid_argument);
    EXPECT_THROW(Chain(earlier, later, Seams(ChainSeamMode::Slope, 1e12)), std::invalid_argument);
    const auto unbounded = std::make_shared<Shaped>(Wavy, Everywhere, RasterGrid{});
    const auto chain = Chain(unbounded, later, Seams(ChainSeamMode::Slope, 1e12));
    EXPECT_EQ(chain->HeightAt(1.0, 2.0, 1.0), unbounded->HeightAt(1.0, 2.0, 1.0))
        << "a source without bounds has no lattice to solve on";
    EXPECT_NO_THROW(terraingen::ValidateChainSeams(ChainSeams{}));
}

TEST(SourceChainSeams, WidensTheGridBeyondTheSeamsReach) {
    EXPECT_EQ(terraingen::SeamReach(Seams(ChainSeamMode::None, 4.0, 6.0)), 0.0);
    EXPECT_EQ(terraingen::SeamReach(Seams(ChainSeamMode::Feather, 4.0, 6.0)), 6.0);
    EXPECT_EQ(terraingen::SeamReach(Seams(ChainSeamMode::Height, 4.0, 6.0)), 12.0);
    EXPECT_EQ(terraingen::SeamReach(Seams(ChainSeamMode::Slope, 4.0, 6.0)), 12.0);
    const RasterGrid lattice = terraingen::SeamLattice(kSquare, Seams(ChainSeamMode::Height, 4.0));
    EXPECT_EQ(lattice.width, 62u) << "12 posts of reach and 3 more each side";
    EXPECT_EQ(lattice.height, 62u);
    EXPECT_EQ(lattice.transform, GeoTransform::NorthUp(-31.0, 31.0, 1.0));
    const RasterGrid feather =
        terraingen::SeamLattice(kSquare, Seams(ChainSeamMode::Feather, 4.0, 2.5));
    EXPECT_EQ(feather.width, 32u + 2u * 6u);
    EXPECT_THROW(terraingen::SeamLattice(RasterGrid{4, 4, GeoTransform{0, 0, 0, 0, 0, 0}},
                                         Seams(ChainSeamMode::Height, 4.0)),
                 std::invalid_argument);
}
