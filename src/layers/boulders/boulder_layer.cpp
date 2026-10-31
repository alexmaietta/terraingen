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
        : m_ground(ground), m_points(points), m_heights(heights), m_spacing(spacing) {}

  protected:
    void FillSamples(SampleBlock& samples, double spacing, unsigned /*threads*/) const override {
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const Vec2d p = samples.Position(i);
            const std::optional<float> held = Held(p, spacing);
            samples.Height(i) = held ? *held : m_ground.HeightAt(p.x, p.y, spacing);
        }
    }

  private:
    /// The height read up front at `p` and `spacing`, if any.
    std::optional<float> Held(const Vec2d& p, double spacing) const {
        if (spacing == m_spacing) {
            for (std::size_t n = 0; n < m_points.size(); ++n) {
                if (BitCast<std::uint64_t>(m_points[n].x) == BitCast<std::uint64_t>(p.x) &&
                    BitCast<std::uint64_t>(m_points[n].y) == BitCast<std::uint64_t>(p.y)) {
                    return m_heights[n];
                }
            }
        }
        return std::nullopt;
    }

    const Ground& m_ground;
    Span<const Vec2d> m_points;
    Span<const float> m_heights;
    double m_spacing;
};

/// How far in radii a boulder shape can reach from its center: half the diagonal of its unit box
/// over half its longest axis, for a shape tilted or resting on a side.
constexpr double kShapeReachRadii = 1.7320508075688772;

}  // namespace

