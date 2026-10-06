#include "terraingen/layers/craters/population.h"

#include "terraingen/layers/craters/filters.h"
#include "terraingen/noise/kernels/philox.h"
#include "terraingen/random/samplers.h"
#include "terraingen/util/log.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace terraingen {

namespace {

constexpr std::uint32_t kCraterStream = 0x43524154u;
constexpr std::uint32_t kCraterAgeStream = 0x43524147u;
constexpr std::uint32_t kCraterIdStream = 0x43524944u;

struct Band {
    int exponent = 0;
    double low = 0.0;
    double high = 0.0;
    double cellSize = 0.0;
};

std::vector<Band> Bands(double dMin, double dMax, double cellSizeFactor) {
    std::vector<Band> bands;
    for (int k = std::ilogb(dMin); std::ldexp(1.0, k) < dMax; ++k) {
        const double low = std::fmax(dMin, std::ldexp(1.0, k));
        const double high = std::fmin(dMax, std::ldexp(1.0, k + 1));
        bands.push_back({k, low, high, cellSizeFactor * std::ldexp(1.0, k + 1)});
    }
    return bands;
}

std::uint64_t CraterId(std::uint32_t seed, const Band& band, const CellIndex& cell,
                       std::uint32_t index) {
    const U32x4 block =
        Philox4x32({static_cast<std::uint32_t>(cell.i), static_cast<std::uint32_t>(cell.j),
                    static_cast<std::uint32_t>(band.exponent), index},
                   {seed, kCraterIdStream});
    return (static_cast<std::uint64_t>(block.x) << 32) | block.y;
}

void CheckOptions(const CraterGenerationOptions& options) {
    ValidateCraterSpacing(options.spacing);
    if (!(std::isfinite(options.dMin) && options.dMin > 0.0)) {
        throw std::invalid_argument("dMin must be positive and finite");
    }
    if (!(options.dMax > options.dMin)) {
        throw std::invalid_argument("dMax must exceed dMin");
    }
    if (!(std::isfinite(options.marginRadii) && options.marginRadii >= 0.0)) {
        throw std::invalid_argument("marginRadii must be finite and not negative");
    }
    if (!(std::isfinite(options.cellSizeFactor) && options.cellSizeFactor > 0.0)) {
        throw std::invalid_argument("cellSizeFactor must be positive and finite");
    }
    if (!(options.maxExpectedCraters >= 0.0)) {
        throw std::invalid_argument("maxExpectedCraters must not be negative");
    }
}

}  // namespace

