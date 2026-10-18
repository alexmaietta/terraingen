#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/noise/hash.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/cells.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// A ground whose heights at some points, at one spacing, are read up front in one pass; any
/// other point or spacing is read from the ground beneath. Gives the same heights either way.
class PrefetchedGround final : public Ground {
  public:
    PrefetchedGround(const Ground& ground, std::span<const Vec2d> points, double spacing)
        : ground_(ground), spacing_(spacing) {
        std::vector<float> heights(points.size());
        ground.HeightsAt(points, heights, spacing);
        heights_.reserve(points.size());
        for (std::size_t n = 0; n < points.size(); ++n) {
            heights_.emplace(KeyOf(points[n].x, points[n].y), heights[n]);
        }
    }

    float HeightAt(double x, double y, double spacing) const override {
        if (spacing == spacing_) {
            const auto found = heights_.find(KeyOf(x, y));
            if (found != heights_.end()) {
                return found->second;
            }
        }
        return ground_.HeightAt(x, y, spacing);
    }

  private:
    using Key = std::pair<std::uint64_t, std::uint64_t>;
    struct KeyHash {
        std::size_t operator()(const Key& key) const {
            return std::hash<std::uint64_t>{}(key.first * 0x9E3779B97F4A7C15ull ^ key.second);
        }
    };

    static Key KeyOf(double x, double y) {
        return {std::bit_cast<std::uint64_t>(x), std::bit_cast<std::uint64_t>(y)};
    }

    const Ground& ground_;
    double spacing_;
    std::unordered_map<Key, float, KeyHash> heights_;
};

constexpr std::uint32_t kBoulderSeed = 0x524F434Bu;
constexpr std::uint32_t kBoulderAttributes = 0x41545452u;
constexpr std::uint32_t kBoulderPieces = 0x50494543u;

/// A uniform in [0, 1) from two 32-bit words.
double Uniform(std::uint32_t high, std::uint32_t low) {
    const std::uint64_t bits = (static_cast<std::uint64_t>(high) << 32) | low;
    return static_cast<double>(bits >> 11) * 0x1.0p-53;
}

/// Cuts `mesh` flat at a plane: triangles crossing it are split along it, sharing the new points
/// between neighbours so the mesh stays closed, and points above it (`distance(p)` > 0, the
/// distance along the unit `normal`) are moved down onto it.
template <typename Distance>
void CutByPlane(TriangleMesh& mesh, const Vec3d& normal, const Distance& distance) {
    std::vector<double> s(mesh.positions.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        s[i] = distance(mesh.positions[i]);
    }
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::uint32_t> crossings;
    const auto crossing = [&](std::uint32_t a, std::uint32_t b) {
        const auto key = std::minmax(a, b);
        const auto found = crossings.find(key);
        if (found != crossings.end()) {
            return found->second;
        }
        const double t = s[a] / (s[a] - s[b]);
        const Vec3d& pa = mesh.positions[a];
        mesh.positions.push_back(pa + (mesh.positions[b] - pa) * t);
        s.push_back(0.0);
        const auto index = static_cast<std::uint32_t>(mesh.positions.size() - 1);
        crossings.emplace(key, index);
        return index;
    };
    std::vector<std::array<std::uint32_t, 3>> triangles;
    triangles.reserve(mesh.triangles.size());
    for (const auto& triangle : mesh.triangles) {
        const auto above = [&](std::uint32_t v) { return s[v] > 0.0; };
        const int count = static_cast<int>(above(triangle[0])) +
                          static_cast<int>(above(triangle[1])) +
                          static_cast<int>(above(triangle[2]));
        if (count == 0 || count == 3) {
            triangles.push_back(triangle);
            continue;
        }
        std::size_t lone = 0;
        while (above(triangle[lone]) == (count == 2)) {
            ++lone;
        }
        const std::uint32_t a = triangle[lone];
        const std::uint32_t b = triangle[(lone + 1) % 3];
        const std::uint32_t c = triangle[(lone + 2) % 3];
        const std::uint32_t ab = crossing(a, b);
        const std::uint32_t ac = crossing(a, c);
        triangles.push_back({a, ab, ac});
        triangles.push_back({ab, b, c});
        triangles.push_back({ab, c, ac});
    }
    mesh.triangles = std::move(triangles);
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] > 0.0) {
            mesh.positions[i] = mesh.positions[i] - normal * s[i];
        }
    }
}

/// The box the relief of a boulder at (`x`, `y`) reaching `reach` metres is drawn over: on a
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

