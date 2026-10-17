#include "terraingen/layers/strata.h"

#include "terraingen/layers/fade.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/surface.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr std::uint32_t kStrataStream = 0x53545241u;
constexpr const char* kGroundReader = "A strata material cover";
/// Width of the fade from bench to cliff in the material map, in bed heights.
constexpr double kCliffFade = 0.05;

}  // namespace

void ValidateStrataOptions(const StrataOptions& options) {
    RequirePositive(options.bedThickness, "Strata bedThickness");
    if (!(options.thicknessVariation >= 0.0 && options.thicknessVariation < 1.0)) {
        throw std::invalid_argument("Strata thicknessVariation must be in [0, 1)");
    }
    if (!(options.dip >= 0.0 && options.dip < 0.5 * std::numbers::pi)) {
        throw std::invalid_argument("Strata dip must be in [0, 90) deg");
    }
    if (!(options.sharpness >= 1.0 && std::isfinite(options.sharpness))) {
        throw std::invalid_argument("Strata sharpness must be finite and at least 1");
    }
    if (!(options.strength >= 0.0 && options.strength <= 1.0)) {
        throw std::invalid_argument("Strata strength must be in [0, 1]");
    }
    if (!(options.cliffShare > 0.0 && options.cliffShare <= 1.0)) {
        throw std::invalid_argument("Strata cliffShare must be in (0, 1]");
    }
    if (!(std::isfinite(options.dipAzimuth) && std::isfinite(options.anchor.x) &&
          std::isfinite(options.anchor.y))) {
        throw std::invalid_argument("Strata dipAzimuth and anchor must be finite");
    }
}

StrataLayer::StrataLayer(StrataOptions options, Surface surface)
    : FormulaLayer(surface), options_(options) {
    ValidateStrataOptions(options_);
    dipDirection_ = {std::cos(options_.dipAzimuth), std::sin(options_.dipAzimuth)};
    dipSlope_ = std::tan(options_.dip);
    if (surface_.IsBody()) {
        anchorDirection_ = UnitVector(options_.anchor.x, options_.anchor.y);
    }
}

double StrataLayer::BoundaryOf(std::int64_t k) const {
    const auto bits = static_cast<std::uint64_t>(k);
    RandomStream random(options_.seed, kStrataStream, static_cast<std::uint32_t>(bits),
                        static_cast<std::uint32_t>(bits >> 32));
    const double offset = options_.thicknessVariation * (random.NextDouble() - 0.5);
    return options_.bedThickness * (static_cast<double>(k) + offset);
}

StrataLayer::Bed StrataLayer::BedOf(double level) const {
    auto k = static_cast<std::int64_t>(std::floor(level / options_.bedThickness));
    double bottom = BoundaryOf(k);
    while (level < bottom) {
        --k;
        bottom = BoundaryOf(k);
    }
    double top = BoundaryOf(k + 1);
    while (level >= top) {
        ++k;
        bottom = top;
        top = BoundaryOf(k + 1);
    }
    return {bottom, top};
}

bool StrataLayer::LevelOf(double height, double x, double y, double& level) const {
    Vec2d q{x, y};
    if (surface_.IsBody()) {
        if (Dot(UnitVector(x, y), anchorDirection_) <= 0.0) {
            return false;
        }
        q = surface_.Offset(options_.anchor, {x, y});
    }
    level = height + dipSlope_ * (q.x * dipDirection_.x + q.y * dipDirection_.y);
    return std::isfinite(level);
}

double StrataLayer::BedPositionAt(double height, double x, double y) const {
    double level = 0.0;
    if (!LevelOf(height, x, y, level)) {
        return 0.0;
    }
    const Bed bed = BedOf(level);
    return std::clamp((level - bed.bottom) / (bed.top - bed.bottom), 0.0, 1.0);
}

double StrataLayer::TerracedAt(double height, double x, double y) const {
    double level = 0.0;
    if (!LevelOf(height, x, y, level) || options_.strength <= 0.0) {
        return height;
    }
    const Bed bed = BedOf(level);
    const double rise = bed.top - bed.bottom;
    const double u = std::clamp((level - bed.bottom) / rise, 0.0, 1.0);
    const double terraced = height + rise * (std::pow(u, options_.sharpness) - u);
    return height + options_.strength * (terraced - height);
}

double StrataLayer::MaterialCoverAt(double x, double y, double spacing,
                                    const Ground* ground) const {
    RequireGround(ground, kGroundReader);
    const double height = static_cast<double>(ground->HeightAt(x, y, spacing));
    if (!std::isfinite(height) || options_.strength <= 0.0) {
        return 0.0;
    }
    const double u = BedPositionAt(height, x, y);
    return Smoothstep((u - (1.0 - options_.cliffShare)) / kCliffFade + 0.5);
}

float StrataLayer::HeightAt(float height, double x, double y, double /*spacing*/,
                            const Ground* /*ground*/) const {
    return static_cast<float>(TerracedAt(static_cast<double>(height), x, y));
}

}  // namespace terraingen
