#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

using terraingen::ApplyCraters;
using terraingen::Crater;
using terraingen::CraterList;
using terraingen::FassettThomsonProfile;
using terraingen::GeoTransform;
using terraingen::Raster;
using terraingen::RimSmoothing;
using terraingen::SmoothCraterRims;

namespace {

/// A flat 1 m raster centred on the origin with one 40 m fresh crater applied at the centre.
Raster<float> Cratered(CraterList& craters) {
    Raster<float> raster(120, 120, 0.0f);
    raster.SetTransform(GeoTransform::NorthUp(-60.0, 60.0, 1.0));
    craters.Append({0.5, 0.5, 40.0, 0.0, 1u});
    ApplyCraters(raster, craters, FassettThomsonProfile(),
                 terraingen::NoDegradation(terraingen::LunarFreshCraterMorphometry));
    return raster;
}

float RimCrest(const Raster<float>& raster) {
    float crest = -1e9f;
    for (std::size_t col = 60; col < 85; ++col) {
        crest = std::max(crest, raster(col, 59));
    }
    return crest;
}

}  // namespace

TEST(SmoothCraterRims, LowersTheRimCrestAndLeavesTheRestAlone) {
    CraterList craters;
    const Raster<float> before = Cratered(craters);
    Raster<float> after = before;
    SmoothCraterRims(after, craters, {.sigmaRadii = 0.08});
    EXPECT_LT(RimCrest(after), RimCrest(before) - 0.02f);
    EXPECT_EQ(after(60, 59), before(60, 59));
    EXPECT_EQ(after(66, 59), before(66, 59));
    EXPECT_EQ(after(110, 59), before(110, 59));
    EXPECT_EQ(after(60 + 25, 59), before(60 + 25, 59));
}

TEST(SmoothCraterRims, DoesNothingWithoutStrengthSigmaOrResolution) {
    CraterList craters;
    const Raster<float> before = Cratered(craters);
    for (const RimSmoothing& smoothing :
         {RimSmoothing{.sigmaRadii = 0.0}, RimSmoothing{.strength = 0.0},
          RimSmoothing{.fade = {100.0, 200.0}}}) {
        Raster<float> after = before;
        SmoothCraterRims(after, craters, smoothing);
        EXPECT_TRUE(
            std::equal(after.Values().begin(), after.Values().end(), before.Values().begin()));
    }
}

TEST(SmoothCraterRims, RejectsInvalidSettings) {
    CraterList craters;
    Raster<float> raster = Cratered(craters);
    EXPECT_THROW(SmoothCraterRims(raster, craters, {.sigmaRadii = -0.1}), std::invalid_argument);
    EXPECT_THROW(SmoothCraterRims(raster, craters, {.innerRadii = 1.0}), std::invalid_argument);
    EXPECT_THROW(SmoothCraterRims(raster, craters, {.outerRadii = 1.0}), std::invalid_argument);
    EXPECT_THROW(SmoothCraterRims(raster, craters, {.strength = 2.0}), std::invalid_argument);
    EXPECT_THROW(SmoothCraterRims(raster, craters, {.fade = {-1.0, 1.0}}), std::invalid_argument);
    EXPECT_THROW(SmoothCraterRims(raster, craters, {}, -1.0), std::invalid_argument);
}

TEST(RimSmoothingFilter, SmoothsTheRimsOfThePlacedCraters) {
    CraterList craters;
    Raster<float> heights = Cratered(craters);
    Raster<float> expected = heights;
    terraingen::TerrainFeatures features;
    features.craters = craters;
    const RimSmoothing smoothing{.sigmaRadii = 0.08};
    terraingen::RimSmoothingFilter(smoothing).Apply(heights, 1.0, features);
    SmoothCraterRims(expected, craters, smoothing);
    for (std::size_t i = 0; i < expected.Size(); ++i) {
        ASSERT_EQ(heights.Values()[i], expected.Values()[i]) << i;
    }
    EXPECT_EQ(terraingen::RimSmoothingFilter(smoothing).Smoothing(), smoothing);
}

