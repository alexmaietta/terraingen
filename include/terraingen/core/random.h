#pragma once

#include "terraingen/kernels/philox.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// A counter-based stream of random numbers built on `Philox4x32`.
///
/// The key is (`seed`, `stream`), where `stream` separates purposes (a layer, a feature kind). The
/// words `a`, `b` and `c` address an independent substream, e.g. a cell's x and y and a size band,
/// so the numbers for one cell never depend on any other cell. The last counter word counts blocks
/// of four 32-bit draws, giving each substream 2^34 draws before it repeats.
class RandomStream {
  public:
    RandomStream(std::uint32_t seed, std::uint32_t stream, std::uint32_t a = 0, std::uint32_t b = 0,
                 std::uint32_t c = 0);

    /// Next uniformly distributed 32-bit word.
    std::uint32_t NextU32();

    /// Uniform float in [0, 1) from the top 24 bits of one draw; every value is exactly
    /// representable.
    float NextFloat();

    /// Uniform double in [0, 1) with 53 random bits from two draws.
    double NextDouble();

    /// Uniform double in (0, 1], for logarithms.
    double NextDoubleOpenBelow();

  private:
    U32x2 key_;
    U32x4 counter_;
    U32x4 block_;
    int used_ = 4;
};

/// Poisson-distributed count with the given mean.
///
/// Uses the multiplication method for means below 10 and Hörmann's transformed rejection with
/// squeeze (PTRS) above.
///
/// Reference: W. Hörmann, "The Transformed Rejection Method for Generating Poisson Random
/// Variables", Insurance: Mathematics and Economics 12(1):39–45, 1993.
/// https://doi.org/10.1016/0167-6687(93)90997-4
///
/// @throws std::invalid_argument if `mean` is negative, not finite, or above 1e9.
std::uint64_t SamplePoisson(RandomStream& random, double mean);

/// Sample of a power law truncated to [`min`, `max`], with complementary cumulative distribution
/// P(X > x) ∝ x^(−`exponent`), drawn by inverting its CDF. `max` may be infinite (a Pareto tail).
/// This is the form of cumulative crater and boulder size-frequency distributions.
/// @throws std::invalid_argument unless 0 < `min` < `max` and `exponent` > 0 is finite.
double SampleTruncatedPowerLaw(RandomStream& random, double min, double max, double exponent);

/// Exponentially distributed sample with the given rate, drawn by inversion.
/// @throws std::invalid_argument unless `rate` is positive and finite.
double SampleExponential(RandomStream& random, double rate);

/// Normally distributed sample, drawn with the Box–Muller transform.
///
/// Reference: G. E. P. Box and M. E. Muller, "A Note on the Generation of Random Normal Deviates",
/// Annals of Mathematical Statistics 29(2):610–611, 1958.
/// @throws std::invalid_argument unless `mean` is finite and `sigma` is finite and not negative.
double SampleNormal(RandomStream& random, double mean, double sigma);

/// A distribution given as a piecewise-linear cumulative distribution function, sampled by
/// inversion. Used for tabulated size-frequency distributions.
class TabulatedDistribution {
  public:
    /// @param values Strictly increasing, finite sample values.
    /// @param cumulative Non-decreasing CDF at each value, starting at 0 and ending at 1.
    /// @throws std::invalid_argument if the table is malformed.
    TabulatedDistribution(std::vector<double> values, std::vector<double> cumulative);

    /// CDF at `x`: 0 below the first value, 1 above the last, linear in between.
    double Cdf(double x) const;

    double Sample(RandomStream& random) const;

  private:
    std::vector<double> values_;
    std::vector<double> cumulative_;
};

}  // namespace terraingen
