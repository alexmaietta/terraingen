#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <stdexcept>

using terraingen::Crater;
using terraingen::CraterMorphometry;
using terraingen::CraterState;
using terraingen::DiffusedProfile;
using terraingen::DiffusionDegradation;
using terraingen::FassettThomsonProfile;
using terraingen::FassettThomsonRim;
using terraingen::PowerLawDiffusivity;

namespace {

/// A Gaussian bump a D exp(-x^2 / (2 s^2)), x = r / D, whose radial diffusion is known exactly.
class Gaussian final : public terraingen::CraterProfile {
  public:
    double Height(double distance, double diameter, const CraterState&) const override {
        const double x = distance / diameter;
        return diameter * kAmplitude * std::exp(-x * x / (2.0 * kSigma * kSigma));
    }
    double ReachRadii() const override {
        return 3.0;
    }
    static constexpr double kAmplitude = 0.2;
    static constexpr double kSigma = 0.1;
};

double DepthKept(const DiffusedProfile& profile, const CraterMorphometry& fresh, double diameter,
                 double kappaT) {
    const CraterMorphometry worn =
        profile.Morphometry(fresh, diameter, kappaT / (diameter * diameter));
    const CraterMorphometry start = profile.Morphometry(fresh, diameter, 1e-9);
    return worn.depthRatio / start.depthRatio;
}

}  // namespace

TEST(DiffusedProfile, MatchesTheExactRadialDiffusionOfAGaussian) {
    const DiffusedProfile profile(std::make_shared<Gaussian>(), 3.0, 600);
    const double diameter = 250.0;
    for (const double tau : {1e-4, 2e-3, 1e-2, 3e-2}) {
        const CraterState state{{}, {}, tau};
        const double width = Gaussian::kSigma * Gaussian::kSigma + 2.0 * tau;
        for (const double x : {0.0, 0.05, 0.13, 0.3}) {
            const double exact = diameter * Gaussian::kAmplitude * Gaussian::kSigma *
                                 Gaussian::kSigma / width * std::exp(-x * x / (2.0 * width));
            EXPECT_NEAR(profile.Height(x * diameter, diameter, state), exact,
                        2e-3 * diameter * Gaussian::kAmplitude)
                << tau << " " << x;
        }
    }
}

TEST(DiffusedProfile, IsTheFreshProfileAtAgeZeroAndChangesContinuously) {
    const auto fresh = std::make_shared<FassettThomsonProfile>(FassettThomsonRim::Plateau);
    const DiffusedProfile profile(fresh);
    const CraterMorphometry morphometry = terraingen::LunarFreshCraterMorphometry(800.0);
    const CraterState start{morphometry, morphometry, 0.0};
    for (const double r : {0.0, 150.0, 390.0, 500.0}) {
        EXPECT_EQ(profile.Height(r, 800.0, start), fresh->Height(r, 800.0, start));
        EXPECT_NEAR(profile.Height(r, 800.0, {morphometry, morphometry, 1e-7}),
                    fresh->Height(r, 800.0, start), 0.02 * 800.0 * 0.01);
    }
    EXPECT_EQ(profile.Height(1300.0, 800.0, {morphometry, morphometry, 0.01}), 0.0);
    EXPECT_EQ(profile.ReachRadii(), 3.0);
    EXPECT_FALSE(profile.IsComplex(800.0));
    EXPECT_EQ(profile.Morphometry(morphometry, 800.0, 0.0), morphometry);
    const auto& ages = profile.Ages();
    EXPECT_EQ(ages.front(), 0.0);
    EXPECT_DOUBLE_EQ(ages.back(), 0.5);
    double previous = profile.Morphometry(morphometry, 800.0, 1e-6).depthRatio;
    for (const double tau : {1e-5, 1e-4, 1e-3, 1e-2, 0.1, 0.5, 2.0}) {
        const double depth = profile.Morphometry(morphometry, 800.0, tau).depthRatio;
        EXPECT_LE(depth, previous + 1e-12) << tau;
        previous = depth;
    }
    EXPECT_LT(previous, 0.01);
}

TEST(DiffusedProfile, WearsCratersAsFassettAndThomsonFound) {
    const DiffusedProfile profile(std::make_shared<FassettThomsonProfile>());
    const CraterMorphometry fresh{0.218, 0.034};
    const double kappaT = 5.5 * 3000.0;
    EXPECT_NEAR(DepthKept(profile, fresh, 1000.0, kappaT), 0.52, 0.03);
    EXPECT_NEAR(DepthKept(profile, fresh, 300.0, kappaT), 0.07, 0.03);
    EXPECT_NEAR(terraingen::FassettThomson2014DiffusionAge(0.5), 5750.0, 25.0);
    EXPECT_NEAR(terraingen::FassettThomson2014DiffusionAge(1.0), 8750.0, 25.0);
    EXPECT_EQ(terraingen::FassettThomson2014DiffusionAge(0.0), 0.0);
}

TEST(DiffusedProfile, ReachesTheVisibilityCutoffAtFassettEtAl2022Lifetimes) {
    const auto fresh = std::make_shared<FassettThomsonProfile>(FassettThomsonRim::Plateau);
    const DiffusedProfile profile(fresh);
    const PowerLawDiffusivity hartmann = terraingen::Fassett2022HartmannDiffusivity();
    for (const auto& [diameter, lifetimeMyr] :
         {std::pair{10.0, 50.8}, std::pair{40.0, 284.0}, std::pair{128.0, 1337.9}}) {
        const CraterMorphometry morphometry = terraingen::LunarFreshCraterMorphometry(diameter);
        const double kappaT = hartmann(diameter, lifetimeMyr / 1000.0);
        const double depth =
            profile.Morphometry(morphometry, diameter, kappaT / (diameter * diameter)).depthRatio;
        EXPECT_NEAR(depth, 0.04, 0.012) << diameter;
    }
}

