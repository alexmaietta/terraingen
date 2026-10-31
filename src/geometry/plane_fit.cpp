// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/geometry/plane_fit.h"

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
    m_n += 1.0;
    m_sx += dx;
    m_sy += dy;
    m_sxx += dx * dx;
    m_sxy += dx * dy;
    m_syy += dy * dy;
    m_sh += h;
    m_sxh += dx * h;
    m_syh += dy * h;
}

Plane PlaneFit::Solve() const {
    const Matrix3 normal{{{m_n, m_sx, m_sy}, {m_sx, m_sxx, m_sxy}, {m_sy, m_sxy, m_syy}}};
    const double det = Determinant(normal);
    if (m_n < 3.0 || !(std::fabs(det) > 1e-9 * m_n * m_sxx * m_syy)) {
        return {m_sh / m_n, 0.0, 0.0};
    }
    const std::array<double, 3> rhs{m_sh, m_sxh, m_syh};
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
