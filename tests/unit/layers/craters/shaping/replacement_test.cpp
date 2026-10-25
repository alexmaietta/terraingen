// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

using terraingen::Crater;
using terraingen::CraterApplyOptions;
using terraingen::CraterDatum;
using terraingen::CraterList;
using terraingen::CraterReplacement;
using terraingen::CraterState;
using terraingen::FassettThomsonProfile;
using terraingen::GeoTransform;
using terraingen::NoDegradation;
using terraingen::Raster;

namespace {

const NoDegradation kFresh(terraingen::LunarFreshCraterMorphometry);
const FassettThomsonProfile kProfile;

/// `terraingen::ApplyCraters` with fresh lunar Fassett-Thomson craters on one thread.
void Apply(Raster<float>& raster, const CraterList& craters, const CraterApplyOptions& options = {},
           const terraingen::HeightSampler& ground = {}) {
    const terraingen::CraterShapeModels models{
        std::shared_ptr<const terraingen::CraterProfile>(
            std::shared_ptr<const terraingen::CraterProfile>{}, &kProfile),
        std::shared_ptr<const terraingen::DegradationModel>(
            std::shared_ptr<const terraingen::DegradationModel>{}, &kFresh)};
    terraingen::ApplyCraters(raster, craters, models, options, {0.0, 1, ground});
}

/// The tilted plane the tests' terrain lies on.
double Ground(double x, double y) {
    return 3.0 + 0.05 * x - 0.02 * y;
}

/// A 200 x 200 raster of 1 m pixels centered on the origin, on `Ground`.
Raster<float> Tilted() {
    Raster<float> raster(200, 200, 0.0f);
    raster.SetTransform(GeoTransform::NorthUp(-100.0, 100.0, 1.0));
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            const auto world = raster.Transform().PixelToWorld(static_cast<double>(col) + 0.5,
                                                               static_cast<double>(row) + 0.5);
            raster(col, row) = static_cast<float>(Ground(world.x, world.y));
        }
    }
    return raster;
}

/// `raster` averaged over 9 x 9 pixel boxes, as a coarse DEM's interpolation softens craters.
Raster<float> Blurred(const Raster<float>& raster) {
    Raster<float> blurred = raster;
    constexpr int kHalf = 4;
    const int width = static_cast<int>(raster.Width());
    const int height = static_cast<int>(raster.Height());
    for (int row = kHalf; row < height - kHalf; ++row) {
        for (int col = kHalf; col < width - kHalf; ++col) {
            double sum = 0.0;
            for (int j = -kHalf; j <= kHalf; ++j) {
                for (int i = -kHalf; i <= kHalf; ++i) {
                    sum += static_cast<double>(raster(static_cast<std::size_t>(col + i),
                                                      static_cast<std::size_t>(row + j)));
                }
            }
            blurred(static_cast<std::size_t>(col), static_cast<std::size_t>(row)) =
                static_cast<float>(sum / ((2 * kHalf + 1) * (2 * kHalf + 1)));
        }
    }
    return blurred;
}

/// The height of `raster` at world (`x`, `y`).
float At(const Raster<float>& raster, double x, double y) {
    const auto pixel = raster.Transform().WorldToPixel(x, y);
    return raster(static_cast<std::size_t>(pixel.x), static_cast<std::size_t>(pixel.y));
}

/// The fresh profile of a crater `diameter` across at `distance` from its center.
double Fresh(double distance, double diameter) {
    const CraterState state = kFresh.State(Crater{0.0, 0.0, diameter, 0.0, 0u});
    return kProfile.Height(distance, diameter, state);
}

/// A tilted terrain holding a shallow crater 40 m across at the origin, blurred.
Raster<float> BlurredCrater() {
    Raster<float> raster = Tilted();
    Apply(raster, {terraingen::ManualCrater(0.0, 0.0, 40.0).Depth(2.0)});
    return Blurred(raster);
}

/// A fresh crater 40 m across at the origin that replaces the terrain.
CraterList Replacing() {
    Crater crater{0.0, 0.0, 40.0, 0.0, 7u};
    crater.Replace();
    return {crater};
}

}  // namespace

TEST(CraterReplacement, SwapsABlurredCraterForASharpOne) {
    Raster<float> raster = BlurredCrater();
    Apply(raster, Replacing());
    for (const double x : {0.5, 5.5, 12.5, 19.5}) {
        const double expected = Ground(x, 0.5) + Fresh(std::hypot(x, 0.5), 40.0);
        EXPECT_NEAR(At(raster, x, 0.5), expected, 2e-3) << x;
    }
}

TEST(CraterReplacement, LeavesTheTerrainBeyondTheProfilesReach) {
    const Raster<float> before = BlurredCrater();
    Raster<float> raster = before;
    Apply(raster, Replacing());
    for (const double x : {30.5, 40.5, 60.5}) {
        EXPECT_EQ(At(raster, x, 0.5), At(before, x, 0.5)) << x;
    }
}

