// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/maturity.h"

#include "terraingen/util/checks.h"

#include <cmath>
#include <stdexcept>

namespace terraingen {

double ReflectanceAtMaturity(double reflectance, double reference, double maturity,
                             double originReflectance) {
    if (!(IsPositive(reference) && IsNonNegative(maturity) && std::isfinite(originReflectance))) {
        throw std::invalid_argument(
            "Reflectance at a maturity needs a positive reference and a non-negative maturity");
    }
    const double origin = std::fmin(originReflectance, reflectance);
    return origin + (reflectance - origin) * maturity / reference;
}

}  // namespace terraingen
