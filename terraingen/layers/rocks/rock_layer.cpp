#include "terraingen/layers/rocks/rock_layer.h"

#include "terraingen/layers/craters/ages/fresh.h"
#include "terraingen/noise/kernels/perlin.h"
#include "terraingen/noise/kernels/philox.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/cells.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace terraingen {

namespace {

constexpr std::uint32_t kRockSeed = 0x524F434Bu;
constexpr std::uint32_t kRockAttributes = 0x41545452u;
constexpr std::uint32_t kRockPatches = 0x50415443u;

/// Golombek-Rapp rocks per m^2 per metre of diameter at `diameter` for coverage `k`.
double Density(double diameter, double k, double qa, double qb) {
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

}  // namespace

void ValidateRockLayerOptions(const RockLayerOptions& options) {
    if (!(options.coverage > 0.0 && options.coverage < 1.0)) {
        throw std::invalid_argument("Rock coverage must be between 0 and 1");
    }
    if (!(std::isfinite(options.minDiameter) && options.minDiameter > 0.0 &&
          std::isfinite(options.maxDiameter) && options.maxDiameter > options.minDiameter)) {
        throw std::invalid_argument("Rock diameters must satisfy 0 < minDiameter < maxDiameter");
    }
    if (!(std::isfinite(options.patches.spread) && options.patches.spread >= 1.0 &&
          std::isfinite(options.patches.size) && options.patches.size > 0.0)) {
        throw std::invalid_argument("Rock patches need spread >= 1 and a positive size");
    }
    const RockEjecta& ejecta = options.ejecta;
    if (!(std::isfinite(ejecta.minCraterDiameter) && ejecta.minCraterDiameter >= 0.0)) {
        throw std::invalid_argument("Rock ejecta minCraterDiameter must not be negative");
    }
    if (ejecta.minCraterDiameter > 0.0 &&
        !(ejecta.coverage > 0.0 && ejecta.coverage < 1.0 && std::isfinite(ejecta.reach) &&
          ejecta.reach > 1.0 && ejecta.minFreshness >= 0.0 && ejecta.minFreshness < 1.0)) {
        throw std::invalid_argument("Rock ejecta need a coverage in (0, 1), a reach over 1 radius "
                                    "and minFreshness in [0, 1)");
    }
    if (options.coverage * options.patches.spread >= 1.0) {
        throw std::invalid_argument("Rock coverage times the patch spread must stay below 1");
    }
    if (options.exposure) {
        ValidateExposureParams(*options.exposure);
    }
    if (!(std::isfinite(options.exposureSpacing) && options.exposureSpacing > 0.0)) {
        throw std::invalid_argument("Rock exposureSpacing must be positive");
    }
    if (!(options.minHeightRatio > 0.0 && options.maxHeightRatio >= options.minHeightRatio &&
          std::isfinite(options.maxHeightRatio))) {
        throw std::invalid_argument("Rock height ratios must satisfy 0 < min <= max");
    }
    if (!(options.minBurial >= 0.0 && options.maxBurial >= options.minBurial &&
          options.maxBurial < 1.0)) {
        throw std::invalid_argument("Rock burial must satisfy 0 <= min <= max < 1");
    }
    if (!(std::isfinite(options.maxLean) && options.maxLean >= 0.0)) {
        throw std::invalid_argument("Rock maxLean must not be negative");
    }
    ValidateResolutionFade(options.fade);
}

double RockCandidateCoverage(const RockLayerOptions& options) {
    const double patches = options.coverage * options.patches.spread;
    return options.ejecta.minCraterDiameter > 0.0 ? std::max(patches, options.ejecta.coverage)
                                                  : patches;
}

RockLayer::RockLayer(RockLayerOptions options, std::shared_ptr<const CraterLayer> craters)
    : options_(std::move(options)), craters_(std::move(craters)) {
    ValidateRockLayerOptions(options_);
    if (options_.ejecta.minCraterDiameter > 0.0 && !craters_) {
        throw std::invalid_argument("Rock ejecta need the crater layer whose craters throw them");
    }
    maxCoverage_ = RockCandidateCoverage(options_);
    candidates_ = std::make_shared<GolombekRappSfd>(maxCoverage_, options_.qa, options_.qb);
    if (options_.cacheRocks > 0) {
        cache_ = std::make_shared<CraterCellCache>(options_.cacheRocks);
    }
    if (options_.exposure) {
        exposure_.emplace(*options_.exposure);
    }
}

CraterList RockLayer::EjectaCraters(const Bounds2d& region) const {
    if (options_.ejecta.minCraterDiameter <= 0.0) {
        return {};
    }
    const double margin = options_.ejecta.reach * 0.5 * craters_->MaxFeatureDiameter();
    const double spacing =
        options_.ejecta.minCraterDiameter / craters_->Options().apply.fade.startSamples;
    const CraterList all = craters_->Craters(region.Expanded(margin), spacing);
    CraterList large;
    for (std::size_t n = 0; n < all.Size(); ++n) {
        if (all.At(n).diameter >= options_.ejecta.minCraterDiameter) {
            large.Append(all.At(n));
        }
    }
    return large;
}

double RockLayer::Coverage(double x, double y, const Ground* ground,
                           const CraterList& ejecta) const {
    double k = options_.coverage;
    const RockPatches& patches = options_.patches;
    if (patches.spread > 1.0) {
        const auto px = static_cast<float>(x / patches.size);
        const auto py = static_cast<float>(y / patches.size);
        const std::uint32_t seed = options_.seed ^ kRockPatches;
        const double noise = std::clamp(
            2.5 * (0.7 * static_cast<double>(PerlinNoise2(px, py, seed)) +
                   0.3 * static_cast<double>(PerlinNoise2(4.0f * px, 4.0f * py, seed + 1u))),
            -1.0, 1.0);
        k *= std::pow(patches.spread, noise);
    }
    const double richest = maxCoverage_;
    if (exposure_) {
        if (ground == nullptr) {
            throw std::invalid_argument("Rocks that read the exposure field need the ground");
        }
        const double bedrock = exposure_->Exposure(*ground, x, y, options_.exposureSpacing);
        k += 0.5 * bedrock * (richest - k);
    }
    const RockEjecta& eject = options_.ejecta;
    if (!ejecta.Empty()) {
        const DegradationModel& degradation = *craters_->Models().degradation;
        double boost = 0.0;
        for (std::size_t n = 0; n < ejecta.Size(); ++n) {
            const Crater crater = ejecta.At(n);
            const double radius = 0.5 * crater.diameter;
            const double rho = std::hypot(x - crater.x, y - crater.y) / radius;
            if (rho >= eject.reach) {
                continue;
            }
            const CraterState state = degradation.State(crater);
            const double freshness = state.fresh.depthRatio > 0.0
                                         ? state.current.depthRatio / state.fresh.depthRatio
                                         : 0.0;
            const double fresh =
                Smoothstep((freshness - eject.minFreshness) / (1.0 - eject.minFreshness));
            const double nearRim =
                rho <= 1.0 ? 1.0 : Smoothstep((eject.reach - rho) / (eject.reach - 1.0));
            boost = std::max(boost, fresh * nearRim);
        }
        if (eject.coverage > k) {
            k += boost * (eject.coverage - k);
        }
    }
    return std::min(k, maxCoverage_);
}

double RockLayer::CoverageAt(double x, double y, const Ground* ground) const {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    return Coverage(x, y, ground,
                    EjectaCraters({x, y, std::nextafter(x, kInf), std::nextafter(y, kInf)}));
}

std::vector<Rock> RockLayer::Rocks(const Bounds2d& region, double spacing,
                                   const Ground* ground) const {
    if (exposure_ && ground == nullptr) {
        throw std::invalid_argument("Rocks that read the exposure field need the ground");
    }
    const std::uint32_t seed = options_.seed ^ kRockSeed;
    const FreshAges ages;
    CraterGenerationOptions generation{
        .dMin = options_.minDiameter, .dMax = options_.maxDiameter, .marginRadii = 1.0};
    if (spacing > 0.0) {
        generation.minResolvedDiameter = options_.fade.startSamples * spacing;
    }
    if (cache_) {
        generation.cellCraters = [this, seed, &ages](const CraterBand& band,
                                                     const CellIndex& cell) {
            return cache_->Get(band.exponent, cell, [&] {
                return GenerateCellCraters(PlanarCells(), band, cell, *candidates_, ages, seed);
            });
        };
    }
    const CraterList candidates =
        GenerateCraters(PlanarCells(), region, *candidates_, ages, seed, generation);
    const CraterList ejecta = EjectaCraters(region);
    std::vector<Rock> rocks;
    for (std::size_t n = 0; n < candidates.Size(); ++n) {
        const Crater candidate = candidates.At(n);
        const auto low = static_cast<std::uint32_t>(candidate.id);
        const auto high = static_cast<std::uint32_t>(candidate.id >> 32);
        const U32x4 draw = Philox4x32({low, high, kRockAttributes, 0u}, {options_.seed, kRockSeed});
        const double k = Coverage(candidate.x, candidate.y, ground, ejecta);
        const double keep = Density(candidate.diameter, k, options_.qa, options_.qb) /
                            Density(candidate.diameter, maxCoverage_, options_.qa, options_.qb);
        if (Uniform(draw.x, draw.y) >= keep) {
            continue;
        }
        const U32x4 shape =
            Philox4x32({low, high, kRockAttributes, 1u}, {options_.seed, kRockSeed});
        const double ratio =
            options_.minHeightRatio +
            (options_.maxHeightRatio - options_.minHeightRatio) * Uniform(shape.x, shape.y);
        const double burial = options_.minBurial +
                              (options_.maxBurial - options_.minBurial) * Uniform(draw.z, draw.w);
        rocks.push_back({candidate.x, candidate.y, candidate.diameter, ratio * candidate.diameter,
                         burial, 2.0 * std::numbers::pi * Uniform(shape.z, shape.w),
                         options_.maxLean * Uniform(shape.w, shape.x), candidate.id});
    }
    return rocks;
}

float AddRock(float height, double x, double y, const Rock& rock, double weight) {
    const double dx = x - rock.x;
    const double dy = y - rock.y;
    const double radius = 0.5 * rock.diameter;
    const double r2 = (dx * dx + dy * dy) / (radius * radius);
    if (r2 >= 1.0) {
        return height;
    }
    return static_cast<float>(static_cast<double>(height) +
                              weight * rock.ExposedHeight() * std::sqrt(1.0 - r2));
}

void RockLayer::Apply(Raster<float>& heights, const LayerContext& context,
                      TerrainFeatures& features) const {
    const RasterGrid grid = RasterGrid::Of(heights);
    const double resolved = std::max(context.spacing, grid.PixelSize());
    const std::vector<Rock> rocks = Rocks(grid.Bounds(), resolved, context.ground);
    if (options_.relief == RockRelief::Body) {
        std::vector<Window> windows;
        std::vector<double> weights;
        windows.reserve(rocks.size());
        weights.reserve(rocks.size());
        for (const Rock& rock : rocks) {
            windows.push_back(WindowAround(heights, rock.x, rock.y, 0.5 * rock.diameter));
            weights.push_back(ResolutionWeight(rock.diameter, resolved, options_.fade));
        }
        constexpr std::size_t kBandRows = 32;
        const std::size_t bands = (heights.Height() + kBandRows - 1) / kBandRows;
        ParallelFor(bands, context.threads, [&](std::size_t band) {
            const std::size_t bandBegin = band * kBandRows;
            const std::size_t bandEnd = std::min(heights.Height(), bandBegin + kBandRows);
            for (std::size_t n = 0; n < rocks.size(); ++n) {
                if (weights[n] == 0.0) {
                    continue;
                }
                const Window& window = windows[n];
                for (std::size_t row = std::max(bandBegin, window.row0);
                     row < std::min(bandEnd, window.row1); ++row) {
                    for (std::size_t col = window.col0; col < window.col1; ++col) {
                        float& cell = heights(col, row);
                        if (heights.IsNoData(cell)) {
                            continue;
                        }
                        const Vec2d centre = heights.CellCenter(col, row);
                        cell = AddRock(cell, centre.x, centre.y, rocks[n], weights[n]);
                    }
                }
            }
        });
    }
    features.rocks.insert(features.rocks.end(), rocks.begin(), rocks.end());
}

float RockLayer::ApplyAt(float height, double x, double y, const LayerContext& context) const {
    if (!(std::isfinite(context.spacing) && context.spacing > 0.0)) {
        throw std::invalid_argument("Sample spacing must be positive and finite");
    }
    if (std::isnan(height) || options_.relief == RockRelief::None) {
        return height;
    }
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const Bounds2d point{x, y, std::nextafter(x, kInf), std::nextafter(y, kInf)};
    for (const Rock& rock : Rocks(point, context.spacing, context.ground)) {
        const double weight = ResolutionWeight(rock.diameter, context.spacing, options_.fade);
        if (weight > 0.0) {
            height = AddRock(height, x, y, rock, weight);
        }
    }
    return height;
}

}  // namespace terraingen
