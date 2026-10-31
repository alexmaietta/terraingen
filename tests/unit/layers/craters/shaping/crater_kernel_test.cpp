// SPDX-License-Identifier: BSD-3-Clause

#include "support/apply_crater.h"
#include "terraingen/terraingen.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using terraingen::Crater;
using terraingen::CraterApplyOptions;
using terraingen::CraterProfile;
using terraingen::CraterState;
using terraingen::PreparedCrater;
using terraingen::testing::ApplyCraterTo;

namespace {

/// A profile the kernel does not know, forwarding to `inner`, so the kernel tabulates it.
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
    std::vector<CraterApplyOptions> options(8);
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
    options[6].slope.asymmetric = true;
    options[6].slope.upslopeRim = 0.3;
    options[6].slope.fullSlopeDegrees = 0.01 * terraingen::kRadian;
    options[7].terraces.enabled = true;
    options[7].terraces.onsetDiameter = 10.0;
    options[7].terraces.fullDiameter = 100.0;
    options[7].terraces.seed = 4u;
    return options;
}

}  // namespace

TEST(CraterKernel, ShapesTheProfilesItKnowsToWithinFloatRounding) {
    for (const Case& c : Profiles()) {
        for (const double diameter : {40.0, 30000.0}) {
            const Crater crater{1000.0, -2000.0, diameter, 0.1, 7u};
            const double radius = 0.5 * diameter;
            for (const double depth : {0.2, 0.12}) {
                for (const double diffusion : {0.0, 5e-6, 0.004}) {
                    const CraterState state{{0.2, 0.04}, {depth, 0.04 * depth / 0.2}, diffusion};
                    const PreparedCrater prepared{crater, 1.0, state};
                    for (int k = 0; k < 400; ++k) {
                        const double r = radius * 3.2 * (k + 0.37) / 400.0;
                        const double a = 0.7 * k;
                        const float kernel =
                            ApplyCraterTo(3.0f, crater.x + r * std::cos(a),
                                          crater.y + r * std::sin(a), prepared, *c.profile, {});
                        const double exact = 3.0 + c.profile->Height(r, diameter, state);
                        ASSERT_NEAR(kernel, exact, 2e-5 * diameter + 1e-4)
                            << c.name << ", D " << diameter << ", depth " << depth << ", tau "
                            << diffusion << " at " << r / radius << " radii";
                    }
                }
            }
        }
    }
}

TEST(CraterKernel, TabulatesProfilesOfYourOwnCloseToTheirHeights) {
    for (const Case& c : Profiles()) {
        const Opaque custom(c.profile);
        for (const double diameter : {40.0, 30000.0}) {
            const Crater crater{1000.0, -2000.0, diameter, 0.1, 7u};
            const double radius = 0.5 * diameter;
            const CraterState state{{0.2, 0.04}, {0.12, 0.024}, 5e-6};
            PreparedCrater prepared{crater, 0.8, state};
            prepared.datum = terraingen::Plane{5.0, 0.01, -0.02};
            for (std::size_t o = 0; o < Options().size(); ++o) {
                const CraterApplyOptions options = Options()[o];
                if (options.terraces.enabled) {
                    continue;
                }
                prepared.slope = terraingen::SlopeShape(crater, 0.3, -0.2, options.slope);
                for (int k = 0; k < 400; ++k) {
                    const double r = radius * 3.2 * (k + 0.37) / 400.0;
                    const double a = 0.7 * k;
                    const double x = crater.x + r * std::cos(a);
                    const double y = crater.y + r * std::sin(a);
                    const float known = ApplyCraterTo(3.0f, x, y, prepared, *c.profile, options);
                    const float tabulated = ApplyCraterTo(3.0f, x, y, prepared, custom, options);
                    ASSERT_NEAR(tabulated, known, 5e-4 * diameter + 1e-3)
                        << c.name << ", D " << diameter << ", options " << o << " at " << r / radius
                        << " radii";
                }
            }
        }
    }
}

TEST(CraterKernel, FollowsIrregularOutlines) {
    const auto profile = std::make_shared<terraingen::FassettThomsonProfile>();
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
        prepared.outline = terraingen::IrregularOutline(crater, options.irregularity);
        ASSERT_FALSE(prepared.outline.Round());
        for (int k = 0; k < 2000; ++k) {
            const double r = 160.0 * k / 2000.0;
            const double a = 0.37 * k;
            const double x = r * std::cos(a);
            const double y = r * std::sin(a);
            const float kernel = ApplyCraterTo(0.0f, x, y, prepared, *profile, options);
            const double distance =
                terraingen::CraterProfileDistance(crater, prepared.outline, x, y, {});
            const double exact = profile->Height(distance, 200.0, prepared.state);
            ASSERT_NEAR(kernel, exact, 2e-3) << "at " << r << " m";
        }
    }
}

