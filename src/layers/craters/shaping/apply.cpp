// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/shaping/apply.h"

#include "gpu/kernel.h"
#include "layers/craters/shaping/crater_kernel.h"
#include "layers/craters/shaping/datum_fit.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/bits.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

/// The box a crater reaching `reachRadii` radii can change, in surface coordinates.
Bounds2d Footprint(const PreparedCrater& prepared, double reachRadii, const Surface& surface) {
    const Crater& crater = prepared.crater;
    return surface.Expand({crater.x, crater.y, crater.x, crater.y},
                          CraterReach(prepared, reachRadii));
}

/// Craters `indices` of `prepared` split into those that replace the terrain beneath them and the
/// others, each in order.
struct ReplacingSplit {
    std::vector<std::size_t> replacing;
    std::vector<std::size_t> others;
};

ReplacingSplit SplitReplacing(const std::vector<PreparedCrater>& prepared,
                              const std::vector<std::size_t>& indices) {
    ReplacingSplit split;
    for (const std::size_t k : indices) {
        (prepared[k].crater.replace ? split.replacing : split.others).push_back(k);
    }
    return split;
}

/// A prepared crater with the box its profile can reach.
struct ReachingCrater {
    PreparedCrater* prepared;
    Bounds2d footprint;
};

/// The craters of `prepared` reaching `reachRadii` radii that reach any of `samples`, in order.
std::vector<ReachingCrater> CratersReaching(const SampleBlock& samples,
                                            std::vector<PreparedCrater>& prepared,
                                            double reachRadii, const Surface& surface) {
    std::vector<ReachingCrater> reaching;
    for (PreparedCrater& crater : prepared) {
        const Crater& c = crater.crater;
        const ReachingCrater candidate{&crater, Footprint(crater, reachRadii, surface)};
        const KernelOutline outline(crater.outline, c.diameter);
        const double reach = reachRadii * 0.5 * c.diameter;
        for (std::size_t i = 0; i < samples.Size(); ++i) {
            const Vec2d p = samples.Position(i);
            if (surface.Contains(candidate.footprint, p) &&
                outline.ProfileDistance(surface.Offset({c.x, c.y}, p)) < reach) {
                reaching.push_back(candidate);
                break;
            }
        }
    }
    return reaching;
}

/// The frames (`KernelFrameOffset`) of craters `indices` of `prepared` on a body; a placeholder
/// on a plane.
std::vector<CraterFrame> MakeCraterFrames(const std::vector<PreparedCrater>& prepared,
                                          const std::vector<std::size_t>& indices,
                                          const Surface& surface) {
    if (!surface.IsBody() || indices.empty()) {
        return {CraterFrame{}};
    }
    std::vector<CraterFrame> frames(indices.size());
    for (std::size_t n = 0; n < indices.size(); ++n) {
        frames[n] = MakeCraterFrame(surface, prepared[indices[n]].crater);
    }
    return frames;
}

/// The height after craters `entries[first]` to `entries[first + count - 1]` at surface point
/// `p`, as the GPU shapes it.
float ShapeAt(const CraterShapeKernel& kernel, const CraterKernelBatch& batch,
              const std::vector<CraterFrame>& frames, const std::uint32_t* entries,
              std::uint32_t first, std::uint32_t count, float height, const Vec2d& p,
              const Surface& surface) {
    const SplitPoint at = MakeSplitPoint(surface, p);
    if (surface.IsBody()) {
        return ShapeCratersOnBody(kernel, batch.Data(), batch.records.data(), frames.data(),
                                  entries, first, count, height, at);
    }
    return ShapeCraters(kernel, batch.Data(), batch.records.data(), entries, first, count, height,
                        at);
}

/// Side of the tiles craters are binned into, in cells.
constexpr std::size_t kTileCells = 16;

/// Each tile's craters as `CraterTileHead` reads them: tiles across, side and 1 on a body, then
/// each tile's first entry and count; and the entries.
struct CraterTiles {
    std::vector<std::uint32_t> heads;
    std::vector<std::uint32_t> entries;
};

