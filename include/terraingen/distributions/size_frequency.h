#pragma once

#include "terraingen/random/random_stream.h"

#include <limits>
#include <memory>
#include <vector>

namespace terraingen {

/// What a size-frequency distribution's values measure.
enum class SfdKind {
    Density,         ///< Cumulative count per m^2 (an equilibrium or present-day population).
    ProductionRate,  ///< Cumulative count per m^2 per Gyr (a production function).
};

/// Diameters, in metres, over which a distribution is defined: (`min`, `max`], or with `min` = 0
/// all positive diameters up to `max`.
struct DiameterRange {
    double min = 0.0;
    double max = std::numeric_limits<double>::infinity();

    bool Contains(double diameter) const {
        return diameter > 0.0 && diameter >= min && diameter <= max;
    }
};

/// A cumulative size-frequency distribution N(>=D) of craters or rocks.
///
/// Diameters are in metres and counts per m^2 (or per m^2 per Gyr; see `Kind`). Distributions
/// decrease monotonically. Evaluating outside `ValidRange` throws unless extrapolation has been
/// allowed, because published fits are only valid where they were measured.
///
/// Terminology follows Robbins et al., "Revised Recommended Methods for Analyzing Crater
/// Size-Frequency Distributions", Meteoritics & Planetary Science 53(4):891-931, 2018.
/// https://doi.org/10.1111/maps.12990
class SizeFrequencyDistribution {
  public:
    virtual ~SizeFrequencyDistribution() = default;

    SfdKind Kind() const {
        return kind_;
    }

    const DiameterRange& ValidRange() const {
        return range_;
    }

    bool AllowsExtrapolation() const {
        return extrapolate_;
    }

    /// Permits evaluation at any positive diameter outside `ValidRange`.
    void SetAllowExtrapolation(bool allow) {
        extrapolate_ = allow;
    }

    /// N(>=D): cumulative count per m^2 (or per m^2 per Gyr) of features at least `diameter`
    /// metres.
    /// @throws std::domain_error if `diameter` is not positive and finite, or is outside the valid
    /// range without extrapolation.
    double Cumulative(double diameter) const;

    /// -dN/dD at `diameter`, per m^2 per metre (or per m^2 per Gyr per metre).
    /// @throws std::domain_error under the same conditions as `Cumulative`.
    double Incremental(double diameter) const;

    /// Expected number of features with diameter in [`dMin`, `dMax`) over `area` m^2:
    /// area * (N(dMin) - N(dMax)). `dMax` may be infinite.
    /// @throws std::invalid_argument unless 0 < `dMin` < `dMax` and `area` >= 0 is finite.
    double ExpectedCount(double area, double dMin, double dMax) const;

    /// Diameter in [`dMin`, `dMax`) drawn from this distribution by inverting N(>=D).
    /// @throws std::invalid_argument unless 0 < `dMin` < `dMax`, or if N(dMin) = N(dMax).
    double SampleDiameter(RandomStream& random, double dMin, double dMax) const;

  protected:
    SizeFrequencyDistribution(SfdKind kind, DiameterRange range);

    /// N(>=D) without range checks.
    virtual double CumulativeAt(double diameter) const = 0;

    /// -dN/dD without range checks. Defaults to a central difference in log diameter.
    virtual double IncrementalAt(double diameter) const;

    /// Diameter D in [`dMin`, `dMax`] with N(>=D) = `target`, for N(dMax) <= `target` <= N(dMin).
    /// Defaults to bisection in log diameter.
    virtual double InvertCumulative(double target, double dMin, double dMax) const;

    /// Unchecked access to another distribution, for distributions built from others.
    static double CumulativeOf(const SizeFrequencyDistribution& distribution, double diameter) {
        return distribution.CumulativeAt(diameter);
    }

    static double IncrementalOf(const SizeFrequencyDistribution& distribution, double diameter) {
        return distribution.IncrementalAt(diameter);
    }

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
