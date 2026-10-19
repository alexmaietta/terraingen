/// @file
/// @brief `RadialShape`, a star-shaped body's radius along each direction from its centre.
#pragma once

#include "terraingen/util/geometry.h"

#include <cstddef>
#include <functional>
#include <vector>

namespace terraingen {

/// The radius of a star-shaped body (one every ray from its centre crosses once) along each
/// direction: a radial ICQ grid, the vertex labels (I, J, F) of Gaskell's implicitly connected
/// quadrilateral format (I and J from 0 to Q, faces F from 1 to 6 centred on +z, -y, -x, +y, +x
/// and -z) each at a fixed equiangular direction, holding the radius there. Within each of a
/// face's Q^2 quadrilaterals the radius is interpolated bilinearly in those angles. Labels the
/// format shares between faces have the same direction and radius, so the radius is continuous;
/// it never leaves the range of the label radii.
class RadialShape {
  public:
    /// The shape of `q` intervals per face edge from a radius in metres along each unit direction,
    /// evaluated at every label.
    /// @throws std::invalid_argument for a `q` of 0, or a radius that is not positive and finite.
    static RadialShape Sample(std::size_t q, const std::function<double(const Vec3d&)>& radius);

    /// The shape of `q` intervals per face edge from 6 (q + 1)^2 label radii in metres, in ICQ
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

    /// The index of label (`i`, `j`, `face`) in the label radii, `face` from 1 to 6: ICQ file
    /// order, face, then J, then I.
    std::size_t LabelIndex(std::size_t i, std::size_t j, std::size_t face) const {
        return ((face - 1) * (q_ + 1) + j) * (q_ + 1) + i;
    }

    /// The radius in metres along a nonzero `direction`.
    double At(const Vec3d& direction) const;

    /// Q, the intervals per face edge.
    std::size_t Q() const {
        return q_;
    }

    /// The label radii in metres, in the order of `LabelIndex`.
    const std::vector<double>& Radii() const {
        return radii_;
    }

    /// The smallest label radius in metres, a lower bound of `At`.
    double MinRadius() const {
        return min_;
    }

    /// The largest label radius in metres, an upper bound of `At`.
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
    bool operator==(const RadialShape&) const = default;

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
