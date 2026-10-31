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

/// Shapes the craters `onRaster` of `prepared`, reaching `boxes`, into the cells of `raster`
/// holding data with the kernel, on `threads` threads, every cell taking them in order
/// (`ForEachFeatureCell`).
void ShapeCells(Raster<float>& raster, unsigned threads, const CraterShapeKernel& kernel,
                const CraterKernelBatch& batch, const std::vector<PreparedCrater>& prepared,
                const std::vector<std::size_t>& onRaster, const std::vector<Bounds2d>& boxes,
                const Surface& surface) {
    const CraterData data = batch.Data();
    ForEachFeatureCell(raster, threads, boxes, surface, [&](float& cell, const FeatureCell& at) {
        const Crater& crater = prepared[onRaster[at.feature]].crater;
        const KernelOffset offset = ShapingOffset(surface, crater, at.center);
        cell =
            ApplyCraterKernel(kernel, data, batch.records[at.feature], cell, offset.dx, offset.dy);
    });
}

/// Side of the tiles craters are binned into on a GPU, in cells.
constexpr std::size_t kGpuTileCells = 16;

/// The craters of each tile of `kGpuTileCells`, `tilesX` across, as the GPU kernel reads them:
/// tiles across and side, 1 on a body, then each tile's first entry and count; and the entries.
std::pair<std::vector<std::uint32_t>, std::vector<std::uint32_t>>
GpuTileLists(const Raster<float>& raster, const std::vector<std::vector<PixelWindow>>& windows,
             bool onBody) {
    const std::size_t tilesX = CeilDiv(raster.Width(), kGpuTileCells);
    const std::size_t tilesY = CeilDiv(raster.Height(), kGpuTileCells);
    std::vector<std::vector<std::uint32_t>> lists(tilesX * tilesY);
    for (std::size_t n = 0; n < windows.size(); ++n) {
        for (const PixelWindow& window : windows[n]) {
            ForEachTileIn(window, kGpuTileCells, kGpuTileCells,
                          [&](std::size_t tx, std::size_t ty, const PixelWindow&) {
                              std::vector<std::uint32_t>& list = lists[ty * tilesX + tx];
                              if (list.empty() || list.back() != n) {
                                  list.push_back(static_cast<std::uint32_t>(n));
                              }
                          });
        }
    }
    std::vector<std::uint32_t> tiles{static_cast<std::uint32_t>(tilesX),
                                     static_cast<std::uint32_t>(kGpuTileCells), onBody ? 1u : 0u};
    std::vector<std::uint32_t> entries = FlattenKernelLists(lists, tiles);
    return {std::move(tiles), std::move(entries)};
}

/// Where the GPU reads crater offsets on a body: each crater's frame, and each cell's position as
/// x, y, z triples row by row, both from one origin; a placeholder of each on a plane.
struct GpuFrames {
    std::vector<CraterFrame> frames{CraterFrame{}};
    std::vector<float> positions{0.0f};
};

/// The frames of craters `onRaster` of `prepared` and the positions of the cells of `raster` on
/// body `surface`, measured from the position of surface point `center`, on `threads` threads.
GpuFrames BodyFrames(const Raster<float>& raster, const std::vector<PreparedCrater>& prepared,
                     const std::vector<std::size_t>& onRaster, const Surface& surface,
                     const Vec2d& center, unsigned threads) {
    const Vec3d origin = surface.Position(center);
    GpuFrames body;
    body.frames.resize(onRaster.size());
    for (std::size_t n = 0; n < onRaster.size(); ++n) {
        body.frames[n] = MakeCraterFrame(surface, prepared[onRaster[n]].crater, origin);
    }
    body.positions.resize(3 * raster.Size());
    ParallelFor(raster.Height(), threads, [&](std::size_t row) {
        for (std::size_t col = 0; col < raster.Width(); ++col) {
            StoreKernelPosition(surface.Position(raster.CellCenter(col, row)) - origin,
                                body.positions.data() + 3 * (row * raster.Width() + col));
        }
    });
    return body;
}

