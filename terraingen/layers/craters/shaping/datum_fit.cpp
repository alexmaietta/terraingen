// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/shaping/datum_fit.h"

#include "terraingen/gpu/kernel.h"
#include "terraingen/layers/craters/band_buckets.h"
#include "terraingen/layers/craters/shaping/resolved_state.h"
#include "terraingen/raster/resample.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace terraingen {

namespace {

/// The size band of `crater`: its diameter stretched by its outline, as a power of two.
int ReliefBand(const PreparedCrater& crater) {
    return std::ilogb(crater.crater.diameter * crater.outline.MaxScale());
}

/// The datum fit footprint of a crater, in meters from its center.
double FitRadius(const Crater& crater, const CraterApplyOptions& options) {
    return options.datumFitRadii * 0.5 * crater.diameter;
}

/// The surface points of the datum lattices of some craters, a row of `kDatumLatticePoints` per
/// crater, with `inside` 1 for the points inside the fit footprint.
struct DatumLattice {
    std::vector<Vec2d> points;
    std::vector<std::uint8_t> inside;
};

/// The datum lattices of craters `fitted` of `prepared`, located on `threads` threads.
DatumLattice LocateDatumLattice(const std::vector<PreparedCrater>& prepared,
                                const std::vector<std::size_t>& fitted,
                                const CraterApplyOptions& options, unsigned threads) {
    DatumLattice lattice{std::vector<Vec2d>(kDatumLatticePoints * fitted.size()),
                         std::vector<std::uint8_t>(kDatumLatticePoints * fitted.size(), 0)};
    ParallelFor(fitted.size(), threads, [&](std::size_t n) {
        const Crater& c = prepared[fitted[n]].crater;
        ForEachDatumPoint(FitRadius(c, options), [&](std::size_t point, double dx, double dy) {
            const std::size_t at = n * kDatumLatticePoints + point;
            lattice.points[at] = options.surface.Locate({c.x, c.y}, {dx, dy});
            lattice.inside[at] = 1;
        });
    });
    return lattice;
}

/// The ground under `lattice`, a row per crater, read in one pass by `batchGround` when given,
/// else by `ground` on `threads` threads; NaN, the no-data value, outside the footprint and where
/// the ground has no data.
Raster<float> ReadGround(const DatumLattice& lattice, const HeightSampler& ground,
                         const HeightsSampler& batchGround, unsigned threads) {
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    Raster<float> heights(kDatumLatticePoints, lattice.points.size() / kDatumLatticePoints, kNaN);
    heights.SetNoData(kNaN);
    if (batchGround) {
        std::vector<Vec2d> read;
        std::vector<std::size_t> slots;
        for (std::size_t at = 0; at < lattice.points.size(); ++at) {
            if (lattice.inside[at] != 0) {
                read.push_back(lattice.points[at]);
                slots.push_back(at);
            }
        }
        std::vector<float> values(read.size());
        batchGround(read, values);
        for (std::size_t i = 0; i < read.size(); ++i) {
            heights.Values()[slots[i]] = values[i];
        }
        return heights;
    }
    ParallelFor(heights.Height(), threads, [&](std::size_t row) {
        for (std::size_t at = row * kDatumLatticePoints; at < (row + 1) * kDatumLatticePoints;
             ++at) {
            if (lattice.inside[at] != 0) {
                heights.Values()[at] = ground(lattice.points[at].x, lattice.points[at].y);
            }
        }
    });
    return heights;
}

/// 1 for each point of `ground` holding data, 0 elsewhere.
std::vector<std::uint8_t> ValidPoints(const Raster<float>& ground) {
    std::vector<std::uint8_t> valid(ground.Size());
    for (std::size_t at = 0; at < ground.Size(); ++at) {
        valid[at] = std::isnan(ground.Values()[at]) ? 0 : 1;
    }
    return valid;
}

/// Fits the datums of craters `fitted` of `prepared` to their rows of `lattice`.
void SolveDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
                 const Raster<float>& lattice, const std::vector<std::uint8_t>& valid,
                 const CraterApplyOptions& options, unsigned threads) {
    ParallelFor(fitted.size(), threads, [&](std::size_t n) {
        PlaneFit fit;
        const Crater& c = prepared[fitted[n]].crater;
        ForEachDatumPoint(FitRadius(c, options), [&](std::size_t point, double dx, double dy) {
            const std::size_t at = n * kDatumLatticePoints + point;
            if (valid[at] != 0) {
                fit.Add(dx, dy, static_cast<double>(lattice.Values()[at]));
            }
        });
        prepared[fitted[n]].datum = FittedDatum(fit, options.datum);
    });
}

