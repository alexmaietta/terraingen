// SPDX-License-Identifier: BSD-3-Clause

#include "terraingen/layers/craters/generation/population.h"

#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/random/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/util/log.h"
#include "terraingen/util/parallel.h"
#include "util/checks.h"
#include "util/numerics.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

namespace {

std::uint64_t CraterId(std::uint32_t seed, const CraterBand& band, const CellIndex& cell,
                       std::uint32_t index) {
    const U32x4 block =
        Philox4x32({static_cast<std::uint32_t>(cell.i), static_cast<std::uint32_t>(cell.j),
                    static_cast<std::uint32_t>(band.exponent), index},
                   {seed, kCraterIdStream});
    return JoinWords(block.x, block.y);
}

}  // namespace

void ValidateCraterGenerationOptions(const CraterGenerationOptions& options) {
    ValidateCraterSpacing(options.spacing);
    RequirePositive(options.minDiameter, "minDiameter");
    if (!(options.maxDiameter > options.minDiameter)) {
        throw std::invalid_argument("maxDiameter must exceed minDiameter");
    }
    RequireNonNegative(options.marginRadii, "marginRadii");
    RequirePositive(options.cellSizeFactor, "cellSizeFactor");
    if (!(options.maxExpectedCraters >= 0.0)) {
        throw std::invalid_argument("maxExpectedCraters must not be negative");
    }
    RequireNonNegative(options.minResolvedDiameter, "minResolvedDiameter");
    if (!(options.maxBandDiameter > 0.0)) {
        throw std::invalid_argument("maxBandDiameter must be positive");
    }
}

namespace {

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
        : m_cells(cells), m_region(region), m_density(density), m_ages(ages), m_seed(seed),
          m_options(options), m_surface(options.surface) {
        if (density.Kind() != SfdKind::Density) {
            throw std::invalid_argument("Craters are generated from a Density distribution");
        }
        m_maxDiameter = ResolveMaxDiameter(options.maxDiameter, density);
        if (std::isinf(m_maxDiameter)) {
            throw std::invalid_argument("An unbounded density needs a finite maxDiameter");
        }
        if (!(m_maxDiameter > options.minDiameter)) {
            throw std::invalid_argument("maxDiameter must exceed minDiameter");
        }
    }

    CraterList Run(GenerationCounts& counts) const {
        const std::vector<BandPlan> plan = PlanBands();
        counts.bands = plan.size();
        for (const BandPlan& entry : plan) {
            counts.expected += entry.expected;
        }
        if (counts.expected > m_options.maxExpectedCraters) {
            throw std::invalid_argument("Expected crater count " + std::to_string(counts.expected) +
                                        " exceeds maxExpectedCraters");
        }
        CraterList craters = DrawPlannedCells(plan, counts.expected);
        const std::size_t generated = craters.Size();
        AppendManualCraters(m_options.manual, craters);
        if (m_options.spacing.Enabled()) {
            craters = RemoveSpacingConflicts(craters, counts.removed);
        } else if (m_options.manual.Size() > 0) {
            craters = CropToRegion(craters, generated);
        }
        return KeepLargest(craters, counts.capped);
    }

