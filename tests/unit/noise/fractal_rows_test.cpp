// SPDX-License-Identifier: BSD-3-Clause

#include "noise/fractal_rows.h"

#include "terraingen/noise/surface_noise.h"
#include "terraingen/util/bits.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using terraingen::BitCast;
using terraingen::Fractal;
using terraingen::FractalBlend;
using terraingen::FractalType;
using terraingen::NoiseBasis;

namespace {

/// 700 points, more than two runs, from near the origin out to kilometers, both signs.
void Points(std::vector<float>& xs, std::vector<float>& ys) {
    for (int i = 0; i < 700; ++i) {
        const float scale = i % 3 == 0 ? 1.0f : (i % 3 == 1 ? 97.0f : 4321.0f);
        xs.push_back(scale * (0.731f * static_cast<float>(i % 41) - 13.0f));
        ys.push_back(scale * (0.517f * static_cast<float>(i % 37) - 9.5f));
    }
}

void ExpectSameBits(const std::vector<float>& found, const std::vector<float>& expected,
                    const char* what) {
    ASSERT_EQ(found.size(), expected.size());
    for (std::size_t i = 0; i < found.size(); ++i) {
        ASSERT_EQ(BitCast<std::uint32_t>(found[i]), BitCast<std::uint32_t>(expected[i]))
            << what << " at point " << i << ": " << found[i] << " against " << expected[i];
    }
}

}  // namespace

TEST(Fractal2Rows, GivesFractal2sBitsForEveryBasisAndType) {
    std::vector<float> xs;
    std::vector<float> ys;
    Points(xs, ys);
    for (std::uint32_t basis = 0; basis < terraingen::kNoiseBasisCount; ++basis) {
        for (std::uint32_t type = 0; type < terraingen::kFractalTypeCount; ++type) {
            terraingen::FractalParams params;
            params.seed = 11u + basis;
            params.wavelength = 37.0f;
            params.amplitude = 3.0f;
            params.octaves = 6;
            params.basis = static_cast<NoiseBasis>(basis);
            params.type = static_cast<FractalType>(type);
            params.offset = 0.8f;
            std::vector<float> expected(xs.size());
            for (std::size_t i = 0; i < xs.size(); ++i) {
                expected[i] = terraingen::Fractal2(xs[i], ys[i], params);
            }
            std::vector<float> found(xs.size());
            terraingen::Fractal2Rows(xs, ys, params, found);
            ExpectSameBits(found, expected, "Fractal2");
        }
    }
}

TEST(EvaluateFractalsRows, GivesEvaluateFractalsBitsWithWarpsBlendsAndDisabledFractals) {
    std::vector<float> xs;
    std::vector<float> ys;
    Points(xs, ys);
    std::vector<Fractal> fractals(5, Fractal{});
    fractals[0].wavelength = 400.0f;
    fractals[0].amplitude = 20.0f;
    fractals[0].warpAmplitude = 60.0f;
    fractals[1].enabled = false;
    fractals[2].basis = NoiseBasis::Simplex;
    fractals[2].type = FractalType::Ridged;
    fractals[2].blend = FractalBlend::Max;
    fractals[2].wavelength = 90.0f;
    fractals[3].basis = NoiseBasis::WorleyF2MinusF1;
    fractals[3].blend = FractalBlend::Mask;
    fractals[3].octaves = 3;
    fractals[4].blend = FractalBlend::Min;
    fractals[4].type = FractalType::HybridMultifractal;
    fractals[4].seed = 9u;
    std::vector<float> expected(xs.size());
    for (std::size_t i = 0; i < xs.size(); ++i) {
        expected[i] = terraingen::EvaluateFractals(xs[i], ys[i], fractals.data(), fractals.size());
    }
    std::vector<float> found(xs.size());
    terraingen::EvaluateFractalsRows(xs, ys, fractals.data(), fractals.size(), found);
    ExpectSameBits(found, expected, "EvaluateFractals");
    std::vector<float> none(xs.size(), 5.0f);
    terraingen::EvaluateFractalsRows(xs, ys, fractals.data(), 0, none);
    EXPECT_EQ(none[3], 0.0f) << "no fractals give 0";
}

TEST(FractalStackRows, GivesFractalStackAtsBitsOnPlanesAndBodies) {
    std::vector<Fractal> fractals(2, Fractal{});
    fractals[1].warpAmplitude = 30.0f;
    fractals[1].wavelength = 150.0f;
    std::vector<terraingen::Vec2d> points;
    for (int i = 0; i < 300; ++i) {
        points.push_back({1000.0 + 0.37 * i, -2000.0 + 0.11 * (i % 17)});
    }
    const terraingen::Vec2d origin{990.0, -1990.0};
    for (const terraingen::Surface& surface :
         {terraingen::Surface{}, terraingen::Surface::Sphere(1737400.0)}) {
        std::vector<terraingen::Vec2d> at = points;
        if (surface.IsBody()) {
            for (terraingen::Vec2d& p : at) {
                p = {p.x * 1e-4, p.y * 1e-4};
            }
        }
        std::vector<float> expected(at.size());
        for (std::size_t i = 0; i < at.size(); ++i) {
            expected[i] = terraingen::FractalStackAt(fractals, origin, surface, at[i].x, at[i].y);
        }
        std::vector<float> found(at.size());
        terraingen::FractalStackRows(fractals, origin, surface, at, found);
        ExpectSameBits(found, expected, surface.IsBody() ? "sphere" : "plane");
    }
    std::vector<float> empty(points.size(), 2.0f);
    terraingen::FractalStackRows({}, origin, terraingen::Surface{}, points, empty);
    EXPECT_EQ(empty[0], 0.0f);
}
