// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `ExposureField`, how far the ground is bare bedrock rather than soil, and `PatchNoise`.
#pragma once

#include "terraingen/geometry/surface.h"
#include "terraingen/layers/ground.h"
#include "terraingen/raster/raster.h"

#include <array>
#include <cstdint>
#include <optional>

namespace terraingen {

/// Two-octave Perlin patch noise in [-1, 1], about half the ground near either end.
/// @param size Typical width of a patch, in meters; positive.
/// @param seed Seed of the coarser octave; the finer uses `seed` + 1.
double PatchNoise(const Surface& surface, double x, double y, double size, std::uint32_t seed);

/// The thresholds an `ExposureField` tells soil from bedrock by; a rule of thumb, not a
/// measurement.
struct ExposureParams {
    double reach = 12.0;  ///< How far around a point its surroundings are read (m).
    /// Standing this share of the reach above the surroundings or less: soil.
    double riseSoil = 0.010;
    double riseBoulder = 0.035;  ///< Standing this share of the reach above them or more: bedrock.
    double slopeSoil = 0.20;     ///< Slope (rise over run) at or under which soil holds.
    double slopeBoulder = 0.40;  ///< Slope at or over which none does.
    /// Share of level ground that is bedrock too, in patches; 0 for none.
    double floorCover = 0.0;
    double floorPatch = 60.0;  ///< How far across those patches are (m).
    std::uint32_t seed = 0;    ///< Varies the floor patches.

    bool operator==(const ExposureParams& other) const {
        return reach == other.reach && riseSoil == other.riseSoil &&
               riseBoulder == other.riseBoulder && slopeSoil == other.slopeSoil &&
               slopeBoulder == other.slopeBoulder && floorCover == other.floorCover &&
               floorPatch == other.floorPatch && seed == other.seed;
    }
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
    /// Neighbors without data count as level with the point.
    /// @param spacing Sample spacing in world units.
    double Exposure(const Ground& ground, double x, double y, double spacing) const;

    /// `Exposure` with the surroundings read on `surface` in place of the field's own.
    /// @param spacing Sample spacing in world units.
    double Exposure(const Ground& ground, const Surface& surface, double x, double y,
                    double spacing) const;

    /// Fills `raster` with `Exposure` at each cell center.
    /// @param spacing Sample spacing in world units.
    /// @param threads Worker threads; 0 for every hardware thread.
    void Fill(Raster<float>& raster, const Ground& ground, double spacing,
              unsigned threads = 1) const;

    /// The thresholds the field was made with.
    const ExposureParams& Params() const {
        return params_;
    }

  private:
    ExposureParams params_;
    Surface surface_;
    /// The surroundings read around a point, in meters east and north of it.
    std::array<Vec2d, 8> ring_{};
};

/// The exposure field a layer may read, at a fixed ground spacing, on the surface it is read on;
/// empty when the layer has none.
class LayerExposure {
  public:
    LayerExposure() = default;

    /// @param spacing Sample spacing in meters the ground is read at.
    /// @param reader What reads the field, which starts the error message without a ground.
    /// @throws std::invalid_argument as `ValidateExposureParams`.
    LayerExposure(const std::optional<ExposureParams>& params, double spacing, const char* reader);

    /// Whether the layer reads an exposure field.
    explicit operator bool() const {
        return field_.has_value();
    }

    /// Checks that a layer reading the field was given a ground.
    /// @throws std::invalid_argument if the field is set and `ground` is null.
    void Require(const Ground* ground) const;

    /// The field's exposure at world (x, y) on `ground` lying on `surface`; call only when set.
    /// @throws std::invalid_argument if `ground` is null.
    double At(double x, double y, const Ground* ground, const Surface& surface) const;

  private:
    std::optional<ExposureField> field_;
    double spacing_ = 1.0;
    const char* reader_ = "";
};

}  // namespace terraingen