/// The tiles of `kTileCells` of `raster` each crater's `windows` cover, craters in order.
CraterTiles TileLists(const Raster<float>& raster,
                      const std::vector<std::vector<PixelWindow>>& windows, bool onBody) {
    const std::size_t tilesX = CeilDiv(raster.Width(), kTileCells);
    const std::size_t tilesY = CeilDiv(raster.Height(), kTileCells);
    std::vector<std::vector<std::uint32_t>> lists(tilesX * tilesY);
    for (std::size_t n = 0; n < windows.size(); ++n) {
        for (const PixelWindow& window : windows[n]) {
            ForEachTileIn(window, kTileCells, kTileCells,
                          [&](std::size_t tx, std::size_t ty, const PixelWindow&) {
                              std::vector<std::uint32_t>& list = lists[ty * tilesX + tx];
                              if (list.empty() || list.back() != n) {
                                  list.push_back(static_cast<std::uint32_t>(n));
                              }
                          });
        }
    }
    CraterTiles tiles;
    tiles.heads = {static_cast<std::uint32_t>(tilesX), static_cast<std::uint32_t>(kTileCells),
                   onBody ? 1u : 0u};
    tiles.entries = FlattenKernelLists(lists, tiles.heads);
    return tiles;
}

/// What the GPU reads of each cell's position beyond `RunKernel`'s float columns and rows: on a
/// plane the remainders of the columns' x then the rows' y, on a body each cell's split position.
struct CellPositions {
    std::vector<float> lows{0.0f};
    std::vector<SplitPoint> points{SplitPoint{}};
};

CellPositions MakeCellPositions(const Raster<float>& raster, const Surface& surface,
                                unsigned threads) {
    CellPositions cells;
    if (surface.IsBody()) {
        cells.points.resize(raster.Size());
        ParallelFor(raster.Height(), threads, [&](std::size_t row) {
            for (std::size_t col = 0; col < raster.Width(); ++col) {
                cells.points[row * raster.Width() + col] =
                    MakeSplitPoint(surface, raster.CellCenter(col, row));
            }
        });
        return cells;
    }
    cells.lows.clear();
    for (std::size_t col = 0; col < raster.Width(); ++col) {
        cells.lows.push_back(MakeSplitPoint(surface, raster.CellCenter(col, 0)).xLow);
    }
    for (std::size_t row = 0; row < raster.Height(); ++row) {
        cells.lows.push_back(MakeSplitPoint(surface, raster.CellCenter(0, row)).yLow);
    }
    return cells;
}

/// Shapes `raster` on GPU `backend`; false, leaving it unchanged, when the backend is the CPU or
/// cannot take the grid (`RunKernel`).
bool ShapeTilesOnGpu(Raster<float>& raster, Backend backend, unsigned threads,
                     const CraterShapeKernel& kernel, const CraterKernelBatch& batch,
                     const std::vector<CraterFrame>& frames, const CraterTiles& tiles,
                     const Surface& surface) {
    if (backend == Backend::Cpu) {
        return false;
    }
    const CellPositions cells = MakeCellPositions(raster, surface, threads);
    const std::array<KernelBuffer, 7> extra{
        MakeKernelBuffer(batch.records),
        MakeKernelBuffer(batch.data),
        MakeKernelBuffer(tiles.heads),
        MakeKernelBuffer(tiles.entries),
        MakeKernelBuffer(frames),
        MakeKernelBuffer(cells.points, surface.IsBody() ? std::size_t{1} : std::size_t{0}),
        MakeKernelBuffer(cells.lows)};
    if (!RunKernel(backend, CompiledKernel<CraterShapeKernel>(), AsBytes(Span(&kernel, 1)), raster,
                   Vec2d{}, extra)) {
        return false;
    }
    LogDebug("craters", "Shaped ", batch.records.size(), " craters on ", BackendName(backend));
    return true;
}

/// Shapes `raster` on `threads` threads, each tile of cells running its craters as the GPU does.
void ShapeTilesOnCpu(Raster<float>& raster, unsigned threads, const CraterShapeKernel& kernel,
                     const CraterKernelBatch& batch, const std::vector<CraterFrame>& frames,
                     const CraterTiles& tiles, const Surface& surface) {
    ForEachTile(
        raster.Width(), raster.Height(), kTileCells, threads, [&](const PixelWindow& cells) {
            const std::uint32_t head =
                CraterTileHead(tiles.heads.data(), static_cast<std::uint32_t>(cells.colBegin),
                               static_cast<std::uint32_t>(cells.rowBegin));
            const std::uint32_t first = tiles.heads[head];
            const std::uint32_t count = tiles.heads[head + 1];
            if (count == 0) {
                return;
            }
            for (std::size_t row = cells.rowBegin; row < cells.rowEnd; ++row) {
                for (std::size_t col = cells.colBegin; col < cells.colEnd; ++col) {
                    float& cell = raster(col, row);
                    if (!raster.IsNoData(cell)) {
                        cell = ShapeAt(kernel, batch, frames, tiles.entries.data(), first, count,
                                       cell, raster.CellCenter(col, row), surface);
                    }
                }
            }
        });
}

