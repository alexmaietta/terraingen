#include "terraingen/layers/sand_fill.h"

#include "terraingen/layers/fade.h"
#include "terraingen/util/checks.h"

#include <cmath>
#include <numbers>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr const char* kGroundReader = "A sand fill layer";
/// Points on each ring the surroundings are read at; the inner ring is turned half a step.
constexpr int kRingPoints = 8;

}  // namespace

void ValidateSandFillOptions(const SandFillOptions& options) {
    RequirePositive(options.reach, "Sand fill reach");
    RequirePositive(options.maxThickness, "Sand fill maxThickness");
    RequirePositive(options.coverThickness, "Sand fill coverThickness");
    if (!(options.fill >= 0.0 && options.fill <= 1.0)) {
        throw std::invalid_argument("Sand fill share must be in [0, 1]");
    }
}

SandFillLayer::SandFillLayer(SandFillOptions options, Surface surface)
    : FormulaLayer(surface), options_(options) {
    ValidateSandFillOptions(options_);
}

double SandFillLayer::Thickness(const Ground& ground, double x, double y, double height,
                                double spacing) const {
    if (std::isnan(height) || options_.fill <= 0.0) {
        return 0.0;
    }
    double mean = 0.0;
    for (int ring = 0; ring < 2; ++ring) {
        const double radius = options_.reach * (ring == 0 ? 0.5 : 1.0);
        const double turn = ring == 0 ? std::numbers::pi / kRingPoints : 0.0;
        for (int k = 0; k < kRingPoints; ++k) {
            const double angle = turn + 2.0 * std::numbers::pi * k / kRingPoints;
            const Vec2d at =
                surface_.Locate({x, y}, {radius * std::cos(angle), radius * std::sin(angle)});
            const float there = ground.HeightAt(at.x, at.y, spacing);
            mean += std::isnan(there) ? height : static_cast<double>(there);
        }
    }
    mean /= 2.0 * kRingPoints;
    const double depth = options_.fill * (mean - height);
    if (depth <= 0.0) {
        return 0.0;
    }
    return options_.maxThickness * -std::expm1(-depth / options_.maxThickness);
}

double SandFillLayer::ThicknessAt(const Ground& ground, double x, double y, double spacing) const {
    return Thickness(ground, x, y, static_cast<double>(ground.HeightAt(x, y, spacing)), spacing);
}

double SandFillLayer::MaterialCoverAt(double x, double y, double spacing,
                                      const Ground* ground) const {
    RequireGround(ground, kGroundReader);
    return Smoothstep(ThicknessAt(*ground, x, y, spacing) / options_.coverThickness);
}

float SandFillLayer::HeightAt(float height, double x, double y, double spacing,
                              const Ground* ground) const {
    RequireGround(ground, kGroundReader);
    return static_cast<float>(static_cast<double>(height) +
                              Thickness(*ground, x, y, static_cast<double>(height), spacing));
}

void SandFillLayer::CheckContext(const LayerContext& context) const {
    RequireGround(context.ground, kGroundReader);
}

}  // namespace terraingen
