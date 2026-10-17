/// @file
/// @brief The directions of a crater's rays, shared by its secondaries and its bright streaks.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/random/value_distribution.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// How many rays a crater has and how wide they are, after Elliott et al. (2018), drawn from the
/// crater's ID and `seed`; layers given the same layout see the same rays.
struct RayLayout {
    std::uint32_t seed = 0;  ///< Seed of the directions.
    /// Rays per crater, 0 to 64.
    ValueDistribution count{ValueDistribution::Kind::Normal, 10.7, 3.1, 6.0, 20.0};
    /// Angular width 2 radii from the centre, in (0, 180) degrees.
    ValueDistribution widthDegrees{ValueDistribution::Kind::Normal, 22.0, 8.3, 5.4, 38.6};

    bool operator==(const RayLayout&) const = default;
};

/// Checks a ray layout.
/// @throws std::invalid_argument for invalid distributions, a count that can be below 0 or above
/// 64, or widths outside (0, 180) degrees.
void ValidateRayLayout(const RayLayout& layout);

/// One ray of a crater.
struct RayDirection {
    double azimuth = 0.0;       ///< Radians counter-clockwise from east.
    double sinHalfWidth = 0.0;  ///< Sine of half its angular width 2 radii from the centre.
    std::uint32_t key = 0;      ///< Varies what lies along the ray.
};

/// The rays of `crater` under `layout`, at uniform random azimuths.
std::vector<RayDirection> RayDirections(const Crater& crater, const RayLayout& layout);

}  // namespace terraingen
