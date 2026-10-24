// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `StrataLayer`, layered rock: the ground beneath terraced into benches and cliffs.
#pragma once

#include "terraingen/layers/anchored_plane.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/geometry.h"

#include <cstdint>

namespace terraingen {

/// The beds a `StrataLayer` cuts. Lengths are in meters, angles in degrees.
struct StrataOptions {
    double bedThickness = 2.0;  ///< Mean thickness of a bed.
    /// How far each bed boundary strays, in bed thicknesses, in [0, 1).
    double thicknessVariation = 0.4;
    double dipDegrees = 0.0;  ///< Dip of the beds from horizontal, below 90 deg.
    /// Direction the beds dip toward, degrees counter-clockwise from +x (local east on a body).
    double dipAzimuthDegrees = 0.0;
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
    /// Share of the beds that are hard, resistant rock, in [0, 1]: a hard bed is cut at
    /// `sharpness` into a bench and a cliff and lays bedrock there, a soft one at `softSharpness`
    /// into a slope without bedrock. Each bed is drawn hard or soft by the seed.
    double hardShare = 1.0;
    double softSharpness = 1.0;  ///< How sharply soft beds are cut, at least 1.

    bool operator==(const StrataOptions& other) const {
        return bedThickness == other.bedThickness &&
               thicknessVariation == other.thicknessVariation && dipDegrees == other.dipDegrees &&
               dipAzimuthDegrees == other.dipAzimuthDegrees && sharpness == other.sharpness &&
               strength == other.strength && cliffShare == other.cliffShare && seed == other.seed &&
               anchor == other.anchor && hardShare == other.hardShare &&
               softSharpness == other.softSharpness;
    }
};

/// Checks strata options.
/// @throws std::invalid_argument for a bed thickness that is not positive and finite, a thickness
/// variation outside [0, 1), a dip outside [0, 90) deg, a sharpness or soft sharpness below 1, a
/// strength or hard share outside [0, 1], a cliff share outside (0, 1], or a non-finite azimuth or
/// anchor.
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

    /// The options the layer was made with.
    const StrataOptions& Options() const {
        return options_;
    }

    MaterialClass Material() const override {
        return MaterialClass::Bedrock;
    }
    /// 1 on the cliffs of hard beds, the top `cliffShare` of each, fading to 0 on the benches; 0 on
    /// soft beds.
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
        bool hard = true;
    };
    Bed BedAt(double level) const;
    /// The terraced height of ground `height` meters high at world (x, y) (m).
    double TerracedAt(double height, double x, double y) const;
    double BedBoundary(std::int64_t k) const;
    /// The random stream `stream` of bed `k`.
    RandomStream BedStream(std::uint32_t stream, std::int64_t k) const;
    /// The stratigraphic level of ground `height` meters high at (x, y): its height less the
    /// dipping bed plane. False on the far hemisphere of a body, where the layer does nothing.
    bool StratigraphicLevel(double height, double x, double y, double& level) const;

    StrataOptions options_;
    Vec2d dipDirection_{1.0, 0.0};
    double dipSlope_ = 0.0;
    AnchoredPlane plane_;
};

}  // namespace terraingen