/// How far a placed shape reaches from its boulder's centre `cx`, `cy` along either axis.
double ShapeReach(const TriangleMesh& placed, double cx, double cy) {
    const Box3d box = Bounds(placed);
    return std::fmax(std::fmax(std::fabs(box.min.x - cx), std::fabs(box.max.x - cx)),
                     std::fmax(std::fabs(box.min.y - cy), std::fabs(box.max.y - cy)));
}

/// A uniform in [`low`, `high`) from `u`.
double Between(double low, double high, double u) {
    return low + (high - low) * u;
}

}  // namespace

BoulderLayer::BoulderLayer(BoulderLayerOptions options, std::shared_ptr<const CraterLayer> craters,
                           Surface surface)
    : options_(std::move(options)), craters_(std::move(craters)), surface_(surface) {
    if (craters_ && !(craters_->Options().surface == surface_)) {
        throw std::invalid_argument("A boulder layer and its crater layer must share a surface");
    }
    if (surface_.IsBody()) {
        cells_ = std::make_shared<CubeSphereCells>(surface_);
    } else {
        cells_ = std::make_shared<PlanarCells>();
    }
    candidates_ = BoulderCandidates(options_);
    minCraterDiameter_ = options_.ejecta.MinCraterDiameter();
    if (minCraterDiameter_ > 0.0 && !craters_) {
        throw std::invalid_argument(
            "Boulder ejecta need the crater layer whose craters throw them");
    }
    maxBackground_ = MaxBackgroundAbundance(options_);
    if (options_.densityMap) {
        mapAbundance_.emplace(options_);
        patchMean_ = MeanPatchAbundance(options_.patches);
    }
    if (options_.slopeMap) {
        referenceCount_ =
            options_.sizes.distribution->Cumulative(options_.slopeMap->referenceDiameter);
    }
    if (options_.clusters.share > 0.0) {
        clusterMean_ = options_.clusters.model == BoulderClusterModel::Thomas
                           ? MeanThomasField(options_.clusters, surface_.IsBody())
                           : MeanClusterBump(options_.clusters);
    }
    ejectaSizes_ = minCraterDiameter_ > 0.0 ? EjectaSizes(options_) : nullptr;
    if (options_.cacheBoulders > 0) {
        cache_ = std::make_shared<CraterCellCache>(options_.cacheBoulders);
    }
    if (options_.bedrock.exposure) {
        exposure_.emplace(*options_.bedrock.exposure, surface_);
    }
    if (exposure_ && options_.densityMap) {
        constexpr std::size_t kMeanCells = 1u << 16;
        const double side = options_.bedrock.meanSize / surface_.UnitLength();
        meanCell_ = {side, side};
        if (const auto* raster =
                dynamic_cast<const RasterField*>(options_.densityMap->field.get())) {
            const GeoTransform& transform = raster->Values().Transform();
            meanOrigin_ = {transform.originX, transform.originY};
            meanCell_ = {transform.pixelWidth, transform.pixelHeight};
        }
        bedrockMeans_ = std::make_shared<LruCache<MeanCell, double, MeanCellHash>>(
            kMeanCells, [](const double&) { return std::size_t{1}; });
    }
    const std::uint32_t prototypes = options_.rendering.shapePrototypes;
    shapes_ = std::make_shared<LruCache<std::uint64_t, BoulderShape>>(
        prototypes > 0 ? prototypes : 4096, [](const BoulderShape&) { return std::size_t{1}; });
    fragments_ = std::make_shared<LruCache<std::uint64_t, std::vector<BoulderFragment>>>(
        prototypes > 0 ? prototypes : 4096,
        [](const std::vector<BoulderFragment>&) { return std::size_t{1}; });
}

std::shared_ptr<const BoulderShape> BoulderLayer::ShapeOf(std::uint64_t shapeSeed) const {
    return shapes_->Get(shapeSeed,
                        [&] { return GenerateBoulderShape(shapeSeed, options_.rendering.shapes); });
}

std::shared_ptr<const std::vector<BoulderFragment>>
BoulderLayer::FragmentsOf(std::uint64_t shapeSeed) const {
    return fragments_->Get(shapeSeed, [&] {
        const BoulderFragmentation& fragmentation = options_.fragmentation;
        const auto span =
            static_cast<std::uint32_t>(fragmentation.maxPieces - fragmentation.minPieces + 1);
        const int pieces =
            fragmentation.minPieces +
            static_cast<int>(PcgHash(static_cast<std::uint32_t>(shapeSeed) ^ kBoulderPieces) %
                             span);
        const std::uint32_t pick = PcgHash(static_cast<std::uint32_t>(shapeSeed >> 32) ^
                                           kBoulderPieces ^ static_cast<std::uint32_t>(shapeSeed));
        const BoulderCracks cracks = (pick >> 8) * 0x1.0p-24 < fragmentation.slabShare
                                         ? BoulderCracks::Slabs
                                         : BoulderCracks::Radial;
        return FragmentBoulderShape(*ShapeOf(shapeSeed), pieces, shapeSeed, cracks);
    });
}

