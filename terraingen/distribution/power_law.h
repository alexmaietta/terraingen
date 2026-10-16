/// @file
/// @brief Power-law size-frequency distributions and their segments.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <cmath>

namespace terraingen {

/// @brief A power law N(>=D) = `coefficient` * D^(-`exponent`) from `start` metres, alone or as
/// one segment of a `PiecewisePowerLawSfd`.
struct PowerLawSegment {
    double start = 0.0;        ///< First diameter, metres.
    double coefficient = 1.0;  ///< N(>=1 m), per m^2 (or per m^2 per Gyr); positive.
    double exponent = 2.0;     ///< Cumulative slope; positive.

    /// @brief N(>=D).
    /// @param diameter D in metres.
    /// @return coefficient * D^(-exponent).
    double Cumulative(double diameter) const {
        return coefficient * std::pow(diameter, -exponent);
    }

    /// @brief -dN/dD.
    /// @param diameter D in metres.
    /// @return coefficient * exponent * D^(-exponent - 1).
    double Incremental(double diameter) const {
        return coefficient * exponent * std::pow(diameter, -exponent - 1.0);
    }

    /// @brief The diameter where N(>=D) is `target`.
    /// @param target A positive cumulative count.
    /// @return (target / coefficient)^(-1 / exponent), in metres.
    double Invert(double target) const {
        return std::pow(target / coefficient, -1.0 / exponent);
    }

    bool operator==(const PowerLawSegment&) const = default;
};

/// @brief N(>=D) = `coefficient` * D^(-`exponent`), with D in metres and N per m^2 (or per m^2 per
/// Gyr).
class PowerLawSfd final : public SizeFrequencyDistribution {
  public:
    /// @brief The power law with the given coefficient and exponent.
    /// @param coefficient N(>=1 m), per m^2 (or per m^2 per Gyr).
    /// @param exponent The cumulative slope.
    /// @param kind What the values measure.
    /// @param range The valid range, in metres.
    /// @throws std::invalid_argument unless `coefficient` and `exponent` are positive and finite
    /// and the range is valid.
    PowerLawSfd(double coefficient, double exponent, SfdKind kind = SfdKind::Density,
                DiameterRange range = {});

    /// @brief The coefficient.
    /// @return N(>=1 m), per m^2 (or per m^2 per Gyr).
    double Coefficient() const {
        return law_.coefficient;
    }

    /// @brief The exponent.
    /// @return The cumulative slope.
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

/// @brief Converts a power-law coefficient published for D in km and N per km^2 to D in metres and
/// N per m^2.
/// @param coefficient The published coefficient.
/// @param exponent The power law's exponent.
/// @return The coefficient for metres.
double PowerLawCoefficientFromKilometres(double coefficient, double exponent);

}  // namespace terraingen
