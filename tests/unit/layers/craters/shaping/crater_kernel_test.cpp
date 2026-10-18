#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

using terraingen::ApplyCraterTo;
using terraingen::Crater;
using terraingen::CraterApplyOptions;
using terraingen::CraterProfile;
using terraingen::CraterState;
using terraingen::PreparedCrater;

namespace {

/// A profile the kernel does not know, forwarding to `inner`, so `ApplyCraterTo` takes its
/// double-precision path.
class Opaque final : public CraterProfile {
  public:
    explicit Opaque(std::shared_ptr<const CraterProfile> inner) : inner_(std::move(inner)) {}
    double Height(double distance, double diameter, const CraterState& state) const override {
        return inner_->Height(distance, diameter, state);
    }
    double ReachRadii() const override {
        return inner_->ReachRadii();
    }
    bool IsComplex(double diameter) const override {
        return inner_->IsComplex(diameter);
    }

  private:
    std::shared_ptr<const CraterProfile> inner_;
};

struct Case {
    std::string name;
    std::shared_ptr<const CraterProfile> profile;
};

std::vector<Case> Profiles() {
    using terraingen::FassettThomsonRim;
    const auto ft = std::make_shared<terraingen::FassettThomsonProfile>();
    const auto complex = std::make_shared<terraingen::ComplexCraterProfile>(
        ft, terraingen::LunarComplexCraterShape());
    return {
        {"fassett thomson", ft},
        {"at one radius",
         std::make_shared<terraingen::FassettThomsonProfile>(FassettThomsonRim::AtOneRadius)},
        {"plateau",
         std::make_shared<terraingen::FassettThomsonProfile>(FassettThomsonRim::Plateau)},
        {"martin parkes dunstan", std::make_shared<terraingen::MartinParkesDunstanProfile>()},
        {"complex", complex},
        {"diffused", std::make_shared<terraingen::DiffusedProfile>(ft)},
        {"diffused complex", std::make_shared<terraingen::DiffusedProfile>(complex)},
        {"mars complex", std::make_shared<terraingen::ComplexCraterProfile>(
                             ft, terraingen::MarsComplexCraterShape())},
        {"diffused martin parkes dunstan",
         std::make_shared<terraingen::DiffusedProfile>(
             std::make_shared<terraingen::MartinParkesDunstanProfile>())},
    };
}

std::vector<CraterApplyOptions> Options() {
    std::vector<CraterApplyOptions> options(7);
    options[1].rimSmoothing.sigmaRadii = 0.05;
    options[2].noise.amplitude = 0.01;
    options[2].noise.rimAmplitude = 0.01;
    options[3].texture.wallStreaks = 0.01;
    options[3].texture.ejecta = 0.5;
    options[3].texture.mantling = 0.5;
    options[4].interiorReplacement = 0.7;
    options[4].blend.shape = terraingen::BlendShape::Gaussian;
    options[5].blend.shape = terraingen::BlendShape::Linear;
    options[5].rimSmoothing.sigmaRadii = 0.05;
    options[5].rimSmoothing.shape = terraingen::BlendShape::Smoothstep;
    options[6].datum = terraingen::CraterDatum::Plane;
    options[6].slope.enabled = true;
    options[6].slope.upslopeRim = 0.3;
    options[6].slope.fullSlope = 0.01;
    return options;
}

}  // namespace

TEST(CraterKernel, ShapesAsTheDoublePathToWithinFloatRounding) {
    for (const Case& c : Profiles()) {
        const Opaque reference(c.profile);
        for (const double diameter : {40.0, 30000.0}) {
            const Crater crater{1000.0, -2000.0, diameter, 0.1, 7u};
            const double radius = 0.5 * diameter;
            for (const double depth : {0.2, 0.12}) {
                for (const double diffusion : {0.0, 5e-6, 0.004}) {
                    CraterState state{{0.2, 0.04}, {depth, 0.04 * depth / 0.2}, diffusion};
                    PreparedCrater prepared{crater, 0.8, state};
                    prepared.datum = terraingen::Plane{5.0, 0.01, -0.02};
                    for (std::size_t o = 0; o < Options().size(); ++o) {
                        const CraterApplyOptions options = Options()[o];
                        prepared.slope = terraingen::SlopeShape(crater, 0.3, -0.2, options.slope);
                        for (int k = 0; k < 400; ++k) {
                            const double r = radius * 3.2 * (k + 0.37) / 400.0;
                            const double a = 0.7 * k;
                            const double x = crater.x + r * std::cos(a);
                            const double y = crater.y + r * std::sin(a);
                            const float kernel =
                                ApplyCraterTo(3.0f, x, y, prepared, *c.profile, options);
                            const float exact =
                                ApplyCraterTo(3.0f, x, y, prepared, reference, options);
                            ASSERT_NEAR(kernel, exact, 2e-5 * diameter + 1e-4)
                                << c.name << ", D " << diameter << ", depth " << depth << ", tau "
                                << diffusion << ", options " << o << " at " << r / radius
                                << " radii";
                        }
                    }
                }
            }
        }
    }
}

