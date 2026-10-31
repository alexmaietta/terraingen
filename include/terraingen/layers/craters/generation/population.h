// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief Generating crater populations cell by cell, in size bands.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/geometry/cells.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/generation/spacing.h"
#include "terraingen/layers/field.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace terraingen {

/// A size band of crater generation: diameters [`low`, `high`) meters, from 2^`exponent` to
/// 2^(`exponent` + 1) clipped to the requested range.
struct CraterBand {
    int exponent = 0;       ///< The band's power of two.
    double low = 0.0;       ///< Smallest diameter (m).
    double high = 0.0;      ///< Largest diameter (m), exclusive.
    double cellSize = 0.0;  ///< Side of the cells the band is drawn in (m).

    /// Memberwise equality.
    bool operator==(const CraterBand& other) const {
        return exponent == other.exponent && low == other.low && high == other.high &&
               cellSize == other.cellSize;
    }
    bool operator!=(const CraterBand& other) const {
        return !(*this == other);
    }
};

/// The bands `GenerateCraters` uses to cover [`minDiameter`, `maxDiameter`) meters, smallest first.
/// @param cellSizeFactor Cell side over each band's largest whole diameter.
std::vector<CraterBand> CraterBands(double minDiameter, double maxDiameter, double cellSizeFactor);

/// The craters `GenerateCraters` draws in one cell of one band, in the order drawn, before
/// regions, manual craters, spacing and caps apply.
CraterList GenerateCellCraters(const SurfaceCells& cells, const CraterBand& band,
                               const CellIndex& cell, const SizeFrequencyDistribution& density,
                               const CraterAgeModel& ages, std::uint32_t seed);

/// The craters of one cell in the order drawn, with their indices ordered by y so the craters of a
/// row range are found by binary search.
struct CellCraters {
    CraterList craters;              ///< The craters in the order drawn.
    std::vector<std::uint32_t> byY;  ///< Indices into `craters` by increasing y, then index.
    double maxDiameter = 0.0;        ///< The largest diameter, in meters; 0 without craters.

    /// Indexes `craters` by y.
    explicit CellCraters(CraterList craters);

    /// The indices, in drawn order, of the craters with y in [`minY`, `maxY`].
    std::vector<std::uint32_t> Rows(double minY, double maxY) const;
};

/// Supplies the craters of one cell of one band as `GenerateCellCraters` would, for example from
/// a cache; must be thread-safe.
using CellCraterSource = std::function<std::shared_ptr<const CellCraters>(const CraterBand& band,
                                                                          const CellIndex& cell)>;

/// The share of generated craters like `crater` that `GenerateCraters` keeps, in [0, 1].
using CraterKeepShare = std::function<double(const Crater& crater)>;

