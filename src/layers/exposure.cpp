// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/exposure.h"

#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/layer_fields.h"
#include "terraingen/noise/perlin.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/cell_loops.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

/// Smoothstep of where `value` lies between `low` and `high`.
double BetweenSmooth(double value, double low, double high) {
    return Smoothstep((value - low) / (high - low));
}

}  // namespace

void ValidateExposureParams(const ExposureParams& params) {
    CheckRanges(params);
    if (!(params.riseBoulder > params.riseSoil)) {
        throw std::invalid_argument("Exposure riseBoulder must exceed riseSoil");
    }
    if (!(params.slopeBoulder > params.slopeSoil)) {
        throw std::invalid_argument("Exposure slopeBoulder must exceed slopeSoil");
    }
}

ExposureField::ExposureField(const ExposureParams& params, const Surface& surface)
    : params_(params), surface_(surface) {
    ValidateExposureParams(params_);
    constexpr double kDiagonal = 0.70710678118654752;
    constexpr std::array<double, 8> kEast{1.0,  kDiagonal,  0.0, -kDiagonal,
                                          -1.0, -kDiagonal, 0.0, kDiagonal};
    constexpr std::array<double, 8> kNorth{0.0, kDiagonal,  1.0,  kDiagonal,
                                           0.0, -kDiagonal, -1.0, -kDiagonal};
    for (std::size_t k = 0; k < ring_.size(); ++k) {
        ring_[k] = {kEast[k] * params_.reach, kNorth[k] * params_.reach};
    }
}

double ExposureField::Exposure(const Ground& ground, double x, double y, double spacing) const {
    return Exposure(ground, surface_, x, y, spacing);
}

double ExposureField::Exposure(const Ground& ground, const Surface& surface, double x, double y,
                               double spacing) const {
    const float here = ground.HeightAt(x, y, spacing);
    if (std::isnan(here)) {
        return 0.0;
    }
    const double reach = params_.reach;
    std::array<double, 8> heights{};
    GroundAround(ground, surface, {x, y}, ring_, spacing, static_cast<double>(here), heights);
    double mean = 0.0;
    for (const double height : heights) {
        mean += height / 8.0;
    }
    const double rise = (static_cast<double>(here) - mean) / reach;
    const double slope =
        std::hypot(heights[0] - heights[4], heights[2] - heights[6]) / (2.0 * reach);
    double exposure = std::max(BetweenSmooth(rise, params_.riseSoil, params_.riseBoulder),
                               BetweenSmooth(slope, params_.slopeSoil, params_.slopeBoulder));
    if (params_.floorCover > 0.0) {
        const double noise = PatchNoise(surface, x, y, params_.floorPatch, params_.seed);
        const double threshold = 1.0 - 2.0 * params_.floorCover;
        exposure = std::max(exposure, BetweenSmooth(noise, threshold - 0.2, threshold + 0.2));
    }
    return exposure;
}

void ExposureField::Fill(Raster<float>& raster, const Ground& ground, double spacing,
                         unsigned threads) const {
    MapCells(raster, threads, [&](float, const Vec2d& center) {
        return Exposure(ground, center.x, center.y, spacing);
    });
}

LayerExposure::LayerExposure(const std::optional<ExposureParams>& params, double spacing,
                             const char* reader)
    : spacing_(spacing), reader_(reader) {
    if (params) {
        field_.emplace(*params);
    }
}

void LayerExposure::Require(const Ground* ground) const {
    if (field_) {
        RequireGround(ground, reader_);
    }
}

double LayerExposure::At(double x, double y, const Ground* ground, const Surface& surface) const {
    RequireGround(ground, reader_);
    return field_->Exposure(*ground, surface, x, y, spacing_);
}

double PatchNoise(const Surface& surface, double x, double y, double size, std::uint32_t seed) {
    const std::uint32_t fineSeed = SubstreamSeed(seed, kPatchFineStream);
    double coarse = 0.0;
    double fine = 0.0;
    if (surface.IsBody()) {
        const Vec3d p = surface.Position({x, y});
        const double s = 1.0 / size;
        coarse = static_cast<double>(PerlinNoise3Precise(s * p.x, s * p.y, s * p.z, seed));
        fine = static_cast<double>(
            PerlinNoise3Precise(4.0 * s * p.x, 4.0 * s * p.y, 4.0 * s * p.z, fineSeed));
    } else {
        const auto px = static_cast<float>(x / size);
        const auto py = static_cast<float>(y / size);
        coarse = static_cast<double>(PerlinNoise2(px, py, seed));
        fine = static_cast<double>(PerlinNoise2(4.0f * px, 4.0f * py, fineSeed));
    }
    return std::clamp(2.5 * (0.7 * coarse + 0.3 * fine), -1.0, 1.0);
}

}  // namespace terraingen
