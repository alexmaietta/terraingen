#include "terraingen/layers/craters/shaping/apply.h"

#include "terraingen/gpu/kernel.h"
#include "terraingen/layers/craters/shaping/crater_kernel.h"
#include "terraingen/noise/fractal.h"
#include "terraingen/noise/hash.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// Nodes and weights of 9-point Gauss-Hermite quadrature (the node at 0 and the positive half):
/// the mean of f over a normal distribution of mean m and deviation s is
/// sum(w f(m + sqrt(2) s x)) / sqrt(pi).
constexpr std::array<double, 5> kHermiteNodes{0.0, 0.723551018752838, 1.46855328921667,
                                              2.26658058453184, 3.19099320178153};
constexpr std::array<double, 5> kHermiteWeights{0.720235215606051, 0.432651559002556,
                                                0.0884745273943766, 0.00494362427553695,
                                                0.0000396069772632645};

/// The profile's height at `distance`, its rim rounded by `rim`: blended by `rim.Weight` toward
/// the profile averaged over a normal spread of distances of deviation `sigmaRadii` radii.
double ProfileHeight(const CraterProfile& profile, double distance, double diameter,
                     const CraterState& state, const RimSmoothing& rim) {
    const double height = profile.Height(distance, diameter, state);
    const double radius = 0.5 * diameter;
    const double weight = rim.Weight(distance / radius);
    if (weight == 0.0) {
        return height;
    }
    const double step = std::numbers::sqrt2 * rim.sigmaRadii * radius;
    double smoothed = kHermiteWeights[0] * height;
    for (std::size_t i = 1; i < kHermiteNodes.size(); ++i) {
        for (const double sign : {-1.0, 1.0}) {
            const double at = std::fabs(distance + sign * step * kHermiteNodes[i]);
            smoothed += kHermiteWeights[i] * profile.Height(at, diameter, state);
        }
    }
    smoothed /= std::sqrt(std::numbers::pi);
    return height + weight * (smoothed - height);
}

/// The noise `noise` adds to `crater` at `offset` metres from its centre and `radii` crater radii
/// out: footprint noise fading to nothing from the rim to `reachRadii`, and rim noise in its band.
double NoiseHeight(const Crater& crater, const Vec2d& offset, double radii, double reachRadii,
                   const CraterNoise& noise) {
    if (noise.amplitude == 0.0 && noise.rimAmplitude == 0.0) {
        return 0.0;
    }
    const double radius = 0.5 * crater.diameter;
    const std::uint32_t key =
        PcgHash(static_cast<std::uint32_t>(crater.id) ^
                PcgHash(static_cast<std::uint32_t>(crater.id >> 32) ^ noise.seed));
    const auto fractal = [&](double wavelengthRadii, std::uint32_t seed) {
        FractalParams params;
        params.seed = seed;
        params.wavelength = static_cast<float>(wavelengthRadii * radius);
        params.amplitude = 1.0f;
        params.octaves = noise.octaves;
        params.gain = static_cast<float>(noise.gain);
        return static_cast<double>(
            Fractal2(static_cast<float>(offset.x), static_cast<float>(offset.y), params));
    };
    const double footprint =
        radii <= 1.0 ? 1.0 : Falloff(radii, 1.0, reachRadii, BlendShape::Smootherstep);
    const double rim =
        radii <= 1.0
            ? Falloff(2.0 - radii, 1.0, 2.0 - noise.rimInnerRadii, BlendShape::Smootherstep)
            : Falloff(radii, 1.0, noise.rimOuterRadii, BlendShape::Smootherstep);
    double height = 0.0;
    if (noise.amplitude > 0.0 && footprint > 0.0) {
        height += noise.amplitude * footprint * fractal(noise.wavelengthRadii, key);
    }
    if (noise.rimAmplitude > 0.0 && rim > 0.0) {
        height += noise.rimAmplitude * rim * fractal(noise.rimWavelengthRadii, key + 1u);
    }
    return crater.diameter * height;
}

/// Fractal noise of `octaves` octaves streaked radially about a crater: `across` radii wide around
/// it at the rim and `along` radii long outward, at `radii` radii and `angle` radians from its
/// centre.
double RadialStreaks(double radii, double angle, double across, double along, int octaves,
                     std::uint32_t seed) {
    FractalParams params;
    params.seed = seed;
    params.wavelength = 1.0f;
    params.amplitude = 1.0f;
    params.octaves = octaves;
    const double ring = 1.0 / across;
    return static_cast<double>(
        Fractal3(ring * std::cos(angle), ring * std::sin(angle), radii / along, params));
}