RandomStream BoulderLayer::AttributeStream(std::uint64_t id, BoulderDraw draw) const {
    const IdWords words = SplitId(id);
    return RandomStream(m_options.seed, kBoulderAttributeStream, words.low, words.high,
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
    : m_options(std::move(options)), m_craters(std::move(craters)),
      m_surface(std::move(base.surface)), m_source(std::move(base.source)),
      m_completeness(m_options.completeness) {
    for (const auto& layer : m_craters) {
        if (!layer) {
            throw std::invalid_argument("A boulder layer's crater layers must not be null");
        }
        if (!(layer->LayerBase()->surface == m_surface)) {
            throw std::invalid_argument(
                "A boulder layer and its crater layer must share a surface");
        }
    }
    ValidateBoulderLayerOptions(m_options);
    if (auto limit = LayerResolvedLimit(m_source, m_options.sourceResolvesPixels, m_surface)) {
        m_completeness = m_completeness
                             ? std::make_shared<const MinimumField>(m_completeness, limit)
                             : std::move(limit);
    }
    for (const ManualBoulder& boulder : m_options.manual) {
        if (boulder.FixesAny()) {
            m_fixed.emplace(ManualBoulderId(boulder), boulder);
        }
        m_largestManual = std::fmax(m_largestManual, boulder.diameter);
    }
    m_cells = MakeCells(m_surface);
    SetUpShapeCaches();
    if (!m_options.generateRandom) {
        return;
    }
    m_minCraterDiameter = m_options.ejecta.ThrowingDiameter().value_or(0.0);
    if (m_minCraterDiameter > 0.0 && m_craters.empty()) {
        throw std::invalid_argument(
            "Boulder ejecta need the crater layer whose craters throw them");
    }
    SetUpAbundance();
    if (m_options.cacheBoulders > 0) {
        m_cache = std::make_shared<CraterCellCache>(m_options.cacheBoulders);
    }
    m_exposure = LayerExposure(m_options.bedrock.exposure, m_options.groundSpacing,
                               "Boulders that read the exposure field");
    if (m_exposure && m_options.densityMap) {
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
    BoulderLayerOptions options = m_options;
    options.seed = ReseedWord(seed, 0);
    options.rendering.shapes.seed = ReseedWord(seed, 1);
    return std::make_shared<BoulderLayer>(std::move(options), m_craters, *LayerBase());
}

LayerPtr BoulderLayer::Rebound(const LayerReplacements& replaced) const {
    std::vector<std::shared_ptr<const CraterLayer>> craters;
    for (const std::shared_ptr<const CraterLayer>& layer : m_craters) {
        craters.push_back(Replacement(layer, replaced));
    }
    if (craters == m_craters) {
        return nullptr;
    }
    return std::make_shared<BoulderLayer>(m_options, std::move(craters), *LayerBase());
}

LayerPtr BoulderLayer::Rebased(const TerrainBase& base) const {
    return std::make_shared<BoulderLayer>(m_options, m_craters, base);
}

double BoulderLayer::MaxFeatureDiameter() const {
    return std::fmax(m_options.generateRandom ? m_options.sizes.maxDiameter : 0.0, m_largestManual);
}

void BoulderLayer::SetUpAbundance() {
    m_background.emplace(m_options);
    m_clusterSeed = SubstreamSeed(m_options.seed, kBoulderClusterStream);
    m_patchSeed = SubstreamSeed(m_options.seed, kBoulderPatchStream);
    m_candidates = BoulderCandidates(m_options, m_background->max);
    if (m_options.slopeMap) {
        m_referenceCount =
            m_options.sizes.distribution->Cumulative(m_options.slopeMap->referenceDiameter);
    }
    if (m_options.clusters.share > 0.0) {
        m_clusterMean = m_options.clusters.model == BoulderClusterModel::Thomas
                            ? MeanThomasField(m_options.clusters, m_surface.IsBody())
                            : MeanClusterBump(m_options.clusters);
    }
    m_ejectaSizes = m_minCraterDiameter > 0.0 ? EjectaSizes(m_options) : nullptr;
}

void BoulderLayer::SetUpBedrockMeans() {
    constexpr std::size_t kMeanCells = 1u << 16;
    const double side = m_options.bedrock.meanSize / m_surface.UnitLength();
    m_bedrockCellSize = {side, side};
    if (const auto* raster = dynamic_cast<const RasterField*>(m_options.densityMap->field.get())) {
        const GeoTransform& transform = raster->Values().Transform();
        m_bedrockOrigin = {transform.originX, transform.originY};
        m_bedrockCellSize = {transform.pixelWidth, transform.pixelHeight};
    }
    m_bedrockMeans = std::make_shared<LruCache<BedrockCell, double, BedrockCellHash>>(
        kMeanCells, [](const double&) { return std::size_t{1}; });
}

void BoulderLayer::SetUpShapeCaches() {
    const std::uint32_t prototypes = m_options.rendering.shapePrototypes;
    m_shapes = std::make_shared<LruCache<std::uint64_t, BoulderShape>>(
        prototypes > 0 ? prototypes : 4096, [](const BoulderShape&) { return std::size_t{1}; });
    m_fragments = std::make_shared<LruCache<std::uint64_t, std::vector<BoulderFragment>>>(
        prototypes > 0 ? prototypes : 4096,
        [](const std::vector<BoulderFragment>&) { return std::size_t{1}; });
}

std::shared_ptr<const BoulderShape> BoulderLayer::CachedShape(std::uint64_t shapeSeed) const {
    return m_shapes->Get(
        shapeSeed, [&] { return GenerateBoulderShape(shapeSeed, m_options.rendering.shapes); });
}

std::shared_ptr<const std::vector<BoulderFragment>>
BoulderLayer::CachedFragments(std::uint64_t shapeSeed) const {
    return m_fragments->Get(shapeSeed, [&] {
        const BoulderFragmentation& fragmentation = *m_options.fragmentation;
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
    const std::uint64_t seed = ShapeSeed(boulder, m_options.rendering.shapePrototypes);
    if (boulder.parent == 0) {
        return CachedShape(seed);
    }
    const auto fragments = CachedFragments(seed);
    return {fragments, &(*fragments)[boulder.piece].shape};
}

double BoulderLayer::OwnMarginRadii() const {
    const double body = m_options.rendering.relief == BoulderRelief::Shape ? kShapeReachRadii : 1.0;
    const double fillet =
        m_options.fillet.heightShare > 0.0 ? 1.0 + 2.0 * m_options.fillet.widthRatio : 1.0;
    return std::fmax(body, fillet);
}

double BoulderLayer::BreakMarginRadii() const {
    const std::optional<BoulderFragmentation>& fragmentation = m_options.fragmentation;
    return fragmentation ? 2.0 * (fragmentation->gapRatio + fragmentation->spreadRatio) : 0.0;
}

CraterList BoulderLayer::ScatterBoulderCandidates(const Bounds2d& region, double smallest) const {
    const double margin = OwnMarginRadii() + BreakMarginRadii();
    if (!m_options.generateRandom) {
        CraterList manual;
        for (const ManualBoulder& boulder : m_options.manual) {
            const Vec2d center{boulder.x, boulder.y};
            if (m_surface.Contains(m_surface.Expand(region, 0.5 * margin * boulder.diameter),
                                   center)) {
                manual.Append(
                    {boulder.x, boulder.y, boulder.diameter, 0.0, ManualBoulderId(boulder), true});
            }
        }
        return manual;
    }
    const std::uint32_t seed = SubstreamSeed(m_options.seed, kBoulderCandidateStream);
    const FreshAges ages;
    CraterGenerationOptions generation;
    generation.minDiameter = m_options.sizes.minDiameter;
    generation.maxDiameter = m_options.sizes.maxDiameter;
    generation.marginRadii = margin;
    generation.surface = m_surface;
    generation.logName = "boulders";
    if (smallest > 0.0) {
        generation.minResolvedDiameter = smallest;
    }
    for (const ManualBoulder& boulder : m_options.manual) {
        generation.manual.Append(
            {boulder.x, boulder.y, boulder.diameter, 0.0, boulder.id.value_or(0u), true});
    }
    generation.completeness = m_completeness;
    return GenerateCachedCraters(m_cache.get(), *m_cells, region, *m_candidates, ages, seed,
                                 std::move(generation));
}

std::vector<Boulder> BoulderLayer::DrawBoulders(const CraterList& candidates, double spacing,
                                                const Ground* ground, const EjectaIndex& ejecta,
                                                unsigned threads) const {
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
    const BoulderPose& pose = m_options.pose;
    const std::size_t each = pose.followSlope || pose.maxSlopeDegrees.has_value() ? 5 : 1;
    std::vector<Vec2d> points;
    points.reserve(thinned.size() * each);
    for (const ThinnedCandidate& one : thinned) {
        const Crater& c = one.candidate;
        points.push_back({c.x, c.y});
        if (each == 5) {
            for (const Vec2d& p : GradientPoints(m_surface, {c.x, c.y}, 0.5 * c.diameter)) {
                points.push_back(p);
            }
        }
    }
    std::vector<float> heights(points.size());
    ground->HeightsAt(points, heights, m_options.groundSpacing, threads);
    for (std::size_t n = 0; n < thinned.size(); ++n) {
        const CandidateGround under(*ground, Span(points).subspan(n * each, each),
                                    Span(heights).subspan(n * each, each), m_options.groundSpacing);
        if (std::optional<Boulder> boulder = PlaceCandidate(thinned[n], spacing, &under)) {
            boulders.push_back(*boulder);
        }
    }
    return boulders;
}

std::optional<BoulderLayer::ThinnedCandidate>
BoulderLayer::Thin(const Crater& candidate, const Ground* ground, const EjectaIndex& ejecta) const {
    if (!m_options.generateRandom) {
        return ThinnedCandidate{candidate, BoulderAbundance{}};
    }
    ThinnedCandidate thinned{candidate, Abundance(candidate.x, candidate.y, ground, ejecta)};
    if (!candidate.manual) {
        thinned.density = Density(candidate.diameter, thinned.abundance);
        RandomStream random = AttributeStream(candidate.id, BoulderDraw::Keep);
        if (random.NextDouble() >=
            thinned.density / m_candidates->Incremental(candidate.diameter)) {
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
    return ToBoulderList(BoulderRecords(region, spacing, ground, 0));
}

std::vector<Boulder> BoulderLayer::BoulderRecords(const Bounds2d& region, double spacing,
                                                  const Ground* ground, unsigned threads) const {
    const double smallest = spacing > 0.0 ? m_options.fade.startSamples * spacing : 0.0;
    return PlaceBoulders(region, spacing, smallest, ground, threads);
}

std::vector<Boulder> BoulderLayer::PlaceBoulders(const Bounds2d& region, double spacing,
                                                 double smallest, const Ground* ground,
                                                 unsigned threads) const {
    m_exposure.Require(ground);
    const double roll = ground != nullptr ? m_options.pose.rollDistance.value_or(0.0) : 0.0;
    const Bounds2d from = roll > 0.0 ? m_surface.Expand(region, roll) : region;
    const auto rests = [&](const Boulder& boulder) {
        const double own = 0.5 * boulder.diameter * OwnMarginRadii();
        return roll == 0.0 ||
               m_surface.Contains(m_surface.Expand(region, own), {boulder.x, boulder.y});
    };
    const CraterList candidates = ScatterBoulderCandidates(from, smallest);
    if (!m_options.fragmentation) {
        std::vector<Boulder> boulders =
            DrawBoulders(candidates, spacing, ground, EjectaCraters(from), threads);
        boulders.erase(std::remove_if(boulders.begin(), boulders.end(),
                                      [&](const Boulder& boulder) { return !rests(boulder); }),
                       boulders.end());
        return boulders;
    }
    const double reach = 0.5 * MaxFeatureDiameter() * (OwnMarginRadii() + BreakMarginRadii());
    const EjectaIndex ejecta = EjectaCraters(m_surface.Expand(from, reach));
    std::vector<Boulder> boulders;
    std::vector<std::size_t> pieces;
    for (const Boulder& parent : DrawBoulders(candidates, spacing, ground, ejecta, threads)) {
        for (const Boulder& boulder : Break(parent, spacing, smallest)) {
            const double own = 0.5 * boulder.diameter * OwnMarginRadii();
            if (m_surface.Contains(m_surface.Expand(region, own), {boulder.x, boulder.y})) {
                if (boulder.parent != 0) {
                    pieces.push_back(boulders.size());
                }
                boulders.push_back(boulder);
            }
        }
    }
    ReadGrounds(boulders, pieces, ground, threads);
    return boulders;
}

BoulderFeatures BoulderLayer::BouldersOver(const RasterGrid& grid, double spacing,
                                           const Ground* ground) const {
    const std::vector<Boulder> boulders =
        PlaceBoulders(grid.Bounds(), ListedSpacing(grid, spacing, m_surface), 0.0, ground, 0);
    return {ToBoulderList(boulders), Models(boulders)};
}

}  // namespace terraingen