/// Craters indexed for `KernelDatumRelief`, with the kernel that reads them. Positions are the
/// floats nearest the craters' with `lows` what those drop, and cells are absolute, so the relief
/// at a point depends only on the craters reaching it. On a plane only.
struct DatumKernelIndex {
    CraterDatumKernel kernel;
    CraterKernelBatch batch;
    std::vector<float> lows;
    /// Per size band: its cell size, which is its largest reach, and its first cell's column and
    /// row.
    std::vector<float> bandFloats;
    /// Per size band: its cells across and down and its first cell.
    std::vector<std::uint32_t> bandCells;
    /// Per cell: its first entry and its entry count.
    std::vector<std::uint32_t> cells;
    /// Indices into the records, ascending within each cell.
    std::vector<std::uint32_t> entries;
    /// The record of each prepared crater, for the craters indexed.
    std::vector<std::uint32_t> slot;
};

/// Most cells `IndexDatumRelief` lays out, and the largest cell column or row a float holds
/// exactly enough, before datums are left to `ReliefIndex`.
constexpr std::size_t kMaxReliefCells = std::size_t{1} << 24;
constexpr float kMaxReliefCellIndex = 8388608.0f;

/// The box holding the datum footprints of craters `fitted` of `prepared`.
Bounds2d FitRegion(const std::vector<PreparedCrater>& prepared,
                   const std::vector<std::size_t>& fitted, const CraterApplyOptions& options) {
    constexpr double kInfinity = std::numeric_limits<double>::infinity();
    Bounds2d region{kInfinity, kInfinity, -kInfinity, -kInfinity};
    for (const std::size_t k : fitted) {
        const Crater& c = prepared[k].crater;
        const double fitRadius = FitRadius(c, options);
        region = {std::fmin(region.minX, c.x - fitRadius), std::fmin(region.minY, c.y - fitRadius),
                  std::fmax(region.maxX, c.x + fitRadius), std::fmax(region.maxY, c.y + fitRadius)};
    }
    return region;
}

/// The craters of `prepared` up to the last of `fitted` that are fitted or reach their footprints,
/// in order, noting each one's record in `slot`.
std::vector<std::size_t> ReliefCraters(const std::vector<PreparedCrater>& prepared,
                                       const std::vector<std::size_t>& fitted,
                                       const CraterProfile& profile,
                                       const CraterApplyOptions& options,
                                       std::vector<std::uint32_t>& slot) {
    const Bounds2d region = FitRegion(prepared, fitted, options);
    std::vector<bool> isFitted(prepared.size(), false);
    for (const std::size_t k : fitted) {
        isFitted[k] = true;
    }
    const std::size_t last = *std::max_element(fitted.begin(), fitted.end());
    slot.assign(prepared.size(), 0);
    std::vector<std::size_t> kept;
    for (std::size_t n = 0; n <= last; ++n) {
        const PreparedCrater& p = prepared[n];
        const double reach = CraterReach(p, profile.ReachRadii());
        if (isFitted[n] || (p.crater.x + reach > region.minX && p.crater.x - reach < region.maxX &&
                            p.crater.y + reach > region.minY && p.crater.y - reach < region.maxY)) {
            slot[n] = static_cast<std::uint32_t>(kept.size());
            kept.push_back(n);
        }
    }
    return kept;
}

