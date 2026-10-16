/// @file
/// @brief Crater lists, manual craters, size bands and cell-by-cell crater generation.
#include "terraingen/layers/craters/population.h"

#include "terraingen/layers/craters/spacing.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/util/checks.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

namespace {

constexpr std::uint32_t kCraterStream = 0x43524154u;
constexpr std::uint32_t kCraterAgeStream = 0x43524147u;
constexpr std::uint32_t kCraterIdStream = 0x43524944u;

std::uint64_t CraterId(std::uint32_t seed, const CraterBand& band, const CellIndex& cell,
                       std::uint32_t index) {
    const U32x4 block =
        Philox4x32({static_cast<std::uint32_t>(cell.i), static_cast<std::uint32_t>(cell.j),
                    static_cast<std::uint32_t>(band.exponent), index},
                   {seed, kCraterIdStream});
    return (static_cast<std::uint64_t>(block.x) << 32) | block.y;
}

void CheckOptions(const CraterGenerationOptions& options) {
    ValidateCraterSpacing(options.spacing);
    RequirePositive(options.dMin, "dMin");
    if (!(options.dMax > options.dMin)) {
        throw std::invalid_argument("dMax must exceed dMin");
    }
    RequireNonNegative(options.marginRadii, "marginRadii");
    RequirePositive(options.cellSizeFactor, "cellSizeFactor");
    if (!(options.maxExpectedCraters >= 0.0)) {
        throw std::invalid_argument("maxExpectedCraters must not be negative");
    }
    RequireNonNegative(options.minResolvedDiameter, "minResolvedDiameter");
}

/// One band of a generation: the cells drawn and the craters they are expected to hold.
struct BandPlan {
    CraterBand band;
    std::vector<CellIndex> cells;
    double expected = 0.0;
};

/// What a generation keeps and drops, for its log line.
struct GenerationCounts {
    std::size_t bands = 0;
    double expected = 0.0;
    std::size_t removed = 0;  ///< By spacing.
    std::size_t capped = 0;   ///< Over `maxCraters`.
};

/// One call of `GenerateCraters`, in steps.
class Generation {
  public:
    Generation(const SurfaceCells& cells, const Bounds2d& region,
               const SizeFrequencyDistribution& density, const CraterAgeModel& ages,
               std::uint32_t seed, const CraterGenerationOptions& options)
        : cells_(cells), region_(region), density_(density), ages_(ages), seed_(seed),
          options_(options), surface_(options.surface) {
        if (density.Kind() != SfdKind::Density) {
            throw std::invalid_argument("Craters are generated from a Density distribution");
        }
        dMax_ = ResolveDMax(options.dMax, density);
        if (std::isinf(dMax_)) {
            throw std::invalid_argument("An unbounded density needs a finite dMax");
        }
        if (!(dMax_ > options.dMin)) {
            throw std::invalid_argument("dMax must exceed dMin");
        }
    }

    CraterList Run(GenerationCounts& counts) const {
        const std::vector<BandPlan> plan = PlanBands();
        counts.bands = plan.size();
        for (const BandPlan& entry : plan) {
            counts.expected += entry.expected;
        }
        if (counts.expected > options_.maxExpectedCraters) {
            throw std::invalid_argument("Expected crater count " + std::to_string(counts.expected) +
                                        " exceeds maxExpectedCraters");
        }
        CraterList craters = DrawPlannedCells(plan, counts.expected);
        const std::size_t generated = craters.Size();
        AppendManualCraters(options_.manual, craters);
        if (options_.spacing.Enabled()) {
            craters = RemoveSpacingConflicts(craters, counts.removed);
        } else if (options_.manual.Size() > 0) {
            craters = CropToRegion(craters, generated);
        }
        return KeepLargest(craters, counts.capped);
    }

