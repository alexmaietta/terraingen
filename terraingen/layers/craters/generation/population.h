/// @file
/// @brief Generating crater populations cell by cell, in size bands.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/sources/field.h"
#include "terraingen/util/cells.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace terraingen {

/// Keeps craters of similar size apart: two conflict when their diameters differ by at most
/// `diameterRatio` and their centers are closer than `separation` times the larger diameter.
struct CraterSpacing {
    CraterSpacing() = default;
    /// Spacing with `diameterRatio` `ratio` and `separation` `minSeparation`.
    CraterSpacing(double ratio, double minSeparation)
        : diameterRatio(ratio), separation(minSeparation) {}

    double diameterRatio = 1.5;  ///< At least 1.
    double separation = 0.0;     ///< In larger diameters; 0 disables spacing.
    /// Craters at least this large (m) take precedence over smaller ones whatever their ages, as a
    /// crater layer sets for the craters that throw secondaries a total map measures; infinite
    /// ranks by age first. Not part of a configuration.
    double outrankFrom = std::numeric_limits<double>::infinity();

    /// Whether spacing applies: a positive `separation`.
    bool Enabled() const {
        return separation > 0.0;
    }

    /// Memberwise equality.
    bool operator==(const CraterSpacing& other) const {
        return diameterRatio == other.diameterRatio && separation == other.separation &&
               outrankFrom == other.outrankFrom;
    }
    bool operator!=(const CraterSpacing& other) const {
        return !(*this == other);
    }
};

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

/// The bands `GenerateCraters` uses to cover [`dMin`, `dMax`) meters, smallest first.
/// @param cellSizeFactor Cell side over each band's largest whole diameter.
std::vector<CraterBand> CraterBands(double dMin, double dMax, double cellSizeFactor);

/// The craters `GenerateCraters` draws in one cell of one band, in the order drawn, before
/// regions, manual craters, spacing and caps apply.
CraterList GenerateCellCraters(const SurfaceCells& cells, const CraterBand& band,
                               const CellIndex& cell, const SizeFrequencyDistribution& density,
                               const CraterAgeModel& ages, std::uint32_t seed);

/// Supplies the craters of one cell of one band as `GenerateCellCraters` would, for example from
/// a cache; must be thread-safe.
using CellCraterSource =
    std::function<std::shared_ptr<const CraterList>(const CraterBand& band, const CellIndex& cell)>;

/// The share of generated craters like `crater` that `GenerateCraters` keeps, in [0, 1].
using CraterKeepShare = std::function<double(const Crater& crater)>;

/// Which craters `GenerateCraters` draws, and how.
struct CraterGenerationOptions {
    /// Smallest diameter generated, in meters; must be set to a positive value.
    double dMin = 0.0;
    /// Largest diameter (m), exclusive; infinity means the top of the density's valid range, which
    /// must then be finite.
    double dMax = std::numeric_limits<double>::infinity();
    /// Also keep craters centerd outside the region within this many radii of it.
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
    /// Keep at most this many generated craters, the largest (ties to the smaller ID); 0 keeps
    /// all. Manual craters are always kept and do not count. A capped region no longer equals its
    /// tiles joined; `CountLimitDiameter` bounds counts tile-safely.
    std::size_t maxCraters = 0;
    /// Skip bands whose craters are all smaller than this (m) and, with spacing, too small to
    /// remove a crater this large; craters at or above it are unchanged. 0 generates every band.
    double minResolvedDiameter = 0.0;
    /// Skip bands whose craters are all larger than this (m), unless spacing or `maxCraters` is on,
    /// where larger craters change which smaller ones are kept; craters at or below it are
    /// unchanged. Infinity generates every band.
    double maxBandDiameter = std::numeric_limits<double>::infinity();
    /// Where each cell's craters come from; empty draws them with `GenerateCellCraters`.
    CellCraterSource cellCraters{};
    /// The share of generated craters like each one kept, in [0, 1], from its position and size:
    /// drawing at the density's highest local level and keeping each crater with its share, by a
    /// uniform keyed to its ID, makes the density vary over the surface. Must be thread-safe and
    /// depend only on the crater; unset keeps every crater.
    CraterKeepShare keepShare{};
    /// The diameter (m) from which `manual` craters are a complete catalog at each point: generated
    /// craters at least this large centerd there are dropped, so the catalog stands in for them
    /// rather than adding to them. Infinite or NaN where there is no catalog; unset keeps every
    /// generated crater. A DEM's own craters are such a catalog (`UnresolvedCraterLimit`), with
    /// or without `manual` craters.
    std::shared_ptr<const SurfaceField> completeness{};
    /// What the region and margins are measured on: on a sphere, regions are longitude and
    /// latitude boxes and margins are meters, and `cells` must be `CubeSphereCells` of its radius.
    Surface surface{};
    /// What the drawn features are called in the log: its category and the noun of its counts.
    std::string logName = "craters";
};

/// Checks crater generation options.
/// @throws std::invalid_argument for invalid spacing, a non-positive `dMin`, a `dMax` not above
/// it, a negative margin, expected limit or resolved diameter, or a non-positive cell size factor
/// or largest band.
void ValidateCraterGenerationOptions(const CraterGenerationOptions& options);

/// The largest diameter generated in meters: `dMax`, or for an infinite `dMax` the top of the
/// density's valid range; infinite when both are unbounded.
double ResolveDMax(double dMax, const SizeFrequencyDistribution& density);

/// The smallest diameter in [`dMin`, `dMax`) meters from which `density` expects about `count`
/// craters over `area` m^2, found by bisection in log diameter; `dMin` if even that expects fewer.
///
/// Raising `dMin` to it bounds the count while keeping the distribution's shape and tile joins.
/// @throws std::invalid_argument unless `count` and `area` are positive and finite and
/// 0 < `dMin` < `dMax`.
/// @throws std::domain_error if the interval leaves the density's valid range.
double CountLimitDiameter(const SizeFrequencyDistribution& density, double area, double count,
                          double dMin, double dMax);

/// Generates the craters of a density whose centers lie in `region` (surface coordinates) or
/// within `marginRadii` radii of it.
///
/// Craters are drawn per cell of each size band from substreams keyed by (`seed`, band, cell), so
/// tiles join exactly, `dMin` and `dMax` do not move craters in whole bands, and the age model
/// never moves a crater. Order: band, cell, index.
/// @param density A `Density` distribution valid over [`dMin`, `dMax`), or extrapolating.
/// @throws std::invalid_argument for invalid options, an empty or non-finite region, a density that
/// is not a `Density`, or an expected count above `maxExpectedCraters`.
/// @throws std::domain_error if the diameter interval leaves the density's valid range.
CraterList GenerateCraters(const SurfaceCells& cells, const Bounds2d& region,
                           const SizeFrequencyDistribution& density, const CraterAgeModel& ages,
                           std::uint32_t seed, const CraterGenerationOptions& options);

}  // namespace terraingen
