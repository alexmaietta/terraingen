// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/util/radial_shape.h"

#include "terraingen/util/bits.h"
#include "util/checks.h"
#include "util/cube_faces.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

/// Factor on the largest area per steradian found at quadrilateral corners, for its variation
/// within the curved bilinear quadrilaterals.
constexpr double kAreaMargin = 1.25;

/// The gnomonic coordinate of label `k` of `q`, exactly -1, 0 or 1 at multiples of 45 degrees and
/// odd in `k - q/2`, so labels shared between faces get bitwise the same direction.
double Gnomonic(std::size_t k, std::size_t q) {
    if (2 * k > q) {
        return -Gnomonic(q - k, q);
    }
    if (k == 0) {
        return -1.0;
    }
    if (2 * k == q) {
        return 0.0;
    }
    return std::tan(detail::kQuarter *
                    (2.0 * static_cast<double>(k) / static_cast<double>(q) - 1.0));
}

}  // namespace

Vec3d RadialShape::LabelDirection(std::size_t q, std::size_t i, std::size_t j, std::size_t face) {
    if (q == 0 || i > q || j > q || face < 1 || face > 6) {
        throw std::invalid_argument("An ICQ label needs Q > 0, I and J <= Q and F from 1 to 6");
    }
    const Vec3d p =
        detail::CubeFacePoint(detail::kIcqFaces[face - 1], Gnomonic(i, q), Gnomonic(j, q));
    return p / Length(p);
}

RadialShape RadialShape::Sample(std::size_t q, const std::function<double(const Vec3d&)>& radius) {
    if (q == 0) {
        throw std::invalid_argument("A radial shape needs Q > 0");
    }
    std::vector<double> radii(6 * (q + 1) * (q + 1));
    ForEachIcqLabel(q, [&](std::size_t i, std::size_t j, std::size_t face, std::size_t k) {
        radii[k] = radius(LabelDirection(q, i, j, face));
    });
    return RadialShape(q, std::move(radii));
}

RadialShape::RadialShape(std::size_t q, std::vector<double> radii)
    : q_(q), radii_(std::move(radii)) {
    if (q_ == 0) {
        throw std::invalid_argument("A radial shape needs Q > 0");
    }
    if (radii_.size() != 6 * (q_ + 1) * (q_ + 1)) {
        throw std::invalid_argument("A radial shape needs 6 (Q + 1)^2 label radii");
    }
    if (!std::all_of(radii_.begin(), radii_.end(), IsPositive)) {
        throw std::invalid_argument("A radial shape's radii must be positive and finite");
    }
    const auto [lo, hi] = std::minmax_element(radii_.begin(), radii_.end());
    min_ = *lo;
    max_ = *hi;
    CheckSharedLabels();
    maxArea_ = CornerAreaBound() * kAreaMargin;
}

void RadialShape::CheckSharedLabels() const {
    std::map<std::array<double, 3>, double> shared;
    ForEachIcqLabel(q_, [&](std::size_t i, std::size_t j, std::size_t face, std::size_t k) {
        if (i != 0 && i != q_ && j != 0 && j != q_) {
            return;
        }
        const Vec3d d = LabelDirection(q_, i, j, face);
        const auto [it, inserted] = shared.emplace(std::array<double, 3>{d.x, d.y, d.z}, radii_[k]);
        if (!inserted && std::fabs(it->second - radii_[k]) > 1e-9 * max_) {
            throw std::invalid_argument(
                "A radial shape's labels shared between faces must have equal radii");
        }
    });
}

double RadialShape::CornerAreaBound() const {
    double largest = 0.0;
    for (std::size_t face = 1; face <= 6; ++face) {
        for (std::size_t j = 0; j < q_; ++j) {
            for (std::size_t i = 0; i < q_; ++i) {
                largest = std::max(largest, QuadAreaBound(i, j, face));
            }
        }
    }
    return largest;
}

double RadialShape::QuadAreaBound(std::size_t i, std::size_t j, std::size_t face) const {
    const std::array<std::pair<std::size_t, std::size_t>, 4> corners{
        {{i, j}, {i + 1, j}, {i + 1, j + 1}, {i, j + 1}}};
    std::array<Vec3d, 4> p{};
    for (std::size_t k = 0; k < 4; ++k) {
        const auto [ci, cj] = corners[k];
        p[k] = LabelDirection(q_, ci, cj, face) * radii_[LabelIndex(ci, cj, face)];
    }
    Vec3d normal = Cross(p[2] - p[0], p[3] - p[1]);
    normal = normal * (1.0 / Length(normal));
    if (Dot(normal, p[0]) < 0.0) {
        normal = normal * -1.0;
    }
    double largest = 0.0;
    for (const Vec3d& corner : p) {
        const double r = Length(corner);
        const double cosine = Dot(corner, normal) / r;
        largest = std::max(largest, r * r / std::max(cosine, 1e-3));
    }
    return largest;
}

double RadialShape::At(const Vec3d& direction) const {
    const detail::FacePosition at =
        detail::FaceCoordinates(detail::kIcqFaces, direction, static_cast<double>(q_));
    const auto position = [&](double s) {
        const std::size_t k = ClampIndex(std::floor(s), q_);
        return std::pair<std::size_t, double>{k, std::clamp(s - static_cast<double>(k), 0.0, 1.0)};
    };
    const auto [i, fi] = position(at.s);
    const auto [j, fj] = position(at.t);
    const std::size_t face = at.face + 1;
    const double r00 = radii_[LabelIndex(i, j, face)];
    const double r10 = radii_[LabelIndex(i + 1, j, face)];
    const double r01 = radii_[LabelIndex(i, j + 1, face)];
    const double r11 = radii_[LabelIndex(i + 1, j + 1, face)];
    return (1.0 - fj) * ((1.0 - fi) * r00 + fi * r10) + fj * ((1.0 - fi) * r01 + fi * r11);
}

}  // namespace terraingen
