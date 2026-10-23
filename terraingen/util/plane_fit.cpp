// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/plane_fit.h"

#include <array>
#include <cmath>
#include <cstddef>

namespace terraingen {

namespace {

using Matrix3 = std::array<std::array<double, 3>, 3>;

double Determinant(const Matrix3& m) {
    return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
           m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
           m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
}

}  // namespace

void PlaneFit::Add(double dx, double dy, double h) {
    n_ += 1.0;
    sx_ += dx;
    sy_ += dy;
    sxx_ += dx * dx;
    sxy_ += dx * dy;
    syy_ += dy * dy;
    sh_ += h;
    sxh_ += dx * h;
    syh_ += dy * h;
}

Plane PlaneFit::Solve() const {
    const Matrix3 normal{{{n_, sx_, sy_}, {sx_, sxx_, sxy_}, {sy_, sxy_, syy_}}};
    const double det = Determinant(normal);
    if (n_ < 3.0 || !(std::fabs(det) > 1e-9 * n_ * sxx_ * syy_)) {
        return {sh_ / n_, 0.0, 0.0};
    }
    const std::array<double, 3> rhs{sh_, sxh_, syh_};
    std::array<double, 3> solution{};
    for (std::size_t column = 0; column < 3; ++column) {
        Matrix3 replaced = normal;
        for (std::size_t row = 0; row < 3; ++row) {
            replaced[row][column] = rhs[row];
        }
        solution[column] = Determinant(replaced) / det;
    }
    return {solution[0], solution[1], solution[2]};
}

}  // namespace terraingen
