// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `BedformLayer`, fields of wind-blown ripples or dunes.
#pragma once

#include "terraingen/layers/exposure.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/layers/material.h"
#include "terraingen/random/stream.h"
#include "terraingen/sources/field.h"
#include "terraingen/sources/source.h"
#include "terraingen/util/cells.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace terraingen {

/// The bedform fields a `BedformLayer` scatters: their ripples, cover and wind. By default large
/// wind ripples 4 m apart and 0.4 m high (Lapotre et al. 2016,
/// https://doi.org/10.1126/science.aaf3206).
struct BedformOptions {
    double wavelength = 4.0;  ///< Crest to crest (m); each field's varies from 0.8 to 1.2 times it.
    double height = 0.4;      ///< Trough to crest (m), scaled with each field's wavelength.
    /// Where the wind blows toward, degrees counter-clockwise from +x; crests lie across it.
    double windAzimuthDegrees = 0.0;
    double azimuthSpreadDegrees = 0.0;  ///< Each field's wind differs by up to this.
    /// Share of a wavelength the lee face takes, in (0, 1): 0.5 is symmetric, less is steeper.
    double leeFraction = 0.5;
    double sinuosity = 0.5;    ///< How far a crest wanders along its length, in wavelengths.
    double cover = 0.9;        ///< Share of the ground under fields, in [0, 0.9].
    double fieldSize = 100.0;  ///< Typical width of a field (m).
    std::uint32_t seed = 1;    ///< Varies the fields.
    /// When set, a field is left out with probability the exposure at its center, read from the
    /// ground beneath the layer.
    std::optional<ExposureParams> exposure;
    double exposureSpacing = 1.0;  ///< Spacing the ground is read at for the exposure field (m).
    /// How bedforms too fine for the spacing fade out, by samples per wavelength.
    ResolutionFade fade{};
    /// Source cells across from which the source's measured data already holds the bedforms: with
    /// a source given to the layer, they are left out where `wavelength` is at least that many of
    /// its cells (`SourceResolvedLimit`). Unset, or without a source, adds them everywhere.
    std::optional<double> sourceResolvesPixels = 4.0;

    bool operator==(const BedformOptions& other) const {
        return wavelength == other.wavelength && height == other.height &&
               windAzimuthDegrees == other.windAzimuthDegrees &&
               azimuthSpreadDegrees == other.azimuthSpreadDegrees &&
               leeFraction == other.leeFraction && sinuosity == other.sinuosity &&
               cover == other.cover && fieldSize == other.fieldSize && seed == other.seed &&
               exposure == other.exposure && exposureSpacing == other.exposureSpacing &&
               fade == other.fade && sourceResolvesPixels == other.sourceResolvesPixels;
    }
};

/// Checks bedform options.
/// @throws std::invalid_argument for a non-positive wavelength or field size, a negative height,
/// a lee fraction outside (0, 1), a cover outside [0, 0.9], a negative sinuosity or spread, a
/// non-positive exposure spacing, or an invalid fade or exposure.
void ValidateBedformOptions(const BedformOptions& options);

/// Martian large wind ripples: 2 m wavelength, 0.2 m height, a 30 degree lee slope and sinuous
/// crests; cover and field size are placeholders to set per site.
BedformOptions MarsLargeRipples();

/// Martian transverse aeolian ridges: symmetric, 25.8 m wavelength and 1.3 m height; cover and
/// field size are placeholders to set per site.
BedformOptions MarsTransverseAeolianRidges();

/// Fields of wind-blown bedforms, ripples or dunes, added to the terrain.
///
/// Each lattice cell may hold a field with a ragged edge whose crests run across its wind; relief
/// is C1 and zero in the troughs, and overlapping fields average by cover. A function of position
/// only, so grids, tiles and points agree.
class BedformLayer final : public FormulaLayer, public MaterialSource {
  public:
    /// Fields are drawn in the surface's cells (`MakeCells`) and shaped in their centers' local
    /// frames, with the azimuth counter-clockwise from local east. Applying the layer with the
    /// exposure field enabled needs `context.ground`.
    /// @param base The surface and the source, which with `options.sourceResolvesPixels` holds
    /// the bedforms it resolves.
    /// @throws std::invalid_argument as `ValidateBedformOptions`.
    explicit BedformLayer(BedformOptions options, TerrainBase base = {});

    /// How fully world (x, y) lies in a field, in [0, 1]; independent of the spacing.
    /// @param ground Read by the exposure field; may be null without one.
    /// @throws std::invalid_argument if the exposure field is enabled and `ground` is null.
    double CoverAt(double x, double y, const Ground* ground = nullptr) const;

    /// The options the layer was made with.
    const BedformOptions& Options() const {
        return options_;
    }

    MaterialClass Material() const override {
        return MaterialClass::Sand;
    }
    /// `CoverAt`: the bedform fields are sand.
    double MaterialCoverAt(double x, double y, double /*spacing*/,
                           const Ground* ground) const override {
        return CoverAt(x, y, ground);
    }

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;
    void CheckContext(const LayerContext& context) const override;

  private:
    struct FieldSum {
        double cover = 0.0;
        double relief = 0.0;
    };
    FieldSum SumFields(double x, double y, const Ground* ground) const;
    void AddField(FieldSum& sum, RandomStream& random, const Vec2d& center, const Vec2d& offset,
                  const Ground* ground) const;

    BedformOptions options_;
    std::shared_ptr<const SurfaceField> limit_;  ///< `SourceResolvedLimit`; null for none.
    double cell_ = 1.0;
    double density_ = 0.0;
    std::uint32_t wanderSeed_ = 0;  ///< Seed of the crest wander noise.
    LayerExposure exposure_;
    std::shared_ptr<const SurfaceCells> cells_;
};

}  // namespace terraingen