/// Which craters `GenerateCraters` draws, and how.
struct CraterGenerationOptions {
    /// Smallest diameter generated, in meters; must be set to a positive value.
    double minDiameter = 0.0;
    /// Largest diameter (m), exclusive; infinity means the top of the density's valid range, which
    /// must then be finite.
    double maxDiameter = std::numeric_limits<double>::infinity();
    /// Also keep craters centered outside the region within this many radii of it.
    double marginRadii = 0.0;
    /// Cell side as a multiple of each band's largest diameter.
    double cellSizeFactor = 16.0;
    /// Upper limit on the expected number of craters, to catch requests that would exhaust memory.
    double maxExpectedCraters = 5e7;
    /// Removes conflicting craters (`SpacingConflicts`); disabled by default, since it lowers the
    /// density below `density`.
    CraterSpacing spacing{};
    /// Manual craters to merge in: those within the region and margin follow the generated craters,
    /// flagged and given IDs as `AppendManualCraters` does; with spacing, they remove conflicting
    /// generated craters.
    CraterList manual{};
    /// Keep at most this many generated craters, the largest (ties to the smaller ID); unset keeps
    /// all. Manual craters are always kept and do not count. A capped region no longer equals its
    /// tiles joined; `CountLimitDiameter` bounds counts tile-safely.
    std::optional<std::size_t> maxCraters{};
    /// Skip bands whose craters are all smaller than this (m) and, with spacing, too small to
    /// remove a crater this large; craters at or above it are unchanged. 0 generates every band.
    double minResolvedDiameter = 0.0;
    /// Skip bands whose craters are all larger than this (m), unless spacing or `maxCraters` is on,
    /// where larger craters change which smaller ones are kept; craters at or below it are
    /// unchanged. Infinity generates every band.
    double maxBandDiameter = std::numeric_limits<double>::infinity();
    /// Where each cell's craters come from; empty draws them with `GenerateCellCraters`.
    CellCraterSource cellCraters{};
    /// The craters per unit area expected in a band, the density's `ExpectedCount(1, band.low,
    /// band.high)` to the bit, for example from a table; empty asks the density. Must be
    /// thread-safe.
    std::function<double(const CraterBand& band)> bandCount{};
    /// Threads the cells are drawn on, merged in their order so the craters do not depend on it;
    /// 0 for every hardware thread.
    unsigned threads = 1;
    /// The share of generated craters like each one kept, in [0, 1], from its position and size:
    /// drawing at the density's highest local level and keeping each crater with its share, by a
    /// uniform keyed to its ID, makes the density vary over the surface. Must be thread-safe and
    /// depend only on the crater; unset keeps every crater.
    CraterKeepShare keepShare{};
    /// The diameter (m) from which `manual` craters are a complete catalog at each point: generated
    /// craters at least this large centered there are dropped, so the catalog stands in for them
    /// rather than adding to them. Infinite or NaN where there is no catalog; unset keeps every
    /// generated crater. A DEM's own craters are such a catalog (`SourceResolvedLimit`), with
    /// or without `manual` craters.
    std::shared_ptr<const SurfaceField> completeness{};
    /// What the region and margins are measured on: on a sphere, regions are longitude and
    /// latitude boxes and margins are meters, and `cells` must be `CubeSphereCells` of its radius.
    Surface surface{};
    /// What the drawn features are called in the log: its category and the noun of its counts.
    std::string logName = "craters";
};

/// Checks crater generation options.
/// @throws std::invalid_argument for invalid spacing, a non-positive `minDiameter`, a `maxDiameter`
/// not above it, a negative margin, expected limit or resolved diameter, or a non-positive cell
/// size factor or largest band.
void ValidateCraterGenerationOptions(const CraterGenerationOptions& options);

/// The largest diameter generated in meters: `maxDiameter`, or for an infinite `maxDiameter` the
/// top of the density's valid range; infinite when both are unbounded.
double ResolveMaxDiameter(double maxDiameter, const SizeFrequencyDistribution& density);

/// The smallest diameter in [`minDiameter`, `maxDiameter`) meters from which `density` expects
/// about `count` craters over `area` m^2; `minDiameter` if even that expects fewer.
/// @throws std::invalid_argument unless `count` and `area` are positive and 0 < `minDiameter` <
/// `maxDiameter`.
/// @throws std::domain_error if the interval leaves the density's valid range.
double CountLimitDiameter(const SizeFrequencyDistribution& density, double area, double count,
                          double minDiameter, double maxDiameter);

/// Generates the craters of a density centered in `region` (surface coordinates) or within
/// `marginRadii` radii of it, in band, cell and index order; tiles join exactly.
/// @param density A `Density` distribution valid over [`minDiameter`, `maxDiameter`), or
/// extrapolating.
/// @throws std::invalid_argument for invalid options, a bad region or density kind, or more
/// expected craters than `maxExpectedCraters`.
/// @throws std::domain_error if the diameter interval leaves the density's valid range.
CraterList GenerateCraters(const SurfaceCells& cells, const Bounds2d& region,
                           const SizeFrequencyDistribution& density, const CraterAgeModel& ages,
                           std::uint32_t seed, const CraterGenerationOptions& options);

}  // namespace terraingen
