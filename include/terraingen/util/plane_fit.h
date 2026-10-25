// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `Plane` and `PlaneFit`, a least-squares plane through points added one at a time.
#pragma once

namespace terraingen {

/// The plane h = `a` + `b` dx + `c` dy, dx and dy in meters east and north of a reference point.
struct Plane {
    double a = 0.0;  ///< Height at the reference point (m).
    double b = 0.0;  ///< Slope eastward (m/m).
    double c = 0.0;  ///< Slope northward (m/m).

    /// Memberwise equality.
    bool operator==(const Plane& other) const {
        return a == other.a && b == other.b && c == other.c;
    }
    bool operator!=(const Plane& other) const {
        return !(*this == other);
    }
};

/// A least-squares plane h = a + b dx + c dy through points added one at a time.
class PlaneFit {
  public:
    /// Adds the point `dx` east and `dy` north (m) at height `h` (m).
    void Add(double dx, double dy, double h);

    /// Whether no point was added.
    bool Empty() const {
        return n_ == 0.0;
    }

    /// The mean height of the points; call only when not `Empty`.
    double Mean() const {
        return sh_ / n_;
    }

    /// The least-squares plane by Cramer's rule, or the flat plane at the mean for fewer than
    /// three points or a degenerate layout; call only when not `Empty`.
    Plane Solve() const;

  private:
    double n_ = 0.0;
    double sx_ = 0.0;
    double sy_ = 0.0;
    double sxx_ = 0.0;
    double sxy_ = 0.0;
    double syy_ = 0.0;
    double sh_ = 0.0;
    double sxh_ = 0.0;
    double syh_ = 0.0;
};

}  // namespace terraingen
