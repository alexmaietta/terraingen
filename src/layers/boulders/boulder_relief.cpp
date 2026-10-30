// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/raster/cell_loops.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/parallel.h"
#include "util/numerics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
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

/// Where a fillet `top` meters high meets a round dome averaged over `width` meters
/// (`DomeHeight`), the same in every direction and no farther out than its radius.
FilletContact RoundDomeContact(const Boulder& boulder, double width, double top) {
    const double radius = 0.5 * boulder.diameter;
    const double height = std::fmin(top, DomeHeight(boulder, 0.0, width));
    if (!(height > 0.0)) {
        return {};
    }
    if (!(width > 0.0)) {
        const double share = std::fmin(height / boulder.ExposedHeight(), 1.0);
        return {radius * std::sqrt(1.0 - share * share), height};
    }
    if (DomeHeight(boulder, radius, width) >= height) {
        return {radius, height};
    }
    const BisectionInterval contact = Bisect(0.0, radius, 50, 0.0, [&](double distance) {
        return !(DomeHeight(boulder, distance, width) >= height);
    });
    return {contact.low, height};
}

/// Where a fillet `top` meters high meets a dome averaged over `width` meters and cut by the
/// boulder's facets along the unit `direction` from its center: the outermost point within its
/// radius where the rock stands that high, or as high as it stands along `direction`.
FilletContact FacetedDomeContact(const Boulder& boulder, const Vec2d& direction, double width,
                                 double top) {
    const double outer = 0.5 * boulder.diameter;
    const auto rock = [&](double distance) {
        return boulder.CutByFacets({distance * direction.x, distance * direction.y},
                                   DomeHeight(boulder, distance, width));
    };
    constexpr int kSteps = 64;
    std::array<double, kSteps + 1> heights{};
    double highest = 0.0;
    for (std::size_t step = 0; step < heights.size(); ++step) {
        heights[step] = rock(outer * static_cast<double>(step) / kSteps);
        highest = std::fmax(highest, heights[step]);
    }
    const double height = std::fmin(top, highest);
    if (!(height > 0.0)) {
        return {};
    }
    int last = 0;
    for (std::size_t step = 0; step < heights.size(); ++step) {
        if (heights[step] >= height) {
            last = static_cast<int>(step);
        }
    }
    const BisectionInterval contact =
        Bisect(outer * last / kSteps, outer * std::min(last + 1, kSteps) / kSteps, 40, 0.0,
               [&](double distance) { return !(rock(distance) >= height); });
    return {contact.low, height};
}

/// How many directions a shape's fillet contacts are found in for a boulder of `diameter` at
/// `spacing`: about two per sample around its rim.
std::size_t FilletDirections(double diameter, double spacing) {
    const double around = kPi * diameter / std::fmax(spacing, 1e-9);
    return static_cast<std::size_t>(std::clamp(std::ceil(2.0 * around), 16.0, 256.0));
}

/// Where a fillet `top` meters high meets the top surface of `placed` (a shape placed on level
/// ground at 0, centered on `center`) in `count` directions at even steps of azimuth from east.
std::vector<FilletContact> ShapeContacts(const TriangleMesh& placed, const Vec2d& center,
                                         double top, std::size_t count) {
    struct Piece {
        double inner;
        double innerHeight;
        double outer;
        double outerHeight;
    };
    std::vector<FilletContact> contacts(count);
    std::vector<Piece> pieces;
    for (std::size_t k = 0; k < count; ++k) {
        const double azimuth = 2.0 * kPi * static_cast<double>(k) / static_cast<double>(count);
        const Vec2d along{std::cos(azimuth), std::sin(azimuth)};
        pieces.clear();
        double highest = 0.0;
        for (const auto& triangle : placed.triangles) {
            double a[3];
            double s[3];
            double z[3];
            for (int j = 0; j < 3; ++j) {
                const Vec3d& p = placed.positions[triangle[j]];
                a[j] = (p.x - center.x) * along.x + (p.y - center.y) * along.y;
                s[j] = (p.y - center.y) * along.x - (p.x - center.x) * along.y;
                z[j] = p.z;
            }
            double lowA = std::numeric_limits<double>::infinity();
            double highA = -lowA;
            double lowZ = 0.0;
            double highZ = 0.0;
            const auto add = [&](double at, double height) {
                if (at < lowA) {
                    lowA = at;
                    lowZ = height;
                }
                if (at > highA) {
                    highA = at;
                    highZ = height;
                }
            };
            for (int j = 0; j < 3; ++j) {
                const int n = (j + 1) % 3;
                if (s[j] == 0.0) {
                    add(a[j], z[j]);
                }
                if ((s[j] < 0.0 && s[n] > 0.0) || (s[j] > 0.0 && s[n] < 0.0)) {
                    const double t = s[j] / (s[j] - s[n]);
                    add(a[j] + t * (a[n] - a[j]), z[j] + t * (z[n] - z[j]));
                }
            }
            if (!(highA >= 0.0)) {
                continue;
            }
            if (lowA < 0.0) {
                lowZ = highA > lowA ? lowZ + (highZ - lowZ) * (-lowA) / (highA - lowA) : highZ;
                lowA = 0.0;
            }
            highest = std::fmax(highest, std::fmax(lowZ, highZ));
            pieces.push_back({lowA, lowZ, highA, highZ});
        }
        const double height = std::fmin(top, highest);
        if (!(height > 0.0)) {
            continue;
        }
        double distance = 0.0;
        for (const Piece& piece : pieces) {
            if (piece.outerHeight >= height) {
                distance = std::fmax(distance, piece.outer);
            } else if (piece.innerHeight >= height) {
                const double t =
                    (piece.innerHeight - height) / (piece.innerHeight - piece.outerHeight);
                distance = std::fmax(distance, piece.inner + t * (piece.outer - piece.inner));
            }
        }
        contacts[k] = {distance, height};
    }
    return contacts;
}

