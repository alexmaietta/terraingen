// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/sand_fill.h"

#include "terraingen/layers/bedforms.h"
#include "terraingen/layers/dunes.h"
#include "terraingen/layers/material.h"
#include "terraingen/raster/grid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <numeric>
#include <stdexcept>

using terraingen::MaterialClass;
using terraingen::SandFillLayer;
using terraingen::SandFillOptions;

namespace {

/// A Gaussian pit (`depth` < 0) or hill (`depth` > 0) of width `width` at the origin.
class Mound final : public terraingen::Ground {
  public:
    Mound(double depth, double width) : depth_(depth), width_(width) {}

  protected:
    void FillSamples(terraingen::SampleBlock& samples, double,
                     unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const terraingen::Vec2d p = samples.Position(i);
            const double r2 = (p.x * p.x + p.y * p.y) / (width_ * width_);
            samples.Height(i) = static_cast<float>(depth_ * std::exp(-r2));
        }
    }

  private:
    double depth_;
    double width_;
};

double Fill(const SandFillLayer& layer, const terraingen::Ground& ground, double x, double y) {
    const float before = ground.HeightAt(x, y, 0.1);
    return static_cast<double>(layer.ApplyAt(before, x, y, {0.1, 1, &ground}) - before);
}

}  // namespace

TEST(MaterialShares, StartAsSoilAndCoverWithoutLosingTheirSum) {
    terraingen::MaterialShares shares = terraingen::SoilShares();
    EXPECT_EQ(shares[static_cast<std::size_t>(MaterialClass::Soil)], 1.0);
    terraingen::CoverWith(shares, MaterialClass::Bedrock, 0.4);
    terraingen::CoverWith(shares, MaterialClass::Sand, 0.5);
    terraingen::CoverWith(shares, MaterialClass::Sand, 2.0);
    EXPECT_DOUBLE_EQ(shares[static_cast<std::size_t>(MaterialClass::Sand)], 1.0) << "clamped";
    terraingen::CoverWith(shares, MaterialClass::Dust, 0.25);
    EXPECT_DOUBLE_EQ(std::accumulate(shares.begin(), shares.end(), 0.0), 1.0);
    EXPECT_DOUBLE_EQ(shares[static_cast<std::size_t>(MaterialClass::Sand)], 0.75);
    terraingen::CoverWith(shares, MaterialClass::Rock, -1.0);
    EXPECT_DOUBLE_EQ(shares[static_cast<std::size_t>(MaterialClass::Rock)], 0.0);
}

TEST(SandFillLayer, FillsLowsUpToItsThicknessAndLeavesRisesBare) {
    const SandFillOptions options{8.0, 0.6, 0.5, 0.02};
    const SandFillLayer layer(options);
    const Mound pit(-0.4, 3.0);
    const double center = Fill(layer, pit, 0.0, 0.0);
    EXPECT_GT(center, 0.1);
    EXPECT_LT(center, options.fill * 0.4) << "a share of the depth, approached smoothly";
    EXPECT_NEAR(layer.ThicknessAt(0.0, 0.0, {0.1, 1, &pit}), center, 1e-6);
    EXPECT_EQ(Fill(layer, Mound(0.4, 3.0), 0.0, 0.0), 0.0) << "a hill holds no sand";
    EXPECT_EQ(Fill(layer, Mound(0.0, 3.0), 2.0, 1.0), 0.0) << "nor does level ground";
    EXPECT_LE(Fill(layer, Mound(-50.0, 3.0), 0.0, 0.0), options.maxThickness);
    EXPECT_GT(Fill(layer, Mound(-50.0, 3.0), 0.0, 0.0), 0.99 * options.maxThickness);

    EXPECT_EQ(layer.Material(), MaterialClass::Sand);
    EXPECT_EQ(layer.MaterialCoverAt(0.0, 0.0, {0.1, 1, &pit}), 1.0);
    EXPECT_EQ(layer.MaterialCoverAt(20.0, 0.0, {0.1, 1, &pit}), 0.0);
    EXPECT_THROW(layer.MaterialCoverAt(0.0, 0.0, {0.1}), std::invalid_argument);
    EXPECT_THROW(layer.ApplyAt(0.0f, 0.0, 0.0, {0.1}), std::invalid_argument) << "needs ground";
    const SandFillLayer none(SandFillOptions{8.0, 0.0, 0.5, 0.02});
    EXPECT_EQ(none.ThicknessAt(0.0, 0.0, {0.1, 1, &pit}), 0.0);
    EXPECT_EQ(layer.Reseeded(3), nullptr) << "no seeds";
}

