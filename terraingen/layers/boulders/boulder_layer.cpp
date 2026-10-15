#include "terraingen/layers/boulders/boulder_layer.h"

#include "terraingen/distribution/summed.h"
#include "terraingen/layers/craters/ages/fresh.h"
#include "terraingen/noise/kernels/perlin.h"
#include "terraingen/noise/kernels/philox.h"
#include "terraingen/raster/grid.h"
#include "terraingen/raster/resample.h"
#include "terraingen/util/cells.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

constexpr std::uint32_t kBoulderSeed = 0x524F434Bu;
constexpr std::uint32_t kBoulderAttributes = 0x41545452u;
constexpr std::uint32_t kBoulderPatches = 0x50415443u;

/// Golombek-Rapp boulders per m^2 per metre of diameter at `diameter` for coverage `k`.
double GolombekRappDensity(double diameter, double k, double qa, double qb) {
    const double q = qa + qb / k;
    return k * q * std::exp(-q * diameter) / (std::numbers::pi * diameter * diameter / 4.0);
}

/// A uniform in [0, 1) from two 32-bit words.
double Uniform(std::uint32_t high, std::uint32_t low) {
    const std::uint64_t bits = (static_cast<std::uint64_t>(high) << 32) | low;
    return static_cast<double>(bits >> 11) * 0x1.0p-53;
}

double Smoothstep(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

/// The rows and columns of `raster` within `reach` of (`x`, `y`).
struct Window {
    std::size_t col0 = 0;
    std::size_t col1 = 0;
    std::size_t row0 = 0;
    std::size_t row1 = 0;
};

Window WindowAround(const Raster<float>& raster, double x, double y, double reach) {
    const GeoTransform& transform = raster.Transform();
    constexpr double kInf = std::numeric_limits<double>::infinity();
    double colMin = kInf;
    double colMax = -kInf;
    double rowMin = kInf;
    double rowMax = -kInf;
    for (const double dx : {-reach, reach}) {
        for (const double dy : {-reach, reach}) {
            const Vec2d pixel = transform.WorldToPixel(x + dx, y + dy);
            colMin = std::fmin(colMin, pixel.x);
            colMax = std::fmax(colMax, pixel.x);
            rowMin = std::fmin(rowMin, pixel.y);
            rowMax = std::fmax(rowMax, pixel.y);
        }
    }
    const auto clampTo = [](double value, std::size_t limit) {
        return static_cast<std::size_t>(std::clamp(value, 0.0, static_cast<double>(limit)));
    };
    return {clampTo(std::floor(colMin), raster.Width()), clampTo(std::ceil(colMax), raster.Width()),
            clampTo(std::floor(rowMin), raster.Height()),
            clampTo(std::ceil(rowMax), raster.Height())};
}

/// The windows of `raster` within `reach` metres of (`x`, `y`): one on a plane, and on a sphere
/// one for each turn the raster's longitudes cover.
std::vector<Window> WindowsAround(const Raster<float>& raster, double x, double y, double reach,
                                  const Surface& surface) {
    if (!surface.IsSphere()) {
        return {WindowAround(raster, x, y, reach)};
    }
    const Bounds2d box = surface.Expand({x, y, x, y}, reach);
    const double cx = 0.5 * (box.minX + box.maxX);
    const double cy = 0.5 * (box.minY + box.maxY);
    const double half = std::fmax(0.5 * (box.maxX - box.minX), 0.5 * (box.maxY - box.minY));
    std::vector<Window> windows;
    for (const double shift : {-360.0, 0.0, 360.0}) {
        const Window window = WindowAround(raster, cx + shift, cy, half);
        if (window.col0 < window.col1 && window.row0 < window.row1) {
            windows.push_back(window);
        }
    }
    return windows;
}

/// Whether `distribution` may be evaluated from `low` to `high` metres.
bool Covers(const SizeFrequencyDistribution& distribution, double low, double high) {
    return distribution.AllowsExtrapolation() ||
           (distribution.ValidRange().min <= low && distribution.ValidRange().max >= high);
}

/// The largest background abundance: the richest patches on bedrock.
double MaxBackground(const BoulderLayerOptions& options) {
    return options.patches.spread *
           (options.exposure ? std::fmax(1.0, options.bedrockAbundance) : 1.0);
}

/// The sizes of the crater blocks at full share.
std::shared_ptr<const SizeFrequencyDistribution> EjectaSizes(const BoulderLayerOptions& options) {
    const BoulderEjecta& ejecta = options.ejecta;
    if (ejecta.distribution) {
        return ejecta.distribution;
    }
    return std::make_shared<GolombekRappSfd>(ejecta.coverage, options.qa, options.qb);
}

void CheckSizes(const SizeFrequencyDistribution& distribution, const BoulderLayerOptions& options,
                const char* what) {
    if (distribution.Kind() != SfdKind::Density) {
        throw std::invalid_argument(std::string(what) + " must be a density, not a rate");
    }
    if (!Covers(distribution, options.minDiameter, options.maxDiameter)) {
        throw std::invalid_argument(std::string(what) +
                                    " must be valid from minDiameter to maxDiameter, or allow "
                                    "extrapolation");
    }
}

}  // namespace

