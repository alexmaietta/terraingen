/// @file
/// @brief How craters are applied to the terrain: their order, datum, blend and outlines.
#pragma once

#include "terraingen/layers/craters/irregularity.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/fade.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <vector>

namespace terraingen {

/// The order in which overlapping craters are applied; later craters overprint earlier ones.
enum class CraterOrder {
    OldestFirst,   ///< Decreasing age, then decreasing diameter: the physical order.
    LargestFirst,  ///< Decreasing diameter, then decreasing age.
};

/// The reference surface a crater's interior is cut relative to.
enum class CraterDatum {
    None,          ///< No datum: the profile is added onto the terrain everywhere.
    Plane,         ///< Least-squares plane over the fit footprint; keeps regional slopes.
    MeanHeight,    ///< Flat datum at the mean height over the fit footprint.
    CentreHeight,  ///< Flat datum at the terrain height under the crater centre.
};

/// Where and how a crater's replaced interior blends into the surrounding terrain, in crater radii:
/// the interior weight is 1 up to `startRadii` and falls to 0 at `endRadii` along `shape`.
/// See SCIENCE.md, "Crater datums and blends".
struct CraterBlend {
    double startRadii = 0.8;
    double endRadii = 1.2;
    BlendShape shape = BlendShape::Smootherstep;

    /// @brief The interior weight at a distance from the centre.
    /// @param radii The distance, in crater radii.
    /// @return 1 up to `startRadii`, 0 from `endRadii`, and `shape` in between.
    double Weight(double radii) const {
        return Falloff(radii, startRadii, endRadii, shape);
    }

    bool operator==(const CraterBlend&) const = default;
};

/// How `ApplyCraters` shapes craters into the terrain.
struct CraterApplyOptions {
    CraterOrder order = CraterOrder::OldestFirst;
    /// How craters too small to resolve at the sample spacing fade out.
    ResolutionFade fade{};
    /// Reference surface for the interior, fitted to the ground beneath the craters (see
    /// `FitCraterDatum`); with `None` craters are purely additive and the settings below are
    /// unused.
    CraterDatum datum = CraterDatum::None;
    /// Footprint the datum is fitted over, in crater radii.
    double datumFitRadii = 1.0;
    /// How much of the interior is replaced by datum plus profile: 1 erases older relief inside the
    /// crater, as an impact excavates it; 0 keeps it; values in between blend.
    double interiorReplacement = 1.0;
    CraterBlend blend{};
    /// Irregular, non-circular outlines; off by default.
    CraterIrregularity irregularity{};
    /// What craters are shaped on: on a sphere, crater centres and rasters are in longitude and
    /// latitude, and each crater is shaped in its local frame (`Surface::Offset`), with spacings in
    /// metres.
    Surface surface{};

    bool operator==(const CraterApplyOptions&) const = default;
};

/// @brief Checks crater apply options.
/// @param options The options to check.
/// @throws std::invalid_argument for an invalid `fade` or `irregularity`, a non-positive
/// `datumFitRadii`, an `interiorReplacement` outside [0, 1], or a blend that does not satisfy
/// 0 <= `startRadii` < `endRadii`.
void ValidateCraterApplyOptions(const CraterApplyOptions& options);

/// @brief The order craters are applied in.
/// @param craters The craters.
/// @param order Which craters go first; ties fall to age, then diameter, then the smaller ID.
/// @return Indices into `craters`, in the order applied.
std::vector<std::size_t> CraterApplyOrder(const CraterList& craters, CraterOrder order);

}  // namespace terraingen
