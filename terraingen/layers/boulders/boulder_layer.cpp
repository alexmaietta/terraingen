#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/layers/boulders/boulder_clusters.h"

#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/hash.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/cells.h"
#include "terraingen/util/parallel.h"
#include "terraingen/util/span.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// The ground under one candidate: its heights at a few points, at one spacing, read up front
/// with every other candidate's; any other point or spacing is read from the ground beneath.
/// Gives the same heights either way.
class CandidateGround final : public Ground {
  public:
    CandidateGround(const Ground& ground, Span<const Vec2d> points, Span<const float> heights,
                    double spacing)
        : ground_(ground), points_(points), heights_(heights), spacing_(spacing) {}

    float HeightAt(double x, double y, double spacing) const override {
        if (spacing == spacing_) {
            for (std::size_t n = 0; n < points_.size(); ++n) {
                if (BitCast<std::uint64_t>(points_[n].x) == BitCast<std::uint64_t>(x) &&
                    BitCast<std::uint64_t>(points_[n].y) == BitCast<std::uint64_t>(y)) {
                    return heights_[n];
                }
            }
        }
        return ground_.HeightAt(x, y, spacing);
    }

  private:
    const Ground& ground_;
    Span<const Vec2d> points_;
    Span<const float> heights_;
    double spacing_;
};

/// How far in radii a boulder shape can reach from its centre: half the diagonal of its unit box
/// over half its longest axis, for a shape tilted or resting on a side.
constexpr double kShapeReachRadii = 1.7320508075688772;

/// The purposes a boulder draws numbers for, each its own substream of its ID.
enum class BoulderDraw : std::uint32_t { Keep, Pose, Wear, Settle, Abrade, Fragments };

/// The substream of `kBoulderAttributeStream` for boulder or shape `id` and purpose `draw`.
RandomStream AttributeStream(std::uint32_t seed, std::uint64_t id, BoulderDraw draw) {
    const IdWords words = SplitId(id);
    return RandomStream(seed, kBoulderAttributeStream, words.low, words.high,
                        static_cast<std::uint32_t>(draw));
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

}  // namespace

BoulderLayer::BoulderLayer(BoulderLayerOptions options, std::shared_ptr<const CraterLayer> craters,
                           Surface surface, std::shared_ptr<const Source> source)
    : BoulderLayer(std::move(options),
                   craters ? std::vector<std::shared_ptr<const CraterLayer>>{std::move(craters)}
                           : std::vector<std::shared_ptr<const CraterLayer>>{},
                   surface, std::move(source)) {}

BoulderLayer::BoulderLayer(BoulderLayerOptions options,
                           std::vector<std::shared_ptr<const CraterLayer>> craters, Surface surface,
                           std::shared_ptr<const Source> source)
    : options_(std::move(options)), craters_(std::move(craters)), surface_(surface) {
    for (const auto& layer : craters_) {
        if (!layer) {
            throw std::invalid_argument("A boulder layer's crater layers must not be null");
        }
        if (!(layer->Options().surface == surface_)) {
            throw std::invalid_argument(
                "A boulder layer and its crater layer must share a surface");
        }
    }
    ValidateBoulderLayerOptions(options_);
    if (auto limit = LayerResolvedLimit(source, options_.sourceResolvesPixels, surface_)) {
        std::shared_ptr<const SurfaceField>& completeness = options_.completeness;
        completeness = completeness ? std::make_shared<const MinimumField>(completeness, limit)
                                    : std::move(limit);
    }
    for (const ManualBoulder& boulder : options_.manual) {
        if (boulder.FixesAny()) {
            fixed_.emplace(ManualBoulderId(boulder), boulder);
        }
        largestManual_ = std::fmax(largestManual_, boulder.diameter);
    }
    cells_ = MakeCells(surface_);
    SetUpShapeCaches();
    if (!options_.generateRandom) {
        return;
    }
    minCraterDiameter_ = options_.ejecta.MinCraterDiameter();
    if (minCraterDiameter_ > 0.0 && craters_.empty()) {
        throw std::invalid_argument(
            "Boulder ejecta need the crater layer whose craters throw them");
    }
    SetUpAbundance();
    if (options_.cacheBoulders > 0) {
        cache_ = std::make_shared<CraterCellCache>(options_.cacheBoulders);
    }
    if (options_.bedrock.exposure) {
        exposure_.emplace(*options_.bedrock.exposure, surface_);
    }
    if (exposure_ && options_.densityMap) {
        SetUpBedrockMeans();
    }
}