    void Log(std::size_t kept, const GenerationCounts& counts, const Stopwatch& stopwatch) const {
        const bool spaced = options_.spacing.Enabled();
        const std::size_t manual = options_.manual.Size();
        LogInfo("craters", "Generated ", kept, " craters (", options_.dMin, "-", dMax_, " m, ",
                counts.bands, " bands, ", static_cast<long long>(std::llround(counts.expected)),
                " expected",
                spaced ? ", " + std::to_string(counts.removed) + " removed by spacing" : "",
                manual > 0 ? ", " + std::to_string(manual) + " manual supplied" : "",
                counts.capped > 0
                    ? ", " + std::to_string(counts.capped) + " over maxCraters dropped"
                    : "",
                ") in ", FormatDuration(stopwatch.Milliseconds()));
    }

  private:
    /// How far beyond the region a crater `diameter` across is drawn: its margin, and with spacing
    /// as far again as the craters it can conflict with reach.
    double CandidateMargin(double diameter) const {
        const CraterSpacing& spacing = options_.spacing;
        if (!spacing.Enabled()) {
            return 0.5 * options_.marginRadii * diameter;
        }
        const double neighbour = std::fmin(spacing.diameterRatio * diameter, dMax_);
        return SpacingReach(diameter, dMax_, spacing) + 0.5 * options_.marginRadii * neighbour;
    }

    /// Whether `crater` is centred in the region or within its margin of it.
    bool InRegion(const Crater& crater, double margin) const {
        return surface_.Contains(surface_.Expand(region_, margin), {crater.x, crater.y});
    }

    std::vector<BandPlan> PlanBands() const {
        const CraterSpacing& spacing = options_.spacing;
        const double skipBelow =
            options_.minResolvedDiameter / (spacing.Enabled() ? spacing.diameterRatio : 1.0);
        std::vector<BandPlan> plan;
        for (const CraterBand& band : CraterBands(options_.dMin, dMax_, options_.cellSizeFactor)) {
            if (band.high <= skipBelow) {
                continue;
            }
            const double margin = CandidateMargin(band.high);
            BandPlan entry{band,
                           cells_.CellsCovering(surface_.Expand(region_, margin), band.cellSize)};
            const double countPerArea = density_.ExpectedCount(1.0, band.low, band.high);
            for (const CellIndex& cell : entry.cells) {
                entry.expected += countPerArea * cells_.CellArea(cell, band.cellSize);
            }
            LogDebug("craters", "Band [", band.low, ", ", band.high, ") m: ", entry.cells.size(),
                     " cells of ", band.cellSize, " m, ", entry.expected, " craters expected");
            plan.push_back(std::move(entry));
        }
        return plan;
    }

    CraterList DrawPlannedCells(const std::vector<BandPlan>& plan, double expected) const {
        CraterList craters;
        craters.Reserve(static_cast<std::size_t>(expected));
        for (const BandPlan& entry : plan) {
            for (const CellIndex& cell : entry.cells) {
                const std::shared_ptr<const CraterList> drawn = DrawCell(entry.band, cell);
                for (std::size_t n = 0; n < drawn->Size(); ++n) {
                    const Crater crater = drawn->At(n);
                    if (InRegion(crater, CandidateMargin(crater.diameter))) {
                        craters.Append(crater);
                    }
                }
            }
        }
        return craters;
    }

    std::shared_ptr<const CraterList> DrawCell(const CraterBand& band,
                                               const CellIndex& cell) const {
        if (options_.cellCraters) {
            return options_.cellCraters(band, cell);
        }
        return std::make_shared<const CraterList>(
            GenerateCellCraters(cells_, band, cell, density_, ages_, seed_));
    }

    /// The craters within their margin of the region, the first `generated` of which already are.
    CraterList CropToRegion(const CraterList& craters, std::size_t generated) const {
        return FilterCraters(craters, [&](std::size_t n, const Crater& crater) {
            return n < generated || InRegion(crater, 0.5 * options_.marginRadii * crater.diameter);
        });
    }

