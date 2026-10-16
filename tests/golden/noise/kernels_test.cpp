#include "terraingen/noise/noise_layers.h"

#include <gtest/gtest.h>

using terraingen::EvaluateNoiseLayers;
using terraingen::Fractal2;
using terraingen::FractalParams;
using terraingen::FractalType;
using terraingen::NoiseBasis;
using terraingen::NoiseLayer;
using terraingen::Pcg3dHash;
using terraingen::PcgHash;
using terraingen::PerlinNoise2;
using terraingen::SimplexNoise2;
using terraingen::UVec3;
using terraingen::ValueNoise2;
using terraingen::Worley2;

TEST(KernelsGolden, PcgHash) {
    EXPECT_EQ(PcgHash(0u), 129708002u);
    EXPECT_EQ(PcgHash(1u), 2831084092u);
    EXPECT_EQ(PcgHash(0xDEADBEEFu), 1730779506u);
}

TEST(KernelsGolden, Pcg3dHash) {
    EXPECT_EQ(Pcg3dHash(UVec3{1u, 2u, 3u}), (UVec3{4204755366u, 1223881804u, 1500469937u}));
}

TEST(KernelsGolden, PerlinNoise2) {
    EXPECT_EQ(PerlinNoise2(0x1p-2f, 0x1.8p-1f, 1234u), 0x1.93a3e6p-4f);
    EXPECT_EQ(PerlinNoise2(-0x1.9p+3f, 0x1.9p+1f, 1234u), -0x1.459948p-1f);
    EXPECT_EQ(PerlinNoise2(0x1.f42666p+9f, -0x1.f56666p+7f, 1234u), 0x1.0de226p-1f);
}

TEST(KernelsGolden, PerlinFbm) {
    FractalParams params;
    params.seed = 42u;
    EXPECT_EQ(Fractal2(0.0f, 0.0f, params), 0x1.822c5cp+3f);
    EXPECT_EQ(Fractal2(0x1.004p+9f, -0x1.001p+10f, params), -0x1.a18ed6p+0f);
    EXPECT_EQ(Fractal2(-0x1.77p+11f, 0x1.000cp+12f, params), 0x1.972ec2p+3f);
}

TEST(KernelsGolden, SimplexValueAndWorley) {
    EXPECT_EQ(SimplexNoise2(0x1p-2f, 0x1.8p-1f, 1234u), 0x1.bac986p-4f);
    EXPECT_EQ(SimplexNoise2(-0x1.9p+3f, 0x1.9p+1f, 1234u), 0x1.88e9b6p-2f);
    EXPECT_EQ(ValueNoise2(0x1p-2f, 0x1.8p-1f, 1234u), -0x1.0ed85ap-1f);
    EXPECT_EQ(ValueNoise2(-0x1.9p+3f, 0x1.9p+1f, 1234u), -0x1.a6cecp-2f);
    const auto worley = Worley2(0x1p-2f, 0x1.8p-1f, 1234u);
    EXPECT_EQ(worley.f1, 0x1.0f7b3p-1f);
    EXPECT_EQ(worley.f2, 0x1.2e7808p-1f);
}

TEST(KernelsGolden, FractalTypes) {
    FractalParams params;
    params.seed = 42u;
    params.type = FractalType::Ridged;
    EXPECT_EQ(Fractal2(0x1.004p+9f, -0x1.001p+10f, params), 0x1.8fd9ap+5f);
    params.type = FractalType::Billow;
    EXPECT_EQ(Fractal2(0x1.004p+9f, -0x1.001p+10f, params), -0x1.26132cp+6f);
    params.type = FractalType::HybridMultifractal;
    EXPECT_EQ(Fractal2(0x1.004p+9f, -0x1.001p+10f, params), 0x1.58502ap+5f);
    params.type = FractalType::Fbm;
    params.basis = NoiseBasis::Simplex;
    EXPECT_EQ(Fractal2(0x1.004p+9f, -0x1.001p+10f, params), -0x1.0f204ep+4f);
}

TEST(KernelsGolden, LayersWithWarp) {
    NoiseLayer layers[2];
    layers[0].fractal.seed = 1u;
    layers[1].fractal.seed = 2u;
    layers[1].fractal.basis = NoiseBasis::WorleyF2MinusF1;
    layers[1].fractal.amplitude = 10.0f;
    layers[1].warpAmplitude = 50.0f;
    EXPECT_EQ(EvaluateNoiseLayers(0x1.004p+9f, -0x1.001p+10f, layers, 2), -0x1.38f3cp+4f);
}

TEST(KernelsGolden, VoronoiAndWorley3) {
    EXPECT_EQ(Worley2(0x1p-2f, 0x1.8p-1f, 1234u).value, -0x1.73b024p-1f);
    const auto worley3 = terraingen::Worley3(0x1p-2f, 0x1.8p-1f, -0x1.8p+0f, 1234u);
    EXPECT_EQ(worley3.f1, 0x1.113fap-1f);
    EXPECT_EQ(worley3.f2, 0x1.dc0d32p-1f);
    EXPECT_EQ(worley3.value, -0x1.20f528p-1f);
    EXPECT_EQ(terraingen::BasisNoise2(NoiseBasis::Voronoi, -0x1.9p+3f, 0x1.9p+1f, 1234u),
              -0x1.e2e784p-1f);
    FractalParams params;
    params.seed = 42u;
    params.basis = NoiseBasis::Voronoi;
    EXPECT_EQ(Fractal2(0x1.0008p+9f, -0x1.004p+10f, params), -0x1.ab66bp+5f);
    EXPECT_EQ(terraingen::Fractal3(512.0625, -1025.0, 300.5, params), -0x1.d05156p+5f);
    params.basis = NoiseBasis::WorleyF2MinusF1;
    EXPECT_EQ(terraingen::Fractal3(512.0625, -1025.0, 300.5, params), -0x1.605be8p+6f);
}

TEST(KernelsGolden, Simplex3) {
    EXPECT_EQ(terraingen::SimplexNoise3(0x1p-2f, 0x1.8p-1f, -0x1.8p+0f, 1234u), 0x1.62c8a8p-3f);
    EXPECT_EQ(terraingen::SimplexNoise3(-0x1.9p+3f, 0x1.9p+1f, 0x1.c4p+2f, 1234u), 0x1.24874ep-6f);
    FractalParams params;
    params.seed = 42u;
    params.basis = NoiseBasis::Simplex;
    EXPECT_EQ(terraingen::Fractal3(512.0625, -1025.0, 300.5, params), -0x1.e90b4p+4f);
}
