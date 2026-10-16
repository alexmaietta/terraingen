#include "terraingen/noise/noise_layers.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using terraingen::BasisNoise3;
using terraingen::EvaluateNoiseLayers3;
using terraingen::Fractal3;
using terraingen::FractalParams;
using terraingen::FractalType;
using terraingen::HasBasisNoise3;
using terraingen::LatticePoint2;
using terraingen::LatticePoint3;
using terraingen::LayerBlend;
using terraingen::NoiseBasis;
using terraingen::NoiseLayer;
using terraingen::PerlinNoise3;
using terraingen::SplitLattice2;
using terraingen::SplitLattice3;
using terraingen::ValueNoise3;

TEST(ValueNoise3, InterpolatesLatticeValuesWithinRangeAndContinuously) {
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < 4000; ++i) {
        const float fx = static_cast<float>(i % 13) / 13.0f;
        const float fy = static_cast<float>(i % 7) / 7.0f;
        const float fz = static_cast<float>(i % 11) / 11.0f;
        const float v = ValueNoise3({i % 17 - 8, i / 17 - 100, i % 5, fx, fy, fz}, 3u);
        EXPECT_GE(v, -1.0f);
        EXPECT_LE(v, 1.0f);
        sum += static_cast<double>(v);
        ++count;
    }
    EXPECT_NEAR(sum / count, 0.0, 0.1);
    EXPECT_NEAR(ValueNoise3({2, 3, 4, 0.999999f, 0.5f, 0.25f}, 9u),
                ValueNoise3({3, 3, 4, 0.0f, 0.5f, 0.25f}, 9u), 1e-4f);
    EXPECT_NEAR(ValueNoise3({2, 3, 4, 0.5f, 0.5f, 0.999999f}, 9u),
                ValueNoise3({2, 3, 5, 0.5f, 0.5f, 0.0f}, 9u), 1e-4f);
    EXPECT_NE(ValueNoise3({2, 3, 4, 0.0f, 0.0f, 0.0f}, 9u),
              ValueNoise3({2, 3, 4, 0.0f, 0.0f, 0.0f}, 10u));
}

TEST(BasisNoise3, OffersPerlinAndValueNoise) {
    EXPECT_TRUE(HasBasisNoise3(NoiseBasis::Perlin));
    EXPECT_TRUE(HasBasisNoise3(NoiseBasis::Value));
    EXPECT_FALSE(HasBasisNoise3(NoiseBasis::Simplex));
    EXPECT_FALSE(HasBasisNoise3(NoiseBasis::WorleyF1));
    const LatticePoint3 point = {1, 2, 3, 0.3f, 0.6f, 0.2f};
    EXPECT_EQ(BasisNoise3(NoiseBasis::Perlin, point, 5u), PerlinNoise3(point, 5u));
    EXPECT_EQ(BasisNoise3(NoiseBasis::Value, point, 5u), ValueNoise3(point, 5u));
    EXPECT_EQ(BasisNoise3(NoiseBasis::Simplex, point, 5u), PerlinNoise3(point, 5u));
    EXPECT_EQ(PerlinNoise3(1.3f, 2.6f, 3.2f, 5u),
              PerlinNoise3(SplitLattice3(1.3f, 2.6f, 3.2f), 5u));
}

TEST(LatticePoint, SplitsAtTheFloorOfEachCoordinate) {
    const LatticePoint2 a = SplitLattice2(-1.25f, 2.5f);
    EXPECT_EQ(a.ix, -2);
    EXPECT_EQ(a.iy, 2);
    EXPECT_EQ(a.fx, 0.75f);
    EXPECT_EQ(a.fy, 0.5f);
    const LatticePoint3 b = SplitLattice3(0.25f, -0.5f, 7.0f);
    EXPECT_EQ(b.iy, -1);
    EXPECT_EQ(b.fy, 0.5f);
    EXPECT_EQ(b.iz, 7);
    EXPECT_EQ(b.fz, 0.0f);
    const LatticePoint2 c = terraingen::SplitLatticePrecise2(3.0e9 + 0.25, -0.75);
    EXPECT_EQ(c.ix, static_cast<std::int32_t>(static_cast<std::int64_t>(3.0e9)));
    EXPECT_EQ(c.fx, 0.25f);
    EXPECT_EQ(c.iy, -1);
    const LatticePoint3 d = terraingen::SplitLatticePrecise3(1737400.125, -0.5, 2.0);
    EXPECT_EQ(d.ix, 1737400);
    EXPECT_EQ(d.fx, 0.125f);
    EXPECT_EQ(d.iy, -1);
    EXPECT_EQ(d.iz, 2);
}

