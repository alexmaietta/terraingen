#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

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
    std::vector<CraterApplyOptions> options(6);
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