/// The craters of `prepared` whose footprints reach `raster`, with the windows of it they cover.
std::vector<std::size_t> CratersOnRaster(const Raster<float>& raster,
                                         const std::vector<PreparedCrater>& prepared,
                                         double reachRadii, const Surface& surface,
                                         std::vector<std::vector<PixelWindow>>& windows) {
    const RasterGrid grid = raster.Grid();
    std::vector<std::size_t> onRaster;
    for (std::size_t k = 0; k < prepared.size(); ++k) {
        const Bounds2d box = Footprint(prepared[k], reachRadii, surface);
        std::vector<PixelWindow> around = PixelWindowsIn(grid, box, surface);
        if (!around.empty()) {
            onRaster.push_back(k);
            windows.push_back(std::move(around));
        }
    }
    return onRaster;
}

/// Shapes the craters `reaching`, craters `indices` of `prepared` whose records `batch` holds, into
/// the points of `samples`, each point running those whose footprint holds it, in order.
void ShapePoints(SampleBlock& samples, const std::vector<ReachingCrater>& reaching,
                 const CraterShapeKernel& kernel, const CraterKernelBatch& batch,
                 const std::vector<PreparedCrater>& prepared,
                 const std::vector<std::size_t>& indices, const Surface& surface) {
    std::vector<std::vector<std::uint32_t>> lists(samples.Size());
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        const Vec2d p = samples.Position(i);
        for (std::size_t j = 0; j < reaching.size(); ++j) {
            if (surface.Contains(reaching[j].footprint, p)) {
                lists[i].push_back(static_cast<std::uint32_t>(j));
            }
        }
    }
    std::vector<std::uint32_t> heads;
    const std::vector<std::uint32_t> entries = FlattenKernelLists(lists, heads);
    const std::vector<CraterFrame> frames = MakeCraterFrames(prepared, indices, surface);
    for (std::size_t i = 0; i < samples.Size(); ++i) {
        if (!samples.IsNoData(i) && heads[2 * i + 1] > 0) {
            samples.Height(i) =
                ShapeAt(kernel, batch, frames, entries.data(), heads[2 * i], heads[2 * i + 1],
                        samples.Height(i), samples.Position(i), surface);
        }
    }
}

/// The prepared craters point queries reuse: those of `sampling`'s owner kept for its cache, when
/// it has both.
PreparedReuse PointReuse(const CraterSampling& sampling) {
    if (sampling.cache == nullptr || sampling.owner == nullptr) {
        return {};
    }
    return {sampling.cache->Id(), sampling.owner};
}

/// Shapes the craters reaching `samples` at their points, with datums fitted, or with `cached`
/// taken from `sampling.cache`; false, leaving the samples as they are, when a datum is not held.
bool ApplyAtPoints(SampleBlock& samples, const CraterList& craters, const CraterShapeModels& models,
                   const CraterApplyOptions& options, const CraterSampling& sampling, bool cached) {
    const CraterProfile& profile = *models.profile;
    std::vector<PreparedCrater> prepared =
        PrepareCraters(craters, options, sampling.spacing, models, sampling.threads,
                       sampling.reshape, PointReuse(sampling));
    const std::vector<ReachingCrater> reaching =
        CratersReaching(samples, prepared, CraterReachRadii(profile, options), options.surface);
    const CraterShapeKernel kernel = MakeCraterShapeKernel(profile, options);
    std::vector<std::size_t> indices;
    for (const ReachingCrater& crater : reaching) {
        indices.push_back(static_cast<std::size_t>(crater.prepared - prepared.data()));
    }
    const ReplacingSplit split = SplitReplacing(prepared, indices);
    if (!split.replacing.empty() && !sampling.ground) {
        throw std::invalid_argument("Replacing craters at points need the ground");
    }
    if (cached) {
        if (!UseCachedDatums(prepared, indices, models, options, sampling)) {
            return false;
        }
    } else {
        if (options.datum != CraterDatum::None && !split.others.empty()) {
            FitPointDatums(prepared, split.others, models, kernel, options, sampling);
        }
        FitReplacementDatums(prepared, split.replacing, nullptr, models, kernel, options, sampling);
    }
    const CraterKernelBatch batch =
        MakeCraterKernelBatch(kernel, profile, prepared, indices, options);
    ShapePoints(samples, reaching, kernel, batch, prepared, indices, options.surface);
    return true;
}

}  // namespace