/// Sets each record of `index` to the float nearest its crater's center, with what that drops in
/// `lows`, and groups the records by size band.
std::map<int, std::vector<std::uint32_t>> RoundRecords(const std::vector<PreparedCrater>& prepared,
                                                       const std::vector<std::size_t>& kept,
                                                       DatumKernelIndex& index) {
    index.lows.resize(2 * kept.size());
    std::map<int, std::vector<std::uint32_t>> bands;
    for (std::size_t m = 0; m < kept.size(); ++m) {
        const Crater& c = prepared[kept[m]].crater;
        CraterRecord& record = index.batch.records[m];
        record.x = static_cast<float>(c.x);
        record.y = static_cast<float>(c.y);
        index.lows[2 * m] = static_cast<float>(c.x - static_cast<double>(record.x));
        index.lows[2 * m + 1] = static_cast<float>(c.y - static_cast<double>(record.y));
        bands[ReliefBand(prepared[kept[m]])].push_back(static_cast<std::uint32_t>(m));
    }
    return bands;
}

/// The cells of side `size` that records `members` of `index` fall in: the first one's column
/// and row, and the cells across and down.
struct CellExtent {
    float minI = 0.0f;
    float minJ = 0.0f;
    float maxI = 0.0f;
    float maxJ = 0.0f;

    std::size_t Across() const {
        return static_cast<std::size_t>(maxI - minI) + 1;
    }
    std::size_t Down() const {
        return static_cast<std::size_t>(maxJ - minJ) + 1;
    }
};

CellExtent RecordCells(const DatumKernelIndex& index, float size,
                       const std::vector<std::uint32_t>& members) {
    const float infinity = std::numeric_limits<float>::infinity();
    CellExtent extent{infinity, infinity, -infinity, -infinity};
    for (const std::uint32_t m : members) {
        const float i = std::floor(index.batch.records[m].x / size);
        const float j = std::floor(index.batch.records[m].y / size);
        extent.minI = std::fmin(extent.minI, i);
        extent.minJ = std::fmin(extent.minJ, j);
        extent.maxI = std::fmax(extent.maxI, i);
        extent.maxJ = std::fmax(extent.maxJ, j);
    }
    return extent;
}

/// Lays out `extent`'s cells from cell `first` and sorts records `members` into them.
void FillCells(DatumKernelIndex& index, float size, const CellExtent& extent, std::size_t first,
               const std::vector<std::uint32_t>& members) {
    const std::size_t across = extent.Across();
    const std::size_t count = across * extent.Down();
    index.cells.resize(2 * (first + count), 0);
    std::vector<std::uint32_t> cellIndex(members.size());
    for (std::size_t m = 0; m < members.size(); ++m) {
        const CraterRecord& r = index.batch.records[members[m]];
        const auto i = static_cast<std::size_t>(std::floor(r.x / size) - extent.minI);
        const auto j = static_cast<std::size_t>(std::floor(r.y / size) - extent.minJ);
        cellIndex[m] = static_cast<std::uint32_t>(first + j * across + i);
        ++index.cells[2 * cellIndex[m] + 1];
    }
    auto start = static_cast<std::uint32_t>(index.entries.size());
    for (std::size_t cell = first; cell < first + count; ++cell) {
        index.cells[2 * cell] = start;
        start += index.cells[2 * cell + 1];
    }
    index.entries.resize(start);
    std::vector<std::uint32_t> filled(count, 0);
    for (std::size_t m = 0; m < members.size(); ++m) {
        const std::uint32_t cell = cellIndex[m];
        index.entries[index.cells[2 * cell] + filled[cell - first]++] = members[m];
    }
}

/// Lays out the cells of one size band of `index`, `size` meters across, holding records
/// `members`; false when they spread too far for float cell indices or the cell budget.
bool AddReliefBand(DatumKernelIndex& index, float size, const std::vector<std::uint32_t>& members) {
    const CellExtent extent = RecordCells(index, size, members);
    const std::size_t first = index.cells.size() / 2;
    if (std::fmax(std::fabs(extent.minI), std::fabs(extent.maxI)) >= kMaxReliefCellIndex ||
        std::fmax(std::fabs(extent.minJ), std::fabs(extent.maxJ)) >= kMaxReliefCellIndex ||
        first + extent.Across() * extent.Down() > kMaxReliefCells) {
        return false;
    }
    index.bandFloats.insert(index.bandFloats.end(), {size, extent.minI, extent.minJ});
    index.bandCells.insert(index.bandCells.end(), {static_cast<std::uint32_t>(extent.Across()),
                                                   static_cast<std::uint32_t>(extent.Down()),
                                                   static_cast<std::uint32_t>(first)});
    FillCells(index, size, extent, first, members);
    return true;
}

