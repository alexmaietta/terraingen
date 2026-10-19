#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using terraingen::ComplexCraterProfile;
using terraingen::Crater;
using terraingen::CraterAttributes;
using terraingen::CraterProfile;
using terraingen::CraterState;
using terraingen::DegradationModel;
using terraingen::DiffusedProfile;
using terraingen::DiffusionDegradation;
using terraingen::FassettThomsonProfile;
using terraingen::FreshnessDegradation;
using terraingen::LunarFreshCraterMorphometry;
using terraingen::MartinParkesDunstanProfile;
using terraingen::NoDegradation;
using terraingen::ResolvedCraterState;

namespace {

/// A manual crater `diameter` across at the origin, `ageGyr` old, `depth` deep with rim `rim`.
Crater Manual(double diameter, double depth, double rim = std::nan(""), double ageGyr = 1.0) {
    return Crater{0.0, 0.0, diameter, ageGyr, 7u, true, depth, rim};
}

/// The attributes of `crater` in its resolved state under `profile` and `degradation`.
CraterAttributes Shaped(const CraterProfile& profile, const DegradationModel& degradation,
                        const Crater& crater) {
    return terraingen::ComputeCraterAttributes(
        crater, ResolvedCraterState(profile, degradation, crater), profile);
}

/// The message `ResolvedCraterState` throws for `crater`, or empty.
std::string Rejection(const CraterProfile& profile, const DegradationModel& degradation,
                      const Crater& crater) {
    try {
        ResolvedCraterState(profile, degradation, crater);
    } catch (const std::invalid_argument& error) {
        return error.what();
    }
    return {};
}

const NoDegradation kFresh(LunarFreshCraterMorphometry);

const FreshnessDegradation kWorn(std::make_shared<terraingen::UniformAges>(2.0),
                                 LunarFreshCraterMorphometry);

std::shared_ptr<const DiffusedProfile> Diffused() {
    static const auto profile =
        std::make_shared<DiffusedProfile>(std::make_shared<MartinParkesDunstanProfile>());
    return profile;
}

const DiffusionDegradation& Diffusion() {
    static const DiffusionDegradation degradation(
        LunarFreshCraterMorphometry, terraingen::Fassett2022HartmannDiffusivity(), Diffused());
    return degradation;
}

}  // namespace

TEST(ResolvedCraterState, IsTheModelStateWithoutExplicitValues) {
    const MartinParkesDunstanProfile mpd;
    const Crater crater{0.0, 0.0, 120.0, 1.5, 3u};
    for (const DegradationModel* degradation :
         {static_cast<const DegradationModel*>(&kFresh),
          static_cast<const DegradationModel*>(&kWorn),
          static_cast<const DegradationModel*>(&Diffusion())}) {
        const CraterProfile& profile =
            degradation == &Diffusion() ? static_cast<const CraterProfile&>(*Diffused()) : mpd;
        EXPECT_EQ(ResolvedCraterState(profile, *degradation, crater), degradation->State(crater));
        EXPECT_EQ(ResolvedCraterState(profile, *degradation, crater, 3.0),
                  degradation->WornState(crater, 3.0));
    }
}

TEST(ResolvedCraterState, GivesMartinParkesDunstanCratersTheirDepthAndRim) {
    const MartinParkesDunstanProfile mpd;
    for (const Crater& crater :
         {Manual(6.0, 0.9, 0.2), Manual(60.0, 4.0, 0.0), Manual(200.0, 30.0, 12.0, 0.0)}) {
        const CraterAttributes shaped = Shaped(mpd, kWorn, crater);
        EXPECT_NEAR(shaped.rimHeight, crater.rimHeight, 1e-3 * crater.diameter);
        EXPECT_NEAR(shaped.floorDepth + shaped.rimHeight, crater.depth, 1e-3 * crater.diameter);
        EXPECT_DOUBLE_EQ(shaped.depthRatio, crater.depth / crater.diameter);
        EXPECT_LE(shaped.freshness, 1.0);
    }
    const CraterState deep = ResolvedCraterState(mpd, kFresh, Manual(6.0, 0.9, 0.3));
    EXPECT_DOUBLE_EQ(deep.fresh.depthRatio, 0.15) << "raised to the deeper current state";
    EXPECT_DOUBLE_EQ(deep.fresh.rimHeightRatio, 0.05);
    EXPECT_EQ(deep.Freshness(), 1.0);
}

TEST(ResolvedCraterState, LowersTheFreshRimInProportionWithoutARimHeight) {
    const MartinParkesDunstanProfile mpd;
    const terraingen::CraterMorphometry fresh = LunarFreshCraterMorphometry(100.0);
    const Crater crater = Manual(100.0, 0.5 * fresh.depthRatio * 100.0);
    const CraterState state = ResolvedCraterState(mpd, kWorn, crater);
    EXPECT_EQ(state.fresh, fresh);
    EXPECT_DOUBLE_EQ(state.current.depthRatio, 0.5 * fresh.depthRatio);
    EXPECT_DOUBLE_EQ(state.current.rimHeightRatio, 0.5 * fresh.rimHeightRatio);
    EXPECT_EQ(state.diffusion, 0.0);
    const CraterState deeper = ResolvedCraterState(mpd, kWorn, Manual(100.0, 40.0));
    EXPECT_DOUBLE_EQ(deeper.current.rimHeightRatio, fresh.rimHeightRatio) << "kept at most fresh";
}

