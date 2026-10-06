#pragma once

#include "terraingen/distributions/size_frequency.h"

#include <vector>

namespace terraingen {

/// N(>=D) = 10^P(x) / `areaUnit` with P(x) = sum_i a_i x^i and x = log10(D / `diameterUnit`), the
/// form of the Neukum et al. (2001) production function. With `diameterUnit` = 1000 and `areaUnit`
/// = 1e6, coefficients published for D in km and N per km^2 give D in metres and N per m^2.
class LogPolynomialSfd final : public SizeFrequencyDistribution {
  public:
    /// @param coefficients a_0, a_1, ... in increasing degree; at least one, all finite.
    /// @param range Diameters in metres with 0 < `min` and finite `max`, since polynomials diverge
    /// outside the interval they were fitted on.
    /// @throws std::invalid_argument for malformed arguments or if N(>=D) increases anywhere in the
    /// range.
    LogPolynomialSfd(std::vector<double> coefficients, DiameterRange range,
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

}  // namespace terraingen