CraterSampling SamplingFrom(const LayerContext& context, double spacing, const void* owner) {
    CraterSampling sampling{spacing, context.threads};
    if (context.ground != nullptr) {
        const Ground* ground = context.ground;
        sampling.ground = [ground, spacing](double x, double y) {
            return ground->HeightAt(x, y, spacing);
        };
        sampling.groundBatch = [ground, spacing, threads = context.threads](
                                   Span<const Vec2d> points, Span<float> heights) {
            ground->HeightsAt(points, heights, spacing, threads);
        };
    }
    sampling.cache = context.cache;
    sampling.owner = owner;
    sampling.backend = context.backend;
    return sampling;
}

double CraterReachRadii(const CraterProfile& profile, const CraterApplyOptions& options) {
    const double reach = profile.ReachRadii();
    return options.texture.ReachesBeyondRim() ? std::fmax(reach, options.texture.ejectaReachRadii)
                                              : reach;
}

void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterShapeModels& models,
                  const CraterApplyOptions& options, const CraterSampling& sampling) {
    ValidateCraterApplyOptions(options);
    RequireNonNegative(sampling.spacing, "Sample spacing");
    if (raster.Empty() || craters.Empty()) {
        return;
    }
    const Stopwatch stopwatch;
    const double resolved = ResolvedSpacing(raster.Grid(), sampling.spacing, options.surface);
    const CraterProfile& profile = *models.profile;
    std::vector<PreparedCrater> prepared =
        PrepareCraters(craters, options, resolved, models, sampling.threads, sampling.reshape);
    std::vector<std::vector<PixelWindow>> windows;
    const std::vector<std::size_t> onRaster = CratersOnRaster(
        raster, prepared, CraterReachRadii(profile, options), options.surface, windows);
    const CraterShapeKernel kernel = MakeCraterShapeKernel(profile, options);
    const ReplacingSplit split = SplitReplacing(prepared, onRaster);
    if (options.datum != CraterDatum::None && !split.others.empty()) {
        FitGridDatums(prepared, split.others, raster, models, kernel, options, sampling);
    }
    FitReplacementDatums(prepared, split.replacing, &raster, models, kernel, options, sampling);
    if (!onRaster.empty()) {
        const CraterKernelBatch batch =
            MakeCraterKernelBatch(kernel, profile, prepared, onRaster, options);
        const std::vector<CraterFrame> frames =
            MakeCraterFrames(prepared, onRaster, options.surface);
        const CraterTiles tiles = TileLists(raster, windows, options.surface.IsBody());
        if (!ShapeTilesOnGpu(raster, sampling.backend, sampling.threads, kernel, batch, frames,
                             tiles, options.surface)) {
            ShapeTilesOnCpu(raster, sampling.threads, kernel, batch, frames, tiles,
                            options.surface);
        }
    }
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
    ApplyAtPoints(samples, craters, models, options, sampling, false);
}

bool ApplyCratersOnCachedDatums(SampleBlock& samples, const CraterList& craters,
                                const CraterShapeModels& models, const CraterApplyOptions& options,
                                const CraterSampling& sampling) {
    if (samples.Grid() != nullptr || options.datum == CraterDatum::None ||
        sampling.cache == nullptr || !sampling.ground) {
        return false;
    }
    ValidateCraterApplyOptions(options);
    RequirePositive(sampling.spacing, "Sample spacing");
    if (samples.Size() == 0 || craters.Empty()) {
        return true;
    }
    return ApplyAtPoints(samples, craters, models, options, sampling, true);
}

}  // namespace terraingen
