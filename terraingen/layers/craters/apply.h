#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/raster/raster.h"

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

/// Shape of the transition from the replaced interior to the draped exterior.
enum class BlendShape {
    Linear,        ///< Straight ramp; continuous, with kinks at both ends.
    Smoothstep,    ///< Cubic 3t^2 - 2t^3; continuous first derivative.
    Smootherstep,  ///< Quintic 6t^5 - 15t^4 + 10t^3; continuous first and second derivatives.
    Gaussian,      ///< Gaussian falloff with sigma a third of the band, rescaled to end at zero.
};

/// Where and how a crater's replaced interior blends into the surrounding terrain, in crater radii.
///
/// The interior weight is 1 up to `startRadii` and falls to 0 at `endRadii` along `shape`. Starting
/// inside the rim and ending outside it smooths the join between the cut interior and the draped
/// rim and ejecta.
///
/// References:
/// - D. S. Ebert, F. K. Musgrave, D. Peachey, K. Perlin and S. Worley, "Texturing and Modeling: A
///   Procedural Approach", 3rd ed., Morgan Kaufmann, 2003 (smoothstep).
/// - K. Perlin, "Improving Noise", ACM Transactions on Graphics 21(3):681-682, 2002 (quintic
///   smootherstep). https://doi.org/10.1145/566654.566636
struct CraterBlend {
    double startRadii = 0.8;
    double endRadii = 1.2;
    BlendShape shape = BlendShape::Smootherstep;

    bool operator==(const CraterBlend&) const = default;
};

/// Interior weight at `radii` crater radii from the centre: 1 up to `blend.startRadii`, 0 from
/// `blend.endRadii`, and `blend.shape` in between.
double CraterBlendWeight(double radii, const CraterBlend& blend);

/// How features too small to resolve at the sample spacing fade out, by how many samples span
/// their diameter: absent up to `startSamples`, whole from `endSamples`, and `shape` in between.
///
/// Fading rather than cutting keeps levels of detail consistent: a crater sampled at a coarse and
/// at a fine spacing changes continuously instead of appearing or vanishing at one spacing.
struct ResolutionFade {
    double startSamples = 2.0;
    double endSamples = 4.0;
    BlendShape shape = BlendShape::Smoothstep;

    bool operator==(const ResolutionFade&) const = default;
};

/// @throws std::invalid_argument unless 0 <= `startSamples` < `endSamples`, both finite.
void ValidateResolutionFade(const ResolutionFade& fade);

/// The weight of a feature `diameter` across sampled every `spacing` (same units): 0 up to
/// `fade.startSamples` samples across it, 1 from `fade.endSamples`, and `fade.shape` in between.
double ResolutionWeight(double diameter, double spacing, const ResolutionFade& fade);

/// Settings for `ApplyCraters`.
struct CraterApplyOptions {
    CraterOrder order = CraterOrder::OldestFirst;
    /// How craters too small to resolve at the sample spacing fade out.
    ResolutionFade fade{};
    /// Reference surface for the interior; with `None` craters are purely additive and the
    /// settings below are unused.
    CraterDatum datum = CraterDatum::None;
    /// Footprint the datum is fitted over, in crater radii.
    double datumFitRadii = 1.0;
    /// How much of the interior is replaced by datum plus profile: 1 erases older relief inside the
    /// crater, as an impact excavates it; 0 keeps it; values in between blend.
    double interiorReplacement = 1.0;
    CraterBlend blend{};

    bool operator==(const CraterApplyOptions&) const = default;
};

/// @throws std::invalid_argument for an invalid `fade`, a non-positive `datumFitRadii`, an
/// `interiorReplacement` outside [0, 1], or a blend that does not satisfy 0 <= `startRadii` <
/// `endRadii`.
void ValidateCraterApplyOptions(const CraterApplyOptions& options);

/// Applies `craters` to `raster`, whose `GeoTransform` maps pixels to the craters' surface
/// coordinates, shaping each crater with `profile` in the state `degradation` gives it.
///
/// Craters are applied in turn. Without a datum, each crater's profile is added to the terrain.
/// With a datum, the datum is fitted to the terrain within `datumFitRadii` of the centre, and the
/// terrain inside the crater is replaced by the datum plus the profile, weighted by
/// `interiorReplacement` times `CraterBlendWeight`; beyond the blend the profile is added. A plane
/// datum follows regional slopes, the approach of Martin, Parkes and Dunstan (2014), DLES terrain
/// crafting (Foreman et al. 2025) and Furlong et al. (2019). Cells holding the raster's no-data
/// value are left unchanged and excluded from the fit.
///
/// Each crater's change is weighted by `ResolutionWeight(diameter, spacing, fade)`, so craters too
/// small to resolve at the sample `spacing` (world units; 0 means the raster's cell size, and a
/// finer spacing is raised to it) fade out. Without a datum every cell depends only on the craters
/// reaching it, so a tile of a larger raster gets the same cells as the larger raster.
///
/// Craters overlapping the raster edge are fitted to the cells available, so with a datum a crater
/// near the edge of one raster can differ slightly from the same crater in a larger raster.
///
/// References:
/// - S. Martin, S. Parkes and M. Dunstan, "Modeling cratered surfaces with real and synthetic
///   terrain for testing planetary landers", IEEE Transactions on Aerospace and Electronic Systems
///   50(4):2916-2928, 2014. https://doi.org/10.1109/TAES.2014.120282
/// - C. Foreman, J. Kincaid, E. Z. Crues, D. Fenn and A. Joshi, "Digital Lunar Exploration Sites
///   (DLES) Terrain Crafting", IEEE Aerospace Conference, 2025.
///   https://doi.org/10.1109/AERO63441.2025.11068736
/// - P. M. Furlong, U. Wong, M. Allan et al., "Planetary Rover Simulation for Lunar Exploration
///   Missions", IEEE Aerospace Conference, 2019. https://doi.org/10.1109/AERO.2019.8741780
///
/// @throws std::invalid_argument for invalid `options` or a negative or non-finite `spacing`.
/// @throws std::domain_error if the raster's transform is not invertible.
void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const DegradationModel& degradation, const CraterApplyOptions& options = {},
                  double spacing = 0.0);

/// `ApplyCraters` with every crater fresh (`NoDegradation`).
void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const CraterApplyOptions& options = {}, double spacing = 0.0);

/// The order `ApplyCraters` applies `craters` in under `order`, as indices into `craters`.
std::vector<std::size_t> CraterApplyOrder(const CraterList& craters, CraterOrder order);

}  // namespace terraingen
