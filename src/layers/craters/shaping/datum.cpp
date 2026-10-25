// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/shaping/datum.h"

#include "terraingen/util/bits.h"

#include <cmath>

namespace terraingen {

namespace {

/// Circles across a replacing crater's fit ring, and points around each.
constexpr int kReplacementFitCircles = 3;
constexpr int kReplacementFitPoints = 24;

/// The ground under the lattice points inside a crater's fit footprint, fitted by least squares.
PlaneFit FitFootprint(const Crater& crater, double fitRadii, const Surface& surface,
                      const HeightSampler& ground) {
    PlaneFit fit;
    ForEachDatumPoint(fitRadii * 0.5 * crater.diameter,
                      [&](std::size_t /*point*/, double dx, double dy) {
                          const Vec2d at = surface.Locate({crater.x, crater.y}, {dx, dy});
                          const float height = ground(at.x, at.y);
                          if (!std::isnan(height)) {
                              fit.Add(dx, dy, static_cast<double>(height));
                          }
                      });
    return fit;
}

}  // namespace

std::optional<Plane> FittedDatum(const PlaneFit& fit, CraterDatum datum) {
    if (fit.Empty()) {
        return std::nullopt;
    }
    return datum == CraterDatum::Plane ? fit.Solve() : Plane{fit.Mean(), 0.0, 0.0};
}

std::optional<Plane> FitCraterDatum(const Crater& crater, const CraterApplyOptions& options,
                                    const HeightSampler& ground) {
    if (options.datum == CraterDatum::None) {
        return std::nullopt;
    }
    if (options.datum == CraterDatum::CenterHeight) {
        const float height = ground(crater.x, crater.y);
        if (std::isnan(height)) {
            return std::nullopt;
        }
        return Plane{static_cast<double>(height), 0.0, 0.0};
    }
    return FittedDatum(FitFootprint(crater, options.datumFitRadii, options.surface, ground),
                       options.datum);
}

std::optional<Plane> FitReplacementDatum(const Crater& crater, const CraterApplyOptions& options,
                                         double reachRadii, double outlineScale,
                                         const HeightSampler& ground) {
    const double radius = 0.5 * crater.diameter;
    const double inner = outlineScale * options.replacement.EndRadii(reachRadii) * radius;
    const double width = options.replacement.fitWidthRadii * radius;
    PlaneFit fit;
    for (int circle = 0; circle < kReplacementFitCircles; ++circle) {
        const double distance = inner + width * (circle + 0.5) / kReplacementFitCircles;
        for (int n = 0; n < kReplacementFitPoints; ++n) {
            const double angle = 2.0 * kPi * (n + 0.5 * circle) / kReplacementFitPoints;
            const double dx = distance * std::cos(angle);
            const double dy = distance * std::sin(angle);
            const Vec2d at = options.surface.Locate({crater.x, crater.y}, {dx, dy});
            const float height = ground(at.x, at.y);
            if (!std::isnan(height)) {
                fit.Add(dx, dy, static_cast<double>(height));
            }
        }
    }
    return FittedDatum(fit, CraterDatum::Plane);
}

double ReplacementMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                         double maxDiameter) {
    const double reach = profile.ReachRadii();
    const double scale = MaxOutlineScale(options.irregularity);
    return (scale * (options.replacement.EndRadii(reach) + reach) +
            options.replacement.fitWidthRadii) *
           0.5 * maxDiameter;
}

double DatumMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                   double maxDiameter) {
    if (options.datum == CraterDatum::None) {
        return 0.0;
    }
    return (options.datumFitRadii + MaxOutlineScale(options.irregularity) * profile.ReachRadii()) *
           0.5 * maxDiameter;
}

}  // namespace terraingen
