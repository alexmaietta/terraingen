/// @file
/// @brief Craters and crater lists, and generating crater populations cell by cell.
#pragma once

#include "terraingen/distribution/size_frequency.h"
#include "terraingen/layers/craters/ages/age_model.h"
#include "terraingen/util/cells.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <vector>

namespace terraingen {

/// @brief One crater: centre in surface coordinates, rim-to-rim diameter, age and a stable ID.
struct Crater {
    double x = 0.0;         ///< Surface x of the centre.
    double y = 0.0;         ///< Surface y of the centre.
    double diameter = 0.0;  ///< Rim-to-rim diameter (m).
    double ageGyr = 0.0;    ///< Age (Gyr).
    std::uint64_t id = 0;   ///< Stable ID.
    /// Placed by hand, identified in imagery or read from a catalog rather than generated; takes
    /// precedence over generated craters and is never removed by spacing.
    bool manual = false;

    bool operator==(const Crater&) const = default;
};

/// @brief A stable ID for a manual crater that has none, hashed with `Philox4x32`.
/// @param x Surface x of the centre.
/// @param y Surface y of the centre.
/// @param diameter Diameter, in metres.
/// @return The same ID for the same crater in every run.
std::uint64_t ManualCraterId(double x, double y, double diameter);

/// @brief Craters stored as one array per field, so each column can be shared with NumPy or a GPU
/// without copying.
class CraterList {
  public:
    /// @brief The number of craters.
    /// @return The size.
    std::size_t Size() const {
        return id_.size();
    }

    /// @brief Whether the list holds no crater.
    /// @return True when empty.
    bool Empty() const {
        return id_.empty();
    }

    /// @brief Reserves room in every column.
    /// @param count Craters to make room for.
    void Reserve(std::size_t count);

    /// @brief Appends a crater.
    /// @param crater The crater.
    void Append(const Crater& crater);

    /// @brief Appends every crater of another list, in its order.
    /// @param other The list.
    void Append(const CraterList& other);

    /// @brief The crater at an index.
    /// @param index The index.
    /// @return The crater.
    /// @throws std::out_of_range if `index` >= `Size()`.
    Crater At(std::size_t index) const;

    /// @brief The x column.
    /// @return Surface x of every centre.
    const std::vector<double>& X() const {
        return x_;
    }

    /// @brief The y column.
    /// @return Surface y of every centre.
    const std::vector<double>& Y() const {
        return y_;
    }

    /// @brief The diameter column.
    /// @return Every diameter, in metres.
    const std::vector<double>& Diameters() const {
        return diameter_;
    }

    /// @brief The age column.
    /// @return Every age, in Gyr.
    const std::vector<double>& AgesGyr() const {
        return ageGyr_;
    }

    /// @brief The ID column.
    /// @return Every ID.
    const std::vector<std::uint64_t>& Ids() const {
        return id_;
    }

    /// @brief The manual column, as bytes so it can be shared without copying.
    /// @return 1 for manual craters, 0 for generated ones.
    const std::vector<std::uint8_t>& Manual() const {
        return manual_;
    }

  private:
    std::vector<double> x_;
    std::vector<double> y_;
    std::vector<double> diameter_;
    std::vector<double> ageGyr_;
    std::vector<std::uint64_t> id_;
    std::vector<std::uint8_t> manual_;
};

/// @brief The craters a predicate keeps, in their order.
/// @param craters The craters.
/// @param keep `keep(index, crater)` returning whether to keep the crater at `index`.
/// @return The craters kept.
template <typename Keep> CraterList FilterCraters(const CraterList& craters, const Keep& keep) {
    CraterList kept;
    kept.Reserve(craters.Size());
    for (std::size_t n = 0; n < craters.Size(); ++n) {
        const Crater crater = craters.At(n);
        if (keep(n, crater)) {
            kept.Append(crater);
        }
    }
    return kept;
}

/// @brief Appends manual craters, flagged manual and given a `ManualCraterId` where their ID is 0.
/// @param manual The manual craters.
/// @param craters The list appended to.
/// @param keep Which craters to append; empty for all.
/// @throws std::invalid_argument for a crater with a non-finite position, a diameter that is not
/// positive and finite, or a negative or non-finite age, whether `keep` accepts it or not.
void AppendManualCraters(const CraterList& manual, CraterList& craters,
                         const std::function<bool(const Crater&)>& keep = {});

/// @brief Keeps craters of similar size apart: two conflict when their diameters differ by at most
/// `diameterRatio` and their centres are closer than `separation` times the larger diameter.
struct CraterSpacing {
    CraterSpacing() = default;
    /// @brief Spacing settings.
    /// @param ratio The `diameterRatio`.
    /// @param minSeparation The `separation`.
    CraterSpacing(double ratio, double minSeparation)
        : diameterRatio(ratio), separation(minSeparation) {}

    double diameterRatio = 1.5;  ///< At least 1.
    double separation = 0.0;     ///< In larger diameters; 0 disables spacing.

    /// @brief Whether spacing applies.
    /// @return True for a positive `separation`.
    bool Enabled() const {
        return separation > 0.0;
    }