/// The texture `texture` adds to `crater` in `state` at `offset` metres from its centre and
/// `radii` crater radii out: gullies down the wall and ejecta texture, faded with wear.
double TextureHeight(const Crater& crater, const CraterState& state, const Vec2d& offset,
                     double radii, const CraterTexture& texture) {
    if (texture.wallStreaks == 0.0 && texture.ejecta == 0.0) {
        return 0.0;
    }
    const double wear = (state.Freshness() - texture.minFreshness) / (1.0 - texture.minFreshness);
    const double fade = Smoothstep(std::clamp(wear, 0.0, 1.0));
    if (!(fade > 0.0)) {
        return 0.0;
    }
    const std::uint32_t key =
        PcgHash(static_cast<std::uint32_t>(crater.id) ^
                PcgHash(static_cast<std::uint32_t>(crater.id >> 32) ^ texture.seed ^ 0x54455854u));
    const double angle = std::atan2(offset.y, offset.x);
    double height = 0.0;
    if (texture.wallStreaks > 0.0 && radii > 0.25 && radii < 1.0) {
        const double band = Smoothstep(std::clamp((radii - 0.25) / 0.35, 0.0, 1.0)) *
                            Smoothstep(std::clamp((1.0 - radii) / 0.1, 0.0, 1.0));
        height += texture.wallStreaks * crater.diameter * band *
                  RadialStreaks(radii, angle, texture.wallStreakWidthRadii, 0.5, 2, key);
    }
    if (texture.ejecta > 0.0 && radii > 1.0 && radii < texture.ejectaReachRadii) {
        const double radius = 0.5 * crater.diameter;
        const double thickness =
            0.14 * std::pow(radius, 0.74) * EjectaThicknessShare(radii, texture.ejectaReachRadii);
        const double rise = Smoothstep(std::clamp((radii - 1.0) / 0.1, 0.0, 1.0));
        height += texture.ejecta * thickness * rise *
                  RadialStreaks(radii, angle, texture.ejectaLineationRadii,
                                2.0 * texture.ejectaLineationRadii, 3, key + 1u);
    }
    return fade * height;
}

/// Height of the raster at world point (x, y) by bilinear interpolation of the cell centres, or
/// nothing if a contributing cell is outside the raster or holds no data.
std::optional<double> Sample(const Raster<float>& raster, double x, double y) {
    const Vec2d pixel = raster.Transform().WorldToPixel(x, y);
    const double u = pixel.x - 0.5;
    const double v = pixel.y - 0.5;
    const double col0 = std::floor(u);
    const double row0 = std::floor(v);
    const double maxCol = static_cast<double>(raster.Width()) - 1.0;
    const double maxRow = static_cast<double>(raster.Height()) - 1.0;
    if (col0 < 0.0 || row0 < 0.0 || col0 > maxCol || row0 > maxRow) {
        return std::nullopt;
    }
    const auto c0 = static_cast<std::size_t>(col0);
    const auto r0 = static_cast<std::size_t>(row0);
    const std::size_t c1 = std::min(c0 + 1, raster.Width() - 1);
    const std::size_t r1 = std::min(r0 + 1, raster.Height() - 1);
    const double fu = u - col0;
    const double fv = v - row0;
    const std::array<float, 4> cells{raster(c0, r0), raster(c1, r0), raster(c0, r1),
                                     raster(c1, r1)};
    std::array<double, 4> values{};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (raster.IsNoData(cells[i])) {
            return std::nullopt;
        }
        values[i] = cells[i];
    }
    return (1.0 - fv) * ((1.0 - fu) * values[0] + fu * values[1]) +
           fv * ((1.0 - fu) * values[2] + fu * values[3]);
}

/// The box a crater reaching `reachRadii` radii can change, in surface coordinates.
Bounds2d Footprint(const PreparedCrater& prepared, double reachRadii, const Surface& surface) {
    const Crater& crater = prepared.crater;
    const double reach = prepared.outline.MaxScale() * reachRadii * (0.5 * crater.diameter);
    return surface.Expand({crater.x, crater.y, crater.x, crater.y}, reach);
}