TEST(LatticeKeys, KeepTheirFrozenLayouts) {
    using namespace terraingen::detail;  // NOLINT(google-build-using-namespace): the keys
    EXPECT_EQ(SeededLatticeKey2(1u, 2u, 3u), (terraingen::UVec3{1u, 2u, 3u}));
    EXPECT_EQ(PerlinLatticeKey3(1u, 2u, 3u, 4u),
              (terraingen::UVec3{1u, 2u, 3u + terraingen::PcgHash(4u)}));
    EXPECT_EQ(ValueLatticeKey3(1u, 2u, 3u, 0xf0u), (terraingen::UVec3{1u, 2u, 3u ^ 0xf0u}));
    EXPECT_EQ(OctaveKey(7u, 2u, 1u), (terraingen::UVec3{7u, 2u, 1u}));
}

TEST(DomainWarp3, UsesThreeSuccessiveWarpFields) {
    NoiseLayer layer;
    layer.fractal.seed = 3u;
    layer.warpAmplitude = 25.0f;
    layer.warpWavelength = 300.0f;
    FractalParams warp = terraingen::WarpParams(layer);
    EXPECT_EQ(warp.octaves, 4);
    EXPECT_EQ(warp.amplitude, 25.0f);
    EXPECT_EQ(warp.wavelength, 300.0f);
    EXPECT_EQ(warp.seed, terraingen::PcgHash(3u ^ 0x5bd1e995u));
    const auto offset = terraingen::DomainWarp3(10.0, 20.0, 30.0, layer);
    EXPECT_EQ(offset.dx, static_cast<double>(Fractal3(10.0, 20.0, 30.0, warp)));
    warp.seed = terraingen::PcgHash(warp.seed);
    EXPECT_EQ(offset.dy, static_cast<double>(Fractal3(10.0, 20.0, 30.0, warp)));
    warp.seed = terraingen::PcgHash(warp.seed);
    EXPECT_EQ(offset.dz, static_cast<double>(Fractal3(10.0, 20.0, 30.0, warp)));
    FractalParams unit = layer.fractal;
    unit.amplitude = 1.0f;
    EXPECT_EQ(terraingen::EvaluateNoiseLayer3(10.0, 20.0, 30.0, layer),
              Fractal3(10.0 + offset.dx, 20.0 + offset.dy, 30.0 + offset.dz, unit));
}

TEST(Fractal3, StaysWithinItsAmplitudeForEveryTypeAndIsPreciseFarFromTheOrigin) {
    for (const FractalType type : {FractalType::Fbm, FractalType::Ridged, FractalType::Billow,
                                   FractalType::HybridMultifractal}) {
        FractalParams params;
        params.type = type;
        params.amplitude = 7.0f;
        params.wavelength = 50.0f;
        params.octaves = 5;
        for (int i = 0; i < 500; ++i) {
            const float h = Fractal3(1737400.0 + 3.1 * i, -12.7 * i, 0.5 * i, params);
            EXPECT_LE(std::fabs(h), 7.0f + 1e-4f);
        }
    }
    FractalParams fine;
    fine.wavelength = 1.0f;
    fine.octaves = 1;
    fine.amplitude = 1.0f;
    const double far = 1737400.0;
    const float a = Fractal3(far, 0.0, 0.0, fine);
    const float b = Fractal3(far + 0.01, 0.0, 0.0, fine);
    const float c = Fractal3(far + 0.4, 0.0, 0.0, fine);
    EXPECT_NE(a, b);
    EXPECT_LT(std::fabs(a - b), 0.1f);
    EXPECT_NE(a, c);
}