/// Indexes the craters of `prepared` that can reach the datum footprint of any of `fitted`, in
/// size bands of cells as wide as each band's largest reach; nothing for craters spread too far.
std::optional<DatumKernelIndex> IndexDatumRelief(const std::vector<PreparedCrater>& prepared,
                                                 const std::vector<std::size_t>& fitted,
                                                 const CraterProfile& profile,
                                                 const CraterShapeKernel& shape,
                                                 const CraterApplyOptions& options) {
    DatumKernelIndex index;
    const std::vector<std::size_t> kept =
        ReliefCraters(prepared, fitted, profile, options, index.slot);
    index.batch = MakeCraterKernelBatch(shape, profile, prepared, kept, options);
    const std::map<int, std::vector<std::uint32_t>> bands = RoundRecords(prepared, kept, index);
    for (const auto& [band, members] : bands) {
        const auto size = static_cast<float>(1.0001 * profile.ReachRadii() * std::ldexp(1.0, band));
        if (!AddReliefBand(index, size, members)) {
            return std::nullopt;
        }
    }
    index.kernel.shape = shape;
    index.kernel.fitRadii = static_cast<float>(options.datumFitRadii);
    index.kernel.latticeSide = static_cast<std::uint32_t>(kDatumLatticeSide);
    index.kernel.bandCount = static_cast<std::uint32_t>(bands.size());
    return index;
}

/// Adds to `lattice` the relief of the craters before each of `fitted`, by `KernelDatumRelief`
/// on GPU `backend` when it can take it and on CPU threads otherwise; the backend it ran on.
Backend AddDatumRelief(Raster<float>& lattice, const std::vector<std::uint8_t>& valid,
                       const std::vector<std::size_t>& fitted, const DatumKernelIndex& index,
                       Backend backend, unsigned threads) {
    std::vector<std::uint32_t> targets(fitted.size());
    for (std::size_t n = 0; n < fitted.size(); ++n) {
        targets[n] = index.slot[fitted[n]];
    }
    const CraterKernelBatch& batch = index.batch;
    const std::array<KernelBuffer, 8> extra{
        MakeKernelBuffer(batch.records),   MakeKernelBuffer(batch.data),
        MakeKernelBuffer(index.lows),      MakeKernelBuffer(index.bandFloats),
        MakeKernelBuffer(index.bandCells), MakeKernelBuffer(index.cells),
        MakeKernelBuffer(index.entries),   MakeKernelBuffer(targets)};
    if (RunKernel(backend, CompiledKernel<CraterDatumKernel>(), AsBytes(Span(&index.kernel, 1)),
                  lattice, {0.0, 0.0}, extra)) {
        return backend;
    }
    ParallelFor(fitted.size(), threads, [&](std::size_t n) {
        for (std::size_t point = 0; point < kDatumLatticePoints; ++point) {
            const std::size_t at = n * kDatumLatticePoints + point;
            if (valid[at] != 0) {
                lattice.Values()[at] += KernelDatumRelief(
                    index.kernel, batch.Data(), batch.records.data(), index.lows.data(),
                    index.bandFloats.data(), index.bandCells.data(), index.cells.data(),
                    index.entries.data(), targets[n], static_cast<std::uint32_t>(point));
            }
        }
    });
    return Backend::Cpu;
}

/// Fits the datums of craters `fitted` of `prepared`, all indexed in `index`: the ground under
/// each lattice point read, the relief of the craters before each crater added, and the fit solved
/// in double.
void FitDatumsWithKernel(std::vector<PreparedCrater>& prepared,
                         const std::vector<std::size_t>& fitted, const HeightSampler& ground,
                         const HeightsSampler& batchGround, const DatumKernelIndex& index,
                         const CraterApplyOptions& options, Backend backend, unsigned threads) {
    Raster<float> lattice = ReadGround(LocateDatumLattice(prepared, fitted, options, threads),
                                       ground, batchGround, threads);
    const std::vector<std::uint8_t> valid = ValidPoints(lattice);
    const Backend ran = AddDatumRelief(lattice, valid, fitted, index, backend, threads);
    SolveDatums(prepared, fitted, lattice, valid, options, threads);
    LogDebug("craters", "Fitted ", fitted.size(), " crater datums on ", BackendName(ran));
}

