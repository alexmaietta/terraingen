#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/noise/hash.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/cells.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

constexpr std::uint32_t kBoulderSeed = 0x524F434Bu;
constexpr std::uint32_t kBoulderAttributes = 0x41545452u;
constexpr std::uint32_t kBoulderPieces = 0x50494543u;

/// A uniform in [0, 1) from two 32-bit words.
double Uniform(std::uint32_t high, std::uint32_t low) {
    const std::uint64_t bits = (static_cast<std::uint64_t>(high) << 32) | low;
    return static_cast<double>(bits >> 11) * 0x1.0p-53;
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
    if (options_.clusters.share > 0.0) {
        clusterMean_ = MeanClusterBump(options_.clusters);
    }
    ejectaSizes_ = minCraterDiameter_ > 0.0 ? EjectaSizes(options_) : nullptr;
    if (options_.cacheBoulders > 0) {
        cache_ = std::make_shared<CraterCellCache>(options_.cacheBoulders);
    }
    if (options_.bedrock.exposure) {
        exposure_.emplace(*options_.bedrock.exposure, surface_);
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
    const BoulderPlacement placement = PlaceBoulderModel(InLocalFrame(boulder), *shape, 0.0);
    TriangleMesh mesh = shape->mesh;
    for (Vec3d& p : mesh.positions) {
        p = placement.Apply(p);
    }
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
    double relief = 0.0;
    if (rock && placed == nullptr) {
        relief = DomeHeight(boulder, std::hypot(offset.x, offset.y), width);
    } else if (rock) {
        const std::optional<double> top =
            surface_.IsBody() ? TopSurface(*placed, offset.x, offset.y) : TopSurface(*placed, x, y);
        relief = top ? std::fmax(*top, 0.0) : 0.0;
    }
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
    if (spacing > 0.0) {
        generation.minResolvedDiameter = options_.fade.startSamples * spacing;
    }
    if (cache_) {
        generation.cellCraters = CachedCellCraters(*cache_, *cells_, *candidates_, ages, seed);
    }
    return GenerateCraters(*cells_, region, *candidates_, ages, seed, generation);
}

std::optional<Boulder> BoulderLayer::DrawBoulder(const Crater& candidate, double spacing,
                                                 const Ground* ground,
                                                 const CraterList& ejecta) const {
    const IdWords id = SplitId(candidate.id);
    const U32x4 draw =
        Philox4x32({id.low, id.high, kBoulderAttributes, 0u}, {options_.seed, kBoulderSeed});
    const BoulderAbundance abundance = Abundance(candidate.x, candidate.y, ground, ejecta);
    const double density = Density(candidate.diameter, abundance);
    const double keep = density / candidates_->Incremental(candidate.diameter);
    if (Uniform(draw.x, draw.y) >= keep) {
        return std::nullopt;
    }
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
    return boulder;
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
        const double step = 0.5 * boulder.diameter;
        const auto at = [&](double east, double north) {
            const Vec2d p = surface_.Locate({boulder.x, boulder.y}, {east, north});
            return static_cast<double>(
                ground->HeightAt(p.x, p.y, options_.bedrock.exposureSpacing));
        };
        const double gx = (at(step, 0.0) - at(-step, 0.0)) / (2.0 * step);
        const double gy = (at(0.0, step) - at(0.0, -step)) / (2.0 * step);
        if (std::isfinite(gx) && std::isfinite(gy)) {
            const double slope = std::atan(std::hypot(gx, gy));
            up = TiltRotation(slope, std::atan2(-gy, -gx)).Apply(up);
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
    const CraterList candidates = ScatterBoulderCandidates(region, spacing);
    if (!options_.fragmentation.Enabled()) {
        const CraterList ejecta = EjectaCraters(region);
        std::vector<Boulder> boulders;
        for (std::size_t n = 0; n < candidates.Size(); ++n) {
            if (const std::optional<Boulder> boulder =
                    DrawBoulder(candidates.At(n), spacing, ground, ejecta)) {
                boulders.push_back(*boulder);
            }
        }
        return boulders;
    }
    const double reach = 0.5 * options_.sizes.maxDiameter * (OwnMarginRadii() + BreakMarginRadii());
    const CraterList ejecta = EjectaCraters(surface_.Expand(region, reach));
    std::vector<Boulder> boulders;
    for (std::size_t n = 0; n < candidates.Size(); ++n) {
        const std::optional<Boulder> parent =
            DrawBoulder(candidates.At(n), spacing, ground, ejecta);
        if (!parent) {
            continue;
        }
        for (const Boulder& boulder : Break(*parent, spacing, ground)) {
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
    const BoulderPlacement placement = PlaceBoulderModel(origin, *shape, 0.0);
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
        return static_cast<float>(static_cast<double>(height) +
                                  weight * boulder.ExposedHeight() * std::sqrt(1.0 - r2));
    }
    const double dome = DomeHeight(boulder, std::hypot(offset.x, offset.y), width);
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
            windows.push_back(WindowsAround(*heights, relief.box, surface_));
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
    const double spacing = grid != nullptr
                               ? std::max(context.spacing, RasterGrid::Of(*grid).Spacing(surface_))
                               : context.spacing;
    const double rim = options_.rendering.relief == BoulderRelief::Dome ? 0.5 * spacing : 0.0;
    const std::vector<Boulder> boulders =
        Boulders(surface_.Expand(samples.Bounds(), rim), spacing, context.ground);
    DrawRelief(samples, PrepareRelief(boulders, spacing), context.threads);
}

std::vector<BoulderModel> BoulderLayer::Models(const std::vector<Boulder>& boulders) const {
    std::vector<BoulderModel> models;
    for (const Boulder& boulder : boulders) {
        if (boulder.model && boulder.ground) {
            const auto shape = Shape(boulder);
            models.push_back({boulder,
                              BoulderShapeSeed(boulder.parent != 0 ? boulder.parent : boulder.id,
                                               options_.rendering.shapePrototypes),
                              shape,
                              PlaceBoulderModel(InLocalFrame(boulder), *shape, *boulder.ground)});
        }
    }
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
