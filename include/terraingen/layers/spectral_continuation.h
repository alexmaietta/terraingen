// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief `SpectralContinuationLayer`, detail finer than a base DEM that continues the base's
/// measured power-law spectrum (`EstimatePowerLawSpectrum`).
#pragma once

#include "terraingen/analysis/spectrum.h"
#include "terraingen/geometry/geometry.h"
#include "terraingen/geometry/surface.h"
#include "terraingen/layers/fade.h"
#include "terraingen/layers/layer.h"
#include "terraingen/raster/raster.h"

#include <cstdint>
#include <vector>

namespace terraingen {

/// The spectrum a `SpectralContinuationLayer` continues and the wavelengths it adds.
struct SpectralContinuationOptions {
    double pixelSize = 1.0;       ///< Cell size of the base in meters; detail starts at twice it.
    double slope = 3.0;           ///< Spectral slope beta, in [2, 4] (`PowerLawSpectrum::slope`).
    double octaveRms = 0.1;       ///< Meters (`PowerLawSpectrum::octaveRms`).
    double minWavelength = 0.05;  ///< Shortest wavelength added, in meters.
    /// Octaves above the base's Nyquist frequency over which the detail tapers in, from none at
    /// the Nyquist frequency to the whole power law; positive.
    double taperOctaves = 1.0;
    std::uint32_t seed = 1;  ///< Varies the detail.
    /// How wavelengths too fine to resolve fade out, by samples per wavelength.
    ResolutionFade fade{};

    bool operator==(const SpectralContinuationOptions& other) const {
        return pixelSize == other.pixelSize && slope == other.slope &&
               octaveRms == other.octaveRms && minWavelength == other.minWavelength &&
               taperOctaves == other.taperOctaves && seed == other.seed && fade == other.fade;
    }
    bool operator!=(const SpectralContinuationOptions& other) const {
        return !(*this == other);
    }
};

/// Checks spectral continuation options.
/// @throws std::invalid_argument for a `pixelSize`, `minWavelength` or `taperOctaves` that is not
/// positive and finite, a `slope` outside [2, 4], a negative or non-finite `octaveRms`, or an
/// invalid `fade`.
void ValidateSpectralContinuationOptions(const SpectralContinuationOptions& options);

/// Detail finer than a base DEM that continues its power-law spectrum with random phases (Saupe
/// 1988), as sparse Gabor noise (Lagae et al. 2009, https://doi.org/10.1145/1531326.1531360) so
/// grids, tiles and points agree; it adds nothing at wavelengths the base resolves.
class SpectralContinuationLayer final : public Layer {
  public:
    /// @throws std::invalid_argument as `ValidateSpectralContinuationOptions`.
    explicit SpectralContinuationLayer(SpectralContinuationOptions options);

    /// The options the layer was made with.
    const SpectralContinuationOptions& Options() const {
        return options_;
    }

    /// The layer with its seed `ReseedWord(seed, 0)`.
    LayerPtr Reseeded(std::uint64_t seed) const override;

  protected:
    void ApplySamples(SampleBlock& samples, const LayerContext& context) const override;

  private:
    /// One octave band of impulses.
    struct Band {
        double frequency = 0.0;  ///< Lowest frequency, in cycles per meter.
        double bandwidth = 0.0;  ///< Gaussian envelope parameter a, in 1 / m.
        double reach = 0.0;      ///< Impulse radius and lattice cell side, in meters.
        std::uint32_t seed = 0;  ///< Seed of the band's lattice streams.
    };

    struct ImpulseTable;

    /// The target power spectral density at `frequency` cycles per meter, in m^4.
    double TargetPsd(double frequency) const;

    /// The impulses of `band` over the lattice cells that reach the box [`minX`, `maxX`] x
    /// [`minY`, `maxY`] meters, faded for `spacing` meters.
    ImpulseTable Table(const Band& band, double minX, double minY, double maxX, double maxY,
                       double spacing) const;
    /// The detail at `p` meters from the impulses of `tables`.
    static double DetailAt(const std::vector<ImpulseTable>& tables, const Vec2d& p);
    /// Adds the detail at `positions` (meters) to `heights` at `spacing` meters.
    void AddDetail(const std::vector<Vec2d>& positions, const std::vector<float*>& heights,
                   double spacing) const;

    SpectralContinuationOptions options_;
    double coefficient_ = 0.0;  ///< C in m^(4 - beta), P(f) = C f^-beta.
    std::vector<Band> bands_;
};

}  // namespace terraingen
