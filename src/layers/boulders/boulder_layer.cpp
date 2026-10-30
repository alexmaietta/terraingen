// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/boulders/boulder_layer.h"

#include "layers/boulders/boulder_clusters.h"
#include "layers/craters/generation/cell_cache.h"

#include "terraingen/geometry/cells.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/source_resolved.h"
#include "terraingen/noise/hash.h"
#include "terraingen/random/stream.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/parallel.h"
#include "terraingen/util/span.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
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

  protected:
    void FillSamples(SampleBlock& samples, double spacing) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const Vec2d p = samples.Position(i);
            const std::optional<float> held = Held(p, spacing);
            samples.Height(i) = held ? *held : ground_.HeightAt(p.x, p.y, spacing);
        }
    }

  private:
    /// The height read up front at `p` and `spacing`, if any.
    std::optional<float> Held(const Vec2d& p, double spacing) const {
        if (spacing == spacing_) {
            for (std::size_t n = 0; n < points_.size(); ++n) {
                if (BitCast<std::uint64_t>(points_[n].x) == BitCast<std::uint64_t>(p.x) &&
                    BitCast<std::uint64_t>(points_[n].y) == BitCast<std::uint64_t>(p.y)) {
                    return heights_[n];
                }
            }
        }
        return std::nullopt;
    }

    const Ground& ground_;
    Span<const Vec2d> points_;
    Span<const float> heights_;
    double spacing_;
};

/// How far in radii a boulder shape can reach from its center: half the diagonal of its unit box
/// over half its longest axis, for a shape tilted or resting on a side.
constexpr double kShapeReachRadii = 1.7320508075688772;

}  // namespace

RandomStream BoulderLayer::AttributeStream(std::uint64_t id, BoulderDraw draw) const {
    const IdWords words = SplitId(id);
    return RandomStream(options_.seed, kBoulderAttributeStream, words.low, words.high,
                        static_cast<std::uint32_t>(draw));
}

BoulderLayer::BoulderLayer(BoulderLayerOptions options, std::shared_ptr<const CraterLayer> craters,
                           TerrainBase base)
    : BoulderLayer(std::move(options),
                   craters ? std::vector<std::shared_ptr<const CraterLayer>>{std::move(craters)}
                           : std::vector<std::shared_ptr<const CraterLayer>>{},
                   std::move(base)) {}