TEST(Fractal3, TurnsEachOctaveOffTheLatticeAxes) {
    FractalParams params;
    params.seed = 9u;
    params.octaves = 2;
    params.wavelength = 50.0f;
    params.amplitude = 1.0f;
    const double x = 13.0;
    const double y = -40.0;
    const double z = 7.5;
    const auto h0 = terraingen::Pcg3dHash({params.seed, 0u, 0u});
    const auto g0 = terraingen::Pcg3dHash({params.seed, 0u, 1u});
    const auto h1 = terraingen::Pcg3dHash({params.seed, 1u, 0u});
    const auto g1 = terraingen::Pcg3dHash({params.seed, 1u, 1u});
    const double turn = 3.14159265358979 * (3.0 - std::sqrt(5.0));
    const double ux = 1.0 / std::sqrt(14.0);
    const double uy = 2.0 / std::sqrt(14.0);
    const double uz = 3.0 / std::sqrt(14.0);
    const double c = std::cos(turn);
    const double s = std::sin(turn);
    const double t = 1.0 - c;
    const double rx =
        (t * ux * ux + c) * x + (t * ux * uy - s * uz) * y + (t * ux * uz + s * uy) * z;
    const double ry =
        (t * ux * uy + s * uz) * x + (t * uy * uy + c) * y + (t * uy * uz - s * ux) * z;
    const double rz =
        (t * ux * uz - s * uy) * x + (t * uy * uz + s * ux) * y + (t * uz * uz + c) * z;
    const auto unit = [](std::uint32_t h) {
        return static_cast<double>(terraingen::HashToUnitFloat(h));
    };
    const float first = terraingen::PerlinNoise3Precise(
        x / 50.0 + unit(h0.y), y / 50.0 + unit(h0.z), z / 50.0 + unit(g0.x), h0.x);
    const float second = terraingen::PerlinNoise3Precise(
        rx / 25.0 + unit(h1.y), ry / 25.0 + unit(h1.z), rz / 25.0 + unit(g1.x), h1.x);
    EXPECT_NEAR(Fractal3(x, y, z, params), (first + 0.5f * second) / 1.5f, 1e-5f);
}

TEST(EvaluateNoiseLayers3, BlendsWarpsAndSkipsDisabledLayers) {
    NoiseLayer base;
    base.fractal.amplitude = 10.0f;
    base.fractal.wavelength = 100.0f;
    NoiseLayer detail = base;
    detail.fractal.seed = 4;
    detail.fractal.amplitude = 2.0f;
    const double x = 1000.0;
    const double y = -2000.0;
    const double z = 1.7e6;
    FractalParams unit = base.fractal;
    unit.amplitude = 1.0f;
    const float first = 10.0f * Fractal3(x, y, z, unit);
    unit = detail.fractal;
    unit.amplitude = 1.0f;
    const float second = Fractal3(x, y, z, unit);
    const NoiseLayer pair[] = {base, detail};
    EXPECT_FLOAT_EQ(EvaluateNoiseLayers3(x, y, z, pair, 2), first + 2.0f * second);
    for (const LayerBlend blend : {LayerBlend::Max, LayerBlend::Min, LayerBlend::Mask}) {
        NoiseLayer blended = detail;
        blended.blend = blend;
        const NoiseLayer layers[] = {base, blended};
        const float expected = blend == LayerBlend::Max   ? std::max(first, 2.0f * second)
                               : blend == LayerBlend::Min ? std::min(first, 2.0f * second)
                                                          : first * 0.5f * (second + 1.0f);
        EXPECT_FLOAT_EQ(EvaluateNoiseLayers3(x, y, z, layers, 2), expected);
    }
    NoiseLayer off = base;
    off.enabled = false;
    const NoiseLayer skipped[] = {off, detail};
    EXPECT_FLOAT_EQ(EvaluateNoiseLayers3(x, y, z, skipped, 2), 2.0f * second);
    EXPECT_EQ(EvaluateNoiseLayers3(x, y, z, &off, 1), 0.0f);
    NoiseLayer warped = base;
    warped.warpAmplitude = 30.0f;
    warped.warpWavelength = 400.0f;
    EXPECT_NE(EvaluateNoiseLayers3(x, y, z, &warped, 1), first);
    EXPECT_LE(std::fabs(EvaluateNoiseLayers3(x, y, z, &warped, 1)), 10.0f + 1e-4f);
}