TEST(CraterKernel, ShapesEveryProfileOnEverySurface) {
    const terraingen::FassettThomsonProfile profile;
    const Opaque custom(std::make_shared<terraingen::FassettThomsonProfile>());
    CraterApplyOptions options;
    EXPECT_EQ(terraingen::MakeCraterShapeKernel(profile, options).profile.simple,
              terraingen::CraterProfileKind::FassettThomson);
    const terraingen::CraterShapeKernel tabulated =
        terraingen::MakeCraterShapeKernel(custom, options);
    EXPECT_EQ(tabulated.profile.simple, terraingen::CraterProfileKind::Tabulated);
    EXPECT_EQ(tabulated.profile.tableCells, terraingen::kProfileTableCells);
    EXPECT_FLOAT_EQ(tabulated.profile.tableExtent, 0.5f * static_cast<float>(profile.ReachRadii()));
    options.surface = terraingen::Surface::Sphere(1737400.0);
    EXPECT_EQ(terraingen::MakeCraterShapeKernel(profile, options).profile.simple,
              terraingen::CraterProfileKind::FassettThomson);
    const Crater crater{0.0, 0.0, 20.0, 0.0, 1u};
    const PreparedCrater prepared{crater, 1.0, {{0.2, 0.04}, {0.2, 0.04}, 0.0}};
    const auto batch =
        terraingen::MakeCraterKernelBatch(tabulated, custom, {prepared, prepared}, {0, 1}, options);
    EXPECT_EQ(batch.records[0].profileOffset, batch.records[1].profileOffset)
        << "craters alike share a table";
    EXPECT_EQ(batch.data.size(), terraingen::kProfileTableCells + 1u);
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
    options.slope.asymmetric = true;
    options.slope.fullSlopeDegrees = 0.002 * terraingen::kRadian;
    options.slope.criticalGradient = 0.05;
    const terraingen::CraterShapeModels models{
        diffused, std::make_shared<terraingen::DiffusionDegradation>(
                      terraingen::LunarFreshCraterMorphometry,
                      terraingen::Fassett2022HartmannDiffusivity(), diffused)};
    terraingen::Raster<float> base(
        terraingen::RasterGrid{300, 300, terraingen::GeoTransform::NorthUp(-600.0, 600.0, 4.0)},
        0.0f);
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
    terraingen::Raster<float> base(
        terraingen::RasterGrid{256, 256, terraingen::GeoTransform::NorthUp(-512.0, 512.0, 4.0)},
        0.0f);
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

namespace {

struct BodySurface {
    std::string name;
    terraingen::Surface surface;
    terraingen::Vec2d center;  ///< Longitude and latitude in degrees.
    double step;               ///< Grid cell side in degrees.
    double maxDiameter;        ///< Largest crater on the grid, in meters.
};

std::vector<BodySurface> BodySurfaces() {
    const auto asteroid = std::make_shared<const terraingen::RadialShape>(
        terraingen::RadialShape::Sample(24, [](const terraingen::Vec3d& d) {
            return 8000.0 + 600.0 * d.x * d.y + 300.0 * d.z * d.z;
        }));
    return {{"sphere", terraingen::Surface::Sphere(1737400.0), {30.0, 60.0}, 0.0005, 400.0},
            {"ellipsoid",
             terraingen::Surface::Ellipsoid(1738100.0, 1737400.0, 1735970.0),
             {-40.0, 25.0},
             0.0005,
             400.0},
            {"shape model", terraingen::Surface::ShapeModel(asteroid), {10.0, 20.0}, 0.01, 60.0}};
}

/// `count` craters up to `body.maxDiameter` spread over a square of `side` cells around the
/// body's center.
terraingen::CraterList BodyCraters(const BodySurface& body, std::size_t side, int count) {
    terraingen::CraterList craters;
    const double half = 0.5 * static_cast<double>(side) * body.step;
    for (int i = 0; i < count; ++i) {
        const double u = std::fmod(0.37 * i, 1.0);
        const double v = std::fmod(0.53 * i + 0.1, 1.0);
        const double d = body.maxDiameter * (0.02 + 0.98 * std::fmod(0.29 * i, 1.0));
        craters.Append({body.center.x - half + 2.0 * half * u,
                        body.center.y - half + 2.0 * half * v, d, 0.002 * (i % 40),
                        static_cast<std::uint64_t>(i + 1)});
    }
    return craters;
}

/// A longitude-latitude grid of `side` cells centered on `body`'s center, sloping east.
terraingen::Raster<float> BodyGrid(const BodySurface& body, std::size_t side) {
    const double half = 0.5 * static_cast<double>(side) * body.step;
    terraingen::Raster<float> grid(terraingen::RasterGrid{
        side, side,
        terraingen::GeoTransform::NorthUp(body.center.x - half, body.center.y + half, body.step)});
    for (std::size_t i = 0; i < grid.Size(); ++i) {
        grid.Values()[i] = 0.02f * static_cast<float>(i % side);
    }
    return grid;
}

/// The largest difference between two rasters of one size, in meters.
double LargestDifference(const terraingen::Raster<float>& a, const terraingen::Raster<float>& b) {
    double largest = 0.0;
    for (std::size_t i = 0; i < a.Size(); ++i) {
        largest = std::max(largest, std::fabs(static_cast<double>(a.Values()[i]) -
                                              static_cast<double>(b.Values()[i])));
    }
    return largest;
}

}  // namespace

TEST(CraterKernel, TakesArcsineRatiosToWithinFloatRounding) {
    EXPECT_EQ(terraingen::KernelArcsineRatio(0.0f), 1.0f);
    for (int i = 1; i <= 1000; ++i) {
        const double s = 0.001 * i;
        EXPECT_NEAR(terraingen::KernelArcsineRatio(static_cast<float>(s)), std::asin(s) / s,
                    4e-7 * std::asin(s) / s)
            << "at " << s;
    }
}

TEST(CraterKernel, WorksOutBodyOffsetsInEachCratersFrameAsTheSurfaceDoes) {
    for (const BodySurface& body : BodySurfaces()) {
        const Crater crater{body.center.x, body.center.y, body.maxDiameter, 0.0, 1u};
        const terraingen::Vec2d corner = body.surface.Locate(body.center, {-900.0, 700.0});
        const terraingen::Vec3d origin = body.surface.Position(corner);
        const terraingen::CraterFrame frame =
            terraingen::MakeCraterFrame(body.surface, crater, origin);
        for (const double meters : {0.0, 0.01, 1.0, 37.0, 250.0, 1200.0}) {
            for (const double azimuth : {0.0, 0.7, 2.2, 3.9, 5.5}) {
                const terraingen::Vec2d point = body.surface.Locate(
                    body.center, {meters * std::sin(azimuth), meters * std::cos(azimuth)});
                const terraingen::Vec3d p = body.surface.Position(point) - origin;
                const terraingen::KernelOffset kernel =
                    terraingen::KernelFrameOffset(frame, static_cast<float>(p.x),
                                                  static_cast<float>(p.y), static_cast<float>(p.z));
                const terraingen::KernelOffset exact =
                    terraingen::ShapingOffset(body.surface, crater, point);
                const double tolerance = 1e-3 + 1e-6 * meters;
                EXPECT_NEAR(kernel.dx, exact.dx, tolerance) << body.name << " at " << meters;
                EXPECT_NEAR(kernel.dy, exact.dy, tolerance) << body.name << " at " << meters;
            }
        }
    }
}

TEST(CraterKernel, ShapesBodyGridsOnEveryAvailableGpuAsOnTheCpu) {
    const auto ft = std::make_shared<terraingen::FassettThomsonProfile>();
    const auto diffused = std::make_shared<terraingen::DiffusedProfile>(ft);
    const terraingen::CraterShapeModels models{
        diffused, std::make_shared<terraingen::DiffusionDegradation>(
                      terraingen::LunarFreshCraterMorphometry,
                      terraingen::Fassett2022HartmannDiffusivity(), diffused)};
    constexpr std::size_t kSide = 256;
    int ran = 0;
    for (const BodySurface& body : BodySurfaces()) {
        const terraingen::CraterList craters = BodyCraters(body, kSide, 250);
        const terraingen::Raster<float> base = BodyGrid(body, kSide);
        for (const auto datum : {terraingen::CraterDatum::None, terraingen::CraterDatum::Plane,
                                 terraingen::CraterDatum::MeanHeight}) {
            CraterApplyOptions options;
            options.surface = body.surface;
            options.datum = datum;
            options.noise.amplitude = 0.01;
            options.texture.ejecta = 0.5;
            options.irregularity.enabled = true;
            options.irregularity.selection.manual = true;
            options.slope.asymmetric = datum == terraingen::CraterDatum::Plane;
            options.slope.fullSlopeDegrees = 0.002 * terraingen::kRadian;
            terraingen::Raster<float> cpu = base;
            terraingen::ApplyCraters(cpu, craters, models, options, {});
            EXPECT_GT(LargestDifference(cpu, base), 0.05 * body.maxDiameter) << body.name;
            for (const terraingen::BackendStatus& status : terraingen::Backends()) {
                if (status.backend == terraingen::Backend::Cpu || !status.available) {
                    continue;
                }
                terraingen::Raster<float> gpu = base;
                terraingen::CraterSampling sampling;
                sampling.backend = status.backend;
                std::vector<std::string> messages;
                terraingen::SetLogLevel(terraingen::LogLevel::Debug);
                terraingen::SetLogSink([&messages](const terraingen::LogRecord& record) {
                    messages.push_back(record.message);
                });
                terraingen::ApplyCraters(gpu, craters, models, options, sampling);
                terraingen::SetLogSink(nullptr);
                terraingen::SetLogLevel(terraingen::LogLevel::Warning);
                const std::string name(terraingen::BackendName(status.backend));
                const auto logged = [&](const std::string& text) {
                    return std::any_of(messages.begin(), messages.end(), [&](const auto& m) {
                        return m.find(text) != std::string::npos && m.size() >= name.size() &&
                               m.compare(m.size() - name.size(), name.size(), name) == 0;
                    });
                };
                EXPECT_TRUE(logged("craters on")) << body.name << " shaped on " << name;
                if (datum != terraingen::CraterDatum::None) {
                    EXPECT_TRUE(logged("crater datums on")) << body.name << " fitted on " << name;
                }
                for (std::size_t i = 0; i < gpu.Size(); ++i) {
                    ASSERT_FALSE(std::isnan(gpu.Values()[i]))
                        << body.name << " on " << name << ", cell " << i;
                }
                const double largest = LargestDifference(gpu, cpu);
                EXPECT_LT(largest, 1e-5 * body.maxDiameter)
                    << body.name << " on " << terraingen::BackendName(status.backend) << ", datum "
                    << static_cast<int>(datum);
                ++ran;
            }
        }
    }
    if (ran == 0) {
        GTEST_SKIP() << "no GPU backend here";
    }
}

TEST(FlattenKernelLists, AppendsEachListsFirstEntryAndCountAndNeverLeavesNoEntries) {
    std::vector<std::uint32_t> heads{9u};
    const std::vector<std::uint32_t> entries =
        terraingen::FlattenKernelLists({{4u, 5u}, {}, {6u}}, heads);
    EXPECT_EQ(heads, (std::vector<std::uint32_t>{9u, 0u, 2u, 2u, 0u, 2u, 1u}));
    EXPECT_EQ(entries, (std::vector<std::uint32_t>{4u, 5u, 6u}));
    std::vector<std::uint32_t> none;
    EXPECT_EQ(terraingen::FlattenKernelLists({{}}, none), (std::vector<std::uint32_t>{0u}));
    EXPECT_EQ(none, (std::vector<std::uint32_t>{0u, 0u}));
}

TEST(StoreKernelPosition, StoresAFloatTriple) {
    float at[3] = {0.0f, 0.0f, 0.0f};
    terraingen::StoreKernelPosition({1.5, -2.0, 1e9 + 0.25}, at);
    EXPECT_EQ(at[0], 1.5f);
    EXPECT_EQ(at[1], -2.0f);
    EXPECT_EQ(at[2], static_cast<float>(1e9 + 0.25));
}

TEST(KernelPeriodicAt, WrapsTheAngleIntoTheTable) {
    const terraingen::KernelPeriodicCell quarter = terraingen::KernelPeriodicAt(0.25f, 8u);
    EXPECT_EQ(quarter.index, 2u);
    EXPECT_EQ(quarter.t, 0.0f);
    const terraingen::KernelPeriodicCell behind = terraingen::KernelPeriodicAt(-0.0625f, 8u);
    EXPECT_EQ(behind.index, 7u);
    EXPECT_FLOAT_EQ(behind.t, 0.5f);
}

TEST(KernelEjectaShare, FallsAsTheInverseCubeAndTapersToTheReach) {
    EXPECT_EQ(terraingen::KernelEjectaShare(0.5f, 3.0f), 1.0f);
    EXPECT_FLOAT_EQ(terraingen::KernelEjectaShare(2.0f, 3.0f),
                    (1.0f / 8.0f - 1.0f / 27.0f) / (1.0f - 1.0f / 27.0f));
    EXPECT_EQ(terraingen::KernelEjectaShare(3.0f, 3.0f), 0.0f);
    EXPECT_FLOAT_EQ(terraingen::KernelEjectaScale(1.0f), 0.14f);
}

TEST(KernelWornOutlineShare, KeepsTheWobbleInProportionToTheRimLeft) {
    terraingen::CraterRecord record{};
    EXPECT_EQ(terraingen::KernelWornOutlineShare(record), 1.0f) << "no fresh rim";
    record.freshRim = 0.04f;
    record.currentRim = 0.04f;
    EXPECT_EQ(terraingen::KernelWornOutlineShare(record), 1.0f);
    record.currentRim = 0.01f;
    EXPECT_FLOAT_EQ(terraingen::KernelWornOutlineShare(record), 0.25f);
    record.currentRim = 0.0f;
    EXPECT_EQ(terraingen::KernelWornOutlineShare(record), 0.0f);
}

TEST(CraterKernel, WornRimsRoundOutTheirShortWiggles) {
    CraterApplyOptions options;
    options.irregularity.enabled = true;
    options.irregularity.selection.manual = true;
    const Crater crater{0.0, 0.0, 200.0, 0.0, 11u, true};
    const terraingen::CraterOutline outline =
        terraingen::IrregularOutline(crater, options.irregularity);
    ASSERT_FALSE(outline.Round());
    const auto spread = [&](float currentRim) {
        terraingen::CraterRecord record{};
        std::vector<float> data;
        terraingen::AppendKernelOutline(outline, record, data);
        record.diameter = 200.0f;
        record.freshRim = 0.04f;
        record.currentRim = currentRim;
        terraingen::CraterData view;
        view.values = data.data();
        double lowest = 1e9;
        double highest = 0.0;
        for (int k = 0; k < 720; ++k) {
            const double a = terraingen::kPi * k / 360.0;
            const double d = terraingen::KernelProfileDistance(
                view, record, static_cast<float>(100.0 * std::cos(a)),
                static_cast<float>(100.0 * std::sin(a)));
            lowest = std::min(lowest, d);
            highest = std::max(highest, d);
        }
        return highest - lowest;
    };
    const double fresh = spread(0.04f);
    const double worn = spread(0.01f);
    EXPECT_GT(fresh, 1.0) << "a rough fresh rim";
    EXPECT_LT(worn, 0.5 * fresh) << "a worn rim follows less of the wobble";
}

TEST(CraterKernel, RimSmoothingRoundsTheCrestWithoutLeavingTerraces) {
    const terraingen::FassettThomsonProfile profile;
    const Crater crater{0.0, 0.0, 80.0, 0.0, 3u};
    const CraterState state{{0.2, 0.04}, {0.2, 0.04}, 0.0};
    const PreparedCrater prepared{crater, 1.0, state};
    const auto sharpest = [&](double sigma) {
        CraterApplyOptions options;
        options.rimSmoothing.sigmaRadii = sigma;
        const double step = 0.001 * 40.0;
        double largest = 0.0;
        for (int k = 1; k < 240; ++k) {
            const double r = 40.0 * 0.88 + step * k;
            const double before = ApplyCraterTo(0.0f, r - step, 0.0, prepared, profile, options);
            const double here = ApplyCraterTo(0.0f, r, 0.0, prepared, profile, options);
            const double after = ApplyCraterTo(0.0f, r + step, 0.0, prepared, profile, options);
            largest = std::max(largest, std::abs(before - 2.0 * here + after));
        }
        return largest;
    };
    const double sharp = sharpest(0.0);
    EXPECT_GT(sharp, 0.0);
    for (const double sigma : {0.02, 0.05, 0.15}) {
        EXPECT_LT(sharpest(sigma), 0.1 * sharp)
            << "no kink left about a sigma from the rim at " << sigma << " radii";
    }
}

TEST(MartinParkesDunstanProfile, WornEjectaLowersWithTheRimAndNeverDipsBelowTheGround) {
    const terraingen::MartinParkesDunstanProfile profile;
    const double d = 150.0;
    const CraterState fresh{{0.2, 0.036}, {0.2, 0.036}};
    for (const double freshness : {0.7, 0.4, 0.2, 0.05}) {
        const CraterState worn{{0.2, 0.036}, {0.2 * (0.2 + 0.8 * freshness), 0.036 * freshness}};
        for (double r = 76.0; r < 150.0; r += 0.5) {
            EXPECT_GE(profile.Height(r, d, worn), 0.0) << freshness << " at " << r;
        }
        EXPECT_NEAR(profile.Height(140.0, d, worn), freshness * profile.Height(140.0, d, fresh),
                    1e-4)
            << "beyond the rounded rim the ejecta is the fresh ejecta lowered with the rim";
    }
}

namespace {

/// A fresh 60 km lunar complex crater prepared at the origin, its terraces set from `terraces`.
struct TerracedCrater {
    std::shared_ptr<const CraterProfile> profile;
    PreparedCrater prepared;
    terraingen::CraterRecord record{};
    std::vector<float> data;

    explicit TerracedCrater(const terraingen::CraterTerraces& terraces, double freshness = 1.0,
                            std::uint64_t id = 7u)
        : profile(std::make_shared<terraingen::ComplexCraterProfile>(
              std::make_shared<terraingen::FassettThomsonProfile>(),
              terraingen::LunarComplexCraterShape())),
          prepared{Crater{0.0, 0.0, 60000.0, 0.0, id}, 1.0, State(freshness)} {
        record.diameter = 60000.0f;
        terraingen::AppendKernelTerraces(prepared, terraces, *profile, record, data);
        if (data.empty()) {
            data.push_back(0.0f);
        }
    }

    static CraterState State(double freshness) {
        const terraingen::CraterMorphometry fresh =
            terraingen::LunarFreshCraterMorphometry(60000.0);
        CraterState state{fresh, fresh};
        state.current.depthRatio *= freshness;
        return state;
    }

    double Radii(double radii, double theta) const {
        terraingen::CraterData view;
        view.values = data.data();
        return terraingen::KernelTerracedRadii(view, record, static_cast<float>(radii),
                                               static_cast<float>(theta));
    }
};

terraingen::CraterTerraces Terraces() {
    terraingen::CraterTerraces terraces;
    terraces.enabled = true;
    terraces.seed = 3u;
    return terraces;
}

/// Expects `crater` to map its wall monotonically between an untouched floor, rim crest and
/// exterior, with benches and scarps toward most azimuths; missing blocks and arcs leave the rest
/// unstepped.
void ExpectSteppedMonotonicWall(const TerracedCrater& crater) {
    ASSERT_GE(crater.record.terraceCount, 1u);
    const double inner = crater.data[0];
    EXPECT_GT(inner, 0.2);
    EXPECT_LT(inner, 0.9);
    int azimuths = 0;
    int stepped = 0;
    for (double theta = 0.0; theta < 6.28; theta += 0.1) {
        EXPECT_EQ(crater.Radii(0.5 * inner, theta), static_cast<float>(0.5 * inner))
            << "the floor is untouched";
        EXPECT_EQ(crater.Radii(1.0, theta), 1.0f) << "the rim crest stays";
        EXPECT_EQ(crater.Radii(1.4, theta), static_cast<float>(1.4)) << "the exterior is untouched";
        double previous = inner;
        double flattest = 1e9;
        double steepest = 0.0;
        const double step = (1.0 - inner) / 2000.0;
        for (double x = inner + step; x < 1.0; x += step) {
            const double mapped = crater.Radii(x, theta);
            ASSERT_GE(mapped, previous) << "monotonic at " << x;
            flattest = std::min(flattest, (mapped - previous) / step);
            steepest = std::max(steepest, (mapped - previous) / step);
            previous = mapped;
        }
        ++azimuths;
        stepped += flattest < 0.6 && steepest > 1.2 ? 1 : 0;
    }
    EXPECT_GT(stepped, 0.75 * azimuths) << "benches and scarps toward most azimuths";
}

}  // namespace

TEST(CraterTerraces, StepTheWallMonotonicallyBetweenTheFloorAndTheRim) {
    terraingen::CraterTerraces stepped = Terraces();
    stepped.blockOffset = 1.0;
    stepped.rimVariation = 0.3;
    stepped.maxBenches = 8;
    stepped.fullDiameter = 30000.0;
    for (const auto& terraces : {Terraces(), stepped}) {
        SCOPED_TRACE(terraces.maxBenches);
        ExpectSteppedMonotonicWall(TerracedCrater(terraces));
    }
}

TEST(CraterTerraces, DisplaceTheWallRatherThanCutIt) {
    terraingen::CraterTerraces whole = Terraces();
    whole.arcShare = 1.0;
    whole.blockGaps = 0.0;
    for (const int benches : {1, 4}) {
        whole.minBenches = benches;
        whole.maxBenches = benches;
        const TerracedCrater crater(whole);
        ASSERT_EQ(crater.record.terraceCount, static_cast<std::uint32_t>(benches));
        const double radius = 30000.0;
        const double inner = crater.data[0];
        const auto height = [&](double radii) {
            return crater.profile->Height(radii * radius, 60000.0, crater.prepared.state);
        };
        double net = 0.0;
        double moved = 0.0;
        double above = 0.0;
        for (double theta = -3.1; theta < 3.1; theta += 0.1) {
            for (double x = inner + 0.0005; x < 1.0; x += 0.001) {
                const double change = height(crater.Radii(x, theta)) - height(x);
                net += x * change;
                moved += x * std::abs(change);
                above = std::max(above, change);
            }
        }
        SCOPED_TRACE(benches);
        EXPECT_GT(moved, 0.0);
        EXPECT_LT(std::abs(net), 0.25 * moved) << "the wall keeps about its volume";
        EXPECT_GT(above, 100.0) << "slumped toes stand above the unterraced wall";
    }
}

TEST(CraterTerraces, ScallopTheRimCrestAroundTheRoundRadius) {
    const TerracedCrater crater(Terraces());
    terraingen::CraterData view;
    view.values = crater.data.data();
    double lowest = 2.0;
    double highest = 0.0;
    double sum = 0.0;
    constexpr int kSamples = 3600;
    for (int n = 0; n < kSamples; ++n) {
        const double scale = terraingen::KernelTerraceRimScale(
            view, crater.record,
            static_cast<float>(2.0 * terraingen::kPi * n / kSamples - terraingen::kPi));
        lowest = std::min(lowest, scale);
        highest = std::max(highest, scale);
        sum += scale;
    }
    EXPECT_NEAR(sum / kSamples, 1.0, 1e-3) << "the mean radius stays";
    EXPECT_GT(highest - lowest, 0.01) << "scalloped";
    EXPECT_LT(highest - lowest, 0.08 * 0.5 * 1.4 + 1e-6) << "within the rim variation";
}

TEST(CraterTerraces, BreakBenchesIntoBlocksThatStepAndGoMissing) {
    terraingen::CraterTerraces whole = Terraces();
    whole.arcShare = 1.0;
    whole.blockGaps = 0.0;
    terraingen::CraterTerraces gappy = whole;
    gappy.blockGaps = 0.5;
    const auto benched = [](const TerracedCrater& crater) {
        const double inner = crater.data[0];
        const double step = (1.0 - inner) / 500.0;
        int count = 0;
        for (double theta = -3.1; theta < 3.1; theta += 0.02) {
            for (double x = inner + step; x < 1.0 - step; x += step) {
                count += crater.Radii(x + step, theta) - crater.Radii(x, theta) < 0.3 * step;
            }
        }
        return count;
    };
    const int full = benched(TerracedCrater(whole));
    EXPECT_GT(full, 0);
    EXPECT_LT(benched(TerracedCrater(gappy)), 0.8 * full) << "missing blocks leave no bench";
    terraingen::CraterTerraces level = whole;
    level.blockOffset = 0.0;
    const TerracedCrater offset(whole);
    const TerracedCrater straight(level);
    int moved = 0;
    for (double theta = -3.1; theta < 3.1; theta += 0.05) {
        const double x = double{offset.data[0]} + 0.5 * (1.0 - double{offset.data[0]});
        moved += offset.Radii(x, theta) != straight.Radii(x, theta) ? 1 : 0;
    }
    EXPECT_GT(moved, 10) << "block offsets move the scarps";
}

TEST(CraterTerraces, VaryAroundTheRimAndByCraterButRepeatForTheSameCrater) {
    const TerracedCrater a(Terraces(), 1.0, 7u);
    const TerracedCrater again(Terraces(), 1.0, 7u);
    const TerracedCrater b(Terraces(), 1.0, 8u);
    EXPECT_EQ(a.data, again.data);
    EXPECT_NE(a.data, b.data);
    const double x = double{a.data[0]} + 0.4 * (1.0 - double{a.data[0]});
    EXPECT_NE(a.Radii(x, 0.3), a.Radii(x, 2.3)) << "benches wander around the rim";
}

TEST(CraterTerraces, AreOffBelowTheOnsetWhenDisabledAndOnceWorn) {
    terraingen::CraterTerraces small = Terraces();
    small.onsetDiameter = 80000.0;
    small.fullDiameter = 100000.0;
    EXPECT_EQ(TerracedCrater(small).record.terraceCount, 0u);
    terraingen::CraterTerraces off = Terraces();
    off.enabled = false;
    EXPECT_EQ(TerracedCrater(off).record.terraceCount, 0u);
    EXPECT_EQ(TerracedCrater(Terraces(), 0.25).record.terraceCount, 0u) << "worn below 0.3";
    const TerracedCrater worn(Terraces(), 0.5);
    const TerracedCrater fresh(Terraces(), 1.0);
    ASSERT_GE(worn.record.terraceCount, 1u);
    EXPECT_LT(worn.data[2], fresh.data[2]) << "softer as the crater wears";
    EXPECT_TRUE(terraingen::DefaultCraterApply().terraces.enabled) << "on for crater layers";
    EXPECT_FALSE(CraterApplyOptions{}.terraces.enabled);
}

TEST(CraterTerraces, GiveMoreBenchesAndFullerArcsToLargerCraters) {
    terraingen::CraterTerraces terraces = Terraces();
    terraces.onsetDiameter = 59000.0;
    terraces.fullDiameter = 200000.0;
    const TerracedCrater nearOnset(terraces);
    terraces.onsetDiameter = 10000.0;
    terraces.fullDiameter = 50000.0;
    const TerracedCrater full(terraces);
    EXPECT_LE(nearOnset.record.terraceCount, 2u);
    EXPECT_EQ(full.record.terraceCount, 4u);
    EXPECT_GT(full.data[4], -1.5f) << "even full craters have benches of limited length";
    EXPECT_LT(full.data[4], nearOnset.data[4]) << "more of the wall from the full diameter";
    terraces.arcShare = 1.0;
    EXPECT_LT(TerracedCrater(terraces).data[4], -1.5f)
        << "an arc share of 1 goes all the way round";
}

TEST(CraterTerraces, RejectInvalidSettings) {
    const auto rejects = [](auto change) {
        terraingen::CraterTerraces terraces;
        change(terraces);
        EXPECT_THROW(terraingen::ValidateCraterTerraces(terraces), std::invalid_argument);
    };
    EXPECT_NO_THROW(terraingen::ValidateCraterTerraces(terraingen::CraterTerraces{}));
    rejects([](auto& t) { t.onsetDiameter = 0.0; });
    rejects([](auto& t) { t.fullDiameter = t.onsetDiameter; });
    rejects([](auto& t) { t.minBenches = 0; });
    rejects([](auto& t) { t.maxBenches = 13; });
    rejects([](auto& t) { t.minBenches = 5; });
    rejects([](auto& t) { t.benchSlopeDegrees = 45.0; });
    rejects([](auto& t) { t.scarpShare = 1.0; });
    rejects([](auto& t) { t.arcShare = 0.0; });
    rejects([](auto& t) { t.rimVariation = -0.1; });
    rejects([](auto& t) { t.blockOffset = 1.1; });
    rejects([](auto& t) { t.blockGaps = 1.0; });
    rejects([](auto& t) { t.scarpSlopeDegrees = t.benchSlopeDegrees; });
    rejects([](auto& t) { t.scarpSlopeDegrees = 60.0; });
    rejects([](auto& t) { t.minFreshness = 1.0; });
}

namespace {

/// A 60 km lunar complex crater prepared at the origin, its central peak set from `peaks`.
struct PeakedCrater {
    std::shared_ptr<const CraterProfile> profile;
    PreparedCrater prepared;
    terraingen::CraterRecord record{};
    std::vector<float> data;

    explicit PeakedCrater(const terraingen::CraterPeaks& peaks, double freshness = 1.0,
                          std::uint64_t id = 7u, double diameter = 60000.0)
        : profile(std::make_shared<terraingen::ComplexCraterProfile>(
              std::make_shared<terraingen::FassettThomsonProfile>(),
              terraingen::LunarComplexCraterShape())),
          prepared{Crater{0.0, 0.0, diameter, 0.0, id}, 1.0, TerracedCrater::State(freshness)} {
        record.diameter = static_cast<float>(diameter);
        terraingen::AppendKernelPeaks(prepared, peaks, *profile, record, data);
        if (data.empty()) {
            data.push_back(0.0f);
        }
    }

    /// The peak's height (m) at `px` east and `py` north of the center, in peak base radii, for
    /// a peak 1000 m high on a base 5000 m in radius.
    double Height(double px, double py) const {
        terraingen::CraterData view;
        view.values = data.data();
        terraingen::KernelPeakSize size;
        size.height = 1000.0f;
        size.base = 5000.0f;
        return terraingen::KernelPeakMassifs(view, record, size, static_cast<float>(5000.0 * px),
                                             static_cast<float>(5000.0 * py));
    }
};

terraingen::CraterPeaks Peaks() {
    terraingen::CraterPeaks peaks;
    peaks.enabled = true;
    peaks.seed = 3u;
    return peaks;
}

/// The highest point of `crater`'s peak on a grid of `step` base radii out to 2.6, with where it
/// is.
struct Summit {
    double height = 0.0;
    double x = 0.0;
    double y = 0.0;
};

Summit HighestPoint(const PeakedCrater& crater, double step = 0.02) {
    Summit summit;
    for (double y = -2.6; y <= 2.6; y += step) {
        for (double x = -2.6; x <= 2.6; x += step) {
            const double height = crater.Height(x, y);
            if (height > summit.height) {
                summit = {height, x, y};
            }
        }
    }
    return summit;
}

}  // namespace

TEST(CraterPeaks, RaiseSeveralMassifsAboutTheCenterRepeatablyPerCrater) {
    terraingen::CraterPeaks peaks = Peaks();
    peaks.minMassifs = 3;
    peaks.maxMassifs = 3;
    const PeakedCrater crater(peaks);
    ASSERT_EQ(crater.record.peakCount, 3u);
    EXPECT_EQ(crater.data.size(), terraingen::kPeakHeader + 3u * terraingen::kPeakMassifValues);
    const Summit summit = HighestPoint(crater, 0.05);
    EXPECT_GT(summit.height, 500.0);
    EXPECT_LT(summit.height, 1000.0 * (1.0 + peaks.roughness) + 1e-3);
    EXPECT_EQ(crater.Height(2.7, 0.0), 0.0) << "nothing beyond the massifs' reach";
    EXPECT_EQ(PeakedCrater(peaks).data, crater.data);
    EXPECT_NE(PeakedCrater(peaks, 1.0, 8u).data, crater.data);
}

TEST(CraterPeaks, SharpenAndTiltTheirMassifs) {
    terraingen::CraterPeaks single = Peaks();
    single.maxMassifs = 1;
    single.roughness = 0.0;
    single.warp = 0.0;
    single.lopsidedness = 0.0;
    const auto volume = [](const PeakedCrater& crater) {
        double sum = 0.0;
        for (double y = -1.5; y <= 1.5; y += 0.02) {
            for (double x = -1.5; x <= 1.5; x += 0.02) {
                sum += crater.Height(x, y);
            }
        }
        return sum;
    };
    terraingen::CraterPeaks domed = single;
    domed.sharpness = 0.0;
    terraingen::CraterPeaks spired = single;
    spired.sharpness = 1.0;
    EXPECT_LT(volume(PeakedCrater(spired)), 0.8 * volume(PeakedCrater(domed)))
        << "sharp massifs are spires";
    const PeakedCrater upright(single);
    const Summit centered = HighestPoint(upright);
    EXPECT_NEAR(centered.height, 1000.0, 50.0) << "the grid misses the sharp summit a little";
    terraingen::CraterPeaks tilted = single;
    tilted.lopsidedness = 1.0;
    const Summit leaning = HighestPoint(PeakedCrater(tilted));
    EXPECT_NEAR(leaning.height, 1000.0, 50.0);
    EXPECT_GT(std::hypot(leaning.x - centered.x, leaning.y - centered.y), 0.04)
        << "a lopsided summit sits off its massif's center";
}

TEST(CraterPeaks, AreOffWhenDisabledForSimpleCratersAndSmootherOnceWorn) {
    terraingen::CraterPeaks off = Peaks();
    off.enabled = false;
    EXPECT_EQ(PeakedCrater(off).record.peakCount, 0u);
    EXPECT_EQ(PeakedCrater(Peaks(), 1.0, 7u, 5000.0).record.peakCount, 0u)
        << "below the complex transition";
    PreparedCrater simple{Crater{0.0, 0.0, 60000.0, 0.0, 7u}, 1.0, TerracedCrater::State(1.0)};
    terraingen::CraterRecord record{};
    std::vector<float> data;
    terraingen::AppendKernelPeaks(simple, Peaks(), terraingen::FassettThomsonProfile(), record,
                                  data);
    EXPECT_EQ(record.peakCount, 0u) << "only complex profiles";
    const PeakedCrater fresh(Peaks());
    const PeakedCrater worn(Peaks(), 0.5);
    EXPECT_FLOAT_EQ(worn.data[0], 0.5f * fresh.data[0]) << "roughness fades as the crater wears";
    EXPECT_TRUE(terraingen::DefaultCraterApply().peaks.enabled) << "on for crater layers";
    EXPECT_FALSE(CraterApplyOptions{}.peaks.enabled);
}

TEST(CraterPeaks, RejectInvalidSettings) {
    const auto rejects = [](auto change) {
        terraingen::CraterPeaks peaks;
        change(peaks);
        EXPECT_THROW(terraingen::ValidateCraterPeaks(peaks), std::invalid_argument);
    };
    EXPECT_NO_THROW(terraingen::ValidateCraterPeaks(terraingen::CraterPeaks{}));
    rejects([](auto& p) { p.minMassifs = 0; });
    rejects([](auto& p) { p.maxMassifs = 9; });
    rejects([](auto& p) { p.minMassifs = 5; });
    rejects([](auto& p) { p.spreadRadii = 2.1; });
    rejects([](auto& p) { p.elongation = 0.9; });
    rejects([](auto& p) { p.chainSpreadDegrees = 361.0; });
    rejects([](auto& p) { p.sharpness = 1.1; });
    rejects([](auto& p) { p.lopsidedness = -0.1; });
    rejects([](auto& p) { p.heightVariation = 1.5; });
    rejects([](auto& p) { p.roughness = 2.0; });
    rejects([](auto& p) { p.roughnessWavelength = 0.0; });
    rejects([](auto& p) { p.warp = 0.4; });
    rejects([](auto& p) { p.heightScale = 0.0; });
    rejects([](auto& p) { p.widthScale = 3.5; });
}
