// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief The size-frequency distribution base class, its kinds and diameter ranges.
#pragma once

#include "terraingen/random/stream.h"

#include <limits>
#include <memory>
#include <vector>

namespace terraingen {

/// What a size-frequency distribution's values measure.
enum class SfdKind {
    Density,         ///< Cumulative count per m^2 (an equilibrium or present-day population).
    ProductionRate,  ///< Cumulative count per m^2 per Gyr (a production function).
};

/// Diameters in meters where a distribution is defined: (`min`, `max`], or with `min` = 0 all
/// positive diameters up to `max`.
struct DiameterRange {
    double min = 0.0;                                      ///< Lower bound in meters, >= 0.
    double max = std::numeric_limits<double>::infinity();  ///< Upper bound in meters, > `min`.

    /// Whether `diameter` (meters) is positive and within [`min`, `max`].
    bool Contains(double diameter) const {
        return diameter > 0.0 && diameter >= min && diameter <= max;
    }
};

/// A cumulative size-frequency distribution N(>=D) of craters or boulders.
///
/// Diameters are in meters and counts per m^2 (or per m^2 per Gyr; see `Kind`). Distributions
/// decrease monotonically. Evaluating outside `ValidRange` throws unless extrapolation is allowed.
class SizeFrequencyDistribution {
  public:
    virtual ~SizeFrequencyDistribution() = default;

    /// What the values measure.
    SfdKind Kind() const {
        return kind_;
    }

    /// The diameters in meters where the distribution may be evaluated without extrapolation.
    const DiameterRange& ValidRange() const {
        return range_;
    }

    /// Whether evaluation outside `ValidRange` is allowed.
    bool AllowsExtrapolation() const {
        return extrapolate_;
    }

    /// Permits or forbids evaluation at positive diameters outside `ValidRange`.
    void SetAllowExtrapolation(bool allow) {
        extrapolate_ = allow;
    }

    /// N(>=D), the count per m^2 (or per m^2 per Gyr) of features at least `diameter` meters
    /// across.
    /// @throws std::domain_error if `diameter` is not positive and finite, or is outside the valid
    /// range without extrapolation.
    double Cumulative(double diameter) const;

    /// -dN/dD at `diameter` meters, per m^2 per meter (or per m^2 per Gyr per meter).
    /// @throws std::domain_error under the same conditions as `Cumulative`.
    double Incremental(double diameter) const;

    /// Expected number of features with diameter in [`dMin`, `dMax`), area * (N(dMin) - N(dMax)).
    /// @param area Area in m^2 (or m^2 Gyr for a production rate), finite and >= 0.
    /// @param dMin Smallest diameter in meters.
    /// @param dMax Largest diameter in meters; may be infinite.
    /// @throws std::invalid_argument unless 0 < `dMin` < `dMax` and `area` >= 0 is finite.
    /// @throws std::domain_error if a diameter is outside the valid range without extrapolation,
    /// or `dMax` is infinite and the valid range is bounded.
    double ExpectedCount(double area, double dMin, double dMax) const;

    /// Draws a diameter in meters in [`dMin`, `dMax`) by inverting N(>=D); `dMax` may be infinite.
    /// @throws std::invalid_argument unless 0 < `dMin` < `dMax`, or if N(dMin) = N(dMax).
    /// @throws std::domain_error under the same conditions as `ExpectedCount`.
    double SampleDiameter(RandomStream& random, double dMin, double dMax) const;

  protected:
    /// Sets the kind and valid range.
    /// @throws std::invalid_argument unless 0 <= `range.min` < `range.max`.
    SizeFrequencyDistribution(SfdKind kind, DiameterRange range);

    /// N(>=D) at a positive `diameter` in meters, without range checks.
    virtual double CumulativeAt(double diameter) const = 0;

    /// -dN/dD without range checks; defaults to a central difference in log diameter.
    virtual double IncrementalAt(double diameter) const;

    /// Diameter D in meters in [`dMin`, `dMax`] with N(>=D) = `target`; defaults to bisection in
    /// log diameter.
    /// @param target Cumulative count with N(dMax) <= `target` <= N(dMin).
    /// @param dMax Largest diameter in meters; may be infinite.
    virtual double InvertCumulative(double target, double dMin, double dMax) const;

    /// Unchecked `CumulativeAt` of another distribution, for distributions built from others.
    static double UncheckedCumulative(const SizeFrequencyDistribution& distribution,
                                      double diameter) {
        return distribution.CumulativeAt(diameter);
    }

    /// Unchecked `IncrementalAt` of another distribution.
    static double UncheckedIncremental(const SizeFrequencyDistribution& distribution,
                                       double diameter) {
        return distribution.IncrementalAt(diameter);
    }

    /// Unchecked `InvertCumulative` of another distribution.
    static double UncheckedInverse(const SizeFrequencyDistribution& distribution, double target,
                                   double dMin, double dMax) {
        return distribution.InvertCumulative(target, dMin, dMax);
    }

  private:
    void CheckDiameter(double diameter) const;

    SfdKind kind_;
    DiameterRange range_;
    bool extrapolate_ = false;
};

}  // namespace terraingen
