#pragma once

#include "terraingen/layers/craters/degradation/degradation_model.h"
#include "terraingen/layers/craters/population.h"
#include "terraingen/layers/craters/profiles/profile.h"
#include "terraingen/raster/raster.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
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

    bool operator==(const CraterApplyOptions&) const = default;
};

/// @throws std::invalid_argument for an invalid `fade`, a non-positive `datumFitRadii`, an
/// `interiorReplacement` outside [0, 1], or a blend that does not satisfy 0 <= `startRadii` <
/// `endRadii`.
void ValidateCraterApplyOptions(const CraterApplyOptions& options);

/// Heights of the ground beneath a layer at world positions, NaN where there is none; must be safe
/// to call from several threads at once.
using HeightSampler = std::function<float(double x, double y)>;

/// The datum `options` asks for under `crater`, fitted to `ground`: its height at the centre
/// (`CentreHeight`), or the mean (`MeanHeight`) or least-squares plane (`Plane`) of its heights at
/// the points of a 7 x 7 lattice spanning the footprint of `datumFitRadii` radii that lie inside
/// it. The lattice depends only on the crater, so the datum is the same on every grid, tile and
/// level of detail. Nothing for `None`, or where the ground has no data.
std::optional<std::array<double, 3>> FitCraterDatum(const Crater& crater,
                                                    const CraterApplyOptions& options,
                                                    const HeightSampler& ground);

/// The craters of a list in the order they are applied, each with its resolution weight and
/// degradation state, indexed by position so that the relief of the craters applied before any one
/// of them can be summed at a point quickly. Craters are bucketed by size band, each band in square
/// buckets twice its largest reach, so a point reads only the nine buckets around it per band.
class CraterInfluence {
  public:
    /// One crater with its weight at the evaluation's spacing and its state.
    struct Entry {
        Crater crater;
        double resolution = 1.0;
        CraterState state;
    };

    /// `entries` in the order they are applied; `profile` must outlive the index.
    CraterInfluence(std::vector<Entry> entries, const CraterProfile& profile);

    /// The sum of `resolution` times the profile height at (`x`, `y`) of the entries before entry
    /// `k` that reach it, in a fixed order that does not depend on which other craters the list
    /// holds, so lists sharing the craters near a point give the same bits there.
    double ReliefBefore(double x, double y, std::size_t k) const;

    const std::vector<Entry>& Entries() const {
        return entries_;
    }

  private:
    struct Key {
        int band;
        std::int64_t i;
        std::int64_t j;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        std::size_t operator()(const Key& key) const;
    };

    std::vector<Entry> entries_;
    const CraterProfile& profile_;
    std::vector<int> bands_;
    std::unordered_map<Key, std::vector<std::size_t>, KeyHash> buckets_;
};

/// The height datums are fitted to under entry `k` of `influence`: `ground` plus the relief of the
/// craters applied before it, as if those were added without their own datums. A crater landing in
/// an earlier crater's bowl is thus cut relative to the bowl, while its datum still depends only on
/// the ground and the craters near it, so it is the same on every grid, tile and point.
HeightSampler DatumGround(const HeightSampler& ground, const CraterInfluence& influence,
                          std::size_t k);

/// The distance beyond a grid, in world units, within which craters with a datum can change it
/// through their datum fit: the fit footprint plus the profile's reach, for craters up to
/// `maxDiameter`. Craters for a grid or point should be generated this far around it.
double DatumMargin(const CraterApplyOptions& options, const CraterProfile& profile,
                   double maxDiameter);

/// `height` at (`x`, `y`) after `crater` in `state`, with weight `resolution` and `datum` (from
/// `FitCraterDatum`): what `ApplyCraters` does to one cell, shared with point queries so both give
/// the same bits.
float ApplyCraterTo(float height, double x, double y, const Crater& crater,
                    const CraterState& state, double resolution,
                    const std::optional<std::array<double, 3>>& datum, const CraterProfile& profile,
                    const CraterApplyOptions& options);

/// Applies `craters` to `raster`, whose `GeoTransform` maps pixels to the craters' surface
/// coordinates, shaping each crater with `profile` in the state `degradation` gives it.
///
/// Craters are applied in turn. Without a datum, each crater's profile is added to the terrain.
/// With a datum, the datum is fitted (`FitCraterDatum`) to the ground beneath the craters (`ground`
/// when given, otherwise `raster` as passed in, interpolated bilinearly) plus the relief of the
/// craters before it (`DatumGround`). The terrain inside the crater is replaced by the datum plus
/// the profile, weighted by `interiorReplacement` times `CraterBlendWeight`; beyond the blend the
/// profile is added. Craters off the raster still count toward the datums of craters on it, so
/// callers pass the craters within `DatumMargin` of the raster. A plane datum follows regional
/// slopes, the approach of Martin, Parkes and Dunstan (2014), DLES terrain crafting (Foreman et al.
/// 2025) and Furlong et al. (2019). Cells holding the raster's no-data value are left unchanged and
/// excluded from the fit.
///
/// Each crater's change is weighted by `ResolutionWeight(diameter, spacing, fade)`, so craters too
/// small to resolve at the sample `spacing` (world units; 0 means the raster's cell size, and a
/// finer spacing is raised to it) fade out. Every cell depends only on the craters reaching it and
/// their datums, so with a `ground` that covers the footprints a tile of a larger raster gets the
/// same cells as the larger raster; without one, footprints beyond the raster's edge are fitted to
/// the cells available.
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
/// Datums are fitted and rows applied over up to `threads` threads (0 for every hardware thread),
/// each applying the craters in the same order, so the result does not depend on `threads`.
///
/// @throws std::invalid_argument for invalid `options` or a negative or non-finite `spacing`.
/// @throws std::domain_error if the raster's transform is not invertible.
void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const DegradationModel& degradation, const CraterApplyOptions& options = {},
                  double spacing = 0.0, unsigned threads = 1, const HeightSampler& ground = {});

/// `ApplyCraters` with every crater fresh (`NoDegradation`).
void ApplyCraters(Raster<float>& raster, const CraterList& craters, const CraterProfile& profile,
                  const CraterApplyOptions& options = {}, double spacing = 0.0,
                  unsigned threads = 1, const HeightSampler& ground = {});

/// The order `ApplyCraters` applies `craters` in under `order`, as indices into `craters`.
std::vector<std::size_t> CraterApplyOrder(const CraterList& craters, CraterOrder order);

}  // namespace terraingen