/// Prepared craters bucketed by surface position, to sum with `CraterRelief` the relief of the
/// craters before any one of them at a point, on any surface, each read at its own local-frame
/// offset (`ShapingOffset`). A point sums in an order fixed by the craters' positions, so the
/// relief depends only on the craters reaching it.
class ReliefIndex {
  public:
    /// Indexes `prepared`, which must outlive the index; their datums are not read.
    ReliefIndex(const std::vector<PreparedCrater>& prepared, const CraterShapeKernel& shape,
                const CraterProfile& profile, const CraterApplyOptions& options)
        : prepared_(&prepared), shape_(shape), surface_(options.surface),
          profileReach_(profile.ReachRadii()), buckets_(surface_) {
        std::vector<std::size_t> all(prepared.size());
        for (std::size_t n = 0; n < all.size(); ++n) {
            all[n] = n;
        }
        batch_ = MakeCraterKernelBatch(shape, profile, prepared, all, options);
        std::map<int, std::vector<std::size_t>> members;
        for (std::size_t n = 0; n < prepared.size(); ++n) {
            const int band = ReliefBand(prepared[n]);
            const Crater& crater = prepared[n].crater;
            buckets_.Add(band, profileReach_ * std::ldexp(1.0, band + 1), {crater.x, crater.y}, n);
            members[band].push_back(n);
        }
        for (auto& [band, craters] : members) {
            bands_.push_back(band);
            members_.push_back(std::move(craters));
        }
    }

    ReliefIndex(const ReliefIndex&) = delete;
    ReliefIndex& operator=(const ReliefIndex&) = delete;

    /// The records of every crater, in order.
    const CraterKernelBatch& Batch() const {
        return batch_;
    }

    /// How far (m) crater `n` reaches, stretched by its outline, with a little slack: the bound
    /// `CraterRelief` takes.
    float Bound(std::size_t n) const {
        return static_cast<float>(1.0001 * CraterReach((*prepared_)[n], profileReach_));
    }

    /// Calls `visit(n)` for each crater n before crater `k` whose center may lie within `radius`
    /// meters plus its reach of surface `point`, and perhaps for others before `k`, in no fixed
    /// order.
    template <typename Visit>
    void ForEachNear(const Vec2d& point, double radius, std::size_t k, const Visit& visit) const {
        const auto before = [&](const std::vector<std::size_t>& craters) {
            for (const std::size_t n : craters) {
                if (n >= k) {
                    break;
                }
                visit(n);
            }
        };
        for (std::size_t b = 0; b < bands_.size(); ++b) {
            const auto span =
                static_cast<long long>(std::ceil(radius / buckets_.Size(bands_[b]))) + 1;
            const long long depth = surface_.IsBody() ? span : 0;
            const auto buckets = static_cast<double>((2 * span + 1) * (2 * span + 1)) *
                                 static_cast<double>(2 * depth + 1);
            if (buckets >= static_cast<double>(members_[b].size())) {
                before(members_[b]);
            } else {
                buckets_.ForEachAround(bands_[b], point, span, before);
            }
        }
    }

    /// The relief in meters at surface `point` of the craters before crater `k`.
    float ReliefBefore(const Vec2d& point, std::size_t k) const {
        float relief = 0.0f;
        const CraterData data = batch_.Data();
        for (const int band : bands_) {
            buckets_.ForEachAround(band, point, 1, [&](const std::vector<std::size_t>& bucket) {
                for (const std::size_t n : bucket) {
                    if (n >= k) {
                        break;
                    }
                    const KernelOffset offset =
                        ShapingOffset(surface_, (*prepared_)[n].crater, point);
                    relief += CraterRelief(shape_, data, batch_.records[n], Bound(n), offset.dx,
                                           offset.dy);
                }
            });
        }
        return relief;
    }