/// Shapes the craters `onRaster` of `prepared` into `raster` on GPU `backend`, each cell running
/// the craters of its tile in order; false, leaving `raster` unchanged, when the backend is the
/// CPU or cannot take the grid (`RunKernel`). On a plane the GPU reads offsets from per-column and
/// per-row positions; on a body from cell positions and crater frames, set on `threads` threads.
bool ShapeTilesOnGpu(Raster<float>& raster, Backend backend, unsigned threads,
                     const CraterShapeKernel& kernel, CraterKernelBatch& batch,
                     const std::vector<PreparedCrater>& prepared,
                     const std::vector<std::size_t>& onRaster,
                     const std::vector<std::vector<PixelWindow>>& windows, const Surface& surface) {
    if (backend == Backend::Cpu || batch.records.empty()) {
        return false;
    }
    const bool onBody = surface.IsBody();
    const auto [tiles, entries] = GpuTileLists(raster, windows, onBody);
    const Bounds2d bounds = raster.Grid().Bounds();
    const Vec2d origin{0.5 * (bounds.minX + bounds.maxX), 0.5 * (bounds.minY + bounds.maxY)};
    GpuFrames body;
    if (onBody) {
        body = BodyFrames(raster, prepared, onRaster, surface, origin, threads);
    } else {
        for (std::size_t n = 0; n < onRaster.size(); ++n) {
            const Crater& c = prepared[onRaster[n]].crater;
            batch.records[n].x = static_cast<float>(c.x - origin.x);
            batch.records[n].y = static_cast<float>(c.y - origin.y);
        }
    }
    const std::array<KernelBuffer, 6> extra{
        MakeKernelBuffer(batch.records),
        MakeKernelBuffer(batch.data),
        MakeKernelBuffer(tiles),
        MakeKernelBuffer(entries),
        MakeKernelBuffer(body.frames),
        MakeKernelBuffer(body.positions, onBody ? std::size_t{3} : std::size_t{0})};
    if (!RunKernel(backend, CompiledKernel<CraterShapeKernel>(), AsBytes(Span(&kernel, 1)), raster,
                   origin, extra)) {
        return false;
    }
    LogDebug("craters", "Shaped ", onRaster.size(), " craters on ", BackendName(backend));
    return true;
}

/// The craters of `prepared` whose footprints reach `raster`, with their footprints and the windows
/// of it they cover.
std::vector<std::size_t> CratersOnRaster(const Raster<float>& raster,
                                         const std::vector<PreparedCrater>& prepared,
                                         double reachRadii, const Surface& surface,
                                         std::vector<Bounds2d>& boxes,
                                         std::vector<std::vector<PixelWindow>>& windows) {
    const RasterGrid grid = raster.Grid();
    std::vector<std::size_t> onRaster;
    for (std::size_t k = 0; k < prepared.size(); ++k) {
        const Bounds2d box = Footprint(prepared[k], reachRadii, surface);
        std::vector<PixelWindow> around = PixelWindowsIn(grid, box, surface);
        if (!around.empty()) {
            onRaster.push_back(k);
            boxes.push_back(box);
            windows.push_back(std::move(around));
        }
    }
    return onRaster;
}

/// Shapes the craters `reaching` into the points of `samples`, each point taking them in order.
void ShapePoints(SampleBlock& samples, const std::vector<ReachingCrater>& reaching,
                 const CraterShapeKernel& kernel, const CraterKernelBatch& batch,
                 const Surface& surface) {
    const CraterData data = batch.Data();
    MapPoints(samples, [&](float height, const Vec2d& p) {
        for (std::size_t j = 0; j < reaching.size(); ++j) {
            if (surface.Contains(reaching[j].footprint, p)) {
                const KernelOffset at = ShapingOffset(surface, reaching[j].prepared->crater, p);
                height = ApplyCraterKernel(kernel, data, batch.records[j], height, at.dx, at.dy);
            }
        }
        return height;
    });
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
    ShapePoints(samples, reaching, kernel, batch, options.surface);
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
    std::vector<Bounds2d> boxes;
    std::vector<std::vector<PixelWindow>> windows;
    const std::vector<std::size_t> onRaster = CratersOnRaster(
        raster, prepared, CraterReachRadii(profile, options), options.surface, boxes, windows);
    const CraterShapeKernel kernel = MakeCraterShapeKernel(profile, options);
    const ReplacingSplit split = SplitReplacing(prepared, onRaster);
    if (options.datum != CraterDatum::None && !split.others.empty()) {
        FitGridDatums(prepared, split.others, raster, models, kernel, options, sampling);
    }
    FitReplacementDatums(prepared, split.replacing, &raster, models, kernel, options, sampling);
    CraterKernelBatch batch = MakeCraterKernelBatch(kernel, profile, prepared, onRaster, options);
    if (!ShapeTilesOnGpu(raster, sampling.backend, sampling.threads, kernel, batch, prepared,
                         onRaster, windows, options.surface)) {
        ShapeCells(raster, sampling.threads, kernel, batch, prepared, onRaster, boxes,
                   options.surface);
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