std::shared_ptr<BoulderLayer> BoulderLayer::Manual(std::vector<ManualBoulder> boulders,
                                                   BoulderLayerOptions options, Surface surface) {
    options.generateRandom = false;
    options.manual = std::move(boulders);
    return std::make_shared<BoulderLayer>(std::move(options), std::shared_ptr<const CraterLayer>{},
                                          surface);
}

double BoulderLayer::MaxFeatureDiameter() const {
    return std::fmax(options_.generateRandom ? options_.sizes.maxDiameter : 0.0, largestManual_);
}

void BoulderLayer::SetUpAbundance() {
    BoulderBackground background(options_);
    maxBackground_ = background.max;
    clusterSeed_ = SubstreamSeed(options_.seed, kBoulderClusterStream);
    patchSeed_ = SubstreamSeed(options_.seed, kBoulderPatchStream);
    patchMean_ = background.patchMean;
    mapAbundance_ = std::move(background.map);
    candidates_ = BoulderCandidates(options_, maxBackground_);
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
}

void BoulderLayer::SetUpBedrockMeans() {
    constexpr std::size_t kMeanCells = 1u << 16;
    const double side = options_.bedrock.meanSize / surface_.UnitLength();
    bedrockCellSize_ = {side, side};
    if (const auto* raster = dynamic_cast<const RasterField*>(options_.densityMap->field.get())) {
        const GeoTransform& transform = raster->Values().Transform();
        bedrockOrigin_ = {transform.originX, transform.originY};
        bedrockCellSize_ = {transform.pixelWidth, transform.pixelHeight};
    }
    bedrockMeans_ = std::make_shared<LruCache<BedrockCell, double, BedrockCellHash>>(
        kMeanCells, [](const double&) { return std::size_t{1}; });
}

void BoulderLayer::SetUpShapeCaches() {
    const std::uint32_t prototypes = options_.rendering.shapePrototypes;
    shapes_ = std::make_shared<LruCache<std::uint64_t, BoulderShape>>(
        prototypes > 0 ? prototypes : 4096, [](const BoulderShape&) { return std::size_t{1}; });
    fragments_ = std::make_shared<LruCache<std::uint64_t, std::vector<BoulderFragment>>>(
        prototypes > 0 ? prototypes : 4096,
        [](const std::vector<BoulderFragment>&) { return std::size_t{1}; });
}

std::shared_ptr<const BoulderShape> BoulderLayer::CachedShape(std::uint64_t shapeSeed) const {
    return shapes_->Get(shapeSeed,
                        [&] { return GenerateBoulderShape(shapeSeed, options_.rendering.shapes); });
}

std::shared_ptr<const std::vector<BoulderFragment>>
BoulderLayer::CachedFragments(std::uint64_t shapeSeed) const {
    return fragments_->Get(shapeSeed, [&] {
        const BoulderFragmentation& fragmentation = options_.fragmentation;
        const auto span =
            static_cast<std::uint32_t>(fragmentation.maxPieces - fragmentation.minPieces + 1);
        RandomStream random = AttributeStream(options_.seed, shapeSeed, BoulderDraw::Fragments);
        const int pieces = fragmentation.minPieces + static_cast<int>(random.NextU32() % span);
        const BoulderCracks cracks = random.NextDouble() < fragmentation.slabShare
                                         ? BoulderCracks::Slabs
                                         : BoulderCracks::Radial;
        return FragmentBoulderShape(*CachedShape(shapeSeed), pieces, shapeSeed, cracks);
    });
}

std::shared_ptr<const BoulderShape> BoulderLayer::Shape(const Boulder& boulder) const {
    const std::uint64_t seed = ShapeSeed(boulder, options_.rendering.shapePrototypes);
    if (boulder.parent == 0) {
        return CachedShape(seed);
    }
    const auto fragments = CachedFragments(seed);
    return {fragments, &(*fragments)[boulder.piece].shape};
}

const ManualBoulder* BoulderLayer::Fixed(std::uint64_t id) const {
    const auto found = fixed_.find(id);
    return found != fixed_.end() ? &found->second : nullptr;
}

