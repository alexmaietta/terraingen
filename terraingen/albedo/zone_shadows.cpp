#include "terraingen/albedo/zone_shadows.h"

#include "terraingen/layers/fade.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace terraingen::detail {

double ShadowProfile::Shade(double distance, double angle) const {
    if (distance <= start) {
        return 0.0;
    }
    const auto rings = static_cast<double>(azimuths);
    const double r = std::fmin((distance - start) / step, static_cast<double>(samples - 1));
    double a = angle / (2.0 * kPi) * rings;
    a -= std::floor(a / rings) * rings;
    const std::size_t a0 = std::min(static_cast<std::size_t>(a), azimuths - 1);
    const std::size_t a1 = (a0 + 1) % azimuths;
    const std::size_t r0 = std::min(static_cast<std::size_t>(r), samples - 1);
    const std::size_t r1 = std::min(r0 + 1, samples - 1);
    const double fa = a - static_cast<double>(a0);
    const double fr = r - static_cast<double>(r0);
    const auto at = [&](std::size_t i, std::size_t j) {
        const std::size_t k = (i + azimuths - first) % azimuths;
        return k < rays ? static_cast<double>(gap[k * used + std::min(j, used - 1)]) : 0.0;
    };
    const double g = (1.0 - fa) * ((1.0 - fr) * at(a0, r0) + fr * at(a0, r1)) +
                     fa * ((1.0 - fr) * at(a1, r0) + fr * at(a1, r1));
    return Smoothstep(g / depth);
}

std::vector<Vec2d> ShadowProfile::Points(const Vec2d& center, const Surface& surface) const {
    std::vector<Vec2d> points;
    points.reserve(rays * used);
    for (std::size_t k = 0; k < rays; ++k) {
        const std::size_t i = (first + k) % azimuths;
        const double angle = 2.0 * kPi * static_cast<double>(i) / static_cast<double>(azimuths);
        for (std::size_t j = 0; j < used; ++j) {
            const double distance = start + static_cast<double>(j) * step;
            points.push_back(
                surface.Locate(center, {distance * std::cos(angle), distance * std::sin(angle)}));
        }
    }
    return points;
}

void ShadowProfile::TraceJets(const std::vector<float>& heights, double drop) {
    gap.assign(heights.size(), 0.0f);
    for (std::size_t k = 0; k < rays; ++k) {
        const std::size_t row = k * used;
        double jet = static_cast<double>(heights[row]);
        for (std::size_t j = 1; j < used; ++j) {
            const double h = static_cast<double>(heights[row + j]);
            if (std::isnan(h) || std::isnan(jet)) {
                jet = h;
                continue;
            }
            jet = std::fmax(h, jet - drop);
            gap[row + j] = static_cast<float>(jet - h);
        }
    }
}

namespace {

/// The offsets from `center` of the corners of `region`.
std::array<Vec2d, 4> CornerOffsets(const Vec2d& center, const Bounds2d& region,
                                   const Surface& surface) {
    return {surface.Offset(center, {region.minX, region.minY}),
            surface.Offset(center, {region.minX, region.maxY}),
            surface.Offset(center, {region.maxX, region.minY}),
            surface.Offset(center, {region.maxX, region.maxY})};
}

/// The middle direction of `corners` and the largest angle of any of them from it, in radians;
/// nothing when the corners surround the center.
std::optional<std::pair<double, double>> AngleSpan(const std::array<Vec2d, 4>& corners) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    Bounds2d box{kInf, kInf, -kInf, -kInf};
    for (const Vec2d& c : corners) {
        box = {std::fmin(box.minX, c.x), std::fmin(box.minY, c.y), std::fmax(box.maxX, c.x),
               std::fmax(box.maxY, c.y)};
    }
    if (box.minX <= 0.0 && box.maxX >= 0.0 && box.minY <= 0.0 && box.maxY >= 0.0) {
        return std::nullopt;
    }
    const double mid = std::atan2(0.5 * (box.minY + box.maxY), 0.5 * (box.minX + box.maxX));
    double half = 0.0;
    for (const Vec2d& c : corners) {
        half = std::fmax(half, std::fabs(std::remainder(std::atan2(c.y, c.x) - mid, 2.0 * kPi)));
    }
    return std::pair{mid, half};
}

}  // namespace

RayWindow RaysOver(const Vec2d& center, const Bounds2d& region, const Surface& surface,
                   std::size_t azimuths) {
    const std::array<Vec2d, 4> corners = CornerOffsets(center, region, surface);
    RayWindow window;
    window.rays = azimuths;
    for (const Vec2d& c : corners) {
        window.far = std::fmax(window.far, std::hypot(c.x, c.y));
    }
    const auto span = AngleSpan(corners);
    if (!span) {
        return window;
    }
    const double ray = 2.0 * kPi / static_cast<double>(azimuths);
    const double from = std::floor((span->first - span->second) / ray) - 1.0;
    const double to = std::ceil((span->first + span->second) / ray) + 1.0;
    if (to - from + 1.0 >= static_cast<double>(azimuths)) {
        return window;
    }
    const auto n = static_cast<std::int64_t>(azimuths);
    window.first = static_cast<std::size_t>(((static_cast<std::int64_t>(from) % n) + n) % n);
    window.rays = static_cast<std::size_t>(to - from) + 1;
    return window;
}

ShadowProfile MakeShadowProfile(const ZoneShadows& shadows, double radius, double reach,
                                const RayWindow& window) {

    ShadowProfile profile;
    profile.azimuths = static_cast<std::size_t>(shadows.azimuths);
    profile.samples = static_cast<std::size_t>(shadows.radialSamples);
    profile.start = radius;
    profile.step = (reach - radius) / static_cast<double>(profile.samples - 1);
    profile.depth = shadows.depth;
    const double used = std::ceil((window.far - radius) / profile.step) + 2.0;
    profile.first = window.first;
    profile.rays = window.rays;
    profile.used =
        static_cast<std::size_t>(std::clamp(used, 2.0, static_cast<double>(profile.samples)));
    return profile;
}

}  // namespace terraingen::detail