std::shared_ptr<const BoulderShape> BoulderLayer::Shape(const Boulder& boulder) const {
    const std::uint32_t prototypes = options_.rendering.shapePrototypes;
    if (boulder.parent == 0) {
        return ShapeOf(BoulderShapeSeed(boulder.id, prototypes));
    }
    const auto fragments = FragmentsOf(BoulderShapeSeed(boulder.parent, prototypes));
    return {fragments, &(*fragments)[boulder.piece].shape};
}

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
        PlaceBoulderModel(InLocalFrame(boulder), *shape, 0.0, options_.pose.settle);
    TriangleMesh mesh = shape->mesh;
    for (Vec3d& p : mesh.positions) {
        p = placement.Apply(p);
    }
    CutFacets(mesh, InLocalFrame(boulder), 0.0);
    return mesh;
}

double BoulderLayer::Wear(double diameter, const BoulderAbundance& abundance, double density,
                          std::uint64_t id) const {
    const IdWords words = SplitId(id);
    const U32x4 draw =
        Philox4x32({words.low, words.high, kBoulderAttributes, 2u}, {options_.seed, kBoulderSeed});
    if (abundance.ejecta > 0.0 && density > 0.0) {
        const double fromCrater = abundance.ejecta * ejectaSizes_->Incremental(diameter) / density;
        if (Uniform(draw.x, draw.y) < fromCrater) {
            return abundance.ejectaWear;
        }
    }
    return Uniform(draw.z, draw.w);
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

double BoulderLayer::MaterialCoverAt(double x, double y, double spacing,
                                     const Ground* ground) const {
    const double rim = options_.rendering.relief == BoulderRelief::Dome ? 0.5 * spacing : 0.0;
    const std::vector<Boulder> boulders =
        Boulders(surface_.Expand({x, y, x, y}, rim), spacing, ground);
    double cover = 0.0;
    for (const Relief& relief : PrepareRelief(boulders, spacing)) {
        const TriangleMesh* shape = relief.placed ? &*relief.placed : nullptr;
        if (relief.rock && RockRelief(x, y, *relief.boulder, shape, relief.width) > 0.0) {
            cover = std::fmax(cover, relief.weight);
        }
    }
    for (const Boulder& boulder : boulders) {
        const Vec2d offset = surface_.Offset({boulder.x, boulder.y}, {x, y});
        if (boulder.model && std::hypot(offset.x, offset.y) < 0.5 * boulder.diameter) {
            cover = std::fmax(cover, ResolutionWeight(boulder.diameter, spacing, options_.fade));
        }
    }
    return cover;
}

Raster<double> BoulderLayer::MaterialCoverOver(const RasterGrid& grid, double spacing,
                                               const Ground* ground, unsigned threads) const {
    Raster<double> cover(grid.width, grid.height);
    cover.SetTransform(grid.transform);
    const double rim = options_.rendering.relief == BoulderRelief::Dome ? 0.5 * spacing : 0.0;
    const std::vector<Boulder> boulders =
        Boulders(surface_.Expand(grid.Bounds(), rim), spacing, ground);
    std::vector<Relief> reliefs = PrepareRelief(boulders, spacing);
    std::erase_if(reliefs, [](const Relief& relief) { return !relief.rock; });
    std::vector<std::vector<PixelWindow>> windows;
    for (const Relief& relief : reliefs) {
        windows.push_back(PixelWindowsIn(grid, relief.box, surface_));
    }
    ForEachFeatureCell(
        cover, threads, reliefs.size(),
        [&](std::size_t n) -> const std::vector<PixelWindow>& { return windows[n]; },
        [&](double& cell, const FeatureCell& at) {
            const Relief& relief = reliefs[at.feature];
            const TriangleMesh* shape = relief.placed ? &*relief.placed : nullptr;
            if (RockRelief(at.centre.x, at.centre.y, *relief.boulder, shape, relief.width) > 0.0) {
                cell = std::fmax(cell, relief.weight);
            }
        });
    std::vector<const Boulder*> models;
    windows.clear();
    for (const Boulder& boulder : boulders) {
        if (boulder.model) {
            models.push_back(&boulder);
            windows.push_back(PixelWindowsIn(
                grid, ReliefBox(boulder.x, boulder.y, 0.5 * boulder.diameter, surface_), surface_));
        }
    }
    ForEachFeatureCell(
        cover, threads, models.size(),
        [&](std::size_t n) -> const std::vector<PixelWindow>& { return windows[n]; },
        [&](double& cell, const FeatureCell& at) {
            const Boulder& boulder = *models[at.feature];
            const Vec2d offset = surface_.Offset({boulder.x, boulder.y}, at.centre);
            if (std::hypot(offset.x, offset.y) < 0.5 * boulder.diameter) {
                cell = std::fmax(cell, ResolutionWeight(boulder.diameter, spacing, options_.fade));
            }
        });
    return cover;
}

double BoulderLayer::OwnMarginRadii() const {
    return options_.fillet.heightShare > 0.0 ? 1.0 + 2.0 * options_.fillet.widthRatio : 1.0;
}

double BoulderLayer::BreakMarginRadii() const {
    const BoulderFragmentation& fragmentation = options_.fragmentation;
    return fragmentation.Enabled() ? 2.0 * (fragmentation.gapRatio + fragmentation.spreadRatio)
                                   : 0.0;
}

CraterList BoulderLayer::ScatterBoulderCandidates(const Bounds2d& region, double spacing) const {
    const std::uint32_t seed = options_.seed ^ kBoulderSeed;
    const FreshAges ages;
    const double margin = OwnMarginRadii() + BreakMarginRadii();
    CraterGenerationOptions generation{.dMin = options_.sizes.minDiameter,
                                       .dMax = options_.sizes.maxDiameter,
                                       .marginRadii = margin};
    generation.surface = surface_;
    generation.logName = "boulders";
    if (spacing > 0.0) {
        generation.minResolvedDiameter = options_.fade.startSamples * spacing;
    }
    for (const Crater& boulder : options_.manual) {
        generation.manual.Append(boulder);
    }
    generation.completeness = options_.completeness;
    if (cache_) {
        generation.cellCraters = CachedCellCraters(*cache_, *cells_, *candidates_, ages, seed);
    }
    return GenerateCraters(*cells_, region, *candidates_, ages, seed, generation);
}

std::vector<Boulder> BoulderLayer::DrawBoulders(const CraterList& candidates, double spacing,
                                                const Ground* ground,
                                                const EjectaIndex& ejecta) const {
    std::vector<Kept> kept;
    for (std::size_t n = 0; n < candidates.Size(); ++n) {
        if (std::optional<Kept> k = Keep(candidates.At(n), ground, ejecta)) {
            kept.push_back(*k);
        }
    }
    std::optional<PrefetchedGround> fetched;
    if (ground != nullptr) {
        const BoulderPose& pose = options_.pose;
        const bool gradient = pose.followSlope || pose.maxSlope.has_value();
        std::vector<Vec2d> points;
        points.reserve(kept.size() * (gradient ? 5 : 1));
        for (const Kept& k : kept) {
            const Crater& c = k.candidate;
            points.push_back({c.x, c.y});
            if (gradient) {
                for (const Vec2d& p : GradientPoints(c.x, c.y, c.diameter)) {
                    points.push_back(p);
                }
            }
        }
        fetched.emplace(*ground, points, options_.bedrock.exposureSpacing);
    }
    std::vector<Boulder> boulders;
    boulders.reserve(kept.size());
    for (const Kept& k : kept) {
        if (std::optional<Boulder> boulder = Place(k, spacing, fetched ? &*fetched : nullptr)) {
            boulders.push_back(*boulder);
        }
    }
    return boulders;
}

std::optional<BoulderLayer::Kept> BoulderLayer::Keep(const Crater& candidate, const Ground* ground,
                                                     const EjectaIndex& ejecta) const {
    const IdWords id = SplitId(candidate.id);
    const U32x4 draw =
        Philox4x32({id.low, id.high, kBoulderAttributes, 0u}, {options_.seed, kBoulderSeed});
    Kept kept{candidate, Abundance(candidate.x, candidate.y, ground, ejecta)};
    if (!candidate.manual) {
        kept.density = Density(candidate.diameter, kept.abundance);
        if (Uniform(draw.x, draw.y) >=
            kept.density / candidates_->Incremental(candidate.diameter)) {
            return std::nullopt;
        }
    }
    return kept;
}

std::optional<Boulder> BoulderLayer::Place(const Kept& kept, double spacing,
                                           const Ground* ground) const {
    const Crater& candidate = kept.candidate;
    const BoulderAbundance& abundance = kept.abundance;
    const double density = kept.density;
    const IdWords id = SplitId(candidate.id);
    const U32x4 draw =
        Philox4x32({id.low, id.high, kBoulderAttributes, 0u}, {options_.seed, kBoulderSeed});
    const U32x4 shape =
        Philox4x32({id.low, id.high, kBoulderAttributes, 1u}, {options_.seed, kBoulderSeed});
    const BoulderPose& pose = options_.pose;
    const double ratio =
        Between(pose.minHeightRatio, pose.maxHeightRatio, Uniform(shape.x, shape.y));
    const double wear = Wear(candidate.diameter, abundance, density, candidate.id);
    double share = Uniform(draw.z, draw.w);
    if (pose.burialByWear > 0.0) {
        share = (1.0 - pose.burialByWear) * share + pose.burialByWear * wear;
    }
    const double burial = Between(pose.minBurial, pose.maxBurial, share);
    Boulder boulder{candidate.x,
                    candidate.y,
                    candidate.diameter,
                    ratio * candidate.diameter,
                    burial,
                    2.0 * std::numbers::pi * Uniform(shape.z, shape.w),
                    pose.maxLean * Uniform(shape.w, shape.x),
                    candidate.id};
    boulder.wear = wear;
    boulder.model = options_.rendering.models.IsModel(boulder.diameter, spacing);
    if (pose.maxSlope && ground != nullptr && !candidate.manual && !Roll(boulder, *ground)) {
        return std::nullopt;
    }
    if (ground != nullptr) {
        const double height =
            ground->HeightAt(boulder.x, boulder.y, options_.bedrock.exposureSpacing);
        if (!std::isnan(height)) {
            boulder.ground = height;
        }
    }
    if (pose.sideShare > 0.0 || pose.endShare > 0.0 || pose.maxTilt > 0.0 || pose.followSlope) {
        Settle(boulder, ground);
    }
    if (options_.ventifacts.share > 0.0) {
        Abrade(boulder);
    }
    return boulder;
}

void BoulderLayer::Abrade(Boulder& boulder) const {
    const BoulderVentifacts& ventifacts = options_.ventifacts;
    const IdWords id = SplitId(boulder.id);
    const U32x4 draw =
        Philox4x32({id.low, id.high, kBoulderAttributes, 4u}, {options_.seed, kBoulderSeed});
    if (Uniform(draw.x, draw.y) >= ventifacts.share) {
        return;
    }
    const U32x4 angles =
        Philox4x32({id.low, id.high, kBoulderAttributes, 5u}, {options_.seed, kBoulderSeed});
    const double reach = 0.5 * boulder.diameter * (1.0 - boulder.wear);
    const auto facet = [&](double wind, double pick) {
        return BoulderFacet{wind + std::numbers::pi,
                            Between(ventifacts.minAngle, ventifacts.maxAngle, pick), reach};
    };
    boulder.facets[0] = facet(ventifacts.windAzimuth, Uniform(angles.x, angles.y));
    boulder.facetCount = 1;
    if (ventifacts.secondWindAzimuth && Uniform(draw.z, draw.w) < ventifacts.keelShare) {
        boulder.facets[1] = facet(*ventifacts.secondWindAzimuth, Uniform(angles.z, angles.w));
        boulder.facetCount = 2;
    }
}

void BoulderLayer::CutFacets(TriangleMesh& placed, const Boulder& local, double base) const {
    for (std::uint8_t k = 0; k < local.facetCount; ++k) {
        const BoulderFacet& facet = local.facets[k];
        const Vec3d normal{std::cos(facet.azimuth) * std::sin(facet.angle),
                           std::sin(facet.azimuth) * std::sin(facet.angle), std::cos(facet.angle)};
        CutByPlane(placed, normal, [&](const Vec3d& p) {
            return (p.z - base - facet.HeightAt({p.x - local.x, p.y - local.y})) * normal.z;
        });
    }
}

BoulderShape BoulderLayer::Abraded(const BoulderShape& shape, const BoulderPlacement& placement,
                                   const Boulder& local, double base) const {
    BoulderShape cut = shape;
    for (Vec3d& p : cut.mesh.positions) {
        p = placement.Apply(p);
    }
    CutFacets(cut.mesh, local, base);
    const std::array<double, 9>& r = placement.pose.rotation;
    for (Vec3d& p : cut.mesh.positions) {
        const Vec3d d = p - placement.pose.translation;
        p = {(r[0] * d.x + r[3] * d.y + r[6] * d.z) / placement.scale.x,
             (r[1] * d.x + r[4] * d.y + r[7] * d.z) / placement.scale.y,
             (r[2] * d.x + r[5] * d.y + r[8] * d.z) / placement.scale.z};
    }
    return cut;
}

bool BoulderLayer::Roll(Boulder& boulder, const Ground& ground) const {
    constexpr double kMaxSteps = 128.0;
    const BoulderPose& pose = options_.pose;
    const double limit = std::tan(*pose.maxSlope);
    const double reach = pose.rollDistance.value_or(0.0);
    const double step = std::fmax(boulder.diameter, reach / kMaxSteps);
    double rolled = 0.0;
    for (;;) {
        const std::optional<Vec2d> g = GroundGradient(boulder, ground);
        const double gradient = g ? std::hypot(g->x, g->y) : 0.0;
        if (!(gradient > limit)) {
            return true;
        }
        if (rolled + step > reach) {
            return false;
        }
        const Vec2d to = surface_.Locate({boulder.x, boulder.y},
                                         {-g->x / gradient * step, -g->y / gradient * step});
        boulder.x = to.x;
        boulder.y = to.y;
        rolled += step;
    }
}

std::array<Vec2d, 4> BoulderLayer::GradientPoints(double x, double y, double diameter) const {
    const double step = 0.5 * diameter;
    return {surface_.Locate({x, y}, {step, 0.0}), surface_.Locate({x, y}, {-step, 0.0}),
            surface_.Locate({x, y}, {0.0, step}), surface_.Locate({x, y}, {0.0, -step})};
}

std::optional<Vec2d> BoulderLayer::GroundGradient(const Boulder& boulder,
                                                  const Ground& ground) const {
    const double step = 0.5 * boulder.diameter;
    const std::array<Vec2d, 4> points = GradientPoints(boulder.x, boulder.y, boulder.diameter);
    const auto at = [&](std::size_t n) {
        return static_cast<double>(
            ground.HeightAt(points[n].x, points[n].y, options_.bedrock.exposureSpacing));
    };
    const double gx = (at(0) - at(1)) / (2.0 * step);
    const double gy = (at(2) - at(3)) / (2.0 * step);
    if (!std::isfinite(gx) || !std::isfinite(gy)) {
        return std::nullopt;
    }
    return Vec2d{gx, gy};
}

void BoulderLayer::Settle(Boulder& boulder, const Ground* ground) const {
    const BoulderPose& pose = options_.pose;
    const IdWords id = SplitId(boulder.id);
    const U32x4 draw =
        Philox4x32({id.low, id.high, kBoulderAttributes, 3u}, {options_.seed, kBoulderSeed});
    const double pick = Uniform(draw.x, draw.y);
    boulder.rest = pick < pose.endShare                    ? BoulderRest::End
                   : pick < pose.endShare + pose.sideShare ? BoulderRest::Side
                                                           : BoulderRest::Face;
    Vec3d up{0.0, 0.0, 1.0};
    if (pose.maxTilt > 0.0) {
        const double cosine = 1.0 - Uniform(draw.z, draw.w) * (1.0 - std::cos(pose.maxTilt));
        const double sine = std::sqrt(std::fmax(0.0, 1.0 - cosine * cosine));
        const double azimuth = 2.0 * std::numbers::pi * Uniform(draw.w, draw.x);
        up = {sine * std::cos(azimuth), sine * std::sin(azimuth), cosine};
    }
    if (pose.followSlope && ground != nullptr) {
        if (const std::optional<Vec2d> g = GroundGradient(boulder, *ground)) {
            const double slope = std::atan(std::hypot(g->x, g->y));
            up = TiltRotation(slope, std::atan2(-g->y, -g->x)).Apply(up);
        }
    }
    boulder.tilt = std::acos(std::clamp(up.z, -1.0, 1.0));
    boulder.tiltAzimuth = std::atan2(up.y, up.x);
}

std::vector<Boulder> BoulderLayer::Boulders(const Bounds2d& region, double spacing,
                                            const Ground* ground) const {
    if (exposure_) {
        RequireGround(ground, "Boulders that read the exposure field");
    }
    const double roll = ground != nullptr ? options_.pose.rollDistance.value_or(0.0) : 0.0;
    const Bounds2d from = roll > 0.0 ? surface_.Expand(region, roll) : region;
    const auto rests = [&](const Boulder& boulder) {
        const double own = 0.5 * boulder.diameter * OwnMarginRadii();
        return roll == 0.0 ||
               surface_.Contains(surface_.Expand(region, own), {boulder.x, boulder.y});
    };
    const CraterList candidates = ScatterBoulderCandidates(from, spacing);
    if (!options_.fragmentation.Enabled()) {
        std::vector<Boulder> boulders =
            DrawBoulders(candidates, spacing, ground, EjectaCraters(from));
        std::erase_if(boulders, [&](const Boulder& boulder) { return !rests(boulder); });
        return boulders;
    }
    const double reach = 0.5 * options_.sizes.maxDiameter * (OwnMarginRadii() + BreakMarginRadii());
    const EjectaIndex ejecta = EjectaCraters(surface_.Expand(from, reach));
    std::vector<Boulder> boulders;
    for (const Boulder& parent : DrawBoulders(candidates, spacing, ground, ejecta)) {
        for (const Boulder& boulder : Break(parent, spacing, ground)) {
            const double own = 0.5 * boulder.diameter * OwnMarginRadii();
            if (surface_.Contains(surface_.Expand(region, own), {boulder.x, boulder.y})) {
                boulders.push_back(boulder);
            }
        }
    }
    return boulders;
}

std::vector<Boulder> BoulderLayer::Break(const Boulder& parent, double spacing,
                                         const Ground* ground) const {
    const BoulderFragmentation& fragmentation = options_.fragmentation;
    if (parent.diameter < fragmentation.minDiameter || parent.wear < fragmentation.splitWear) {
        return {parent};
    }
    const bool rubble = parent.wear >= fragmentation.rubbleWear;
    const double opening = rubble || fragmentation.rubbleWear <= fragmentation.splitWear
                               ? 1.0
                               : (parent.wear - fragmentation.splitWear) /
                                     (fragmentation.rubbleWear - fragmentation.splitWear);
    const double scatter =
        !rubble ? 0.0
        : fragmentation.rubbleWear < 1.0
            ? (parent.wear - fragmentation.rubbleWear) / (1.0 - fragmentation.rubbleWear)
            : 1.0;
    const auto shape = Shape(parent);
    const auto fragments =
        FragmentsOf(BoulderShapeSeed(parent.id, options_.rendering.shapePrototypes));
    Boulder origin = parent;
    origin.x = 0.0;
    origin.y = 0.0;
    const BoulderPlacement placement = PlaceBoulderModel(origin, *shape, 0.0, options_.pose.settle);
    const IdWords id = SplitId(parent.id);
    RandomStream random(options_.seed ^ kBoulderPieces, kBoulderSeed, id.low, id.high);
    const double smallest = spacing > 0.0 ? options_.fade.startSamples * spacing : 0.0;
    std::vector<Boulder> pieces;
    for (std::size_t k = 0; k < fragments->size(); ++k) {
        const BoulderFragment& fragment = (*fragments)[k];
        const double push = random.NextDouble();
        const double twist = random.NextDouble();
        double low = std::numeric_limits<double>::infinity();
        double high = -low;
        for (const Vec3d& p : fragment.shape.mesh.positions) {
            const double z = placement.Apply(fragment.centre + p * fragment.scale).z;
            low = std::fmin(low, z);
            high = std::fmax(high, z);
        }
        const Vec3d centre = placement.Apply(fragment.centre);
        const double out = std::hypot(centre.x, centre.y);
        const double angle =
            out > 0.0 ? std::atan2(centre.y, centre.x) : 2.0 * std::numbers::pi * push;
        const double gap = (opening * fragmentation.gapRatio +
                            scatter * fragmentation.spreadRatio * (0.5 + 0.5 * push)) *
                           parent.diameter;
        Boulder piece = parent;
        piece.facetCount = 0;
        const UVec3 key =
            Pcg3dHash({id.low, id.high, kBoulderPieces + static_cast<std::uint32_t>(k)});
        piece.id = (static_cast<std::uint64_t>(key.x) << 32) | key.y;
        piece.parent = parent.id;
        piece.piece = static_cast<std::uint32_t>(k);
        piece.diameter = parent.diameter * fragment.scale;
        piece.height = parent.height * fragment.shape.shortAxis * fragment.scale / shape->shortAxis;
        piece.yaw = parent.yaw + scatter * std::numbers::pi * (2.0 * twist - 1.0);
        piece.lean = parent.lean * (1.0 - scatter);
        piece.tilt = parent.tilt * (1.0 - scatter);
        const double sunk = std::clamp(-low / (high - low), 0.0, 0.95);
        piece.burial = std::fmax(sunk, sunk + scatter * (options_.pose.maxBurial - sunk));
        const Vec2d at = surface_.Locate({parent.x, parent.y}, {centre.x + gap * std::cos(angle),
                                                                centre.y + gap * std::sin(angle)});
        piece.x = at.x;
        piece.y = at.y;
        if (piece.diameter < smallest) {
            continue;
        }
        piece.model = options_.rendering.models.IsModel(piece.diameter, spacing);
        piece.ground.reset();
        if (ground != nullptr) {
            const double height =
                ground->HeightAt(piece.x, piece.y, options_.bedrock.exposureSpacing);
            if (!std::isnan(height)) {
                piece.ground = height;
            }
        }
        pieces.push_back(piece);
    }
    return pieces;
}

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

float AddBoulder(float height, double x, double y, const Boulder& boulder, double weight,
                 const Surface& surface, double width) {
    const Vec2d offset = surface.Offset({boulder.x, boulder.y}, {x, y});
    if (!(width > 0.0)) {
        const double dx = offset.x;
        const double dy = offset.y;
        const double radius = 0.5 * boulder.diameter;
        const double r2 = (dx * dx + dy * dy) / (radius * radius);
        if (r2 >= 1.0) {
            return height;
        }
        const double top =
            boulder.CutByFacets(offset, boulder.ExposedHeight() * std::sqrt(1.0 - r2));
        return static_cast<float>(static_cast<double>(height) + weight * top);
    }
    const double dome =
        boulder.CutByFacets(offset, DomeHeight(boulder, std::hypot(offset.x, offset.y), width));
    return dome > 0.0 ? static_cast<float>(static_cast<double>(height) + weight * dome) : height;
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

void BoulderLayer::DrawRelief(SampleBlock& samples, const std::vector<Relief>& reliefs,
                              unsigned threads) const {
    const auto draw = [&](float height, float base, const Vec2d& p, const Relief& relief) {
        const TriangleMesh* shape = relief.placed ? &*relief.placed : nullptr;
        return AddRelief(height, base, p.x, p.y, *relief.boulder, shape, relief.weight, relief.rock,
                         relief.width);
    };
    if (Raster<float>* heights = samples.Grid()) {
        const std::span<const float> values = std::as_const(*heights).Values();
        const std::vector<float> ground(values.begin(), values.end());
        const std::size_t width = heights->Width();
        std::vector<std::vector<PixelWindow>> windows;
        for (const Relief& relief : reliefs) {
            windows.push_back(PixelWindowsIn(*heights, relief.box, surface_));
        }
        ForEachFeatureCell(
            *heights, threads, reliefs.size(),
            [&](std::size_t n) -> const std::vector<PixelWindow>& { return windows[n]; },
            [&](float& cell, const FeatureCell& at) {
                const float base = ground[at.row * width + at.col];
                cell = draw(cell, base, at.centre, reliefs[at.feature]);
            });
        return;
    }
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        if (samples.IsNoData(i)) {
            continue;
        }
        const Vec2d p = samples.Position(i);
        const float base = samples.Height(i);
        for (const Relief& relief : reliefs) {
            if (surface_.Contains(relief.box, p)) {
                samples.Height(i) = draw(samples.Height(i), base, p, relief);
            }
        }
    }
}

void BoulderLayer::ApplySamples(SampleBlock& samples, const LayerContext& context) const {
    if (options_.rendering.relief == BoulderRelief::None && options_.fillet.heightShare <= 0.0) {
        return;
    }
    const Raster<float>* grid = samples.Grid();
    const double spacing =
        grid != nullptr ? std::max(context.spacing, RasterGrid::FromRaster(*grid).Spacing(surface_))
                        : context.spacing;
    const double rim = options_.rendering.relief == BoulderRelief::Dome ? 0.5 * spacing : 0.0;
    const std::vector<Boulder> boulders =
        Boulders(surface_.Expand(samples.Bounds(), rim), spacing, context.ground);
    DrawRelief(samples, PrepareRelief(boulders, spacing), context.threads);
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
            PlaceBoulderModel(local, *shape, *boulder.ground, options_.pose.settle);
        if (boulder.facetCount > 0) {
            shape = std::make_shared<const BoulderShape>(
                Abraded(*shape, placement, local, *boulder.ground));
        }
        models[n] = {boulder,
                     BoulderShapeSeed(boulder.parent != 0 ? boulder.parent : boulder.id,
                                      options_.rendering.shapePrototypes),
                     shape, placement};
    });
    return models;
}

BoulderFeatures BoulderLayer::BouldersOver(const RasterGrid& grid, double spacing,
                                           const Ground* ground) const {
    BoulderFeatures placed{
        Boulders(grid.Bounds(), std::max(spacing, grid.Spacing(surface_)), ground), {}};
    placed.models = Models(placed.boulders);
    return placed;
}

}  // namespace terraingen
