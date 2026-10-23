#include "terraingen/util/special_functions.h"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace terraingen {

namespace {

/// The Euler-Mascheroni constant.
constexpr double kEulerGamma = 0.577215664901532860606512090082402431;

constexpr int kMaxIterations = 200;
constexpr double kEpsilon = std::numeric_limits<double>::epsilon();

/// E_n(x) for x > 1 by its continued fraction, evaluated with Lentz's method.
double ExponentialIntegralFraction(int n, double x) {
    constexpr double kTiny = std::numeric_limits<double>::min() / kEpsilon;
    const int nm1 = n - 1;
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

/// E_n(x) for 0 < x <= 1 by its power series.
double ExponentialIntegralSeries(int n, double x) {
    const int nm1 = n - 1;
    double sum = nm1 != 0 ? 1.0 / nm1 : -std::log(x) - kEulerGamma;
    double factor = 1.0;
    for (int i = 1; i <= kMaxIterations; ++i) {
        factor *= -x / i;
        double delta = 0.0;
        if (i != nm1) {
            delta = -factor / (i - nm1);
        } else {
            double psi = -kEulerGamma;
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

}  // namespace

double ExponentialIntegral(int n, double x) {
    if (n < 0 || !(std::isfinite(x)) || x < 0.0 || (x == 0.0 && n <= 1)) {
        throw std::domain_error("The exponential integral E_n(x) needs n >= 0 and x > 0");
    }
    if (n == 0) {
        return std::exp(-x) / x;
    }
    if (x == 0.0) {
        return 1.0 / (n - 1);
    }
    return x > 1.0 ? ExponentialIntegralFraction(n, x) : ExponentialIntegralSeries(n, x);
}

}  // namespace terraingen