/// Craters binned by size band for the crater datum kernel (kernels/crater_datums.slang): per
/// band its grid's corner and cell size, its cells across and down and its first cell, then
/// each cell's first entry and entry count, and the entries, crater indices in ascending order.
struct ReliefBands {
    std::vector<float> floats;
    std::vector<std::uint32_t> grids;
    std::vector<std::uint32_t> cells;
    std::vector<std::uint32_t> entries;
};

/// Most cells `BandRelief` lays out before datums are left to the CPU.
constexpr std::size_t kMaxReliefCells = std::size_t{1} << 24;

/// Bins `records`, the craters `kept` of `prepared`, by size band, each band in cells a little
/// over its largest reach, so the 3 x 3 cells around a point hold every crater reaching it;
/// nothing beyond `kMaxReliefCells` cells.
std::optional<ReliefBands> BandRelief(const std::vector<PreparedCrater>& prepared,
                                      const std::vector<std::size_t>& kept,
                                      const std::vector<CraterRecord>& records, double reachRadii,
                                      double outlineScale) {
    std::map<int, std::vector<std::uint32_t>> bands;
    for (std::size_t n = 0; n < kept.size(); ++n) {
        bands[std::ilogb(prepared[kept[n]].crater.diameter)].push_back(
            static_cast<std::uint32_t>(n));
    }
    ReliefBands relief;
    std::vector<std::uint32_t> cellOf;
    std::vector<std::uint32_t> filled;
    for (const auto& [band, members] : bands) {
        const auto size =
            static_cast<float>(1.001 * outlineScale * reachRadii * std::ldexp(1.0, band));
        float minX = std::numeric_limits<float>::infinity();
        float minY = minX;
        float maxX = -minX;
        float maxY = -minX;
        for (const std::uint32_t n : members) {
            minX = std::fmin(minX, records[n].x);
            minY = std::fmin(minY, records[n].y);
            maxX = std::fmax(maxX, records[n].x);
            maxY = std::fmax(maxY, records[n].y);
        }
        const auto across = static_cast<std::size_t>(std::floor((maxX - minX) / size)) + 1;
        const auto down = static_cast<std::size_t>(std::floor((maxY - minY) / size)) + 1;
        const std::size_t first = relief.cells.size() / 2;
        if (first + across * down > kMaxReliefCells) {
            return std::nullopt;
        }
        relief.floats.insert(relief.floats.end(), {minX, minY, size});
        relief.grids.insert(relief.grids.end(),
                            {static_cast<std::uint32_t>(across), static_cast<std::uint32_t>(down),
                             static_cast<std::uint32_t>(first)});
        relief.cells.resize(2 * (first + across * down), 0);
        cellOf.resize(members.size());
        for (std::size_t m = 0; m < members.size(); ++m) {
            const CraterRecord& r = records[members[m]];
            const auto i =
                std::min(across - 1, static_cast<std::size_t>(std::floor((r.x - minX) / size)));
            const auto j =
                std::min(down - 1, static_cast<std::size_t>(std::floor((r.y - minY) / size)));
            cellOf[m] = static_cast<std::uint32_t>(first + j * across + i);
            ++relief.cells[2 * cellOf[m] + 1];
        }
        auto start = static_cast<std::uint32_t>(relief.entries.size());
        for (std::size_t cell = first; cell < first + across * down; ++cell) {
            relief.cells[2 * cell] = start;
            start += relief.cells[2 * cell + 1];
        }
        relief.entries.resize(start);
        filled.assign(across * down, 0);
        for (std::size_t m = 0; m < members.size(); ++m) {
            const std::uint32_t cell = cellOf[m];
            relief.entries[relief.cells[2 * cell] + filled[cell - first]++] = members[m];
        }
    }
    return relief;
}

