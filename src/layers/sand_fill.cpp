// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/sand_fill.h"

#include "terraingen/layers/fade.h"
#include "terraingen/util/bits.h"
#include "util/checks.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr const char* kGroundReader = "A sand fill layer";

}  // namespace

void ValidateSandFillOptions(const SandFillOptions& options) {
    RequirePositive(options.reach, "Sand fill reach");
    RequirePositive(options.maxThickness, "Sand fill maxThickness");
    RequirePositive(options.coverThickness, "Sand fill coverThickness");
    RequireUnitInterval(options.fill, "Sand fill share");
}

SandFillLayer::SandFillLayer(SandFillOptions options, TerrainBase base)
    : FormulaLayer(base.surface), options_(options) {
    ValidateSandFillOptions(options_);
    for (int ring = 0; ring < 2; ++ring) {
        const double radius = options_.reach * (ring == 0 ? 0.5 : 1.0);
        const double turn = ring == 0 ? kPi / kRingPoints : 0.0;
        for (int k = 0; k < kRingPoints; ++k) {
            const double angle = turn + 2.0 * kPi * k / kRingPoints;
            rings_[static_cast<std::size_t>(ring * kRingPoints + k)] = {radius * std::cos(angle),
                                                                        radius * std::sin(angle)};
        }
    }
}

double SandFillLayer::Thickness(const Ground& ground, double x, double y, double height,
                                double spacing) const {
    if (std::isnan(height) || options_.fill <= 0.0) {
        return 0.0;
    }
    std::array<double, 2 * kRingPoints> heights{};
    GroundAround(ground, surface_, {x, y}, rings_, spacing, height, heights);
    double mean = 0.0;
    for (const double there : heights) {
        mean += there;
    }
    mean /= 2.0 * kRingPoints;
    const double depth = options_.fill * (mean - height);
    if (depth <= 0.0) {
        return 0.0;
    }
    return options_.maxThickness * -std::expm1(-depth / options_.maxThickness);
}

double SandFillLayer::ThicknessAt(double x, double y, double spacing, const Ground& ground) const {
    return Thickness(ground, x, y, static_cast<double>(ground.HeightAt(x, y, spacing)), spacing);
}

double SandFillLayer::MaterialCoverAt(double x, double y, double spacing,
                                      const Ground* ground) const {
    RequireGround(ground, kGroundReader);
    return Smoothstep(ThicknessAt(x, y, spacing, *ground) / options_.coverThickness);
}

float SandFillLayer::HeightAt(float height, double x, double y, double spacing,
                              const Ground* ground) const {
    return static_cast<float>(static_cast<double>(height) +
                              Thickness(*ground, x, y, static_cast<double>(height), spacing));
}

void SandFillLayer::CheckContext(const LayerContext& context) const {
    RequireGround(context.ground, kGroundReader);
}

}  // namespace terraingen
