#include "terraingen/albedo/blast_zones.h"

#include "terraingen/layers/fade.h"
#include "terraingen/noise/fractal.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr double kFullRadius = 0.3;
constexpr double kLobeRing = 180.0 / (std::numbers::pi * 45.0);

/// The zone's radial profile at `rho` of its radius: 1 to `kFullRadius`, fading to 0 at 1.
double Profile(double rho) {
    return 1.0 - Smoothstep(std::clamp((rho - kFullRadius) / (1.0 - kFullRadius), 0.0, 1.0));
}

/// The semi-axes (long, short) in metres of `zone`.
std::pair<double, double> SemiAxes(const BlastZone& zone) {
    const double minor = std::sqrt(zone.area / (std::numbers::pi * zone.elongation));
    return {zone.elongation * minor, minor};
}

}  // namespace

void ValidateBlastZone(const BlastZone& zone) {
    if (!(std::isfinite(zone.x) && std::isfinite(zone.y) && std::isfinite(zone.headingDegrees))) {
        throw std::invalid_argument("A blast zone needs a finite position and heading");
    }
    RequirePositive(zone.area, "A blast zone's area");
    if (!(zone.elongation >= 1.0 && std::isfinite(zone.elongation) && zone.haloReach >= 1.0 &&
          std::isfinite(zone.haloReach))) {
        throw std::invalid_argument("A blast zone's elongation and halo reach must be at least 1");
    }
    for (const double change : {zone.change, zone.haloChange, zone.coreChange}) {
        if (!(change > -1.0 && std::isfinite(change))) {
            throw std::invalid_argument("Blast zone changes must be finite and above -1");
        }
    }
    RequireNonNegative(zone.coreRadius, "A blast zone's core radius");
    if (!(zone.raggedness >= 0.0 && zone.raggedness < 1.0)) {
        throw std::invalid_argument("A blast zone's raggedness must be in [0, 1)");
    }
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

BlastZoneAlbedo::BlastZoneAlbedo(std::vector<BlastZone> zones, Surface surface)
    : zones_(std::move(zones)), surface_(std::move(surface)) {
    for (const BlastZone& zone : zones_) {
        ValidateBlastZone(zone);
    }
}

double BlastZoneAlbedo::Reach(const BlastZone& zone) {
    return std::fmax(SemiAxes(zone).first * zone.haloReach, zone.coreRadius);
}

double BlastZoneAlbedo::Change(const BlastZone& zone, double x, double y) const {
    const Vec2d offset = surface_.Offset({zone.x, zone.y}, {x, y});
    const double distance = std::hypot(offset.x, offset.y);
    if (distance >= Reach(zone)) {
        return 0.0;
    }
    const auto [major, minor] = SemiAxes(zone);
    const double heading = zone.headingDegrees * std::numbers::pi / 180.0;
    const double u = (offset.x * std::cos(heading) + offset.y * std::sin(heading)) / major;
    const double v = (offset.y * std::cos(heading) - offset.x * std::sin(heading)) / minor;
    double lobe = 1.0;
    if (zone.raggedness > 0.0) {
        FractalParams params;
        params.seed = zone.seed ^ 0x424c5354u;
        params.wavelength = 1.0f;
        params.amplitude = 1.0f;
        params.octaves = 3;
        const double angle = std::atan2(v, u);
        const double n = static_cast<double>(
            Fractal3(kLobeRing * std::cos(angle), kLobeRing * std::sin(angle), 0.0, params));
        lobe = 1.0 - zone.raggedness * std::clamp(0.5 - n, 0.0, 1.0);
    }
    const double rho = std::hypot(u, v) / lobe;
    const double share = BlastZoneMeanShare();
    const double peak = (zone.change - zone.haloChange * (1.0 - share)) / share;
    const double inner = rho < 1.0 ? Profile(rho) : 0.0;
    const double halo =
        rho <= 1.0 ? 1.0
        : zone.haloReach > 1.0
            ? 1.0 - Smoothstep(std::clamp((rho - 1.0) / (zone.haloReach - 1.0), 0.0, 1.0))
            : 0.0;
    double change = peak * inner + (1.0 - inner) * zone.haloChange * halo;
    if (zone.coreRadius > 0.0) {
        const double core =
            1.0 - Smoothstep(std::clamp((distance / zone.coreRadius - 0.7) / 0.3, 0.0, 1.0));
        change = zone.coreChange * core + (1.0 - core) * change;
    }
    return change;
}

void BlastZoneAlbedo::ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                              const AlbedoContext& context) const {
    ValidateAlbedoContext(context);
    double change = 0.0;
    for (const BlastZone& zone : zones_) {
        change += Change(zone, x, y);
    }
    if (change == 0.0) {
        return;
    }
    const double factor = std::fmax(0.0, 1.0 + change);
    for (std::size_t band = 0; band < bands; ++band) {
        if (!std::isnan(value[band])) {
            value[band] = static_cast<float>(factor * static_cast<double>(value[band]));
        }
    }
}

}  // namespace terraingen
