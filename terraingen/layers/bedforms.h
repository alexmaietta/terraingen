#pragma once

#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/cells.h"

#include <cstdint>
#include <optional>

namespace terraingen {

/// The bedform fields a `BedformLayer` scatters: their ripples, cover and wind.
struct BedformOptions {
    double wavelength = 1.0;  ///< Crest to crest (m); each field's varies from 0.8 to 1.2 times it.
    double height = 0.1;      ///< Trough to crest (m), scaled with each field's wavelength.
    /// Where the wind blows toward, radians counter-clockwise from +x; crests lie across it.
    double windAzimuth = 0.0;
    double azimuthSpread = 0.0;  ///< Each field's wind differs by up to this (radians).
    /// Share of a wavelength the lee face takes, in (0, 1): 0.5 is symmetric, less is steeper.
    double leeFraction = 0.5;
    double sinuosity = 0.5;    ///< How far a crest wanders along its length, in wavelengths.
    double cover = 0.0;        ///< Share of the ground under fields, in [0, 0.9].
    double fieldSize = 100.0;  ///< Typical width of a field (m).
    std::uint32_t seed = 1;
    /// Sand settles in lows: with the exposure field, a field whose centre is on bedrock is left
    /// out, the likelier the barer the ground there. Read from the ground beneath the layer.
    std::optional<ExposureParams> exposure;
    double exposureSpacing = 1.0;  ///< Spacing the ground is read at for the exposure field (m).
    /// How bedforms too fine for the spacing fade out, by samples per wavelength.
    ResolutionFade fade{};

    bool operator==(const BedformOptions&) const = default;
};

/// @throws std::invalid_argument for a non-positive wavelength or field size, a negative height,
/// a lee fraction outside (0, 1), a cover outside [0, 0.9], a negative sinuosity or spread, a
/// non-positive exposure spacing, or an invalid fade or exposure.
void ValidateBedformOptions(const BedformOptions& options);

/// Fields of wind-blown bedforms, ripples or dunes, added to the terrain: each lattice cell may
/// hold a field with a ragged edge, whose crests run across its wind, wander along their length,
/// and rise along a gentle upwind face and fall down a lee face, C1 continuous and zero in the
/// troughs. Where fields overlap, their relief is their mean weighted by cover, so sand is shared
/// out rather than piled. Everything is a function of position, so grids, tiles and point queries
/// agree.
///
/// After Chrono::Planet's bedform layer, with its constants: field radii 0.4 to 0.7 lattice cells
/// with edges 30% ragged, thinning over their outer 35%, and crests wandering over 5 wavelengths.
class BedformLayer final : public Layer {
  public:
    /// On a sphere, fields are drawn in `CubeSphereCells` (each with a chance in proportion to its
    /// area), shaped in their centres' local frames, and the wind azimuth is counter-clockwise from
    /// local east.
    /// @throws std::invalid_argument for invalid options.
    explicit BedformLayer(BedformOptions options, Surface surface = {});

    /// @throws std::invalid_argument if the exposure field is enabled and `context.ground` is null.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// The bedforms at (`x`, `y`) added to `height`, to the same bits as a cell centred there; NaN
    /// stays NaN.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// How fully (`x`, `y`) lies in a field, in [0, 1]; it does not depend on the spacing.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    double CoverAt(double x, double y, const Ground* ground = nullptr) const;

    const BedformOptions& Options() const {
        return options_;
    }

  private:
    struct Point {
        double cover = 0.0;
        double relief = 0.0;
    };
    Point Evaluate(double x, double y, const Ground* ground) const;
    void AddField(Point& point, RandomStream& random, const Vec2d& centre, const Vec2d& offset,
                  const Ground* ground) const;
    float Add(float height, double x, double y, double spacing, const Ground* ground) const;

    BedformOptions options_;
    double cell_ = 1.0;
    double density_ = 0.0;
    std::optional<ExposureField> exposure_;
    Surface surface_;
    std::optional<CubeSphereCells> cells_;
};

}  // namespace terraingen