TEST(SandFillLayer, RejectsBadOptions) {
    for (const SandFillOptions& options :
         {SandFillOptions{0.0}, SandFillOptions{1.0, -0.1}, SandFillOptions{1.0, 1.5},
          SandFillOptions{1.0, 0.5, 0.0}, SandFillOptions{1.0, 0.5, 1.0, -1.0}}) {
        EXPECT_THROW(SandFillLayer{options}, std::invalid_argument);
    }
}

TEST(MaterialSource, DunesAndBedformsLaySand) {
    terraingen::DuneOptions options;
    options.shape = terraingen::DuneShape::Transverse;
    options.sinuosity = 0.0;
    options.defects = 0.0;
    const terraingen::DuneLayer dunes(options);
    EXPECT_EQ(dunes.Material(), MaterialClass::Sand);
    double on = 0.0;
    double off = 1.0;
    for (double x = 0.0; x < options.wavelength; x += 1.0) {
        const double cover = dunes.MaterialCoverAt(x, 0.0, {1.0});
        if (dunes.DuneHeightAt(x, 0.0) > 0.5 * options.height) {
            on = std::max(on, cover);
        }
        if (dunes.DuneHeightAt(x, 0.0) == 0.0) {
            off = std::min(off, cover);
        }
    }
    EXPECT_EQ(on, 1.0);
    EXPECT_EQ(off, 0.0);
    options.ripples = terraingen::MarsDuneRipples();
    options.ripples->interduneCover = 0.3;
    const terraingen::DuneLayer rippled(options);
    double interdune = 1.0;
    for (double x = 0.0; x < options.wavelength; x += 1.0) {
        if (rippled.DuneHeightAt(x, 0.0) == 0.0) {
            interdune = std::min(interdune, rippled.MaterialCoverAt(x, 0.0, {1.0}));
        }
    }
    EXPECT_DOUBLE_EQ(interdune, 0.3) << "rippled sand between the dunes";

    terraingen::BedformOptions fields;
    fields.cover = 0.5;
    fields.fieldSize = 30.0;
    const terraingen::BedformLayer bedforms(fields);
    EXPECT_EQ(bedforms.Material(), MaterialClass::Sand);
    double covered = 0.0;
    for (double x = 0.0; x < 200.0; x += 7.0) {
        const double cover = bedforms.MaterialCoverAt(x, 3.0, {1.0});
        EXPECT_GE(cover, 0.0);
        EXPECT_LE(cover, 1.0);
        EXPECT_EQ(cover, bedforms.MaterialCoverAt(x, 3.0, {0.1})) << "independent of the spacing";
        covered += cover;
    }
    EXPECT_GT(covered, 0.0);
}

TEST(MaterialSource, CoversAGridCellByCellByDefault) {
    terraingen::DuneOptions options;
    options.shape = terraingen::DuneShape::Transverse;
    const terraingen::DuneLayer dunes(options);
    const terraingen::RasterGrid grid{30, 4, terraingen::GeoTransform::NorthUp(0.0, 4.0, 10.0)};
    const terraingen::Raster<double> cover = dunes.MaterialCoverOver(grid, {10.0, 2});
    for (std::size_t col = 0; col < grid.width; ++col) {
        const terraingen::Vec2d c = cover.CellCenter(col, 1);
        EXPECT_EQ(cover(col, 1), dunes.MaterialCoverAt(c.x, c.y, {10.0}));
    }
}