  private:
    const std::vector<PreparedCrater>* prepared_;
    CraterShapeKernel shape_;
    Surface surface_;
    double profileReach_ = 1.0;
    CraterKernelBatch batch_;
    BandBuckets buckets_;
    std::vector<int> bands_;
    /// The craters of each of `bands_`, in order.
    std::vector<std::vector<std::size_t>> members_;
};

/// The ground crater `k` fits its datum to: `ground` plus the relief of the craters before it in
/// `index`; NaN where `ground` has no data. Both must outlive the result.
HeightSampler GroundBefore(const HeightSampler& ground, const ReliefIndex& index, std::size_t k) {
    return [&ground, &index, k](double x, double y) {
        const float height = ground(x, y);
        if (std::isnan(height)) {
            return height;
        }
        return static_cast<float>(static_cast<double>(height) +
                                  static_cast<double>(index.ReliefBefore({x, y}, k)));
    };
}

/// How far beyond the sum of their reach and footprint craters are taken as reaching a datum
/// footprint on a body, so local frames, which shorten distances on a tangent plane, miss none.
constexpr double kBodyReachSlack = 1.25;

/// The craters whose relief datums on a body are fitted to, as `KernelBodyDatumRelief` reads
/// them.
struct BodyReliefCraters {
    std::vector<CraterFrame> frames;  ///< Per crater.
    std::vector<float> bounds;        ///< Per crater, `ReliefIndex::Bound`.
    /// Per crater fitted, its first entry and its entry count.
    std::vector<std::uint32_t> lists;
    /// The craters before each crater fitted that can reach its footprint, ascending per crater.
    std::vector<std::uint32_t> entries;
};

/// The craters before crater `k` of `prepared` that can reach its datum footprint on a body,
/// ascending.
std::vector<std::uint32_t> CratersNear(const std::vector<PreparedCrater>& prepared, std::size_t k,
                                       const ReliefIndex& index,
                                       const CraterApplyOptions& options) {
    const Crater& c = prepared[k].crater;
    const double fitRadius = FitRadius(c, options);
    std::vector<std::uint32_t> near;
    index.ForEachNear({c.x, c.y}, kBodyReachSlack * fitRadius, k, [&](std::size_t n) {
        const Crater& other = prepared[n].crater;
        const double reach = static_cast<double>(index.Bound(n)) + fitRadius;
        if (options.surface.Distance({c.x, c.y}, {other.x, other.y}) < kBodyReachSlack * reach) {
            near.push_back(static_cast<std::uint32_t>(n));
        }
    });
    std::sort(near.begin(), near.end());
    return near;
}

/// The craters of `prepared` in `index` as the relief datums of craters `fitted` are fitted to on
/// a body, with frames from `origin` (m), on `threads` threads.
BodyReliefCraters MakeBodyReliefCraters(const std::vector<PreparedCrater>& prepared,
                                        const std::vector<std::size_t>& fitted,
                                        const ReliefIndex& index, const CraterApplyOptions& options,
                                        const Vec3d& origin, unsigned threads) {
    BodyReliefCraters craters;
    craters.frames.resize(prepared.size());
    craters.bounds.resize(prepared.size());
    ParallelFor(prepared.size(), threads, [&](std::size_t n) {
        craters.frames[n] = MakeCraterFrame(options.surface, prepared[n].crater, origin);
        craters.bounds[n] = index.Bound(n);
    });
    std::vector<std::vector<std::uint32_t>> near(fitted.size());
    ParallelFor(fitted.size(), threads,
                [&](std::size_t n) { near[n] = CratersNear(prepared, fitted[n], index, options); });
    craters.entries = FlattenKernelLists(near, craters.lists);
    return craters;
}

