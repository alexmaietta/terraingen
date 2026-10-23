#include "terraingen/layers/sand_shadow.h"

#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/checks.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace terraingen {

void ValidateSandShadowOptions(const SandShadowOptions& options) {
    RequireFinite(options.windAzimuth, "Sand shadow windAzimuth");
    RequirePositive(options.lengthRatio, "Sand shadow lengthRatio");
    if (!(options.flankAngle > 0.0 && options.flankAngle < 0.5 * kPi)) {
        throw std::invalid_argument("Sand shadow flankAngle must be in (0, 90) deg");
    }
    RequireNonNegative(options.minDiameter, "Sand shadow minDiameter");
    RequirePositive(options.coverThickness, "Sand shadow coverThickness");
    ValidateResolutionFade(options.fade);
}

SandShadowLayer::SandShadowLayer(std::shared_ptr<const BoulderLayer> boulders,
                                 SandShadowOptions options, Surface surface)
    : boulders_(std::move(boulders)), options_(options), surface_(surface) {
    ValidateSandShadowOptions(options_);
    if (!boulders_) {
        throw std::invalid_argument("Sand shadows need a boulder layer");
    }
    downwind_ = {std::cos(options_.windAzimuth), std::sin(options_.windAzimuth)};
    flankSlope_ = std::tan(options_.flankAngle);
}

std::vector<SandShadowLayer::Shadow>
SandShadowLayer::ShadowsOver(const Bounds2d& region, double spacing, const Ground* ground) const {
    const double longest = boulders_->MaxFeatureDiameter() * (0.5 + options_.lengthRatio);
    std::vector<Shadow> shadows;
    for (const Boulder& boulder :
         boulders_->Boulders(surface_.Expand(region, longest), spacing, ground)) {
        if (boulder.diameter < options_.minDiameter) {
            continue;
        }
        const double weight = ResolutionWeight(boulder.diameter, spacing, options_.fade);
        const double cap = boulder.ExposedHeight();
        if (weight <= 0.0 || cap <= 0.0) {
            continue;
        }
        const double reach = boulder.diameter * (0.5 + options_.lengthRatio);
        shadows.push_back({{boulder.x, boulder.y},
                           boulder.diameter,
                           cap,
                           weight,
                           surface_.Expand({boulder.x, boulder.y, boulder.x, boulder.y}, reach)});
    }
    return shadows;
}

double SandShadowLayer::ShadowDepth(const Shadow& shadow, double x, double y) const {
    const Vec2d offset = surface_.Offset(shadow.center, {x, y});
    const double along = offset.x * downwind_.x + offset.y * downwind_.y - 0.5 * shadow.width;
    const double length = options_.lengthRatio * shadow.width;
    if (!(along >= 0.0 && along < length)) {
        return 0.0;
    }
    const double across = std::fabs(offset.y * downwind_.x - offset.x * downwind_.y);
    const double taper = 1.0 - along / length;
    const double half = 0.5 * shadow.width * taper;
    if (across >= half) {
        return 0.0;
    }
    const double crest = std::min(0.5 * shadow.width * flankSlope_, shadow.cap) * taper;
    return shadow.weight * crest * (1.0 - across / half);
}

std::vector<Bounds2d> SandShadowLayer::Boxes(const std::vector<Shadow>& shadows) {
    std::vector<Bounds2d> boxes;
    boxes.reserve(shadows.size());
    for (const Shadow& shadow : shadows) {
        boxes.push_back(shadow.box);
    }
    return boxes;
}

double SandShadowLayer::ThicknessAt(double x, double y, double spacing,
                                    const Ground* ground) const {
    double depth = 0.0;
    for (const Shadow& shadow : ShadowsOver({x, y, x, y}, spacing, ground)) {
        if (surface_.Contains(shadow.box, {x, y})) {
            depth = std::max(depth, ShadowDepth(shadow, x, y));
        }
    }
    return depth;
}

double SandShadowLayer::MaterialCoverAt(double x, double y, double spacing,
                                        const Ground* ground) const {
    return Smoothstep(ThicknessAt(x, y, spacing, ground) / options_.coverThickness);
}

Raster<double> SandShadowLayer::MaterialCoverOver(const RasterGrid& grid, double spacing,
                                                  const Ground* ground, unsigned threads) const {
    Raster<double> cover(grid.width, grid.height);
    cover.SetTransform(grid.transform);
    const std::vector<Shadow> shadows = ShadowsOver(grid.Bounds(), spacing, ground);
    ForEachFeatureCell(
        cover, threads, Boxes(shadows), surface_, [&](double& cell, const FeatureCell& at) {
            cell = std::max(cell, ShadowDepth(shadows[at.feature], at.center.x, at.center.y));
        });
    for (double& cell : cover.Values()) {
        cell = Smoothstep(cell / options_.coverThickness);
    }
    return cover;
}

void SandShadowLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    const double spacing = samples.ResolvedSpacing(context.spacing, surface_);
    const std::vector<Shadow> shadows = ShadowsOver(samples.Bounds(), spacing, context.ground);
    DrawFeatures(samples, context.threads, Boxes(shadows), surface_,
                 [&](float height, float base, const Vec2d& p, std::size_t n) {
                     const double depth = ShadowDepth(shadows[n], p.x, p.y);
                     return static_cast<float>(
                         std::max(static_cast<double>(height), static_cast<double>(base) + depth));
                 });
}

}  // namespace terraingen
