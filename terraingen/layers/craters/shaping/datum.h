/// @file
/// @brief Crater datums, the surfaces crater interiors are cut to, fitted to the ground.
#pragma once

#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/prepared.h"
#include "terraingen/layers/ground.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace terraingen {

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

/// Points per side of the lattice crater datums are fitted over.
constexpr int kDatumLatticeSide = 7;

/// The offset (m) from a crater's centre of lattice point `index` (0 to `kDatumLatticeSide` - 1)
/// along one axis of a fit footprint of `fitRadius` (m).
inline double DatumLatticeOffset(int index, double fitRadius) {
    return fitRadius * ((2.0 * index + 1.0) / kDatumLatticeSide - 1.0);
}

/// Calls `visit(point, dx, dy)` for each lattice point inside a fit footprint of `fitRadius`
/// metres, `dx` east and `dy` north of the centre, row by row; `point` numbers every lattice
/// point, inside or not, row by row.
template <typename Visit> void ForEachDatumPoint(double fitRadius, const Visit& visit) {
    for (int j = 0; j < kDatumLatticeSide; ++j) {
        const double dy = DatumLatticeOffset(j, fitRadius);
        for (int i = 0; i < kDatumLatticeSide; ++i) {
            const double dx = DatumLatticeOffset(i, fitRadius);
            if (dx * dx + dy * dy < fitRadius * fitRadius) {
                visit(static_cast<std::size_t>(j * kDatumLatticeSide + i), dx, dy);
            }
        }
    }
}

/// The datum a crater is cut to, fitted to the ground beneath it; nothing for
/// `CraterDatum::None` or where the ground has no data.
///
/// `CentreHeight` reads the ground at the centre; `MeanHeight` and `Plane` fit it at the points of
/// a 7 x 7 lattice inside the `datumFitRadii` footprint.
std::optional<Plane> FitCraterDatum(const Crater& crater, const CraterApplyOptions& options,
                                    const HeightSampler& ground);

/// How far (m) beyond a grid craters with a datum can change it through their datum fit: the fit
/// footprint plus the profile's reach for the largest crater; 0 without a datum. Craters for a
/// grid or point should be generated this far around it.
/// @param maxDiameter The largest crater's diameter, in metres.
double DatumMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                   double maxDiameter);

}  // namespace terraingen
