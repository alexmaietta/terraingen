/// @file
/// @brief Splotches: the dark and bright marks secondary impacts of loose regolith leave.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/layers/craters/crater.h"
#include "terraingen/layers/fade.h"
#include "terraingen/random/value_distribution.h"
#include "terraingen/util/cells.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace terraingen {

/// Splotches after Speyerer et al. (2016): formed at a cumulative rate of `rateCoefficient`
/// d^-`rateExponent` per km^2 per year for diameter d in km, each lasting `lifetimeMyr`, the
/// time splotches take to churn the top centimetres. Most are dark, some bright and the rest mixed,
/// each nearly round; a younger splotch covers older ones as it fades.
struct SplotchOptions {
    std::uint32_t seed = 0;              ///< Seed of where splotches fall and what they are.
    double rateCoefficient = 1.483e-11;  ///< Formation rate coefficient, per km^2 per year.
    double rateExponent = 4.144;         ///< Cumulative slope of the formation rate; positive.
    double lifetimeMyr = 0.081;          ///< How long a splotch lasts, fading linearly (Myr).
    double minDiameter = 1.0;    ///< Smallest splotch (m); the fit is extrapolated below 10.
    double maxDiameter = 40.0;   ///< Largest splotch (m), above `minDiameter`.
    double darkShare = 0.907;    ///< Share of dark splotches, in [0, 1].
    double brightShare = 0.074;  ///< Share of bright ones; the rest are mixed.
    /// Reflectance change of a dark splotch, relative to the ground before it; above -1.
    ValueDistribution darkChange{ValueDistribution::Kind::Constant, -0.04};
    /// Reflectance change of a bright splotch; above -1.
    ValueDistribution brightChange{ValueDistribution::Kind::Constant, 0.10};
    /// Share of splotches on slopes steeper than `steepSlopeDegrees` that are bright, in [0, 1];
    /// unset keeps `brightShare`. Bright splotches are found mostly on such slopes, where mature
    /// regolith is thin (Speyerer et al. 2016), but the sources give no share; needs the ground.
    std::optional<double> steepBrightShare;
    double steepSlopeDegrees = 15.0;  ///< Slope from which ground is steep, in (0, 90) degrees.
    double softness = 0.3;            ///< Share of the radius over which the edge fades, in (0, 1].
    double raggedness = 0.15;         ///< Share of the radius the edge's lobes pull in, in [0, 1).
    ResolutionFade fade{};            ///< How splotches too small to resolve fade out.

    /// The cumulative number of splotches of diameter at least 1 m per m^2 at any time.
    double Coefficient() const;

    bool operator==(const SplotchOptions&) const = default;
};

/// Checks splotch options.
/// @throws std::invalid_argument for a non-positive rate, slope or lifetime, diameters that are
/// not positive and increasing, shares outside [0, 1] or summing above 1, changes that can reach
/// -1 or are unbounded, a softness outside (0, 1], a raggedness outside [0, 1), a steep bright
/// share outside [0, 1], a steep slope outside (0, 90) degrees or an invalid fade.
void ValidateSplotches(const SplotchOptions& options);

/// One splotch.
struct Splotch {
    Crater mark;               ///< Centre, diameter, age and ID, as a crater's.
    double change = 0.0;       ///< Reflectance change of its dark or only side.
    double otherChange = 0.0;  ///< Reflectance change of its other side; `change` unless mixed.
    double split = 0.0;        ///< Direction across which a mixed splotch changes side, radians.
    std::uint32_t key = 0;     ///< Key of its edge's lobes.
};

/// The splotches of `SplotchOptions` on `surface`, darkening and brightening the albedo; the
/// same in any grid, tile or point.
class SplotchAlbedo final : public AlbedoLayer {
  public:
    /// @throws std::invalid_argument for options invalid as `ValidateSplotches`.
    explicit SplotchAlbedo(SplotchOptions options = {}, Surface surface = {});

    /// Changes the albedo at one point by the splotches over it; NaN stays NaN.
    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override;

    /// `ApplyAt` at every cell centre, generating the splotches once.
    void Apply(std::vector<Raster<float>>& bands, const AlbedoContext& context) const override;

    /// The splotches resolved at `spacing` metres reaching `region`, youngest first; with
    /// `ground`, those on steep slopes follow `steepBrightShare`.
    std::vector<Splotch> Splotches(const Bounds2d& region, double spacing,
                                   const Ground* ground = nullptr) const;

    /// The relative reflectance change `splotch` alone gives (`x`, `y`) at `spacing` metres.
    double Change(const Splotch& splotch, double x, double y, double spacing) const;

    /// The slope in degrees of `ground` at the centre of a splotch `diameter` metres across at
    /// (`x`, `y`), over its width, read at `spacing` metres (half the smallest splotch for 0).
    double SlopeDegrees(const Ground& ground, double x, double y, double diameter,
                        double spacing) const;

    /// What the splotches are.
    const SplotchOptions& Options() const {
        return options_;
    }

  private:
    /// The change of `splotches`, youngest first, at (`x`, `y`).
    double ChangeAt(const std::vector<const Splotch*>& splotches, double x, double y,
                    double spacing) const;

    SplotchOptions options_;
    Surface surface_;
    std::shared_ptr<const SurfaceCells> cells_;
};

}  // namespace terraingen
