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

/// The cumulative density of craters on a surface of a given age: a
/// production function scaled so that N(>=1 km) follows the Neukum et al. (2001) lunar chronology,
/// N(D, T) = Phi(D) * N1(T) / Phi(1 km).
///
/// This is the population a surface would have with no erosion; small craters exceed equilibrium
/// on old surfaces.
class ProductionSurface final : public SizeFrequencyDistribution {
  public:
    /// @param production A `ProductionRate` distribution defined at 1 km.
    /// @param ageGyr Surface age in Gyr; see `NeukumLunarChronology`. The surface takes the
    /// production function's valid range and extrapolation setting.
    /// @throws std::invalid_argument if `production` is null or not a production rate.
    /// @throws std::domain_error if the age is out of range or `production` is not defined at 1 km.
    ProductionSurface(std::shared_ptr<const SizeFrequencyDistribution> production, double ageGyr);

    const SizeFrequencyDistribution& Production() const {
        return *production_;
    }

    double AgeGyr() const {
        return ageGyr_;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    std::shared_ptr<const SizeFrequencyDistribution> production_;
    double ageGyr_;
    double scale_;
};

/// Converts a power-law coefficient published for D in km and N per km^2 to D in metres and N per
/// m^2.
double PowerLawCoefficientFromKilometres(double coefficient, double exponent);

/// Trask (1966) lunar equilibrium, N(>=D) = 10^-1.1 D^-2 (the same in km/km^2 and m/m^2). The DSNE
/// uses it as the upper limit on the density of craters smaller than about 100-200 m on any
/// surface.
///
/// References: N. J. Trask, in Ranger VIII and IX Part II, JPL Technical Report 32-800, 1966; NASA
/// SLS-SPEC-159 (DSNE) Revision I, Section 3.4.1.1.
PowerLaw TraskEquilibrium();

/// Hartmann (1984) lunar equilibrium, N(>=D) = 10^-1.14 D_km^-1.83 per km^2, as given by Fassett et
/// al. (2022, eq. 3b).
///
/// Reference: C. I. Fassett et al., "Topographic Diffusion Revisited", JGR Planets 127, 2022.
/// https://doi.org/10.1029/2022JE007510
PowerLaw HartmannEquilibrium();

/// Moore's terrain-specific cumulative distributions for Lunar Roving Vehicle trafficability (USGS,
/// "Section C"), N per m^2 with D in metres.
enum class MooreTerrain {
    RoughMare,        ///< 10^-1 D^-2 below 100 m, 10 D^-3 above.
    SmoothMare,       ///< 10^-1 D^-2 below 40 m, then 10^0.602 D^-3, 10^-2.038 D^-1.68, 10 D^-3.
    Uplands,          ///< 10^-1 D^-2 below 1 km, 10^2 D^-3 above.
    HeavilyCratered,  ///< 10^-1 D^-2 up to 10 km.
};

/// Moore's distribution for `terrain` (H. J. Moore, USGS Section C, c. 1970). The published
/// coefficients are rounded, so segments agree at their joins only to within 0.2%; the presets
/// accept joins within 0.5%.
PiecewisePowerLaw MooreDistribution(MooreTerrain terrain);

/// The VIPER environmental specification (VIPER-MSE-SPEC-001, 2021) as used by synthterrain:
/// N = 29174 D^-1.92 per km^2 for D <= 80 m and 156228 D^-2.389 above (D in m). Kept for comparison
/// with synthterrain; its segments disagree by a factor of 1.46 at 80 m, and Allan et al. (2017)
/// found it predicts too many craters against a NAC image.
PiecewisePowerLaw ViperSpecification();

/// Oldest surface age, in Gyr, accepted by `NeukumLunarChronology`.
inline constexpr double kMaxLunarAgeGyr = 4.6;

/// Neukum et al. (2001) lunar chronology: cumulative density of craters at least 1 km across on a
/// surface `ageGyr` old, N1(T) = 5.44e-14 (e^(6.93 T) - 1) + 8.38e-4 T per km^2, returned per m^2.
/// The linear term is the present-day rate, which equals 10^a_0 of `NeukumProductionFunction`.
///
/// Reference: G. Neukum, B. A. Ivanov and W. K. Hartmann, "Cratering Records in the Inner Solar
/// System in Relation to the Lunar Reference System", Space Science Reviews 96:55-86, 2001, eq. 5.
/// https://doi.org/10.1023/A:1011989004263
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronology(double ageGyr);

/// dN1/dT of `NeukumLunarChronology`: the formation rate of craters at least 1 km across `ageGyr`
/// ago, per m^2 per Gyr.
/// @throws std::domain_error unless 0 <= `ageGyr` <= `kMaxLunarAgeGyr`.
double NeukumLunarChronologyRate(double ageGyr);

/// Neukum et al. (2001) lunar production function (eq. 2, table 1 "new" N(D)), valid from 10 m to
/// 300 km, as a `ProductionRate` per m^2 per Gyr at the present-day flux.
///
/// a_0 is log10(8.38e-4) = -3.076756, as in synthterrain; the paper prints -3.0876, which
/// craterstats treats as a typo for -3.0768. Combined with `NeukumLunarChronology` in a
/// `ProductionSurface` it reproduces the large-crater values of the DSNE 3.5 Ga mare table (NASA
/// SLS-SPEC-159 Revision I, Table 3.4.1.1-1).
///
/// Reference: Neukum, Ivanov and Hartmann (2001); see `NeukumLunarChronology`.
LogPolynomial NeukumProductionFunction();

/// `NeukumProductionFunction` extended below 10 m as a cumulative power law of slope -2.85,
/// continuous at 10 m, and valid from 2.5 to 300000 m.
///
/// Oetting et al. (2025) measured a mean cumulative slope of -2.85 for craters of at most 10 m on
/// young impact melt deposits, fitted by maximum likelihood on unbinned data, and concluded that
/// production functions can be extended down to 2.5 m.
///
/// Reference: A. Oetting et al., "Slopes of Lunar Crater Size-Frequency Distributions on Exterior
/// Impact Melt Deposits of Young Craters", JGR Planets 130, e2024JE008589, 2025.
/// https://doi.org/10.1029/2024JE008589
SplicedSfd ExtendedNeukumProductionFunction();

}  // namespace terraingen
