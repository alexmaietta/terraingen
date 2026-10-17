/// @file
/// @brief `StrataLayer`, layered rock: the ground beneath terraced into benches and cliffs.
#pragma once

#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"
#include "terraingen/util/geometry.h"

#include <cstdint>

namespace terraingen {

/// The beds a `StrataLayer` cuts. Lengths are in metres, angles in radians.
struct StrataOptions {
    double bedThickness = 2.0;  ///< Mean thickness of a bed.
    /// How far each bed boundary strays, in bed thicknesses, in [0, 1).
    double thicknessVariation = 0.4;
    double dip = 0.0;  ///< Dip of the beds from horizontal, below 90 deg.
    /// Direction the beds dip toward, radians counter-clockwise from +x (local east on a body).
    double dipAzimuth = 0.0;
    /// How sharply each bed is cut into a flat bench and a cliff, at least 1: 1 leaves the ground
    /// as it is, larger values flatten the benches and steepen the cliffs.
    double sharpness = 4.0;
    double strength = 1.0;  ///< Share of the terracing applied, in [0, 1].
    /// Share of a bed's height, at its top, that counts as the cliff (bedrock) in the material map,
    /// in (0, 1].
    double cliffShare = 0.3;
    std::uint32_t seed = 1;  ///< Varies the bed boundaries.
    /// On a body, the surface point whose tangent frame the dip is measured in.
    Vec2d anchor{};

    bool operator==(const StrataOptions&) const = default;
};

/// Checks strata options.
/// @throws std::invalid_argument for a bed thickness that is not positive and finite, a thickness
/// variation outside [0, 1), a dip outside [0, 90) deg, a sharpness below 1, a strength outside
/// [0, 1], a cliff share outside (0, 1], or a non-finite azimuth or anchor.
void ValidateStrataOptions(const StrataOptions& options);

/// Layered rock: the ground beneath terraced into beds, each a flat bench below a cliff at its
/// top, so hills become stepped buttes and mesas.
///
/// A point's stratigraphic level is its height less the dipping bed plane; within its bed it is
/// remapped by u^sharpness (u its height through the bed), which keeps the bed's top and bottom in
/// place, so the surface stays continuous. A function of the height beneath and position only, so
/// grids, tiles and points agree; it lays bedrock over the material map on the cliffs.
class StrataLayer final : public FormulaLayer, public MaterialSource {
  public:
    /// On a body, the dip is measured in the tangent frame of `options.anchor`.
    /// @throws std::invalid_argument as `ValidateStrataOptions`.
    explicit StrataLayer(StrataOptions options, Surface surface = {});

    /// The terraced height of ground `height` metres high at world (x, y) (m).
    double TerracedAt(double height, double x, double y) const;

    /// How far through its bed ground `height` metres high at world (x, y) lies, in [0, 1).
    double BedPositionAt(double height, double x, double y) const;

    /// The options the layer was made with.
    const StrataOptions& Options() const {
        return options_;
    }

    MaterialClass Material() const override {
        return MaterialClass::Bedrock;
    }
    /// 1 on the cliffs, the top `cliffShare` of each bed, fading to 0 on the benches.
    /// @throws std::invalid_argument without `ground`.
    double MaterialCoverAt(double x, double y, double spacing, const Ground* ground) const override;

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;

  private:
    /// The bed holding stratigraphic level `level`: its bottom and top levels.
    struct Bed {
        double bottom = 0.0;
        double top = 0.0;
    };
    Bed BedOf(double level) const;
    double BoundaryOf(std::int64_t k) const;
    /// The stratigraphic level of ground `height` metres high at (x, y): its height less the
    /// dipping bed plane. False on the far hemisphere of a body, where the layer does nothing.
    bool LevelOf(double height, double x, double y, double& level) const;

    StrataOptions options_;
    Vec2d dipDirection_{1.0, 0.0};
    double dipSlope_ = 0.0;
    Vec3d anchorDirection_{};
};

}  // namespace terraingen