    /// The craters within their margin of the region that spacing keeps, counting those removed.
    CraterList RemoveSpacingConflicts(const CraterList& craters, std::size_t& removed) const {
        const std::vector<bool> conflicts = SpacingConflicts(craters, options_.spacing, surface_);
        return FilterCraters(craters, [&](std::size_t n, const Crater& crater) {
            if (!InRegion(crater, 0.5 * options_.marginRadii * crater.diameter)) {
                return false;
            }
            if (conflicts[n]) {
                ++removed;
                return false;
            }
            return true;
        });
    }

    /// The manual craters and the `maxCraters` largest generated ones (ties to the smaller ID),
    /// counting the generated craters dropped.
    CraterList KeepLargest(const CraterList& craters, std::size_t& capped) const {
        const std::size_t limit = options_.maxCraters;
        std::vector<std::size_t> generated;
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            if (craters.Manual()[n] == 0) {
                generated.push_back(n);
            }
        }
        if (limit == 0 || generated.size() <= limit) {
            return craters;
        }
        const auto& diameters = craters.Diameters();
        const auto& ids = craters.Ids();
        std::nth_element(generated.begin(), generated.begin() + static_cast<std::ptrdiff_t>(limit),
                         generated.end(), [&](std::size_t a, std::size_t b) {
                             if (diameters[a] != diameters[b]) {
                                 return diameters[a] > diameters[b];
                             }
                             return ids[a] < ids[b];
                         });
        std::vector<bool> keep(craters.Size(), false);
        for (std::size_t k = 0; k < limit; ++k) {
            keep[generated[k]] = true;
        }
        capped = generated.size() - limit;
        return FilterCraters(
            craters, [&](std::size_t n, const Crater& crater) { return keep[n] || crater.manual; });
    }

    const SurfaceCells& cells_;
    const Bounds2d& region_;
    const SizeFrequencyDistribution& density_;
    const CraterAgeModel& ages_;
    std::uint32_t seed_;
    const CraterGenerationOptions& options_;
    const Surface& surface_;
    double dMax_ = 0.0;
};

}  // namespace

std::vector<CraterBand> CraterBands(double dMin, double dMax, double cellSizeFactor) {
    std::vector<CraterBand> bands;
    for (int k = std::ilogb(dMin); std::ldexp(1.0, k) < dMax; ++k) {
        const double low = std::fmax(dMin, std::ldexp(1.0, k));
        const double high = std::fmin(dMax, std::ldexp(1.0, k + 1));
        bands.push_back({k, low, high, cellSizeFactor * std::ldexp(1.0, k + 1)});
    }
    return bands;
}

CraterList GenerateCellCraters(const SurfaceCells& cells, const CraterBand& band,
                               const CellIndex& cell, const SizeFrequencyDistribution& density,
                               const CraterAgeModel& ages, std::uint32_t seed) {
    const auto k = static_cast<std::uint32_t>(band.exponent);
    const auto i = static_cast<std::uint32_t>(cell.i);
    const auto j = static_cast<std::uint32_t>(cell.j);
    RandomStream geometry(seed, kCraterStream, i, j, k);
    RandomStream ageStream(seed, kCraterAgeStream, i, j, k);
    const double countPerArea = density.ExpectedCount(1.0, band.low, band.high);
    const std::uint64_t count =
        SamplePoisson(geometry, countPerArea * cells.CellArea(cell, band.cellSize));
    CraterList craters;
    craters.Reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        const double diameter = density.SampleDiameter(geometry, band.low, band.high);
        const double u = geometry.NextDouble();
        const double v = geometry.NextDouble();
        const Vec2d centre = cells.SamplePoint(u, v, cell, band.cellSize);
        const double ageGyr = ages.SampleAgeGyr(ageStream, diameter);
        craters.Append({centre.x, centre.y, diameter, ageGyr,
                        CraterId(seed, band, cell, static_cast<std::uint32_t>(index))});
    }
    return craters;
}

