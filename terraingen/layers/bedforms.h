/// @file
/// @brief `BedformLayer`, fields of wind-blown ripples or dunes.
#pragma once

#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/random/stream.h"
#include "terraingen/util/cells.h"

#include <cstdint>
#include <optional>

namespace terraingen {

/// @brief The bedform fields a `BedformLayer` scatters: their ripples, cover and wind.
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
    std::uint32_t seed = 1;    ///< Varies the fields.
    /// When set, a field is left out with probability the exposure at its centre, read from the
    /// ground beneath the layer.
    std::optional<ExposureParams> exposure;
    double exposureSpacing = 1.0;  ///< Spacing the ground is read at for the exposure field (m).
    /// How bedforms too fine for the spacing fade out, by samples per wavelength.
    ResolutionFade fade{};

    bool operator==(const BedformOptions&) const = default;
};

/// @brief Checks bedform options.
/// @param options The options to check.
/// @throws std::invalid_argument for a non-positive wavelength or field size, a negative height,
/// a lee fraction outside (0, 1), a cover outside [0, 0.9], a negative sinuosity or spread, a
/// non-positive exposure spacing, or an invalid fade or exposure.
void ValidateBedformOptions(const BedformOptions& options);

/// @brief Fields of wind-blown bedforms, ripples or dunes, added to the terrain.
///
/// Each lattice cell may hold a field with a ragged edge whose crests run across its wind; relief
/// is C1 and zero in the troughs, and overlapping fields average by cover. A function of position
/// only, so grids, tiles and points agree.
class BedformLayer final : public Layer {
  public:
    /// @brief Makes the layer; on a sphere, fields are drawn in `CubeSphereCells`, shaped in their
    /// centres' local frames, with the azimuth counter-clockwise from local east.
    /// @param options The bedform fields.
    /// @param surface What the terrain lies on.
    /// @throws std::invalid_argument as `ValidateBedformOptions`.
    explicit BedformLayer(BedformOptions options, Surface surface = {});

    /// @brief Adds the bedforms to every cell of `heights`.
    /// @param heights The heights to change in place.
    /// @param context How the layer is evaluated.
    /// @param features Unused.
    /// @throws std::invalid_argument if the exposure field is enabled and `context.ground` is null.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// @brief The bedforms at a point added to `height`, bit-identical to a cell centred there.
    /// @param height The height before the layer; NaN stays NaN.
    /// @param x World x of the point.
    /// @param y World y of the point.
    /// @param context How the layer is evaluated.
    /// @return The height after the layer.
    /// @throws std::invalid_argument as `ValidatePointContext`, or if the exposure field is
    /// enabled and `context.ground` is null.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// @brief How fully a point lies in a field; independent of the spacing.
    /// @param x World x of the point.
    /// @param y World y of the point.
    /// @param ground The ground, read by the exposure field; may be null without one.
    /// @return The cover, in [0, 1].
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    double CoverAt(double x, double y, const Ground* ground = nullptr) const;

    /// @brief The bedform options.
    /// @return The options the layer was made with.
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
