#pragma once

#include "terraingen/core/random.h"

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

/// N(>=D) = `coefficient` * D^(-`exponent`), with D in metres and N per m^2 (or per m^2 per Gyr).
class PowerLaw final : public SizeFrequencyDistribution {
  public:
    /// @throws std::invalid_argument unless `coefficient` and `exponent` are positive and finite
    /// and the range is valid.
    PowerLaw(double coefficient, double exponent, SfdKind kind = SfdKind::Density,
             DiameterRange range = {});

    double Coefficient() const {
        return coefficient_;
    }

    double Exponent() const {
        return exponent_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    double coefficient_;
    double exponent_;
};

/// One segment of a `PiecewisePowerLaw`: N(>=D) = `coefficient` * D^(-`exponent`) from `start`
/// metres up to the next segment's start.
struct PowerLawSegment {
    double start = 0.0;
    double coefficient = 1.0;
    double exponent = 2.0;
};

/// A cumulative distribution made of power-law segments in increasing diameter order.
///
/// Segments must meet at their joins within a relative tolerance unless `allowDiscontinuities` is
/// set, which exists for legacy specifications. A downward jump at a join means a finite fraction
/// of the population has exactly the join diameter, and `SampleDiameter` returns it for that
/// fraction.
class PiecewisePowerLaw final : public SizeFrequencyDistribution {
  public:
    /// @param segments At least one segment, with strictly increasing `start` and the first `start`
    /// equal to `range.min`.
    /// @throws std::invalid_argument for malformed segments, a join that rises, or a discontinuity
    /// larger than `tolerance` without `allowDiscontinuities`.
    PiecewisePowerLaw(std::vector<PowerLawSegment> segments, SfdKind kind = SfdKind::Density,
                      DiameterRange range = {}, bool allowDiscontinuities = false,
                      double tolerance = 1e-3);

    const std::vector<PowerLawSegment>& Segments() const {
        return segments_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    const PowerLawSegment& SegmentAt(double diameter) const;

    std::vector<PowerLawSegment> segments_;
};

/// N(>=D) = 10^P(x) / `areaUnit` with P(x) = sum_i a_i x^i and x = log10(D / `diameterUnit`), the
/// form of the Neukum et al. (2001) production function. With `diameterUnit` = 1000 and `areaUnit`
/// = 1e6, coefficients published for D in km and N per km^2 give D in metres and N per m^2.
class LogPolynomial final : public SizeFrequencyDistribution {
  public:
    /// @param coefficients a_0, a_1, ... in increasing degree; at least one, all finite.
    /// @param range Diameters in metres with 0 < `min` and finite `max`, since polynomials diverge
    /// outside the interval they were fitted on.
    /// @throws std::invalid_argument for malformed arguments or if N(>=D) increases anywhere in the
    /// range.
    LogPolynomial(std::vector<double> coefficients, DiameterRange range,
                  SfdKind kind = SfdKind::Density, double diameterUnit = 1.0,
                  double areaUnit = 1.0);

    const std::vector<double>& Coefficients() const {
        return coefficients_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;

  private:
    double Polynomial(double x) const;
    double Derivative(double x) const;

    std::vector<double> coefficients_;
    double diameterUnit_;
    double areaUnit_;
};

/// A cumulative distribution given as a table of (D, N(>=D)) points, interpolated linearly in log D
/// and log N, so each interval is a power law. Extrapolation continues the end intervals' slopes.
class TabulatedSfd final : public SizeFrequencyDistribution {
  public:
    /// @param diameters At least two strictly increasing positive diameters, in `diameterUnit`
    /// metres.
    /// @param cumulative N(>=D) at each diameter, positive, finite and not increasing, per
    /// `areaUnit` m^2.
    /// @throws std::invalid_argument for malformed tables or units.
    TabulatedSfd(std::vector<double> diameters, std::vector<double> cumulative,
                 SfdKind kind = SfdKind::Density, double diameterUnit = 1.0, double areaUnit = 1.0);

    /// Table diameters in metres.
    const std::vector<double>& Diameters() const {
        return diameters_;
    }

    /// Table values per m^2 (or per m^2 per Gyr).
    const std::vector<double>& CumulativeValues() const {
        return cumulative_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    std::size_t IntervalAt(double diameter) const;

    std::vector<double> diameters_;
    std::vector<double> cumulative_;
    std::vector<double> slopes_;
};

/// One piece of a `SplicedSfd`: `distribution` applies from `start` metres up to the next piece's
/// start.
struct SfdPiece {
    double start = 0.0;
    std::shared_ptr<const SizeFrequencyDistribution> distribution;
};

/// A cumulative distribution made of other distributions over consecutive diameter intervals, such
/// as a production function extended below its fitted range by a power law.
///
/// As in `PiecewisePowerLaw`, the lower piece applies at a join, and pieces must meet there within
/// a relative tolerance.
class SplicedSfd final : public SizeFrequencyDistribution {
  public:
    /// @param pieces At least one piece, with strictly increasing `start`, the first `start` equal
    /// to `range.min`, and each distribution valid (or extrapolating) over its interval.
    /// @throws std::invalid_argument for malformed pieces, mixed kinds, a piece not valid over its
    /// interval, or a join that rises or is discontinuous by more than `tolerance`.
    SplicedSfd(std::vector<SfdPiece> pieces, DiameterRange range, double tolerance = 1e-3);

    const std::vector<SfdPiece>& Pieces() const {
        return pieces_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    std::size_t PieceAt(double diameter) const;

    std::vector<SfdPiece> pieces_;
};

/// The pointwise minimum of distributions of one kind, such as the DSNE construction of a surface's
/// crater density: the lesser of the production surface and the equilibrium density at every
/// diameter.
///
/// The valid range is the intersection of the inputs' ranges.
class MinimumSfd final : public SizeFrequencyDistribution {
  public:
    /// @throws std::invalid_argument if `distributions` is empty, holds a null pointer, mixes
    /// kinds, or the valid ranges do not overlap.
    explicit MinimumSfd(
        std::vector<std::shared_ptr<const SizeFrequencyDistribution>> distributions);

    const std::vector<std::shared_ptr<const SizeFrequencyDistribution>>& Distributions() const {
        return distributions_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;

  private:
    const SizeFrequencyDistribution& LowestAt(double diameter) const;

    std::vector<std::shared_ptr<const SizeFrequencyDistribution>> distributions_;
};

/// Converts a power-law coefficient published for D in km and N per km^2 to D in metres and N per
/// m^2.
double PowerLawCoefficientFromKilometres(double coefficient, double exponent);

}  // namespace terraingen