bool BoulderLayer::Settles(const Boulder& boulder) const {
    if (!options_.pose.settle) {
        return false;
    }
    const ManualBoulder* fixed = boulder.parent == 0 ? Fixed(boulder.id) : nullptr;
    return fixed == nullptr || !fixed->FixesOrientation();
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
        PlaceBoulderModel(InLocalFrame(boulder), *shape, 0.0, Settles(boulder));
    TriangleMesh mesh = PlaceMesh(shape->mesh, placement);
    CutFacets(mesh, InLocalFrame(boulder), 0.0);
    return mesh;
}

double BoulderLayer::Wear(double diameter, const BoulderAbundance& abundance, double density,
                          std::uint64_t id) const {
    RandomStream random = AttributeStream(options_.seed, id, BoulderDraw::Wear);
    const double thrown = random.NextDouble();
    if (abundance.ejecta > 0.0 && density > 0.0) {
        const double fromCrater = abundance.ejecta * ejectaSizes_->Incremental(diameter) / density;
        if (thrown < fromCrater) {
            return abundance.ejectaWear;
        }
    }
    return random.NextDouble();
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
        const TriangleMesh* shape = relief.Shape();
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
    AddRockCover(cover, grid, boulders, spacing, threads);
    AddModelCover(cover, grid, boulders, spacing, threads);
    return cover;
}

void BoulderLayer::AddRockCover(Raster<double>& cover, const RasterGrid& grid,
                                const std::vector<Boulder>& boulders, double spacing,
                                unsigned threads) const {
    std::vector<Relief> reliefs = PrepareRelief(boulders, spacing);
    reliefs.erase(std::remove_if(reliefs.begin(), reliefs.end(),
                                 [](const Relief& relief) { return !relief.rock; }),
                  reliefs.end());
    std::vector<std::vector<PixelWindow>> windows;
    for (const Relief& relief : reliefs) {
        windows.push_back(PixelWindowsIn(grid, relief.box, surface_));
    }
    ForEachFeatureCell(
        cover, threads, reliefs.size(),
        [&](std::size_t n) -> const std::vector<PixelWindow>& { return windows[n]; },
        [&](double& cell, const FeatureCell& at) {
            const Relief& relief = reliefs[at.feature];
            const TriangleMesh* shape = relief.Shape();
            if (RockRelief(at.center.x, at.center.y, *relief.boulder, shape, relief.width) > 0.0) {
                cell = std::fmax(cell, relief.weight);
            }
        });
}

void BoulderLayer::AddModelCover(Raster<double>& cover, const RasterGrid& grid,
                                 const std::vector<Boulder>& boulders, double spacing,
                                 unsigned threads) const {
    std::vector<const Boulder*> models;
    std::vector<std::vector<PixelWindow>> windows;
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
            const Vec2d offset = surface_.Offset({boulder.x, boulder.y}, at.center);
            if (std::hypot(offset.x, offset.y) < 0.5 * boulder.diameter) {
                cell = std::fmax(cell, ResolutionWeight(boulder.diameter, spacing, options_.fade));
            }
        });
}

double BoulderLayer::OwnMarginRadii() const {
    const double body = options_.rendering.relief == BoulderRelief::Shape ? kShapeReachRadii : 1.0;
    const double fillet =
        options_.fillet.heightShare > 0.0 ? 1.0 + 2.0 * options_.fillet.widthRatio : 1.0;
    return std::fmax(body, fillet);
}

double BoulderLayer::BreakMarginRadii() const {
    const BoulderFragmentation& fragmentation = options_.fragmentation;
    return fragmentation.Enabled() ? 2.0 * (fragmentation.gapRatio + fragmentation.spreadRatio)
                                   : 0.0;
}