    void Log(std::size_t kept, const GenerationCounts& counts, const Stopwatch& stopwatch) const {
        if (!LogEnabled(LogLevel::Debug)) {
            return;
        }
        const bool spaced = m_options.spacing.Enabled();
        const std::size_t manual = m_options.manual.Size();
        LogDebug(m_options.logName, "Generated ", kept, " ", m_options.logName, " (",
                 m_options.minDiameter, "-", m_maxDiameter, " m, ", counts.bands, " bands, ",
                 static_cast<long long>(std::llround(counts.expected)), " expected",
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
        const CraterSpacing& spacing = m_options.spacing;
        if (!spacing.Enabled()) {
            return 0.5 * m_options.marginRadii * diameter;
        }
        const double neighbor = std::fmin(spacing.diameterRatio * diameter, m_maxDiameter);
        return SpacingReach(diameter, m_maxDiameter, spacing) +
               0.5 * m_options.marginRadii * neighbor;
    }

    /// Whether `crater` is centered in the region or within its margin of it.
    bool InRegion(const Crater& crater, double margin) const {
        return m_surface.Contains(m_surface.Expand(m_region, margin), {crater.x, crater.y});
    }

    std::vector<BandPlan> PlanBands() const {
        const CraterSpacing& spacing = m_options.spacing;
        const double skipBelow =
            m_options.minResolvedDiameter / (spacing.Enabled() ? spacing.diameterRatio : 1.0);
        std::vector<BandPlan> plan;
        for (const CraterBand& band :
             CraterBands(m_options.minDiameter, m_maxDiameter, m_options.cellSizeFactor)) {
            const bool tooLarge =
                !spacing.Enabled() && !m_options.maxCraters && band.low > m_options.maxBandDiameter;
            if (band.high <= skipBelow || tooLarge) {
                continue;
            }
            const double margin = CandidateMargin(band.high);
            BandPlan entry{
                band, m_cells.CellsCovering(m_surface.Expand(m_region, margin), band.cellSize)};
            const double countPerArea = m_options.bandCount
                                            ? m_options.bandCount(band)
                                            : m_density.ExpectedCount(1.0, band.low, band.high);
            for (const CellIndex& cell : entry.cells) {
                entry.expected += countPerArea * m_cells.CellArea(cell, band.cellSize);
            }
            LogDebug(m_options.logName, "Band [", band.low, ", ", band.high,
                     ") m: ", entry.cells.size(), " cells of ", band.cellSize, " m, ",
                     entry.expected, " ", m_options.logName, " expected");
            plan.push_back(std::move(entry));
        }
        return plan;
    }

    CraterList DrawPlannedCells(const std::vector<BandPlan>& plan, double expected) const {
        CraterList craters;
        craters.Reserve(static_cast<std::size_t>(expected));
        std::vector<BandRegion> regions;
        std::vector<std::pair<std::size_t, CellIndex>> cells;
        for (std::size_t b = 0; b < plan.size(); ++b) {
            regions.push_back(BandBounds(plan[b].band));
            for (const CellIndex& cell : plan[b].cells) {
                cells.emplace_back(b, cell);
            }
        }
        if (m_options.threads == 1 || cells.size() < 2) {
            for (const auto& [b, cell] : cells) {
                DrawKept(plan[b].band, regions[b], cell, craters);
            }
            return craters;
        }
        std::vector<CraterList> kept(cells.size());
        ParallelFor(cells.size(), m_options.threads, [&](std::size_t n) {
            const auto& [b, cell] = cells[n];
            DrawKept(plan[b].band, regions[b], cell, kept[n]);
        });
        for (const CraterList& some : kept) {
            craters.Append(some);
        }
        return craters;
    }

    /// The region expanded by the candidate margins of a band's smallest and largest craters,
    /// which bound every margin in between since `Expand` and `CandidateMargin` only grow.
    struct BandRegion {
        CraterBand band;
        Bounds2d inner;
        Bounds2d outer;
    };

    /// Appends to `craters` those of `cell` of `band` kept in the band's `region`, in the order
    /// drawn.
    void DrawKept(const CraterBand& band, const BandRegion& region, const CellIndex& cell,
                  CraterList& craters) const {
        const std::shared_ptr<const CellCraters> drawn = DrawCell(band, cell);
        const CraterList& list = drawn->craters;
        const Bounds2d reach = drawn->maxDiameter > band.high
                                   ? m_surface.Expand(m_region, CandidateMargin(drawn->maxDiameter))
                                   : region.outer;
        for (const std::uint32_t n : drawn->Rows(reach.minY, reach.maxY)) {
            if (OutsideBand(region, list.Diameters()[n], list.X()[n], list.Y()[n])) {
                continue;
            }
            const Crater crater = list.At(n);
            if (InBandRegion(region, crater) && Kept(crater)) {
                craters.Append(crater);
            }
        }
    }

    BandRegion BandBounds(const CraterBand& band) const {
        return {band, m_surface.Expand(m_region, CandidateMargin(band.low)),
                m_surface.Expand(m_region, CandidateMargin(band.high))};
    }

    /// Whether a crater of the band lies outside its outer region, which `InBandRegion` rejects
    /// first; tested on the list's columns before the crater is copied out.
    bool OutsideBand(const BandRegion& region, double diameter, double x, double y) const {
        return diameter >= region.band.low && diameter <= region.band.high &&
               !m_surface.Contains(region.outer, {x, y});
    }

    /// `InRegion` with the crater's candidate margin, expanding the region only for craters of the
    /// band between its inner and outer regions.
    bool InBandRegion(const BandRegion& region, const Crater& crater) const {
        if (!(crater.diameter >= region.band.low && crater.diameter <= region.band.high)) {
            return InRegion(crater, CandidateMargin(crater.diameter));
        }
        const Vec2d center{crater.x, crater.y};
        if (!m_surface.Contains(region.outer, center)) {
            return false;
        }
        return m_surface.Contains(region.inner, center) ||
               InRegion(crater, CandidateMargin(crater.diameter));
    }

    /// Whether a generated crater stays: below the catalog's completeness limit at its center, and
    /// within the keep share there by a uniform keyed to its ID.
    bool Kept(const Crater& crater) const {
        if (m_options.completeness &&
            crater.diameter >= m_options.completeness->ValueAt(crater.x, crater.y)) {
            return false;
        }
        if (!m_options.keepShare) {
            return true;
        }
        const IdWords id = SplitId(crater.id);
        RandomStream random(m_seed, kCraterKeepStream, id.low, id.high);
        return random.NextDouble() < m_options.keepShare(crater);
    }

    std::shared_ptr<const CellCraters> DrawCell(const CraterBand& band,
                                                const CellIndex& cell) const {
        if (m_options.cellCraters) {
            return m_options.cellCraters(band, cell);
        }
        return std::make_shared<const CellCraters>(
            GenerateCellCraters(m_cells, band, cell, m_density, m_ages, m_seed));
    }

    /// The craters within their margin of the region, the first `generated` of which already are.
    CraterList CropToRegion(const CraterList& craters, std::size_t generated) const {
        return FilterCraters(craters, [&](std::size_t n, const Crater& crater) {
            return n < generated || InRegion(crater, 0.5 * m_options.marginRadii * crater.diameter);
        });
    }

    /// The craters within their margin of the region that spacing keeps, counting those removed.
    CraterList RemoveSpacingConflicts(const CraterList& craters, std::size_t& removed) const {
        const std::vector<bool> conflicts = SpacingConflicts(craters, m_options.spacing, m_surface);
        return FilterCraters(craters, [&](std::size_t n, const Crater& crater) {
            if (!InRegion(crater, 0.5 * m_options.marginRadii * crater.diameter)) {
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
        if (!m_options.maxCraters) {
            return craters;
        }
        const std::size_t limit = *m_options.maxCraters;
        std::vector<std::size_t> generated;
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            if (craters.Manual()[n] == 0) {
                generated.push_back(n);
            }
        }
        if (generated.size() <= limit) {
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

    const SurfaceCells& m_cells;
    const Bounds2d& m_region;
    const SizeFrequencyDistribution& m_density;
    const CraterAgeModel& m_ages;
    std::uint32_t m_seed;
    const CraterGenerationOptions& m_options;
    const Surface& m_surface;
    double m_maxDiameter = 0.0;
};

}  // namespace

std::vector<CraterBand> CraterBands(double minDiameter, double maxDiameter, double cellSizeFactor) {
    std::vector<CraterBand> bands;
    for (int k = std::ilogb(minDiameter); std::ldexp(1.0, k) < maxDiameter; ++k) {
        const double low = std::fmax(minDiameter, std::ldexp(1.0, k));
        const double high = std::fmin(maxDiameter, std::ldexp(1.0, k + 1));
        bands.push_back({k, low, high, cellSizeFactor * std::ldexp(1.0, k + 1)});
    }
    return bands;
}

CellCraters::CellCraters(CraterList list) : craters(std::move(list)), byY(craters.Size()) {
    const std::vector<double>& y = craters.Y();
    for (std::uint32_t n = 0; n < byY.size(); ++n) {
        byY[n] = n;
    }
    std::stable_sort(byY.begin(), byY.end(),
                     [&y](std::uint32_t a, std::uint32_t b) { return y[a] < y[b]; });
    for (const double diameter : craters.Diameters()) {
        maxDiameter = std::fmax(maxDiameter, diameter);
    }
}

std::vector<std::uint32_t> CellCraters::Rows(double minY, double maxY) const {
    const std::vector<double>& y = craters.Y();
    const auto first = std::lower_bound(
        byY.begin(), byY.end(), minY, [&y](std::uint32_t n, double value) { return y[n] < value; });
    const auto last = std::upper_bound(
        first, byY.end(), maxY, [&y](double value, std::uint32_t n) { return value < y[n]; });
    std::vector<std::uint32_t> rows(first, last);
    std::sort(rows.begin(), rows.end());
    return rows;
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
    const SurfaceCells::Sampling sampling = cells.CellSampling(cell, band.cellSize);
    const std::uint64_t count = SamplePoisson(geometry, countPerArea * sampling.area);
    CraterList craters;
    craters.Reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        const double diameter = density.SampleDiameter(geometry, band.low, band.high);
        const double u = geometry.NextDouble();
        const double v = geometry.NextDouble();
        const Vec2d center = cells.SamplePoint(u, v, cell, band.cellSize);
        if (sampling.bound > 0.0 &&
            geometry.NextDouble() * sampling.bound >= cells.Weight(center)) {
            continue;
        }
        const double ageGyr = ages.SampleAgeGyr(ageStream, diameter);
        craters.Append({center.x, center.y, diameter, ageGyr,
                        CraterId(seed, band, cell, static_cast<std::uint32_t>(index))});
    }
    return craters;
}

double ResolveMaxDiameter(double maxDiameter, const SizeFrequencyDistribution& density) {
    return std::isinf(maxDiameter) ? density.ValidRange().max : maxDiameter;
}

CraterList GenerateCraters(const SurfaceCells& cells, const Bounds2d& region,
                           const SizeFrequencyDistribution& density, const CraterAgeModel& ages,
                           std::uint32_t seed, const CraterGenerationOptions& options) {
    ValidateCraterGenerationOptions(options);
    const Stopwatch stopwatch;
    const Generation generation(cells, region, density, ages, seed, options);
    GenerationCounts counts;
    CraterList craters = generation.Run(counts);
    generation.Log(craters.Size(), counts, stopwatch);
    return craters;
}

double CountLimitDiameter(const SizeFrequencyDistribution& density, double area, double count,
                          double minDiameter, double maxDiameter) {
    RequirePositive(count, "Count");
    RequirePositive(area, "Area");
    if (!(IsPositive(minDiameter) && maxDiameter > minDiameter && std::isfinite(maxDiameter))) {
        throw std::invalid_argument("Diameter interval must satisfy 0 < minDiameter < maxDiameter");
    }
    if (density.ExpectedCount(area, minDiameter, maxDiameter) <= count) {
        return minDiameter;
    }
    return BisectLog(minDiameter, maxDiameter, 200, 1e-12, [&](double diameter) {
        return !(density.ExpectedCount(area, diameter, maxDiameter) > count);
    });
}

}  // namespace terraingen
