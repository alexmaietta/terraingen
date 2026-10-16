/// @file
/// @brief Power-law size-frequency distributions and their segments.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <cmath>

namespace terraingen {

/// A power law N(>=D) = `coefficient` * D^(-`exponent`) from `start` metres, alone or as
/// one segment of a `PiecewisePowerLawSfd`.
struct PowerLawSegment {
    double start = 0.0;        ///< First diameter, metres.
    double coefficient = 1.0;  ///< N(>=1 m), per m^2 (or per m^2 per Gyr); positive.
    double exponent = 2.0;     ///< Cumulative slope; positive.

    /// N(>=D) = coefficient * D^(-exponent), with D = `diameter` in metres.
    double Cumulative(double diameter) const {
        return coefficient * std::pow(diameter, -exponent);
    }

    /// -dN/dD = coefficient * exponent * D^(-exponent - 1), with D in metres.
    double Incremental(double diameter) const {
        return coefficient * exponent * std::pow(diameter, -exponent - 1.0);
    }

    /// The diameter in metres where N(>=D) is a positive `target`.
    double Invert(double target) const {
        return std::pow(target / coefficient, -1.0 / exponent);
    }

    bool operator==(const PowerLawSegment&) const = default;
};

/// N(>=D) = `coefficient` * D^(-`exponent`), with D in metres and N per m^2 (or per m^2 per
/// Gyr).
class PowerLawSfd final : public SizeFrequencyDistribution {
  public:
    /// The power law with the given coefficient and exponent.
    /// @param coefficient N(>=1 m), per m^2 (or per m^2 per Gyr).
    /// @param range The valid range, in metres.
    /// @throws std::invalid_argument unless `coefficient` and `exponent` are positive and finite
    /// and the range is valid.
    PowerLawSfd(double coefficient, double exponent, SfdKind kind = SfdKind::Density,
                DiameterRange range = {});

    /// N(>=1 m), per m^2 (or per m^2 per Gyr).
    double Coefficient() const {
        return law_.coefficient;
    }

    /// The cumulative slope.
    double Exponent() const {
        return law_.exponent;
    }

  protected:
    double CumulativeAt(double diameter) const override;
    double IncrementalAt(double diameter) const override;
    double InvertCumulative(double target, double dMin, double dMax) const override;

  private:
    PowerLawSegment law_;
};

/// Converts a power-law coefficient published for D in km and N per km^2 to D in metres and
/// N per m^2.
double PowerLawCoefficientFromKilometres(double coefficient, double exponent);

}  // namespace terraingen