CraterList BoulderLayer::ScatterBoulderCandidates(const Bounds2d& region, double smallest) const {
    const double margin = OwnMarginRadii() + BreakMarginRadii();
    if (!options_.generateRandom) {
        CraterList manual;
        for (const ManualBoulder& boulder : options_.manual) {
            const Vec2d center{boulder.x, boulder.y};
            if (surface_.Contains(surface_.Expand(region, 0.5 * margin * boulder.diameter),
                                  center)) {
                manual.Append(
                    {boulder.x, boulder.y, boulder.diameter, 0.0, ManualBoulderId(boulder), true});
            }
        }
        return manual;
    }
    const std::uint32_t seed = SubstreamSeed(options_.seed, kBoulderCandidateStream);
    const FreshAges ages;
    CraterGenerationOptions generation;
    generation.dMin = options_.sizes.minDiameter;
    generation.dMax = options_.sizes.maxDiameter;
    generation.marginRadii = margin;
    generation.surface = surface_;
    generation.logName = "boulders";
    if (smallest > 0.0) {
        generation.minResolvedDiameter = smallest;
    }
    for (const ManualBoulder& boulder : options_.manual) {
        generation.manual.Append({boulder.x, boulder.y, boulder.diameter, 0.0, boulder.id, true});
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
    std::vector<ThinnedCandidate> thinned;
    for (std::size_t n = 0; n < candidates.Size(); ++n) {
        if (std::optional<ThinnedCandidate> one = Thin(candidates.At(n), ground, ejecta)) {
            thinned.push_back(*one);
        }
    }
    std::vector<Boulder> boulders;
    boulders.reserve(thinned.size());
    if (ground == nullptr) {
        for (const ThinnedCandidate& one : thinned) {
            if (std::optional<Boulder> boulder = PlaceCandidate(one, spacing, nullptr)) {
                boulders.push_back(*boulder);
            }
        }
        return boulders;
    }
    const BoulderPose& pose = options_.pose;
    const std::size_t each = pose.followSlope || pose.maxSlope.has_value() ? 5 : 1;
    std::vector<Vec2d> points;
    points.reserve(thinned.size() * each);
    for (const ThinnedCandidate& one : thinned) {
        const Crater& c = one.candidate;
        points.push_back({c.x, c.y});
        if (each == 5) {
            for (const Vec2d& p : GradientPoints(surface_, {c.x, c.y}, 0.5 * c.diameter)) {
                points.push_back(p);
            }
        }
    }
    std::vector<float> heights(points.size());
    ground->HeightsAt(points, heights, options_.groundSpacing);
    for (std::size_t n = 0; n < thinned.size(); ++n) {
        const CandidateGround under(*ground, Span(points).subspan(n * each, each),
                                    Span(heights).subspan(n * each, each), options_.groundSpacing);
        if (std::optional<Boulder> boulder = PlaceCandidate(thinned[n], spacing, &under)) {
            boulders.push_back(*boulder);
        }
    }
    return boulders;
}

std::optional<BoulderLayer::ThinnedCandidate>
BoulderLayer::Thin(const Crater& candidate, const Ground* ground, const EjectaIndex& ejecta) const {
    if (!options_.generateRandom) {
        return ThinnedCandidate{candidate, BoulderAbundance{}};
    }
    ThinnedCandidate thinned{candidate, Abundance(candidate.x, candidate.y, ground, ejecta)};
    if (!candidate.manual) {
        thinned.density = Density(candidate.diameter, thinned.abundance);
        RandomStream random = AttributeStream(options_.seed, candidate.id, BoulderDraw::Keep);
        if (random.NextDouble() >= thinned.density / candidates_->Incremental(candidate.diameter)) {
            return std::nullopt;
        }
    }
    return thinned;
}

Boulder BoulderLayer::DrawPose(const ThinnedCandidate& thinned, double spacing,
                               const ManualBoulder* fixed) const {
    const Crater& candidate = thinned.candidate;
    RandomStream random = AttributeStream(options_.seed, candidate.id, BoulderDraw::Pose);
    const BoulderPose& pose = options_.pose;
    const double ratio =
        ValueRange{pose.minHeightRatio, pose.maxHeightRatio}.Lerp(random.NextDouble());
    double wear = Wear(candidate.diameter, thinned.abundance, thinned.density, candidate.id);
    if (fixed != nullptr && !std::isnan(fixed->wear)) {
        wear = fixed->wear;
    }
    double share = random.NextDouble();
    if (pose.burialByWear > 0.0) {
        share = (1.0 - pose.burialByWear) * share + pose.burialByWear * wear;
    }
    const double burial = ValueRange{pose.minBurial, pose.maxBurial}.Lerp(share);
    const double yaw = 2.0 * kPi * random.NextDouble();
    const double lean = pose.maxLean * random.NextDouble();
    Boulder boulder{
        candidate.x, candidate.y, candidate.diameter, ratio * candidate.diameter, burial,
        yaw,         lean,        candidate.id};
    boulder.wear = wear;
    boulder.model = options_.rendering.models.IsModel(boulder.diameter, spacing);
    if (fixed != nullptr) {
        const auto set = [](double& field, double value) {
            if (!std::isnan(value)) {
                field = value;
            }
        };
        set(boulder.height, fixed->height);
        set(boulder.burial, fixed->burial);
        set(boulder.yaw, fixed->yaw);
        set(boulder.lean, fixed->lean);
        boulder.shapeSeed = fixed->shapeSeed;
    }
    return boulder;
}

std::optional<Boulder> BoulderLayer::PlaceCandidate(const ThinnedCandidate& thinned, double spacing,
                                                    const Ground* ground) const {
    const ManualBoulder* fixed = thinned.candidate.manual ? Fixed(thinned.candidate.id) : nullptr;
    Boulder boulder = DrawPose(thinned, spacing, fixed);
    const BoulderPose& pose = options_.pose;
    if (pose.maxSlope && ground != nullptr && !thinned.candidate.manual &&
        !Roll(boulder, *ground)) {
        return std::nullopt;
    }
    ReadGround(boulder, ground);
    if (pose.sideShare > 0.0 || pose.endShare > 0.0 || pose.maxTilt > 0.0 || pose.followSlope ||
        (fixed != nullptr && fixed->FixesOrientation())) {
        Settle(boulder, ground, fixed);
    }
    if (options_.ventifacts.share > 0.0) {
        Abrade(boulder);
    }
    return boulder;
}

void BoulderLayer::ReadGround(Boulder& boulder, const Ground* ground) const {
    boulder.ground.reset();
    if (ground != nullptr) {
        const double height =
            static_cast<double>(ground->HeightAt(boulder.x, boulder.y, options_.groundSpacing));
        if (!std::isnan(height)) {
            boulder.ground = height;
        }
    }
}

void BoulderLayer::Abrade(Boulder& boulder) const {
    const BoulderVentifacts& ventifacts = options_.ventifacts;
    RandomStream random = AttributeStream(options_.seed, boulder.id, BoulderDraw::Abrade);
    if (random.NextDouble() >= ventifacts.share) {
        return;
    }
    const double reach = 0.5 * boulder.diameter * (1.0 - boulder.wear);
    const auto facet = [&](double wind, double pick) {
        return BoulderFacet{wind + kPi,
                            ValueRange{ventifacts.minAngle, ventifacts.maxAngle}.Lerp(pick), reach};
    };
    boulder.facets[0] = facet(ventifacts.windAzimuth, random.NextDouble());
    boulder.facetCount = 1;
    const double keel = random.NextDouble();
    const double second = random.NextDouble();
    if (ventifacts.secondWindAzimuth && keel < ventifacts.keelShare) {
        boulder.facets[1] = facet(*ventifacts.secondWindAzimuth, second);
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
    cut.mesh = PlaceMesh(std::move(cut.mesh), placement);
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
        const std::optional<Vec2d> g = Gradient(boulder, ground);
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

std::optional<Vec2d> BoulderLayer::Gradient(const Boulder& boulder, const Ground& ground) const {
    return GroundGradient(ground, surface_, {boulder.x, boulder.y}, 0.5 * boulder.diameter,
                          options_.groundSpacing);
}

void BoulderLayer::Settle(Boulder& boulder, const Ground* ground,
                          const ManualBoulder* fixed) const {
    const BoulderPose& pose = options_.pose;
    RandomStream random = AttributeStream(options_.seed, boulder.id, BoulderDraw::Settle);
    const double pick = random.NextDouble();
    boulder.rest = pick < pose.endShare                    ? BoulderRest::End
                   : pick < pose.endShare + pose.sideShare ? BoulderRest::Side
                                                           : BoulderRest::Face;
    Vec3d up{0.0, 0.0, 1.0};
    double cosine = 1.0;
    double azimuth = 0.0;
    if (pose.maxTilt > 0.0) {
        cosine = 1.0 - random.NextDouble() * (1.0 - std::cos(pose.maxTilt));
        const double sine = std::sqrt(std::fmax(0.0, 1.0 - cosine * cosine));
        azimuth = 2.0 * kPi * random.NextDouble();
        up = {sine * std::cos(azimuth), sine * std::sin(azimuth), cosine};
    }
    const bool fixesTilt =
        fixed != nullptr && !(std::isnan(fixed->tilt) && std::isnan(fixed->tiltAzimuth));
    if (fixed != nullptr && fixed->rest) {
        boulder.rest = *fixed->rest;
    }
    if (fixesTilt) {
        if (!(pose.maxTilt > 0.0)) {
            azimuth = 2.0 * kPi * random.NextDouble();
        }
        const double tilt = std::isnan(fixed->tilt) ? std::acos(cosine) : fixed->tilt;
        const double toward = std::isnan(fixed->tiltAzimuth) ? azimuth : fixed->tiltAzimuth;
        up = {std::sin(tilt) * std::cos(toward), std::sin(tilt) * std::sin(toward), std::cos(tilt)};
    }
    bool sloped = false;
    if (pose.followSlope && ground != nullptr) {
        if (const std::optional<Vec2d> g = Gradient(boulder, *ground)) {
            const double slope = std::atan(std::hypot(g->x, g->y));
            up = TiltRotation(slope, std::atan2(-g->y, -g->x)).Apply(up);
            sloped = true;
        }
    }
    boulder.tilt = std::acos(std::clamp(up.z, -1.0, 1.0));
    boulder.tiltAzimuth = std::atan2(up.y, up.x);
    if (fixesTilt && !sloped) {
        if (!std::isnan(fixed->tilt)) {
            boulder.tilt = fixed->tilt;
        }
        if (!std::isnan(fixed->tiltAzimuth)) {
            boulder.tiltAzimuth = fixed->tiltAzimuth;
        }
    }
}

std::vector<Boulder> BoulderLayer::Boulders(const Bounds2d& region, double spacing,
                                            const Ground* ground) const {
    const double smallest = spacing > 0.0 ? options_.fade.startSamples * spacing : 0.0;
    return PlaceBoulders(region, spacing, smallest, ground);
}

std::vector<Boulder> BoulderLayer::PlaceBoulders(const Bounds2d& region, double spacing,
                                                 double smallest, const Ground* ground) const {
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
    const CraterList candidates = ScatterBoulderCandidates(from, smallest);
    if (!options_.fragmentation.Enabled()) {
        std::vector<Boulder> boulders =
            DrawBoulders(candidates, spacing, ground, EjectaCraters(from));
        boulders.erase(std::remove_if(boulders.begin(), boulders.end(),
                                      [&](const Boulder& boulder) { return !rests(boulder); }),
                       boulders.end());
        return boulders;
    }
    const double reach = 0.5 * MaxFeatureDiameter() * (OwnMarginRadii() + BreakMarginRadii());
    const EjectaIndex ejecta = EjectaCraters(surface_.Expand(from, reach));
    std::vector<Boulder> boulders;
    std::vector<std::size_t> pieces;
    for (const Boulder& parent : DrawBoulders(candidates, spacing, ground, ejecta)) {
        for (const Boulder& boulder : Break(parent, spacing, smallest)) {
            const double own = 0.5 * boulder.diameter * OwnMarginRadii();
            if (surface_.Contains(surface_.Expand(region, own), {boulder.x, boulder.y})) {
                if (boulder.parent != 0) {
                    pieces.push_back(boulders.size());
                }
                boulders.push_back(boulder);
            }
        }
    }
    ReadGrounds(boulders, pieces, ground);
    return boulders;
}

void BoulderLayer::ReadGrounds(std::vector<Boulder>& boulders,
                               const std::vector<std::size_t>& indices,
                               const Ground* ground) const {
    std::vector<Vec2d> points;
    points.reserve(indices.size());
    for (const std::size_t n : indices) {
        boulders[n].ground.reset();
        points.push_back({boulders[n].x, boulders[n].y});
    }
    if (ground == nullptr || points.empty()) {
        return;
    }
    std::vector<float> heights(points.size());
    ground->HeightsAt(points, heights, options_.groundSpacing);
    for (std::size_t k = 0; k < indices.size(); ++k) {
        if (!std::isnan(heights[k])) {
            boulders[indices[k]].ground = heights[k];
        }
    }
}

BoulderLayer::BreakStage BoulderLayer::Stage(const Boulder& parent) const {
    const BoulderFragmentation& fragmentation = options_.fragmentation;
    const bool rubble = parent.wear >= fragmentation.rubbleWear;
    BreakStage stage;
    stage.opening = rubble || fragmentation.rubbleWear <= fragmentation.splitWear
                        ? 1.0
                        : (parent.wear - fragmentation.splitWear) /
                              (fragmentation.rubbleWear - fragmentation.splitWear);
    stage.scatter = !rubble                          ? 0.0
                    : fragmentation.rubbleWear < 1.0 ? (parent.wear - fragmentation.rubbleWear) /
                                                           (1.0 - fragmentation.rubbleWear)
                                                     : 1.0;
    return stage;
}

Boulder BoulderLayer::PlacePiece(const Boulder& parent, const BoulderFragment& fragment,
                                 std::size_t k, const PieceFrame& frame, double push,
                                 double twist) const {
    const BoulderFragmentation& fragmentation = options_.fragmentation;
    const BoulderPlacement& placement = frame.placement;
    const double scatter = frame.stage.scatter;
    const auto [low, high] = ZRange(fragment.shape.mesh.positions, [&](const Vec3d& p) {
        return placement.Apply(fragment.center + p * fragment.scale);
    });
    const Vec3d center = placement.Apply(fragment.center);
    const double out = std::hypot(center.x, center.y);
    const double angle = out > 0.0 ? std::atan2(center.y, center.x) : 2.0 * kPi * push;
    const double gap = (frame.stage.opening * fragmentation.gapRatio +
                        scatter * fragmentation.spreadRatio * (0.5 + 0.5 * push)) *
                       parent.diameter;
    Boulder piece = parent;
    piece.facetCount = 0;
    const IdWords id = SplitId(parent.id);
    const UVec3 key =
        Pcg3dHash({id.low, id.high, kBoulderPieceStream + static_cast<std::uint32_t>(k)});
    piece.id = JoinWords(key.x, key.y);
    piece.parent = parent.id;
    piece.piece = static_cast<std::uint32_t>(k);
    piece.diameter = parent.diameter * fragment.scale;
    piece.height = parent.height * fragment.shape.shortAxis * fragment.scale / frame.shortAxis;
    piece.yaw = parent.yaw + scatter * kPi * (2.0 * twist - 1.0);
    piece.lean = parent.lean * (1.0 - scatter);
    piece.tilt = parent.tilt * (1.0 - scatter);
    const double sunk = std::clamp(-low / (high - low), 0.0, 0.95);
    piece.burial = std::fmax(sunk, sunk + scatter * (options_.pose.maxBurial - sunk));
    const Vec2d at = surface_.Locate(
        {parent.x, parent.y}, {center.x + gap * std::cos(angle), center.y + gap * std::sin(angle)});
    piece.x = at.x;
    piece.y = at.y;
    return piece;
}

std::vector<Boulder> BoulderLayer::Break(const Boulder& parent, double spacing,
                                         double smallest) const {
    const BoulderFragmentation& fragmentation = options_.fragmentation;
    if (parent.diameter < fragmentation.minDiameter || parent.wear < fragmentation.splitWear ||
        Fixed(parent.id) != nullptr) {
        return {parent};
    }
    const auto shape = Shape(parent);
    const auto fragments = CachedFragments(ShapeSeed(parent, options_.rendering.shapePrototypes));
    Boulder origin = parent;
    origin.x = 0.0;
    origin.y = 0.0;
    const PieceFrame frame{PlaceBoulderModel(origin, *shape, 0.0, Settles(parent)), Stage(parent),
                           shape->shortAxis};
    const IdWords id = SplitId(parent.id);
    RandomStream random(options_.seed, kBoulderPieceStream, id.low, id.high);
    std::vector<Boulder> pieces;
    for (std::size_t k = 0; k < fragments->size(); ++k) {
        const double push = random.NextDouble();
        const double twist = random.NextDouble();
        Boulder piece = PlacePiece(parent, (*fragments)[k], k, frame, push, twist);
        if (piece.diameter < smallest) {
            continue;
        }
        piece.model = options_.rendering.models.IsModel(piece.diameter, spacing);
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
        const TriangleMesh* shape = relief.Shape();
        return AddRelief(height, base, p.x, p.y, *relief.boulder, shape, relief.weight, relief.rock,
                         relief.width);
    };
    if (Raster<float>* heights = samples.Grid()) {
        const Span<const float> values = std::as_const(*heights).Values();
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
                cell = draw(cell, base, at.center, reliefs[at.feature]);
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
    const double spacing = samples.ResolvedSpacing(context.spacing, surface_);
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

BoulderFeatures BoulderLayer::BouldersOver(const RasterGrid& grid, double spacing,
                                           const Ground* ground) const {
    BoulderFeatures placed{
        PlaceBoulders(grid.Bounds(), ListedSpacing(grid, spacing, surface_), 0.0, ground), {}};
    placed.models = Models(placed.boulders);
    return placed;
}

}  // namespace terraingen