/// Fits the datums of craters `fitted` of `prepared` on GPU `sampling.backend`: the ground under
/// each lattice point is read on the CPU, the relief of the craters before each crater summed on
/// the GPU, and the fit solved on the CPU. False, leaving the datums unset, for the CPU, a
/// centre-height datum, or a backend or crater set the GPU cannot take.
bool FitDatumsOnGpu(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
                    const HeightSampler& ground, const CraterProfile& profile,
                    const CraterShapeKernel& shape, const CraterApplyOptions& options,
                    const CraterSampling& sampling) {
    if (sampling.backend == Backend::Cpu || options.datum == CraterDatum::CentreHeight) {
        return false;
    }
    constexpr double kInfinity = std::numeric_limits<double>::infinity();
    Bounds2d region{kInfinity, kInfinity, -kInfinity, -kInfinity};
    std::vector<bool> isFitted(prepared.size(), false);
    for (const std::size_t k : fitted) {
        const Crater& c = prepared[k].crater;
        const double fitRadius = options.datumFitRadii * 0.5 * c.diameter;
        region = {std::fmin(region.minX, c.x - fitRadius), std::fmin(region.minY, c.y - fitRadius),
                  std::fmax(region.maxX, c.x + fitRadius), std::fmax(region.maxY, c.y + fitRadius)};
        isFitted[k] = true;
    }
    const std::size_t last = *std::max_element(fitted.begin(), fitted.end());
    double outlineScale = MaxOutlineScale(options.irregularity);
    for (std::size_t n = 0; n < last; ++n) {
        outlineScale = std::fmax(outlineScale, prepared[n].outline.MaxScale());
    }
    std::vector<std::size_t> kept;
    std::vector<std::uint32_t> slot(prepared.size(), 0);
    for (std::size_t n = 0; n <= last; ++n) {
        const Crater& c = prepared[n].crater;
        const double reach = outlineScale * profile.ReachRadii() * 0.5 * c.diameter;
        if (isFitted[n] || (c.x + reach > region.minX && c.x - reach < region.maxX &&
                            c.y + reach > region.minY && c.y - reach < region.maxY)) {
            slot[n] = static_cast<std::uint32_t>(kept.size());
            kept.push_back(n);
        }
    }
    const Vec2d origin{0.5 * (region.minX + region.maxX), 0.5 * (region.minY + region.maxY)};
    CraterKernelBatch batch = MakeCraterKernelBatch(shape, profile, prepared, kept, options);
    for (std::size_t n = 0; n < kept.size(); ++n) {
        batch.records[n].x = static_cast<float>(prepared[kept[n]].crater.x - origin.x);
        batch.records[n].y = static_cast<float>(prepared[kept[n]].crater.y - origin.y);
    }
    std::optional<ReliefBands> bands =
        BandRelief(prepared, kept, batch.records, profile.ReachRadii(), outlineScale);
    if (!bands) {
        return false;
    }
    if (batch.data.empty()) {
        batch.data.push_back(0.0f);
    }
    constexpr auto kPoints = static_cast<std::size_t>(kDatumLatticeSide * kDatumLatticeSide);
    Raster<float> lattice(kPoints, fitted.size(), 0.0f);
    std::vector<std::uint8_t> valid(lattice.Size(), 0);
    const auto forEachPoint = [&](std::size_t n, const auto& visit) {
        const double fitRadius = options.datumFitRadii * 0.5 * prepared[fitted[n]].crater.diameter;
        for (int j = 0; j < kDatumLatticeSide; ++j) {
            const double dy = DatumLatticeOffset(j, fitRadius);
            for (int i = 0; i < kDatumLatticeSide; ++i) {
                const double dx = DatumLatticeOffset(i, fitRadius);
                if (dx * dx + dy * dy < fitRadius * fitRadius) {
                    visit(n * kPoints + static_cast<std::size_t>(j * kDatumLatticeSide + i), dx,
                          dy);
                }
            }
        }
    };
    ParallelFor(fitted.size(), sampling.threads, [&](std::size_t n) {
        const Crater& c = prepared[fitted[n]].crater;
        forEachPoint(n, [&](std::size_t at, double dx, double dy) {
            const float height = ground(c.x + dx, c.y + dy);
            if (!std::isnan(height)) {
                lattice.Values()[at] = height;
                valid[at] = 1;
            }
        });
    });
    CraterDatumKernel kernel;
    kernel.shape = shape;
    kernel.fitRadii = static_cast<float>(options.datumFitRadii);
    kernel.latticeSide = static_cast<std::uint32_t>(kDatumLatticeSide);
    kernel.bandCount = static_cast<std::uint32_t>(bands->grids.size() / 3);
    std::vector<std::uint32_t> targets(fitted.size());
    for (std::size_t n = 0; n < fitted.size(); ++n) {
        targets[n] = slot[fitted[n]];
    }
    const std::array<KernelBuffer, 7> extra{
        KernelBuffer{std::as_bytes(std::span(batch.records)), batch.records.size()},
        KernelBuffer{std::as_bytes(std::span(batch.data)), batch.data.size()},
        KernelBuffer{std::as_bytes(std::span(bands->floats)), bands->floats.size()},
        KernelBuffer{std::as_bytes(std::span(bands->grids)), bands->grids.size()},
        KernelBuffer{std::as_bytes(std::span(bands->cells)), bands->cells.size()},
        KernelBuffer{std::as_bytes(std::span(bands->entries)), bands->entries.size()},
        KernelBuffer{std::as_bytes(std::span(targets)), targets.size()}};
    if (!RunKernel(sampling.backend, CompiledKernel<CraterDatumKernel>(),
                   std::as_bytes(std::span(&kernel, 1)), lattice, {0.0, 0.0}, extra)) {
        return false;
    }
    ParallelFor(fitted.size(), sampling.threads, [&](std::size_t n) {
        PlaneFit fit;
        forEachPoint(n, [&](std::size_t at, double dx, double dy) {
            if (valid[at] != 0) {
                fit.Add(dx, dy, lattice.Values()[at]);
            }
        });
        std::optional<Plane>& datum = prepared[fitted[n]].datum;
        if (fit.Empty()) {
            datum.reset();
        } else {
            datum = options.datum == CraterDatum::Plane ? fit.Solve() : Plane{fit.Mean(), 0.0, 0.0};
        }
    });
    LogDebug("craters", "Fitted ", fitted.size(), " crater datums on ",
             BackendName(sampling.backend));
    return true;
}

