// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/sand_fill.h"

#include "terraingen/layers/bedforms_fields.h"
#include "terraingen/layers/fade.h"
#include "terraingen/util/bits.h"

#include <array>
#include <cmath>
#include <stdexcept>

namespace terraingen {

namespace {

constexpr const char* kGroundReader = "A sand fill layer";

}  // namespace

void ValidateSandFillOptions(const SandFillOptions& options) {
    CheckRanges(options);
}

SandFillLayer::SandFillLayer(SandFillOptions options) : options_(options) {
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

double SandFillLayer::Thickness(double x, double y, double height,
                                const LayerContext& context) const {
    if (std::isnan(height) || options_.fill <= 0.0) {
        return 0.0;
    }
    std::array<double, 2 * kRingPoints> heights{};
    GroundAround(*context.ground, context.SurfaceOrPlane(), {x, y}, rings_, context.spacing, height,
                 heights);
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

double SandFillLayer::ThicknessAt(double x, double y, const LayerContext& context) const {
    RequireGround(context.ground, kGroundReader);
    const float height = context.ground->HeightAt(x, y, context.spacing);
    return Thickness(x, y, static_cast<double>(height), context);
}

double SandFillLayer::MaterialCoverAt(double x, double y, const LayerContext& context) const {
    return Smoothstep(ThicknessAt(x, y, context) / options_.coverThickness);
}

float SandFillLayer::HeightAt(float height, double x, double y, const LayerContext& context) const {
    return static_cast<float>(static_cast<double>(height) +
                              Thickness(x, y, static_cast<double>(height), context));
}

void SandFillLayer::CheckContext(const LayerContext& context) const {
    RequireGround(context.ground, kGroundReader);
}

}  // namespace terraingen