std::uint64_t ManualCraterId(double x, double y, double diameter) {
    const auto xBits = std::bit_cast<std::uint64_t>(x);
    const auto yBits = std::bit_cast<std::uint64_t>(y);
    const auto dBits = std::bit_cast<std::uint64_t>(diameter);
    const U32x4 block =
        Philox4x32({static_cast<std::uint32_t>(xBits), static_cast<std::uint32_t>(xBits >> 32),
                    static_cast<std::uint32_t>(yBits), static_cast<std::uint32_t>(yBits >> 32)},
                   {static_cast<std::uint32_t>(dBits), static_cast<std::uint32_t>(dBits >> 32)});
    return (static_cast<std::uint64_t>(block.x) << 32) | block.y;
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

CraterList GenerateCraters(const SurfaceCells& cells, const Bounds2d& region,
                           const SizeFrequencyDistribution& density, const CraterAgeModel& ages,
                           std::uint32_t seed, const CraterGenerationOptions& options) {
    CheckOptions(options);
    const Stopwatch stopwatch;
    if (density.Kind() != SfdKind::Density) {
        throw std::invalid_argument("Craters are generated from a Density distribution");
    }
    double dMax = options.dMax;
    if (std::isinf(dMax)) {
        dMax = density.ValidRange().max;
        if (std::isinf(dMax)) {
            throw std::invalid_argument("An unbounded density needs a finite dMax");
        }
    }
    if (!(dMax > options.dMin)) {
        throw std::invalid_argument("dMax must exceed dMin");
    }

    struct BandCells {
        Band band;
        std::vector<CellIndex> cells;
        double countPerArea = 0.0;
    };
    const CraterSpacing& spacing = options.spacing;
    const auto candidateMargin = [&](double diameter) {
        if (!spacing.Enabled()) {
            return 0.5 * options.marginRadii * diameter;
        }
        const double neighbour = std::fmin(spacing.diameterRatio * diameter, dMax);
        return SpacingReach(diameter, dMax, spacing) + 0.5 * options.marginRadii * neighbour;
    };
    std::vector<BandCells> plan;
    double expected = 0.0;
    for (const Band& band : Bands(options.dMin, dMax, options.cellSizeFactor)) {
        const double margin = candidateMargin(band.high);
        BandCells entry{band, cells.CellsCovering(region.Expanded(margin), band.cellSize),
                        density.ExpectedCount(1.0, band.low, band.high)};
        double bandExpected = 0.0;
        for (const CellIndex& cell : entry.cells) {
            bandExpected += entry.countPerArea * cells.CellArea(cell, band.cellSize);
        }
        LogDebug("craters", "Band [", band.low, ", ", band.high, ") m: ", entry.cells.size(),
                 " cells of ", band.cellSize, " m, ", bandExpected, " craters expected");
        expected += bandExpected;
        plan.push_back(std::move(entry));
    }
    if (expected > options.maxExpectedCraters) {
        throw std::invalid_argument("Expected crater count " + std::to_string(expected) +
                                    " exceeds maxExpectedCraters");
    }

    CraterList craters;
    craters.Reserve(static_cast<std::size_t>(expected));
    for (const BandCells& entry : plan) {
        const Band& band = entry.band;
        const auto k = static_cast<std::uint32_t>(band.exponent);
        for (const CellIndex& cell : entry.cells) {
            const auto i = static_cast<std::uint32_t>(cell.i);
            const auto j = static_cast<std::uint32_t>(cell.j);
            RandomStream geometry(seed, kCraterStream, i, j, k);
            RandomStream ageStream(seed, kCraterAgeStream, i, j, k);
            const std::uint64_t count =
                SamplePoisson(geometry, entry.countPerArea * cells.CellArea(cell, band.cellSize));
            for (std::uint64_t index = 0; index < count; ++index) {
                const double diameter = density.SampleDiameter(geometry, band.low, band.high);
                const double u = geometry.NextDouble();
                const double v = geometry.NextDouble();
                const Vec2d centre = cells.SamplePoint(u, v, cell, band.cellSize);
                const double ageGyr = ages.SampleAgeGyr(ageStream, diameter);
                if (!region.Expanded(candidateMargin(diameter)).Contains(centre.x, centre.y)) {
                    continue;
                }
                craters.Append({centre.x, centre.y, diameter, ageGyr,
                                CraterId(seed, band, cell, static_cast<std::uint32_t>(index))});
            }
        }
    }
    const std::size_t generated = craters.Size();
    for (std::size_t n = 0; n < options.manual.Size(); ++n) {
        Crater crater = options.manual.At(n);
        if (!(std::isfinite(crater.x) && std::isfinite(crater.y) &&
              std::isfinite(crater.diameter) && crater.diameter > 0.0 &&
              std::isfinite(crater.ageGyr) && crater.ageGyr >= 0.0)) {
            throw std::invalid_argument("Manual craters need finite positions, positive diameters "
                                        "and non-negative ages");
        }
        crater.manual = true;
        if (crater.id == 0) {
            crater.id = ManualCraterId(crater.x, crater.y, crater.diameter);
        }
        craters.Append(crater);
    }
    std::size_t removed = 0;
    if (!spacing.Enabled() && options.manual.Size() > 0) {
        CraterList kept;
        kept.Reserve(craters.Size());
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            const Crater crater = craters.At(n);
            if (n < generated || region.Expanded(0.5 * options.marginRadii * crater.diameter)
                                     .Contains(crater.x, crater.y)) {
                kept.Append(crater);
            }
        }
        craters = std::move(kept);
    }
    if (spacing.Enabled()) {
        const std::vector<bool> conflicts = SpacingConflicts(craters, spacing);
        CraterList kept;
        kept.Reserve(craters.Size());
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            const Crater crater = craters.At(n);
            if (!region.Expanded(0.5 * options.marginRadii * crater.diameter)
                     .Contains(crater.x, crater.y)) {
                continue;
            }
            if (conflicts[n]) {
                ++removed;
                continue;
            }
            kept.Append(crater);
        }
        craters = std::move(kept);
    }
    std::size_t capped = 0;
    std::size_t generatedKept = 0;
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        generatedKept += craters.Manual()[n] == 0 ? 1u : 0u;
    }
    if (options.maxCraters > 0 && generatedKept > options.maxCraters) {
        std::vector<std::size_t> order;
        order.reserve(generatedKept);
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            if (craters.Manual()[n] == 0) {
                order.push_back(n);
            }
        }
        const auto& diameters = craters.Diameters();
        const auto& ids = craters.Ids();
        std::nth_element(order.begin(),
                         order.begin() + static_cast<std::ptrdiff_t>(options.maxCraters),
                         order.end(), [&](std::size_t a, std::size_t b) {
                             if (diameters[a] != diameters[b]) {
                                 return diameters[a] > diameters[b];
                             }
                             return ids[a] < ids[b];
                         });
        std::vector<bool> keep(craters.Size(), false);
        for (std::size_t k = 0; k < options.maxCraters; ++k) {
            keep[order[k]] = true;
        }
        CraterList kept;
        kept.Reserve(options.maxCraters + options.manual.Size());
        for (std::size_t n = 0; n < craters.Size(); ++n) {
            if (keep[n] || craters.Manual()[n] != 0) {
                kept.Append(craters.At(n));
            }
        }
        capped = generatedKept - options.maxCraters;
        craters = std::move(kept);
    }
    LogInfo("craters", "Generated ", craters.Size(), " craters (", options.dMin, "-", dMax, " m, ",
            plan.size(), " bands, ", static_cast<long long>(std::llround(expected)), " expected",
            spacing.Enabled() ? ", " + std::to_string(removed) + " removed by spacing" : "",
            options.manual.Size() > 0
                ? ", " + std::to_string(options.manual.Size()) + " manual supplied"
                : "",
            capped > 0 ? ", " + std::to_string(capped) + " over maxCraters dropped" : "", ") in ",
            FormatDuration(stopwatch.Milliseconds()));
    return craters;
}

double MinDiameterForCount(const SizeFrequencyDistribution& density, double area, double count,
                           double dMin, double dMax) {
    if (!(std::isfinite(count) && count > 0.0 && std::isfinite(area) && area > 0.0)) {
        throw std::invalid_argument("Count and area must be positive and finite");
    }
    if (!(std::isfinite(dMin) && dMin > 0.0 && dMax > dMin && std::isfinite(dMax))) {
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