/// Fits the datums of craters `fitted` of `prepared` to `ground`, or to the raster as given when
/// it is empty; on a GPU backend when `shape` is set and the GPU can take them.
void FitDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
               const Raster<float>& raster, const CraterProfile& profile,
               const std::optional<CraterShapeKernel>& shape, const CraterApplyOptions& options,
               const CraterSampling& sampling) {
    std::optional<Raster<float>> input;
    HeightSampler ground = sampling.ground;
    if (!ground) {
        input.emplace(raster);
        ground = [&input](double x, double y) {
            return static_cast<float>(
                Sample(*input, x, y).value_or(std::numeric_limits<double>::quiet_NaN()));
        };
    }
    if (shape && FitDatumsOnGpu(prepared, fitted, ground, profile, *shape, options, sampling)) {
        return;
    }
    const CraterReliefIndex index(prepared, profile, MaxOutlineScale(options.irregularity),
                                  options.surface);
    ParallelFor(fitted.size(), sampling.threads, [&](std::size_t n) {
        const std::size_t k = fitted[n];
        prepared[k].datum =
            FitCraterDatum(prepared[k].crater, options, DatumGround(ground, index, k));
    });
}

/// A prepared crater with the box its profile can reach.
struct ReachingCrater {
    PreparedCrater* prepared;
    Bounds2d footprint;
    double reach;

    bool Reaches(const Vec2d& p, const Surface& surface) const {
        return surface.Contains(footprint, p) &&
               CraterProfileDistance(prepared->crater, prepared->outline, p.x, p.y, surface) <
                   reach;
    }
};

/// The craters of `prepared` reaching `reachRadii` radii that reach any of `samples`, in order.
std::vector<ReachingCrater> CratersReaching(const SampleBlock& samples,
                                            std::vector<PreparedCrater>& prepared,
                                            double reachRadii, const Surface& surface) {
    std::vector<ReachingCrater> reaching;
    for (PreparedCrater& crater : prepared) {
        const ReachingCrater candidate{&crater, Footprint(crater, reachRadii, surface),
                                       reachRadii * 0.5 * crater.crater.diameter};
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            if (candidate.Reaches(samples.Position(i), surface)) {
                reaching.push_back(candidate);
                break;
            }
        }
    }
    return reaching;
}

/// Side of the tiles craters are binned into for the kernel, in cells.
constexpr std::size_t kTileCells = 32;

