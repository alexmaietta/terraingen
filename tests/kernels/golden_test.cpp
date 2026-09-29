#include "terraingen/kernels/fbm.h"

#include <gtest/gtest.h>

using terraingen::Fbm2;
using terraingen::FbmParams;
using terraingen::Pcg3dHash;
using terraingen::PcgHash;
using terraingen::PerlinNoise2;
using terraingen::UVec3;

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

TEST(KernelsGolden, Fbm2) {
    FbmParams params;
    params.seed = 42u;
    EXPECT_EQ(Fbm2(0.0f, 0.0f, params), 0x1.822c5cp+3f);
    EXPECT_EQ(Fbm2(0x1.004p+9f, -0x1.001p+10f, params), -0x1.278372p+3f);
    EXPECT_EQ(Fbm2(-0x1.77p+11f, 0x1.000cp+12f, params), -0x1.ef2c68p+2f);
}
