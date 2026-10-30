// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Crater datums, the surfaces crater interiors are cut to, fitted to the ground.
#pragma once

#include "terraingen/geometry/plane_fit.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/layers/craters/shaping/apply_options.h"
#include "terraingen/layers/craters/shaping/prepared.h"
#include "terraingen/layers/ground.h"

#include <cstddef>
#include <functional>
#include <optional>

namespace terraingen {

/// Heights of the ground at world positions, NaN where there is none; thread-safe.
using HeightSampler = std::function<float(double x, double y)>;

/// Heights of the ground at many world positions at once, into a span of the same size;
/// thread-safe.
using HeightsSampler = std::function<void(Span<const Vec2d> points, Span<float> heights)>;

/// Points per side of the lattice crater datums are fitted over.
constexpr int kDatumLatticeSide = 7;

/// Points of the lattice crater datums are fitted over, inside the footprint or not.
constexpr std::size_t kDatumLatticePoints =
    static_cast<std::size_t>(kDatumLatticeSide * kDatumLatticeSide);

/// The offset (m) from a crater's center of lattice point `index` (0 to `kDatumLatticeSide` - 1)
/// along one axis of a fit footprint of `fitRadius` (m).
inline double DatumLatticeOffset(int index, double fitRadius) {
    return fitRadius * ((2.0 * index + 1.0) / kDatumLatticeSide - 1.0);
}

/// Calls `visit(point, dx, dy)` for each lattice point inside a fit footprint of `fitRadius`
/// meters, `dx` east and `dy` north of the center, row by row; `point` numbers every lattice
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

/// The datum `fit` gives a crater: its plane for `CraterDatum::Plane`, else flat at its mean;
/// nothing when it holds no point.
std::optional<Plane> FittedDatum(const PlaneFit& fit, CraterDatum datum);

/// The datum a crater is cut to, fitted to the ground beneath it; nothing for
/// `CraterDatum::None` or where the ground has no data.
///
/// `CenterHeight` reads the ground at the center; `MeanHeight` and `Plane` fit it at the points of
/// a 7 x 7 lattice inside the `datumFitRadii` footprint.
std::optional<Plane> FitCraterDatum(const Crater& crater, const CraterApplyOptions& options,
                                    const HeightSampler& ground);

/// The plane a crater with `Crater::replace` is cut to, fitted to the ground on the ring
/// `CraterReplacement` gives; nothing where the ground has no data there.
/// @param outlineScale The crater outline's largest stretch (`CraterOutline::MaxScale`).
std::optional<Plane> FitReplacementDatum(const Crater& crater, const CraterApplyOptions& options,
                                         double reachRadii, double outlineScale,
                                         const HeightSampler& ground);

/// How far (m) beyond a grid replacing craters can change it through their datum fit: their fit
/// ring, stretched by the largest outline, plus the profile's reach.
/// @param maxDiameter The largest replacing crater's diameter, in meters; 0 for none.
double ReplacementMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                         double maxDiameter);

/// How far (m) beyond a grid craters with a datum can change it through their datum fit: the fit
/// footprint plus the profile's reach for the largest crater; 0 without a datum. Craters for a
/// grid or point should be generated this far around it.
/// @param maxDiameter The largest crater's diameter, in meters.
double DatumMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                   double maxDiameter);

}  // namespace terraingen