double BoulderEjecta::MinCraterDiameter() const {
    const double excavated = regolithThickness ? *regolithThickness / excavationDepthRatio : 0.0;
    return std::fmax(minCraterDiameter, excavated);
}

double BoulderThrowDistance(double craterDiameter, double regolithThickness, double gravity) {
    for (const double value : {craterDiameter, regolithThickness, gravity}) {
        RequirePositive(value, "Crater diameter, regolith thickness and gravity");
    }
    return 138.5 / gravity * std::pow(craterDiameter / regolithThickness, 0.7086);
}

void ValidateBoulderLayerOptions(const BoulderLayerOptions& options) {
    if (!(IsPositive(options.minDiameter) && std::isfinite(options.maxDiameter) &&
          options.maxDiameter > options.minDiameter)) {
        throw std::invalid_argument("Boulder diameters must satisfy 0 < minDiameter < maxDiameter");
    }
    if (!(std::isfinite(options.patches.spread) && options.patches.spread >= 1.0 &&
          IsPositive(options.patches.size))) {
        throw std::invalid_argument("Boulder patches need spread >= 1 and a positive size");
    }
    RequirePositive(options.bedrockAbundance, "Boulder bedrockAbundance");
    if (options.distribution) {
        CheckSizes(*options.distribution, options, "The boulder distribution");
    } else {
        if (!(options.coverage > 0.0 && options.coverage < 1.0)) {
            throw std::invalid_argument("Boulder coverage must be between 0 and 1");
        }
        if (options.coverage * MaxBackground(options) >= 1.0) {
            throw std::invalid_argument(
                "Boulder coverage times the patch spread and bedrock abundance must stay below 1");
        }
    }
    const BoulderEjecta& ejecta = options.ejecta;
    if (!IsNonNegative(ejecta.minCraterDiameter)) {
        throw std::invalid_argument("Boulder ejecta minCraterDiameter must not be negative");
    }
    if (ejecta.regolithThickness && !IsPositive(*ejecta.regolithThickness)) {
        throw std::invalid_argument("Boulder ejecta regolithThickness must be positive");
    }
    if (!(ejecta.excavationDepthRatio > 0.0 && ejecta.excavationDepthRatio <= 1.0)) {
        throw std::invalid_argument("Boulder ejecta excavationDepthRatio must be in (0, 1]");
    }
    if (ejecta.MinCraterDiameter() > 0.0) {
        if (ejecta.distribution) {
            CheckSizes(*ejecta.distribution, options, "The boulder ejecta distribution");
        } else if (!(ejecta.coverage > 0.0 && ejecta.coverage < 1.0)) {
            throw std::invalid_argument("Boulder ejecta coverage must be between 0 and 1");
        }
        if (!(std::isfinite(ejecta.reach) && ejecta.reach > 1.0)) {
            throw std::invalid_argument("Boulder ejecta need a reach over 1 radius");
        }
        if (!(IsPositive(ejecta.decayRadii) && IsInUnitInterval(ejecta.floorShare))) {
            throw std::invalid_argument(
                "Boulder ejecta need a positive decayRadii and a floorShare in [0, 1]");
        }
        if (!(ejecta.minFreshness >= 0.0 && ejecta.minFreshness < 1.0 &&
              IsPositive(ejecta.lifetimeGyr) && IsNonNegative(ejecta.agingExponent))) {
            throw std::invalid_argument("Boulder ejecta need minFreshness in [0, 1), a positive "
                                        "lifetimeGyr and a non-negative agingExponent");
        }
        if (ejecta.gravity && !IsPositive(*ejecta.gravity)) {
            throw std::invalid_argument("Boulder ejecta gravity must be positive");
        }
    }
    if (options.exposure) {
        ValidateExposureParams(*options.exposure);
    }
    if (!IsPositive(options.exposureSpacing)) {
        throw std::invalid_argument("Boulder exposureSpacing must be positive");
    }
    if (!(options.minHeightRatio > 0.0 && options.maxHeightRatio >= options.minHeightRatio &&
          std::isfinite(options.maxHeightRatio))) {
        throw std::invalid_argument("Boulder height ratios must satisfy 0 < min <= max");
    }
    if (!(options.minBurial >= 0.0 && options.maxBurial >= options.minBurial &&
          options.maxBurial < 1.0)) {
        throw std::invalid_argument("Boulder burial must satisfy 0 <= min <= max < 1");
    }
    if (!IsNonNegative(options.maxLean)) {
        throw std::invalid_argument("Boulder maxLean must not be negative");
    }
    ValidateResolutionFade(options.fade);
    for (const auto& threshold : {options.models.minDiameter, options.models.minSamples}) {
        if (threshold) {
            RequirePositive(*threshold, "Boulder model thresholds");
        }
    }
    ValidateBoulderShapeOptions(options.shapes);
}

