#include "terraingen/features/craters/population.h"

#include "terraingen/kernels/philox.h"

#include <algorithm>
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

void CraterList::Reserve(std::size_t count) {
    x_.reserve(count);
    y_.reserve(count);
    diameter_.reserve(count);
    ageGyr_.reserve(count);
    id_.reserve(count);
}

void CraterList::Append(const Crater& crater) {
    x_.push_back(crater.x);
    y_.push_back(crater.y);
    diameter_.push_back(crater.diameter);
    ageGyr_.push_back(crater.ageGyr);
    id_.push_back(crater.id);
}

void CraterList::Append(const CraterList& other) {
    x_.insert(x_.end(), other.x_.begin(), other.x_.end());
    y_.insert(y_.end(), other.y_.begin(), other.y_.end());
    diameter_.insert(diameter_.end(), other.diameter_.begin(), other.diameter_.end());
    ageGyr_.insert(ageGyr_.end(), other.ageGyr_.begin(), other.ageGyr_.end());
    id_.insert(id_.end(), other.id_.begin(), other.id_.end());
}

Crater CraterList::At(std::size_t index) const {
    if (index >= Size()) {
        throw std::out_of_range("Crater index out of range");
    }
    return {x_[index], y_[index], diameter_[index], ageGyr_[index], id_[index]};
}

CraterList GenerateCraters(const SurfaceCells& cells, const Bounds2d& region,
                           const SizeFrequencyDistribution& density, const CraterAgeModel& ages,
                           std::uint32_t seed, const CraterGenerationOptions& options) {
    CheckOptions(options);
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
    std::vector<BandCells> plan;
    double expected = 0.0;
    for (const Band& band : Bands(options.dMin, dMax, options.cellSizeFactor)) {
        const double margin = 0.5 * options.marginRadii * band.high;
        BandCells entry{band, cells.CellsCovering(region.Expanded(margin), band.cellSize),
                        density.ExpectedCount(1.0, band.low, band.high)};
        for (const CellIndex& cell : entry.cells) {
            expected += entry.countPerArea * cells.CellArea(cell, band.cellSize);
        }
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
                const Vec2d centre = cells.SamplePoint(geometry, cell, band.cellSize);
                const double ageGyr = ages.SampleAgeGyr(ageStream, diameter);
                if (!region.Expanded(0.5 * options.marginRadii * diameter)
                         .Contains(centre.x, centre.y)) {
                    continue;
                }
                craters.Append({centre.x, centre.y, diameter, ageGyr,
                                CraterId(seed, band, cell, static_cast<std::uint32_t>(index))});
            }
        }
    }
    return craters;
}

}  // namespace terraingen
