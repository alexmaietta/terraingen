/// @file
/// @brief Internal: the boulder cluster fields and the lattice means that normalise them.
#pragma once

#include "terraingen/layers/boulders/boulder_options.h"

#include <cstdint>

namespace terraingen {

/// Seed salt of the boulder cluster centres.
inline constexpr std::uint32_t kBoulderClusterStream = 0x434C5553u;
/// Seed salt of the boulder patch noise.
inline constexpr std::uint32_t kBoulderPatchStream = 0x50415443u;

/// The distance in radii beyond which a `Thomas` cluster adds nothing.
inline constexpr double kThomasReachRadii = 4.0;

/// The mean of `f(x, y)` over a square `cells` units across from the origin, sampled at the
/// centres of a 256 x 256 lattice.
template <typename Function> double LatticeMean(double cells, const Function& f) {
    constexpr int kSamples = 256;
    double sum = 0.0;
    for (int j = 0; j < kSamples; ++j) {
        for (int i = 0; i < kSamples; ++i) {
            sum += f((i + 0.5) * cells / kSamples, (j + 0.5) * cells / kSamples);
        }
    }
    return sum / (kSamples * kSamples);
}

/// A cluster's weight `distance` metres from its centre: a Gaussian of the cluster's radius.
double ClusterBump(double distance, const BoulderClusters& clusters);

/// The mean of `ClusterBump` of the nearest cluster centre over the plane, which the cluster field
/// is divided by to keep the mean density.
double MeanClusterBump(const BoulderClusters& clusters);

/// The mean over the surface of the `Thomas` field, the sum of `ClusterBump` within
/// `kThomasReachRadii` of every centre, which it is divided by to keep the mean density.
/// @param body Whether centres are scattered through space around a body, `spacing`^2 per square
/// metre of a plane through them, rather than over a plane.
double MeanThomasField(const BoulderClusters& clusters, bool body = false);

/// The largest a cluster field multiplies the scattered boulder density by; 1 without clusters.
/// For `Thomas` it is where as many centres crowd within 3 radii as a one in a million chance
/// allows, and the rare points above it are capped to it.
double MaxClusterAbundance(const BoulderClusters& clusters);

}  // namespace terraingen