TEST(CraterKernel, FollowsIrregularOutlines) {
    const auto profile = std::make_shared<terraingen::FassettThomsonProfile>();
    const Opaque reference(profile);
    for (const auto model :
         {terraingen::RimRoughnessModel::PowerLaw, terraingen::RimRoughnessModel::Du2024}) {
        CraterApplyOptions options;
        options.irregularity.enabled = true;
        options.irregularity.selection.manual = true;
        options.irregularity.elongatedShare = 1.0;
        options.irregularity.model = model;
        options.irregularity.marksShare = 1.0;
        options.irregularity.marksAmplitude = {terraingen::ValueDistribution::Kind::Constant, 0.02};
        const Crater crater{0.0, 0.0, 200.0, 0.0, 11u, true};
        PreparedCrater prepared{crater, 1.0, {{0.2, 0.04}, {0.2, 0.04}, 0.0}};
        prepared.outline = terraingen::OutlineOf(crater, options.irregularity);
        ASSERT_FALSE(prepared.outline.Round());
        for (int k = 0; k < 2000; ++k) {
            const double r = 160.0 * k / 2000.0;
            const double a = 0.37 * k;
            const float kernel =
                ApplyCraterTo(0.0f, r * std::cos(a), r * std::sin(a), prepared, *profile, options);
            const float exact =
                ApplyCraterTo(0.0f, r * std::cos(a), r * std::sin(a), prepared, reference, options);
            ASSERT_NEAR(kernel, exact, 2e-3) << "at " << r << " m";
        }
    }
}

TEST(CraterKernel, LeavesBodiesAndProfilesOfYourOwnToTheDoublePath) {
    const terraingen::FassettThomsonProfile profile;
    CraterApplyOptions options;
    EXPECT_TRUE(terraingen::MakeCraterShapeKernel(profile, options));
    EXPECT_FALSE(terraingen::MakeCraterShapeKernel(
        Opaque(std::make_shared<terraingen::FassettThomsonProfile>()), options));
    options.surface = terraingen::Surface::Sphere(1737400.0);
    EXPECT_FALSE(terraingen::MakeCraterShapeKernel(profile, options));
}

TEST(CraterKernel, RejectsStatesTheProfileRejects) {
    const terraingen::MartinParkesDunstanProfile profile;
    const Crater crater{0.0, 0.0, 20.0, 0.0, 1u};
    const PreparedCrater prepared{crater, 1.0, {{0.2, 0.04}, {0.2, 0.05}, 0.0}};
    EXPECT_THROW(ApplyCraterTo(0.0f, 1.0, 1.0, prepared, profile, {}), std::invalid_argument);
}

TEST(CraterKernel, ShapesGridsOnEveryAvailableGpuToWithinFloatRounding) {
    const auto ft = std::make_shared<terraingen::FassettThomsonProfile>();
    const auto diffused = std::make_shared<terraingen::DiffusedProfile>(
        std::make_shared<terraingen::ComplexCraterProfile>(ft,
                                                           terraingen::LunarComplexCraterShape()));
    terraingen::CraterList craters;
    for (int i = 0; i < 300; ++i) {
        craters.Append({std::fmod(37.0 * i, 900.0) - 450.0, std::fmod(53.0 * i, 900.0) - 450.0,
                        8.0 + std::fmod(29.0 * i, 180.0), 0.001 * (i % 40),
                        static_cast<std::uint64_t>(i + 1)});
    }
    craters.Append({0.0, 0.0, 30000.0, 0.0, 999u});
    CraterApplyOptions options;
    options.datum = terraingen::CraterDatum::Plane;
    options.noise.amplitude = 0.01;
    options.rimSmoothing.sigmaRadii = 0.05;
    options.texture.wallStreaks = 0.01;
    options.texture.ejecta = 0.5;
    options.irregularity.enabled = true;
    options.irregularity.selection.manual = true;
    options.irregularity.marksShare = 0.5;
    options.irregularity.marksAmplitude = {terraingen::ValueDistribution::Kind::Constant, 0.02};
    options.slope.enabled = true;
    options.slope.fullSlope = 0.002;
    options.slope.criticalGradient = 0.05;
    const terraingen::CraterShapeModels models{
        diffused, std::make_shared<terraingen::DiffusionDegradation>(
                      terraingen::LunarFreshCraterMorphometry,
                      terraingen::Fassett2022HartmannDiffusivity(), diffused)};
    terraingen::Raster<float> base(300, 300, 0.0f);
    base.SetTransform(terraingen::GeoTransform::NorthUp(-600.0, 600.0, 4.0));
    for (std::size_t i = 0; i < base.Size(); ++i) {
        base.Values()[i] = 0.01f * static_cast<float>(i % 300);
    }
    terraingen::Raster<float> cpu = base;
    terraingen::ApplyCraters(cpu, craters, models, options, {});
    int ran = 0;
    for (const terraingen::BackendStatus& status : terraingen::Backends()) {
        if (status.backend == terraingen::Backend::Cpu || !status.available) {
            continue;
        }
        terraingen::Raster<float> gpu = base;
        terraingen::CraterSampling sampling;
        sampling.backend = status.backend;
        terraingen::ApplyCraters(gpu, craters, models, options, sampling);
        double largest = 0.0;
        for (std::size_t i = 0; i < gpu.Size(); ++i) {
            ASSERT_FALSE(std::isnan(gpu.Values()[i]))
                << "every cell set on " << terraingen::BackendName(status.backend) << ", cell "
                << i;
            largest = std::max(largest, std::fabs(static_cast<double>(gpu.Values()[i]) -
                                                  static_cast<double>(cpu.Values()[i])));
        }
        EXPECT_LT(largest, 2e-3) << terraingen::BackendName(status.backend);
        ++ran;
    }
    if (ran == 0) {
        GTEST_SKIP() << "no GPU backend here";
    }
}