/// The positions from `origin` (m) of the points of `lattice` inside the footprint, as x, y, z
/// triples, 0 elsewhere, set on `threads` threads.
std::vector<float> LatticePositions(const DatumLattice& lattice, const Surface& surface,
                                    const Vec3d& origin, unsigned threads) {
    std::vector<float> positions(3 * lattice.points.size(), 0.0f);
    ParallelFor(lattice.points.size() / kDatumLatticePoints, threads, [&](std::size_t row) {
        for (std::size_t at = row * kDatumLatticePoints; at < (row + 1) * kDatumLatticePoints;
             ++at) {
            if (lattice.inside[at] != 0) {
                StoreKernelPosition(surface.Position(lattice.points[at]) - origin,
                                    positions.data() + 3 * at);
            }
        }
    });
    return positions;
}

/// Fits the datums of craters `fitted` of `prepared` on a body with the relief of the craters
/// before each from `index`, added by `KernelBodyDatumRelief` on GPU `sampling.backend`; false,
/// fitting nothing, when the backend is the CPU or cannot take the lattice.
bool FitBodyDatumsOnGpu(std::vector<PreparedCrater>& prepared,
                        const std::vector<std::size_t>& fitted, const HeightSampler& ground,
                        const HeightsSampler& batchGround, const ReliefIndex& index,
                        const CraterShapeKernel& shape, const CraterApplyOptions& options,
                        const CraterSampling& sampling) {
    if (sampling.backend == Backend::Cpu || fitted.empty()) {
        return false;
    }
    const Crater& first = prepared[fitted.front()].crater;
    const Vec3d origin = options.surface.Position({first.x, first.y});
    const DatumLattice points = LocateDatumLattice(prepared, fitted, options, sampling.threads);
    const std::vector<float> positions =
        LatticePositions(points, options.surface, origin, sampling.threads);
    Raster<float> lattice = ReadGround(points, ground, batchGround, sampling.threads);
    const std::vector<std::uint8_t> valid = ValidPoints(lattice);
    const BodyReliefCraters craters =
        MakeBodyReliefCraters(prepared, fitted, index, options, origin, sampling.threads);
    const CraterKernelBatch& batch = index.Batch();
    const CraterBodyDatumKernel kernel{shape};
    const std::array<KernelBuffer, 7> extra{
        MakeKernelBuffer(batch.records),  MakeKernelBuffer(batch.data),
        MakeKernelBuffer(craters.frames), MakeKernelBuffer(craters.bounds),
        MakeKernelBuffer(craters.lists),  MakeKernelBuffer(craters.entries),
        MakeKernelBuffer(positions, 3)};
    if (!RunKernel(sampling.backend, CompiledKernel<CraterBodyDatumKernel>(),
                   AsBytes(Span(&kernel, 1)), lattice, {0.0, 0.0}, extra)) {
        return false;
    }
    SolveDatums(prepared, fitted, lattice, valid, options, sampling.threads);
    LogDebug("craters", "Fitted ", fitted.size(), " crater datums on ",
             BackendName(sampling.backend));
    return true;
}

/// Gives every crater of `prepared` its shape (`SlopeShape`) and wear (`SlopeWearRate`) on the
/// gradient of `ground`, the ground beneath the layer, across its datum footprint; before any
/// datum is fitted, so each crater's slope depends on no other crater of its layer.
void ShapeOnSlopes(std::vector<PreparedCrater>& prepared, const HeightSampler& ground,
                   const HeightsSampler& batch, const CraterShapeModels& models,
                   const CraterApplyOptions& options) {
    const CraterSlopeResponse& response = options.slope;
    if (!response.enabled && !response.criticalGradient) {
        return;
    }
    std::vector<Vec2d> points;
    points.reserve(4 * prepared.size());
    for (const PreparedCrater& crater : prepared) {
        const Crater& c = crater.crater;
        const std::array<Vec2d, 4> around =
            GradientPoints(options.surface, {c.x, c.y}, FitRadius(c, options));
        points.insert(points.end(), around.begin(), around.end());
    }
    std::vector<float> heights(points.size());
    if (batch) {
        batch(points, heights);
    } else {
        for (std::size_t i = 0; i < points.size(); ++i) {
            heights[i] = ground(points[i].x, points[i].y);
        }
    }
    for (std::size_t k = 0; k < prepared.size(); ++k) {
        PreparedCrater& crater = prepared[k];
        const Crater& c = crater.crater;
        const std::optional<Vec2d> gradient = GradientFromHeights(
            Span<const float>(heights.data() + 4 * k, 4), FitRadius(c, options));
        if (!gradient) {
            continue;
        }
        crater.slope = SlopeShape(c, gradient->x, gradient->y, response);
        const double rate = SlopeWearRate(response, gradient->x, gradient->y);
        if (rate > 1.0) {
            crater.state = ResolvedCraterState(models, c, rate);
        }
    }
}

