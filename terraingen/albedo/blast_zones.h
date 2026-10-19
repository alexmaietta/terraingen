/// @file
/// @brief Blast zones: the regolith a lander's descent engine scoured, brighter than around it.
#pragma once

#include "terraingen/albedo/albedo.h"
#include "terraingen/util/surface.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// The blast zone around one lander, after Clegg et al. (2014) and Clegg-Watkins et al. (2016):
/// an elliptical zone of `area` brighter by `change` on average, brightest within about a third of
/// its radius and fading to its edge, inside a fainter halo reaching `haloReach` times as far, with
/// a darker core of `coreRadius` metres where the soil beneath the engine and the crew's tracks
/// were disturbed. Changes are relative to the undisturbed ground at 30 degrees phase.
///
/// Measured areas: Apollo 18,725 to 53,872 m^2 (mean 28,785, thrust 45 kN), Luna 987 to 3355 m^2
/// (mean 2100, 16 to 19 kN), Surveyor 131 to 279 m^2 (mean 215, under 0.5 kN), Chang'e-3 2530 m^2
/// (7.5 kN). Changes: Apollo +7.5% to +12.5% (mean +10.9%), Luna +5.1% to +10.6%, Surveyor +3.4%
/// to +13.2%, Chang'e-3 +10%. Apollo dark cores: -15% to -30% within a few metres.
struct BlastZone {
    double x = 0.0;                ///< Surface x of the lander.
    double y = 0.0;                ///< Surface y of the lander.
    double area = 28785.0;         ///< Area of the zone (m^2), the Apollo mean; positive.
    double elongation = 1.14;      ///< Long over short axis; Apollo 200 / 175, Chang'e-3 1.74.
    double headingDegrees = 90.0;  ///< Long axis, degrees counter-clockwise from east.
    double change = 0.109;         ///< Mean reflectance change over the zone; above -1.
    double haloChange = 0.04;      ///< Change of the halo beyond the zone (Chang'e-3).
    double haloReach = 1.9;        ///< Halo's reach over the zone's (Chang'e-3); at least 1.
    double coreRadius = 0.0;       ///< Radius of the dark core (m); 0 for none.
    double coreChange = -0.2;      ///< Change of the dark core; above -1.
    double raggedness = 0.15;      ///< Share of the radius the edge's lobes pull in, [0, 1).
    std::uint32_t seed = 0;        ///< Varies the edge's lobes.

    bool operator==(const BlastZone& other) const {
        return x == other.x && y == other.y && area == other.area &&
               elongation == other.elongation && headingDegrees == other.headingDegrees &&
               change == other.change && haloChange == other.haloChange &&
               haloReach == other.haloReach && coreRadius == other.coreRadius &&
               coreChange == other.coreChange && raggedness == other.raggedness &&
               seed == other.seed;
    }
    bool operator!=(const BlastZone& other) const {
        return !(*this == other);
    }
};

/// Checks a blast zone.
/// @throws std::invalid_argument for a non-finite position, a non-positive area, an elongation or
/// halo reach below 1, a non-finite heading, changes at or below -1 or not finite, a negative core
/// radius, or a raggedness outside [0, 1).
void ValidateBlastZone(const BlastZone& zone);

/// The share of a zone's peak change it averages over its area, by the radial profile: full out
/// to 0.3 of the radius, then fading to 0 at the edge.
double BlastZoneMeanShare();

/// Blast zones around landers, changing the albedo by the relative reflectance they give; the
/// same in any grid, tile or point.
class BlastZoneAlbedo final : public AlbedoLayer {
  public:
    /// @throws std::invalid_argument for a zone invalid as `ValidateBlastZone`.
    explicit BlastZoneAlbedo(std::vector<BlastZone> zones, Surface surface = {});

    /// Changes the albedo at one point by the zones over it; NaN stays NaN.
    void ApplyAt(AlbedoSample& value, std::size_t bands, double x, double y,
                 const AlbedoContext& context) const override;

    /// The relative reflectance change `zone` gives (`x`, `y`).
    double Change(const BlastZone& zone, double x, double y) const;

    /// How far (m) from its lander `zone` changes the ground.
    static double Reach(const BlastZone& zone);

    /// The zones.
    const std::vector<BlastZone>& Zones() const {
        return zones_;
    }

  private:
    std::vector<BlastZone> zones_;
    Surface surface_;
};

}  // namespace terraingen