TEST(RimSmoothingFilter, LeavesCratersTooSmallForTheSpacingAndRejectsBadSettings) {
    CraterList craters;
    Raster<float> heights = Cratered(craters);
    const Raster<float> original = heights;
    terraingen::TerrainFeatures features;
    features.craters = craters;
    terraingen::RimSmoothingFilter({.sigmaRadii = 0.08}).Apply(heights, 20.0, features);
    EXPECT_TRUE(
        std::equal(heights.Values().begin(), heights.Values().end(), original.Values().begin()))
        << "a 40 m crater is under 3 samples of 20 m";
    EXPECT_THROW(terraingen::RimSmoothingFilter({.strength = 2.0}), std::invalid_argument);
}

TEST(SmoothCraterRims, SmoothsFromTheUnsmoothedTerrainOnAnyNumberOfThreads) {
    CraterList craters;
    Raster<float> raster = Cratered(craters);
    craters.Append({20.5, 0.5, 30.0, 0.0, 2u});
    ApplyCraters(raster, CraterList(), FassettThomsonProfile(),
                 terraingen::NoDegradation(terraingen::LunarFreshCraterMorphometry));
    Raster<float> serial = raster;
    SmoothCraterRims(serial, craters, {.sigmaRadii = 0.08}, 0.0, 1);
    for (const unsigned threads : {2u, 0u}) {
        Raster<float> parallel = raster;
        SmoothCraterRims(parallel, craters, {.sigmaRadii = 0.08}, 0.0, threads);
        EXPECT_TRUE(std::equal(parallel.Values().begin(), parallel.Values().end(),
                               serial.Values().begin()));
    }
    EXPECT_EQ(terraingen::RimSmoothingFilter({.sigmaRadii = 0.0}).Margin(40.0, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(terraingen::RimSmoothingFilter({.sigmaRadii = 0.1}).Margin(40.0, 1.0),
                     (1.2 + 0.3) * 20.0 + 2.0);
}

TEST(SmoothCraterRims, IsRoundInMetresOnASphere) {
    const terraingen::Surface moon = terraingen::Surface::Sphere(1737400.0);
    using terraingen::TerrainFeatures;
    using terraingen::Vec2d;
    terraingen::CraterLayerOptions options;
    options.generate = false;
    options.surface = moon;
    options.generation.manual.Append({10.0, 70.0, 200.0, 0.5, 5u});
    const terraingen::CraterLayer craters(
        {std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
         std::make_shared<terraingen::UniformAges>(1.0),
         std::make_shared<terraingen::NoDegradation>(terraingen::LunarFreshCraterMorphometry),
         std::make_shared<terraingen::FassettThomsonProfile>()},
        options);
    Raster<float> heights(450, 150, 0.0f);
    const double step = 2.0 / moon.UnitLength();
    heights.SetTransform(GeoTransform::NorthUp(10.0 - 225.0 * step, 70.0 + 75.0 * step, step));
    TerrainFeatures features;
    craters.Apply(heights, {.spacing = 2.0}, features);
    const Raster<float> sharp = heights;
    terraingen::RimSmoothing smoothing;
    smoothing.sigmaRadii = 0.2;
    terraingen::SmoothCraterRims(heights, features.craters, smoothing, 2.0, 2, moon);
    float sharpCrest = -1e9f;
    float smoothCrest = -1e9f;
    double changedEast = 0.0;
    double changedNorth = 0.0;
    const Vec2d centre{10.0, 70.0};
    for (std::size_t row = 0; row < heights.Height(); ++row) {
        for (std::size_t col = 0; col < heights.Width(); ++col) {
            sharpCrest = std::max(sharpCrest, sharp(col, row));
            smoothCrest = std::max(smoothCrest, heights(col, row));
            if (heights(col, row) != sharp(col, row)) {
                const Vec2d offset = moon.Offset(centre, heights.CellCenter(col, row));
                changedEast = std::max(changedEast, std::fabs(offset.x));
                changedNorth = std::max(changedNorth, std::fabs(offset.y));
            }
        }
    }
    EXPECT_LT(smoothCrest, sharpCrest);
    EXPECT_NEAR(changedEast, changedNorth, 6.0);
    EXPECT_LT(changedEast, 1.2 * 100.0 + 3.0);
}
