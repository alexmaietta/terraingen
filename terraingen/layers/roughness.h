/// @file
/// @brief `RoughnessLayer`, fine self-affine roughness from octaves of Perlin noise.
#pragma once

#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/util/surface.h"

#include <array>
#include <cstdint>
#include <vector>

namespace terraingen {

/// @brief The roughness a `RoughnessLayer` adds: RMS slope over a baseline and Hurst exponent, as
/// Shepard et al. (2001) give it.
struct RoughnessOptions {
    double rmsSlope = 0.15;       ///< RMS slope (rise over run) over `baseline`.
    double baseline = 0.25;       ///< Baseline the RMS slope is given over (m).
    double hurst = 0.8;           ///< Hurst exponent, in (0, 1].
    double maxWavelength = 4.0;   ///< Longest octave (m).
    double minWavelength = 0.01;  ///< Shortest octave (m); octaves halve down to it.
    std::uint32_t seed = 1;       ///< Varies the octaves' offsets and rotations.
    /// How octaves too fine to resolve fade out, by samples per wavelength.
    ResolutionFade fade{};

    bool operator==(const RoughnessOptions&) const = default;
};

/// @brief Checks roughness options.
/// @param options The options to check.
/// @throws std::invalid_argument for a non-positive `rmsSlope` or `baseline`, a `hurst` outside
/// (0, 1], wavelengths that do not satisfy 0 < `minWavelength` <= `maxWavelength` or that span more
/// than 40 octaves, or an invalid `fade`.
void ValidateRoughnessOptions(const RoughnessOptions& options);

/// @brief The structure function of `PerlinNoise2`: mean squared difference of values a lag apart,
/// over positions, directions and seeds.
/// @param r The lag, in lattice units.
/// @return The structure function; 0 for `r` <= 0.
double PerlinStructureFunction(double r);

/// @brief The structure function of `PerlinNoise3` at a lag in a random 3D direction.
/// @param r The lag, in lattice units.
/// @return The structure function; 0 for `r` <= 0.
double Perlin3StructureFunction(double r);

/// @brief Fine roughness: octaves of Perlin noise halving in wavelength from `maxWavelength` to
/// `minWavelength`, amplitudes growing as wavelength^H.
///
/// Amplitudes are scaled so the RMS slope over `baseline` is `rmsSlope` with every octave
/// resolved. Octaves are rotated and offset so their lattices do not line up, and fade out at
/// spacings too coarse to resolve them.
class RoughnessLayer final : public Layer {
  public:
    /// @brief Makes the layer; on a sphere each octave is 3D Perlin noise at the 3D point, its
    /// lattice turned by a uniform random rotation.
    /// @param options The roughness.
    /// @param surface What the terrain lies on.
    /// @throws std::invalid_argument as `ValidateRoughnessOptions`.
    explicit RoughnessLayer(RoughnessOptions options, Surface surface = {});

    /// @brief Adds the roughness resolved at the grid's spacing to every cell of `heights`.
    /// @param heights The heights to change in place.
    /// @param context How the layer is evaluated.
    /// @param features Unused; roughness places no features.
    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// @brief The roughness at a point added to `height`, bit-identical to a cell centred there.
    /// @param height The height before the layer; NaN stays NaN.
    /// @param x World x of the point.
    /// @param y World y of the point.
    /// @param context How the layer is evaluated.
    /// @return The height after the layer.
    /// @throws std::invalid_argument as `ValidatePointContext`.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// @brief The RMS slope of the roughness with every octave resolved.
    /// @param baseline The baseline, in metres.
    /// @return The RMS slope (rise over run) over `baseline`.
    double RmsSlope(double baseline) const;

    /// @brief The octaves' wavelengths.
    /// @return The wavelengths in metres, longest first.
    std::vector<double> Wavelengths() const;

    /// @brief The roughness options.
    /// @return The options the layer was made with.
    const RoughnessOptions& Options() const {
        return options_;
    }

  private:
    struct Octave {
        double wavelength;
        double amplitude;
        double cos;
        double sin;
        double offsetX;
        double offsetY;
        std::uint32_t seed;
        std::array<double, 9> rotation{};
        double offsetZ = 0.0;
    };

    /// Octave `i` of `wavelength`, before its amplitude is scaled to the RMS slope.
    Octave MakeOctave(std::uint32_t i, double wavelength) const;
    /// The noise of one octave at (`x`, `y`), in [-1, 1].
    double OctaveNoise(const Octave& octave, double x, double y) const;
    /// The sum of the octaves at (`x`, `y`) resolved at `spacing`.
    float Roughness(double x, double y, double spacing) const;

    RoughnessOptions options_;
    Surface surface_;
    std::vector<Octave> octaves_;
};

}  // namespace terraingen
