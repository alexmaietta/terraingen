/// @file
/// @brief How craters are applied to the terrain: their order, datum, blend, outlines and rims.
#pragma once

#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/craters/shaping/irregularity.h"
#include "terraingen/layers/craters/shaping/slope.h"
#include "terraingen/layers/fade.h"
#include "terraingen/util/surface.h"

#include <cstddef>
#include <cstdint>
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
    CenterHeight,  ///< Flat datum at the terrain height under the crater center.
};

/// Where a crater's replaced interior blends into the surrounding terrain, in crater radii.
struct CraterBlend {
    double startRadii = 0.8;                      ///< Interior weight is 1 up to here.
    double endRadii = 1.2;                        ///< And 0 from here; above `startRadii`.
    BlendShape shape = BlendShape::Smootherstep;  ///< The curve in between.

    /// Memberwise equality.
    bool operator==(const CraterBlend& other) const {
        return startRadii == other.startRadii && endRadii == other.endRadii && shape == other.shape;
    }
    bool operator!=(const CraterBlend& other) const {
        return !(*this == other);
    }
};

/// How crater rims are rounded: near the rim, the profile is blended toward itself smoothed across
/// distance by a Gaussian, with full weight at the rim fading to none at the band's edges.
struct RimSmoothing {
    /// Gaussian standard deviation in crater radii; 0 leaves rims sharp.
    double sigmaRadii = 0.0;
    double innerRadii = 0.8;  ///< Inner edge of the smoothed band, in crater radii; in [0, 1).
    double outerRadii = 1.2;  ///< Outer edge of the smoothed band, in crater radii; above 1.
    BlendShape shape = BlendShape::Smootherstep;  ///< How the weight falls toward the edges.
    double strength = 1.0;  ///< Share of the smoothed height used at the rim, in [0, 1].

    /// Memberwise equality.
    bool operator==(const RimSmoothing& other) const {
        return sigmaRadii == other.sigmaRadii && innerRadii == other.innerRadii &&
               outerRadii == other.outerRadii && shape == other.shape && strength == other.strength;
    }
    bool operator!=(const RimSmoothing& other) const {
        return !(*this == other);
    }
};

/// Checks rim smoothing settings.
/// @throws std::invalid_argument for a negative or non-finite `sigmaRadii`, a `strength` outside
/// [0, 1], or a band that does not satisfy 0 <= `innerRadii` < 1 < `outerRadii`.
void ValidateRimSmoothing(const RimSmoothing& smoothing);

/// Fractal noise added to each crater, keyed to its ID so tiles and point queries agree: over the
/// whole footprint, fading out to nothing at the profile's reach, and in a band around the rim.
/// Amplitudes are shares of the crater's diameter, so the noise scales with the crater.
struct CraterNoise {
    double amplitude = 0.0;           ///< Footprint noise amplitude over the diameter; 0 adds none.
    double wavelengthRadii = 0.5;     ///< Wavelength of its first octave, in crater radii.
    int octaves = 4;                  ///< Octaves of both noises, 1 to 12.
    double gain = 0.5;                ///< Weight ratio between successive octaves; positive.
    double rimAmplitude = 0.0;        ///< Rim noise amplitude over the diameter; 0 adds none.
    double rimWavelengthRadii = 0.1;  ///< Wavelength of its first octave, in crater radii.
    double rimInnerRadii = 0.8;       ///< Inner edge of the rim band, in crater radii; in [0, 1).
    double rimOuterRadii = 1.2;       ///< Outer edge of the rim band, in crater radii; above 1.
    std::uint32_t seed = 0;           ///< Varies the noise of every crater.

    /// Memberwise equality.
    bool operator==(const CraterNoise& other) const {
        return amplitude == other.amplitude && wavelengthRadii == other.wavelengthRadii &&
               octaves == other.octaves && gain == other.gain &&
               rimAmplitude == other.rimAmplitude &&
               rimWavelengthRadii == other.rimWavelengthRadii &&
               rimInnerRadii == other.rimInnerRadii && rimOuterRadii == other.rimOuterRadii &&
               seed == other.seed;
    }
    bool operator!=(const CraterNoise& other) const {
        return !(*this == other);
    }
};

/// Checks crater noise settings.
/// @throws std::invalid_argument for a negative or non-finite amplitude, a non-positive
/// wavelength or gain, octaves outside 1 to 12, or a rim band that does not satisfy
/// 0 <= `rimInnerRadii` < 1 < `rimOuterRadii`.
void ValidateCraterNoise(const CraterNoise& noise);

