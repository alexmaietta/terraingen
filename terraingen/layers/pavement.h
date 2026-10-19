/// @file
/// @brief `PavementLayer`, fractured bedrock: polygonal plates parted by cracks or raised ridges.
#pragma once

#include "terraingen/layers/anchored_plane.h"
#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"
#include "terraingen/util/geometry.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace terraingen {

/// The plates and cracks of a `PavementLayer`. Lengths are in metres, angles in radians.
struct PavementOptions {
    double plateSize = 1.0;      ///< Mean plate width: the spacing of the plates' Voronoi sites.
    double jitter = 1.0;         ///< How far sites stray from a square lattice, in [0, 1].
    double warp = 0.15;          ///< Bend of the plate edges, in plate sizes.
    double crackWidth = 0.05;    ///< Width of the crack between two plates.
    double crackDepth = 0.05;    ///< Depth of a crack, or height of a ridge with `raised`.
    bool raised = false;         ///< Whether plates are parted by raised ridges, not cracks.
    double edgeRounding = 0.02;  ///< Width of the rounded shoulder beside each crack.
    double maxTilt = 0.05;       ///< Largest tilt of a plate.
    double maxStep = 0.03;       ///< Largest height offset of a plate, up or down.
    /// Crack octaves: each finer octave splits the plates again into plates `lacunarity` times
    /// smaller, with cracks, rounding, tilts and steps `gain` times the octave before.
    int octaves = 1;
    double lacunarity = 2.0;  ///< Plate size ratio between octaves, above 1.
    double gain = 0.5;        ///< Crack and plate relief ratio between octaves, in (0, 1].
    std::uint32_t seed = 1;   ///< Varies the plates.
    /// When set, plates show where the ground beneath is bedrock (`ExposureField`), fading out
    /// over soil.
    std::optional<ExposureParams> exposure;
    double exposureSpacing = 1.0;  ///< Spacing the ground is read at for the exposure field.
    /// On a body, the surface point whose tangent frame the plates are laid out in.
    Vec2d anchor{};
    ResolutionFade fade{};  ///< How plates and cracks too fine for the spacing fade out.

    bool operator==(const PavementOptions&) const = default;
};

/// Checks pavement options.
/// @throws std::invalid_argument for a plate size, crack width or exposure spacing that is not
/// positive and finite, a negative crack depth, edge rounding, tilt or step, a tilt of 90 deg or
/// more, a jitter outside [0, 1], a negative warp, octaves outside 1 to 8, a lacunarity not above
/// 1, a gain outside (0, 1], or invalid exposure or fade.
void ValidatePavementOptions(const PavementOptions& options);

/// The desiccation ridges of the Old Soaker slab in Gale crater (Stein et al. 2018): 2 cm
/// polygons parted by 5 mm high ridges about 2 mm wide (from ridges covering about 20 % of the
/// slab), flat and untilted. The polygons there are mostly four-sided with T-junctions; Voronoi
/// plates meet at Y-junctions.
PavementOptions MarsDesiccationRidges();

/// Fractured bedrock pavement added to the terrain: Voronoi plates, each flat with its own tilt
/// and height step, parted by cracks (or ridges) with rounded shoulders, and split again by finer
/// octaves of cracks that end at the cracks before them; each octave fades out where too fine for
/// the spacing. Plate heights blend
/// across each crack, so the surface has no steps. A function of position only, so grids, tiles
/// and points agree; it lays bedrock over the material map.
class PavementLayer final : public FormulaLayer, public MaterialSource {
  public:
    /// On a body, plates are laid out in the tangent frame of `options.anchor`. Applying the layer
    /// with the exposure field enabled needs `context.ground`.
    /// @throws std::invalid_argument as `ValidatePavementOptions`.
    explicit PavementLayer(PavementOptions options, Surface surface = {});

    /// The pavement's relief at world (x, y), every octave summed, without exposure or fading (m).
    double ReliefAt(double x, double y) const;

    /// The distance from world (x, y) to the nearest edge of the coarsest plates (m).
    double EdgeDistanceAt(double x, double y) const;

    /// The options the layer was made with.
    const PavementOptions& Options() const {
        return options_;
    }

    MaterialClass Material() const override {
        return MaterialClass::Bedrock;
    }
    /// How fully the pavement covers (x, y): 1, or the exposure of the ground beneath.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    double MaterialCoverAt(double x, double y, double spacing, const Ground* ground) const override;

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;
    void CheckContext(const LayerContext& context) const override;

  private:
    /// One crack octave: its plate size, crack and plate relief, and seed.
    struct Level {
        double size = 1.0;
        double crackWidth = 0.0;
        double crackDepth = 0.0;
        double rounding = 0.0;
        double maxTilt = 0.0;
        double maxStep = 0.0;
        std::uint32_t seed = 0;
    };
    /// The plate of one octave containing planar point `q`: its relief, distance to its nearest
    /// edge, how far it lies inside the plate rather than in its crack (0 on an edge, 1 past the
    /// shoulder), and an id that seeds the finer octave's plates within it.
    struct Plate {
        double relief = 0.0;
        double edge = 0.0;
        double interior = 1.0;
        std::uint32_t id = 0;
    };
    /// Planar point `q` moved by the edge warp of plates `size` across.
    Vec2d Warp(const Vec2d& q, double size, std::uint32_t seed) const;
    Plate PlateAt(const Vec2d& q, const Level& level, std::uint32_t seed, double crackWeight) const;
    /// The relief of every octave at planar point `q`, each faded at `spacing`; 0 for none.
    double Relief(const Vec2d& q, double spacing) const;
    double ExposureWeight(double x, double y, const Ground* ground) const;

    PavementOptions options_;
    std::optional<ExposureField> exposure_;
    AnchoredPlane plane_;
    std::vector<Level> levels_;
};

}  // namespace terraingen
