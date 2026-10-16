/// @file
/// @brief `ExposureField`, how far the ground at a point is bare bedrock rather than soil, and the
/// patch noise that breaks it up.
#pragma once

#include "terraingen/layers/ground.h"
#include "terraingen/raster/raster.h"
#include "terraingen/util/surface.h"

#include <cstdint>

namespace terraingen {

/// @brief Two-octave patch noise: clamp(2.5 (0.7 n(p / size) + 0.3 n(4 p / size)), -1, 1).
///
/// n is 2D Perlin noise on a plane and 3D Perlin noise at the 3D point on a sphere; the finer
/// octave is seeded `seed` + 1. About half the ground lies near either end.
/// @param surface What the point lies on.
/// @param x Surface x of the point.
/// @param y Surface y of the point.
/// @param size Typical width of a patch, in metres; positive.
/// @param seed Seed of the coarser octave.
/// @return The patch value, in [-1, 1].
double PatchNoise(const Surface& surface, double x, double y, double size, std::uint32_t seed);

/// @brief The thresholds an `ExposureField` tells soil from bedrock by; a rule of thumb, not a
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

/// @brief Checks exposure thresholds.
/// @param params The thresholds to check.
/// @throws std::invalid_argument unless `reach` and `floorPatch` are positive, each boulder
/// threshold is over its soil one, and `floorCover` is in [0, 1).
void ValidateExposureParams(const ExposureParams& params);

/// @brief How far the ground at a point is bedrock rather than soil, from its shape alone.
///
/// Rises above the mean of eight points a reach around, and steep slopes, grade from soil to
/// bedrock by smoothsteps between their thresholds (the larger wins); `floorCover` of level ground
/// is bedrock in noise patches. Changes no height; reads the ground at points, so it is the same
/// on every grid, tile and point.
class ExposureField {
  public:
    /// @brief Makes a field; on a sphere, surroundings are read in each point's local frame.
    /// @param params The thresholds.
    /// @param surface What the ground lies on.
    /// @throws std::invalid_argument as `ValidateExposureParams`.
    explicit ExposureField(const ExposureParams& params = {}, const Surface& surface = {});

    /// @brief The share of bedrock at a point; neighbours without data count as level with it.
    /// @param ground The ground to read.
    /// @param x World x of the point.
    /// @param y World y of the point.
    /// @param spacing Sample spacing in world units.
    /// @return In [0, 1]: 0 soil, 1 bedrock; 0 where the ground has no data.
    double Exposure(const Ground& ground, double x, double y, double spacing) const;

    /// @brief `Exposure` at the centre of every cell of a raster.
    /// @param raster The raster to fill.
    /// @param ground The ground to read.
    /// @param spacing Sample spacing in world units.
    /// @param threads Worker threads; 0 for every hardware thread.
    void Fill(Raster<float>& raster, const Ground& ground, double spacing,
              unsigned threads = 1) const;

    /// @brief The thresholds.
    /// @return The parameters the field was made with.
    const ExposureParams& Params() const {
        return params_;
    }

    /// @brief What the ground lies on.
    /// @return The surface the field was made with.
    const Surface& GetSurface() const {
        return surface_;
    }

  private:
    ExposureParams params_;
    Surface surface_;
};

}  // namespace terraingen