TEST(CraterReplacement, AddsOntoTheBlurredCraterWithoutReplace) {
    Raster<float> raster = BlurredCrater();
    Apply(raster, {Crater{0.0, 0.0, 40.0, 0.0, 7u}});
    const double sharp = Ground(0.5, 0.5) + Fresh(std::hypot(0.5, 0.5), 40.0);
    EXPECT_LT(At(raster, 0.5, 0.5), sharp - 0.5);
}

TEST(CraterReplacement, IgnoresTheLayersDatumAndInteriorReplacement) {
    Raster<float> plain = BlurredCrater();
    Apply(plain, Replacing());
    Raster<float> datum = BlurredCrater();
    CraterApplyOptions options;
    options.datum = CraterDatum::MeanHeight;
    options.interiorReplacement = 0.0;
    Apply(datum, Replacing(), options);
    for (const double x : {0.5, 10.5, 19.5}) {
        EXPECT_NEAR(At(datum, x, 0.5), At(plain, x, 0.5), 1e-4) << x;
    }
}

TEST(CraterReplacement, FitsItsDatumToTheGroundWhenGiven) {
    Raster<float> raster = BlurredCrater();
    Apply(raster, Replacing(), {},
          [](double x, double y) { return static_cast<float>(Ground(x, y) + 10.0); });
    const double expected = Ground(0.5, 0.5) + 10.0 + Fresh(std::hypot(0.5, 0.5), 40.0);
    EXPECT_NEAR(At(raster, 0.5, 0.5), expected, 2e-3);
}

TEST(CraterReplacement, BlendsOutBetweenItsStartAndEnd) {
    Raster<float> raster = BlurredCrater();
    CraterApplyOptions options;
    options.replacement.startRadii = 0.5;
    options.replacement.endRadii = 0.6;
    const Raster<float> before = raster;
    Apply(raster, Replacing(), options);
    const double x = 15.5;
    const double added = static_cast<double>(At(before, x, 0.5)) + Fresh(std::hypot(x, 0.5), 40.0);
    EXPECT_NEAR(At(raster, x, 0.5), added, 1e-4);
}

TEST(FitReplacementDatum, FitsTheRingAroundTheCraterNotItsInterior) {
    const Crater crater{10.0, -5.0, 20.0, 0.0, 1u};
    const auto ground = [&](double x, double y) {
        const double r = std::hypot(x - crater.x, y - crater.y);
        return static_cast<float>(Ground(x, y) - (r < 15.0 ? 4.0 : 0.0));
    };
    const auto plane = terraingen::FitReplacementDatum(crater, {}, 1.5, 1.0, ground);
    ASSERT_TRUE(plane.has_value());
    EXPECT_NEAR(plane->a, Ground(crater.x, crater.y), 1e-6);
    EXPECT_NEAR(plane->b, 0.05, 1e-7);
    EXPECT_NEAR(plane->c, -0.02, 1e-7);
}

TEST(FitReplacementDatum, GivesNothingWithoutGround) {
    const auto none = [](double, double) { return std::numeric_limits<float>::quiet_NaN(); };
    EXPECT_FALSE(terraingen::FitReplacementDatum({0.0, 0.0, 20.0}, {}, 1.5, 1.0, none));
}

TEST(CraterReplacement, EndsAtTheProfilesReachAtMost) {
    CraterReplacement replacement;
    EXPECT_EQ(replacement.EndRadii(1.5), 1.5);
    replacement.endRadii = 1.2;
    EXPECT_EQ(replacement.EndRadii(1.5), 1.2);
    replacement.endRadii = 3.0;
    EXPECT_EQ(replacement.EndRadii(2.0), 2.0);
}

TEST(ReplacementMargin, CoversTheFitRingAndTheProfilesReach) {
    EXPECT_DOUBLE_EQ(terraingen::ReplacementMargin({}, kProfile, 10.0), (1.5 + 1.5 + 1.0) * 5.0);
    EXPECT_EQ(terraingen::ReplacementMargin({}, kProfile, 0.0), 0.0);
}

TEST(CraterReplacement, RejectsInvalidSettings) {
    const auto rejects = [](CraterReplacement replacement) {
        EXPECT_THROW(terraingen::ValidateCraterReplacement(replacement), std::invalid_argument);
        CraterApplyOptions options;
        options.replacement = replacement;
        EXPECT_THROW(terraingen::ValidateCraterApplyOptions(options), std::invalid_argument);
    };
    CraterReplacement start;
    start.startRadii = 0.0;
    rejects(start);
    CraterReplacement end;
    end.endRadii = 1.0;
    rejects(end);
    CraterReplacement infinite;
    infinite.endRadii = std::numeric_limits<double>::infinity();
    rejects(infinite);
    CraterReplacement width;
    width.fitWidthRadii = 0.0;
    rejects(width);
    EXPECT_NO_THROW(terraingen::ValidateCraterReplacement({}));
}
