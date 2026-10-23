// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Diameters sampled from size-frequency distributions, for statistical tests.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/random/stream.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace terraingen::testing {

/// Diameters each statistical test draws.
inline constexpr std::size_t kSfdSamples = 40000;

/// `kSfdSamples` diameters from `sfd` between `dMin` and `dMax`, from the stream (`seed`, 77).
inline std::vector<double> SampleDiameters(const SizeFrequencyDistribution& sfd, std::uint32_t seed,
                                           double dMin, double dMax) {
    RandomStream stream(seed, 77u);
    std::vector<double> samples(kSfdSamples);
    for (double& d : samples) {
        d = sfd.SampleDiameter(stream, dMin, dMax);
    }
    return samples;
}

/// The CDF of `sfd`'s diameters truncated to [`dMin`, `dMax`], at `d`.
inline double TruncatedCdf(const SizeFrequencyDistribution& sfd, double dMin, double dMax,
                           double d) {
    const double top = sfd.Cumulative(dMin);
    return (top - sfd.Cumulative(d)) / (top - sfd.Cumulative(dMax));
}

}  // namespace terraingen::testing
