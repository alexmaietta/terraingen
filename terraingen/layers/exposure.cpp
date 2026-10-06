#include "terraingen/layers/exposure.h"

#include "terraingen/noise/kernels/perlin.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

/// Smoothstep of where `value` lies between `low` and `high`.
double Share(double value, double low, double high) {
    const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

}  // namespace

void ValidateExposureParams(const ExposureParams& params) {
    if (!(std::isfinite(params.reach) && params.reach > 0.0)) {
        throw std::invalid_argument("Exposure reach must be positive and finite");
    }
    if (!(params.riseRock > params.riseSoil && std::isfinite(params.riseSoil) &&
          std::isfinite(params.riseRock))) {
        throw std::invalid_argument("Exposure riseRock must exceed riseSoil");
    }
    if (!(params.slopeRock > params.slopeSoil && std::isfinite(params.slopeSoil) &&
          std::isfinite(params.slopeRock))) {
        throw std::invalid_argument("Exposure slopeRock must exceed slopeSoil");
    }
    if (!(params.floorCover >= 0.0 && params.floorCover < 1.0)) {
        throw std::invalid_argument("Exposure floorCover must be in [0, 1)");
    }
    if (!(std::isfinite(params.floorPatch) && params.floorPatch > 0.0)) {
        throw std::invalid_argument("Exposure floorPatch must be positive and finite");
    }
}

ExposureField::ExposureField(const ExposureParams& params) : params_(params) {
    ValidateExposureParams(params_);
}

double ExposureField::Exposure(const Ground& ground, double x, double y, double spacing) const {
    const float here = ground.HeightAt(x, y, spacing);
    if (std::isnan(here)) {
        return 0.0;
    }
    constexpr double kDiagonal = 0.70710678118654752;
    constexpr std::array<double, 8> kEast{1.0,  kDiagonal,  0.0, -kDiagonal,
                                          -1.0, -kDiagonal, 0.0, kDiagonal};
    constexpr std::array<double, 8> kNorth{0.0, kDiagonal,  1.0,  kDiagonal,
                                           0.0, -kDiagonal, -1.0, -kDiagonal};
    const double reach = params_.reach;
    std::array<double, 8> heights{};
    double mean = 0.0;
    for (std::size_t k = 0; k < 8; ++k) {
        const float there = ground.HeightAt(x + kEast[k] * reach, y + kNorth[k] * reach, spacing);
        heights[k] = static_cast<double>(std::isnan(there) ? here : there);
        mean += heights[k] / 8.0;
    }
    const double rise = (static_cast<double>(here) - mean) / reach;
    const double slope =
        std::hypot(heights[0] - heights[4], heights[2] - heights[6]) / (2.0 * reach);
    double exposure = std::max(Share(rise, params_.riseSoil, params_.riseRock),
                               Share(slope, params_.slopeSoil, params_.slopeRock));
    if (params_.floorCover > 0.0) {
        const auto px = static_cast<float>(x / params_.floorPatch);
        const auto py = static_cast<float>(y / params_.floorPatch);
        const double noise = std::clamp(
            2.5 *
                (0.7 * static_cast<double>(PerlinNoise2(px, py, params_.seed)) +
                 0.3 * static_cast<double>(PerlinNoise2(4.0f * px, 4.0f * py, params_.seed + 1u))),
            -1.0, 1.0);
        const double threshold = 1.0 - 2.0 * params_.floorCover;
        exposure = std::max(exposure, Share(noise, threshold - 0.2, threshold + 0.2));
    }
    return exposure;
}

void ExposureField::Fill(Raster<float>& raster, const Ground& ground, double spacing,
                         unsigned threads) const {
    ParallelFor(raster.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            const Vec2d centre = raster.CellCenter(col, row);
            raster(col, row) = static_cast<float>(Exposure(ground, centre.x, centre.y, spacing));
        }
    });
}

}  // namespace terraingen