/// Where the fillet meets its boulder in the direction of `offset` (m east and north of its
/// center): the one contact of a round dome, those of a shape interpolated in azimuth, or found
/// on the dome averaged over `width` meters and cut by its facets.
FilletContact SampleContact(const Boulder& boulder, const Vec2d& offset,
                            const std::vector<FilletContact>& contacts, double width, double top) {
    if (contacts.size() == 1) {
        return contacts.front();
    }
    if (!contacts.empty()) {
        double turn = std::atan2(offset.y, offset.x) / (2.0 * kPi);
        turn -= std::floor(turn);
        const double at = turn * static_cast<double>(contacts.size());
        const std::size_t k = static_cast<std::size_t>(at) % contacts.size();
        const FilletContact& a = contacts[k];
        const FilletContact& b = contacts[(k + 1) % contacts.size()];
        const double t = at - std::floor(at);
        return {a.distance + t * (b.distance - a.distance), a.height + t * (b.height - a.height)};
    }
    const double distance = std::hypot(offset.x, offset.y);
    const Vec2d direction =
        distance > 0.0 ? Vec2d{offset.x / distance, offset.y / distance} : Vec2d{1.0, 0.0};
    return FacetedDomeContact(boulder, direction, width, top);
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
                              const TriangleMesh* placed, double weight, bool rock, double width,
                              const std::vector<FilletContact>& contacts) const {
    const Vec2d offset = surface_.Offset({boulder.x, boulder.y}, {x, y});
    const double relief = rock ? RockRelief(x, y, boulder, placed, width) : 0.0;
    double result = static_cast<double>(height);
    if (relief > 0.0) {
        result = std::fmax(result, static_cast<double>(base) + weight * relief);
    }
    if (options_.fillet.heightShare > 0.0) {
        const double top = options_.fillet.heightShare * boulder.ExposedHeight();
        const double fillet = FilletHeight(boulder, std::hypot(offset.x, offset.y), options_.fillet,
                                           SampleContact(boulder, offset, contacts, width, top));
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
    return BoulderRecords(surface_.Expand(region, rim), spacing, ground);
}

double BoulderLayer::MaterialCoverAt(double x, double y, const LayerContext& context) const {
    const double spacing = context.spacing;
    const std::vector<Boulder> boulders = ReliefBoulders({x, y, x, y}, spacing, context.ground);
    double cover = 0.0;
    for (const Relief& relief : PrepareRelief(boulders, spacing, false)) {
        cover = std::fmax(cover, RockCover(relief, {x, y}));
    }
    for (const Boulder& boulder : boulders) {
        cover = std::fmax(cover, ModelCover(boulder, {x, y}, spacing));
    }
    return cover;
}

Raster<double> BoulderLayer::MaterialCoverOver(const RasterGrid& grid,
                                               const LayerContext& context) const {
    const double spacing = context.spacing;
    const unsigned threads = context.threads;
    Raster<double> cover(grid.width, grid.height);
    cover.SetTransform(grid.transform);
    const std::vector<Boulder> boulders = ReliefBoulders(grid.Bounds(), spacing, context.ground);
    std::vector<Relief> reliefs = PrepareRelief(boulders, spacing, false);
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
                                                              double spacing, bool fillets) const {
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
        if (filleted && fillets) {
            const double top = options_.fillet.heightShare * boulder.ExposedHeight();
            if (rock && look == BoulderRelief::Dome) {
                if (boulder.facetCount == 0) {
                    relief.contacts.push_back(RoundDomeContact(boulder, width, top));
                }
            } else {
                std::optional<TriangleMesh> own;
                const TriangleMesh* shape = relief.Shape();
                if (shape == nullptr) {
                    own = PlacedOnLevel(boulder);
                    shape = &*own;
                }
                const Boulder local = InLocalFrame(boulder);
                relief.contacts = ShapeContacts(*shape, {local.x, local.y}, top,
                                                FilletDirections(boulder.diameter, spacing));
                double farthest = 0.5 * boulder.diameter;
                for (const FilletContact& contact : relief.contacts) {
                    farthest = std::fmax(farthest, contact.distance);
                }
                reach = std::fmax(reach, farthest + options_.fillet.widthRatio * boulder.diameter);
            }
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
    const std::vector<Relief> reliefs = PrepareRelief(boulders, spacing, true);
    DrawFeatures(samples, context.threads, Boxes(reliefs), surface_,
                 [&](float height, float base, const Vec2d& p, std::size_t n) {
                     const Relief& relief = reliefs[n];
                     return AddRelief(height, base, p.x, p.y, *relief.boulder, relief.Shape(),
                                      relief.weight, relief.rock, relief.width, relief.contacts);
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
