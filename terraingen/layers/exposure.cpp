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
    if (!(params.riseBoulder > params.riseSoil && std::isfinite(params.riseSoil) &&
          std::isfinite(params.riseBoulder))) {
        throw std::invalid_argument("Exposure riseBoulder must exceed riseSoil");
    }
    if (!(params.slopeBoulder > params.slopeSoil && std::isfinite(params.slopeSoil) &&
          std::isfinite(params.slopeBoulder))) {
        throw std::invalid_argument("Exposure slopeBoulder must exceed slopeSoil");
    }
    if (!(params.floorCover >= 0.0 && params.floorCover < 1.0)) {
        throw std::invalid_argument("Exposure floorCover must be in [0, 1)");
    }
    if (!(std::isfinite(params.floorPatch) && params.floorPatch > 0.0)) {
        throw std::invalid_argument("Exposure floorPatch must be positive and finite");
    }
}

ExposureField::ExposureField(const ExposureParams& params, const Surface& surface)
    : params_(params), surface_(surface) {
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
        const Vec2d at = surface_.Locate({x, y}, {kEast[k] * reach, kNorth[k] * reach});
        const float there = ground.HeightAt(at.x, at.y, spacing);
        heights[k] = static_cast<double>(std::isnan(there) ? here : there);
        mean += heights[k] / 8.0;
    }
    const double rise = (static_cast<double>(here) - mean) / reach;
    const double slope =
        std::hypot(heights[0] - heights[4], heights[2] - heights[6]) / (2.0 * reach);
    double exposure = std::max(Share(rise, params_.riseSoil, params_.riseBoulder),
                               Share(slope, params_.slopeSoil, params_.slopeBoulder));
    if (params_.floorCover > 0.0) {
        double coarse = 0.0;
        double fine = 0.0;
        if (surface_.IsSphere()) {
            const Vec3d p = surface_.Position({x, y});
            const double s = 1.0 / params_.floorPatch;
            coarse =
                static_cast<double>(PerlinNoise3Precise(s * p.x, s * p.y, s * p.z, params_.seed));
            fine = static_cast<double>(PerlinNoise3Precise(4.0 * s * p.x, 4.0 * s * p.y,
                                                           4.0 * s * p.z, params_.seed + 1u));
        } else {
            const auto px = static_cast<float>(x / params_.floorPatch);
            const auto py = static_cast<float>(y / params_.floorPatch);
            coarse = static_cast<double>(PerlinNoise2(px, py, params_.seed));
            fine = static_cast<double>(PerlinNoise2(4.0f * px, 4.0f * py, params_.seed + 1u));
        }
        const double noise = std::clamp(2.5 * (0.7 * coarse + 0.3 * fine), -1.0, 1.0);
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
