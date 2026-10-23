// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// The box the relief of a boulder at (`x`, `y`) reaching `reach` meters is drawn over: on a
/// sphere a square in degrees around the box `Surface::Expand` gives.
Bounds2d ReliefBox(double x, double y, double reach, const Surface& surface) {
    if (!surface.IsBody()) {
        return {x - reach, y - reach, x + reach, y + reach};
    }
    const Bounds2d box = surface.Expand({x, y, x, y}, reach);
    const double cx = 0.5 * (box.minX + box.maxX);
    const double cy = 0.5 * (box.minY + box.maxY);
    const double half = std::fmax(0.5 * (box.maxX - box.minX), 0.5 * (box.maxY - box.minY));
    return {cx - half, cy - half, cx + half, cy + half};
}

/// How far a placed shape reaches from its boulder's center `cx`, `cy` along either axis.
double ShapeReach(const TriangleMesh& placed, double cx, double cy) {
    const Box3d box = Bounds(placed);
    return std::fmax(std::fmax(std::fabs(box.min.x - cx), std::fabs(box.max.x - cx)),
                     std::fmax(std::fabs(box.min.y - cy), std::fabs(box.max.y - cy)));
}

/// The height (m) of a boulder's dome `distance` meters from its center, averaged over a band
/// `width` meters wide so its rim slopes over a sample; 0 `width` samples it at the point.
double DomeHeight(const Boulder& boulder, double distance, double width) {
    const double radius = 0.5 * boulder.diameter;
    if (!(width > 0.0)) {
        const double r2 = (distance * distance) / (radius * radius);
        return r2 < 1.0 ? boulder.ExposedHeight() * std::sqrt(1.0 - r2) : 0.0;
    }
    const auto area = [](double u) { return 0.5 * (u * std::sqrt(1.0 - u * u) + std::asin(u)); };
    const double low = std::clamp((distance - 0.5 * width) / radius, -1.0, 1.0);
    const double high = std::clamp((distance + 0.5 * width) / radius, -1.0, 1.0);
    if (!(high > low)) {
        return 0.0;
    }
    return boulder.ExposedHeight() * (area(high) - area(low)) * radius / width;
}

}  // namespace

Boulder BoulderLayer::InLocalFrame(const Boulder& boulder) const {
    Boulder local = boulder;
    if (surface_.IsBody()) {
        local.x = 0.0;
        local.y = 0.0;
    }
    return local;
}

TriangleMesh BoulderLayer::PlacedOnLevel(const Boulder& boulder) const {
    const auto shape = Shape(boulder);
    const BoulderPlacement placement =
        PlaceBoulderModel(InLocalFrame(boulder), *shape, 0.0, Settles(boulder));
    TriangleMesh mesh = PlaceMesh(shape->mesh, placement);
    CutFacets(mesh, InLocalFrame(boulder), 0.0);
    return mesh;
}

float BoulderLayer::AddRelief(float height, float base, double x, double y, const Boulder& boulder,
                              const TriangleMesh* placed, double weight, bool rock,
                              double width) const {
    const Vec2d offset = surface_.Offset({boulder.x, boulder.y}, {x, y});
    const double relief = rock ? RockRelief(x, y, boulder, placed, width) : 0.0;
    double result = static_cast<double>(height);
    if (relief > 0.0) {
        result = std::fmax(result, static_cast<double>(base) + weight * relief);
    }
    if (options_.fillet.heightShare > 0.0) {
        const double fillet =
            FilletHeight(boulder, std::hypot(offset.x, offset.y), options_.fillet);
        if (fillet > 0.0) {
            result = std::fmax(result, static_cast<double>(base) + weight * fillet);
        }
    }
    return static_cast<float>(result);
}

double BoulderLayer::RockRelief(double x, double y, const Boulder& boulder,
                                const TriangleMesh* placed, double width) const {
    const Vec2d offset = surface_.Offset({boulder.x, boulder.y}, {x, y});
    if (placed == nullptr) {
        return boulder.CutByFacets(offset,
                                   DomeHeight(boulder, std::hypot(offset.x, offset.y), width));
    }
    const std::optional<double> top =
        surface_.IsBody() ? TopSurface(*placed, offset.x, offset.y) : TopSurface(*placed, x, y);
    return top ? std::fmax(*top, 0.0) : 0.0;
}