/// Shapes the craters `onRaster` of `prepared` into `raster` with the kernel: each crater's
/// `windows` are cut into the tiles they cover, the tiles run on `threads` threads, and a tile
/// shapes its craters in order over their own cells, so every cell sees them in order.
void ShapeTiles(Raster<float>& raster, unsigned threads, const CraterShapeKernel& kernel,
                const CraterKernelBatch& batch, const std::vector<PreparedCrater>& prepared,
                const std::vector<std::size_t>& onRaster,
                const std::vector<std::vector<PixelWindow>>& windows) {
    const std::size_t tilesX = (raster.Width() + kTileCells - 1) / kTileCells;
    const std::size_t tilesY = (raster.Height() + kTileCells - 1) / kTileCells;
    struct Entry {
        std::uint32_t crater;
        PixelWindow cells;
    };
    std::vector<std::vector<Entry>> tiles(tilesX * tilesY);
    for (std::size_t n = 0; n < onRaster.size(); ++n) {
        for (const PixelWindow& window : windows[n]) {
            for (std::size_t ty = window.rowBegin / kTileCells;
                 ty <= (window.rowEnd - 1) / kTileCells; ++ty) {
                for (std::size_t tx = window.colBegin / kTileCells;
                     tx <= (window.colEnd - 1) / kTileCells; ++tx) {
                    PixelWindow cells;
                    cells.colBegin = std::max(window.colBegin, tx * kTileCells);
                    cells.colEnd = std::min(window.colEnd, (tx + 1) * kTileCells);
                    cells.rowBegin = std::max(window.rowBegin, ty * kTileCells);
                    cells.rowEnd = std::min(window.rowEnd, (ty + 1) * kTileCells);
                    tiles[ty * tilesX + tx].push_back({static_cast<std::uint32_t>(n), cells});
                }
            }
        }
    }
    const CraterData data = batch.Data();
    ParallelFor(tiles.size(), threads, [&](std::size_t tile) {
        for (const Entry& entry : tiles[tile]) {
            const Crater& c = prepared[onRaster[entry.crater]].crater;
            const CraterRecord& record = batch.records[entry.crater];
            for (std::size_t row = entry.cells.rowBegin; row < entry.cells.rowEnd; ++row) {
                for (std::size_t col = entry.cells.colBegin; col < entry.cells.colEnd; ++col) {
                    float& cell = raster(col, row);
                    if (!raster.IsNoData(cell)) {
                        const Vec2d centre = raster.CellCenter(col, row);
                        cell = ApplyCraterKernel(kernel, data, record, cell,
                                                 static_cast<float>(centre.x - c.x),
                                                 static_cast<float>(centre.y - c.y));
                    }
                }
            }
        }
    });
}

/// Side of the tiles craters are binned into on a GPU, in cells.
constexpr std::size_t kGpuTileCells = 16;

/// Shapes the craters `onRaster` of `prepared` into `raster` on GPU `backend`, each cell running
/// the craters of its tile in order; false, leaving `raster` unchanged, when the backend is the
/// CPU or cannot take the grid (`RunKernel`).
bool ShapeTilesOnGpu(Raster<float>& raster, Backend backend, const CraterShapeKernel& kernel,
                     CraterKernelBatch& batch, const std::vector<PreparedCrater>& prepared,
                     const std::vector<std::size_t>& onRaster,
                     const std::vector<std::vector<PixelWindow>>& windows) {
    if (backend == Backend::Cpu || batch.records.empty()) {
        return false;
    }
    const std::size_t tilesX = (raster.Width() + kGpuTileCells - 1) / kGpuTileCells;
    const std::size_t tilesY = (raster.Height() + kGpuTileCells - 1) / kGpuTileCells;
    std::vector<std::vector<std::uint32_t>> lists(tilesX * tilesY);
    for (std::size_t n = 0; n < onRaster.size(); ++n) {
        for (const PixelWindow& window : windows[n]) {
            for (std::size_t ty = window.rowBegin / kGpuTileCells;
                 ty <= (window.rowEnd - 1) / kGpuTileCells; ++ty) {
                for (std::size_t tx = window.colBegin / kGpuTileCells;
                     tx <= (window.colEnd - 1) / kGpuTileCells; ++tx) {
                    std::vector<std::uint32_t>& list = lists[ty * tilesX + tx];
                    if (list.empty() || list.back() != n) {
                        list.push_back(static_cast<std::uint32_t>(n));
                    }
                }
            }
        }
    }
    std::vector<std::uint32_t> tiles{static_cast<std::uint32_t>(tilesX),
                                     static_cast<std::uint32_t>(kGpuTileCells)};
    std::vector<std::uint32_t> entries;
    for (const std::vector<std::uint32_t>& list : lists) {
        tiles.push_back(static_cast<std::uint32_t>(entries.size()));
        tiles.push_back(static_cast<std::uint32_t>(list.size()));
        entries.insert(entries.end(), list.begin(), list.end());
    }
    if (entries.empty()) {
        entries.push_back(0);
    }
    const Bounds2d bounds = RasterGrid::FromRaster(raster).Bounds();
    const Vec2d origin{0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY)};
    for (std::size_t n = 0; n < onRaster.size(); ++n) {
        const Crater& c = prepared[onRaster[n]].crater;
        batch.records[n].x = static_cast<float>(c.x - origin.x);
        batch.records[n].y = static_cast<float>(c.y - origin.y);
    }
    if (batch.data.empty()) {
        batch.data.push_back(0.0f);
    }
    const std::array<KernelBuffer, 4> extra{
        KernelBuffer{std::as_bytes(std::span(batch.records)), batch.records.size()},
        KernelBuffer{std::as_bytes(std::span(batch.data)), batch.data.size()},
        KernelBuffer{std::as_bytes(std::span(tiles)), tiles.size()},
        KernelBuffer{std::as_bytes(std::span(entries)), entries.size()}};
    if (!RunKernel(backend, CompiledKernel<CraterShapeKernel>(),
                   std::as_bytes(std::span(&kernel, 1)), raster, origin, extra)) {
        return false;
    }
    LogDebug("craters", "Shaped ", onRaster.size(), " craters on ", BackendName(backend));
    return true;
}

}  // namespace