std::uint64_t ManualCraterId(double x, double y, double diameter) {
    const IdWords xBits = SplitId(std::bit_cast<std::uint64_t>(x));
    const IdWords yBits = SplitId(std::bit_cast<std::uint64_t>(y));
    const IdWords dBits = SplitId(std::bit_cast<std::uint64_t>(diameter));
    const U32x4 block =
        Philox4x32({xBits.low, xBits.high, yBits.low, yBits.high}, {dBits.low, dBits.high});
    return (static_cast<std::uint64_t>(block.x) << 32) | block.y;
}

void AppendManualCraters(const CraterList& manual, CraterList& craters,
                         const std::function<bool(const Crater&)>& keep) {
    for (std::size_t n = 0; n < manual.Size(); ++n) {
        Crater crater = manual.At(n);
        if (!(std::isfinite(crater.x) && std::isfinite(crater.y) && IsPositive(crater.diameter) &&
              IsNonNegative(crater.ageGyr))) {
            throw std::invalid_argument("Manual craters need finite positions, positive diameters "
                                        "and non-negative ages");
        }
        crater.manual = true;
        if (crater.id == 0) {
            crater.id = ManualCraterId(crater.x, crater.y, crater.diameter);
        }
        if (!keep || keep(crater)) {
            craters.Append(crater);
        }
    }
}

void CraterList::Reserve(std::size_t count) {
    x_.reserve(count);
    y_.reserve(count);
    diameter_.reserve(count);
    ageGyr_.reserve(count);
    id_.reserve(count);
    manual_.reserve(count);
}

void CraterList::Append(const Crater& crater) {
    x_.push_back(crater.x);
    y_.push_back(crater.y);
    diameter_.push_back(crater.diameter);
    ageGyr_.push_back(crater.ageGyr);
    id_.push_back(crater.id);
    manual_.push_back(crater.manual ? 1 : 0);
}

void CraterList::Append(const CraterList& other) {
    x_.insert(x_.end(), other.x_.begin(), other.x_.end());
    y_.insert(y_.end(), other.y_.begin(), other.y_.end());
    diameter_.insert(diameter_.end(), other.diameter_.begin(), other.diameter_.end());
    ageGyr_.insert(ageGyr_.end(), other.ageGyr_.begin(), other.ageGyr_.end());
    id_.insert(id_.end(), other.id_.begin(), other.id_.end());
    manual_.insert(manual_.end(), other.manual_.begin(), other.manual_.end());
}

Crater CraterList::At(std::size_t index) const {
    if (index >= Size()) {
        throw std::out_of_range("Crater index out of range");
    }
    return {x_[index],      y_[index],  diameter_[index],
            ageGyr_[index], id_[index], manual_[index] != 0};
}

double ResolveDMax(double dMax, const SizeFrequencyDistribution& density) {
    return std::isinf(dMax) ? density.ValidRange().max : dMax;
}

CraterList GenerateCraters(const SurfaceCells& cells, const Bounds2d& region,
                           const SizeFrequencyDistribution& density, const CraterAgeModel& ages,
                           std::uint32_t seed, const CraterGenerationOptions& options) {
    CheckOptions(options);
    const Stopwatch stopwatch;
    const Generation generation(cells, region, density, ages, seed, options);
    GenerationCounts counts;
    CraterList craters = generation.Run(counts);
    generation.Log(craters.Size(), counts, stopwatch);
    return craters;
}

double MinDiameterForCount(const SizeFrequencyDistribution& density, double area, double count,
                           double dMin, double dMax) {
    if (!(IsPositive(count) && IsPositive(area))) {
        throw std::invalid_argument("Count and area must be positive and finite");
    }
    if (!(IsPositive(dMin) && dMax > dMin && std::isfinite(dMax))) {
        throw std::invalid_argument("Diameter interval must satisfy 0 < dMin < dMax");
    }
    if (density.ExpectedCount(area, dMin, dMax) <= count) {
        return dMin;
    }
    double low = std::log(dMin);
    double high = std::log(dMax);
    for (int i = 0; i < 200 && high - low > 1e-12; ++i) {
        const double middle = 0.5 * (low + high);
        if (density.ExpectedCount(area, std::exp(middle), dMax) > count) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return std::exp(high);
}

}  // namespace terraingen
