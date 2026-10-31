// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/albedo/blast_zones.h"

#include "albedo/bands.h"
#include "terraingen/albedo/albedo_fields.h"
#include "terraingen/geometry/geometry.h"
#include "terraingen/random/streams.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr double kFullRadius = 0.3;

/// The zone's radial profile at `rho` of its radius: 1 to `kFullRadius`, fading to 0 at 1.
double Profile(double rho) {
    return FadeOut(rho, kFullRadius, 1.0 - kFullRadius);
}

/// The semi-axes (long, short) in meters of `zone`.
std::pair<double, double> SemiAxes(const BlastZone& zone) {
    const double minor = std::sqrt(zone.area / (kPi * zone.elongation));
    return {zone.elongation * minor, minor};
}

}  // namespace

void ValidateBlastZone(const BlastZone& zone) {
    CheckRanges(zone);
}

double BlastZoneMeanShare() {
    static const double share = [] {
        constexpr int kSteps = 1 << 14;
        double sum = 0.0;
        for (int i = 0; i < kSteps; ++i) {
            const double rho = (i + 0.5) / kSteps;
            sum += Profile(rho) * 2.0 * rho / kSteps;
        }
        return sum;
    }();
    return share;
}

BlastZoneAlbedo::BlastZoneAlbedo(std::vector<BlastZone> zones) : zones_(std::move(zones)) {
    for (const BlastZone& zone : zones_) {
        ValidateBlastZone(zone);
    }
}

double BlastZoneAlbedo::Reach(const BlastZone& zone) {
    return std::fmax(SemiAxes(zone).first * zone.haloReach, zone.coreRadius);
}

double BlastZoneAlbedo::Change(const BlastZone& zone, double x, double y, const Surface& surface) {
    const Vec2d offset = surface.Offset({zone.x, zone.y}, {x, y});
    const double distance = std::hypot(offset.x, offset.y);
    if (distance >= Reach(zone)) {
        return 0.0;
    }
    const auto [major, minor] = SemiAxes(zone);
    const double heading = zone.headingDegrees * kDegree;
    const double u = (offset.x * std::cos(heading) + offset.y * std::sin(heading)) / major;
    const double v = (offset.y * std::cos(heading) - offset.x * std::sin(heading)) / minor;
    const double lobe = EdgeLobe(zone.raggedness, kLobeRing, std::atan2(v, u), 0.0,
                                 zone.seed ^ kBlastZoneStream, 3);
    const double rho = std::hypot(u, v) / lobe;
    const double share = BlastZoneMeanShare();
    const double peak = (zone.change - zone.haloChange * (1.0 - share)) / share;
    const double inner = rho < 1.0 ? Profile(rho) : 0.0;
    const double halo = rho <= 1.0             ? 1.0
                        : zone.haloReach > 1.0 ? FadeOut(rho, 1.0, zone.haloReach - 1.0)
                                               : 0.0;
    double change = peak * inner + (1.0 - inner) * zone.haloChange * halo;
    if (zone.coreRadius > 0.0) {
        const double core = FadeOut(distance / zone.coreRadius, 0.7, 0.3);
        change = zone.coreChange * core + (1.0 - core) * change;
    }
    return change;
}

void BlastZoneAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                              const LayerContext& context) const {
    double change = 0.0;
    for (const BlastZone& zone : zones_) {
        change += Change(zone, x, y, context.SurfaceOrPlane());
    }
    ChangeBands(value, bands, change);
}

}  // namespace terraingen
