#include "terraingen/layers/craters/shaping/apply.h"

#include "terraingen/gpu/kernel.h"
#include "terraingen/layers/craters/shaping/crater_kernel.h"
#include "terraingen/layers/craters/shaping/explicit_state.h"
#include "terraingen/noise/hash.h"
#include "terraingen/raster/cell_loops.h"
#include "terraingen/raster/grid.h"
#include "terraingen/util/arithmetic.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace terraingen {

namespace {

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
        values[i] = static_cast<double>(cells[i]);
    }
    return (1.0 - fv) * ((1.0 - fu) * values[0] + fu * values[1]) +
           fv * ((1.0 - fu) * values[2] + fu * values[3]);
}

/// The raster's heights as a sampler, NaN where `Sample` gives none; `raster` must outlive it.
HeightSampler RasterSampler(const Raster<float>& raster) {
    return [&raster](double x, double y) {
        return static_cast<float>(
            Sample(raster, x, y).value_or(std::numeric_limits<double>::quiet_NaN()));
    };
}

/// How far (m) from its centre `prepared` reaches `reachRadii` radii out, stretched by its
/// outline.
double CraterReach(const PreparedCrater& prepared, double reachRadii) {
    return prepared.outline.MaxScale() * reachRadii * (0.5 * prepared.crater.diameter);
}

/// The box a crater reaching `reachRadii` radii can change, in surface coordinates.
Bounds2d Footprint(const PreparedCrater& prepared, double reachRadii, const Surface& surface) {
    const Crater& crater = prepared.crater;
    return surface.Expand({crater.x, crater.y, crater.x, crater.y},
                          CraterReach(prepared, reachRadii));
}

/// The size band of `crater`: its diameter stretched by its outline, as a power of two.
int ReliefBand(const PreparedCrater& crater) {
    return std::ilogb(crater.crater.diameter * crater.outline.MaxScale());
}

/// The datum fit footprint of a crater, in metres from its centre.
double FitRadius(const Crater& crater, const CraterApplyOptions& options) {
    return options.datumFitRadii * 0.5 * crater.diameter;
}

/// Craters indexed for `KernelDatumRelief`, with the kernel that reads them.
///
/// Records hold each crater's position as the float nearest it and `lows` what that float drops,
/// so the kernel takes differences between nearby craters exactly; bands and cells are at absolute
/// positions. The relief summed at a point therefore depends only on the craters reaching it, not
/// on which others an index holds, and grids and points fit the same datums. Plane only.
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
        const double reach = p.outline.MaxScale() * profile.ReachRadii() * 0.5 * p.crater.diameter;
        if (isFitted[n] || (p.crater.x + reach > region.minX && p.crater.x - reach < region.maxX &&
                            p.crater.y + reach > region.minY && p.crater.y - reach < region.maxY)) {
            slot[n] = static_cast<std::uint32_t>(kept.size());
            kept.push_back(n);
        }
    }
    return kept;
}

/// Sets each record of `index` to the float nearest its crater's centre, with what that drops in
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

