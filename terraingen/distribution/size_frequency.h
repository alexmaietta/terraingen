/// @file
/// @brief The size-frequency distribution base class, its kinds and diameter ranges.
#pragma once

#include "terraingen/random/stream.h"

#include <limits>
#include <memory>
#include <vector>

namespace terraingen {

/// @brief What a size-frequency distribution's values measure.
enum class SfdKind {
    Density,         ///< Cumulative count per m^2 (an equilibrium or present-day population).
    ProductionRate,  ///< Cumulative count per m^2 per Gyr (a production function).
};

/// @brief Diameters, in metres, over which a distribution is defined: (`min`, `max`], or with
/// `min` = 0 all positive diameters up to `max`.
struct DiameterRange {
    double min = 0.0;                                      ///< Lower bound in metres, >= 0.
    double max = std::numeric_limits<double>::infinity();  ///< Upper bound in metres, > `min`.

    /// @brief Whether a diameter lies in the range.
    /// @param diameter Diameter in metres.
    /// @return True if `diameter` is positive and within [`min`, `max`].
    bool Contains(double diameter) const {
        return diameter > 0.0 && diameter >= min && diameter <= max;
    }
};

/// @brief A cumulative size-frequency distribution N(>=D) of craters or boulders.
///
/// Diameters are in metres and counts per m^2 (or per m^2 per Gyr; see `Kind`). Distributions
/// decrease monotonically. Evaluating outside `ValidRange` throws unless extrapolation is allowed.
class SizeFrequencyDistribution {
  public:
    virtual ~SizeFrequencyDistribution() = default;

    /// @brief What the values measure.
    /// @return The distribution's kind.
    SfdKind Kind() const {
        return kind_;
    }

    /// @brief The diameters where the distribution may be evaluated without extrapolation.
    /// @return The valid range, in metres.
    const DiameterRange& ValidRange() const {
        return range_;
    }

    /// @brief Whether evaluation outside `ValidRange` is allowed.
    /// @return True if extrapolation is allowed.
    bool AllowsExtrapolation() const {
        return extrapolate_;
    }

    /// @brief Permits or forbids evaluation at positive diameters outside `ValidRange`.
    /// @param allow True to allow extrapolation.
    void SetAllowExtrapolation(bool allow) {
        extrapolate_ = allow;
    }

    /// @brief N(>=D), the cumulative count of features at least `diameter` across.
    /// @param diameter D in metres.
    /// @return Count per m^2 (or per m^2 per Gyr).
    /// @throws std::domain_error if `diameter` is not positive and finite, or is outside the valid
    /// range without extrapolation.
    double Cumulative(double diameter) const;

    /// @brief -dN/dD, the incremental density.
    /// @param diameter D in metres.
    /// @return Count per m^2 per metre (or per m^2 per Gyr per metre).
    /// @throws std::domain_error under the same conditions as `Cumulative`.
    double Incremental(double diameter) const;

    /// @brief Expected number of features with diameter in [`dMin`, `dMax`), area * (N(dMin) -
    /// N(dMax)).
    /// @param area Area in m^2 (or m^2 Gyr for a production rate), finite and >= 0.
    /// @param dMin Smallest diameter in metres.
    /// @param dMax Largest diameter in metres; may be infinite.
    /// @return The expected count.
    /// @throws std::invalid_argument unless 0 < `dMin` < `dMax` and `area` >= 0 is finite.
    /// @throws std::domain_error if a diameter is outside the valid range without extrapolation,
    /// or `dMax` is infinite and the valid range is bounded.
    double ExpectedCount(double area, double dMin, double dMax) const;

    /// @brief Draws a diameter in [`dMin`, `dMax`) by inverting N(>=D).
    /// @param random Stream to draw from.
    /// @param dMin Smallest diameter in metres.
    /// @param dMax Largest diameter in metres; may be infinite.
    /// @return The diameter in metres.
    /// @throws std::invalid_argument unless 0 < `dMin` < `dMax`, or if N(dMin) = N(dMax).
    /// @throws std::domain_error under the same conditions as `ExpectedCount`.
    double SampleDiameter(RandomStream& random, double dMin, double dMax) const;

  protected:
    /// @brief Sets the kind and valid range.
    /// @param kind What the values measure.
    /// @param range The valid range.
    /// @throws std::invalid_argument unless 0 <= `range.min` < `range.max`.
    SizeFrequencyDistribution(SfdKind kind, DiameterRange range);

    /// @brief N(>=D) without range checks.
    /// @param diameter D in metres, positive.
    /// @return Count per m^2 (or per m^2 per Gyr).
    virtual double CumulativeAt(double diameter) const = 0;

    /// @brief -dN/dD without range checks; defaults to a central difference in log diameter.
    /// @param diameter D in metres, positive.
    /// @return Count per m^2 per metre (or per m^2 per Gyr per metre).
    virtual double IncrementalAt(double diameter) const;

    /// @brief Diameter D in [`dMin`, `dMax`] with N(>=D) = `target`; defaults to bisection in log
    /// diameter.
    /// @param target Cumulative count with N(dMax) <= `target` <= N(dMin).
    /// @param dMin Smallest diameter in metres.
    /// @param dMax Largest diameter in metres; may be infinite.
    /// @return The diameter in metres.
    virtual double InvertCumulative(double target, double dMin, double dMax) const;

    /// @brief Unchecked `CumulativeAt` of another distribution, for distributions built from
    /// others.
    /// @param distribution The other distribution.
    /// @param diameter D in metres.
    /// @return Its N(>=D).
    static double CumulativeOf(const SizeFrequencyDistribution& distribution, double diameter) {
        return distribution.CumulativeAt(diameter);
    }

    /// @brief Unchecked `IncrementalAt` of another distribution.
    /// @param distribution The other distribution.
    /// @param diameter D in metres.
    /// @return Its -dN/dD.
    static double IncrementalOf(const SizeFrequencyDistribution& distribution, double diameter) {
        return distribution.IncrementalAt(diameter);
    }

    /// @brief Unchecked `InvertCumulative` of another distribution.
    /// @param distribution The other distribution.
    /// @param target Cumulative count to invert.
    /// @param dMin Smallest diameter in metres.
    /// @param dMax Largest diameter in metres.
    /// @return The diameter in metres.
    static double InvertCumulativeOf(const SizeFrequencyDistribution& distribution, double target,
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