/// The texture of a crater's walls and ejecta, keyed to its ID: radial gullies down the inner
/// wall, hummocky ejecta lineated away from the rim, and older relief buried under the ejecta.
/// Ejecta follow McGetchin et al.'s (1973) thickness 0.14 R^0.74 (r / R)^-3 m, tapered to nothing
/// at `ejectaReachRadii`. Gullies and ejecta texture fade as the crater wears; burial stays.
struct CraterTexture {
    double wallStreaks = 0.0;            ///< Gully amplitude over the diameter; 0 adds none.
    double wallStreakWidthRadii = 0.15;  ///< Width of the gullies across the wall, in radii.
    double ejecta = 0.0;  ///< Ejecta texture amplitude over the ejecta thickness; 0 adds none.
    double ejectaLineationRadii = 0.15;  ///< Width of the lineations across, in radii at the rim.
    /// Share of older relief the ejecta replace by the crater's datum at the rim, falling with
    /// their thickness; 0 buries nothing, and without a datum nothing is buried.
    double mantling = 0.0;
    double ejectaReachRadii = 3.0;  ///< How far ejecta texture and burial reach, in radii.
    /// Freshness (`CraterState::Freshness`) at or below which gullies and ejecta texture are gone,
    /// in [0, 1); they fade in along a smoothstep above it.
    double minFreshness = 0.5;
    std::uint32_t seed = 0;  ///< Varies the texture of every crater.

    /// Whether anything is drawn beyond the profile: ejecta texture or burial.
    bool ReachesBeyondRim() const {
        return ejecta > 0.0 || mantling > 0.0;
    }

    /// Memberwise equality.
    bool operator==(const CraterTexture& other) const {
        return wallStreaks == other.wallStreaks &&
               wallStreakWidthRadii == other.wallStreakWidthRadii && ejecta == other.ejecta &&
               ejectaLineationRadii == other.ejectaLineationRadii && mantling == other.mantling &&
               ejectaReachRadii == other.ejectaReachRadii && minFreshness == other.minFreshness &&
               seed == other.seed;
    }
    bool operator!=(const CraterTexture& other) const {
        return !(*this == other);
    }
};

/// Checks crater texture settings.
/// @throws std::invalid_argument for a negative or non-finite amplitude, a non-positive width, a
/// mantling outside [0, 1], a reach of 1 radius or less, or a freshness outside [0, 1).
void ValidateCraterTexture(const CraterTexture& texture);

/// How `ApplyCraters` shapes craters into the terrain.
struct CraterApplyOptions {
    CraterOrder order = CraterOrder::OldestFirst;  ///< Which craters overprint which.
    /// How craters too small to resolve at the sample spacing fade out.
    ResolutionFade fade{};
    /// Reference surface for the interior (`FitCraterDatum`); with `None` craters are purely
    /// additive and the datum settings below are unused.
    CraterDatum datum = CraterDatum::None;
    /// Footprint the datum is fitted over, in crater radii.
    double datumFitRadii = 1.0;
    /// Share of the interior replaced by datum plus profile, in [0, 1]: 1 erases older relief, 0
    /// keeps it.
    double interiorReplacement = 1.0;
    CraterBlend blend{};  ///< Where the replaced interior blends out.
    /// Irregular, non-circular outlines; off by default.
    CraterIrregularity irregularity{};
    RimSmoothing rimSmoothing{};  ///< Rounded rims; off by default.
    CraterNoise noise{};          ///< Fractal noise over each crater; off by default.
    CraterTexture texture{};      ///< Wall gullies, ejecta texture and burial; off by default.
    /// Floors offset downslope, rims lowered upslope and faster wear on the slope of the ground
    /// beneath the layer; needs the `Plane` datum.
    CraterSlopeResponse slope{};
    /// What craters are shaped on; on a sphere, centers and rasters are longitude and latitude and
    /// each crater is shaped in its local frame, spacings in meters.
    Surface surface{};

    /// Memberwise equality.
    bool operator==(const CraterApplyOptions& other) const {
        return order == other.order && fade == other.fade && datum == other.datum &&
               datumFitRadii == other.datumFitRadii &&
               interiorReplacement == other.interiorReplacement && blend == other.blend &&
               irregularity == other.irregularity && rimSmoothing == other.rimSmoothing &&
               noise == other.noise && texture == other.texture && slope == other.slope &&
               surface == other.surface;
    }
    bool operator!=(const CraterApplyOptions& other) const {
        return !(*this == other);
    }
};

/// Checks crater apply options.
/// @throws std::invalid_argument for an invalid `fade`, `irregularity`, `rimSmoothing` or `slope`,
/// a slope response without the `Plane` datum, a
/// non-positive `datumFitRadii`, an `interiorReplacement` outside [0, 1], or a blend that does not
/// satisfy 0 <= `startRadii` < `endRadii`.
void ValidateCraterApplyOptions(const CraterApplyOptions& options);

/// Indices into `craters` in the order they are applied; ties fall to age, then diameter, then
/// the smaller ID.
std::vector<std::size_t> CraterApplyOrder(const CraterList& craters, CraterOrder order);

}  // namespace terraingen
