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

/// `kSfdSamples` diameters from `sfd` between `minDiameter` and `maxDiameter`, from the stream
/// (`seed`, 77).
inline std::vector<double> SampleDiameters(const SizeFrequencyDistribution& sfd, std::uint32_t seed,
                                           double minDiameter, double maxDiameter) {
    RandomStream stream(seed, 77u);
    std::vector<double> samples(kSfdSamples);
    for (double& d : samples) {
        d = sfd.SampleDiameter(stream, minDiameter, maxDiameter);
    }
    return samples;
}

/// The CDF of `sfd`'s diameters truncated to [`minDiameter`, `maxDiameter`], at `d`.
inline double TruncatedCdf(const SizeFrequencyDistribution& sfd, double minDiameter,
                           double maxDiameter, double d) {
    const double top = sfd.Cumulative(minDiameter);
    return (top - sfd.Cumulative(d)) / (top - sfd.Cumulative(maxDiameter));
}

}  // namespace terraingen::testing
