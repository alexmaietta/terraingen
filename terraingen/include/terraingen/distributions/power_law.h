#pragma once

#include "terraingen/distributions/size_frequency.h"

namespace terraingen {

/// N(>=D) = `coefficient` * D^(-`exponent`), with D in metres and N per m^2 (or per m^2 per Gyr).
class PowerLawSfd final : public SizeFrequencyDistribution {
  public:
    /// @throws std::invalid_argument unless `coefficient` and `exponent` are positive and finite
    /// and the range is valid.
    PowerLawSfd(double coefficient, double exponent, SfdKind kind = SfdKind::Density,
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

/// Converts a power-law coefficient published for D in km and N per km^2 to D in metres and N per
/// m^2.
double PowerLawCoefficientFromKilometres(double coefficient, double exponent);

}  // namespace terraingen
