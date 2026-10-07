#pragma once

#include "terraingen/layers/ground.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/surface.h"

#include <cstdint>

namespace terraingen {

/// Settings for an `ExposureField`. The thresholds are a rule of thumb, not a measurement of any
/// site; they follow Chrono::Planet's `ChExposureField`.
struct ExposureParams {
    double reach = 12.0;  ///< How far around a point its surroundings are read (m).
    double riseSoil =
        0.010;  ///< Standing this share of the reach above the surroundings or less: soil.
    double riseRock = 0.035;  ///< Standing this share of the reach above them or more: bedrock.
    double slopeSoil = 0.20;  ///< Slope (rise over run) at or under which soil holds.
    double slopeRock = 0.40;  ///< Slope at or over which none does.
    double floorCover =
        0.0;  ///< Share of level ground that is bedrock too, in patches; 0 for none.
    double floorPatch = 60.0;  ///< How far across those patches are (m).
    std::uint32_t seed = 0;    ///< Varies the floor patches.

    bool operator==(const ExposureParams&) const = default;
};

/// @throws std::invalid_argument unless `reach` and `floorPatch` are positive, each rock threshold
/// is over its soil one, and `floorCover` is in [0, 1).
void ValidateExposureParams(const ExposureParams& params);

/// How far the ground at a point is bedrock rather than soil, from the shape of the ground alone:
/// loose material moves downhill and settles in lows, so rises and steep ground are left bare. A
/// point standing above the mean of eight points a reach around it is bedrock, as is a slope too
/// steep to hold soil; each grades from soil to bedrock with a smoothstep between its two
/// thresholds, and the larger share wins. Level ground can be bedrock too, such as a swept lava
/// floor, which the shape cannot reveal, so `floorCover` of it is bedrock in noise patches.
///
/// It changes no height. Layers that place material, such as rocks, and renderers ask it where the
/// bedrock is, so they agree. It reads the ground at points (`Ground`), so it is the same on every
/// grid, tile and point.
class ExposureField {
  public:
    /// On a sphere, the surroundings are read in each point's local frame and the floor patches
    /// are 3D noise at the 3D point.
    /// @throws std::invalid_argument as `ValidateExposureParams`.
    explicit ExposureField(const ExposureParams& params = {}, const Surface& surface = {});

    /// Share of bedrock at (`x`, `y`) in [0, 1]: 0 soil, 1 bedrock; 0 where the ground has no data.
    /// Points around it without data count as level with it.
    double Exposure(const Ground& ground, double x, double y, double spacing) const;

    /// `Exposure` at the centre of every cell of `raster`, on up to `threads` threads (0 for every
    /// hardware thread).
    void Fill(Raster<float>& raster, const Ground& ground, double spacing,
              unsigned threads = 1) const;

    const ExposureParams& Params() const {
        return params_;
    }

    const Surface& GetSurface() const {
        return surface_;
    }

  private:
    ExposureParams params_;
    Surface surface_;
};

}  // namespace terraingen
