/// @file
/// @brief Size-frequency distributions given by a polynomial in log diameter.
#pragma once

#include "terraingen/distribution/size_frequency.h"

#include <vector>

namespace terraingen {

/// N(>=D) = 10^P(x) / `areaUnit` with P(x) = sum_i a_i x^i and x = log10(D /
/// `diameterUnit`), the form of the Neukum et al. (2001) production function.
class LogPolynomialSfd final : public SizeFrequencyDistribution {
  public:
    /// The distribution with the given coefficients and units.
    /// @param coefficients a_0, a_1, ... in increasing degree; at least one, all finite.
    /// @param range Diameters in metres with 0 < `min` and finite `max`; polynomials diverge
    /// outside the interval they were fitted on.
    /// @param diameterUnit Metres per unit of D in the fit; 1000 for coefficients in km.
    /// @param areaUnit m^2 per unit of area in the fit; 1e6 for counts per km^2.
    /// @throws std::invalid_argument for no or non-finite coefficients, a unit that is not positive
    /// and finite, a range without a positive minimum and finite maximum, or if N(>=D) increases
    /// anywhere in the range.
    LogPolynomialSfd(std::vector<double> coefficients, DiameterRange range,
                     SfdKind kind = SfdKind::Density, double diameterUnit = 1.0,
                     double areaUnit = 1.0);

    /// The coefficients a_0, a_1, ... in increasing degree.
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

}  // namespace terraingen
