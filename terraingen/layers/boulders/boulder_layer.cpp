/// @file
/// @brief `BoulderLayer`: candidates, thinning, poses, relief and models.
#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/layers/craters/ages/fresh.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/resample.h"
#include "terraingen/util/cells.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

constexpr std::uint32_t kBoulderSeed = 0x524F434Bu;
constexpr std::uint32_t kBoulderAttributes = 0x41545452u;

/// A uniform in [0, 1) from two 32-bit words.
double Uniform(std::uint32_t high, std::uint32_t low) {
    const std::uint64_t bits = (static_cast<std::uint64_t>(high) << 32) | low;
    return static_cast<double>(bits >> 11) * 0x1.0p-53;
}

/// The box the relief of a boulder at (`x`, `y`) reaching `reach` metres is drawn over: on a
/// sphere a square in degrees around the box `Surface::Expand` gives.
Bounds2d ReliefBox(double x, double y, double reach, const Surface& surface) {
    if (!surface.IsSphere()) {
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
    if (surface_.IsSphere()) {
        cells_ = std::make_shared<CubeSphereCells>(surface_.Radius());
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
}

std::shared_ptr<const BoulderShape> BoulderLayer::Shape(const Boulder& boulder) const {
    const std::uint64_t seed = BoulderShapeSeed(boulder.id, options_.rendering.shapePrototypes);
    return shapes_->Get(seed,
                        [&] { return GenerateBoulderShape(seed, options_.rendering.shapes); });
}

Boulder BoulderLayer::InLocalFrame(const Boulder& boulder) const {
    Boulder local = boulder;
    if (surface_.IsSphere()) {
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

float BoulderLayer::AddRelief(float height, double x, double y, const Boulder& boulder,
                              const TriangleMesh* placed, double weight) const {
    if (placed == nullptr) {
        return AddBoulder(height, x, y, boulder, weight, surface_);
    }
    std::optional<double> top;
    if (surface_.IsSphere()) {
        const Vec2d offset = surface_.Offset({boulder.x, boulder.y}, {x, y});
        top = TopSurface(*placed, offset.x, offset.y);
    } else {
        top = TopSurface(*placed, x, y);
    }
    if (!top || !(*top > 0.0)) {
        return height;
    }
    return static_cast<float>(static_cast<double>(height) + weight * *top);
}

CraterList BoulderLayer::ScatterBoulderCandidates(const Bounds2d& region, double spacing) const {
    const std::uint32_t seed = options_.seed ^ kBoulderSeed;
    const FreshAges ages;
    CraterGenerationOptions generation{
        .dMin = options_.sizes.minDiameter, .dMax = options_.sizes.maxDiameter, .marginRadii = 1.0};
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
    const double keep =
        Density(candidate.diameter, abundance) / candidates_->Incremental(candidate.diameter);
    if (Uniform(draw.x, draw.y) >= keep) {
        return std::nullopt;
    }
    const U32x4 shape =
        Philox4x32({id.low, id.high, kBoulderAttributes, 1u}, {options_.seed, kBoulderSeed});
    const BoulderPose& pose = options_.pose;
    const double ratio =
        Between(pose.minHeightRatio, pose.maxHeightRatio, Uniform(shape.x, shape.y));
    const double burial = Between(pose.minBurial, pose.maxBurial, Uniform(draw.z, draw.w));
    Boulder boulder{candidate.x,
                    candidate.y,
                    candidate.diameter,
                    ratio * candidate.diameter,
                    burial,
                    2.0 * std::numbers::pi * Uniform(shape.z, shape.w),
                    pose.maxLean * Uniform(shape.w, shape.x),
                    candidate.id};
    boulder.model = options_.rendering.models.IsModel(boulder.diameter, spacing);
    if (ground != nullptr) {
        const double height =
            ground->HeightAt(boulder.x, boulder.y, options_.bedrock.exposureSpacing);
        if (!std::isnan(height)) {
            boulder.ground = height;
        }
    }
    return boulder;
}

std::vector<Boulder> BoulderLayer::Boulders(const Bounds2d& region, double spacing,
                                            const Ground* ground) const {
    if (exposure_) {
        RequireGround(ground, "Boulders that read the exposure field");
    }
    const CraterList candidates = ScatterBoulderCandidates(region, spacing);
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

float AddBoulder(float height, double x, double y, const Boulder& boulder, double weight,
                 const Surface& surface) {
    const Vec2d offset = surface.Offset({boulder.x, boulder.y}, {x, y});
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

std::vector<BoulderLayer::Relief> BoulderLayer::PrepareRelief(const std::vector<Boulder>& boulders,
                                                              double spacing) const {
    const bool shaped = options_.rendering.relief == BoulderRelief::Shape;
    std::vector<Relief> reliefs;
    for (const Boulder& boulder : boulders) {
        const double weight = ResolutionWeight(boulder.diameter, spacing, options_.fade);
        if (boulder.model || weight <= 0.0) {
            continue;
        }
        Relief relief{&boulder, weight, std::nullopt, {}};
        double reach = 0.5 * boulder.diameter;
        if (shaped) {
            relief.placed = PlacedOnLevel(boulder);
            const Boulder local = InLocalFrame(boulder);
            reach = ShapeReach(*relief.placed, local.x, local.y);
        }
        relief.box = ReliefBox(boulder.x, boulder.y, reach, surface_);
        reliefs.push_back(std::move(relief));
    }
    return reliefs;
}

void BoulderLayer::DrawRelief(SampleBlock& samples, const std::vector<Relief>& reliefs,
                              unsigned threads) const {
    const auto draw = [&](float height, const Vec2d& p, const Relief& relief) {
        const TriangleMesh* shape = relief.placed ? &*relief.placed : nullptr;
        return AddRelief(height, p.x, p.y, *relief.boulder, shape, relief.weight);
    };
    if (Raster<float>* heights = samples.Grid()) {
        std::vector<std::vector<PixelWindow>> windows;
        for (const Relief& relief : reliefs) {
            windows.push_back(WindowsAround(*heights, relief.box, surface_));
        }
        ForEachFeatureCell(
            *heights, threads, reliefs.size(),
            [&](std::size_t n) -> const std::vector<PixelWindow>& { return windows[n]; },
            [&](float& cell, const FeatureCell& at) {
                cell = draw(cell, at.centre, reliefs[at.feature]);
            });
        return;
    }
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        if (samples.IsNoData(i)) {
            continue;
        }
        const Vec2d p = samples.Position(i);
        for (const Relief& relief : reliefs) {
            if (surface_.Contains(relief.box, p)) {
                samples.Height(i) = draw(samples.Height(i), p, relief);
            }
        }
    }
}

void BoulderLayer::ApplySamples(SampleBlock& samples, const LayerContext& context,
                                TerrainFeatures& features) const {
    const Raster<float>* grid = samples.Grid();
    if (grid == nullptr && options_.rendering.relief == BoulderRelief::None) {
        return;
    }
    const double spacing = grid != nullptr
                               ? std::max(context.spacing, RasterGrid::Of(*grid).Spacing(surface_))
                               : context.spacing;
    std::vector<Boulder> boulders = Boulders(samples.Bounds(), spacing, context.ground);
    if (grid != nullptr) {
        for (Boulder& boulder : boulders) {
            if (boulder.model && !boulder.ground) {
                const float below =
                    SampleRaster(*grid, boulder.x, boulder.y, ResampleMethod::Bilinear);
                if (!std::isnan(below)) {
                    boulder.ground = static_cast<double>(below);
                }
            }
        }
    }
    if (options_.rendering.relief != BoulderRelief::None) {
        DrawRelief(samples, PrepareRelief(boulders, spacing), context.threads);
    }
    if (grid != nullptr) {
        RecordFeatures(std::move(boulders), features);
    }
}

void BoulderLayer::RecordFeatures(std::vector<Boulder> boulders, TerrainFeatures& features) const {
    for (const Boulder& boulder : boulders) {
        if (boulder.model && boulder.ground) {
            const auto shape = Shape(boulder);
            features.boulderModels.push_back(
                {boulder, BoulderShapeSeed(boulder.id, options_.rendering.shapePrototypes), shape,
                 PlaceBoulderModel(InLocalFrame(boulder), *shape, *boulder.ground)});
        }
    }
    features.boulders.insert(features.boulders.end(), boulders.begin(), boulders.end());
}

}  // namespace terraingen
