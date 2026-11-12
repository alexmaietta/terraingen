// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/sand_shadow.h"

#include "terraingen/layers/bedforms_fields.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/bits.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <utility>

namespace terraingen {

void ValidateSandShadowOptions(const SandShadowOptions& options) {
    CheckRanges(options);
    ValidateResolutionFade(options.fade);
}

SandShadowLayer::SandShadowLayer(std::shared_ptr<const BoulderLayer> boulders,
                                 SandShadowOptions options)
    : m_boulders(std::move(boulders)), m_options(options) {
    ValidateSandShadowOptions(m_options);
    if (!m_boulders) {
        throw std::invalid_argument("Sand shadows need a boulder layer");
    }
    m_downwind = {std::cos(m_options.windAzimuthDegrees * kDegree),
                  std::sin(m_options.windAzimuthDegrees * kDegree)};
    m_flankSlope = std::tan(m_options.flankAngleDegrees * kDegree);
}

LayerPtr SandShadowLayer::Rebound(const LayerReplacements& replaced) const {
    std::shared_ptr<const BoulderLayer> boulders = Replacement(m_boulders, replaced);
    if (boulders == m_boulders) {
        return nullptr;
    }
    auto rebound = std::make_shared<SandShadowLayer>(*this);
    rebound->m_boulders = std::move(boulders);
    return rebound;
}

double SandShadowLayer::LongestShadow() const {
    return m_boulders->MaxFeatureDiameter() * (0.5 + m_options.lengthRatio);
}

double SandShadowLayer::GroundReach(double spacing) const {
    return LongestShadow() + m_boulders->GroundReach(spacing);
}

std::vector<SandShadowLayer::Shadow> SandShadowLayer::ShadowsOver(const Bounds2d& region,
                                                                  double spacing,
                                                                  const Ground* ground,
                                                                  const Surface& surface) const {
    const double longest = LongestShadow();
    std::vector<Shadow> shadows;
    const BoulderList boulders =
        m_boulders->Boulders(surface.Expand(region, longest), spacing, ground);
    for (std::size_t i = 0; i < boulders.Size(); ++i) {
        const Boulder boulder = boulders.At(i);
        if (boulder.diameter < m_options.minDiameter) {
            continue;
        }
        const double weight = ResolutionWeight(boulder.diameter, spacing, m_options.fade);
        const double cap = boulder.ExposedHeight();
        if (weight <= 0.0 || cap <= 0.0) {
            continue;
        }
        const double reach = boulder.diameter * (0.5 + m_options.lengthRatio);
        shadows.push_back({{boulder.x, boulder.y},
                           boulder.diameter,
                           cap,
                           weight,
                           surface.Expand({boulder.x, boulder.y, boulder.x, boulder.y}, reach)});
    }
    return shadows;
}

double SandShadowLayer::ShadowDepth(const Shadow& shadow, double x, double y,
                                    const Surface& surface) const {
    const Vec2d offset = surface.Offset(shadow.center, {x, y});
    const double along = offset.x * m_downwind.x + offset.y * m_downwind.y - 0.5 * shadow.width;
    const double length = m_options.lengthRatio * shadow.width;
    if (!(along >= 0.0 && along < length)) {
        return 0.0;
    }
    const double across = std::fabs(offset.y * m_downwind.x - offset.x * m_downwind.y);
    const double taper = 1.0 - along / length;
    const double half = 0.5 * shadow.width * taper;
    if (across >= half) {
        return 0.0;
    }
    const double crest = std::min(0.5 * shadow.width * m_flankSlope, shadow.cap) * taper;
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

double SandShadowLayer::ThicknessAt(double x, double y, const LayerContext& context) const {
    const Surface& surface = context.SurfaceOrPlane();
    double depth = 0.0;
    const std::vector<Shadow> shadows =
        ShadowsOver({x, y, x, y}, context.spacing, context.ground, surface);
    for (const Shadow& shadow : shadows) {
        if (surface.Contains(shadow.box, {x, y})) {
            depth = std::max(depth, ShadowDepth(shadow, x, y, surface));
        }
    }
    return depth;
}

double SandShadowLayer::MaterialCoverAt(double x, double y, const LayerContext& context) const {
    return Smoothstep(ThicknessAt(x, y, context) / m_options.coverThickness);
}

Raster<double> SandShadowLayer::MaterialCoverOver(const RasterGrid& grid,
                                                  const LayerContext& context) const {
    const Surface& surface = context.SurfaceOrPlane();
    Raster<double> cover(grid);
    const std::vector<Shadow> shadows =
        ShadowsOver(grid.Bounds(), context.spacing, context.ground, surface);
    ForEachFeatureCell(
        cover, context.threads, Boxes(shadows), surface, [&](double& cell, const FeatureCell& at) {
            cell =
                std::max(cell, ShadowDepth(shadows[at.feature], at.center.x, at.center.y, surface));
        });
    for (double& cell : cover.Values()) {
        cell = Smoothstep(cell / m_options.coverThickness);
    }
    return cover;
}

void SandShadowLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    const Surface& surface = context.SurfaceOrPlane();
    const double spacing = samples.ResolvedSpacing(context.spacing, surface);
    const std::vector<Shadow> shadows =
        ShadowsOver(samples.Bounds(), spacing, context.ground, surface);
    DrawFeatures(samples, context.threads, Boxes(shadows), surface,
                 [&](float height, float base, const Vec2d& p, std::size_t n) {
                     const double depth = ShadowDepth(shadows[n], p.x, p.y, surface);
                     return static_cast<float>(
                         std::max(static_cast<double>(height), static_cast<double>(base) + depth));
                 });
}

}  // namespace terraingen
