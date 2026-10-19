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

namespace {

struct BodySurface {
    std::string name;
    terraingen::Surface surface;
    terraingen::Vec2d centre;  ///< Longitude and latitude in degrees.
    double step;               ///< Grid cell side in degrees.
    double maxDiameter;        ///< Largest crater on the grid, in metres.
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
/// body's centre.
terraingen::CraterList BodyCraters(const BodySurface& body, std::size_t side, int count) {
    terraingen::CraterList craters;
    const double half = 0.5 * static_cast<double>(side) * body.step;
    for (int i = 0; i < count; ++i) {
        const double u = std::fmod(0.37 * i, 1.0);
        const double v = std::fmod(0.53 * i + 0.1, 1.0);
        const double d = body.maxDiameter * (0.02 + 0.98 * std::fmod(0.29 * i, 1.0));
        craters.Append({body.centre.x - half + 2.0 * half * u,
                        body.centre.y - half + 2.0 * half * v, d, 0.002 * (i % 40),
                        static_cast<std::uint64_t>(i + 1)});
    }
    return craters;
}

/// A longitude-latitude grid of `side` cells centred on `body`'s centre, sloping east.
terraingen::Raster<float> BodyGrid(const BodySurface& body, std::size_t side) {
    terraingen::Raster<float> grid(side, side, 0.0f);
    const double half = 0.5 * static_cast<double>(side) * body.step;
    grid.SetTransform(
        terraingen::GeoTransform::NorthUp(body.centre.x - half, body.centre.y + half, body.step));
    for (std::size_t i = 0; i < grid.Size(); ++i) {
        grid.Values()[i] = 0.02f * static_cast<float>(i % side);
    }
    return grid;
}

/// The largest difference between two rasters of one size, in metres.
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
        const Crater crater{body.centre.x, body.centre.y, body.maxDiameter, 0.0, 1u};
        const terraingen::Vec2d corner = body.surface.Locate(body.centre, {-900.0, 700.0});
        const terraingen::Vec3d origin = body.surface.Position(corner);
        const terraingen::CraterFrame frame =
            terraingen::MakeCraterFrame(body.surface, crater, origin);
        for (const double metres : {0.0, 0.01, 1.0, 37.0, 250.0, 1200.0}) {
            for (const double azimuth : {0.0, 0.7, 2.2, 3.9, 5.5}) {
                const terraingen::Vec2d point = body.surface.Locate(
                    body.centre, {metres * std::sin(azimuth), metres * std::cos(azimuth)});
                const terraingen::Vec3d p = body.surface.Position(point) - origin;
                const terraingen::KernelOffset kernel =
                    terraingen::KernelFrameOffset(frame, static_cast<float>(p.x),
                                                  static_cast<float>(p.y), static_cast<float>(p.z));
                const terraingen::KernelOffset exact =
                    terraingen::ShapingOffset(body.surface, crater, point);
                const double tolerance = 1e-3 + 1e-6 * metres;
                EXPECT_NEAR(kernel.dx, exact.dx, tolerance) << body.name << " at " << metres;
                EXPECT_NEAR(kernel.dy, exact.dy, tolerance) << body.name << " at " << metres;
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
            options.slope.enabled = datum == terraingen::CraterDatum::Plane;
            options.slope.fullSlope = 0.002;
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
