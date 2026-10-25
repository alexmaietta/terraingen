// SPDX-License-Identifier: BSD-3-Clause

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
#include <optional>
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
    /// Varies the noise of every crater; unset follows the crater layer's `seed`, and is 0
    /// outside a layer.
    std::optional<std::uint32_t> seed{};

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
    /// Varies the texture of every crater; unset follows the crater layer's `seed`, and is 0
    /// outside a layer.
    std::optional<std::uint32_t> seed{};

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

/// Slump terraces on the walls of large craters: benches and scarps stepping the wall between the
/// floor and the rim crest, as the rim and upper wall slump inward after the cavity forms. Each
/// terraced crater draws its number of benches, the arcs of its wall they cover and how their
/// radii wander around the rim from its ID; terraces soften and vanish as the crater wears. The
/// floor edge is the complex profile's floor diameter, else Pike's (1980, USGS PP 1046-C, eq. 15)
/// 0.187 D^1.249 km.
struct CraterTerraces {
    bool enabled = false;
    /// Smallest terraced crater (m); by default 20 km, where half of fresh lunar mare craters are
    /// terraced (Smith and Hartnell 1978); about 8 km on Mars (Pike 1980, LPSC 11, Table 3).
    double onsetDiameter = 20000.0;
    /// Diameter (m) from which every crater has its most benches, over most of its wall; by
    /// default 65 km, from which every fresh lunar crater is terraced (Smith and Hartnell 1978).
    double fullDiameter = 65000.0;
    int minBenches = 1;  ///< Benches at the onset diameter.
    int maxBenches = 4;  ///< Benches from the full diameter.
    /// Slope of a bench (degrees); terraces are mapped as surfaces under 5 degrees (Du et al.
    /// 2024, LPSC 55, abstract 1382).
    double benchSlopeDegrees = 3.0;
    double scarpShare = 0.35;  ///< Share of each step's width the scarp takes, in (0, 1).
    /// Share of the wall's circumference each bench covers at the onset diameter, in (0, 1],
    /// rising to 0.9 at the full diameter; 1 sets benches all the way round.
    double arcShare = 0.6;
    double rimVariation = 0.08;  ///< How far the bench radii wander around the rim, in radii.
    /// How far each slump block's headscarp steps in or out from its neighbors', as a share of
    /// the room between scarps, in [0, 1]; 0 keeps every scarp one unbroken scalloped line.
    double blockOffset = 0.6;
    /// Share of slump blocks missing from each bench, in [0, 1), so benches break into segments
    /// with the wall unstepped between them; 0 keeps every block.
    double blockGaps = 0.25;
    /// Freshness (current over fresh depth) below which the terraces are gone; they soften from
    /// 1 down to it.
    double minFreshness = 0.3;
    std::optional<std::uint32_t> seed{};  ///< Unset follows the crater layer's seed.

    /// Memberwise equality.
    bool operator==(const CraterTerraces& other) const {
        return enabled == other.enabled && onsetDiameter == other.onsetDiameter &&
               fullDiameter == other.fullDiameter && minBenches == other.minBenches &&
               maxBenches == other.maxBenches && benchSlopeDegrees == other.benchSlopeDegrees &&
               scarpShare == other.scarpShare && arcShare == other.arcShare &&
               rimVariation == other.rimVariation && blockOffset == other.blockOffset &&
               blockGaps == other.blockGaps && minFreshness == other.minFreshness &&
               seed == other.seed;
    }
    bool operator!=(const CraterTerraces& other) const {
        return !(*this == other);
    }
};

/// Checks crater terraces.
/// @throws std::invalid_argument for a non-positive onset diameter or a full diameter not above
/// it, benches outside 1 <= min <= max <= 12, a bench slope outside (0, 45) degrees, a scarp share
/// outside (0, 1), an arc share outside (0, 1], a negative or non-finite rim variation, or a
/// minimum freshness outside [0, 1).
void ValidateCraterTerraces(const CraterTerraces& terraces);

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
    /// keeps it. By default 0, so craters cut into a datum keep the older relief on their floor
    /// and walls; 1 gives a clean cut.
    double interiorReplacement = 0.0;
    CraterBlend blend{};  ///< Where the replaced interior blends out.
    /// Irregular, non-circular outlines; off by default.
    CraterIrregularity irregularity{};
    RimSmoothing rimSmoothing{};  ///< Rounded rims; off by default.
    CraterNoise noise{};          ///< Fractal noise over each crater; off by default.
    CraterTexture texture{};      ///< Wall gullies, ejecta texture and burial; off by default.
    /// Floors offset downslope, rims lowered upslope and faster wear on the slope of the ground
    /// beneath the layer; needs the `Plane` datum.
    CraterSlopeResponse slope{};
    CraterTerraces terraces{};  ///< Slump terraces on the walls of large craters; off by default.
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
               terraces == other.terraces && surface == other.surface;
    }
    bool operator!=(const CraterApplyOptions& other) const {
        return !(*this == other);
    }
};

/// How a crater layer shapes its craters by default: irregular outlines and wall terraces on
/// (Bottke et al. 2000, https://doi.org/10.1006/icar.1999.6323).
CraterApplyOptions DefaultCraterApply();

/// `apply` with each unset seed (irregularity, noise, texture, slope and terraces) drawn from the
/// layer's `seed`, each on its own substream, so changing the layer's seed changes them too.
CraterApplyOptions SeededCraterApply(CraterApplyOptions apply, std::uint32_t seed);

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