/// Lays out the cells of one size band of `index`, `size` metres across, holding records
/// `members`; false when they spread too far for float cell indices or the cell budget.
bool AddReliefBand(DatumKernelIndex& index, float size, const std::vector<std::uint32_t>& members) {
    float minI = std::numeric_limits<float>::infinity();
    float minJ = minI;
    float maxI = -minI;
    float maxJ = -minI;
    for (const std::uint32_t m : members) {
        const float i = std::floor(index.batch.records[m].x / size);
        const float j = std::floor(index.batch.records[m].y / size);
        minI = std::fmin(minI, i);
        minJ = std::fmin(minJ, j);
        maxI = std::fmax(maxI, i);
        maxJ = std::fmax(maxJ, j);
    }
    const std::size_t first = index.cells.size() / 2;
    const auto across = static_cast<std::size_t>(maxI - minI) + 1;
    const auto down = static_cast<std::size_t>(maxJ - minJ) + 1;
    if (std::fmax(std::fabs(minI), std::fabs(maxI)) >= kMaxReliefCellIndex ||
        std::fmax(std::fabs(minJ), std::fabs(maxJ)) >= kMaxReliefCellIndex ||
        first + across * down > kMaxReliefCells) {
        return false;
    }
    index.bandFloats.insert(index.bandFloats.end(), {size, minI, minJ});
    index.bandCells.insert(index.bandCells.end(),
                           {static_cast<std::uint32_t>(across), static_cast<std::uint32_t>(down),
                            static_cast<std::uint32_t>(first)});
    index.cells.resize(2 * (first + across * down), 0);
    std::vector<std::uint32_t> cellIndex(members.size());
    for (std::size_t m = 0; m < members.size(); ++m) {
        const CraterRecord& r = index.batch.records[members[m]];
        const auto i = static_cast<std::size_t>(std::floor(r.x / size) - minI);
        const auto j = static_cast<std::size_t>(std::floor(r.y / size) - minJ);
        cellIndex[m] = static_cast<std::uint32_t>(first + j * across + i);
        ++index.cells[2 * cellIndex[m] + 1];
    }
    auto start = static_cast<std::uint32_t>(index.entries.size());
    for (std::size_t cell = first; cell < first + across * down; ++cell) {
        index.cells[2 * cell] = start;
        start += index.cells[2 * cell + 1];
    }
    index.entries.resize(start);
    std::vector<std::uint32_t> filled(across * down, 0);
    for (std::size_t m = 0; m < members.size(); ++m) {
        const std::uint32_t cell = cellIndex[m];
        index.entries[index.cells[2 * cell] + filled[cell - first]++] = members[m];
    }
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
    if (index.batch.data.empty()) {
        index.batch.data.push_back(0.0f);
    }
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

/// The ground under the datum lattice of craters `fitted` of `prepared`, a row per crater, read
/// in one pass by `batchGround` when given, else by `ground` on `threads` threads; `valid` marks
/// the points with data.
Raster<float> ReadDatumLattice(const std::vector<PreparedCrater>& prepared,
                               const std::vector<std::size_t>& fitted, const HeightSampler& ground,
                               const HeightsSampler& batchGround, const CraterApplyOptions& options,
                               unsigned threads, std::vector<std::uint8_t>& valid) {
    constexpr auto kPoints = static_cast<std::size_t>(kDatumLatticeSide * kDatumLatticeSide);
    Raster<float> lattice(kPoints, fitted.size(), 0.0f);
    valid.assign(lattice.Size(), 0);
    const auto keep = [&](std::size_t at, float height) {
        if (!std::isnan(height)) {
            lattice.Values()[at] = height;
            valid[at] = 1;
        }
    };
    if (batchGround) {
        std::vector<Vec2d> points;
        std::vector<std::size_t> slots;
        for (std::size_t n = 0; n < fitted.size(); ++n) {
            const Crater& c = prepared[fitted[n]].crater;
            ForEachDatumPoint(FitRadius(c, options), [&](std::size_t point, double dx, double dy) {
                points.push_back({c.x + dx, c.y + dy});
                slots.push_back(n * kPoints + point);
            });
        }
        std::vector<float> heights(points.size());
        batchGround(points, heights);
        for (std::size_t i = 0; i < points.size(); ++i) {
            keep(slots[i], heights[i]);
        }
        return lattice;
    }
    ParallelFor(fitted.size(), threads, [&](std::size_t n) {
        const Crater& c = prepared[fitted[n]].crater;
        ForEachDatumPoint(FitRadius(c, options), [&](std::size_t point, double dx, double dy) {
            keep(n * kPoints + point, ground(c.x + dx, c.y + dy));
        });
    });
    return lattice;
}

/// Adds to `lattice` the relief of the craters before each of `fitted`, by `KernelDatumRelief`
/// on GPU `backend` when it can take it and on CPU threads otherwise; the backend it ran on.
Backend AddDatumRelief(Raster<float>& lattice, const std::vector<std::uint8_t>& valid,
                       const std::vector<std::size_t>& fitted, const DatumKernelIndex& index,
                       Backend backend, unsigned threads) {
    constexpr auto kPoints = static_cast<std::size_t>(kDatumLatticeSide * kDatumLatticeSide);
    std::vector<std::uint32_t> targets(fitted.size());
    for (std::size_t n = 0; n < fitted.size(); ++n) {
        targets[n] = index.slot[fitted[n]];
    }
    const CraterKernelBatch& batch = index.batch;
    const std::array<KernelBuffer, 8> extra{
        KernelBuffer{AsBytes(Span(batch.records)), batch.records.size()},
        KernelBuffer{AsBytes(Span(batch.data)), batch.data.size()},
        KernelBuffer{AsBytes(Span(index.lows)), index.lows.size()},
        KernelBuffer{AsBytes(Span(index.bandFloats)), index.bandFloats.size()},
        KernelBuffer{AsBytes(Span(index.bandCells)), index.bandCells.size()},
        KernelBuffer{AsBytes(Span(index.cells)), index.cells.size()},
        KernelBuffer{AsBytes(Span(index.entries)), index.entries.size()},
        KernelBuffer{AsBytes(Span(targets)), targets.size()}};
    if (RunKernel(backend, CompiledKernel<CraterDatumKernel>(), AsBytes(Span(&index.kernel, 1)),
                  lattice, {0.0, 0.0}, extra)) {
        return backend;
    }
    ParallelFor(fitted.size(), threads, [&](std::size_t n) {
        for (std::size_t point = 0; point < kPoints; ++point) {
            const std::size_t at = n * kPoints + point;
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

/// Fits the datums of craters `fitted` of `prepared` to their rows of `lattice`.
void SolveDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
                 const Raster<float>& lattice, const std::vector<std::uint8_t>& valid,
                 const CraterApplyOptions& options, unsigned threads) {
    constexpr auto kPoints = static_cast<std::size_t>(kDatumLatticeSide * kDatumLatticeSide);
    ParallelFor(fitted.size(), threads, [&](std::size_t n) {
        PlaneFit fit;
        const Crater& c = prepared[fitted[n]].crater;
        ForEachDatumPoint(FitRadius(c, options), [&](std::size_t point, double dx, double dy) {
            const std::size_t at = n * kPoints + point;
            if (valid[at] != 0) {
                fit.Add(dx, dy, static_cast<double>(lattice.Values()[at]));
            }
        });
        std::optional<Plane>& datum = prepared[fitted[n]].datum;
        if (fit.Empty()) {
            datum.reset();
        } else {
            datum = options.datum == CraterDatum::Plane ? fit.Solve() : Plane{fit.Mean(), 0.0, 0.0};
        }
    });
}

/// Fits the datums of craters `fitted` of `prepared`, all indexed in `index`: the ground under
/// each lattice point read, the relief of the craters before each crater added, and the fit solved
/// in double.
void FitDatumsWithKernel(std::vector<PreparedCrater>& prepared,
                         const std::vector<std::size_t>& fitted, const HeightSampler& ground,
                         const HeightsSampler& batchGround, const DatumKernelIndex& index,
                         const CraterApplyOptions& options, Backend backend, unsigned threads) {
    std::vector<std::uint8_t> valid;
    Raster<float> lattice =
        ReadDatumLattice(prepared, fitted, ground, batchGround, options, threads, valid);
    const Backend ran = AddDatumRelief(lattice, valid, fitted, index, backend, threads);
    SolveDatums(prepared, fitted, lattice, valid, options, threads);
    LogDebug("craters", "Fitted ", fitted.size(), " crater datums on ", BackendName(ran));
}

/// Prepared craters bucketed by surface position, to sum with the kernel's `CraterRelief` the
/// relief of the craters before any one of them at a point, on any surface.
///
/// Each crater is read at its own local-frame offset to the point (`ShapingOffset`), as it is
/// shaped. Buckets of a size band are twice its largest reach, and a point sums the craters of its
/// bucket and the ones around it in an order fixed by their positions, so the relief depends only
/// on the craters reaching the point.
class ReliefIndex {
  public:
    /// Indexes `prepared`, which must outlive the index; their datums are not read.
    ReliefIndex(const std::vector<PreparedCrater>& prepared, const CraterShapeKernel& shape,
                const CraterProfile& profile, const CraterApplyOptions& options)
        : prepared_(&prepared), shape_(shape), surface_(options.surface),
          profileReach_(profile.ReachRadii()) {
        std::vector<std::size_t> all(prepared.size());
        for (std::size_t n = 0; n < all.size(); ++n) {
            all[n] = n;
        }
        batch_ = MakeCraterKernelBatch(shape, profile, prepared, all, options);
        std::map<int, std::vector<std::size_t>> members;
        for (std::size_t n = 0; n < prepared.size(); ++n) {
            const int band = ReliefBand(prepared[n]);
            const Crater& crater = prepared[n].crater;
            const BucketIndex bucket = surface_.Bucket({crater.x, crater.y}, BucketSize(band));
            buckets_[{band, bucket.i, bucket.j, bucket.k}].push_back(n);
            members[band].push_back(n);
        }
        for (auto& [band, craters] : members) {
            bands_.push_back(band);
            members_.push_back(std::move(craters));
        }
    }

    /// The records of every crater, in order.
    const CraterKernelBatch& Batch() const {
        return batch_;
    }

    /// How far (m) crater `n` reaches, stretched by its outline, with a little slack: the bound
    /// `CraterRelief` takes.
    float Bound(std::size_t n) const {
        return static_cast<float>(1.0001 * CraterReach((*prepared_)[n], profileReach_));
    }

    /// Calls `visit(n)` for each crater n before crater `k` whose centre may lie within `radius`
    /// metres plus its reach of surface `point`, and perhaps for others before `k`, in no fixed
    /// order.
    template <typename Visit>
    void ForEachNear(const Vec2d& point, double radius, std::size_t k, const Visit& visit) const {
        for (std::size_t b = 0; b < bands_.size(); ++b) {
            const double size = BucketSize(bands_[b]);
            const auto span = static_cast<long long>(std::ceil(radius / size)) + 1;
            const long long depth = surface_.IsBody() ? span : 0;
            const auto buckets = static_cast<double>((2 * span + 1) * (2 * span + 1)) *
                                 static_cast<double>(2 * depth + 1);
            if (buckets >= static_cast<double>(members_[b].size())) {
                for (const std::size_t n : members_[b]) {
                    if (n >= k) {
                        break;
                    }
                    visit(n);
                }
                continue;
            }
            const BucketIndex center = surface_.Bucket(point, size);
            for (long long z = center.k - depth; z <= center.k + depth; ++z) {
                for (long long j = center.j - span; j <= center.j + span; ++j) {
                    for (long long i = center.i - span; i <= center.i + span; ++i) {
                        const auto found = buckets_.find({bands_[b], i, j, z});
                        if (found == buckets_.end()) {
                            continue;
                        }
                        for (const std::size_t n : found->second) {
                            if (n >= k) {
                                break;
                            }
                            visit(n);
                        }
                    }
                }
            }
        }
    }

    /// The relief in metres at surface `point` of the craters before crater `k`.
    float ReliefBefore(const Vec2d& point, std::size_t k) const {
        float relief = 0.0f;
        const long long depth = surface_.IsBody() ? 1 : 0;
        for (const int band : bands_) {
            const BucketIndex center = surface_.Bucket(point, BucketSize(band));
            for (long long j = center.j - 1; j <= center.j + 1; ++j) {
                for (long long i = center.i - 1; i <= center.i + 1; ++i) {
                    for (long long z = center.k - depth; z <= center.k + depth; ++z) {
                        const auto found = buckets_.find({band, i, j, z});
                        if (found != buckets_.end()) {
                            AddBucket(found->second, point, k, relief);
                        }
                    }
                }
            }
        }
        return relief;
    }

  private:
    struct Key {
        int band;
        long long i;
        long long j;
        long long k;
        bool operator==(const Key& other) const {
            return band == other.band && i == other.i && j == other.j && k == other.k;
        }
        bool operator!=(const Key& other) const {
            return !(*this == other);
        }
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const {
            return HashWords({static_cast<std::uint64_t>(key.band),
                              static_cast<std::uint64_t>(key.i), static_cast<std::uint64_t>(key.j),
                              static_cast<std::uint64_t>(key.k)});
        }
    };

    /// Side of the buckets of a size band: twice the largest reach of its craters.
    double BucketSize(int band) const {
        return profileReach_ * std::ldexp(1.0, band + 1);
    }

    void AddBucket(const std::vector<std::size_t>& bucket, const Vec2d& point, std::size_t k,
                   float& relief) const {
        const CraterData data = batch_.Data();
        for (const std::size_t n : bucket) {
            if (n >= k) {
                break;
            }
            const KernelOffset offset = ShapingOffset(surface_, (*prepared_)[n].crater, point);
            relief += CraterRelief(shape_, data, batch_.records[n], Bound(n), offset.dx, offset.dy);
        }
    }

    const std::vector<PreparedCrater>* prepared_;
    CraterShapeKernel shape_;
    Surface surface_;
    double profileReach_ = 1.0;
    CraterKernelBatch batch_;
    std::vector<int> bands_;
    /// The craters of each of `bands_`, in order.
    std::vector<std::vector<std::size_t>> members_;
    std::unordered_map<Key, std::vector<std::size_t>, KeyHash> buckets_;
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
    for (const std::vector<std::uint32_t>& list : near) {
        craters.lists.push_back(static_cast<std::uint32_t>(craters.entries.size()));
        craters.lists.push_back(static_cast<std::uint32_t>(list.size()));
        craters.entries.insert(craters.entries.end(), list.begin(), list.end());
    }
    if (craters.entries.empty()) {
        craters.entries.push_back(0);
    }
    return craters;
}

/// The datum lattice points of craters on a body, a row of `kDatumLatticeSide` squared per
/// crater.
struct BodyLattice {
    std::vector<Vec2d> points;         ///< Surface coordinates.
    std::vector<std::uint8_t> inside;  ///< 1 for the points inside the fit footprint.
    std::vector<float> positions;      ///< Positions from the origin (m), x, y, z triples.
};

/// The datum lattice points of craters `fitted` of `prepared` on a body, with positions from
/// `origin` (m), located on `threads` threads.
BodyLattice LocateBodyLattice(const std::vector<PreparedCrater>& prepared,
                              const std::vector<std::size_t>& fitted,
                              const CraterApplyOptions& options, unsigned threads,
                              const Vec3d& origin) {
    constexpr auto kPoints = static_cast<std::size_t>(kDatumLatticeSide * kDatumLatticeSide);
    const Surface& surface = options.surface;
    BodyLattice lattice{std::vector<Vec2d>(kPoints * fitted.size()),
                        std::vector<std::uint8_t>(kPoints * fitted.size(), 0),
                        std::vector<float>(3 * kPoints * fitted.size(), 0.0f)};
    ParallelFor(fitted.size(), threads, [&](std::size_t n) {
        const Crater& c = prepared[fitted[n]].crater;
        ForEachDatumPoint(FitRadius(c, options), [&](std::size_t point, double dx, double dy) {
            const std::size_t at = n * kPoints + point;
            lattice.points[at] = surface.Locate({c.x, c.y}, {dx, dy});
            lattice.inside[at] = 1;
            const Vec3d p = surface.Position(lattice.points[at]) - origin;
            lattice.positions[3 * at] = static_cast<float>(p.x);
            lattice.positions[3 * at + 1] = static_cast<float>(p.y);
            lattice.positions[3 * at + 2] = static_cast<float>(p.z);
        });
    });
    return lattice;
}

/// The ground at the points of `lattice`, a row of `side` per crater, read in one pass by
/// `batchGround` when given, else by `ground` on `threads` threads; NaN outside the footprint or
/// where the ground has no data, which is the raster's no-data value.
Raster<float> ReadBodyLattice(const BodyLattice& lattice, std::size_t side,
                              const HeightSampler& ground, const HeightsSampler& batchGround,
                              unsigned threads) {
    constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
    Raster<float> heights(side, lattice.points.size() / side, kNaN);
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
        for (std::size_t at = row * side; at < (row + 1) * side; ++at) {
            if (lattice.inside[at] != 0) {
                heights.Values()[at] = ground(lattice.points[at].x, lattice.points[at].y);
            }
        }
    });
    return heights;
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
    const BodyLattice points =
        LocateBodyLattice(prepared, fitted, options, sampling.threads, origin);
    Raster<float> lattice =
        ReadBodyLattice(points, static_cast<std::size_t>(kDatumLatticeSide * kDatumLatticeSide),
                        ground, batchGround, sampling.threads);
    std::vector<std::uint8_t> valid(lattice.Size());
    for (std::size_t at = 0; at < lattice.Size(); ++at) {
        valid[at] = std::isnan(lattice.Values()[at]) ? 0 : 1;
    }
    const BodyReliefCraters craters =
        MakeBodyReliefCraters(prepared, fitted, index, options, origin, sampling.threads);
    const CraterKernelBatch& batch = index.Batch();
    const std::vector<float> placeholder{0.0f};
    const std::vector<float>& data = batch.data.empty() ? placeholder : batch.data;
    const CraterBodyDatumKernel kernel{shape};
    const std::array<KernelBuffer, 7> extra{
        KernelBuffer{AsBytes(Span(batch.records)), batch.records.size()},
        KernelBuffer{AsBytes(Span(data)), data.size()},
        KernelBuffer{AsBytes(Span(craters.frames)), craters.frames.size()},
        KernelBuffer{AsBytes(Span(craters.bounds)), craters.bounds.size()},
        KernelBuffer{AsBytes(Span(craters.lists)), craters.lists.size()},
        KernelBuffer{AsBytes(Span(craters.entries)), craters.entries.size()},
        KernelBuffer{AsBytes(Span(points.positions)), points.positions.size(), 3}};
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
/// gradient of `ground`, the ground beneath the layer, across its datum footprint. Done before any
/// datum is fitted, so the relief datums are fitted to is the relief applied, and each crater's
/// slope depends on no other crater of its layer.
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

/// Fits the datums of craters `fitted` of `prepared` to `ground`, or to the raster as given when
/// it is empty: with the crater datum kernel where it can take them, on a body with the body
/// datum kernel on a GPU, else through `ReliefIndex`.
void FitDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& fitted,
               const Raster<float>& raster, const CraterProfile& profile,
               const CraterShapeKernel& shape, const CraterApplyOptions& options,
               const CraterSampling& sampling) {
    std::optional<Raster<float>> input;
    HeightSampler ground = sampling.ground;
    if (!ground) {
        input.emplace(raster);
        ground = RasterSampler(*input);
    }
    if (FitsOnKernelIndex(options)) {
        if (const std::optional<DatumKernelIndex> index =
                IndexDatumRelief(prepared, fitted, profile, shape, options)) {
            FitDatumsWithKernel(prepared, fitted, ground,
                                sampling.ground ? sampling.groundBatch : HeightsSampler{}, *index,
                                options, sampling.backend, sampling.threads);
            return;
        }
    }
    const ReliefIndex index(prepared, shape, profile, options);
    if (options.surface.IsBody() && options.datum != CraterDatum::CenterHeight &&
        FitBodyDatumsOnGpu(prepared, fitted, ground,
                           sampling.ground ? sampling.groundBatch : HeightsSampler{}, index, shape,
                           options, sampling)) {
        return;
    }
    ParallelFor(fitted.size(), sampling.threads, [&](std::size_t n) {
        const std::size_t k = fitted[n];
        prepared[k].datum =
            FitCraterDatum(prepared[k].crater, options, GroundBefore(ground, index, k));
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

/// Calls `visit(tile, cells)` for each tile of `side` cells, `tilesX` across, that `window`
/// covers, with the cells of `window` within it.
template <typename Visit>
void ForEachTile(const PixelWindow& window, std::size_t side, std::size_t tilesX,
                 const Visit& visit) {
    for (std::size_t ty = window.rowBegin / side; ty <= (window.rowEnd - 1) / side; ++ty) {
        for (std::size_t tx = window.colBegin / side; tx <= (window.colEnd - 1) / side; ++tx) {
            PixelWindow cells;
            cells.colBegin = std::max(window.colBegin, tx * side);
            cells.colEnd = std::min(window.colEnd, (tx + 1) * side);
            cells.rowBegin = std::max(window.rowBegin, ty * side);
            cells.rowEnd = std::min(window.rowEnd, (ty + 1) * side);
            visit(ty * tilesX + tx, cells);
        }
    }
}

/// Side of the tiles craters are binned into for the kernel, in cells.
constexpr std::size_t kTileCells = 32;

/// Shapes the craters `onRaster` of `prepared` into `raster` with the kernel: each crater's
/// `windows` are cut into the tiles they cover, the tiles run on `threads` threads, and a tile
/// shapes its craters in order over their own cells, so every cell sees them in order.
void ShapeTiles(Raster<float>& raster, unsigned threads, const CraterShapeKernel& kernel,
                const CraterKernelBatch& batch, const std::vector<PreparedCrater>& prepared,
                const std::vector<std::size_t>& onRaster,
                const std::vector<std::vector<PixelWindow>>& windows, const Surface& surface) {
    const std::size_t tilesX = CeilDiv(raster.Width(), kTileCells);
    const std::size_t tilesY = CeilDiv(raster.Height(), kTileCells);
    struct Entry {
        std::uint32_t crater;
        PixelWindow cells;
    };
    std::vector<std::vector<Entry>> tiles(tilesX * tilesY);
    for (std::size_t n = 0; n < onRaster.size(); ++n) {
        for (const PixelWindow& window : windows[n]) {
            ForEachTile(window, kTileCells, tilesX,
                        [&](std::size_t tile, const PixelWindow& cells) {
                            tiles[tile].push_back({static_cast<std::uint32_t>(n), cells});
                        });
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
                        const KernelOffset at =
                            ShapingOffset(surface, c, raster.CellCenter(col, row));
                        cell = ApplyCraterKernel(kernel, data, record, cell, at.dx, at.dy);
                    }
                }
            }
        }
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
            ForEachTile(window, kGpuTileCells, tilesX, [&](std::size_t tile, const PixelWindow&) {
                std::vector<std::uint32_t>& list = lists[tile];
                if (list.empty() || list.back() != n) {
                    list.push_back(static_cast<std::uint32_t>(n));
                }
            });
        }
    }
    std::vector<std::uint32_t> tiles{static_cast<std::uint32_t>(tilesX),
                                     static_cast<std::uint32_t>(kGpuTileCells), onBody ? 1u : 0u};
    std::vector<std::uint32_t> entries;
    for (const std::vector<std::uint32_t>& list : lists) {
        tiles.push_back(static_cast<std::uint32_t>(entries.size()));
        tiles.push_back(static_cast<std::uint32_t>(list.size()));
        entries.insert(entries.end(), list.begin(), list.end());
    }
    if (entries.empty()) {
        entries.push_back(0);
    }
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
            const Vec3d p = surface.Position(raster.CellCenter(col, row)) - origin;
            float* at = body.positions.data() + 3 * (row * raster.Width() + col);
            at[0] = static_cast<float>(p.x);
            at[1] = static_cast<float>(p.y);
            at[2] = static_cast<float>(p.z);
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
    const Bounds2d bounds = RasterGrid::FromRaster(raster).Bounds();
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
    if (batch.data.empty()) {
        batch.data.push_back(0.0f);
    }
    const std::array<KernelBuffer, 6> extra{
        KernelBuffer{AsBytes(Span(batch.records)), batch.records.size()},
        KernelBuffer{AsBytes(Span(batch.data)), batch.data.size()},
        KernelBuffer{AsBytes(Span(tiles)), tiles.size()},
        KernelBuffer{AsBytes(Span(entries)), entries.size()},
        KernelBuffer{AsBytes(Span(body.frames)), body.frames.size()},
        KernelBuffer{AsBytes(Span(body.positions)), body.positions.size(),
                     onBody ? std::size_t{3} : std::size_t{0}}};
    if (!RunKernel(backend, CompiledKernel<CraterShapeKernel>(), AsBytes(Span(&kernel, 1)), raster,
                   origin, extra)) {
        return false;
    }
    LogDebug("craters", "Shaped ", onRaster.size(), " craters on ", BackendName(backend));
    return true;
}

/// The craters of `prepared` whose footprints reach `raster`, with the windows of it they cover.
std::vector<std::size_t> CratersOnRaster(const Raster<float>& raster,
                                         const std::vector<PreparedCrater>& prepared,
                                         double reachRadii, const Surface& surface,
                                         std::vector<std::vector<PixelWindow>>& windows) {
    std::vector<std::size_t> onRaster;
    for (std::size_t k = 0; k < prepared.size(); ++k) {
        std::vector<PixelWindow> around =
            PixelWindowsIn(raster, Footprint(prepared[k], reachRadii, surface), surface);
        if (!around.empty()) {
            onRaster.push_back(k);
            windows.push_back(std::move(around));
        }
    }
    return onRaster;
}

/// Fits the datums of the craters `indices` of `prepared` reaching points, one crater at a time
/// so each can be kept in `sampling.cache`.
void FitPointDatums(std::vector<PreparedCrater>& prepared, const std::vector<std::size_t>& indices,
                    const CraterProfile& profile, const CraterShapeKernel& kernel,
                    const CraterApplyOptions& options, const CraterSampling& sampling) {
    std::optional<DatumKernelIndex> kernelIndex;
    bool indexed = false;
    std::optional<ReliefIndex> index;
    const auto fit = [&](std::size_t k) -> std::optional<Plane> {
        if (FitsOnKernelIndex(options)) {
            if (!indexed) {
                kernelIndex = IndexDatumRelief(prepared, indices, profile, kernel, options);
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
    for (const std::size_t k : indices) {
        if (sampling.cache != nullptr) {
            prepared[k].datum = *sampling.cache->Get<std::optional<Plane>>(
                sampling.owner, prepared[k].crater.id, sampling.spacing, [&] { return fit(k); });
        } else {
            prepared[k].datum = fit(k);
        }
    }
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

}  // namespace

CraterAttributes CraterShapeModels::Attributes(const Crater& crater) const {
    return ComputeCraterAttributes(crater, ResolvedCraterState(*this, crater), *profile);
}

CraterSampling SamplingFrom(const LayerContext& context, double spacing, const void* owner) {
    CraterSampling sampling{spacing, context.threads};
    if (context.ground != nullptr) {
        sampling.ground = context.ground->Sampler(spacing);
        sampling.groundBatch = context.ground->BatchSampler(spacing);
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

float ApplyCraterTo(float height, double x, double y, const PreparedCrater& crater,
                    const CraterProfile& profile, const CraterApplyOptions& options) {
    const CraterShapeKernel kernel = MakeCraterShapeKernel(profile, options);
    const CraterKernelBatch batch = MakeCraterKernelBatch(kernel, profile, {crater}, {0}, options);
    const KernelOffset at = ShapingOffset(options.surface, crater.crater, {x, y});
    return ApplyCraterKernel(kernel, batch.Data(), batch.records[0], height, at.dx, at.dy);
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
        PrepareCraters(craters, options, resolved, models, sampling.reshape);
    std::vector<std::vector<PixelWindow>> windows;
    const std::vector<std::size_t> onRaster = CratersOnRaster(
        raster, prepared, CraterReachRadii(profile, options), options.surface, windows);
    const CraterShapeKernel kernel = MakeCraterShapeKernel(profile, options);
    if (options.datum != CraterDatum::None && !onRaster.empty()) {
        ShapeOnSlopes(prepared, sampling.ground ? sampling.ground : RasterSampler(raster),
                      sampling.ground ? sampling.groundBatch : HeightsSampler{}, models, options);
        FitDatums(prepared, onRaster, raster, profile, kernel, options, sampling);
    }
    CraterKernelBatch batch = MakeCraterKernelBatch(kernel, profile, prepared, onRaster, options);
    if (!ShapeTilesOnGpu(raster, sampling.backend, sampling.threads, kernel, batch, prepared,
                         onRaster, windows, options.surface)) {
        ShapeTiles(raster, sampling.threads, kernel, batch, prepared, onRaster, windows,
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
    const CraterProfile& profile = *models.profile;
    std::vector<PreparedCrater> prepared =
        PrepareCraters(craters, options, sampling.spacing, models, sampling.reshape);
    const std::vector<ReachingCrater> reaching =
        CratersReaching(samples, prepared, CraterReachRadii(profile, options), options.surface);
    const CraterShapeKernel kernel = MakeCraterShapeKernel(profile, options);
    std::vector<std::size_t> indices;
    for (const ReachingCrater& crater : reaching) {
        indices.push_back(static_cast<std::size_t>(crater.prepared - prepared.data()));
    }
    if (options.datum != CraterDatum::None && !reaching.empty()) {
        ShapeOnSlopes(prepared, sampling.ground, sampling.groundBatch, models, options);
        FitPointDatums(prepared, indices, profile, kernel, options, sampling);
    }
    const CraterKernelBatch batch =
        MakeCraterKernelBatch(kernel, profile, prepared, indices, options);
    ShapePoints(samples, reaching, kernel, batch, options.surface);
}

}  // namespace terraingen