double CraterReachRadii(const CraterProfile& profile, const CraterApplyOptions& options) {
    const double reach = profile.ReachRadii();
    return options.texture.ReachesBeyondRim() ? std::fmax(reach, options.texture.ejectaReachRadii)
                                              : reach;
}

float ApplyCraterTo(float height, double x, double y, const PreparedCrater& crater,
                    const CraterProfile& profile, const CraterApplyOptions& options) {
    if (const std::optional<CraterShapeKernel> kernel = MakeCraterShapeKernel(profile, options)) {
        const CraterKernelBatch batch =
            MakeCraterKernelBatch(*kernel, profile, {crater}, {0}, options);
        return ApplyCraterKernel(*kernel, batch.Data(), batch.records[0], height,
                                 static_cast<float>(x - crater.crater.x),
                                 static_cast<float>(y - crater.crater.y));
    }
    const double diameter = crater.crater.diameter;
    const double radius = 0.5 * diameter;
    const Vec2d offset = options.surface.Offset({crater.crater.x, crater.crater.y}, {x, y});
    const double distance =
        CraterProfileDistance(crater.crater, crater.outline, x, y, options.surface);
    if (distance >= CraterReachRadii(profile, options) * radius) {
        return height;
    }
    const double radii = distance / radius;
    const double current = height;
    double replaced = 0.0;
    if (crater.datum) {
        double weight = options.interiorReplacement * options.blend.Weight(radii);
        if (options.texture.mantling > 0.0 && radii > 1.0) {
            weight = std::fmax(weight,
                               options.texture.mantling *
                                   EjectaThicknessShare(radii, options.texture.ejectaReachRadii));
        }
        replaced = weight * (crater.datum->At(offset.x, offset.y) - current);
    }
    const double shaped =
        ProfileHeight(profile, distance, diameter, crater.state, options.rimSmoothing) +
        NoiseHeight(crater.crater, offset, radii, profile.ReachRadii(), options.noise) +
        TextureHeight(crater.crater, crater.state, offset, radii, options.texture);
    return static_cast<float>(current + crater.resolution * shaped + crater.resolution * replaced);
}

void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options, const CraterSampling& sampling) {
    ValidateCraterApplyOptions(options);
    RequireNonNegative(sampling.spacing, "Sample spacing");
    if (raster.Empty() || craters.Empty()) {
        return;
    }
    const Stopwatch stopwatch;
    const double resolved = ResolvedSpacing(raster, sampling.spacing, options.surface);
    const CraterProfile& profile = *models.profile;
    std::vector<PreparedCrater> prepared =
        PrepareCraters(craters, options, resolved, *models.degradation, sampling.reshape);
    std::vector<std::size_t> onRaster;
    std::vector<std::vector<PixelWindow>> windows;
    for (std::size_t k = 0; k < prepared.size(); ++k) {
        std::vector<PixelWindow> around = PixelWindowsIn(
            raster, Footprint(prepared[k], CraterReachRadii(profile, options), options.surface),
            options.surface);
        if (!around.empty()) {
            onRaster.push_back(k);
            windows.push_back(std::move(around));
        }
    }
    const std::optional<CraterShapeKernel> kernel = MakeCraterShapeKernel(profile, options);
    if (options.datum != CraterDatum::None && !onRaster.empty()) {
        FitDatums(prepared, onRaster, raster, profile, kernel, options, sampling);
    }
    if (kernel) {
        CraterKernelBatch batch =
            MakeCraterKernelBatch(*kernel, profile, prepared, onRaster, options);
        if (!ShapeTilesOnGpu(raster, sampling.backend, *kernel, batch, prepared, onRaster,
                             windows)) {
            ShapeTiles(raster, sampling.threads, *kernel, batch, prepared, onRaster, windows);
        }
        LogInfo("craters", "Applied ", onRaster.size(), " of ", craters.Size(), " craters (",
                craters.Size() - prepared.size(), " unresolved at ", resolved, " m, ",
                prepared.size() - onRaster.size(), " off the raster) in ",
                FormatDuration(stopwatch.Milliseconds()));
        return;
    }
    ForEachFeatureCell(
        raster, sampling.threads, onRaster.size(),
        [&](std::size_t n) -> const std::vector<PixelWindow>& { return windows[n]; },
        [&](float& cell, const FeatureCell& at) {
            cell = ApplyCraterTo(cell, at.centre.x, at.centre.y, prepared[onRaster[at.feature]],
                                 profile, options);
        });
    LogInfo("craters", "Applied ", onRaster.size(), " of ", craters.Size(), " craters (",
            craters.Size() - prepared.size(), " unresolved at ", resolved, " m, ",
            prepared.size() - onRaster.size(), " off the raster) in ",
            FormatDuration(stopwatch.Milliseconds()));
}

