#pragma once

#include "terraingen/layers/craters/apply.h"
#include "terraingen/layers/layer.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// Settings for a `RoughnessLayer`.
///
/// Roughness is given as Shepard et al. (2001) do: the RMS slope over a baseline, the RMS height
/// difference between points `baseline` apart divided by `baseline`, and the Hurst exponent, how
/// that RMS height difference grows with the baseline (as baseline^H). Rougher at fine scales is a
/// smaller H.
///
/// Reference: M. K. Shepard, B. A. Campbell, M. H. Bulmer, T. G. Farr, L. R. Gaddis and J. J.
/// Plaut, "The roughness of natural terrain: A planetary and remote sensing perspective", JGR
/// Planets 106(E12):32777-32795, 2001. https://doi.org/10.1029/2000JE001429
struct RoughnessOptions {
    double rmsSlope = 0.15;       ///< RMS slope (rise over run) over `baseline`.
    double baseline = 0.25;       ///< Baseline the RMS slope is given over (m).
    double hurst = 0.8;           ///< Hurst exponent, in (0, 1].
    double maxWavelength = 4.0;   ///< Longest octave (m).
    double minWavelength = 0.01;  ///< Shortest octave (m); octaves halve down to it.
    std::uint32_t seed = 1;
    /// How octaves too fine to resolve fade out, by samples per wavelength.
    ResolutionFade fade{};

    bool operator==(const RoughnessOptions&) const = default;
};

/// @throws std::invalid_argument for a non-positive `rmsSlope` or `baseline`, a `hurst` outside
/// (0, 1], wavelengths that do not satisfy 0 < `minWavelength` <= `maxWavelength` or that span more
/// than 40 octaves, or an invalid `fade`.
void ValidateRoughnessOptions(const RoughnessOptions& options);

/// The structure function of `PerlinNoise2` at a lag of `r` lattice units: the mean squared
/// difference between values `r` apart, over positions, directions and seeds. It grows as 0.7724
/// r^2 for small lags and levels off at 0.18312, twice the variance, from about 4 lattice units;
/// in between it is interpolated from a table computed by Monte Carlo to about 0.1%.
double PerlinStructureFunction(double r);

/// Fine roughness added to the terrain: octaves of Perlin noise halving in wavelength from
/// `maxWavelength` to `minWavelength`, with amplitudes growing as wavelength^H and scaled so that
/// the RMS slope over `baseline` is `rmsSlope` when every octave is resolved. Each octave is
/// rotated and offset so the octaves' lattices do not line up, and fades out at spacings too
/// coarse to resolve it, so coarse levels of detail see the same surface low-passed.
class RoughnessLayer final : public Layer {
  public:
    /// @throws std::invalid_argument for invalid options.
    explicit RoughnessLayer(RoughnessOptions options);

    void Apply(Raster<float>& heights, const LayerContext& context,
               TerrainFeatures& features) const override;

    /// The roughness at (`x`, `y`) added to `height`, to the same bits as a cell centred there;
    /// NaN stays NaN.
    float ApplyAt(float height, double x, double y, const LayerContext& context) const override;

    /// The RMS slope over `baseline` (m) of the roughness with every octave resolved.
    double RmsSlope(double baseline) const;

    /// The octaves' wavelengths (m), longest first.
    std::vector<double> Wavelengths() const;

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
    };

    float Roughness(double x, double y, double spacing) const;

    RoughnessOptions options_;
    std::vector<Octave> octaves_;
};

}  // namespace terraingen
