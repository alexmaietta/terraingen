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