bool BoulderModelThreshold::IsModel(double diameter, double spacing) const {
    return (minDiameter && diameter >= *minDiameter) ||
           (minSamples && spacing > 0.0 && diameter >= *minSamples * spacing);
}

std::shared_ptr<const SizeFrequencyDistribution>
BoulderCandidates(const BoulderLayerOptions& options) {
    ValidateBoulderLayerOptions(options);
    const double richest = MaxBackground(options);
    std::vector<WeightedSfd> terms;
    if (options.distribution) {
        terms.push_back({richest, options.distribution});
    } else {
        terms.push_back({1.0, std::make_shared<GolombekRappSfd>(options.coverage * richest,
                                                                options.qa, options.qb)});
    }
    if (options.ejecta.MinCraterDiameter() > 0.0) {
        terms.push_back({1.0, EjectaSizes(options)});
    }
    return std::make_shared<SummedSfd>(std::move(terms));
}

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
    maxBackground_ = MaxBackground(options_);
    ejectaSizes_ = minCraterDiameter_ > 0.0 ? EjectaSizes(options_) : nullptr;
    if (options_.cacheBoulders > 0) {
        cache_ = std::make_shared<CraterCellCache>(options_.cacheBoulders);
    }
    if (options_.exposure) {
        exposure_.emplace(*options_.exposure, surface_);
    }
    shapes_ = std::make_shared<LruCache<std::uint64_t, BoulderShape>>(
        options_.shapePrototypes > 0 ? options_.shapePrototypes : 4096,
        [](const BoulderShape&) { return std::size_t{1}; });
}

std::shared_ptr<const BoulderShape> BoulderLayer::Shape(const Boulder& boulder) const {
    const std::uint64_t seed = BoulderShapeSeed(boulder.id, options_.shapePrototypes);
    return shapes_->Get(seed, [&] { return GenerateBoulderShape(seed, options_.shapes); });
}

