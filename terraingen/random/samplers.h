/// @file
/// @brief Samplers for Poisson and normal distributions.
#pragma once

#include "terraingen/random/stream.h"

#include <cstdint>

namespace terraingen {

/// A Poisson-distributed count; 0 for a zero `mean`.
/// @param mean The mean count, in [0, 1e9].
/// @throws std::invalid_argument if `mean` is negative, not finite, or above 1e9.
std::uint64_t SamplePoisson(RandomStream& random, double mean);

/// A standard normal from two independent uniforms `u1` and `u2` in [0, 1) by the Box-Muller
/// transform: sqrt(-2 ln(1 - `u1`)) cos(2 pi `u2`).
double StandardNormal(double u1, double u2);

/// A standard normal from the next two doubles of `random` (`StandardNormal`).
double StandardNormal(RandomStream& random);

}  // namespace terraingen
