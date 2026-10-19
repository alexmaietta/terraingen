/// @file
/// @brief Samplers for Poisson, power-law, exponential, normal and tabulated distributions.
#pragma once

#include "terraingen/random/stream.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// A Poisson-distributed count; 0 for a zero `mean`.
/// @param mean The mean count, in [0, 1e9].
/// @throws std::invalid_argument if `mean` is negative, not finite, or above 1e9.
std::uint64_t SamplePoisson(RandomStream& random, double mean);

/// A sample of a power law truncated to [`min`, `max`], drawn by inverting its CDF.
///
/// P(X > x) is proportional to x^(-`exponent`), the form of cumulative crater and boulder
/// size-frequency distributions.
/// @param min The lower bound, > 0.
/// @param max The upper bound, > `min`; may be infinite (a Pareto tail).
/// @param exponent The cumulative exponent, > 0.
/// @throws std::invalid_argument unless 0 < `min` < `max` and `exponent` > 0 is finite.
double SampleTruncatedPowerLaw(RandomStream& random, double min, double max, double exponent);

/// An exponentially distributed sample >= 0, drawn by inversion.
/// @param rate The rate, > 0; the mean is 1 / `rate`.
/// @throws std::invalid_argument unless `rate` is positive and finite.
double SampleExponential(RandomStream& random, double rate);

/// A standard normal from two independent uniforms `u1` and `u2` in [0, 1) by the Box-Muller
/// transform: sqrt(-2 ln(1 - `u1`)) cos(2 pi `u2`).
double StandardNormal(double u1, double u2);

/// A standard normal from the next two doubles of `random` (`StandardNormal`).
double StandardNormal(RandomStream& random);

/// A normally distributed sample, `mean` + `sigma` `StandardNormal(random)`.
/// @param sigma The standard deviation, >= 0.
/// @throws std::invalid_argument unless `mean` is finite and `sigma` is finite and not negative.
double SampleNormal(RandomStream& random, double mean, double sigma);

/// A distribution given as a piecewise-linear CDF, sampled by inversion; used for
/// tabulated size-frequency distributions.
class TabulatedDistribution {
  public:
    /// Builds the distribution from its table.
    /// @param values Strictly increasing, finite sample values; at least two.
    /// @param cumulative Non-decreasing CDF at each value, starting at 0 and ending at 1.
    /// @throws std::invalid_argument for fewer than two values, mismatched sizes, non-finite
    /// entries, values not strictly increasing, a decreasing CDF, or a CDF not from 0 to 1.
    TabulatedDistribution(std::vector<double> values, std::vector<double> cumulative);

    /// The CDF at `x`: 0 below the first value, 1 above the last, linear in between.
    double Cdf(double x) const;

    /// A sample in [first value, last value], drawn by inverting the CDF.
    double Sample(RandomStream& random) const;

  private:
    std::vector<double> values_;
    std::vector<double> cumulative_;
};

}  // namespace terraingen