/// Whether datums are fitted with `DatumKernelIndex`: on a plane, for fitted datums.
bool FitsOnKernelIndex(const CraterApplyOptions& options) {
    return !options.surface.IsBody() && options.datum != CraterDatum::CenterHeight;
}

/// Fits the datums of craters `fitted` of `prepared` to `ground`: with the crater datum kernel
/// where it can take them, on a body with the body datum kernel on a GPU, else through
/// `ReliefIndex`.
void FitDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
               const HeightSampler& ground, const HeightsSampler& batchGround,
               const CraterProfile& profile, const CraterShapeKernel& shape,
               const CraterApplyOptions& options, const CraterSampling& sampling) {
    if (FitsOnKernelIndex(options)) {
        if (const std::optional<DatumKernelIndex> index =
                IndexDatumRelief(prepared, fitted, profile, shape, options)) {
            FitDatumsWithKernel(prepared, fitted, ground, batchGround, *index, options,
                                sampling.backend, sampling.threads);
            return;
        }
    }
    const ReliefIndex index(prepared, shape, profile, options);
    if (options.surface.IsBody() && options.datum != CraterDatum::CenterHeight &&
        FitBodyDatumsOnGpu(prepared, fitted, ground, batchGround, index, shape, options,
                           sampling)) {
        return;
    }
    ParallelFor(fitted.size(), sampling.threads, [&](std::size_t n) {
        const std::size_t k = fitted[n];
        prepared[k].datum =
            FitCraterDatum(prepared[k].crater, options, GroundBefore(ground, index, k));
    });
}

}  // namespace

void FitGridDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
                   const Raster<float>& raster, const CraterShapeModels& models,
                   const CraterShapeKernel& kernel, const CraterApplyOptions& options,
                   const CraterSampling& sampling) {
    const HeightSampler ground =
        sampling.ground ? sampling.ground : HeightSampler([&raster](double x, double y) {
            return SampleRaster(raster, x, y, ResampleMethod::Bilinear);
        });
    const HeightsSampler batch = sampling.ground ? sampling.groundBatch : HeightsSampler{};
    ShapeOnSlopes(prepared, ground, batch, models, options);
    FitDatums(prepared, fitted, ground, batch, *models.profile, kernel, options, sampling);
}

void FitPointDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
                    const CraterShapeModels& models, const CraterShapeKernel& kernel,
                    const CraterApplyOptions& options, const CraterSampling& sampling) {
    ShapeOnSlopes(prepared, sampling.ground, sampling.groundBatch, models, options);
    const CraterProfile& profile = *models.profile;
    std::optional<DatumKernelIndex> kernelIndex;
    bool indexed = false;
    std::optional<ReliefIndex> index;
    const auto fit = [&](std::size_t k) -> std::optional<Plane> {
        if (FitsOnKernelIndex(options)) {
            if (!indexed) {
                kernelIndex = IndexDatumRelief(prepared, fitted, profile, kernel, options);
                indexed = true;
            }
            if (kernelIndex) {
                FitDatumsWithKernel(prepared, {k}, sampling.ground, sampling.groundBatch,
                                    *kernelIndex, options, Backend::Cpu, 1);
                return prepared[k].datum;
            }
        }
        if (!index) {
            index.emplace(prepared, kernel, profile, options);
        }
        return FitCraterDatum(prepared[k].crater, options,
                              GroundBefore(sampling.ground, *index, k));
    };
    for (const std::size_t k : fitted) {
        if (sampling.cache != nullptr) {
            prepared[k].datum = *sampling.cache->Get<std::optional<Plane>>(
                sampling.owner, prepared[k].crater.id, sampling.spacing, [&] { return fit(k); });
        } else {
            prepared[k].datum = fit(k);
        }
    }
}

}  // namespace terraingen
