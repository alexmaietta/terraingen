#include "terraingen/distribution/golombek_rapp.h"

#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace terraingen {

double ExponentialIntegral(int n, double x) {
    if (n < 0 || !(std::isfinite(x)) || x < 0.0 || (x == 0.0 && n <= 1)) {
        throw std::domain_error("The exponential integral E_n(x) needs n >= 0 and x > 0");
    }
    constexpr int kMaxIterations = 200;
    constexpr double kEpsilon = std::numeric_limits<double>::epsilon();
    constexpr double kTiny = std::numeric_limits<double>::min() / kEpsilon;
    const int nm1 = n - 1;
    if (n == 0) {
        return std::exp(-x) / x;
    }
    if (x == 0.0) {
        return 1.0 / nm1;
    }
    if (x > 1.0) {
        double b = x + n;
        double c = 1.0 / kTiny;
        double d = 1.0 / b;
        double h = d;
        for (int i = 1; i <= kMaxIterations; ++i) {
            const double a = -static_cast<double>(i) * (nm1 + i);
            b += 2.0;
            d = 1.0 / (a * d + b);
            c = b + a / c;
            const double delta = c * d;
            h *= delta;
            if (std::fabs(delta - 1.0) <= kEpsilon) {
                break;
            }
        }
        return h * std::exp(-x);
    }
    double sum = nm1 != 0 ? 1.0 / nm1 : -std::log(x) - std::numbers::egamma;
    double factor = 1.0;
    for (int i = 1; i <= kMaxIterations; ++i) {
        factor *= -x / i;
        double delta = 0.0;
        if (i != nm1) {
            delta = -factor / (i - nm1);
        } else {
            double psi = -std::numbers::egamma;
            for (int k = 1; k <= nm1; ++k) {
                psi += 1.0 / k;
            }
            delta = factor * (-std::log(x) + psi);
        }
        sum += delta;
        if (std::fabs(delta) <= std::fabs(sum) * kEpsilon) {
            break;
        }
    }
    return sum;
}

GolombekRappSfd::GolombekRappSfd(double coverage, double qa, double qb, DiameterRange range)
    : SizeFrequencyDistribution(SfdKind::Density, range), coverage_(coverage) {
    if (!(coverage_ > 0.0 && coverage_ < 1.0)) {
        throw std::invalid_argument("Boulder coverage k must be between 0 and 1");
    }
    if (!(std::isfinite(qa) && std::isfinite(qb) && qa >= 0.0 && qb >= 0.0)) {
        throw std::invalid_argument("Golombek-Rapp qa and qb must be finite and not negative");
    }
    q_ = qa + qb / coverage_;
    if (!(q_ > 0.0)) {
        throw std::invalid_argument("Golombek-Rapp q must be positive");
    }
}

double GolombekRappSfd::FractionalArea(double diameter) const {
    return coverage_ * std::exp(-q_ * diameter);
}

double GolombekRappSfd::CumulativeAt(double diameter) const {
    return 4.0 * coverage_ * q_ / std::numbers::pi * ExponentialIntegral(2, q_ * diameter) /
           diameter;
}

double GolombekRappSfd::IncrementalAt(double diameter) const {
    return coverage_ * q_ * std::exp(-q_ * diameter) /
           (std::numbers::pi * diameter * diameter / 4.0);
}

}  // namespace terraingen
