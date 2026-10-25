// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/strata.h"

#include "terraingen/layers/fade.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/surface.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr const char* kGroundReader = "A strata material cover";
/// Width of the fade from bench to cliff in the material map, in bed heights.
constexpr double kCliffFade = 0.05;

}  // namespace

void ValidateStrataOptions(const StrataOptions& options) {
    RequirePositive(options.bedThickness, "Strata bedThickness");
    if (!(options.thicknessVariation >= 0.0 && options.thicknessVariation < 1.0)) {
        throw std::invalid_argument("Strata thicknessVariation must be in [0, 1)");
    }
    if (!((options.dipDegrees * kDegree) >= 0.0 && (options.dipDegrees * kDegree) < 0.5 * kPi)) {
        throw std::invalid_argument("Strata dipDegrees must be in [0, 90) deg");
    }
    if (!(options.sharpness >= 1.0 && std::isfinite(options.sharpness) &&
          options.softSharpness >= 1.0 && std::isfinite(options.softSharpness))) {
        throw std::invalid_argument(
            "Strata sharpness and softSharpness must be finite and at least 1");
    }
    RequireUnitInterval(options.strength, "Strata strength");
    RequireUnitInterval(options.hardShare, "Strata hardShare");
    if (!(options.cliffShare > 0.0 && options.cliffShare <= 1.0)) {
        throw std::invalid_argument("Strata cliffShare must be in (0, 1]");
    }
    RequireFinite((options.dipAzimuthDegrees * kDegree), "Strata dipAzimuthDegrees");
    RequireFinite(options.anchor.x, "Strata anchor x");
    RequireFinite(options.anchor.y, "Strata anchor y");
}

StrataLayer::StrataLayer(StrataOptions options, TerrainBase base)
    : FormulaLayer(base.surface), options_(options), plane_(base.surface, options.anchor) {
    ValidateStrataOptions(options_);
    dipDirection_ = {std::cos(options_.dipAzimuthDegrees * kDegree),
                     std::sin(options_.dipAzimuthDegrees * kDegree)};
    dipSlope_ = std::tan(options_.dipDegrees * kDegree);
}

RandomStream StrataLayer::BedStream(std::uint32_t stream, std::int64_t k) const {
    const IdWords bed = SplitId(static_cast<std::uint64_t>(k));
    return RandomStream(options_.seed, stream, bed.low, bed.high);
}

double StrataLayer::BedBoundary(std::int64_t k) const {
    RandomStream random = BedStream(kStrataStream, k);
    const double offset = options_.thicknessVariation * (random.NextDouble() - 0.5);
    return options_.bedThickness * (static_cast<double>(k) + offset);
}

StrataLayer::Bed StrataLayer::BedAt(double level) const {
    auto k = static_cast<std::int64_t>(std::floor(level / options_.bedThickness));
    double bottom = BedBoundary(k);
    while (level < bottom) {
        --k;
        bottom = BedBoundary(k);
    }
    double top = BedBoundary(k + 1);
    while (level >= top) {
        ++k;
        bottom = top;
        top = BedBoundary(k + 1);
    }
    const bool hard = options_.hardShare >= 1.0 ||
                      BedStream(kStrataHardnessStream, k).NextDouble() < options_.hardShare;
    return {bottom, top, hard};
}

bool StrataLayer::StratigraphicLevel(double height, double x, double y, double& level) const {
    const std::optional<Vec2d> q = plane_.Project(x, y);
    if (!q) {
        return false;
    }
    level = height + dipSlope_ * (q->x * dipDirection_.x + q->y * dipDirection_.y);
    return std::isfinite(level);
}

double StrataLayer::TerracedAt(double height, double x, double y) const {
    double level = 0.0;
    if (!StratigraphicLevel(height, x, y, level) || options_.strength <= 0.0) {
        return height;
    }
    const Bed bed = BedAt(level);
    const double rise = bed.top - bed.bottom;
    const double u = std::clamp((level - bed.bottom) / rise, 0.0, 1.0);
    const double sharpness = bed.hard ? options_.sharpness : options_.softSharpness;
    const double terraced = height + rise * (std::pow(u, sharpness) - u);
    return height + options_.strength * (terraced - height);
}

double StrataLayer::MaterialCoverAt(double x, double y, double spacing,
                                    const Ground* ground) const {
    RequireGround(ground, kGroundReader);
    const double height = static_cast<double>(ground->HeightAt(x, y, spacing));
    if (!std::isfinite(height) || options_.strength <= 0.0) {
        return 0.0;
    }
    double level = 0.0;
    if (!StratigraphicLevel(height, x, y, level)) {
        return 0.0;
    }
    const Bed bed = BedAt(level);
    if (!bed.hard) {
        return 0.0;
    }
    const double u = std::clamp((level - bed.bottom) / (bed.top - bed.bottom), 0.0, 1.0);
    return Smoothstep((u - (1.0 - options_.cliffShare)) / kCliffFade + 0.5);
}

float StrataLayer::HeightAt(float height, double x, double y, double /*spacing*/,
                            const Ground* /*ground*/) const {
    return static_cast<float>(TerracedAt(static_cast<double>(height), x, y));
}

}  // namespace terraingen