TEST(ResolvedCraterState, IgnoresTheAgeAndSlopeWearOfExplicitCraters) {
    const MartinParkesDunstanProfile mpd;
    const Crater young = Manual(80.0, 6.0, 1.0, 0.0);
    const Crater old = Manual(80.0, 6.0, 1.0, 3.0);
    EXPECT_EQ(ResolvedCraterState(mpd, kWorn, young), ResolvedCraterState(mpd, kWorn, old));
    EXPECT_EQ(ResolvedCraterState(mpd, kWorn, young, 4.0), ResolvedCraterState(mpd, kWorn, young));
}

TEST(ResolvedCraterState, GivesComplexCratersTheirDepthAndRim) {
    const ComplexCraterProfile complex(std::make_shared<MartinParkesDunstanProfile>(),
                                       terraingen::LunarComplexCraterShape());
    const Crater crater = Manual(30000.0, 2500.0, 600.0);
    const CraterAttributes shaped = Shaped(complex, kWorn, crater);
    ASSERT_TRUE(shaped.complex);
    EXPECT_NEAR(shaped.rimHeight, 600.0, 1e-3 * crater.diameter);
    EXPECT_NEAR(shaped.floorDepth + shaped.rimHeight, 2500.0, 1e-3 * crater.diameter);
}

TEST(ResolvedCraterState, SolvesTheDiffusionAgeForADepthUnderDiffusion) {
    const double diameter = 100.0;
    const terraingen::CraterMorphometry fresh = LunarFreshCraterMorphometry(diameter);
    const Crater crater = Manual(diameter, 0.5 * fresh.depthRatio * diameter);
    const CraterState state = ResolvedCraterState(*Diffused(), Diffusion(), crater);
    EXPECT_EQ(state.fresh, fresh);
    EXPECT_GT(state.diffusion, 0.0);
    EXPECT_NEAR(state.current.depthRatio, 0.5 * fresh.depthRatio, 1e-9);
    EXPECT_LT(state.current.rimHeightRatio, fresh.rimHeightRatio);
    const CraterAttributes shaped = terraingen::ComputeCraterAttributes(crater, state, *Diffused());
    EXPECT_NEAR(shaped.floorDepth + shaped.rimHeight, crater.depth, 0.02 * crater.depth);
    EXPECT_EQ(ResolvedCraterState(*Diffused(), Diffusion(), crater, 5.0), state);

    const CraterState deep = ResolvedCraterState(*Diffused(), Diffusion(), Manual(diameter, 30.0));
    EXPECT_EQ(deep.diffusion, 0.0) << "deeper than fresh: the fresh shape, deepened";
    EXPECT_EQ(deep.fresh.depthRatio, 0.3);
    EXPECT_EQ(deep.current, deep.fresh);
}

TEST(ResolvedCraterState, RejectsWhatDiffusionCannotShape) {
    EXPECT_NE(
        Rejection(*Diffused(), Diffusion(), Manual(100.0, 5.0, 1.0)).find("ties to its depth"),
        std::string::npos);
    const std::string shallow = Rejection(*Diffused(), Diffusion(), Manual(100.0, 1e-4));
    EXPECT_NE(shallow.find("The manual crater at (0, 0), 100 m across is shallower"),
              std::string::npos)
        << shallow;
}

TEST(ResolvedCraterState, RejectsWhatTheFassettThomsonFitCannotShape) {
    const FassettThomsonProfile ft;
    const std::string rim = Rejection(ft, kFresh, Manual(10.0, 1.0, 0.2));
    EXPECT_NE(rim.find("the Fassett-Thomson profile fixes"), std::string::npos) << rim;
    EXPECT_NE(rim.find("Martin-Parkes-Dunstan profile and complex craters honour"),
              std::string::npos);
    const std::string deep = Rejection(ft, kFresh, Manual(10.0, 3.0));
    EXPECT_NE(deep.find("deeper than the Fassett-Thomson fit"), std::string::npos) << deep;
    EXPECT_EQ(Rejection(ft, kFresh, Manual(10.0, 1.0)), "");
    const CraterAttributes shaped = Shaped(ft, kFresh, Manual(10.0, 1.0));
    EXPECT_DOUBLE_EQ(shaped.depthRatio, 0.1);

    const ComplexCraterProfile complex(std::make_shared<FassettThomsonProfile>(),
                                       terraingen::LunarComplexCraterShape());
    EXPECT_NE(Rejection(complex, kFresh, Manual(10.0, 1.0, 0.2)), "") << "a simple crater";
    EXPECT_EQ(Rejection(complex, kFresh, Manual(30000.0, 2500.0, 600.0)), "") << "a complex one";
}

