#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ernie::testing {

inline constexpr double kPi = 3.14159265358979323846;

/// One evaluation: cos(i), cos(e), cos(g), cos(psi) and the albedo input.
struct Geometry {
    double mu0;
    double mu;
    double cosG;
    double cosPsi;
    glm::dvec3 albedo;
};

/// Grid of incidence, emission and azimuth angles with a coloured albedo.
inline std::vector<Geometry> GeometryGrid(glm::dvec3 albedo = {0.12, 0.35, 0.8}) {
    std::vector<Geometry> grid;
    for (const double i : {0.0, 10.0, 30.0, 50.0, 70.0, 85.0}) {
        for (const double e : {0.0, 15.0, 30.0, 60.0, 80.0}) {
            for (const double psi : {0.0, 45.0, 90.0, 135.0, 180.0}) {
                const double ri = i * kPi / 180.0;
                const double re = e * kPi / 180.0;
                const double rp = psi * kPi / 180.0;
                grid.push_back(
                    {std::cos(ri), std::cos(re),
                     std::cos(ri) * std::cos(re) + std::sin(ri) * std::sin(re) * std::cos(rp),
                     std::cos(rp), albedo});
            }
        }
    }
    return grid;
}

inline double HenyeyGreenstein(double xi, double cosG) {
    return (1.0 - xi * xi) / std::pow(1.0 + 2.0 * xi * cosG + xi * xi, 1.5);
}

inline double HapkeH(double w, double x) {
    const double gamma = std::sqrt(1.0 - w);
    const double r0 = (1.0 - gamma) / (1.0 + gamma);
    return 1.0 / (1.0 - w * x * (r0 + 0.5 * (1.0 - 2.0 * r0 * x) * std::log((1.0 + x) / x)));
}

/// Reference Hapke model (see brdf_hapke.slang), written from the published formulas in double
/// precision with the shader's guards on degenerate angles.
inline double HapkeReference(const Geometry& g, double w, const std::vector<float>& values) {
    const std::vector<double> p(values.begin(), values.end());
    const double b = p[0];
    const double c = p[1];
    const double bs0 = p[2];
    const double hs = p[3];
    const double thetaBar = p[4] * kPi / 180.0;
    const double phi = std::clamp(p[5], 1e-6, 0.74);
    const double kArg = 1.209 * std::pow(phi, 2.0 / 3.0);
    const double k = -std::log(1.0 - kArg) / kArg;

    double mu0e = g.mu0;
    double mue = g.mu;
    double s = 1.0;
    if (thetaBar >= 1e-4) {
        const double tanT = std::tan(thetaBar);
        const double chi = 1.0 / std::sqrt(1.0 + kPi * tanT * tanT);
        const double i = std::max(std::acos(g.mu0), 1e-4);
        const double e = std::max(std::acos(g.mu), 1e-4);
        const double psi = std::min(std::acos(std::clamp(g.cosPsi, -1.0, 1.0)), kPi - 1e-4);
        const double f = std::exp(-2.0 * std::tan(psi / 2.0));
        const double sin2 = std::pow(std::sin(psi / 2.0), 2.0);
        auto e1 = [&](double x) { return std::exp(-2.0 / (kPi * tanT * std::tan(x))); };
        auto e2 = [&](double x) {
            return std::exp(-1.0 / (kPi * tanT * tanT * std::tan(x) * std::tan(x)));
        };
        auto eta = [&](double x) {
            return chi * (std::cos(x) + std::sin(x) * tanT * e2(x) / (2.0 - e1(x)));
        };
        if (i <= e) {
            const double d = 2.0 - e1(e) - psi / kPi * e1(i);
            mu0e = chi *
                   (std::cos(i) + std::sin(i) * tanT * (std::cos(psi) * e2(e) + sin2 * e2(i)) / d);
            mue = chi * (std::cos(e) + std::sin(e) * tanT * (e2(e) - sin2 * e2(i)) / d);
            s = mue / eta(e) * g.mu0 / eta(i) * chi / (1.0 - f + f * chi * g.mu0 / eta(i));
        } else {
            const double d = 2.0 - e1(i) - psi / kPi * e1(e);
            mu0e = chi * (std::cos(i) + std::sin(i) * tanT * (e2(i) - sin2 * e2(e)) / d);
            mue = chi *
                  (std::cos(e) + std::sin(e) * tanT * (std::cos(psi) * e2(i) + sin2 * e2(e)) / d);
            s = mue / eta(e) * g.mu0 / eta(i) * chi / (1.0 - f + f * chi * g.mu / eta(e));
        }
    }
    const double cosG = g.cosG;
    const double gAngle = std::acos(std::clamp(cosG, -1.0, 1.0));
    const double phase =
        0.5 * (1.0 + c) * (1.0 - b * b) / std::pow(1.0 - 2.0 * b * cosG + b * b, 1.5) +
        0.5 * (1.0 - c) * (1.0 - b * b) / std::pow(1.0 + 2.0 * b * cosG + b * b, 1.5);
    const double shoe = bs0 / (1.0 + std::tan(gAngle / 2.0) / hs);
    const double multiple = HapkeH(w, mu0e / k) * HapkeH(w, mue / k) - 1.0;
    return k * w / 4.0 * mu0e / (mu0e + mue) * (phase * (1.0 + shoe) + multiple) * s;
}

}  // namespace ernie::testing
