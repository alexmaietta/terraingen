// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `RadialShape`, a star-shaped body's radius along each direction from its center.
#pragma once

#include "terraingen/util/geometry.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace terraingen {

/// The index of ICQ label (`i`, `j`, `face`) of a grid of `q` intervals per face edge, `face` from
/// 1 to 6: ICQ file order, face, then J, then I.
constexpr std::size_t IcqLabelIndex(std::size_t q, std::size_t i, std::size_t j, std::size_t face) {
    return ((face - 1) * (q + 1) + j) * (q + 1) + i;
}

/// Calls `visit(i, j, face, index)` for every label of a grid of `q` intervals per face edge, in
/// ICQ file order (`IcqLabelIndex`).
template <typename Visit> void ForEachIcqLabel(std::size_t q, const Visit& visit) {
    for (std::size_t face = 1; face <= 6; ++face) {
        for (std::size_t j = 0; j <= q; ++j) {
            for (std::size_t i = 0; i <= q; ++i) {
                visit(i, j, face, IcqLabelIndex(q, i, j, face));
            }
        }
    }
}

/// The radius of a star-shaped body along each direction: the radius at every label of Gaskell's
/// ICQ grid (`LabelDirection`), interpolated bilinearly in equiangular coordinates within each
/// quadrilateral, so it is continuous and stays within the range of the label radii.
class RadialShape {
  public:
    /// The shape of `q` intervals per face edge from a radius in meters along each unit direction,
    /// evaluated at every label.
    /// @throws std::invalid_argument for a `q` of 0, or a radius that is not positive and finite.
    static RadialShape Sample(std::size_t q, const std::function<double(const Vec3d&)>& radius);

    /// The shape of `q` intervals per face edge from 6 (q + 1)^2 label radii in meters, in ICQ
    /// file order (`LabelIndex`).
    /// @throws std::invalid_argument for a `q` of 0, the wrong count, a radius that is not
    /// positive and finite, or labels shared between faces with different radii.
    RadialShape(std::size_t q, std::vector<double> radii);

    /// The unit direction of label (`i`, `j`, `face`) of a grid of `q` intervals per edge, at
    /// equiangular coordinates with Gaskell's face orientation: on face 1 (+z) I runs along +x and
    /// J along -y; on faces 2 to 5 (-y, -x, +y, +x) J runs along -z; on face 6 (-z) I runs along +x
    /// and J along +y. Labels the format shares between faces get the same direction exactly.
    /// @throws std::invalid_argument unless `q` > 0, `i` and `j` <= `q` and 1 <= `face` <= 6.
    static Vec3d LabelDirection(std::size_t q, std::size_t i, std::size_t j, std::size_t face);

    /// The index of label (`i`, `j`, `face`) in the label radii (`IcqLabelIndex`).
    std::size_t LabelIndex(std::size_t i, std::size_t j, std::size_t face) const {
        return IcqLabelIndex(q_, i, j, face);
    }

    /// The radius in meters along a nonzero `direction`.
    double At(const Vec3d& direction) const;

    /// Q, the intervals per face edge.
    std::size_t Q() const {
        return q_;
    }

    /// The label radii in meters, in the order of `LabelIndex`.
    const std::vector<double>& Radii() const {
        return radii_;
    }

    /// The smallest label radius in meters, a lower bound of `At`.
    double MinRadius() const {
        return min_;
    }

    /// The largest label radius in meters, an upper bound of `At`.
    double MaxRadius() const {
        return max_;
    }

    /// An upper bound in m^2 of the area per steradian, r^2 / (u . n), over the interpolated
    /// surface: its largest value at any quadrilateral's corner with the quadrilateral's mean
    /// normal (from the cross product of its diagonals), times a margin for the variation within
    /// quadrilaterals.
    double MaxAreaPerSteradian() const {
        return maxArea_;
    }

    /// Whether two shapes have the same Q and label radii.
    bool operator==(const RadialShape& other) const {
        return q_ == other.q_ && radii_ == other.radii_;
    }
    bool operator!=(const RadialShape& other) const {
        return !(*this == other);
    }

  private:
    /// Checks that labels shared between faces have equal radii.
    void CheckSharedLabels() const;
    /// The largest area per steradian at any quadrilateral's corner, before the margin.
    double CornerAreaBound() const;
    /// The largest area per steradian at the corners of quadrilateral (`i`, `j`) of `face`.
    double QuadAreaBound(std::size_t i, std::size_t j, std::size_t face) const;

    std::size_t q_ = 0;
    std::vector<double> radii_;
    double min_ = 0.0;
    double max_ = 0.0;
    double maxArea_ = 0.0;
};

}  // namespace terraingen
