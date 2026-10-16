/// @file
/// @brief Samplers for Poisson, power-law, exponential, normal and tabulated distributions.
#pragma once

#include "terraingen/random/stream.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// @brief A Poisson-distributed count.
/// @param random The stream to draw from.
/// @param mean The mean count, in [0, 1e9].
/// @return The count; 0 for a zero `mean`.
/// @throws std::invalid_argument if `mean` is negative, not finite, or above 1e9.
std::uint64_t SamplePoisson(RandomStream& random, double mean);

/// @brief A sample of a power law truncated to [`min`, `max`], drawn by inverting its CDF.
///
/// P(X > x) is proportional to x^(-`exponent`), the form of cumulative crater and boulder
/// size-frequency distributions.
/// @param random The stream to draw from.
/// @param min The lower bound, > 0.
/// @param max The upper bound, > `min`; may be infinite (a Pareto tail).
/// @param exponent The cumulative exponent, > 0.
/// @return A value in [`min`, `max`].
/// @throws std::invalid_argument unless 0 < `min` < `max` and `exponent` > 0 is finite.
double SampleTruncatedPowerLaw(RandomStream& random, double min, double max, double exponent);

/// @brief An exponentially distributed sample, drawn by inversion.
/// @param random The stream to draw from.
/// @param rate The rate, > 0; the mean is 1 / `rate`.
/// @return A value >= 0.
/// @throws std::invalid_argument unless `rate` is positive and finite.
double SampleExponential(RandomStream& random, double rate);

/// @brief A normally distributed sample, drawn with the Box-Muller transform.
/// @param random The stream to draw from.
/// @param mean The mean.
/// @param sigma The standard deviation, >= 0.
/// @return The sample.
/// @throws std::invalid_argument unless `mean` is finite and `sigma` is finite and not negative.
double SampleNormal(RandomStream& random, double mean, double sigma);

/// @brief A distribution given as a piecewise-linear CDF, sampled by inversion; used for
/// tabulated size-frequency distributions.
class TabulatedDistribution {
  public:
    /// @brief Builds the distribution from its table.
    /// @param values Strictly increasing, finite sample values; at least two.
    /// @param cumulative Non-decreasing CDF at each value, starting at 0 and ending at 1.
    /// @throws std::invalid_argument for fewer than two values, mismatched sizes, non-finite
    /// entries, values not strictly increasing, a decreasing CDF, or a CDF not from 0 to 1.
    TabulatedDistribution(std::vector<double> values, std::vector<double> cumulative);

    /// @brief The CDF at a value.
    /// @param x The value.
    /// @return 0 below the first value, 1 above the last, linear in between.
    double Cdf(double x) const;

    /// @brief A sample drawn by inverting the CDF.
    /// @param random The stream to draw from.
    /// @return A value in [first value, last value].
    double Sample(RandomStream& random) const;

  private:
    std::vector<double> values_;
    std::vector<double> cumulative_;
};

}  // namespace terraingen