TEST(PowerLawDiffusivity, ScalesWithDiameterAboveItsFloor) {
    const PowerLawDiffusivity trask = terraingen::Fassett2022TraskDiffusivity();
    EXPECT_NEAR(trask.Diffusivity(1000.0), 3.1, 1e-12);
    EXPECT_NEAR(trask.Diffusivity(100.0), 3.1 * std::pow(0.1, 1.05), 1e-12);
    EXPECT_EQ(trask.Diffusivity(1.0), trask.Diffusivity(5.0));
    EXPECT_NEAR(trask(1000.0, 2.0), 3.1 * 2000.0, 1e-9);
    EXPECT_EQ(terraingen::FassettThomson2014Diffusivity()(300.0, 1.0), 5500.0);
    EXPECT_NO_THROW(terraingen::ValidatePowerLawDiffusivity(trask));
    EXPECT_THROW(terraingen::ValidatePowerLawDiffusivity({0.0, 1.0, 0.0}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidatePowerLawDiffusivity({1.0, NAN, 0.0}), std::invalid_argument);
    EXPECT_THROW(terraingen::ValidatePowerLawDiffusivity({1.0, 1.0, -1.0}), std::invalid_argument);
}

TEST(DiffusionDegradation, GivesEachCraterItsDiffusionAgeAndWornShape) {
    const auto profile = std::make_shared<DiffusedProfile>(
        std::make_shared<FassettThomsonProfile>(FassettThomsonRim::Plateau));
    const DiffusionDegradation degradation(terraingen::LunarFreshCraterMorphometry,
                                           terraingen::Fassett2022HartmannDiffusivity(), profile);
    const Crater crater{0.0, 0.0, 60.0, 0.4, 1u};
    const CraterState state = degradation.State(crater);
    const double kappaT = terraingen::Fassett2022HartmannDiffusivity()(60.0, 0.4);
    EXPECT_DOUBLE_EQ(state.diffusion, kappaT / 3600.0);
    EXPECT_EQ(state.fresh, terraingen::LunarFreshCraterMorphometry(60.0));
    EXPECT_LT(state.current.depthRatio, state.fresh.depthRatio);
    EXPECT_EQ(state.current, profile->Morphometry(state.fresh, 60.0, state.diffusion));
    const CraterState young = degradation.State({0.0, 0.0, 60.0, 0.0, 1u});
    EXPECT_EQ(young.current, young.fresh);
    EXPECT_THROW(degradation.State({0.0, 0.0, 0.0, 1.0, 1u}), std::domain_error);
    EXPECT_THROW(degradation.State({0.0, 0.0, 10.0, -1.0, 1u}), std::domain_error);
    const DiffusionDegradation broken(
        terraingen::LunarFreshCraterMorphometry, [](double, double) { return -1.0; }, profile);
    EXPECT_THROW(broken.State(crater), std::domain_error);
    EXPECT_THROW(DiffusionDegradation({}, terraingen::Fassett2022HartmannDiffusivity(), profile),
                 std::invalid_argument);
    EXPECT_THROW(DiffusionDegradation(terraingen::LunarFreshCraterMorphometry,
                                      terraingen::Fassett2022HartmannDiffusivity(), nullptr),
                 std::invalid_argument);
    EXPECT_THROW(DiffusedProfile(nullptr), std::invalid_argument);
    EXPECT_THROW(DiffusedProfile(std::make_shared<FassettThomsonProfile>(), 1.0),
                 std::invalid_argument);
    EXPECT_THROW(DiffusedProfile(std::make_shared<FassettThomsonProfile>(), 3.0, 10),
                 std::invalid_argument);
}

TEST(DiffusionDegradation, CraterLayersAgreeOnGridsAndPoints) {
    const auto profile = std::make_shared<DiffusedProfile>(
        std::make_shared<FassettThomsonProfile>(FassettThomsonRim::Plateau));
    terraingen::CraterLayerOptions options;
    options.seed = 8;
    options.generation.dMin = 6.0;
    options.generation.dMax = 60.0;
    const terraingen::CraterLayer layer({std::make_shared<terraingen::PowerLawSfd>(0.05, 2.0),
                                         std::make_shared<terraingen::UniformAges>(1.0),
                                         std::make_shared<DiffusionDegradation>(
                                             terraingen::LunarFreshCraterMorphometry,
                                             terraingen::Fassett2022HartmannDiffusivity(), profile),
                                         profile},
                                        options);
    terraingen::Raster<float> heights(64, 64, 0.0f);
    heights.SetTransform(terraingen::GeoTransform::NorthUp(-64.0, 64.0, 2.0));
    layer.Apply(heights, {.spacing = 2.0, .threads = 0});
    const auto craters = layer.Craters(terraingen::RasterGrid::Of(heights).Bounds(), 2.0);
    ASSERT_GT(craters.Size(), 3u);
    for (const auto& [col, row] :
         {std::pair<std::size_t, std::size_t>{10, 10}, std::pair<std::size_t, std::size_t>{40, 22},
          std::pair<std::size_t, std::size_t>{63, 63}}) {
        const auto centre = heights.CellCenter(col, row);
        EXPECT_EQ(layer.ApplyAt(0.0f, centre.x, centre.y, {2.0}), heights(col, row));
    }
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        EXPECT_LE(layer.Attributes(craters.At(n)).freshness, 1.0);
    }
}
