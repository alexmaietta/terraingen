// SPDX-License-Identifier: BSD-3-Clause

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

/// The roughness a `RoughnessLayer` adds: RMS slope over a baseline and Hurst exponent, as
/// Shepard et al. (2001) give it.
struct RoughnessOptions {
    double rmsSlope = 0.15;  ///< RMS slope (rise over run) over `baseline`.
    double baseline = 0.25;  ///< Baseline the RMS slope is given over (m).
    double hurst = 0.8;      ///< Hurst exponent, in (0, 1].
    /// Longest octave (m); by default 16 m, below the LOLA slope baselines (Rosenburg et al. 2011,
    /// https://doi.org/10.1029/2010JE003716).
    double maxWavelength = 16.0;
    double minWavelength = 0.01;  ///< Shortest octave (m); octaves halve down to it.
    std::uint32_t seed = 1;       ///< Varies the octaves' offsets and rotations.
    /// How octaves too fine to resolve fade out, by samples per wavelength.
    ResolutionFade fade{};

    bool operator==(const RoughnessOptions& other) const {
        return rmsSlope == other.rmsSlope && baseline == other.baseline && hurst == other.hurst &&
               maxWavelength == other.maxWavelength && minWavelength == other.minWavelength &&
               seed == other.seed && fade == other.fade;
    }
};

/// Checks roughness options.
/// @throws std::invalid_argument for a non-positive `rmsSlope` or `baseline`, a `hurst` outside
/// (0, 1], wavelengths that do not satisfy 0 < `minWavelength` <= `maxWavelength` or that span more
/// than 40 octaves, or an invalid `fade`.
void ValidateRoughnessOptions(const RoughnessOptions& options);

/// Fine roughness: octaves of Perlin noise from `maxWavelength` to `minWavelength`, amplitudes
/// growing as wavelength^H and scaled to `rmsSlope` over `baseline`; unresolved octaves fade.
class RoughnessLayer final : public FormulaLayer {
  public:
    /// On a sphere each octave is 3D Perlin noise at the 3D point, its lattice turned by a uniform
    /// random rotation.
    /// @throws std::invalid_argument as `ValidateRoughnessOptions`.
    explicit RoughnessLayer(RoughnessOptions options, TerrainBase base = {});

    /// The RMS slope (rise over run) over `baseline` meters with every octave resolved.
    double RmsSlope(double baseline) const;

    /// The octaves' wavelengths in meters, longest first.
    std::vector<double> Wavelengths() const;

    /// The options the layer was made with.
    const RoughnessOptions& Options() const {
        return options_;
    }

  protected:
    float HeightAt(float height, double x, double y, double spacing,
                   const Ground* ground) const override;

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

    RoughnessOptions options_;
    std::vector<Octave> octaves_;
};

}  // namespace terraingen
