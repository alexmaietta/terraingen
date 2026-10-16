/// @file
/// @brief `ExposureField`, how far the ground is bare bedrock rather than soil, and `PatchNoise`.
#pragma once

#include "terraingen/layers/ground.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/surface.h"

#include <cstdint>

namespace terraingen {

/// Two-octave patch noise in [-1, 1]: clamp(2.5 (0.7 n(p / size) + 0.3 n(4 p / size)), -1, 1).
///
/// n is 2D Perlin noise on a plane and 3D at the 3D point on a sphere; the finer octave is seeded
/// `seed` + 1. About half the ground lies near either end.
/// @param size Typical width of a patch, in metres; positive.
/// @param seed Seed of the coarser octave.
double PatchNoise(const Surface& surface, double x, double y, double size, std::uint32_t seed);

/// The thresholds an `ExposureField` tells soil from bedrock by; a rule of thumb, not a
/// measurement.
struct ExposureParams {
    double reach = 12.0;  ///< How far around a point its surroundings are read (m).
    double riseSoil =
        0.010;  ///< Standing this share of the reach above the surroundings or less: soil.
    double riseBoulder = 0.035;  ///< Standing this share of the reach above them or more: bedrock.
    double slopeSoil = 0.20;     ///< Slope (rise over run) at or under which soil holds.
    double slopeBoulder = 0.40;  ///< Slope at or over which none does.
    double floorCover =
        0.0;  ///< Share of level ground that is bedrock too, in patches; 0 for none.
    double floorPatch = 60.0;  ///< How far across those patches are (m).
    std::uint32_t seed = 0;    ///< Varies the floor patches.

    bool operator==(const ExposureParams&) const = default;
};

/// Checks exposure thresholds.
/// @throws std::invalid_argument unless `reach` and `floorPatch` are positive, each boulder
/// threshold is over its soil one, and `floorCover` is in [0, 1).
void ValidateExposureParams(const ExposureParams& params);

/// How far the ground at a point is bedrock rather than soil, from its shape alone.
///
/// Rises above the mean of eight points a reach around, and steep slopes, grade from soil to
/// bedrock by smoothsteps between their thresholds (the larger wins); `floorCover` of level ground
/// is bedrock in noise patches. Reads the ground at points, so it agrees on every grid and tile.
class ExposureField {
  public:
    /// Makes a field; on a sphere, surroundings are read in each point's local frame.
    /// @throws std::invalid_argument as `ValidateExposureParams`.
    explicit ExposureField(const ExposureParams& params = {}, const Surface& surface = {});

    /// The share of bedrock at world (x, y), in [0, 1] (0 soil, 1 bedrock; 0 without data).
    /// Neighbours without data count as level with the point.
    /// @param spacing Sample spacing in world units.
    double Exposure(const Ground& ground, double x, double y, double spacing) const;

    /// Fills `raster` with `Exposure` at each cell centre.
    /// @param spacing Sample spacing in world units.
    /// @param threads Worker threads; 0 for every hardware thread.
    void Fill(Raster<float>& raster, const Ground& ground, double spacing,
              unsigned threads = 1) const;

    /// The thresholds the field was made with.
    const ExposureParams& Params() const {
        return params_;
    }

    /// What the ground lies on.
    const Surface& GetSurface() const {
        return surface_;
    }

  private:
    ExposureParams params_;
    Surface surface_;
};

}  // namespace terraingen