double BoulderLayer::RockCover(const Relief& relief, const Vec2d& p) const {
    const bool rock =
        relief.rock && RockRelief(p.x, p.y, *relief.boulder, relief.Shape(), relief.width) > 0.0;
    return rock ? relief.weight : 0.0;
}

double BoulderLayer::ModelCover(const Boulder& boulder, const Vec2d& p, double spacing) const {
    const Vec2d offset = surface_.Offset({boulder.x, boulder.y}, p);
    const bool under = boulder.model && std::hypot(offset.x, offset.y) < 0.5 * boulder.diameter;
    return under ? ResolutionWeight(boulder.diameter, spacing, options_.fade) : 0.0;
}

std::vector<Boulder> BoulderLayer::ReliefBoulders(const Bounds2d& region, double spacing,
                                                  const Ground* ground) const {
    const double rim = options_.rendering.relief == BoulderRelief::Dome ? 0.5 * spacing : 0.0;
    return Boulders(surface_.Expand(region, rim), spacing, ground);
}

double BoulderLayer::MaterialCoverAt(double x, double y, double spacing,
                                     const Ground* ground) const {
    const std::vector<Boulder> boulders = ReliefBoulders({x, y, x, y}, spacing, ground);
    double cover = 0.0;
    for (const Relief& relief : PrepareRelief(boulders, spacing)) {
        cover = std::fmax(cover, RockCover(relief, {x, y}));
    }
    for (const Boulder& boulder : boulders) {
        cover = std::fmax(cover, ModelCover(boulder, {x, y}, spacing));
    }
    return cover;
}

Raster<double> BoulderLayer::MaterialCoverOver(const RasterGrid& grid, double spacing,
                                               const Ground* ground, unsigned threads) const {
    Raster<double> cover(grid.width, grid.height);
    cover.SetTransform(grid.transform);
    const std::vector<Boulder> boulders = ReliefBoulders(grid.Bounds(), spacing, ground);
    std::vector<Relief> reliefs = PrepareRelief(boulders, spacing);
    reliefs.erase(std::remove_if(reliefs.begin(), reliefs.end(),
                                 [](const Relief& relief) { return !relief.rock; }),
                  reliefs.end());
    ForEachFeatureCell(cover, threads, Boxes(reliefs), surface_,
                       [&](double& cell, const FeatureCell& at) {
                           cell = std::fmax(cell, RockCover(reliefs[at.feature], at.center));
                       });
    std::vector<const Boulder*> models;
    std::vector<Bounds2d> boxes;
    for (const Boulder& boulder : boulders) {
        if (boulder.model) {
            models.push_back(&boulder);
            boxes.push_back(ReliefBox(boulder.x, boulder.y, 0.5 * boulder.diameter, surface_));
        }
    }
    ForEachFeatureCell(cover, threads, boxes, surface_, [&](double& cell, const FeatureCell& at) {
        cell = std::fmax(cell, ModelCover(*models[at.feature], at.center, spacing));
    });
    return cover;
}

void BoulderLayer::CutFacets(TriangleMesh& placed, const Boulder& local, double base) const {
    for (std::uint8_t k = 0; k < local.facetCount; ++k) {
        const BoulderFacet& facet = local.facets[k];
        const Vec3d normal{std::cos(facet.azimuth) * std::sin(facet.angle),
                           std::sin(facet.azimuth) * std::sin(facet.angle), std::cos(facet.angle)};
        std::vector<double> distances;
        distances.reserve(placed.positions.size());
        for (const Vec3d& p : placed.positions) {
            distances.push_back((p.z - base - facet.HeightAt({p.x - local.x, p.y - local.y})) *
                                normal.z);
        }
        CutAtPlane(placed, normal, std::move(distances));
    }
}