    bool operator==(const CraterSpacing&) const = default;
};

/// @brief A size band of crater generation: diameters [`low`, `high`) metres, from 2^`exponent` to
/// 2^(`exponent` + 1) clipped to the requested range.
struct CraterBand {
    int exponent = 0;       ///< The band's power of two.
    double low = 0.0;       ///< Smallest diameter (m).
    double high = 0.0;      ///< Largest diameter (m), exclusive.
    double cellSize = 0.0;  ///< Side of the cells the band is drawn in (m).

    bool operator==(const CraterBand&) const = default;
};

/// @brief The bands `GenerateCraters` uses.
/// @param dMin Smallest diameter, in metres.
/// @param dMax Largest diameter, exclusive, in metres.
/// @param cellSizeFactor Cell side over each band's largest whole diameter.
/// @return The bands covering [`dMin`, `dMax`), smallest first.
std::vector<CraterBand> CraterBands(double dMin, double dMax, double cellSizeFactor);

/// @brief The craters `GenerateCraters` draws in one cell of one band, before regions, manual
/// craters, spacing and caps apply.
/// @param cells The cells craters are drawn in.
/// @param band The size band.
/// @param cell The cell.
/// @param density The crater density.
/// @param ages The age model.
/// @param seed The generation seed.
/// @return The craters, in the order drawn.
CraterList GenerateCellCraters(const SurfaceCells& cells, const CraterBand& band,
                               const CellIndex& cell, const SizeFrequencyDistribution& density,
                               const CraterAgeModel& ages, std::uint32_t seed);

/// @brief Supplies the craters of one cell of one band as `GenerateCellCraters` would, for example
/// from a cache; must be thread-safe.
using CellCraterSource =
    std::function<std::shared_ptr<const CraterList>(const CraterBand& band, const CellIndex& cell)>;

/// @brief Which craters `GenerateCraters` draws, and how.
struct CraterGenerationOptions {
    /// Smallest diameter generated, in metres; must be set to a positive value.
    double dMin = 0.0;
    /// Largest diameter, exclusive; infinity means the top of the density's valid range, which must
    /// then be finite.
    double dMax = std::numeric_limits<double>::infinity();
    /// Also keep craters centred outside the region within this many radii of it.
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
    /// tiles joined; `MinDiameterForCount` bounds counts tile-safely.
    std::size_t maxCraters = 0;
    /// Skip bands whose craters are all smaller than this and, with spacing, too small to remove a
    /// crater this large; craters at or above it are unchanged. 0 generates every band.
    double minResolvedDiameter = 0.0;
    /// Where each cell's craters come from; empty draws them with `GenerateCellCraters`.
    CellCraterSource cellCraters{};
    /// What the region and margins are measured on: on a sphere, regions are longitude and
    /// latitude boxes and margins are metres, and `cells` must be `CubeSphereCells` of its radius.
    Surface surface{};
};

/// @brief The largest diameter generated: `dMax`, or for an unbounded `dMax` the top of the
/// density's valid range.
/// @param dMax The requested largest diameter, in metres; may be infinite.
/// @param density The crater density.
/// @return The diameter, in metres; infinite when both are unbounded.
double ResolveDMax(double dMax, const SizeFrequencyDistribution& density);

/// @brief The smallest diameter from which a density expects about a count of craters over an
/// area, found by bisection in log diameter.
///
/// Raising `dMin` to it bounds the count while keeping the distribution's shape and tile joins.
/// @param density The crater density.
/// @param area The area, in m^2.
/// @param count The number of craters wanted.
/// @param dMin Smallest diameter searched, in metres.
/// @param dMax Largest diameter searched, in metres.
/// @return The diameter in [`dMin`, `dMax`), in metres; `dMin` if even that expects fewer.
/// @throws std::invalid_argument unless `count` and `area` are positive and finite and
/// 0 < `dMin` < `dMax`.
/// @throws std::domain_error if the interval leaves the density's valid range.
double MinDiameterForCount(const SizeFrequencyDistribution& density, double area, double count,
                           double dMin, double dMax);

/// @brief Generates the craters of a density whose centres lie in a region or within
/// `marginRadii` radii of it.
///
/// Craters are drawn per cell of each size band from substreams keyed by (`seed`, band, cell), so
/// tiles join exactly, `dMin` and `dMax` do not move craters in whole bands, and the age model
/// never moves a crater. Order: band, cell, index.
/// @param cells The cells craters are drawn in.
/// @param region The region, in surface coordinates.
/// @param density A `Density` distribution valid over [`dMin`, `dMax`), or extrapolating.
/// @param ages The age model.
/// @param seed The generation seed.
/// @param options Diameters, margins, spacing, manual craters and caps.
/// @return The craters.
/// @throws std::invalid_argument for invalid options, an empty or non-finite region, a density that
/// is not a `Density`, or an expected count above `maxExpectedCraters`.
/// @throws std::domain_error if the diameter interval leaves the density's valid range.
CraterList GenerateCraters(const SurfaceCells& cells, const Bounds2d& region,
                           const SizeFrequencyDistribution& density, const CraterAgeModel& ages,
                           std::uint32_t seed, const CraterGenerationOptions& options);

}  // namespace terraingen