void ApplyCraters(SampleBlock& samples, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options, const CraterSampling& sampling) {
    if (Raster<float>* grid = samples.Grid()) {
        ApplyCraters(*grid, craters, models, options, sampling);
        return;
    }
    ValidateCraterApplyOptions(options);
    RequirePositive(sampling.spacing, "Sample spacing");
    if (options.datum != CraterDatum::None && !sampling.ground) {
        throw std::invalid_argument("Craters with a datum at points need the ground");
    }
    if (samples.Size() == 0 || craters.Empty()) {
        return;
    }
    const CraterProfile& profile = *models.profile;
    std::vector<PreparedCrater> prepared =
        PrepareCraters(craters, options, sampling.spacing, *models.degradation, sampling.reshape);
    const std::vector<ReachingCrater> reaching =
        CratersReaching(samples, prepared, CraterReachRadii(profile, options), options.surface);
    if (options.datum != CraterDatum::None && !reaching.empty()) {
        std::optional<CraterReliefIndex> index;
        const auto fit = [&](const ReachingCrater& crater) {
            if (!index) {
                index.emplace(prepared, profile, MaxOutlineScale(options.irregularity),
                              options.surface);
            }
            const auto k = static_cast<std::size_t>(crater.prepared - prepared.data());
            return FitCraterDatum(crater.prepared->crater, options,
                                  DatumGround(sampling.ground, *index, k));
        };
        for (const ReachingCrater& crater : reaching) {
            if (sampling.cache != nullptr) {
                crater.prepared->datum = *sampling.cache->Get<std::optional<Plane>>(
                    sampling.owner, crater.prepared->crater.id, sampling.spacing,
                    [&] { return fit(crater); });
            } else {
                crater.prepared->datum = fit(crater);
            }
        }
    }
    if (const std::optional<CraterShapeKernel> kernel = MakeCraterShapeKernel(profile, options)) {
        std::vector<std::size_t> indices;
        for (const ReachingCrater& crater : reaching) {
            indices.push_back(static_cast<std::size_t>(crater.prepared - prepared.data()));
        }
        const CraterKernelBatch batch =
            MakeCraterKernelBatch(*kernel, profile, prepared, indices, options);
        const CraterData data = batch.Data();
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            if (samples.IsNoData(i)) {
                continue;
            }
            const Vec2d p = samples.Position(i);
            float height = samples.Height(i);
            for (std::size_t j = 0; j < reaching.size(); ++j) {
                if (options.surface.Contains(reaching[j].footprint, p)) {
                    const Crater& c = reaching[j].prepared->crater;
                    height = ApplyCraterKernel(*kernel, data, batch.records[j], height,
                                               static_cast<float>(p.x - c.x),
                                               static_cast<float>(p.y - c.y));
                }
            }
            samples.Height(i) = height;
        }
        return;
    }
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        const Vec2d p = samples.Position(i);
        for (const ReachingCrater& crater : reaching) {
            if (!samples.IsNoData(i) && crater.Reaches(p, options.surface)) {
                samples.Height(i) =
                    ApplyCraterTo(samples.Height(i), p.x, p.y, *crater.prepared, profile, options);
            }
        }
    }
}

}  // namespace terraingen