BoulderLayer::BoulderLayer(BoulderLayerOptions options,
                           std::vector<std::shared_ptr<const CraterLayer>> craters,
                           TerrainBase base)
    : options_(std::move(options)), craters_(std::move(craters)), surface_(base.surface) {
    for (const auto& layer : craters_) {
        if (!layer) {
            throw std::invalid_argument("A boulder layer's crater layers must not be null");
        }
        if (!(layer->LayerSurface() == surface_)) {
            throw std::invalid_argument(
                "A boulder layer and its crater layer must share a surface");
        }
    }
    ValidateBoulderLayerOptions(options_);
    if (auto limit = LayerResolvedLimit(base.source, options_.sourceResolvesPixels, surface_)) {
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
    minCraterDiameter_ = options_.ejecta.ThrowingDiameter().value_or(0.0);
    if (minCraterDiameter_ > 0.0 && craters_.empty()) {
        throw std::invalid_argument(
            "Boulder ejecta need the crater layer whose craters throw them");
    }
    SetUpAbundance();
    if (options_.cacheBoulders > 0) {
        cache_ = std::make_shared<CraterCellCache>(options_.cacheBoulders);
    }
    exposure_ = LayerExposure(options_.bedrock.exposure, options_.groundSpacing,
                              "Boulders that read the exposure field");
    if (exposure_ && options_.densityMap) {
        SetUpBedrockMeans();
    }
}

std::shared_ptr<BoulderLayer> BoulderLayer::Manual(std::vector<ManualBoulder> boulders,
                                                   BoulderLayerOptions options, TerrainBase base) {
    if (!options.manual.empty()) {
        throw std::invalid_argument(
            "BoulderLayer::Manual takes its boulders as an argument; leave options.manual empty");
    }
    options.generateRandom = false;
    options.manual = std::move(boulders);
    return std::make_shared<BoulderLayer>(std::move(options), std::shared_ptr<const CraterLayer>{},
                                          std::move(base));
}

LayerPtr BoulderLayer::Reseeded(std::uint64_t seed) const {
    BoulderLayerOptions options = options_;
    options.seed = ReseedWord(seed, 0);
    options.rendering.shapes.seed = ReseedWord(seed, 1);
    return std::make_shared<BoulderLayer>(std::move(options), craters_, TerrainBase{surface_});
}

LayerPtr BoulderLayer::Rebound(const LayerReplacements& replaced) const {
    std::vector<std::shared_ptr<const CraterLayer>> craters;
    for (const std::shared_ptr<const CraterLayer>& layer : craters_) {
        craters.push_back(Replacement(layer, replaced));
    }
    if (craters == craters_) {
        return nullptr;
    }
    return std::make_shared<BoulderLayer>(options_, std::move(craters), TerrainBase{surface_});
}

double BoulderLayer::MaxFeatureDiameter() const {
    return std::fmax(options_.generateRandom ? options_.sizes.maxDiameter : 0.0, largestManual_);
}

void BoulderLayer::SetUpAbundance() {
    background_.emplace(options_);
    clusterSeed_ = SubstreamSeed(options_.seed, kBoulderClusterStream);
    patchSeed_ = SubstreamSeed(options_.seed, kBoulderPatchStream);
    candidates_ = BoulderCandidates(options_, background_->max);
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
        const BoulderFragmentation& fragmentation = *options_.fragmentation;
        const auto span =
            static_cast<std::uint32_t>(fragmentation.maxPieces - fragmentation.minPieces + 1);
        RandomStream random = AttributeStream(shapeSeed, BoulderDraw::Fragments);
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

double BoulderLayer::OwnMarginRadii() const {
    const double body = options_.rendering.relief == BoulderRelief::Shape ? kShapeReachRadii : 1.0;
    const double fillet =
        options_.fillet.heightShare > 0.0 ? 1.0 + 2.0 * options_.fillet.widthRatio : 1.0;
    return std::fmax(body, fillet);
}

double BoulderLayer::BreakMarginRadii() const {
    const std::optional<BoulderFragmentation>& fragmentation = options_.fragmentation;
    return fragmentation ? 2.0 * (fragmentation->gapRatio + fragmentation->spreadRatio) : 0.0;
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
        generation.manual.Append(
            {boulder.x, boulder.y, boulder.diameter, 0.0, boulder.id.value_or(0u), true});
    }
    generation.completeness = options_.completeness;
    return GenerateCachedCraters(cache_.get(), *cells_, region, *candidates_, ages, seed,
                                 std::move(generation));
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
    const std::size_t each = pose.followSlope || pose.maxSlopeDegrees.has_value() ? 5 : 1;
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
        RandomStream random = AttributeStream(candidate.id, BoulderDraw::Keep);
        if (random.NextDouble() >= thinned.density / candidates_->Incremental(candidate.diameter)) {
            return std::nullopt;
        }
    }
    return thinned;
}

namespace {

BoulderList ToBoulderList(const std::vector<Boulder>& boulders) {
    BoulderList list;
    list.Reserve(boulders.size());
    for (const Boulder& boulder : boulders) {
        list.Append(boulder);
    }
    return list;
}

}  // namespace

BoulderList BoulderLayer::Boulders(const Bounds2d& region, double spacing,
                                   const Ground* ground) const {
    return ToBoulderList(BoulderRecords(region, spacing, ground));
}

std::vector<Boulder> BoulderLayer::BoulderRecords(const Bounds2d& region, double spacing,
                                                  const Ground* ground) const {
    const double smallest = spacing > 0.0 ? options_.fade.startSamples * spacing : 0.0;
    return PlaceBoulders(region, spacing, smallest, ground);
}

std::vector<Boulder> BoulderLayer::PlaceBoulders(const Bounds2d& region, double spacing,
                                                 double smallest, const Ground* ground) const {
    exposure_.Require(ground);
    const double roll = ground != nullptr ? options_.pose.rollDistance.value_or(0.0) : 0.0;
    const Bounds2d from = roll > 0.0 ? surface_.Expand(region, roll) : region;
    const auto rests = [&](const Boulder& boulder) {
        const double own = 0.5 * boulder.diameter * OwnMarginRadii();
        return roll == 0.0 ||
               surface_.Contains(surface_.Expand(region, own), {boulder.x, boulder.y});
    };
    const CraterList candidates = ScatterBoulderCandidates(from, smallest);
    if (!options_.fragmentation) {
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

BoulderFeatures BoulderLayer::BouldersOver(const RasterGrid& grid, double spacing,
                                           const Ground* ground) const {
    const std::vector<Boulder> boulders =
        PlaceBoulders(grid.Bounds(), ListedSpacing(grid, spacing, surface_), 0.0, ground);
    return {ToBoulderList(boulders), Models(boulders)};
}

}  // namespace terraingen