TriangleMesh BoulderLayer::PlacedOnLevel(const Boulder& boulder) const {
    const auto shape = Shape(boulder);
    Boulder local = boulder;
    if (surface_.IsSphere()) {
        local.x = 0.0;
        local.y = 0.0;
    }
    const BoulderPlacement placement = PlaceBoulderModel(local, *shape, 0.0);
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

CraterList BoulderLayer::EjectaCraters(const Bounds2d& region) const {
    if (minCraterDiameter_ <= 0.0) {
        return {};
    }
    const double margin = options_.ejecta.reach * 0.5 * craters_->MaxFeatureDiameter();
    const double spacing = minCraterDiameter_ / craters_->Options().apply.fade.startSamples;
    const CraterList all = craters_->Craters(surface_.Expand(region, margin), spacing);
    CraterList large;
    for (std::size_t n = 0; n < all.Size(); ++n) {
        if (all.At(n).diameter >= minCraterDiameter_) {
            large.Append(all.At(n));
        }
    }
    return large;
}

BoulderAbundance BoulderLayer::Abundance(double x, double y, const Ground* ground,
                                         const CraterList& ejecta) const {
    BoulderAbundance abundance;
    const BoulderPatches& patches = options_.patches;
    if (patches.spread > 1.0) {
        const std::uint32_t seed = options_.seed ^ kBoulderPatches;
        double coarse = 0.0;
        double fine = 0.0;
        if (surface_.IsSphere()) {
            const Vec3d p = surface_.Position({x, y});
            const double s = 1.0 / patches.size;
            coarse = static_cast<double>(PerlinNoise3Precise(s * p.x, s * p.y, s * p.z, seed));
            fine = static_cast<double>(
                PerlinNoise3Precise(4.0 * s * p.x, 4.0 * s * p.y, 4.0 * s * p.z, seed + 1u));
        } else {
            const auto px = static_cast<float>(x / patches.size);
            const auto py = static_cast<float>(y / patches.size);
            coarse = static_cast<double>(PerlinNoise2(px, py, seed));
            fine = static_cast<double>(PerlinNoise2(4.0f * px, 4.0f * py, seed + 1u));
        }
        const double noise = std::clamp(2.5 * (0.7 * coarse + 0.3 * fine), -1.0, 1.0);
        abundance.background = std::pow(patches.spread, noise);
    }
    if (exposure_) {
        if (ground == nullptr) {
            throw std::invalid_argument("Boulders that read the exposure field need the ground");
        }
        const double bedrock = exposure_->Exposure(*ground, x, y, options_.exposureSpacing);
        abundance.background *= 1.0 + bedrock * (options_.bedrockAbundance - 1.0);
    }
    abundance.background = std::min(abundance.background, maxBackground_);
    const BoulderEjecta& eject = options_.ejecta;
    if (ejecta.Empty()) {
        return abundance;
    }
    const DegradationModel& degradation = *craters_->Models().degradation;
    for (std::size_t n = 0; n < ejecta.Size(); ++n) {
        const Crater crater = ejecta.At(n);
        const double radius = 0.5 * crater.diameter;
        double reach = eject.reach;
        if (eject.gravity && eject.regolithThickness) {
            reach = std::fmin(reach, std::fmax(1.0, BoulderThrowDistance(crater.diameter,
                                                                         *eject.regolithThickness,
                                                                         *eject.gravity) /
                                                        radius));
        }
        const double rho = (surface_.IsSphere() ? surface_.Distance({crater.x, crater.y}, {x, y})
                                                : std::hypot(x - crater.x, y - crater.y)) /
                           radius;
        if (rho >= reach) {
            continue;
        }
        double radial = 1.0;
        if (eject.profile == EjectaProfile::Smoothstep) {
            radial = rho <= 1.0 ? 1.0 : Smoothstep((reach - rho) / (reach - 1.0));
        } else {
            radial = rho <= 1.0 ? eject.floorShare : std::exp(-(rho - 1.0) / eject.decayRadii);
        }
        if (radial <= abundance.ejecta) {
            continue;
        }
        double kept = 1.0;
        if (eject.aging == EjectaAging::Freshness) {
            const CraterState state = degradation.State(crater);
            const double freshness = state.fresh.depthRatio > 0.0
                                         ? state.current.depthRatio / state.fresh.depthRatio
                                         : 0.0;
            kept = Smoothstep((freshness - eject.minFreshness) / (1.0 - eject.minFreshness));
        } else {
            kept = std::pow(std::fmax(0.0, 1.0 - crater.ageGyr / eject.lifetimeGyr),
                            eject.agingExponent);
        }
        abundance.ejecta = std::fmax(abundance.ejecta, kept * radial);
    }
    return abundance;
}

BoulderAbundance BoulderLayer::AbundanceAt(double x, double y, const Ground* ground) const {
    return Abundance(x, y, ground, EjectaCraters(PointBounds(x, y)));
}

double BoulderLayer::Density(double diameter, const BoulderAbundance& abundance) const {
    double density = options_.distribution
                         ? abundance.background * options_.distribution->Incremental(diameter)
                         : GolombekRappDensity(diameter, options_.coverage * abundance.background,
                                               options_.qa, options_.qb);
    if (abundance.ejecta > 0.0) {
        density += abundance.ejecta * ejectaSizes_->Incremental(diameter);
    }
    return density;
}

double BoulderLayer::DensityAt(double diameter, double x, double y, const Ground* ground) const {
    return Density(diameter, AbundanceAt(x, y, ground));
}

std::vector<Boulder> BoulderLayer::Boulders(const Bounds2d& region, double spacing,
                                            const Ground* ground) const {
    if (exposure_ && ground == nullptr) {
        throw std::invalid_argument("Boulders that read the exposure field need the ground");
    }
    const std::uint32_t seed = options_.seed ^ kBoulderSeed;
    const FreshAges ages;
    CraterGenerationOptions generation{
        .dMin = options_.minDiameter, .dMax = options_.maxDiameter, .marginRadii = 1.0};
    generation.surface = surface_;
    if (spacing > 0.0) {
        generation.minResolvedDiameter = options_.fade.startSamples * spacing;
    }
    if (cache_) {
        generation.cellCraters = [this, seed, &ages](const CraterBand& band,
                                                     const CellIndex& cell) {
            return cache_->Get(band.exponent, cell, [&] {
                return GenerateCellCraters(*cells_, band, cell, *candidates_, ages, seed);
            });
        };
    }
    const CraterList candidates =
        GenerateCraters(*cells_, region, *candidates_, ages, seed, generation);
    const CraterList ejecta = EjectaCraters(region);
    std::vector<Boulder> boulders;
    for (std::size_t n = 0; n < candidates.Size(); ++n) {
        const Crater candidate = candidates.At(n);
        const auto low = static_cast<std::uint32_t>(candidate.id);
        const auto high = static_cast<std::uint32_t>(candidate.id >> 32);
        const U32x4 draw =
            Philox4x32({low, high, kBoulderAttributes, 0u}, {options_.seed, kBoulderSeed});
        const BoulderAbundance abundance = Abundance(candidate.x, candidate.y, ground, ejecta);
        const double keep =
            Density(candidate.diameter, abundance) / candidates_->Incremental(candidate.diameter);
        if (Uniform(draw.x, draw.y) >= keep) {
            continue;
        }
        const U32x4 shape =
            Philox4x32({low, high, kBoulderAttributes, 1u}, {options_.seed, kBoulderSeed});
        const double ratio =
            options_.minHeightRatio +
            (options_.maxHeightRatio - options_.minHeightRatio) * Uniform(shape.x, shape.y);
        const double burial = options_.minBurial +
                              (options_.maxBurial - options_.minBurial) * Uniform(draw.z, draw.w);
        Boulder boulder{candidate.x,
                        candidate.y,
                        candidate.diameter,
                        ratio * candidate.diameter,
                        burial,
                        2.0 * std::numbers::pi * Uniform(shape.z, shape.w),
                        options_.maxLean * Uniform(shape.w, shape.x),
                        candidate.id};
        boulder.model = options_.models.IsModel(boulder.diameter, spacing);
        if (ground != nullptr) {
            const double height = ground->HeightAt(boulder.x, boulder.y, options_.exposureSpacing);
            if (!std::isnan(height)) {
                boulder.ground = height;
            }
        }
        boulders.push_back(boulder);
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

void BoulderLayer::Apply(Raster<float>& heights, const LayerContext& context,
                         TerrainFeatures& features) const {
    const RasterGrid grid = RasterGrid::Of(heights);
    const double resolved = std::max(context.spacing, grid.Spacing(surface_));
    std::vector<Boulder> boulders = Boulders(grid.Bounds(), resolved, context.ground);
    for (Boulder& boulder : boulders) {
        if (boulder.model && !boulder.ground) {
            const float below =
                SampleRaster(heights, boulder.x, boulder.y, ResampleMethod::Bilinear);
            if (!std::isnan(below)) {
                boulder.ground = static_cast<double>(below);
            }
        }
    }
    if (options_.relief != BoulderRelief::None) {
        const bool shaped = options_.relief == BoulderRelief::Shape;
        std::vector<std::vector<Window>> windows(boulders.size());
        std::vector<double> weights(boulders.size(), 0.0);
        std::vector<TriangleMesh> placed(shaped ? boulders.size() : 0);
        for (std::size_t n = 0; n < boulders.size(); ++n) {
            const Boulder& boulder = boulders[n];
            if (boulder.model) {
                continue;
            }
            weights[n] = ResolutionWeight(boulder.diameter, resolved, options_.fade);
            if (weights[n] == 0.0) {
                continue;
            }
            if (shaped) {
                placed[n] = PlacedOnLevel(boulder);
                const Box3d box = Bounds(placed[n]);
                const double cx = surface_.IsSphere() ? 0.0 : boulder.x;
                const double cy = surface_.IsSphere() ? 0.0 : boulder.y;
                const double reach =
                    std::fmax(std::fmax(std::fabs(box.min.x - cx), std::fabs(box.max.x - cx)),
                              std::fmax(std::fabs(box.min.y - cy), std::fabs(box.max.y - cy)));
                windows[n] = WindowsAround(heights, boulder.x, boulder.y, reach, surface_);
            } else {
                windows[n] =
                    WindowsAround(heights, boulder.x, boulder.y, 0.5 * boulder.diameter, surface_);
            }
        }
        constexpr std::size_t kBandRows = 32;
        const std::size_t bands = (heights.Height() + kBandRows - 1) / kBandRows;
        ParallelFor(bands, context.threads, [&](std::size_t band) {
            const std::size_t bandBegin = band * kBandRows;
            const std::size_t bandEnd = std::min(heights.Height(), bandBegin + kBandRows);
            for (std::size_t n = 0; n < boulders.size(); ++n) {
                if (weights[n] == 0.0) {
                    continue;
                }
                const TriangleMesh* shape = shaped ? &placed[n] : nullptr;
                for (const Window& window : windows[n]) {
                    for (std::size_t row = std::max(bandBegin, window.row0);
                         row < std::min(bandEnd, window.row1); ++row) {
                        for (std::size_t col = window.col0; col < window.col1; ++col) {
                            float& cell = heights(col, row);
                            if (heights.IsNoData(cell)) {
                                continue;
                            }
                            const Vec2d centre = heights.CellCenter(col, row);
                            cell =
                                AddRelief(cell, centre.x, centre.y, boulders[n], shape, weights[n]);
                        }
                    }
                }
            }
        });
    }
    for (const Boulder& boulder : boulders) {
        if (boulder.model && boulder.ground) {
            const auto shape = Shape(boulder);
            Boulder local = boulder;
            if (surface_.IsSphere()) {
                local.x = 0.0;
                local.y = 0.0;
            }
            features.boulderModels.push_back(
                {boulder, BoulderShapeSeed(boulder.id, options_.shapePrototypes), shape,
                 PlaceBoulderModel(local, *shape, *boulder.ground)});
        }
    }
    features.boulders.insert(features.boulders.end(), boulders.begin(), boulders.end());
}

float BoulderLayer::ApplyAt(float height, double x, double y, const LayerContext& context) const {
    RequirePositive(context.spacing, "Sample spacing");
    if (std::isnan(height) || options_.relief == BoulderRelief::None) {
        return height;
    }
    for (const Boulder& boulder : Boulders(PointBounds(x, y), context.spacing, context.ground)) {
        const double weight = ResolutionWeight(boulder.diameter, context.spacing, options_.fade);
        if (boulder.model || weight <= 0.0) {
            continue;
        }
        if (options_.relief == BoulderRelief::Shape) {
            const TriangleMesh placed = PlacedOnLevel(boulder);
            height = AddRelief(height, x, y, boulder, &placed, weight);
        } else {
            height = AddRelief(height, x, y, boulder, nullptr, weight);
        }
    }
    return height;
}

}  // namespace terraingen