BoulderShape BoulderLayer::Abraded(const BoulderShape& shape, const BoulderPlacement& placement,
                                   const Boulder& local, double base) const {
    BoulderShape cut = shape;
    cut.mesh = PlaceMesh(std::move(cut.mesh), placement);
    CutFacets(cut.mesh, local, base);
    for (Vec3d& p : cut.mesh.positions) {
        p = placement.Unapply(p);
    }
    return cut;
}

std::vector<Bounds2d> BoulderLayer::Boxes(const std::vector<Relief>& reliefs) {
    std::vector<Bounds2d> boxes;
    boxes.reserve(reliefs.size());
    for (const Relief& relief : reliefs) {
        boxes.push_back(relief.box);
    }
    return boxes;
}

std::vector<BoulderLayer::Relief> BoulderLayer::PrepareRelief(const std::vector<Boulder>& boulders,
                                                              double spacing) const {
    const BoulderRelief look = options_.rendering.relief;
    const bool filleted = options_.fillet.heightShare > 0.0;
    std::vector<Relief> reliefs;
    for (const Boulder& boulder : boulders) {
        const double weight = ResolutionWeight(boulder.diameter, spacing, options_.fade);
        const bool drawn = options_.rendering.reliefRange.Holds(boulder.diameter, boulder.model);
        const bool rock = drawn && look != BoulderRelief::None;
        const bool apron = filleted && (drawn || boulder.model);
        if (!(rock || apron) || weight <= 0.0) {
            continue;
        }
        const double width = look == BoulderRelief::Dome ? spacing : 0.0;
        Relief relief{&boulder, weight, rock, width, std::nullopt, {}};
        double reach = 0.5 * (boulder.diameter + width);
        if (rock && look == BoulderRelief::Shape) {
            relief.placed = PlacedOnLevel(boulder);
            const Boulder local = InLocalFrame(boulder);
            reach = ShapeReach(*relief.placed, local.x, local.y);
        }
        if (filleted) {
            reach = std::fmax(reach, FilletReach(boulder, options_.fillet));
        }
        relief.box = ReliefBox(boulder.x, boulder.y, reach, surface_);
        reliefs.push_back(std::move(relief));
    }
    return reliefs;
}

void BoulderLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    if (options_.rendering.relief == BoulderRelief::None && options_.fillet.heightShare <= 0.0) {
        return;
    }
    const double spacing = samples.ResolvedSpacing(context.spacing, surface_);
    const std::vector<Boulder> boulders = ReliefBoulders(samples.Bounds(), spacing, context.ground);
    const std::vector<Relief> reliefs = PrepareRelief(boulders, spacing);
    DrawFeatures(samples, context.threads, Boxes(reliefs), surface_,
                 [&](float height, float base, const Vec2d& p, std::size_t n) {
                     const Relief& relief = reliefs[n];
                     return AddRelief(height, base, p.x, p.y, *relief.boulder, relief.Shape(),
                                      relief.weight, relief.rock, relief.width);
                 });
}

std::vector<BoulderModel> BoulderLayer::Models(const std::vector<Boulder>& boulders) const {
    std::vector<const Boulder*> placed;
    for (const Boulder& boulder : boulders) {
        if (boulder.model && boulder.ground) {
            placed.push_back(&boulder);
        }
    }
    std::vector<BoulderModel> models(placed.size());
    ParallelFor(placed.size(), 0, [&](std::size_t n) {
        const Boulder& boulder = *placed[n];
        std::shared_ptr<const BoulderShape> shape = Shape(boulder);
        const Boulder local = InLocalFrame(boulder);
        const BoulderPlacement placement =
            PlaceBoulderModel(local, *shape, *boulder.ground, Settles(boulder));
        if (boulder.facetCount > 0) {
            shape = std::make_shared<const BoulderShape>(
                Abraded(*shape, placement, local, *boulder.ground));
        }
        models[n] = {boulder, ShapeSeed(boulder, options_.rendering.shapePrototypes), shape,
                     placement};
    });
    return models;
}

}  // namespace terraingen
