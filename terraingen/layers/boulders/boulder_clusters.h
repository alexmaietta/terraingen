// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Internal: the boulder cluster fields and the lattice means that normalize them.
#pragma once

#include "terraingen/layers/boulders/boulder_options.h"
#include "terraingen/random/streams.h"

#include <cstdint>

namespace terraingen {

/// The distance in radii beyond which a `Thomas` cluster adds nothing.
inline constexpr double kThomasReachRadii = 4.0;

/// The mean of `f(x, y)` over a square `cells` units across from the origin, sampled at the
/// centers of a 256 x 256 lattice.
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

/// A cluster's weight `distance` meters from its center: a Gaussian of the cluster's radius.
double ClusterBump(double distance, const BoulderClusters& clusters);

/// The mean of `ClusterBump` of the nearest cluster center over the plane, which the cluster field
/// is divided by to keep the mean density.
double MeanClusterBump(const BoulderClusters& clusters);

/// The mean over the surface of the `Thomas` field, the sum of `ClusterBump` within
/// `kThomasReachRadii` of every center, which it is divided by to keep the mean density.
/// @param body Whether centers are scattered through space around a body, `spacing`^2 per square
/// meter of a plane through them, rather than over a plane.
double MeanThomasField(const BoulderClusters& clusters, bool body = false);

/// The largest a cluster field multiplies the scattered boulder density by; 1 without clusters.
/// For `Thomas` it is where as many centers crowd within 3 radii as a one in a million chance
/// allows, and the rare points above it are capped to it.
double MaxClusterAbundance(const BoulderClusters& clusters);

}  // namespace terraingen