TEST(CraterKernel, FitsDatumsOnEveryAvailableGpuAsOnTheCpu) {
    const auto ft = std::make_shared<terraingen::FassettThomsonProfile>();
    const auto diffused = std::make_shared<terraingen::DiffusedProfile>(ft);
    const auto degradation = std::make_shared<terraingen::DiffusionDegradation>(
        terraingen::LunarFreshCraterMorphometry, terraingen::Fassett2022HartmannDiffusivity(),
        diffused);
    struct DatumCase {
        terraingen::CraterShapeModels models;
        terraingen::CraterDatum datum;
        bool irregular;
    };
    const std::vector<DatumCase> cases{
        {{diffused, degradation}, terraingen::CraterDatum::Plane, true},
        {{diffused, degradation}, terraingen::CraterDatum::MeanHeight, true},
        {{ft, degradation}, terraingen::CraterDatum::Plane, false},
    };
    terraingen::CraterList craters;
    for (int i = 0; i < 600; ++i) {
        craters.Append({std::fmod(37.0 * i, 1300.0) - 650.0, std::fmod(53.0 * i, 1300.0) - 650.0,
                        6.0 + std::fmod(29.0 * i, 240.0), 0.002 * (i % 40),
                        static_cast<std::uint64_t>(i + 1)});
    }
    terraingen::Raster<float> base(256, 256, 0.0f);
    base.SetTransform(terraingen::GeoTransform::NorthUp(-512.0, 512.0, 4.0));
    for (std::size_t row = 0; row < 256; ++row) {
        for (std::size_t col = 0; col < 256; ++col) {
            base.Values()[row * 256 + col] =
                0.05f * static_cast<float>(col) + 3.0f * std::sin(0.07f * static_cast<float>(row));
        }
    }
    int ran = 0;
    for (std::size_t c = 0; c < cases.size(); ++c) {
        CraterApplyOptions options;
        options.datum = cases[c].datum;
        options.irregularity.enabled = cases[c].irregular;
        options.irregularity.selection.manual = true;
        const terraingen::CraterShapeModels& models = cases[c].models;
        terraingen::Raster<float> cpu = base;
        terraingen::ApplyCraters(cpu, craters, models, options, {});
        for (const terraingen::BackendStatus& status : terraingen::Backends()) {
            if (status.backend == terraingen::Backend::Cpu || !status.available) {
                continue;
            }
            terraingen::Raster<float> gpu = base;
            terraingen::CraterSampling sampling;
            sampling.backend = status.backend;
            terraingen::ApplyCraters(gpu, craters, models, options, sampling);
            double largest = 0.0;
            for (std::size_t i = 0; i < gpu.Size(); ++i) {
                ASSERT_FALSE(std::isnan(gpu.Values()[i]))
                    << terraingen::BackendName(status.backend) << ", cell " << i;
                largest = std::max(largest, std::fabs(static_cast<double>(gpu.Values()[i]) -
                                                      static_cast<double>(cpu.Values()[i])));
            }
            EXPECT_LT(largest, 3e-5 * 246.0)
                << terraingen::BackendName(status.backend) << ", case " << c;
            ++ran;
        }
    }
    if (ran == 0) {
        GTEST_SKIP() << "no GPU backend here";
    }
}