TEST(ApplyCraters, LeavesExplicitCratersUnwornOnSteepGround) {
    const terraingen::CraterShapeModels models{Diffused(),
                                               std::make_shared<DiffusionDegradation>(Diffusion())};
    terraingen::Raster<float> base(161, 161, 0.0f);
    base.SetTransform(terraingen::GeoTransform::NorthUp(-80.5, 80.5, 1.0));
    for (std::size_t row = 0; row < base.Height(); ++row) {
        for (std::size_t col = 0; col < base.Width(); ++col) {
            base(col, row) = static_cast<float>(0.4 * base.CellCenter(col, row).y);
        }
    }
    const auto depth = [&](const Crater& crater, const terraingen::CraterApplyOptions& options) {
        terraingen::CraterList craters;
        craters.Append(crater);
        terraingen::Raster<float> heights = base;
        terraingen::ApplyCraters(heights, craters, models, options, {});
        double deepest = 0.0;
        for (std::size_t i = 0; i < heights.Size(); ++i) {
            deepest = std::fmin(deepest, static_cast<double>(heights.Values()[i]) -
                                             static_cast<double>(base.Values()[i]));
        }
        return -deepest;
    };
    terraingen::CraterApplyOptions level;
    level.datum = terraingen::CraterDatum::Plane;
    terraingen::CraterApplyOptions steep = level;
    steep.slope.criticalGradient = 0.7;
    const Crater generated{0.0, 0.0, 100.0, 0.8, 1u};
    ASSERT_GT(depth(generated, level), depth(generated, steep) + 0.1) << "worn faster";
    const Crater manual = Manual(100.0, 8.0, std::nan(""), 0.8);
    EXPECT_EQ(depth(manual, level), depth(manual, steep));
}

TEST(ApplyCraters, ShapesExplicitCratersOnEveryAvailableGpuAsOnTheCpu) {
    const auto mpd = std::make_shared<MartinParkesDunstanProfile>();
    const auto complex =
        std::make_shared<ComplexCraterProfile>(mpd, terraingen::LunarComplexCraterShape());
    const auto worn = std::make_shared<FreshnessDegradation>(kWorn);
    terraingen::CraterList craters;
    for (int i = 0; i < 40; ++i) {
        const double diameter = 8.0 + std::fmod(29.0 * i, 120.0);
        craters.Append({std::fmod(37.0 * i, 900.0) - 450.0, std::fmod(53.0 * i, 900.0) - 450.0,
                        diameter, 0.05 * (i % 40), static_cast<std::uint64_t>(i + 1), true,
                        (0.05 + 0.005 * (i % 20)) * diameter,
                        i % 2 == 0 ? 0.02 * diameter : std::nan("")});
    }
    craters.Append(Manual(30000.0, 2500.0, 600.0));
    const std::vector<terraingen::CraterShapeModels> cases{
        {mpd, worn},
        {complex, worn},
        {Diffused(), std::make_shared<DiffusionDegradation>(Diffusion())}};
    int ran = 0;
    for (std::size_t c = 0; c < cases.size(); ++c) {
        terraingen::CraterList shaped;
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            Crater crater = craters.At(n);
            if (c == 2) {
                crater.rimHeight = std::nan("");
            }
            shaped.Append(crater);
        }
        terraingen::Raster<float> base(300, 300, 0.0f);
        base.SetTransform(terraingen::GeoTransform::NorthUp(-600.0, 600.0, 4.0));
        terraingen::Raster<float> cpu = base;
        terraingen::ApplyCraters(cpu, shaped, cases[c], {}, {});
        for (const terraingen::BackendStatus& status : terraingen::Backends()) {
            if (status.backend == terraingen::Backend::Cpu || !status.available) {
                continue;
            }
            terraingen::Raster<float> gpu = base;
            terraingen::CraterSampling sampling;
            sampling.backend = status.backend;
            terraingen::ApplyCraters(gpu, shaped, cases[c], {}, sampling);
            double largest = 0.0;
            for (std::size_t i = 0; i < gpu.Size(); ++i) {
                largest = std::max(largest, std::fabs(static_cast<double>(gpu.Values()[i]) -
                                                      static_cast<double>(cpu.Values()[i])));
            }
            EXPECT_LT(largest, 2e-3) << terraingen::BackendName(status.backend) << ", case " << c;
            ++ran;
        }
    }
    if (ran == 0) {
        GTEST_SKIP() << "no GPU backend here";
    }
}

TEST(ResolvedCraterState, RejectsInvalidExplicitValues) {
    const MartinParkesDunstanProfile mpd;
    EXPECT_NE(Rejection(mpd, kFresh, Manual(10.0, std::nan(""), 0.2)), "");
    EXPECT_NE(Rejection(mpd, kFresh, Manual(10.0, -1.0)), "");
    EXPECT_NE(Rejection(mpd, kFresh, Manual(10.0, 1.0, -0.5)), "");
}
